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

const u8 ALLFFMAC[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

#define BRCMF_DEFAULT_SCAN_CHANNEL_TIME	40
#define BRCMF_DEFAULT_SCAN_UNASSOC_TIME	40

/* boost value for RSSI_DELTA in preferred join selection */
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
/* always succeed brcmf_bus_start() */
static int brcmf_ignore_probe_fail;
module_param_named(ignore_probe_fail, brcmf_ignore_probe_fail, int, 0);
MODULE_PARM_DESC(ignore_probe_fail, "always succeed probe for debugging");
#endif

struct brcmf_mp_global_t brcmf_mp_global;

int brcmf_c_preinit_dcmds(struct brcmf_if *ifp)
{
	s8 eventmask[BRCMF_EVENTING_MASK_LEN];
	u8 buf[BRCMF_DCMD_SMLEN];
	struct brcmf_join_pref_params join_pref_params[2];
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
	brcmf_err("Firmware version = %s\n", buf);

	/* locate firmware version number for ethtool */
	ptr = strrchr(buf, ' ') + 1;
	strlcpy(ifp->drvr->fwver, ptr, sizeof(ifp->drvr->fwver));

	/* set mpc */
	err = brcmf_fil_iovar_int_set(ifp, "mpc", 1);
	if (err) {
		brcmf_err("failed setting mpc\n");
		goto done;
	}

	/* Setup join_pref to select target by RSSI(with boost on 5GHz) */
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

void brcmf_mp_attach(void)
{
	strlcpy(brcmf_mp_global.firmware_path, brcmf_firmware_path,
		BRCMF_FW_ALTPATH_LEN);
}

int brcmf_mp_device_attach(struct brcmf_pub *drvr)
{
	drvr->settings = kzalloc(sizeof(*drvr->settings), GFP_ATOMIC);
	if (!drvr->settings) {
		brcmf_err("Failed to alloca storage space for settings\n");
		return -ENOMEM;
	}

	drvr->settings->sdiod_txglomsz = brcmf_sdiod_txglomsz;
	drvr->settings->p2p_enable = !!brcmf_p2p_enable;
	drvr->settings->feature_disable = brcmf_feature_disable;
	drvr->settings->fcmode = brcmf_fcmode;
	drvr->settings->roamoff = !!brcmf_roamoff;
#ifdef DEBUG
	drvr->settings->ignore_probe_fail = !!brcmf_ignore_probe_fail;
#endif
	return 0;
}

void brcmf_mp_device_detach(struct brcmf_pub *drvr)
{
	kfree(drvr->settings);
}

