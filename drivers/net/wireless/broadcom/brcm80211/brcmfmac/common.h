/* Copyright (c) 2014 Broadcom Corporation
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
#ifndef BRCMFMAC_COMMON_H
#define BRCMFMAC_COMMON_H

#include <linux/platform_device.h>
#include <linux/platform_data/brcmfmac-sdio.h>
#include "fwil_types.h"

extern const u8 ALLFFMAC[ETH_ALEN];

#define BRCMF_FW_ALTPATH_LEN			256

/* Definitions for the module global and device specific settings are defined
 * here. Two structs are used for them. brcmf_mp_global_t and brcmf_mp_device.
 * The mp_global is instantiated once in a global struct and gets initialized
 * by the common_attach function which should be called before any other
 * (module) initiliazation takes place. The device specific settings is part
 * of the drvr struct and should be initialized on every brcmf_attach.
 */

/**
 * struct brcmf_mp_global_t - Global module paramaters.
 *
 * @firmware_path: Alternative firmware path.
 */
struct brcmf_mp_global_t {
	char	firmware_path[BRCMF_FW_ALTPATH_LEN];
};

extern struct brcmf_mp_global_t brcmf_mp_global;

/**
 * struct cc_entry - Struct for translating user space country code (iso3166) to
 *		     firmware country code and revision.
 *
 * @iso3166: iso3166 alpha 2 country code string.
 * @cc: firmware country code string.
 * @rev: firmware country code revision.
 */
struct cc_entry {
	char	iso3166[BRCMF_COUNTRY_BUF_SZ];
	char	cc[BRCMF_COUNTRY_BUF_SZ];
	s32	rev;
};

/**
 * struct cc_translate - Struct for translating country codes as set by user
 *			 space to a country code and rev which can be used by
 *			 firmware.
 *
 * @table_size: number of entries in table (> 0)
 * @table: dynamic array of 1 or more elements with translation information.
 */
struct cc_translate {
	int	table_size;
	struct cc_entry table[0];
};

/**
 * struct brcmf_mp_device - Device module paramaters.
 *
 * @sdiod_txglomsz: SDIO txglom size.
 * @joinboost_5g_rssi: 5g rssi booost for preferred join selection.
 * @p2p_enable: Legacy P2P0 enable (old wpa_supplicant).
 * @feature_disable: Feature_disable bitmask.
 * @fcmode: FWS flow control.
 * @roamoff: Firmware roaming off?
 * @country_codes: If available, pointer to struct for translating country codes
 */
struct brcmf_mp_device {
	int	sdiod_txglomsz;
	int	joinboost_5g_rssi;
	bool	p2p_enable;
	int	feature_disable;
	int	fcmode;
	bool	roamoff;
	bool	ignore_probe_fail;
	struct cc_translate *country_codes;
};

struct brcmfmac_sdio_platform_data *brcmf_get_module_param(struct device *dev);
int brcmf_mp_device_attach(struct brcmf_pub *drvr);
void brcmf_mp_device_detach(struct brcmf_pub *drvr);
#ifdef DEBUG
static inline bool brcmf_ignoring_probe_fail(struct brcmf_pub *drvr)
{
	return drvr->settings->ignore_probe_fail;
}
#else
static inline bool brcmf_ignoring_probe_fail(struct brcmf_pub *drvr)
{
	return false;
}
#endif

/* Sets dongle media info (drv_version, mac address). */
int brcmf_c_preinit_dcmds(struct brcmf_if *ifp);

#endif /* BRCMFMAC_COMMON_H */
