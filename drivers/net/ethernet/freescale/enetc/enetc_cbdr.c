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
