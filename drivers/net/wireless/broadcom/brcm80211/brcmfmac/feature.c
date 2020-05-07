// SPDX-License-Identifier: ISC
/*
 * Copyright (c) 2014 Broadcom Corporation
 */

#include <linux/netdevice.h>
#include <linux/module.h>

#include <brcm_hw_ids.h>
#include <brcmu_wifi.h>
#include "core.h"
#include "bus.h"
#include "debug.h"
#include "fwil.h"
#include "fwil_types.h"
#include "feature.h"
#include "common.h"

#define BRCMF_FW_UNSUPPORTED	23

/*
 * expand feature list to array of feature strings.
 */
#define BRCMF_FEAT_DEF(_f) \
	#_f,
static const char *brcmf_feat_names[] = {
	BRCMF_FEAT_LIST
};
#undef BRCMF_FEAT_DEF

struct brcmf_feat_fwcap {
	enum brcmf_feat_id feature;
	const char * const fwcap_id;
};

static const struct brcmf_feat_fwcap brcmf_fwcap_map[] = {
	{ BRCMF_FEAT_MBSS, "mbss" },
	{ BRCMF_FEAT_MCHAN, "mchan" },
	{ BRCMF_FEAT_P2P, "p2p" },
	{ BRCMF_FEAT_MONITOR, "monitor" },
	{ BRCMF_FEAT_MONITOR_FLAG, "rtap" },
	{ BRCMF_FEAT_MONITOR_FMT_RADIOTAP, "rtap" },
	{ BRCMF_FEAT_DOT11H, "802.11h" },
	{ BRCMF_FEAT_SAE, "sae" },
};

#ifdef DEBUG
/*
 * expand quirk list to array of quirk strings.
 */
#define BRCMF_QUIRK_DEF(_q) \
	#_q,
static const char * const brcmf_quirk_names[] = {
	BRCMF_QUIRK_LIST
};
#undef BRCMF_QUIRK_DEF

/**
 * brcmf_feat_debugfs_read() - expose feature info to debugfs.
 *
 * @seq: sequence for debugfs entry.
 * @data: raw data pointer.
 */
static int brcmf_feat_debugfs_read(struct seq_file *seq, void *data)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(seq->private);
	u32 feats = bus_if->drvr->feat_flags;
	u32 quirks = bus_if->drvr->chip_quirks;
	int id;

	seq_printf(seq, "Features: %08x\n", feats);
	for (id = 0; id < BRCMF_FEAT_LAST; id++)
		if (feats & BIT(id))
			seq_printf(seq, "\t%s\n", brcmf_feat_names[id]);
	seq_printf(seq, "\nQuirks:   %08x\n", quirks);
	for (id = 0; id < BRCMF_FEAT_QUIRK_LAST; id++)
		if (quirks & BIT(id))
			seq_printf(seq, "\t%s\n", brcmf_quirk_names[id]);
	return 0;
}
#else
static int brcmf_feat_debugfs_read(struct seq_file *seq, void *data)
{
	return 0;
}
#endif /* DEBUG */

struct brcmf_feat_fwfeat {
	const char * const fwid;
	u32 feat_flags;
};

static const struct brcmf_feat_fwfeat brcmf_feat_fwfeat_map[] = {
	/* brcmfmac43602-pcie.ap.bin from linux-firmware.git commit ea1178515b88 */
	{ "01-6cb8e269", BIT(BRCMF_FEAT_MONITOR) },
	/* brcmfmac4366b-pcie.bin from linux-firmware.git commit 52442afee990 */
	{ "01-c47a91a4", BIT(BRCMF_FEAT_MONITOR) },
	/* brcmfmac4366b-pcie.bin from linux-firmware.git commit 211de1679a68 */
	{ "01-801fb449", BIT(BRCMF_FEAT_MONITOR_FMT_HW_RX_HDR) },
	/* brcmfmac4366c-pcie.bin from linux-firmware.git commit 211de1679a68 */
	{ "01-d2cbb8fd", BIT(BRCMF_FEAT_MONITOR_FMT_HW_RX_HDR) },
};

static void brcmf_feat_firmware_overrides(struct brcmf_pub *drv)
{
	const struct brcmf_feat_fwfeat *e;
	u32 feat_flags = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(brcmf_feat_fwfeat_map); i++) {
		e = &brcmf_feat_fwfeat_map[i];
		if (!strcmp(e->fwid, drv->fwver)) {
			feat_flags = e->feat_flags;
			break;
		}
	}

	if (!feat_flags)
		return;

	for (i = 0; i < BRCMF_FEAT_LAST; i++)
		if (feat_flags & BIT(i))
			brcmf_dbg(INFO, "enabling firmware feature: %s\n",
				  brcmf_feat_names[i]);
	drv->feat_flags |= feat_flags;
}

/**
 * brcmf_feat_iovar_int_get() - determine feature through iovar query.
 *
 * @ifp: interface to query.
 * @id: feature id.
 * @name: iovar name.
 */
static void brcmf_feat_iovar_int_get(struct brcmf_if *ifp,
				     enum brcmf_feat_id id, char *name)
{
	u32 data;
	int err;

	/* we need to know firmware error */
	ifp->fwil_fwerr = true;

	err = brcmf_fil_iovar_int_get(ifp, name, &data);
	if (err == 0) {
		brcmf_dbg(INFO, "enabling feature: %s\n", brcmf_feat_names[id]);
		ifp->drvr->feat_flags |= BIT(id);
	} else {
		brcmf_dbg(TRACE, "%s feature check failed: %d\n",
			  brcmf_feat_names[id], err);
	}

	ifp->fwil_fwerr = false;
}

static void brcmf_feat_iovar_data_set(struct brcmf_if *ifp,
				      enum brcmf_feat_id id, char *name,
				      const void *data, size_t len)
{
	int err;

	/* we need to know firmware error */
	ifp->fwil_fwerr = true;

	err = brcmf_fil_iovar_data_set(ifp, name, data, len);
	if (err != -BRCMF_FW_UNSUPPORTED) {
		brcmf_dbg(INFO, "enabling feature: %s\n", brcmf_feat_names[id]);
		ifp->drvr->feat_flags |= BIT(id);
	} else {
		brcmf_dbg(TRACE, "%s feature check failed: %d\n",
			  brcmf_feat_names[id], err);
	}

	ifp->fwil_fwerr = false;
}

#define MAX_CAPS_BUFFER_SIZE	768
static void brcmf_feat_firmware_capabilities(struct brcmf_if *ifp)
{
	struct brcmf_pub *drvr = ifp->drvr;
	char caps[MAX_CAPS_BUFFER_SIZE];
	enum brcmf_feat_id id;
	int i, err;

	err = brcmf_fil_iovar_data_get(ifp, "cap", caps, sizeof(caps));
	if (err) {
		bphy_err(drvr, "could not get firmware cap (%d)\n", err);
		return;
	}

	brcmf_dbg(INFO, "[ %s]\n", caps);

	for (i = 0; i < ARRAY_SIZE(brcmf_fwcap_map); i++) {
		if (strnstr(caps, brcmf_fwcap_map[i].fwcap_id, sizeof(caps))) {
			id = brcmf_fwcap_map[i].feature;
			brcmf_dbg(INFO, "enabling feature: %s\n",
				  brcmf_feat_names[id]);
			ifp->drvr->feat_flags |= BIT(id);
		}
	}
}

/**
 * brcmf_feat_fwcap_debugfs_read() - expose firmware capabilities to debugfs.
 *
 * @seq: sequence for debugfs entry.
 * @data: raw data pointer.
 */
static int brcmf_feat_fwcap_debugfs_read(struct seq_file *seq, void *data)
{
	struct brcmf_bus *bus_if = dev_get_drvdata(seq->private);
	struct brcmf_pub *drvr = bus_if->drvr;
	struct brcmf_if *ifp = brcmf_get_ifp(drvr, 0);
	char caps[MAX_CAPS_BUFFER_SIZE + 1] = { };
	char *tmp;
	int err;

	err = brcmf_fil_iovar_data_get(ifp, "cap", caps, sizeof(caps));
	if (err) {
		bphy_err(drvr, "could not get firmware cap (%d)\n", err);
		return err;
	}

	/* Put every capability in a new line */
	for (tmp = caps; *tmp; tmp++) {
		if (*tmp == ' ')
			*tmp = '\n';
	}

	/* Usually there is a space at the end of capabilities string */
	seq_printf(seq, "%s", caps);
	/* So make sure we don't print two line breaks */
	if (tmp > caps && *(tmp - 1) != '\n')
		seq_printf(seq, "\n");

	return 0;
}

void brcmf_feat_attach(struct brcmf_pub *drvr)
{
	struct brcmf_if *ifp = brcmf_get_ifp(drvr, 0);
	struct brcmf_pno_macaddr_le pfn_mac;
	struct brcmf_gscan_config gscan_cfg;
	u32 wowl_cap;
	s32 err;

	brcmf_feat_firmware_capabilities(ifp);
	memset(&gscan_cfg, 0, sizeof(gscan_cfg));
	if (drvr->bus_if->chip != BRCM_CC_43430_CHIP_ID &&
	    drvr->bus_if->chip != BRCM_CC_4345_CHIP_ID)
		brcmf_feat_iovar_data_set(ifp, BRCMF_FEAT_GSCAN,
					  "pfn_gscan_cfg",
					  &gscan_cfg, sizeof(gscan_cfg));
	brcmf_feat_iovar_int_get(ifp, BRCMF_FEAT_PNO, "pfn");
	if (drvr->bus_if->wowl_supported)
		brcmf_feat_iovar_int_get(ifp, BRCMF_FEAT_WOWL, "wowl");
	if (brcmf_feat_is_enabled(ifp, BRCMF_FEAT_WOWL)) {
		err = brcmf_fil_iovar_int_get(ifp, "wowl_cap", &wowl_cap);
		if (!err) {
			ifp->drvr->feat_flags |= BIT(BRCMF_FEAT_WOWL_ARP_ND);
			if (wowl_cap & BRCMF_WOWL_PFN_FOUND)
				ifp->drvr->feat_flags |=
					BIT(BRCMF_FEAT_WOWL_ND);
			if (wowl_cap & BRCMF_WOWL_GTK_FAILURE)
				ifp->drvr->feat_flags |=
					BIT(BRCMF_FEAT_WOWL_GTK);
		}
	}
	/* MBSS does not work for all chips */
	switch (drvr->bus_if->chip) {
	case BRCM_CC_4330_CHIP_ID:
	case BRCM_CC_43362_CHIP_ID:
		ifp->drvr->feat_flags &= ~BIT(BRCMF_FEAT_MBSS);
		break;
	default:
		break;
	}
	brcmf_feat_iovar_int_get(ifp, BRCMF_FEAT_RSDB, "rsdb_mode");
	brcmf_feat_iovar_int_get(ifp, BRCMF_FEAT_TDLS, "tdls_enable");
	brcmf_feat_iovar_int_get(ifp, BRCMF_FEAT_MFP, "mfp");

	pfn_mac.version = BRCMF_PFN_MACADDR_CFG_VER;
	err = brcmf_fil_iovar_data_get(ifp, "pfn_macaddr", &pfn_mac,
				       sizeof(pfn_mac));
	if (!err)
		ifp->drvr->feat_flags |= BIT(BRCMF_FEAT_SCAN_RANDOM_MAC);

	if (drvr->settings->feature_disable) {
		brcmf_dbg(INFO, "Features: 0x%02x, disable: 0x%02x\n",
			  ifp->drvr->feat_flags,
			  drvr->settings->feature_disable);
		ifp->drvr->feat_flags &= ~drvr->settings->feature_disable;
	}
	brcmf_feat_iovar_int_get(ifp, BRCMF_FEAT_FWSUP, "sup_wpa");

	brcmf_feat_firmware_overrides(drvr);

	/* set chip related quirks */
	switch (drvr->bus_if->chip) {
	case BRCM_CC_43236_CHIP_ID:
		drvr->chip_quirks |= BIT(BRCMF_FEAT_QUIRK_AUTO_AUTH);
		break;
	case BRCM_CC_4329_CHIP_ID:
		drvr->chip_quirks |= BIT(BRCMF_FEAT_QUIRK_NEED_MPC);
		break;
	default:
		/* no quirks */
		break;
	}
}

void brcmf_feat_debugfs_create(struct brcmf_pub *drvr)
{
	brcmf_debugfs_add_entry(drvr, "features", brcmf_feat_debugfs_read);
	brcmf_debugfs_add_entry(drvr, "fwcap", brcmf_feat_fwcap_debugfs_read);
}

bool brcmf_feat_is_enabled(struct brcmf_if *ifp, enum brcmf_feat_id id)
{
	return (ifp->drvr->feat_flags & BIT(id));
}

bool brcmf_feat_is_quirk_enabled(struct brcmf_if *ifp,
				 enum brcmf_feat_quirk quirk)
{
	return (ifp->drvr->chip_quirks & BIT(quirk));
}
