// SPDX-License-Identifier: GPL-2.0
/*
 *    DIAGNOSE X'2C4' instruction based HMC FTP services, useable on z/VM
 *
 *    Copyright IBM Corp. 2013
 *    Author(s): Ralf Hoppe (rhoppe@de.ibm.com)
 *
 */

#define KMSG_COMPONENT "hmcdrv"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/irq.h>
#include <linux/wait.h>
#include <linux/string.h>
#include <asm/asm-extable.h>
#include <asm/ctl_reg.h>
#include <asm/diag.h>

#include "hmcdrv_ftp.h"
#include "diag_ftp.h"

/* DIAGNOSE X'2C4' return codes in Ry */
#define DIAG_FTP_RET_OK	0 /* HMC FTP started successfully */
#define DIAG_FTP_RET_EBUSY	4 /* HMC FTP service currently busy */
#define DIAG_FTP_RET_EIO	8 /* HMC FTP service I/O error */
/* and an artificial extension */
#define DIAG_FTP_RET_EPERM	2 /* HMC FTP service privilege error */

/* FTP service status codes (after INTR at guest real location 133) */
#define DIAG_FTP_STAT_OK	0U /* request completed successfully */
#define DIAG_FTP_STAT_PGCC	4U /* program check condition */
#define DIAG_FTP_STAT_PGIOE	8U /* paging I/O error */
#define DIAG_FTP_STAT_TIMEOUT	12U /* timeout */
#define DIAG_FTP_STAT_EBASE	16U /* base of error codes from SCLP */
#define DIAG_FTP_STAT_LDFAIL	(DIAG_FTP_STAT_EBASE + 1U) /* failed */
#define DIAG_FTP_STAT_LDNPERM	(DIAG_FTP_STAT_EBASE + 2U) /* not allowed */
#define DIAG_FTP_STAT_LDRUNS	(DIAG_FTP_STAT_EBASE + 3U) /* runs */
#define DIAG_FTP_STAT_LDNRUNS	(DIAG_FTP_STAT_EBASE + 4U) /* not runs */

/**
 * struct diag_ftp_ldfpl - load file FTP parameter list (LDFPL)
 * @bufaddr: real buffer address (at 4k boundary)
 * @buflen: length of buffer
 * @offset: dir/file offset
 * @intparm: interruption parameter (unused)
 * @transferred: bytes transferred
 * @fsize: file size, filled on GET
 * @failaddr: failing address
 * @spare: padding
 * @fident: file name - ASCII
 */
struct diag_ftp_ldfpl {
	u64 bufaddr;
	u64 buflen;
	u64 offset;
	u64 intparm;
	u64 transferred;
	u64 fsize;
	u64 failaddr;
	u64 spare;
	u8 fident[HMCDRV_FTP_FIDENT_MAX];
} __packed;

static DECLARE_COMPLETION(diag_ftp_rx_complete);
static int diag_ftp_subcode;

/**
 * diag_ftp_handler() - FTP services IRQ handler
 * @extirq: external interrupt (sub-) code
 * @param32: 32-bit interruption parameter from &struct diag_ftp_ldfpl
 * @param64: unused (for 64-bit interrupt parameters)
 */
static void diag_ftp_handler(struct ext_code extirq,
			     unsigned int param32,
			     unsigned long param64)
{
	if ((extirq.subcode >> 8) != 8)
		return; /* not a FTP services sub-code */

	inc_irq_stat(IRQEXT_FTP);
	diag_ftp_subcode = extirq.subcode & 0xffU;
	complete(&diag_ftp_rx_complete);
}

/**
 * diag_ftp_2c4() - DIAGNOSE X'2C4' service call
 * @fpl: pointer to prepared LDFPL
 * @cmd: FTP command to be executed
 *
 * Performs a DIAGNOSE X'2C4' call with (input/output) FTP parameter list
 * @fpl and FTP function code @cmd. In case of an error the function does
 * nothing and returns an (negative) error code.
 *
 * Notes:
 * 1. This function only initiates a transfer, so the caller must wait
 *    for completion (asynchronous execution).
 * 2. The FTP parameter list @fpl must be aligned to a double-word boundary.
 * 3. fpl->bufaddr must be a real address, 4k aligned
 */
static int diag_ftp_2c4(struct diag_ftp_ldfpl *fpl,
			enum hmcdrv_ftp_cmdid cmd)
{
	int rc;

	diag_stat_inc(DIAG_STAT_X2C4);
	asm volatile(
		"	diag	%[addr],%[cmd],0x2c4\n"
		"0:	j	2f\n"
		"1:	la	%[rc],%[err]\n"
		"2:\n"
		EX_TABLE(0b, 1b)
		: [rc] "=d" (rc), "+m" (*fpl)
		: [cmd] "0" (cmd), [addr] "d" (virt_to_phys(fpl)),
		  [err] "i" (DIAG_FTP_RET_EPERM)
		: "cc");

	switch (rc) {
	case DIAG_FTP_RET_OK:
		return 0;
	case DIAG_FTP_RET_EBUSY:
		return -EBUSY;
	case DIAG_FTP_RET_EPERM:
		return -EPERM;
	case DIAG_FTP_RET_EIO:
	default:
		return -EIO;
	}
}

/**
 * diag_ftp_cmd() - executes a DIAG X'2C4' FTP command, targeting a HMC
 * @ftp: pointer to FTP command specification
 * @fsize: return of file size (or NULL if undesirable)
 *
 * Attention: Notice that this function is not reentrant - so the caller
 * must ensure locking.
 *
 * Return: number of bytes read/written or a (negative) error code
 */
ssize_t diag_ftp_cmd(const struct hmcdrv_ftp_cmdspec *ftp, size_t *fsize)
{
	struct diag_ftp_ldfpl *ldfpl;
	ssize_t len;
#ifdef DEBUG
	unsigned long start_jiffies;

	pr_debug("starting DIAG X'2C4' on '%s', requesting %zd bytes\n",
		 ftp->fname, ftp->len);
	start_jiffies = jiffies;
#endif
	init_completion(&diag_ftp_rx_complete);

	ldfpl = (void *) get_zeroed_page(GFP_KERNEL | GFP_DMA);
	if (!ldfpl) {
		len = -ENOMEM;
		goto out;
	}

	len = strscpy(ldfpl->fident, ftp->fname, sizeof(ldfpl->fident));
	if (len < 0) {
		len = -EINVAL;
		goto out_free;
	}

	ldfpl->transferred = 0;
	ldfpl->fsize = 0;
	ldfpl->offset = ftp->ofs;
	ldfpl->buflen = ftp->len;
	ldfpl->bufaddr = virt_to_phys(ftp->buf);

	len = diag_ftp_2c4(ldfpl, ftp->id);
	if (len)
		goto out_free;

	/*
	 * There is no way to cancel the running diag X'2C4', the code
	 * needs to wait unconditionally until the transfer is complete.
	 */
	wait_for_completion(&diag_ftp_rx_complete);

#ifdef DEBUG
	pr_debug("completed DIAG X'2C4' after %lu ms\n",
		 (jiffies - start_jiffies) * 1000 / HZ);
	pr_debug("status of DIAG X'2C4' is %u, with %lld/%lld bytes\n",
		 diag_ftp_subcode, ldfpl->transferred, ldfpl->fsize);
#endif

	switch (diag_ftp_subcode) {
	case DIAG_FTP_STAT_OK: /* success */
		len = ldfpl->transferred;
		if (fsize)
			*fsize = ldfpl->fsize;
		break;
	case DIAG_FTP_STAT_LDNPERM:
		len = -EPERM;
		break;
	case DIAG_FTP_STAT_LDRUNS:
		len = -EBUSY;
		break;
	case DIAG_FTP_STAT_LDFAIL:
		len = -ENOENT; /* no such file or media */
		break;
	default:
		len = -EIO;
		break;
	}

out_free:
	free_page((unsigned long) ldfpl);
out:
	return len;
}

/**
 * diag_ftp_startup() - startup of FTP services, when running on z/VM
 *
 * Return: 0 on success, else an (negative) error code
 */
int diag_ftp_startup(void)
{
	int rc;

	rc = register_external_irq(EXT_IRQ_CP_SERVICE, diag_ftp_handler);
	if (rc)
		return rc;

	irq_subclass_register(IRQ_SUBCLASS_SERVICE_SIGNAL);
	return 0;
}

/**
 * diag_ftp_shutdown() - shutdown of FTP services, when running on z/VM
 */
void diag_ftp_shutdown(void)
{
	irq_subclass_unregister(IRQ_SUBCLASS_SERVICE_SIGNAL);
	unregister_external_irq(EXT_IRQ_CP_SERVICE, diag_ftp_handler);
}
