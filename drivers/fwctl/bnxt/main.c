// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2026, Broadcom Corporation
 */

#include <linux/auxiliary_bus.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/fwctl.h>
#include <linux/bnxt/hsi.h>
#include <linux/bnxt/ulp.h>
#include <uapi/fwctl/fwctl.h>
#include <uapi/fwctl/bnxt.h>

struct bnxtctl_uctx {
	struct fwctl_uctx uctx;
	u32 uctx_caps;
};

struct bnxtctl_dev {
	struct fwctl_device fwctl;
	struct bnxt_aux_priv *aux_priv;
};

DEFINE_FREE(bnxtctl, struct bnxtctl_dev *, if (_T) fwctl_put(&_T->fwctl))

static int bnxtctl_open_uctx(struct fwctl_uctx *uctx)
{
	struct bnxtctl_uctx *bnxtctl_uctx =
		container_of(uctx, struct bnxtctl_uctx, uctx);

	bnxtctl_uctx->uctx_caps = BIT(FWCTL_BNXT_INLINE_COMMANDS) |
				  BIT(FWCTL_BNXT_QUERY_COMMANDS) |
				  BIT(FWCTL_BNXT_SEND_COMMANDS) |
				  BIT(FWCTL_BNXT_DMA_COMMANDS);
	return 0;
}

static void bnxtctl_close_uctx(struct fwctl_uctx *uctx)
{
}

static void *bnxtctl_info(struct fwctl_uctx *uctx, size_t *length)
{
	struct bnxtctl_uctx *bnxtctl_uctx =
		container_of(uctx, struct bnxtctl_uctx, uctx);
	struct fwctl_info_bnxt *info;

	info = kzalloc_obj(*info);
	if (!info)
		return ERR_PTR(-ENOMEM);

	info->uctx_caps = bnxtctl_uctx->uctx_caps;

	*length = sizeof(*info);
	return info;
}

#define BNXTCTL_MAX_DMA_FIELDS	4

struct bnxtctl_dma_field {
	size_t offset;		/* offsetof(hwrm_xxx_input, addr_field) */
	enum dma_data_direction dir;
	size_t len_offset;	/* offsetof(hwrm_xxx_input, len_field); 0 if the
				 * command carries no transfer-length field
				 */
	u8     len_width;	/* byte width of the length field: 2 or 4 */
	u8     len_unit;	/* bytes represented by one unit of the length field */
	u32    buf_len;		/* for commands with no length in payload */
};

struct bnxtctl_cmd_dma_desc {
	u16                      req_type;
	u8                       num_fields;
	u8                       scope_min;
	size_t                   req_size;   /* sizeof(struct hwrm_xxx_input) */
	struct bnxtctl_dma_field fields[BNXTCTL_MAX_DMA_FIELDS];
};

/* input struct has an addr/len pair, but len is multiplied by _unit */
#define CMD_DATA_UNIT(_struct, _dir, _data, _len, _unit) \
	{ .offset = offsetof(_struct, _data),            \
	  .dir = _dir,                                   \
	  .len_offset = offsetof(_struct, _len),         \
	  .len_width = sizeof(((_struct *)0)->_len),     \
	  .len_unit = _unit }

/* input struct has an addr/len pair with byte length */
#define CMD_DATA_SIMPLE(_struct, _dir, _data, _len) \
	CMD_DATA_UNIT(_struct, _dir, _data, _len, 1)

/* input struct has an addr but the length is fixed */
#define CMD_DATA_FIXED(_struct, _dir, _data, _len) \
	{ .offset = offsetof(_struct, _data), .dir = _dir, .buf_len = _len }

#define CMD_DMAS(_req_type, _scope_min, _struct, _num_fields, ...) \
	{                                                          \
		.req_type = _req_type,                             \
		.scope_min = _scope_min,                           \
		.req_size = sizeof(_struct),                       \
		.num_fields = _num_fields,                         \
		.fields = { __VA_ARGS__ },                         \
	}

#define CMD_DMA_LEN(_req_type, _scope_min, _dir, _struct, _data, _len) \
	CMD_DMAS(_req_type, _scope_min, _struct, 1,                    \
		 CMD_DATA_SIMPLE(_struct, _dir, _data, _len))

/*
 * Per-command DMA buffer descriptor table for HWRM commands that
 * carry __le64 DMA address fields in their input
 */
static const struct bnxtctl_cmd_dma_desc bnxtctl_dma_cmds[] = {
	CMD_DMA_LEN(HWRM_NVM_SET_VARIABLE, FWCTL_RPC_CONFIGURATION,
		    DMA_TO_DEVICE,
		    struct hwrm_nvm_set_variable_input, src_data_addr,
		    data_len),
	CMD_DMA_LEN(HWRM_NVM_GET_VARIABLE, FWCTL_RPC_CONFIGURATION,
		    DMA_FROM_DEVICE,
		    struct hwrm_nvm_get_variable_input, dest_data_addr,
		    data_len),
	CMD_DMA_LEN(HWRM_NVM_READ, FWCTL_RPC_DEBUG_READ_ONLY,
		    DMA_FROM_DEVICE, struct hwrm_nvm_read_input,
		    host_dest_addr, len),
	CMD_DMAS(HWRM_NVM_GET_DIR_ENTRIES, FWCTL_RPC_DEBUG_READ_ONLY,
		 struct hwrm_nvm_get_dir_entries_input, 1,
		 CMD_DATA_FIXED(struct hwrm_nvm_get_dir_entries_input,
				DMA_FROM_DEVICE, host_dest_addr,
				FWCTL_BNXT_MAX_DMABUF)),
	CMD_DMA_LEN(HWRM_NVM_WRITE, FWCTL_RPC_DEBUG_WRITE,
		    DMA_TO_DEVICE, struct hwrm_nvm_write_input,
		    host_src_addr, dir_data_length),
	CMD_DMA_LEN(HWRM_NVM_MODIFY, FWCTL_RPC_DEBUG_WRITE,
		    DMA_TO_DEVICE, struct hwrm_nvm_modify_input,
		    host_src_addr, len),
	CMD_DMA_LEN(HWRM_NVM_RAW_WRITE_BLK, FWCTL_RPC_DEBUG_WRITE_FULL,
		    DMA_TO_DEVICE,
		    struct hwrm_nvm_raw_write_blk_input, host_src_addr, len),
	CMD_DMA_LEN(HWRM_NVM_RAW_DUMP, FWCTL_RPC_DEBUG_READ_ONLY,
		    DMA_FROM_DEVICE, struct hwrm_nvm_raw_dump_input,
		    host_dest_addr, len),

	CMD_DMA_LEN(HWRM_FW_GET_STRUCTURED_DATA, FWCTL_RPC_DEBUG_READ_ONLY,
		    DMA_FROM_DEVICE,
		    struct hwrm_fw_get_structured_data_input, dest_data_addr,
		    data_len),
	CMD_DMA_LEN(HWRM_FW_SET_STRUCTURED_DATA, FWCTL_RPC_DEBUG_WRITE,
		    DMA_TO_DEVICE,
		    struct hwrm_fw_set_structured_data_input, src_data_addr,
		    data_len),
	CMD_DMA_LEN(HWRM_FW_LIVEPATCH, FWCTL_RPC_DEBUG_WRITE_FULL,
		    DMA_TO_DEVICE, struct hwrm_fw_livepatch_input,
		    host_addr, patch_len),

	CMD_DMA_LEN(HWRM_DBG_COREDUMP_LIST, FWCTL_RPC_DEBUG_READ_ONLY,
		    DMA_FROM_DEVICE,
		    struct hwrm_dbg_coredump_list_input, host_dest_addr,
		    host_buf_len),
	CMD_DMA_LEN(HWRM_DBG_COREDUMP_RETRIEVE, FWCTL_RPC_DEBUG_READ_ONLY,
		    DMA_FROM_DEVICE,
		    struct hwrm_dbg_coredump_retrieve_input, host_dest_addr,
		    host_buf_len),
	/* read_len32 counts 32-bit words, not bytes (see bnxt_dbg_hwrm_rd_reg()). */
	CMD_DMAS(HWRM_DBG_READ_DIRECT, FWCTL_RPC_DEBUG_READ_ONLY,
		 struct hwrm_dbg_read_direct_input, 1,
		 CMD_DATA_UNIT(struct hwrm_dbg_read_direct_input,
			       DMA_FROM_DEVICE,
			       host_dest_addr, read_len32, 4)),
	CMD_DMA_LEN(HWRM_DBG_READ_INDIRECT, FWCTL_RPC_DEBUG_READ_ONLY,
		    DMA_FROM_DEVICE,
		    struct hwrm_dbg_read_indirect_input, host_dest_addr,
		    host_dest_addr_len),
	CMD_DMA_LEN(HWRM_DBG_SERDES_TEST, FWCTL_RPC_DEBUG_READ_ONLY,
		    DMA_FROM_DEVICE,
		    struct hwrm_dbg_serdes_test_input, resp_data_addr,
		    data_len),
	CMD_DMA_LEN(HWRM_DBG_TOKEN_CFG, FWCTL_RPC_DEBUG_WRITE_FULL,
		    DMA_TO_DEVICE, struct hwrm_dbg_token_cfg_input,
		    host_src_addr, dbg_token_len),

	CMD_DMA_LEN(HWRM_QUEUE_DSCP2PRI_QCFG, FWCTL_RPC_DEBUG_READ_ONLY,
		    DMA_FROM_DEVICE,
		    struct hwrm_queue_dscp2pri_qcfg_input, dest_data_addr,
		    dest_data_buffer_size),

	CMD_DMAS(HWRM_PORT_QSTATS, FWCTL_RPC_DEBUG_READ_ONLY,
		 struct hwrm_port_qstats_input, 2,
		 CMD_DATA_FIXED(struct hwrm_port_qstats_input,
				DMA_FROM_DEVICE, tx_stat_host_addr,
				sizeof(struct tx_port_stats)),
		 CMD_DATA_FIXED(struct hwrm_port_qstats_input,
				DMA_FROM_DEVICE, rx_stat_host_addr,
				sizeof(struct rx_port_stats))),
	CMD_DMAS(HWRM_PORT_QSTATS_EXT, FWCTL_RPC_DEBUG_READ_ONLY,
		 struct hwrm_port_qstats_ext_input, 2,
		 CMD_DATA_SIMPLE(struct hwrm_port_qstats_ext_input,
				 DMA_FROM_DEVICE, tx_stat_host_addr,
				 tx_stat_size),
		 CMD_DATA_SIMPLE(struct hwrm_port_qstats_ext_input,
				 DMA_FROM_DEVICE, rx_stat_host_addr,
				 rx_stat_size)),
	CMD_DMAS(HWRM_PORT_QSTATS_EXT_PFC_ADV, FWCTL_RPC_DEBUG_READ_ONLY,
		 struct hwrm_port_qstats_ext_pfc_adv_input, 2,
		 CMD_DATA_SIMPLE(struct hwrm_port_qstats_ext_pfc_adv_input,
				 DMA_FROM_DEVICE,
				 tx_pfc_adv_stat_host_addr, pfc_adv_stat_size),
		 CMD_DATA_SIMPLE(struct hwrm_port_qstats_ext_pfc_adv_input,
				 DMA_FROM_DEVICE,
				 rx_pfc_adv_stat_host_addr, pfc_adv_stat_size)),
	CMD_DMA_LEN(HWRM_PCIE_QSTATS, FWCTL_RPC_DEBUG_READ_ONLY,
		    DMA_FROM_DEVICE, struct hwrm_pcie_qstats_input,
		    pcie_stat_host_addr, pcie_stat_size),
	CMD_DMA_LEN(HWRM_STAT_GENERIC_QSTATS, FWCTL_RPC_DEBUG_READ_ONLY,
		    DMA_FROM_DEVICE,
		    struct hwrm_stat_generic_qstats_input,
		    generic_stat_host_addr, generic_stat_size),
	CMD_DMA_LEN(HWRM_STAT_QUERY_ROCE_STATS, FWCTL_RPC_DEBUG_READ_ONLY,
		    DMA_FROM_DEVICE,
		    struct hwrm_stat_query_roce_stats_input,
		    roce_stat_host_addr, roce_stat_size),
	CMD_DMA_LEN(HWRM_STAT_QUERY_ROCE_STATS_EXT, FWCTL_RPC_DEBUG_READ_ONLY,
		    DMA_FROM_DEVICE,
		    struct hwrm_stat_query_roce_stats_ext_input,
		    roce_stat_host_addr, roce_stat_size),

	CMD_DMA_LEN(HWRM_PORT_EVENTS_LOG, FWCTL_RPC_DEBUG_READ_ONLY,
		    DMA_FROM_DEVICE,
		    struct hwrm_port_events_log_input, host_dest_addr,
		    host_dest_addr_len),
	CMD_DMA_LEN(HWRM_PORT_PRBS_TEST, FWCTL_RPC_DEBUG_READ_ONLY,
		    DMA_FROM_DEVICE,
		    struct hwrm_port_prbs_test_input, resp_data_addr, data_len),
	CMD_DMA_LEN(HWRM_PORT_DSC_DUMP, FWCTL_RPC_DEBUG_READ_ONLY,
		    DMA_FROM_DEVICE, struct hwrm_port_dsc_dump_input,
		    resp_data_addr, data_len),

	/* num_fids counts 16-bit FIDs, not bytes. */
	CMD_DMAS(HWRM_SCH_GRP_CFG, FWCTL_RPC_DEBUG_WRITE,
		 struct hwrm_sch_grp_cfg_input, 1,
		 CMD_DATA_UNIT(struct hwrm_sch_grp_cfg_input,
			       DMA_TO_DEVICE, fid_table_addr,
			       num_fids, 2)),
	CMD_DMA_LEN(HWRM_SCH_GRP_QCFG, FWCTL_RPC_DEBUG_READ_ONLY,
		    DMA_FROM_DEVICE, struct hwrm_sch_grp_qcfg_input,
		    fid_table_addr, fid_table_len),

	CMD_DMA_LEN(HWRM_SELFTEST_RETRIEVE_SERDES_DATA,
		    FWCTL_RPC_DEBUG_READ_ONLY, DMA_FROM_DEVICE,
		    struct hwrm_selftest_retrieve_serdes_data_input,
		    resp_data_addr, data_len),

	CMD_DMAS(HWRM_DBG_PTRACE, FWCTL_RPC_DEBUG_WRITE,
		 struct hwrm_dbg_ptrace_input, 2,
		 CMD_DATA_SIMPLE(struct hwrm_dbg_ptrace_input,
				 DMA_TO_DEVICE, pdi_cmd_buf_addr,
				 pdi_req_buf_len),
		 CMD_DATA_SIMPLE(struct hwrm_dbg_ptrace_input,
				 DMA_FROM_DEVICE, pdi_resp_buf_addr,
				 pdi_req_buf_len)),
};

#undef CMD_DATA_UNIT
#undef CMD_DATA_SIMPLE
#undef CMD_DATA_FIXED
#undef CMD_DMAS
#undef CMD_DMA_LEN

static const struct bnxtctl_cmd_dma_desc *
bnxtctl_find_dma_desc(u16 req_type)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(bnxtctl_dma_cmds); i++)
		if (bnxtctl_dma_cmds[i].req_type == req_type)
			return &bnxtctl_dma_cmds[i];
	return NULL;
}

static void bnxtctl_extract_and_zero_dma_fields(void *cmd,
						const struct bnxtctl_cmd_dma_desc *desc,
						u64 *user_addrs)
{
	unsigned int i;

	for (i = 0; i < desc->num_fields; i++) {
		__le32 *field = cmd + desc->fields[i].offset;

		user_addrs[i] = le32_to_cpu(field[0]) |
				((u64)le32_to_cpu(field[1]) << 32);
		field[0] = 0;
		field[1] = 0;
	}
}

static u32 bnxtctl_read_len_field(void *cmd, const struct bnxtctl_dma_field *f)
{
	if (f->len_width == 2)
		return le16_to_cpup((__le16 *)(cmd + f->len_offset));
	return le32_to_cpup((__le32 *)(cmd + f->len_offset));
}

static int bnxtctl_check_dma_lens(void *cmd, const struct bnxtctl_cmd_dma_desc *desc,
				  u32 *lens)
{
	unsigned int i;

	for (i = 0; i < desc->num_fields; i++) {
		const struct bnxtctl_dma_field *f = &desc->fields[i];
		u64 len;

		if (f->len_offset) {
			if (check_mul_overflow(bnxtctl_read_len_field(cmd, f),
					       f->len_unit, &len))
				return -EINVAL;
		} else {
			len = f->buf_len;
		}

		if (!len || len > FWCTL_BNXT_MAX_DMABUF)
			return -EINVAL;

		lens[i] = len;
	}
	return 0;
}

static int bnxtctl_map_dma_bufs(struct device *dev, void *cmd,
				const struct bnxtctl_cmd_dma_desc *desc,
				const u64 *user_addrs, const u32 *lens,
				void **kbufs, dma_addr_t *dma_addrs,
				unsigned int *num_mapped)
{
	unsigned int i;

	*num_mapped = 0;
	for (i = 0; i < desc->num_fields; i++) {
		const struct bnxtctl_dma_field *f = &desc->fields[i];
		__le32 *field;

		kbufs[i] = dma_alloc_coherent(dev, lens[i],
					      &dma_addrs[i], GFP_KERNEL);
		if (!kbufs[i])
			return -ENOMEM;

		if (f->dir == DMA_TO_DEVICE &&
		    copy_from_user(kbufs[i], u64_to_user_ptr(user_addrs[i]),
				   lens[i])) {
			dma_free_coherent(dev, lens[i], kbufs[i],
					  dma_addrs[i]);
			kbufs[i] = NULL;
			return -EFAULT;
		}

		(*num_mapped)++;

		field = cmd + f->offset;
		field[0] = cpu_to_le32(lower_32_bits(dma_addrs[i]));
		field[1] = cpu_to_le32(upper_32_bits(dma_addrs[i]));
	}
	return 0;
}

static int bnxtctl_unmap_dma_bufs(struct device *dev,
				  const struct bnxtctl_cmd_dma_desc *desc,
				  const u64 *user_addrs, const u32 *lens,
				  void **kbufs, dma_addr_t *dma_addrs,
				  unsigned int num_mapped)
{
	unsigned int i;
	int rc = 0;

	for (i = 0; i < num_mapped; i++) {
		if (desc->fields[i].dir == DMA_FROM_DEVICE &&
		    copy_to_user(u64_to_user_ptr(user_addrs[i]),
				 kbufs[i], lens[i]))
			rc = -EFAULT;

		dma_free_coherent(dev, lens[i], kbufs[i], dma_addrs[i]);
	}
	return rc;
}

/* Caller must hold edev->en_dev_lock */
static bool bnxtctl_validate_rpc(struct bnxt_en_dev *edev,
				 struct bnxt_fw_msg *hwrm_in,
				 enum fwctl_rpc_scope scope)
{
	struct input *req = (struct input *)hwrm_in->msg;
	u16 req_type = le16_to_cpu(req->req_type);
	const struct bnxtctl_cmd_dma_desc *desc;

	lockdep_assert_held(&edev->en_dev_lock);
	if (edev->flags & BNXT_EN_FLAG_ULP_STOPPED)
		return false;

	desc = bnxtctl_find_dma_desc(req_type);
	if (desc)
		return scope >= desc->scope_min;

	switch (req_type) {
	case HWRM_FUNC_RESET:
	case HWRM_PORT_CLR_STATS:
	case HWRM_FW_RESET:
	case HWRM_FW_SYNC:
	case HWRM_FW_SET_TIME:
	case HWRM_DBG_LOG_BUFFER_FLUSH:
	case HWRM_DBG_ERASE_NVM:
	case HWRM_DBG_CFG:
	case HWRM_NVM_DEFRAG:
	case HWRM_NVM_FACTORY_DEFAULTS:
	case HWRM_NVM_FLUSH:
	case HWRM_NVM_VERIFY_UPDATE:
	case HWRM_NVM_ERASE_DIR_ENTRY:
	case HWRM_NVM_MOD_DIR_ENTRY:
	case HWRM_NVM_FIND_DIR_ENTRY:
		return scope >= FWCTL_RPC_CONFIGURATION;

	case HWRM_VER_GET:
	case HWRM_ERROR_RECOVERY_QCFG:
	case HWRM_FUNC_QCAPS:
	case HWRM_FUNC_QCFG:
	case HWRM_FUNC_QSTATS:
	case HWRM_PORT_PHY_QCFG:
	case HWRM_PORT_MAC_QCFG:
	case HWRM_PORT_PHY_QCAPS:
	case HWRM_PORT_PHY_I2C_READ:
	case HWRM_PORT_PHY_MDIO_READ:
	case HWRM_QUEUE_PRI2COS_QCFG:
	case HWRM_QUEUE_COS2BW_QCFG:
	case HWRM_VNIC_RSS_QCFG:
	case HWRM_QUEUE_GLOBAL_QCFG:
	case HWRM_QUEUE_ADPTV_QOS_RX_FEATURE_QCFG:
	case HWRM_QUEUE_ADPTV_QOS_TX_FEATURE_QCFG:
	case HWRM_QUEUE_QCAPS:
	case HWRM_QUEUE_ADPTV_QOS_RX_TUNING_QCFG:
	case HWRM_QUEUE_ADPTV_QOS_TX_TUNING_QCFG:
	case HWRM_TUNNEL_DST_PORT_QUERY:
	case HWRM_PORT_TX_FIR_QCFG:
	case HWRM_FW_LIVEPATCH_QUERY:
	case HWRM_FW_QSTATUS:
	case HWRM_FW_HEALTH_CHECK:
	case HWRM_FW_GET_TIME:
	case HWRM_PORT_EP_TX_QCFG:
	case HWRM_PORT_QCFG:
	case HWRM_PORT_MAC_QCAPS:
	case HWRM_TEMP_MONITOR_QUERY:
	case HWRM_REG_POWER_QUERY:
	case HWRM_CORE_FREQUENCY_QUERY:
	case HWRM_CFA_REDIRECT_QUERY_TUNNEL_TYPE:
	case HWRM_CFA_ADV_FLOW_MGNT_QCAPS:
	case HWRM_FUNC_RESOURCE_QCAPS:
	case HWRM_FUNC_BACKING_STORE_QCAPS:
	case HWRM_FUNC_BACKING_STORE_QCFG:
	case HWRM_FUNC_QSTATS_EXT:
	case HWRM_FUNC_PTP_PIN_QCFG:
	case HWRM_FUNC_PTP_EXT_QCFG:
	case HWRM_FUNC_BACKING_STORE_QCFG_V2:
	case HWRM_FUNC_BACKING_STORE_QCAPS_V2:
	case HWRM_FUNC_SYNCE_QCFG:
	case HWRM_FUNC_TTX_PACING_RATE_PROF_QUERY:
	case HWRM_PORT_PHY_FDRSTAT:
	case HWRM_DBG_RING_INFO_GET:
	case HWRM_DBG_QCAPS:
	case HWRM_DBG_QCFG:
	case HWRM_DBG_USEQ_FLUSH:
	case HWRM_DBG_USEQ_QCAPS:
	case HWRM_DBG_SIM_CABLE_STATE:
	case HWRM_DBG_TOKEN_QUERY_AUTH_IDS:
	case HWRM_NVM_GET_DEV_INFO:
	case HWRM_NVM_GET_DIR_INFO:
	case HWRM_SELFTEST_QLIST:
	case HWRM_DBG_COREDUMP_INITIATE:
		return scope >= FWCTL_RPC_DEBUG_READ_ONLY;

	case HWRM_PORT_PHY_I2C_WRITE:
	case HWRM_PORT_PHY_MDIO_WRITE:
		return scope >= FWCTL_RPC_DEBUG_WRITE;

	default:
		return false;
	}
}

#define BNXTCTL_HWRM_CMD_TIMEOUT_DFLT	500	/* ms */
#define BNXTCTL_HWRM_CMD_TIMEOUT_MEDM	2000	/* ms */
#define BNXTCTL_HWRM_CMD_TIMEOUT_LONG	60000	/* ms */

static unsigned int bnxtctl_get_timeout(struct input *req)
{
	switch (le16_to_cpu(req->req_type)) {
	case HWRM_NVM_DEFRAG:
	case HWRM_NVM_FACTORY_DEFAULTS:
	case HWRM_NVM_FLUSH:
	case HWRM_NVM_VERIFY_UPDATE:
	case HWRM_NVM_ERASE_DIR_ENTRY:
	case HWRM_NVM_MOD_DIR_ENTRY:
	case HWRM_NVM_WRITE:
	case HWRM_FW_SYNC:
	case HWRM_DBG_COREDUMP_LIST:
	case HWRM_DBG_COREDUMP_RETRIEVE:
	case HWRM_DBG_COREDUMP_INITIATE:
	case HWRM_SELFTEST_RETRIEVE_SERDES_DATA:
	case HWRM_DBG_SERDES_TEST:
	case HWRM_NVM_RAW_WRITE_BLK:
	case HWRM_FW_HEALTH_CHECK:
		return BNXTCTL_HWRM_CMD_TIMEOUT_LONG;
	case HWRM_FUNC_RESET:
		return BNXTCTL_HWRM_CMD_TIMEOUT_MEDM;
	default:
		return BNXTCTL_HWRM_CMD_TIMEOUT_DFLT;
	}
}

static void *bnxtctl_fw_rpc(struct fwctl_uctx *uctx,
			    enum fwctl_rpc_scope scope,
			    void *in, size_t in_len, size_t *out_len)
{
	struct bnxtctl_dev *bnxtctl =
		container_of(uctx->fwctl, struct bnxtctl_dev, fwctl);
	struct bnxt_en_dev *edev = bnxtctl->aux_priv->edev;
	dma_addr_t dma_addrs[BNXTCTL_MAX_DMA_FIELDS];
	void *kbufs[BNXTCTL_MAX_DMA_FIELDS] = {};
	const struct bnxtctl_cmd_dma_desc *desc;
	u64 user_addrs[BNXTCTL_MAX_DMA_FIELDS];
	struct device *dev = &edev->pdev->dev;
	u32 dma_lens[BNXTCTL_MAX_DMA_FIELDS];
	struct bnxt_fw_msg rpc_in = {};
	unsigned int num_mapped = 0;
	struct input *req = in;
	int rc;

	if (in_len < sizeof(struct input) || in_len > HWRM_MAX_REQ_LEN)
		return ERR_PTR(-EINVAL);

	if (*out_len < sizeof(struct output))
		return ERR_PTR(-EINVAL);

	desc = bnxtctl_find_dma_desc(le16_to_cpu(req->req_type));

	if (desc) {
		if (in_len != desc->req_size)
			return ERR_PTR(-EINVAL);

		rc = bnxtctl_check_dma_lens(in, desc, dma_lens);
		if (rc)
			return ERR_PTR(rc);

		bnxtctl_extract_and_zero_dma_fields(in, desc, user_addrs);
	}

	rpc_in.msg = in;
	rpc_in.msg_len = in_len;
	rpc_in.resp = kvzalloc(*out_len, GFP_KERNEL);
	if (!rpc_in.resp)
		return ERR_PTR(-ENOMEM);

	rpc_in.resp_max_len = *out_len;
	rpc_in.timeout = bnxtctl_get_timeout(in);

	guard(mutex)(&edev->en_dev_lock);

	if (!bnxtctl_validate_rpc(edev, &rpc_in, scope)) {
		kvfree(rpc_in.resp);
		return ERR_PTR(-EPERM);
	}

	if (desc) {
		rc = bnxtctl_map_dma_bufs(dev, in, desc, user_addrs, dma_lens,
					  kbufs, dma_addrs, &num_mapped);
		if (rc) {
			bnxtctl_unmap_dma_bufs(dev, desc, user_addrs, dma_lens,
					       kbufs, dma_addrs, num_mapped);
			kvfree(rpc_in.resp);
			return ERR_PTR(rc);
		}
	}

	rc = bnxt_send_msg(edev, &rpc_in);
	if (rc) {
		struct output *resp = rpc_in.resp;

		/* Copy the response to user always, as it contains
		 * detailed status of the command failure
		 */
		if (!resp->error_code)
			/* bnxt_send_msg() returned much before FW
			 * received the command.
			 */
			resp->error_code = cpu_to_le16(rc);
	}

	if (desc) {
		int unmap_rc;

		unmap_rc = bnxtctl_unmap_dma_bufs(dev, desc, user_addrs,
						  dma_lens, kbufs, dma_addrs,
						  num_mapped);
		if (unmap_rc) {
			kvfree(rpc_in.resp);
			return ERR_PTR(unmap_rc);
		}
	}

	return rpc_in.resp;
}

static const struct fwctl_ops bnxtctl_ops = {
	.device_type = FWCTL_DEVICE_TYPE_BNXT,
	.uctx_size = sizeof(struct bnxtctl_uctx),
	.open_uctx = bnxtctl_open_uctx,
	.close_uctx = bnxtctl_close_uctx,
	.info = bnxtctl_info,
	.fw_rpc = bnxtctl_fw_rpc,
};

static int bnxtctl_probe(struct auxiliary_device *adev,
			 const struct auxiliary_device_id *id)
{
	struct bnxt_aux_priv *aux_priv =
		container_of(adev, struct bnxt_aux_priv, aux_dev);
	struct bnxtctl_dev *bnxtctl __free(bnxtctl) =
		fwctl_alloc_device(&aux_priv->edev->pdev->dev, &bnxtctl_ops,
				   struct bnxtctl_dev, fwctl);
	int rc;

	if (!bnxtctl)
		return -ENOMEM;

	bnxtctl->aux_priv = aux_priv;

	rc = fwctl_register(&bnxtctl->fwctl);
	if (rc)
		return rc;

	auxiliary_set_drvdata(adev, no_free_ptr(bnxtctl));
	return 0;
}

static void bnxtctl_remove(struct auxiliary_device *adev)
{
	struct bnxtctl_dev *ctldev = auxiliary_get_drvdata(adev);

	fwctl_unregister(&ctldev->fwctl);
	fwctl_put(&ctldev->fwctl);
}

static const struct auxiliary_device_id bnxtctl_id_table[] = {
	{ .name = "bnxt_en.fwctl", },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, bnxtctl_id_table);

static struct auxiliary_driver bnxtctl_driver = {
	.name = "bnxt_fwctl",
	.probe = bnxtctl_probe,
	.remove = bnxtctl_remove,
	.id_table = bnxtctl_id_table,
};

module_auxiliary_driver(bnxtctl_driver);

MODULE_IMPORT_NS("FWCTL");
MODULE_DESCRIPTION("BNXT fwctl driver");
MODULE_AUTHOR("Pavan Chebbi <pavan.chebbi@broadcom.com>");
MODULE_AUTHOR("Andy Gospodarek <gospo@broadcom.com>");
MODULE_LICENSE("GPL");
