// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <net/macsec.h>
#include <linux/mlx5/qp.h>
#include <linux/if_vlan.h>
#include <linux/mlx5/fs_helpers.h>
#include <linux/mlx5/macsec.h>
#include "fs_core.h"
#include "lib/macsec_fs.h"
#include "mlx5_core.h"

/* MACsec TX flow steering */
#define CRYPTO_NUM_MAXSEC_FTE BIT(15)
#define CRYPTO_TABLE_DEFAULT_RULE_GROUP_SIZE 1

#define TX_CRYPTO_TABLE_LEVEL 0
#define TX_CRYPTO_TABLE_NUM_GROUPS 3
#define TX_CRYPTO_TABLE_MKE_GROUP_SIZE 1
#define TX_CRYPTO_TABLE_SA_GROUP_SIZE \
	(CRYPTO_NUM_MAXSEC_FTE - (TX_CRYPTO_TABLE_MKE_GROUP_SIZE + \
				  CRYPTO_TABLE_DEFAULT_RULE_GROUP_SIZE))
#define TX_CHECK_TABLE_LEVEL 1
#define TX_CHECK_TABLE_NUM_FTE 2
#define RX_CRYPTO_TABLE_LEVEL 0
#define RX_CHECK_TABLE_LEVEL 1
#define RX_ROCE_TABLE_LEVEL 2
#define RX_CHECK_TABLE_NUM_FTE 3
#define RX_ROCE_TABLE_NUM_FTE 2
#define RX_CRYPTO_TABLE_NUM_GROUPS 3
#define RX_CRYPTO_TABLE_SA_RULE_WITH_SCI_GROUP_SIZE \
	((CRYPTO_NUM_MAXSEC_FTE - CRYPTO_TABLE_DEFAULT_RULE_GROUP_SIZE) / 2)
#define RX_CRYPTO_TABLE_SA_RULE_WITHOUT_SCI_GROUP_SIZE \
	(CRYPTO_NUM_MAXSEC_FTE - RX_CRYPTO_TABLE_SA_RULE_WITH_SCI_GROUP_SIZE)
#define RX_NUM_OF_RULES_PER_SA 2

#define RDMA_RX_ROCE_IP_TABLE_LEVEL 0
#define RDMA_RX_ROCE_MACSEC_OP_TABLE_LEVEL 1

#define MLX5_MACSEC_TAG_LEN 8 /* SecTAG length with ethertype and without the optional SCI */
#define MLX5_MACSEC_SECTAG_TCI_AN_FIELD_BITMASK 0x23
#define MLX5_MACSEC_SECTAG_TCI_AN_FIELD_OFFSET 0x8
#define MLX5_MACSEC_SECTAG_TCI_SC_FIELD_OFFSET 0x5
#define MLX5_MACSEC_SECTAG_TCI_SC_FIELD_BIT (0x1 << MLX5_MACSEC_SECTAG_TCI_SC_FIELD_OFFSET)
#define MLX5_SECTAG_HEADER_SIZE_WITHOUT_SCI 0x8
#define MLX5_SECTAG_HEADER_SIZE_WITH_SCI (MLX5_SECTAG_HEADER_SIZE_WITHOUT_SCI + MACSEC_SCI_LEN)

/* MACsec RX flow steering */
#define MLX5_ETH_WQE_FT_META_MACSEC_MASK 0x3E

/* MACsec fs_id handling for steering */
#define macsec_fs_set_tx_fs_id(fs_id) (MLX5_ETH_WQE_FT_META_MACSEC | (fs_id) << 2)
#define macsec_fs_set_rx_fs_id(fs_id) ((fs_id) | BIT(30))

struct mlx5_sectag_header {
	__be16 ethertype;
	u8 tci_an;
	u8 sl;
	u32 pn;
	u8 sci[MACSEC_SCI_LEN]; /* optional */
}  __packed;

struct mlx5_roce_macsec_tx_rule {
	u32 fs_id;
	u16 gid_idx;
	struct list_head entry;
	struct mlx5_flow_handle *rule;
	struct mlx5_modify_hdr *meta_modhdr;
};

struct mlx5_macsec_tx_rule {
	struct mlx5_flow_handle *rule;
	struct mlx5_pkt_reformat *pkt_reformat;
	u32 fs_id;
};

struct mlx5_macsec_flow_table {
	int num_groups;
	struct mlx5_flow_table *t;
	struct mlx5_flow_group **g;
};

struct mlx5_macsec_tables {
	struct mlx5_macsec_flow_table ft_crypto;
	struct mlx5_flow_handle *crypto_miss_rule;

	struct mlx5_flow_table *ft_check;
	struct mlx5_flow_group  *ft_check_group;
	struct mlx5_fc *check_miss_rule_counter;
	struct mlx5_flow_handle *check_miss_rule;
	struct mlx5_fc *check_rule_counter;

	u32 refcnt;
};

struct mlx5_fs_id {
	u32 id;
	refcount_t refcnt;
	sci_t sci;
	struct rhash_head hash;
};

struct mlx5_macsec_device {
	struct list_head macsec_devices_list_entry;
	void *macdev;
	struct xarray tx_id_xa;
	struct xarray rx_id_xa;
};

struct mlx5_macsec_tx {
	struct mlx5_flow_handle *crypto_mke_rule;
	struct mlx5_flow_handle *check_rule;

	struct ida tx_halloc;

	struct mlx5_macsec_tables tables;

	struct mlx5_flow_table *ft_rdma_tx;
};

struct mlx5_roce_macsec_rx_rule {
	u32 fs_id;
	u16 gid_idx;
	struct mlx5_flow_handle *op;
	struct mlx5_flow_handle *ip;
	struct list_head entry;
};

struct mlx5_macsec_rx_rule {
	struct mlx5_flow_handle *rule[RX_NUM_OF_RULES_PER_SA];
	struct mlx5_modify_hdr *meta_modhdr;
};

struct mlx5_macsec_miss {
	struct mlx5_flow_group *g;
	struct mlx5_flow_handle *rule;
};

struct mlx5_macsec_rx_roce {
	/* Flow table/rules in NIC domain, to check if it's a RoCE packet */
	struct mlx5_flow_group *g;
	struct mlx5_flow_table *ft;
	struct mlx5_flow_handle *rule;
	struct mlx5_modify_hdr *copy_modify_hdr;
	struct mlx5_macsec_miss nic_miss;

	/* Flow table/rule in RDMA domain, to check dgid */
	struct mlx5_flow_table *ft_ip_check;
	struct mlx5_flow_table *ft_macsec_op_check;
	struct mlx5_macsec_miss miss;
};

struct mlx5_macsec_rx {
	struct mlx5_flow_handle *check_rule[2];
	struct mlx5_pkt_reformat *check_rule_pkt_reformat[2];

	struct mlx5_macsec_tables tables;
	struct mlx5_macsec_rx_roce roce;
};

union mlx5_macsec_rule {
	struct mlx5_macsec_tx_rule tx_rule;
	struct mlx5_macsec_rx_rule rx_rule;
};

static const struct rhashtable_params rhash_sci = {
	.key_len = sizeof_field(struct mlx5_fs_id, sci),
	.key_offset = offsetof(struct mlx5_fs_id, sci),
	.head_offset = offsetof(struct mlx5_fs_id, hash),
	.automatic_shrinking = true,
	.min_size = 1,
};

static const struct rhashtable_params rhash_fs_id = {
	.key_len = sizeof_field(struct mlx5_fs_id, id),
	.key_offset = offsetof(struct mlx5_fs_id, id),
	.head_offset = offsetof(struct mlx5_fs_id, hash),
	.automatic_shrinking = true,
	.min_size = 1,
};

struct mlx5_macsec_fs {
	struct mlx5_core_dev *mdev;
	struct mlx5_macsec_tx *tx_fs;
	struct mlx5_macsec_rx *rx_fs;

	/* Stats manage */
	struct mlx5_macsec_stats stats;

	/* Tx sci -> fs id mapping handling */
	struct rhashtable sci_hash;      /* sci -> mlx5_fs_id */

	/* RX fs_id -> mlx5_fs_id mapping handling */
	struct rhashtable fs_id_hash;      /* fs_id -> mlx5_fs_id */

	/* TX & RX fs_id lists per macsec device */
	struct list_head macsec_devices_list;
};

static void macsec_fs_destroy_groups(struct mlx5_macsec_flow_table *ft)
{
	int i;

	for (i = ft->num_groups - 1; i >= 0; i--) {
		if (!IS_ERR_OR_NULL(ft->g[i]))
			mlx5_destroy_flow_group(ft->g[i]);
		ft->g[i] = NULL;
	}
	ft->num_groups = 0;
}

static void macsec_fs_destroy_flow_table(struct mlx5_macsec_flow_table *ft)
{
	macsec_fs_destroy_groups(ft);
	kfree(ft->g);
	mlx5_destroy_flow_table(ft->t);
	ft->t = NULL;
}

static void macsec_fs_tx_destroy(struct mlx5_macsec_fs *macsec_fs)
{
	struct mlx5_macsec_tx *tx_fs = macsec_fs->tx_fs;
	struct mlx5_macsec_tables *tx_tables;

	if (mlx5_is_macsec_roce_supported(macsec_fs->mdev))
		mlx5_destroy_flow_table(tx_fs->ft_rdma_tx);

	tx_tables = &tx_fs->tables;

	/* Tx check table */
	if (tx_fs->check_rule) {
		mlx5_del_flow_rules(tx_fs->check_rule);
		tx_fs->check_rule = NULL;
	}

	if (tx_tables->check_miss_rule) {
		mlx5_del_flow_rules(tx_tables->check_miss_rule);
		tx_tables->check_miss_rule = NULL;
	}

	if (tx_tables->ft_check_group) {
		mlx5_destroy_flow_group(tx_tables->ft_check_group);
		tx_tables->ft_check_group = NULL;
	}

	if (tx_tables->ft_check) {
		mlx5_destroy_flow_table(tx_tables->ft_check);
		tx_tables->ft_check = NULL;
	}

	/* Tx crypto table */
	if (tx_fs->crypto_mke_rule) {
		mlx5_del_flow_rules(tx_fs->crypto_mke_rule);
		tx_fs->crypto_mke_rule = NULL;
	}

	if (tx_tables->crypto_miss_rule) {
		mlx5_del_flow_rules(tx_tables->crypto_miss_rule);
		tx_tables->crypto_miss_rule = NULL;
	}

	macsec_fs_destroy_flow_table(&tx_tables->ft_crypto);
}

static int macsec_fs_tx_create_crypto_table_groups(struct mlx5_macsec_flow_table *ft)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	int mclen = MLX5_ST_SZ_BYTES(fte_match_param);
	int ix = 0;
	u32 *in;
	int err;
	u8 *mc;

	ft->g = kcalloc(TX_CRYPTO_TABLE_NUM_GROUPS, sizeof(*ft->g), GFP_KERNEL);
	if (!ft->g)
		return -ENOMEM;
	in = kvzalloc(inlen, GFP_KERNEL);

	if (!in) {
		kfree(ft->g);
		ft->g = NULL;
		return -ENOMEM;
	}

	mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);

	/* Flow Group for MKE match */
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.ethertype);

	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += TX_CRYPTO_TABLE_MKE_GROUP_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err;
	ft->num_groups++;

	/* Flow Group for SA rules */
	memset(in, 0, inlen);
	memset(mc, 0, mclen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_MISC_PARAMETERS_2);
	MLX5_SET(fte_match_param, mc, misc_parameters_2.metadata_reg_a,
		 MLX5_ETH_WQE_FT_META_MACSEC_MASK);

	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += TX_CRYPTO_TABLE_SA_GROUP_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err;
	ft->num_groups++;

	/* Flow Group for l2 traps */
	memset(in, 0, inlen);
	memset(mc, 0, mclen);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += CRYPTO_TABLE_DEFAULT_RULE_GROUP_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err;
	ft->num_groups++;

	kvfree(in);
	return 0;

err:
	err = PTR_ERR(ft->g[ft->num_groups]);
	ft->g[ft->num_groups] = NULL;
	kvfree(in);

	return err;
}

static struct mlx5_flow_table
	*macsec_fs_auto_group_table_create(struct mlx5_flow_namespace *ns, int flags,
					   int level, int max_fte)
{
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_table *fdb = NULL;

	/* reserve entry for the match all miss group and rule */
	ft_attr.autogroup.num_reserved_entries = 1;
	ft_attr.autogroup.max_num_groups = 1;
	ft_attr.prio = 0;
	ft_attr.flags = flags;
	ft_attr.level = level;
	ft_attr.max_fte = max_fte;

	fdb = mlx5_create_auto_grouped_flow_table(ns, &ft_attr);

	return fdb;
}

enum {
	RDMA_TX_MACSEC_LEVEL = 0,
};

static int macsec_fs_tx_roce_create(struct mlx5_macsec_fs *macsec_fs)
{
	struct mlx5_macsec_tx *tx_fs = macsec_fs->tx_fs;
	struct mlx5_core_dev *mdev = macsec_fs->mdev;
	struct mlx5_flow_namespace *ns;
	struct mlx5_flow_table *ft;
	int err;

	if (!mlx5_is_macsec_roce_supported(mdev)) {
		mlx5_core_dbg(mdev, "Failed to init RoCE MACsec, capabilities not supported\n");
		return 0;
	}

	ns = mlx5_get_flow_namespace(mdev, MLX5_FLOW_NAMESPACE_RDMA_TX_MACSEC);
	if (!ns)
		return -ENOMEM;

	/* Tx RoCE crypto table  */
	ft = macsec_fs_auto_group_table_create(ns, 0, RDMA_TX_MACSEC_LEVEL, CRYPTO_NUM_MAXSEC_FTE);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		mlx5_core_err(mdev, "Failed to create MACsec RoCE Tx crypto table err(%d)\n", err);
		return err;
	}
	tx_fs->ft_rdma_tx = ft;

	return 0;
}

static int macsec_fs_tx_create(struct mlx5_macsec_fs *macsec_fs)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_macsec_tx *tx_fs = macsec_fs->tx_fs;
	struct mlx5_core_dev *mdev = macsec_fs->mdev;
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_destination dest = {};
	struct mlx5_macsec_tables *tx_tables;
	struct mlx5_flow_act flow_act = {};
	struct mlx5_macsec_flow_table *ft_crypto;
	struct mlx5_flow_table *flow_table;
	struct mlx5_flow_group *flow_group;
	struct mlx5_flow_namespace *ns;
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	u32 *flow_group_in;
	int err;

	ns = mlx5_get_flow_namespace(mdev, MLX5_FLOW_NAMESPACE_EGRESS_MACSEC);
	if (!ns)
		return -ENOMEM;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	if (!flow_group_in) {
		err = -ENOMEM;
		goto out_spec;
	}

	tx_tables = &tx_fs->tables;
	ft_crypto = &tx_tables->ft_crypto;

	/* Tx crypto table  */
	ft_attr.flags = MLX5_FLOW_TABLE_TUNNEL_EN_REFORMAT;
	ft_attr.level = TX_CRYPTO_TABLE_LEVEL;
	ft_attr.max_fte = CRYPTO_NUM_MAXSEC_FTE;

	flow_table = mlx5_create_flow_table(ns, &ft_attr);
	if (IS_ERR(flow_table)) {
		err = PTR_ERR(flow_table);
		mlx5_core_err(mdev, "Failed to create MACsec Tx crypto table err(%d)\n", err);
		goto out_flow_group;
	}
	ft_crypto->t = flow_table;

	/* Tx crypto table groups */
	err = macsec_fs_tx_create_crypto_table_groups(ft_crypto);
	if (err) {
		mlx5_core_err(mdev,
			      "Failed to create default flow group for MACsec Tx crypto table err(%d)\n",
			      err);
		goto err;
	}

	/* Tx crypto table MKE rule - MKE packets shouldn't be offloaded */
	spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;

	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ethertype);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ethertype, ETH_P_PAE);
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_ALLOW;

	rule = mlx5_add_flow_rules(ft_crypto->t, spec, &flow_act, NULL, 0);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "Failed to add MACsec TX MKE rule, err=%d\n", err);
		goto err;
	}
	tx_fs->crypto_mke_rule = rule;

	/* Tx crypto table Default miss rule */
	memset(&flow_act, 0, sizeof(flow_act));
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_ALLOW;
	rule = mlx5_add_flow_rules(ft_crypto->t, NULL, &flow_act, NULL, 0);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "Failed to add MACsec Tx table default miss rule %d\n", err);
		goto err;
	}
	tx_tables->crypto_miss_rule = rule;

	/* Tx check table */
	flow_table = macsec_fs_auto_group_table_create(ns, 0, TX_CHECK_TABLE_LEVEL,
						       TX_CHECK_TABLE_NUM_FTE);
	if (IS_ERR(flow_table)) {
		err = PTR_ERR(flow_table);
		mlx5_core_err(mdev, "Fail to create MACsec TX check table, err(%d)\n", err);
		goto err;
	}
	tx_tables->ft_check = flow_table;

	/* Tx check table Default miss group/rule */
	memset(flow_group_in, 0, inlen);
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, flow_table->max_fte - 1);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, flow_table->max_fte - 1);
	flow_group = mlx5_create_flow_group(tx_tables->ft_check, flow_group_in);
	if (IS_ERR(flow_group)) {
		err = PTR_ERR(flow_group);
		mlx5_core_err(mdev,
			      "Failed to create default flow group for MACsec Tx crypto table err(%d)\n",
			      err);
		goto err;
	}
	tx_tables->ft_check_group = flow_group;

	/* Tx check table default drop rule */
	memset(&dest, 0, sizeof(struct mlx5_flow_destination));
	memset(&flow_act, 0, sizeof(flow_act));
	dest.type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
	dest.counter = tx_tables->check_miss_rule_counter;
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_DROP | MLX5_FLOW_CONTEXT_ACTION_COUNT;
	rule = mlx5_add_flow_rules(tx_tables->ft_check,  NULL, &flow_act, &dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "Failed to added MACsec tx check drop rule, err(%d)\n", err);
		goto err;
	}
	tx_tables->check_miss_rule = rule;

	/* Tx check table rule */
	memset(spec, 0, sizeof(struct mlx5_flow_spec));
	memset(&dest, 0, sizeof(struct mlx5_flow_destination));
	memset(&flow_act, 0, sizeof(flow_act));

	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, misc_parameters_2.metadata_reg_c_4);
	MLX5_SET(fte_match_param, spec->match_value, misc_parameters_2.metadata_reg_c_4, 0);
	spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS_2;

	flow_act.flags = FLOW_ACT_NO_APPEND;
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_ALLOW | MLX5_FLOW_CONTEXT_ACTION_COUNT;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
	dest.counter = tx_tables->check_rule_counter;
	rule = mlx5_add_flow_rules(tx_tables->ft_check, spec, &flow_act, &dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "Failed to add MACsec check rule, err=%d\n", err);
		goto err;
	}
	tx_fs->check_rule = rule;

	err = macsec_fs_tx_roce_create(macsec_fs);
	if (err)
		goto err;

	kvfree(flow_group_in);
	kvfree(spec);
	return 0;

err:
	macsec_fs_tx_destroy(macsec_fs);
out_flow_group:
	kvfree(flow_group_in);
out_spec:
	kvfree(spec);
	return err;
}

static int macsec_fs_tx_ft_get(struct mlx5_macsec_fs *macsec_fs)
{
	struct mlx5_macsec_tx *tx_fs = macsec_fs->tx_fs;
	struct mlx5_macsec_tables *tx_tables;
	int err = 0;

	tx_tables = &tx_fs->tables;
	if (tx_tables->refcnt)
		goto out;

	err = macsec_fs_tx_create(macsec_fs);
	if (err)
		return err;

out:
	tx_tables->refcnt++;
	return err;
}

static void macsec_fs_tx_ft_put(struct mlx5_macsec_fs *macsec_fs)
{
	struct mlx5_macsec_tables *tx_tables = &macsec_fs->tx_fs->tables;

	if (--tx_tables->refcnt)
		return;

	macsec_fs_tx_destroy(macsec_fs);
}

static int macsec_fs_tx_setup_fte(struct mlx5_macsec_fs *macsec_fs,
				  struct mlx5_flow_spec *spec,
				  struct mlx5_flow_act *flow_act,
				  u32 macsec_obj_id,
				  u32 *fs_id)
{
	struct mlx5_macsec_tx *tx_fs = macsec_fs->tx_fs;
	int err = 0;
	u32 id;

	err = ida_alloc_range(&tx_fs->tx_halloc, 1,
			      MLX5_MACSEC_NUM_OF_SUPPORTED_INTERFACES,
			      GFP_KERNEL);
	if (err < 0)
		return err;

	id = err;
	spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS_2;

	/* Metadata match */
	MLX5_SET(fte_match_param, spec->match_criteria, misc_parameters_2.metadata_reg_a,
		 MLX5_ETH_WQE_FT_META_MACSEC_MASK);
	MLX5_SET(fte_match_param, spec->match_value, misc_parameters_2.metadata_reg_a,
		 macsec_fs_set_tx_fs_id(id));

	*fs_id = id;
	flow_act->crypto.type = MLX5_FLOW_CONTEXT_ENCRYPT_DECRYPT_TYPE_MACSEC;
	flow_act->crypto.obj_id = macsec_obj_id;

	mlx5_core_dbg(macsec_fs->mdev, "Tx fte: macsec obj_id %u, fs_id %u\n", macsec_obj_id, id);
	return 0;
}

static void macsec_fs_tx_create_sectag_header(const struct macsec_context *ctx,
					      char *reformatbf,
					      size_t *reformat_size)
{
	const struct macsec_secy *secy = ctx->secy;
	bool sci_present = macsec_send_sci(secy);
	struct mlx5_sectag_header sectag = {};
	const struct macsec_tx_sc *tx_sc;

	tx_sc = &secy->tx_sc;
	sectag.ethertype = htons(ETH_P_MACSEC);

	if (sci_present) {
		sectag.tci_an |= MACSEC_TCI_SC;
		memcpy(&sectag.sci, &secy->sci,
		       sizeof(sectag.sci));
	} else {
		if (tx_sc->end_station)
			sectag.tci_an |= MACSEC_TCI_ES;
		if (tx_sc->scb)
			sectag.tci_an |= MACSEC_TCI_SCB;
	}

	/* With GCM, C/E clear for !encrypt, both set for encrypt */
	if (tx_sc->encrypt)
		sectag.tci_an |= MACSEC_TCI_CONFID;
	else if (secy->icv_len != MACSEC_DEFAULT_ICV_LEN)
		sectag.tci_an |= MACSEC_TCI_C;

	sectag.tci_an |= tx_sc->encoding_sa;

	*reformat_size = MLX5_MACSEC_TAG_LEN + (sci_present ? MACSEC_SCI_LEN : 0);

	memcpy(reformatbf, &sectag, *reformat_size);
}

static bool macsec_fs_is_macsec_device_empty(struct mlx5_macsec_device *macsec_device)
{
	if (xa_empty(&macsec_device->tx_id_xa) &&
	    xa_empty(&macsec_device->rx_id_xa))
		return true;

	return false;
}

static void macsec_fs_id_del(struct list_head *macsec_devices_list, u32 fs_id,
			     void *macdev, struct rhashtable *hash_table, bool is_tx)
{
	const struct rhashtable_params *rhash = (is_tx) ? &rhash_sci : &rhash_fs_id;
	struct mlx5_macsec_device *iter, *macsec_device = NULL;
	struct mlx5_fs_id *fs_id_found;
	struct xarray *fs_id_xa;

	list_for_each_entry(iter, macsec_devices_list, macsec_devices_list_entry) {
		if (iter->macdev == macdev) {
			macsec_device = iter;
			break;
		}
	}
	WARN_ON(!macsec_device);

	fs_id_xa = (is_tx) ? &macsec_device->tx_id_xa :
			     &macsec_device->rx_id_xa;
	xa_lock(fs_id_xa);
	fs_id_found = xa_load(fs_id_xa, fs_id);
	WARN_ON(!fs_id_found);

	if (!refcount_dec_and_test(&fs_id_found->refcnt)) {
		xa_unlock(fs_id_xa);
		return;
	}

	if (fs_id_found->id) {
		/* Make sure ongoing datapath readers sees a valid SA */
		rhashtable_remove_fast(hash_table, &fs_id_found->hash, *rhash);
		fs_id_found->id = 0;
	}
	xa_unlock(fs_id_xa);

	xa_erase(fs_id_xa, fs_id);

	kfree(fs_id_found);

	if (macsec_fs_is_macsec_device_empty(macsec_device)) {
		list_del(&macsec_device->macsec_devices_list_entry);
		kfree(macsec_device);
	}
}

static int macsec_fs_id_add(struct list_head *macsec_devices_list, u32 fs_id,
			    void *macdev, struct rhashtable *hash_table, sci_t sci,
			    bool is_tx)
{
	const struct rhashtable_params *rhash = (is_tx) ? &rhash_sci : &rhash_fs_id;
	struct mlx5_macsec_device *iter, *macsec_device = NULL;
	struct mlx5_fs_id *fs_id_iter;
	struct xarray *fs_id_xa;
	int err;

	if (!is_tx) {
		rcu_read_lock();
		fs_id_iter = rhashtable_lookup(hash_table, &fs_id, rhash_fs_id);
		if (fs_id_iter) {
			refcount_inc(&fs_id_iter->refcnt);
			rcu_read_unlock();
			return 0;
		}
		rcu_read_unlock();
	}

	fs_id_iter = kzalloc(sizeof(*fs_id_iter), GFP_KERNEL);
	if (!fs_id_iter)
		return -ENOMEM;

	list_for_each_entry(iter, macsec_devices_list, macsec_devices_list_entry) {
		if (iter->macdev == macdev) {
			macsec_device = iter;
			break;
		}
	}

	if (!macsec_device) { /* first time adding a SA to that device */
		macsec_device = kzalloc(sizeof(*macsec_device), GFP_KERNEL);
		if (!macsec_device) {
			err = -ENOMEM;
			goto err_alloc_dev;
		}
		macsec_device->macdev = macdev;
		xa_init(&macsec_device->tx_id_xa);
		xa_init(&macsec_device->rx_id_xa);
		list_add(&macsec_device->macsec_devices_list_entry, macsec_devices_list);
	}

	fs_id_xa = (is_tx) ? &macsec_device->tx_id_xa :
			     &macsec_device->rx_id_xa;
	fs_id_iter->id = fs_id;
	refcount_set(&fs_id_iter->refcnt, 1);
	fs_id_iter->sci = sci;
	err = xa_err(xa_store(fs_id_xa, fs_id, fs_id_iter, GFP_KERNEL));
	if (err)
		goto err_store_id;

	err = rhashtable_insert_fast(hash_table, &fs_id_iter->hash, *rhash);
	if (err)
		goto err_hash_insert;

	return 0;

err_hash_insert:
	xa_erase(fs_id_xa, fs_id);
err_store_id:
	if (macsec_fs_is_macsec_device_empty(macsec_device)) {
		list_del(&macsec_device->macsec_devices_list_entry);
		kfree(macsec_device);
	}
err_alloc_dev:
	kfree(fs_id_iter);
	return err;
}

static void macsec_fs_tx_del_rule(struct mlx5_macsec_fs *macsec_fs,
				  struct mlx5_macsec_tx_rule *tx_rule,
				  void *macdev)
{
	macsec_fs_id_del(&macsec_fs->macsec_devices_list, tx_rule->fs_id, macdev,
			 &macsec_fs->sci_hash, true);

	if (tx_rule->rule) {
		mlx5_del_flow_rules(tx_rule->rule);
		tx_rule->rule = NULL;
	}

	if (tx_rule->pkt_reformat) {
		mlx5_packet_reformat_dealloc(macsec_fs->mdev, tx_rule->pkt_reformat);
		tx_rule->pkt_reformat = NULL;
	}

	if (tx_rule->fs_id) {
		ida_free(&macsec_fs->tx_fs->tx_halloc, tx_rule->fs_id);
		tx_rule->fs_id = 0;
	}

	kfree(tx_rule);

	macsec_fs_tx_ft_put(macsec_fs);
}

#define MLX5_REFORMAT_PARAM_ADD_MACSEC_OFFSET_4_BYTES 1

static union mlx5_macsec_rule *
macsec_fs_tx_add_rule(struct mlx5_macsec_fs *macsec_fs,
		      const struct macsec_context *macsec_ctx,
		      struct mlx5_macsec_rule_attrs *attrs, u32 *fs_id)
{
	char reformatbf[MLX5_MACSEC_TAG_LEN + MACSEC_SCI_LEN];
	struct mlx5_pkt_reformat_params reformat_params = {};
	struct mlx5_macsec_tx *tx_fs = macsec_fs->tx_fs;
	struct mlx5_core_dev *mdev = macsec_fs->mdev;
	union mlx5_macsec_rule *macsec_rule = NULL;
	struct mlx5_flow_destination dest = {};
	struct mlx5_macsec_tables *tx_tables;
	struct mlx5_macsec_tx_rule *tx_rule;
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	size_t reformat_size;
	int err = 0;

	tx_tables = &tx_fs->tables;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return NULL;

	err = macsec_fs_tx_ft_get(macsec_fs);
	if (err)
		goto out_spec;

	macsec_rule = kzalloc(sizeof(*macsec_rule), GFP_KERNEL);
	if (!macsec_rule) {
		macsec_fs_tx_ft_put(macsec_fs);
		goto out_spec;
	}

	tx_rule = &macsec_rule->tx_rule;

	/* Tx crypto table crypto rule */
	macsec_fs_tx_create_sectag_header(macsec_ctx, reformatbf, &reformat_size);

	reformat_params.type = MLX5_REFORMAT_TYPE_ADD_MACSEC;
	reformat_params.size = reformat_size;
	reformat_params.data = reformatbf;

	if (is_vlan_dev(macsec_ctx->netdev))
		reformat_params.param_0 = MLX5_REFORMAT_PARAM_ADD_MACSEC_OFFSET_4_BYTES;

	flow_act.pkt_reformat = mlx5_packet_reformat_alloc(mdev,
							   &reformat_params,
							   MLX5_FLOW_NAMESPACE_EGRESS_MACSEC);
	if (IS_ERR(flow_act.pkt_reformat)) {
		err = PTR_ERR(flow_act.pkt_reformat);
		mlx5_core_err(mdev, "Failed to allocate MACsec Tx reformat context err=%d\n",  err);
		goto err;
	}
	tx_rule->pkt_reformat = flow_act.pkt_reformat;

	err = macsec_fs_tx_setup_fte(macsec_fs, spec, &flow_act, attrs->macsec_obj_id, fs_id);
	if (err) {
		mlx5_core_err(mdev,
			      "Failed to add packet reformat for MACsec TX crypto rule, err=%d\n",
			      err);
		goto err;
	}

	tx_rule->fs_id = *fs_id;

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
			  MLX5_FLOW_CONTEXT_ACTION_CRYPTO_ENCRYPT |
			  MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = tx_tables->ft_check;
	rule = mlx5_add_flow_rules(tx_tables->ft_crypto.t, spec, &flow_act, &dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "Failed to add MACsec TX crypto rule, err=%d\n", err);
		goto err;
	}
	tx_rule->rule = rule;

	err = macsec_fs_id_add(&macsec_fs->macsec_devices_list, *fs_id, macsec_ctx->secy->netdev,
			       &macsec_fs->sci_hash, attrs->sci, true);
	if (err) {
		mlx5_core_err(mdev, "Failed to save fs_id, err=%d\n", err);
		goto err;
	}

	goto out_spec;

err:
	macsec_fs_tx_del_rule(macsec_fs, tx_rule, macsec_ctx->secy->netdev);
	macsec_rule = NULL;
out_spec:
	kvfree(spec);

	return macsec_rule;
}

static void macsec_fs_tx_cleanup(struct mlx5_macsec_fs *macsec_fs)
{
	struct mlx5_macsec_tx *tx_fs = macsec_fs->tx_fs;
	struct mlx5_core_dev *mdev = macsec_fs->mdev;
	struct mlx5_macsec_tables *tx_tables;

	if (!tx_fs)
		return;

	tx_tables = &tx_fs->tables;
	if (tx_tables->refcnt) {
		mlx5_core_err(mdev,
			      "Can't destroy MACsec offload tx_fs, refcnt(%u) isn't 0\n",
			      tx_tables->refcnt);
		return;
	}

	ida_destroy(&tx_fs->tx_halloc);

	if (tx_tables->check_miss_rule_counter) {
		mlx5_fc_destroy(mdev, tx_tables->check_miss_rule_counter);
		tx_tables->check_miss_rule_counter = NULL;
	}

	if (tx_tables->check_rule_counter) {
		mlx5_fc_destroy(mdev, tx_tables->check_rule_counter);
		tx_tables->check_rule_counter = NULL;
	}

	kfree(tx_fs);
	macsec_fs->tx_fs = NULL;
}

static int macsec_fs_tx_init(struct mlx5_macsec_fs *macsec_fs)
{
	struct mlx5_core_dev *mdev = macsec_fs->mdev;
	struct mlx5_macsec_tables *tx_tables;
	struct mlx5_macsec_tx *tx_fs;
	struct mlx5_fc *flow_counter;
	int err;

	tx_fs = kzalloc(sizeof(*tx_fs), GFP_KERNEL);
	if (!tx_fs)
		return -ENOMEM;

	tx_tables = &tx_fs->tables;

	flow_counter = mlx5_fc_create(mdev, false);
	if (IS_ERR(flow_counter)) {
		err = PTR_ERR(flow_counter);
		mlx5_core_err(mdev,
			      "Failed to create MACsec Tx encrypt flow counter, err(%d)\n",
			      err);
		goto err_encrypt_counter;
	}
	tx_tables->check_rule_counter = flow_counter;

	flow_counter = mlx5_fc_create(mdev, false);
	if (IS_ERR(flow_counter)) {
		err = PTR_ERR(flow_counter);
		mlx5_core_err(mdev,
			      "Failed to create MACsec Tx drop flow counter, err(%d)\n",
			      err);
		goto err_drop_counter;
	}
	tx_tables->check_miss_rule_counter = flow_counter;

	ida_init(&tx_fs->tx_halloc);
	INIT_LIST_HEAD(&macsec_fs->macsec_devices_list);

	macsec_fs->tx_fs = tx_fs;

	return 0;

err_drop_counter:
	mlx5_fc_destroy(mdev, tx_tables->check_rule_counter);
	tx_tables->check_rule_counter = NULL;

err_encrypt_counter:
	kfree(tx_fs);
	macsec_fs->tx_fs = NULL;

	return err;
}

static void macsec_fs_rx_roce_miss_destroy(struct mlx5_macsec_miss *miss)
{
	mlx5_del_flow_rules(miss->rule);
	mlx5_destroy_flow_group(miss->g);
}

static void macsec_fs_rdma_rx_destroy(struct mlx5_macsec_rx_roce *roce, struct mlx5_core_dev *mdev)
{
	if (!mlx5_is_macsec_roce_supported(mdev))
		return;

	mlx5_del_flow_rules(roce->nic_miss.rule);
	mlx5_del_flow_rules(roce->rule);
	mlx5_modify_header_dealloc(mdev, roce->copy_modify_hdr);
	mlx5_destroy_flow_group(roce->nic_miss.g);
	mlx5_destroy_flow_group(roce->g);
	mlx5_destroy_flow_table(roce->ft);

	macsec_fs_rx_roce_miss_destroy(&roce->miss);
	mlx5_destroy_flow_table(roce->ft_macsec_op_check);
	mlx5_destroy_flow_table(roce->ft_ip_check);
}

static void macsec_fs_rx_destroy(struct mlx5_macsec_fs *macsec_fs)
{
	struct mlx5_macsec_rx *rx_fs = macsec_fs->rx_fs;
	struct mlx5_macsec_tables *rx_tables;
	int i;

	/* Rx check table */
	for (i = 1; i >= 0; --i) {
		if (rx_fs->check_rule[i]) {
			mlx5_del_flow_rules(rx_fs->check_rule[i]);
			rx_fs->check_rule[i] = NULL;
		}

		if (rx_fs->check_rule_pkt_reformat[i]) {
			mlx5_packet_reformat_dealloc(macsec_fs->mdev,
						     rx_fs->check_rule_pkt_reformat[i]);
			rx_fs->check_rule_pkt_reformat[i] = NULL;
		}
	}

	rx_tables = &rx_fs->tables;

	if (rx_tables->check_miss_rule) {
		mlx5_del_flow_rules(rx_tables->check_miss_rule);
		rx_tables->check_miss_rule = NULL;
	}

	if (rx_tables->ft_check_group) {
		mlx5_destroy_flow_group(rx_tables->ft_check_group);
		rx_tables->ft_check_group = NULL;
	}

	if (rx_tables->ft_check) {
		mlx5_destroy_flow_table(rx_tables->ft_check);
		rx_tables->ft_check = NULL;
	}

	/* Rx crypto table */
	if (rx_tables->crypto_miss_rule) {
		mlx5_del_flow_rules(rx_tables->crypto_miss_rule);
		rx_tables->crypto_miss_rule = NULL;
	}

	macsec_fs_destroy_flow_table(&rx_tables->ft_crypto);

	macsec_fs_rdma_rx_destroy(&macsec_fs->rx_fs->roce, macsec_fs->mdev);
}

static int macsec_fs_rx_create_crypto_table_groups(struct mlx5_macsec_flow_table *ft)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	int mclen = MLX5_ST_SZ_BYTES(fte_match_param);
	int ix = 0;
	u32 *in;
	int err;
	u8 *mc;

	ft->g = kcalloc(RX_CRYPTO_TABLE_NUM_GROUPS, sizeof(*ft->g), GFP_KERNEL);
	if (!ft->g)
		return -ENOMEM;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in) {
		kfree(ft->g);
		return -ENOMEM;
	}

	mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);

	/* Flow group for SA rule with SCI */
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS |
						MLX5_MATCH_MISC_PARAMETERS_5);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.ethertype);

	MLX5_SET(fte_match_param, mc, misc_parameters_5.macsec_tag_0,
		 MLX5_MACSEC_SECTAG_TCI_AN_FIELD_BITMASK <<
		 MLX5_MACSEC_SECTAG_TCI_AN_FIELD_OFFSET);
	MLX5_SET_TO_ONES(fte_match_param, mc, misc_parameters_5.macsec_tag_2);
	MLX5_SET_TO_ONES(fte_match_param, mc, misc_parameters_5.macsec_tag_3);

	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += RX_CRYPTO_TABLE_SA_RULE_WITH_SCI_GROUP_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err;
	ft->num_groups++;

	/* Flow group for SA rule without SCI */
	memset(in, 0, inlen);
	memset(mc, 0, mclen);
	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS |
						MLX5_MATCH_MISC_PARAMETERS_5);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.smac_47_16);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.smac_15_0);
	MLX5_SET_TO_ONES(fte_match_param, mc, outer_headers.ethertype);

	MLX5_SET(fte_match_param, mc, misc_parameters_5.macsec_tag_0,
		 MLX5_MACSEC_SECTAG_TCI_AN_FIELD_BITMASK << MLX5_MACSEC_SECTAG_TCI_AN_FIELD_OFFSET);

	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += RX_CRYPTO_TABLE_SA_RULE_WITHOUT_SCI_GROUP_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err;
	ft->num_groups++;

	/* Flow Group for l2 traps */
	memset(in, 0, inlen);
	memset(mc, 0, mclen);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += CRYPTO_TABLE_DEFAULT_RULE_GROUP_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	ft->g[ft->num_groups] = mlx5_create_flow_group(ft->t, in);
	if (IS_ERR(ft->g[ft->num_groups]))
		goto err;
	ft->num_groups++;

	kvfree(in);
	return 0;

err:
	err = PTR_ERR(ft->g[ft->num_groups]);
	ft->g[ft->num_groups] = NULL;
	kvfree(in);

	return err;
}

static int macsec_fs_rx_create_check_decap_rule(struct mlx5_macsec_fs *macsec_fs,
						struct mlx5_flow_destination *dest,
						struct mlx5_flow_act *flow_act,
						struct mlx5_flow_spec *spec,
						int reformat_param_size)
{
	int rule_index = (reformat_param_size == MLX5_SECTAG_HEADER_SIZE_WITH_SCI) ? 0 : 1;
	u8 mlx5_reformat_buf[MLX5_SECTAG_HEADER_SIZE_WITH_SCI];
	struct mlx5_pkt_reformat_params reformat_params = {};
	struct mlx5_macsec_rx *rx_fs = macsec_fs->rx_fs;
	struct mlx5_core_dev *mdev = macsec_fs->mdev;
	struct mlx5_flow_destination roce_dest[2];
	struct mlx5_macsec_tables *rx_tables;
	struct mlx5_flow_handle *rule;
	int err = 0, dstn = 0;

	rx_tables = &rx_fs->tables;

	/* Rx check table decap 16B rule */
	memset(dest, 0, sizeof(*dest));
	memset(flow_act, 0, sizeof(*flow_act));
	memset(spec, 0, sizeof(*spec));

	reformat_params.type = MLX5_REFORMAT_TYPE_DEL_MACSEC;
	reformat_params.size = reformat_param_size;
	reformat_params.data = mlx5_reformat_buf;
	flow_act->pkt_reformat = mlx5_packet_reformat_alloc(mdev,
							    &reformat_params,
							    MLX5_FLOW_NAMESPACE_KERNEL_RX_MACSEC);
	if (IS_ERR(flow_act->pkt_reformat)) {
		err = PTR_ERR(flow_act->pkt_reformat);
		mlx5_core_err(mdev, "Failed to allocate MACsec Rx reformat context err=%d\n", err);
		return err;
	}
	rx_fs->check_rule_pkt_reformat[rule_index] = flow_act->pkt_reformat;

	spec->match_criteria_enable = MLX5_MATCH_MISC_PARAMETERS_2;
	/* MACsec syndrome match */
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, misc_parameters_2.macsec_syndrome);
	MLX5_SET(fte_match_param, spec->match_value, misc_parameters_2.macsec_syndrome, 0);
	/* ASO return reg syndrome match */
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, misc_parameters_2.metadata_reg_c_4);
	MLX5_SET(fte_match_param, spec->match_value, misc_parameters_2.metadata_reg_c_4, 0);

	spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS_5;
	/* Sectag TCI SC present bit*/
	MLX5_SET(fte_match_param, spec->match_criteria, misc_parameters_5.macsec_tag_0,
		 MLX5_MACSEC_SECTAG_TCI_SC_FIELD_BIT << MLX5_MACSEC_SECTAG_TCI_AN_FIELD_OFFSET);

	if (reformat_param_size == MLX5_SECTAG_HEADER_SIZE_WITH_SCI)
		MLX5_SET(fte_match_param, spec->match_value, misc_parameters_5.macsec_tag_0,
			 MLX5_MACSEC_SECTAG_TCI_SC_FIELD_BIT <<
			 MLX5_MACSEC_SECTAG_TCI_AN_FIELD_OFFSET);

	flow_act->flags = FLOW_ACT_NO_APPEND;

	if (rx_fs->roce.ft) {
		flow_act->action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
		roce_dest[dstn].type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
		roce_dest[dstn].ft = rx_fs->roce.ft;
		dstn++;
	} else {
		flow_act->action = MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_PRIO;
	}

	flow_act->action |= MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT |
			    MLX5_FLOW_CONTEXT_ACTION_COUNT;
	roce_dest[dstn].type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
	roce_dest[dstn].counter = rx_tables->check_rule_counter;
	rule = mlx5_add_flow_rules(rx_tables->ft_check, spec, flow_act, roce_dest, dstn + 1);

	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "Failed to add MACsec Rx check rule, err=%d\n", err);
		return err;
	}

	rx_fs->check_rule[rule_index] = rule;

	return 0;
}

static int macsec_fs_rx_roce_miss_create(struct mlx5_core_dev *mdev,
					 struct mlx5_macsec_rx_roce *roce)
{
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_group *flow_group;
	struct mlx5_flow_handle *rule;
	u32 *flow_group_in;
	int err;

	flow_group_in = kvzalloc(MLX5_ST_SZ_BYTES(create_flow_group_in), GFP_KERNEL);
	if (!flow_group_in)
		return -ENOMEM;

	/* IP check ft has no miss rule since we use default miss action which is go to next PRIO */
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index,
		 roce->ft_macsec_op_check->max_fte - 1);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index,
		 roce->ft_macsec_op_check->max_fte - 1);
	flow_group = mlx5_create_flow_group(roce->ft_macsec_op_check, flow_group_in);
	if (IS_ERR(flow_group)) {
		err = PTR_ERR(flow_group);
		mlx5_core_err(mdev,
			      "Failed to create miss flow group for MACsec RoCE operation check table err(%d)\n",
			      err);
		goto err_macsec_op_miss_group;
	}
	roce->miss.g = flow_group;

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_DROP;
	rule = mlx5_add_flow_rules(roce->ft_macsec_op_check,  NULL, &flow_act, NULL, 0);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "Failed to add miss rule to MACsec RoCE operation check table err(%d)\n",
			      err);
		goto err_macsec_op_rule;
	}
	roce->miss.rule = rule;

	kvfree(flow_group_in);
	return 0;

err_macsec_op_rule:
	mlx5_destroy_flow_group(roce->miss.g);
err_macsec_op_miss_group:
	kvfree(flow_group_in);
	return err;
}

#define MLX5_RX_ROCE_GROUP_SIZE BIT(0)

static int macsec_fs_rx_roce_jump_to_rdma_groups_create(struct mlx5_core_dev *mdev,
							struct mlx5_macsec_rx_roce *roce)
{
	struct mlx5_flow_group *g;
	void *outer_headers_c;
	int ix = 0;
	u32 *in;
	int err;
	u8 *mc;

	in = kvzalloc(MLX5_ST_SZ_BYTES(create_flow_group_in), GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	mc = MLX5_ADDR_OF(create_flow_group_in, in, match_criteria);
	outer_headers_c = MLX5_ADDR_OF(fte_match_param, mc, outer_headers);
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, ip_protocol);
	MLX5_SET_TO_ONES(fte_match_set_lyr_2_4, outer_headers_c, udp_dport);

	MLX5_SET_CFG(in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5_RX_ROCE_GROUP_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	g = mlx5_create_flow_group(roce->ft, in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		mlx5_core_err(mdev, "Failed to create main flow group for MACsec RoCE NIC UDP table err(%d)\n",
			      err);
		goto err_udp_group;
	}
	roce->g = g;

	memset(in, 0, MLX5_ST_SZ_BYTES(create_flow_group_in));
	MLX5_SET_CFG(in, start_flow_index, ix);
	ix += MLX5_RX_ROCE_GROUP_SIZE;
	MLX5_SET_CFG(in, end_flow_index, ix - 1);
	g = mlx5_create_flow_group(roce->ft, in);
	if (IS_ERR(g)) {
		err = PTR_ERR(g);
		mlx5_core_err(mdev, "Failed to create miss flow group for MACsec RoCE NIC UDP table err(%d)\n",
			      err);
		goto err_udp_miss_group;
	}
	roce->nic_miss.g = g;

	kvfree(in);
	return 0;

err_udp_miss_group:
	mlx5_destroy_flow_group(roce->g);
err_udp_group:
	kvfree(in);
	return err;
}

static int macsec_fs_rx_roce_jump_to_rdma_rules_create(struct mlx5_macsec_fs *macsec_fs,
						       struct mlx5_macsec_rx_roce *roce)
{
	u8 action[MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto)] = {};
	struct mlx5_core_dev *mdev = macsec_fs->mdev;
	struct mlx5_flow_destination dst = {};
	struct mlx5_modify_hdr *modify_hdr;
	MLX5_DECLARE_FLOW_ACT(flow_act);
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	int err;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ip_protocol);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ip_protocol, IPPROTO_UDP);
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.udp_dport);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.udp_dport, ROCE_V2_UDP_DPORT);

	MLX5_SET(copy_action_in, action, action_type, MLX5_ACTION_TYPE_COPY);
	MLX5_SET(copy_action_in, action, src_field, MLX5_ACTION_IN_FIELD_METADATA_REG_B);
	MLX5_SET(copy_action_in, action, src_offset, 0);
	MLX5_SET(copy_action_in, action, length, 32);
	MLX5_SET(copy_action_in, action, dst_field, MLX5_ACTION_IN_FIELD_METADATA_REG_C_5);
	MLX5_SET(copy_action_in, action, dst_offset, 0);

	modify_hdr = mlx5_modify_header_alloc(macsec_fs->mdev, MLX5_FLOW_NAMESPACE_KERNEL_RX_MACSEC,
					      1, action);

	if (IS_ERR(modify_hdr)) {
		err = PTR_ERR(modify_hdr);
		mlx5_core_err(mdev,
			      "Failed to alloc macsec copy modify_header_id err(%d)\n", err);
		goto err_alloc_hdr;
	}

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_MOD_HDR | MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	flow_act.modify_hdr = modify_hdr;
	dst.type = MLX5_FLOW_DESTINATION_TYPE_TABLE_TYPE;
	dst.ft = roce->ft_ip_check;
	rule = mlx5_add_flow_rules(roce->ft, spec, &flow_act, &dst, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "Failed to add rule to MACsec RoCE NIC UDP table err(%d)\n",
			      err);
		goto err_add_rule;
	}
	roce->rule = rule;
	roce->copy_modify_hdr = modify_hdr;

	memset(&flow_act, 0, sizeof(flow_act));
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_PRIO;
	rule = mlx5_add_flow_rules(roce->ft, NULL, &flow_act, NULL, 0);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "Failed to add miss rule to MACsec RoCE NIC UDP table err(%d)\n",
			      err);
		goto err_add_rule2;
	}
	roce->nic_miss.rule = rule;

	kvfree(spec);
	return 0;

err_add_rule2:
	mlx5_del_flow_rules(roce->rule);
err_add_rule:
	mlx5_modify_header_dealloc(macsec_fs->mdev, modify_hdr);
err_alloc_hdr:
	kvfree(spec);
	return err;
}

static int macsec_fs_rx_roce_jump_to_rdma_create(struct mlx5_macsec_fs *macsec_fs,
						 struct mlx5_macsec_rx_roce *roce)
{
	int err;

	err = macsec_fs_rx_roce_jump_to_rdma_groups_create(macsec_fs->mdev, roce);
	if (err)
		return err;

	err = macsec_fs_rx_roce_jump_to_rdma_rules_create(macsec_fs, roce);
	if (err)
		goto err;

	return 0;
err:
	mlx5_destroy_flow_group(roce->nic_miss.g);
	mlx5_destroy_flow_group(roce->g);
	return err;
}

static int macsec_fs_rx_roce_create(struct mlx5_macsec_fs *macsec_fs)
{
	struct mlx5_macsec_rx *rx_fs = macsec_fs->rx_fs;
	struct mlx5_core_dev *mdev = macsec_fs->mdev;
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_namespace *ns;
	struct mlx5_flow_table *ft;
	int err = 0;

	if (!mlx5_is_macsec_roce_supported(macsec_fs->mdev)) {
		mlx5_core_dbg(mdev, "Failed to init RoCE MACsec, capabilities not supported\n");
		return 0;
	}

	ns = mlx5_get_flow_namespace(macsec_fs->mdev, MLX5_FLOW_NAMESPACE_RDMA_RX_MACSEC);
	if (!ns)
		return -ENOMEM;

	ft = macsec_fs_auto_group_table_create(ns, 0, RDMA_RX_ROCE_IP_TABLE_LEVEL,
					       CRYPTO_NUM_MAXSEC_FTE);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		mlx5_core_err(mdev,
			      "Failed to create MACsec IP check RoCE table err(%d)\n", err);
		return err;
	}
	rx_fs->roce.ft_ip_check = ft;

	ft = macsec_fs_auto_group_table_create(ns, 0, RDMA_RX_ROCE_MACSEC_OP_TABLE_LEVEL,
					       CRYPTO_NUM_MAXSEC_FTE);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		mlx5_core_err(mdev,
			      "Failed to create MACsec operation check RoCE table err(%d)\n",
			      err);
		goto err_macsec_op;
	}
	rx_fs->roce.ft_macsec_op_check = ft;

	err = macsec_fs_rx_roce_miss_create(mdev, &rx_fs->roce);
	if (err)
		goto err_miss_create;

	ns = mlx5_get_flow_namespace(macsec_fs->mdev, MLX5_FLOW_NAMESPACE_KERNEL_RX_MACSEC);
	if (!ns) {
		err = -EOPNOTSUPP;
		goto err_ns;
	}

	ft_attr.level = RX_ROCE_TABLE_LEVEL;
	ft_attr.max_fte = RX_ROCE_TABLE_NUM_FTE;
	ft = mlx5_create_flow_table(ns, &ft_attr);
	if (IS_ERR(ft)) {
		err = PTR_ERR(ft);
		mlx5_core_err(mdev,
			      "Failed to create MACsec jump to RX RoCE, NIC table err(%d)\n", err);
		goto err_ns;
	}
	rx_fs->roce.ft = ft;

	err = macsec_fs_rx_roce_jump_to_rdma_create(macsec_fs, &rx_fs->roce);
	if (err)
		goto err_udp_ft;

	return 0;

err_udp_ft:
	mlx5_destroy_flow_table(rx_fs->roce.ft);
err_ns:
	macsec_fs_rx_roce_miss_destroy(&rx_fs->roce.miss);
err_miss_create:
	mlx5_destroy_flow_table(rx_fs->roce.ft_macsec_op_check);
err_macsec_op:
	mlx5_destroy_flow_table(rx_fs->roce.ft_ip_check);
	return err;
}

static int macsec_fs_rx_create(struct mlx5_macsec_fs *macsec_fs)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5_macsec_rx *rx_fs = macsec_fs->rx_fs;
	struct mlx5_core_dev *mdev = macsec_fs->mdev;
	struct mlx5_macsec_flow_table *ft_crypto;
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_destination dest = {};
	struct mlx5_macsec_tables *rx_tables;
	struct mlx5_flow_table *flow_table;
	struct mlx5_flow_group *flow_group;
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_namespace *ns;
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	u32 *flow_group_in;
	int err;

	ns = mlx5_get_flow_namespace(mdev, MLX5_FLOW_NAMESPACE_KERNEL_RX_MACSEC);
	if (!ns)
		return -ENOMEM;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	if (!flow_group_in) {
		err = -ENOMEM;
		goto free_spec;
	}

	rx_tables = &rx_fs->tables;
	ft_crypto = &rx_tables->ft_crypto;

	err = macsec_fs_rx_roce_create(macsec_fs);
	if (err)
		goto out_flow_group;

	/* Rx crypto table */
	ft_attr.level = RX_CRYPTO_TABLE_LEVEL;
	ft_attr.max_fte = CRYPTO_NUM_MAXSEC_FTE;

	flow_table = mlx5_create_flow_table(ns, &ft_attr);
	if (IS_ERR(flow_table)) {
		err = PTR_ERR(flow_table);
		mlx5_core_err(mdev, "Failed to create MACsec Rx crypto table err(%d)\n", err);
		goto err;
	}
	ft_crypto->t = flow_table;

	/* Rx crypto table groups */
	err = macsec_fs_rx_create_crypto_table_groups(ft_crypto);
	if (err) {
		mlx5_core_err(mdev,
			      "Failed to create default flow group for MACsec Tx crypto table err(%d)\n",
			      err);
		goto err;
	}

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_NEXT_PRIO;
	rule = mlx5_add_flow_rules(ft_crypto->t, NULL, &flow_act, NULL, 0);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev,
			      "Failed to add MACsec Rx crypto table default miss rule %d\n",
			      err);
		goto err;
	}
	rx_tables->crypto_miss_rule = rule;

	/* Rx check table */
	flow_table = macsec_fs_auto_group_table_create(ns,
						       MLX5_FLOW_TABLE_TUNNEL_EN_REFORMAT,
						       RX_CHECK_TABLE_LEVEL,
						       RX_CHECK_TABLE_NUM_FTE);
	if (IS_ERR(flow_table)) {
		err = PTR_ERR(flow_table);
		mlx5_core_err(mdev, "Fail to create MACsec RX check table, err(%d)\n", err);
		goto err;
	}
	rx_tables->ft_check = flow_table;

	/* Rx check table Default miss group/rule */
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, flow_table->max_fte - 1);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, flow_table->max_fte - 1);
	flow_group = mlx5_create_flow_group(rx_tables->ft_check, flow_group_in);
	if (IS_ERR(flow_group)) {
		err = PTR_ERR(flow_group);
		mlx5_core_err(mdev,
			      "Failed to create default flow group for MACsec Rx check table err(%d)\n",
			      err);
		goto err;
	}
	rx_tables->ft_check_group = flow_group;

	/* Rx check table default drop rule */
	memset(&flow_act, 0, sizeof(flow_act));

	dest.type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
	dest.counter = rx_tables->check_miss_rule_counter;
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_DROP | MLX5_FLOW_CONTEXT_ACTION_COUNT;
	rule = mlx5_add_flow_rules(rx_tables->ft_check,  NULL, &flow_act, &dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev, "Failed to added MACsec Rx check drop rule, err(%d)\n", err);
		goto err;
	}
	rx_tables->check_miss_rule = rule;

	/* Rx check table decap rules */
	err = macsec_fs_rx_create_check_decap_rule(macsec_fs, &dest, &flow_act, spec,
						   MLX5_SECTAG_HEADER_SIZE_WITH_SCI);
	if (err)
		goto err;

	err = macsec_fs_rx_create_check_decap_rule(macsec_fs, &dest, &flow_act, spec,
						   MLX5_SECTAG_HEADER_SIZE_WITHOUT_SCI);
	if (err)
		goto err;

	goto out_flow_group;

err:
	macsec_fs_rx_destroy(macsec_fs);
out_flow_group:
	kvfree(flow_group_in);
free_spec:
	kvfree(spec);
	return err;
}

static int macsec_fs_rx_ft_get(struct mlx5_macsec_fs *macsec_fs)
{
	struct mlx5_macsec_tables *rx_tables = &macsec_fs->rx_fs->tables;
	int err = 0;

	if (rx_tables->refcnt)
		goto out;

	err = macsec_fs_rx_create(macsec_fs);
	if (err)
		return err;

out:
	rx_tables->refcnt++;
	return err;
}

static void macsec_fs_rx_ft_put(struct mlx5_macsec_fs *macsec_fs)
{
	struct mlx5_macsec_tables *rx_tables = &macsec_fs->rx_fs->tables;

	if (--rx_tables->refcnt)
		return;

	macsec_fs_rx_destroy(macsec_fs);
}

static void macsec_fs_rx_del_rule(struct mlx5_macsec_fs *macsec_fs,
				  struct mlx5_macsec_rx_rule *rx_rule,
				  void *macdev, u32 fs_id)
{
	int i;

	macsec_fs_id_del(&macsec_fs->macsec_devices_list, fs_id, macdev,
			 &macsec_fs->fs_id_hash, false);

	for (i = 0; i < RX_NUM_OF_RULES_PER_SA; ++i) {
		if (rx_rule->rule[i]) {
			mlx5_del_flow_rules(rx_rule->rule[i]);
			rx_rule->rule[i] = NULL;
		}
	}

	if (rx_rule->meta_modhdr) {
		mlx5_modify_header_dealloc(macsec_fs->mdev, rx_rule->meta_modhdr);
		rx_rule->meta_modhdr = NULL;
	}

	kfree(rx_rule);

	macsec_fs_rx_ft_put(macsec_fs);
}

static void macsec_fs_rx_setup_fte(struct mlx5_flow_spec *spec,
				   struct mlx5_flow_act *flow_act,
				   struct mlx5_macsec_rule_attrs *attrs,
				   bool sci_present)
{
	u8 tci_an = (sci_present << MLX5_MACSEC_SECTAG_TCI_SC_FIELD_OFFSET) | attrs->assoc_num;
	struct mlx5_flow_act_crypto_params *crypto_params = &flow_act->crypto;
	__be32 *sci_p = (__be32 *)(&attrs->sci);

	spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;

	/* MACsec ethertype */
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ethertype);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ethertype, ETH_P_MACSEC);

	spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS_5;

	/* Sectag AN + TCI SC present bit*/
	MLX5_SET(fte_match_param, spec->match_criteria, misc_parameters_5.macsec_tag_0,
		 MLX5_MACSEC_SECTAG_TCI_AN_FIELD_BITMASK << MLX5_MACSEC_SECTAG_TCI_AN_FIELD_OFFSET);
	MLX5_SET(fte_match_param, spec->match_value, misc_parameters_5.macsec_tag_0,
		 tci_an << MLX5_MACSEC_SECTAG_TCI_AN_FIELD_OFFSET);

	if (sci_present) {
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 misc_parameters_5.macsec_tag_2);
		MLX5_SET(fte_match_param, spec->match_value, misc_parameters_5.macsec_tag_2,
			 be32_to_cpu(sci_p[0]));

		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 misc_parameters_5.macsec_tag_3);
		MLX5_SET(fte_match_param, spec->match_value, misc_parameters_5.macsec_tag_3,
			 be32_to_cpu(sci_p[1]));
	} else {
		/* When SCI isn't present in the Sectag, need to match the source */
		/* MAC address only if the SCI contains the default MACsec PORT	  */
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.smac_47_16);
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.smac_15_0);
		memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value, outer_headers.smac_47_16),
		       sci_p, ETH_ALEN);
	}

	crypto_params->type = MLX5_FLOW_CONTEXT_ENCRYPT_DECRYPT_TYPE_MACSEC;
	crypto_params->obj_id = attrs->macsec_obj_id;
}

static union mlx5_macsec_rule *
macsec_fs_rx_add_rule(struct mlx5_macsec_fs *macsec_fs,
		      const struct macsec_context *macsec_ctx,
		      struct mlx5_macsec_rule_attrs *attrs,
		      u32 fs_id)
{
	u8 action[MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto)] = {};
	struct mlx5_macsec_rx *rx_fs = macsec_fs->rx_fs;
	struct mlx5_core_dev *mdev = macsec_fs->mdev;
	union mlx5_macsec_rule *macsec_rule = NULL;
	struct mlx5_modify_hdr *modify_hdr = NULL;
	struct mlx5_macsec_flow_table *ft_crypto;
	struct mlx5_flow_destination dest = {};
	struct mlx5_macsec_tables *rx_tables;
	struct mlx5_macsec_rx_rule *rx_rule;
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	int err = 0;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return NULL;

	err = macsec_fs_rx_ft_get(macsec_fs);
	if (err)
		goto out_spec;

	macsec_rule = kzalloc(sizeof(*macsec_rule), GFP_KERNEL);
	if (!macsec_rule) {
		macsec_fs_rx_ft_put(macsec_fs);
		goto out_spec;
	}

	rx_rule = &macsec_rule->rx_rule;
	rx_tables = &rx_fs->tables;
	ft_crypto = &rx_tables->ft_crypto;

	/* Set bit[31 - 30] macsec marker - 0x01 */
	/* Set bit[15-0] fs id */
	MLX5_SET(set_action_in, action, action_type, MLX5_ACTION_TYPE_SET);
	MLX5_SET(set_action_in, action, field, MLX5_ACTION_IN_FIELD_METADATA_REG_B);
	MLX5_SET(set_action_in, action, data, macsec_fs_set_rx_fs_id(fs_id));
	MLX5_SET(set_action_in, action, offset, 0);
	MLX5_SET(set_action_in, action, length, 32);

	modify_hdr = mlx5_modify_header_alloc(mdev, MLX5_FLOW_NAMESPACE_KERNEL_RX_MACSEC,
					      1, action);
	if (IS_ERR(modify_hdr)) {
		err = PTR_ERR(modify_hdr);
		mlx5_core_err(mdev, "Fail to alloc MACsec set modify_header_id err=%d\n", err);
		modify_hdr = NULL;
		goto err;
	}
	rx_rule->meta_modhdr = modify_hdr;

	/* Rx crypto table with SCI rule */
	macsec_fs_rx_setup_fte(spec, &flow_act, attrs, true);

	flow_act.modify_hdr = modify_hdr;
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
			  MLX5_FLOW_CONTEXT_ACTION_CRYPTO_DECRYPT |
			  MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;

	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = rx_tables->ft_check;
	rule = mlx5_add_flow_rules(ft_crypto->t, spec, &flow_act, &dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		mlx5_core_err(mdev,
			      "Failed to add SA with SCI rule to Rx crypto rule, err=%d\n",
			      err);
		goto err;
	}
	rx_rule->rule[0] = rule;

	/* Rx crypto table without SCI rule */
	if ((cpu_to_be64((__force u64)attrs->sci) & 0xFFFF) == ntohs(MACSEC_PORT_ES)) {
		memset(spec, 0, sizeof(struct mlx5_flow_spec));
		memset(&dest, 0, sizeof(struct mlx5_flow_destination));
		memset(&flow_act, 0, sizeof(flow_act));

		macsec_fs_rx_setup_fte(spec, &flow_act, attrs, false);

		flow_act.modify_hdr = modify_hdr;
		flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
				  MLX5_FLOW_CONTEXT_ACTION_CRYPTO_DECRYPT |
				  MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;

		dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
		dest.ft = rx_tables->ft_check;
		rule = mlx5_add_flow_rules(ft_crypto->t, spec, &flow_act, &dest, 1);
		if (IS_ERR(rule)) {
			err = PTR_ERR(rule);
			mlx5_core_err(mdev,
				      "Failed to add SA without SCI rule to Rx crypto rule, err=%d\n",
				      err);
			goto err;
		}
		rx_rule->rule[1] = rule;
	}

	err = macsec_fs_id_add(&macsec_fs->macsec_devices_list, fs_id, macsec_ctx->secy->netdev,
			       &macsec_fs->fs_id_hash, attrs->sci, false);
	if (err) {
		mlx5_core_err(mdev, "Failed to save fs_id, err=%d\n", err);
		goto err;
	}

	kvfree(spec);
	return macsec_rule;

err:
	macsec_fs_rx_del_rule(macsec_fs, rx_rule, macsec_ctx->secy->netdev, fs_id);
	macsec_rule = NULL;
out_spec:
	kvfree(spec);
	return macsec_rule;
}

static int macsec_fs_rx_init(struct mlx5_macsec_fs *macsec_fs)
{
	struct mlx5_core_dev *mdev = macsec_fs->mdev;
	struct mlx5_macsec_tables *rx_tables;
	struct mlx5_macsec_rx *rx_fs;
	struct mlx5_fc *flow_counter;
	int err;

	rx_fs =	kzalloc(sizeof(*rx_fs), GFP_KERNEL);
	if (!rx_fs)
		return -ENOMEM;

	flow_counter = mlx5_fc_create(mdev, false);
	if (IS_ERR(flow_counter)) {
		err = PTR_ERR(flow_counter);
		mlx5_core_err(mdev,
			      "Failed to create MACsec Rx encrypt flow counter, err(%d)\n",
			      err);
		goto err_encrypt_counter;
	}

	rx_tables = &rx_fs->tables;
	rx_tables->check_rule_counter = flow_counter;

	flow_counter = mlx5_fc_create(mdev, false);
	if (IS_ERR(flow_counter)) {
		err = PTR_ERR(flow_counter);
		mlx5_core_err(mdev,
			      "Failed to create MACsec Rx drop flow counter, err(%d)\n",
			      err);
		goto err_drop_counter;
	}
	rx_tables->check_miss_rule_counter = flow_counter;

	macsec_fs->rx_fs = rx_fs;

	return 0;

err_drop_counter:
	mlx5_fc_destroy(mdev, rx_tables->check_rule_counter);
	rx_tables->check_rule_counter = NULL;

err_encrypt_counter:
	kfree(rx_fs);
	macsec_fs->rx_fs = NULL;

	return err;
}

static void macsec_fs_rx_cleanup(struct mlx5_macsec_fs *macsec_fs)
{
	struct mlx5_macsec_rx *rx_fs = macsec_fs->rx_fs;
	struct mlx5_core_dev *mdev = macsec_fs->mdev;
	struct mlx5_macsec_tables *rx_tables;

	if (!rx_fs)
		return;

	rx_tables = &rx_fs->tables;

	if (rx_tables->refcnt) {
		mlx5_core_err(mdev,
			      "Can't destroy MACsec offload rx_fs, refcnt(%u) isn't 0\n",
			      rx_tables->refcnt);
		return;
	}

	if (rx_tables->check_miss_rule_counter) {
		mlx5_fc_destroy(mdev, rx_tables->check_miss_rule_counter);
		rx_tables->check_miss_rule_counter = NULL;
	}

	if (rx_tables->check_rule_counter) {
		mlx5_fc_destroy(mdev, rx_tables->check_rule_counter);
		rx_tables->check_rule_counter = NULL;
	}

	kfree(rx_fs);
	macsec_fs->rx_fs = NULL;
}

static void set_ipaddr_spec_v4(struct sockaddr_in *in, struct mlx5_flow_spec *spec, bool is_dst_ip)
{
	MLX5_SET(fte_match_param, spec->match_value,
		 outer_headers.ip_version, MLX5_FS_IPV4_VERSION);

	if (is_dst_ip) {
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.dst_ipv4_dst_ipv6.ipv4_layout.ipv4);
		memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    outer_headers.dst_ipv4_dst_ipv6.ipv4_layout.ipv4),
		       &in->sin_addr.s_addr, 4);
	} else {
		MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
				 outer_headers.src_ipv4_src_ipv6.ipv4_layout.ipv4);
		memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    outer_headers.src_ipv4_src_ipv6.ipv4_layout.ipv4),
		       &in->sin_addr.s_addr, 4);
	}
}

static void set_ipaddr_spec_v6(struct sockaddr_in6 *in6, struct mlx5_flow_spec *spec,
			       bool is_dst_ip)
{
	MLX5_SET(fte_match_param, spec->match_value,
		 outer_headers.ip_version, MLX5_FS_IPV6_VERSION);

	if (is_dst_ip) {
		memset(MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				    outer_headers.dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       0xff, 16);
		memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    outer_headers.dst_ipv4_dst_ipv6.ipv6_layout.ipv6),
		       &in6->sin6_addr, 16);
	} else {
		memset(MLX5_ADDR_OF(fte_match_param, spec->match_criteria,
				    outer_headers.src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       0xff, 16);
		memcpy(MLX5_ADDR_OF(fte_match_param, spec->match_value,
				    outer_headers.src_ipv4_src_ipv6.ipv6_layout.ipv6),
		       &in6->sin6_addr, 16);
	}
}

static void set_ipaddr_spec(const struct sockaddr *addr,
			    struct mlx5_flow_spec *spec, bool is_dst_ip)
{
	struct sockaddr_in6 *in6;

	spec->match_criteria_enable |= MLX5_MATCH_OUTER_HEADERS;
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria,
			 outer_headers.ip_version);

	if (addr->sa_family == AF_INET) {
		struct sockaddr_in *in = (struct sockaddr_in *)addr;

		set_ipaddr_spec_v4(in, spec, is_dst_ip);
		return;
	}

	in6 = (struct sockaddr_in6 *)addr;
	set_ipaddr_spec_v6(in6, spec, is_dst_ip);
}

static void macsec_fs_del_roce_rule_rx(struct mlx5_roce_macsec_rx_rule *rx_rule)
{
	mlx5_del_flow_rules(rx_rule->op);
	mlx5_del_flow_rules(rx_rule->ip);
	list_del(&rx_rule->entry);
	kfree(rx_rule);
}

static void macsec_fs_del_roce_rules_rx(struct mlx5_macsec_fs *macsec_fs, u32 fs_id,
					struct list_head *rx_rules_list)
{
	struct mlx5_roce_macsec_rx_rule *rx_rule, *next;

	if (!mlx5_is_macsec_roce_supported(macsec_fs->mdev))
		return;

	list_for_each_entry_safe(rx_rule, next, rx_rules_list, entry) {
		if (rx_rule->fs_id == fs_id)
			macsec_fs_del_roce_rule_rx(rx_rule);
	}
}

static void macsec_fs_del_roce_rule_tx(struct mlx5_core_dev *mdev,
				       struct mlx5_roce_macsec_tx_rule *tx_rule)
{
	mlx5_del_flow_rules(tx_rule->rule);
	mlx5_modify_header_dealloc(mdev, tx_rule->meta_modhdr);
	list_del(&tx_rule->entry);
	kfree(tx_rule);
}

static void macsec_fs_del_roce_rules_tx(struct mlx5_macsec_fs *macsec_fs, u32 fs_id,
					struct list_head *tx_rules_list)
{
	struct mlx5_roce_macsec_tx_rule *tx_rule, *next;

	if (!mlx5_is_macsec_roce_supported(macsec_fs->mdev))
		return;

	list_for_each_entry_safe(tx_rule, next, tx_rules_list, entry) {
		if (tx_rule->fs_id == fs_id)
			macsec_fs_del_roce_rule_tx(macsec_fs->mdev, tx_rule);
	}
}

void mlx5_macsec_fs_get_stats_fill(struct mlx5_macsec_fs *macsec_fs, void *macsec_stats)
{
	struct mlx5_macsec_stats *stats = (struct mlx5_macsec_stats *)macsec_stats;
	struct mlx5_macsec_tables *tx_tables = &macsec_fs->tx_fs->tables;
	struct mlx5_macsec_tables *rx_tables = &macsec_fs->rx_fs->tables;
	struct mlx5_core_dev *mdev = macsec_fs->mdev;

	if (tx_tables->check_rule_counter)
		mlx5_fc_query(mdev, tx_tables->check_rule_counter,
			      &stats->macsec_tx_pkts, &stats->macsec_tx_bytes);

	if (tx_tables->check_miss_rule_counter)
		mlx5_fc_query(mdev, tx_tables->check_miss_rule_counter,
			      &stats->macsec_tx_pkts_drop, &stats->macsec_tx_bytes_drop);

	if (rx_tables->check_rule_counter)
		mlx5_fc_query(mdev, rx_tables->check_rule_counter,
			      &stats->macsec_rx_pkts, &stats->macsec_rx_bytes);

	if (rx_tables->check_miss_rule_counter)
		mlx5_fc_query(mdev, rx_tables->check_miss_rule_counter,
			      &stats->macsec_rx_pkts_drop, &stats->macsec_rx_bytes_drop);
}

struct mlx5_macsec_stats *mlx5_macsec_fs_get_stats(struct mlx5_macsec_fs *macsec_fs)
{
	if (!macsec_fs)
		return NULL;

	return &macsec_fs->stats;
}

u32 mlx5_macsec_fs_get_fs_id_from_hashtable(struct mlx5_macsec_fs *macsec_fs, sci_t *sci)
{
	struct mlx5_fs_id *mlx5_fs_id;
	u32 fs_id = 0;

	rcu_read_lock();
	mlx5_fs_id = rhashtable_lookup(&macsec_fs->sci_hash, sci, rhash_sci);
	if (mlx5_fs_id)
		fs_id = mlx5_fs_id->id;
	rcu_read_unlock();

	return fs_id;
}

union mlx5_macsec_rule *
mlx5_macsec_fs_add_rule(struct mlx5_macsec_fs *macsec_fs,
			const struct macsec_context *macsec_ctx,
			struct mlx5_macsec_rule_attrs *attrs,
			u32 *sa_fs_id)
{
	struct mlx5_macsec_event_data data = {.macsec_fs = macsec_fs,
					      .macdev = macsec_ctx->secy->netdev,
					      .is_tx =
					      (attrs->action == MLX5_ACCEL_MACSEC_ACTION_ENCRYPT)
	};
	union mlx5_macsec_rule *macsec_rule;
	u32 tx_new_fs_id;

	macsec_rule = (attrs->action == MLX5_ACCEL_MACSEC_ACTION_ENCRYPT) ?
		macsec_fs_tx_add_rule(macsec_fs, macsec_ctx, attrs, &tx_new_fs_id) :
		macsec_fs_rx_add_rule(macsec_fs, macsec_ctx, attrs, *sa_fs_id);

	data.fs_id = (data.is_tx) ? tx_new_fs_id : *sa_fs_id;
	if (macsec_rule)
		blocking_notifier_call_chain(&macsec_fs->mdev->macsec_nh,
					     MLX5_DRIVER_EVENT_MACSEC_SA_ADDED,
					     &data);

	return macsec_rule;
}

void mlx5_macsec_fs_del_rule(struct mlx5_macsec_fs *macsec_fs,
			     union mlx5_macsec_rule *macsec_rule,
			     int action, void *macdev, u32 sa_fs_id)
{
	struct mlx5_macsec_event_data data = {.macsec_fs = macsec_fs,
					      .macdev = macdev,
					      .is_tx = (action == MLX5_ACCEL_MACSEC_ACTION_ENCRYPT)
	};

	data.fs_id = (data.is_tx) ? macsec_rule->tx_rule.fs_id : sa_fs_id;
	blocking_notifier_call_chain(&macsec_fs->mdev->macsec_nh,
				     MLX5_DRIVER_EVENT_MACSEC_SA_DELETED,
				     &data);

	(action == MLX5_ACCEL_MACSEC_ACTION_ENCRYPT) ?
		macsec_fs_tx_del_rule(macsec_fs, &macsec_rule->tx_rule, macdev) :
		macsec_fs_rx_del_rule(macsec_fs, &macsec_rule->rx_rule, macdev, sa_fs_id);
}

static int mlx5_macsec_fs_add_roce_rule_rx(struct mlx5_macsec_fs *macsec_fs, u32 fs_id, u16 gid_idx,
					   const struct sockaddr *addr,
					   struct list_head *rx_rules_list)
{
	struct mlx5_macsec_rx *rx_fs = macsec_fs->rx_fs;
	struct mlx5_roce_macsec_rx_rule *rx_rule;
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *new_rule;
	struct mlx5_flow_spec *spec;
	int err = 0;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	rx_rule = kzalloc(sizeof(*rx_rule), GFP_KERNEL);
	if (!rx_rule) {
		err = -ENOMEM;
		goto out;
	}

	set_ipaddr_spec(addr, spec, true);

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST;
	dest.ft = rx_fs->roce.ft_macsec_op_check;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	new_rule = mlx5_add_flow_rules(rx_fs->roce.ft_ip_check, spec, &flow_act,
				       &dest, 1);
	if (IS_ERR(new_rule)) {
		err = PTR_ERR(new_rule);
		goto ip_rule_err;
	}
	rx_rule->ip = new_rule;

	memset(&flow_act, 0, sizeof(flow_act));
	memset(spec, 0, sizeof(*spec));

	spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS_2;
	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, misc_parameters_2.metadata_reg_c_5);
	MLX5_SET(fte_match_param, spec->match_value, misc_parameters_2.metadata_reg_c_5,
		 macsec_fs_set_rx_fs_id(fs_id));
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_ALLOW;
	new_rule = mlx5_add_flow_rules(rx_fs->roce.ft_macsec_op_check, spec, &flow_act,
				       NULL, 0);
	if (IS_ERR(new_rule)) {
		err = PTR_ERR(new_rule);
		goto op_rule_err;
	}
	rx_rule->op = new_rule;
	rx_rule->gid_idx = gid_idx;
	rx_rule->fs_id = fs_id;
	list_add_tail(&rx_rule->entry, rx_rules_list);

	goto out;

op_rule_err:
	mlx5_del_flow_rules(rx_rule->ip);
	rx_rule->ip = NULL;
ip_rule_err:
	kfree(rx_rule);
out:
	kvfree(spec);
	return err;
}

static int mlx5_macsec_fs_add_roce_rule_tx(struct mlx5_macsec_fs *macsec_fs, u32 fs_id, u16 gid_idx,
					   const struct sockaddr *addr,
					   struct list_head *tx_rules_list)
{
	u8 action[MLX5_UN_SZ_BYTES(set_add_copy_action_in_auto)] = {};
	struct mlx5_macsec_tx *tx_fs = macsec_fs->tx_fs;
	struct mlx5_core_dev *mdev = macsec_fs->mdev;
	struct mlx5_modify_hdr *modify_hdr = NULL;
	struct mlx5_roce_macsec_tx_rule *tx_rule;
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *new_rule;
	struct mlx5_flow_spec *spec;
	int err = 0;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	tx_rule = kzalloc(sizeof(*tx_rule), GFP_KERNEL);
	if (!tx_rule) {
		err = -ENOMEM;
		goto out;
	}

	set_ipaddr_spec(addr, spec, false);

	MLX5_SET(set_action_in, action, action_type, MLX5_ACTION_TYPE_SET);
	MLX5_SET(set_action_in, action, field, MLX5_ACTION_IN_FIELD_METADATA_REG_A);
	MLX5_SET(set_action_in, action, data, macsec_fs_set_tx_fs_id(fs_id));
	MLX5_SET(set_action_in, action, offset, 0);
	MLX5_SET(set_action_in, action, length, 32);

	modify_hdr = mlx5_modify_header_alloc(mdev, MLX5_FLOW_NAMESPACE_RDMA_TX_MACSEC,
					      1, action);
	if (IS_ERR(modify_hdr)) {
		err = PTR_ERR(modify_hdr);
		mlx5_core_err(mdev, "Fail to alloc ROCE MACsec set modify_header_id err=%d\n",
			      err);
		modify_hdr = NULL;
		goto modify_hdr_err;
	}
	tx_rule->meta_modhdr = modify_hdr;

	flow_act.modify_hdr = modify_hdr;
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST | MLX5_FLOW_CONTEXT_ACTION_MOD_HDR;

	dest.type = MLX5_FLOW_DESTINATION_TYPE_TABLE_TYPE;
	dest.ft = tx_fs->tables.ft_crypto.t;
	new_rule = mlx5_add_flow_rules(tx_fs->ft_rdma_tx, spec, &flow_act, &dest, 1);
	if (IS_ERR(new_rule)) {
		err = PTR_ERR(new_rule);
		mlx5_core_err(mdev, "Failed to add ROCE TX rule, err=%d\n", err);
		goto rule_err;
	}
	tx_rule->rule = new_rule;
	tx_rule->gid_idx = gid_idx;
	tx_rule->fs_id = fs_id;
	list_add_tail(&tx_rule->entry, tx_rules_list);

	goto out;

rule_err:
	mlx5_modify_header_dealloc(mdev, tx_rule->meta_modhdr);
modify_hdr_err:
	kfree(tx_rule);
out:
	kvfree(spec);
	return err;
}

void mlx5_macsec_del_roce_rule(u16 gid_idx, struct mlx5_macsec_fs *macsec_fs,
			       struct list_head *tx_rules_list, struct list_head *rx_rules_list)
{
	struct mlx5_roce_macsec_rx_rule *rx_rule, *next_rx;
	struct mlx5_roce_macsec_tx_rule *tx_rule, *next_tx;

	list_for_each_entry_safe(tx_rule, next_tx, tx_rules_list, entry) {
		if (tx_rule->gid_idx == gid_idx)
			macsec_fs_del_roce_rule_tx(macsec_fs->mdev, tx_rule);
	}

	list_for_each_entry_safe(rx_rule, next_rx, rx_rules_list, entry) {
		if (rx_rule->gid_idx == gid_idx)
			macsec_fs_del_roce_rule_rx(rx_rule);
	}
}
EXPORT_SYMBOL_GPL(mlx5_macsec_del_roce_rule);

int mlx5_macsec_add_roce_rule(void *macdev, const struct sockaddr *addr, u16 gid_idx,
			      struct list_head *tx_rules_list, struct list_head *rx_rules_list,
			      struct mlx5_macsec_fs *macsec_fs)
{
	struct mlx5_macsec_device *iter, *macsec_device = NULL;
	struct mlx5_core_dev *mdev = macsec_fs->mdev;
	struct mlx5_fs_id *fs_id_iter;
	unsigned long index = 0;
	int err;

	list_for_each_entry(iter, &macsec_fs->macsec_devices_list, macsec_devices_list_entry) {
		if (iter->macdev == macdev) {
			macsec_device = iter;
			break;
		}
	}

	if (!macsec_device)
		return 0;

	xa_for_each(&macsec_device->tx_id_xa, index, fs_id_iter) {
		err = mlx5_macsec_fs_add_roce_rule_tx(macsec_fs, fs_id_iter->id, gid_idx, addr,
						      tx_rules_list);
		if (err) {
			mlx5_core_err(mdev, "MACsec offload: Failed to add roce TX rule\n");
			goto out;
		}
	}

	index = 0;
	xa_for_each(&macsec_device->rx_id_xa, index, fs_id_iter) {
		err = mlx5_macsec_fs_add_roce_rule_rx(macsec_fs, fs_id_iter->id, gid_idx, addr,
						      rx_rules_list);
		if (err) {
			mlx5_core_err(mdev, "MACsec offload: Failed to add roce TX rule\n");
			goto out;
		}
	}

	return 0;
out:
	mlx5_macsec_del_roce_rule(gid_idx, macsec_fs, tx_rules_list, rx_rules_list);
	return err;
}
EXPORT_SYMBOL_GPL(mlx5_macsec_add_roce_rule);

void mlx5_macsec_add_roce_sa_rules(u32 fs_id, const struct sockaddr *addr, u16 gid_idx,
				   struct list_head *tx_rules_list,
				   struct list_head *rx_rules_list,
				   struct mlx5_macsec_fs *macsec_fs, bool is_tx)
{
	(is_tx) ?
		mlx5_macsec_fs_add_roce_rule_tx(macsec_fs, fs_id, gid_idx, addr,
						tx_rules_list) :
		mlx5_macsec_fs_add_roce_rule_rx(macsec_fs, fs_id, gid_idx, addr,
						rx_rules_list);
}
EXPORT_SYMBOL_GPL(mlx5_macsec_add_roce_sa_rules);

void mlx5_macsec_del_roce_sa_rules(u32 fs_id, struct mlx5_macsec_fs *macsec_fs,
				   struct list_head *tx_rules_list,
				   struct list_head *rx_rules_list, bool is_tx)
{
	(is_tx) ?
		macsec_fs_del_roce_rules_tx(macsec_fs, fs_id, tx_rules_list) :
		macsec_fs_del_roce_rules_rx(macsec_fs, fs_id, rx_rules_list);
}
EXPORT_SYMBOL_GPL(mlx5_macsec_del_roce_sa_rules);

void mlx5_macsec_fs_cleanup(struct mlx5_macsec_fs *macsec_fs)
{
	macsec_fs_rx_cleanup(macsec_fs);
	macsec_fs_tx_cleanup(macsec_fs);
	rhashtable_destroy(&macsec_fs->fs_id_hash);
	rhashtable_destroy(&macsec_fs->sci_hash);
	kfree(macsec_fs);
}

struct mlx5_macsec_fs *
mlx5_macsec_fs_init(struct mlx5_core_dev *mdev)
{
	struct mlx5_macsec_fs *macsec_fs;
	int err;

	macsec_fs = kzalloc(sizeof(*macsec_fs), GFP_KERNEL);
	if (!macsec_fs)
		return NULL;

	macsec_fs->mdev = mdev;

	err = rhashtable_init(&macsec_fs->sci_hash, &rhash_sci);
	if (err) {
		mlx5_core_err(mdev, "MACsec offload: Failed to init SCI hash table, err=%d\n",
			      err);
		goto err_hash;
	}

	err = rhashtable_init(&macsec_fs->fs_id_hash, &rhash_fs_id);
	if (err) {
		mlx5_core_err(mdev, "MACsec offload: Failed to init FS_ID hash table, err=%d\n",
			      err);
		goto sci_hash_cleanup;
	}

	err = macsec_fs_tx_init(macsec_fs);
	if (err) {
		mlx5_core_err(mdev, "MACsec offload: Failed to init tx_fs, err=%d\n", err);
		goto fs_id_hash_cleanup;
	}

	err = macsec_fs_rx_init(macsec_fs);
	if (err) {
		mlx5_core_err(mdev, "MACsec offload: Failed to init tx_fs, err=%d\n", err);
		goto tx_cleanup;
	}

	BLOCKING_INIT_NOTIFIER_HEAD(&mdev->macsec_nh);

	return macsec_fs;

tx_cleanup:
	macsec_fs_tx_cleanup(macsec_fs);
fs_id_hash_cleanup:
	rhashtable_destroy(&macsec_fs->fs_id_hash);
sci_hash_cleanup:
	rhashtable_destroy(&macsec_fs->sci_hash);
err_hash:
	kfree(macsec_fs);
	return NULL;
}
