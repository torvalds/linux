// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2017-2019 NXP */

#include "enetc.h"

int enetc_setup_cbdr(struct device *dev, struct enetc_hw *hw, int bd_count,
		     struct enetc_cbdr *cbdr)
{
	int size = bd_count * sizeof(struct enetc_cbd);

	cbdr->bd_base = dma_alloc_coherent(dev, size, &cbdr->bd_dma_base,
					   GFP_KERNEL);
	if (!cbdr->bd_base)
		return -ENOMEM;

	/* h/w requires 128B alignment */
	if (!IS_ALIGNED(cbdr->bd_dma_base, 128)) {
		dma_free_coherent(dev, size, cbdr->bd_base,
				  cbdr->bd_dma_base);
		return -EINVAL;
	}

	cbdr->next_to_clean = 0;
	cbdr->next_to_use = 0;
	cbdr->dma_dev = dev;
	cbdr->bd_count = bd_count;

	cbdr->pir = hw->reg + ENETC_SICBDRPIR;
	cbdr->cir = hw->reg + ENETC_SICBDRCIR;
	cbdr->mr = hw->reg + ENETC_SICBDRMR;

	/* set CBDR cache attributes */
	enetc_wr(hw, ENETC_SICAR2,
		 ENETC_SICAR_RD_COHERENT | ENETC_SICAR_WR_COHERENT);

	enetc_wr(hw, ENETC_SICBDRBAR0, lower_32_bits(cbdr->bd_dma_base));
	enetc_wr(hw, ENETC_SICBDRBAR1, upper_32_bits(cbdr->bd_dma_base));
	enetc_wr(hw, ENETC_SICBDRLENR, ENETC_RTBLENR_LEN(cbdr->bd_count));

	enetc_wr_reg(cbdr->pir, cbdr->next_to_clean);
	enetc_wr_reg(cbdr->cir, cbdr->next_to_use);
	/* enable ring */
	enetc_wr_reg(cbdr->mr, BIT(31));

	return 0;
}
EXPORT_SYMBOL_GPL(enetc_setup_cbdr);

void enetc_teardown_cbdr(struct enetc_cbdr *cbdr)
{
	int size = cbdr->bd_count * sizeof(struct enetc_cbd);

	/* disable ring */
	enetc_wr_reg(cbdr->mr, 0);

	dma_free_coherent(cbdr->dma_dev, size, cbdr->bd_base,
			  cbdr->bd_dma_base);
	cbdr->bd_base = NULL;
	cbdr->dma_dev = NULL;
}
EXPORT_SYMBOL_GPL(enetc_teardown_cbdr);

static void enetc_clean_cbdr(struct enetc_cbdr *ring)
{
	struct enetc_cbd *dest_cbd;
	int i, status;

	i = ring->next_to_clean;

	while (enetc_rd_reg(ring->cir) != i) {
		dest_cbd = ENETC_CBD(*ring, i);
		status = dest_cbd->status_flags & ENETC_CBD_STATUS_MASK;
		if (status)
			dev_warn(ring->dma_dev, "CMD err %04x for cmd %04x\n",
				 status, dest_cbd->cmd);

		memset(dest_cbd, 0, sizeof(*dest_cbd));

		i = (i + 1) % ring->bd_count;
	}

	ring->next_to_clean = i;
}

static int enetc_cbd_unused(struct enetc_cbdr *r)
{
	return (r->next_to_clean - r->next_to_use - 1 + r->bd_count) %
		r->bd_count;
}

int enetc_send_cmd(struct enetc_si *si, struct enetc_cbd *cbd)
{
	struct enetc_cbdr *ring = &si->cbd_ring;
	int timeout = ENETC_CBDR_TIMEOUT;
	struct enetc_cbd *dest_cbd;
	int i;

	if (unlikely(!ring->bd_base))
		return -EIO;

	if (unlikely(!enetc_cbd_unused(ring)))
		enetc_clean_cbdr(ring);

	i = ring->next_to_use;
	dest_cbd = ENETC_CBD(*ring, i);

	/* copy command to the ring */
	*dest_cbd = *cbd;
	i = (i + 1) % ring->bd_count;

	ring->next_to_use = i;
	/* let H/W know BD ring has been updated */
	enetc_wr_reg(ring->pir, i);

	do {
		if (enetc_rd_reg(ring->cir) == i)
			break;
		udelay(10); /* cannot sleep, rtnl_lock() */
		timeout -= 10;
	} while (timeout);

	if (!timeout)
		return -EBUSY;

	/* CBD may writeback data, feedback up level */
	*cbd = *dest_cbd;

	enetc_clean_cbdr(ring);

	return 0;
}
EXPORT_SYMBOL_GPL(enetc_send_cmd);

int enetc_clear_mac_flt_entry(struct enetc_si *si, int index)
{
	struct enetc_cbd cbd;

	memset(&cbd, 0, sizeof(cbd));

	cbd.cls = 1;
	cbd.status_flags = ENETC_CBD_FLAGS_SF;
	cbd.index = cpu_to_le16(index);

	return enetc_send_cmd(si, &cbd);
}
EXPORT_SYMBOL_GPL(enetc_clear_mac_flt_entry);

int enetc_set_mac_flt_entry(struct enetc_si *si, int index,
			    char *mac_addr, int si_map)
{
	struct enetc_cbd cbd;
	u32 upper;
	u16 lower;

	memset(&cbd, 0, sizeof(cbd));

	/* fill up the "set" descriptor */
	cbd.cls = 1;
	cbd.status_flags = ENETC_CBD_FLAGS_SF;
	cbd.index = cpu_to_le16(index);
	cbd.opt[3] = cpu_to_le32(si_map);
	/* enable entry */
	cbd.opt[0] = cpu_to_le32(BIT(31));

	upper = *(const u32 *)mac_addr;
	lower = *(const u16 *)(mac_addr + 4);
	cbd.addr[0] = cpu_to_le32(upper);
	cbd.addr[1] = cpu_to_le32(lower);

	return enetc_send_cmd(si, &cbd);
}
EXPORT_SYMBOL_GPL(enetc_set_mac_flt_entry);

/* Set entry in RFS table */
int enetc_set_fs_entry(struct enetc_si *si, struct enetc_cmd_rfse *rfse,
		       int index)
{
	struct enetc_cbdr *ring = &si->cbd_ring;
	struct enetc_cbd cbd = {.cmd = 0};
	void *tmp, *tmp_align;
	dma_addr_t dma;
	int err;

	/* fill up the "set" descriptor */
	cbd.cmd = 0;
	cbd.cls = 4;
	cbd.index = cpu_to_le16(index);
	cbd.opt[3] = cpu_to_le32(0); /* SI */

	tmp = enetc_cbd_alloc_data_mem(si, &cbd, sizeof(*rfse),
				       &dma, &tmp_align);
	if (!tmp)
		return -ENOMEM;

	memcpy(tmp_align, rfse, sizeof(*rfse));

	err = enetc_send_cmd(si, &cbd);
	if (err)
		dev_err(ring->dma_dev, "FS entry add failed (%d)!", err);

	enetc_cbd_free_data_mem(si, sizeof(*rfse), tmp, &dma);

	return err;
}
EXPORT_SYMBOL_GPL(enetc_set_fs_entry);

static int enetc_cmd_rss_table(struct enetc_si *si, u32 *table, int count,
			       bool read)
{
	struct enetc_cbdr *ring = &si->cbd_ring;
	struct enetc_cbd cbd = {.cmd = 0};
	u8 *tmp, *tmp_align;
	dma_addr_t dma;
	int err, i;

	if (count < ENETC_CBD_DATA_MEM_ALIGN)
		/* HW only takes in a full 64 entry table */
		return -EINVAL;

	tmp = enetc_cbd_alloc_data_mem(si, &cbd, count,
				       &dma, (void *)&tmp_align);
	if (!tmp)
		return -ENOMEM;

	if (!read)
		for (i = 0; i < count; i++)
			tmp_align[i] = (u8)(table[i]);

	/* fill up the descriptor */
	cbd.cmd = read ? 2 : 1;
	cbd.cls = 3;

	err = enetc_send_cmd(si, &cbd);
	if (err)
		dev_err(ring->dma_dev, "RSS cmd failed (%d)!", err);

	if (read)
		for (i = 0; i < count; i++)
			table[i] = tmp_align[i];

	enetc_cbd_free_data_mem(si, count, tmp, &dma);

	return err;
}

/* Get RSS table */
int enetc_get_rss_table(struct enetc_si *si, u32 *table, int count)
{
	return enetc_cmd_rss_table(si, table, count, true);
}
EXPORT_SYMBOL_GPL(enetc_get_rss_table);

/* Set RSS table */
int enetc_set_rss_table(struct enetc_si *si, const u32 *table, int count)
{
	return enetc_cmd_rss_table(si, (u32 *)table, count, false);
}
EXPORT_SYMBOL_GPL(enetc_set_rss_table);
