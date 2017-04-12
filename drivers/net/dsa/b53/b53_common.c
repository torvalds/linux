/*
 * B53 switch driver main logic
 *
 * Copyright (C) 2011-2013 Jonas Gorski <jogo@openwrt.org>
 * Copyright (C) 2016 Florian Fainelli <f.fainelli@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/delay.h>
#include <linux/export.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_data/b53.h>
#include <linux/phy.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <net/dsa.h>
#include <net/switchdev.h>

#include "b53_regs.h"
#include "b53_priv.h"

struct b53_mib_desc {
	u8 size;
	u8 offset;
	const char *name;
};

/* BCM5365 MIB counters */
static const struct b53_mib_desc b53_mibs_65[] = {
	{ 8, 0x00, "TxOctets" },
	{ 4, 0x08, "TxDropPkts" },
	{ 4, 0x10, "TxBroadcastPkts" },
	{ 4, 0x14, "TxMulticastPkts" },
	{ 4, 0x18, "TxUnicastPkts" },
	{ 4, 0x1c, "TxCollisions" },
	{ 4, 0x20, "TxSingleCollision" },
	{ 4, 0x24, "TxMultipleCollision" },
	{ 4, 0x28, "TxDeferredTransmit" },
	{ 4, 0x2c, "TxLateCollision" },
	{ 4, 0x30, "TxExcessiveCollision" },
	{ 4, 0x38, "TxPausePkts" },
	{ 8, 0x44, "RxOctets" },
	{ 4, 0x4c, "RxUndersizePkts" },
	{ 4, 0x50, "RxPausePkts" },
	{ 4, 0x54, "Pkts64Octets" },
	{ 4, 0x58, "Pkts65to127Octets" },
	{ 4, 0x5c, "Pkts128to255Octets" },
	{ 4, 0x60, "Pkts256to511Octets" },
	{ 4, 0x64, "Pkts512to1023Octets" },
	{ 4, 0x68, "Pkts1024to1522Octets" },
	{ 4, 0x6c, "RxOversizePkts" },
	{ 4, 0x70, "RxJabbers" },
	{ 4, 0x74, "RxAlignmentErrors" },
	{ 4, 0x78, "RxFCSErrors" },
	{ 8, 0x7c, "RxGoodOctets" },
	{ 4, 0x84, "RxDropPkts" },
	{ 4, 0x88, "RxUnicastPkts" },
	{ 4, 0x8c, "RxMulticastPkts" },
	{ 4, 0x90, "RxBroadcastPkts" },
	{ 4, 0x94, "RxSAChanges" },
	{ 4, 0x98, "RxFragments" },
};

#define B53_MIBS_65_SIZE	ARRAY_SIZE(b53_mibs_65)

/* BCM63xx MIB counters */
static const struct b53_mib_desc b53_mibs_63xx[] = {
	{ 8, 0x00, "TxOctets" },
	{ 4, 0x08, "TxDropPkts" },
	{ 4, 0x0c, "TxQoSPkts" },
	{ 4, 0x10, "TxBroadcastPkts" },
	{ 4, 0x14, "TxMulticastPkts" },
	{ 4, 0x18, "TxUnicastPkts" },
	{ 4, 0x1c, "TxCollisions" },
	{ 4, 0x20, "TxSingleCollision" },
	{ 4, 0x24, "TxMultipleCollision" },
	{ 4, 0x28, "TxDeferredTransmit" },
	{ 4, 0x2c, "TxLateCollision" },
	{ 4, 0x30, "TxExcessiveCollision" },
	{ 4, 0x38, "TxPausePkts" },
	{ 8, 0x3c, "TxQoSOctets" },
	{ 8, 0x44, "RxOctets" },
	{ 4, 0x4c, "RxUndersizePkts" },
	{ 4, 0x50, "RxPausePkts" },
	{ 4, 0x54, "Pkts64Octets" },
	{ 4, 0x58, "Pkts65to127Octets" },
	{ 4, 0x5c, "Pkts128to255Octets" },
	{ 4, 0x60, "Pkts256to511Octets" },
	{ 4, 0x64, "Pkts512to1023Octets" },
	{ 4, 0x68, "Pkts1024to1522Octets" },
	{ 4, 0x6c, "RxOversizePkts" },
	{ 4, 0x70, "RxJabbers" },
	{ 4, 0x74, "RxAlignmentErrors" },
	{ 4, 0x78, "RxFCSErrors" },
	{ 8, 0x7c, "RxGoodOctets" },
	{ 4, 0x84, "RxDropPkts" },
	{ 4, 0x88, "RxUnicastPkts" },
	{ 4, 0x8c, "RxMulticastPkts" },
	{ 4, 0x90, "RxBroadcastPkts" },
	{ 4, 0x94, "RxSAChanges" },
	{ 4, 0x98, "RxFragments" },
	{ 4, 0xa0, "RxSymbolErrors" },
	{ 4, 0xa4, "RxQoSPkts" },
	{ 8, 0xa8, "RxQoSOctets" },
	{ 4, 0xb0, "Pkts1523to2047Octets" },
	{ 4, 0xb4, "Pkts2048to4095Octets" },
	{ 4, 0xb8, "Pkts4096to8191Octets" },
	{ 4, 0xbc, "Pkts8192to9728Octets" },
	{ 4, 0xc0, "RxDiscarded" },
};

#define B53_MIBS_63XX_SIZE	ARRAY_SIZE(b53_mibs_63xx)

/* MIB counters */
static const struct b53_mib_desc b53_mibs[] = {
	{ 8, 0x00, "TxOctets" },
	{ 4, 0x08, "TxDropPkts" },
	{ 4, 0x10, "TxBroadcastPkts" },
	{ 4, 0x14, "TxMulticastPkts" },
	{ 4, 0x18, "TxUnicastPkts" },
	{ 4, 0x1c, "TxCollisions" },
	{ 4, 0x20, "TxSingleCollision" },
	{ 4, 0x24, "TxMultipleCollision" },
	{ 4, 0x28, "TxDeferredTransmit" },
	{ 4, 0x2c, "TxLateCollision" },
	{ 4, 0x30, "TxExcessiveCollision" },
	{ 4, 0x38, "TxPausePkts" },
	{ 8, 0x50, "RxOctets" },
	{ 4, 0x58, "RxUndersizePkts" },
	{ 4, 0x5c, "RxPausePkts" },
	{ 4, 0x60, "Pkts64Octets" },
	{ 4, 0x64, "Pkts65to127Octets" },
	{ 4, 0x68, "Pkts128to255Octets" },
	{ 4, 0x6c, "Pkts256to511Octets" },
	{ 4, 0x70, "Pkts512to1023Octets" },
	{ 4, 0x74, "Pkts1024to1522Octets" },
	{ 4, 0x78, "RxOversizePkts" },
	{ 4, 0x7c, "RxJabbers" },
	{ 4, 0x80, "RxAlignmentErrors" },
	{ 4, 0x84, "RxFCSErrors" },
	{ 8, 0x88, "RxGoodOctets" },
	{ 4, 0x90, "RxDropPkts" },
	{ 4, 0x94, "RxUnicastPkts" },
	{ 4, 0x98, "RxMulticastPkts" },
	{ 4, 0x9c, "RxBroadcastPkts" },
	{ 4, 0xa0, "RxSAChanges" },
	{ 4, 0xa4, "RxFragments" },
	{ 4, 0xa8, "RxJumboPkts" },
	{ 4, 0xac, "RxSymbolErrors" },
	{ 4, 0xc0, "RxDiscarded" },
};

#define B53_MIBS_SIZE	ARRAY_SIZE(b53_mibs)

static const struct b53_mib_desc b53_mibs_58xx[] = {
	{ 8, 0x00, "TxOctets" },
	{ 4, 0x08, "TxDropPkts" },
	{ 4, 0x0c, "TxQPKTQ0" },
	{ 4, 0x10, "TxBroadcastPkts" },
	{ 4, 0x14, "TxMulticastPkts" },
	{ 4, 0x18, "TxUnicastPKts" },
	{ 4, 0x1c, "TxCollisions" },
	{ 4, 0x20, "TxSingleCollision" },
	{ 4, 0x24, "TxMultipleCollision" },
	{ 4, 0x28, "TxDeferredCollision" },
	{ 4, 0x2c, "TxLateCollision" },
	{ 4, 0x30, "TxExcessiveCollision" },
	{ 4, 0x34, "TxFrameInDisc" },
	{ 4, 0x38, "TxPausePkts" },
	{ 4, 0x3c, "TxQPKTQ1" },
	{ 4, 0x40, "TxQPKTQ2" },
	{ 4, 0x44, "TxQPKTQ3" },
	{ 4, 0x48, "TxQPKTQ4" },
	{ 4, 0x4c, "TxQPKTQ5" },
	{ 8, 0x50, "RxOctets" },
	{ 4, 0x58, "RxUndersizePkts" },
	{ 4, 0x5c, "RxPausePkts" },
	{ 4, 0x60, "RxPkts64Octets" },
	{ 4, 0x64, "RxPkts65to127Octets" },
	{ 4, 0x68, "RxPkts128to255Octets" },
	{ 4, 0x6c, "RxPkts256to511Octets" },
	{ 4, 0x70, "RxPkts512to1023Octets" },
	{ 4, 0x74, "RxPkts1024toMaxPktsOctets" },
	{ 4, 0x78, "RxOversizePkts" },
	{ 4, 0x7c, "RxJabbers" },
	{ 4, 0x80, "RxAlignmentErrors" },
	{ 4, 0x84, "RxFCSErrors" },
	{ 8, 0x88, "RxGoodOctets" },
	{ 4, 0x90, "RxDropPkts" },
	{ 4, 0x94, "RxUnicastPkts" },
	{ 4, 0x98, "RxMulticastPkts" },
	{ 4, 0x9c, "RxBroadcastPkts" },
	{ 4, 0xa0, "RxSAChanges" },
	{ 4, 0xa4, "RxFragments" },
	{ 4, 0xa8, "RxJumboPkt" },
	{ 4, 0xac, "RxSymblErr" },
	{ 4, 0xb0, "InRangeErrCount" },
	{ 4, 0xb4, "OutRangeErrCount" },
	{ 4, 0xb8, "EEELpiEvent" },
	{ 4, 0xbc, "EEELpiDuration" },
	{ 4, 0xc0, "RxDiscard" },
	{ 4, 0xc8, "TxQPKTQ6" },
	{ 4, 0xcc, "TxQPKTQ7" },
	{ 4, 0xd0, "TxPkts64Octets" },
	{ 4, 0xd4, "TxPkts65to127Octets" },
	{ 4, 0xd8, "TxPkts128to255Octets" },
	{ 4, 0xdc, "TxPkts256to511Ocets" },
	{ 4, 0xe0, "TxPkts512to1023Ocets" },
	{ 4, 0xe4, "TxPkts1024toMaxPktOcets" },
};

#define B53_MIBS_58XX_SIZE	ARRAY_SIZE(b53_mibs_58xx)

static int b53_do_vlan_op(struct b53_device *dev, u8 op)
{
	unsigned int i;

	b53_write8(dev, B53_ARLIO_PAGE, dev->vta_regs[0], VTA_START_CMD | op);

	for (i = 0; i < 10; i++) {
		u8 vta;

		b53_read8(dev, B53_ARLIO_PAGE, dev->vta_regs[0], &vta);
		if (!(vta & VTA_START_CMD))
			return 0;

		usleep_range(100, 200);
	}

	return -EIO;
}

static void b53_set_vlan_entry(struct b53_device *dev, u16 vid,
			       struct b53_vlan *vlan)
{
	if (is5325(dev)) {
		u32 entry = 0;

		if (vlan->members) {
			entry = ((vlan->untag & VA_UNTAG_MASK_25) <<
				 VA_UNTAG_S_25) | vlan->members;
			if (dev->core_rev >= 3)
				entry |= VA_VALID_25_R4 | vid << VA_VID_HIGH_S;
			else
				entry |= VA_VALID_25;
		}

		b53_write32(dev, B53_VLAN_PAGE, B53_VLAN_WRITE_25, entry);
		b53_write16(dev, B53_VLAN_PAGE, B53_VLAN_TABLE_ACCESS_25, vid |
			    VTA_RW_STATE_WR | VTA_RW_OP_EN);
	} else if (is5365(dev)) {
		u16 entry = 0;

		if (vlan->members)
			entry = ((vlan->untag & VA_UNTAG_MASK_65) <<
				 VA_UNTAG_S_65) | vlan->members | VA_VALID_65;

		b53_write16(dev, B53_VLAN_PAGE, B53_VLAN_WRITE_65, entry);
		b53_write16(dev, B53_VLAN_PAGE, B53_VLAN_TABLE_ACCESS_65, vid |
			    VTA_RW_STATE_WR | VTA_RW_OP_EN);
	} else {
		b53_write16(dev, B53_ARLIO_PAGE, dev->vta_regs[1], vid);
		b53_write32(dev, B53_ARLIO_PAGE, dev->vta_regs[2],
			    (vlan->untag << VTE_UNTAG_S) | vlan->members);

		b53_do_vlan_op(dev, VTA_CMD_WRITE);
	}

	dev_dbg(dev->ds->dev, "VID: %d, members: 0x%04x, untag: 0x%04x\n",
		vid, vlan->members, vlan->untag);
}

static void b53_get_vlan_entry(struct b53_device *dev, u16 vid,
			       struct b53_vlan *vlan)
{
	if (is5325(dev)) {
		u32 entry = 0;

		b53_write16(dev, B53_VLAN_PAGE, B53_VLAN_TABLE_ACCESS_25, vid |
			    VTA_RW_STATE_RD | VTA_RW_OP_EN);
		b53_read32(dev, B53_VLAN_PAGE, B53_VLAN_WRITE_25, &entry);

		if (dev->core_rev >= 3)
			vlan->valid = !!(entry & VA_VALID_25_R4);
		else
			vlan->valid = !!(entry & VA_VALID_25);
		vlan->members = entry & VA_MEMBER_MASK;
		vlan->untag = (entry >> VA_UNTAG_S_25) & VA_UNTAG_MASK_25;

	} else if (is5365(dev)) {
		u16 entry = 0;

		b53_write16(dev, B53_VLAN_PAGE, B53_VLAN_TABLE_ACCESS_65, vid |
			    VTA_RW_STATE_WR | VTA_RW_OP_EN);
		b53_read16(dev, B53_VLAN_PAGE, B53_VLAN_WRITE_65, &entry);

		vlan->valid = !!(entry & VA_VALID_65);
		vlan->members = entry & VA_MEMBER_MASK;
		vlan->untag = (entry >> VA_UNTAG_S_65) & VA_UNTAG_MASK_65;
	} else {
		u32 entry = 0;

		b53_write16(dev, B53_ARLIO_PAGE, dev->vta_regs[1], vid);
		b53_do_vlan_op(dev, VTA_CMD_READ);
		b53_read32(dev, B53_ARLIO_PAGE, dev->vta_regs[2], &entry);
		vlan->members = entry & VTE_MEMBERS;
		vlan->untag = (entry >> VTE_UNTAG_S) & VTE_MEMBERS;
		vlan->valid = true;
	}
}

static void b53_set_forwarding(struct b53_device *dev, int enable)
{
	u8 mgmt;

	b53_read8(dev, B53_CTRL_PAGE, B53_SWITCH_MODE, &mgmt);

	if (enable)
		mgmt |= SM_SW_FWD_EN;
	else
		mgmt &= ~SM_SW_FWD_EN;

	b53_write8(dev, B53_CTRL_PAGE, B53_SWITCH_MODE, mgmt);
}

static void b53_enable_vlan(struct b53_device *dev, bool enable)
{
	u8 mgmt, vc0, vc1, vc4 = 0, vc5;

	b53_read8(dev, B53_CTRL_PAGE, B53_SWITCH_MODE, &mgmt);
	b53_read8(dev, B53_VLAN_PAGE, B53_VLAN_CTRL0, &vc0);
	b53_read8(dev, B53_VLAN_PAGE, B53_VLAN_CTRL1, &vc1);

	if (is5325(dev) || is5365(dev)) {
		b53_read8(dev, B53_VLAN_PAGE, B53_VLAN_CTRL4_25, &vc4);
		b53_read8(dev, B53_VLAN_PAGE, B53_VLAN_CTRL5_25, &vc5);
	} else if (is63xx(dev)) {
		b53_read8(dev, B53_VLAN_PAGE, B53_VLAN_CTRL4_63XX, &vc4);
		b53_read8(dev, B53_VLAN_PAGE, B53_VLAN_CTRL5_63XX, &vc5);
	} else {
		b53_read8(dev, B53_VLAN_PAGE, B53_VLAN_CTRL4, &vc4);
		b53_read8(dev, B53_VLAN_PAGE, B53_VLAN_CTRL5, &vc5);
	}

	mgmt &= ~SM_SW_FWD_MODE;

	if (enable) {
		vc0 |= VC0_VLAN_EN | VC0_VID_CHK_EN | VC0_VID_HASH_VID;
		vc1 |= VC1_RX_MCST_UNTAG_EN | VC1_RX_MCST_FWD_EN;
		vc4 &= ~VC4_ING_VID_CHECK_MASK;
		vc4 |= VC4_ING_VID_VIO_DROP << VC4_ING_VID_CHECK_S;
		vc5 |= VC5_DROP_VTABLE_MISS;

		if (is5325(dev))
			vc0 &= ~VC0_RESERVED_1;

		if (is5325(dev) || is5365(dev))
			vc1 |= VC1_RX_MCST_TAG_EN;

	} else {
		vc0 &= ~(VC0_VLAN_EN | VC0_VID_CHK_EN | VC0_VID_HASH_VID);
		vc1 &= ~(VC1_RX_MCST_UNTAG_EN | VC1_RX_MCST_FWD_EN);
		vc4 &= ~VC4_ING_VID_CHECK_MASK;
		vc5 &= ~VC5_DROP_VTABLE_MISS;

		if (is5325(dev) || is5365(dev))
			vc4 |= VC4_ING_VID_VIO_FWD << VC4_ING_VID_CHECK_S;
		else
			vc4 |= VC4_ING_VID_VIO_TO_IMP << VC4_ING_VID_CHECK_S;

		if (is5325(dev) || is5365(dev))
			vc1 &= ~VC1_RX_MCST_TAG_EN;
	}

	if (!is5325(dev) && !is5365(dev))
		vc5 &= ~VC5_VID_FFF_EN;

	b53_write8(dev, B53_VLAN_PAGE, B53_VLAN_CTRL0, vc0);
	b53_write8(dev, B53_VLAN_PAGE, B53_VLAN_CTRL1, vc1);

	if (is5325(dev) || is5365(dev)) {
		/* enable the high 8 bit vid check on 5325 */
		if (is5325(dev) && enable)
			b53_write8(dev, B53_VLAN_PAGE, B53_VLAN_CTRL3,
				   VC3_HIGH_8BIT_EN);
		else
			b53_write8(dev, B53_VLAN_PAGE, B53_VLAN_CTRL3, 0);

		b53_write8(dev, B53_VLAN_PAGE, B53_VLAN_CTRL4_25, vc4);
		b53_write8(dev, B53_VLAN_PAGE, B53_VLAN_CTRL5_25, vc5);
	} else if (is63xx(dev)) {
		b53_write16(dev, B53_VLAN_PAGE, B53_VLAN_CTRL3_63XX, 0);
		b53_write8(dev, B53_VLAN_PAGE, B53_VLAN_CTRL4_63XX, vc4);
		b53_write8(dev, B53_VLAN_PAGE, B53_VLAN_CTRL5_63XX, vc5);
	} else {
		b53_write16(dev, B53_VLAN_PAGE, B53_VLAN_CTRL3, 0);
		b53_write8(dev, B53_VLAN_PAGE, B53_VLAN_CTRL4, vc4);
		b53_write8(dev, B53_VLAN_PAGE, B53_VLAN_CTRL5, vc5);
	}

	b53_write8(dev, B53_CTRL_PAGE, B53_SWITCH_MODE, mgmt);
}

static int b53_set_jumbo(struct b53_device *dev, bool enable, bool allow_10_100)
{
	u32 port_mask = 0;
	u16 max_size = JMS_MIN_SIZE;

	if (is5325(dev) || is5365(dev))
		return -EINVAL;

	if (enable) {
		port_mask = dev->enabled_ports;
		max_size = JMS_MAX_SIZE;
		if (allow_10_100)
			port_mask |= JPM_10_100_JUMBO_EN;
	}

	b53_write32(dev, B53_JUMBO_PAGE, dev->jumbo_pm_reg, port_mask);
	return b53_write16(dev, B53_JUMBO_PAGE, dev->jumbo_size_reg, max_size);
}

static int b53_flush_arl(struct b53_device *dev, u8 mask)
{
	unsigned int i;

	b53_write8(dev, B53_CTRL_PAGE, B53_FAST_AGE_CTRL,
		   FAST_AGE_DONE | FAST_AGE_DYNAMIC | mask);

	for (i = 0; i < 10; i++) {
		u8 fast_age_ctrl;

		b53_read8(dev, B53_CTRL_PAGE, B53_FAST_AGE_CTRL,
			  &fast_age_ctrl);

		if (!(fast_age_ctrl & FAST_AGE_DONE))
			goto out;

		msleep(1);
	}

	return -ETIMEDOUT;
out:
	/* Only age dynamic entries (default behavior) */
	b53_write8(dev, B53_CTRL_PAGE, B53_FAST_AGE_CTRL, FAST_AGE_DYNAMIC);
	return 0;
}

static int b53_fast_age_port(struct b53_device *dev, int port)
{
	b53_write8(dev, B53_CTRL_PAGE, B53_FAST_AGE_PORT_CTRL, port);

	return b53_flush_arl(dev, FAST_AGE_PORT);
}

static int b53_fast_age_vlan(struct b53_device *dev, u16 vid)
{
	b53_write16(dev, B53_CTRL_PAGE, B53_FAST_AGE_VID_CTRL, vid);

	return b53_flush_arl(dev, FAST_AGE_VLAN);
}

static void b53_imp_vlan_setup(struct dsa_switch *ds, int cpu_port)
{
	struct b53_device *dev = ds->priv;
	unsigned int i;
	u16 pvlan;

	/* Enable the IMP port to be in the same VLAN as the other ports
	 * on a per-port basis such that we only have Port i and IMP in
	 * the same VLAN.
	 */
	b53_for_each_port(dev, i) {
		b53_read16(dev, B53_PVLAN_PAGE, B53_PVLAN_PORT_MASK(i), &pvlan);
		pvlan |= BIT(cpu_port);
		b53_write16(dev, B53_PVLAN_PAGE, B53_PVLAN_PORT_MASK(i), pvlan);
	}
}

static int b53_enable_port(struct dsa_switch *ds, int port,
			   struct phy_device *phy)
{
	struct b53_device *dev = ds->priv;
	unsigned int cpu_port = dev->cpu_port;
	u16 pvlan;

	/* Clear the Rx and Tx disable bits and set to no spanning tree */
	b53_write8(dev, B53_CTRL_PAGE, B53_PORT_CTRL(port), 0);

	/* Set this port, and only this one to be in the default VLAN,
	 * if member of a bridge, restore its membership prior to
	 * bringing down this port.
	 */
	b53_read16(dev, B53_PVLAN_PAGE, B53_PVLAN_PORT_MASK(port), &pvlan);
	pvlan &= ~0x1ff;
	pvlan |= BIT(port);
	pvlan |= dev->ports[port].vlan_ctl_mask;
	b53_write16(dev, B53_PVLAN_PAGE, B53_PVLAN_PORT_MASK(port), pvlan);

	b53_imp_vlan_setup(ds, cpu_port);

	return 0;
}

static void b53_disable_port(struct dsa_switch *ds, int port,
			     struct phy_device *phy)
{
	struct b53_device *dev = ds->priv;
	u8 reg;

	/* Disable Tx/Rx for the port */
	b53_read8(dev, B53_CTRL_PAGE, B53_PORT_CTRL(port), &reg);
	reg |= PORT_CTRL_RX_DISABLE | PORT_CTRL_TX_DISABLE;
	b53_write8(dev, B53_CTRL_PAGE, B53_PORT_CTRL(port), reg);
}

static void b53_enable_cpu_port(struct b53_device *dev)
{
	unsigned int cpu_port = dev->cpu_port;
	u8 port_ctrl;

	/* BCM5325 CPU port is at 8 */
	if ((is5325(dev) || is5365(dev)) && cpu_port == B53_CPU_PORT_25)
		cpu_port = B53_CPU_PORT;

	port_ctrl = PORT_CTRL_RX_BCST_EN |
		    PORT_CTRL_RX_MCST_EN |
		    PORT_CTRL_RX_UCST_EN;
	b53_write8(dev, B53_CTRL_PAGE, B53_PORT_CTRL(cpu_port), port_ctrl);
}

static void b53_enable_mib(struct b53_device *dev)
{
	u8 gc;

	b53_read8(dev, B53_MGMT_PAGE, B53_GLOBAL_CONFIG, &gc);
	gc &= ~(GC_RESET_MIB | GC_MIB_AC_EN);
	b53_write8(dev, B53_MGMT_PAGE, B53_GLOBAL_CONFIG, gc);
}

static int b53_configure_vlan(struct b53_device *dev)
{
	struct b53_vlan vl = { 0 };
	int i;

	/* clear all vlan entries */
	if (is5325(dev) || is5365(dev)) {
		for (i = 1; i < dev->num_vlans; i++)
			b53_set_vlan_entry(dev, i, &vl);
	} else {
		b53_do_vlan_op(dev, VTA_CMD_CLEAR);
	}

	b53_enable_vlan(dev, false);

	b53_for_each_port(dev, i)
		b53_write16(dev, B53_VLAN_PAGE,
			    B53_VLAN_PORT_DEF_TAG(i), 1);

	if (!is5325(dev) && !is5365(dev))
		b53_set_jumbo(dev, dev->enable_jumbo, false);

	return 0;
}

static void b53_switch_reset_gpio(struct b53_device *dev)
{
	int gpio = dev->reset_gpio;

	if (gpio < 0)
		return;

	/* Reset sequence: RESET low(50ms)->high(20ms)
	 */
	gpio_set_value(gpio, 0);
	mdelay(50);

	gpio_set_value(gpio, 1);
	mdelay(20);

	dev->current_page = 0xff;
}

static int b53_switch_reset(struct b53_device *dev)
{
	u8 mgmt;

	b53_switch_reset_gpio(dev);

	if (is539x(dev)) {
		b53_write8(dev, B53_CTRL_PAGE, B53_SOFTRESET, 0x83);
		b53_write8(dev, B53_CTRL_PAGE, B53_SOFTRESET, 0x00);
	}

	b53_read8(dev, B53_CTRL_PAGE, B53_SWITCH_MODE, &mgmt);

	if (!(mgmt & SM_SW_FWD_EN)) {
		mgmt &= ~SM_SW_FWD_MODE;
		mgmt |= SM_SW_FWD_EN;

		b53_write8(dev, B53_CTRL_PAGE, B53_SWITCH_MODE, mgmt);
		b53_read8(dev, B53_CTRL_PAGE, B53_SWITCH_MODE, &mgmt);

		if (!(mgmt & SM_SW_FWD_EN)) {
			dev_err(dev->dev, "Failed to enable switch!\n");
			return -EINVAL;
		}
	}

	b53_enable_mib(dev);

	return b53_flush_arl(dev, FAST_AGE_STATIC);
}

static int b53_phy_read16(struct dsa_switch *ds, int addr, int reg)
{
	struct b53_device *priv = ds->priv;
	u16 value = 0;
	int ret;

	if (priv->ops->phy_read16)
		ret = priv->ops->phy_read16(priv, addr, reg, &value);
	else
		ret = b53_read16(priv, B53_PORT_MII_PAGE(addr),
				 reg * 2, &value);

	return ret ? ret : value;
}

static int b53_phy_write16(struct dsa_switch *ds, int addr, int reg, u16 val)
{
	struct b53_device *priv = ds->priv;

	if (priv->ops->phy_write16)
		return priv->ops->phy_write16(priv, addr, reg, val);

	return b53_write16(priv, B53_PORT_MII_PAGE(addr), reg * 2, val);
}

static int b53_reset_switch(struct b53_device *priv)
{
	/* reset vlans */
	priv->enable_jumbo = false;

	memset(priv->vlans, 0, sizeof(*priv->vlans) * priv->num_vlans);
	memset(priv->ports, 0, sizeof(*priv->ports) * priv->num_ports);

	return b53_switch_reset(priv);
}

static int b53_apply_config(struct b53_device *priv)
{
	/* disable switching */
	b53_set_forwarding(priv, 0);

	b53_configure_vlan(priv);

	/* enable switching */
	b53_set_forwarding(priv, 1);

	return 0;
}

static void b53_reset_mib(struct b53_device *priv)
{
	u8 gc;

	b53_read8(priv, B53_MGMT_PAGE, B53_GLOBAL_CONFIG, &gc);

	b53_write8(priv, B53_MGMT_PAGE, B53_GLOBAL_CONFIG, gc | GC_RESET_MIB);
	msleep(1);
	b53_write8(priv, B53_MGMT_PAGE, B53_GLOBAL_CONFIG, gc & ~GC_RESET_MIB);
	msleep(1);
}

static const struct b53_mib_desc *b53_get_mib(struct b53_device *dev)
{
	if (is5365(dev))
		return b53_mibs_65;
	else if (is63xx(dev))
		return b53_mibs_63xx;
	else if (is58xx(dev))
		return b53_mibs_58xx;
	else
		return b53_mibs;
}

static unsigned int b53_get_mib_size(struct b53_device *dev)
{
	if (is5365(dev))
		return B53_MIBS_65_SIZE;
	else if (is63xx(dev))
		return B53_MIBS_63XX_SIZE;
	else if (is58xx(dev))
		return B53_MIBS_58XX_SIZE;
	else
		return B53_MIBS_SIZE;
}

void b53_get_strings(struct dsa_switch *ds, int port, uint8_t *data)
{
	struct b53_device *dev = ds->priv;
	const struct b53_mib_desc *mibs = b53_get_mib(dev);
	unsigned int mib_size = b53_get_mib_size(dev);
	unsigned int i;

	for (i = 0; i < mib_size; i++)
		memcpy(data + i * ETH_GSTRING_LEN,
		       mibs[i].name, ETH_GSTRING_LEN);
}
EXPORT_SYMBOL(b53_get_strings);

void b53_get_ethtool_stats(struct dsa_switch *ds, int port, uint64_t *data)
{
	struct b53_device *dev = ds->priv;
	const struct b53_mib_desc *mibs = b53_get_mib(dev);
	unsigned int mib_size = b53_get_mib_size(dev);
	const struct b53_mib_desc *s;
	unsigned int i;
	u64 val = 0;

	if (is5365(dev) && port == 5)
		port = 8;

	mutex_lock(&dev->stats_mutex);

	for (i = 0; i < mib_size; i++) {
		s = &mibs[i];

		if (s->size == 8) {
			b53_read64(dev, B53_MIB_PAGE(port), s->offset, &val);
		} else {
			u32 val32;

			b53_read32(dev, B53_MIB_PAGE(port), s->offset,
				   &val32);
			val = val32;
		}
		data[i] = (u64)val;
	}

	mutex_unlock(&dev->stats_mutex);
}
EXPORT_SYMBOL(b53_get_ethtool_stats);

int b53_get_sset_count(struct dsa_switch *ds)
{
	struct b53_device *dev = ds->priv;

	return b53_get_mib_size(dev);
}
EXPORT_SYMBOL(b53_get_sset_count);

static int b53_setup(struct dsa_switch *ds)
{
	struct b53_device *dev = ds->priv;
	unsigned int port;
	int ret;

	ret = b53_reset_switch(dev);
	if (ret) {
		dev_err(ds->dev, "failed to reset switch\n");
		return ret;
	}

	b53_reset_mib(dev);

	ret = b53_apply_config(dev);
	if (ret)
		dev_err(ds->dev, "failed to apply configuration\n");

	for (port = 0; port < dev->num_ports; port++) {
		if (BIT(port) & ds->enabled_port_mask)
			b53_enable_port(ds, port, NULL);
		else if (dsa_is_cpu_port(ds, port))
			b53_enable_cpu_port(dev);
		else
			b53_disable_port(ds, port, NULL);
	}

	return ret;
}

static void b53_adjust_link(struct dsa_switch *ds, int port,
			    struct phy_device *phydev)
{
	struct b53_device *dev = ds->priv;
	u8 rgmii_ctrl = 0, reg = 0, off;

	if (!phy_is_pseudo_fixed_link(phydev))
		return;

	/* Override the port settings */
	if (port == dev->cpu_port) {
		off = B53_PORT_OVERRIDE_CTRL;
		reg = PORT_OVERRIDE_EN;
	} else {
		off = B53_GMII_PORT_OVERRIDE_CTRL(port);
		reg = GMII_PO_EN;
	}

	/* Set the link UP */
	if (phydev->link)
		reg |= PORT_OVERRIDE_LINK;

	if (phydev->duplex == DUPLEX_FULL)
		reg |= PORT_OVERRIDE_FULL_DUPLEX;

	switch (phydev->speed) {
	case 2000:
		reg |= PORT_OVERRIDE_SPEED_2000M;
		/* fallthrough */
	case SPEED_1000:
		reg |= PORT_OVERRIDE_SPEED_1000M;
		break;
	case SPEED_100:
		reg |= PORT_OVERRIDE_SPEED_100M;
		break;
	case SPEED_10:
		reg |= PORT_OVERRIDE_SPEED_10M;
		break;
	default:
		dev_err(ds->dev, "unknown speed: %d\n", phydev->speed);
		return;
	}

	/* Enable flow control on BCM5301x's CPU port */
	if (is5301x(dev) && port == dev->cpu_port)
		reg |= PORT_OVERRIDE_RX_FLOW | PORT_OVERRIDE_TX_FLOW;

	if (phydev->pause) {
		if (phydev->asym_pause)
			reg |= PORT_OVERRIDE_TX_FLOW;
		reg |= PORT_OVERRIDE_RX_FLOW;
	}

	b53_write8(dev, B53_CTRL_PAGE, off, reg);

	if (is531x5(dev) && phy_interface_is_rgmii(phydev)) {
		if (port == 8)
			off = B53_RGMII_CTRL_IMP;
		else
			off = B53_RGMII_CTRL_P(port);

		/* Configure the port RGMII clock delay by DLL disabled and
		 * tx_clk aligned timing (restoring to reset defaults)
		 */
		b53_read8(dev, B53_CTRL_PAGE, off, &rgmii_ctrl);
		rgmii_ctrl &= ~(RGMII_CTRL_DLL_RXC | RGMII_CTRL_DLL_TXC |
				RGMII_CTRL_TIMING_SEL);

		/* PHY_INTERFACE_MODE_RGMII_TXID means TX internal delay, make
		 * sure that we enable the port TX clock internal delay to
		 * account for this internal delay that is inserted, otherwise
		 * the switch won't be able to receive correctly.
		 *
		 * PHY_INTERFACE_MODE_RGMII means that we are not introducing
		 * any delay neither on transmission nor reception, so the
		 * BCM53125 must also be configured accordingly to account for
		 * the lack of delay and introduce
		 *
		 * The BCM53125 switch has its RX clock and TX clock control
		 * swapped, hence the reason why we modify the TX clock path in
		 * the "RGMII" case
		 */
		if (phydev->interface == PHY_INTERFACE_MODE_RGMII_TXID)
			rgmii_ctrl |= RGMII_CTRL_DLL_TXC;
		if (phydev->interface == PHY_INTERFACE_MODE_RGMII)
			rgmii_ctrl |= RGMII_CTRL_DLL_TXC | RGMII_CTRL_DLL_RXC;
		rgmii_ctrl |= RGMII_CTRL_TIMING_SEL;
		b53_write8(dev, B53_CTRL_PAGE, off, rgmii_ctrl);

		dev_info(ds->dev, "Configured port %d for %s\n", port,
			 phy_modes(phydev->interface));
	}

	/* configure MII port if necessary */
	if (is5325(dev)) {
		b53_read8(dev, B53_CTRL_PAGE, B53_PORT_OVERRIDE_CTRL,
			  &reg);

		/* reverse mii needs to be enabled */
		if (!(reg & PORT_OVERRIDE_RV_MII_25)) {
			b53_write8(dev, B53_CTRL_PAGE, B53_PORT_OVERRIDE_CTRL,
				   reg | PORT_OVERRIDE_RV_MII_25);
			b53_read8(dev, B53_CTRL_PAGE, B53_PORT_OVERRIDE_CTRL,
				  &reg);

			if (!(reg & PORT_OVERRIDE_RV_MII_25)) {
				dev_err(ds->dev,
					"Failed to enable reverse MII mode\n");
				return;
			}
		}
	} else if (is5301x(dev)) {
		if (port != dev->cpu_port) {
			u8 po_reg = B53_GMII_PORT_OVERRIDE_CTRL(dev->cpu_port);
			u8 gmii_po;

			b53_read8(dev, B53_CTRL_PAGE, po_reg, &gmii_po);
			gmii_po |= GMII_PO_LINK |
				   GMII_PO_RX_FLOW |
				   GMII_PO_TX_FLOW |
				   GMII_PO_EN |
				   GMII_PO_SPEED_2000M;
			b53_write8(dev, B53_CTRL_PAGE, po_reg, gmii_po);
		}
	}
}

int b53_vlan_filtering(struct dsa_switch *ds, int port, bool vlan_filtering)
{
	return 0;
}
EXPORT_SYMBOL(b53_vlan_filtering);

int b53_vlan_prepare(struct dsa_switch *ds, int port,
		     const struct switchdev_obj_port_vlan *vlan,
		     struct switchdev_trans *trans)
{
	struct b53_device *dev = ds->priv;

	if ((is5325(dev) || is5365(dev)) && vlan->vid_begin == 0)
		return -EOPNOTSUPP;

	if (vlan->vid_end > dev->num_vlans)
		return -ERANGE;

	b53_enable_vlan(dev, true);

	return 0;
}
EXPORT_SYMBOL(b53_vlan_prepare);

void b53_vlan_add(struct dsa_switch *ds, int port,
		  const struct switchdev_obj_port_vlan *vlan,
		  struct switchdev_trans *trans)
{
	struct b53_device *dev = ds->priv;
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	unsigned int cpu_port = dev->cpu_port;
	struct b53_vlan *vl;
	u16 vid;

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; ++vid) {
		vl = &dev->vlans[vid];

		b53_get_vlan_entry(dev, vid, vl);

		vl->members |= BIT(port) | BIT(cpu_port);
		if (untagged)
			vl->untag |= BIT(port);
		else
			vl->untag &= ~BIT(port);
		vl->untag &= ~BIT(cpu_port);

		b53_set_vlan_entry(dev, vid, vl);
		b53_fast_age_vlan(dev, vid);
	}

	if (pvid) {
		b53_write16(dev, B53_VLAN_PAGE, B53_VLAN_PORT_DEF_TAG(port),
			    vlan->vid_end);
		b53_fast_age_vlan(dev, vid);
	}
}
EXPORT_SYMBOL(b53_vlan_add);

int b53_vlan_del(struct dsa_switch *ds, int port,
		 const struct switchdev_obj_port_vlan *vlan)
{
	struct b53_device *dev = ds->priv;
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	struct b53_vlan *vl;
	u16 vid;
	u16 pvid;

	b53_read16(dev, B53_VLAN_PAGE, B53_VLAN_PORT_DEF_TAG(port), &pvid);

	for (vid = vlan->vid_begin; vid <= vlan->vid_end; ++vid) {
		vl = &dev->vlans[vid];

		b53_get_vlan_entry(dev, vid, vl);

		vl->members &= ~BIT(port);

		if (pvid == vid) {
			if (is5325(dev) || is5365(dev))
				pvid = 1;
			else
				pvid = 0;
		}

		if (untagged)
			vl->untag &= ~(BIT(port));

		b53_set_vlan_entry(dev, vid, vl);
		b53_fast_age_vlan(dev, vid);
	}

	b53_write16(dev, B53_VLAN_PAGE, B53_VLAN_PORT_DEF_TAG(port), pvid);
	b53_fast_age_vlan(dev, pvid);

	return 0;
}
EXPORT_SYMBOL(b53_vlan_del);

int b53_vlan_dump(struct dsa_switch *ds, int port,
		  struct switchdev_obj_port_vlan *vlan,
		  int (*cb)(struct switchdev_obj *obj))
{
	struct b53_device *dev = ds->priv;
	u16 vid, vid_start = 0, pvid;
	struct b53_vlan *vl;
	int err = 0;

	if (is5325(dev) || is5365(dev))
		vid_start = 1;

	b53_read16(dev, B53_VLAN_PAGE, B53_VLAN_PORT_DEF_TAG(port), &pvid);

	/* Use our software cache for dumps, since we do not have any HW
	 * operation returning only the used/valid VLANs
	 */
	for (vid = vid_start; vid < dev->num_vlans; vid++) {
		vl = &dev->vlans[vid];

		if (!vl->valid)
			continue;

		if (!(vl->members & BIT(port)))
			continue;

		vlan->vid_begin = vlan->vid_end = vid;
		vlan->flags = 0;

		if (vl->untag & BIT(port))
			vlan->flags |= BRIDGE_VLAN_INFO_UNTAGGED;
		if (pvid == vid)
			vlan->flags |= BRIDGE_VLAN_INFO_PVID;

		err = cb(&vlan->obj);
		if (err)
			break;
	}

	return err;
}
EXPORT_SYMBOL(b53_vlan_dump);

/* Address Resolution Logic routines */
static int b53_arl_op_wait(struct b53_device *dev)
{
	unsigned int timeout = 10;
	u8 reg;

	do {
		b53_read8(dev, B53_ARLIO_PAGE, B53_ARLTBL_RW_CTRL, &reg);
		if (!(reg & ARLTBL_START_DONE))
			return 0;

		usleep_range(1000, 2000);
	} while (timeout--);

	dev_warn(dev->dev, "timeout waiting for ARL to finish: 0x%02x\n", reg);

	return -ETIMEDOUT;
}

static int b53_arl_rw_op(struct b53_device *dev, unsigned int op)
{
	u8 reg;

	if (op > ARLTBL_RW)
		return -EINVAL;

	b53_read8(dev, B53_ARLIO_PAGE, B53_ARLTBL_RW_CTRL, &reg);
	reg |= ARLTBL_START_DONE;
	if (op)
		reg |= ARLTBL_RW;
	else
		reg &= ~ARLTBL_RW;
	b53_write8(dev, B53_ARLIO_PAGE, B53_ARLTBL_RW_CTRL, reg);

	return b53_arl_op_wait(dev);
}

static int b53_arl_read(struct b53_device *dev, u64 mac,
			u16 vid, struct b53_arl_entry *ent, u8 *idx,
			bool is_valid)
{
	unsigned int i;
	int ret;

	ret = b53_arl_op_wait(dev);
	if (ret)
		return ret;

	/* Read the bins */
	for (i = 0; i < dev->num_arl_entries; i++) {
		u64 mac_vid;
		u32 fwd_entry;

		b53_read64(dev, B53_ARLIO_PAGE,
			   B53_ARLTBL_MAC_VID_ENTRY(i), &mac_vid);
		b53_read32(dev, B53_ARLIO_PAGE,
			   B53_ARLTBL_DATA_ENTRY(i), &fwd_entry);
		b53_arl_to_entry(ent, mac_vid, fwd_entry);

		if (!(fwd_entry & ARLTBL_VALID))
			continue;
		if ((mac_vid & ARLTBL_MAC_MASK) != mac)
			continue;
		*idx = i;
	}

	return -ENOENT;
}

static int b53_arl_op(struct b53_device *dev, int op, int port,
		      const unsigned char *addr, u16 vid, bool is_valid)
{
	struct b53_arl_entry ent;
	u32 fwd_entry;
	u64 mac, mac_vid = 0;
	u8 idx = 0;
	int ret;

	/* Convert the array into a 64-bit MAC */
	mac = ether_addr_to_u64(addr);

	/* Perform a read for the given MAC and VID */
	b53_write48(dev, B53_ARLIO_PAGE, B53_MAC_ADDR_IDX, mac);
	b53_write16(dev, B53_ARLIO_PAGE, B53_VLAN_ID_IDX, vid);

	/* Issue a read operation for this MAC */
	ret = b53_arl_rw_op(dev, 1);
	if (ret)
		return ret;

	ret = b53_arl_read(dev, mac, vid, &ent, &idx, is_valid);
	/* If this is a read, just finish now */
	if (op)
		return ret;

	/* We could not find a matching MAC, so reset to a new entry */
	if (ret) {
		fwd_entry = 0;
		idx = 1;
	}

	memset(&ent, 0, sizeof(ent));
	ent.port = port;
	ent.is_valid = is_valid;
	ent.vid = vid;
	ent.is_static = true;
	memcpy(ent.mac, addr, ETH_ALEN);
	b53_arl_from_entry(&mac_vid, &fwd_entry, &ent);

	b53_write64(dev, B53_ARLIO_PAGE,
		    B53_ARLTBL_MAC_VID_ENTRY(idx), mac_vid);
	b53_write32(dev, B53_ARLIO_PAGE,
		    B53_ARLTBL_DATA_ENTRY(idx), fwd_entry);

	return b53_arl_rw_op(dev, 0);
}

int b53_fdb_prepare(struct dsa_switch *ds, int port,
		    const struct switchdev_obj_port_fdb *fdb,
		    struct switchdev_trans *trans)
{
	struct b53_device *priv = ds->priv;

	/* 5325 and 5365 require some more massaging, but could
	 * be supported eventually
	 */
	if (is5325(priv) || is5365(priv))
		return -EOPNOTSUPP;

	return 0;
}
EXPORT_SYMBOL(b53_fdb_prepare);

void b53_fdb_add(struct dsa_switch *ds, int port,
		 const struct switchdev_obj_port_fdb *fdb,
		 struct switchdev_trans *trans)
{
	struct b53_device *priv = ds->priv;

	if (b53_arl_op(priv, 0, port, fdb->addr, fdb->vid, true))
		pr_err("%s: failed to add MAC address\n", __func__);
}
EXPORT_SYMBOL(b53_fdb_add);

int b53_fdb_del(struct dsa_switch *ds, int port,
		const struct switchdev_obj_port_fdb *fdb)
{
	struct b53_device *priv = ds->priv;

	return b53_arl_op(priv, 0, port, fdb->addr, fdb->vid, false);
}
EXPORT_SYMBOL(b53_fdb_del);

static int b53_arl_search_wait(struct b53_device *dev)
{
	unsigned int timeout = 1000;
	u8 reg;

	do {
		b53_read8(dev, B53_ARLIO_PAGE, B53_ARL_SRCH_CTL, &reg);
		if (!(reg & ARL_SRCH_STDN))
			return 0;

		if (reg & ARL_SRCH_VLID)
			return 0;

		usleep_range(1000, 2000);
	} while (timeout--);

	return -ETIMEDOUT;
}

static void b53_arl_search_rd(struct b53_device *dev, u8 idx,
			      struct b53_arl_entry *ent)
{
	u64 mac_vid;
	u32 fwd_entry;

	b53_read64(dev, B53_ARLIO_PAGE,
		   B53_ARL_SRCH_RSTL_MACVID(idx), &mac_vid);
	b53_read32(dev, B53_ARLIO_PAGE,
		   B53_ARL_SRCH_RSTL(idx), &fwd_entry);
	b53_arl_to_entry(ent, mac_vid, fwd_entry);
}

static int b53_fdb_copy(struct net_device *dev, int port,
			const struct b53_arl_entry *ent,
			struct switchdev_obj_port_fdb *fdb,
			int (*cb)(struct switchdev_obj *obj))
{
	if (!ent->is_valid)
		return 0;

	if (port != ent->port)
		return 0;

	ether_addr_copy(fdb->addr, ent->mac);
	fdb->vid = ent->vid;
	fdb->ndm_state = ent->is_static ? NUD_NOARP : NUD_REACHABLE;

	return cb(&fdb->obj);
}

int b53_fdb_dump(struct dsa_switch *ds, int port,
		 struct switchdev_obj_port_fdb *fdb,
		 int (*cb)(struct switchdev_obj *obj))
{
	struct b53_device *priv = ds->priv;
	struct net_device *dev = ds->ports[port].netdev;
	struct b53_arl_entry results[2];
	unsigned int count = 0;
	int ret;
	u8 reg;

	/* Start search operation */
	reg = ARL_SRCH_STDN;
	b53_write8(priv, B53_ARLIO_PAGE, B53_ARL_SRCH_CTL, reg);

	do {
		ret = b53_arl_search_wait(priv);
		if (ret)
			return ret;

		b53_arl_search_rd(priv, 0, &results[0]);
		ret = b53_fdb_copy(dev, port, &results[0], fdb, cb);
		if (ret)
			return ret;

		if (priv->num_arl_entries > 2) {
			b53_arl_search_rd(priv, 1, &results[1]);
			ret = b53_fdb_copy(dev, port, &results[1], fdb, cb);
			if (ret)
				return ret;

			if (!results[0].is_valid && !results[1].is_valid)
				break;
		}

	} while (count++ < 1024);

	return 0;
}
EXPORT_SYMBOL(b53_fdb_dump);

int b53_br_join(struct dsa_switch *ds, int port, struct net_device *br)
{
	struct b53_device *dev = ds->priv;
	s8 cpu_port = ds->dst->cpu_port;
	u16 pvlan, reg;
	unsigned int i;

	/* Make this port leave the all VLANs join since we will have proper
	 * VLAN entries from now on
	 */
	if (is58xx(dev)) {
		b53_read16(dev, B53_VLAN_PAGE, B53_JOIN_ALL_VLAN_EN, &reg);
		reg &= ~BIT(port);
		if ((reg & BIT(cpu_port)) == BIT(cpu_port))
			reg &= ~BIT(cpu_port);
		b53_write16(dev, B53_VLAN_PAGE, B53_JOIN_ALL_VLAN_EN, reg);
	}

	b53_read16(dev, B53_PVLAN_PAGE, B53_PVLAN_PORT_MASK(port), &pvlan);

	b53_for_each_port(dev, i) {
		if (ds->ports[i].bridge_dev != br)
			continue;

		/* Add this local port to the remote port VLAN control
		 * membership and update the remote port bitmask
		 */
		b53_read16(dev, B53_PVLAN_PAGE, B53_PVLAN_PORT_MASK(i), &reg);
		reg |= BIT(port);
		b53_write16(dev, B53_PVLAN_PAGE, B53_PVLAN_PORT_MASK(i), reg);
		dev->ports[i].vlan_ctl_mask = reg;

		pvlan |= BIT(i);
	}

	/* Configure the local port VLAN control membership to include
	 * remote ports and update the local port bitmask
	 */
	b53_write16(dev, B53_PVLAN_PAGE, B53_PVLAN_PORT_MASK(port), pvlan);
	dev->ports[port].vlan_ctl_mask = pvlan;

	return 0;
}
EXPORT_SYMBOL(b53_br_join);

void b53_br_leave(struct dsa_switch *ds, int port, struct net_device *br)
{
	struct b53_device *dev = ds->priv;
	struct b53_vlan *vl = &dev->vlans[0];
	s8 cpu_port = ds->dst->cpu_port;
	unsigned int i;
	u16 pvlan, reg, pvid;

	b53_read16(dev, B53_PVLAN_PAGE, B53_PVLAN_PORT_MASK(port), &pvlan);

	b53_for_each_port(dev, i) {
		/* Don't touch the remaining ports */
		if (ds->ports[i].bridge_dev != br)
			continue;

		b53_read16(dev, B53_PVLAN_PAGE, B53_PVLAN_PORT_MASK(i), &reg);
		reg &= ~BIT(port);
		b53_write16(dev, B53_PVLAN_PAGE, B53_PVLAN_PORT_MASK(i), reg);
		dev->ports[port].vlan_ctl_mask = reg;

		/* Prevent self removal to preserve isolation */
		if (port != i)
			pvlan &= ~BIT(i);
	}

	b53_write16(dev, B53_PVLAN_PAGE, B53_PVLAN_PORT_MASK(port), pvlan);
	dev->ports[port].vlan_ctl_mask = pvlan;

	if (is5325(dev) || is5365(dev))
		pvid = 1;
	else
		pvid = 0;

	/* Make this port join all VLANs without VLAN entries */
	if (is58xx(dev)) {
		b53_read16(dev, B53_VLAN_PAGE, B53_JOIN_ALL_VLAN_EN, &reg);
		reg |= BIT(port);
		if (!(reg & BIT(cpu_port)))
			reg |= BIT(cpu_port);
		b53_write16(dev, B53_VLAN_PAGE, B53_JOIN_ALL_VLAN_EN, reg);
	} else {
		b53_get_vlan_entry(dev, pvid, vl);
		vl->members |= BIT(port) | BIT(dev->cpu_port);
		vl->untag |= BIT(port) | BIT(dev->cpu_port);
		b53_set_vlan_entry(dev, pvid, vl);
	}
}
EXPORT_SYMBOL(b53_br_leave);

void b53_br_set_stp_state(struct dsa_switch *ds, int port, u8 state)
{
	struct b53_device *dev = ds->priv;
	u8 hw_state;
	u8 reg;

	switch (state) {
	case BR_STATE_DISABLED:
		hw_state = PORT_CTRL_DIS_STATE;
		break;
	case BR_STATE_LISTENING:
		hw_state = PORT_CTRL_LISTEN_STATE;
		break;
	case BR_STATE_LEARNING:
		hw_state = PORT_CTRL_LEARN_STATE;
		break;
	case BR_STATE_FORWARDING:
		hw_state = PORT_CTRL_FWD_STATE;
		break;
	case BR_STATE_BLOCKING:
		hw_state = PORT_CTRL_BLOCK_STATE;
		break;
	default:
		dev_err(ds->dev, "invalid STP state: %d\n", state);
		return;
	}

	b53_read8(dev, B53_CTRL_PAGE, B53_PORT_CTRL(port), &reg);
	reg &= ~PORT_CTRL_STP_STATE_MASK;
	reg |= hw_state;
	b53_write8(dev, B53_CTRL_PAGE, B53_PORT_CTRL(port), reg);
}
EXPORT_SYMBOL(b53_br_set_stp_state);

void b53_br_fast_age(struct dsa_switch *ds, int port)
{
	struct b53_device *dev = ds->priv;

	if (b53_fast_age_port(dev, port))
		dev_err(ds->dev, "fast ageing failed\n");
}
EXPORT_SYMBOL(b53_br_fast_age);

static enum dsa_tag_protocol b53_get_tag_protocol(struct dsa_switch *ds)
{
	return DSA_TAG_PROTO_NONE;
}

int b53_mirror_add(struct dsa_switch *ds, int port,
		   struct dsa_mall_mirror_tc_entry *mirror, bool ingress)
{
	struct b53_device *dev = ds->priv;
	u16 reg, loc;

	if (ingress)
		loc = B53_IG_MIR_CTL;
	else
		loc = B53_EG_MIR_CTL;

	b53_read16(dev, B53_MGMT_PAGE, loc, &reg);
	reg &= ~MIRROR_MASK;
	reg |= BIT(port);
	b53_write16(dev, B53_MGMT_PAGE, loc, reg);

	b53_read16(dev, B53_MGMT_PAGE, B53_MIR_CAP_CTL, &reg);
	reg &= ~CAP_PORT_MASK;
	reg |= mirror->to_local_port;
	reg |= MIRROR_EN;
	b53_write16(dev, B53_MGMT_PAGE, B53_MIR_CAP_CTL, reg);

	return 0;
}
EXPORT_SYMBOL(b53_mirror_add);

void b53_mirror_del(struct dsa_switch *ds, int port,
		    struct dsa_mall_mirror_tc_entry *mirror)
{
	struct b53_device *dev = ds->priv;
	bool loc_disable = false, other_loc_disable = false;
	u16 reg, loc;

	if (mirror->ingress)
		loc = B53_IG_MIR_CTL;
	else
		loc = B53_EG_MIR_CTL;

	/* Update the desired ingress/egress register */
	b53_read16(dev, B53_MGMT_PAGE, loc, &reg);
	reg &= ~BIT(port);
	if (!(reg & MIRROR_MASK))
		loc_disable = true;
	b53_write16(dev, B53_MGMT_PAGE, loc, reg);

	/* Now look at the other one to know if we can disable mirroring
	 * entirely
	 */
	if (mirror->ingress)
		b53_read16(dev, B53_MGMT_PAGE, B53_EG_MIR_CTL, &reg);
	else
		b53_read16(dev, B53_MGMT_PAGE, B53_IG_MIR_CTL, &reg);
	if (!(reg & MIRROR_MASK))
		other_loc_disable = true;

	b53_read16(dev, B53_MGMT_PAGE, B53_MIR_CAP_CTL, &reg);
	/* Both no longer have ports, let's disable mirroring */
	if (loc_disable && other_loc_disable) {
		reg &= ~MIRROR_EN;
		reg &= ~mirror->to_local_port;
	}
	b53_write16(dev, B53_MGMT_PAGE, B53_MIR_CAP_CTL, reg);
}
EXPORT_SYMBOL(b53_mirror_del);

static const struct dsa_switch_ops b53_switch_ops = {
	.get_tag_protocol	= b53_get_tag_protocol,
	.setup			= b53_setup,
	.get_strings		= b53_get_strings,
	.get_ethtool_stats	= b53_get_ethtool_stats,
	.get_sset_count		= b53_get_sset_count,
	.phy_read		= b53_phy_read16,
	.phy_write		= b53_phy_write16,
	.adjust_link		= b53_adjust_link,
	.port_enable		= b53_enable_port,
	.port_disable		= b53_disable_port,
	.port_bridge_join	= b53_br_join,
	.port_bridge_leave	= b53_br_leave,
	.port_stp_state_set	= b53_br_set_stp_state,
	.port_fast_age		= b53_br_fast_age,
	.port_vlan_filtering	= b53_vlan_filtering,
	.port_vlan_prepare	= b53_vlan_prepare,
	.port_vlan_add		= b53_vlan_add,
	.port_vlan_del		= b53_vlan_del,
	.port_vlan_dump		= b53_vlan_dump,
	.port_fdb_prepare	= b53_fdb_prepare,
	.port_fdb_dump		= b53_fdb_dump,
	.port_fdb_add		= b53_fdb_add,
	.port_fdb_del		= b53_fdb_del,
	.port_mirror_add	= b53_mirror_add,
	.port_mirror_del	= b53_mirror_del,
};

struct b53_chip_data {
	u32 chip_id;
	const char *dev_name;
	u16 vlans;
	u16 enabled_ports;
	u8 cpu_port;
	u8 vta_regs[3];
	u8 arl_entries;
	u8 duplex_reg;
	u8 jumbo_pm_reg;
	u8 jumbo_size_reg;
};

#define B53_VTA_REGS	\
	{ B53_VT_ACCESS, B53_VT_INDEX, B53_VT_ENTRY }
#define B53_VTA_REGS_9798 \
	{ B53_VT_ACCESS_9798, B53_VT_INDEX_9798, B53_VT_ENTRY_9798 }
#define B53_VTA_REGS_63XX \
	{ B53_VT_ACCESS_63XX, B53_VT_INDEX_63XX, B53_VT_ENTRY_63XX }

static const struct b53_chip_data b53_switch_chips[] = {
	{
		.chip_id = BCM5325_DEVICE_ID,
		.dev_name = "BCM5325",
		.vlans = 16,
		.enabled_ports = 0x1f,
		.arl_entries = 2,
		.cpu_port = B53_CPU_PORT_25,
		.duplex_reg = B53_DUPLEX_STAT_FE,
	},
	{
		.chip_id = BCM5365_DEVICE_ID,
		.dev_name = "BCM5365",
		.vlans = 256,
		.enabled_ports = 0x1f,
		.arl_entries = 2,
		.cpu_port = B53_CPU_PORT_25,
		.duplex_reg = B53_DUPLEX_STAT_FE,
	},
	{
		.chip_id = BCM5395_DEVICE_ID,
		.dev_name = "BCM5395",
		.vlans = 4096,
		.enabled_ports = 0x1f,
		.arl_entries = 4,
		.cpu_port = B53_CPU_PORT,
		.vta_regs = B53_VTA_REGS,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
	{
		.chip_id = BCM5397_DEVICE_ID,
		.dev_name = "BCM5397",
		.vlans = 4096,
		.enabled_ports = 0x1f,
		.arl_entries = 4,
		.cpu_port = B53_CPU_PORT,
		.vta_regs = B53_VTA_REGS_9798,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
	{
		.chip_id = BCM5398_DEVICE_ID,
		.dev_name = "BCM5398",
		.vlans = 4096,
		.enabled_ports = 0x7f,
		.arl_entries = 4,
		.cpu_port = B53_CPU_PORT,
		.vta_regs = B53_VTA_REGS_9798,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
	{
		.chip_id = BCM53115_DEVICE_ID,
		.dev_name = "BCM53115",
		.vlans = 4096,
		.enabled_ports = 0x1f,
		.arl_entries = 4,
		.vta_regs = B53_VTA_REGS,
		.cpu_port = B53_CPU_PORT,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
	{
		.chip_id = BCM53125_DEVICE_ID,
		.dev_name = "BCM53125",
		.vlans = 4096,
		.enabled_ports = 0xff,
		.cpu_port = B53_CPU_PORT,
		.vta_regs = B53_VTA_REGS,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
	{
		.chip_id = BCM53128_DEVICE_ID,
		.dev_name = "BCM53128",
		.vlans = 4096,
		.enabled_ports = 0x1ff,
		.arl_entries = 4,
		.cpu_port = B53_CPU_PORT,
		.vta_regs = B53_VTA_REGS,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
	{
		.chip_id = BCM63XX_DEVICE_ID,
		.dev_name = "BCM63xx",
		.vlans = 4096,
		.enabled_ports = 0, /* pdata must provide them */
		.arl_entries = 4,
		.cpu_port = B53_CPU_PORT,
		.vta_regs = B53_VTA_REGS_63XX,
		.duplex_reg = B53_DUPLEX_STAT_63XX,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK_63XX,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE_63XX,
	},
	{
		.chip_id = BCM53010_DEVICE_ID,
		.dev_name = "BCM53010",
		.vlans = 4096,
		.enabled_ports = 0x1f,
		.arl_entries = 4,
		.cpu_port = B53_CPU_PORT_25, /* TODO: auto detect */
		.vta_regs = B53_VTA_REGS,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
	{
		.chip_id = BCM53011_DEVICE_ID,
		.dev_name = "BCM53011",
		.vlans = 4096,
		.enabled_ports = 0x1bf,
		.arl_entries = 4,
		.cpu_port = B53_CPU_PORT_25, /* TODO: auto detect */
		.vta_regs = B53_VTA_REGS,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
	{
		.chip_id = BCM53012_DEVICE_ID,
		.dev_name = "BCM53012",
		.vlans = 4096,
		.enabled_ports = 0x1bf,
		.arl_entries = 4,
		.cpu_port = B53_CPU_PORT_25, /* TODO: auto detect */
		.vta_regs = B53_VTA_REGS,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
	{
		.chip_id = BCM53018_DEVICE_ID,
		.dev_name = "BCM53018",
		.vlans = 4096,
		.enabled_ports = 0x1f,
		.arl_entries = 4,
		.cpu_port = B53_CPU_PORT_25, /* TODO: auto detect */
		.vta_regs = B53_VTA_REGS,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
	{
		.chip_id = BCM53019_DEVICE_ID,
		.dev_name = "BCM53019",
		.vlans = 4096,
		.enabled_ports = 0x1f,
		.arl_entries = 4,
		.cpu_port = B53_CPU_PORT_25, /* TODO: auto detect */
		.vta_regs = B53_VTA_REGS,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
	{
		.chip_id = BCM58XX_DEVICE_ID,
		.dev_name = "BCM585xx/586xx/88312",
		.vlans	= 4096,
		.enabled_ports = 0x1ff,
		.arl_entries = 4,
		.cpu_port = B53_CPU_PORT_25,
		.vta_regs = B53_VTA_REGS,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
	{
		.chip_id = BCM7445_DEVICE_ID,
		.dev_name = "BCM7445",
		.vlans	= 4096,
		.enabled_ports = 0x1ff,
		.arl_entries = 4,
		.cpu_port = B53_CPU_PORT,
		.vta_regs = B53_VTA_REGS,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
	{
		.chip_id = BCM7278_DEVICE_ID,
		.dev_name = "BCM7278",
		.vlans = 4096,
		.enabled_ports = 0x1ff,
		.arl_entries= 4,
		.cpu_port = B53_CPU_PORT,
		.vta_regs = B53_VTA_REGS,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
};

static int b53_switch_init(struct b53_device *dev)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(b53_switch_chips); i++) {
		const struct b53_chip_data *chip = &b53_switch_chips[i];

		if (chip->chip_id == dev->chip_id) {
			if (!dev->enabled_ports)
				dev->enabled_ports = chip->enabled_ports;
			dev->name = chip->dev_name;
			dev->duplex_reg = chip->duplex_reg;
			dev->vta_regs[0] = chip->vta_regs[0];
			dev->vta_regs[1] = chip->vta_regs[1];
			dev->vta_regs[2] = chip->vta_regs[2];
			dev->jumbo_pm_reg = chip->jumbo_pm_reg;
			dev->cpu_port = chip->cpu_port;
			dev->num_vlans = chip->vlans;
			dev->num_arl_entries = chip->arl_entries;
			break;
		}
	}

	/* check which BCM5325x version we have */
	if (is5325(dev)) {
		u8 vc4;

		b53_read8(dev, B53_VLAN_PAGE, B53_VLAN_CTRL4_25, &vc4);

		/* check reserved bits */
		switch (vc4 & 3) {
		case 1:
			/* BCM5325E */
			break;
		case 3:
			/* BCM5325F - do not use port 4 */
			dev->enabled_ports &= ~BIT(4);
			break;
		default:
/* On the BCM47XX SoCs this is the supported internal switch.*/
#ifndef CONFIG_BCM47XX
			/* BCM5325M */
			return -EINVAL;
#else
			break;
#endif
		}
	} else if (dev->chip_id == BCM53115_DEVICE_ID) {
		u64 strap_value;

		b53_read48(dev, B53_STAT_PAGE, B53_STRAP_VALUE, &strap_value);
		/* use second IMP port if GMII is enabled */
		if (strap_value & SV_GMII_CTRL_115)
			dev->cpu_port = 5;
	}

	/* cpu port is always last */
	dev->num_ports = dev->cpu_port + 1;
	dev->enabled_ports |= BIT(dev->cpu_port);

	dev->ports = devm_kzalloc(dev->dev,
				  sizeof(struct b53_port) * dev->num_ports,
				  GFP_KERNEL);
	if (!dev->ports)
		return -ENOMEM;

	dev->vlans = devm_kzalloc(dev->dev,
				  sizeof(struct b53_vlan) * dev->num_vlans,
				  GFP_KERNEL);
	if (!dev->vlans)
		return -ENOMEM;

	dev->reset_gpio = b53_switch_get_reset_gpio(dev);
	if (dev->reset_gpio >= 0) {
		ret = devm_gpio_request_one(dev->dev, dev->reset_gpio,
					    GPIOF_OUT_INIT_HIGH, "robo_reset");
		if (ret)
			return ret;
	}

	return 0;
}

struct b53_device *b53_switch_alloc(struct device *base,
				    const struct b53_io_ops *ops,
				    void *priv)
{
	struct dsa_switch *ds;
	struct b53_device *dev;

	ds = dsa_switch_alloc(base, DSA_MAX_PORTS);
	if (!ds)
		return NULL;

	dev = devm_kzalloc(base, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	ds->priv = dev;
	dev->dev = base;

	dev->ds = ds;
	dev->priv = priv;
	dev->ops = ops;
	ds->ops = &b53_switch_ops;
	mutex_init(&dev->reg_mutex);
	mutex_init(&dev->stats_mutex);

	return dev;
}
EXPORT_SYMBOL(b53_switch_alloc);

int b53_switch_detect(struct b53_device *dev)
{
	u32 id32;
	u16 tmp;
	u8 id8;
	int ret;

	ret = b53_read8(dev, B53_MGMT_PAGE, B53_DEVICE_ID, &id8);
	if (ret)
		return ret;

	switch (id8) {
	case 0:
		/* BCM5325 and BCM5365 do not have this register so reads
		 * return 0. But the read operation did succeed, so assume this
		 * is one of them.
		 *
		 * Next check if we can write to the 5325's VTA register; for
		 * 5365 it is read only.
		 */
		b53_write16(dev, B53_VLAN_PAGE, B53_VLAN_TABLE_ACCESS_25, 0xf);
		b53_read16(dev, B53_VLAN_PAGE, B53_VLAN_TABLE_ACCESS_25, &tmp);

		if (tmp == 0xf)
			dev->chip_id = BCM5325_DEVICE_ID;
		else
			dev->chip_id = BCM5365_DEVICE_ID;
		break;
	case BCM5395_DEVICE_ID:
	case BCM5397_DEVICE_ID:
	case BCM5398_DEVICE_ID:
		dev->chip_id = id8;
		break;
	default:
		ret = b53_read32(dev, B53_MGMT_PAGE, B53_DEVICE_ID, &id32);
		if (ret)
			return ret;

		switch (id32) {
		case BCM53115_DEVICE_ID:
		case BCM53125_DEVICE_ID:
		case BCM53128_DEVICE_ID:
		case BCM53010_DEVICE_ID:
		case BCM53011_DEVICE_ID:
		case BCM53012_DEVICE_ID:
		case BCM53018_DEVICE_ID:
		case BCM53019_DEVICE_ID:
			dev->chip_id = id32;
			break;
		default:
			pr_err("unsupported switch detected (BCM53%02x/BCM%x)\n",
			       id8, id32);
			return -ENODEV;
		}
	}

	if (dev->chip_id == BCM5325_DEVICE_ID)
		return b53_read8(dev, B53_STAT_PAGE, B53_REV_ID_25,
				 &dev->core_rev);
	else
		return b53_read8(dev, B53_MGMT_PAGE, B53_REV_ID,
				 &dev->core_rev);
}
EXPORT_SYMBOL(b53_switch_detect);

int b53_switch_register(struct b53_device *dev)
{
	int ret;

	if (dev->pdata) {
		dev->chip_id = dev->pdata->chip_id;
		dev->enabled_ports = dev->pdata->enabled_ports;
	}

	if (!dev->chip_id && b53_switch_detect(dev))
		return -EINVAL;

	ret = b53_switch_init(dev);
	if (ret)
		return ret;

	pr_info("found switch: %s, rev %i\n", dev->name, dev->core_rev);

	return dsa_register_switch(dev->ds, dev->ds->dev);
}
EXPORT_SYMBOL(b53_switch_register);

MODULE_AUTHOR("Jonas Gorski <jogo@openwrt.org>");
MODULE_DESCRIPTION("B53 switch library");
MODULE_LICENSE("Dual BSD/GPL");
