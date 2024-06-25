// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 * Copyright (C) 2019-2021, 2023-2024 Intel Corporation
 */
#include <linux/kernel.h>
#include <linux/bsearch.h>

#include "fw/api/tx.h"
#include "iwl-trans.h"
#include "iwl-drv.h"
#include "iwl-fh.h"
#include <linux/dmapool.h>
#include "fw/api/commands.h"
#include "pcie/internal.h"
#include "iwl-context-info-gen3.h"

struct iwl_trans *iwl_trans_alloc(unsigned int priv_size,
				  struct device *dev,
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
	trans->num_rx_queues = 1;

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

	snprintf(trans->dev_cmd_pool_name, sizeof(trans->dev_cmd_pool_name),
		 "iwl_cmd_pool:%s", dev_name(trans->dev));
	trans->dev_cmd_pool =
		kmem_cache_create(trans->dev_cmd_pool_name,
				  txcmd_size, txcmd_align,
				  SLAB_HWCACHE_ALIGN, NULL);
	if (!trans->dev_cmd_pool)
		return -ENOMEM;

	/* Initialize the wait queue for commands */
	init_waitqueue_head(&trans->wait_command_queue);

	return 0;
}

void iwl_trans_free(struct iwl_trans *trans)
{
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

	if (WARN_ONCE(trans->state != IWL_TRANS_FW_ALIVE,
		      "bad state = %d\n", trans->state))
		return -EIO;

	if (!(cmd->flags & CMD_ASYNC))
		lock_map_acquire_read(&trans->sync_cmd_lockdep_map);

	if (trans->wide_cmd_header && !iwl_cmd_groupid(cmd->id)) {
		if (cmd->id != REPLY_ERROR)
			cmd->id = DEF_ID(cmd->id);
	}

	ret = iwl_trans_pcie_send_hcmd(trans, cmd);

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

void iwl_trans_configure(struct iwl_trans *trans,
			 const struct iwl_trans_config *trans_cfg)
{
	trans->op_mode = trans_cfg->op_mode;

	iwl_trans_pcie_configure(trans, trans_cfg);
	WARN_ON(iwl_cmd_groups_verify_sorted(trans_cfg));
}
IWL_EXPORT_SYMBOL(iwl_trans_configure);

int iwl_trans_start_hw(struct iwl_trans *trans)
{
	might_sleep();

	return iwl_trans_pcie_start_hw(trans);
}
IWL_EXPORT_SYMBOL(iwl_trans_start_hw);

void iwl_trans_op_mode_leave(struct iwl_trans *trans)
{
	might_sleep();

	iwl_trans_pcie_op_mode_leave(trans);

	trans->op_mode = NULL;

	trans->state = IWL_TRANS_NO_FW;
}
IWL_EXPORT_SYMBOL(iwl_trans_op_mode_leave);

void iwl_trans_write8(struct iwl_trans *trans, u32 ofs, u8 val)
{
	iwl_trans_pcie_write8(trans, ofs, val);
}
IWL_EXPORT_SYMBOL(iwl_trans_write8);

void iwl_trans_write32(struct iwl_trans *trans, u32 ofs, u32 val)
{
	iwl_trans_pcie_write32(trans, ofs, val);
}
IWL_EXPORT_SYMBOL(iwl_trans_write32);

u32 iwl_trans_read32(struct iwl_trans *trans, u32 ofs)
{
	return iwl_trans_pcie_read32(trans, ofs);
}
IWL_EXPORT_SYMBOL(iwl_trans_read32);

u32 iwl_trans_read_prph(struct iwl_trans *trans, u32 ofs)
{
	return iwl_trans_pcie_read_prph(trans, ofs);
}
IWL_EXPORT_SYMBOL(iwl_trans_read_prph);

void iwl_trans_write_prph(struct iwl_trans *trans, u32 ofs, u32 val)
{
	return iwl_trans_pcie_write_prph(trans, ofs, val);
}
IWL_EXPORT_SYMBOL(iwl_trans_write_prph);

int iwl_trans_read_mem(struct iwl_trans *trans, u32 addr,
		       void *buf, int dwords)
{
	return iwl_trans_pcie_read_mem(trans, addr, buf, dwords);
}
IWL_EXPORT_SYMBOL(iwl_trans_read_mem);

int iwl_trans_write_mem(struct iwl_trans *trans, u32 addr,
			const void *buf, int dwords)
{
	return iwl_trans_pcie_write_mem(trans, addr, buf, dwords);
}
IWL_EXPORT_SYMBOL(iwl_trans_write_mem);

void iwl_trans_set_pmi(struct iwl_trans *trans, bool state)
{
	if (state)
		set_bit(STATUS_TPOWER_PMI, &trans->status);
	else
		clear_bit(STATUS_TPOWER_PMI, &trans->status);
}
IWL_EXPORT_SYMBOL(iwl_trans_set_pmi);

int iwl_trans_sw_reset(struct iwl_trans *trans, bool retake_ownership)
{
	return iwl_trans_pcie_sw_reset(trans, retake_ownership);
}
IWL_EXPORT_SYMBOL(iwl_trans_sw_reset);

struct iwl_trans_dump_data *
iwl_trans_dump_data(struct iwl_trans *trans, u32 dump_mask,
		    const struct iwl_dump_sanitize_ops *sanitize_ops,
		    void *sanitize_ctx)
{
	return iwl_trans_pcie_dump_data(trans, dump_mask,
					sanitize_ops, sanitize_ctx);
}
IWL_EXPORT_SYMBOL(iwl_trans_dump_data);

int iwl_trans_d3_suspend(struct iwl_trans *trans, bool test, bool reset)
{
	might_sleep();

	return iwl_trans_pcie_d3_suspend(trans, test, reset);
}
IWL_EXPORT_SYMBOL(iwl_trans_d3_suspend);

int iwl_trans_d3_resume(struct iwl_trans *trans, enum iwl_d3_status *status,
			bool test, bool reset)
{
	might_sleep();

	return iwl_trans_pcie_d3_resume(trans, status, test, reset);
}
IWL_EXPORT_SYMBOL(iwl_trans_d3_resume);

void iwl_trans_interrupts(struct iwl_trans *trans, bool enable)
{
	iwl_trans_pci_interrupts(trans, enable);
}
IWL_EXPORT_SYMBOL(iwl_trans_interrupts);

void iwl_trans_sync_nmi(struct iwl_trans *trans)
{
	iwl_trans_pcie_sync_nmi(trans);
}
IWL_EXPORT_SYMBOL(iwl_trans_sync_nmi);

int iwl_trans_write_imr_mem(struct iwl_trans *trans, u32 dst_addr,
			    u64 src_addr, u32 byte_cnt)
{
	return iwl_trans_pcie_copy_imr(trans, dst_addr, src_addr, byte_cnt);
}
IWL_EXPORT_SYMBOL(iwl_trans_write_imr_mem);

void iwl_trans_set_bits_mask(struct iwl_trans *trans, u32 reg,
			     u32 mask, u32 value)
{
	iwl_trans_pcie_set_bits_mask(trans, reg, mask, value);
}
IWL_EXPORT_SYMBOL(iwl_trans_set_bits_mask);

int iwl_trans_read_config32(struct iwl_trans *trans, u32 ofs,
			    u32 *val)
{
	return iwl_trans_pcie_read_config32(trans, ofs, val);
}
IWL_EXPORT_SYMBOL(iwl_trans_read_config32);

bool _iwl_trans_grab_nic_access(struct iwl_trans *trans)
{
	return iwl_trans_pcie_grab_nic_access(trans);
}
IWL_EXPORT_SYMBOL(_iwl_trans_grab_nic_access);

void __releases(nic_access)
iwl_trans_release_nic_access(struct iwl_trans *trans)
{
	iwl_trans_pcie_release_nic_access(trans);
	__release(nic_access);
}
IWL_EXPORT_SYMBOL(iwl_trans_release_nic_access);

void iwl_trans_fw_alive(struct iwl_trans *trans, u32 scd_addr)
{
	might_sleep();

	trans->state = IWL_TRANS_FW_ALIVE;

	if (trans->trans_cfg->gen2)
		iwl_trans_pcie_gen2_fw_alive(trans);
	else
		iwl_trans_pcie_fw_alive(trans, scd_addr);
}
IWL_EXPORT_SYMBOL(iwl_trans_fw_alive);

int iwl_trans_start_fw(struct iwl_trans *trans, const struct fw_img *fw,
		       bool run_in_rfkill)
{
	int ret;

	might_sleep();

	WARN_ON_ONCE(!trans->rx_mpdu_cmd);

	clear_bit(STATUS_FW_ERROR, &trans->status);

	if (trans->trans_cfg->gen2)
		ret = iwl_trans_pcie_gen2_start_fw(trans, fw, run_in_rfkill);
	else
		ret = iwl_trans_pcie_start_fw(trans, fw, run_in_rfkill);

	if (ret == 0)
		trans->state = IWL_TRANS_FW_STARTED;

	return ret;
}
IWL_EXPORT_SYMBOL(iwl_trans_start_fw);

void iwl_trans_stop_device(struct iwl_trans *trans)
{
	might_sleep();

	if (trans->trans_cfg->gen2)
		iwl_trans_pcie_gen2_stop_device(trans);
	else
		iwl_trans_pcie_stop_device(trans);

	trans->state = IWL_TRANS_NO_FW;
}
IWL_EXPORT_SYMBOL(iwl_trans_stop_device);

int iwl_trans_tx(struct iwl_trans *trans, struct sk_buff *skb,
		 struct iwl_device_tx_cmd *dev_cmd, int queue)
{
	if (unlikely(test_bit(STATUS_FW_ERROR, &trans->status)))
		return -EIO;

	if (WARN_ONCE(trans->state != IWL_TRANS_FW_ALIVE,
		      "bad state = %d\n", trans->state))
		return -EIO;

	if (trans->trans_cfg->gen2)
		return iwl_txq_gen2_tx(trans, skb, dev_cmd, queue);

	return iwl_trans_pcie_tx(trans, skb, dev_cmd, queue);
}
IWL_EXPORT_SYMBOL(iwl_trans_tx);

void iwl_trans_reclaim(struct iwl_trans *trans, int queue, int ssn,
		       struct sk_buff_head *skbs, bool is_flush)
{
	if (WARN_ONCE(trans->state != IWL_TRANS_FW_ALIVE,
		      "bad state = %d\n", trans->state))
		return;

	iwl_pcie_reclaim(trans, queue, ssn, skbs, is_flush);
}
IWL_EXPORT_SYMBOL(iwl_trans_reclaim);

void iwl_trans_txq_disable(struct iwl_trans *trans, int queue,
			   bool configure_scd)
{
	iwl_trans_pcie_txq_disable(trans, queue, configure_scd);
}
IWL_EXPORT_SYMBOL(iwl_trans_txq_disable);

bool iwl_trans_txq_enable_cfg(struct iwl_trans *trans, int queue, u16 ssn,
			      const struct iwl_trans_txq_scd_cfg *cfg,
			      unsigned int queue_wdg_timeout)
{
	might_sleep();

	if (WARN_ONCE(trans->state != IWL_TRANS_FW_ALIVE,
		      "bad state = %d\n", trans->state))
		return false;

	return iwl_trans_pcie_txq_enable(trans, queue, ssn,
					 cfg, queue_wdg_timeout);
}
IWL_EXPORT_SYMBOL(iwl_trans_txq_enable_cfg);

int iwl_trans_wait_txq_empty(struct iwl_trans *trans, int queue)
{
	if (WARN_ONCE(trans->state != IWL_TRANS_FW_ALIVE,
		      "bad state = %d\n", trans->state))
		return -EIO;

	return iwl_trans_pcie_wait_txq_empty(trans, queue);
}
IWL_EXPORT_SYMBOL(iwl_trans_wait_txq_empty);

int iwl_trans_wait_tx_queues_empty(struct iwl_trans *trans, u32 txqs)
{
	if (WARN_ONCE(trans->state != IWL_TRANS_FW_ALIVE,
		      "bad state = %d\n", trans->state))
		return -EIO;

	return iwl_trans_pcie_wait_txqs_empty(trans, txqs);
}
IWL_EXPORT_SYMBOL(iwl_trans_wait_tx_queues_empty);

void iwl_trans_freeze_txq_timer(struct iwl_trans *trans,
				unsigned long txqs, bool freeze)
{
	if (WARN_ONCE(trans->state != IWL_TRANS_FW_ALIVE,
		      "bad state = %d\n", trans->state))
		return;

	iwl_pcie_freeze_txq_timer(trans, txqs, freeze);
}
IWL_EXPORT_SYMBOL(iwl_trans_freeze_txq_timer);

void iwl_trans_txq_set_shared_mode(struct iwl_trans *trans,
				   int txq_id, bool shared_mode)
{
	iwl_trans_pcie_txq_set_shared_mode(trans, txq_id, shared_mode);
}
IWL_EXPORT_SYMBOL(iwl_trans_txq_set_shared_mode);

#ifdef CONFIG_IWLWIFI_DEBUGFS
void iwl_trans_debugfs_cleanup(struct iwl_trans *trans)
{
	iwl_trans_pcie_debugfs_cleanup(trans);
}
IWL_EXPORT_SYMBOL(iwl_trans_debugfs_cleanup);
#endif

void iwl_trans_set_q_ptrs(struct iwl_trans *trans, int queue, int ptr)
{
	if (WARN_ONCE(trans->state != IWL_TRANS_FW_ALIVE,
		      "bad state = %d\n", trans->state))
		return;

	iwl_pcie_set_q_ptrs(trans, queue, ptr);
}
IWL_EXPORT_SYMBOL(iwl_trans_set_q_ptrs);

int iwl_trans_txq_alloc(struct iwl_trans *trans, u32 flags, u32 sta_mask,
			u8 tid, int size, unsigned int wdg_timeout)
{
	might_sleep();

	if (WARN_ONCE(trans->state != IWL_TRANS_FW_ALIVE,
		      "bad state = %d\n", trans->state))
		return -EIO;

	return iwl_txq_dyn_alloc(trans, flags, sta_mask, tid,
				 size, wdg_timeout);
}
IWL_EXPORT_SYMBOL(iwl_trans_txq_alloc);

void iwl_trans_txq_free(struct iwl_trans *trans, int queue)
{
	iwl_txq_dyn_free(trans, queue);
}
IWL_EXPORT_SYMBOL(iwl_trans_txq_free);

int iwl_trans_get_rxq_dma_data(struct iwl_trans *trans, int queue,
			       struct iwl_trans_rxq_dma_data *data)
{
	return iwl_trans_pcie_rxq_dma_data(trans, queue, data);
}
IWL_EXPORT_SYMBOL(iwl_trans_get_rxq_dma_data);

int iwl_trans_load_pnvm(struct iwl_trans *trans,
			const struct iwl_pnvm_image *pnvm_data,
			const struct iwl_ucode_capabilities *capa)
{
	return iwl_trans_pcie_ctx_info_gen3_load_pnvm(trans, pnvm_data, capa);
}
IWL_EXPORT_SYMBOL(iwl_trans_load_pnvm);

void iwl_trans_set_pnvm(struct iwl_trans *trans,
			const struct iwl_ucode_capabilities *capa)
{
	iwl_trans_pcie_ctx_info_gen3_set_pnvm(trans, capa);
}
IWL_EXPORT_SYMBOL(iwl_trans_set_pnvm);

int iwl_trans_load_reduce_power(struct iwl_trans *trans,
				const struct iwl_pnvm_image *payloads,
				const struct iwl_ucode_capabilities *capa)
{
	return iwl_trans_pcie_ctx_info_gen3_load_reduce_power(trans, payloads,
							      capa);
}
IWL_EXPORT_SYMBOL(iwl_trans_load_reduce_power);

void iwl_trans_set_reduce_power(struct iwl_trans *trans,
				const struct iwl_ucode_capabilities *capa)
{
	iwl_trans_pcie_ctx_info_gen3_set_reduce_power(trans, capa);
}
IWL_EXPORT_SYMBOL(iwl_trans_set_reduce_power);
