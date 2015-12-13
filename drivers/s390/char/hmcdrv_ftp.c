/*
 *    HMC Drive FTP Services
 *
 *    Copyright IBM Corp. 2013
 *    Author(s): Ralf Hoppe (rhoppe@de.ibm.com)
 */

#define KMSG_COMPONENT "hmcdrv"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/export.h>

#include <linux/ctype.h>
#include <linux/crc16.h>

#include "hmcdrv_ftp.h"
#include "hmcdrv_cache.h"
#include "sclp_ftp.h"
#include "diag_ftp.h"

/**
 * struct hmcdrv_ftp_ops - HMC drive FTP operations
 * @startup: startup function
 * @shutdown: shutdown function
 * @cmd: FTP transfer function
 */
struct hmcdrv_ftp_ops {
	int (*startup)(void);
	void (*shutdown)(void);
	ssize_t (*transfer)(const struct hmcdrv_ftp_cmdspec *ftp,
			    size_t *fsize);
};

static enum hmcdrv_ftp_cmdid hmcdrv_ftp_cmd_getid(const char *cmd, int len);
static int hmcdrv_ftp_parse(char *cmd, struct hmcdrv_ftp_cmdspec *ftp);

static const struct hmcdrv_ftp_ops *hmcdrv_ftp_funcs; /* current operations */
static DEFINE_MUTEX(hmcdrv_ftp_mutex); /* mutex for hmcdrv_ftp_funcs */
static unsigned hmcdrv_ftp_refcnt; /* start/shutdown reference counter */

/**
 * hmcdrv_ftp_cmd_getid() - determine FTP command ID from a command string
 * @cmd: FTP command string (NOT zero-terminated)
 * @len: length of FTP command string in @cmd
 */
static enum hmcdrv_ftp_cmdid hmcdrv_ftp_cmd_getid(const char *cmd, int len)
{
	/* HMC FTP command descriptor */
	struct hmcdrv_ftp_cmd_desc {
		const char *str;	   /* command string */
		enum hmcdrv_ftp_cmdid cmd; /* associated command as enum */
	};

	/* Description of all HMC drive FTP commands
	 *
	 * Notes:
	 * 1. Array size should be a prime number.
	 * 2. Do not change the order of commands in table (because the
	 *    index is determined by CRC % ARRAY_SIZE).
	 * 3. Original command 'nlist' was renamed, else the CRC would
	 *    collide with 'append' (see point 2).
	 */
	static const struct hmcdrv_ftp_cmd_desc ftpcmds[7] = {

		{.str = "get", /* [0] get (CRC = 0x68eb) */
		 .cmd = HMCDRV_FTP_GET},
		{.str = "dir", /* [1] dir (CRC = 0x6a9e) */
		 .cmd = HMCDRV_FTP_DIR},
		{.str = "delete", /* [2] delete (CRC = 0x53ae) */
		 .cmd = HMCDRV_FTP_DELETE},
		{.str = "nls", /* [3] nls (CRC = 0xf87c) */
		 .cmd = HMCDRV_FTP_NLIST},
		{.str = "put", /* [4] put (CRC = 0xac56) */
		 .cmd = HMCDRV_FTP_PUT},
		{.str = "append", /* [5] append (CRC = 0xf56e) */
		 .cmd = HMCDRV_FTP_APPEND},
		{.str = NULL} /* [6] unused */
	};

	const struct hmcdrv_ftp_cmd_desc *pdesc;

	u16 crc = 0xffffU;

	if (len == 0)
		return HMCDRV_FTP_NOOP; /* error indiactor */

	crc = crc16(crc, cmd, len);
	pdesc = ftpcmds + (crc % ARRAY_SIZE(ftpcmds));
	pr_debug("FTP command '%s' has CRC 0x%04x, at table pos. %lu\n",
		 cmd, crc, (crc % ARRAY_SIZE(ftpcmds)));

	if (!pdesc->str || strncmp(pdesc->str, cmd, len))
		return HMCDRV_FTP_NOOP;

	pr_debug("FTP command '%s' found, with ID %d\n",
		 pdesc->str, pdesc->cmd);

	return pdesc->cmd;
}

/**
 * hmcdrv_ftp_parse() - HMC drive FTP command parser
 * @cmd: FTP command string "<cmd> <filename>"
 * @ftp: Pointer to FTP command specification buffer (output)
 *
 * Return: 0 on success, else a (negative) error code
 */
static int hmcdrv_ftp_parse(char *cmd, struct hmcdrv_ftp_cmdspec *ftp)
{
	char *start;
	int argc = 0;

	ftp->id = HMCDRV_FTP_NOOP;
	ftp->fname = NULL;

	while (*cmd != '\0') {

		while (isspace(*cmd))
			++cmd;

		if (*cmd == '\0')
			break;

		start = cmd;

		switch (argc) {
		case 0: /* 1st argument (FTP command) */
			while ((*cmd != '\0') && !isspace(*cmd))
				++cmd;
			ftp->id = hmcdrv_ftp_cmd_getid(start, cmd - start);
			break;
		case 1: /* 2nd / last argument (rest of line) */
			while ((*cmd != '\0') && !iscntrl(*cmd))
				++cmd;
			ftp->fname = start;
			/* fall through */
		default:
			*cmd = '\0';
			break;
		} /* switch */

		++argc;
	} /* while */

	if (!ftp->fname || (ftp->id == HMCDRV_FTP_NOOP))
		return -EINVAL;

	return 0;
}

/**
 * hmcdrv_ftp_do() - perform a HMC drive FTP, with data from kernel-space
 * @ftp: pointer to FTP command specification
 *
 * Return: number of bytes read/written or a negative error code
 */
ssize_t hmcdrv_ftp_do(const struct hmcdrv_ftp_cmdspec *ftp)
{
	ssize_t len;

	mutex_lock(&hmcdrv_ftp_mutex);

	if (hmcdrv_ftp_funcs && hmcdrv_ftp_refcnt) {
		pr_debug("starting transfer, cmd %d for '%s' at %lld with %zd bytes\n",
			 ftp->id, ftp->fname, (long long) ftp->ofs, ftp->len);
		len = hmcdrv_cache_cmd(ftp, hmcdrv_ftp_funcs->transfer);
	} else {
		len = -ENXIO;
	}

	mutex_unlock(&hmcdrv_ftp_mutex);
	return len;
}
EXPORT_SYMBOL(hmcdrv_ftp_do);

/**
 * hmcdrv_ftp_probe() - probe for the HMC drive FTP service
 *
 * Return: 0 if service is available, else an (negative) error code
 */
int hmcdrv_ftp_probe(void)
{
	int rc;

	struct hmcdrv_ftp_cmdspec ftp = {
		.id = HMCDRV_FTP_NOOP,
		.ofs = 0,
		.fname = "",
		.len = PAGE_SIZE
	};

	ftp.buf = (void *) get_zeroed_page(GFP_KERNEL | GFP_DMA);

	if (!ftp.buf)
		return -ENOMEM;

	rc = hmcdrv_ftp_startup();

	if (rc)
		goto out;

	rc = hmcdrv_ftp_do(&ftp);
	hmcdrv_ftp_shutdown();

	switch (rc) {
	case -ENOENT: /* no such file/media or currently busy, */
	case -EBUSY:  /* but service seems to be available */
		rc = 0;
		break;
	default: /* leave 'rc' as it is for [0, -EPERM, -E...] */
		if (rc > 0)
			rc = 0; /* clear length (success) */
		break;
	} /* switch */
out:
	free_page((unsigned long) ftp.buf);
	return rc;
}
EXPORT_SYMBOL(hmcdrv_ftp_probe);

/**
 * hmcdrv_ftp_cmd() - Perform a HMC drive FTP, with data from user-space
 *
 * @cmd: FTP command string "<cmd> <filename>"
 * @offset: file position to read/write
 * @buf: user-space buffer for read/written directory/file
 * @len: size of @buf (read/dir) or number of bytes to write
 *
 * This function must not be called before hmcdrv_ftp_startup() was called.
 *
 * Return: number of bytes read/written or a negative error code
 */
ssize_t hmcdrv_ftp_cmd(char __kernel *cmd, loff_t offset,
		       char __user *buf, size_t len)
{
	int order;

	struct hmcdrv_ftp_cmdspec ftp = {.len = len, .ofs = offset};
	ssize_t retlen = hmcdrv_ftp_parse(cmd, &ftp);

	if (retlen)
		return retlen;

	order = get_order(ftp.len);
	ftp.buf = (void *) __get_free_pages(GFP_KERNEL | GFP_DMA, order);

	if (!ftp.buf)
		return -ENOMEM;

	switch (ftp.id) {
	case HMCDRV_FTP_DIR:
	case HMCDRV_FTP_NLIST:
	case HMCDRV_FTP_GET:
		retlen = hmcdrv_ftp_do(&ftp);

		if ((retlen >= 0) &&
		    copy_to_user(buf, ftp.buf, retlen))
			retlen = -EFAULT;
		break;

	case HMCDRV_FTP_PUT:
	case HMCDRV_FTP_APPEND:
		if (!copy_from_user(ftp.buf, buf, ftp.len))
			retlen = hmcdrv_ftp_do(&ftp);
		else
			retlen = -EFAULT;
		break;

	case HMCDRV_FTP_DELETE:
		retlen = hmcdrv_ftp_do(&ftp);
		break;

	default:
		retlen = -EOPNOTSUPP;
		break;
	}

	free_pages((unsigned long) ftp.buf, order);
	return retlen;
}

/**
 * hmcdrv_ftp_startup() - startup of HMC drive FTP functionality for a
 * dedicated (owner) instance
 *
 * Return: 0 on success, else an (negative) error code
 */
int hmcdrv_ftp_startup(void)
{
	static const struct hmcdrv_ftp_ops hmcdrv_ftp_zvm = {
		.startup = diag_ftp_startup,
		.shutdown = diag_ftp_shutdown,
		.transfer = diag_ftp_cmd
	};

	static const struct hmcdrv_ftp_ops hmcdrv_ftp_lpar = {
		.startup = sclp_ftp_startup,
		.shutdown = sclp_ftp_shutdown,
		.transfer = sclp_ftp_cmd
	};

	int rc = 0;

	mutex_lock(&hmcdrv_ftp_mutex); /* block transfers while start-up */

	if (hmcdrv_ftp_refcnt == 0) {
		if (MACHINE_IS_VM)
			hmcdrv_ftp_funcs = &hmcdrv_ftp_zvm;
		else if (MACHINE_IS_LPAR || MACHINE_IS_KVM)
			hmcdrv_ftp_funcs = &hmcdrv_ftp_lpar;
		else
			rc = -EOPNOTSUPP;

		if (hmcdrv_ftp_funcs)
			rc = hmcdrv_ftp_funcs->startup();
	}

	if (!rc)
		++hmcdrv_ftp_refcnt;

	mutex_unlock(&hmcdrv_ftp_mutex);
	return rc;
}
EXPORT_SYMBOL(hmcdrv_ftp_startup);

/**
 * hmcdrv_ftp_shutdown() - shutdown of HMC drive FTP functionality for a
 * dedicated (owner) instance
 */
void hmcdrv_ftp_shutdown(void)
{
	mutex_lock(&hmcdrv_ftp_mutex);
	--hmcdrv_ftp_refcnt;

	if ((hmcdrv_ftp_refcnt == 0) && hmcdrv_ftp_funcs)
		hmcdrv_ftp_funcs->shutdown();

	mutex_unlock(&hmcdrv_ftp_mutex);
}
EXPORT_SYMBOL(hmcdrv_ftp_shutdown);
