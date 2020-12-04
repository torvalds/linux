/* SPDX-License-Identifier: GPL-2.0 */
/** @file dbus.c
 *
 * Hides details of USB / SDIO / SPI interfaces and OS details. It is intended to shield details and
 * provide the caller with one common bus interface for all dongle devices. In practice, it is only
 * used for USB interfaces. DBUS is not a protocol, but an abstraction layer.
 *
 * Copyright (C) 1999-2019, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: dbus.c 553311 2015-04-29 10:23:08Z $
 */


#include "osl.h"
#include "dbus.h"
#include <bcmutils.h>

#if defined(BCM_DNGL_EMBEDIMAGE)
#include <bcmsrom_fmt.h>
#include <trxhdr.h>
#include <usbrdl.h>
#include <bcmendian.h>
#include <zutil.h>
#include <sbpcmcia.h>
#include <bcmnvram.h>
#include <bcmdevs.h>
#elif defined(BCM_REQUEST_FW)
#include <bcmsrom_fmt.h>
#include <trxhdr.h>
#include <usbrdl.h>
#include <bcmendian.h>
#include <sbpcmcia.h>
#include <bcmnvram.h>
#include <bcmdevs.h>
#endif /* #if defined(BCM_DNGL_EMBEDIMAGE) */
#if defined(EHCI_FASTPATH_TX) || defined(EHCI_FASTPATH_RX)
#include <linux/usb.h>
#endif /* EHCI_FASTPATH_TX || EHCI_FASTPATH_RX */


#if defined(BCM_DNGL_EMBEDIMAGE)
/* zlib file format field ids etc from gzio.c */
#define Z_DEFLATED     8
#define ASCII_FLAG     0x01 /* bit 0 set: file probably ascii text */
#define HEAD_CRC       0x02 /* bit 1 set: header CRC present */
#define EXTRA_FIELD    0x04 /* bit 2 set: extra field present */
#define ORIG_NAME      0x08 /* bit 3 set: original file name present */
#define COMMENT        0x10 /* bit 4 set: file comment present */
#define RESERVED       0xE0 /* bits 5..7: reserved */

#ifdef WL_FW_DECOMP
#define UNZIP_ENAB(info)  1
#else
#define UNZIP_ENAB(info)  0

#ifdef inflateInit2
#undef inflateInit2
#define inflateInit2(a, b)  Z_ERRNO
#endif
#define inflate(a, b)       Z_STREAM_ERROR
#define inflateEnd(a)       do {} while (0)
#define crc32(a, b, c)      -1
#define free(a)             do {} while (0)
#endif /* WL_FW_DECOMP */

#elif defined(BCM_REQUEST_FW)
#ifndef VARS_MAX
#define VARS_MAX            8192
#endif
#endif /* #if defined(BCM_DNGL_EMBEDIMAGE) */

#ifdef DBUS_USB_LOOPBACK
extern bool is_loopback_pkt(void *buf);
extern int matches_loopback_pkt(void *buf);
#endif

/** General info for all BUS types */
typedef struct dbus_irbq {
	dbus_irb_t *head;
	dbus_irb_t *tail;
	int cnt;
} dbus_irbq_t;

/**
 * This private structure dbus_info_t is also declared in dbus_usb_linux.c.
 * All the fields must be consistent in both declarations.
 */
typedef struct dbus_info {
	dbus_pub_t   pub; /* MUST BE FIRST */

	void        *cbarg;
	dbus_callbacks_t *cbs; /* callbacks to higher level, e.g. dhd_linux.c */
	void        *bus_info;
	dbus_intf_t *drvintf;  /* callbacks to lower level, e.g. dbus_usb.c or dbus_usb_linux.c */
	uint8       *fw;
	int         fwlen;
	uint32      errmask;
	int         rx_low_watermark;  /* avoid rx overflow by filling rx with free IRBs */
	int         tx_low_watermark;
	bool        txoff;
	bool        txoverride;   /* flow control related */
	bool        rxoff;
	bool        tx_timer_ticking;


	dbus_irbq_t *rx_q;
	dbus_irbq_t *tx_q;

#ifdef EHCI_FASTPATH_RX
	atomic_t    rx_outstanding;
#endif
	uint8        *nvram;
	int          nvram_len;
	uint8        *image;  /* buffer for combine fw and nvram */
	int          image_len;
	uint8        *orig_fw;
	int          origfw_len;
	int          decomp_memsize;
	dbus_extdl_t extdl;
	int          nvram_nontxt;
#if defined(BCM_REQUEST_FW)
	void         *firmware;
	void         *nvfile;
#endif
} dbus_info_t;

struct exec_parms {
	union {
		/* Can consolidate same params, if need be, but this shows
		 * group of parameters per function
		 */
		struct {
			dbus_irbq_t  *q;
			dbus_irb_t   *b;
		} qenq;

		struct {
			dbus_irbq_t  *q;
		} qdeq;
	};
};

#define EXEC_RXLOCK(info, fn, a) \
	info->drvintf->exec_rxlock(dbus_info->bus_info, ((exec_cb_t)fn), ((struct exec_parms *) a))

#define EXEC_TXLOCK(info, fn, a) \
	info->drvintf->exec_txlock(dbus_info->bus_info, ((exec_cb_t)fn), ((struct exec_parms *) a))

/*
 * Callbacks common for all BUS
 */
static void dbus_if_send_irb_timeout(void *handle, dbus_irb_tx_t *txirb);
static void dbus_if_send_irb_complete(void *handle, dbus_irb_tx_t *txirb, int status);
static void dbus_if_recv_irb_complete(void *handle, dbus_irb_rx_t *rxirb, int status);
static void dbus_if_errhandler(void *handle, int err);
static void dbus_if_ctl_complete(void *handle, int type, int status);
static void dbus_if_state_change(void *handle, int state);
static void *dbus_if_pktget(void *handle, uint len, bool send);
static void dbus_if_pktfree(void *handle, void *p, bool send);
static struct dbus_irb *dbus_if_getirb(void *cbarg, bool send);
static void dbus_if_rxerr_indicate(void *handle, bool on);

/** functions in this file that are called by lower DBUS levels, e.g. dbus_usb.c */
static dbus_intf_callbacks_t dbus_intf_cbs = {
	dbus_if_send_irb_timeout,
	dbus_if_send_irb_complete,
	dbus_if_recv_irb_complete,
	dbus_if_errhandler,
	dbus_if_ctl_complete,
	dbus_if_state_change,
	NULL,			/* isr */
	NULL,			/* dpc */
	NULL,			/* watchdog */
	dbus_if_pktget,
	dbus_if_pktfree,
	dbus_if_getirb,
	dbus_if_rxerr_indicate
};

/*
 * Need global for probe() and disconnect() since
 * attach() is not called at probe and detach()
 * can be called inside disconnect()
 */
static dbus_intf_t     *g_busintf = NULL;
static probe_cb_t      probe_cb = NULL;
static disconnect_cb_t disconnect_cb = NULL;
static void            *probe_arg = NULL;
static void            *disc_arg = NULL;

#if defined(BCM_REQUEST_FW)
int8 *nonfwnvram = NULL; /* stand-alone multi-nvram given with driver load */
int nonfwnvramlen = 0;
#endif /* #if defined(BCM_REQUEST_FW) */

static void* q_enq(dbus_irbq_t *q, dbus_irb_t *b);
static void* q_enq_exec(struct exec_parms *args);
static dbus_irb_t*q_deq(dbus_irbq_t *q);
static void* q_deq_exec(struct exec_parms *args);
static int   dbus_tx_timer_init(dbus_info_t *dbus_info);
static int   dbus_tx_timer_start(dbus_info_t *dbus_info, uint timeout);
static int   dbus_tx_timer_stop(dbus_info_t *dbus_info);
static int   dbus_irbq_init(dbus_info_t *dbus_info, dbus_irbq_t *q, int nq, int size_irb);
static int   dbus_irbq_deinit(dbus_info_t *dbus_info, dbus_irbq_t *q, int size_irb);
static int   dbus_rxirbs_fill(dbus_info_t *dbus_info);
static int   dbus_send_irb(dbus_pub_t *pub, uint8 *buf, int len, void *pkt, void *info);
static void  dbus_disconnect(void *handle);
static void *dbus_probe(void *arg, const char *desc, uint32 bustype, uint32 hdrlen);

#if (defined(BCM_DNGL_EMBEDIMAGE) || defined(BCM_REQUEST_FW))
#if defined(BCM_REQUEST_FW)
extern char * dngl_firmware;
extern unsigned int dngl_fwlen;
#endif  /* #if defined(BCM_REQUEST_FW) */
static int dbus_get_nvram(dbus_info_t *dbus_info);
static int dbus_jumbo_nvram(dbus_info_t *dbus_info);
static int dbus_otp(dbus_info_t *dbus_info, uint16 *boardtype, uint16 *boardrev);
static int dbus_select_nvram(dbus_info_t *dbus_info, int8 *jumbonvram, int jumbolen,
uint16 boardtype, uint16 boardrev, int8 **nvram, int *nvram_len);
#ifndef BCM_REQUEST_FW
static int dbus_zlib_decomp(dbus_info_t *dbus_info);
extern void *dbus_zlib_calloc(int num, int size);
extern void dbus_zlib_free(void *ptr);
#endif
#endif /* defined(BCM_DNGL_EMBEDIMAGE) || defined(BCM_REQUEST_FW) */

/* function */
void
dbus_flowctrl_tx(void *dbi, bool on)
{
	dbus_info_t *dbus_info = dbi;

	if (dbus_info == NULL)
		return;

	DBUSTRACE(("%s on %d\n", __FUNCTION__, on));

	if (dbus_info->txoff == on)
		return;

	dbus_info->txoff = on;

	if (dbus_info->cbs && dbus_info->cbs->txflowcontrol)
		dbus_info->cbs->txflowcontrol(dbus_info->cbarg, on);
}

/**
 * if lower level DBUS signaled a rx error, more free rx IRBs should be allocated or flow control
 * should kick in to make more free rx IRBs available.
 */
static void
dbus_if_rxerr_indicate(void *handle, bool on)
{
	dbus_info_t *dbus_info = (dbus_info_t *) handle;

	DBUSTRACE(("%s, on %d\n", __FUNCTION__, on));

	if (dbus_info == NULL)
		return;

	if (dbus_info->txoverride == on)
		return;

	dbus_info->txoverride = on;	/* flow control */

	if (!on)
		dbus_rxirbs_fill(dbus_info);

}

/** q_enq()/q_deq() are executed with protection via exec_rxlock()/exec_txlock() */
static void*
q_enq(dbus_irbq_t *q, dbus_irb_t *b)
{
	ASSERT(q->tail != b);
	ASSERT(b->next == NULL);
	b->next = NULL;
	if (q->tail) {
		q->tail->next = b;
		q->tail = b;
	} else
		q->head = q->tail = b;

	q->cnt++;

	return b;
}

static void*
q_enq_exec(struct exec_parms *args)
{
	return q_enq(args->qenq.q, args->qenq.b);
}

static dbus_irb_t*
q_deq(dbus_irbq_t *q)
{
	dbus_irb_t *b;

	b = q->head;
	if (b) {
		q->head = q->head->next;
		b->next = NULL;

		if (q->head == NULL)
			q->tail = q->head;

		q->cnt--;
	}
	return b;
}

static void*
q_deq_exec(struct exec_parms *args)
{
	return q_deq(args->qdeq.q);
}

/**
 * called during attach phase. Status @ Dec 2012: this function does nothing since for all of the
 * lower DBUS levels dbus_info->drvintf->tx_timer_init is NULL.
 */
static int
dbus_tx_timer_init(dbus_info_t *dbus_info)
{
	if (dbus_info && dbus_info->drvintf && dbus_info->drvintf->tx_timer_init)
		return dbus_info->drvintf->tx_timer_init(dbus_info->bus_info);
	else
		return DBUS_ERR;
}

static int
dbus_tx_timer_start(dbus_info_t *dbus_info, uint timeout)
{
	if (dbus_info == NULL)
		return DBUS_ERR;

	if (dbus_info->tx_timer_ticking)
		return DBUS_OK;

	if (dbus_info->drvintf && dbus_info->drvintf->tx_timer_start) {
		if (dbus_info->drvintf->tx_timer_start(dbus_info->bus_info, timeout) == DBUS_OK) {
			dbus_info->tx_timer_ticking = TRUE;
			return DBUS_OK;
		}
	}

	return DBUS_ERR;
}

static int
dbus_tx_timer_stop(dbus_info_t *dbus_info)
{
	if (dbus_info == NULL)
		return DBUS_ERR;

	if (!dbus_info->tx_timer_ticking)
		return DBUS_OK;

	if (dbus_info->drvintf && dbus_info->drvintf->tx_timer_stop) {
		if (dbus_info->drvintf->tx_timer_stop(dbus_info->bus_info) == DBUS_OK) {
			dbus_info->tx_timer_ticking = FALSE;
			return DBUS_OK;
		}
	}

	return DBUS_ERR;
}

/** called during attach phase. */
static int
dbus_irbq_init(dbus_info_t *dbus_info, dbus_irbq_t *q, int nq, int size_irb)
{
	int i;
	dbus_irb_t *irb;

	ASSERT(q);
	ASSERT(dbus_info);

	for (i = 0; i < nq; i++) {
		/* MALLOC dbus_irb_tx or dbus_irb_rx, but cast to simple dbus_irb_t linkedlist */
		irb = (dbus_irb_t *) MALLOC(dbus_info->pub.osh, size_irb);
		if (irb == NULL) {
			ASSERT(irb);
			return DBUS_ERR;
		}
		bzero(irb, size_irb);

		/* q_enq() does not need to go through EXEC_xxLOCK() during init() */
		q_enq(q, irb);
	}

	return DBUS_OK;
}

/** called during detach phase or when attach failed */
static int
dbus_irbq_deinit(dbus_info_t *dbus_info, dbus_irbq_t *q, int size_irb)
{
	dbus_irb_t *irb;

	ASSERT(q);
	ASSERT(dbus_info);

	/* q_deq() does not need to go through EXEC_xxLOCK()
	 * during deinit(); all callbacks are stopped by this time
	 */
	while ((irb = q_deq(q)) != NULL) {
		MFREE(dbus_info->pub.osh, irb, size_irb);
	}

	if (q->cnt)
		DBUSERR(("deinit: q->cnt=%d > 0\n", q->cnt));
	return DBUS_OK;
}

/** multiple code paths require the rx queue to be filled with more free IRBs */
static int
dbus_rxirbs_fill(dbus_info_t *dbus_info)
{
	int err = DBUS_OK;

#ifdef EHCI_FASTPATH_RX
	while (atomic_read(&dbus_info->rx_outstanding) < 100)
	{
#if defined(BCM_RPC_NOCOPY) || defined(BCM_RPC_RXNOCOPY)
		/* NOCOPY force new packet allocation */
		optimize_submit_rx_request(&dbus_info->pub, 1, NULL, NULL);
#else
		/* Copy mode - allocate own buffer to be reused */
		void *buf = MALLOC(dbus_info->pub.osh, 4000); /* usbos_info->rxbuf_len */
		optimize_submit_rx_request(&dbus_info->pub, 1, NULL, buf);
#endif /* BCM_RPC_NOCOPY || BCM_RPC_RXNOCOPY */
		atomic_inc(&dbus_info->rx_outstanding);
	}
#else /* EHCI_FASTPATH_RX */

	dbus_irb_rx_t *rxirb;
	struct exec_parms args;

	ASSERT(dbus_info);
	if (dbus_info->pub.busstate != DBUS_STATE_UP) {
		DBUSERR(("dbus_rxirbs_fill: DBUS not up \n"));
		return DBUS_ERR;
	} else if (!dbus_info->drvintf || (dbus_info->drvintf->recv_irb == NULL)) {
		/* Lower edge bus interface does not support recv_irb().
		 * No need to pre-submit IRBs in this case.
		 */
		return DBUS_ERR;
	}

	/* The dongle recv callback is freerunning without lock. So multiple callbacks(and this
	 *  refill) can run in parallel. While the rxoff condition is triggered outside,
	 *  below while loop has to check and abort posting more to avoid RPC rxq overflow.
	 */
	args.qdeq.q = dbus_info->rx_q;
	while ((!dbus_info->rxoff) &&
	       (rxirb = (EXEC_RXLOCK(dbus_info, q_deq_exec, &args))) != NULL) {
		err = dbus_info->drvintf->recv_irb(dbus_info->bus_info, rxirb);
		if (err == DBUS_ERR_RXDROP || err == DBUS_ERR_RXFAIL) {
			/* Add the the free rxirb back to the queue
			 * and wait till later
			 */
			bzero(rxirb, sizeof(dbus_irb_rx_t));
			args.qenq.q = dbus_info->rx_q;
			args.qenq.b = (dbus_irb_t *) rxirb;
			EXEC_RXLOCK(dbus_info, q_enq_exec, &args);
			break;
		} else if (err != DBUS_OK) {
			int i = 0;
			while (i++ < 100) {
				DBUSERR(("%s :: memory leak for rxirb note?\n", __FUNCTION__));
			}
		}
	}
#endif /* EHCI_FASTPATH_RX */
	return err;
} /* dbus_rxirbs_fill */

/** called when the DBUS interface state changed. */
void
dbus_flowctrl_rx(dbus_pub_t *pub, bool on)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;

	if (dbus_info == NULL)
		return;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dbus_info->rxoff == on)
		return;

	dbus_info->rxoff = on;

	if (dbus_info->pub.busstate == DBUS_STATE_UP) {
		if (!on) {
			/* post more irbs, resume rx if necessary */
			dbus_rxirbs_fill(dbus_info);
			if (dbus_info && dbus_info->drvintf->recv_resume) {
				dbus_info->drvintf->recv_resume(dbus_info->bus_info);
			}
		} else {
			/* ??? cancell posted irbs first */

			if (dbus_info && dbus_info->drvintf->recv_stop) {
				dbus_info->drvintf->recv_stop(dbus_info->bus_info);
			}
		}
	}
}

/**
 * Several code paths in this file want to send a buffer to the dongle. This function handles both
 * sending of a buffer or a pkt.
 */
static int
dbus_send_irb(dbus_pub_t *pub, uint8 *buf, int len, void *pkt, void *info)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;
	int err = DBUS_OK;
#ifndef EHCI_FASTPATH_TX
	dbus_irb_tx_t *txirb = NULL;
	int txirb_pending;
	struct exec_parms args;
#endif /* EHCI_FASTPATH_TX */

	if (dbus_info == NULL)
		return DBUS_ERR;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dbus_info->pub.busstate == DBUS_STATE_UP ||
		dbus_info->pub.busstate == DBUS_STATE_SLEEP) {
#ifdef EHCI_FASTPATH_TX
		struct ehci_qtd *qtd;
		int token = EHCI_QTD_SET_CERR(3);
		int len;

		ASSERT(buf == NULL); /* Not handled */
		ASSERT(pkt != NULL);

		qtd = optimize_ehci_qtd_alloc(GFP_KERNEL);

		if (qtd == NULL)
			return DBUS_ERR;

		len = PKTLEN(pub->osh, pkt);

		len = ROUNDUP(len, sizeof(uint32));


		optimize_qtd_fill_with_rpc(pub, 0, qtd, pkt, token, len);
		err = optimize_submit_async(qtd, 0);

		if (err) {
			optimize_ehci_qtd_free(qtd);
			err = DBUS_ERR_TXDROP;
		}

#else
		args.qdeq.q = dbus_info->tx_q;
		if (dbus_info->drvintf)
			txirb = EXEC_TXLOCK(dbus_info, q_deq_exec, &args);

		if (txirb == NULL) {
			DBUSERR(("Out of tx dbus_bufs\n"));
			return DBUS_ERR;
		}

		if (pkt != NULL) {
			txirb->pkt = pkt;
			txirb->buf = NULL;
			txirb->len = 0;
		} else if (buf != NULL) {
			txirb->pkt = NULL;
			txirb->buf = buf;
			txirb->len = len;
		} else {
			ASSERT(0); /* Should not happen */
		}
		txirb->info = info;
		txirb->arg = NULL;
		txirb->retry_count = 0;

		if (dbus_info->drvintf && dbus_info->drvintf->send_irb) {
			/* call lower DBUS level send_irb function */
			err = dbus_info->drvintf->send_irb(dbus_info->bus_info, txirb);
			if (err == DBUS_ERR_TXDROP) {
				/* tx fail and no completion routine to clean up, reclaim irb NOW */
				DBUSERR(("%s: send_irb failed, status = %d\n", __FUNCTION__, err));
				bzero(txirb, sizeof(dbus_irb_tx_t));
				args.qenq.q = dbus_info->tx_q;
				args.qenq.b = (dbus_irb_t *) txirb;
				EXEC_TXLOCK(dbus_info, q_enq_exec, &args);
			} else {
				dbus_tx_timer_start(dbus_info, DBUS_TX_TIMEOUT_INTERVAL);
				txirb_pending = dbus_info->pub.ntxq - dbus_info->tx_q->cnt;
				if (txirb_pending > (dbus_info->tx_low_watermark * 3)) {
					dbus_flowctrl_tx(dbus_info, TRUE);
				}
			}
		}
#endif /* EHCI_FASTPATH_TX */
	} else {
		err = DBUS_ERR_TXFAIL;
		DBUSTRACE(("%s: bus down, send_irb failed\n", __FUNCTION__));
	}

	return err;
} /* dbus_send_irb */

#if (defined(BCM_DNGL_EMBEDIMAGE) || defined(BCM_REQUEST_FW))

/**
 * Before downloading a firmware image into the dongle, the validity of the image must be checked.
 */
static int
check_file(osl_t *osh, unsigned char *headers)
{
	struct trx_header *trx;
	int actual_len = -1;

	/* Extract trx header */
	trx = (struct trx_header *)headers;
	if (ltoh32(trx->magic) != TRX_MAGIC) {
		printf("Error: trx bad hdr %x\n", ltoh32(trx->magic));
		return -1;
	}

	headers += SIZEOF_TRX(trx);

	/* TRX V1: get firmware len */
	/* TRX V2: get firmware len and DSG/CFG lengths */
	if (ltoh32(trx->flag_version) & TRX_UNCOMP_IMAGE) {
		actual_len = ltoh32(trx->offsets[TRX_OFFSETS_DLFWLEN_IDX]) +
		                     SIZEOF_TRX(trx);
#ifdef BCMTRXV2
		if (ISTRX_V2(trx)) {
			actual_len += ltoh32(trx->offsets[TRX_OFFSETS_DSG_LEN_IDX]) +
				ltoh32(trx->offsets[TRX_OFFSETS_CFG_LEN_IDX]);
		}
#endif
		return actual_len;
	}  else {
		printf("compressed image\n");
	}

	return -1;
}

/**
 * It is easy for the user to pass one jumbo nvram file to the driver than a set of smaller files.
 * The 'jumbo nvram' file format is essentially a set of nvram files. Before commencing firmware
 * download, the dongle needs to be probed so that the correct nvram contents within the jumbo nvram
 * file is selected.
 */
static int
dbus_jumbo_nvram(dbus_info_t *dbus_info)
{
	int8 *nvram = NULL;
	int nvram_len = 0;
	int ret = DBUS_OK;
	uint16 boardrev = 0xFFFF;
	uint16 boardtype = 0xFFFF;

	/* read the otp for boardrev & boardtype
	* if boardtype/rev are present in otp
	* select nvram data for that boardtype/rev
	*/
	dbus_otp(dbus_info, &boardtype, &boardrev);

	ret = dbus_select_nvram(dbus_info, dbus_info->extdl.vars, dbus_info->extdl.varslen,
		boardtype, boardrev, &nvram, &nvram_len);

	if (ret == DBUS_JUMBO_BAD_FORMAT)
			return DBUS_ERR_NVRAM;
	else if (ret == DBUS_JUMBO_NOMATCH &&
		(boardtype != 0xFFFF || boardrev  != 0xFFFF)) {
			DBUSERR(("No matching NVRAM for boardtype 0x%02x boardrev 0x%02x\n",
				boardtype, boardrev));
			return DBUS_ERR_NVRAM;
	}
	dbus_info->nvram = nvram;
	dbus_info->nvram_len =  nvram_len;

	return DBUS_OK;
}

/** before commencing fw download, the correct NVRAM image to download has to be picked */
static int
dbus_get_nvram(dbus_info_t *dbus_info)
{
	int len, i;
	struct trx_header *hdr;
	int	actual_fwlen;
	uint32 img_offset = 0;

	dbus_info->nvram_len = 0;
	if (dbus_info->extdl.varslen) {
		if (DBUS_OK != dbus_jumbo_nvram(dbus_info))
			return DBUS_ERR_NVRAM;
		DBUSERR(("NVRAM %d bytes downloaded\n", dbus_info->nvram_len));
	}
#if defined(BCM_REQUEST_FW)
	else if (nonfwnvram) {
		dbus_info->nvram = nonfwnvram;
		dbus_info->nvram_len = nonfwnvramlen;
		DBUSERR(("NVRAM %d bytes downloaded\n", dbus_info->nvram_len));
	}
#endif
	if (dbus_info->nvram) {
		uint8 nvram_words_pad = 0;
		/* Validate the format/length etc of the file */
		if ((actual_fwlen = check_file(dbus_info->pub.osh, dbus_info->fw)) <= 0) {
			DBUSERR(("%s: bad firmware format!\n", __FUNCTION__));
			return DBUS_ERR_NVRAM;
		}

		if (!dbus_info->nvram_nontxt) {
			/* host supplied nvram could be in .txt format
			* with all the comments etc...
			*/
			dbus_info->nvram_len = process_nvram_vars(dbus_info->nvram,
				dbus_info->nvram_len);
		}
		if (dbus_info->nvram_len % 4)
			nvram_words_pad = 4 - dbus_info->nvram_len % 4;

		len = actual_fwlen + dbus_info->nvram_len + nvram_words_pad;
		dbus_info->image = MALLOC(dbus_info->pub.osh, len);
		dbus_info->image_len = len;
		if (dbus_info->image == NULL) {
			DBUSERR(("%s: malloc failed!\n", __FUNCTION__));
			return DBUS_ERR_NVRAM;
		}
		hdr = (struct trx_header *)dbus_info->fw;
		/* Step1: Copy trx header + firmwre */
		img_offset = SIZEOF_TRX(hdr) + hdr->offsets[TRX_OFFSETS_DLFWLEN_IDX];
		bcopy(dbus_info->fw, dbus_info->image, img_offset);
		/* Step2: Copy NVRAM + pad */
		bcopy(dbus_info->nvram, (uint8 *)(dbus_info->image + img_offset),
			dbus_info->nvram_len);
		img_offset += dbus_info->nvram_len;
		if (nvram_words_pad) {
			bzero(&dbus_info->image[img_offset],
				nvram_words_pad);
			img_offset += nvram_words_pad;
		}
#ifdef BCMTRXV2
		/* Step3: Copy DSG/CFG for V2 */
		if (ISTRX_V2(hdr) &&
			(hdr->offsets[TRX_OFFSETS_DSG_LEN_IDX] ||
			hdr->offsets[TRX_OFFSETS_CFG_LEN_IDX])) {

			bcopy(dbus_info->fw + SIZEOF_TRX(hdr) +
				hdr->offsets[TRX_OFFSETS_DLFWLEN_IDX] +
				hdr->offsets[TRX_OFFSETS_NVM_LEN_IDX],
				dbus_info->image + img_offset,
				hdr->offsets[TRX_OFFSETS_DSG_LEN_IDX] +
				hdr->offsets[TRX_OFFSETS_CFG_LEN_IDX]);

			img_offset += hdr->offsets[TRX_OFFSETS_DSG_LEN_IDX] +
				hdr->offsets[TRX_OFFSETS_CFG_LEN_IDX];
		}
#endif /* BCMTRXV2 */
		/* Step4: update TRX header for nvram size */
		hdr = (struct trx_header *)dbus_info->image;
		hdr->len = htol32(len);
		/* Pass the actual fw len */
		hdr->offsets[TRX_OFFSETS_NVM_LEN_IDX] =
			htol32(dbus_info->nvram_len + nvram_words_pad);
		/* Calculate CRC over header */
		hdr->crc32 = hndcrc32((uint8 *)&hdr->flag_version,
			SIZEOF_TRX(hdr) - OFFSETOF(struct trx_header, flag_version),
			CRC32_INIT_VALUE);

		/* Calculate CRC over data */
		for (i = SIZEOF_TRX(hdr); i < len; ++i)
				hdr->crc32 = hndcrc32((uint8 *)&dbus_info->image[i], 1, hdr->crc32);
		hdr->crc32 = htol32(hdr->crc32);
	} else {
		dbus_info->image = dbus_info->fw;
		dbus_info->image_len = (uint32)dbus_info->fwlen;
	}

	return DBUS_OK;
} /* dbus_get_nvram */

/**
 * during driver initialization ('attach') or after PnP 'resume', firmware needs to be loaded into
 * the dongle
 */
static int
dbus_do_download(dbus_info_t *dbus_info)
{
	int err = DBUS_OK;
#ifndef BCM_REQUEST_FW
	int decomp_override = 0;
#endif
#ifdef BCM_REQUEST_FW
	uint16 boardrev = 0xFFFF, boardtype = 0xFFFF;
	int8 *temp_nvram;
	int temp_len;
#endif

#if defined(BCM_DNGL_EMBEDIMAGE)
	if (dbus_info->extdl.fw && (dbus_info->extdl.fwlen > 0)) {
		dbus_info->fw = (uint8 *)dbus_info->extdl.fw;
		dbus_info->fwlen = dbus_info->extdl.fwlen;
		DBUSERR(("dbus_do_download: using override firmmware %d bytes\n",
			dbus_info->fwlen));
	} else
		dbus_bus_fw_get(dbus_info->bus_info, &dbus_info->fw, &dbus_info->fwlen,
			&decomp_override);

	if (!dbus_info->fw) {
		DBUSERR(("dbus_do_download: devid 0x%x / %d not supported\n",
			dbus_info->pub.attrib.devid, dbus_info->pub.attrib.devid));
		return DBUS_ERR;
	}
#elif defined(BCM_REQUEST_FW)
	dbus_info->firmware = dbus_get_fw_nvfile(dbus_info->pub.attrib.devid,
		dbus_info->pub.attrib.chiprev, &dbus_info->fw, &dbus_info->fwlen,
		DBUS_FIRMWARE, 0, 0);
	if (!dbus_info->firmware)
		return DBUS_ERR;
#endif /* defined(BCM_DNGL_EMBEDIMAGE) */

	dbus_info->image = dbus_info->fw;
	dbus_info->image_len = (uint32)dbus_info->fwlen;

#ifndef BCM_REQUEST_FW
	if (UNZIP_ENAB(dbus_info) && !decomp_override) {
		err = dbus_zlib_decomp(dbus_info);
		if (err) {
			DBUSERR(("dbus_attach: fw decompress fail %d\n", err));
			return err;
		}
	}
#endif

#if defined(BCM_REQUEST_FW)
	/* check if firmware is appended with nvram file */
	err = dbus_otp(dbus_info, &boardtype, &boardrev);
	/* check if nvram is provided as separte file */
	nonfwnvram = NULL;
	nonfwnvramlen = 0;
	dbus_info->nvfile = dbus_get_fw_nvfile(dbus_info->pub.attrib.devid,
		dbus_info->pub.attrib.chiprev, (void *)&temp_nvram, &temp_len,
		DBUS_NVFILE, boardtype, boardrev);
	if (dbus_info->nvfile) {
		int8 *tmp = MALLOC(dbus_info->pub.osh, temp_len);
		if (tmp) {
			bcopy(temp_nvram, tmp, temp_len);
			nonfwnvram = tmp;
			nonfwnvramlen = temp_len;
		} else {
			err = DBUS_ERR;
			goto fail;
		}
	}
#endif /* defined(BCM_REQUEST_FW) */

	err = dbus_get_nvram(dbus_info);
	if (err) {
		DBUSERR(("dbus_do_download: fail to get nvram %d\n", err));
		return err;
	}


	if (dbus_info->drvintf->dlstart && dbus_info->drvintf->dlrun) {
		err = dbus_info->drvintf->dlstart(dbus_info->bus_info,
			dbus_info->image, dbus_info->image_len);

		if (err == DBUS_OK)
			err = dbus_info->drvintf->dlrun(dbus_info->bus_info);
	} else
		err = DBUS_ERR;

	if (dbus_info->nvram) {
		MFREE(dbus_info->pub.osh, dbus_info->image, dbus_info->image_len);
		dbus_info->image = dbus_info->fw;
		dbus_info->image_len = (uint32)dbus_info->fwlen;
	}

#ifndef BCM_REQUEST_FW
	if (UNZIP_ENAB(dbus_info) && (!decomp_override) && dbus_info->orig_fw) {
		MFREE(dbus_info->pub.osh, dbus_info->fw, dbus_info->decomp_memsize);
		dbus_info->image = dbus_info->fw = dbus_info->orig_fw;
		dbus_info->image_len = dbus_info->fwlen = dbus_info->origfw_len;
	}
#endif

#if defined(BCM_REQUEST_FW)
fail:
	if (dbus_info->firmware) {
		dbus_release_fw_nvfile(dbus_info->firmware);
		dbus_info->firmware = NULL;
	}
	if (dbus_info->nvfile) {
		dbus_release_fw_nvfile(dbus_info->nvfile);
		dbus_info->nvfile = NULL;
	}
	if (nonfwnvram) {
		MFREE(dbus_info->pub.osh, nonfwnvram, nonfwnvramlen);
		nonfwnvram = NULL;
		nonfwnvramlen = 0;
	}
#endif
	return err;
} /* dbus_do_download */

#endif /* defined(BCM_DNGL_EMBEDIMAGE) || defined(BCM_REQUEST_FW) */

/** required for DBUS deregistration */
static void
dbus_disconnect(void *handle)
{
	DBUSTRACE(("%s\n", __FUNCTION__));

	if (disconnect_cb)
		disconnect_cb(disc_arg);
}

/**
 * This function is called when the sent irb times out without a tx response status.
 * DBUS adds reliability by resending timed out IRBs DBUS_TX_RETRY_LIMIT times.
 */
static void
dbus_if_send_irb_timeout(void *handle, dbus_irb_tx_t *txirb)
{
	dbus_info_t *dbus_info = (dbus_info_t *) handle;

	if ((dbus_info == NULL) || (dbus_info->drvintf == NULL) || (txirb == NULL)) {
		return;
	}

	DBUSTRACE(("%s\n", __FUNCTION__));

	return;

} /* dbus_if_send_irb_timeout */

/**
 * When lower DBUS level signals that a send IRB completed, either successful or not, the higher
 * level (e.g. dhd_linux.c) has to be notified, and transmit flow control has to be evaluated.
 */
static void BCMFASTPATH
dbus_if_send_irb_complete(void *handle, dbus_irb_tx_t *txirb, int status)
{
	dbus_info_t *dbus_info = (dbus_info_t *) handle;
	int txirb_pending;
	struct exec_parms args;
	void *pktinfo;

	if ((dbus_info == NULL) || (txirb == NULL)) {
		return;
	}

	DBUSTRACE(("%s: status = %d\n", __FUNCTION__, status));

	dbus_tx_timer_stop(dbus_info);

	/* re-queue BEFORE calling send_complete which will assume that this irb
	   is now available.
	 */
	pktinfo = txirb->info;
	bzero(txirb, sizeof(dbus_irb_tx_t));
	args.qenq.q = dbus_info->tx_q;
	args.qenq.b = (dbus_irb_t *) txirb;
	EXEC_TXLOCK(dbus_info, q_enq_exec, &args);

	if (dbus_info->pub.busstate != DBUS_STATE_DOWN) {
		if ((status == DBUS_OK) || (status == DBUS_ERR_NODEVICE)) {
			if (dbus_info->cbs && dbus_info->cbs->send_complete)
				dbus_info->cbs->send_complete(dbus_info->cbarg, pktinfo,
					status);

			if (status == DBUS_OK) {
				txirb_pending = dbus_info->pub.ntxq - dbus_info->tx_q->cnt;
				if (txirb_pending)
					dbus_tx_timer_start(dbus_info, DBUS_TX_TIMEOUT_INTERVAL);
				if ((txirb_pending < dbus_info->tx_low_watermark) &&
					dbus_info->txoff && !dbus_info->txoverride) {
					dbus_flowctrl_tx(dbus_info, OFF);
				}
			}
		} else {
			DBUSERR(("%s: %d WARNING freeing orphan pkt %p\n", __FUNCTION__, __LINE__,
				pktinfo));
#if defined(BCM_RPC_NOCOPY) || defined(BCM_RPC_TXNOCOPY) || defined(BCM_RPC_TOC)
			if (pktinfo)
				if (dbus_info->cbs && dbus_info->cbs->send_complete)
					dbus_info->cbs->send_complete(dbus_info->cbarg, pktinfo,
						status);
#else
			dbus_if_pktfree(dbus_info, (void*)pktinfo, TRUE);
#endif /* defined(BCM_RPC_NOCOPY) || defined(BCM_RPC_TXNOCOPY) || defined(BCM_RPC_TOC) */
		}
	} else {
		DBUSERR(("%s: %d WARNING freeing orphan pkt %p\n", __FUNCTION__, __LINE__,
			pktinfo));
#if defined(BCM_RPC_NOCOPY) || defined(BCM_RPC_TXNOCOPY) || defined(BCM_RPC_TOC)
		if (pktinfo)
			if (dbus_info->cbs && dbus_info->cbs->send_complete)
				dbus_info->cbs->send_complete(dbus_info->cbarg, pktinfo,
					status);
#else
		dbus_if_pktfree(dbus_info, (void*)pktinfo, TRUE);
#endif /* defined(BCM_RPC_NOCOPY) || defined(BCM_RPC_TXNOCOPY) defined(BCM_RPC_TOC) */
	}
} /* dbus_if_send_irb_complete */

/**
 * When lower DBUS level signals that a receive IRB completed, either successful or not, the higher
 * level (e.g. dhd_linux.c) has to be notified, and fresh free receive IRBs may have to be given
 * to lower levels.
 */
static void BCMFASTPATH
dbus_if_recv_irb_complete(void *handle, dbus_irb_rx_t *rxirb, int status)
{
	dbus_info_t *dbus_info = (dbus_info_t *) handle;
	int rxirb_pending;
	struct exec_parms args;

	if ((dbus_info == NULL) || (rxirb == NULL)) {
		return;
	}
	DBUSTRACE(("%s\n", __FUNCTION__));
	if (dbus_info->pub.busstate != DBUS_STATE_DOWN &&
		dbus_info->pub.busstate != DBUS_STATE_SLEEP) {
		if (status == DBUS_OK) {
			if ((rxirb->buf != NULL) && (rxirb->actual_len > 0)) {
#ifdef DBUS_USB_LOOPBACK
				if (is_loopback_pkt(rxirb->buf)) {
					matches_loopback_pkt(rxirb->buf);
				} else
#endif
				if (dbus_info->cbs && dbus_info->cbs->recv_buf) {
					dbus_info->cbs->recv_buf(dbus_info->cbarg, rxirb->buf,
					rxirb->actual_len);
				}
			} else if (rxirb->pkt != NULL) {
				if (dbus_info->cbs && dbus_info->cbs->recv_pkt)
					dbus_info->cbs->recv_pkt(dbus_info->cbarg, rxirb->pkt);
			} else {
				ASSERT(0); /* Should not happen */
			}

			rxirb_pending = dbus_info->pub.nrxq - dbus_info->rx_q->cnt - 1;
			if ((rxirb_pending <= dbus_info->rx_low_watermark) &&
				!dbus_info->rxoff) {
				DBUSTRACE(("Low watermark so submit more %d <= %d \n",
					dbus_info->rx_low_watermark, rxirb_pending));
				dbus_rxirbs_fill(dbus_info);
			} else if (dbus_info->rxoff)
				DBUSTRACE(("rx flow controlled. not filling more. cut_rxq=%d\n",
					dbus_info->rx_q->cnt));
		} else if (status == DBUS_ERR_NODEVICE) {
			DBUSERR(("%s: %d status = %d, buf %p\n", __FUNCTION__, __LINE__, status,
				rxirb->buf));
#if defined(BCM_RPC_NOCOPY) || defined(BCM_RPC_RXNOCOPY)
			if (rxirb->buf) {
				PKTFRMNATIVE(dbus_info->pub.osh, rxirb->buf);
				PKTFREE(dbus_info->pub.osh, rxirb->buf, FALSE);
			}
#endif /* BCM_RPC_NOCOPY || BCM_RPC_TXNOCOPY || BCM_RPC_TOC */
		} else {
			if (status != DBUS_ERR_RXZLP)
				DBUSERR(("%s: %d status = %d, buf %p\n", __FUNCTION__, __LINE__,
					status, rxirb->buf));
#if defined(BCM_RPC_NOCOPY) || defined(BCM_RPC_RXNOCOPY)
			if (rxirb->buf) {
				PKTFRMNATIVE(dbus_info->pub.osh, rxirb->buf);
				PKTFREE(dbus_info->pub.osh, rxirb->buf, FALSE);
			}
#endif /* BCM_RPC_NOCOPY || BCM_RPC_TXNOCOPY || BCM_RPC_TOC */
		}
	} else {
		DBUSTRACE(("%s: DBUS down, ignoring recv callback. buf %p\n", __FUNCTION__,
			rxirb->buf));
#if defined(BCM_RPC_NOCOPY) || defined(BCM_RPC_RXNOCOPY)
		if (rxirb->buf) {
			PKTFRMNATIVE(dbus_info->pub.osh, rxirb->buf);
			PKTFREE(dbus_info->pub.osh, rxirb->buf, FALSE);
		}
#endif /* BCM_RPC_NOCOPY || BCM_RPC_TXNOCOPY || BCM_RPC_TOC */
	}
	if (dbus_info->rx_q != NULL) {
		bzero(rxirb, sizeof(dbus_irb_rx_t));
		args.qenq.q = dbus_info->rx_q;
		args.qenq.b = (dbus_irb_t *) rxirb;
		EXEC_RXLOCK(dbus_info, q_enq_exec, &args);
	} else
		MFREE(dbus_info->pub.osh, rxirb, sizeof(dbus_irb_tx_t));
} /* dbus_if_recv_irb_complete */

/**
 *  Accumulate errors signaled by lower DBUS levels and signal them to higher (e.g. dhd_linux.c)
 *  level.
 */
static void
dbus_if_errhandler(void *handle, int err)
{
	dbus_info_t *dbus_info = handle;
	uint32 mask = 0;

	if (dbus_info == NULL)
		return;

	switch (err) {
		case DBUS_ERR_TXFAIL:
			dbus_info->pub.stats.tx_errors++;
			mask |= ERR_CBMASK_TXFAIL;
			break;
		case DBUS_ERR_TXDROP:
			dbus_info->pub.stats.tx_dropped++;
			mask |= ERR_CBMASK_TXFAIL;
			break;
		case DBUS_ERR_RXFAIL:
			dbus_info->pub.stats.rx_errors++;
			mask |= ERR_CBMASK_RXFAIL;
			break;
		case DBUS_ERR_RXDROP:
			dbus_info->pub.stats.rx_dropped++;
			mask |= ERR_CBMASK_RXFAIL;
			break;
		default:
			break;
	}

	if (dbus_info->cbs && dbus_info->cbs->errhandler && (dbus_info->errmask & mask))
		dbus_info->cbs->errhandler(dbus_info->cbarg, err);
}

/**
 * When lower DBUS level signals control IRB completed, higher level (e.g. dhd_linux.c) has to be
 * notified.
 */
static void
dbus_if_ctl_complete(void *handle, int type, int status)
{
	dbus_info_t *dbus_info = (dbus_info_t *) handle;

	if (dbus_info == NULL)
		return;

	if (dbus_info->pub.busstate != DBUS_STATE_DOWN) {
		if (dbus_info->cbs && dbus_info->cbs->ctl_complete)
			dbus_info->cbs->ctl_complete(dbus_info->cbarg, type, status);
	}
}

/**
 * Rx related functionality (flow control, posting of free IRBs to rx queue) is dependent upon the
 * bus state. When lower DBUS level signals a change in the interface state, take appropriate action
 * and forward the signaling to the higher (e.g. dhd_linux.c) level.
 */
static void
dbus_if_state_change(void *handle, int state)
{
	dbus_info_t *dbus_info = (dbus_info_t *) handle;
	int old_state;

	if (dbus_info == NULL)
		return;

	if (dbus_info->pub.busstate == state)
		return;
	old_state = dbus_info->pub.busstate;
	if (state == DBUS_STATE_DISCONNECT) {
		DBUSERR(("DBUS disconnected\n"));
	}

	/* Ignore USB SUSPEND while not up yet */
	if (state == DBUS_STATE_SLEEP && old_state != DBUS_STATE_UP)
		return;

	DBUSTRACE(("dbus state change from %d to to %d\n", old_state, state));

	/* Don't update state if it's PnP firmware re-download */
	if (state != DBUS_STATE_PNP_FWDL)
		dbus_info->pub.busstate = state;
	else
		dbus_flowctrl_rx(handle, FALSE);
	if (state == DBUS_STATE_SLEEP)
		dbus_flowctrl_rx(handle, TRUE);
	if (state == DBUS_STATE_UP) {
		dbus_rxirbs_fill(dbus_info);
		dbus_flowctrl_rx(handle, FALSE);
	}

	if (dbus_info->cbs && dbus_info->cbs->state_change)
		dbus_info->cbs->state_change(dbus_info->cbarg, state);
}

/** Forward request for packet from lower DBUS layer to higher layer (e.g. dhd_linux.c) */
static void *
dbus_if_pktget(void *handle, uint len, bool send)
{
	dbus_info_t *dbus_info = (dbus_info_t *) handle;
	void *p = NULL;

	if (dbus_info == NULL)
		return NULL;

	if (dbus_info->cbs && dbus_info->cbs->pktget)
		p = dbus_info->cbs->pktget(dbus_info->cbarg, len, send);
	else
		ASSERT(0);

	return p;
}

/** Forward request to free packet from lower DBUS layer to higher layer (e.g. dhd_linux.c) */
static void
dbus_if_pktfree(void *handle, void *p, bool send)
{
	dbus_info_t *dbus_info = (dbus_info_t *) handle;

	if (dbus_info == NULL)
		return;

	if (dbus_info->cbs && dbus_info->cbs->pktfree)
		dbus_info->cbs->pktfree(dbus_info->cbarg, p, send);
	else
		ASSERT(0);
}

/** Lower DBUS level requests either a send or receive IRB */
static struct dbus_irb*
dbus_if_getirb(void *cbarg, bool send)
{
	dbus_info_t *dbus_info = (dbus_info_t *) cbarg;
	struct exec_parms args;
	struct dbus_irb *irb;

	if ((dbus_info == NULL) || (dbus_info->pub.busstate != DBUS_STATE_UP))
		return NULL;

	if (send == TRUE) {
		args.qdeq.q = dbus_info->tx_q;
		irb = EXEC_TXLOCK(dbus_info, q_deq_exec, &args);
	} else {
		args.qdeq.q = dbus_info->rx_q;
		irb = EXEC_RXLOCK(dbus_info, q_deq_exec, &args);
	}

	return irb;
}

/**
 * Called as part of DBUS bus registration. Calls back into higher level (e.g. dhd_linux.c) probe
 * function.
 */
static void *
dbus_probe(void *arg, const char *desc, uint32 bustype, uint32 hdrlen)
{
	if (probe_cb) {
		disc_arg = probe_cb(probe_arg, desc, bustype, hdrlen);
		return disc_arg;
	}

	return (void *)DBUS_ERR;
}

/**
 * As part of initialization, higher level (e.g. dhd_linux.c) requests DBUS to prepare for
 * action.
 */
int
dbus_register(int vid, int pid, probe_cb_t prcb,
	disconnect_cb_t discb, void *prarg, void *param1, void *param2)
{
	int err;

	DBUSTRACE(("%s\n", __FUNCTION__));

	probe_cb = prcb;
	disconnect_cb = discb;
	probe_arg = prarg;

	err = dbus_bus_register(vid, pid, dbus_probe, /* call lower DBUS level register function */
		dbus_disconnect, NULL, &g_busintf, param1, param2);

	return err;
}

int
dbus_deregister()
{
	int ret;

	DBUSTRACE(("%s\n", __FUNCTION__));

	probe_cb = NULL;
	ret = dbus_bus_deregister();
	disconnect_cb = NULL;
	probe_arg = NULL;

	return ret;

}

/** As part of initialization, data structures have to be allocated and initialized */
dbus_pub_t *
dbus_attach(osl_t *osh, int rxsize, int nrxq, int ntxq, void *cbarg,
	dbus_callbacks_t *cbs, dbus_extdl_t *extdl, struct shared_info *sh)
{
	dbus_info_t *dbus_info;
	int err;

	if ((g_busintf == NULL) || (g_busintf->attach == NULL) || (cbs == NULL))
		return NULL;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if ((nrxq <= 0) || (ntxq <= 0))
		return NULL;

	dbus_info = MALLOC(osh, sizeof(dbus_info_t));
	if (dbus_info == NULL)
		return NULL;

	bzero(dbus_info, sizeof(dbus_info_t));

	/* BUS-specific driver interface (at a lower DBUS level) */
	dbus_info->drvintf = g_busintf;
	dbus_info->cbarg = cbarg;
	dbus_info->cbs = cbs;

	dbus_info->pub.sh = sh;
	dbus_info->pub.osh = osh;
	dbus_info->pub.rxsize = rxsize;

#ifdef EHCI_FASTPATH_RX
	atomic_set(&dbus_info->rx_outstanding, 0);
#endif

	dbus_info->pub.nrxq = nrxq;
	dbus_info->rx_low_watermark = nrxq / 2;	/* keep enough posted rx urbs */
	dbus_info->pub.ntxq = ntxq;
	dbus_info->tx_low_watermark = ntxq / 4;	/* flow control when too many tx urbs posted */

	dbus_info->tx_q = MALLOC(osh, sizeof(dbus_irbq_t));
	if (dbus_info->tx_q == NULL)
		goto error;
	else {
		bzero(dbus_info->tx_q, sizeof(dbus_irbq_t));
		err = dbus_irbq_init(dbus_info, dbus_info->tx_q, ntxq, sizeof(dbus_irb_tx_t));
		if (err != DBUS_OK)
			goto error;
	}

	dbus_info->rx_q = MALLOC(osh, sizeof(dbus_irbq_t));
	if (dbus_info->rx_q == NULL)
		goto error;
	else {
		bzero(dbus_info->rx_q, sizeof(dbus_irbq_t));
		err = dbus_irbq_init(dbus_info, dbus_info->rx_q, nrxq, sizeof(dbus_irb_rx_t));
		if (err != DBUS_OK)
			goto error;
	}


	dbus_info->bus_info = (void *)g_busintf->attach(&dbus_info->pub,
		dbus_info, &dbus_intf_cbs);
	if (dbus_info->bus_info == NULL)
		goto error;

	dbus_tx_timer_init(dbus_info);

#if defined(BCM_DNGL_EMBEDIMAGE) || defined(BCM_REQUEST_FW)
	/* Need to copy external image for re-download */
	if (extdl && extdl->fw && (extdl->fwlen > 0)) {
		dbus_info->extdl.fw = MALLOC(osh, extdl->fwlen);
		if (dbus_info->extdl.fw) {
			bcopy(extdl->fw, dbus_info->extdl.fw, extdl->fwlen);
			dbus_info->extdl.fwlen = extdl->fwlen;
		}
	}

	if (extdl && extdl->vars && (extdl->varslen > 0)) {
		dbus_info->extdl.vars = MALLOC(osh, extdl->varslen);
		if (dbus_info->extdl.vars) {
			bcopy(extdl->vars, dbus_info->extdl.vars, extdl->varslen);
			dbus_info->extdl.varslen = extdl->varslen;
		}
	}

	if (dbus_download_firmware(&dbus_info->pub) != DBUS_OK)
		goto error;
#endif /* defined(BCM_DNGL_EMBEDIMAGE) || defined(BCM_REQUEST_FW) */

	return (dbus_pub_t *)dbus_info;

error:
	dbus_detach((dbus_pub_t *)dbus_info);
	return NULL;
} /* dbus_attach */

void
dbus_detach(dbus_pub_t *pub)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;
	osl_t *osh;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dbus_info == NULL)
		return;

	dbus_tx_timer_stop(dbus_info);

	osh = pub->osh;

	if (dbus_info->drvintf && dbus_info->drvintf->detach)
		 dbus_info->drvintf->detach((dbus_pub_t *)dbus_info, dbus_info->bus_info);

	if (dbus_info->tx_q) {
		dbus_irbq_deinit(dbus_info, dbus_info->tx_q, sizeof(dbus_irb_tx_t));
		MFREE(osh, dbus_info->tx_q, sizeof(dbus_irbq_t));
		dbus_info->tx_q = NULL;
	}

	if (dbus_info->rx_q) {
		dbus_irbq_deinit(dbus_info, dbus_info->rx_q, sizeof(dbus_irb_rx_t));
		MFREE(osh, dbus_info->rx_q, sizeof(dbus_irbq_t));
		dbus_info->rx_q = NULL;
	}


	if (dbus_info->extdl.fw && (dbus_info->extdl.fwlen > 0)) {
		MFREE(osh, dbus_info->extdl.fw, dbus_info->extdl.fwlen);
		dbus_info->extdl.fw = NULL;
		dbus_info->extdl.fwlen = 0;
	}

	if (dbus_info->extdl.vars && (dbus_info->extdl.varslen > 0)) {
		MFREE(osh, dbus_info->extdl.vars, dbus_info->extdl.varslen);
		dbus_info->extdl.vars = NULL;
		dbus_info->extdl.varslen = 0;
	}

	MFREE(osh, dbus_info, sizeof(dbus_info_t));
} /* dbus_detach */

#if (defined(BCM_DNGL_EMBEDIMAGE) || defined(BCM_REQUEST_FW))

int dbus_download_firmware(dbus_pub_t *pub)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;
	int err = DBUS_OK;

	DBUSTRACE(("%s: state %d\n", __FUNCTION__, dbus_info->pub.busstate));

	if (dbus_info->drvintf->dlneeded) {
		if (dbus_info->drvintf->dlneeded(dbus_info->bus_info)) {
			dbus_info->pub.busstate = DBUS_STATE_DL_PENDING;
			err = dbus_do_download(dbus_info);
			if (err == DBUS_OK) {
				dbus_info->pub.busstate = DBUS_STATE_DL_DONE;
			} else {
				DBUSERR(("%s: download failed (%d)\n", __FUNCTION__, err));
			}
		}
	}

	return err;
}

#endif /* BCM_DNGL_EMBEDIMAGE || BCM_REQUEST_FW */

/**
 * higher layer requests us to 'up' the interface to the dongle. Prerequisite is that firmware (not
 * bootloader) must be active in the dongle.
 */
int
dbus_up(dbus_pub_t *pub)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;
	int err = DBUS_OK;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dbus_info == NULL)
		return DBUS_ERR;

	if ((dbus_info->pub.busstate == DBUS_STATE_DL_DONE) ||
		(dbus_info->pub.busstate == DBUS_STATE_DOWN) ||
		(dbus_info->pub.busstate == DBUS_STATE_SLEEP)) {
		if (dbus_info->drvintf && dbus_info->drvintf->up) {
			err = dbus_info->drvintf->up(dbus_info->bus_info);

			if (err == DBUS_OK) {
				dbus_rxirbs_fill(dbus_info);
			}
		}
	} else
		err = DBUS_ERR;

	return err;
}

/** higher layer requests us to 'down' the interface to the dongle. */
int
dbus_down(dbus_pub_t *pub)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dbus_info == NULL)
		return DBUS_ERR;

	dbus_tx_timer_stop(dbus_info);

	if (dbus_info->pub.busstate == DBUS_STATE_UP ||
		dbus_info->pub.busstate == DBUS_STATE_SLEEP) {
		if (dbus_info->drvintf && dbus_info->drvintf->down)
			return dbus_info->drvintf->down(dbus_info->bus_info);
	}

	return DBUS_ERR;
}

int
dbus_shutdown(dbus_pub_t *pub)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dbus_info == NULL)
		return DBUS_ERR;

	if (dbus_info->drvintf && dbus_info->drvintf->shutdown)
		return dbus_info->drvintf->shutdown(dbus_info->bus_info);

	return DBUS_OK;
}

int
dbus_stop(dbus_pub_t *pub)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dbus_info == NULL)
		return DBUS_ERR;

	if (dbus_info->pub.busstate == DBUS_STATE_UP ||
		dbus_info->pub.busstate == DBUS_STATE_SLEEP) {
		if (dbus_info->drvintf && dbus_info->drvintf->stop)
			return dbus_info->drvintf->stop(dbus_info->bus_info);
	}

	return DBUS_ERR;
}

int dbus_send_txdata(dbus_pub_t *dbus, void *pktbuf)
{
#ifdef BCMSDIO
	return dbus_send_pkt(dbus, pktbuf, NULL /* pktinfo */);
#else
	return dbus_send_pkt(dbus, pktbuf, pktbuf /* pktinfo */);
#endif
}

int
dbus_send_buf(dbus_pub_t *pub, uint8 *buf, int len, void *info)
{
	return dbus_send_irb(pub, buf, len, NULL, info);
}

int
dbus_send_pkt(dbus_pub_t *pub, void *pkt, void *info)
{
	return dbus_send_irb(pub, NULL, 0, pkt, info);
}

int
dbus_send_ctl(dbus_pub_t *pub, uint8 *buf, int len)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;

	if (dbus_info == NULL)
		return DBUS_ERR;

	if (dbus_info->pub.busstate == DBUS_STATE_UP ||
		dbus_info->pub.busstate == DBUS_STATE_SLEEP) {
		if (dbus_info->drvintf && dbus_info->drvintf->send_ctl)
			return dbus_info->drvintf->send_ctl(dbus_info->bus_info, buf, len);
	}

	return DBUS_ERR;
}

int
dbus_recv_ctl(dbus_pub_t *pub, uint8 *buf, int len)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;

	if ((dbus_info == NULL) || (buf == NULL))
		return DBUS_ERR;

	if (dbus_info->pub.busstate == DBUS_STATE_UP ||
		dbus_info->pub.busstate == DBUS_STATE_SLEEP) {
		if (dbus_info->drvintf && dbus_info->drvintf->recv_ctl)
			return dbus_info->drvintf->recv_ctl(dbus_info->bus_info, buf, len);
	}

	return DBUS_ERR;
}

/** Only called via RPC (Dec 2012) */
int
dbus_recv_bulk(dbus_pub_t *pub, uint32 ep_idx)
{
#ifdef EHCI_FASTPATH_RX
	/* 2nd bulk in not supported for EHCI_FASTPATH_RX */
	ASSERT(0);
#else
	dbus_info_t *dbus_info = (dbus_info_t *) pub;

	dbus_irb_rx_t *rxirb;
	struct exec_parms args;
	int status;


	if (dbus_info == NULL)
		return DBUS_ERR;

	args.qdeq.q = dbus_info->rx_q;
	if (dbus_info->pub.busstate == DBUS_STATE_UP) {
		if (dbus_info->drvintf && dbus_info->drvintf->recv_irb_from_ep) {
			if ((rxirb = (EXEC_RXLOCK(dbus_info, q_deq_exec, &args))) != NULL) {
				status = dbus_info->drvintf->recv_irb_from_ep(dbus_info->bus_info,
					rxirb, ep_idx);
				if (status == DBUS_ERR_RXDROP) {
					bzero(rxirb, sizeof(dbus_irb_rx_t));
					args.qenq.q = dbus_info->rx_q;
					args.qenq.b = (dbus_irb_t *) rxirb;
					EXEC_RXLOCK(dbus_info, q_enq_exec, &args);
				}
			}
		}
	}
#endif /* EHCI_FASTPATH_RX */

	return DBUS_ERR;
}

/** only called by dhd_cdc.c (Dec 2012) */
int
dbus_poll_intr(dbus_pub_t *pub)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;

	int status = DBUS_ERR;

	if (dbus_info == NULL)
		return DBUS_ERR;

	if (dbus_info->pub.busstate == DBUS_STATE_UP) {
		if (dbus_info->drvintf && dbus_info->drvintf->recv_irb_from_ep) {
			status = dbus_info->drvintf->recv_irb_from_ep(dbus_info->bus_info,
				NULL, 0xff);
		}
	}
	return status;
}

/** called by nobody (Dec 2012) */
void *
dbus_pktget(dbus_pub_t *pub, int len)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;

	if ((dbus_info == NULL) || (len < 0))
		return NULL;

	return PKTGET(dbus_info->pub.osh, len, TRUE);
}

/** called by nobody (Dec 2012) */
void
dbus_pktfree(dbus_pub_t *pub, void* pkt)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;

	if ((dbus_info == NULL) || (pkt == NULL))
		return;

	PKTFREE(dbus_info->pub.osh, pkt, TRUE);
}

/** called by nobody (Dec 2012) */
int
dbus_get_stats(dbus_pub_t *pub, dbus_stats_t *stats)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;

	if ((dbus_info == NULL) || (stats == NULL))
		return DBUS_ERR;

	bcopy(&dbus_info->pub.stats, stats, sizeof(dbus_stats_t));

	return DBUS_OK;
}

int
dbus_get_attrib(dbus_pub_t *pub, dbus_attrib_t *attrib)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;
	int err = DBUS_ERR;

	if ((dbus_info == NULL) || (attrib == NULL))
		return DBUS_ERR;

	if (dbus_info->drvintf && dbus_info->drvintf->get_attrib) {
		err = dbus_info->drvintf->get_attrib(dbus_info->bus_info,
		&dbus_info->pub.attrib);
	}

	bcopy(&dbus_info->pub.attrib, attrib, sizeof(dbus_attrib_t));
	return err;
}

int
dbus_get_device_speed(dbus_pub_t *pub)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;

	if (dbus_info == NULL)
		return INVALID_SPEED;

	return (dbus_info->pub.device_speed);
}

int
dbus_set_config(dbus_pub_t *pub, dbus_config_t *config)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;
	int err = DBUS_ERR;

	if ((dbus_info == NULL) || (config == NULL))
		return DBUS_ERR;

	if (dbus_info->drvintf && dbus_info->drvintf->set_config) {
		err = dbus_info->drvintf->set_config(dbus_info->bus_info,
			config);

		if ((config->config_id == DBUS_CONFIG_ID_AGGR_LIMIT) &&
			(!err) &&
			(dbus_info->pub.busstate == DBUS_STATE_UP)) {
			dbus_rxirbs_fill(dbus_info);
		}
	}

	return err;
}

int
dbus_get_config(dbus_pub_t *pub, dbus_config_t *config)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;
	int err = DBUS_ERR;

	if ((dbus_info == NULL) || (config == NULL))
		return DBUS_ERR;

	if (dbus_info->drvintf && dbus_info->drvintf->get_config) {
		err = dbus_info->drvintf->get_config(dbus_info->bus_info,
		config);
	}

	return err;
}

int
dbus_set_errmask(dbus_pub_t *pub, uint32 mask)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;
	int err = DBUS_OK;

	if (dbus_info == NULL)
		return DBUS_ERR;

	dbus_info->errmask = mask;
	return err;
}

int
dbus_pnp_resume(dbus_pub_t *pub, int *fw_reload)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;
	int err = DBUS_ERR;
	bool fwdl = FALSE;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dbus_info == NULL)
		return DBUS_ERR;

	if (dbus_info->pub.busstate == DBUS_STATE_UP) {
		return DBUS_OK;
	}


#if defined(BCM_DNGL_EMBEDIMAGE)
	if (dbus_info->drvintf->device_exists &&
		dbus_info->drvintf->device_exists(dbus_info->bus_info)) {
		if (dbus_info->drvintf->dlneeded) {
			if (dbus_info->drvintf->dlneeded(dbus_info->bus_info)) {
				err = dbus_do_download(dbus_info);
				if (err == DBUS_OK) {
					fwdl = TRUE;
				}
				if (dbus_info->pub.busstate == DBUS_STATE_DL_DONE)
					dbus_up(&dbus_info->pub);
			}
		}
	} else {
		return DBUS_ERR;
	}
#endif /* BCM_DNGL_EMBEDIMAGE */

	if (dbus_info->drvintf->pnp) {
		err = dbus_info->drvintf->pnp(dbus_info->bus_info,
			DBUS_PNP_RESUME);
	}

	if (dbus_info->drvintf->recv_needed) {
		if (dbus_info->drvintf->recv_needed(dbus_info->bus_info)) {
			/* Refill after sleep/hibernate */
			dbus_rxirbs_fill(dbus_info);
		}
	}

#if defined(BCM_DNGL_EMBEDIMAGE)
	if (fwdl == TRUE) {
		dbus_if_state_change(dbus_info, DBUS_STATE_PNP_FWDL);
	}
#endif /* BCM_DNGL_EMBEDIMAGE */

	if (fw_reload)
		*fw_reload = fwdl;

	return err;
} /* dbus_pnp_resume */

int
dbus_pnp_sleep(dbus_pub_t *pub)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;
	int err = DBUS_ERR;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dbus_info == NULL)
		return DBUS_ERR;

	dbus_tx_timer_stop(dbus_info);

	if (dbus_info->drvintf && dbus_info->drvintf->pnp) {
		err = dbus_info->drvintf->pnp(dbus_info->bus_info,
			DBUS_PNP_SLEEP);
	}

	return err;
}

int
dbus_pnp_disconnect(dbus_pub_t *pub)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;
	int err = DBUS_ERR;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dbus_info == NULL)
		return DBUS_ERR;

	dbus_tx_timer_stop(dbus_info);

	if (dbus_info->drvintf && dbus_info->drvintf->pnp) {
		err = dbus_info->drvintf->pnp(dbus_info->bus_info,
			DBUS_PNP_DISCONNECT);
	}

	return err;
}

int
dbus_iovar_op(dbus_pub_t *pub, const char *name,
	void *params, int plen, void *arg, int len, bool set)
{
	dbus_info_t *dbus_info = (dbus_info_t *) pub;
	int err = DBUS_ERR;

	DBUSTRACE(("%s\n", __FUNCTION__));

	if (dbus_info == NULL)
		return DBUS_ERR;

	if (dbus_info->drvintf && dbus_info->drvintf->iovar_op) {
		err = dbus_info->drvintf->iovar_op(dbus_info->bus_info,
			name, params, plen, arg, len, set);
	}

	return err;
}


void *
dhd_dbus_txq(const dbus_pub_t *pub)
{
	return NULL;
}

uint
dhd_dbus_hdrlen(const dbus_pub_t *pub)
{
	return 0;
}

void *
dbus_get_devinfo(dbus_pub_t *pub)
{
	return pub->dev_info;
}

#if defined(BCM_DNGL_EMBEDIMAGE) || defined(BCM_REQUEST_FW)

static int
dbus_otp(dbus_info_t *dbus_info, uint16 *boardtype, uint16 *boardrev)
{
	uint32 value = 0;
	uint8 *cis;
	uint16 *otpinfo;
	uint32 i;
	bool standard_cis = TRUE;
	uint8 tup, tlen;
	bool btype_present = FALSE;
	bool brev_present = FALSE;
	int ret;
	int devid;
	uint16 btype = 0;
	uint16 brev = 0;
	uint32 otp_size = 0, otp_addr = 0, otp_sw_rgn = 0;

	if (dbus_info == NULL || dbus_info->drvintf == NULL ||
		dbus_info->drvintf->readreg == NULL)
		return DBUS_ERR;

	devid = dbus_info->pub.attrib.devid;

	if ((devid == BCM43234_CHIP_ID) || (devid == BCM43235_CHIP_ID) ||
		(devid == BCM43236_CHIP_ID)) {

		otp_size = BCM_OTP_SIZE_43236;
		otp_sw_rgn = BCM_OTP_SW_RGN_43236;
		otp_addr = BCM_OTP_ADDR_43236;

	} else {
		return DBUS_ERR_NVRAM;
	}

	cis = MALLOC(dbus_info->pub.osh, otp_size * 2);
	if (cis == NULL)
		return DBUS_ERR;

	otpinfo = (uint16 *) cis;

	for (i = 0; i < otp_size; i++) {

		ret = dbus_info->drvintf->readreg(dbus_info->bus_info,
			otp_addr + ((otp_sw_rgn + i) << 1), 2, &value);

		if (ret != DBUS_OK) {
			MFREE(dbus_info->pub.osh, cis, otp_size * 2);
			return ret;
		}
		otpinfo[i] = (uint16) value;
	}

	for (i = 0; i < (otp_size << 1); ) {

		if (standard_cis) {
			tup = cis[i++];
			if (tup == CISTPL_NULL || tup == CISTPL_END)
				tlen = 0;
			else
				tlen = cis[i++];
		} else {
			if (cis[i] == CISTPL_NULL || cis[i] == CISTPL_END) {
				tlen = 0;
				tup = cis[i];
			} else {
				tlen = cis[i];
				tup = CISTPL_BRCM_HNBU;
			}
			++i;
		}

		if (tup == CISTPL_END || (i + tlen) >= (otp_size << 1)) {
			break;
		}

		switch (tup) {

		case CISTPL_BRCM_HNBU:

			switch (cis[i]) {

			case HNBU_BOARDTYPE:

				btype = (uint16) ((cis[i + 2] << 8) + cis[i + 1]);
				btype_present = TRUE;
				DBUSTRACE(("%s: HNBU_BOARDTYPE = 0x%2x\n", __FUNCTION__,
					(uint32)btype));
				break;

			case HNBU_BOARDREV:

				if (tlen == 2)
					brev = (uint16) cis[i + 1];
				else
					brev = (uint16) ((cis[i + 2] << 8) + cis[i + 1]);
				brev_present = TRUE;
				DBUSTRACE(("%s: HNBU_BOARDREV =  0x%2x\n", __FUNCTION__,
					(uint32)*boardrev));
				break;

			case HNBU_HNBUCIS:
				DBUSTRACE(("%s: HNBU_HNBUCIS\n", __FUNCTION__));
				tlen++;
				standard_cis = FALSE;
				break;
			}
			break;
		}

		i += tlen;
	}

	MFREE(dbus_info->pub.osh, cis, otp_size * 2);

	if (btype_present == TRUE && brev_present == TRUE) {
		*boardtype = btype;
		*boardrev = brev;
		DBUSERR(("otp boardtype = 0x%2x boardrev = 0x%2x\n",
			*boardtype, *boardrev));

		return DBUS_OK;
	}
	else
		return DBUS_ERR;
} /* dbus_otp */

static int
dbus_select_nvram(dbus_info_t *dbus_info, int8 *jumbonvram, int jumbolen,
uint16 boardtype, uint16 boardrev, int8 **nvram, int *nvram_len)
{
	/* Multi board nvram file format is contenation of nvram info with \r
	*  The file format for two contatenated set is
	*  \nBroadcom Jumbo Nvram file\nfirst_set\nsecond_set\nthird_set\n
	*/
	uint8 *nvram_start = NULL, *nvram_end = NULL;
	uint8 *nvram_start_prev = NULL, *nvram_end_prev = NULL;
	uint16 btype = 0, brev = 0;
	int len  = 0;
	char *field;

	*nvram = NULL;
	*nvram_len = 0;

	if (strncmp(BCM_JUMBO_START, jumbonvram, strlen(BCM_JUMBO_START))) {
		/* single nvram file in the native format */
		DBUSTRACE(("%s: Non-Jumbo NVRAM File \n", __FUNCTION__));
		*nvram = jumbonvram;
		*nvram_len = jumbolen;
		return DBUS_OK;
	} else {
		DBUSTRACE(("%s: Jumbo NVRAM File \n", __FUNCTION__));
	}

	/* sanity test the end of the config sets for proper ending */
	if (jumbonvram[jumbolen - 1] != BCM_JUMBO_NVRAM_DELIMIT ||
		jumbonvram[jumbolen - 2] != '\0') {
		DBUSERR(("%s: Bad Jumbo NVRAM file format\n", __FUNCTION__));
		return DBUS_JUMBO_BAD_FORMAT;
	}

	dbus_info->nvram_nontxt = DBUS_NVRAM_NONTXT;

	nvram_start = jumbonvram;

	while (*nvram_start != BCM_JUMBO_NVRAM_DELIMIT && len < jumbolen) {

		/* consume the  first file info line
		* \nBroadcom Jumbo Nvram file\nfile1\n ...
		*/
		len ++;
		nvram_start ++;
	}

	nvram_end = nvram_start;

	/* search for "boardrev=0xabcd" and "boardtype=0x1234" information in
	* the concatenated nvram config files /sets
	*/

	while (len < jumbolen) {

		if (*nvram_end == '\0') {
			/* end of a config set is marked by multiple null characters */
			len ++;
			nvram_end ++;
			DBUSTRACE(("%s: NULL chr len = %d char = 0x%x\n", __FUNCTION__,
				len, *nvram_end));
			continue;

		} else if (*nvram_end == BCM_JUMBO_NVRAM_DELIMIT) {

			/* config set delimiter is reached */
			/* check if next config set is present or not
			*  return  if next config is not present
			*/

			/* start search the next config set */
			nvram_start_prev = nvram_start;
			nvram_end_prev = nvram_end;

			nvram_end ++;
			nvram_start = nvram_end;
			btype = brev = 0;
			DBUSTRACE(("%s: going to next record len = %d "
					"char = 0x%x \n", __FUNCTION__, len, *nvram_end));
			len ++;
			if (len >= jumbolen) {

				*nvram = nvram_start_prev;
				*nvram_len = (int)(nvram_end_prev - nvram_start_prev);

				DBUSTRACE(("%s: no more len = %d nvram_end = 0x%p",
					__FUNCTION__, len, nvram_end));

				return DBUS_JUMBO_NOMATCH;

			} else {
				continue;
			}

		} else {

			DBUSTRACE(("%s: config str = %s\n", __FUNCTION__, nvram_end));

			if (bcmp(nvram_end, "boardtype", strlen("boardtype")) == 0) {

				field = strchr(nvram_end, '=');
				field++;
				btype = (uint16)bcm_strtoul(field, NULL, 0);

				DBUSTRACE(("%s: btype = 0x%x boardtype = 0x%x \n", __FUNCTION__,
					btype, boardtype));
			}

			if (bcmp(nvram_end, "boardrev", strlen("boardrev")) == 0) {

				field = strchr(nvram_end, '=');
				field++;
				brev = (uint16)bcm_strtoul(field, NULL, 0);

				DBUSTRACE(("%s: brev = 0x%x boardrev = 0x%x \n", __FUNCTION__,
					brev, boardrev));
			}
			if (btype == boardtype && brev == boardrev) {
				/* locate nvram config set end - ie.find '\r' char */
				while (*nvram_end != BCM_JUMBO_NVRAM_DELIMIT)
					nvram_end ++;
				*nvram = nvram_start;
				*nvram_len = (int) (nvram_end - nvram_start);
				DBUSTRACE(("found len = %d nvram_start = 0x%p "
					"nvram_end = 0x%p\n", *nvram_len, nvram_start, nvram_end));
				return DBUS_OK;
			}

			len += (strlen(nvram_end) + 1);
			nvram_end += (strlen(nvram_end) + 1);
		}
	}
	return DBUS_JUMBO_NOMATCH;
} /* dbus_select_nvram */

#if defined(BCM_DNGL_EMBEDIMAGE)

/* store the global osh handle */
static osl_t *osl_handle = NULL;

/** this function is a combination of trx.c and bcmdl.c plus dbus adaptation */
static int
dbus_zlib_decomp(dbus_info_t *dbus_info)
{

	int method, flags, len, status;
	unsigned int uncmp_len, uncmp_crc, dec_crc, crc_init;
	struct trx_header *trx, *newtrx;
	unsigned char *file = NULL;
	unsigned char gz_magic[2] = {0x1f, 0x8b}; /* gzip magic header */
	z_stream d_stream;
	unsigned char unused;
	int actual_len = -1;
	unsigned char *headers;
	unsigned int trxhdrsize, nvramsize, decomp_memsize, i;

	(void)actual_len;
	(void)unused;
	(void)crc_init;

	osl_handle = dbus_info->pub.osh;
	dbus_info->orig_fw = NULL;

	headers = dbus_info->fw;
	/* Extract trx header */
	trx = (struct trx_header *)headers;
	trxhdrsize = sizeof(struct trx_header);

	if (ltoh32(trx->magic) != TRX_MAGIC) {
		DBUSERR(("%s: Error: trx bad hdr %x\n", __FUNCTION__,
			ltoh32(trx->magic)));
		return -1;
	}

	headers += sizeof(struct trx_header);

	if (ltoh32(trx->flag_version) & TRX_UNCOMP_IMAGE) {
		actual_len = ltoh32(trx->offsets[TRX_OFFSETS_DLFWLEN_IDX]) +
		                     sizeof(struct trx_header);
		DBUSERR(("%s: not a compressed image\n", __FUNCTION__));
		return 0;
	} else {
		/* Extract the gzip header info */
		if ((*headers++ != gz_magic[0]) || (*headers++ != gz_magic[1])) {
			DBUSERR(("%s: Error: gzip bad hdr\n", __FUNCTION__));
			return -1;
		}

		method = (int) *headers++;
		flags = (int) *headers++;

		if (method != Z_DEFLATED || (flags & RESERVED) != 0) {
			DBUSERR(("%s: Error: gzip bad hdr not a Z_DEFLATED file\n", __FUNCTION__));
			return -1;
		}
	}

	/* Discard time, xflags and OS code: */
	for (len = 0; len < 6; len++)
		unused = *headers++;

	if ((flags & EXTRA_FIELD) != 0) { /* skip the extra field */
		len = (uint32) *headers++;
		len += ((uint32)*headers++)<<8;
		/* len is garbage if EOF but the loop below will quit anyway */
		while (len-- != 0) unused = *headers++;
	}

	if ((flags & ORIG_NAME) != 0) { /* skip the original file name */
		while (*headers++ && (*headers != 0));
	}

	if ((flags & COMMENT) != 0) {   /* skip the .gz file comment */
		while (*headers++ && (*headers != 0));
	}

	if ((flags & HEAD_CRC) != 0) {  /* skip the header crc */
		for (len = 0; len < 2; len++) unused = *headers++;
	}

	headers++;


	/* create space for the uncompressed file */
	/* the space is for trx header, uncompressed image  and nvram file */
	/* with typical compression of 0.6, space double of firmware should be ok */

	decomp_memsize = dbus_info->fwlen * 2;
	dbus_info->decomp_memsize = decomp_memsize;
	if (!(file = MALLOC(osl_handle, decomp_memsize))) {
		DBUSERR(("%s: check_file : failed malloc\n", __FUNCTION__));
		goto err;
	}

	bzero(file, decomp_memsize);

	/* Initialise the decompression struct */
	d_stream.next_in = NULL;
	d_stream.avail_in = 0;
	d_stream.next_out = NULL;
	d_stream.avail_out = decomp_memsize - trxhdrsize;
	d_stream.zalloc = (alloc_func)0;
	d_stream.zfree = (free_func)0;
	if (inflateInit2(&d_stream, -15) != Z_OK) {
		DBUSERR(("%s: Err: inflateInit2\n", __FUNCTION__));
		goto err;
	}

	/* Inflate the code */
	d_stream.next_in = headers;
	d_stream.avail_in = ltoh32(trx->len);
	d_stream.next_out = (unsigned char*)(file + trxhdrsize);

	status = inflate(&d_stream, Z_SYNC_FLUSH);

	if (status != Z_STREAM_END)	{
		DBUSERR(("%s: Error: decompression failed\n", __FUNCTION__));
		goto err;
	}

	uncmp_crc = *d_stream.next_in++;
	uncmp_crc |= *d_stream.next_in++<<8;
	uncmp_crc |= *d_stream.next_in++<<16;
	uncmp_crc |= *d_stream.next_in++<<24;

	uncmp_len = *d_stream.next_in++;
	uncmp_len |= *d_stream.next_in++<<8;
	uncmp_len |= *d_stream.next_in++<<16;
	uncmp_len |= *d_stream.next_in++<<24;

	actual_len = (int) (d_stream.next_in - (unsigned char *)trx);

	inflateEnd(&d_stream);

	/* Do a CRC32 on the uncompressed data */
	crc_init = crc32(0L, Z_NULL, 0);
	dec_crc = crc32(crc_init, file + trxhdrsize, uncmp_len);

	if (dec_crc != uncmp_crc) {
		DBUSERR(("%s: decompression: bad crc check \n", __FUNCTION__));
		goto err;
	}
	else {
		DBUSTRACE(("%s: decompression: good crc check \n", __FUNCTION__));
	}

	/* rebuild the new trx header and calculate crc */
	newtrx = (struct trx_header *)file;
	newtrx->magic = trx->magic;
	/* add the uncompressed image flag */
	newtrx->flag_version = trx->flag_version;
	newtrx->flag_version  |= htol32(TRX_UNCOMP_IMAGE);
	newtrx->offsets[TRX_OFFSETS_DLFWLEN_IDX] = htol32(uncmp_len);
	newtrx->offsets[TRX_OFFSETS_JUMPTO_IDX] = trx->offsets[TRX_OFFSETS_JUMPTO_IDX];
	newtrx->offsets[TRX_OFFSETS_NVM_LEN_IDX] = trx->offsets[TRX_OFFSETS_NVM_LEN_IDX];

	nvramsize = ltoh32(trx->offsets[TRX_OFFSETS_NVM_LEN_IDX]);

	/* the original firmware has nvram file appended */
	/* copy the nvram file to uncompressed firmware */

	if (nvramsize) {
		if (nvramsize + uncmp_len > decomp_memsize) {
			DBUSERR(("%s: nvram cannot be accomodated\n", __FUNCTION__));
			goto err;
		}
		bcopy(d_stream.next_in, &file[uncmp_len], nvramsize);
		uncmp_len += nvramsize;
	}

	/* add trx header size to uncmp_len */
	uncmp_len += trxhdrsize;
	uncmp_len = ROUNDUP(uncmp_len, 4096);
	newtrx->len	= htol32(uncmp_len);

	/* Calculate CRC over header */
	newtrx->crc32 = hndcrc32((uint8 *)&newtrx->flag_version,
	sizeof(struct trx_header) - OFFSETOF(struct trx_header, flag_version),
	CRC32_INIT_VALUE);

	/* Calculate CRC over data */
	for (i = trxhdrsize; i < (uncmp_len); ++i)
				newtrx->crc32 = hndcrc32((uint8 *)&file[i], 1, newtrx->crc32);
	newtrx->crc32 = htol32(newtrx->crc32);

	dbus_info->orig_fw = dbus_info->fw;
	dbus_info->origfw_len = dbus_info->fwlen;
	dbus_info->image = dbus_info->fw = file;
	dbus_info->image_len = dbus_info->fwlen = uncmp_len;

	return 0;

err:
	if (file)
		free(file);
	return -1;
} /* dbus_zlib_decomp */

void *
dbus_zlib_calloc(int num, int size)
{
	uint *ptr;
	uint totalsize;

	if (osl_handle == NULL)
		return NULL;

	totalsize = (num * (size + 1));

	ptr  = MALLOC(osl_handle, totalsize);

	if (ptr == NULL)
		return NULL;
	bzero(ptr, totalsize);

	/* store the size in the first integer space */

	ptr[0] = totalsize;

	return ((void *) &ptr[1]);
}

void
dbus_zlib_free(void *ptr)
{
	uint totalsize;
	uchar *memptr = (uchar *)ptr;

	if (ptr && osl_handle) {
		memptr -= sizeof(uint);
		totalsize = *(uint *) memptr;
		MFREE(osl_handle, memptr, totalsize);
	}
}

#endif /* #if defined(BCM_DNGL_EMBEDIMAGE) */
#endif /* #if defined(BCM_DNGL_EMBEDIMAGE) || defined(BCM_REQUEST_FW) */


#ifdef LINUX_EXTERNAL_MODULE_DBUS

static int __init
bcm_dbus_module_init(void)
{
	printf("Inserting bcm_dbus module \n");
	return 0;
}

static void __exit
bcm_dbus_module_exit(void)
{
	printf("Removing bcm_dbus module \n");
	return;
}

EXPORT_SYMBOL(dbus_pnp_sleep);
EXPORT_SYMBOL(dbus_register);
EXPORT_SYMBOL(dbus_get_devinfo);
EXPORT_SYMBOL(dbus_detach);
EXPORT_SYMBOL(dbus_get_attrib);
EXPORT_SYMBOL(dbus_down);
EXPORT_SYMBOL(dbus_pnp_resume);
EXPORT_SYMBOL(dbus_set_config);
EXPORT_SYMBOL(dbus_flowctrl_rx);
EXPORT_SYMBOL(dbus_up);
EXPORT_SYMBOL(dbus_get_device_speed);
EXPORT_SYMBOL(dbus_send_pkt);
EXPORT_SYMBOL(dbus_recv_ctl);
EXPORT_SYMBOL(dbus_attach);
EXPORT_SYMBOL(dbus_deregister);

MODULE_LICENSE("GPL");

module_init(bcm_dbus_module_init);
module_exit(bcm_dbus_module_exit);

#endif  /* #ifdef LINUX_EXTERNAL_MODULE_DBUS */
