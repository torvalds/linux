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

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/netdevice.h>
#include <linux/module.h>
#include <brcmu_wifi.h>
#include <brcmu_utils.h>
#include "core.h"
#include "bus.h"
#include "debug.h"
#include "fwil.h"
#include "fwil_types.h"
#include "tracepoint.h"
#include "common.h"
#include "of.h"

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("Broadcom 802.11 wireless LAN fullmac driver.");
MODULE_LICENSE("Dual BSD/GPL");

const u8 ALLFFMAC[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

#define BRCMF_DEFAULT_SCAN_CHANNEL_TIME	40
#define BRCMF_DEFAULT_SCAN_UNASSOC_TIME	40

/* default boost value for RSSI_DELTA in preferred join selection */
#define BRCMF_JOIN_PREF_RSSI_BOOST	8

#define BRCMF_DEFAULT_TXGLOM_SIZE	32  /* max tx frames in glom chain */

static int brcmf_sdiod_txglomsz = BRCMF_DEFAULT_TXGLOM_SIZE;
module_param_named(txglomsz, brcmf_sdiod_txglomsz, int, 0);
MODULE_PARM_DESC(txglomsz, "Maximum tx packet chain size [SDIO]");

/* Debug level configuration. See debug.h for bits, sysfs modifiable */
int brcmf_msg_level;
module_param_named(debug, brcmf_msg_level, int, S_IRUSR | S_IWUSR);
MODULE_PARM_DESC(debug, "Level of debug output");

static int brcmf_p2p_enable;
module_param_named(p2pon, brcmf_p2p_enable, int, 0);
MODULE_PARM_DESC(p2pon, "Enable legacy p2p management functionality");

static int brcmf_feature_disable;
module_param_named(feature_disable, brcmf_feature_disable, int, 0);
MODULE_PARM_DESC(feature_disable, "Disable features");

static char brcmf_firmware_path[BRCMF_FW_ALTPATH_LEN];
module_param_string(alternative_fw_path, brcmf_firmware_path,
		    BRCMF_FW_ALTPATH_LEN, S_IRUSR);
MODULE_PARM_DESC(alternative_fw_path, "Alternative firmware path");

static int brcmf_fcmode;
module_param_named(fcmode, brcmf_fcmode, int, 0);
MODULE_PARM_DESC(fcmode, "Mode of firmware signalled flow control");

static int brcmf_roamoff;
module_param_named(roamoff, brcmf_roamoff, int, S_IRUSR);
MODULE_PARM_DESC(roamoff, "Do not use internal roaming engine");

#ifdef DEBUG
/* always succeed brcmf_bus_started() */
static int brcmf_ignore_probe_fail;
module_param_named(ignore_probe_fail, brcmf_ignore_probe_fail, int, 0);
MODULE_PARM_DESC(ignore_probe_fail, "always succeed probe for debugging");
#endif

static struct brcmfmac_platform_data *brcmfmac_pdata;
struct brcmf_mp_global_t brcmf_mp_global;

void brcmf_c_set_joinpref_default(struct brcmf_if *ifp)
{
	struct brcmf_join_pref_params join_pref_params[2];
	int err;

	/* Setup join_pref to select target by RSSI (boost on 5GHz) */
	join_pref_params[0].type = BRCMF_JOIN_PREF_RSSI_DELTA;
	join_pref_params[0].len = 2;
	join_pref_params[0].rssi_gain = BRCMF_JOIN_PREF_RSSI_BOOST;
	join_pref_params[0].band = WLC_BAND_5G;

	join_pref_params[1].type = BRCMF_JOIN_PREF_RSSI;
	join_pref_params[1].len = 2;
	join_pref_params[1].rssi_gain = 0;
	join_pref_params[1].band = 0;
	err = brcmf_fil_iovar_data_set(ifp, "join_pref", join_pref_params,
				       sizeof(join_pref_params));
	if (err)
		brcmf_err("Set join_pref error (%d)\n", err);
}

int brcmf_c_preinit_dcmds(struct brcmf_if *ifp)
{
	s8 eventmask[BRCMF_EVENTING_MASK_LEN];
	u8 buf[BRCMF_DCMD_SMLEN];
	struct brcmf_rev_info_le revinfo;
	struct brcmf_rev_info *ri;
	char *ptr;
	s32 err;

	/* retreive mac address */
	err = brcmf_fil_iovar_data_get(ifp, "cur_etheraddr", ifp->mac_addr,
				       sizeof(ifp->mac_addr));
	if (err < 0) {
		brcmf_err("Retreiving cur_etheraddr failed, %d\n", err);
		goto done;
	}
	memcpy(ifp->drvr->mac, ifp->mac_addr, sizeof(ifp->drvr->mac));

	err = brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_REVINFO,
				     &revinfo, sizeof(revinfo));
	ri = &ifp->drvr->revinfo;
	if (err < 0) {
		brcmf_err("retrieving revision info failed, %d\n", err);
	} else {
		ri->vendorid = le32_to_cpu(revinfo.vendorid);
		ri->deviceid = le32_to_cpu(revinfo.deviceid);
		ri->radiorev = le32_to_cpu(revinfo.radiorev);
		ri->chiprev = le32_to_cpu(revinfo.chiprev);
		ri->corerev = le32_to_cpu(revinfo.corerev);
		ri->boardid = le32_to_cpu(revinfo.boardid);
		ri->boardvendor = le32_to_cpu(revinfo.boardvendor);
		ri->boardrev = le32_to_cpu(revinfo.boardrev);
		ri->driverrev = le32_to_cpu(revinfo.driverrev);
		ri->ucoderev = le32_to_cpu(revinfo.ucoderev);
		ri->bus = le32_to_cpu(revinfo.bus);
		ri->chipnum = le32_to_cpu(revinfo.chipnum);
		ri->phytype = le32_to_cpu(revinfo.phytype);
		ri->phyrev = le32_to_cpu(revinfo.phyrev);
		ri->anarev = le32_to_cpu(revinfo.anarev);
		ri->chippkg = le32_to_cpu(revinfo.chippkg);
		ri->nvramrev = le32_to_cpu(revinfo.nvramrev);
	}
	ri->result = err;

	/* query for 'ver' to get version info from firmware */
	memset(buf, 0, sizeof(buf));
	strcpy(buf, "ver");
	err = brcmf_fil_iovar_data_get(ifp, "ver", buf, sizeof(buf));
	if (err < 0) {
		brcmf_err("Retreiving version information failed, %d\n",
			  err);
		goto done;
	}
	ptr = (char *)buf;
	strsep(&ptr, "\n");

	/* Print fw version info */
	brcmf_info("Firmware version = %s\n", buf);

	/* locate firmware version number for ethtool */
	ptr = strrchr(buf, ' ') + 1;
	strlcpy(ifp->drvr->fwver, ptr, sizeof(ifp->drvr->fwver));

	/* set mpc */
	err = brcmf_fil_iovar_int_set(ifp, "mpc", 1);
	if (err) {
		brcmf_err("failed setting mpc\n");
		goto done;
	}

	brcmf_c_set_joinpref_default(ifp);

	/* Setup event_msgs, enable E_IF */
	err = brcmf_fil_iovar_data_get(ifp, "event_msgs", eventmask,
				       BRCMF_EVENTING_MASK_LEN);
	if (err) {
		brcmf_err("Get event_msgs error (%d)\n", err);
		goto done;
	}
	setbit(eventmask, BRCMF_E_IF);
	err = brcmf_fil_iovar_data_set(ifp, "event_msgs", eventmask,
				       BRCMF_EVENTING_MASK_LEN);
	if (err) {
		brcmf_err("Set event_msgs error (%d)\n", err);
		goto done;
	}

	/* Setup default scan channel time */
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_CHANNEL_TIME,
				    BRCMF_DEFAULT_SCAN_CHANNEL_TIME);
	if (err) {
		brcmf_err("BRCMF_C_SET_SCAN_CHANNEL_TIME error (%d)\n",
			  err);
		goto done;
	}

	/* Setup default scan unassoc time */
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_UNASSOC_TIME,
				    BRCMF_DEFAULT_SCAN_UNASSOC_TIME);
	if (err) {
		brcmf_err("BRCMF_C_SET_SCAN_UNASSOC_TIME error (%d)\n",
			  err);
		goto done;
	}

	/* Enable tx beamforming, errors can be ignored (not supported) */
	(void)brcmf_fil_iovar_int_set(ifp, "txbf", 1);

	/* do bus specific preinit here */
	err = brcmf_bus_preinit(ifp->drvr->bus_if);
done:
	return err;
}

#ifndef CONFIG_BRCM_TRACING
void __brcmf_err(const char *func, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;
	pr_err("%s: %pV", func, &vaf);

	va_end(args);
}
#endif

#if defined(CONFIG_BRCM_TRACING) || defined(CONFIG_BRCMDBG)
void __brcmf_dbg(u32 level, const char *func, const char *fmt, ...)
{
	struct va_format vaf = {
		.fmt = fmt,
	};
	va_list args;

	va_start(args, fmt);
	vaf.va = &args;
	if (brcmf_msg_level & level)
		pr_debug("%s %pV", func, &vaf);
	trace_brcmf_dbg(level, func, &vaf);
	va_end(args);
}
#endif

static void brcmf_mp_attach(void)
{
	/* If module param firmware path is set then this will always be used,
	 * if not set then if available use the platform data version. To make
	 * sure it gets initialized at all, always copy the module param version
	 */
	strlcpy(brcmf_mp_global.firmware_path, brcmf_firmware_path,
		BRCMF_FW_ALTPATH_LEN);
	if ((brcmfmac_pdata) && (brcmfmac_pdata->fw_alternative_path) &&
	    (brcmf_mp_global.firmware_path[0] == '\0')) {
		strlcpy(brcmf_mp_global.firmware_path,
			brcmfmac_pdata->fw_alternative_path,
			BRCMF_FW_ALTPATH_LEN);
	}
}

struct brcmf_mp_device *brcmf_get_module_param(struct device *dev,
					       enum brcmf_bus_type bus_type,
					       u32 chip, u32 chiprev)
{
	struct brcmf_mp_device *settings;
	struct brcmfmac_pd_device *device_pd;
	bool found;
	int i;

	brcmf_dbg(INFO, "Enter, bus=%d, chip=%d, rev=%d\n", bus_type, chip,
		  chiprev);
	settings = kzalloc(sizeof(*settings), GFP_ATOMIC);
	if (!settings)
		return NULL;

	/* start by using the module paramaters */
	settings->p2p_enable = !!brcmf_p2p_enable;
	settings->feature_disable = brcmf_feature_disable;
	settings->fcmode = brcmf_fcmode;
	settings->roamoff = !!brcmf_roamoff;
#ifdef DEBUG
	settings->ignore_probe_fail = !!brcmf_ignore_probe_fail;
#endif

	if (bus_type == BRCMF_BUSTYPE_SDIO)
		settings->bus.sdio.txglomsz = brcmf_sdiod_txglomsz;

	/* See if there is any device specific platform data configured */
	found = false;
	if (brcmfmac_pdata) {
		for (i = 0; i < brcmfmac_pdata->device_count; i++) {
			device_pd = &brcmfmac_pdata->devices[i];
			if ((device_pd->bus_type == bus_type) &&
			    (device_pd->id == chip) &&
			    ((device_pd->rev == chiprev) ||
			     (device_pd->rev == -1))) {
				brcmf_dbg(INFO, "Platform data for device found\n");
				settings->country_codes =
						device_pd->country_codes;
				if (device_pd->bus_type == BRCMF_BUSTYPE_SDIO)
					memcpy(&settings->bus.sdio,
					       &device_pd->bus.sdio,
					       sizeof(settings->bus.sdio));
				found = true;
				break;
			}
		}
	}
	if (!found) {
		/* No platform data for this device, try OF (Open Firwmare) */
		brcmf_of_probe(dev, bus_type, settings);
	}
	return settings;
}

void brcmf_release_module_param(struct brcmf_mp_device *module_param)
{
	kfree(module_param);
}

static int __init brcmf_common_pd_probe(struct platform_device *pdev)
{
	brcmf_dbg(INFO, "Enter\n");

	brcmfmac_pdata = dev_get_platdata(&pdev->dev);

	if (brcmfmac_pdata->power_on)
		brcmfmac_pdata->power_on();

	return 0;
}

static int brcmf_common_pd_remove(struct platform_device *pdev)
{
	brcmf_dbg(INFO, "Enter\n");

	if (brcmfmac_pdata->power_off)
		brcmfmac_pdata->power_off();

	return 0;
}

static struct platform_driver brcmf_pd = {
	.remove		= brcmf_common_pd_remove,
	.driver		= {
		.name	= BRCMFMAC_PDATA_NAME,
	}
};

static int __init brcmfmac_module_init(void)
{
	int err;

	/* Initialize debug system first */
	brcmf_debugfs_init();

	/* Get the platform data (if available) for our devices */
	err = platform_driver_probe(&brcmf_pd, brcmf_common_pd_probe);
	if (err == -ENODEV)
		brcmf_dbg(INFO, "No platform data available.\n");

	/* Initialize global module paramaters */
	brcmf_mp_attach();

	/* Continue the initialization by registering the different busses */
	err = brcmf_core_init();
	if (err) {
		brcmf_debugfs_exit();
		if (brcmfmac_pdata)
			platform_driver_unregister(&brcmf_pd);
	}

	return err;
}

static void __exit brcmfmac_module_exit(void)
{
	brcmf_core_exit();
	if (brcmfmac_pdata)
		platform_driver_unregister(&brcmf_pd);
	brcmf_debugfs_exit();
}

module_init(brcmfmac_module_init);
module_exit(brcmfmac_module_exit);

