// SPDX-License-Identifier: GPL-2.0-only
//Copyright (c) 2024 Qualcomm Innovation Center, Inc. All rights reserved.

#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/platform_device.h>

#include "mhi.h"
#include "mhi_hwio.h"

int mhi_dev_mmio_read(struct mhi_dev *dev, uint32_t offset,
			uint32_t *reg_value)
{
	void __iomem *addr;

	if (WARN_ON(!dev))
		return -EINVAL;

	addr = dev->mmio_base_addr + offset;

	*reg_value = readl_relaxed(addr);

	pr_debug("reg read:0x%x with value 0x%x\n", offset, *reg_value);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_read);

int mhi_dev_mmio_write(struct mhi_dev *dev, uint32_t offset,
				uint32_t val)
{
	void __iomem *addr;

	if (WARN_ON(!dev))
		return -EINVAL;

	addr = dev->mmio_base_addr + offset;

	writel_relaxed(val, addr);

	pr_debug("reg write:0x%x with value 0x%x\n", offset, val);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_write);

int mhi_dev_mmio_masked_write(struct mhi_dev *dev, uint32_t offset,
						uint32_t mask, uint32_t shift,
						uint32_t val)
{
	uint32_t reg_val;

	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_read(dev, offset, &reg_val);

	reg_val &= ~mask;
	reg_val |= ((val << shift) & mask);

	mhi_dev_mmio_write(dev, offset, reg_val);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_masked_write);

int mhi_dev_mmio_masked_read(struct mhi_dev *dev, uint32_t offset,
						uint32_t mask, uint32_t shift,
						uint32_t *reg_val)
{
	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_read(dev, offset, reg_val);

	*reg_val &= mask;
	*reg_val >>= shift;

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_masked_read);

static int mhi_dev_mmio_mask_set_chdb_int_a7(struct mhi_dev *dev,
						uint32_t chdb_id, bool enable)
{
	uint32_t chid_mask, chid_idx, chid_shft, val = 0;

	chid_shft = chdb_id%32;
	chid_mask = (1 << chid_shft);
	chid_idx = chdb_id/32;

	if (chid_idx >= MHI_MASK_ROWS_CH_EV_DB) {
		mhi_log(dev->vf_id, MHI_MSG_ERROR, "Invalid ch_id:%d\n", chid_idx);
		return -EINVAL;
	}

	if (enable)
		val = 1;

	mhi_dev_mmio_masked_write(dev, MHI_CHDB_INT_MASK_A7_n(chid_idx),
					chid_mask, chid_shft, val);

	mhi_dev_mmio_read(dev, MHI_CHDB_INT_MASK_A7_n(chid_idx),
						&dev->chdb[chid_idx].mask);

	return 0;
}

int mhi_dev_mmio_enable_chdb_a7(struct mhi_dev *dev, uint32_t chdb_id)
{
	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_mask_set_chdb_int_a7(dev, chdb_id, true);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_enable_chdb_a7);

int mhi_dev_mmio_disable_chdb_a7(struct mhi_dev *dev, uint32_t chdb_id)
{
	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_mask_set_chdb_int_a7(dev, chdb_id, false);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_disable_chdb_a7);

static int mhi_dev_mmio_set_erdb_int_a7(struct mhi_dev *dev,
					uint32_t erdb_ch_id, bool enable)
{
	uint32_t erdb_id_shft, erdb_id_mask, erdb_id_idx, val = 0;

	erdb_id_shft = erdb_ch_id%32;
	erdb_id_mask = (1 << erdb_id_shft);
	erdb_id_idx = erdb_ch_id/32;

	if (enable)
		val = 1;

	mhi_dev_mmio_masked_write(dev,
			MHI_ERDB_INT_MASK_A7_n(erdb_id_idx),
			erdb_id_mask, erdb_id_shft, val);

	return 0;
}

int mhi_dev_mmio_enable_erdb_a7(struct mhi_dev *dev, uint32_t erdb_id)
{
	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_set_erdb_int_a7(dev, erdb_id, true);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_enable_erdb_a7);

int mhi_dev_mmio_disable_erdb_a7(struct mhi_dev *dev, uint32_t erdb_id)
{
	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_set_erdb_int_a7(dev, erdb_id, false);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_disable_erdb_a7);

int mhi_dev_mmio_get_mhi_state(struct mhi_dev *dev, enum mhi_dev_state *state,
						u32 *mhi_reset)
{
	uint32_t reg_value = 0;

	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_masked_read(dev, MHICTRL,
		MHISTATUS_MHISTATE_MASK, MHISTATUS_MHISTATE_SHIFT, state);

	mhi_dev_mmio_read(dev, MHICTRL, &reg_value);

	if (reg_value & MHICTRL_RESET_MASK)
		*mhi_reset = 1;
	else
		*mhi_reset = 0;

	mhi_log(dev->vf_id, MHI_MSG_VERBOSE, "MHICTRL is 0x%x, reset:%d\n",
			reg_value, *mhi_reset);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_get_mhi_state);

static int mhi_dev_mmio_set_chdb_interrupts(struct mhi_dev *dev, bool enable)
{
	uint32_t mask = 0, i = 0;

	if (enable)
		mask = MHI_CHDB_INT_MASK_A7_n_MASK_MASK;

	for (i = 0; i < MHI_MASK_ROWS_CH_EV_DB; i++) {
		mhi_dev_mmio_write(dev,
				MHI_CHDB_INT_MASK_A7_n(i), mask);
		dev->chdb[i].mask = mask;
	}

	return 0;
}

int mhi_dev_mmio_enable_chdb_interrupts(struct mhi_dev *dev)
{
	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_set_chdb_interrupts(dev, true);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_enable_chdb_interrupts);

int mhi_dev_mmio_mask_chdb_interrupts(struct mhi_dev *dev)
{
	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_set_chdb_interrupts(dev, false);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_mask_chdb_interrupts);

int mhi_dev_mmio_read_chdb_status_interrupts(struct mhi_dev *dev)
{
	uint32_t i;

	if (WARN_ON(!dev))
		return -EINVAL;

	for (i = 0; i < MHI_MASK_ROWS_CH_EV_DB; i++)
		mhi_dev_mmio_read(dev,
			MHI_CHDB_INT_STATUS_A7_n(i), &dev->chdb[i].status);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_read_chdb_status_interrupts);

static int mhi_dev_mmio_set_erdb_interrupts(struct mhi_dev *dev, bool enable)
{
	uint32_t mask = 0, i;

	if (enable)
		mask = MHI_ERDB_INT_MASK_A7_n_MASK_MASK;

	for (i = 0; i < MHI_MASK_ROWS_CH_EV_DB; i++)
		mhi_dev_mmio_write(dev,
				MHI_ERDB_INT_MASK_A7_n(i), mask);

	return 0;
}

int mhi_dev_mmio_enable_erdb_interrupts(struct mhi_dev *dev)
{
	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_set_erdb_interrupts(dev, true);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_enable_erdb_interrupts);

int mhi_dev_mmio_mask_erdb_interrupts(struct mhi_dev *dev)
{
	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_set_erdb_interrupts(dev, false);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_mask_erdb_interrupts);

int mhi_dev_mmio_read_erdb_status_interrupts(struct mhi_dev *dev)
{
	uint32_t i;

	if (WARN_ON(!dev))
		return -EINVAL;

	for (i = 0; i < MHI_MASK_ROWS_CH_EV_DB; i++)
		mhi_dev_mmio_read(dev, MHI_ERDB_INT_STATUS_A7_n(i),
						&dev->evdb[i].status);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_read_erdb_status_interrupts);

int mhi_dev_mmio_enable_ctrl_interrupt(struct mhi_dev *dev)
{
	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_masked_write(dev, MHI_CTRL_INT_MASK_A7,
			MHI_CTRL_MHICTRL_MASK, MHI_CTRL_MHICTRL_SHFT, 1);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_enable_ctrl_interrupt);

int mhi_dev_mmio_disable_ctrl_interrupt(struct mhi_dev *dev)
{
	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_masked_write(dev, MHI_CTRL_INT_MASK_A7,
			MHI_CTRL_MHICTRL_MASK, MHI_CTRL_MHICTRL_SHFT, 0);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_disable_ctrl_interrupt);

int mhi_dev_mmio_read_ctrl_status_interrupt(struct mhi_dev *dev)
{
	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_read(dev, MHI_CTRL_INT_STATUS_A7, &dev->ctrl_int);

	dev->ctrl_int &= 0x1;

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_read_ctrl_status_interrupt);

int mhi_dev_mmio_read_cmdb_status_interrupt(struct mhi_dev *dev)
{
	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_read(dev, MHI_CTRL_INT_STATUS_A7, &dev->cmd_int);

	dev->cmd_int &= 0x10;

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_read_cmdb_status_interrupt);

int mhi_dev_mmio_enable_cmdb_interrupt(struct mhi_dev *dev)
{
	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_masked_write(dev, MHI_CTRL_INT_MASK_A7,
			MHI_CTRL_CRDB_MASK, MHI_CTRL_CRDB_SHFT, 1);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_enable_cmdb_interrupt);

int mhi_dev_mmio_disable_cmdb_interrupt(struct mhi_dev *dev)
{
	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_masked_write(dev, MHI_CTRL_INT_MASK_A7,
			MHI_CTRL_CRDB_MASK, MHI_CTRL_CRDB_SHFT, 0);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_disable_cmdb_interrupt);

void mhi_dev_mmio_mask_interrupts(struct mhi_dev *dev)
{
	mhi_dev_mmio_disable_ctrl_interrupt(dev);

	mhi_dev_mmio_disable_cmdb_interrupt(dev);

	mhi_dev_mmio_mask_chdb_interrupts(dev);

	mhi_dev_mmio_mask_erdb_interrupts(dev);
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_mask_interrupts);

int mhi_dev_mmio_clear_interrupts(struct mhi_dev *dev)
{
	uint32_t i = 0;

	if (WARN_ON(!dev))
		return -EINVAL;

	for (i = 0; i < MHI_MASK_ROWS_CH_EV_DB; i++)
		mhi_dev_mmio_write(dev, MHI_CHDB_INT_CLEAR_A7_n(i),
				MHI_CHDB_INT_CLEAR_A7_n_CLEAR_MASK);

	for (i = 0; i < MHI_MASK_ROWS_CH_EV_DB; i++)
		mhi_dev_mmio_write(dev, MHI_ERDB_INT_CLEAR_A7_n(i),
				MHI_ERDB_INT_CLEAR_A7_n_CLEAR_MASK);

	mhi_dev_mmio_write(dev, MHI_CTRL_INT_CLEAR_A7,
		(MHI_CTRL_INT_MMIO_WR_CLEAR | MHI_CTRL_INT_CRDB_CLEAR |
		MHI_CTRL_INT_CRDB_MHICTRL_CLEAR));

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_clear_interrupts);

int mhi_dev_mmio_get_chc_base(struct mhi_dev *dev)
{
	uint32_t ccabap_value = 0, offset = 0;

	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_read(dev, CCABAP_HIGHER, &ccabap_value);

	dev->ch_ctx_shadow.host_pa = ccabap_value;
	dev->ch_ctx_shadow.host_pa <<= 32;

	mhi_dev_mmio_read(dev, CCABAP_LOWER, &ccabap_value);

	dev->ch_ctx_shadow.host_pa |= ccabap_value;

	offset = (uint32_t)(dev->ch_ctx_shadow.host_pa -
					dev->ctrl_base.host_pa);

	dev->ch_ctx_shadow.device_pa = dev->ctrl_base.device_pa + offset;
	dev->ch_ctx_shadow.device_va = dev->ctrl_base.device_va + offset;

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_get_chc_base);

int mhi_dev_mmio_get_erc_base(struct mhi_dev *dev)
{
	uint32_t ecabap_value = 0, offset = 0;

	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_read(dev, ECABAP_HIGHER, &ecabap_value);

	dev->ev_ctx_shadow.host_pa = ecabap_value;
	dev->ev_ctx_shadow.host_pa <<= 32;

	mhi_dev_mmio_read(dev, ECABAP_LOWER, &ecabap_value);

	dev->ev_ctx_shadow.host_pa |= ecabap_value;

	offset = (uint32_t)(dev->ev_ctx_shadow.host_pa -
					dev->ctrl_base.host_pa);

	dev->ev_ctx_shadow.device_pa = dev->ctrl_base.device_pa + offset;
	dev->ev_ctx_shadow.device_va = dev->ctrl_base.device_va + offset;

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_get_erc_base);

int mhi_dev_mmio_get_crc_base(struct mhi_dev *dev)
{
	uint32_t crcbap_value = 0, offset = 0;

	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_read(dev, CRCBAP_HIGHER, &crcbap_value);

	dev->cmd_ctx_shadow.host_pa = crcbap_value;
	dev->cmd_ctx_shadow.host_pa <<= 32;

	mhi_dev_mmio_read(dev, CRCBAP_LOWER, &crcbap_value);

	dev->cmd_ctx_shadow.host_pa |= crcbap_value;

	offset = (uint32_t)(dev->cmd_ctx_shadow.host_pa -
					dev->ctrl_base.host_pa);

	dev->cmd_ctx_shadow.device_pa = dev->ctrl_base.device_pa + offset;
	dev->cmd_ctx_shadow.device_va = dev->ctrl_base.device_va + offset;

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_get_crc_base);

int mhi_dev_mmio_get_ch_db(struct mhi_dev_ring *ring, uint64_t *wr_offset)
{
	uint32_t value = 0, ch_start_idx = 0;

	if (WARN_ON(!ring))
		return -EINVAL;

	ch_start_idx = ring->mhi_dev->ch_ring_start;

	mhi_dev_mmio_read(ring->mhi_dev,
			CHDB_HIGHER_n(ring->id-ch_start_idx), &value);

	*wr_offset = value;
	*wr_offset <<= 32;

	mhi_dev_mmio_read(ring->mhi_dev,
			CHDB_LOWER_n(ring->id-ch_start_idx), &value);

	*wr_offset |= value;

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_get_ch_db);

int mhi_dev_mmio_get_erc_db(struct mhi_dev_ring *ring, uint64_t *wr_offset)
{
	uint32_t value = 0, ev_idx_start = 0;

	if (WARN_ON(!ring))
		return -EINVAL;

	ev_idx_start = ring->mhi_dev->ev_ring_start;
	mhi_dev_mmio_read(ring->mhi_dev,
			ERDB_HIGHER_n(ring->id - ev_idx_start), &value);

	*wr_offset = value;
	*wr_offset <<= 32;

	mhi_dev_mmio_read(ring->mhi_dev,
			ERDB_LOWER_n(ring->id - ev_idx_start), &value);

	*wr_offset |= value;

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_get_erc_db);

int mhi_dev_mmio_get_cmd_db(struct mhi_dev_ring *ring, uint64_t *wr_offset)
{
	uint32_t value = 0;

	if (WARN_ON(!ring))
		return -EINVAL;

	mhi_dev_mmio_read(ring->mhi_dev, CRDB_HIGHER, &value);

	*wr_offset = value;
	*wr_offset <<= 32;

	mhi_dev_mmio_read(ring->mhi_dev, CRDB_LOWER, &value);

	*wr_offset |= value;

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_get_cmd_db);

int mhi_dev_mmio_set_env(struct mhi_dev *dev, uint32_t value)
{
	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_write(dev, BHI_EXECENV, value);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_set_env);

int mhi_dev_mmio_clear_reset(struct mhi_dev *dev)
{
	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_masked_write(dev, MHICTRL,
		MHICTRL_RESET_MASK, MHICTRL_RESET_SHIFT, 0);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_clear_reset);

int mhi_dev_mmio_reset(struct mhi_dev *dev)
{
	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_write(dev, MHICTRL, 0);
	mhi_dev_mmio_write(dev, MHISTATUS, 0);
	mhi_dev_mmio_clear_interrupts(dev);

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_reset);

int mhi_dev_restore_mmio(struct mhi_dev *dev)
{
	int rc = 0;
	uint32_t i, reg_cntl_value;
	void *reg_cntl_addr;

	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_disable_ctrl_interrupt(dev);

	mhi_dev_mmio_disable_cmdb_interrupt(dev);

	mhi_dev_mmio_mask_erdb_interrupts(dev);

	for (i = 0; i < (MHI_DEV_MMIO_RANGE/4); i++) {
		reg_cntl_addr = dev->mmio_base_addr +
				MHI_DEV_MMIO_OFFSET + (i * 4);
		reg_cntl_value = dev->mmio_backup[i];
		writel_relaxed(reg_cntl_value, reg_cntl_addr);
	}

	mhi_dev_mmio_clear_interrupts(dev);

	for (i = 0; i < MHI_MASK_ROWS_CH_EV_DB; i++) {
		/* Enable channel interrupt whose mask is enabled */
		if (dev->chdb[i].mask) {
			mhi_log(dev->vf_id, MHI_MSG_VERBOSE,
				"Enabling id: %d, chdb mask  0x%x\n",
							i, dev->chdb[i].mask);

			rc = mhi_dev_mmio_write(dev, MHI_CHDB_INT_MASK_A7_n(i),
							dev->chdb[i].mask);
			if (rc) {
				mhi_log(dev->vf_id, MHI_MSG_ERROR,
					"Error writing enable for A7\n");
				return rc;
			}
		}
	}

	/* Mask and enable control interrupt */
	mhi_dev_mmio_enable_ctrl_interrupt(dev);

	/*Enable cmdb interrupt*/
	mhi_dev_mmio_enable_cmdb_interrupt(dev);

	/*Mem barrier to ensure write is visible*/
	mb();

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_restore_mmio);

int mhi_dev_backup_mmio(struct mhi_dev *dev)
{
	uint32_t i = 0;
	void __iomem *reg_cntl_addr;

	if (WARN_ON(!dev))
		return -EINVAL;

	for (i = 0; i < MHI_DEV_MMIO_RANGE/4; i++) {
		reg_cntl_addr = (void __iomem *) (dev->mmio_base_addr +
				MHI_DEV_MMIO_OFFSET + (i * 4));
		dev->mmio_backup[i] = readl_relaxed(reg_cntl_addr);
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_backup_mmio);

int mhi_dev_get_mhi_addr(struct mhi_dev *dev)
{
	uint32_t data_value = 0;

	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_read(dev, MHICTRLBASE_LOWER, &data_value);
	dev->host_addr.ctrl_base_lsb = data_value;

	mhi_dev_mmio_read(dev, MHICTRLBASE_HIGHER, &data_value);
	dev->host_addr.ctrl_base_msb = data_value;

	mhi_dev_mmio_read(dev, MHICTRLLIMIT_LOWER, &data_value);
	dev->host_addr.ctrl_limit_lsb = data_value;

	mhi_dev_mmio_read(dev, MHICTRLLIMIT_HIGHER, &data_value);
	dev->host_addr.ctrl_limit_msb = data_value;

	mhi_dev_mmio_read(dev, MHIDATABASE_LOWER, &data_value);
	dev->host_addr.data_base_lsb = data_value;

	mhi_dev_mmio_read(dev, MHIDATABASE_HIGHER, &data_value);
	dev->host_addr.data_base_msb = data_value;

	mhi_dev_mmio_read(dev, MHIDATALIMIT_LOWER, &data_value);
	dev->host_addr.data_limit_lsb = data_value;

	mhi_dev_mmio_read(dev, MHIDATALIMIT_HIGHER, &data_value);
	dev->host_addr.data_limit_msb = data_value;

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_get_mhi_addr);

int mhi_dev_mmio_init(struct mhi_dev *dev)
{
	int rc = 0;

	if (WARN_ON(!dev))
		return -EINVAL;

	mhi_dev_mmio_read(dev, MHIREGLEN, &dev->cfg.mhi_reg_len);

	mhi_dev_mmio_masked_read(dev, MHICFG, MHICFG_NER_MASK,
				MHICFG_NER_SHIFT, &dev->cfg.event_rings);

	rc = mhi_dev_mmio_masked_read(dev, MHICFG, MHICFG_NHWER_MASK,
				MHICFG_NHWER_SHIFT, &dev->cfg.hw_event_rings);
	if (rc)
		return rc;

	rc = mhi_dev_mmio_read(dev, CHDBOFF, &dev->cfg.chdb_offset);
	if (rc)
		return rc;

	mhi_dev_mmio_read(dev, ERDBOFF, &dev->cfg.erdb_offset);

	if (!dev->is_flashless)
		mhi_dev_mmio_reset(dev);

	dev->cfg.channels = NUM_CHANNELS;

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_mmio_init);

int mhi_dev_update_ner(struct mhi_dev *dev)
{
	int rc = 0, mhi_cfg = 0;

	if (WARN_ON(!dev))
		return -EINVAL;

	rc = mhi_dev_mmio_read(dev, MHICFG, &mhi_cfg);
	if (rc)
		return rc;

	pr_debug("MHICFG: 0x%x\n", mhi_cfg);

	dev->cfg.event_rings =
		(mhi_cfg & MHICFG_NER_MASK) >> MHICFG_NER_SHIFT;
	dev->cfg.hw_event_rings =
		(mhi_cfg & MHICFG_NHWER_MASK) >> MHICFG_NHWER_SHIFT;

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_update_ner);

int mhi_dev_dump_mmio(struct mhi_dev *dev)
{
	uint32_t r1, r2, r3, r4, i, offset = 0;

	if (WARN_ON(!dev))
		return -EINVAL;

	for (i = 0; i < MHI_DEV_MMIO_RANGE/4; i += 4) {
		mhi_dev_mmio_read(dev, offset, &r1);

		mhi_dev_mmio_read(dev, offset+4, &r2);

		mhi_dev_mmio_read(dev, offset+8, &r3);

		mhi_dev_mmio_read(dev, offset+0xC, &r4);

		mhi_log(dev->vf_id, MHI_MSG_ERROR, "0x%08x 0x%08x 0x%08x 0x%08x 0x%08x\n",
				offset, r1, r2, r3, r4);
		offset += 0x10;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mhi_dev_dump_mmio);
