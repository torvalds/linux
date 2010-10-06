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

#include <typedefs.h>
#include <bcmutils.h>
#include <sdio.h>		/* SDIO Specs */
#include <bcmsdbus.h>		/* bcmsdh to/from specific controller APIs */
#include <sdiovar.h>		/* to get msglevel bit values */

#include <linux/sched.h>	/* request_irq() */

#include <linux/mmc/core.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>

#if !defined(SDIO_VENDOR_ID_BROADCOM)
#define SDIO_VENDOR_ID_BROADCOM		0x02d0
#endif				/* !defined(SDIO_VENDOR_ID_BROADCOM) */

#define SDIO_DEVICE_ID_BROADCOM_DEFAULT	0x0000

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

#include <bcmsdh_sdmmc.h>

#include <dhd_dbg.h>
#ifdef CONFIG_CFG80211
#include <wl_cfg80211.h>
#endif

extern void sdioh_sdmmc_devintr_off(sdioh_info_t *sd);
extern void sdioh_sdmmc_devintr_on(sdioh_info_t *sd);

int sdio_function_init(void);
void sdio_function_cleanup(void);

/* module param defaults */
static int clockoverride;

module_param(clockoverride, int, 0644);
MODULE_PARM_DESC(clockoverride, "SDIO card clock override");

PBCMSDH_SDMMC_INSTANCE gInstance;

/* Maximum number of bcmsdh_sdmmc devices supported by driver */
#define BCMSDH_SDMMC_MAX_DEVICES 1

extern int bcmsdh_probe(struct device *dev);
extern int bcmsdh_remove(struct device *dev);
struct device sdmmc_dev;

static int bcmsdh_sdmmc_probe(struct sdio_func *func,
			      const struct sdio_device_id *id)
{
	int ret = 0;
	static struct sdio_func sdio_func_0;
	sd_trace(("bcmsdh_sdmmc: %s Enter\n", __func__));
	sd_trace(("sdio_bcmsdh: func->class=%x\n", func->class));
	sd_trace(("sdio_vendor: 0x%04x\n", func->vendor));
	sd_trace(("sdio_device: 0x%04x\n", func->device));
	sd_trace(("Function#: 0x%04x\n", func->num));

	if (func->num == 1) {
		sdio_func_0.num = 0;
		sdio_func_0.card = func->card;
		gInstance->func[0] = &sdio_func_0;
		if (func->device == 0x4) {	/* 4318 */
			gInstance->func[2] = NULL;
			sd_trace(("NIC found, calling bcmsdh_probe...\n"));
			ret = bcmsdh_probe(&sdmmc_dev);
		}
	}

	gInstance->func[func->num] = func;

	if (func->num == 2) {
#ifdef CONFIG_CFG80211
		wl_cfg80211_sdio_func(func);
#endif
		sd_trace(("F2 found, calling bcmsdh_probe...\n"));
		ret = bcmsdh_probe(&sdmmc_dev);
	}

	return ret;
}

static void bcmsdh_sdmmc_remove(struct sdio_func *func)
{
	sd_trace(("bcmsdh_sdmmc: %s Enter\n", __func__));
	sd_info(("sdio_bcmsdh: func->class=%x\n", func->class));
	sd_info(("sdio_vendor: 0x%04x\n", func->vendor));
	sd_info(("sdio_device: 0x%04x\n", func->device));
	sd_info(("Function#: 0x%04x\n", func->num));

	if (func->num == 2) {
		sd_trace(("F2 found, calling bcmsdh_remove...\n"));
		bcmsdh_remove(&sdmmc_dev);
	}
}

/* devices we support, null terminated */
static const struct sdio_device_id bcmsdh_sdmmc_ids[] = {
	{SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, SDIO_DEVICE_ID_BROADCOM_DEFAULT)},
	{SDIO_DEVICE
	 (SDIO_VENDOR_ID_BROADCOM, SDIO_DEVICE_ID_BROADCOM_4325_SDGWB)},
	{SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, SDIO_DEVICE_ID_BROADCOM_4325)},
	{SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, SDIO_DEVICE_ID_BROADCOM_4329)},
	{SDIO_DEVICE(SDIO_VENDOR_ID_BROADCOM, SDIO_DEVICE_ID_BROADCOM_4319)},
	{ /* end: all zeroes */ },
};

MODULE_DEVICE_TABLE(sdio, bcmsdh_sdmmc_ids);

static struct sdio_driver bcmsdh_sdmmc_driver = {
	.probe = bcmsdh_sdmmc_probe,
	.remove = bcmsdh_sdmmc_remove,
	.name = "brcmfmac",
	.id_table = bcmsdh_sdmmc_ids,
};

struct sdos_info {
	sdioh_info_t *sd;
	spinlock_t lock;
};

int sdioh_sdmmc_osinit(sdioh_info_t *sd)
{
	struct sdos_info *sdos;

	sdos = (struct sdos_info *)MALLOC(sd->osh, sizeof(struct sdos_info));
	sd->sdos_info = (void *)sdos;
	if (sdos == NULL)
		return BCME_NOMEM;

	sdos->sd = sd;
	spin_lock_init(&sdos->lock);
	return BCME_OK;
}

void sdioh_sdmmc_osfree(sdioh_info_t *sd)
{
	struct sdos_info *sdos;
	ASSERT(sd && sd->sdos_info);

	sdos = (struct sdos_info *)sd->sdos_info;
	MFREE(sd->osh, sdos, sizeof(struct sdos_info));
}

/* Interrupt enable/disable */
SDIOH_API_RC sdioh_interrupt_set(sdioh_info_t *sd, bool enable)
{
	unsigned long flags;
	struct sdos_info *sdos;

	sd_trace(("%s: %s\n", __func__, enable ? "Enabling" : "Disabling"));

	sdos = (struct sdos_info *)sd->sdos_info;
	ASSERT(sdos);

#if !defined(OOB_INTR_ONLY)
	if (enable && !(sd->intr_handler && sd->intr_handler_arg)) {
		sd_err(("%s: no handler registered, will not enable\n",
			__func__));
		return SDIOH_API_RC_FAIL;
	}
#endif				/* !defined(OOB_INTR_ONLY) */

	/* Ensure atomicity for enable/disable calls */
	spin_lock_irqsave(&sdos->lock, flags);

	sd->client_intr_enabled = enable;
	if (enable)
		sdioh_sdmmc_devintr_on(sd);
	else
		sdioh_sdmmc_devintr_off(sd);

	spin_unlock_irqrestore(&sdos->lock, flags);

	return SDIOH_API_RC_SUCCESS;
}

/*
 * module init
*/
int sdio_function_init(void)
{
	int error = 0;
	sd_trace(("bcmsdh_sdmmc: %s Enter\n", __func__));

	gInstance = kzalloc(sizeof(BCMSDH_SDMMC_INSTANCE), GFP_KERNEL);
	if (!gInstance)
		return -ENOMEM;

	bzero(&sdmmc_dev, sizeof(sdmmc_dev));
	error = sdio_register_driver(&bcmsdh_sdmmc_driver);

	return error;
}

/*
 * module cleanup
*/
extern int bcmsdh_remove(struct device *dev);
void sdio_function_cleanup(void)
{
	sd_trace(("%s Enter\n", __func__));

	sdio_unregister_driver(&bcmsdh_sdmmc_driver);

	kfree(gInstance);
}
