/*
 * FST module - FST group object definitions
 * Copyright (c) 2014, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef FST_GROUP_H
#define FST_GROUP_H

struct fst_group {
	char group_id[IFNAMSIZ + 1];
	struct dl_list ifaces;
	u8 dialog_token;
	u32 fsts_id;
	struct dl_list global_groups_lentry;
};

struct session_transition_ie;

#define foreach_fst_group_iface(g, i) \
	dl_list_for_each((i), &(g)->ifaces, struct fst_iface, group_lentry)

struct fst_group * fst_group_create(const char *group_id);
void fst_group_attach_iface(struct fst_group *g, struct fst_iface *i);
void fst_group_detach_iface(struct fst_group *g, struct fst_iface *i);
void fst_group_delete(struct fst_group *g);

void fst_group_update_ie(struct fst_group *g);

static inline Boolean fst_group_has_ifaces(struct fst_group *g)
{
	return !dl_list_empty(&g->ifaces);
}

static inline struct fst_iface * fst_group_first_iface(struct fst_group *g)
{
	return dl_list_first(&g->ifaces, struct fst_iface, group_lentry);
}

static inline const char * fst_group_get_id(struct fst_group *g)
{
	return g->group_id;
}

Boolean fst_group_delete_if_empty(struct fst_group *group);
struct fst_iface * fst_group_get_iface_by_name(struct fst_group *g,
					       const char *ifname);
struct fst_iface *
fst_group_get_peer_other_connection(struct fst_iface *iface,
				    const u8 *peer_addr, u8 band_id,
				    u8 *other_peer_addr);
u8  fst_group_assign_dialog_token(struct fst_group *g);
u32 fst_group_assign_fsts_id(struct fst_group *g);

extern struct dl_list fst_global_groups_list;

#define foreach_fst_group(g) \
	dl_list_for_each((g), &fst_global_groups_list, \
			 struct fst_group, global_groups_lentry)

static inline struct fst_group * fst_first_group(void)
{
	return dl_list_first(&fst_global_groups_list, struct fst_group,
			     global_groups_lentry);
}

#endif /* FST_GROUP_H */
