// SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause)
/* Copyright 2017-2019 NXP */

#include "enetc.h"

static void enetc_clean_cbdr(struct enetc_si *si)
{
	struct enetc_cbdr *ring = &si->cbd_ring;
	struct enetc_cbd *dest_cbd;
	int i, status;

	i = ring->next_to_clean;

	while (enetc_rd_reg(ring->cir) != i) {
		dest_cbd = ENETC_CBD(*ring, i);
		status = dest_cbd->status_flags & ENETC_CBD_STATUS_MASK;
		if (status)
			dev_warn(&si->pdev->dev, "CMD err %04x for cmd %04x\n",
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

static int enetc_send_cmd(struct enetc_si *si, struct enetc_cbd *cbd)
{
	struct enetc_cbdr *ring = &si->cbd_ring;
	int timeout = ENETC_CBDR_TIMEOUT;
	struct enetc_cbd *dest_cbd;
	int i;

	if (unlikely(!ring->bd_base))
		return -EIO;

	if (unlikely(!enetc_cbd_unused(ring)))
		enetc_clean_cbdr(si);

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

	enetc_clean_cbdr(si);

	return 0;
}

int enetc_clear_mac_flt_entry(struct enetc_si *si, int index)
{
	struct enetc_cbd cbd;

	memset(&cbd, 0, sizeof(cbd));

	cbd.cls = 1;
	cbd.status_flags = ENETC_CBD_FLAGS_SF;
	cbd.index = cpu_to_le16(index);

	return enetc_send_cmd(si, &cbd);
}

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

#define RFSE_ALIGN	64
/* Set entry in RFS table */
int enetc_set_fs_entry(struct enetc_si *si, struct enetc_cmd_rfse *rfse,
		       int index)
{
	struct enetc_cbd cbd = {.cmd = 0};
	dma_addr_t dma, dma_align;
	void *tmp, *tmp_align;
	int err;

	/* fill up the "set" descriptor */
	cbd.cmd = 0;
	cbd.cls = 4;
	cbd.index = cpu_to_le16(index);
	cbd.length = cpu_to_le16(sizeof(*rfse));
	cbd.opt[3] = cpu_to_le32(0); /* SI */

	tmp = dma_alloc_coherent(&si->pdev->dev, sizeof(*rfse) + RFSE_ALIGN,
				 &dma, GFP_KERNEL);
	if (!tmp) {
		dev_err(&si->pdev->dev, "DMA mapping of RFS entry failed!\n");
		return -ENOMEM;
	}

	dma_align = ALIGN(dma, RFSE_ALIGN);
	tmp_align = PTR_ALIGN(tmp, RFSE_ALIGN);
	memcpy(tmp_align, rfse, sizeof(*rfse));

	cbd.addr[0] = cpu_to_le32(lower_32_bits(dma_align));
	cbd.addr[1] = cpu_to_le32(upper_32_bits(dma_align));

	err = enetc_send_cmd(si, &cbd);
	if (err)
		dev_err(&si->pdev->dev, "FS entry add failed (%d)!", err);

	dma_free_coherent(&si->pdev->dev, sizeof(*rfse) + RFSE_ALIGN,
			  tmp, dma);

	return err;
}

#define RSSE_ALIGN	64
static int enetc_cmd_rss_table(struct enetc_si *si, u32 *table, int count,
			       bool read)
{
	struct enetc_cbd cbd = {.cmd = 0};
	dma_addr_t dma, dma_align;
	u8 *tmp, *tmp_align;
	int err, i;

	if (count < RSSE_ALIGN)
		/* HW only takes in a full 64 entry table */
		return -EINVAL;

	tmp = dma_alloc_coherent(&si->pdev->dev, count + RSSE_ALIGN,
				 &dma, GFP_KERNEL);
	if (!tmp) {
		dev_err(&si->pdev->dev, "DMA mapping of RSS table failed!\n");
		return -ENOMEM;
	}
	dma_align = ALIGN(dma, RSSE_ALIGN);
	tmp_align = PTR_ALIGN(tmp, RSSE_ALIGN);

	if (!read)
		for (i = 0; i < count; i++)
			tmp_align[i] = (u8)(table[i]);

	/* fill up the descriptor */
	cbd.cmd = read ? 2 : 1;
	cbd.cls = 3;
	cbd.length = cpu_to_le16(count);

	cbd.addr[0] = cpu_to_le32(lower_32_bits(dma_align));
	cbd.addr[1] = cpu_to_le32(upper_32_bits(dma_align));

	err = enetc_send_cmd(si, &cbd);
	if (err)
		dev_err(&si->pdev->dev, "RSS cmd failed (%d)!", err);

	if (read)
		for (i = 0; i < count; i++)
			table[i] = tmp_align[i];

	dma_free_coherent(&si->pdev->dev, count + RSSE_ALIGN, tmp, dma);

	return err;
}

/* Get RSS table */
int enetc_get_rss_table(struct enetc_si *si, u32 *table, int count)
{
	return enetc_cmd_rss_table(si, table, count, true);
}

/* Set RSS table */
int enetc_set_rss_table(struct enetc_si *si, const u32 *table, int count)
{
	return enetc_cmd_rss_table(si, (u32 *)table, count, false);
}
