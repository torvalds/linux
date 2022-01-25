// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2018-2021 Intel Corporation
 */
#include <linux/firmware.h>
#include "iwl-drv.h"
#include "iwl-trans.h"
#include "iwl-dbg-tlv.h"
#include "fw/dbg.h"
#include "fw/runtime.h"

/**
 * enum iwl_dbg_tlv_type - debug TLV types
 * @IWL_DBG_TLV_TYPE_DEBUG_INFO: debug info TLV
 * @IWL_DBG_TLV_TYPE_BUF_ALLOC: buffer allocation TLV
 * @IWL_DBG_TLV_TYPE_HCMD: host command TLV
 * @IWL_DBG_TLV_TYPE_REGION: region TLV
 * @IWL_DBG_TLV_TYPE_TRIGGER: trigger TLV
 * @IWL_DBG_TLV_TYPE_CONF_SET: conf set TLV
 * @IWL_DBG_TLV_TYPE_NUM: number of debug TLVs
 */
enum iwl_dbg_tlv_type {
	IWL_DBG_TLV_TYPE_DEBUG_INFO =
		IWL_UCODE_TLV_TYPE_DEBUG_INFO - IWL_UCODE_TLV_DEBUG_BASE,
	IWL_DBG_TLV_TYPE_BUF_ALLOC,
	IWL_DBG_TLV_TYPE_HCMD,
	IWL_DBG_TLV_TYPE_REGION,
	IWL_DBG_TLV_TYPE_TRIGGER,
	IWL_DBG_TLV_TYPE_CONF_SET,
	IWL_DBG_TLV_TYPE_NUM,
};

/**
 * struct iwl_dbg_tlv_ver_data -  debug TLV version struct
 * @min_ver: min version supported
 * @max_ver: max version supported
 */
struct iwl_dbg_tlv_ver_data {
	int min_ver;
	int max_ver;
};

/**
 * struct iwl_dbg_tlv_timer_node - timer node struct
 * @list: list of &struct iwl_dbg_tlv_timer_node
 * @timer: timer
 * @fwrt: &struct iwl_fw_runtime
 * @tlv: TLV attach to the timer node
 */
struct iwl_dbg_tlv_timer_node {
	struct list_head list;
	struct timer_list timer;
	struct iwl_fw_runtime *fwrt;
	struct iwl_ucode_tlv *tlv;
};

static const struct iwl_dbg_tlv_ver_data
dbg_ver_table[IWL_DBG_TLV_TYPE_NUM] = {
	[IWL_DBG_TLV_TYPE_DEBUG_INFO]	= {.min_ver = 1, .max_ver = 1,},
	[IWL_DBG_TLV_TYPE_BUF_ALLOC]	= {.min_ver = 1, .max_ver = 1,},
	[IWL_DBG_TLV_TYPE_HCMD]		= {.min_ver = 1, .max_ver = 1,},
	[IWL_DBG_TLV_TYPE_REGION]	= {.min_ver = 1, .max_ver = 3,},
	[IWL_DBG_TLV_TYPE_TRIGGER]	= {.min_ver = 1, .max_ver = 1,},
	[IWL_DBG_TLV_TYPE_CONF_SET]	= {.min_ver = 1, .max_ver = 1,},
};

static int iwl_dbg_tlv_add(const struct iwl_ucode_tlv *tlv,
			   struct list_head *list)
{
	u32 len = le32_to_cpu(tlv->length);
	struct iwl_dbg_tlv_node *node;

	node = kzalloc(sizeof(*node) + len, GFP_KERNEL);
	if (!node)
		return -ENOMEM;

	memcpy(&node->tlv, tlv, sizeof(node->tlv) + len);
	list_add_tail(&node->list, list);

	return 0;
}

static bool iwl_dbg_tlv_ver_support(const struct iwl_ucode_tlv *tlv)
{
	const struct iwl_fw_ini_header *hdr = (const void *)&tlv->data[0];
	u32 type = le32_to_cpu(tlv->type);
	u32 tlv_idx = type - IWL_UCODE_TLV_DEBUG_BASE;
	u32 ver = le32_to_cpu(hdr->version);

	if (ver < dbg_ver_table[tlv_idx].min_ver ||
	    ver > dbg_ver_table[tlv_idx].max_ver)
		return false;

	return true;
}

static int iwl_dbg_tlv_alloc_debug_info(struct iwl_trans *trans,
					const struct iwl_ucode_tlv *tlv)
{
	const struct iwl_fw_ini_debug_info_tlv *debug_info = (const void *)tlv->data;

	if (le32_to_cpu(tlv->length) != sizeof(*debug_info))
		return -EINVAL;

	IWL_DEBUG_FW(trans, "WRT: Loading debug cfg: %s\n",
		     debug_info->debug_cfg_name);

	return iwl_dbg_tlv_add(tlv, &trans->dbg.debug_info_tlv_list);
}

static int iwl_dbg_tlv_alloc_buf_alloc(struct iwl_trans *trans,
				       const struct iwl_ucode_tlv *tlv)
{
	const struct iwl_fw_ini_allocation_tlv *alloc = (const void *)tlv->data;
	u32 buf_location;
	u32 alloc_id;

	if (le32_to_cpu(tlv->length) != sizeof(*alloc))
		return -EINVAL;

	buf_location = le32_to_cpu(alloc->buf_location);
	alloc_id = le32_to_cpu(alloc->alloc_id);

	if (buf_location == IWL_FW_INI_LOCATION_INVALID ||
	    buf_location >= IWL_FW_INI_LOCATION_NUM)
		goto err;

	if (alloc_id == IWL_FW_INI_ALLOCATION_INVALID ||
	    alloc_id >= IWL_FW_INI_ALLOCATION_NUM)
		goto err;

	if (buf_location == IWL_FW_INI_LOCATION_NPK_PATH &&
	    alloc_id != IWL_FW_INI_ALLOCATION_ID_DBGC1)
		goto err;

	if (buf_location == IWL_FW_INI_LOCATION_SRAM_PATH &&
	    alloc_id != IWL_FW_INI_ALLOCATION_ID_DBGC1)
		goto err;

	trans->dbg.fw_mon_cfg[alloc_id] = *alloc;

	return 0;
err:
	IWL_ERR(trans,
		"WRT: Invalid allocation id %u and/or location id %u for allocation TLV\n",
		alloc_id, buf_location);
	return -EINVAL;
}

static int iwl_dbg_tlv_alloc_hcmd(struct iwl_trans *trans,
				  const struct iwl_ucode_tlv *tlv)
{
	const struct iwl_fw_ini_hcmd_tlv *hcmd = (const void *)tlv->data;
	u32 tp = le32_to_cpu(hcmd->time_point);

	if (le32_to_cpu(tlv->length) <= sizeof(*hcmd))
		return -EINVAL;

	/* Host commands can not be sent in early time point since the FW
	 * is not ready
	 */
	if (tp == IWL_FW_INI_TIME_POINT_INVALID ||
	    tp >= IWL_FW_INI_TIME_POINT_NUM ||
	    tp == IWL_FW_INI_TIME_POINT_EARLY) {
		IWL_ERR(trans,
			"WRT: Invalid time point %u for host command TLV\n",
			tp);
		return -EINVAL;
	}

	return iwl_dbg_tlv_add(tlv, &trans->dbg.time_point[tp].hcmd_list);
}

static int iwl_dbg_tlv_alloc_region(struct iwl_trans *trans,
				    const struct iwl_ucode_tlv *tlv)
{
	const struct iwl_fw_ini_region_tlv *reg = (const void *)tlv->data;
	struct iwl_ucode_tlv **active_reg;
	u32 id = le32_to_cpu(reg->id);
	u8 type = reg->type;
	u32 tlv_len = sizeof(*tlv) + le32_to_cpu(tlv->length);

	/*
	 * The higher part of the ID in from version 2 is irrelevant for
	 * us, so mask it out.
	 */
	if (le32_to_cpu(reg->hdr.version) >= 2)
		id &= IWL_FW_INI_REGION_V2_MASK;

	if (le32_to_cpu(tlv->length) < sizeof(*reg))
		return -EINVAL;

	/* for safe use of a string from FW, limit it to IWL_FW_INI_MAX_NAME */
	IWL_DEBUG_FW(trans, "WRT: parsing region: %.*s\n",
		     IWL_FW_INI_MAX_NAME, reg->name);

	if (id >= IWL_FW_INI_MAX_REGION_ID) {
		IWL_ERR(trans, "WRT: Invalid region id %u\n", id);
		return -EINVAL;
	}

	if (type <= IWL_FW_INI_REGION_INVALID ||
	    type >= IWL_FW_INI_REGION_NUM) {
		IWL_ERR(trans, "WRT: Invalid region type %u\n", type);
		return -EINVAL;
	}

	if (type == IWL_FW_INI_REGION_PCI_IOSF_CONFIG &&
	    !trans->ops->read_config32) {
		IWL_ERR(trans, "WRT: Unsupported region type %u\n", type);
		return -EOPNOTSUPP;
	}

	active_reg = &trans->dbg.active_regions[id];
	if (*active_reg) {
		IWL_WARN(trans, "WRT: Overriding region id %u\n", id);

		kfree(*active_reg);
	}

	*active_reg = kmemdup(tlv, tlv_len, GFP_KERNEL);
	if (!*active_reg)
		return -ENOMEM;

	IWL_DEBUG_FW(trans, "WRT: Enabling region id %u type %u\n", id, type);

	return 0;
}

static int iwl_dbg_tlv_alloc_trigger(struct iwl_trans *trans,
				     const struct iwl_ucode_tlv *tlv)
{
	const struct iwl_fw_ini_trigger_tlv *trig = (const void *)tlv->data;
	struct iwl_fw_ini_trigger_tlv *dup_trig;
	u32 tp = le32_to_cpu(trig->time_point);
	u32 rf = le32_to_cpu(trig->reset_fw);
	struct iwl_ucode_tlv *dup = NULL;
	int ret;

	if (le32_to_cpu(tlv->length) < sizeof(*trig))
		return -EINVAL;

	if (tp <= IWL_FW_INI_TIME_POINT_INVALID ||
	    tp >= IWL_FW_INI_TIME_POINT_NUM) {
		IWL_ERR(trans,
			"WRT: Invalid time point %u for trigger TLV\n",
			tp);
		return -EINVAL;
	}

	IWL_DEBUG_FW(trans,
		     "WRT: time point %u for trigger TLV with reset_fw %u\n",
		     tp, rf);
	trans->dbg.last_tp_resetfw = 0xFF;
	if (!le32_to_cpu(trig->occurrences)) {
		dup = kmemdup(tlv, sizeof(*tlv) + le32_to_cpu(tlv->length),
				GFP_KERNEL);
		if (!dup)
			return -ENOMEM;
		dup_trig = (void *)dup->data;
		dup_trig->occurrences = cpu_to_le32(-1);
		tlv = dup;
	}

	ret = iwl_dbg_tlv_add(tlv, &trans->dbg.time_point[tp].trig_list);
	kfree(dup);

	return ret;
}

static int iwl_dbg_tlv_config_set(struct iwl_trans *trans,
				  const struct iwl_ucode_tlv *tlv)
{
	struct iwl_fw_ini_conf_set_tlv *conf_set = (void *)tlv->data;
	u32 tp = le32_to_cpu(conf_set->time_point);
	u32 type = le32_to_cpu(conf_set->set_type);

	if (tp <= IWL_FW_INI_TIME_POINT_INVALID ||
	    tp >= IWL_FW_INI_TIME_POINT_NUM) {
		IWL_DEBUG_FW(trans,
			     "WRT: Invalid time point %u for config set TLV\n", tp);
		return -EINVAL;
	}

	if (type <= IWL_FW_INI_CONFIG_SET_TYPE_INVALID ||
	    type >= IWL_FW_INI_CONFIG_SET_TYPE_MAX_NUM) {
		IWL_DEBUG_FW(trans,
			     "WRT: Invalid config set type %u for config set TLV\n", type);
		return -EINVAL;
	}

	return iwl_dbg_tlv_add(tlv, &trans->dbg.time_point[tp].config_list);
}

static int (*dbg_tlv_alloc[])(struct iwl_trans *trans,
			      const struct iwl_ucode_tlv *tlv) = {
	[IWL_DBG_TLV_TYPE_DEBUG_INFO]	= iwl_dbg_tlv_alloc_debug_info,
	[IWL_DBG_TLV_TYPE_BUF_ALLOC]	= iwl_dbg_tlv_alloc_buf_alloc,
	[IWL_DBG_TLV_TYPE_HCMD]		= iwl_dbg_tlv_alloc_hcmd,
	[IWL_DBG_TLV_TYPE_REGION]	= iwl_dbg_tlv_alloc_region,
	[IWL_DBG_TLV_TYPE_TRIGGER]	= iwl_dbg_tlv_alloc_trigger,
	[IWL_DBG_TLV_TYPE_CONF_SET]	= iwl_dbg_tlv_config_set,
};

void iwl_dbg_tlv_alloc(struct iwl_trans *trans, const struct iwl_ucode_tlv *tlv,
		       bool ext)
{
	enum iwl_ini_cfg_state *cfg_state = ext ?
		&trans->dbg.external_ini_cfg : &trans->dbg.internal_ini_cfg;
	const struct iwl_fw_ini_header *hdr = (const void *)&tlv->data[0];
	u32 type;
	u32 tlv_idx;
	u32 domain;
	int ret;

	if (le32_to_cpu(tlv->length) < sizeof(*hdr))
		return;

	type = le32_to_cpu(tlv->type);
	tlv_idx = type - IWL_UCODE_TLV_DEBUG_BASE;
	domain = le32_to_cpu(hdr->domain);

	if (domain != IWL_FW_INI_DOMAIN_ALWAYS_ON &&
	    !(domain & trans->dbg.domains_bitmap)) {
		IWL_DEBUG_FW(trans,
			     "WRT: Skipping TLV with disabled domain 0x%0x (0x%0x)\n",
			     domain, trans->dbg.domains_bitmap);
		return;
	}

	if (tlv_idx >= ARRAY_SIZE(dbg_tlv_alloc) || !dbg_tlv_alloc[tlv_idx]) {
		IWL_ERR(trans, "WRT: Unsupported TLV type 0x%x\n", type);
		goto out_err;
	}

	if (!iwl_dbg_tlv_ver_support(tlv)) {
		IWL_ERR(trans, "WRT: Unsupported TLV 0x%x version %u\n", type,
			le32_to_cpu(hdr->version));
		goto out_err;
	}

	ret = dbg_tlv_alloc[tlv_idx](trans, tlv);
	if (ret) {
		IWL_ERR(trans,
			"WRT: Failed to allocate TLV 0x%x, ret %d, (ext=%d)\n",
			type, ret, ext);
		goto out_err;
	}

	if (*cfg_state == IWL_INI_CFG_STATE_NOT_LOADED)
		*cfg_state = IWL_INI_CFG_STATE_LOADED;

	return;

out_err:
	*cfg_state = IWL_INI_CFG_STATE_CORRUPTED;
}

void iwl_dbg_tlv_del_timers(struct iwl_trans *trans)
{
	struct list_head *timer_list = &trans->dbg.periodic_trig_list;
	struct iwl_dbg_tlv_timer_node *node, *tmp;

	list_for_each_entry_safe(node, tmp, timer_list, list) {
		del_timer(&node->timer);
		list_del(&node->list);
		kfree(node);
	}
}
IWL_EXPORT_SYMBOL(iwl_dbg_tlv_del_timers);

static void iwl_dbg_tlv_fragments_free(struct iwl_trans *trans,
				       enum iwl_fw_ini_allocation_id alloc_id)
{
	struct iwl_fw_mon *fw_mon;
	int i;

	if (alloc_id <= IWL_FW_INI_ALLOCATION_INVALID ||
	    alloc_id >= IWL_FW_INI_ALLOCATION_NUM)
		return;

	fw_mon = &trans->dbg.fw_mon_ini[alloc_id];

	for (i = 0; i < fw_mon->num_frags; i++) {
		struct iwl_dram_data *frag = &fw_mon->frags[i];

		dma_free_coherent(trans->dev, frag->size, frag->block,
				  frag->physical);

		frag->physical = 0;
		frag->block = NULL;
		frag->size = 0;
	}

	kfree(fw_mon->frags);
	fw_mon->frags = NULL;
	fw_mon->num_frags = 0;
}

void iwl_dbg_tlv_free(struct iwl_trans *trans)
{
	struct iwl_dbg_tlv_node *tlv_node, *tlv_node_tmp;
	int i;

	iwl_dbg_tlv_del_timers(trans);

	for (i = 0; i < ARRAY_SIZE(trans->dbg.active_regions); i++) {
		struct iwl_ucode_tlv **active_reg =
			&trans->dbg.active_regions[i];

		kfree(*active_reg);
		*active_reg = NULL;
	}

	list_for_each_entry_safe(tlv_node, tlv_node_tmp,
				 &trans->dbg.debug_info_tlv_list, list) {
		list_del(&tlv_node->list);
		kfree(tlv_node);
	}

	for (i = 0; i < ARRAY_SIZE(trans->dbg.time_point); i++) {
		struct iwl_dbg_tlv_time_point_data *tp =
			&trans->dbg.time_point[i];

		list_for_each_entry_safe(tlv_node, tlv_node_tmp, &tp->trig_list,
					 list) {
			list_del(&tlv_node->list);
			kfree(tlv_node);
		}

		list_for_each_entry_safe(tlv_node, tlv_node_tmp, &tp->hcmd_list,
					 list) {
			list_del(&tlv_node->list);
			kfree(tlv_node);
		}

		list_for_each_entry_safe(tlv_node, tlv_node_tmp,
					 &tp->active_trig_list, list) {
			list_del(&tlv_node->list);
			kfree(tlv_node);
		}

		list_for_each_entry_safe(tlv_node, tlv_node_tmp,
					 &tp->config_list, list) {
			list_del(&tlv_node->list);
			kfree(tlv_node);
		}

	}

	for (i = 0; i < ARRAY_SIZE(trans->dbg.fw_mon_ini); i++)
		iwl_dbg_tlv_fragments_free(trans, i);
}

static int iwl_dbg_tlv_parse_bin(struct iwl_trans *trans, const u8 *data,
				 size_t len)
{
	const struct iwl_ucode_tlv *tlv;
	u32 tlv_len;

	while (len >= sizeof(*tlv)) {
		len -= sizeof(*tlv);
		tlv = (void *)data;

		tlv_len = le32_to_cpu(tlv->length);

		if (len < tlv_len) {
			IWL_ERR(trans, "invalid TLV len: %zd/%u\n",
				len, tlv_len);
			return -EINVAL;
		}
		len -= ALIGN(tlv_len, 4);
		data += sizeof(*tlv) + ALIGN(tlv_len, 4);

		iwl_dbg_tlv_alloc(trans, tlv, true);
	}

	return 0;
}

void iwl_dbg_tlv_load_bin(struct device *dev, struct iwl_trans *trans)
{
	const struct firmware *fw;
	const char *yoyo_bin = "iwl-debug-yoyo.bin";
	int res;

	if (!iwlwifi_mod_params.enable_ini ||
	    trans->trans_cfg->device_family <= IWL_DEVICE_FAMILY_8000)
		return;

	res = firmware_request_nowarn(&fw, yoyo_bin, dev);
	IWL_DEBUG_FW(trans, "%s %s\n", res ? "didn't load" : "loaded", yoyo_bin);

	if (res)
		return;

	iwl_dbg_tlv_parse_bin(trans, fw->data, fw->size);

	release_firmware(fw);
}

void iwl_dbg_tlv_init(struct iwl_trans *trans)
{
	int i;

	INIT_LIST_HEAD(&trans->dbg.debug_info_tlv_list);
	INIT_LIST_HEAD(&trans->dbg.periodic_trig_list);

	for (i = 0; i < ARRAY_SIZE(trans->dbg.time_point); i++) {
		struct iwl_dbg_tlv_time_point_data *tp =
			&trans->dbg.time_point[i];

		INIT_LIST_HEAD(&tp->trig_list);
		INIT_LIST_HEAD(&tp->hcmd_list);
		INIT_LIST_HEAD(&tp->active_trig_list);
		INIT_LIST_HEAD(&tp->config_list);
	}
}

static int iwl_dbg_tlv_alloc_fragment(struct iwl_fw_runtime *fwrt,
				      struct iwl_dram_data *frag, u32 pages)
{
	void *block = NULL;
	dma_addr_t physical;

	if (!frag || frag->size || !pages)
		return -EIO;

	/*
	 * We try to allocate as many pages as we can, starting with
	 * the requested amount and going down until we can allocate
	 * something.  Because of DIV_ROUND_UP(), pages will never go
	 * down to 0 and stop the loop, so stop when pages reaches 1,
	 * which is too small anyway.
	 */
	while (pages > 1) {
		block = dma_alloc_coherent(fwrt->dev, pages * PAGE_SIZE,
					   &physical,
					   GFP_KERNEL | __GFP_NOWARN);
		if (block)
			break;

		IWL_WARN(fwrt, "WRT: Failed to allocate fragment size %lu\n",
			 pages * PAGE_SIZE);

		pages = DIV_ROUND_UP(pages, 2);
	}

	if (!block)
		return -ENOMEM;

	frag->physical = physical;
	frag->block = block;
	frag->size = pages * PAGE_SIZE;

	return pages;
}

static int iwl_dbg_tlv_alloc_fragments(struct iwl_fw_runtime *fwrt,
				       enum iwl_fw_ini_allocation_id alloc_id)
{
	struct iwl_fw_mon *fw_mon;
	struct iwl_fw_ini_allocation_tlv *fw_mon_cfg;
	u32 num_frags, remain_pages, frag_pages;
	int i;

	if (alloc_id < IWL_FW_INI_ALLOCATION_INVALID ||
	    alloc_id >= IWL_FW_INI_ALLOCATION_NUM)
		return -EIO;

	fw_mon_cfg = &fwrt->trans->dbg.fw_mon_cfg[alloc_id];
	fw_mon = &fwrt->trans->dbg.fw_mon_ini[alloc_id];

	if (fw_mon->num_frags ||
	    fw_mon_cfg->buf_location !=
	    cpu_to_le32(IWL_FW_INI_LOCATION_DRAM_PATH))
		return 0;

	num_frags = le32_to_cpu(fw_mon_cfg->max_frags_num);
	if (!fw_has_capa(&fwrt->fw->ucode_capa,
			 IWL_UCODE_TLV_CAPA_DBG_BUF_ALLOC_CMD_SUPP)) {
		if (alloc_id != IWL_FW_INI_ALLOCATION_ID_DBGC1)
			return -EIO;
		num_frags = 1;
	}

	remain_pages = DIV_ROUND_UP(le32_to_cpu(fw_mon_cfg->req_size),
				    PAGE_SIZE);
	num_frags = min_t(u32, num_frags, BUF_ALLOC_MAX_NUM_FRAGS);
	num_frags = min_t(u32, num_frags, remain_pages);
	frag_pages = DIV_ROUND_UP(remain_pages, num_frags);

	fw_mon->frags = kcalloc(num_frags, sizeof(*fw_mon->frags), GFP_KERNEL);
	if (!fw_mon->frags)
		return -ENOMEM;

	for (i = 0; i < num_frags; i++) {
		int pages = min_t(u32, frag_pages, remain_pages);

		IWL_DEBUG_FW(fwrt,
			     "WRT: Allocating DRAM buffer (alloc_id=%u, fragment=%u, size=0x%lx)\n",
			     alloc_id, i, pages * PAGE_SIZE);

		pages = iwl_dbg_tlv_alloc_fragment(fwrt, &fw_mon->frags[i],
						   pages);
		if (pages < 0) {
			u32 alloc_size = le32_to_cpu(fw_mon_cfg->req_size) -
				(remain_pages * PAGE_SIZE);

			if (alloc_size < le32_to_cpu(fw_mon_cfg->min_size)) {
				iwl_dbg_tlv_fragments_free(fwrt->trans,
							   alloc_id);
				return pages;
			}
			break;
		}

		remain_pages -= pages;
		fw_mon->num_frags++;
	}

	return 0;
}

static int iwl_dbg_tlv_apply_buffer(struct iwl_fw_runtime *fwrt,
				    enum iwl_fw_ini_allocation_id alloc_id)
{
	struct iwl_fw_mon *fw_mon;
	u32 remain_frags, num_commands;
	int i, fw_mon_idx = 0;

	if (!fw_has_capa(&fwrt->fw->ucode_capa,
			 IWL_UCODE_TLV_CAPA_DBG_BUF_ALLOC_CMD_SUPP))
		return 0;

	if (alloc_id < IWL_FW_INI_ALLOCATION_INVALID ||
	    alloc_id >= IWL_FW_INI_ALLOCATION_NUM)
		return -EIO;

	if (le32_to_cpu(fwrt->trans->dbg.fw_mon_cfg[alloc_id].buf_location) !=
	    IWL_FW_INI_LOCATION_DRAM_PATH)
		return 0;

	fw_mon = &fwrt->trans->dbg.fw_mon_ini[alloc_id];

	/* the first fragment of DBGC1 is given to the FW via register
	 * or context info
	 */
	if (alloc_id == IWL_FW_INI_ALLOCATION_ID_DBGC1)
		fw_mon_idx++;

	remain_frags = fw_mon->num_frags - fw_mon_idx;
	if (!remain_frags)
		return 0;

	num_commands = DIV_ROUND_UP(remain_frags, BUF_ALLOC_MAX_NUM_FRAGS);

	IWL_DEBUG_FW(fwrt, "WRT: Applying DRAM destination (alloc_id=%u)\n",
		     alloc_id);

	for (i = 0; i < num_commands; i++) {
		u32 num_frags = min_t(u32, remain_frags,
				      BUF_ALLOC_MAX_NUM_FRAGS);
		struct iwl_buf_alloc_cmd data = {
			.alloc_id = cpu_to_le32(alloc_id),
			.num_frags = cpu_to_le32(num_frags),
			.buf_location =
				cpu_to_le32(IWL_FW_INI_LOCATION_DRAM_PATH),
		};
		struct iwl_host_cmd hcmd = {
			.id = WIDE_ID(DEBUG_GROUP, BUFFER_ALLOCATION),
			.data[0] = &data,
			.len[0] = sizeof(data),
			.flags = CMD_SEND_IN_RFKILL,
		};
		int ret, j;

		for (j = 0; j < num_frags; j++) {
			struct iwl_buf_alloc_frag *frag = &data.frags[j];
			struct iwl_dram_data *fw_mon_frag =
				&fw_mon->frags[fw_mon_idx++];

			frag->addr = cpu_to_le64(fw_mon_frag->physical);
			frag->size = cpu_to_le32(fw_mon_frag->size);
		}
		ret = iwl_trans_send_cmd(fwrt->trans, &hcmd);
		if (ret)
			return ret;

		remain_frags -= num_frags;
	}

	return 0;
}

static void iwl_dbg_tlv_apply_buffers(struct iwl_fw_runtime *fwrt)
{
	int ret, i;

	if (fw_has_capa(&fwrt->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_DRAM_FRAG_SUPPORT))
		return;

	for (i = 0; i < IWL_FW_INI_ALLOCATION_NUM; i++) {
		ret = iwl_dbg_tlv_apply_buffer(fwrt, i);
		if (ret)
			IWL_WARN(fwrt,
				 "WRT: Failed to apply DRAM buffer for allocation id %d, ret=%d\n",
				 i, ret);
	}
}

static int iwl_dbg_tlv_update_dram(struct iwl_fw_runtime *fwrt,
				   enum iwl_fw_ini_allocation_id alloc_id,
				   struct iwl_dram_info *dram_info)
{
	struct iwl_fw_mon *fw_mon;
	u32 remain_frags, num_frags;
	int j, fw_mon_idx = 0;
	struct iwl_buf_alloc_cmd *data;

	if (le32_to_cpu(fwrt->trans->dbg.fw_mon_cfg[alloc_id].buf_location) !=
			IWL_FW_INI_LOCATION_DRAM_PATH) {
		IWL_DEBUG_FW(fwrt, "DRAM_PATH is not supported alloc_id %u\n", alloc_id);
		return -1;
	}

	fw_mon = &fwrt->trans->dbg.fw_mon_ini[alloc_id];

	/* the first fragment of DBGC1 is given to the FW via register
	 * or context info
	 */
	if (alloc_id == IWL_FW_INI_ALLOCATION_ID_DBGC1)
		fw_mon_idx++;

	remain_frags = fw_mon->num_frags - fw_mon_idx;
	if (!remain_frags)
		return -1;

	num_frags = min_t(u32, remain_frags, BUF_ALLOC_MAX_NUM_FRAGS);
	data = &dram_info->dram_frags[alloc_id - 1];
	data->alloc_id = cpu_to_le32(alloc_id);
	data->num_frags = cpu_to_le32(num_frags);
	data->buf_location = cpu_to_le32(IWL_FW_INI_LOCATION_DRAM_PATH);

	IWL_DEBUG_FW(fwrt, "WRT: DRAM buffer details alloc_id=%u, num_frags=%u\n",
		     cpu_to_le32(alloc_id), cpu_to_le32(num_frags));

	for (j = 0; j < num_frags; j++) {
		struct iwl_buf_alloc_frag *frag = &data->frags[j];
		struct iwl_dram_data *fw_mon_frag = &fw_mon->frags[fw_mon_idx++];

		frag->addr = cpu_to_le64(fw_mon_frag->physical);
		frag->size = cpu_to_le32(fw_mon_frag->size);
		IWL_DEBUG_FW(fwrt, "WRT: DRAM fragment details\n");
		IWL_DEBUG_FW(fwrt, "frag=%u, addr=0x%016llx, size=0x%x)\n",
			     j, cpu_to_le64(fw_mon_frag->physical),
			     cpu_to_le32(fw_mon_frag->size));
	}
	return 0;
}

static void iwl_dbg_tlv_update_drams(struct iwl_fw_runtime *fwrt)
{
	int ret, i, dram_alloc = 0;
	struct iwl_dram_info dram_info;
	struct iwl_dram_data *frags =
		&fwrt->trans->dbg.fw_mon_ini[IWL_FW_INI_ALLOCATION_ID_DBGC1].frags[0];

	if (!fw_has_capa(&fwrt->fw->ucode_capa,
			 IWL_UCODE_TLV_CAPA_DRAM_FRAG_SUPPORT))
		return;

	dram_info.first_word = cpu_to_le32(DRAM_INFO_FIRST_MAGIC_WORD);
	dram_info.second_word = cpu_to_le32(DRAM_INFO_SECOND_MAGIC_WORD);

	for (i = IWL_FW_INI_ALLOCATION_ID_DBGC1;
	     i <= IWL_FW_INI_ALLOCATION_ID_DBGC3; i++) {
		ret = iwl_dbg_tlv_update_dram(fwrt, i, &dram_info);
		if (!ret)
			dram_alloc++;
		else
			IWL_WARN(fwrt,
				 "WRT: Failed to set DRAM buffer for alloc id %d, ret=%d\n",
				 i, ret);
	}
	if (dram_alloc) {
		memcpy(frags->block, &dram_info, sizeof(dram_info));
		IWL_DEBUG_FW(fwrt, "block data after  %016x\n",
			     *((int *)fwrt->trans->dbg.fw_mon_ini[1].frags[0].block));
	}
}

static void iwl_dbg_tlv_send_hcmds(struct iwl_fw_runtime *fwrt,
				   struct list_head *hcmd_list)
{
	struct iwl_dbg_tlv_node *node;

	list_for_each_entry(node, hcmd_list, list) {
		struct iwl_fw_ini_hcmd_tlv *hcmd = (void *)node->tlv.data;
		struct iwl_fw_ini_hcmd *hcmd_data = &hcmd->hcmd;
		u16 hcmd_len = le32_to_cpu(node->tlv.length) - sizeof(*hcmd);
		struct iwl_host_cmd cmd = {
			.id = WIDE_ID(hcmd_data->group, hcmd_data->id),
			.len = { hcmd_len, },
			.data = { hcmd_data->data, },
		};

		iwl_trans_send_cmd(fwrt->trans, &cmd);
	}
}

static void iwl_dbg_tlv_apply_config(struct iwl_fw_runtime *fwrt,
				     struct list_head *config_list)
{
	struct iwl_dbg_tlv_node *node;

	list_for_each_entry(node, config_list, list) {
		struct iwl_fw_ini_conf_set_tlv *config_list = (void *)node->tlv.data;
		u32 count, address, value;
		u32 len = (le32_to_cpu(node->tlv.length) - sizeof(*config_list)) / 8;
		u32 type = le32_to_cpu(config_list->set_type);
		u32 offset = le32_to_cpu(config_list->addr_offset);

		switch (type) {
		case IWL_FW_INI_CONFIG_SET_TYPE_DEVICE_PERIPHERY_MAC: {
			if (!iwl_trans_grab_nic_access(fwrt->trans)) {
				IWL_DEBUG_FW(fwrt, "WRT: failed to get nic access\n");
				IWL_DEBUG_FW(fwrt, "WRT: skipping MAC PERIPHERY config\n");
				continue;
			}
			IWL_DEBUG_FW(fwrt, "WRT:  MAC PERIPHERY config len: len %u\n", len);
			for (count = 0; count < len; count++) {
				address = le32_to_cpu(config_list->addr_val[count].address);
				value = le32_to_cpu(config_list->addr_val[count].value);
				iwl_trans_write_prph(fwrt->trans, address + offset, value);
			}
			iwl_trans_release_nic_access(fwrt->trans);
		break;
		}
		case IWL_FW_INI_CONFIG_SET_TYPE_DEVICE_MEMORY: {
			for (count = 0; count < len; count++) {
				address = le32_to_cpu(config_list->addr_val[count].address);
				value = le32_to_cpu(config_list->addr_val[count].value);
				iwl_trans_write_mem32(fwrt->trans, address + offset, value);
				IWL_DEBUG_FW(fwrt, "WRT: DEV_MEM: count %u, add: %u val: %u\n",
					     count, address, value);
			}
		break;
		}
		case IWL_FW_INI_CONFIG_SET_TYPE_CSR: {
			for (count = 0; count < len; count++) {
				address = le32_to_cpu(config_list->addr_val[count].address);
				value = le32_to_cpu(config_list->addr_val[count].value);
				iwl_write32(fwrt->trans, address + offset, value);
				IWL_DEBUG_FW(fwrt, "WRT: CSR: count %u, add: %u val: %u\n",
					     count, address, value);
			}
		break;
		}
		case IWL_FW_INI_CONFIG_SET_TYPE_DBGC_DRAM_ADDR: {
			struct iwl_dbgc1_info dram_info = {};
			struct iwl_dram_data *frags = &fwrt->trans->dbg.fw_mon_ini[1].frags[0];
			__le64 dram_base_addr = cpu_to_le64(frags->physical);
			__le32 dram_size = cpu_to_le32(frags->size);
			u64  dram_addr = le64_to_cpu(dram_base_addr);
			u32 ret;

			IWL_DEBUG_FW(fwrt, "WRT: dram_base_addr 0x%016llx, dram_size 0x%x\n",
				     dram_base_addr, dram_size);
			IWL_DEBUG_FW(fwrt, "WRT: config_list->addr_offset: %u\n",
				     le32_to_cpu(config_list->addr_offset));
			for (count = 0; count < len; count++) {
				address = le32_to_cpu(config_list->addr_val[count].address);
				dram_info.dbgc1_add_lsb =
					cpu_to_le32((dram_addr & 0x00000000FFFFFFFFULL) + 0x400);
				dram_info.dbgc1_add_msb =
					cpu_to_le32((dram_addr & 0xFFFFFFFF00000000ULL) >> 32);
				dram_info.dbgc1_size = cpu_to_le32(le32_to_cpu(dram_size) - 0x400);
				ret = iwl_trans_write_mem(fwrt->trans,
							  address + offset, &dram_info, 4);
				if (ret) {
					IWL_ERR(fwrt, "Failed to write dram_info to HW_SMEM\n");
					break;
				}
			}
			break;
		}
		case IWL_FW_INI_CONFIG_SET_TYPE_PERIPH_SCRATCH_HWM: {
			u32 debug_token_config =
				le32_to_cpu(config_list->addr_val[0].value);

			IWL_DEBUG_FW(fwrt, "WRT: Setting HWM debug token config: %u\n",
				     debug_token_config);
			fwrt->trans->dbg.ucode_preset = debug_token_config;
			break;
		}
		default:
			break;
		}
	}
}

static void iwl_dbg_tlv_periodic_trig_handler(struct timer_list *t)
{
	struct iwl_dbg_tlv_timer_node *timer_node =
		from_timer(timer_node, t, timer);
	struct iwl_fwrt_dump_data dump_data = {
		.trig = (void *)timer_node->tlv->data,
	};
	int ret;

	ret = iwl_fw_dbg_ini_collect(timer_node->fwrt, &dump_data, false);
	if (!ret || ret == -EBUSY) {
		u32 occur = le32_to_cpu(dump_data.trig->occurrences);
		u32 collect_interval = le32_to_cpu(dump_data.trig->data[0]);

		if (!occur)
			return;

		mod_timer(t, jiffies + msecs_to_jiffies(collect_interval));
	}
}

static void iwl_dbg_tlv_set_periodic_trigs(struct iwl_fw_runtime *fwrt)
{
	struct iwl_dbg_tlv_node *node;
	struct list_head *trig_list =
		&fwrt->trans->dbg.time_point[IWL_FW_INI_TIME_POINT_PERIODIC].active_trig_list;

	list_for_each_entry(node, trig_list, list) {
		struct iwl_fw_ini_trigger_tlv *trig = (void *)node->tlv.data;
		struct iwl_dbg_tlv_timer_node *timer_node;
		u32 occur = le32_to_cpu(trig->occurrences), collect_interval;
		u32 min_interval = 100;

		if (!occur)
			continue;

		/* make sure there is at least one dword of data for the
		 * interval value
		 */
		if (le32_to_cpu(node->tlv.length) <
		    sizeof(*trig) + sizeof(__le32)) {
			IWL_ERR(fwrt,
				"WRT: Invalid periodic trigger data was not given\n");
			continue;
		}

		if (le32_to_cpu(trig->data[0]) < min_interval) {
			IWL_WARN(fwrt,
				 "WRT: Override min interval from %u to %u msec\n",
				 le32_to_cpu(trig->data[0]), min_interval);
			trig->data[0] = cpu_to_le32(min_interval);
		}

		collect_interval = le32_to_cpu(trig->data[0]);

		timer_node = kzalloc(sizeof(*timer_node), GFP_KERNEL);
		if (!timer_node) {
			IWL_ERR(fwrt,
				"WRT: Failed to allocate periodic trigger\n");
			continue;
		}

		timer_node->fwrt = fwrt;
		timer_node->tlv = &node->tlv;
		timer_setup(&timer_node->timer,
			    iwl_dbg_tlv_periodic_trig_handler, 0);

		list_add_tail(&timer_node->list,
			      &fwrt->trans->dbg.periodic_trig_list);

		IWL_DEBUG_FW(fwrt, "WRT: Enabling periodic trigger\n");

		mod_timer(&timer_node->timer,
			  jiffies + msecs_to_jiffies(collect_interval));
	}
}

static bool is_trig_data_contained(const struct iwl_ucode_tlv *new,
				   const struct iwl_ucode_tlv *old)
{
	const struct iwl_fw_ini_trigger_tlv *new_trig = (const void *)new->data;
	const struct iwl_fw_ini_trigger_tlv *old_trig = (const void *)old->data;
	const __le32 *new_data = new_trig->data, *old_data = old_trig->data;
	u32 new_dwords_num = iwl_tlv_array_len(new, new_trig, data);
	u32 old_dwords_num = iwl_tlv_array_len(old, old_trig, data);
	int i, j;

	for (i = 0; i < new_dwords_num; i++) {
		bool match = false;

		for (j = 0; j < old_dwords_num; j++) {
			if (new_data[i] == old_data[j]) {
				match = true;
				break;
			}
		}
		if (!match)
			return false;
	}

	return true;
}

static int iwl_dbg_tlv_override_trig_node(struct iwl_fw_runtime *fwrt,
					  struct iwl_ucode_tlv *trig_tlv,
					  struct iwl_dbg_tlv_node *node)
{
	struct iwl_ucode_tlv *node_tlv = &node->tlv;
	struct iwl_fw_ini_trigger_tlv *node_trig = (void *)node_tlv->data;
	struct iwl_fw_ini_trigger_tlv *trig = (void *)trig_tlv->data;
	u32 policy = le32_to_cpu(trig->apply_policy);
	u32 size = le32_to_cpu(trig_tlv->length);
	u32 trig_data_len = size - sizeof(*trig);
	u32 offset = 0;

	if (!(policy & IWL_FW_INI_APPLY_POLICY_OVERRIDE_DATA)) {
		u32 data_len = le32_to_cpu(node_tlv->length) -
			sizeof(*node_trig);

		IWL_DEBUG_FW(fwrt,
			     "WRT: Appending trigger data (time point %u)\n",
			     le32_to_cpu(trig->time_point));

		offset += data_len;
		size += data_len;
	} else {
		IWL_DEBUG_FW(fwrt,
			     "WRT: Overriding trigger data (time point %u)\n",
			     le32_to_cpu(trig->time_point));
	}

	if (size != le32_to_cpu(node_tlv->length)) {
		struct list_head *prev = node->list.prev;
		struct iwl_dbg_tlv_node *tmp;

		list_del(&node->list);

		tmp = krealloc(node, sizeof(*node) + size, GFP_KERNEL);
		if (!tmp) {
			IWL_WARN(fwrt,
				 "WRT: No memory to override trigger (time point %u)\n",
				 le32_to_cpu(trig->time_point));

			list_add(&node->list, prev);

			return -ENOMEM;
		}

		list_add(&tmp->list, prev);
		node_tlv = &tmp->tlv;
		node_trig = (void *)node_tlv->data;
	}

	memcpy(node_trig->data + offset, trig->data, trig_data_len);
	node_tlv->length = cpu_to_le32(size);

	if (policy & IWL_FW_INI_APPLY_POLICY_OVERRIDE_CFG) {
		IWL_DEBUG_FW(fwrt,
			     "WRT: Overriding trigger configuration (time point %u)\n",
			     le32_to_cpu(trig->time_point));

		/* the first 11 dwords are configuration related */
		memcpy(node_trig, trig, sizeof(__le32) * 11);
	}

	if (policy & IWL_FW_INI_APPLY_POLICY_OVERRIDE_REGIONS) {
		IWL_DEBUG_FW(fwrt,
			     "WRT: Overriding trigger regions (time point %u)\n",
			     le32_to_cpu(trig->time_point));

		node_trig->regions_mask = trig->regions_mask;
	} else {
		IWL_DEBUG_FW(fwrt,
			     "WRT: Appending trigger regions (time point %u)\n",
			     le32_to_cpu(trig->time_point));

		node_trig->regions_mask |= trig->regions_mask;
	}

	return 0;
}

static int
iwl_dbg_tlv_add_active_trigger(struct iwl_fw_runtime *fwrt,
			       struct list_head *trig_list,
			       struct iwl_ucode_tlv *trig_tlv)
{
	struct iwl_fw_ini_trigger_tlv *trig = (void *)trig_tlv->data;
	struct iwl_dbg_tlv_node *node, *match = NULL;
	u32 policy = le32_to_cpu(trig->apply_policy);

	list_for_each_entry(node, trig_list, list) {
		if (!(policy & IWL_FW_INI_APPLY_POLICY_MATCH_TIME_POINT))
			break;

		if (!(policy & IWL_FW_INI_APPLY_POLICY_MATCH_DATA) ||
		    is_trig_data_contained(trig_tlv, &node->tlv)) {
			match = node;
			break;
		}
	}

	if (!match) {
		IWL_DEBUG_FW(fwrt, "WRT: Enabling trigger (time point %u)\n",
			     le32_to_cpu(trig->time_point));
		return iwl_dbg_tlv_add(trig_tlv, trig_list);
	}

	return iwl_dbg_tlv_override_trig_node(fwrt, trig_tlv, match);
}

static void
iwl_dbg_tlv_gen_active_trig_list(struct iwl_fw_runtime *fwrt,
				 struct iwl_dbg_tlv_time_point_data *tp)
{
	struct iwl_dbg_tlv_node *node;
	struct list_head *trig_list = &tp->trig_list;
	struct list_head *active_trig_list = &tp->active_trig_list;

	list_for_each_entry(node, trig_list, list) {
		struct iwl_ucode_tlv *tlv = &node->tlv;

		iwl_dbg_tlv_add_active_trigger(fwrt, active_trig_list, tlv);
	}
}

static bool iwl_dbg_tlv_check_fw_pkt(struct iwl_fw_runtime *fwrt,
				     struct iwl_fwrt_dump_data *dump_data,
				     union iwl_dbg_tlv_tp_data *tp_data,
				     u32 trig_data)
{
	struct iwl_rx_packet *pkt = tp_data->fw_pkt;
	struct iwl_cmd_header *wanted_hdr = (void *)&trig_data;

	if (pkt && (pkt->hdr.cmd == wanted_hdr->cmd &&
		    pkt->hdr.group_id == wanted_hdr->group_id)) {
		struct iwl_rx_packet *fw_pkt =
			kmemdup(pkt,
				sizeof(*pkt) + iwl_rx_packet_payload_len(pkt),
				GFP_ATOMIC);

		if (!fw_pkt)
			return false;

		dump_data->fw_pkt = fw_pkt;

		return true;
	}

	return false;
}

static int
iwl_dbg_tlv_tp_trigger(struct iwl_fw_runtime *fwrt, bool sync,
		       struct list_head *active_trig_list,
		       union iwl_dbg_tlv_tp_data *tp_data,
		       bool (*data_check)(struct iwl_fw_runtime *fwrt,
					  struct iwl_fwrt_dump_data *dump_data,
					  union iwl_dbg_tlv_tp_data *tp_data,
					  u32 trig_data))
{
	struct iwl_dbg_tlv_node *node;

	list_for_each_entry(node, active_trig_list, list) {
		struct iwl_fwrt_dump_data dump_data = {
			.trig = (void *)node->tlv.data,
		};
		u32 num_data = iwl_tlv_array_len(&node->tlv, dump_data.trig,
						 data);
		int ret, i;
		u32 tp = le32_to_cpu(dump_data.trig->time_point);


		if (!num_data) {
			ret = iwl_fw_dbg_ini_collect(fwrt, &dump_data, sync);
			if (ret)
				return ret;
		}

		for (i = 0; i < num_data; i++) {
			if (!data_check ||
			    data_check(fwrt, &dump_data, tp_data,
				       le32_to_cpu(dump_data.trig->data[i]))) {
				ret = iwl_fw_dbg_ini_collect(fwrt, &dump_data, sync);
				if (ret)
					return ret;

				break;
			}
		}

		fwrt->trans->dbg.restart_required = FALSE;
		IWL_DEBUG_INFO(fwrt, "WRT: tp %d, reset_fw %d\n",
			       tp, dump_data.trig->reset_fw);
		IWL_DEBUG_INFO(fwrt, "WRT: restart_required %d, last_tp_resetfw %d\n",
			       fwrt->trans->dbg.restart_required,
			       fwrt->trans->dbg.last_tp_resetfw);

		if (fwrt->trans->trans_cfg->device_family ==
		    IWL_DEVICE_FAMILY_9000) {
			fwrt->trans->dbg.restart_required = TRUE;
		} else if (tp == IWL_FW_INI_TIME_POINT_FW_ASSERT &&
			   fwrt->trans->dbg.last_tp_resetfw ==
			   IWL_FW_INI_RESET_FW_MODE_STOP_FW_ONLY) {
			fwrt->trans->dbg.restart_required = FALSE;
			fwrt->trans->dbg.last_tp_resetfw = 0xFF;
			IWL_DEBUG_FW(fwrt, "WRT: FW_ASSERT due to reset_fw_mode-no restart\n");
		} else if (le32_to_cpu(dump_data.trig->reset_fw) ==
			   IWL_FW_INI_RESET_FW_MODE_STOP_AND_RELOAD_FW) {
			IWL_DEBUG_INFO(fwrt, "WRT: stop and reload firmware\n");
			fwrt->trans->dbg.restart_required = TRUE;
		} else if (le32_to_cpu(dump_data.trig->reset_fw) ==
			   IWL_FW_INI_RESET_FW_MODE_STOP_FW_ONLY) {
			IWL_DEBUG_INFO(fwrt, "WRT: stop only and no reload firmware\n");
			fwrt->trans->dbg.restart_required = FALSE;
			fwrt->trans->dbg.last_tp_resetfw =
				le32_to_cpu(dump_data.trig->reset_fw);
		} else if (le32_to_cpu(dump_data.trig->reset_fw) ==
			   IWL_FW_INI_RESET_FW_MODE_NOTHING) {
			IWL_DEBUG_INFO(fwrt,
				       "WRT: nothing need to be done after debug collection\n");
		} else {
			IWL_ERR(fwrt, "WRT: wrong resetfw %d\n",
				le32_to_cpu(dump_data.trig->reset_fw));
		}
	}
	return 0;
}

static void iwl_dbg_tlv_init_cfg(struct iwl_fw_runtime *fwrt)
{
	enum iwl_fw_ini_buffer_location *ini_dest = &fwrt->trans->dbg.ini_dest;
	int ret, i;
	u32 failed_alloc = 0;

	if (*ini_dest != IWL_FW_INI_LOCATION_INVALID)
		return;

	IWL_DEBUG_FW(fwrt,
		     "WRT: Generating active triggers list, domain 0x%x\n",
		     fwrt->trans->dbg.domains_bitmap);

	for (i = 0; i < ARRAY_SIZE(fwrt->trans->dbg.time_point); i++) {
		struct iwl_dbg_tlv_time_point_data *tp =
			&fwrt->trans->dbg.time_point[i];

		iwl_dbg_tlv_gen_active_trig_list(fwrt, tp);
	}

	*ini_dest = IWL_FW_INI_LOCATION_INVALID;
	for (i = 0; i < IWL_FW_INI_ALLOCATION_NUM; i++) {
		struct iwl_fw_ini_allocation_tlv *fw_mon_cfg =
			&fwrt->trans->dbg.fw_mon_cfg[i];
		u32 dest = le32_to_cpu(fw_mon_cfg->buf_location);

		if (dest == IWL_FW_INI_LOCATION_INVALID) {
			failed_alloc |= BIT(i);
			continue;
		}

		if (*ini_dest == IWL_FW_INI_LOCATION_INVALID)
			*ini_dest = dest;

		if (dest != *ini_dest)
			continue;

		ret = iwl_dbg_tlv_alloc_fragments(fwrt, i);

		if (ret) {
			IWL_WARN(fwrt,
				 "WRT: Failed to allocate DRAM buffer for allocation id %d, ret=%d\n",
				 i, ret);
			failed_alloc |= BIT(i);
		}
	}

	if (!failed_alloc)
		return;

	for (i = 0; i < ARRAY_SIZE(fwrt->trans->dbg.active_regions) && failed_alloc; i++) {
		struct iwl_fw_ini_region_tlv *reg;
		struct iwl_ucode_tlv **active_reg =
			&fwrt->trans->dbg.active_regions[i];
		u32 reg_type;

		if (!*active_reg) {
			fwrt->trans->dbg.unsupported_region_msk |= BIT(i);
			continue;
		}

		reg = (void *)(*active_reg)->data;
		reg_type = reg->type;

		if (reg_type != IWL_FW_INI_REGION_DRAM_BUFFER ||
		    !(BIT(le32_to_cpu(reg->dram_alloc_id)) & failed_alloc))
			continue;

		IWL_DEBUG_FW(fwrt,
			     "WRT: removing allocation id %d from region id %d\n",
			     le32_to_cpu(reg->dram_alloc_id), i);

		failed_alloc &= ~le32_to_cpu(reg->dram_alloc_id);
		fwrt->trans->dbg.unsupported_region_msk |= BIT(i);

		kfree(*active_reg);
		*active_reg = NULL;
	}
}

void _iwl_dbg_tlv_time_point(struct iwl_fw_runtime *fwrt,
			     enum iwl_fw_ini_time_point tp_id,
			     union iwl_dbg_tlv_tp_data *tp_data,
			     bool sync)
{
	struct list_head *hcmd_list, *trig_list, *conf_list;

	if (!iwl_trans_dbg_ini_valid(fwrt->trans) ||
	    tp_id == IWL_FW_INI_TIME_POINT_INVALID ||
	    tp_id >= IWL_FW_INI_TIME_POINT_NUM)
		return;

	hcmd_list = &fwrt->trans->dbg.time_point[tp_id].hcmd_list;
	trig_list = &fwrt->trans->dbg.time_point[tp_id].active_trig_list;
	conf_list = &fwrt->trans->dbg.time_point[tp_id].config_list;

	switch (tp_id) {
	case IWL_FW_INI_TIME_POINT_EARLY:
		iwl_dbg_tlv_init_cfg(fwrt);
		iwl_dbg_tlv_apply_config(fwrt, conf_list);
		iwl_dbg_tlv_update_drams(fwrt);
		iwl_dbg_tlv_tp_trigger(fwrt, sync, trig_list, tp_data, NULL);
		break;
	case IWL_FW_INI_TIME_POINT_AFTER_ALIVE:
		iwl_dbg_tlv_apply_buffers(fwrt);
		iwl_dbg_tlv_send_hcmds(fwrt, hcmd_list);
		iwl_dbg_tlv_apply_config(fwrt, conf_list);
		iwl_dbg_tlv_tp_trigger(fwrt, sync, trig_list, tp_data, NULL);
		break;
	case IWL_FW_INI_TIME_POINT_PERIODIC:
		iwl_dbg_tlv_set_periodic_trigs(fwrt);
		iwl_dbg_tlv_send_hcmds(fwrt, hcmd_list);
		break;
	case IWL_FW_INI_TIME_POINT_FW_RSP_OR_NOTIF:
	case IWL_FW_INI_TIME_POINT_MISSED_BEACONS:
	case IWL_FW_INI_TIME_POINT_FW_DHC_NOTIFICATION:
		iwl_dbg_tlv_send_hcmds(fwrt, hcmd_list);
		iwl_dbg_tlv_apply_config(fwrt, conf_list);
		iwl_dbg_tlv_tp_trigger(fwrt, sync, trig_list, tp_data,
				       iwl_dbg_tlv_check_fw_pkt);
		break;
	default:
		iwl_dbg_tlv_send_hcmds(fwrt, hcmd_list);
		iwl_dbg_tlv_apply_config(fwrt, conf_list);
		iwl_dbg_tlv_tp_trigger(fwrt, sync, trig_list, tp_data, NULL);
		break;
	}
}
IWL_EXPORT_SYMBOL(_iwl_dbg_tlv_time_point);
