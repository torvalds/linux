// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/slab.h>
#include <net/inet_ecn.h>
#include <net/ipv6.h>

#include "reg.h"
#include "spectrum.h"
#include "spectrum_nve.h"

const struct mlxsw_sp_nve_ops *mlxsw_sp1_nve_ops_arr[] = {
	[MLXSW_SP_NVE_TYPE_VXLAN]	= &mlxsw_sp1_nve_vxlan_ops,
};

const struct mlxsw_sp_nve_ops *mlxsw_sp2_nve_ops_arr[] = {
	[MLXSW_SP_NVE_TYPE_VXLAN]	= &mlxsw_sp2_nve_vxlan_ops,
};

struct mlxsw_sp_nve_mc_entry;
struct mlxsw_sp_nve_mc_record;
struct mlxsw_sp_nve_mc_list;

struct mlxsw_sp_nve_mc_record_ops {
	enum mlxsw_reg_tnumt_record_type type;
	int (*entry_add)(struct mlxsw_sp_nve_mc_record *mc_record,
			 struct mlxsw_sp_nve_mc_entry *mc_entry,
			 const union mlxsw_sp_l3addr *addr);
	void (*entry_del)(const struct mlxsw_sp_nve_mc_record *mc_record,
			  const struct mlxsw_sp_nve_mc_entry *mc_entry);
	void (*entry_set)(const struct mlxsw_sp_nve_mc_record *mc_record,
			  const struct mlxsw_sp_nve_mc_entry *mc_entry,
			  char *tnumt_pl, unsigned int entry_index);
	bool (*entry_compare)(const struct mlxsw_sp_nve_mc_record *mc_record,
			      const struct mlxsw_sp_nve_mc_entry *mc_entry,
			      const union mlxsw_sp_l3addr *addr);
};

struct mlxsw_sp_nve_mc_list_key {
	u16 fid_index;
};

struct mlxsw_sp_nve_mc_ipv6_entry {
	struct in6_addr addr6;
	u32 addr6_kvdl_index;
};

struct mlxsw_sp_nve_mc_entry {
	union {
		__be32 addr4;
		struct mlxsw_sp_nve_mc_ipv6_entry ipv6_entry;
	};
	u8 valid:1;
};

struct mlxsw_sp_nve_mc_record {
	struct list_head list;
	enum mlxsw_sp_l3proto proto;
	unsigned int num_entries;
	struct mlxsw_sp *mlxsw_sp;
	struct mlxsw_sp_nve_mc_list *mc_list;
	const struct mlxsw_sp_nve_mc_record_ops *ops;
	u32 kvdl_index;
	struct mlxsw_sp_nve_mc_entry entries[0];
};

struct mlxsw_sp_nve_mc_list {
	struct list_head records_list;
	struct rhash_head ht_node;
	struct mlxsw_sp_nve_mc_list_key key;
};

static const struct rhashtable_params mlxsw_sp_nve_mc_list_ht_params = {
	.key_len = sizeof(struct mlxsw_sp_nve_mc_list_key),
	.key_offset = offsetof(struct mlxsw_sp_nve_mc_list, key),
	.head_offset = offsetof(struct mlxsw_sp_nve_mc_list, ht_node),
};

static int
mlxsw_sp_nve_mc_record_ipv4_entry_add(struct mlxsw_sp_nve_mc_record *mc_record,
				      struct mlxsw_sp_nve_mc_entry *mc_entry,
				      const union mlxsw_sp_l3addr *addr)
{
	mc_entry->addr4 = addr->addr4;

	return 0;
}

static void
mlxsw_sp_nve_mc_record_ipv4_entry_del(const struct mlxsw_sp_nve_mc_record *mc_record,
				      const struct mlxsw_sp_nve_mc_entry *mc_entry)
{
}

static void
mlxsw_sp_nve_mc_record_ipv4_entry_set(const struct mlxsw_sp_nve_mc_record *mc_record,
				      const struct mlxsw_sp_nve_mc_entry *mc_entry,
				      char *tnumt_pl, unsigned int entry_index)
{
	u32 udip = be32_to_cpu(mc_entry->addr4);

	mlxsw_reg_tnumt_udip_set(tnumt_pl, entry_index, udip);
}

static bool
mlxsw_sp_nve_mc_record_ipv4_entry_compare(const struct mlxsw_sp_nve_mc_record *mc_record,
					  const struct mlxsw_sp_nve_mc_entry *mc_entry,
					  const union mlxsw_sp_l3addr *addr)
{
	return mc_entry->addr4 == addr->addr4;
}

static const struct mlxsw_sp_nve_mc_record_ops
mlxsw_sp_nve_mc_record_ipv4_ops = {
	.type		= MLXSW_REG_TNUMT_RECORD_TYPE_IPV4,
	.entry_add	= &mlxsw_sp_nve_mc_record_ipv4_entry_add,
	.entry_del	= &mlxsw_sp_nve_mc_record_ipv4_entry_del,
	.entry_set	= &mlxsw_sp_nve_mc_record_ipv4_entry_set,
	.entry_compare	= &mlxsw_sp_nve_mc_record_ipv4_entry_compare,
};

static int
mlxsw_sp_nve_mc_record_ipv6_entry_add(struct mlxsw_sp_nve_mc_record *mc_record,
				      struct mlxsw_sp_nve_mc_entry *mc_entry,
				      const union mlxsw_sp_l3addr *addr)
{
	WARN_ON(1);

	return -EINVAL;
}

static void
mlxsw_sp_nve_mc_record_ipv6_entry_del(const struct mlxsw_sp_nve_mc_record *mc_record,
				      const struct mlxsw_sp_nve_mc_entry *mc_entry)
{
}

static void
mlxsw_sp_nve_mc_record_ipv6_entry_set(const struct mlxsw_sp_nve_mc_record *mc_record,
				      const struct mlxsw_sp_nve_mc_entry *mc_entry,
				      char *tnumt_pl, unsigned int entry_index)
{
	u32 udip_ptr = mc_entry->ipv6_entry.addr6_kvdl_index;

	mlxsw_reg_tnumt_udip_ptr_set(tnumt_pl, entry_index, udip_ptr);
}

static bool
mlxsw_sp_nve_mc_record_ipv6_entry_compare(const struct mlxsw_sp_nve_mc_record *mc_record,
					  const struct mlxsw_sp_nve_mc_entry *mc_entry,
					  const union mlxsw_sp_l3addr *addr)
{
	return ipv6_addr_equal(&mc_entry->ipv6_entry.addr6, &addr->addr6);
}

static const struct mlxsw_sp_nve_mc_record_ops
mlxsw_sp_nve_mc_record_ipv6_ops = {
	.type		= MLXSW_REG_TNUMT_RECORD_TYPE_IPV6,
	.entry_add	= &mlxsw_sp_nve_mc_record_ipv6_entry_add,
	.entry_del	= &mlxsw_sp_nve_mc_record_ipv6_entry_del,
	.entry_set	= &mlxsw_sp_nve_mc_record_ipv6_entry_set,
	.entry_compare	= &mlxsw_sp_nve_mc_record_ipv6_entry_compare,
};

static const struct mlxsw_sp_nve_mc_record_ops *
mlxsw_sp_nve_mc_record_ops_arr[] = {
	[MLXSW_SP_L3_PROTO_IPV4] = &mlxsw_sp_nve_mc_record_ipv4_ops,
	[MLXSW_SP_L3_PROTO_IPV6] = &mlxsw_sp_nve_mc_record_ipv6_ops,
};

int mlxsw_sp_nve_learned_ip_resolve(struct mlxsw_sp *mlxsw_sp, u32 uip,
				    enum mlxsw_sp_l3proto proto,
				    union mlxsw_sp_l3addr *addr)
{
	switch (proto) {
	case MLXSW_SP_L3_PROTO_IPV4:
		addr->addr4 = cpu_to_be32(uip);
		return 0;
	default:
		WARN_ON(1);
		return -EINVAL;
	}
}

static struct mlxsw_sp_nve_mc_list *
mlxsw_sp_nve_mc_list_find(struct mlxsw_sp *mlxsw_sp,
			  const struct mlxsw_sp_nve_mc_list_key *key)
{
	struct mlxsw_sp_nve *nve = mlxsw_sp->nve;

	return rhashtable_lookup_fast(&nve->mc_list_ht, key,
				      mlxsw_sp_nve_mc_list_ht_params);
}

static struct mlxsw_sp_nve_mc_list *
mlxsw_sp_nve_mc_list_create(struct mlxsw_sp *mlxsw_sp,
			    const struct mlxsw_sp_nve_mc_list_key *key)
{
	struct mlxsw_sp_nve *nve = mlxsw_sp->nve;
	struct mlxsw_sp_nve_mc_list *mc_list;
	int err;

	mc_list = kmalloc(sizeof(*mc_list), GFP_KERNEL);
	if (!mc_list)
		return ERR_PTR(-ENOMEM);

	INIT_LIST_HEAD(&mc_list->records_list);
	mc_list->key = *key;

	err = rhashtable_insert_fast(&nve->mc_list_ht, &mc_list->ht_node,
				     mlxsw_sp_nve_mc_list_ht_params);
	if (err)
		goto err_rhashtable_insert;

	return mc_list;

err_rhashtable_insert:
	kfree(mc_list);
	return ERR_PTR(err);
}

static void mlxsw_sp_nve_mc_list_destroy(struct mlxsw_sp *mlxsw_sp,
					 struct mlxsw_sp_nve_mc_list *mc_list)
{
	struct mlxsw_sp_nve *nve = mlxsw_sp->nve;

	rhashtable_remove_fast(&nve->mc_list_ht, &mc_list->ht_node,
			       mlxsw_sp_nve_mc_list_ht_params);
	WARN_ON(!list_empty(&mc_list->records_list));
	kfree(mc_list);
}

static struct mlxsw_sp_nve_mc_list *
mlxsw_sp_nve_mc_list_get(struct mlxsw_sp *mlxsw_sp,
			 const struct mlxsw_sp_nve_mc_list_key *key)
{
	struct mlxsw_sp_nve_mc_list *mc_list;

	mc_list = mlxsw_sp_nve_mc_list_find(mlxsw_sp, key);
	if (mc_list)
		return mc_list;

	return mlxsw_sp_nve_mc_list_create(mlxsw_sp, key);
}

static void
mlxsw_sp_nve_mc_list_put(struct mlxsw_sp *mlxsw_sp,
			 struct mlxsw_sp_nve_mc_list *mc_list)
{
	if (!list_empty(&mc_list->records_list))
		return;
	mlxsw_sp_nve_mc_list_destroy(mlxsw_sp, mc_list);
}

static struct mlxsw_sp_nve_mc_record *
mlxsw_sp_nve_mc_record_create(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_nve_mc_list *mc_list,
			      enum mlxsw_sp_l3proto proto)
{
	unsigned int num_max_entries = mlxsw_sp->nve->num_max_mc_entries[proto];
	struct mlxsw_sp_nve_mc_record *mc_record;
	int err;

	mc_record = kzalloc(sizeof(*mc_record) + num_max_entries *
			    sizeof(struct mlxsw_sp_nve_mc_entry), GFP_KERNEL);
	if (!mc_record)
		return ERR_PTR(-ENOMEM);

	err = mlxsw_sp_kvdl_alloc(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_TNUMT, 1,
				  &mc_record->kvdl_index);
	if (err)
		goto err_kvdl_alloc;

	mc_record->ops = mlxsw_sp_nve_mc_record_ops_arr[proto];
	mc_record->mlxsw_sp = mlxsw_sp;
	mc_record->mc_list = mc_list;
	mc_record->proto = proto;
	list_add_tail(&mc_record->list, &mc_list->records_list);

	return mc_record;

err_kvdl_alloc:
	kfree(mc_record);
	return ERR_PTR(err);
}

static void
mlxsw_sp_nve_mc_record_destroy(struct mlxsw_sp_nve_mc_record *mc_record)
{
	struct mlxsw_sp *mlxsw_sp = mc_record->mlxsw_sp;

	list_del(&mc_record->list);
	mlxsw_sp_kvdl_free(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_TNUMT, 1,
			   mc_record->kvdl_index);
	WARN_ON(mc_record->num_entries);
	kfree(mc_record);
}

static struct mlxsw_sp_nve_mc_record *
mlxsw_sp_nve_mc_record_get(struct mlxsw_sp *mlxsw_sp,
			   struct mlxsw_sp_nve_mc_list *mc_list,
			   enum mlxsw_sp_l3proto proto)
{
	struct mlxsw_sp_nve_mc_record *mc_record;

	list_for_each_entry_reverse(mc_record, &mc_list->records_list, list) {
		unsigned int num_entries = mc_record->num_entries;
		struct mlxsw_sp_nve *nve = mlxsw_sp->nve;

		if (mc_record->proto == proto &&
		    num_entries < nve->num_max_mc_entries[proto])
			return mc_record;
	}

	return mlxsw_sp_nve_mc_record_create(mlxsw_sp, mc_list, proto);
}

static void
mlxsw_sp_nve_mc_record_put(struct mlxsw_sp_nve_mc_record *mc_record)
{
	if (mc_record->num_entries != 0)
		return;

	mlxsw_sp_nve_mc_record_destroy(mc_record);
}

static struct mlxsw_sp_nve_mc_entry *
mlxsw_sp_nve_mc_free_entry_find(struct mlxsw_sp_nve_mc_record *mc_record)
{
	struct mlxsw_sp_nve *nve = mc_record->mlxsw_sp->nve;
	unsigned int num_max_entries;
	int i;

	num_max_entries = nve->num_max_mc_entries[mc_record->proto];
	for (i = 0; i < num_max_entries; i++) {
		if (mc_record->entries[i].valid)
			continue;
		return &mc_record->entries[i];
	}

	return NULL;
}

static int
mlxsw_sp_nve_mc_record_refresh(struct mlxsw_sp_nve_mc_record *mc_record)
{
	enum mlxsw_reg_tnumt_record_type type = mc_record->ops->type;
	struct mlxsw_sp_nve_mc_list *mc_list = mc_record->mc_list;
	struct mlxsw_sp *mlxsw_sp = mc_record->mlxsw_sp;
	char tnumt_pl[MLXSW_REG_TNUMT_LEN];
	unsigned int num_max_entries;
	unsigned int num_entries = 0;
	u32 next_kvdl_index = 0;
	bool next_valid = false;
	int i;

	if (!list_is_last(&mc_record->list, &mc_list->records_list)) {
		struct mlxsw_sp_nve_mc_record *next_record;

		next_record = list_next_entry(mc_record, list);
		next_kvdl_index = next_record->kvdl_index;
		next_valid = true;
	}

	mlxsw_reg_tnumt_pack(tnumt_pl, type, MLXSW_REG_TNUMT_TUNNEL_PORT_NVE,
			     mc_record->kvdl_index, next_valid,
			     next_kvdl_index, mc_record->num_entries);

	num_max_entries = mlxsw_sp->nve->num_max_mc_entries[mc_record->proto];
	for (i = 0; i < num_max_entries; i++) {
		struct mlxsw_sp_nve_mc_entry *mc_entry;

		mc_entry = &mc_record->entries[i];
		if (!mc_entry->valid)
			continue;
		mc_record->ops->entry_set(mc_record, mc_entry, tnumt_pl,
					  num_entries++);
	}

	WARN_ON(num_entries != mc_record->num_entries);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(tnumt), tnumt_pl);
}

static bool
mlxsw_sp_nve_mc_record_is_first(struct mlxsw_sp_nve_mc_record *mc_record)
{
	struct mlxsw_sp_nve_mc_list *mc_list = mc_record->mc_list;
	struct mlxsw_sp_nve_mc_record *first_record;

	first_record = list_first_entry(&mc_list->records_list,
					struct mlxsw_sp_nve_mc_record, list);

	return mc_record == first_record;
}

static struct mlxsw_sp_nve_mc_entry *
mlxsw_sp_nve_mc_entry_find(struct mlxsw_sp_nve_mc_record *mc_record,
			   union mlxsw_sp_l3addr *addr)
{
	struct mlxsw_sp_nve *nve = mc_record->mlxsw_sp->nve;
	unsigned int num_max_entries;
	int i;

	num_max_entries = nve->num_max_mc_entries[mc_record->proto];
	for (i = 0; i < num_max_entries; i++) {
		struct mlxsw_sp_nve_mc_entry *mc_entry;

		mc_entry = &mc_record->entries[i];
		if (!mc_entry->valid)
			continue;
		if (mc_record->ops->entry_compare(mc_record, mc_entry, addr))
			return mc_entry;
	}

	return NULL;
}

static int
mlxsw_sp_nve_mc_record_ip_add(struct mlxsw_sp_nve_mc_record *mc_record,
			      union mlxsw_sp_l3addr *addr)
{
	struct mlxsw_sp_nve_mc_entry *mc_entry = NULL;
	int err;

	mc_entry = mlxsw_sp_nve_mc_free_entry_find(mc_record);
	if (WARN_ON(!mc_entry))
		return -EINVAL;

	err = mc_record->ops->entry_add(mc_record, mc_entry, addr);
	if (err)
		return err;
	mc_record->num_entries++;
	mc_entry->valid = true;

	err = mlxsw_sp_nve_mc_record_refresh(mc_record);
	if (err)
		goto err_record_refresh;

	/* If this is a new record and not the first one, then we need to
	 * update the next pointer of the previous entry
	 */
	if (mc_record->num_entries != 1 ||
	    mlxsw_sp_nve_mc_record_is_first(mc_record))
		return 0;

	err = mlxsw_sp_nve_mc_record_refresh(list_prev_entry(mc_record, list));
	if (err)
		goto err_prev_record_refresh;

	return 0;

err_prev_record_refresh:
err_record_refresh:
	mc_entry->valid = false;
	mc_record->num_entries--;
	mc_record->ops->entry_del(mc_record, mc_entry);
	return err;
}

static void
mlxsw_sp_nve_mc_record_entry_del(struct mlxsw_sp_nve_mc_record *mc_record,
				 struct mlxsw_sp_nve_mc_entry *mc_entry)
{
	struct mlxsw_sp_nve_mc_list *mc_list = mc_record->mc_list;

	mc_entry->valid = false;
	mc_record->num_entries--;

	/* When the record continues to exist we only need to invalidate
	 * the requested entry
	 */
	if (mc_record->num_entries != 0) {
		mlxsw_sp_nve_mc_record_refresh(mc_record);
		mc_record->ops->entry_del(mc_record, mc_entry);
		return;
	}

	/* If the record needs to be deleted, but it is not the first,
	 * then we need to make sure that the previous record no longer
	 * points to it. Remove deleted record from the list to reflect
	 * that and then re-add it at the end, so that it could be
	 * properly removed by the record destruction code
	 */
	if (!mlxsw_sp_nve_mc_record_is_first(mc_record)) {
		struct mlxsw_sp_nve_mc_record *prev_record;

		prev_record = list_prev_entry(mc_record, list);
		list_del(&mc_record->list);
		mlxsw_sp_nve_mc_record_refresh(prev_record);
		list_add_tail(&mc_record->list, &mc_list->records_list);
		mc_record->ops->entry_del(mc_record, mc_entry);
		return;
	}

	/* If the first record needs to be deleted, but the list is not
	 * singular, then the second record needs to be written in the
	 * first record's address, as this address is stored as a property
	 * of the FID
	 */
	if (mlxsw_sp_nve_mc_record_is_first(mc_record) &&
	    !list_is_singular(&mc_list->records_list)) {
		struct mlxsw_sp_nve_mc_record *next_record;

		next_record = list_next_entry(mc_record, list);
		swap(mc_record->kvdl_index, next_record->kvdl_index);
		mlxsw_sp_nve_mc_record_refresh(next_record);
		mc_record->ops->entry_del(mc_record, mc_entry);
		return;
	}

	/* This is the last case where the last remaining record needs to
	 * be deleted. Simply delete the entry
	 */
	mc_record->ops->entry_del(mc_record, mc_entry);
}

static struct mlxsw_sp_nve_mc_record *
mlxsw_sp_nve_mc_record_find(struct mlxsw_sp_nve_mc_list *mc_list,
			    enum mlxsw_sp_l3proto proto,
			    union mlxsw_sp_l3addr *addr,
			    struct mlxsw_sp_nve_mc_entry **mc_entry)
{
	struct mlxsw_sp_nve_mc_record *mc_record;

	list_for_each_entry(mc_record, &mc_list->records_list, list) {
		if (mc_record->proto != proto)
			continue;

		*mc_entry = mlxsw_sp_nve_mc_entry_find(mc_record, addr);
		if (*mc_entry)
			return mc_record;
	}

	return NULL;
}

static int mlxsw_sp_nve_mc_list_ip_add(struct mlxsw_sp *mlxsw_sp,
				       struct mlxsw_sp_nve_mc_list *mc_list,
				       enum mlxsw_sp_l3proto proto,
				       union mlxsw_sp_l3addr *addr)
{
	struct mlxsw_sp_nve_mc_record *mc_record;
	int err;

	mc_record = mlxsw_sp_nve_mc_record_get(mlxsw_sp, mc_list, proto);
	if (IS_ERR(mc_record))
		return PTR_ERR(mc_record);

	err = mlxsw_sp_nve_mc_record_ip_add(mc_record, addr);
	if (err)
		goto err_ip_add;

	return 0;

err_ip_add:
	mlxsw_sp_nve_mc_record_put(mc_record);
	return err;
}

static void mlxsw_sp_nve_mc_list_ip_del(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_nve_mc_list *mc_list,
					enum mlxsw_sp_l3proto proto,
					union mlxsw_sp_l3addr *addr)
{
	struct mlxsw_sp_nve_mc_record *mc_record;
	struct mlxsw_sp_nve_mc_entry *mc_entry;

	mc_record = mlxsw_sp_nve_mc_record_find(mc_list, proto, addr,
						&mc_entry);
	if (!mc_record)
		return;

	mlxsw_sp_nve_mc_record_entry_del(mc_record, mc_entry);
	mlxsw_sp_nve_mc_record_put(mc_record);
}

static int
mlxsw_sp_nve_fid_flood_index_set(struct mlxsw_sp_fid *fid,
				 struct mlxsw_sp_nve_mc_list *mc_list)
{
	struct mlxsw_sp_nve_mc_record *mc_record;

	/* The address of the first record in the list is a property of
	 * the FID and we never change it. It only needs to be set when
	 * a new list is created
	 */
	if (mlxsw_sp_fid_nve_flood_index_is_set(fid))
		return 0;

	mc_record = list_first_entry(&mc_list->records_list,
				     struct mlxsw_sp_nve_mc_record, list);

	return mlxsw_sp_fid_nve_flood_index_set(fid, mc_record->kvdl_index);
}

static void
mlxsw_sp_nve_fid_flood_index_clear(struct mlxsw_sp_fid *fid,
				   struct mlxsw_sp_nve_mc_list *mc_list)
{
	struct mlxsw_sp_nve_mc_record *mc_record;

	/* The address of the first record needs to be invalidated only when
	 * the last record is about to be removed
	 */
	if (!list_is_singular(&mc_list->records_list))
		return;

	mc_record = list_first_entry(&mc_list->records_list,
				     struct mlxsw_sp_nve_mc_record, list);
	if (mc_record->num_entries != 1)
		return;

	return mlxsw_sp_fid_nve_flood_index_clear(fid);
}

int mlxsw_sp_nve_flood_ip_add(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_fid *fid,
			      enum mlxsw_sp_l3proto proto,
			      union mlxsw_sp_l3addr *addr)
{
	struct mlxsw_sp_nve_mc_list_key key = { 0 };
	struct mlxsw_sp_nve_mc_list *mc_list;
	int err;

	key.fid_index = mlxsw_sp_fid_index(fid);
	mc_list = mlxsw_sp_nve_mc_list_get(mlxsw_sp, &key);
	if (IS_ERR(mc_list))
		return PTR_ERR(mc_list);

	err = mlxsw_sp_nve_mc_list_ip_add(mlxsw_sp, mc_list, proto, addr);
	if (err)
		goto err_add_ip;

	err = mlxsw_sp_nve_fid_flood_index_set(fid, mc_list);
	if (err)
		goto err_fid_flood_index_set;

	return 0;

err_fid_flood_index_set:
	mlxsw_sp_nve_mc_list_ip_del(mlxsw_sp, mc_list, proto, addr);
err_add_ip:
	mlxsw_sp_nve_mc_list_put(mlxsw_sp, mc_list);
	return err;
}

void mlxsw_sp_nve_flood_ip_del(struct mlxsw_sp *mlxsw_sp,
			       struct mlxsw_sp_fid *fid,
			       enum mlxsw_sp_l3proto proto,
			       union mlxsw_sp_l3addr *addr)
{
	struct mlxsw_sp_nve_mc_list_key key = { 0 };
	struct mlxsw_sp_nve_mc_list *mc_list;

	key.fid_index = mlxsw_sp_fid_index(fid);
	mc_list = mlxsw_sp_nve_mc_list_find(mlxsw_sp, &key);
	if (!mc_list)
		return;

	mlxsw_sp_nve_fid_flood_index_clear(fid, mc_list);
	mlxsw_sp_nve_mc_list_ip_del(mlxsw_sp, mc_list, proto, addr);
	mlxsw_sp_nve_mc_list_put(mlxsw_sp, mc_list);
}

static void
mlxsw_sp_nve_mc_record_delete(struct mlxsw_sp_nve_mc_record *mc_record)
{
	struct mlxsw_sp_nve *nve = mc_record->mlxsw_sp->nve;
	unsigned int num_max_entries;
	int i;

	num_max_entries = nve->num_max_mc_entries[mc_record->proto];
	for (i = 0; i < num_max_entries; i++) {
		struct mlxsw_sp_nve_mc_entry *mc_entry = &mc_record->entries[i];

		if (!mc_entry->valid)
			continue;
		mlxsw_sp_nve_mc_record_entry_del(mc_record, mc_entry);
	}

	WARN_ON(mc_record->num_entries);
	mlxsw_sp_nve_mc_record_put(mc_record);
}

static void mlxsw_sp_nve_flood_ip_flush(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_fid *fid)
{
	struct mlxsw_sp_nve_mc_record *mc_record, *tmp;
	struct mlxsw_sp_nve_mc_list_key key = { 0 };
	struct mlxsw_sp_nve_mc_list *mc_list;

	if (!mlxsw_sp_fid_nve_flood_index_is_set(fid))
		return;

	mlxsw_sp_fid_nve_flood_index_clear(fid);

	key.fid_index = mlxsw_sp_fid_index(fid);
	mc_list = mlxsw_sp_nve_mc_list_find(mlxsw_sp, &key);
	if (WARN_ON(!mc_list))
		return;

	list_for_each_entry_safe(mc_record, tmp, &mc_list->records_list, list)
		mlxsw_sp_nve_mc_record_delete(mc_record);

	WARN_ON(!list_empty(&mc_list->records_list));
	mlxsw_sp_nve_mc_list_put(mlxsw_sp, mc_list);
}

u32 mlxsw_sp_nve_decap_tunnel_index_get(const struct mlxsw_sp *mlxsw_sp)
{
	WARN_ON(mlxsw_sp->nve->num_nve_tunnels == 0);

	return mlxsw_sp->nve->tunnel_index;
}

bool mlxsw_sp_nve_ipv4_route_is_decap(const struct mlxsw_sp *mlxsw_sp,
				      u32 tb_id, __be32 addr)
{
	struct mlxsw_sp_nve *nve = mlxsw_sp->nve;
	struct mlxsw_sp_nve_config *config = &nve->config;

	if (nve->num_nve_tunnels &&
	    config->ul_proto == MLXSW_SP_L3_PROTO_IPV4 &&
	    config->ul_sip.addr4 == addr && config->ul_tb_id == tb_id)
		return true;

	return false;
}

static int mlxsw_sp_nve_tunnel_init(struct mlxsw_sp *mlxsw_sp,
				    struct mlxsw_sp_nve_config *config)
{
	struct mlxsw_sp_nve *nve = mlxsw_sp->nve;
	const struct mlxsw_sp_nve_ops *ops;
	int err;

	if (nve->num_nve_tunnels++ != 0)
		return 0;

	err = mlxsw_sp_kvdl_alloc(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_ADJ, 1,
				  &nve->tunnel_index);
	if (err)
		goto err_kvdl_alloc;

	ops = nve->nve_ops_arr[config->type];
	err = ops->init(nve, config);
	if (err)
		goto err_ops_init;

	return 0;

err_ops_init:
	mlxsw_sp_kvdl_free(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_ADJ, 1,
			   nve->tunnel_index);
err_kvdl_alloc:
	nve->num_nve_tunnels--;
	return err;
}

static void mlxsw_sp_nve_tunnel_fini(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_nve *nve = mlxsw_sp->nve;
	const struct mlxsw_sp_nve_ops *ops;

	ops = nve->nve_ops_arr[nve->config.type];

	if (mlxsw_sp->nve->num_nve_tunnels == 1) {
		ops->fini(nve);
		mlxsw_sp_kvdl_free(mlxsw_sp, MLXSW_SP_KVDL_ENTRY_TYPE_ADJ, 1,
				   nve->tunnel_index);
	}
	nve->num_nve_tunnels--;
}

static void mlxsw_sp_nve_fdb_flush_by_fid(struct mlxsw_sp *mlxsw_sp,
					  u16 fid_index)
{
	char sfdf_pl[MLXSW_REG_SFDF_LEN];

	mlxsw_reg_sfdf_pack(sfdf_pl, MLXSW_REG_SFDF_FLUSH_PER_NVE_AND_FID);
	mlxsw_reg_sfdf_fid_set(sfdf_pl, fid_index);
	mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(sfdf), sfdf_pl);
}

static void mlxsw_sp_nve_fdb_clear_offload(struct mlxsw_sp *mlxsw_sp,
					   const struct mlxsw_sp_fid *fid,
					   const struct net_device *nve_dev,
					   __be32 vni)
{
	const struct mlxsw_sp_nve_ops *ops;
	enum mlxsw_sp_nve_type type;

	if (WARN_ON(mlxsw_sp_fid_nve_type(fid, &type)))
		return;

	ops = mlxsw_sp->nve->nve_ops_arr[type];
	ops->fdb_clear_offload(nve_dev, vni);
}

int mlxsw_sp_nve_fid_enable(struct mlxsw_sp *mlxsw_sp, struct mlxsw_sp_fid *fid,
			    struct mlxsw_sp_nve_params *params,
			    struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_nve *nve = mlxsw_sp->nve;
	const struct mlxsw_sp_nve_ops *ops;
	struct mlxsw_sp_nve_config config;
	int err;

	ops = nve->nve_ops_arr[params->type];

	if (!ops->can_offload(nve, params->dev, extack))
		return -EOPNOTSUPP;

	memset(&config, 0, sizeof(config));
	ops->nve_config(nve, params->dev, &config);
	if (nve->num_nve_tunnels &&
	    memcmp(&config, &nve->config, sizeof(config))) {
		NL_SET_ERR_MSG_MOD(extack, "Conflicting NVE tunnels configuration");
		return -EOPNOTSUPP;
	}

	err = mlxsw_sp_nve_tunnel_init(mlxsw_sp, &config);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to initialize NVE tunnel");
		return err;
	}

	err = mlxsw_sp_fid_vni_set(fid, params->type, params->vni,
				   params->dev->ifindex);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to set VNI on FID");
		goto err_fid_vni_set;
	}

	nve->config = config;

	err = ops->fdb_replay(params->dev, params->vni);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to offload the FDB");
		goto err_fdb_replay;
	}

	return 0;

err_fdb_replay:
	mlxsw_sp_fid_vni_clear(fid);
err_fid_vni_set:
	mlxsw_sp_nve_tunnel_fini(mlxsw_sp);
	return err;
}

void mlxsw_sp_nve_fid_disable(struct mlxsw_sp *mlxsw_sp,
			      struct mlxsw_sp_fid *fid)
{
	u16 fid_index = mlxsw_sp_fid_index(fid);
	struct net_device *nve_dev;
	int nve_ifindex;
	__be32 vni;

	mlxsw_sp_nve_flood_ip_flush(mlxsw_sp, fid);
	mlxsw_sp_nve_fdb_flush_by_fid(mlxsw_sp, fid_index);

	if (WARN_ON(mlxsw_sp_fid_nve_ifindex(fid, &nve_ifindex) ||
		    mlxsw_sp_fid_vni(fid, &vni)))
		goto out;

	nve_dev = dev_get_by_index(&init_net, nve_ifindex);
	if (!nve_dev)
		goto out;

	mlxsw_sp_nve_fdb_clear_offload(mlxsw_sp, fid, nve_dev, vni);
	mlxsw_sp_fid_fdb_clear_offload(fid, nve_dev);

	dev_put(nve_dev);

out:
	mlxsw_sp_fid_vni_clear(fid);
	mlxsw_sp_nve_tunnel_fini(mlxsw_sp);
}

int mlxsw_sp_port_nve_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char tnqdr_pl[MLXSW_REG_TNQDR_LEN];

	mlxsw_reg_tnqdr_pack(tnqdr_pl, mlxsw_sp_port->local_port);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(tnqdr), tnqdr_pl);
}

void mlxsw_sp_port_nve_fini(struct mlxsw_sp_port *mlxsw_sp_port)
{
}

static int mlxsw_sp_nve_qos_init(struct mlxsw_sp *mlxsw_sp)
{
	char tnqcr_pl[MLXSW_REG_TNQCR_LEN];

	mlxsw_reg_tnqcr_pack(tnqcr_pl);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(tnqcr), tnqcr_pl);
}

static int mlxsw_sp_nve_ecn_encap_init(struct mlxsw_sp *mlxsw_sp)
{
	int i;

	/* Iterate over inner ECN values */
	for (i = INET_ECN_NOT_ECT; i <= INET_ECN_CE; i++) {
		u8 outer_ecn = INET_ECN_encapsulate(0, i);
		char tneem_pl[MLXSW_REG_TNEEM_LEN];
		int err;

		mlxsw_reg_tneem_pack(tneem_pl, i, outer_ecn);
		err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(tneem),
				      tneem_pl);
		if (err)
			return err;
	}

	return 0;
}

static int __mlxsw_sp_nve_ecn_decap_init(struct mlxsw_sp *mlxsw_sp,
					 u8 inner_ecn, u8 outer_ecn)
{
	char tndem_pl[MLXSW_REG_TNDEM_LEN];
	bool trap_en, set_ce = false;
	u8 new_inner_ecn;

	trap_en = !!__INET_ECN_decapsulate(outer_ecn, inner_ecn, &set_ce);
	new_inner_ecn = set_ce ? INET_ECN_CE : inner_ecn;

	mlxsw_reg_tndem_pack(tndem_pl, outer_ecn, inner_ecn, new_inner_ecn,
			     trap_en, trap_en ? MLXSW_TRAP_ID_DECAP_ECN0 : 0);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(tndem), tndem_pl);
}

static int mlxsw_sp_nve_ecn_decap_init(struct mlxsw_sp *mlxsw_sp)
{
	int i;

	/* Iterate over inner ECN values */
	for (i = INET_ECN_NOT_ECT; i <= INET_ECN_CE; i++) {
		int j;

		/* Iterate over outer ECN values */
		for (j = INET_ECN_NOT_ECT; j <= INET_ECN_CE; j++) {
			int err;

			err = __mlxsw_sp_nve_ecn_decap_init(mlxsw_sp, i, j);
			if (err)
				return err;
		}
	}

	return 0;
}

static int mlxsw_sp_nve_ecn_init(struct mlxsw_sp *mlxsw_sp)
{
	int err;

	err = mlxsw_sp_nve_ecn_encap_init(mlxsw_sp);
	if (err)
		return err;

	return mlxsw_sp_nve_ecn_decap_init(mlxsw_sp);
}

static int mlxsw_sp_nve_resources_query(struct mlxsw_sp *mlxsw_sp)
{
	unsigned int max;

	if (!MLXSW_CORE_RES_VALID(mlxsw_sp->core, MAX_NVE_MC_ENTRIES_IPV4) ||
	    !MLXSW_CORE_RES_VALID(mlxsw_sp->core, MAX_NVE_MC_ENTRIES_IPV6))
		return -EIO;
	max = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_NVE_MC_ENTRIES_IPV4);
	mlxsw_sp->nve->num_max_mc_entries[MLXSW_SP_L3_PROTO_IPV4] = max;
	max = MLXSW_CORE_RES_GET(mlxsw_sp->core, MAX_NVE_MC_ENTRIES_IPV6);
	mlxsw_sp->nve->num_max_mc_entries[MLXSW_SP_L3_PROTO_IPV6] = max;

	return 0;
}

int mlxsw_sp_nve_init(struct mlxsw_sp *mlxsw_sp)
{
	struct mlxsw_sp_nve *nve;
	int err;

	nve = kzalloc(sizeof(*mlxsw_sp->nve), GFP_KERNEL);
	if (!nve)
		return -ENOMEM;
	mlxsw_sp->nve = nve;
	nve->mlxsw_sp = mlxsw_sp;
	nve->nve_ops_arr = mlxsw_sp->nve_ops_arr;

	err = rhashtable_init(&nve->mc_list_ht,
			      &mlxsw_sp_nve_mc_list_ht_params);
	if (err)
		goto err_rhashtable_init;

	err = mlxsw_sp_nve_qos_init(mlxsw_sp);
	if (err)
		goto err_nve_qos_init;

	err = mlxsw_sp_nve_ecn_init(mlxsw_sp);
	if (err)
		goto err_nve_ecn_init;

	err = mlxsw_sp_nve_resources_query(mlxsw_sp);
	if (err)
		goto err_nve_resources_query;

	return 0;

err_nve_resources_query:
err_nve_ecn_init:
err_nve_qos_init:
	rhashtable_destroy(&nve->mc_list_ht);
err_rhashtable_init:
	mlxsw_sp->nve = NULL;
	kfree(nve);
	return err;
}

void mlxsw_sp_nve_fini(struct mlxsw_sp *mlxsw_sp)
{
	WARN_ON(mlxsw_sp->nve->num_nve_tunnels);
	rhashtable_destroy(&mlxsw_sp->nve->mc_list_ht);
	kfree(mlxsw_sp->nve);
	mlxsw_sp->nve = NULL;
}
