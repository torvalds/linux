/*
 * Copyright (c) 2014 Broadcom Corporation
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

#include <linux/netdevice.h>

#include <brcm_hw_ids.h>
#include "dhd.h"
#include "dhd_bus.h"
#include "dhd_dbg.h"
#include "fwil.h"
#include "feature.h"

/*
 * firmware error code received if iovar is unsupported.
 */
#define EBRCMF_FEAT_UNSUPPORTED		23

/*
 * expand feature list to array of feature strings.
 */
#define BRCMF_FEAT_DEF(_f) \
	#_f,
static const char *brcmf_feat_names[] = {
	BRCMF_FEAT_LIST
};
#undef BRCMF_FEAT_DEF

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

	err = brcmf_fil_iovar_int_get(ifp, name, &data);
	if (err == 0) {
		brcmf_dbg(INFO, "enabling feature: %s\n", brcmf_feat_names[id]);
		ifp->drvr->feat_flags |= BIT(id);
	} else {
		brcmf_dbg(TRACE, "%s feature check failed: %d\n",
			  brcmf_feat_names[id], err);
	}
}

void brcmf_feat_attach(struct brcmf_pub *drvr)
{
	struct brcmf_if *ifp = drvr->iflist[0];

	brcmf_feat_iovar_int_get(ifp, BRCMF_FEAT_MCHAN, "mchan");

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

	brcmf_debugfs_add_entry(drvr, "features", brcmf_feat_debugfs_read);
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
