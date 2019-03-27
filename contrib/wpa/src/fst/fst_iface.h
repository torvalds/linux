/*
 * FST module - FST interface object definitions
 * Copyright (c) 2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */


#ifndef FST_IFACE_H
#define FST_IFACE_H

#include "utils/includes.h"
#include "utils/common.h"
#include "list.h"
#include "fst.h"

struct fst_iface {
	struct fst_group *group;
	struct fst_wpa_obj iface_obj;
	u8 own_addr[ETH_ALEN];
	struct wpabuf *mb_ie;
	char ifname[IFNAMSIZ + 1];
	struct fst_iface_cfg cfg;
	struct dl_list group_lentry;
};

struct fst_iface * fst_iface_create(struct fst_group *g, const char *ifname,
				    const u8 *own_addr,
				    const struct fst_wpa_obj *iface_obj,
				    const struct fst_iface_cfg *cfg);
void fst_iface_delete(struct fst_iface *i);

static inline struct fst_group * fst_iface_get_group(struct fst_iface *i)
{
	return i->group;
}

static inline const char * fst_iface_get_name(struct fst_iface *i)
{
	return i->ifname;
}

static inline const u8 * fst_iface_get_addr(struct fst_iface *i)
{
	return i->own_addr;
}

static inline const char * fst_iface_get_group_id(struct fst_iface *i)
{
	return i->cfg.group_id;
}

static inline u8 fst_iface_get_priority(struct fst_iface *i)
{
	return i->cfg.priority;
}

static inline u32 fst_iface_get_llt(struct fst_iface *i)
{
	return i->cfg.llt;
}

static inline const struct wpabuf * fst_iface_get_mbie(struct fst_iface *i)
{
	return i->mb_ie;
}

static inline const u8 * fst_iface_get_bssid(struct fst_iface *i)
{
	return i->iface_obj.get_bssid(i->iface_obj.ctx);
}

static inline void fst_iface_get_channel_info(struct fst_iface *i,
					      enum hostapd_hw_mode *hw_mode,
					      u8 *channel)
{
	i->iface_obj.get_channel_info(i->iface_obj.ctx, hw_mode, channel);
}

static inline int fst_iface_get_hw_modes(struct fst_iface *i,
					 struct hostapd_hw_modes **modes)
{
	return i->iface_obj.get_hw_modes(i->iface_obj.ctx, modes);
}

static inline void fst_iface_set_ies(struct fst_iface *i,
				     const struct wpabuf *fst_ies)
{
	i->iface_obj.set_ies(i->iface_obj.ctx, fst_ies);
}

static inline int fst_iface_send_action(struct fst_iface *i,
					const u8 *addr, struct wpabuf *data)
{
	return i->iface_obj.send_action(i->iface_obj.ctx, addr, data);
}

static inline const struct wpabuf *
fst_iface_get_peer_mb_ie(struct fst_iface *i, const u8 *addr)
{
	return i->iface_obj.get_mb_ie(i->iface_obj.ctx, addr);
}

static inline void fst_iface_update_mb_ie(struct fst_iface *i,
					  const u8 *addr,
					  const u8 *buf, size_t size)
{
	i->iface_obj.update_mb_ie(i->iface_obj.ctx, addr, buf, size);
}

static inline const u8 * fst_iface_get_peer_first(struct fst_iface *i,
						  struct fst_get_peer_ctx **ctx,
						  Boolean mb_only)
{
	return i->iface_obj.get_peer_first(i->iface_obj.ctx, ctx, mb_only);
}

static inline const u8 * fst_iface_get_peer_next(struct fst_iface *i,
						 struct fst_get_peer_ctx **ctx,
						 Boolean mb_only)
{
	return i->iface_obj.get_peer_next(i->iface_obj.ctx, ctx, mb_only);
}

Boolean fst_iface_is_connected(struct fst_iface *iface, const u8 *addr,
			       Boolean mb_only);
void fst_iface_attach_mbie(struct fst_iface *i, struct wpabuf *mbie);
enum mb_band_id fst_iface_get_band_id(struct fst_iface *i);

static inline void * fst_iface_get_wpa_obj_ctx(struct fst_iface *i)
{
	return i->iface_obj.ctx;
}

#endif /* FST_IFACE_H */
