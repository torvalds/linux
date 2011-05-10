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
#include <linux/types.h>

#include <bcmdefs.h>
#include <bcmutils.h>
#include <siutils.h>
#include <wlioctl.h>
#include <sbhnddma.h>

#include "d11.h"
#include "wlc_types.h"
#include "wlc_cfg.h"
#include "wlc_scb.h"
#include "wlc_pub.h"
#include "wlc_key.h"
#include "wlc_alloc.h"
#include "wl_dbg.h"
#include "wlc_rate.h"
#include "wlc_bsscfg.h"
#include "phy/wlc_phy_hal.h"
#include "wlc_channel.h"
#include "wlc_main.h"

static struct wlc_bsscfg *wlc_bsscfg_malloc(uint unit);
static void wlc_bsscfg_mfree(struct wlc_bsscfg *cfg);
static struct wlc_pub *wlc_pub_malloc(uint unit,
				      uint *err, uint devid);
static void wlc_pub_mfree(struct wlc_pub *pub);
static void wlc_tunables_init(wlc_tunables_t *tunables, uint devid);

void *wlc_calloc(uint unit, uint size)
{
	void *item;

	item = kzalloc(size, GFP_ATOMIC);
	if (item == NULL)
		WL_ERROR("wl%d: %s: out of memory\n", unit, __func__);
	return item;
}

void wlc_tunables_init(wlc_tunables_t *tunables, uint devid)
{
	tunables->ntxd = NTXD;
	tunables->nrxd = NRXD;
	tunables->rxbufsz = RXBUFSZ;
	tunables->nrxbufpost = NRXBUFPOST;
	tunables->maxscb = MAXSCB;
	tunables->ampdunummpdu = AMPDU_NUM_MPDU;
	tunables->maxpktcb = MAXPKTCB;
	tunables->maxucodebss = WLC_MAX_UCODE_BSS;
	tunables->maxucodebss4 = WLC_MAX_UCODE_BSS4;
	tunables->maxbss = MAXBSS;
	tunables->datahiwat = WLC_DATAHIWAT;
	tunables->ampdudatahiwat = WLC_AMPDUDATAHIWAT;
	tunables->rxbnd = RXBND;
	tunables->txsbnd = TXSBND;
}

static struct wlc_pub *wlc_pub_malloc(uint unit, uint *err, uint devid)
{
	struct wlc_pub *pub;

	pub = wlc_calloc(unit, sizeof(struct wlc_pub));
	if (pub == NULL) {
		*err = 1001;
		goto fail;
	}

	pub->tunables = wlc_calloc(unit,
		sizeof(wlc_tunables_t));
	if (pub->tunables == NULL) {
		*err = 1028;
		goto fail;
	}

	/* need to init the tunables now */
	wlc_tunables_init(pub->tunables, devid);

	pub->_cnt = wlc_calloc(unit, sizeof(struct wl_cnt));
	if (pub->_cnt == NULL)
		goto fail;

	pub->multicast = (u8 *)wlc_calloc(unit,
		(ETH_ALEN * MAXMULTILIST));
	if (pub->multicast == NULL) {
		*err = 1003;
		goto fail;
	}

	return pub;

 fail:
	wlc_pub_mfree(pub);
	return NULL;
}

static void wlc_pub_mfree(struct wlc_pub *pub)
{
	if (pub == NULL)
		return;

	kfree(pub->multicast);
	kfree(pub->_cnt);
	kfree(pub->tunables);
	kfree(pub);
}

static struct wlc_bsscfg *wlc_bsscfg_malloc(uint unit)
{
	struct wlc_bsscfg *cfg;

	cfg = (struct wlc_bsscfg *) wlc_calloc(unit, sizeof(struct wlc_bsscfg));
	if (cfg == NULL)
		goto fail;

	cfg->current_bss = (wlc_bss_info_t *)wlc_calloc(unit,
		sizeof(wlc_bss_info_t));
	if (cfg->current_bss == NULL)
		goto fail;

	return cfg;

 fail:
	wlc_bsscfg_mfree(cfg);
	return NULL;
}

static void wlc_bsscfg_mfree(struct wlc_bsscfg *cfg)
{
	if (cfg == NULL)
		return;

	kfree(cfg->maclist);
	kfree(cfg->current_bss);
	kfree(cfg);
}

void wlc_bsscfg_ID_assign(struct wlc_info *wlc, struct wlc_bsscfg *bsscfg)
{
	bsscfg->ID = wlc->next_bsscfg_ID;
	wlc->next_bsscfg_ID++;
}

/*
 * The common driver entry routine. Error codes should be unique
 */
struct wlc_info *wlc_attach_malloc(uint unit, uint *err, uint devid)
{
	struct wlc_info *wlc;

	wlc = (struct wlc_info *) wlc_calloc(unit, sizeof(struct wlc_info));
	if (wlc == NULL) {
		*err = 1002;
		goto fail;
	}

	wlc->hwrxoff = WL_HWRXOFF;

	/* allocate struct wlc_pub state structure */
	wlc->pub = wlc_pub_malloc(unit, err, devid);
	if (wlc->pub == NULL) {
		*err = 1003;
		goto fail;
	}
	wlc->pub->wlc = wlc;

	/* allocate struct wlc_hw_info state structure */

	wlc->hw = (struct wlc_hw_info *)wlc_calloc(unit,
			sizeof(struct wlc_hw_info));
	if (wlc->hw == NULL) {
		*err = 1005;
		goto fail;
	}
	wlc->hw->wlc = wlc;

	wlc->hw->bandstate[0] = wlc_calloc(unit,
		(sizeof(struct wlc_hwband) * MAXBANDS));
	if (wlc->hw->bandstate[0] == NULL) {
		*err = 1006;
		goto fail;
	} else {
		int i;

		for (i = 1; i < MAXBANDS; i++) {
			wlc->hw->bandstate[i] = (struct wlc_hwband *)
			    ((unsigned long)wlc->hw->bandstate[0] +
			     (sizeof(struct wlc_hwband) * i));
		}
	}

	wlc->modulecb = wlc_calloc(unit,
		sizeof(struct modulecb) * WLC_MAXMODULES);
	if (wlc->modulecb == NULL) {
		*err = 1009;
		goto fail;
	}

	wlc->default_bss = (wlc_bss_info_t *)wlc_calloc(unit,
		sizeof(wlc_bss_info_t));
	if (wlc->default_bss == NULL) {
		*err = 1010;
		goto fail;
	}

	wlc->cfg = wlc_bsscfg_malloc(unit);
	if (wlc->cfg == NULL) {
		*err = 1011;
		goto fail;
	}
	wlc_bsscfg_ID_assign(wlc, wlc->cfg);

	wlc->pkt_callback = wlc_calloc(unit,
		(sizeof(struct pkt_cb) * (wlc->pub->tunables->maxpktcb + 1)));
	if (wlc->pkt_callback == NULL) {
		*err = 1013;
		goto fail;
	}

	wlc->wsec_def_keys[0] = (wsec_key_t *)wlc_calloc(unit,
		(sizeof(wsec_key_t) * WLC_DEFAULT_KEYS));
	if (wlc->wsec_def_keys[0] == NULL) {
		*err = 1015;
		goto fail;
	} else {
		int i;
		for (i = 1; i < WLC_DEFAULT_KEYS; i++) {
			wlc->wsec_def_keys[i] = (wsec_key_t *)
			    ((unsigned long)wlc->wsec_def_keys[0] +
			     (sizeof(wsec_key_t) * i));
		}
	}

	wlc->protection = wlc_calloc(unit,
		sizeof(struct wlc_protection));
	if (wlc->protection == NULL) {
		*err = 1016;
		goto fail;
	}

	wlc->stf = wlc_calloc(unit, sizeof(struct wlc_stf));
	if (wlc->stf == NULL) {
		*err = 1017;
		goto fail;
	}

	wlc->bandstate[0] = (struct wlcband *)wlc_calloc(unit,
				(sizeof(struct wlcband)*MAXBANDS));
	if (wlc->bandstate[0] == NULL) {
		*err = 1025;
		goto fail;
	} else {
		int i;

		for (i = 1; i < MAXBANDS; i++) {
			wlc->bandstate[i] =
			    (struct wlcband *) ((unsigned long)wlc->bandstate[0]
			    + (sizeof(struct wlcband)*i));
		}
	}

	wlc->corestate = (struct wlccore *)wlc_calloc(unit,
						      sizeof(struct wlccore));
	if (wlc->corestate == NULL) {
		*err = 1026;
		goto fail;
	}

	wlc->corestate->macstat_snapshot =
		(macstat_t *)wlc_calloc(unit, sizeof(macstat_t));
	if (wlc->corestate->macstat_snapshot == NULL) {
		*err = 1027;
		goto fail;
	}

	return wlc;

 fail:
	wlc_detach_mfree(wlc);
	return NULL;
}

void wlc_detach_mfree(struct wlc_info *wlc)
{
	if (wlc == NULL)
		return;

	wlc_bsscfg_mfree(wlc->cfg);
	wlc_pub_mfree(wlc->pub);
	kfree(wlc->modulecb);
	kfree(wlc->default_bss);
	kfree(wlc->pkt_callback);
	kfree(wlc->wsec_def_keys[0]);
	kfree(wlc->protection);
	kfree(wlc->stf);
	kfree(wlc->bandstate[0]);
	kfree(wlc->corestate->macstat_snapshot);
	kfree(wlc->corestate);
	kfree(wlc->hw->bandstate[0]);
	kfree(wlc->hw);

	/* free the wlc */
	kfree(wlc);
	wlc = NULL;
}
