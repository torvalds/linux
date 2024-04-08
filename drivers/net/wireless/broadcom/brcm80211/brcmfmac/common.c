// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2010 Broadcom Corporation
 */

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/netdevice.h>
#include <linux/module.h>
#include <linux/firmware.h>
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
#include "firmware.h"
#include "chip.h"

MODULE_AUTHOR("Broadcom Corporation");
MODULE_DESCRIPTION("Broadcom 802.11 wireless LAN fullmac driver.");
MODULE_LICENSE("Dual BSD/GPL");

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
module_param_named(debug, brcmf_msg_level, int, 0600);
MODULE_PARM_DESC(debug, "Level of debug output");

static int brcmf_p2p_enable;
module_param_named(p2pon, brcmf_p2p_enable, int, 0);
MODULE_PARM_DESC(p2pon, "Enable legacy p2p management functionality");

static int brcmf_feature_disable;
module_param_named(feature_disable, brcmf_feature_disable, int, 0);
MODULE_PARM_DESC(feature_disable, "Disable features");

static char brcmf_firmware_path[BRCMF_FW_ALTPATH_LEN];
module_param_string(alternative_fw_path, brcmf_firmware_path,
		    BRCMF_FW_ALTPATH_LEN, 0400);
MODULE_PARM_DESC(alternative_fw_path, "Alternative firmware path");

static int brcmf_fcmode;
module_param_named(fcmode, brcmf_fcmode, int, 0);
MODULE_PARM_DESC(fcmode, "Mode of firmware signalled flow control");

static int brcmf_roamoff;
module_param_named(roamoff, brcmf_roamoff, int, 0400);
MODULE_PARM_DESC(roamoff, "Do not use internal roaming engine");

static int brcmf_iapp_enable;
module_param_named(iapp, brcmf_iapp_enable, int, 0);
MODULE_PARM_DESC(iapp, "Enable partial support for the obsoleted Inter-Access Point Protocol");

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
	struct brcmf_pub *drvr = ifp->drvr;
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
		bphy_err(drvr, "Set join_pref error (%d)\n", err);
}

static int brcmf_c_download(struct brcmf_if *ifp, u16 flag,
			    struct brcmf_dload_data_le *dload_buf,
			    u32 len, const char *var)
{
	s32 err;

	flag |= (DLOAD_HANDLER_VER << DLOAD_FLAG_VER_SHIFT);
	dload_buf->flag = cpu_to_le16(flag);
	dload_buf->dload_type = cpu_to_le16(DL_TYPE_CLM);
	dload_buf->len = cpu_to_le32(len);
	dload_buf->crc = cpu_to_le32(0);

	err = brcmf_fil_iovar_data_set(ifp, var, dload_buf,
				       struct_size(dload_buf, data, len));

	return err;
}

static int brcmf_c_download_blob(struct brcmf_if *ifp,
				 const void *data, size_t size,
				 const char *loadvar, const char *statvar)
{
	struct brcmf_pub *drvr = ifp->drvr;
	struct brcmf_dload_data_le *chunk_buf;
	u32 chunk_len;
	u32 datalen;
	u32 cumulative_len;
	u16 dl_flag = DL_BEGIN;
	u32 status;
	s32 err;

	brcmf_dbg(TRACE, "Enter\n");

	chunk_buf = kzalloc(struct_size(chunk_buf, data, MAX_CHUNK_LEN),
			    GFP_KERNEL);
	if (!chunk_buf) {
		err = -ENOMEM;
		return -ENOMEM;
	}

	datalen = size;
	cumulative_len = 0;
	do {
		if (datalen > MAX_CHUNK_LEN) {
			chunk_len = MAX_CHUNK_LEN;
		} else {
			chunk_len = datalen;
			dl_flag |= DL_END;
		}
		memcpy(chunk_buf->data, data + cumulative_len, chunk_len);

		err = brcmf_c_download(ifp, dl_flag, chunk_buf, chunk_len,
				       loadvar);

		dl_flag &= ~DL_BEGIN;

		cumulative_len += chunk_len;
		datalen -= chunk_len;
	} while ((datalen > 0) && (err == 0));

	if (err) {
		bphy_err(drvr, "%s (%zu byte file) failed (%d)\n",
			 loadvar, size, err);
		/* Retrieve status and print */
		err = brcmf_fil_iovar_int_get(ifp, statvar, &status);
		if (err)
			bphy_err(drvr, "get %s failed (%d)\n", statvar, err);
		else
			brcmf_dbg(INFO, "%s=%d\n", statvar, status);
		err = -EIO;
	}

	kfree(chunk_buf);
	return err;
}

static int brcmf_c_process_clm_blob(struct brcmf_if *ifp)
{
	struct brcmf_pub *drvr = ifp->drvr;
	struct brcmf_bus *bus = drvr->bus_if;
	const struct firmware *fw = NULL;
	s32 err;

	brcmf_dbg(TRACE, "Enter\n");

	err = brcmf_bus_get_blob(bus, &fw, BRCMF_BLOB_CLM);
	if (err || !fw) {
		brcmf_info("no clm_blob available (err=%d), device may have limited channels available\n",
			   err);
		return 0;
	}

	err = brcmf_c_download_blob(ifp, fw->data, fw->size,
				    "clmload", "clmload_status");

	release_firmware(fw);
	return err;
}

static int brcmf_c_process_txcap_blob(struct brcmf_if *ifp)
{
	struct brcmf_pub *drvr = ifp->drvr;
	struct brcmf_bus *bus = drvr->bus_if;
	const struct firmware *fw = NULL;
	s32 err;

	brcmf_dbg(TRACE, "Enter\n");

	err = brcmf_bus_get_blob(bus, &fw, BRCMF_BLOB_TXCAP);
	if (err || !fw) {
		brcmf_info("no txcap_blob available (err=%d)\n", err);
		return 0;
	}

	brcmf_info("TxCap blob found, loading\n");
	err = brcmf_c_download_blob(ifp, fw->data, fw->size,
				    "txcapload", "txcapload_status");

	release_firmware(fw);
	return err;
}

int brcmf_c_set_cur_etheraddr(struct brcmf_if *ifp, const u8 *addr)
{
	s32 err;

	err = brcmf_fil_iovar_data_set(ifp, "cur_etheraddr", addr, ETH_ALEN);
	if (err < 0)
		bphy_err(ifp->drvr, "Setting cur_etheraddr failed, %d\n", err);

	return err;
}

/* On some boards there is no eeprom to hold the nvram, in this case instead
 * a board specific nvram is loaded from /lib/firmware. On most boards the
 * macaddr setting in the /lib/firmware nvram file is ignored because the
 * wifibt chip has a unique MAC programmed into the chip itself.
 * But in some cases the actual MAC from the /lib/firmware nvram file gets
 * used, leading to MAC conflicts.
 * The MAC addresses in the troublesome nvram files seem to all come from
 * the same nvram file template, so we only need to check for 1 known
 * address to detect this.
 */
static const u8 brcmf_default_mac_address[ETH_ALEN] = {
	0x00, 0x90, 0x4c, 0xc5, 0x12, 0x38
};

static int brcmf_c_process_cal_blob(struct brcmf_if *ifp)
{
	struct brcmf_pub *drvr = ifp->drvr;
	struct brcmf_mp_device *settings = drvr->settings;
	s32 err;

	brcmf_dbg(TRACE, "Enter\n");

	if (!settings->cal_blob || !settings->cal_size)
		return 0;

	brcmf_info("Calibration blob provided by platform, loading\n");
	err = brcmf_c_download_blob(ifp, settings->cal_blob, settings->cal_size,
				    "calload", "calload_status");
	return err;
}

int brcmf_c_preinit_dcmds(struct brcmf_if *ifp)
{
	struct brcmf_pub *drvr = ifp->drvr;
	struct brcmf_fweh_info *fweh = drvr->fweh;
	u8 buf[BRCMF_DCMD_SMLEN];
	struct brcmf_bus *bus;
	struct brcmf_rev_info_le revinfo;
	struct brcmf_rev_info *ri;
	char *clmver;
	char *ptr;
	s32 err;

	if (is_valid_ether_addr(ifp->mac_addr)) {
		/* set mac address */
		err = brcmf_c_set_cur_etheraddr(ifp, ifp->mac_addr);
		if (err < 0)
			goto done;
	} else {
		/* retrieve mac address */
		err = brcmf_fil_iovar_data_get(ifp, "cur_etheraddr", ifp->mac_addr,
					       sizeof(ifp->mac_addr));
		if (err < 0) {
			bphy_err(drvr, "Retrieving cur_etheraddr failed, %d\n", err);
			goto done;
		}

		if (ether_addr_equal_unaligned(ifp->mac_addr, brcmf_default_mac_address)) {
			bphy_err(drvr, "Default MAC is used, replacing with random MAC to avoid conflicts\n");
			eth_random_addr(ifp->mac_addr);
			ifp->ndev->addr_assign_type = NET_ADDR_RANDOM;
			err = brcmf_c_set_cur_etheraddr(ifp, ifp->mac_addr);
			if (err < 0)
				goto done;
		}
	}

	memcpy(ifp->drvr->mac, ifp->mac_addr, sizeof(ifp->drvr->mac));
	memcpy(ifp->drvr->wiphy->perm_addr, ifp->drvr->mac, ETH_ALEN);

	bus = ifp->drvr->bus_if;
	ri = &ifp->drvr->revinfo;

	err = brcmf_fil_cmd_data_get(ifp, BRCMF_C_GET_REVINFO,
				     &revinfo, sizeof(revinfo));
	if (err < 0) {
		bphy_err(drvr, "retrieving revision info failed, %d\n", err);
		strscpy(ri->chipname, "UNKNOWN", sizeof(ri->chipname));
	} else {
		ri->vendorid = le32_to_cpu(revinfo.vendorid);
		ri->deviceid = le32_to_cpu(revinfo.deviceid);
		ri->radiorev = le32_to_cpu(revinfo.radiorev);
		ri->corerev = le32_to_cpu(revinfo.corerev);
		ri->boardid = le32_to_cpu(revinfo.boardid);
		ri->boardvendor = le32_to_cpu(revinfo.boardvendor);
		ri->boardrev = le32_to_cpu(revinfo.boardrev);
		ri->driverrev = le32_to_cpu(revinfo.driverrev);
		ri->ucoderev = le32_to_cpu(revinfo.ucoderev);
		ri->bus = le32_to_cpu(revinfo.bus);
		ri->phytype = le32_to_cpu(revinfo.phytype);
		ri->phyrev = le32_to_cpu(revinfo.phyrev);
		ri->anarev = le32_to_cpu(revinfo.anarev);
		ri->chippkg = le32_to_cpu(revinfo.chippkg);
		ri->nvramrev = le32_to_cpu(revinfo.nvramrev);

		/* use revinfo if not known yet */
		if (!bus->chip) {
			bus->chip = le32_to_cpu(revinfo.chipnum);
			bus->chiprev = le32_to_cpu(revinfo.chiprev);
		}
	}
	ri->result = err;

	if (bus->chip)
		brcmf_chip_name(bus->chip, bus->chiprev,
				ri->chipname, sizeof(ri->chipname));

	/* Do any CLM downloading */
	err = brcmf_c_process_clm_blob(ifp);
	if (err < 0) {
		bphy_err(drvr, "download CLM blob file failed, %d\n", err);
		goto done;
	}

	/* Do TxCap downloading, if needed */
	err = brcmf_c_process_txcap_blob(ifp);
	if (err < 0) {
		bphy_err(drvr, "download TxCap blob file failed, %d\n", err);
		goto done;
	}

	/* Download external calibration blob, if available */
	err = brcmf_c_process_cal_blob(ifp);
	if (err < 0) {
		bphy_err(drvr, "download calibration blob file failed, %d\n", err);
		goto done;
	}

	/* query for 'ver' to get version info from firmware */
	memset(buf, 0, sizeof(buf));
	err = brcmf_fil_iovar_data_get(ifp, "ver", buf, sizeof(buf));
	if (err < 0) {
		bphy_err(drvr, "Retrieving version information failed, %d\n",
			 err);
		goto done;
	}
	buf[sizeof(buf) - 1] = '\0';
	ptr = (char *)buf;
	strsep(&ptr, "\n");

	/* Print fw version info */
	brcmf_info("Firmware: %s %s\n", ri->chipname, buf);

	/* locate firmware version number for ethtool */
	ptr = strrchr(buf, ' ');
	if (!ptr) {
		bphy_err(drvr, "Retrieving version number failed");
		goto done;
	}
	strscpy(ifp->drvr->fwver, ptr + 1, sizeof(ifp->drvr->fwver));

	/* Query for 'clmver' to get CLM version info from firmware */
	memset(buf, 0, sizeof(buf));
	err = brcmf_fil_iovar_data_get(ifp, "clmver", buf, sizeof(buf));
	if (err) {
		brcmf_dbg(TRACE, "retrieving clmver failed, %d\n", err);
	} else {
		buf[sizeof(buf) - 1] = '\0';
		clmver = (char *)buf;

		/* Replace all newline/linefeed characters with space
		 * character
		 */
		strreplace(clmver, '\n', ' ');

		/* store CLM version for adding it to revinfo debugfs file */
		memcpy(ifp->drvr->clmver, clmver, sizeof(ifp->drvr->clmver));

		brcmf_dbg(INFO, "CLM version = %s\n", clmver);
	}

	/* set mpc */
	err = brcmf_fil_iovar_int_set(ifp, "mpc", 1);
	if (err) {
		bphy_err(drvr, "failed setting mpc\n");
		goto done;
	}

	brcmf_c_set_joinpref_default(ifp);

	/* Setup event_msgs, enable E_IF */
	err = brcmf_fil_iovar_data_get(ifp, "event_msgs", fweh->event_mask,
				       fweh->event_mask_len);
	if (err) {
		bphy_err(drvr, "Get event_msgs error (%d)\n", err);
		goto done;
	}
	/*
	 * BRCMF_E_IF can safely be used to set the appropriate bit
	 * in the event_mask as the firmware event code is guaranteed
	 * to match the value of BRCMF_E_IF because it is old cruft
	 * that all vendors have.
	 */
	setbit(fweh->event_mask, BRCMF_E_IF);
	err = brcmf_fil_iovar_data_set(ifp, "event_msgs", fweh->event_mask,
				       fweh->event_mask_len);
	if (err) {
		bphy_err(drvr, "Set event_msgs error (%d)\n", err);
		goto done;
	}

	/* Setup default scan channel time */
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_CHANNEL_TIME,
				    BRCMF_DEFAULT_SCAN_CHANNEL_TIME);
	if (err) {
		bphy_err(drvr, "BRCMF_C_SET_SCAN_CHANNEL_TIME error (%d)\n",
			 err);
		goto done;
	}

	/* Setup default scan unassoc time */
	err = brcmf_fil_cmd_int_set(ifp, BRCMF_C_SET_SCAN_UNASSOC_TIME,
				    BRCMF_DEFAULT_SCAN_UNASSOC_TIME);
	if (err) {
		bphy_err(drvr, "BRCMF_C_SET_SCAN_UNASSOC_TIME error (%d)\n",
			 err);
		goto done;
	}

	/* Enable tx beamforming, errors can be ignored (not supported) */
	(void)brcmf_fil_iovar_int_set(ifp, "txbf", 1);
done:
	return err;
}

#ifndef CONFIG_BRCM_TRACING
void __brcmf_err(struct brcmf_bus *bus, const char *func, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;
	if (bus)
		dev_err(bus->dev, "%s: %pV", func, &vaf);
	else
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
	strscpy(brcmf_mp_global.firmware_path, brcmf_firmware_path,
		BRCMF_FW_ALTPATH_LEN);
	if ((brcmfmac_pdata) && (brcmfmac_pdata->fw_alternative_path) &&
	    (brcmf_mp_global.firmware_path[0] == '\0')) {
		strscpy(brcmf_mp_global.firmware_path,
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
	settings->iapp = !!brcmf_iapp_enable;
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
		/* No platform data for this device, try OF and DMI data */
		brcmf_dmi_probe(settings, chip, chiprev);
		brcmf_of_probe(dev, bus_type, settings);
		brcmf_acpi_probe(dev, bus_type, settings);
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

static void brcmf_common_pd_remove(struct platform_device *pdev)
{
	brcmf_dbg(INFO, "Enter\n");

	if (brcmfmac_pdata->power_off)
		brcmfmac_pdata->power_off();
}

static struct platform_driver brcmf_pd = {
	.remove_new	= brcmf_common_pd_remove,
	.driver		= {
		.name	= BRCMFMAC_PDATA_NAME,
	}
};

static int __init brcmfmac_module_init(void)
{
	int err;

	/* Get the platform data (if available) for our devices */
	err = platform_driver_probe(&brcmf_pd, brcmf_common_pd_probe);
	if (err == -ENODEV)
		brcmf_dbg(INFO, "No platform data available.\n");

	/* Initialize global module paramaters */
	brcmf_mp_attach();

	/* Continue the initialization by registering the different busses */
	err = brcmf_core_init();
	if (err) {
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
}

module_init(brcmfmac_module_init);
module_exit(brcmfmac_module_exit);

