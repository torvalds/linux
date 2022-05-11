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

#include <linux/delay.h>
#include <linux/export.h>
#include <linux/gpio.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_data/b53.h>
#include <linux/phy.h>
#include <linux/phylink.h>
#include <linux/etherdevice.h>
#include <linux/if_bridge.h>
#include <net/dsa.h>

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

	/* Include IMP port in dumb forwarding mode
	 */
	b53_read8(dev, B53_CTRL_PAGE, B53_SWITCH_CTRL, &mgmt);
	mgmt |= B53_MII_DUMB_FWDG_EN;
	b53_write8(dev, B53_CTRL_PAGE, B53_SWITCH_CTRL, mgmt);

	/* Look at B53_UC_FWD_EN and B53_MC_FWD_EN to decide whether
	 * frames should be flooded or not.
	 */
	b53_read8(dev, B53_CTRL_PAGE, B53_IP_MULTICAST_CTRL, &mgmt);
	mgmt |= B53_UC_FWD_EN | B53_MC_FWD_EN | B53_IPMC_FWD_EN;
	b53_write8(dev, B53_CTRL_PAGE, B53_IP_MULTICAST_CTRL, mgmt);
}

static void b53_enable_vlan(struct b53_device *dev, int port, bool enable,
			    bool enable_filtering)
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

	if (enable) {
		vc0 |= VC0_VLAN_EN | VC0_VID_CHK_EN | VC0_VID_HASH_VID;
		vc1 |= VC1_RX_MCST_UNTAG_EN | VC1_RX_MCST_FWD_EN;
		vc4 &= ~VC4_ING_VID_CHECK_MASK;
		if (enable_filtering) {
			vc4 |= VC4_ING_VID_VIO_DROP << VC4_ING_VID_CHECK_S;
			vc5 |= VC5_DROP_VTABLE_MISS;
		} else {
			vc4 |= VC4_ING_VID_VIO_FWD << VC4_ING_VID_CHECK_S;
			vc5 &= ~VC5_DROP_VTABLE_MISS;
		}

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

	dev->vlan_enabled = enable;

	dev_dbg(dev->dev, "Port %d VLAN enabled: %d, filtering: %d\n",
		port, enable, enable_filtering);
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

void b53_imp_vlan_setup(struct dsa_switch *ds, int cpu_port)
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
EXPORT_SYMBOL(b53_imp_vlan_setup);

static void b53_port_set_ucast_flood(struct b53_device *dev, int port,
				     bool unicast)
{
	u16 uc;

	b53_read16(dev, B53_CTRL_PAGE, B53_UC_FLOOD_MASK, &uc);
	if (unicast)
		uc |= BIT(port);
	else
		uc &= ~BIT(port);
	b53_write16(dev, B53_CTRL_PAGE, B53_UC_FLOOD_MASK, uc);
}

static void b53_port_set_mcast_flood(struct b53_device *dev, int port,
				     bool multicast)
{
	u16 mc;

	b53_read16(dev, B53_CTRL_PAGE, B53_MC_FLOOD_MASK, &mc);
	if (multicast)
		mc |= BIT(port);
	else
		mc &= ~BIT(port);
	b53_write16(dev, B53_CTRL_PAGE, B53_MC_FLOOD_MASK, mc);

	b53_read16(dev, B53_CTRL_PAGE, B53_IPMC_FLOOD_MASK, &mc);
	if (multicast)
		mc |= BIT(port);
	else
		mc &= ~BIT(port);
	b53_write16(dev, B53_CTRL_PAGE, B53_IPMC_FLOOD_MASK, mc);
}

static void b53_port_set_learning(struct b53_device *dev, int port,
				  bool learning)
{
	u16 reg;

	b53_read16(dev, B53_CTRL_PAGE, B53_DIS_LEARNING, &reg);
	if (learning)
		reg &= ~BIT(port);
	else
		reg |= BIT(port);
	b53_write16(dev, B53_CTRL_PAGE, B53_DIS_LEARNING, reg);
}

int b53_enable_port(struct dsa_switch *ds, int port, struct phy_device *phy)
{
	struct b53_device *dev = ds->priv;
	unsigned int cpu_port;
	int ret = 0;
	u16 pvlan;

	if (!dsa_is_user_port(ds, port))
		return 0;

	cpu_port = dsa_to_port(ds, port)->cpu_dp->index;

	b53_port_set_ucast_flood(dev, port, true);
	b53_port_set_mcast_flood(dev, port, true);
	b53_port_set_learning(dev, port, false);

	if (dev->ops->irq_enable)
		ret = dev->ops->irq_enable(dev, port);
	if (ret)
		return ret;

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

	/* If EEE was enabled, restore it */
	if (dev->ports[port].eee.eee_enabled)
		b53_eee_enable_set(ds, port, true);

	return 0;
}
EXPORT_SYMBOL(b53_enable_port);

void b53_disable_port(struct dsa_switch *ds, int port)
{
	struct b53_device *dev = ds->priv;
	u8 reg;

	/* Disable Tx/Rx for the port */
	b53_read8(dev, B53_CTRL_PAGE, B53_PORT_CTRL(port), &reg);
	reg |= PORT_CTRL_RX_DISABLE | PORT_CTRL_TX_DISABLE;
	b53_write8(dev, B53_CTRL_PAGE, B53_PORT_CTRL(port), reg);

	if (dev->ops->irq_disable)
		dev->ops->irq_disable(dev, port);
}
EXPORT_SYMBOL(b53_disable_port);

void b53_brcm_hdr_setup(struct dsa_switch *ds, int port)
{
	struct b53_device *dev = ds->priv;
	bool tag_en = !(dev->tag_protocol == DSA_TAG_PROTO_NONE);
	u8 hdr_ctl, val;
	u16 reg;

	/* Resolve which bit controls the Broadcom tag */
	switch (port) {
	case 8:
		val = BRCM_HDR_P8_EN;
		break;
	case 7:
		val = BRCM_HDR_P7_EN;
		break;
	case 5:
		val = BRCM_HDR_P5_EN;
		break;
	default:
		val = 0;
		break;
	}

	/* Enable management mode if tagging is requested */
	b53_read8(dev, B53_CTRL_PAGE, B53_SWITCH_MODE, &hdr_ctl);
	if (tag_en)
		hdr_ctl |= SM_SW_FWD_MODE;
	else
		hdr_ctl &= ~SM_SW_FWD_MODE;
	b53_write8(dev, B53_CTRL_PAGE, B53_SWITCH_MODE, hdr_ctl);

	/* Configure the appropriate IMP port */
	b53_read8(dev, B53_MGMT_PAGE, B53_GLOBAL_CONFIG, &hdr_ctl);
	if (port == 8)
		hdr_ctl |= GC_FRM_MGMT_PORT_MII;
	else if (port == 5)
		hdr_ctl |= GC_FRM_MGMT_PORT_M;
	b53_write8(dev, B53_MGMT_PAGE, B53_GLOBAL_CONFIG, hdr_ctl);

	/* Enable Broadcom tags for IMP port */
	b53_read8(dev, B53_MGMT_PAGE, B53_BRCM_HDR, &hdr_ctl);
	if (tag_en)
		hdr_ctl |= val;
	else
		hdr_ctl &= ~val;
	b53_write8(dev, B53_MGMT_PAGE, B53_BRCM_HDR, hdr_ctl);

	/* Registers below are only accessible on newer devices */
	if (!is58xx(dev))
		return;

	/* Enable reception Broadcom tag for CPU TX (switch RX) to
	 * allow us to tag outgoing frames
	 */
	b53_read16(dev, B53_MGMT_PAGE, B53_BRCM_HDR_RX_DIS, &reg);
	if (tag_en)
		reg &= ~BIT(port);
	else
		reg |= BIT(port);
	b53_write16(dev, B53_MGMT_PAGE, B53_BRCM_HDR_RX_DIS, reg);

	/* Enable transmission of Broadcom tags from the switch (CPU RX) to
	 * allow delivering frames to the per-port net_devices
	 */
	b53_read16(dev, B53_MGMT_PAGE, B53_BRCM_HDR_TX_DIS, &reg);
	if (tag_en)
		reg &= ~BIT(port);
	else
		reg |= BIT(port);
	b53_write16(dev, B53_MGMT_PAGE, B53_BRCM_HDR_TX_DIS, reg);
}
EXPORT_SYMBOL(b53_brcm_hdr_setup);

static void b53_enable_cpu_port(struct b53_device *dev, int port)
{
	u8 port_ctrl;

	/* BCM5325 CPU port is at 8 */
	if ((is5325(dev) || is5365(dev)) && port == B53_CPU_PORT_25)
		port = B53_CPU_PORT;

	port_ctrl = PORT_CTRL_RX_BCST_EN |
		    PORT_CTRL_RX_MCST_EN |
		    PORT_CTRL_RX_UCST_EN;
	b53_write8(dev, B53_CTRL_PAGE, B53_PORT_CTRL(port), port_ctrl);

	b53_brcm_hdr_setup(dev->ds, port);

	b53_port_set_ucast_flood(dev, port, true);
	b53_port_set_mcast_flood(dev, port, true);
	b53_port_set_learning(dev, port, false);
}

static void b53_enable_mib(struct b53_device *dev)
{
	u8 gc;

	b53_read8(dev, B53_MGMT_PAGE, B53_GLOBAL_CONFIG, &gc);
	gc &= ~(GC_RESET_MIB | GC_MIB_AC_EN);
	b53_write8(dev, B53_MGMT_PAGE, B53_GLOBAL_CONFIG, gc);
}

static u16 b53_default_pvid(struct b53_device *dev)
{
	if (is5325(dev) || is5365(dev))
		return 1;
	else
		return 0;
}

static bool b53_vlan_port_needs_forced_tagged(struct dsa_switch *ds, int port)
{
	struct b53_device *dev = ds->priv;

	return dev->tag_protocol == DSA_TAG_PROTO_NONE && dsa_is_cpu_port(ds, port);
}

int b53_configure_vlan(struct dsa_switch *ds)
{
	struct b53_device *dev = ds->priv;
	struct b53_vlan vl = { 0 };
	struct b53_vlan *v;
	int i, def_vid;
	u16 vid;

	def_vid = b53_default_pvid(dev);

	/* clear all vlan entries */
	if (is5325(dev) || is5365(dev)) {
		for (i = def_vid; i < dev->num_vlans; i++)
			b53_set_vlan_entry(dev, i, &vl);
	} else {
		b53_do_vlan_op(dev, VTA_CMD_CLEAR);
	}

	b53_enable_vlan(dev, -1, dev->vlan_enabled, ds->vlan_filtering);

	/* Create an untagged VLAN entry for the default PVID in case
	 * CONFIG_VLAN_8021Q is disabled and there are no calls to
	 * dsa_slave_vlan_rx_add_vid() to create the default VLAN
	 * entry. Do this only when the tagging protocol is not
	 * DSA_TAG_PROTO_NONE
	 */
	b53_for_each_port(dev, i) {
		v = &dev->vlans[def_vid];
		v->members |= BIT(i);
		if (!b53_vlan_port_needs_forced_tagged(ds, i))
			v->untag = v->members;
		b53_write16(dev, B53_VLAN_PAGE,
			    B53_VLAN_PORT_DEF_TAG(i), def_vid);
	}

	/* Upon initial call we have not set-up any VLANs, but upon
	 * system resume, we need to restore all VLAN entries.
	 */
	for (vid = def_vid; vid < dev->num_vlans; vid++) {
		v = &dev->vlans[vid];

		if (!v->members)
			continue;

		b53_set_vlan_entry(dev, vid, v);
		b53_fast_age_vlan(dev, vid);
	}

	return 0;
}
EXPORT_SYMBOL(b53_configure_vlan);

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
	unsigned int timeout = 1000;
	u8 mgmt, reg;

	b53_switch_reset_gpio(dev);

	if (is539x(dev)) {
		b53_write8(dev, B53_CTRL_PAGE, B53_SOFTRESET, 0x83);
		b53_write8(dev, B53_CTRL_PAGE, B53_SOFTRESET, 0x00);
	}

	/* This is specific to 58xx devices here, do not use is58xx() which
	 * covers the larger Starfigther 2 family, including 7445/7278 which
	 * still use this driver as a library and need to perform the reset
	 * earlier.
	 */
	if (dev->chip_id == BCM58XX_DEVICE_ID ||
	    dev->chip_id == BCM583XX_DEVICE_ID) {
		b53_read8(dev, B53_CTRL_PAGE, B53_SOFTRESET, &reg);
		reg |= SW_RST | EN_SW_RST | EN_CH_RST;
		b53_write8(dev, B53_CTRL_PAGE, B53_SOFTRESET, reg);

		do {
			b53_read8(dev, B53_CTRL_PAGE, B53_SOFTRESET, &reg);
			if (!(reg & SW_RST))
				break;

			usleep_range(1000, 2000);
		} while (timeout-- > 0);

		if (timeout == 0) {
			dev_err(dev->dev,
				"Timeout waiting for SW_RST to clear!\n");
			return -ETIMEDOUT;
		}
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
	memset(priv->vlans, 0, sizeof(*priv->vlans) * priv->num_vlans);
	memset(priv->ports, 0, sizeof(*priv->ports) * priv->num_ports);

	priv->serdes_lane = B53_INVALID_LANE;

	return b53_switch_reset(priv);
}

static int b53_apply_config(struct b53_device *priv)
{
	/* disable switching */
	b53_set_forwarding(priv, 0);

	b53_configure_vlan(priv->ds);

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

static struct phy_device *b53_get_phy_device(struct dsa_switch *ds, int port)
{
	/* These ports typically do not have built-in PHYs */
	switch (port) {
	case B53_CPU_PORT_25:
	case 7:
	case B53_CPU_PORT:
		return NULL;
	}

	return mdiobus_get_phy(ds->slave_mii_bus, port);
}

void b53_get_strings(struct dsa_switch *ds, int port, u32 stringset,
		     uint8_t *data)
{
	struct b53_device *dev = ds->priv;
	const struct b53_mib_desc *mibs = b53_get_mib(dev);
	unsigned int mib_size = b53_get_mib_size(dev);
	struct phy_device *phydev;
	unsigned int i;

	if (stringset == ETH_SS_STATS) {
		for (i = 0; i < mib_size; i++)
			strlcpy(data + i * ETH_GSTRING_LEN,
				mibs[i].name, ETH_GSTRING_LEN);
	} else if (stringset == ETH_SS_PHY_STATS) {
		phydev = b53_get_phy_device(ds, port);
		if (!phydev)
			return;

		phy_ethtool_get_strings(phydev, data);
	}
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

void b53_get_ethtool_phy_stats(struct dsa_switch *ds, int port, uint64_t *data)
{
	struct phy_device *phydev;

	phydev = b53_get_phy_device(ds, port);
	if (!phydev)
		return;

	phy_ethtool_get_stats(phydev, NULL, data);
}
EXPORT_SYMBOL(b53_get_ethtool_phy_stats);

int b53_get_sset_count(struct dsa_switch *ds, int port, int sset)
{
	struct b53_device *dev = ds->priv;
	struct phy_device *phydev;

	if (sset == ETH_SS_STATS) {
		return b53_get_mib_size(dev);
	} else if (sset == ETH_SS_PHY_STATS) {
		phydev = b53_get_phy_device(ds, port);
		if (!phydev)
			return 0;

		return phy_ethtool_get_sset_count(phydev);
	}

	return 0;
}
EXPORT_SYMBOL(b53_get_sset_count);

enum b53_devlink_resource_id {
	B53_DEVLINK_PARAM_ID_VLAN_TABLE,
};

static u64 b53_devlink_vlan_table_get(void *priv)
{
	struct b53_device *dev = priv;
	struct b53_vlan *vl;
	unsigned int i;
	u64 count = 0;

	for (i = 0; i < dev->num_vlans; i++) {
		vl = &dev->vlans[i];
		if (vl->members)
			count++;
	}

	return count;
}

int b53_setup_devlink_resources(struct dsa_switch *ds)
{
	struct devlink_resource_size_params size_params;
	struct b53_device *dev = ds->priv;
	int err;

	devlink_resource_size_params_init(&size_params, dev->num_vlans,
					  dev->num_vlans,
					  1, DEVLINK_RESOURCE_UNIT_ENTRY);

	err = dsa_devlink_resource_register(ds, "VLAN", dev->num_vlans,
					    B53_DEVLINK_PARAM_ID_VLAN_TABLE,
					    DEVLINK_RESOURCE_ID_PARENT_TOP,
					    &size_params);
	if (err)
		goto out;

	dsa_devlink_resource_occ_get_register(ds,
					      B53_DEVLINK_PARAM_ID_VLAN_TABLE,
					      b53_devlink_vlan_table_get, dev);

	return 0;
out:
	dsa_devlink_resources_unregister(ds);
	return err;
}
EXPORT_SYMBOL(b53_setup_devlink_resources);

static int b53_setup(struct dsa_switch *ds)
{
	struct b53_device *dev = ds->priv;
	unsigned int port;
	int ret;

	/* Request bridge PVID untagged when DSA_TAG_PROTO_NONE is set
	 * which forces the CPU port to be tagged in all VLANs.
	 */
	ds->untag_bridge_pvid = dev->tag_protocol == DSA_TAG_PROTO_NONE;

	ret = b53_reset_switch(dev);
	if (ret) {
		dev_err(ds->dev, "failed to reset switch\n");
		return ret;
	}

	b53_reset_mib(dev);

	ret = b53_apply_config(dev);
	if (ret) {
		dev_err(ds->dev, "failed to apply configuration\n");
		return ret;
	}

	/* Configure IMP/CPU port, disable all other ports. Enabled
	 * ports will be configured with .port_enable
	 */
	for (port = 0; port < dev->num_ports; port++) {
		if (dsa_is_cpu_port(ds, port))
			b53_enable_cpu_port(dev, port);
		else
			b53_disable_port(ds, port);
	}

	return b53_setup_devlink_resources(ds);
}

static void b53_teardown(struct dsa_switch *ds)
{
	dsa_devlink_resources_unregister(ds);
}

static void b53_force_link(struct b53_device *dev, int port, int link)
{
	u8 reg, val, off;

	/* Override the port settings */
	if (port == dev->imp_port) {
		off = B53_PORT_OVERRIDE_CTRL;
		val = PORT_OVERRIDE_EN;
	} else {
		off = B53_GMII_PORT_OVERRIDE_CTRL(port);
		val = GMII_PO_EN;
	}

	b53_read8(dev, B53_CTRL_PAGE, off, &reg);
	reg |= val;
	if (link)
		reg |= PORT_OVERRIDE_LINK;
	else
		reg &= ~PORT_OVERRIDE_LINK;
	b53_write8(dev, B53_CTRL_PAGE, off, reg);
}

static void b53_force_port_config(struct b53_device *dev, int port,
				  int speed, int duplex,
				  bool tx_pause, bool rx_pause)
{
	u8 reg, val, off;

	/* Override the port settings */
	if (port == dev->imp_port) {
		off = B53_PORT_OVERRIDE_CTRL;
		val = PORT_OVERRIDE_EN;
	} else {
		off = B53_GMII_PORT_OVERRIDE_CTRL(port);
		val = GMII_PO_EN;
	}

	b53_read8(dev, B53_CTRL_PAGE, off, &reg);
	reg |= val;
	if (duplex == DUPLEX_FULL)
		reg |= PORT_OVERRIDE_FULL_DUPLEX;
	else
		reg &= ~PORT_OVERRIDE_FULL_DUPLEX;

	switch (speed) {
	case 2000:
		reg |= PORT_OVERRIDE_SPEED_2000M;
		fallthrough;
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
		dev_err(dev->dev, "unknown speed: %d\n", speed);
		return;
	}

	if (rx_pause)
		reg |= PORT_OVERRIDE_RX_FLOW;
	if (tx_pause)
		reg |= PORT_OVERRIDE_TX_FLOW;

	b53_write8(dev, B53_CTRL_PAGE, off, reg);
}

static void b53_adjust_link(struct dsa_switch *ds, int port,
			    struct phy_device *phydev)
{
	struct b53_device *dev = ds->priv;
	struct ethtool_eee *p = &dev->ports[port].eee;
	u8 rgmii_ctrl = 0, reg = 0, off;
	bool tx_pause = false;
	bool rx_pause = false;

	if (!phy_is_pseudo_fixed_link(phydev))
		return;

	/* Enable flow control on BCM5301x's CPU port */
	if (is5301x(dev) && dsa_is_cpu_port(ds, port))
		tx_pause = rx_pause = true;

	if (phydev->pause) {
		if (phydev->asym_pause)
			tx_pause = true;
		rx_pause = true;
	}

	b53_force_port_config(dev, port, phydev->speed, phydev->duplex,
			      tx_pause, rx_pause);
	b53_force_link(dev, port, phydev->link);

	if (is531x5(dev) && phy_interface_is_rgmii(phydev)) {
		if (port == dev->imp_port)
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
	}

	/* Re-negotiate EEE if it was enabled already */
	p->eee_enabled = b53_eee_init(ds, port, phydev);
}

void b53_port_event(struct dsa_switch *ds, int port)
{
	struct b53_device *dev = ds->priv;
	bool link;
	u16 sts;

	b53_read16(dev, B53_STAT_PAGE, B53_LINK_STAT, &sts);
	link = !!(sts & BIT(port));
	dsa_port_phylink_mac_change(ds, port, link);
}
EXPORT_SYMBOL(b53_port_event);

static void b53_phylink_get_caps(struct dsa_switch *ds, int port,
				 struct phylink_config *config)
{
	struct b53_device *dev = ds->priv;

	/* Internal ports need GMII for PHYLIB */
	__set_bit(PHY_INTERFACE_MODE_GMII, config->supported_interfaces);

	/* These switches appear to support MII and RevMII too, but beyond
	 * this, the code gives very few clues. FIXME: We probably need more
	 * interface modes here.
	 *
	 * According to b53_srab_mux_init(), ports 3..5 can support:
	 *  SGMII, MII, GMII, RGMII or INTERNAL depending on the MUX setting.
	 * However, the interface mode read from the MUX configuration is
	 * not passed back to DSA, so phylink uses NA.
	 * DT can specify RGMII for ports 0, 1.
	 * For MDIO, port 8 can be RGMII_TXID.
	 */
	__set_bit(PHY_INTERFACE_MODE_MII, config->supported_interfaces);
	__set_bit(PHY_INTERFACE_MODE_REVMII, config->supported_interfaces);

	config->mac_capabilities = MAC_ASYM_PAUSE | MAC_SYM_PAUSE |
		MAC_10 | MAC_100;

	/* 5325/5365 are not capable of gigabit speeds, everything else is.
	 * Note: the original code also exclulded Gigagbit for MII, RevMII
	 * and 802.3z modes. MII and RevMII are not able to work above 100M,
	 * so will be excluded by the generic validator implementation.
	 * However, the exclusion of Gigabit for 802.3z just seems wrong.
	 */
	if (!(is5325(dev) || is5365(dev)))
		config->mac_capabilities |= MAC_1000;

	/* Get the implementation specific capabilities */
	if (dev->ops->phylink_get_caps)
		dev->ops->phylink_get_caps(dev, port, config);

	/* This driver does not make use of the speed, duplex, pause or the
	 * advertisement in its mac_config, so it is safe to mark this driver
	 * as non-legacy.
	 */
	config->legacy_pre_march2020 = false;
}

static struct phylink_pcs *b53_phylink_mac_select_pcs(struct dsa_switch *ds,
						      int port,
						      phy_interface_t interface)
{
	struct b53_device *dev = ds->priv;

	if (!dev->ops->phylink_mac_select_pcs)
		return NULL;

	return dev->ops->phylink_mac_select_pcs(dev, port, interface);
}

void b53_phylink_mac_config(struct dsa_switch *ds, int port,
			    unsigned int mode,
			    const struct phylink_link_state *state)
{
}
EXPORT_SYMBOL(b53_phylink_mac_config);

void b53_phylink_mac_link_down(struct dsa_switch *ds, int port,
			       unsigned int mode,
			       phy_interface_t interface)
{
	struct b53_device *dev = ds->priv;

	if (mode == MLO_AN_PHY)
		return;

	if (mode == MLO_AN_FIXED) {
		b53_force_link(dev, port, false);
		return;
	}

	if (phy_interface_mode_is_8023z(interface) &&
	    dev->ops->serdes_link_set)
		dev->ops->serdes_link_set(dev, port, mode, interface, false);
}
EXPORT_SYMBOL(b53_phylink_mac_link_down);

void b53_phylink_mac_link_up(struct dsa_switch *ds, int port,
			     unsigned int mode,
			     phy_interface_t interface,
			     struct phy_device *phydev,
			     int speed, int duplex,
			     bool tx_pause, bool rx_pause)
{
	struct b53_device *dev = ds->priv;

	if (mode == MLO_AN_PHY)
		return;

	if (mode == MLO_AN_FIXED) {
		b53_force_port_config(dev, port, speed, duplex,
				      tx_pause, rx_pause);
		b53_force_link(dev, port, true);
		return;
	}

	if (phy_interface_mode_is_8023z(interface) &&
	    dev->ops->serdes_link_set)
		dev->ops->serdes_link_set(dev, port, mode, interface, true);
}
EXPORT_SYMBOL(b53_phylink_mac_link_up);

int b53_vlan_filtering(struct dsa_switch *ds, int port, bool vlan_filtering,
		       struct netlink_ext_ack *extack)
{
	struct b53_device *dev = ds->priv;

	b53_enable_vlan(dev, port, dev->vlan_enabled, vlan_filtering);

	return 0;
}
EXPORT_SYMBOL(b53_vlan_filtering);

static int b53_vlan_prepare(struct dsa_switch *ds, int port,
			    const struct switchdev_obj_port_vlan *vlan)
{
	struct b53_device *dev = ds->priv;

	if ((is5325(dev) || is5365(dev)) && vlan->vid == 0)
		return -EOPNOTSUPP;

	/* Port 7 on 7278 connects to the ASP's UniMAC which is not capable of
	 * receiving VLAN tagged frames at all, we can still allow the port to
	 * be configured for egress untagged.
	 */
	if (dev->chip_id == BCM7278_DEVICE_ID && port == 7 &&
	    !(vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED))
		return -EINVAL;

	if (vlan->vid >= dev->num_vlans)
		return -ERANGE;

	b53_enable_vlan(dev, port, true, ds->vlan_filtering);

	return 0;
}

int b53_vlan_add(struct dsa_switch *ds, int port,
		 const struct switchdev_obj_port_vlan *vlan,
		 struct netlink_ext_ack *extack)
{
	struct b53_device *dev = ds->priv;
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	bool pvid = vlan->flags & BRIDGE_VLAN_INFO_PVID;
	struct b53_vlan *vl;
	int err;

	err = b53_vlan_prepare(ds, port, vlan);
	if (err)
		return err;

	vl = &dev->vlans[vlan->vid];

	b53_get_vlan_entry(dev, vlan->vid, vl);

	if (vlan->vid == 0 && vlan->vid == b53_default_pvid(dev))
		untagged = true;

	vl->members |= BIT(port);
	if (untagged && !b53_vlan_port_needs_forced_tagged(ds, port))
		vl->untag |= BIT(port);
	else
		vl->untag &= ~BIT(port);

	b53_set_vlan_entry(dev, vlan->vid, vl);
	b53_fast_age_vlan(dev, vlan->vid);

	if (pvid && !dsa_is_cpu_port(ds, port)) {
		b53_write16(dev, B53_VLAN_PAGE, B53_VLAN_PORT_DEF_TAG(port),
			    vlan->vid);
		b53_fast_age_vlan(dev, vlan->vid);
	}

	return 0;
}
EXPORT_SYMBOL(b53_vlan_add);

int b53_vlan_del(struct dsa_switch *ds, int port,
		 const struct switchdev_obj_port_vlan *vlan)
{
	struct b53_device *dev = ds->priv;
	bool untagged = vlan->flags & BRIDGE_VLAN_INFO_UNTAGGED;
	struct b53_vlan *vl;
	u16 pvid;

	b53_read16(dev, B53_VLAN_PAGE, B53_VLAN_PORT_DEF_TAG(port), &pvid);

	vl = &dev->vlans[vlan->vid];

	b53_get_vlan_entry(dev, vlan->vid, vl);

	vl->members &= ~BIT(port);

	if (pvid == vlan->vid)
		pvid = b53_default_pvid(dev);

	if (untagged && !b53_vlan_port_needs_forced_tagged(ds, port))
		vl->untag &= ~(BIT(port));

	b53_set_vlan_entry(dev, vlan->vid, vl);
	b53_fast_age_vlan(dev, vlan->vid);

	b53_write16(dev, B53_VLAN_PAGE, B53_VLAN_PORT_DEF_TAG(port), pvid);
	b53_fast_age_vlan(dev, pvid);

	return 0;
}
EXPORT_SYMBOL(b53_vlan_del);

/* Address Resolution Logic routines. Caller must hold &dev->arl_mutex. */
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
	if (dev->vlan_enabled)
		reg &= ~ARLTBL_IVL_SVL_SELECT;
	else
		reg |= ARLTBL_IVL_SVL_SELECT;
	b53_write8(dev, B53_ARLIO_PAGE, B53_ARLTBL_RW_CTRL, reg);

	return b53_arl_op_wait(dev);
}

static int b53_arl_read(struct b53_device *dev, u64 mac,
			u16 vid, struct b53_arl_entry *ent, u8 *idx)
{
	DECLARE_BITMAP(free_bins, B53_ARLTBL_MAX_BIN_ENTRIES);
	unsigned int i;
	int ret;

	ret = b53_arl_op_wait(dev);
	if (ret)
		return ret;

	bitmap_zero(free_bins, dev->num_arl_bins);

	/* Read the bins */
	for (i = 0; i < dev->num_arl_bins; i++) {
		u64 mac_vid;
		u32 fwd_entry;

		b53_read64(dev, B53_ARLIO_PAGE,
			   B53_ARLTBL_MAC_VID_ENTRY(i), &mac_vid);
		b53_read32(dev, B53_ARLIO_PAGE,
			   B53_ARLTBL_DATA_ENTRY(i), &fwd_entry);
		b53_arl_to_entry(ent, mac_vid, fwd_entry);

		if (!(fwd_entry & ARLTBL_VALID)) {
			set_bit(i, free_bins);
			continue;
		}
		if ((mac_vid & ARLTBL_MAC_MASK) != mac)
			continue;
		if (dev->vlan_enabled &&
		    ((mac_vid >> ARLTBL_VID_S) & ARLTBL_VID_MASK) != vid)
			continue;
		*idx = i;
		return 0;
	}

	if (bitmap_weight(free_bins, dev->num_arl_bins) == 0)
		return -ENOSPC;

	*idx = find_first_bit(free_bins, dev->num_arl_bins);

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

	ret = b53_arl_read(dev, mac, vid, &ent, &idx);

	/* If this is a read, just finish now */
	if (op)
		return ret;

	switch (ret) {
	case -ETIMEDOUT:
		return ret;
	case -ENOSPC:
		dev_dbg(dev->dev, "{%pM,%.4d} no space left in ARL\n",
			addr, vid);
		return is_valid ? ret : 0;
	case -ENOENT:
		/* We could not find a matching MAC, so reset to a new entry */
		dev_dbg(dev->dev, "{%pM,%.4d} not found, using idx: %d\n",
			addr, vid, idx);
		fwd_entry = 0;
		break;
	default:
		dev_dbg(dev->dev, "{%pM,%.4d} found, using idx: %d\n",
			addr, vid, idx);
		break;
	}

	/* For multicast address, the port is a bitmask and the validity
	 * is determined by having at least one port being still active
	 */
	if (!is_multicast_ether_addr(addr)) {
		ent.port = port;
		ent.is_valid = is_valid;
	} else {
		if (is_valid)
			ent.port |= BIT(port);
		else
			ent.port &= ~BIT(port);

		ent.is_valid = !!(ent.port);
	}

	ent.vid = vid;
	ent.is_static = true;
	ent.is_age = false;
	memcpy(ent.mac, addr, ETH_ALEN);
	b53_arl_from_entry(&mac_vid, &fwd_entry, &ent);

	b53_write64(dev, B53_ARLIO_PAGE,
		    B53_ARLTBL_MAC_VID_ENTRY(idx), mac_vid);
	b53_write32(dev, B53_ARLIO_PAGE,
		    B53_ARLTBL_DATA_ENTRY(idx), fwd_entry);

	return b53_arl_rw_op(dev, 0);
}

int b53_fdb_add(struct dsa_switch *ds, int port,
		const unsigned char *addr, u16 vid,
		struct dsa_db db)
{
	struct b53_device *priv = ds->priv;
	int ret;

	/* 5325 and 5365 require some more massaging, but could
	 * be supported eventually
	 */
	if (is5325(priv) || is5365(priv))
		return -EOPNOTSUPP;

	mutex_lock(&priv->arl_mutex);
	ret = b53_arl_op(priv, 0, port, addr, vid, true);
	mutex_unlock(&priv->arl_mutex);

	return ret;
}
EXPORT_SYMBOL(b53_fdb_add);

int b53_fdb_del(struct dsa_switch *ds, int port,
		const unsigned char *addr, u16 vid,
		struct dsa_db db)
{
	struct b53_device *priv = ds->priv;
	int ret;

	mutex_lock(&priv->arl_mutex);
	ret = b53_arl_op(priv, 0, port, addr, vid, false);
	mutex_unlock(&priv->arl_mutex);

	return ret;
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

static int b53_fdb_copy(int port, const struct b53_arl_entry *ent,
			dsa_fdb_dump_cb_t *cb, void *data)
{
	if (!ent->is_valid)
		return 0;

	if (port != ent->port)
		return 0;

	return cb(ent->mac, ent->vid, ent->is_static, data);
}

int b53_fdb_dump(struct dsa_switch *ds, int port,
		 dsa_fdb_dump_cb_t *cb, void *data)
{
	struct b53_device *priv = ds->priv;
	struct b53_arl_entry results[2];
	unsigned int count = 0;
	int ret;
	u8 reg;

	mutex_lock(&priv->arl_mutex);

	/* Start search operation */
	reg = ARL_SRCH_STDN;
	b53_write8(priv, B53_ARLIO_PAGE, B53_ARL_SRCH_CTL, reg);

	do {
		ret = b53_arl_search_wait(priv);
		if (ret)
			break;

		b53_arl_search_rd(priv, 0, &results[0]);
		ret = b53_fdb_copy(port, &results[0], cb, data);
		if (ret)
			break;

		if (priv->num_arl_bins > 2) {
			b53_arl_search_rd(priv, 1, &results[1]);
			ret = b53_fdb_copy(port, &results[1], cb, data);
			if (ret)
				break;

			if (!results[0].is_valid && !results[1].is_valid)
				break;
		}

	} while (count++ < b53_max_arl_entries(priv) / 2);

	mutex_unlock(&priv->arl_mutex);

	return 0;
}
EXPORT_SYMBOL(b53_fdb_dump);

int b53_mdb_add(struct dsa_switch *ds, int port,
		const struct switchdev_obj_port_mdb *mdb,
		struct dsa_db db)
{
	struct b53_device *priv = ds->priv;
	int ret;

	/* 5325 and 5365 require some more massaging, but could
	 * be supported eventually
	 */
	if (is5325(priv) || is5365(priv))
		return -EOPNOTSUPP;

	mutex_lock(&priv->arl_mutex);
	ret = b53_arl_op(priv, 0, port, mdb->addr, mdb->vid, true);
	mutex_unlock(&priv->arl_mutex);

	return ret;
}
EXPORT_SYMBOL(b53_mdb_add);

int b53_mdb_del(struct dsa_switch *ds, int port,
		const struct switchdev_obj_port_mdb *mdb,
		struct dsa_db db)
{
	struct b53_device *priv = ds->priv;
	int ret;

	mutex_lock(&priv->arl_mutex);
	ret = b53_arl_op(priv, 0, port, mdb->addr, mdb->vid, false);
	mutex_unlock(&priv->arl_mutex);
	if (ret)
		dev_err(ds->dev, "failed to delete MDB entry\n");

	return ret;
}
EXPORT_SYMBOL(b53_mdb_del);

int b53_br_join(struct dsa_switch *ds, int port, struct dsa_bridge bridge,
		bool *tx_fwd_offload, struct netlink_ext_ack *extack)
{
	struct b53_device *dev = ds->priv;
	s8 cpu_port = dsa_to_port(ds, port)->cpu_dp->index;
	u16 pvlan, reg;
	unsigned int i;

	/* On 7278, port 7 which connects to the ASP should only receive
	 * traffic from matching CFP rules.
	 */
	if (dev->chip_id == BCM7278_DEVICE_ID && port == 7)
		return -EINVAL;

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
		if (!dsa_port_offloads_bridge(dsa_to_port(ds, i), &bridge))
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

void b53_br_leave(struct dsa_switch *ds, int port, struct dsa_bridge bridge)
{
	struct b53_device *dev = ds->priv;
	struct b53_vlan *vl = &dev->vlans[0];
	s8 cpu_port = dsa_to_port(ds, port)->cpu_dp->index;
	unsigned int i;
	u16 pvlan, reg, pvid;

	b53_read16(dev, B53_PVLAN_PAGE, B53_PVLAN_PORT_MASK(port), &pvlan);

	b53_for_each_port(dev, i) {
		/* Don't touch the remaining ports */
		if (!dsa_port_offloads_bridge(dsa_to_port(ds, i), &bridge))
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

	pvid = b53_default_pvid(dev);

	/* Make this port join all VLANs without VLAN entries */
	if (is58xx(dev)) {
		b53_read16(dev, B53_VLAN_PAGE, B53_JOIN_ALL_VLAN_EN, &reg);
		reg |= BIT(port);
		if (!(reg & BIT(cpu_port)))
			reg |= BIT(cpu_port);
		b53_write16(dev, B53_VLAN_PAGE, B53_JOIN_ALL_VLAN_EN, reg);
	} else {
		b53_get_vlan_entry(dev, pvid, vl);
		vl->members |= BIT(port) | BIT(cpu_port);
		vl->untag |= BIT(port) | BIT(cpu_port);
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

int b53_br_flags_pre(struct dsa_switch *ds, int port,
		     struct switchdev_brport_flags flags,
		     struct netlink_ext_ack *extack)
{
	if (flags.mask & ~(BR_FLOOD | BR_MCAST_FLOOD | BR_LEARNING))
		return -EINVAL;

	return 0;
}
EXPORT_SYMBOL(b53_br_flags_pre);

int b53_br_flags(struct dsa_switch *ds, int port,
		 struct switchdev_brport_flags flags,
		 struct netlink_ext_ack *extack)
{
	if (flags.mask & BR_FLOOD)
		b53_port_set_ucast_flood(ds->priv, port,
					 !!(flags.val & BR_FLOOD));
	if (flags.mask & BR_MCAST_FLOOD)
		b53_port_set_mcast_flood(ds->priv, port,
					 !!(flags.val & BR_MCAST_FLOOD));
	if (flags.mask & BR_LEARNING)
		b53_port_set_learning(ds->priv, port,
				      !!(flags.val & BR_LEARNING));

	return 0;
}
EXPORT_SYMBOL(b53_br_flags);

static bool b53_possible_cpu_port(struct dsa_switch *ds, int port)
{
	/* Broadcom switches will accept enabling Broadcom tags on the
	 * following ports: 5, 7 and 8, any other port is not supported
	 */
	switch (port) {
	case B53_CPU_PORT_25:
	case 7:
	case B53_CPU_PORT:
		return true;
	}

	return false;
}

static bool b53_can_enable_brcm_tags(struct dsa_switch *ds, int port,
				     enum dsa_tag_protocol tag_protocol)
{
	bool ret = b53_possible_cpu_port(ds, port);

	if (!ret) {
		dev_warn(ds->dev, "Port %d is not Broadcom tag capable\n",
			 port);
		return ret;
	}

	switch (tag_protocol) {
	case DSA_TAG_PROTO_BRCM:
	case DSA_TAG_PROTO_BRCM_PREPEND:
		dev_warn(ds->dev,
			 "Port %d is stacked to Broadcom tag switch\n", port);
		ret = false;
		break;
	default:
		ret = true;
		break;
	}

	return ret;
}

enum dsa_tag_protocol b53_get_tag_protocol(struct dsa_switch *ds, int port,
					   enum dsa_tag_protocol mprot)
{
	struct b53_device *dev = ds->priv;

	if (!b53_can_enable_brcm_tags(ds, port, mprot)) {
		dev->tag_protocol = DSA_TAG_PROTO_NONE;
		goto out;
	}

	/* Older models require a different 6 byte tag */
	if (is5325(dev) || is5365(dev) || is63xx(dev)) {
		dev->tag_protocol = DSA_TAG_PROTO_BRCM_LEGACY;
		goto out;
	}

	/* Broadcom BCM58xx chips have a flow accelerator on Port 8
	 * which requires us to use the prepended Broadcom tag type
	 */
	if (dev->chip_id == BCM58XX_DEVICE_ID && port == B53_CPU_PORT) {
		dev->tag_protocol = DSA_TAG_PROTO_BRCM_PREPEND;
		goto out;
	}

	dev->tag_protocol = DSA_TAG_PROTO_BRCM;
out:
	return dev->tag_protocol;
}
EXPORT_SYMBOL(b53_get_tag_protocol);

int b53_mirror_add(struct dsa_switch *ds, int port,
		   struct dsa_mall_mirror_tc_entry *mirror, bool ingress,
		   struct netlink_ext_ack *extack)
{
	struct b53_device *dev = ds->priv;
	u16 reg, loc;

	if (ingress)
		loc = B53_IG_MIR_CTL;
	else
		loc = B53_EG_MIR_CTL;

	b53_read16(dev, B53_MGMT_PAGE, loc, &reg);
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

void b53_eee_enable_set(struct dsa_switch *ds, int port, bool enable)
{
	struct b53_device *dev = ds->priv;
	u16 reg;

	b53_read16(dev, B53_EEE_PAGE, B53_EEE_EN_CTRL, &reg);
	if (enable)
		reg |= BIT(port);
	else
		reg &= ~BIT(port);
	b53_write16(dev, B53_EEE_PAGE, B53_EEE_EN_CTRL, reg);
}
EXPORT_SYMBOL(b53_eee_enable_set);


/* Returns 0 if EEE was not enabled, or 1 otherwise
 */
int b53_eee_init(struct dsa_switch *ds, int port, struct phy_device *phy)
{
	int ret;

	ret = phy_init_eee(phy, false);
	if (ret)
		return 0;

	b53_eee_enable_set(ds, port, true);

	return 1;
}
EXPORT_SYMBOL(b53_eee_init);

int b53_get_mac_eee(struct dsa_switch *ds, int port, struct ethtool_eee *e)
{
	struct b53_device *dev = ds->priv;
	struct ethtool_eee *p = &dev->ports[port].eee;
	u16 reg;

	if (is5325(dev) || is5365(dev))
		return -EOPNOTSUPP;

	b53_read16(dev, B53_EEE_PAGE, B53_EEE_LPI_INDICATE, &reg);
	e->eee_enabled = p->eee_enabled;
	e->eee_active = !!(reg & BIT(port));

	return 0;
}
EXPORT_SYMBOL(b53_get_mac_eee);

int b53_set_mac_eee(struct dsa_switch *ds, int port, struct ethtool_eee *e)
{
	struct b53_device *dev = ds->priv;
	struct ethtool_eee *p = &dev->ports[port].eee;

	if (is5325(dev) || is5365(dev))
		return -EOPNOTSUPP;

	p->eee_enabled = e->eee_enabled;
	b53_eee_enable_set(ds, port, e->eee_enabled);

	return 0;
}
EXPORT_SYMBOL(b53_set_mac_eee);

static int b53_change_mtu(struct dsa_switch *ds, int port, int mtu)
{
	struct b53_device *dev = ds->priv;
	bool enable_jumbo;
	bool allow_10_100;

	if (is5325(dev) || is5365(dev))
		return -EOPNOTSUPP;

	enable_jumbo = (mtu >= JMS_MIN_SIZE);
	allow_10_100 = (dev->chip_id == BCM583XX_DEVICE_ID);

	return b53_set_jumbo(dev, enable_jumbo, allow_10_100);
}

static int b53_get_max_mtu(struct dsa_switch *ds, int port)
{
	return JMS_MAX_SIZE;
}

static const struct dsa_switch_ops b53_switch_ops = {
	.get_tag_protocol	= b53_get_tag_protocol,
	.setup			= b53_setup,
	.teardown		= b53_teardown,
	.get_strings		= b53_get_strings,
	.get_ethtool_stats	= b53_get_ethtool_stats,
	.get_sset_count		= b53_get_sset_count,
	.get_ethtool_phy_stats	= b53_get_ethtool_phy_stats,
	.phy_read		= b53_phy_read16,
	.phy_write		= b53_phy_write16,
	.adjust_link		= b53_adjust_link,
	.phylink_get_caps	= b53_phylink_get_caps,
	.phylink_mac_select_pcs	= b53_phylink_mac_select_pcs,
	.phylink_mac_config	= b53_phylink_mac_config,
	.phylink_mac_link_down	= b53_phylink_mac_link_down,
	.phylink_mac_link_up	= b53_phylink_mac_link_up,
	.port_enable		= b53_enable_port,
	.port_disable		= b53_disable_port,
	.get_mac_eee		= b53_get_mac_eee,
	.set_mac_eee		= b53_set_mac_eee,
	.port_bridge_join	= b53_br_join,
	.port_bridge_leave	= b53_br_leave,
	.port_pre_bridge_flags	= b53_br_flags_pre,
	.port_bridge_flags	= b53_br_flags,
	.port_stp_state_set	= b53_br_set_stp_state,
	.port_fast_age		= b53_br_fast_age,
	.port_vlan_filtering	= b53_vlan_filtering,
	.port_vlan_add		= b53_vlan_add,
	.port_vlan_del		= b53_vlan_del,
	.port_fdb_dump		= b53_fdb_dump,
	.port_fdb_add		= b53_fdb_add,
	.port_fdb_del		= b53_fdb_del,
	.port_mirror_add	= b53_mirror_add,
	.port_mirror_del	= b53_mirror_del,
	.port_mdb_add		= b53_mdb_add,
	.port_mdb_del		= b53_mdb_del,
	.port_max_mtu		= b53_get_max_mtu,
	.port_change_mtu	= b53_change_mtu,
};

struct b53_chip_data {
	u32 chip_id;
	const char *dev_name;
	u16 vlans;
	u16 enabled_ports;
	u8 imp_port;
	u8 cpu_port;
	u8 vta_regs[3];
	u8 arl_bins;
	u16 arl_buckets;
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
		.enabled_ports = 0x3f,
		.arl_bins = 2,
		.arl_buckets = 1024,
		.imp_port = 5,
		.duplex_reg = B53_DUPLEX_STAT_FE,
	},
	{
		.chip_id = BCM5365_DEVICE_ID,
		.dev_name = "BCM5365",
		.vlans = 256,
		.enabled_ports = 0x3f,
		.arl_bins = 2,
		.arl_buckets = 1024,
		.imp_port = 5,
		.duplex_reg = B53_DUPLEX_STAT_FE,
	},
	{
		.chip_id = BCM5389_DEVICE_ID,
		.dev_name = "BCM5389",
		.vlans = 4096,
		.enabled_ports = 0x11f,
		.arl_bins = 4,
		.arl_buckets = 1024,
		.imp_port = 8,
		.vta_regs = B53_VTA_REGS,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
	{
		.chip_id = BCM5395_DEVICE_ID,
		.dev_name = "BCM5395",
		.vlans = 4096,
		.enabled_ports = 0x11f,
		.arl_bins = 4,
		.arl_buckets = 1024,
		.imp_port = 8,
		.vta_regs = B53_VTA_REGS,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
	{
		.chip_id = BCM5397_DEVICE_ID,
		.dev_name = "BCM5397",
		.vlans = 4096,
		.enabled_ports = 0x11f,
		.arl_bins = 4,
		.arl_buckets = 1024,
		.imp_port = 8,
		.vta_regs = B53_VTA_REGS_9798,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
	{
		.chip_id = BCM5398_DEVICE_ID,
		.dev_name = "BCM5398",
		.vlans = 4096,
		.enabled_ports = 0x17f,
		.arl_bins = 4,
		.arl_buckets = 1024,
		.imp_port = 8,
		.vta_regs = B53_VTA_REGS_9798,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
	{
		.chip_id = BCM53115_DEVICE_ID,
		.dev_name = "BCM53115",
		.vlans = 4096,
		.enabled_ports = 0x11f,
		.arl_bins = 4,
		.arl_buckets = 1024,
		.vta_regs = B53_VTA_REGS,
		.imp_port = 8,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
	{
		.chip_id = BCM53125_DEVICE_ID,
		.dev_name = "BCM53125",
		.vlans = 4096,
		.enabled_ports = 0x1ff,
		.arl_bins = 4,
		.arl_buckets = 1024,
		.imp_port = 8,
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
		.arl_bins = 4,
		.arl_buckets = 1024,
		.imp_port = 8,
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
		.arl_bins = 4,
		.arl_buckets = 1024,
		.imp_port = 8,
		.vta_regs = B53_VTA_REGS_63XX,
		.duplex_reg = B53_DUPLEX_STAT_63XX,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK_63XX,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE_63XX,
	},
	{
		.chip_id = BCM53010_DEVICE_ID,
		.dev_name = "BCM53010",
		.vlans = 4096,
		.enabled_ports = 0x1bf,
		.arl_bins = 4,
		.arl_buckets = 1024,
		.imp_port = 8,
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
		.arl_bins = 4,
		.arl_buckets = 1024,
		.imp_port = 8,
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
		.arl_bins = 4,
		.arl_buckets = 1024,
		.imp_port = 8,
		.vta_regs = B53_VTA_REGS,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
	{
		.chip_id = BCM53018_DEVICE_ID,
		.dev_name = "BCM53018",
		.vlans = 4096,
		.enabled_ports = 0x1bf,
		.arl_bins = 4,
		.arl_buckets = 1024,
		.imp_port = 8,
		.vta_regs = B53_VTA_REGS,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
	{
		.chip_id = BCM53019_DEVICE_ID,
		.dev_name = "BCM53019",
		.vlans = 4096,
		.enabled_ports = 0x1bf,
		.arl_bins = 4,
		.arl_buckets = 1024,
		.imp_port = 8,
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
		.arl_bins = 4,
		.arl_buckets = 1024,
		.imp_port = 8,
		.vta_regs = B53_VTA_REGS,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
	{
		.chip_id = BCM583XX_DEVICE_ID,
		.dev_name = "BCM583xx/11360",
		.vlans = 4096,
		.enabled_ports = 0x103,
		.arl_bins = 4,
		.arl_buckets = 1024,
		.imp_port = 8,
		.vta_regs = B53_VTA_REGS,
		.duplex_reg = B53_DUPLEX_STAT_GE,
		.jumbo_pm_reg = B53_JUMBO_PORT_MASK,
		.jumbo_size_reg = B53_JUMBO_MAX_SIZE,
	},
	/* Starfighter 2 */
	{
		.chip_id = BCM4908_DEVICE_ID,
		.dev_name = "BCM4908",
		.vlans = 4096,
		.enabled_ports = 0x1bf,
		.arl_bins = 4,
		.arl_buckets = 256,
		.imp_port = 8,
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
		.arl_bins = 4,
		.arl_buckets = 1024,
		.imp_port = 8,
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
		.arl_bins = 4,
		.arl_buckets = 256,
		.imp_port = 8,
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
			dev->imp_port = chip->imp_port;
			dev->num_vlans = chip->vlans;
			dev->num_arl_bins = chip->arl_bins;
			dev->num_arl_buckets = chip->arl_buckets;
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
	}

	dev->num_ports = fls(dev->enabled_ports);

	dev->ds->num_ports = min_t(unsigned int, dev->num_ports, DSA_MAX_PORTS);

	/* Include non standard CPU port built-in PHYs to be probed */
	if (is539x(dev) || is531x5(dev)) {
		for (i = 0; i < dev->num_ports; i++) {
			if (!(dev->ds->phys_mii_mask & BIT(i)) &&
			    !b53_possible_cpu_port(dev->ds, i))
				dev->ds->phys_mii_mask |= BIT(i);
		}
	}

	dev->ports = devm_kcalloc(dev->dev,
				  dev->num_ports, sizeof(struct b53_port),
				  GFP_KERNEL);
	if (!dev->ports)
		return -ENOMEM;

	dev->vlans = devm_kcalloc(dev->dev,
				  dev->num_vlans, sizeof(struct b53_vlan),
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

	ds = devm_kzalloc(base, sizeof(*ds), GFP_KERNEL);
	if (!ds)
		return NULL;

	ds->dev = base;

	dev = devm_kzalloc(base, sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return NULL;

	ds->priv = dev;
	dev->dev = base;

	dev->ds = ds;
	dev->priv = priv;
	dev->ops = ops;
	ds->ops = &b53_switch_ops;
	dev->vlan_enabled = true;
	/* Let DSA handle the case were multiple bridges span the same switch
	 * device and different VLAN awareness settings are requested, which
	 * would be breaking filtering semantics for any of the other bridge
	 * devices. (not hardware supported)
	 */
	ds->vlan_filtering_is_global = true;

	mutex_init(&dev->reg_mutex);
	mutex_init(&dev->stats_mutex);
	mutex_init(&dev->arl_mutex);

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
	case BCM5389_DEVICE_ID:
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
			dev_err(dev->dev,
				"unsupported switch detected (BCM53%02x/BCM%x)\n",
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

	dev_info(dev->dev, "found switch: %s, rev %i\n",
		 dev->name, dev->core_rev);

	return dsa_register_switch(dev->ds);
}
EXPORT_SYMBOL(b53_switch_register);

MODULE_AUTHOR("Jonas Gorski <jogo@openwrt.org>");
MODULE_DESCRIPTION("B53 switch library");
MODULE_LICENSE("Dual BSD/GPL");
