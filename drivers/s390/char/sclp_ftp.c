// SPDX-License-Identifier: GPL-2.0
/*
 *    SCLP Event Type (ET) 7 - Diagnostic Test FTP Services, useable on LPAR
 *
 *    Copyright IBM Corp. 2013
 *    Author(s): Ralf Hoppe (rhoppe@de.ibm.com)
 *
 */

#define KMSG_COMPONENT "hmcdrv"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/wait.h>
#include <linux/string.h>
#include <linux/jiffies.h>
#include <asm/sysinfo.h>
#include <asm/ebcdic.h>

#include "sclp.h"
#include "sclp_diag.h"
#include "sclp_ftp.h"

static DECLARE_COMPLETION(sclp_ftp_rx_complete);
static u8 sclp_ftp_ldflg;
static u64 sclp_ftp_fsize;
static u64 sclp_ftp_length;

/**
 * sclp_ftp_txcb() - Diagnostic Test FTP services SCLP command callback
 * @req: sclp request
 * @data: pointer to struct completion
 */
static void sclp_ftp_txcb(struct sclp_req *req, void *data)
{
	struct completion *completion = data;

#ifdef DEBUG
	pr_debug("SCLP (ET7) TX-IRQ, SCCB @ 0x%p: %*phN\n",
		 req->sccb, 24, req->sccb);
#endif
	complete(completion);
}

/**
 * sclp_ftp_rxcb() - Diagnostic Test FTP services receiver event callback
 * @evbuf: pointer to Diagnostic Test (ET7) event buffer
 */
static void sclp_ftp_rxcb(struct evbuf_header *evbuf)
{
	struct sclp_diag_evbuf *diag = (struct sclp_diag_evbuf *) evbuf;

	/*
	 * Check for Diagnostic Test FTP Service
	 */
	if (evbuf->type != EVTYP_DIAG_TEST ||
	    diag->route != SCLP_DIAG_FTP_ROUTE ||
	    diag->mdd.ftp.pcx != SCLP_DIAG_FTP_XPCX ||
	    evbuf->length < SCLP_DIAG_FTP_EVBUF_LEN)
		return;

#ifdef DEBUG
	pr_debug("SCLP (ET7) RX-IRQ, Event @ 0x%p: %*phN\n",
		 evbuf, 24, evbuf);
#endif

	/*
	 * Because the event buffer is located in a page which is owned
	 * by the SCLP core, all data of interest must be copied. The
	 * error indication is in 'sclp_ftp_ldflg'
	 */
	sclp_ftp_ldflg = diag->mdd.ftp.ldflg;
	sclp_ftp_fsize = diag->mdd.ftp.fsize;
	sclp_ftp_length = diag->mdd.ftp.length;

	complete(&sclp_ftp_rx_complete);
}

/**
 * sclp_ftp_et7() - start a Diagnostic Test FTP Service SCLP request
 * @ftp: pointer to FTP descriptor
 *
 * Return: 0 on success, else a (negative) error code
 */
static int sclp_ftp_et7(const struct hmcdrv_ftp_cmdspec *ftp)
{
	struct completion completion;
	struct sclp_diag_sccb *sccb;
	struct sclp_req *req;
	size_t len;
	int rc;

	req = kzalloc(sizeof(*req), GFP_KERNEL);
	sccb = (void *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!req || !sccb) {
		rc = -ENOMEM;
		goto out_free;
	}

	sccb->hdr.length = SCLP_DIAG_FTP_EVBUF_LEN +
		sizeof(struct sccb_header);
	sccb->evbuf.hdr.type = EVTYP_DIAG_TEST;
	sccb->evbuf.hdr.length = SCLP_DIAG_FTP_EVBUF_LEN;
	sccb->evbuf.hdr.flags = 0; /* clear processed-buffer */
	sccb->evbuf.route = SCLP_DIAG_FTP_ROUTE;
	sccb->evbuf.mdd.ftp.pcx = SCLP_DIAG_FTP_XPCX;
	sccb->evbuf.mdd.ftp.srcflg = 0;
	sccb->evbuf.mdd.ftp.pgsize = 0;
	sccb->evbuf.mdd.ftp.asce = _ASCE_REAL_SPACE;
	sccb->evbuf.mdd.ftp.ldflg = SCLP_DIAG_FTP_LDFAIL;
	sccb->evbuf.mdd.ftp.fsize = 0;
	sccb->evbuf.mdd.ftp.cmd = ftp->id;
	sccb->evbuf.mdd.ftp.offset = ftp->ofs;
	sccb->evbuf.mdd.ftp.length = ftp->len;
	sccb->evbuf.mdd.ftp.bufaddr = virt_to_phys(ftp->buf);

	len = strlcpy(sccb->evbuf.mdd.ftp.fident, ftp->fname,
		      HMCDRV_FTP_FIDENT_MAX);
	if (len >= HMCDRV_FTP_FIDENT_MAX) {
		rc = -EINVAL;
		goto out_free;
	}

	req->command = SCLP_CMDW_WRITE_EVENT_DATA;
	req->sccb = sccb;
	req->status = SCLP_REQ_FILLED;
	req->callback = sclp_ftp_txcb;
	req->callback_data = &completion;

	init_completion(&completion);

	rc = sclp_add_request(req);
	if (rc)
		goto out_free;

	/* Wait for end of ftp sclp command. */
	wait_for_completion(&completion);

#ifdef DEBUG
	pr_debug("status of SCLP (ET7) request is 0x%04x (0x%02x)\n",
		 sccb->hdr.response_code, sccb->evbuf.hdr.flags);
#endif

	/*
	 * Check if sclp accepted the request. The data transfer runs
	 * asynchronously and the completion is indicated with an
	 * sclp ET7 event.
	 */
	if (req->status != SCLP_REQ_DONE ||
	    (sccb->evbuf.hdr.flags & 0x80) == 0 || /* processed-buffer */
	    (sccb->hdr.response_code & 0xffU) != 0x20U) {
		rc = -EIO;
	}

out_free:
	free_page((unsigned long) sccb);
	kfree(req);
	return rc;
}

/**
 * sclp_ftp_cmd() - executes a HMC related SCLP Diagnose (ET7) FTP command
 * @ftp: pointer to FTP command specification
 * @fsize: return of file size (or NULL if undesirable)
 *
 * Attention: Notice that this function is not reentrant - so the caller
 * must ensure locking.
 *
 * Return: number of bytes read/written or a (negative) error code
 */
ssize_t sclp_ftp_cmd(const struct hmcdrv_ftp_cmdspec *ftp, size_t *fsize)
{
	ssize_t len;
#ifdef DEBUG
	unsigned long start_jiffies;

	pr_debug("starting SCLP (ET7), cmd %d for '%s' at %lld with %zd bytes\n",
		 ftp->id, ftp->fname, (long long) ftp->ofs, ftp->len);
	start_jiffies = jiffies;
#endif

	init_completion(&sclp_ftp_rx_complete);

	/* Start ftp sclp command. */
	len = sclp_ftp_et7(ftp);
	if (len)
		goto out_unlock;

	/*
	 * There is no way to cancel the sclp ET7 request, the code
	 * needs to wait unconditionally until the transfer is complete.
	 */
	wait_for_completion(&sclp_ftp_rx_complete);

#ifdef DEBUG
	pr_debug("completed SCLP (ET7) request after %lu ms (all)\n",
		 (jiffies - start_jiffies) * 1000 / HZ);
	pr_debug("return code of SCLP (ET7) FTP Service is 0x%02x, with %lld/%lld bytes\n",
		 sclp_ftp_ldflg, sclp_ftp_length, sclp_ftp_fsize);
#endif

	switch (sclp_ftp_ldflg) {
	case SCLP_DIAG_FTP_OK:
		len = sclp_ftp_length;
		if (fsize)
			*fsize = sclp_ftp_fsize;
		break;
	case SCLP_DIAG_FTP_LDNPERM:
		len = -EPERM;
		break;
	case SCLP_DIAG_FTP_LDRUNS:
		len = -EBUSY;
		break;
	case SCLP_DIAG_FTP_LDFAIL:
		len = -ENOENT;
		break;
	default:
		len = -EIO;
		break;
	}

out_unlock:
	return len;
}

/*
 * ET7 event listener
 */
static struct sclp_register sclp_ftp_event = {
	.send_mask = EVTYP_DIAG_TEST_MASK,    /* want tx events */
	.receive_mask = EVTYP_DIAG_TEST_MASK, /* want rx events */
	.receiver_fn = sclp_ftp_rxcb,	      /* async callback (rx) */
	.state_change_fn = NULL,
};

/**
 * sclp_ftp_startup() - startup of FTP services, when running on LPAR
 */
int sclp_ftp_startup(void)
{
#ifdef DEBUG
	unsigned long info;
#endif
	int rc;

	rc = sclp_register(&sclp_ftp_event);
	if (rc)
		return rc;

#ifdef DEBUG
	info = get_zeroed_page(GFP_KERNEL);

	if (info != 0) {
		struct sysinfo_2_2_2 *info222 = (struct sysinfo_2_2_2 *)info;

		if (!stsi(info222, 2, 2, 2)) { /* get SYSIB 2.2.2 */
			info222->name[sizeof(info222->name) - 1] = '\0';
			EBCASC_500(info222->name, sizeof(info222->name) - 1);
			pr_debug("SCLP (ET7) FTP Service working on LPAR %u (%s)\n",
				 info222->lpar_number, info222->name);
		}

		free_page(info);
	}
#endif	/* DEBUG */
	return 0;
}

/**
 * sclp_ftp_shutdown() - shutdown of FTP services, when running on LPAR
 */
void sclp_ftp_shutdown(void)
{
	sclp_unregister(&sclp_ftp_event);
}
