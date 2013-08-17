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

#include <linux/slab.h>
#include <net/mac80211.h>

#include "types.h"
#include "main.h"
#include "phy_shim.h"
#include "antsel.h"

#define ANT_SELCFG_AUTO		0x80	/* bit indicates antenna sel AUTO */
#define ANT_SELCFG_MASK		0x33	/* antenna configuration mask */
#define ANT_SELCFG_TX_UNICAST	0	/* unicast tx antenna configuration */
#define ANT_SELCFG_RX_UNICAST	1	/* unicast rx antenna configuration */
#define ANT_SELCFG_TX_DEF	2	/* default tx antenna configuration */
#define ANT_SELCFG_RX_DEF	3	/* default rx antenna configuration */

/* useful macros */
#define BRCMS_ANTSEL_11N_0(ant)	((((ant) & ANT_SELCFG_MASK) >> 4) & 0xf)
#define BRCMS_ANTSEL_11N_1(ant)	(((ant) & ANT_SELCFG_MASK) & 0xf)
#define BRCMS_ANTIDX_11N(ant)	(((BRCMS_ANTSEL_11N_0(ant)) << 2) +\
				(BRCMS_ANTSEL_11N_1(ant)))
#define BRCMS_ANT_ISAUTO_11N(ant) (((ant) & ANT_SELCFG_AUTO) == ANT_SELCFG_AUTO)
#define BRCMS_ANTSEL_11N(ant)	((ant) & ANT_SELCFG_MASK)

/* antenna switch */
/* defines for no boardlevel antenna diversity */
#define ANT_SELCFG_DEF_2x2	0x01	/* default antenna configuration */

/* 2x3 antdiv defines and tables for GPIO communication */
#define ANT_SELCFG_NUM_2x3	3
#define ANT_SELCFG_DEF_2x3	0x01	/* default antenna configuration */

/* 2x4 antdiv rev4 defines and tables for GPIO communication */
#define ANT_SELCFG_NUM_2x4	4
#define ANT_SELCFG_DEF_2x4	0x02	/* default antenna configuration */

static const u16 mimo_2x4_div_antselpat_tbl[] = {
	0, 0, 0x9, 0xa,		/* ant0: 0 ant1: 2,3 */
	0, 0, 0x5, 0x6,		/* ant0: 1 ant1: 2,3 */
	0, 0, 0, 0,		/* n.a.              */
	0, 0, 0, 0		/* n.a.              */
};

static const u8 mimo_2x4_div_antselid_tbl[16] = {
	0, 0, 0, 0, 0, 2, 3, 0,
	0, 0, 1, 0, 0, 0, 0, 0	/* pat to antselid */
};

static const u16 mimo_2x3_div_antselpat_tbl[] = {
	16, 0, 1, 16,		/* ant0: 0 ant1: 1,2 */
	16, 16, 16, 16,		/* n.a.              */
	16, 2, 16, 16,		/* ant0: 2 ant1: 1   */
	16, 16, 16, 16		/* n.a.              */
};

static const u8 mimo_2x3_div_antselid_tbl[16] = {
	0, 1, 2, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0	/* pat to antselid */
};

/* boardlevel antenna selection: init antenna selection structure */
static void
brcms_c_antsel_init_cfg(struct antsel_info *asi, struct brcms_antselcfg *antsel,
		    bool auto_sel)
{
	if (asi->antsel_type == ANTSEL_2x3) {
		u8 antcfg_def = ANT_SELCFG_DEF_2x3 |
		    ((asi->antsel_avail && auto_sel) ? ANT_SELCFG_AUTO : 0);
		antsel->ant_config[ANT_SELCFG_TX_DEF] = antcfg_def;
		antsel->ant_config[ANT_SELCFG_TX_UNICAST] = antcfg_def;
		antsel->ant_config[ANT_SELCFG_RX_DEF] = antcfg_def;
		antsel->ant_config[ANT_SELCFG_RX_UNICAST] = antcfg_def;
		antsel->num_antcfg = ANT_SELCFG_NUM_2x3;

	} else if (asi->antsel_type == ANTSEL_2x4) {

		antsel->ant_config[ANT_SELCFG_TX_DEF] = ANT_SELCFG_DEF_2x4;
		antsel->ant_config[ANT_SELCFG_TX_UNICAST] = ANT_SELCFG_DEF_2x4;
		antsel->ant_config[ANT_SELCFG_RX_DEF] = ANT_SELCFG_DEF_2x4;
		antsel->ant_config[ANT_SELCFG_RX_UNICAST] = ANT_SELCFG_DEF_2x4;
		antsel->num_antcfg = ANT_SELCFG_NUM_2x4;

	} else {		/* no antenna selection available */

		antsel->ant_config[ANT_SELCFG_TX_DEF] = ANT_SELCFG_DEF_2x2;
		antsel->ant_config[ANT_SELCFG_TX_UNICAST] = ANT_SELCFG_DEF_2x2;
		antsel->ant_config[ANT_SELCFG_RX_DEF] = ANT_SELCFG_DEF_2x2;
		antsel->ant_config[ANT_SELCFG_RX_UNICAST] = ANT_SELCFG_DEF_2x2;
		antsel->num_antcfg = 0;
	}
}

struct antsel_info *brcms_c_antsel_attach(struct brcms_c_info *wlc)
{
	struct antsel_info *asi;
	struct si_pub *sih = wlc->hw->sih;

	asi = kzalloc(sizeof(struct antsel_info), GFP_ATOMIC);
	if (!asi)
		return NULL;

	asi->wlc = wlc;
	asi->pub = wlc->pub;
	asi->antsel_type = ANTSEL_NA;
	asi->antsel_avail = false;
	asi->antsel_antswitch = (u8) getintvar(sih, BRCMS_SROM_ANTSWITCH);

	if ((asi->pub->sromrev >= 4) && (asi->antsel_antswitch != 0)) {
		switch (asi->antsel_antswitch) {
		case ANTSWITCH_TYPE_1:
		case ANTSWITCH_TYPE_2:
		case ANTSWITCH_TYPE_3:
			/* 4321/2 board with 2x3 switch logic */
			asi->antsel_type = ANTSEL_2x3;
			/* Antenna selection availability */
			if (((u16) getintvar(sih, BRCMS_SROM_AA2G) == 7) ||
			    ((u16) getintvar(sih, BRCMS_SROM_AA5G) == 7)) {
				asi->antsel_avail = true;
			} else if (
				(u16) getintvar(sih, BRCMS_SROM_AA2G) == 3 ||
				(u16) getintvar(sih, BRCMS_SROM_AA5G) == 3) {
				asi->antsel_avail = false;
			} else {
				asi->antsel_avail = false;
				wiphy_err(wlc->wiphy, "antsel_attach: 2o3 "
					  "board cfg invalid\n");
			}

			break;
		default:
			break;
		}
	} else if ((asi->pub->sromrev == 4) &&
		   ((u16) getintvar(sih, BRCMS_SROM_AA2G) == 7) &&
		   ((u16) getintvar(sih, BRCMS_SROM_AA5G) == 0)) {
		/* hack to match old 4321CB2 cards with 2of3 antenna switch */
		asi->antsel_type = ANTSEL_2x3;
		asi->antsel_avail = true;
	} else if (asi->pub->boardflags2 & BFL2_2X4_DIV) {
		asi->antsel_type = ANTSEL_2x4;
		asi->antsel_avail = true;
	}

	/* Set the antenna selection type for the low driver */
	brcms_b_antsel_type_set(wlc->hw, asi->antsel_type);

	/* Init (auto/manual) antenna selection */
	brcms_c_antsel_init_cfg(asi, &asi->antcfg_11n, true);
	brcms_c_antsel_init_cfg(asi, &asi->antcfg_cur, true);

	return asi;
}

void brcms_c_antsel_detach(struct antsel_info *asi)
{
	kfree(asi);
}

/*
 * boardlevel antenna selection:
 *   convert ant_cfg to mimo_antsel (ucode interface)
 */
static u16 brcms_c_antsel_antcfg2antsel(struct antsel_info *asi, u8 ant_cfg)
{
	u8 idx = BRCMS_ANTIDX_11N(BRCMS_ANTSEL_11N(ant_cfg));
	u16 mimo_antsel = 0;

	if (asi->antsel_type == ANTSEL_2x4) {
		/* 2x4 antenna diversity board, 4 cfgs: 0-2 0-3 1-2 1-3 */
		mimo_antsel = (mimo_2x4_div_antselpat_tbl[idx] & 0xf);
		return mimo_antsel;

	} else if (asi->antsel_type == ANTSEL_2x3) {
		/* 2x3 antenna selection, 3 cfgs: 0-1 0-2 2-1 */
		mimo_antsel = (mimo_2x3_div_antselpat_tbl[idx] & 0xf);
		return mimo_antsel;
	}

	return mimo_antsel;
}

/* boardlevel antenna selection: ucode interface control */
static int brcms_c_antsel_cfgupd(struct antsel_info *asi,
				 struct brcms_antselcfg *antsel)
{
	struct brcms_c_info *wlc = asi->wlc;
	u8 ant_cfg;
	u16 mimo_antsel;

	/* 1) Update TX antconfig for all frames that are not unicast data
	 *    (aka default TX)
	 */
	ant_cfg = antsel->ant_config[ANT_SELCFG_TX_DEF];
	mimo_antsel = brcms_c_antsel_antcfg2antsel(asi, ant_cfg);
	brcms_b_write_shm(wlc->hw, M_MIMO_ANTSEL_TXDFLT, mimo_antsel);
	/*
	 * Update driver stats for currently selected
	 * default tx/rx antenna config
	 */
	asi->antcfg_cur.ant_config[ANT_SELCFG_TX_DEF] = ant_cfg;

	/* 2) Update RX antconfig for all frames that are not unicast data
	 *    (aka default RX)
	 */
	ant_cfg = antsel->ant_config[ANT_SELCFG_RX_DEF];
	mimo_antsel = brcms_c_antsel_antcfg2antsel(asi, ant_cfg);
	brcms_b_write_shm(wlc->hw, M_MIMO_ANTSEL_RXDFLT, mimo_antsel);
	/*
	 * Update driver stats for currently selected
	 * default tx/rx antenna config
	 */
	asi->antcfg_cur.ant_config[ANT_SELCFG_RX_DEF] = ant_cfg;

	return 0;
}

void brcms_c_antsel_init(struct antsel_info *asi)
{
	if ((asi->antsel_type == ANTSEL_2x3) ||
	    (asi->antsel_type == ANTSEL_2x4))
		brcms_c_antsel_cfgupd(asi, &asi->antcfg_11n);
}

/* boardlevel antenna selection: convert id to ant_cfg */
static u8 brcms_c_antsel_id2antcfg(struct antsel_info *asi, u8 id)
{
	u8 antcfg = ANT_SELCFG_DEF_2x2;

	if (asi->antsel_type == ANTSEL_2x4) {
		/* 2x4 antenna diversity board, 4 cfgs: 0-2 0-3 1-2 1-3 */
		antcfg = (((id & 0x2) << 3) | ((id & 0x1) + 2));
		return antcfg;

	} else if (asi->antsel_type == ANTSEL_2x3) {
		/* 2x3 antenna selection, 3 cfgs: 0-1 0-2 2-1 */
		antcfg = (((id & 0x02) << 4) | ((id & 0x1) + 1));
		return antcfg;
	}

	return antcfg;
}

void
brcms_c_antsel_antcfg_get(struct antsel_info *asi, bool usedef, bool sel,
		      u8 antselid, u8 fbantselid, u8 *antcfg,
		      u8 *fbantcfg)
{
	u8 ant;

	/* if use default, assign it and return */
	if (usedef) {
		*antcfg = asi->antcfg_11n.ant_config[ANT_SELCFG_TX_DEF];
		*fbantcfg = *antcfg;
		return;
	}

	if (!sel) {
		*antcfg = asi->antcfg_11n.ant_config[ANT_SELCFG_TX_UNICAST];
		*fbantcfg = *antcfg;

	} else {
		ant = asi->antcfg_11n.ant_config[ANT_SELCFG_TX_UNICAST];
		if ((ant & ANT_SELCFG_AUTO) == ANT_SELCFG_AUTO) {
			*antcfg = brcms_c_antsel_id2antcfg(asi, antselid);
			*fbantcfg = brcms_c_antsel_id2antcfg(asi, fbantselid);
		} else {
			*antcfg =
			    asi->antcfg_11n.ant_config[ANT_SELCFG_TX_UNICAST];
			*fbantcfg = *antcfg;
		}
	}
	return;
}

/* boardlevel antenna selection: convert mimo_antsel (ucode interface) to id */
u8 brcms_c_antsel_antsel2id(struct antsel_info *asi, u16 antsel)
{
	u8 antselid = 0;

	if (asi->antsel_type == ANTSEL_2x4) {
		/* 2x4 antenna diversity board, 4 cfgs: 0-2 0-3 1-2 1-3 */
		antselid = mimo_2x4_div_antselid_tbl[(antsel & 0xf)];
		return antselid;

	} else if (asi->antsel_type == ANTSEL_2x3) {
		/* 2x3 antenna selection, 3 cfgs: 0-1 0-2 2-1 */
		antselid = mimo_2x3_div_antselid_tbl[(antsel & 0xf)];
		return antselid;
	}

	return antselid;
}
