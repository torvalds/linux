/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include <linux/types.h>
#include <linux/netdevice.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/core.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/card.h>
#include <linux/suspend.h>
#include <linux/errno.h>
#include <linux/sched.h>	/* request_irq() */
#include <net/cfg80211.h>

#include <defs.h>
#include <brcm_hw_ids.h>
#include <brcmu_utils.h>
#include <brcmu_wifi.h>
#include "sdio_host.h"
#include "dhd.h"
#include "dhd_dbg.h"
#include "wl_cfg80211.h"

#define BLOCK_SIZE_64 64
#define BLOCK_SIZE_512 512
#define BLOCK_SIZE_4318 64
#define BLOCK_SIZE_4328 512

/* private bus modes */
#define SDIOH_MODE_SD4		2

#define CLIENT_INTR		0x100	/* Get rid of this! */

#if !defined(SDIO_VENDOR_ID_BROADCOM)
#define SDIO_VENDOR_ID_BROADCOM		0x02d0
#endif				/* !defined(SDIO_VENDOR_ID_BROADCOM) */

#define SDIO_DEVICE_ID_BROADCOM_DEFAULT	0x0000

#define DMA_ALIGN_MASK	0x03

#if !defined(SDIO_DEVICE_ID_BROADCOM_4325_SDGWB)
#define SDIO_DEVICE_ID_BROADCOM_4325_SDGWB	0x0492	/* BCM94325SDGWB */
#endif		/* !defined(SDIO_DEVICE_ID_BROADCOM_4325_SDGWB) */
#if !defined(SDIO_DEVICE_ID_BROADCOM_4325)
#define SDIO_DEVICE_ID_BROADCOM_4325	0x0493
#endif		/* !defined(SDIO_DEVICE_ID_BROADCOM_4325) */
#if !defined(SDIO_DEVICE_ID_BROADCOM_4329)
#define SDIO_DEVICE_ID_BROADCOM_4329	0x4329
#endif		/* !defined(SDIO_DEVICE_ID_BROADCOM_4329) */
#if !defined(SDIO_DEVICE_ID_BROADCOM_4319)
#define SDIO_DEVICE_ID_BROADCOM_4319	0x4319
#endif		/* !defined(SDIO_DEVICE_ID_BROADCOM_4329) */

/* Common msglevel constants */
#define SDH_ERROR_VAL		0x0001	/* Error */
#define SDH_TRACE_VAL		0x0002	/* Trace */
#define SDH_INFO_VAL		0x0004	/* Info */
#define SDH_DEBUG_VAL		0x0008	/* Debug */
#define SDH_DATA_VAL		0x0010	/* Data */
#define SDH_CTRL_VAL		0x0020	/* Control Regs */
#define SDH_LOG_VAL		0x0040	/* Enable bcmlog */
#define SDH_DMA_VAL		0x0080	/* DMA */

#ifdef BCMDBG
#define sd_err(x)	\
	do { \
		if ((sd_msglevel & SDH_ERROR_VAL) && net_ratelimit()) \
			printk x; \
	} while (0)
#define sd_trace(x)	\
	do { \
		if ((sd_msglevel & SDH_TRACE_VAL) && net_ratelimit()) \
			printk x; \
	} while (0)
#define sd_info(x)	\
	do { \
		if ((sd_msglevel & SDH_INFO_VAL) && net_ratelimit()) \
			printk x; \
	} while (0)
#define sd_debug(x)	\
	do { \
		if ((sd_msglevel & SDH_DEBUG_VAL) && net_ratelimit()) \
			printk x; \
	} while (0)
#define sd_data(x)	\
	do { \
		if ((sd_msglevel & SDH_DATA_VAL) && net_ratelimit()) \
			printk x; \
	} while (0)
#define sd_ctrl(x)	\
	do { \
		if ((sd_msglevel & SDH_CTRL_VAL) && net_ratelimit()) \
			printk x; \
	} while (0)
#else
#define sd_err(x)
#define sd_trace(x)
#define sd_info(x)
#define sd_debug(x)
#define sd_data(x)
#define sd_ctrl(x)
#endif

struct sdos_info {
	struct sdioh_info *sd;
	spinlock_t lock;
};

static void brcmf_sdioh_irqhandler(struct sdio_func *func);
static void brcmf_sdioh_irqhandler_f2(struct sdio_func *func);
static int brcmf_sdioh_get_cisaddr(struct sdioh_info *sd, u32 regaddr);
static int brcmf_ops_sdio_probe(struct sdio_func *func,
				const struct sdio_device_id *id);
static void brcmf_ops_sdio_remove(struct sdio_func *func);

#ifdef CONFIG_PM
static int brcmf_sdio_suspend(struct device *dev);
static int brcmf_sdio_resume(struct device *dev);
#endif /* CONFIG_PM */

uint sd_f2_blocksize = 512;	/* Default blocksize */

uint sd_msglevel = 0x01;

/* module param defaults */
static int clockoverride;

module_param(clockoverride, int, 0644);
MODULE_PARM_DESC(clockoverride, "SDIO card clock override");

struct brcmf_sdmmc_instance *gInstance;

struct device sdmmc_dev;

/* devices we support, null terminated */
static const struct sdio_device_id brcmf_sdmmc_ids[] = {
	{SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, SDIO_DEVICE_ID_BROADCOM_DEFAULT)},
	{SDIO_DEVICE
	 (SDIO_VENDOR_ID_BROADCOM, SDIO_DEVICE_ID_BROADCOM_4325_SDGWB)},
	{SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, SDIO_DEVICE_ID_BROADCOM_4325)},
	{SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, SDIO_DEVICE_ID_BROADCOM_4329)},
	{SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, SDIO_DEVICE_ID_BROADCOM_4319)},
	{ /* end: all zeroes */ },
};

#ifdef CONFIG_PM
static const struct dev_pm_ops brcmf_sdio_pm_ops = {
	.suspend	= brcmf_sdio_suspend,
	.resume		= brcmf_sdio_resume,
};
#endif	/* CONFIG_PM */

static struct sdio_driver brcmf_sdmmc_driver = {
	.probe = brcmf_ops_sdio_probe,
	.remove = brcmf_ops_sdio_remove,
	.name = "brcmfmac",
	.id_table = brcmf_sdmmc_ids,
#ifdef CONFIG_PM
	.drv = {
		.pm = &brcmf_sdio_pm_ops,
	},
#endif	/* CONFIG_PM */
};

MODULE_DEVICE_TABLE(sdio, brcmf_sdmmc_ids);

BRCMF_PM_RESUME_WAIT_INIT(sdioh_request_byte_wait);
BRCMF_PM_RESUME_WAIT_INIT(sdioh_request_word_wait);
BRCMF_PM_RESUME_WAIT_INIT(sdioh_request_packet_wait);
BRCMF_PM_RESUME_WAIT_INIT(sdioh_request_buffer_wait);

static int
brcmf_sdioh_card_regread(struct sdioh_info *sd, int func, u32 regaddr,
			 int regsize, u32 *data);

static int brcmf_sdioh_enablefuncs(struct sdioh_info *sd)
{
	int err_ret;
	u32 fbraddr;
	u8 func;

	sd_trace(("%s\n", __func__));

	/* Get the Card's common CIS address */
	sd->com_cis_ptr = brcmf_sdioh_get_cisaddr(sd, SDIO_CCCR_CIS);
	sd->func_cis_ptr[0] = sd->com_cis_ptr;
	sd_info(("%s: Card's Common CIS Ptr = 0x%x\n", __func__,
		 sd->com_cis_ptr));

	/* Get the Card's function CIS (for each function) */
	for (fbraddr = SDIO_FBR_BASE(1), func = 1;
	     func <= sd->num_funcs; func++, fbraddr += SDIOD_FBR_SIZE) {
		sd->func_cis_ptr[func] =
		    brcmf_sdioh_get_cisaddr(sd, SDIO_FBR_CIS + fbraddr);
		sd_info(("%s: Function %d CIS Ptr = 0x%x\n", __func__, func,
			 sd->func_cis_ptr[func]));
	}

	sd->func_cis_ptr[0] = sd->com_cis_ptr;
	sd_info(("%s: Card's Common CIS Ptr = 0x%x\n", __func__,
		 sd->com_cis_ptr));

	/* Enable Function 1 */
	sdio_claim_host(gInstance->func[1]);
	err_ret = sdio_enable_func(gInstance->func[1]);
	sdio_release_host(gInstance->func[1]);
	if (err_ret) {
		sd_err(("brcmf_sdioh_enablefuncs: Failed to enable F1 "
			"Err: 0x%08x\n", err_ret));
	}

	return false;
}

/*
 *	Public entry points & extern's
 */
struct sdioh_info *brcmf_sdioh_attach(void *bar0, uint irq)
{
	struct sdioh_info *sd;
	int err_ret;

	sd_trace(("%s\n", __func__));

	if (gInstance == NULL) {
		sd_err(("%s: SDIO Device not present\n", __func__));
		return NULL;
	}

	sd = kzalloc(sizeof(struct sdioh_info), GFP_ATOMIC);
	if (sd == NULL) {
		sd_err(("sdioh_attach: out of memory\n"));
		return NULL;
	}
	if (brcmf_sdioh_osinit(sd) != 0) {
		sd_err(("%s:sdioh_sdmmc_osinit() failed\n", __func__));
		kfree(sd);
		return NULL;
	}

	sd->num_funcs = 2;
	sd->use_client_ints = true;
	sd->client_block_size[0] = 64;

	gInstance->sd = sd;

	/* Claim host controller */
	sdio_claim_host(gInstance->func[1]);

	sd->client_block_size[1] = 64;
	err_ret = sdio_set_block_size(gInstance->func[1], 64);
	if (err_ret)
		sd_err(("brcmf_sdioh_attach: Failed to set F1 blocksize\n"));

	/* Release host controller F1 */
	sdio_release_host(gInstance->func[1]);

	if (gInstance->func[2]) {
		/* Claim host controller F2 */
		sdio_claim_host(gInstance->func[2]);

		sd->client_block_size[2] = sd_f2_blocksize;
		err_ret =
		    sdio_set_block_size(gInstance->func[2], sd_f2_blocksize);
		if (err_ret)
			sd_err(("brcmf_sdioh_attach: Failed to set F2 blocksize"
				" to %d\n", sd_f2_blocksize));

		/* Release host controller F2 */
		sdio_release_host(gInstance->func[2]);
	}

	brcmf_sdioh_enablefuncs(sd);

	sd_trace(("%s: Done\n", __func__));
	return sd;
}

extern int brcmf_sdioh_detach(struct sdioh_info *sd)
{
	sd_trace(("%s\n", __func__));

	if (sd) {

		/* Disable Function 2 */
		sdio_claim_host(gInstance->func[2]);
		sdio_disable_func(gInstance->func[2]);
		sdio_release_host(gInstance->func[2]);

		/* Disable Function 1 */
		sdio_claim_host(gInstance->func[1]);
		sdio_disable_func(gInstance->func[1]);
		sdio_release_host(gInstance->func[1]);

		/* deregister irq */
		brcmf_sdioh_osfree(sd);

		kfree(sd);
	}
	return 0;
}

/* Configure callback to client when we receive client interrupt */
extern int
brcmf_sdioh_interrupt_register(struct sdioh_info *sd, void (*fn)(void *),
			       void *argh)
{
	sd_trace(("%s: Entering\n", __func__));
	if (fn == NULL) {
		sd_err(("%s: interrupt handler is NULL, not registering\n",
			__func__));
		return -EINVAL;
	}

	sd->intr_handler = fn;
	sd->intr_handler_arg = argh;
	sd->intr_handler_valid = true;

	/* register and unmask irq */
	if (gInstance->func[2]) {
		sdio_claim_host(gInstance->func[2]);
		sdio_claim_irq(gInstance->func[2], brcmf_sdioh_irqhandler_f2);
		sdio_release_host(gInstance->func[2]);
	}

	if (gInstance->func[1]) {
		sdio_claim_host(gInstance->func[1]);
		sdio_claim_irq(gInstance->func[1], brcmf_sdioh_irqhandler);
		sdio_release_host(gInstance->func[1]);
	}

	return 0;
}

extern int brcmf_sdioh_interrupt_deregister(struct sdioh_info *sd)
{
	sd_trace(("%s: Entering\n", __func__));

	if (gInstance->func[1]) {
		/* register and unmask irq */
		sdio_claim_host(gInstance->func[1]);
		sdio_release_irq(gInstance->func[1]);
		sdio_release_host(gInstance->func[1]);
	}

	if (gInstance->func[2]) {
		/* Claim host controller F2 */
		sdio_claim_host(gInstance->func[2]);
		sdio_release_irq(gInstance->func[2]);
		/* Release host controller F2 */
		sdio_release_host(gInstance->func[2]);
	}

	sd->intr_handler_valid = false;
	sd->intr_handler = NULL;
	sd->intr_handler_arg = NULL;

	return 0;
}

/* IOVar table */
enum {
	IOV_MSGLEVEL = 1,
	IOV_BLOCKSIZE,
	IOV_USEINTS,
	IOV_NUMINTS,
	IOV_DEVREG,
	IOV_HCIREGS,
	IOV_RXCHAIN
};

const struct brcmu_iovar sdioh_iovars[] = {
	{"sd_msglevel", IOV_MSGLEVEL, 0, IOVT_UINT32, 0},
	{"sd_blocksize", IOV_BLOCKSIZE, 0, IOVT_UINT32, 0},/* ((fn << 16) |
								 size) */
	{"sd_ints", IOV_USEINTS, 0, IOVT_BOOL, 0},
	{"sd_numints", IOV_NUMINTS, 0, IOVT_UINT32, 0},
	{"sd_devreg", IOV_DEVREG, 0, IOVT_BUFFER, sizeof(struct brcmf_sdreg)}
	,
	{"sd_rxchain", IOV_RXCHAIN, 0, IOVT_BOOL, 0}
	,
	{NULL, 0, 0, 0, 0}
};

int
brcmf_sdioh_iovar_op(struct sdioh_info *si, const char *name,
		     void *params, int plen, void *arg, int len, bool set)
{
	const struct brcmu_iovar *vi = NULL;
	int bcmerror = 0;
	int val_size;
	s32 int_val = 0;
	bool bool_val;
	u32 actionid;

	if (name == NULL || len <= 0)
		return -EINVAL;

	/* Set does not take qualifiers */
	if (set && (params || plen))
		return -EINVAL;

	/* Get must have return space;*/
	if (!set && !(arg && len))
		return -EINVAL;

	sd_trace(("%s: Enter (%s %s)\n", __func__, (set ? "set" : "get"),
		  name));

	vi = brcmu_iovar_lookup(sdioh_iovars, name);
	if (vi == NULL) {
		bcmerror = -ENOTSUPP;
		goto exit;
	}

	bcmerror = brcmu_iovar_lencheck(vi, arg, len, set);
	if (bcmerror != 0)
		goto exit;

	/* Set up params so get and set can share the convenience variables */
	if (params == NULL) {
		params = arg;
		plen = len;
	}

	if (vi->type == IOVT_VOID)
		val_size = 0;
	else if (vi->type == IOVT_BUFFER)
		val_size = len;
	else
		val_size = sizeof(int);

	if (plen >= (int)sizeof(int_val))
		memcpy(&int_val, params, sizeof(int_val));

	bool_val = (int_val != 0) ? true : false;

	actionid = set ? IOV_SVAL(vi->varid) : IOV_GVAL(vi->varid);
	switch (actionid) {
	case IOV_GVAL(IOV_MSGLEVEL):
		int_val = (s32) sd_msglevel;
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_SVAL(IOV_MSGLEVEL):
		sd_msglevel = int_val;
		break;

	case IOV_GVAL(IOV_BLOCKSIZE):
		if ((u32) int_val > si->num_funcs) {
			bcmerror = -EINVAL;
			break;
		}
		int_val = (s32) si->client_block_size[int_val];
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_SVAL(IOV_BLOCKSIZE):
		{
			uint func = ((u32) int_val >> 16);
			uint blksize = (u16) int_val;
			uint maxsize;

			if (func > si->num_funcs) {
				bcmerror = -EINVAL;
				break;
			}

			switch (func) {
			case 0:
				maxsize = 32;
				break;
			case 1:
				maxsize = BLOCK_SIZE_4318;
				break;
			case 2:
				maxsize = BLOCK_SIZE_4328;
				break;
			default:
				maxsize = 0;
			}
			if (blksize > maxsize) {
				bcmerror = -EINVAL;
				break;
			}
			if (!blksize)
				blksize = maxsize;

			/* Now set it */
			si->client_block_size[func] = blksize;

			break;
		}

	case IOV_GVAL(IOV_RXCHAIN):
		int_val = false;
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_GVAL(IOV_USEINTS):
		int_val = (s32) si->use_client_ints;
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_SVAL(IOV_USEINTS):
		si->use_client_ints = (bool) int_val;
		if (si->use_client_ints)
			si->intmask |= CLIENT_INTR;
		else
			si->intmask &= ~CLIENT_INTR;

		break;

	case IOV_GVAL(IOV_NUMINTS):
		int_val = (s32) si->intrcount;
		memcpy(arg, &int_val, val_size);
		break;

	case IOV_GVAL(IOV_DEVREG):
		{
			struct brcmf_sdreg *sd_ptr =
					(struct brcmf_sdreg *) params;
			u8 data = 0;

			if (brcmf_sdioh_cfg_read
			    (si, sd_ptr->func, sd_ptr->offset, &data)) {
				bcmerror = -EIO;
				break;
			}

			int_val = (int)data;
			memcpy(arg, &int_val, sizeof(int_val));
			break;
		}

	case IOV_SVAL(IOV_DEVREG):
		{
			struct brcmf_sdreg *sd_ptr =
					(struct brcmf_sdreg *) params;
			u8 data = (u8) sd_ptr->value;

			if (brcmf_sdioh_cfg_write
			    (si, sd_ptr->func, sd_ptr->offset, &data)) {
				bcmerror = -EIO;
				break;
			}
			break;
		}

	default:
		bcmerror = -ENOTSUPP;
		break;
	}
exit:

	return bcmerror;
}

extern int
brcmf_sdioh_cfg_read(struct sdioh_info *sd, uint fnc_num, u32 addr, u8 *data)
{
	int status;
	/* No lock needed since brcmf_sdioh_request_byte does locking */
	status = brcmf_sdioh_request_byte(sd, SDIOH_READ, fnc_num, addr, data);
	return status;
}

extern int
brcmf_sdioh_cfg_write(struct sdioh_info *sd, uint fnc_num, u32 addr, u8 *data)
{
	/* No lock needed since brcmf_sdioh_request_byte does locking */
	int status;
	status = brcmf_sdioh_request_byte(sd, SDIOH_WRITE, fnc_num, addr, data);
	return status;
}

static int brcmf_sdioh_get_cisaddr(struct sdioh_info *sd, u32 regaddr)
{
	/* read 24 bits and return valid 17 bit addr */
	int i;
	u32 scratch, regdata;
	u8 *ptr = (u8 *)&scratch;
	for (i = 0; i < 3; i++) {
		if ((brcmf_sdioh_card_regread(sd, 0, regaddr, 1, &regdata)) !=
		    SUCCESS)
			sd_err(("%s: Can't read!\n", __func__));

		*ptr++ = (u8) regdata;
		regaddr++;
	}

	/* Only the lower 17-bits are valid */
	scratch = le32_to_cpu(scratch);
	scratch &= 0x0001FFFF;
	return scratch;
}

extern int
brcmf_sdioh_cis_read(struct sdioh_info *sd, uint func, u8 *cisd, u32 length)
{
	u32 count;
	int offset;
	u32 foo;
	u8 *cis = cisd;

	sd_trace(("%s: Func = %d\n", __func__, func));

	if (!sd->func_cis_ptr[func]) {
		memset(cis, 0, length);
		sd_err(("%s: no func_cis_ptr[%d]\n", __func__, func));
		return -ENOTSUPP;
	}

	sd_err(("%s: func_cis_ptr[%d]=0x%04x\n", __func__, func,
		sd->func_cis_ptr[func]));

	for (count = 0; count < length; count++) {
		offset = sd->func_cis_ptr[func] + count;
		if (brcmf_sdioh_card_regread(sd, 0, offset, 1, &foo) < 0) {
			sd_err(("%s: regread failed: Can't read CIS\n",
				__func__));
			return -EIO;
		}

		*cis = (u8) (foo & 0xff);
		cis++;
	}

	return 0;
}

extern int
brcmf_sdioh_request_byte(struct sdioh_info *sd, uint rw, uint func,
			 uint regaddr, u8 *byte)
{
	int err_ret;

	sd_info(("%s: rw=%d, func=%d, addr=0x%05x\n", __func__, rw, func,
		 regaddr));

	BRCMF_PM_RESUME_WAIT(sdioh_request_byte_wait);
	BRCMF_PM_RESUME_RETURN_ERROR(-EIO);
	if (rw) {		/* CMD52 Write */
		if (func == 0) {
			/* Can only directly write to some F0 registers.
			 * Handle F2 enable
			 * as a special case.
			 */
			if (regaddr == SDIO_CCCR_IOEx) {
				if (gInstance->func[2]) {
					sdio_claim_host(gInstance->func[2]);
					if (*byte & SDIO_FUNC_ENABLE_2) {
						/* Enable Function 2 */
						err_ret =
						    sdio_enable_func
						    (gInstance->func[2]);
						if (err_ret)
							sd_err(("request_byte: "
								"enable F2 "
								"failed:%d\n",
								 err_ret));
					} else {
						/* Disable Function 2 */
						err_ret =
						    sdio_disable_func
						    (gInstance->func[2]);
						if (err_ret)
							sd_err(("request_byte: "
								"Disab F2 "
								"failed:%d\n",
								 err_ret));
					}
					sdio_release_host(gInstance->func[2]);
				}
			}
			/* to allow abort command through F1 */
			else if (regaddr == SDIO_CCCR_ABORT) {
				sdio_claim_host(gInstance->func[func]);
				/*
				 * this sdio_f0_writeb() can be replaced
				 * with another api
				 * depending upon MMC driver change.
				 * As of this time, this is temporaray one
				 */
				sdio_writeb(gInstance->func[func], *byte,
					    regaddr, &err_ret);
				sdio_release_host(gInstance->func[func]);
			} else if (regaddr < 0xF0) {
				sd_err(("brcmf: F0 Wr:0x%02x: write "
					"disallowed\n", regaddr));
			} else {
				/* Claim host controller, perform F0 write,
				 and release */
				sdio_claim_host(gInstance->func[func]);
				sdio_f0_writeb(gInstance->func[func], *byte,
					       regaddr, &err_ret);
				sdio_release_host(gInstance->func[func]);
			}
		} else {
			/* Claim host controller, perform Fn write,
			 and release */
			sdio_claim_host(gInstance->func[func]);
			sdio_writeb(gInstance->func[func], *byte, regaddr,
				    &err_ret);
			sdio_release_host(gInstance->func[func]);
		}
	} else {		/* CMD52 Read */
		/* Claim host controller, perform Fn read, and release */
		sdio_claim_host(gInstance->func[func]);

		if (func == 0) {
			*byte =
			    sdio_f0_readb(gInstance->func[func], regaddr,
					  &err_ret);
		} else {
			*byte =
			    sdio_readb(gInstance->func[func], regaddr,
				       &err_ret);
		}

		sdio_release_host(gInstance->func[func]);
	}

	if (err_ret)
		sd_err(("brcmf: Failed to %s byte F%d:@0x%05x=%02x, "
			"Err: %d\n", rw ? "Write" : "Read", func, regaddr,
			*byte, err_ret));

	return err_ret;
}

extern int
brcmf_sdioh_request_word(struct sdioh_info *sd, uint cmd_type, uint rw,
			 uint func, uint addr, u32 *word, uint nbytes)
{
	int err_ret = -EIO;

	if (func == 0) {
		sd_err(("%s: Only CMD52 allowed to F0.\n", __func__));
		return -EINVAL;
	}

	sd_info(("%s: cmd_type=%d, rw=%d, func=%d, addr=0x%05x, nbytes=%d\n",
		 __func__, cmd_type, rw, func, addr, nbytes));

	BRCMF_PM_RESUME_WAIT(sdioh_request_word_wait);
	BRCMF_PM_RESUME_RETURN_ERROR(-EIO);
	/* Claim host controller */
	sdio_claim_host(gInstance->func[func]);

	if (rw) {		/* CMD52 Write */
		if (nbytes == 4) {
			sdio_writel(gInstance->func[func], *word, addr,
				    &err_ret);
		} else if (nbytes == 2) {
			sdio_writew(gInstance->func[func], (*word & 0xFFFF),
				    addr, &err_ret);
		} else {
			sd_err(("%s: Invalid nbytes: %d\n", __func__, nbytes));
		}
	} else {		/* CMD52 Read */
		if (nbytes == 4) {
			*word =
			    sdio_readl(gInstance->func[func], addr, &err_ret);
		} else if (nbytes == 2) {
			*word =
			    sdio_readw(gInstance->func[func], addr,
				       &err_ret) & 0xFFFF;
		} else {
			sd_err(("%s: Invalid nbytes: %d\n", __func__, nbytes));
		}
	}

	/* Release host controller */
	sdio_release_host(gInstance->func[func]);

	if (err_ret) {
		sd_err(("brcmf: Failed to %s word, Err: 0x%08x\n",
			rw ? "Write" : "Read", err_ret));
	}

	return err_ret;
}

static int
brcmf_sdioh_request_packet(struct sdioh_info *sd, uint fix_inc, uint write,
			   uint func, uint addr, struct sk_buff *pkt)
{
	bool fifo = (fix_inc == SDIOH_DATA_FIX);
	u32 SGCount = 0;
	int err_ret = 0;

	struct sk_buff *pnext;

	sd_trace(("%s: Enter\n", __func__));

	BRCMF_PM_RESUME_WAIT(sdioh_request_packet_wait);
	BRCMF_PM_RESUME_RETURN_ERROR(-EIO);

	/* Claim host controller */
	sdio_claim_host(gInstance->func[func]);
	for (pnext = pkt; pnext; pnext = pnext->next) {
		uint pkt_len = pnext->len;
		pkt_len += 3;
		pkt_len &= 0xFFFFFFFC;

		if ((write) && (!fifo)) {
			err_ret = sdio_memcpy_toio(gInstance->func[func], addr,
						   ((u8 *) (pnext->data)),
						   pkt_len);
		} else if (write) {
			err_ret = sdio_memcpy_toio(gInstance->func[func], addr,
						   ((u8 *) (pnext->data)),
						   pkt_len);
		} else if (fifo) {
			err_ret = sdio_readsb(gInstance->func[func],
					      ((u8 *) (pnext->data)),
					      addr, pkt_len);
		} else {
			err_ret = sdio_memcpy_fromio(gInstance->func[func],
						     ((u8 *) (pnext->data)),
						     addr, pkt_len);
		}

		if (err_ret) {
			sd_err(("%s: %s FAILED %p[%d], addr=0x%05x, pkt_len=%d,"
				 "ERR=0x%08x\n", __func__,
				 (write) ? "TX" : "RX",
				 pnext, SGCount, addr, pkt_len, err_ret));
		} else {
			sd_trace(("%s: %s xfr'd %p[%d], addr=0x%05x, len=%d\n",
				  __func__,
				  (write) ? "TX" : "RX",
				  pnext, SGCount, addr, pkt_len));
		}

		if (!fifo)
			addr += pkt_len;
		SGCount++;

	}

	/* Release host controller */
	sdio_release_host(gInstance->func[func]);

	sd_trace(("%s: Exit\n", __func__));
	return err_ret;
}

/*
 * This function takes a buffer or packet, and fixes everything up
 * so that in the end, a DMA-able packet is created.
 *
 * A buffer does not have an associated packet pointer,
 * and may or may not be aligned.
 * A packet may consist of a single packet, or a packet chain.
 * If it is a packet chain, then all the packets in the chain
 * must be properly aligned.
 *
 * If the packet data is not aligned, then there may only be
 * one packet, and in this case,  it is copied to a new
 * aligned packet.
 *
 */
extern int
brcmf_sdioh_request_buffer(struct sdioh_info *sd, uint pio_dma, uint fix_inc,
			   uint write, uint func, uint addr, uint reg_width,
			   uint buflen_u, u8 *buffer, struct sk_buff *pkt)
{
	int Status;
	struct sk_buff *mypkt = NULL;

	sd_trace(("%s: Enter\n", __func__));

	BRCMF_PM_RESUME_WAIT(sdioh_request_buffer_wait);
	BRCMF_PM_RESUME_RETURN_ERROR(-EIO);
	/* Case 1: we don't have a packet. */
	if (pkt == NULL) {
		sd_data(("%s: Creating new %s Packet, len=%d\n",
			 __func__, write ? "TX" : "RX", buflen_u));
		mypkt = brcmu_pkt_buf_get_skb(buflen_u);
		if (!mypkt) {
			sd_err(("%s: brcmu_pkt_buf_get_skb failed: len %d\n",
				__func__, buflen_u));
			return -EIO;
		}

		/* For a write, copy the buffer data into the packet. */
		if (write)
			memcpy(mypkt->data, buffer, buflen_u);

		Status = brcmf_sdioh_request_packet(sd, fix_inc, write, func,
						    addr, mypkt);

		/* For a read, copy the packet data back to the buffer. */
		if (!write)
			memcpy(buffer, mypkt->data, buflen_u);

		brcmu_pkt_buf_free_skb(mypkt);
	} else if (((ulong) (pkt->data) & DMA_ALIGN_MASK) != 0) {
		/*
		 * Case 2: We have a packet, but it is unaligned.
		 * In this case, we cannot have a chain (pkt->next == NULL)
		 */
		sd_data(("%s: Creating aligned %s Packet, len=%d\n",
			 __func__, write ? "TX" : "RX", pkt->len));
		mypkt = brcmu_pkt_buf_get_skb(pkt->len);
		if (!mypkt) {
			sd_err(("%s: brcmu_pkt_buf_get_skb failed: len %d\n",
				__func__, pkt->len));
			return -EIO;
		}

		/* For a write, copy the buffer data into the packet. */
		if (write)
			memcpy(mypkt->data, pkt->data, pkt->len);

		Status = brcmf_sdioh_request_packet(sd, fix_inc, write, func,
						    addr, mypkt);

		/* For a read, copy the packet data back to the buffer. */
		if (!write)
			memcpy(pkt->data, mypkt->data, mypkt->len);

		brcmu_pkt_buf_free_skb(mypkt);
	} else {		/* case 3: We have a packet and
				 it is aligned. */
		sd_data(("%s: Aligned %s Packet, direct DMA\n",
			 __func__, write ? "Tx" : "Rx"));
		Status = brcmf_sdioh_request_packet(sd, fix_inc, write, func,
						    addr, pkt);
	}

	return Status;
}

/* this function performs "abort" for both of host & device */
extern int brcmf_sdioh_abort(struct sdioh_info *sd, uint func)
{
	char t_func = (char)func;
	sd_trace(("%s: Enter\n", __func__));

	/* issue abort cmd52 command through F0 */
	brcmf_sdioh_request_byte(sd, SDIOH_WRITE, SDIO_FUNC_0, SDIO_CCCR_ABORT,
			   &t_func);

	sd_trace(("%s: Exit\n", __func__));
	return 0;
}

/* Disable device interrupt */
void brcmf_sdioh_dev_intr_off(struct sdioh_info *sd)
{
	sd_trace(("%s: %d\n", __func__, sd->use_client_ints));
	sd->intmask &= ~CLIENT_INTR;
}

/* Enable device interrupt */
void brcmf_sdioh_dev_intr_on(struct sdioh_info *sd)
{
	sd_trace(("%s: %d\n", __func__, sd->use_client_ints));
	sd->intmask |= CLIENT_INTR;
}

/* Read client card reg */
int
brcmf_sdioh_card_regread(struct sdioh_info *sd, int func, u32 regaddr,
			 int regsize, u32 *data)
{

	if ((func == 0) || (regsize == 1)) {
		u8 temp = 0;

		brcmf_sdioh_request_byte(sd, SDIOH_READ, func, regaddr, &temp);
		*data = temp;
		*data &= 0xff;
		sd_data(("%s: byte read data=0x%02x\n", __func__, *data));
	} else {
		brcmf_sdioh_request_word(sd, 0, SDIOH_READ, func, regaddr, data,
				   regsize);
		if (regsize == 2)
			*data &= 0xffff;

		sd_data(("%s: word read data=0x%08x\n", __func__, *data));
	}

	return SUCCESS;
}

static void brcmf_sdioh_irqhandler(struct sdio_func *func)
{
	struct sdioh_info *sd;

	sd_trace(("brcmf: ***IRQHandler\n"));
	sd = gInstance->sd;

	sdio_release_host(gInstance->func[0]);

	if (sd->use_client_ints) {
		sd->intrcount++;
		(sd->intr_handler) (sd->intr_handler_arg);
	} else {
		sd_err(("brcmf: ***IRQHandler\n"));

		sd_err(("%s: Not ready for intr: enabled %d, handler %p\n",
			__func__, sd->client_intr_enabled, sd->intr_handler));
	}

	sdio_claim_host(gInstance->func[0]);
}

/* interrupt handler for F2 (dummy handler) */
static void brcmf_sdioh_irqhandler_f2(struct sdio_func *func)
{
	struct sdioh_info *sd;

	sd_trace(("brcmf: ***IRQHandlerF2\n"));

	sd = gInstance->sd;
}

static int brcmf_ops_sdio_probe(struct sdio_func *func,
			      const struct sdio_device_id *id)
{
	int ret = 0;
	static struct sdio_func sdio_func_0;
	sd_trace(("sdio_probe: %s Enter\n", __func__));
	sd_trace(("sdio_probe: func->class=%x\n", func->class));
	sd_trace(("sdio_vendor: 0x%04x\n", func->vendor));
	sd_trace(("sdio_device: 0x%04x\n", func->device));
	sd_trace(("Function#: 0x%04x\n", func->num));

	if (func->num == 1) {
		sdio_func_0.num = 0;
		sdio_func_0.card = func->card;
		gInstance->func[0] = &sdio_func_0;
		if (func->device == 0x4) {	/* 4318 */
			gInstance->func[2] = NULL;
			sd_trace(("NIC found, calling brcmf_sdio_probe...\n"));
			ret = brcmf_sdio_probe(&sdmmc_dev);
		}
	}

	gInstance->func[func->num] = func;

	if (func->num == 2) {
		brcmf_cfg80211_sdio_func(func);
		sd_trace(("F2 found, calling brcmf_sdio_probe...\n"));
		ret = brcmf_sdio_probe(&sdmmc_dev);
	}

	return ret;
}

static void brcmf_ops_sdio_remove(struct sdio_func *func)
{
	sd_trace(("%s Enter\n", __func__));
	sd_info(("func->class=%x\n", func->class));
	sd_info(("sdio_vendor: 0x%04x\n", func->vendor));
	sd_info(("sdio_device: 0x%04x\n", func->device));
	sd_info(("Function#: 0x%04x\n", func->num));

	if (func->num == 2) {
		sd_trace(("F2 found, calling brcmf_sdio_remove...\n"));
		brcmf_sdio_remove(&sdmmc_dev);
	}
}


#ifdef CONFIG_PM
static int brcmf_sdio_suspend(struct device *dev)
{
	mmc_pm_flag_t sdio_flags;
	int ret = 0;

	sd_trace(("%s\n", __func__));

	sdio_flags = sdio_get_host_pm_caps(gInstance->func[1]);
	if (!(sdio_flags & MMC_PM_KEEP_POWER)) {
		sd_err(("Host can't keep power while suspended\n"));
		return -EINVAL;
	}

	ret = sdio_set_host_pm_flags(gInstance->func[1], MMC_PM_KEEP_POWER);
	if (ret) {
		sd_err(("Failed to set pm_flags\n"));
		return ret;
	}

	brcmf_sdio_wdtmr_enable(false);

	return ret;
}

static int brcmf_sdio_resume(struct device *dev)
{
	brcmf_sdio_wdtmr_enable(true);
	return 0;
}
#endif		/* CONFIG_PM */

int brcmf_sdioh_osinit(struct sdioh_info *sd)
{
	struct sdos_info *sdos;

	sdos = kmalloc(sizeof(struct sdos_info), GFP_ATOMIC);
	sd->sdos_info = (void *)sdos;
	if (sdos == NULL)
		return -ENOMEM;

	sdos->sd = sd;
	spin_lock_init(&sdos->lock);
	return 0;
}

void brcmf_sdioh_osfree(struct sdioh_info *sd)
{
	struct sdos_info *sdos;

	sdos = (struct sdos_info *)sd->sdos_info;
	kfree(sdos);
}

/* Interrupt enable/disable */
int brcmf_sdioh_interrupt_set(struct sdioh_info *sd, bool enable)
{
	unsigned long flags;
	struct sdos_info *sdos;

	sd_trace(("%s: %s\n", __func__, enable ? "Enabling" : "Disabling"));

	sdos = (struct sdos_info *)sd->sdos_info;

	if (enable && !(sd->intr_handler && sd->intr_handler_arg)) {
		sd_err(("%s: no handler registered, will not enable\n",
			__func__));
		return -EINVAL;
	}

	/* Ensure atomicity for enable/disable calls */
	spin_lock_irqsave(&sdos->lock, flags);

	sd->client_intr_enabled = enable;
	if (enable)
		brcmf_sdioh_dev_intr_on(sd);
	else
		brcmf_sdioh_dev_intr_off(sd);

	spin_unlock_irqrestore(&sdos->lock, flags);

	return 0;
}

/*
 * module init
*/
int brcmf_sdio_function_init(void)
{
	int error = 0;
	sd_trace(("brcmf_sdio_function_init: %s Enter\n", __func__));

	gInstance = kzalloc(sizeof(struct brcmf_sdmmc_instance), GFP_KERNEL);
	if (!gInstance)
		return -ENOMEM;

	memset(&sdmmc_dev, 0, sizeof(sdmmc_dev));
	error = sdio_register_driver(&brcmf_sdmmc_driver);

	return error;
}

/*
 * module cleanup
*/
void brcmf_sdio_function_cleanup(void)
{
	sd_trace(("%s Enter\n", __func__));

	sdio_unregister_driver(&brcmf_sdmmc_driver);

	kfree(gInstance);
}
