// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 * Copyright (C) 2019-2021 Intel Corporation
 */
#include <linux/kernel.h>
#include <linux/bsearch.h>

#include "fw/api/tx.h"
#include "iwl-trans.h"
#include "iwl-drv.h"
#include "iwl-fh.h"
#include "queue/tx.h"
#include <linux/dmapool.h>
#include "fw/api/commands.h"

struct iwl_trans *iwl_trans_alloc(unsigned int priv_size,
				  struct device *dev,
				  const struct iwl_trans_ops *ops,
				  const struct iwl_cfg_trans_params *cfg_trans)
{
	struct iwl_trans *trans;
#ifdef CONFIG_LOCKDEP
	static struct lock_class_key __key;
#endif

	trans = devm_kzalloc(dev, sizeof(*trans) + priv_size, GFP_KERNEL);
	if (!trans)
		return NULL;

	trans->trans_cfg = cfg_trans;

#ifdef CONFIG_LOCKDEP
	lockdep_init_map(&trans->sync_cmd_lockdep_map, "sync_cmd_lockdep_map",
			 &__key, 0);
#endif

	trans->dev = dev;
	trans->ops = ops;
	trans->num_rx_queues = 1;

	WARN_ON(!ops->wait_txq_empty && !ops->wait_tx_queues_empty);

	if (trans->trans_cfg->use_tfh) {
		trans->txqs.tfd.addr_size = 64;
		trans->txqs.tfd.max_tbs = IWL_TFH_NUM_TBS;
		trans->txqs.tfd.size = sizeof(struct iwl_tfh_tfd);
	} else {
		trans->txqs.tfd.addr_size = 36;
		trans->txqs.tfd.max_tbs = IWL_NUM_OF_TBS;
		trans->txqs.tfd.size = sizeof(struct iwl_tfd);
	}
	trans->max_skb_frags = IWL_TRANS_MAX_FRAGS(trans);

	return trans;
}

int iwl_trans_init(struct iwl_trans *trans)
{
	int txcmd_size, txcmd_align;

	if (!trans->trans_cfg->gen2) {
		txcmd_size = sizeof(struct iwl_tx_cmd);
		txcmd_align = sizeof(void *);
	} else if (trans->trans_cfg->device_family < IWL_DEVICE_FAMILY_AX210) {
		txcmd_size = sizeof(struct iwl_tx_cmd_gen2);
		txcmd_align = 64;
	} else {
		txcmd_size = sizeof(struct iwl_tx_cmd_gen3);
		txcmd_align = 128;
	}

	txcmd_size += sizeof(struct iwl_cmd_header);
	txcmd_size += 36; /* biggest possible 802.11 header */

	/* Ensure device TX cmd cannot reach/cross a page boundary in gen2 */
	if (WARN_ON(trans->trans_cfg->gen2 && txcmd_size >= txcmd_align))
		return -EINVAL;

	if (trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_BZ)
		trans->txqs.bc_tbl_size =
			sizeof(struct iwl_gen3_bc_tbl_entry) * TFD_QUEUE_BC_SIZE_GEN3_BZ;
	else if (trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_AX210)
		trans->txqs.bc_tbl_size =
			sizeof(struct iwl_gen3_bc_tbl_entry) * TFD_QUEUE_BC_SIZE_GEN3_AX210;
	else
		trans->txqs.bc_tbl_size = sizeof(struct iwlagn_scd_bc_tbl);
	/*
	 * For gen2 devices, we use a single allocation for each byte-count
	 * table, but they're pretty small (1k) so use a DMA pool that we
	 * allocate here.
	 */
	if (trans->trans_cfg->gen2) {
		trans->txqs.bc_pool = dmam_pool_create("iwlwifi:bc", trans->dev,
						       trans->txqs.bc_tbl_size,
						       256, 0);
		if (!trans->txqs.bc_pool)
			return -ENOMEM;
	}

	/* Some things must not change even if the config does */
	WARN_ON(trans->txqs.tfd.addr_size !=
		(trans->trans_cfg->use_tfh ? 64 : 36));

	snprintf(trans->dev_cmd_pool_name, sizeof(trans->dev_cmd_pool_name),
		 "iwl_cmd_pool:%s", dev_name(trans->dev));
	trans->dev_cmd_pool =
		kmem_cache_create(trans->dev_cmd_pool_name,
				  txcmd_size, txcmd_align,
				  SLAB_HWCACHE_ALIGN, NULL);
	if (!trans->dev_cmd_pool)
		return -ENOMEM;

	trans->txqs.tso_hdr_page = alloc_percpu(struct iwl_tso_hdr_page);
	if (!trans->txqs.tso_hdr_page) {
		kmem_cache_destroy(trans->dev_cmd_pool);
		return -ENOMEM;
	}

	/* Initialize the wait queue for commands */
	init_waitqueue_head(&trans->wait_command_queue);

	return 0;
}

void iwl_trans_free(struct iwl_trans *trans)
{
	int i;

	if (trans->txqs.tso_hdr_page) {
		for_each_possible_cpu(i) {
			struct iwl_tso_hdr_page *p =
				per_cpu_ptr(trans->txqs.tso_hdr_page, i);

			if (p && p->page)
				__free_page(p->page);
		}

		free_percpu(trans->txqs.tso_hdr_page);
	}

	kmem_cache_destroy(trans->dev_cmd_pool);
}

int iwl_trans_send_cmd(struct iwl_trans *trans, struct iwl_host_cmd *cmd)
{
	int ret;

	if (unlikely(!(cmd->flags & CMD_SEND_IN_RFKILL) &&
		     test_bit(STATUS_RFKILL_OPMODE, &trans->status)))
		return -ERFKILL;

	/*
	 * We can't test IWL_MVM_STATUS_IN_D3 in mvm->status because this
	 * bit is set early in the D3 flow, before we send all the commands
	 * that configure the firmware for D3 operation (power, patterns, ...)
	 * and we don't want to flag all those with CMD_SEND_IN_D3.
	 * So use the system_pm_mode instead. The only command sent after
	 * we set system_pm_mode is D3_CONFIG_CMD, which we now flag with
	 * CMD_SEND_IN_D3.
	 */
	if (unlikely(trans->system_pm_mode == IWL_PLAT_PM_MODE_D3 &&
		     !(cmd->flags & CMD_SEND_IN_D3)))
		return -EHOSTDOWN;

	if (unlikely(test_bit(STATUS_FW_ERROR, &trans->status)))
		return -EIO;

	if (unlikely(trans->state != IWL_TRANS_FW_ALIVE)) {
		IWL_ERR(trans, "%s bad state = %d\n", __func__, trans->state);
		return -EIO;
	}

	if (WARN_ON((cmd->flags & CMD_WANT_ASYNC_CALLBACK) &&
		    !(cmd->flags & CMD_ASYNC)))
		return -EINVAL;

	if (!(cmd->flags & CMD_ASYNC))
		lock_map_acquire_read(&trans->sync_cmd_lockdep_map);

	if (trans->wide_cmd_header && !iwl_cmd_groupid(cmd->id)) {
		if (cmd->id != REPLY_ERROR)
			cmd->id = DEF_ID(cmd->id);
	}

	ret = iwl_trans_txq_send_hcmd(trans, cmd);

	if (!(cmd->flags & CMD_ASYNC))
		lock_map_release(&trans->sync_cmd_lockdep_map);

	if (WARN_ON((cmd->flags & CMD_WANT_SKB) && !ret && !cmd->resp_pkt))
		return -EIO;

	return ret;
}
IWL_EXPORT_SYMBOL(iwl_trans_send_cmd);

/* Comparator for struct iwl_hcmd_names.
 * Used in the binary search over a list of host commands.
 *
 * @key: command_id that we're looking for.
 * @elt: struct iwl_hcmd_names candidate for match.
 *
 * @return 0 iff equal.
 */
static int iwl_hcmd_names_cmp(const void *key, const void *elt)
{
	const struct iwl_hcmd_names *name = elt;
	const u8 *cmd1 = key;
	u8 cmd2 = name->cmd_id;

	return (*cmd1 - cmd2);
}

const char *iwl_get_cmd_string(struct iwl_trans *trans, u32 id)
{
	u8 grp, cmd;
	struct iwl_hcmd_names *ret;
	const struct iwl_hcmd_arr *arr;
	size_t size = sizeof(struct iwl_hcmd_names);

	grp = iwl_cmd_groupid(id);
	cmd = iwl_cmd_opcode(id);

	if (!trans->command_groups || grp >= trans->command_groups_size ||
	    !trans->command_groups[grp].arr)
		return "UNKNOWN";

	arr = &trans->command_groups[grp];
	ret = bsearch(&cmd, arr->arr, arr->size, size, iwl_hcmd_names_cmp);
	if (!ret)
		return "UNKNOWN";
	return ret->cmd_name;
}
IWL_EXPORT_SYMBOL(iwl_get_cmd_string);

int iwl_cmd_groups_verify_sorted(const struct iwl_trans_config *trans)
{
	int i, j;
	const struct iwl_hcmd_arr *arr;

	for (i = 0; i < trans->command_groups_size; i++) {
		arr = &trans->command_groups[i];
		if (!arr->arr)
			continue;
		for (j = 0; j < arr->size - 1; j++)
			if (arr->arr[j].cmd_id > arr->arr[j + 1].cmd_id)
				return -1;
	}
	return 0;
}
IWL_EXPORT_SYMBOL(iwl_cmd_groups_verify_sorted);
