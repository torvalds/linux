/*
 * FST module - FST interface object implementation
 * Copyright (c) 2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"
#include "utils/common.h"
#include "fst/fst_internal.h"
#include "fst/fst_defs.h"


struct fst_iface * fst_iface_create(struct fst_group *g, const char *ifname,
				    const u8 *own_addr,
				    const struct fst_wpa_obj *iface_obj,
				    const struct fst_iface_cfg *cfg)
{
	struct fst_iface *i;

	i = os_zalloc(sizeof(*i));
	if (!i) {
		fst_printf_group(g, MSG_ERROR, "cannot allocate iface for %s",
				ifname);
		return NULL;
	}

	i->cfg = *cfg;
	i->iface_obj = *iface_obj;
	i->group = g;
	os_strlcpy(i->ifname, ifname, sizeof(i->ifname));
	os_memcpy(i->own_addr, own_addr, sizeof(i->own_addr));

	if (!i->cfg.llt) {
		fst_printf_iface(i, MSG_WARNING, "Zero llt adjusted");
		i->cfg.llt = FST_DEFAULT_LLT_CFG_VALUE;
	}

	return i;
}


void fst_iface_delete(struct fst_iface *i)
{
	fst_iface_set_ies(i, NULL);
	wpabuf_free(i->mb_ie);
	os_free(i);
}


Boolean fst_iface_is_connected(struct fst_iface *iface, const u8 *addr,
			       Boolean mb_only)
{
	struct fst_get_peer_ctx *ctx;
	const u8 *a = fst_iface_get_peer_first(iface, &ctx, mb_only);

	for (; a != NULL; a = fst_iface_get_peer_next(iface, &ctx, mb_only))
		if (os_memcmp(addr, a, ETH_ALEN) == 0)
			return TRUE;

	return FALSE;
}


void fst_iface_attach_mbie(struct fst_iface *i, struct wpabuf *mbie)
{
	wpabuf_free(i->mb_ie);
	i->mb_ie = mbie;
}


enum mb_band_id fst_iface_get_band_id(struct fst_iface *i)
{
	enum hostapd_hw_mode hw_mode;
	u8 channel;

	fst_iface_get_channel_info(i, &hw_mode, &channel);
	return fst_hw_mode_to_band(hw_mode);
}
