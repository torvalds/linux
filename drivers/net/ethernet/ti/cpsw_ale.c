/*
 * Texas Instruments 3-Port Ethernet Switch Address Lookup Engine
 *
 * Copyright (C) 2012 Texas Instruments
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/stat.h>
#include <linux/sysfs.h>
#include <linux/etherdevice.h>

#include "cpsw_ale.h"

#define BITMASK(bits)		(BIT(bits) - 1)
#define ALE_ENTRY_BITS		68
#define ALE_ENTRY_WORDS	DIV_ROUND_UP(ALE_ENTRY_BITS, 32)

#define ALE_VERSION_MAJOR(rev)	((rev >> 8) & 0xff)
#define ALE_VERSION_MINOR(rev)	(rev & 0xff)

/* ALE Registers */
#define ALE_IDVER		0x00
#define ALE_CONTROL		0x08
#define ALE_PRESCALE		0x10
#define ALE_UNKNOWNVLAN		0x18
#define ALE_TABLE_CONTROL	0x20
#define ALE_TABLE		0x34
#define ALE_PORTCTL		0x40

#define ALE_TABLE_WRITE		BIT(31)

#define ALE_TYPE_FREE			0
#define ALE_TYPE_ADDR			1
#define ALE_TYPE_VLAN			2
#define ALE_TYPE_VLAN_ADDR		3

#define ALE_UCAST_PERSISTANT		0
#define ALE_UCAST_UNTOUCHED		1
#define ALE_UCAST_OUI			2
#define ALE_UCAST_TOUCHED		3

static inline int cpsw_ale_get_field(u32 *ale_entry, u32 start, u32 bits)
{
	int idx;

	idx    = start / 32;
	start -= idx * 32;
	idx    = 2 - idx; /* flip */
	return (ale_entry[idx] >> start) & BITMASK(bits);
}

static inline void cpsw_ale_set_field(u32 *ale_entry, u32 start, u32 bits,
				      u32 value)
{
	int idx;

	value &= BITMASK(bits);
	idx    = start / 32;
	start -= idx * 32;
	idx    = 2 - idx; /* flip */
	ale_entry[idx] &= ~(BITMASK(bits) << start);
	ale_entry[idx] |=  (value << start);
}

#define DEFINE_ALE_FIELD(name, start, bits)				\
static inline int cpsw_ale_get_##name(u32 *ale_entry)			\
{									\
	return cpsw_ale_get_field(ale_entry, start, bits);		\
}									\
static inline void cpsw_ale_set_##name(u32 *ale_entry, u32 value)	\
{									\
	cpsw_ale_set_field(ale_entry, start, bits, value);		\
}

DEFINE_ALE_FIELD(entry_type,		60,	2)
DEFINE_ALE_FIELD(vlan_id,		48,	12)
DEFINE_ALE_FIELD(mcast_state,		62,	2)
DEFINE_ALE_FIELD(port_mask,		66,     3)
DEFINE_ALE_FIELD(super,			65,	1)
DEFINE_ALE_FIELD(ucast_type,		62,     2)
DEFINE_ALE_FIELD(port_num,		66,     2)
DEFINE_ALE_FIELD(blocked,		65,     1)
DEFINE_ALE_FIELD(secure,		64,     1)
DEFINE_ALE_FIELD(vlan_untag_force,	24,	3)
DEFINE_ALE_FIELD(vlan_reg_mcast,	16,	3)
DEFINE_ALE_FIELD(vlan_unreg_mcast,	8,	3)
DEFINE_ALE_FIELD(vlan_member_list,	0,	3)
DEFINE_ALE_FIELD(mcast,			40,	1)

/* The MAC address field in the ALE entry cannot be macroized as above */
static inline void cpsw_ale_get_addr(u32 *ale_entry, u8 *addr)
{
	int i;

	for (i = 0; i < 6; i++)
		addr[i] = cpsw_ale_get_field(ale_entry, 40 - 8*i, 8);
}

static inline void cpsw_ale_set_addr(u32 *ale_entry, u8 *addr)
{
	int i;

	for (i = 0; i < 6; i++)
		cpsw_ale_set_field(ale_entry, 40 - 8*i, 8, addr[i]);
}

static int cpsw_ale_read(struct cpsw_ale *ale, int idx, u32 *ale_entry)
{
	int i;

	WARN_ON(idx > ale->params.ale_entries);

	__raw_writel(idx, ale->params.ale_regs + ALE_TABLE_CONTROL);

	for (i = 0; i < ALE_ENTRY_WORDS; i++)
		ale_entry[i] = __raw_readl(ale->params.ale_regs +
					   ALE_TABLE + 4 * i);

	return idx;
}

static int cpsw_ale_write(struct cpsw_ale *ale, int idx, u32 *ale_entry)
{
	int i;

	WARN_ON(idx > ale->params.ale_entries);

	for (i = 0; i < ALE_ENTRY_WORDS; i++)
		__raw_writel(ale_entry[i], ale->params.ale_regs +
			     ALE_TABLE + 4 * i);

	__raw_writel(idx | ALE_TABLE_WRITE, ale->params.ale_regs +
		     ALE_TABLE_CONTROL);

	return idx;
}

int cpsw_ale_match_addr(struct cpsw_ale *ale, u8 *addr, u16 vid)
{
	u32 ale_entry[ALE_ENTRY_WORDS];
	int type, idx;

	for (idx = 0; idx < ale->params.ale_entries; idx++) {
		u8 entry_addr[6];

		cpsw_ale_read(ale, idx, ale_entry);
		type = cpsw_ale_get_entry_type(ale_entry);
		if (type != ALE_TYPE_ADDR && type != ALE_TYPE_VLAN_ADDR)
			continue;
		if (cpsw_ale_get_vlan_id(ale_entry) != vid)
			continue;
		cpsw_ale_get_addr(ale_entry, entry_addr);
		if (ether_addr_equal(entry_addr, addr))
			return idx;
	}
	return -ENOENT;
}

int cpsw_ale_match_vlan(struct cpsw_ale *ale, u16 vid)
{
	u32 ale_entry[ALE_ENTRY_WORDS];
	int type, idx;

	for (idx = 0; idx < ale->params.ale_entries; idx++) {
		cpsw_ale_read(ale, idx, ale_entry);
		type = cpsw_ale_get_entry_type(ale_entry);
		if (type != ALE_TYPE_VLAN)
			continue;
		if (cpsw_ale_get_vlan_id(ale_entry) == vid)
			return idx;
	}
	return -ENOENT;
}

static int cpsw_ale_match_free(struct cpsw_ale *ale)
{
	u32 ale_entry[ALE_ENTRY_WORDS];
	int type, idx;

	for (idx = 0; idx < ale->params.ale_entries; idx++) {
		cpsw_ale_read(ale, idx, ale_entry);
		type = cpsw_ale_get_entry_type(ale_entry);
		if (type == ALE_TYPE_FREE)
			return idx;
	}
	return -ENOENT;
}

static int cpsw_ale_find_ageable(struct cpsw_ale *ale)
{
	u32 ale_entry[ALE_ENTRY_WORDS];
	int type, idx;

	for (idx = 0; idx < ale->params.ale_entries; idx++) {
		cpsw_ale_read(ale, idx, ale_entry);
		type = cpsw_ale_get_entry_type(ale_entry);
		if (type != ALE_TYPE_ADDR && type != ALE_TYPE_VLAN_ADDR)
			continue;
		if (cpsw_ale_get_mcast(ale_entry))
			continue;
		type = cpsw_ale_get_ucast_type(ale_entry);
		if (type != ALE_UCAST_PERSISTANT &&
		    type != ALE_UCAST_OUI)
			return idx;
	}
	return -ENOENT;
}

static void cpsw_ale_flush_mcast(struct cpsw_ale *ale, u32 *ale_entry,
				 int port_mask)
{
	int mask;

	mask = cpsw_ale_get_port_mask(ale_entry);
	if ((mask & port_mask) == 0)
		return; /* ports dont intersect, not interested */
	mask &= ~port_mask;

	/* free if only remaining port is host port */
	if (mask)
		cpsw_ale_set_port_mask(ale_entry, mask);
	else
		cpsw_ale_set_entry_type(ale_entry, ALE_TYPE_FREE);
}

int cpsw_ale_flush_multicast(struct cpsw_ale *ale, int port_mask)
{
	u32 ale_entry[ALE_ENTRY_WORDS];
	int ret, idx;

	for (idx = 0; idx < ale->params.ale_entries; idx++) {
		cpsw_ale_read(ale, idx, ale_entry);
		ret = cpsw_ale_get_entry_type(ale_entry);
		if (ret != ALE_TYPE_ADDR && ret != ALE_TYPE_VLAN_ADDR)
			continue;

		if (cpsw_ale_get_mcast(ale_entry)) {
			u8 addr[6];

			cpsw_ale_get_addr(ale_entry, addr);
			if (!is_broadcast_ether_addr(addr))
				cpsw_ale_flush_mcast(ale, ale_entry, port_mask);
		}

		cpsw_ale_write(ale, idx, ale_entry);
	}
	return 0;
}

static void cpsw_ale_flush_ucast(struct cpsw_ale *ale, u32 *ale_entry,
				 int port_mask)
{
	int port;

	port = cpsw_ale_get_port_num(ale_entry);
	if ((BIT(port) & port_mask) == 0)
		return; /* ports dont intersect, not interested */
	cpsw_ale_set_entry_type(ale_entry, ALE_TYPE_FREE);
}

int cpsw_ale_flush(struct cpsw_ale *ale, int port_mask)
{
	u32 ale_entry[ALE_ENTRY_WORDS];
	int ret, idx;

	for (idx = 0; idx < ale->params.ale_entries; idx++) {
		cpsw_ale_read(ale, idx, ale_entry);
		ret = cpsw_ale_get_entry_type(ale_entry);
		if (ret != ALE_TYPE_ADDR && ret != ALE_TYPE_VLAN_ADDR)
			continue;

		if (cpsw_ale_get_mcast(ale_entry))
			cpsw_ale_flush_mcast(ale, ale_entry, port_mask);
		else
			cpsw_ale_flush_ucast(ale, ale_entry, port_mask);

		cpsw_ale_write(ale, idx, ale_entry);
	}
	return 0;
}

static inline void cpsw_ale_set_vlan_entry_type(u32 *ale_entry,
						int flags, u16 vid)
{
	if (flags & ALE_VLAN) {
		cpsw_ale_set_entry_type(ale_entry, ALE_TYPE_VLAN_ADDR);
		cpsw_ale_set_vlan_id(ale_entry, vid);
	} else {
		cpsw_ale_set_entry_type(ale_entry, ALE_TYPE_ADDR);
	}
}

int cpsw_ale_add_ucast(struct cpsw_ale *ale, u8 *addr, int port,
		       int flags, u16 vid)
{
	u32 ale_entry[ALE_ENTRY_WORDS] = {0, 0, 0};
	int idx;

	cpsw_ale_set_vlan_entry_type(ale_entry, flags, vid);

	cpsw_ale_set_addr(ale_entry, addr);
	cpsw_ale_set_ucast_type(ale_entry, ALE_UCAST_PERSISTANT);
	cpsw_ale_set_secure(ale_entry, (flags & ALE_SECURE) ? 1 : 0);
	cpsw_ale_set_blocked(ale_entry, (flags & ALE_BLOCKED) ? 1 : 0);
	cpsw_ale_set_port_num(ale_entry, port);

	idx = cpsw_ale_match_addr(ale, addr, (flags & ALE_VLAN) ? vid : 0);
	if (idx < 0)
		idx = cpsw_ale_match_free(ale);
	if (idx < 0)
		idx = cpsw_ale_find_ageable(ale);
	if (idx < 0)
		return -ENOMEM;

	cpsw_ale_write(ale, idx, ale_entry);
	return 0;
}

int cpsw_ale_del_ucast(struct cpsw_ale *ale, u8 *addr, int port,
		       int flags, u16 vid)
{
	u32 ale_entry[ALE_ENTRY_WORDS] = {0, 0, 0};
	int idx;

	idx = cpsw_ale_match_addr(ale, addr, (flags & ALE_VLAN) ? vid : 0);
	if (idx < 0)
		return -ENOENT;

	cpsw_ale_set_entry_type(ale_entry, ALE_TYPE_FREE);
	cpsw_ale_write(ale, idx, ale_entry);
	return 0;
}

int cpsw_ale_add_mcast(struct cpsw_ale *ale, u8 *addr, int port_mask,
		       int flags, u16 vid, int mcast_state)
{
	u32 ale_entry[ALE_ENTRY_WORDS] = {0, 0, 0};
	int idx, mask;

	idx = cpsw_ale_match_addr(ale, addr, (flags & ALE_VLAN) ? vid : 0);
	if (idx >= 0)
		cpsw_ale_read(ale, idx, ale_entry);

	cpsw_ale_set_vlan_entry_type(ale_entry, flags, vid);

	cpsw_ale_set_addr(ale_entry, addr);
	cpsw_ale_set_super(ale_entry, (flags & ALE_BLOCKED) ? 1 : 0);
	cpsw_ale_set_mcast_state(ale_entry, mcast_state);

	mask = cpsw_ale_get_port_mask(ale_entry);
	port_mask |= mask;
	cpsw_ale_set_port_mask(ale_entry, port_mask);

	if (idx < 0)
		idx = cpsw_ale_match_free(ale);
	if (idx < 0)
		idx = cpsw_ale_find_ageable(ale);
	if (idx < 0)
		return -ENOMEM;

	cpsw_ale_write(ale, idx, ale_entry);
	return 0;
}

int cpsw_ale_del_mcast(struct cpsw_ale *ale, u8 *addr, int port_mask,
		       int flags, u16 vid)
{
	u32 ale_entry[ALE_ENTRY_WORDS] = {0, 0, 0};
	int idx;

	idx = cpsw_ale_match_addr(ale, addr, (flags & ALE_VLAN) ? vid : 0);
	if (idx < 0)
		return -EINVAL;

	cpsw_ale_read(ale, idx, ale_entry);

	if (port_mask)
		cpsw_ale_set_port_mask(ale_entry, port_mask);
	else
		cpsw_ale_set_entry_type(ale_entry, ALE_TYPE_FREE);

	cpsw_ale_write(ale, idx, ale_entry);
	return 0;
}

int cpsw_ale_add_vlan(struct cpsw_ale *ale, u16 vid, int port, int untag,
		      int reg_mcast, int unreg_mcast)
{
	u32 ale_entry[ALE_ENTRY_WORDS] = {0, 0, 0};
	int idx;

	idx = cpsw_ale_match_vlan(ale, vid);
	if (idx >= 0)
		cpsw_ale_read(ale, idx, ale_entry);

	cpsw_ale_set_entry_type(ale_entry, ALE_TYPE_VLAN);
	cpsw_ale_set_vlan_id(ale_entry, vid);

	cpsw_ale_set_vlan_untag_force(ale_entry, untag);
	cpsw_ale_set_vlan_reg_mcast(ale_entry, reg_mcast);
	cpsw_ale_set_vlan_unreg_mcast(ale_entry, unreg_mcast);
	cpsw_ale_set_vlan_member_list(ale_entry, port);

	if (idx < 0)
		idx = cpsw_ale_match_free(ale);
	if (idx < 0)
		idx = cpsw_ale_find_ageable(ale);
	if (idx < 0)
		return -ENOMEM;

	cpsw_ale_write(ale, idx, ale_entry);
	return 0;
}

int cpsw_ale_del_vlan(struct cpsw_ale *ale, u16 vid, int port_mask)
{
	u32 ale_entry[ALE_ENTRY_WORDS] = {0, 0, 0};
	int idx;

	idx = cpsw_ale_match_vlan(ale, vid);
	if (idx < 0)
		return -ENOENT;

	cpsw_ale_read(ale, idx, ale_entry);

	if (port_mask)
		cpsw_ale_set_vlan_member_list(ale_entry, port_mask);
	else
		cpsw_ale_set_entry_type(ale_entry, ALE_TYPE_FREE);

	cpsw_ale_write(ale, idx, ale_entry);
	return 0;
}

struct ale_control_info {
	const char	*name;
	int		offset, port_offset;
	int		shift, port_shift;
	int		bits;
};

static const struct ale_control_info ale_controls[ALE_NUM_CONTROLS] = {
	[ALE_ENABLE]		= {
		.name		= "enable",
		.offset		= ALE_CONTROL,
		.port_offset	= 0,
		.shift		= 31,
		.port_shift	= 0,
		.bits		= 1,
	},
	[ALE_CLEAR]		= {
		.name		= "clear",
		.offset		= ALE_CONTROL,
		.port_offset	= 0,
		.shift		= 30,
		.port_shift	= 0,
		.bits		= 1,
	},
	[ALE_AGEOUT]		= {
		.name		= "ageout",
		.offset		= ALE_CONTROL,
		.port_offset	= 0,
		.shift		= 29,
		.port_shift	= 0,
		.bits		= 1,
	},
	[ALE_P0_UNI_FLOOD]	= {
		.name		= "port0_unicast_flood",
		.offset		= ALE_CONTROL,
		.port_offset	= 0,
		.shift		= 8,
		.port_shift	= 0,
		.bits		= 1,
	},
	[ALE_VLAN_NOLEARN]	= {
		.name		= "vlan_nolearn",
		.offset		= ALE_CONTROL,
		.port_offset	= 0,
		.shift		= 7,
		.port_shift	= 0,
		.bits		= 1,
	},
	[ALE_NO_PORT_VLAN]	= {
		.name		= "no_port_vlan",
		.offset		= ALE_CONTROL,
		.port_offset	= 0,
		.shift		= 6,
		.port_shift	= 0,
		.bits		= 1,
	},
	[ALE_OUI_DENY]		= {
		.name		= "oui_deny",
		.offset		= ALE_CONTROL,
		.port_offset	= 0,
		.shift		= 5,
		.port_shift	= 0,
		.bits		= 1,
	},
	[ALE_BYPASS]		= {
		.name		= "bypass",
		.offset		= ALE_CONTROL,
		.port_offset	= 0,
		.shift		= 4,
		.port_shift	= 0,
		.bits		= 1,
	},
	[ALE_RATE_LIMIT_TX]	= {
		.name		= "rate_limit_tx",
		.offset		= ALE_CONTROL,
		.port_offset	= 0,
		.shift		= 3,
		.port_shift	= 0,
		.bits		= 1,
	},
	[ALE_VLAN_AWARE]	= {
		.name		= "vlan_aware",
		.offset		= ALE_CONTROL,
		.port_offset	= 0,
		.shift		= 2,
		.port_shift	= 0,
		.bits		= 1,
	},
	[ALE_AUTH_ENABLE]	= {
		.name		= "auth_enable",
		.offset		= ALE_CONTROL,
		.port_offset	= 0,
		.shift		= 1,
		.port_shift	= 0,
		.bits		= 1,
	},
	[ALE_RATE_LIMIT]	= {
		.name		= "rate_limit",
		.offset		= ALE_CONTROL,
		.port_offset	= 0,
		.shift		= 0,
		.port_shift	= 0,
		.bits		= 1,
	},
	[ALE_PORT_STATE]	= {
		.name		= "port_state",
		.offset		= ALE_PORTCTL,
		.port_offset	= 4,
		.shift		= 0,
		.port_shift	= 0,
		.bits		= 2,
	},
	[ALE_PORT_DROP_UNTAGGED] = {
		.name		= "drop_untagged",
		.offset		= ALE_PORTCTL,
		.port_offset	= 4,
		.shift		= 2,
		.port_shift	= 0,
		.bits		= 1,
	},
	[ALE_PORT_DROP_UNKNOWN_VLAN] = {
		.name		= "drop_unknown",
		.offset		= ALE_PORTCTL,
		.port_offset	= 4,
		.shift		= 3,
		.port_shift	= 0,
		.bits		= 1,
	},
	[ALE_PORT_NOLEARN]	= {
		.name		= "nolearn",
		.offset		= ALE_PORTCTL,
		.port_offset	= 4,
		.shift		= 4,
		.port_shift	= 0,
		.bits		= 1,
	},
	[ALE_PORT_NO_SA_UPDATE]	= {
		.name		= "no_source_update",
		.offset		= ALE_PORTCTL,
		.port_offset	= 4,
		.shift		= 5,
		.port_shift	= 0,
		.bits		= 1,
	},
	[ALE_PORT_MCAST_LIMIT]	= {
		.name		= "mcast_limit",
		.offset		= ALE_PORTCTL,
		.port_offset	= 4,
		.shift		= 16,
		.port_shift	= 0,
		.bits		= 8,
	},
	[ALE_PORT_BCAST_LIMIT]	= {
		.name		= "bcast_limit",
		.offset		= ALE_PORTCTL,
		.port_offset	= 4,
		.shift		= 24,
		.port_shift	= 0,
		.bits		= 8,
	},
	[ALE_PORT_UNKNOWN_VLAN_MEMBER] = {
		.name		= "unknown_vlan_member",
		.offset		= ALE_UNKNOWNVLAN,
		.port_offset	= 0,
		.shift		= 0,
		.port_shift	= 0,
		.bits		= 6,
	},
	[ALE_PORT_UNKNOWN_MCAST_FLOOD] = {
		.name		= "unknown_mcast_flood",
		.offset		= ALE_UNKNOWNVLAN,
		.port_offset	= 0,
		.shift		= 8,
		.port_shift	= 0,
		.bits		= 6,
	},
	[ALE_PORT_UNKNOWN_REG_MCAST_FLOOD] = {
		.name		= "unknown_reg_flood",
		.offset		= ALE_UNKNOWNVLAN,
		.port_offset	= 0,
		.shift		= 16,
		.port_shift	= 0,
		.bits		= 6,
	},
	[ALE_PORT_UNTAGGED_EGRESS] = {
		.name		= "untagged_egress",
		.offset		= ALE_UNKNOWNVLAN,
		.port_offset	= 0,
		.shift		= 24,
		.port_shift	= 0,
		.bits		= 6,
	},
};

int cpsw_ale_control_set(struct cpsw_ale *ale, int port, int control,
			 int value)
{
	const struct ale_control_info *info;
	int offset, shift;
	u32 tmp, mask;

	if (control < 0 || control >= ARRAY_SIZE(ale_controls))
		return -EINVAL;

	info = &ale_controls[control];
	if (info->port_offset == 0 && info->port_shift == 0)
		port = 0; /* global, port is a dont care */

	if (port < 0 || port > ale->params.ale_ports)
		return -EINVAL;

	mask = BITMASK(info->bits);
	if (value & ~mask)
		return -EINVAL;

	offset = info->offset + (port * info->port_offset);
	shift  = info->shift  + (port * info->port_shift);

	tmp = __raw_readl(ale->params.ale_regs + offset);
	tmp = (tmp & ~(mask << shift)) | (value << shift);
	__raw_writel(tmp, ale->params.ale_regs + offset);

	return 0;
}

int cpsw_ale_control_get(struct cpsw_ale *ale, int port, int control)
{
	const struct ale_control_info *info;
	int offset, shift;
	u32 tmp;

	if (control < 0 || control >= ARRAY_SIZE(ale_controls))
		return -EINVAL;

	info = &ale_controls[control];
	if (info->port_offset == 0 && info->port_shift == 0)
		port = 0; /* global, port is a dont care */

	if (port < 0 || port > ale->params.ale_ports)
		return -EINVAL;

	offset = info->offset + (port * info->port_offset);
	shift  = info->shift  + (port * info->port_shift);

	tmp = __raw_readl(ale->params.ale_regs + offset) >> shift;
	return tmp & BITMASK(info->bits);
}

static void cpsw_ale_timer(unsigned long arg)
{
	struct cpsw_ale *ale = (struct cpsw_ale *)arg;

	cpsw_ale_control_set(ale, 0, ALE_AGEOUT, 1);

	if (ale->ageout) {
		ale->timer.expires = jiffies + ale->ageout;
		add_timer(&ale->timer);
	}
}

int cpsw_ale_set_ageout(struct cpsw_ale *ale, int ageout)
{
	del_timer_sync(&ale->timer);
	ale->ageout = ageout * HZ;
	if (ale->ageout) {
		ale->timer.expires = jiffies + ale->ageout;
		add_timer(&ale->timer);
	}
	return 0;
}

void cpsw_ale_start(struct cpsw_ale *ale)
{
	u32 rev;

	rev = __raw_readl(ale->params.ale_regs + ALE_IDVER);
	dev_dbg(ale->params.dev, "initialized cpsw ale revision %d.%d\n",
		ALE_VERSION_MAJOR(rev), ALE_VERSION_MINOR(rev));
	cpsw_ale_control_set(ale, 0, ALE_ENABLE, 1);
	cpsw_ale_control_set(ale, 0, ALE_CLEAR, 1);

	init_timer(&ale->timer);
	ale->timer.data	    = (unsigned long)ale;
	ale->timer.function = cpsw_ale_timer;
	if (ale->ageout) {
		ale->timer.expires = jiffies + ale->ageout;
		add_timer(&ale->timer);
	}
}

void cpsw_ale_stop(struct cpsw_ale *ale)
{
	del_timer_sync(&ale->timer);
}

struct cpsw_ale *cpsw_ale_create(struct cpsw_ale_params *params)
{
	struct cpsw_ale *ale;

	ale = kzalloc(sizeof(*ale), GFP_KERNEL);
	if (!ale)
		return NULL;

	ale->params = *params;
	ale->ageout = ale->params.ale_ageout * HZ;

	return ale;
}

int cpsw_ale_destroy(struct cpsw_ale *ale)
{
	if (!ale)
		return -EINVAL;
	cpsw_ale_stop(ale);
	cpsw_ale_control_set(ale, 0, ALE_ENABLE, 0);
	kfree(ale);
	return 0;
}
