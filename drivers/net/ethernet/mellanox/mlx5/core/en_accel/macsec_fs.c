// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include <net/macsec.h>
#include <linux/netdevice.h>
#include <linux/mlx5/qp.h>
#include "fs_core.h"
#include "en/fs.h"
#include "en_accel/macsec_fs.h"
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

#define MLX5_MACSEC_TAG_LEN 8 /* SecTAG length with ethertype and without the optional SCI */

#define MLX5_ETH_WQE_FT_META_MACSEC_MASK 0x3E

struct mlx5_sectag_header {
	__be16 ethertype;
	u8 tci_an;
	u8 sl;
	u32 pn;
	u8 sci[MACSEC_SCI_LEN]; /* optional */
}  __packed;

struct mlx5e_macsec_tx_rule {
	struct mlx5_flow_handle *rule;
	struct mlx5_pkt_reformat *pkt_reformat;
	u32 fs_id;
};

struct mlx5e_macsec_tx {
	struct mlx5e_flow_table ft_crypto;
	struct mlx5_flow_handle *crypto_miss_rule;
	struct mlx5_flow_handle *crypto_mke_rule;

	struct mlx5_flow_table *ft_check;
	struct mlx5_flow_group  *ft_check_group;
	struct mlx5_fc *check_miss_rule_counter;
	struct mlx5_flow_handle *check_miss_rule;
	struct mlx5_fc *check_rule_counter;
	struct mlx5_flow_handle *check_rule;

	struct ida tx_halloc;

	u32 refcnt;
};

struct mlx5e_macsec_fs {
	struct mlx5_core_dev *mdev;
	struct net_device *netdev;
	struct mlx5e_macsec_tx *tx_fs;
};

static void macsec_fs_tx_destroy(struct mlx5e_macsec_fs *macsec_fs)
{
	struct mlx5e_macsec_tx *tx_fs = macsec_fs->tx_fs;

	/* Tx check table */
	if (tx_fs->check_rule) {
		mlx5_del_flow_rules(tx_fs->check_rule);
		tx_fs->check_rule = NULL;
	}

	if (tx_fs->check_miss_rule) {
		mlx5_del_flow_rules(tx_fs->check_miss_rule);
		tx_fs->check_miss_rule = NULL;
	}

	if (tx_fs->ft_check_group) {
		mlx5_destroy_flow_group(tx_fs->ft_check_group);
		tx_fs->ft_check_group = NULL;
	}

	if (tx_fs->ft_check) {
		mlx5_destroy_flow_table(tx_fs->ft_check);
		tx_fs->ft_check = NULL;
	}

	/* Tx crypto table */
	if (tx_fs->crypto_mke_rule) {
		mlx5_del_flow_rules(tx_fs->crypto_mke_rule);
		tx_fs->crypto_mke_rule = NULL;
	}

	if (tx_fs->crypto_miss_rule) {
		mlx5_del_flow_rules(tx_fs->crypto_miss_rule);
		tx_fs->crypto_miss_rule = NULL;
	}

	mlx5e_destroy_flow_table(&tx_fs->ft_crypto);
}

static int macsec_fs_tx_create_crypto_table_groups(struct mlx5e_flow_table *ft)
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

static int macsec_fs_tx_create(struct mlx5e_macsec_fs *macsec_fs)
{
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	struct mlx5e_macsec_tx *tx_fs = macsec_fs->tx_fs;
	struct net_device *netdev = macsec_fs->netdev;
	struct mlx5_flow_table_attr ft_attr = {};
	struct mlx5_flow_destination dest = {};
	struct mlx5_flow_act flow_act = {};
	struct mlx5e_flow_table *ft_crypto;
	struct mlx5_flow_table *flow_table;
	struct mlx5_flow_group *flow_group;
	struct mlx5_flow_namespace *ns;
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	u32 *flow_group_in;
	int err = 0;

	ns = mlx5_get_flow_namespace(macsec_fs->mdev, MLX5_FLOW_NAMESPACE_EGRESS_MACSEC);
	if (!ns)
		return -ENOMEM;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;

	flow_group_in = kvzalloc(inlen, GFP_KERNEL);
	if (!flow_group_in)
		goto out_spec;

	ft_crypto = &tx_fs->ft_crypto;

	/* Tx crypto table  */
	ft_attr.flags = MLX5_FLOW_TABLE_TUNNEL_EN_REFORMAT;
	ft_attr.level = TX_CRYPTO_TABLE_LEVEL;
	ft_attr.max_fte = CRYPTO_NUM_MAXSEC_FTE;

	flow_table = mlx5_create_flow_table(ns, &ft_attr);
	if (IS_ERR(flow_table)) {
		err = PTR_ERR(flow_table);
		netdev_err(netdev, "Failed to create MACsec Tx crypto table err(%d)\n", err);
		goto out_flow_group;
	}
	ft_crypto->t = flow_table;

	/* Tx crypto table groups */
	err = macsec_fs_tx_create_crypto_table_groups(ft_crypto);
	if (err) {
		netdev_err(netdev,
			   "Failed to create default flow group for MACsec Tx crypto table err(%d)\n",
			   err);
		goto err;
	}

	/* Tx crypto table MKE rule - MKE packets shouldn't be offloaded */
	memset(&flow_act, 0, sizeof(flow_act));
	memset(spec, 0, sizeof(*spec));
	spec->match_criteria_enable = MLX5_MATCH_OUTER_HEADERS;

	MLX5_SET_TO_ONES(fte_match_param, spec->match_criteria, outer_headers.ethertype);
	MLX5_SET(fte_match_param, spec->match_value, outer_headers.ethertype, ETH_P_PAE);
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_ALLOW;

	rule = mlx5_add_flow_rules(ft_crypto->t, spec, &flow_act, NULL, 0);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		netdev_err(netdev, "Failed to add MACsec TX MKE rule, err=%d\n", err);
		goto err;
	}
	tx_fs->crypto_mke_rule = rule;

	/* Tx crypto table Default miss rule */
	memset(&flow_act, 0, sizeof(flow_act));
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_ALLOW;
	rule = mlx5_add_flow_rules(ft_crypto->t, NULL, &flow_act, NULL, 0);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		netdev_err(netdev, "Failed to add MACsec Tx table default miss rule %d\n", err);
		goto err;
	}
	tx_fs->crypto_miss_rule = rule;

	/* Tx check table */
	flow_table = macsec_fs_auto_group_table_create(ns, 0, TX_CHECK_TABLE_LEVEL,
						       TX_CHECK_TABLE_NUM_FTE);
	if (IS_ERR(flow_table)) {
		err = PTR_ERR(flow_table);
		netdev_err(netdev, "fail to create MACsec TX check table, err(%d)\n", err);
		goto err;
	}
	tx_fs->ft_check = flow_table;

	/* Tx check table Default miss group/rule */
	memset(flow_group_in, 0, inlen);
	MLX5_SET(create_flow_group_in, flow_group_in, start_flow_index, flow_table->max_fte - 1);
	MLX5_SET(create_flow_group_in, flow_group_in, end_flow_index, flow_table->max_fte - 1);
	flow_group = mlx5_create_flow_group(tx_fs->ft_check, flow_group_in);
	if (IS_ERR(flow_group)) {
		err = PTR_ERR(flow_group);
		netdev_err(netdev,
			   "Failed to create default flow group for MACsec Tx crypto table err(%d)\n",
			   err);
		goto err;
	}
	tx_fs->ft_check_group = flow_group;

	/* Tx check table default drop rule */
	memset(&dest, 0, sizeof(struct mlx5_flow_destination));
	memset(&flow_act, 0, sizeof(flow_act));
	dest.type = MLX5_FLOW_DESTINATION_TYPE_COUNTER;
	dest.counter_id = mlx5_fc_id(tx_fs->check_miss_rule_counter);
	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_DROP | MLX5_FLOW_CONTEXT_ACTION_COUNT;
	rule = mlx5_add_flow_rules(tx_fs->ft_check,  NULL, &flow_act, &dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		netdev_err(netdev, "Failed to added MACsec tx check drop rule, err(%d)\n", err);
		goto err;
	}
	tx_fs->check_miss_rule = rule;

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
	dest.counter_id = mlx5_fc_id(tx_fs->check_rule_counter);
	rule = mlx5_add_flow_rules(tx_fs->ft_check, spec, &flow_act, &dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		netdev_err(netdev, "Failed to add MACsec check rule, err=%d\n", err);
		goto err;
	}
	tx_fs->check_rule = rule;

	goto out_flow_group;

err:
	macsec_fs_tx_destroy(macsec_fs);
out_flow_group:
	kvfree(flow_group_in);
out_spec:
	kvfree(spec);
	return err;
}

static int macsec_fs_tx_ft_get(struct mlx5e_macsec_fs *macsec_fs)
{
	struct mlx5e_macsec_tx *tx_fs = macsec_fs->tx_fs;
	int err = 0;

	if (tx_fs->refcnt)
		goto out;

	err = macsec_fs_tx_create(macsec_fs);
	if (err)
		return err;

out:
	tx_fs->refcnt++;
	return err;
}

static void macsec_fs_tx_ft_put(struct mlx5e_macsec_fs *macsec_fs)
{
	struct mlx5e_macsec_tx *tx_fs = macsec_fs->tx_fs;

	if (--tx_fs->refcnt)
		return;

	macsec_fs_tx_destroy(macsec_fs);
}

static int macsec_fs_tx_setup_fte(struct mlx5e_macsec_fs *macsec_fs,
				  struct mlx5_flow_spec *spec,
				  struct mlx5_flow_act *flow_act,
				  u32 macsec_obj_id,
				  u32 *fs_id)
{
	struct mlx5e_macsec_tx *tx_fs = macsec_fs->tx_fs;
	int err = 0;
	u32 id;

	err = ida_alloc_range(&tx_fs->tx_halloc, 1, MLX5_MACSEC_NUM_OF_SUPPORTED_INTERFACES,
			      GFP_KERNEL);
	if (err < 0)
		return err;

	id = err;
	spec->match_criteria_enable |= MLX5_MATCH_MISC_PARAMETERS_2;

	/* Metadata match */
	MLX5_SET(fte_match_param, spec->match_criteria, misc_parameters_2.metadata_reg_a,
		 MLX5_ETH_WQE_FT_META_MACSEC_MASK);
	MLX5_SET(fte_match_param, spec->match_value, misc_parameters_2.metadata_reg_a,
		 MLX5_ETH_WQE_FT_META_MACSEC | id << 2);

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

static void macsec_fs_tx_del_rule(struct mlx5e_macsec_fs *macsec_fs,
				  struct mlx5e_macsec_tx_rule *tx_rule)
{
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

static struct mlx5e_macsec_tx_rule *
macsec_fs_tx_add_rule(struct mlx5e_macsec_fs *macsec_fs,
		      const struct macsec_context *macsec_ctx,
		      struct mlx5_macsec_rule_attrs *attrs,
		      u32 *sa_fs_id)
{
	char reformatbf[MLX5_MACSEC_TAG_LEN + MACSEC_SCI_LEN];
	struct mlx5_pkt_reformat_params reformat_params = {};
	struct mlx5e_macsec_tx *tx_fs = macsec_fs->tx_fs;
	struct net_device *netdev = macsec_fs->netdev;
	struct mlx5_flow_destination dest = {};
	struct mlx5e_macsec_tx_rule *tx_rule;
	struct mlx5_flow_act flow_act = {};
	struct mlx5_flow_handle *rule;
	struct mlx5_flow_spec *spec;
	size_t reformat_size;
	int err = 0;
	u32 fs_id;

	spec = kvzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return NULL;

	err = macsec_fs_tx_ft_get(macsec_fs);
	if (err)
		goto out_spec;

	tx_rule = kzalloc(sizeof(*tx_rule), GFP_KERNEL);
	if (!tx_rule) {
		macsec_fs_tx_ft_put(macsec_fs);
		goto out_spec;
	}

	/* Tx crypto table crypto rule */
	macsec_fs_tx_create_sectag_header(macsec_ctx, reformatbf, &reformat_size);

	reformat_params.type = MLX5_REFORMAT_TYPE_ADD_MACSEC;
	reformat_params.size = reformat_size;
	reformat_params.data = reformatbf;
	flow_act.pkt_reformat = mlx5_packet_reformat_alloc(macsec_fs->mdev,
							   &reformat_params,
							   MLX5_FLOW_NAMESPACE_EGRESS_MACSEC);
	if (IS_ERR(flow_act.pkt_reformat)) {
		err = PTR_ERR(flow_act.pkt_reformat);
		netdev_err(netdev, "Failed to allocate MACsec Tx reformat context err=%d\n",  err);
		goto err;
	}
	tx_rule->pkt_reformat = flow_act.pkt_reformat;

	err = macsec_fs_tx_setup_fte(macsec_fs, spec, &flow_act, attrs->macsec_obj_id, &fs_id);
	if (err) {
		netdev_err(netdev,
			   "Failed to add packet reformat for MACsec TX crypto rule, err=%d\n",
			   err);
		goto err;
	}

	tx_rule->fs_id = fs_id;
	*sa_fs_id = fs_id;

	flow_act.action = MLX5_FLOW_CONTEXT_ACTION_FWD_DEST |
			  MLX5_FLOW_CONTEXT_ACTION_CRYPTO_ENCRYPT |
			  MLX5_FLOW_CONTEXT_ACTION_PACKET_REFORMAT;
	dest.type = MLX5_FLOW_DESTINATION_TYPE_FLOW_TABLE;
	dest.ft = tx_fs->ft_check;
	rule = mlx5_add_flow_rules(tx_fs->ft_crypto.t, spec, &flow_act, &dest, 1);
	if (IS_ERR(rule)) {
		err = PTR_ERR(rule);
		netdev_err(netdev, "Failed to add MACsec TX crypto rule, err=%d\n", err);
		goto err;
	}
	tx_rule->rule = rule;

	goto out_spec;

err:
	macsec_fs_tx_del_rule(macsec_fs, tx_rule);
	tx_rule = NULL;
out_spec:
	kvfree(spec);

	return tx_rule;
}

static void macsec_fs_tx_cleanup(struct mlx5e_macsec_fs *macsec_fs)
{
	struct mlx5e_macsec_tx *tx_fs = macsec_fs->tx_fs;
	struct mlx5_core_dev *mdev = macsec_fs->mdev;

	if (!tx_fs)
		return;

	if (tx_fs->refcnt) {
		netdev_err(macsec_fs->netdev,
			   "Can't destroy MACsec offload tx_fs, refcnt(%u) isn't 0\n",
			   tx_fs->refcnt);
		return;
	}

	ida_destroy(&tx_fs->tx_halloc);

	if (tx_fs->check_miss_rule_counter) {
		mlx5_fc_destroy(mdev, tx_fs->check_miss_rule_counter);
		tx_fs->check_miss_rule_counter = NULL;
	}

	if (tx_fs->check_rule_counter) {
		mlx5_fc_destroy(mdev, tx_fs->check_rule_counter);
		tx_fs->check_rule_counter = NULL;
	}

	kfree(tx_fs);
	macsec_fs->tx_fs = NULL;
}

static int macsec_fs_tx_init(struct mlx5e_macsec_fs *macsec_fs)
{
	struct net_device *netdev = macsec_fs->netdev;
	struct mlx5_core_dev *mdev = macsec_fs->mdev;
	struct mlx5e_macsec_tx *tx_fs;
	struct mlx5_fc *flow_counter;
	int err;

	tx_fs = kzalloc(sizeof(*tx_fs), GFP_KERNEL);
	if (!tx_fs)
		return -ENOMEM;

	flow_counter = mlx5_fc_create(mdev, false);
	if (IS_ERR(flow_counter)) {
		err = PTR_ERR(flow_counter);
		netdev_err(netdev,
			   "Failed to create MACsec Tx encrypt flow counter, err(%d)\n",
			   err);
		goto err_encrypt_counter;
	}
	tx_fs->check_rule_counter = flow_counter;

	flow_counter = mlx5_fc_create(mdev, false);
	if (IS_ERR(flow_counter)) {
		err = PTR_ERR(flow_counter);
		netdev_err(netdev,
			   "Failed to create MACsec Tx drop flow counter, err(%d)\n",
			   err);
		goto err_drop_counter;
	}
	tx_fs->check_miss_rule_counter = flow_counter;

	ida_init(&tx_fs->tx_halloc);

	macsec_fs->tx_fs = tx_fs;

	return 0;

err_drop_counter:
	mlx5_fc_destroy(mdev, tx_fs->check_rule_counter);
	tx_fs->check_rule_counter = NULL;

err_encrypt_counter:
	kfree(tx_fs);
	macsec_fs->tx_fs = NULL;

	return err;
}

struct mlx5e_macsec_tx_rule *
mlx5e_macsec_fs_add_rule(struct mlx5e_macsec_fs *macsec_fs,
			 const struct macsec_context *macsec_ctx,
			 struct mlx5_macsec_rule_attrs *attrs,
			 u32 *sa_fs_id)
{
	if (attrs->action == MLX5_ACCEL_MACSEC_ACTION_ENCRYPT)
		return macsec_fs_tx_add_rule(macsec_fs, macsec_ctx, attrs, sa_fs_id);

	return NULL;
}

void mlx5e_macsec_fs_del_rule(struct mlx5e_macsec_fs *macsec_fs,
			      struct mlx5e_macsec_tx_rule *tx_rule,
			      int action)
{
	if (action == MLX5_ACCEL_MACSEC_ACTION_ENCRYPT)
		macsec_fs_tx_del_rule(macsec_fs, tx_rule);
}

void mlx5e_macsec_fs_cleanup(struct mlx5e_macsec_fs *macsec_fs)
{
	macsec_fs_tx_cleanup(macsec_fs);
	kfree(macsec_fs);
}

struct mlx5e_macsec_fs *
mlx5e_macsec_fs_init(struct mlx5_core_dev *mdev,
		     struct net_device *netdev)
{
	struct mlx5e_macsec_fs *macsec_fs;
	int err;

	macsec_fs = kzalloc(sizeof(*macsec_fs), GFP_KERNEL);
	if (!macsec_fs)
		return NULL;

	macsec_fs->mdev = mdev;
	macsec_fs->netdev = netdev;

	err = macsec_fs_tx_init(macsec_fs);
	if (err) {
		netdev_err(netdev, "MACsec offload: Failed to init tx_fs, err=%d\n", err);
		goto err;
	}

	return macsec_fs;

err:
	kfree(macsec_fs);
	return NULL;
}
