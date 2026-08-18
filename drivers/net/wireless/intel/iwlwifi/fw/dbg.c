// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2005-2014, 2018-2026 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2015-2017 Intel Deutschland GmbH
 */
#include <linux/devcoredump.h>
#include "iwl-drv.h"
#include "runtime.h"
#include "dbg.h"
#include "debugfs.h"
#include "iwl-io.h"
#include "iwl-prph.h"
#include "iwl-csr.h"
#include "iwl-fh.h"

/*
 * iwl_fw_dbg_alloc_sgtable - allocates (chained) scatterlist in the given size,
 *	fills it with pages and returns it
 * @size: the size (in bytes) of the table
 */
struct scatterlist *iwl_fw_dbg_alloc_sgtable(ssize_t size)
{
	struct scatterlist *result = NULL, *prev;
	int nents, i, n_prev;

	nents = DIV_ROUND_UP(size, PAGE_SIZE);

#define N_ENTRIES_PER_PAGE (PAGE_SIZE / sizeof(*result))
	/*
	 * We need an additional entry for table chaining,
	 * this ensures the loop can finish i.e. we can
	 * fit at least two entries per page (obviously,
	 * many more really fit.)
	 */
	BUILD_BUG_ON(N_ENTRIES_PER_PAGE < 2);

	while (nents > 0) {
		struct scatterlist *new, *iter;
		int n_fill, n_alloc;

		if (nents <= N_ENTRIES_PER_PAGE) {
			/* last needed table */
			n_fill = nents;
			n_alloc = nents;
			nents = 0;
		} else {
			/* fill a page with entries */
			n_alloc = N_ENTRIES_PER_PAGE;
			/* reserve one for chaining */
			n_fill = n_alloc - 1;
			nents -= n_fill;
		}

		new = kzalloc_objs(*new, n_alloc);
		if (!new) {
			if (result)
				_devcd_free_sgtable(result);
			return NULL;
		}
		sg_init_table(new, n_alloc);

		if (!result)
			result = new;
		else
			sg_chain(prev, n_prev, new);
		prev = new;
		n_prev = n_alloc;

		for_each_sg(new, iter, n_fill, i) {
			struct page *new_page = alloc_page(GFP_KERNEL);

			if (!new_page) {
				_devcd_free_sgtable(result);
				return NULL;
			}

			sg_set_page(iter, new_page, PAGE_SIZE, 0);
		}
	}

	return result;
}

/**
 * struct iwl_dump_ini_region_data - region data
 * @reg_tlv: region TLV
 * @dump_data: dump data
 */
struct iwl_dump_ini_region_data {
	struct iwl_ucode_tlv *reg_tlv;
	struct iwl_fwrt_dump_data *dump_data;
};

static int iwl_dump_ini_prph_mac_iter_common(struct iwl_fw_runtime *fwrt,
					     void *range_ptr, u32 addr,
					     __le32 size)
{
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	__le32 *val = range->data;
	int i;

	range->internal_base_addr = cpu_to_le32(addr);
	range->range_data_size = size;
	for (i = 0; i < le32_to_cpu(size); i += 4)
		*val++ = cpu_to_le32(iwl_trans_read_prph(fwrt->trans, addr + i));

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int
iwl_dump_ini_prph_mac_iter(struct iwl_fw_runtime *fwrt,
			   struct iwl_dump_ini_region_data *reg_data,
			   void *range_ptr, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	u32 addr = le32_to_cpu(reg->addrs[idx]) +
		   le32_to_cpu(reg->dev_addr.offset);

	return iwl_dump_ini_prph_mac_iter_common(fwrt, range_ptr, addr,
						 reg->dev_addr.size);
}

static int
iwl_dump_ini_prph_mac_block_iter(struct iwl_fw_runtime *fwrt,
				 struct iwl_dump_ini_region_data *reg_data,
				 void *range_ptr, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_addr_size *pairs = (void *)reg->addrs;
	u32 addr = le32_to_cpu(reg->dev_addr_range.offset) +
		   le32_to_cpu(pairs[idx].addr);

	return iwl_dump_ini_prph_mac_iter_common(fwrt, range_ptr, addr,
						 pairs[idx].size);
}

static int iwl_dump_ini_prph_phy_iter_common(struct iwl_fw_runtime *fwrt,
					     void *range_ptr, u32 addr,
					     __le32 size, __le32 offset)
{
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	__le32 *val = range->data;
	u32 indirect_wr_addr = WMAL_INDRCT_RD_CMD1;
	u32 indirect_rd_addr = WMAL_MRSPF_1;
	u32 prph_val;
	u32 dphy_state;
	u32 dphy_addr;
	u32 prph_stts;
	int i;

	range->internal_base_addr = cpu_to_le32(addr);
	range->range_data_size = size;

	if (fwrt->trans->mac_cfg->device_family < IWL_DEVICE_FAMILY_AX210)
		indirect_wr_addr = WMAL_INDRCT_CMD1;

	indirect_wr_addr += le32_to_cpu(offset);
	indirect_rd_addr += le32_to_cpu(offset);

	dphy_addr = (offset) ? WFPM_LMAC2_PS_CTL_RW : WFPM_LMAC1_PS_CTL_RW;
	dphy_state = iwl_read_umac_prph_no_grab(fwrt->trans, dphy_addr);

	for (i = 0; i < le32_to_cpu(size); i += 4) {
		if (dphy_state == HBUS_TIMEOUT ||
		    (dphy_state & WFPM_PS_CTL_RW_PHYRF_PD_FSM_CURSTATE_MSK) !=
		    WFPM_PHYRF_STATE_ON) {
			*val++ = cpu_to_le32(WFPM_DPHY_OFF);
			continue;
		}

		iwl_trans_write_prph(fwrt->trans, indirect_wr_addr,
				     WMAL_INDRCT_CMD(addr + i));

		if (fwrt->trans->info.hw_rf_id != IWL_CFG_RF_TYPE_JF1 &&
		    fwrt->trans->info.hw_rf_id != IWL_CFG_RF_TYPE_JF2 &&
		    fwrt->trans->info.hw_rf_id != IWL_CFG_RF_TYPE_HR1 &&
		    fwrt->trans->info.hw_rf_id != IWL_CFG_RF_TYPE_HR2) {
			udelay(2);
			prph_stts = iwl_trans_read_prph(fwrt->trans,
							WMAL_MRSPF_STTS);

			/* Abort dump if status is 0xA5A5A5A2 or FIFO1 empty */
			if (prph_stts == WMAL_TIMEOUT_VAL ||
			    !WMAL_MRSPF_STTS_IS_FIFO1_NOT_EMPTY(prph_stts))
				break;
		}

		prph_val = iwl_trans_read_prph(fwrt->trans,
					       indirect_rd_addr);
		*val++ = cpu_to_le32(prph_val);
	}

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int
iwl_dump_ini_prph_phy_iter(struct iwl_fw_runtime *fwrt,
			   struct iwl_dump_ini_region_data *reg_data,
			   void *range_ptr, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	u32 addr = le32_to_cpu(reg->addrs[idx]);

	return iwl_dump_ini_prph_phy_iter_common(fwrt, range_ptr, addr,
						 reg->dev_addr.size,
						 reg->dev_addr.offset);
}

static int
iwl_dump_ini_prph_phy_block_iter(struct iwl_fw_runtime *fwrt,
				 struct iwl_dump_ini_region_data *reg_data,
				 void *range_ptr, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_addr_size *pairs = (void *)reg->addrs;
	u32 addr = le32_to_cpu(pairs[idx].addr);

	return iwl_dump_ini_prph_phy_iter_common(fwrt, range_ptr, addr,
						 pairs[idx].size,
						 reg->dev_addr_range.offset);
}

static int iwl_dump_ini_csr_iter(struct iwl_fw_runtime *fwrt,
				 struct iwl_dump_ini_region_data *reg_data,
				 void *range_ptr, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	__le32 *val = range->data;
	u32 addr = le32_to_cpu(reg->addrs[idx]) +
		   le32_to_cpu(reg->dev_addr.offset);
	int i;

	range->internal_base_addr = cpu_to_le32(addr);
	range->range_data_size = reg->dev_addr.size;
	for (i = 0; i < le32_to_cpu(reg->dev_addr.size); i += 4)
		*val++ = cpu_to_le32(iwl_trans_read32(fwrt->trans, addr + i));

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int iwl_dump_ini_config_iter(struct iwl_fw_runtime *fwrt,
				    struct iwl_dump_ini_region_data *reg_data,
				    void *range_ptr, int idx)
{
	struct iwl_trans *trans = fwrt->trans;
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	__le32 *val = range->data;
	u32 addr = le32_to_cpu(reg->addrs[idx]) +
		   le32_to_cpu(reg->dev_addr.offset);
	int i;

	range->internal_base_addr = cpu_to_le32(addr);
	range->range_data_size = reg->dev_addr.size;
	for (i = 0; i < le32_to_cpu(reg->dev_addr.size); i += 4) {
		int ret;
		u32 tmp;

		ret = iwl_trans_read_config32(trans, addr + i, &tmp);
		if (ret < 0)
			return ret;

		*val++ = cpu_to_le32(tmp);
	}

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int iwl_dump_ini_dev_mem_iter(struct iwl_fw_runtime *fwrt,
				     struct iwl_dump_ini_region_data *reg_data,
				     void *range_ptr, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	u32 addr = le32_to_cpu(reg->addrs[idx]) +
		   le32_to_cpu(reg->dev_addr.offset);

	range->internal_base_addr = cpu_to_le32(addr);
	range->range_data_size = reg->dev_addr.size;
	iwl_trans_read_mem_bytes_no_grab(fwrt->trans, addr, range->data,
					 le32_to_cpu(reg->dev_addr.size));

	if (reg->sub_type == IWL_FW_INI_REGION_DEVICE_MEMORY_SUBTYPE_HW_SMEM &&
	    fwrt->sanitize_ops && fwrt->sanitize_ops->frob_txf)
		fwrt->sanitize_ops->frob_txf(fwrt->sanitize_ctx,
					     range->data,
					     le32_to_cpu(reg->dev_addr.size));

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int _iwl_dump_ini_paging_iter(struct iwl_fw_runtime *fwrt,
				     void *range_ptr, int idx)
{
	struct page *page = fwrt->fw_paging_db[idx].fw_paging_block;
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	dma_addr_t addr = fwrt->fw_paging_db[idx].fw_paging_phys;
	u32 page_size = fwrt->fw_paging_db[idx].fw_paging_size;

	range->page_num = cpu_to_le32(idx);
	range->range_data_size = cpu_to_le32(page_size);
	dma_sync_single_for_cpu(fwrt->trans->dev, addr,	page_size,
				DMA_BIDIRECTIONAL);
	memcpy(range->data, page_address(page), page_size);
	dma_sync_single_for_device(fwrt->trans->dev, addr, page_size,
				   DMA_BIDIRECTIONAL);

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int iwl_dump_ini_paging_iter(struct iwl_fw_runtime *fwrt,
				    struct iwl_dump_ini_region_data *reg_data,
				    void *range_ptr, int idx)
{
	struct iwl_fw_ini_error_dump_range *range;
	u32 page_size;

	/* all paged index start from 1 to skip CSS section */
	idx++;

	if (!fwrt->trans->mac_cfg->gen2)
		return _iwl_dump_ini_paging_iter(fwrt, range_ptr, idx);

	range = range_ptr;
	page_size = fwrt->trans->init_dram.paging[idx].size;

	range->page_num = cpu_to_le32(idx);
	range->range_data_size = cpu_to_le32(page_size);
	memcpy(range->data, fwrt->trans->init_dram.paging[idx].block,
	       page_size);

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int
iwl_dump_ini_mon_dram_iter(struct iwl_fw_runtime *fwrt,
			   struct iwl_dump_ini_region_data *reg_data,
			   void *range_ptr, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	struct iwl_dram_data *frag;
	u32 alloc_id = le32_to_cpu(reg->dram_alloc_id);

	frag = &fwrt->trans->dbg.fw_mon_ini[alloc_id].frags[idx];

	range->dram_base_addr = cpu_to_le64(frag->physical);
	range->range_data_size = cpu_to_le32(frag->size);

	memcpy(range->data, frag->block, frag->size);

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int iwl_dump_ini_mon_smem_iter(struct iwl_fw_runtime *fwrt,
				      struct iwl_dump_ini_region_data *reg_data,
				      void *range_ptr, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	u32 addr = le32_to_cpu(reg->internal_buffer.base_addr);

	range->internal_base_addr = cpu_to_le32(addr);
	range->range_data_size = reg->internal_buffer.size;
	iwl_trans_read_mem_bytes_no_grab(fwrt->trans, addr, range->data,
					 le32_to_cpu(reg->internal_buffer.size));

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static bool iwl_ini_txf_iter(struct iwl_fw_runtime *fwrt,
			     struct iwl_dump_ini_region_data *reg_data, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_txf_iter_data *iter = &fwrt->dump.txf_iter_data;
	struct iwl_fwrt_shared_mem_cfg *cfg = &fwrt->smem_cfg;
	int txf_num = cfg->num_txfifo_entries;
	int int_txf_num = ARRAY_SIZE(cfg->internal_txfifo_size);
	u32 lmac_bitmap = le32_to_cpu(reg->fifos.fid[0]);

	if (!idx) {
		if (le32_to_cpu(reg->fifos.offset) && cfg->num_lmacs == 1) {
			IWL_ERR(fwrt, "WRT: Invalid lmac offset 0x%x\n",
				le32_to_cpu(reg->fifos.offset));
			return false;
		}

		iter->internal_txf = 0;
		iter->fifo_size = 0;
		iter->fifo = -1;
		if (le32_to_cpu(reg->fifos.offset))
			iter->lmac = 1;
		else
			iter->lmac = 0;
	}

	if (!iter->internal_txf) {
		for (iter->fifo++; iter->fifo < txf_num; iter->fifo++) {
			iter->fifo_size =
				cfg->lmac[iter->lmac].txfifo_size[iter->fifo];
			if (iter->fifo_size && (lmac_bitmap & BIT(iter->fifo)))
				return true;
		}
		iter->fifo--;
	}

	iter->internal_txf = 1;

	if (!fw_has_capa(&fwrt->fw->ucode_capa,
			 IWL_UCODE_TLV_CAPA_EXTEND_SHARED_MEM_CFG))
		return false;

	for (iter->fifo++; iter->fifo < int_txf_num + txf_num; iter->fifo++) {
		iter->fifo_size =
			cfg->internal_txfifo_size[iter->fifo - txf_num];
		if (iter->fifo_size && (lmac_bitmap & BIT(iter->fifo)))
			return true;
	}

	return false;
}

static int iwl_dump_ini_txf_iter(struct iwl_fw_runtime *fwrt,
				 struct iwl_dump_ini_region_data *reg_data,
				 void *range_ptr, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	struct iwl_txf_iter_data *iter = &fwrt->dump.txf_iter_data;
	struct iwl_fw_ini_error_dump_register *reg_dump = (void *)range->data;
	u32 offs = le32_to_cpu(reg->fifos.offset), addr;
	u32 registers_num = iwl_tlv_array_len(reg_data->reg_tlv, reg, addrs);
	u32 registers_size = registers_num * sizeof(*reg_dump);
	__le32 *data;
	int i;

	if (!iwl_ini_txf_iter(fwrt, reg_data, idx))
		return -EIO;

	range->fifo_hdr.fifo_num = cpu_to_le32(iter->fifo);
	range->fifo_hdr.num_of_registers = cpu_to_le32(registers_num);
	range->range_data_size = cpu_to_le32(iter->fifo_size + registers_size);

	iwl_trans_write_prph(fwrt->trans, TXF_LARC_NUM + offs, iter->fifo);

	/*
	 * read txf registers. for each register, write to the dump the
	 * register address and its value
	 */
	for (i = 0; i < registers_num; i++) {
		addr = le32_to_cpu(reg->addrs[i]) + offs;

		reg_dump->addr = cpu_to_le32(addr);
		reg_dump->data = cpu_to_le32(iwl_trans_read_prph(fwrt->trans,
								 addr));

		reg_dump++;
	}

	if (reg->fifos.hdr_only) {
		range->range_data_size = cpu_to_le32(registers_size);
		goto out;
	}

	/* Set the TXF_READ_MODIFY_ADDR to TXF_WR_PTR */
	iwl_trans_write_prph(fwrt->trans, TXF_READ_MODIFY_ADDR + offs,
			     TXF_WR_PTR + offs);

	/* Dummy-read to advance the read pointer to the head */
	iwl_trans_read_prph(fwrt->trans, TXF_READ_MODIFY_DATA + offs);

	/* Read FIFO */
	addr = TXF_READ_MODIFY_DATA + offs;
	data = (void *)reg_dump;
	for (i = 0; i < iter->fifo_size; i += sizeof(*data))
		*data++ = cpu_to_le32(iwl_trans_read_prph(fwrt->trans, addr));

	if (fwrt->sanitize_ops && fwrt->sanitize_ops->frob_txf)
		fwrt->sanitize_ops->frob_txf(fwrt->sanitize_ctx,
					     reg_dump, iter->fifo_size);

out:
	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int
iwl_dump_ini_prph_snps_dphyip_iter(struct iwl_fw_runtime *fwrt,
				   struct iwl_dump_ini_region_data *reg_data,
				   void *range_ptr, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	__le32 *val = range->data;
	__le32 offset = reg->dev_addr.offset;
	u32 indirect_rd_wr_addr = DPHYIP_INDIRECT;
	u32 addr = le32_to_cpu(reg->addrs[idx]);
	u32 dphy_state, dphy_addr, prph_val;
	int i;

	range->internal_base_addr = cpu_to_le32(addr);
	range->range_data_size = reg->dev_addr.size;

	indirect_rd_wr_addr += le32_to_cpu(offset);

	dphy_addr = offset ? WFPM_LMAC2_PS_CTL_RW : WFPM_LMAC1_PS_CTL_RW;
	dphy_state = iwl_read_umac_prph_no_grab(fwrt->trans, dphy_addr);

	for (i = 0; i < le32_to_cpu(reg->dev_addr.size); i += 4) {
		if (dphy_state == HBUS_TIMEOUT ||
		    (dphy_state & WFPM_PS_CTL_RW_PHYRF_PD_FSM_CURSTATE_MSK) !=
		    WFPM_PHYRF_STATE_ON) {
			*val++ = cpu_to_le32(WFPM_DPHY_OFF);
			continue;
		}

		iwl_trans_write_prph(fwrt->trans, indirect_rd_wr_addr,
				     addr + i);
		/* wait a bit for value to be ready in register */
		udelay(1);
		prph_val = iwl_trans_read_prph(fwrt->trans,
					       indirect_rd_wr_addr);
		*val++ = cpu_to_le32((prph_val & DPHYIP_INDIRECT_RD_MSK) >>
				     DPHYIP_INDIRECT_RD_SHIFT);
	}

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

struct iwl_ini_rxf_data {
	u32 fifo_num;
	u32 size;
	u32 offset;
};

static void iwl_ini_get_rxf_data(struct iwl_fw_runtime *fwrt,
				 struct iwl_dump_ini_region_data *reg_data,
				 struct iwl_ini_rxf_data *data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	u32 fid1 = le32_to_cpu(reg->fifos.fid[0]);
	u32 fid2 = le32_to_cpu(reg->fifos.fid[1]);
	u8 fifo_idx;

	if (!data)
		return;

	memset(data, 0, sizeof(*data));

	/* make sure only one bit is set in only one fid */
	if (WARN_ONCE(hweight_long(fid1) + hweight_long(fid2) != 1,
		      "fid1=%x, fid2=%x\n", fid1, fid2))
		return;

	if (fid1) {
		fifo_idx = ffs(fid1) - 1;
		if (WARN_ONCE(fifo_idx >= MAX_NUM_LMAC, "fifo_idx=%d\n",
			      fifo_idx))
			return;

		data->size = fwrt->smem_cfg.lmac[fifo_idx].rxfifo1_size;
		data->fifo_num = fifo_idx;
	} else {
		u8 max_idx;

		fifo_idx = ffs(fid2) - 1;
		if (iwl_fw_lookup_notif_ver(fwrt->fw, SYSTEM_GROUP,
					    SHARED_MEM_CFG_CMD, 0) <= 3)
			max_idx = 0;
		else
			max_idx = 1;

		if (WARN_ONCE(fifo_idx > max_idx,
			      "invalid umac fifo idx %d", fifo_idx))
			return;

		/* use bit 31 to distinguish between umac and lmac rxf while
		 * parsing the dump
		 */
		data->fifo_num = fifo_idx | IWL_RXF_UMAC_BIT;

		switch (fifo_idx) {
		case 0:
			data->size = fwrt->smem_cfg.rxfifo2_size;
			data->offset = iwl_umac_prph(fwrt->trans,
						     RXF_DIFF_FROM_PREV);
			break;
		case 1:
			data->size = fwrt->smem_cfg.rxfifo2_control_size;
			data->offset = iwl_umac_prph(fwrt->trans,
						     RXF2C_DIFF_FROM_PREV);
			break;
		}
	}
}

static int iwl_dump_ini_rxf_iter(struct iwl_fw_runtime *fwrt,
				 struct iwl_dump_ini_region_data *reg_data,
				 void *range_ptr, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	struct iwl_ini_rxf_data rxf_data;
	struct iwl_fw_ini_error_dump_register *reg_dump = (void *)range->data;
	u32 offs = le32_to_cpu(reg->fifos.offset), addr;
	u32 registers_num = iwl_tlv_array_len(reg_data->reg_tlv, reg, addrs);
	u32 registers_size = registers_num * sizeof(*reg_dump);
	__le32 *data;
	int i;

	iwl_ini_get_rxf_data(fwrt, reg_data, &rxf_data);
	if (!rxf_data.size)
		return -EIO;

	range->fifo_hdr.fifo_num = cpu_to_le32(rxf_data.fifo_num);
	range->fifo_hdr.num_of_registers = cpu_to_le32(registers_num);
	range->range_data_size = cpu_to_le32(rxf_data.size + registers_size);

	/*
	 * read rxf registers. for each register, write to the dump the
	 * register address and its value
	 */
	for (i = 0; i < registers_num; i++) {
		addr = le32_to_cpu(reg->addrs[i]) + offs;

		reg_dump->addr = cpu_to_le32(addr);
		reg_dump->data = cpu_to_le32(iwl_trans_read_prph(fwrt->trans,
								 addr));

		reg_dump++;
	}

	if (reg->fifos.hdr_only) {
		range->range_data_size = cpu_to_le32(registers_size);
		goto out;
	}

	offs = rxf_data.offset;

	/* Lock fence */
	iwl_trans_write_prph(fwrt->trans, RXF_SET_FENCE_MODE + offs, 0x1);
	/* Set fence pointer to the same place like WR pointer */
	iwl_trans_write_prph(fwrt->trans, RXF_LD_WR2FENCE + offs, 0x1);
	/* Set fence offset */
	iwl_trans_write_prph(fwrt->trans, RXF_LD_FENCE_OFFSET_ADDR + offs, 0x0);

	/* Read FIFO */
	addr =  RXF_FIFO_RD_FENCE_INC + offs;
	data = (void *)reg_dump;
	for (i = 0; i < rxf_data.size; i += sizeof(*data))
		*data++ = cpu_to_le32(iwl_trans_read_prph(fwrt->trans, addr));

out:
	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int
iwl_dump_ini_err_table_iter(struct iwl_fw_runtime *fwrt,
			    struct iwl_dump_ini_region_data *reg_data,
			    void *range_ptr, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_region_err_table *err_table = &reg->err_table;
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	u32 addr = le32_to_cpu(err_table->base_addr) +
		   le32_to_cpu(err_table->offset);

	range->internal_base_addr = cpu_to_le32(addr);
	range->range_data_size = err_table->size;
	iwl_trans_read_mem_bytes_no_grab(fwrt->trans, addr, range->data,
					 le32_to_cpu(err_table->size));

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int
iwl_dump_ini_special_mem_iter(struct iwl_fw_runtime *fwrt,
			      struct iwl_dump_ini_region_data *reg_data,
			      void *range_ptr, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_region_special_device_memory *special_mem =
		&reg->special_mem;

	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	u32 addr = le32_to_cpu(special_mem->base_addr) +
		   le32_to_cpu(special_mem->offset);

	range->internal_base_addr = cpu_to_le32(addr);
	range->range_data_size = special_mem->size;
	iwl_trans_read_mem_bytes_no_grab(fwrt->trans, addr, range->data,
					 le32_to_cpu(special_mem->size));

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int
iwl_dump_ini_dbgi_sram_iter(struct iwl_fw_runtime *fwrt,
			    struct iwl_dump_ini_region_data *reg_data,
			    void *range_ptr, int idx)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	__le32 *val = range->data;
	u32 prph_data;
	int i;

	range->range_data_size = reg->dev_addr.size;
	for (i = 0; i < (le32_to_cpu(reg->dev_addr.size) / 4); i++) {
		prph_data =
			iwl_trans_read_prph(fwrt->trans,
					    (i % 2) ?
						DBGI_SRAM_TARGET_ACCESS_RDATA_MSB :
						DBGI_SRAM_TARGET_ACCESS_RDATA_LSB);
		if (iwl_trans_is_hw_error_value(prph_data))
			return -EBUSY;
		*val++ = cpu_to_le32(prph_data);
	}

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int iwl_dump_ini_fw_pkt_iter(struct iwl_fw_runtime *fwrt,
				    struct iwl_dump_ini_region_data *reg_data,
				    void *range_ptr, int idx)
{
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	struct iwl_rx_packet *pkt = reg_data->dump_data->fw_pkt;
	u32 pkt_len;

	if (!pkt)
		return -EIO;

	pkt_len = iwl_rx_packet_payload_len(pkt);

	memcpy(&range->fw_pkt_hdr, &pkt->hdr, sizeof(range->fw_pkt_hdr));
	range->range_data_size = cpu_to_le32(pkt_len);

	memcpy(range->data, pkt->data, pkt_len);

	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static int iwl_dump_ini_imr_iter(struct iwl_fw_runtime *fwrt,
				 struct iwl_dump_ini_region_data *reg_data,
				 void *range_ptr, int idx)
{
	/* read the IMR memory and DMA it to SRAM */
	struct iwl_fw_ini_error_dump_range *range = range_ptr;
	u64 imr_curr_addr = fwrt->trans->dbg.imr_data.imr_curr_addr;
	u32 imr_rem_bytes = fwrt->trans->dbg.imr_data.imr2sram_remainbyte;
	u32 sram_addr = fwrt->trans->dbg.imr_data.sram_addr;
	u32 sram_size = fwrt->trans->dbg.imr_data.sram_size;
	u32 size_to_dump = (imr_rem_bytes > sram_size) ? sram_size : imr_rem_bytes;

	range->range_data_size = cpu_to_le32(size_to_dump);
	if (iwl_trans_write_imr_mem(fwrt->trans, sram_addr,
				    imr_curr_addr, size_to_dump)) {
		IWL_ERR(fwrt, "WRT_DEBUG: IMR Memory transfer failed\n");
		return -1;
	}

	fwrt->trans->dbg.imr_data.imr_curr_addr = imr_curr_addr + size_to_dump;
	fwrt->trans->dbg.imr_data.imr2sram_remainbyte -= size_to_dump;

	iwl_trans_read_mem_bytes_no_grab(fwrt->trans, sram_addr, range->data,
					 size_to_dump);
	return sizeof(*range) + le32_to_cpu(range->range_data_size);
}

static void *
iwl_dump_ini_mem_fill_header(struct iwl_fw_runtime *fwrt,
			     struct iwl_dump_ini_region_data *reg_data,
			     void *data, u32 data_len)
{
	struct iwl_fw_ini_error_dump *dump = data;

	dump->header.version = cpu_to_le32(IWL_INI_DUMP_VER);

	return dump->data;
}

/**
 * mask_apply_and_normalize - applies mask on val and normalize the result
 *
 * @val: value
 * @mask: mask to apply and to normalize with
 *
 * The normalization is based on the first set bit in the mask
 *
 * Returns: the extracted value
 */
static u32 mask_apply_and_normalize(u32 val, u32 mask)
{
	return (val & mask) >> (ffs(mask) - 1);
}

static __le32 iwl_get_mon_reg(struct iwl_fw_runtime *fwrt, u32 alloc_id,
			      const struct iwl_fw_mon_reg *reg_info)
{
	u32 val, offs;

	/* The header addresses of DBGCi is calculate as follows:
	 * DBGC1 address + (0x100 * i)
	 */
	offs = (alloc_id - IWL_FW_INI_ALLOCATION_ID_DBGC1) * 0x100;

	if (!reg_info || !reg_info->addr || !reg_info->mask)
		return 0;

	val = iwl_trans_read_prph(fwrt->trans, reg_info->addr + offs);

	return cpu_to_le32(mask_apply_and_normalize(val, reg_info->mask));
}

static void *
iwl_dump_ini_mon_fill_header(struct iwl_fw_runtime *fwrt, u32 alloc_id,
			     struct iwl_fw_ini_monitor_dump *data,
			     const struct iwl_fw_mon_regs *addrs)
{
	data->write_ptr = iwl_get_mon_reg(fwrt, alloc_id,
					  &addrs->write_ptr);
	if (fwrt->trans->mac_cfg->device_family >= IWL_DEVICE_FAMILY_AX210) {
		u32 wrt_ptr = le32_to_cpu(data->write_ptr);

		data->write_ptr = cpu_to_le32(wrt_ptr >> 2);
	}
	data->cycle_cnt = iwl_get_mon_reg(fwrt, alloc_id,
					  &addrs->cycle_cnt);
	data->cur_frag = iwl_get_mon_reg(fwrt, alloc_id,
					 &addrs->cur_frag);

	data->header.version = cpu_to_le32(IWL_INI_DUMP_VER);

	return data->data;
}

static void *
iwl_dump_ini_mon_dram_fill_header(struct iwl_fw_runtime *fwrt,
				  struct iwl_dump_ini_region_data *reg_data,
				  void *data, u32 data_len)
{
	struct iwl_fw_ini_monitor_dump *mon_dump = (void *)data;
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	u32 alloc_id = le32_to_cpu(reg->dram_alloc_id);

	return iwl_dump_ini_mon_fill_header(fwrt, alloc_id, mon_dump,
					    &fwrt->trans->mac_cfg->base->mon_dram_regs);
}

static void *
iwl_dump_ini_mon_smem_fill_header(struct iwl_fw_runtime *fwrt,
				  struct iwl_dump_ini_region_data *reg_data,
				  void *data, u32 data_len)
{
	struct iwl_fw_ini_monitor_dump *mon_dump = (void *)data;
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	u32 alloc_id = le32_to_cpu(reg->internal_buffer.alloc_id);

	return iwl_dump_ini_mon_fill_header(fwrt, alloc_id, mon_dump,
					    &fwrt->trans->mac_cfg->base->mon_smem_regs);
}

static void *
iwl_dump_ini_mon_dbgi_fill_header(struct iwl_fw_runtime *fwrt,
				  struct iwl_dump_ini_region_data *reg_data,
				  void *data, u32 data_len)
{
	struct iwl_fw_ini_monitor_dump *mon_dump = (void *)data;

	return iwl_dump_ini_mon_fill_header(fwrt,
					    /* no offset calculation later */
					    IWL_FW_INI_ALLOCATION_ID_DBGC1,
					    mon_dump,
					    &fwrt->trans->mac_cfg->base->mon_dbgi_regs);
}

static void *
iwl_dump_ini_err_table_fill_header(struct iwl_fw_runtime *fwrt,
				   struct iwl_dump_ini_region_data *reg_data,
				   void *data, u32 data_len)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_err_table_dump *dump = data;

	dump->header.version = cpu_to_le32(IWL_INI_DUMP_VER);
	dump->version = reg->err_table.version;

	return dump->data;
}

static void *
iwl_dump_ini_special_mem_fill_header(struct iwl_fw_runtime *fwrt,
				     struct iwl_dump_ini_region_data *reg_data,
				     void *data, u32 data_len)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_special_device_memory *dump = data;

	dump->header.version = cpu_to_le32(IWL_INI_DUMP_VER);
	dump->type = reg->special_mem.type;
	dump->version = reg->special_mem.version;

	return dump->data;
}

static void *
iwl_dump_ini_imr_fill_header(struct iwl_fw_runtime *fwrt,
			     struct iwl_dump_ini_region_data *reg_data,
			     void *data, u32 data_len)
{
	struct iwl_fw_ini_error_dump *dump = data;

	dump->header.version = cpu_to_le32(IWL_INI_DUMP_VER);

	return dump->data;
}

static u32 iwl_dump_ini_mem_ranges(struct iwl_fw_runtime *fwrt,
				   struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;

	return iwl_tlv_array_len(reg_data->reg_tlv, reg, addrs);
}

static u32
iwl_dump_ini_mem_block_ranges(struct iwl_fw_runtime *fwrt,
			      struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	size_t size = sizeof(struct iwl_fw_ini_addr_size);

	return iwl_tlv_array_len_with_size(reg_data->reg_tlv, reg, size);
}

static u32 iwl_dump_ini_paging_ranges(struct iwl_fw_runtime *fwrt,
				      struct iwl_dump_ini_region_data *reg_data)
{
	if (fwrt->trans->mac_cfg->gen2) {
		if (fwrt->trans->init_dram.paging_cnt)
			return fwrt->trans->init_dram.paging_cnt - 1;
		else
			return 0;
	}

	return fwrt->num_of_paging_blk;
}

static u32
iwl_dump_ini_mon_dram_ranges(struct iwl_fw_runtime *fwrt,
			     struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_mon *fw_mon;
	u32 ranges = 0, alloc_id = le32_to_cpu(reg->dram_alloc_id);
	int i;

	fw_mon = &fwrt->trans->dbg.fw_mon_ini[alloc_id];

	for (i = 0; i < fw_mon->num_frags; i++) {
		if (!fw_mon->frags[i].size)
			break;

		ranges++;
	}

	return ranges;
}

static u32 iwl_dump_ini_txf_ranges(struct iwl_fw_runtime *fwrt,
				   struct iwl_dump_ini_region_data *reg_data)
{
	u32 num_of_fifos = 0;

	while (iwl_ini_txf_iter(fwrt, reg_data, num_of_fifos))
		num_of_fifos++;

	return num_of_fifos;
}

static u32 iwl_dump_ini_single_range(struct iwl_fw_runtime *fwrt,
				     struct iwl_dump_ini_region_data *reg_data)
{
	return 1;
}

static u32 iwl_dump_ini_imr_ranges(struct iwl_fw_runtime *fwrt,
				   struct iwl_dump_ini_region_data *reg_data)
{
	/* range is total number of pages need to copied from
	 *IMR memory to SRAM and later from SRAM to DRAM
	 */
	u32 imr_enable = fwrt->trans->dbg.imr_data.imr_enable;
	u32 imr_size = fwrt->trans->dbg.imr_data.imr_size;
	u32 sram_size = fwrt->trans->dbg.imr_data.sram_size;

	if (imr_enable == 0 || imr_size == 0 || sram_size == 0) {
		IWL_DEBUG_INFO(fwrt,
			       "WRT: Invalid imr data enable: %d, imr_size: %d, sram_size: %d\n",
			       imr_enable, imr_size, sram_size);
		return 0;
	}

	return((imr_size % sram_size) ? (imr_size / sram_size + 1) : (imr_size / sram_size));
}

static u32 iwl_dump_ini_mem_get_size(struct iwl_fw_runtime *fwrt,
				     struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	u32 size = le32_to_cpu(reg->dev_addr.size);
	u32 ranges = iwl_dump_ini_mem_ranges(fwrt, reg_data);

	if (!size || !ranges)
		return 0;

	return sizeof(struct iwl_fw_ini_error_dump) + ranges *
		(size + sizeof(struct iwl_fw_ini_error_dump_range));
}

static u32
iwl_dump_ini_mem_block_get_size(struct iwl_fw_runtime *fwrt,
				struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_addr_size *pairs = (void *)reg->addrs;
	u32 ranges = iwl_dump_ini_mem_block_ranges(fwrt, reg_data);
	u32 size = sizeof(struct iwl_fw_ini_error_dump);
	int range;

	if (!ranges)
		return 0;

	for (range = 0; range < ranges; range++)
		size += le32_to_cpu(pairs[range].size);

	return size + ranges * sizeof(struct iwl_fw_ini_error_dump_range);
}

static u32
iwl_dump_ini_paging_get_size(struct iwl_fw_runtime *fwrt,
			     struct iwl_dump_ini_region_data *reg_data)
{
	int i;
	u32 range_header_len = sizeof(struct iwl_fw_ini_error_dump_range);
	u32 size = sizeof(struct iwl_fw_ini_error_dump);

	/* start from 1 to skip CSS section */
	for (i = 1; i <= iwl_dump_ini_paging_ranges(fwrt, reg_data); i++) {
		size += range_header_len;
		if (fwrt->trans->mac_cfg->gen2)
			size += fwrt->trans->init_dram.paging[i].size;
		else
			size += fwrt->fw_paging_db[i].fw_paging_size;
	}

	return size;
}

static u32
iwl_dump_ini_mon_dram_get_size(struct iwl_fw_runtime *fwrt,
			       struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_mon *fw_mon;
	u32 size = 0, alloc_id = le32_to_cpu(reg->dram_alloc_id);
	int i;

	fw_mon = &fwrt->trans->dbg.fw_mon_ini[alloc_id];

	for (i = 0; i < fw_mon->num_frags; i++) {
		struct iwl_dram_data *frag = &fw_mon->frags[i];

		if (!frag->size)
			break;

		size += sizeof(struct iwl_fw_ini_error_dump_range) + frag->size;
	}

	if (size)
		size += sizeof(struct iwl_fw_ini_monitor_dump);

	return size;
}

static u32
iwl_dump_ini_mon_smem_get_size(struct iwl_fw_runtime *fwrt,
			       struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	u32 size;

	size = le32_to_cpu(reg->internal_buffer.size);
	if (!size)
		return 0;

	size += sizeof(struct iwl_fw_ini_monitor_dump) +
		sizeof(struct iwl_fw_ini_error_dump_range);

	return size;
}

static u32 iwl_dump_ini_mon_dbgi_get_size(struct iwl_fw_runtime *fwrt,
					  struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	u32 size = le32_to_cpu(reg->dev_addr.size);
	u32 ranges = iwl_dump_ini_mem_ranges(fwrt, reg_data);

	if (!size || !ranges)
		return 0;

	return sizeof(struct iwl_fw_ini_monitor_dump) + ranges *
		(size + sizeof(struct iwl_fw_ini_error_dump_range));
}

static u32 iwl_dump_ini_txf_get_size(struct iwl_fw_runtime *fwrt,
				     struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_txf_iter_data *iter = &fwrt->dump.txf_iter_data;
	u32 registers_num = iwl_tlv_array_len(reg_data->reg_tlv, reg, addrs);
	u32 size = 0;
	u32 fifo_hdr = sizeof(struct iwl_fw_ini_error_dump_range) +
		       registers_num *
		       sizeof(struct iwl_fw_ini_error_dump_register);

	while (iwl_ini_txf_iter(fwrt, reg_data, size)) {
		size += fifo_hdr;
		if (!reg->fifos.hdr_only)
			size += iter->fifo_size;
	}

	if (!size)
		return 0;

	return size + sizeof(struct iwl_fw_ini_error_dump);
}

static u32 iwl_dump_ini_rxf_get_size(struct iwl_fw_runtime *fwrt,
				     struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_ini_rxf_data rx_data;
	u32 registers_num = iwl_tlv_array_len(reg_data->reg_tlv, reg, addrs);
	u32 size = sizeof(struct iwl_fw_ini_error_dump) +
		sizeof(struct iwl_fw_ini_error_dump_range) +
		registers_num * sizeof(struct iwl_fw_ini_error_dump_register);

	if (reg->fifos.hdr_only)
		return size;

	iwl_ini_get_rxf_data(fwrt, reg_data, &rx_data);
	size += rx_data.size;

	return size;
}

static u32
iwl_dump_ini_err_table_get_size(struct iwl_fw_runtime *fwrt,
				struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	u32 size = le32_to_cpu(reg->err_table.size);

	if (size)
		size += sizeof(struct iwl_fw_ini_err_table_dump) +
			sizeof(struct iwl_fw_ini_error_dump_range);

	return size;
}

static u32
iwl_dump_ini_special_mem_get_size(struct iwl_fw_runtime *fwrt,
				  struct iwl_dump_ini_region_data *reg_data)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	u32 size = le32_to_cpu(reg->special_mem.size);

	if (size)
		size += sizeof(struct iwl_fw_ini_special_device_memory) +
			sizeof(struct iwl_fw_ini_error_dump_range);

	return size;
}

static u32
iwl_dump_ini_fw_pkt_get_size(struct iwl_fw_runtime *fwrt,
			     struct iwl_dump_ini_region_data *reg_data)
{
	u32 size = 0;

	if (!reg_data->dump_data->fw_pkt)
		return 0;

	size += iwl_rx_packet_payload_len(reg_data->dump_data->fw_pkt);
	if (size)
		size += sizeof(struct iwl_fw_ini_error_dump) +
			sizeof(struct iwl_fw_ini_error_dump_range);

	return size;
}

static u32
iwl_dump_ini_imr_get_size(struct iwl_fw_runtime *fwrt,
			  struct iwl_dump_ini_region_data *reg_data)
{
	u32 ranges = 0;
	u32 imr_enable = fwrt->trans->dbg.imr_data.imr_enable;
	u32 imr_size = fwrt->trans->dbg.imr_data.imr_size;
	u32 sram_size = fwrt->trans->dbg.imr_data.sram_size;

	if (imr_enable == 0 || imr_size == 0 || sram_size == 0) {
		IWL_DEBUG_INFO(fwrt,
			       "WRT: Invalid imr data enable: %d, imr_size: %d, sram_size: %d\n",
			       imr_enable, imr_size, sram_size);
		return 0;
	}
	ranges = iwl_dump_ini_imr_ranges(fwrt, reg_data);
	if (!ranges) {
		IWL_ERR(fwrt, "WRT: ranges :=%d\n", ranges);
		return 0;
	}
	imr_size += sizeof(struct iwl_fw_ini_error_dump) +
		ranges * sizeof(struct iwl_fw_ini_error_dump_range);
	return imr_size;
}

/**
 * struct iwl_dump_ini_mem_ops - ini memory dump operations
 * @get_num_of_ranges: returns the number of memory ranges in the region.
 * @get_size: returns the total size of the region.
 * @fill_mem_hdr: fills region type specific headers and returns pointer to
 *	the first range or NULL if failed to fill headers.
 * @fill_range: copies a given memory range into the dump.
 *	Returns the size of the range or negative error value otherwise.
 * @requires_nic_access: indicates to dump only if NIC access was acquired
 */
struct iwl_dump_ini_mem_ops {
	u32 (*get_num_of_ranges)(struct iwl_fw_runtime *fwrt,
				 struct iwl_dump_ini_region_data *reg_data);
	u32 (*get_size)(struct iwl_fw_runtime *fwrt,
			struct iwl_dump_ini_region_data *reg_data);
	void *(*fill_mem_hdr)(struct iwl_fw_runtime *fwrt,
			      struct iwl_dump_ini_region_data *reg_data,
			      void *data, u32 data_len);
	int (*fill_range)(struct iwl_fw_runtime *fwrt,
			  struct iwl_dump_ini_region_data *reg_data,
			  void *range, int idx);
	bool requires_nic_access;
};

struct iwl_fw_ini_dump_entry {
	const struct iwl_dump_ini_mem_ops *ops;
	struct iwl_dump_ini_region_data reg_data;
	struct list_head list;
	u32 region_dump_policy;
	u32 size;
	u8 data[];
} __packed;

static void iwl_dump_ini_mem_prep(struct iwl_fw_runtime *fwrt,
				  struct list_head *list,
				  struct iwl_dump_ini_region_data *reg_data,
				  const struct iwl_dump_ini_mem_ops *ops)
{
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	struct iwl_fw_ini_dump_entry *entry;
	struct iwl_fw_ini_error_dump_data *tlv;
	struct iwl_fw_ini_error_dump_header *header;
	u32 type = reg->type;
	u32 id = le32_get_bits(reg->id, IWL_FW_INI_REGION_ID_MASK);
	u32 num_of_ranges, size;
	u32 dump_policy = IWL_FW_INI_DUMP_VERBOSE;
	u32 dp;

	IWL_DEBUG_FW(fwrt, "WRT: Collecting region: dump type=%d, id=%d, type=%d\n",
		     dump_policy, id, type);

	if (le32_to_cpu(reg->hdr.version) >= 2) {
		dp = le32_get_bits(reg->id, IWL_FW_INI_REGION_DUMP_POLICY_MASK);

		if (dump_policy == IWL_FW_INI_DUMP_VERBOSE &&
		    !(dp & IWL_FW_INI_DEBUG_DUMP_POLICY_NO_LIMIT)) {
			IWL_DEBUG_FW(fwrt,
				     "WRT: no dump - type %d and policy mismatch=%d\n",
				     dump_policy, dp);
			return;
		} else if (dump_policy == IWL_FW_INI_DUMP_MEDIUM &&
			   !(dp & IWL_FW_IWL_DEBUG_DUMP_POLICY_MAX_LIMIT_5MB)) {
			IWL_DEBUG_FW(fwrt,
				     "WRT: no dump - type %d and policy mismatch=%d\n",
				     dump_policy, dp);
			return;
		} else if (dump_policy == IWL_FW_INI_DUMP_BRIEF &&
			   !(dp & IWL_FW_INI_DEBUG_DUMP_POLICY_MAX_LIMIT_600KB)) {
			IWL_DEBUG_FW(fwrt,
				     "WRT: no dump - type %d and policy mismatch=%d\n",
				     dump_policy, dp);
			return;
		}
	} else {
		dp = 0;
	}

	if (!ops->get_num_of_ranges || !ops->get_size || !ops->fill_mem_hdr ||
	    !ops->fill_range) {
		IWL_DEBUG_FW(fwrt, "WRT: no ops for collecting data\n");
		return;
	}

	size = ops->get_size(fwrt, reg_data);

	if (size < sizeof(*header)) {
		IWL_DEBUG_FW(fwrt, "WRT: size didn't include space for header\n");
		return;
	}

	entry = vzalloc(sizeof(*entry) + sizeof(*tlv) + size);
	if (!entry)
		return;

	entry->size = sizeof(*tlv) + size;
	entry->reg_data = *reg_data;
	entry->region_dump_policy = dp;

	tlv = (void *)entry->data;
	tlv->type = reg->type;
	tlv->sub_type = reg->sub_type;
	tlv->sub_type_ver = reg->sub_type_ver;
	tlv->reserved = reg->reserved;
	tlv->len = cpu_to_le32(size);

	num_of_ranges = ops->get_num_of_ranges(fwrt, reg_data);

	header = (void *)tlv->data;
	header->region_id = cpu_to_le32(id);
	header->num_of_ranges = cpu_to_le32(num_of_ranges);
	header->name_len = cpu_to_le32(IWL_FW_INI_MAX_NAME);
	memcpy(header->name, reg->name, IWL_FW_INI_MAX_NAME);

	entry->ops = ops;
	list_add_tail(&entry->list, list);
}

static u32 iwl_dump_ini_mem(struct iwl_fw_runtime *fwrt,
			    struct iwl_fw_ini_dump_entry *entry,
			    bool have_nic_access)
{
	struct iwl_dump_ini_region_data *reg_data = &entry->reg_data;
	struct iwl_fw_ini_region_tlv *reg = (void *)reg_data->reg_tlv->data;
	const struct iwl_dump_ini_mem_ops *ops = entry->ops;
	struct iwl_fw_ini_error_dump_data *tlv;
	struct iwl_fw_ini_error_dump_header *header;
	u32 type = reg->type;
	u32 id = le32_get_bits(reg->id, IWL_FW_INI_REGION_ID_MASK);
	u32 i;
	u8 *range;
	u32 free_size;
	u64 header_size;

	if (!have_nic_access && ops->requires_nic_access)
		goto out_err;

	tlv = (void *)entry->data;
	header = (void *)tlv->data;

	free_size = entry->size - sizeof(*tlv);
	range = ops->fill_mem_hdr(fwrt, reg_data, header, free_size);
	if (!range) {
		IWL_ERR(fwrt,
			"WRT: Failed to fill region header: id=%d, type=%d\n",
			id, type);
		goto out_err;
	}

	header_size = range - (u8 *)header;

	if (WARN(header_size > free_size,
		 "header size %llu > free_size %d",
		 header_size, free_size)) {
		IWL_ERR(fwrt,
			"WRT: fill_mem_hdr used more than given free_size\n");
		goto out_err;
	}

	free_size -= header_size;

	for (i = 0; i < le32_to_cpu(header->num_of_ranges); i++) {
		int range_size = ops->fill_range(fwrt, reg_data, range, i);

		if (range_size < 0) {
			IWL_ERR(fwrt,
				"WRT: Failed to dump region: id=%d, type=%d\n",
				id, type);
			goto out_err;
		}

		if (WARN(range_size > free_size, "range_size %d > free_size %d",
			 range_size, free_size)) {
			IWL_ERR(fwrt,
				"WRT: fill_raged used more than given free_size\n");
			goto out_err;
		}

		free_size -= range_size;
		range = range + range_size;
	}

	return entry->size;

out_err:
	list_del(&entry->list);
	vfree(entry);

	return 0;
}

static u32 iwl_dump_ini_info(struct iwl_fw_runtime *fwrt,
			     struct iwl_fw_ini_trigger_tlv *trigger,
			     struct list_head *list)
{
	struct iwl_fw_ini_dump_entry *entry;
	struct iwl_fw_error_dump_data *tlv;
	struct iwl_fw_ini_dump_info *dump;
	struct iwl_dbg_tlv_node *node;
	struct iwl_fw_ini_dump_cfg_name *cfg_name;
	u32 size = sizeof(*tlv) + sizeof(*dump);
	u32 num_of_cfg_names = 0;
	u32 hw_type, is_cdb;

	list_for_each_entry(node, &fwrt->trans->dbg.debug_info_tlv_list, list) {
		size += sizeof(*cfg_name);
		num_of_cfg_names++;
	}

	entry = vzalloc(sizeof(*entry) + size);
	if (!entry)
		return 0;

	entry->size = size;

	tlv = (void *)entry->data;
	tlv->type = cpu_to_le32(IWL_INI_DUMP_INFO_TYPE);
	tlv->len = cpu_to_le32(size - sizeof(*tlv));

	dump = (void *)tlv->data;

	dump->version = cpu_to_le32(IWL_INI_DUMP_VER);
	dump->time_point = trigger->time_point;
	dump->trigger_reason = trigger->trigger_reason;
	dump->external_cfg_state =
		cpu_to_le32(fwrt->trans->dbg.external_ini_cfg);

	dump->ver_type = cpu_to_le32(fwrt->dump.fw_ver.type);
	dump->ver_subtype = cpu_to_le32(fwrt->dump.fw_ver.subtype);

	dump->hw_step = cpu_to_le32(fwrt->trans->info.hw_rev_step);

	hw_type = CSR_HW_REV_TYPE(fwrt->trans->info.hw_rev);

	is_cdb = CSR_HW_RFID_IS_CDB(fwrt->trans->info.hw_rf_id);
	hw_type |= IWL_CDB_MASK(is_cdb);

	dump->hw_type = cpu_to_le32(hw_type);

	dump->rf_id_flavor =
		cpu_to_le32(CSR_HW_RFID_FLAVOR(fwrt->trans->info.hw_rf_id));
	dump->rf_id_dash = cpu_to_le32(CSR_HW_RFID_DASH(fwrt->trans->info.hw_rf_id));
	dump->rf_id_step = cpu_to_le32(CSR_HW_RFID_STEP(fwrt->trans->info.hw_rf_id));
	dump->rf_id_type = cpu_to_le32(CSR_HW_RFID_TYPE(fwrt->trans->info.hw_rf_id));

	dump->lmac_major = cpu_to_le32(fwrt->dump.fw_ver.lmac_major);
	dump->lmac_minor = cpu_to_le32(fwrt->dump.fw_ver.lmac_minor);
	dump->umac_major = cpu_to_le32(fwrt->dump.fw_ver.umac_major);
	dump->umac_minor = cpu_to_le32(fwrt->dump.fw_ver.umac_minor);

	dump->fw_mon_mode = cpu_to_le32(fwrt->trans->dbg.ini_dest);
	dump->regions_mask = trigger->regions_mask &
			     ~cpu_to_le64(fwrt->trans->dbg.unsupported_region_msk);

	dump->build_tag_len = cpu_to_le32(sizeof(dump->build_tag));
	memcpy(dump->build_tag, fwrt->fw->human_readable,
	       sizeof(dump->build_tag));

	cfg_name = dump->cfg_names;
	dump->num_of_cfg_names = cpu_to_le32(num_of_cfg_names);
	list_for_each_entry(node, &fwrt->trans->dbg.debug_info_tlv_list, list) {
		struct iwl_fw_ini_debug_info_tlv *debug_info =
			(void *)node->tlv.data;

		BUILD_BUG_ON(sizeof(cfg_name->cfg_name) !=
			     sizeof(debug_info->debug_cfg_name));

		cfg_name->image_type = debug_info->image_type;
		cfg_name->cfg_name_len =
			cpu_to_le32(sizeof(cfg_name->cfg_name));
		memcpy(cfg_name->cfg_name, debug_info->debug_cfg_name,
		       sizeof(cfg_name->cfg_name));
		cfg_name++;
	}

	/* add dump info TLV to the beginning of the list since it needs to be
	 * the first TLV in the dump
	 */
	list_add(&entry->list, list);

	return entry->size;
}

static const struct iwl_dump_ini_mem_ops iwl_dump_ini_region_ops[] = {
	[IWL_FW_INI_REGION_INVALID] = {},
	[IWL_FW_INI_REGION_INTERNAL_BUFFER] = {
		.get_num_of_ranges = iwl_dump_ini_single_range,
		.get_size = iwl_dump_ini_mon_smem_get_size,
		.fill_mem_hdr = iwl_dump_ini_mon_smem_fill_header,
		.fill_range = iwl_dump_ini_mon_smem_iter,
		.requires_nic_access = true,
	},
	[IWL_FW_INI_REGION_DRAM_BUFFER] = {
		.get_num_of_ranges = iwl_dump_ini_mon_dram_ranges,
		.get_size = iwl_dump_ini_mon_dram_get_size,
		.fill_mem_hdr = iwl_dump_ini_mon_dram_fill_header,
		.fill_range = iwl_dump_ini_mon_dram_iter,
		.requires_nic_access = true,
	},
	[IWL_FW_INI_REGION_TXF] = {
		.get_num_of_ranges = iwl_dump_ini_txf_ranges,
		.get_size = iwl_dump_ini_txf_get_size,
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.fill_range = iwl_dump_ini_txf_iter,
		.requires_nic_access = true,
	},
	[IWL_FW_INI_REGION_RXF] = {
		.get_num_of_ranges = iwl_dump_ini_single_range,
		.get_size = iwl_dump_ini_rxf_get_size,
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.fill_range = iwl_dump_ini_rxf_iter,
		.requires_nic_access = true,
	},
	[IWL_FW_INI_REGION_LMAC_ERROR_TABLE] = {
		.get_num_of_ranges = iwl_dump_ini_single_range,
		.get_size = iwl_dump_ini_err_table_get_size,
		.fill_mem_hdr = iwl_dump_ini_err_table_fill_header,
		.fill_range = iwl_dump_ini_err_table_iter,
		.requires_nic_access = true,
	},
	[IWL_FW_INI_REGION_UMAC_ERROR_TABLE] = {
		.get_num_of_ranges = iwl_dump_ini_single_range,
		.get_size = iwl_dump_ini_err_table_get_size,
		.fill_mem_hdr = iwl_dump_ini_err_table_fill_header,
		.fill_range = iwl_dump_ini_err_table_iter,
		.requires_nic_access = true,
	},
	[IWL_FW_INI_REGION_RSP_OR_NOTIF] = {
		.get_num_of_ranges = iwl_dump_ini_single_range,
		.get_size = iwl_dump_ini_fw_pkt_get_size,
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.fill_range = iwl_dump_ini_fw_pkt_iter,
	},
	[IWL_FW_INI_REGION_DEVICE_MEMORY] = {
		.get_num_of_ranges = iwl_dump_ini_mem_ranges,
		.get_size = iwl_dump_ini_mem_get_size,
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.fill_range = iwl_dump_ini_dev_mem_iter,
		.requires_nic_access = true,
	},
	[IWL_FW_INI_REGION_PERIPHERY_MAC] = {
		.get_num_of_ranges = iwl_dump_ini_mem_ranges,
		.get_size = iwl_dump_ini_mem_get_size,
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.fill_range = iwl_dump_ini_prph_mac_iter,
		.requires_nic_access = true,
	},
	[IWL_FW_INI_REGION_PERIPHERY_PHY] = {
		.get_num_of_ranges = iwl_dump_ini_mem_ranges,
		.get_size = iwl_dump_ini_mem_get_size,
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.fill_range = iwl_dump_ini_prph_phy_iter,
		.requires_nic_access = true,
	},
	[IWL_FW_INI_REGION_PERIPHERY_MAC_RANGE] = {
		.get_num_of_ranges = iwl_dump_ini_mem_block_ranges,
		.get_size = iwl_dump_ini_mem_block_get_size,
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.fill_range = iwl_dump_ini_prph_mac_block_iter,
		.requires_nic_access = true,
	},
	[IWL_FW_INI_REGION_PERIPHERY_PHY_RANGE] = {
		.get_num_of_ranges = iwl_dump_ini_mem_block_ranges,
		.get_size = iwl_dump_ini_mem_block_get_size,
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.fill_range = iwl_dump_ini_prph_phy_block_iter,
		.requires_nic_access = true,
	},
	[IWL_FW_INI_REGION_PERIPHERY_AUX] = {},
	[IWL_FW_INI_REGION_PAGING] = {
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.get_num_of_ranges = iwl_dump_ini_paging_ranges,
		.get_size = iwl_dump_ini_paging_get_size,
		.fill_range = iwl_dump_ini_paging_iter,
	},
	[IWL_FW_INI_REGION_CSR] = {
		.get_num_of_ranges = iwl_dump_ini_mem_ranges,
		.get_size = iwl_dump_ini_mem_get_size,
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.fill_range = iwl_dump_ini_csr_iter,
	},
	[IWL_FW_INI_REGION_DRAM_IMR] = {
		.get_num_of_ranges = iwl_dump_ini_imr_ranges,
		.get_size = iwl_dump_ini_imr_get_size,
		.fill_mem_hdr = iwl_dump_ini_imr_fill_header,
		.fill_range = iwl_dump_ini_imr_iter,
		.requires_nic_access = true,
	},
	[IWL_FW_INI_REGION_PCI_IOSF_CONFIG] = {
		.get_num_of_ranges = iwl_dump_ini_mem_ranges,
		.get_size = iwl_dump_ini_mem_get_size,
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.fill_range = iwl_dump_ini_config_iter,
	},
	[IWL_FW_INI_REGION_SPECIAL_DEVICE_MEMORY] = {
		.get_num_of_ranges = iwl_dump_ini_single_range,
		.get_size = iwl_dump_ini_special_mem_get_size,
		.fill_mem_hdr = iwl_dump_ini_special_mem_fill_header,
		.fill_range = iwl_dump_ini_special_mem_iter,
		.requires_nic_access = true,
	},
	[IWL_FW_INI_REGION_DBGI_SRAM] = {
		.get_num_of_ranges = iwl_dump_ini_mem_ranges,
		.get_size = iwl_dump_ini_mon_dbgi_get_size,
		.fill_mem_hdr = iwl_dump_ini_mon_dbgi_fill_header,
		.fill_range = iwl_dump_ini_dbgi_sram_iter,
		.requires_nic_access = true,
	},
	[IWL_FW_INI_REGION_PERIPHERY_SNPS_DPHYIP] = {
		.get_num_of_ranges = iwl_dump_ini_mem_ranges,
		.get_size = iwl_dump_ini_mem_get_size,
		.fill_mem_hdr = iwl_dump_ini_mem_fill_header,
		.fill_range = iwl_dump_ini_prph_snps_dphyip_iter,
		.requires_nic_access = true,
	},
};

enum iwl_dump_ini_region_selector {
	IWL_INI_DUMP_ALL_REGIONS,
	IWL_INI_DUMP_EARLY_REGIONS,
	IWL_INI_DUMP_LATE_REGIONS,
};

static bool iwl_dump_due_to_error(enum iwl_fw_ini_time_point tp_id)
{
	return tp_id == IWL_FW_INI_TIME_POINT_FW_ASSERT ||
	       tp_id == IWL_FW_INI_TIME_POINT_FW_HW_ERROR;
}

static void
iwl_dump_ini_dump_regions_prep(struct iwl_fw_runtime *fwrt,
			       struct iwl_fwrt_dump_data *dump_data,
			       struct list_head *list,
			       enum iwl_fw_ini_time_point tp_id,
			       u64 regions_mask,
			       struct iwl_ucode_tlv **imr_tlv)
{
	for (int i = 0; i < ARRAY_SIZE(fwrt->trans->dbg.active_regions); i++) {
		struct iwl_dump_ini_region_data reg_data = {
			.dump_data = dump_data,
		};
		u32 reg_type;
		struct iwl_fw_ini_region_tlv *reg;

		if (!(BIT_ULL(i) & regions_mask))
			continue;

		reg_data.reg_tlv = fwrt->trans->dbg.active_regions[i];
		if (!reg_data.reg_tlv) {
			IWL_WARN(fwrt,
				 "WRT: Unassigned region id %d, skipping\n", i);
			continue;
		}

		reg = (void *)reg_data.reg_tlv->data;
		reg_type = reg->type;
		if (reg_type >= ARRAY_SIZE(iwl_dump_ini_region_ops))
			continue;

		if ((reg_type == IWL_FW_INI_REGION_PERIPHERY_PHY ||
		     reg_type == IWL_FW_INI_REGION_PERIPHERY_PHY_RANGE ||
		     reg_type == IWL_FW_INI_REGION_PERIPHERY_SNPS_DPHYIP) &&
		    tp_id != IWL_FW_INI_TIME_POINT_FW_ASSERT) {
			IWL_WARN(fwrt,
				 "WRT: trying to collect phy prph at time point: %d, skipping\n",
				 tp_id);
			continue;
		}

		/*
		 * DRAM_IMR can be collected only for FW/HW error timepoint
		 * when fw is not alive. In addition, it must be collected
		 * lastly as it overwrites SRAM that can possibly contain
		 * debug data which also need to be collected.
		 */
		if (reg_type == IWL_FW_INI_REGION_DRAM_IMR) {
			if (iwl_dump_due_to_error(tp_id))
				*imr_tlv = fwrt->trans->dbg.active_regions[i];
			else
				IWL_INFO(fwrt,
					 "WRT: trying to collect DRAM_IMR at time point: %d, skipping\n",
					 tp_id);
			/* continue to next region */
			continue;
		}

		iwl_dump_ini_mem_prep(fwrt, list, &reg_data,
				      &iwl_dump_ini_region_ops[reg_type]);
	}
}

static u32
iwl_dump_ini_dump_entries(struct iwl_fw_runtime *fwrt,
			  struct list_head *list,
			  enum iwl_dump_ini_region_selector which)
{
	struct iwl_fw_ini_dump_entry *entry, *tmp;
	bool have_nic_access;
	u32 size = 0;

	have_nic_access = iwl_trans_grab_nic_access(fwrt->trans);

	list_for_each_entry_safe(entry, tmp, list, list) {
		u32 dp = entry->region_dump_policy;

		switch (which) {
		case IWL_INI_DUMP_ALL_REGIONS:
			break;
		case IWL_INI_DUMP_EARLY_REGIONS:
			if (!(dp & IWL_FW_IWL_DEBUG_DUMP_POLICY_BEFORE_RESET))
				continue;
			break;
		case IWL_INI_DUMP_LATE_REGIONS:
			if (dp & IWL_FW_IWL_DEBUG_DUMP_POLICY_BEFORE_RESET)
				continue;
			break;
		}

		size += iwl_dump_ini_mem(fwrt, entry, have_nic_access);

		if (have_nic_access)
			iwl_trans_resched_with_nic_access(fwrt->trans);
	}

	if (have_nic_access)
		iwl_trans_release_nic_access(fwrt->trans);

	return size;
}

static u32 iwl_dump_ini_trigger(struct iwl_fw_runtime *fwrt,
				struct iwl_fwrt_dump_data *dump_data,
				struct list_head *list)
{
	struct iwl_fw_ini_trigger_tlv *trigger = dump_data->trig;
	enum iwl_fw_ini_time_point tp_id = le32_to_cpu(trigger->time_point);
	struct iwl_dump_ini_region_data imr_reg_data = {
		.dump_data = dump_data,
	};
	u32 size = 0;
	u64 regions_mask = le64_to_cpu(trigger->regions_mask) &
			   ~(fwrt->trans->dbg.unsupported_region_msk);

	BUILD_BUG_ON(sizeof(trigger->regions_mask) != sizeof(regions_mask));
	BUILD_BUG_ON((sizeof(trigger->regions_mask) * BITS_PER_BYTE) <
		     ARRAY_SIZE(fwrt->trans->dbg.active_regions));

	iwl_dump_ini_dump_regions_prep(fwrt, dump_data, list, tp_id,
				       regions_mask, &imr_reg_data.reg_tlv);

	/* append DRAM_IMR region to be collected last */
	if (imr_reg_data.reg_tlv)
		iwl_dump_ini_mem_prep(fwrt, list, &imr_reg_data,
				      &iwl_dump_ini_region_ops[IWL_FW_INI_REGION_DRAM_IMR]);

	if (trigger->apply_policy &
			cpu_to_le32(IWL_FW_INI_APPLY_POLICY_SPLIT_DUMP_RESET)) {
		size += iwl_dump_ini_dump_entries(fwrt, list,
						  IWL_INI_DUMP_EARLY_REGIONS);
		iwl_trans_pcie_fw_reset_handshake(fwrt->trans);
		size += iwl_dump_ini_dump_entries(fwrt, list,
						  IWL_INI_DUMP_LATE_REGIONS);
	} else {
		if (fw_has_capa(&fwrt->fw->ucode_capa,
				IWL_UCODE_TLV_CAPA_RESET_DURING_ASSERT) &&
		    iwl_dump_due_to_error(tp_id))
			iwl_trans_pcie_fw_reset_handshake(fwrt->trans);
		size += iwl_dump_ini_dump_entries(fwrt, list,
						  IWL_INI_DUMP_ALL_REGIONS);
	}

	if (size)
		size += iwl_dump_ini_info(fwrt, trigger, list);

	return size;
}

static bool iwl_fw_ini_trigger_on(struct iwl_fw_runtime *fwrt,
				  struct iwl_fw_ini_trigger_tlv *trig)
{
	enum iwl_fw_ini_time_point tp_id = le32_to_cpu(trig->time_point);
	u32 usec = le32_to_cpu(trig->ignore_consec);

	if (!iwl_trans_dbg_ini_valid(fwrt->trans) ||
	    tp_id == IWL_FW_INI_TIME_POINT_INVALID ||
	    tp_id >= IWL_FW_INI_TIME_POINT_NUM ||
	    iwl_fw_dbg_no_trig_window(fwrt, tp_id, usec))
		return false;

	return true;
}

static u32 iwl_dump_ini_file_gen(struct iwl_fw_runtime *fwrt,
				 struct iwl_fwrt_dump_data *dump_data,
				 struct list_head *list)
{
	struct iwl_fw_ini_trigger_tlv *trigger = dump_data->trig;
	struct iwl_fw_ini_dump_entry *entry;
	struct iwl_fw_ini_dump_file_hdr *hdr;
	u32 size;

	if (!trigger || !iwl_fw_ini_trigger_on(fwrt, trigger) ||
	    !le64_to_cpu(trigger->regions_mask))
		return 0;

	entry = vzalloc(sizeof(*entry) + sizeof(*hdr));
	if (!entry)
		return 0;

	entry->size = sizeof(*hdr);

	size = iwl_dump_ini_trigger(fwrt, dump_data, list);
	if (!size) {
		vfree(entry);
		return 0;
	}

	hdr = (void *)entry->data;
	hdr->barker = cpu_to_le32(IWL_FW_INI_ERROR_DUMP_BARKER);
	hdr->file_len = cpu_to_le32(size + entry->size);

	list_add(&entry->list, list);

	return le32_to_cpu(hdr->file_len);
}

static inline void iwl_fw_free_dump_desc(struct iwl_fw_runtime *fwrt,
					 const struct iwl_fw_dump_desc *desc)
{
	if (desc && desc != &iwl_dump_desc_assert)
		kfree(desc);

	fwrt->dump.lmac_err_id[0] = 0;
	if (fwrt->smem_cfg.num_lmacs > 1)
		fwrt->dump.lmac_err_id[1] = 0;
	fwrt->dump.umac_err_id = 0;
}

static void iwl_dump_ini_list_free(struct list_head *list)
{
	while (!list_empty(list)) {
		struct iwl_fw_ini_dump_entry *entry =
			list_entry(list->next, typeof(*entry), list);

		list_del(&entry->list);
		vfree(entry);
	}
}

static void iwl_fw_error_dump_data_free(struct iwl_fwrt_dump_data *dump_data)
{
	dump_data->trig = NULL;
	kfree(dump_data->fw_pkt);
	dump_data->fw_pkt = NULL;
}

static void iwl_fw_error_ini_dump(struct iwl_fw_runtime *fwrt,
				  struct iwl_fwrt_dump_data *dump_data)
{
	LIST_HEAD(dump_list);
	struct scatterlist *sg_dump_data;
	u32 file_len = iwl_dump_ini_file_gen(fwrt, dump_data, &dump_list);

	if (!file_len)
		return;

	sg_dump_data = iwl_fw_dbg_alloc_sgtable(file_len);
	if (sg_dump_data) {
		struct iwl_fw_ini_dump_entry *entry;
		int sg_entries = sg_nents(sg_dump_data);
		u32 offs = 0;

		list_for_each_entry(entry, &dump_list, list) {
			sg_pcopy_from_buffer(sg_dump_data, sg_entries,
					     entry->data, entry->size, offs);
			offs += entry->size;
		}
		dev_coredumpsg(fwrt->trans->dev, sg_dump_data, file_len,
			       GFP_KERNEL);
	}
	iwl_dump_ini_list_free(&dump_list);
}

const struct iwl_fw_dump_desc iwl_dump_desc_assert = {
	.trig_desc = {
		.type = cpu_to_le32(FW_DBG_TRIGGER_FW_ASSERT),
	},
};
IWL_EXPORT_SYMBOL(iwl_dump_desc_assert);

int iwl_fw_dbg_collect_desc(struct iwl_fw_runtime *fwrt,
			    const struct iwl_fw_dump_desc *desc,
			    bool monitor_only,
			    unsigned int delay)
{
	struct iwl_fwrt_wk_data *wk_data;
	unsigned long idx;

	if (iwl_trans_dbg_ini_valid(fwrt->trans)) {
		iwl_fw_free_dump_desc(fwrt, desc);
		return 0;
	}

	/*
	 * Check there is an available worker.
	 * ffz return value is undefined if no zero exists,
	 * so check against ~0UL first.
	 */
	if (fwrt->dump.active_wks == ~0UL)
		return -EBUSY;

	idx = ffz(fwrt->dump.active_wks);

	if (idx >= IWL_FW_RUNTIME_DUMP_WK_NUM ||
	    test_and_set_bit(fwrt->dump.wks[idx].idx, &fwrt->dump.active_wks))
		return -EBUSY;

	wk_data = &fwrt->dump.wks[idx];

	if (WARN_ON(wk_data->dump_data.desc))
		iwl_fw_free_dump_desc(fwrt, wk_data->dump_data.desc);

	wk_data->dump_data.desc = desc;
	wk_data->dump_data.monitor_only = monitor_only;

	IWL_WARN(fwrt, "Collecting data: trigger %d fired.\n",
		 le32_to_cpu(desc->trig_desc.type));

	queue_delayed_work(system_dfl_wq, &wk_data->wk,
			   usecs_to_jiffies(delay));

	return 0;
}
IWL_EXPORT_SYMBOL(iwl_fw_dbg_collect_desc);

int iwl_fw_dbg_error_collect(struct iwl_fw_runtime *fwrt,
			     enum iwl_fw_dbg_trigger trig_type)
{
	if (!iwl_trans_device_enabled(fwrt->trans))
		return -EIO;

	if (iwl_trans_dbg_ini_valid(fwrt->trans)) {
		if (trig_type != FW_DBG_TRIGGER_ALIVE_TIMEOUT &&
		    trig_type != FW_DBG_TRIGGER_DRIVER)
			return -EIO;

		iwl_dbg_tlv_time_point(fwrt,
				       IWL_FW_INI_TIME_POINT_HOST_ALIVE_TIMEOUT,
				       NULL);
	} else {
		struct iwl_fw_dump_desc *iwl_dump_error_desc;
		int ret;

		iwl_dump_error_desc = kmalloc_obj(*iwl_dump_error_desc);

		if (!iwl_dump_error_desc)
			return -ENOMEM;

		iwl_dump_error_desc->trig_desc.type = cpu_to_le32(trig_type);
		iwl_dump_error_desc->len = 0;

		ret = iwl_fw_dbg_collect_desc(fwrt, iwl_dump_error_desc,
					      false, 0);
		if (ret) {
			kfree(iwl_dump_error_desc);
			return ret;
		}
	}

	iwl_trans_sync_nmi(fwrt->trans);

	return 0;
}
IWL_EXPORT_SYMBOL(iwl_fw_dbg_error_collect);

int iwl_fw_dbg_collect(struct iwl_fw_runtime *fwrt,
		       enum iwl_fw_dbg_trigger trig,
		       const char *str, size_t len,
		       struct iwl_fw_dbg_trigger_tlv *trigger)
{
	struct iwl_fw_dump_desc *desc;
	unsigned int delay = 0;
	bool monitor_only = false;
	int ret;

	if (trigger) {
		u16 occurrences = le16_to_cpu(trigger->occurrences) - 1;

		if (!le16_to_cpu(trigger->occurrences))
			return 0;

		if (trigger->flags & IWL_FW_DBG_FORCE_RESTART) {
			IWL_WARN(fwrt, "Force restart: trigger %d fired.\n",
				 trig);
			iwl_force_nmi(fwrt->trans);
			return 0;
		}

		trigger->occurrences = cpu_to_le16(occurrences);
		monitor_only = trigger->mode & IWL_FW_DBG_TRIGGER_MONITOR_ONLY;

		/* convert msec to usec */
		delay = le32_to_cpu(trigger->stop_delay) * USEC_PER_MSEC;
	}

	desc = kzalloc_flex(*desc, trig_desc.data, len, GFP_ATOMIC);
	if (!desc)
		return -ENOMEM;


	desc->len = len;
	desc->trig_desc.type = cpu_to_le32(trig);
	memcpy(desc->trig_desc.data, str, len);

	ret = iwl_fw_dbg_collect_desc(fwrt, desc, monitor_only, delay);
	if (ret)
		kfree(desc);

	return ret;
}
IWL_EXPORT_SYMBOL(iwl_fw_dbg_collect);

int iwl_fw_dbg_collect_trig(struct iwl_fw_runtime *fwrt,
			    struct iwl_fw_dbg_trigger_tlv *trigger,
			    const char *fmt, ...)
{
	int len = 0;
	char buf[64];

	if (iwl_trans_dbg_ini_valid(fwrt->trans))
		return 0;

	if (fmt) {
		va_list ap;

		buf[sizeof(buf) - 1] = '\0';

		va_start(ap, fmt);
		vsnprintf(buf, sizeof(buf), fmt, ap);
		va_end(ap);

		/* check for truncation */
		if (WARN_ON_ONCE(buf[sizeof(buf) - 1]))
			buf[sizeof(buf) - 1] = '\0';

		len = strlen(buf) + 1;
	}

	return iwl_fw_dbg_collect(fwrt, le32_to_cpu(trigger->id), buf, len,
				  trigger);
}
IWL_EXPORT_SYMBOL(iwl_fw_dbg_collect_trig);

int iwl_fw_start_dbg_conf(struct iwl_fw_runtime *fwrt, u8 conf_id)
{
	u8 *ptr;
	int ret;
	int i;

	if (WARN_ONCE(conf_id >= ARRAY_SIZE(fwrt->fw->dbg.conf_tlv),
		      "Invalid configuration %d\n", conf_id))
		return -EINVAL;

	/* EARLY START - firmware's configuration is hard coded */
	if ((!fwrt->fw->dbg.conf_tlv[conf_id] ||
	     !fwrt->fw->dbg.conf_tlv[conf_id]->num_of_hcmds) &&
	    conf_id == FW_DBG_START_FROM_ALIVE)
		return 0;

	if (!fwrt->fw->dbg.conf_tlv[conf_id])
		return -EINVAL;

	if (fwrt->dump.conf != FW_DBG_INVALID)
		IWL_INFO(fwrt, "FW already configured (%d) - re-configuring\n",
			 fwrt->dump.conf);

	/* Send all HCMDs for configuring the FW debug */
	ptr = (void *)&fwrt->fw->dbg.conf_tlv[conf_id]->hcmd;
	for (i = 0; i < fwrt->fw->dbg.conf_tlv[conf_id]->num_of_hcmds; i++) {
		struct iwl_fw_dbg_conf_hcmd *cmd = (void *)ptr;
		struct iwl_host_cmd hcmd = {
			.id = cmd->id,
			.len = { le16_to_cpu(cmd->len), },
			.data = { cmd->data, },
		};

		ret = iwl_trans_send_cmd(fwrt->trans, &hcmd);
		if (ret)
			return ret;

		ptr += sizeof(*cmd);
		ptr += le16_to_cpu(cmd->len);
	}

	fwrt->dump.conf = conf_id;

	return 0;
}
IWL_EXPORT_SYMBOL(iwl_fw_start_dbg_conf);

static void iwl_send_dbg_dump_complete_cmd(struct iwl_fw_runtime *fwrt,
					   u32 timepoint, u32 timepoint_data)
{
	struct iwl_dbg_dump_complete_cmd hcmd_data;
	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(DEBUG_GROUP, FW_DUMP_COMPLETE_CMD),
		.data[0] = &hcmd_data,
		.len[0] = sizeof(hcmd_data),
	};

	if (iwl_trans_is_fw_error(fwrt->trans))
		return;

	if (fw_has_capa(&fwrt->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_DUMP_COMPLETE_SUPPORT)) {
		hcmd_data.tp = cpu_to_le32(timepoint);
		hcmd_data.tp_data = cpu_to_le32(timepoint_data);
		iwl_trans_send_cmd(fwrt->trans, &hcmd);
	}
}

/* this function assumes dump_start was called beforehand and dump_end will be
 * called afterwards
 */
static void iwl_fw_dbg_collect_sync(struct iwl_fw_runtime *fwrt, u8 wk_idx)
{
	struct iwl_fw_dbg_params params = {0};
	struct iwl_fwrt_dump_data *dump_data =
		&fwrt->dump.wks[wk_idx].dump_data;

	if (!test_bit(wk_idx, &fwrt->dump.active_wks))
		return;

	/* also checks 'desc' for pre-ini mode, since that shadows in union */
	if (!dump_data->trig) {
		IWL_ERR(fwrt, "dump trigger data is not set\n");
		goto out;
	}

	if (!iwl_trans_device_enabled(fwrt->trans)) {
		IWL_ERR(fwrt, "Device is not enabled - cannot dump error\n");
		goto out;
	}

	/* there's no point in fw dump if the bus is dead */
	if (iwl_trans_is_dead(fwrt->trans)) {
		IWL_ERR(fwrt, "Skip fw error dump since bus is dead\n");
		goto out;
	}

	iwl_fw_dbg_stop_restart_recording(fwrt, &params, true);

	IWL_DEBUG_FW_INFO(fwrt, "WRT: Data collection start\n");
	if (iwl_trans_dbg_ini_valid(fwrt->trans))
		iwl_fw_error_ini_dump(fwrt, dump_data);
	else
		iwl_fw_error_dump(fwrt, dump_data);
	IWL_DEBUG_FW_INFO(fwrt, "WRT: Data collection done\n");

	iwl_fw_dbg_stop_restart_recording(fwrt, &params, false);

	if (iwl_trans_dbg_ini_valid(fwrt->trans)) {
		u32 policy = le32_to_cpu(dump_data->trig->apply_policy);
		u32 time_point = le32_to_cpu(dump_data->trig->time_point);

		if (policy & IWL_FW_INI_APPLY_POLICY_DUMP_COMPLETE_CMD) {
			IWL_DEBUG_FW_INFO(fwrt, "WRT: sending dump complete\n");
			iwl_send_dbg_dump_complete_cmd(fwrt, time_point, 0);
		}
	}

	if (fwrt->trans->dbg.last_tp_resetfw == IWL_FW_INI_RESET_FW_MODE_STOP_FW_ONLY)
		iwl_force_nmi(fwrt->trans);
out:
	if (iwl_trans_dbg_ini_valid(fwrt->trans)) {
		iwl_fw_error_dump_data_free(dump_data);
	} else {
		iwl_fw_free_dump_desc(fwrt, dump_data->desc);
		dump_data->desc = NULL;
	}

	clear_bit(wk_idx, &fwrt->dump.active_wks);
}

int iwl_fw_dbg_ini_collect(struct iwl_fw_runtime *fwrt,
			   struct iwl_fwrt_dump_data *dump_data,
			   bool sync)
{
	struct iwl_fw_ini_trigger_tlv *trig = dump_data->trig;
	enum iwl_fw_ini_time_point tp_id = le32_to_cpu(trig->time_point);
	u32 occur, delay;
	unsigned long idx;

	if (!iwl_fw_ini_trigger_on(fwrt, trig)) {
		IWL_WARN(fwrt, "WRT: Trigger %d is not active, aborting dump\n",
			 tp_id);
		return -EINVAL;
	}

	delay = le32_to_cpu(trig->dump_delay);
	occur = le32_to_cpu(trig->occurrences);
	if (!occur)
		return 0;

	trig->occurrences = cpu_to_le32(--occur);

	/* Check there is an available worker.
	 * ffz return value is undefined if no zero exists,
	 * so check against ~0UL first.
	 */
	if (fwrt->dump.active_wks == ~0UL)
		return -EBUSY;

	idx = ffz(fwrt->dump.active_wks);

	if (idx >= IWL_FW_RUNTIME_DUMP_WK_NUM ||
	    test_and_set_bit(fwrt->dump.wks[idx].idx, &fwrt->dump.active_wks))
		return -EBUSY;

	fwrt->dump.wks[idx].dump_data = *dump_data;

	if (sync)
		delay = 0;

	IWL_WARN(fwrt,
		 "WRT: Collecting data: ini trigger %d fired (delay=%dms).\n",
		 tp_id, (u32)(delay / USEC_PER_MSEC));

	if (sync)
		iwl_fw_dbg_collect_sync(fwrt, idx);
	else
		queue_delayed_work(system_dfl_wq,
				   &fwrt->dump.wks[idx].wk,
				   usecs_to_jiffies(delay));

	return 0;
}

void iwl_fw_error_dump_wk(struct work_struct *work)
{
	struct iwl_fwrt_wk_data *wks =
		container_of(work, typeof(*wks), wk.work);
	struct iwl_fw_runtime *fwrt =
		container_of(wks, typeof(*fwrt), dump.wks[wks->idx]);

	/* assumes the op mode mutex is locked in dump_start since
	 * iwl_fw_dbg_collect_sync can't run in parallel
	 */
	if (fwrt->ops && fwrt->ops->dump_start)
		fwrt->ops->dump_start(fwrt->ops_ctx);

	iwl_fw_dbg_collect_sync(fwrt, wks->idx);

	if (fwrt->ops && fwrt->ops->dump_end)
		fwrt->ops->dump_end(fwrt->ops_ctx);
}

void iwl_fw_dbg_read_d3_debug_data(struct iwl_fw_runtime *fwrt)
{
	const struct iwl_mac_cfg *mac_cfg = fwrt->trans->mac_cfg;

	if (!iwl_fw_dbg_is_d3_debug_enabled(fwrt))
		return;

	if (!fwrt->dump.d3_debug_data) {
		fwrt->dump.d3_debug_data = kmalloc(mac_cfg->base->d3_debug_data_length,
						   GFP_KERNEL);
		if (!fwrt->dump.d3_debug_data) {
			IWL_ERR(fwrt,
				"failed to allocate memory for D3 debug data\n");
			return;
		}
	}

	/* if the buffer holds previous debug data it is overwritten */
	iwl_trans_read_mem_bytes(fwrt->trans, mac_cfg->base->d3_debug_data_base_addr,
				 fwrt->dump.d3_debug_data,
				 mac_cfg->base->d3_debug_data_length);

	if (fwrt->sanitize_ops && fwrt->sanitize_ops->frob_mem)
		fwrt->sanitize_ops->frob_mem(fwrt->sanitize_ctx,
					     mac_cfg->base->d3_debug_data_base_addr,
					     fwrt->dump.d3_debug_data,
					     mac_cfg->base->d3_debug_data_length);
}
IWL_EXPORT_SYMBOL(iwl_fw_dbg_read_d3_debug_data);

void iwl_fw_dbg_stop_sync(struct iwl_fw_runtime *fwrt)
{
	int i;

	iwl_dbg_tlv_del_timers(fwrt->trans);
	for (i = 0; i < IWL_FW_RUNTIME_DUMP_WK_NUM; i++)
		iwl_fw_dbg_collect_sync(fwrt, i);

	iwl_fw_dbg_stop_restart_recording(fwrt, NULL, true);
}
IWL_EXPORT_SYMBOL(iwl_fw_dbg_stop_sync);

static int iwl_fw_dbg_suspend_resume_hcmd(struct iwl_trans *trans, bool suspend)
{
	struct iwl_dbg_suspend_resume_cmd cmd = {
		.operation = suspend ?
			cpu_to_le32(DBGC_SUSPEND_CMD) :
			cpu_to_le32(DBGC_RESUME_CMD),
	};
	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(DEBUG_GROUP, DBGC_SUSPEND_RESUME),
		.data[0] = &cmd,
		.len[0] = sizeof(cmd),
	};

	return iwl_trans_send_cmd(trans, &hcmd);
}

static void iwl_fw_dbg_stop_recording(struct iwl_trans *trans,
				      struct iwl_fw_dbg_params *params)
{
	if (trans->mac_cfg->device_family == IWL_DEVICE_FAMILY_7000) {
		iwl_set_bits_prph(trans, MON_BUFF_SAMPLE_CTL, 0x100);
		return;
	}

	if (params) {
		params->in_sample = iwl_read_umac_prph(trans, DBGC_IN_SAMPLE);
		params->out_ctrl = iwl_read_umac_prph(trans, DBGC_OUT_CTRL);
	}

	iwl_write_umac_prph(trans, DBGC_IN_SAMPLE, 0);
	/* wait for the DBGC to finish writing the internal buffer to DRAM to
	 * avoid halting the HW while writing
	 */
	usleep_range(700, 1000);
	iwl_write_umac_prph(trans, DBGC_OUT_CTRL, 0);
}

static int iwl_fw_dbg_restart_recording(struct iwl_trans *trans,
					struct iwl_fw_dbg_params *params)
{
	if (!params)
		return -EIO;

	if (trans->mac_cfg->device_family == IWL_DEVICE_FAMILY_7000) {
		iwl_clear_bits_prph(trans, MON_BUFF_SAMPLE_CTL, 0x100);
		iwl_clear_bits_prph(trans, MON_BUFF_SAMPLE_CTL, 0x1);
		iwl_set_bits_prph(trans, MON_BUFF_SAMPLE_CTL, 0x1);
	} else {
		iwl_write_umac_prph(trans, DBGC_IN_SAMPLE, params->in_sample);
		iwl_write_umac_prph(trans, DBGC_OUT_CTRL, params->out_ctrl);
	}

	return 0;
}

int iwl_fw_send_timestamp_marker_cmd(struct iwl_fw_runtime *fwrt)
{
	struct iwl_marker marker = {
		.dw_len = sizeof(struct iwl_marker) / 4,
		.marker_id = MARKER_ID_SYNC_CLOCK,
	};
	struct iwl_host_cmd hcmd = {
		.flags = CMD_ASYNC,
		.id = WIDE_ID(LONG_GROUP, MARKER_CMD),
		.dataflags = {},
	};
	struct iwl_marker_rsp *resp;
	int cmd_ver = iwl_fw_lookup_cmd_ver(fwrt->fw,
					    WIDE_ID(LONG_GROUP, MARKER_CMD),
					    IWL_FW_CMD_VER_UNKNOWN);
	int ret;

	if (cmd_ver == 1) {
		/* the real timestamp is taken from the ftrace clock
		 * this is for finding the match between fw and kernel logs
		 */
		marker.timestamp = cpu_to_le64(fwrt->timestamp.seq++);
	} else if (cmd_ver == 2) {
		marker.timestamp = cpu_to_le64(ktime_get_boottime_ns());
	} else {
		IWL_DEBUG_INFO(fwrt,
			       "Invalid version of Marker CMD. Ver = %d\n",
			       cmd_ver);
		return -EINVAL;
	}

	hcmd.data[0] = &marker;
	hcmd.len[0] = sizeof(marker);

	ret = iwl_trans_send_cmd(fwrt->trans, &hcmd);

	if (cmd_ver > 1 && hcmd.resp_pkt) {
		resp = (void *)hcmd.resp_pkt->data;
		IWL_DEBUG_INFO(fwrt, "FW GP2 time: %u\n",
			       le32_to_cpu(resp->gp2));
	}

	return ret;
}

void iwl_fw_dbg_stop_restart_recording(struct iwl_fw_runtime *fwrt,
				       struct iwl_fw_dbg_params *params,
				       bool stop)
{
	int ret __maybe_unused = 0;

	if (!iwl_trans_fw_running(fwrt->trans))
		return;

	if (fw_has_capa(&fwrt->fw->ucode_capa,
			IWL_UCODE_TLV_CAPA_DBG_SUSPEND_RESUME_CMD_SUPP)) {
		if (stop)
			iwl_fw_send_timestamp_marker_cmd(fwrt);
		ret = iwl_fw_dbg_suspend_resume_hcmd(fwrt->trans, stop);
	} else if (stop) {
		iwl_fw_dbg_stop_recording(fwrt->trans, params);
	} else {
		ret = iwl_fw_dbg_restart_recording(fwrt->trans, params);
	}
#ifdef CONFIG_IWLWIFI_DEBUGFS
	if (!ret) {
		if (stop)
			fwrt->trans->dbg.rec_on = false;
		else
			iwl_fw_set_dbg_rec_on(fwrt);
	}
#endif
}
IWL_EXPORT_SYMBOL(iwl_fw_dbg_stop_restart_recording);

void iwl_fw_disable_dbg_asserts(struct iwl_fw_runtime *fwrt)
{
	struct iwl_fw_dbg_config_cmd cmd = {
		.type = cpu_to_le32(DEBUG_TOKEN_CONFIG_TYPE),
		.conf = cpu_to_le32(IWL_FW_DBG_CONFIG_TOKEN),
	};
	struct iwl_host_cmd hcmd = {
		.id = WIDE_ID(LONG_GROUP, LDBG_CONFIG_CMD),
		.data[0] = &cmd,
		.len[0] = sizeof(cmd),
	};
	u32 preset = u32_get_bits(fwrt->trans->dbg.domains_bitmap,
				  GENMASK(31, IWL_FW_DBG_DOMAIN_POS + 1));

	/* supported starting from 9000 devices */
	if (fwrt->trans->mac_cfg->device_family < IWL_DEVICE_FAMILY_9000)
		return;

	if (fwrt->trans->dbg.yoyo_bin_loaded || (preset && preset != 1))
		return;

	iwl_trans_send_cmd(fwrt->trans, &hcmd);
}
IWL_EXPORT_SYMBOL(iwl_fw_disable_dbg_asserts);

void iwl_fw_dbg_clear_monitor_buf(struct iwl_fw_runtime *fwrt)
{
	struct iwl_fw_dbg_params params = {0};

	iwl_fw_dbg_stop_sync(fwrt);

	if (fw_has_api(&fwrt->fw->ucode_capa,
		       IWL_UCODE_TLV_API_INT_DBG_BUF_CLEAR)) {
		struct iwl_host_cmd hcmd = {
			.id = WIDE_ID(DEBUG_GROUP, FW_CLEAR_BUFFER),
		};
		iwl_trans_send_cmd(fwrt->trans, &hcmd);
	}

	iwl_dbg_tlv_init_cfg(fwrt);
	iwl_fw_dbg_stop_restart_recording(fwrt, &params, false);
}
IWL_EXPORT_SYMBOL(iwl_fw_dbg_clear_monitor_buf);
