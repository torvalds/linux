/*
 * Texas Instruments N-Port Ethernet Switch Address Lookup Engine
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
#include <linux/module.h>
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

#define ALE_VERSION_MAJOR(rev, mask) (((rev) >> 8) & (mask))
#define ALE_VERSION_MINOR(rev)	(rev & 0xff)
#define ALE_VERSION_1R3		0x0103
#define ALE_VERSION_1R4		0x0104

/* ALE Registers */
#define ALE_IDVER		0x00
#define ALE_STATUS		0x04
#define ALE_CONTROL		0x08
#define ALE_PRESCALE		0x10
#define ALE_UNKNOWNVLAN		0x18
#define ALE_TABLE_CONTROL	0x20
#define ALE_TABLE		0x34
#define ALE_PORTCTL		0x40

/* ALE NetCP NU switch specific Registers */
#define ALE_UNKNOWNVLAN_MEMBER			0x90
#define ALE_UNKNOWNVLAN_UNREG_MCAST_FLOOD	0x94
#define ALE_UNKNOWNVLAN_REG_MCAST_FLOOD		0x98
#define ALE_UNKNOWNVLAN_FORCE_UNTAG_EGRESS	0x9C
#define ALE_VLAN_MASK_MUX(reg)			(0xc0 + (0x4 * (reg)))

#define ALE_TABLE_WRITE		BIT(31)

#define ALE_TYPE_FREE			0
#define ALE_TYPE_ADDR			1
#define ALE_TYPE_VLAN			2
#define ALE_TYPE_VLAN_ADDR		3

#define ALE_UCAST_PERSISTANT		0
#define ALE_UCAST_UNTOUCHED		1
#define ALE_UCAST_OUI			2
#define ALE_UCAST_TOUCHED		3

#define ALE_TABLE_SIZE_MULTIPLIER	1024
#define ALE_STATUS_SIZE_MASK		0x1f
#define ALE_TABLE_SIZE_DEFAULT		64

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

#define DEFINE_ALE_FIELD1(name, start)					\
static inline int cpsw_ale_get_##name(u32 *ale_entry, u32 bits)		\
{									\
	return cpsw_ale_get_field(ale_entry, start, bits);		\
}									\
static inline void cpsw_ale_set_##name(u32 *ale_entry, u32 value,	\
		u32 bits)						\
{									\
	cpsw_ale_set_field(ale_entry, start, bits, value);		\
}

DEFINE_ALE_FIELD(entry_type,		60,	2)
DEFINE_ALE_FIELD(vlan_id,		48,	12)
DEFINE_ALE_FIELD(mcast_state,		62,	2)
DEFINE_ALE_FIELD1(port_mask,		66)
DEFINE_ALE_FIELD(super,			65,	1)
DEFINE_ALE_FIELD(ucast_type,		62,     2)
DEFINE_ALE_FIELD1(port_num,		66)
DEFINE_ALE_FIELD(blocked,		65,     1)
DEFINE_ALE_FIELD(secure,		64,     1)
DEFINE_ALE_FIELD1(vlan_untag_force,	24)
DEFINE_ALE_FIELD1(vlan_reg_mcast,	16)
DEFINE_ALE_FIELD1(vlan_unreg_mcast,	8)
DEFINE_ALE_FIELD1(vlan_member_list,	0)
DEFINE_ALE_FIELD(mcast,			40,	1)
/* ALE NetCP nu switch specific */
DEFINE_ALE_FIELD(vlan_unreg_mcast_idx,	20,	3)
DEFINE_ALE_FIELD(vlan_reg_mcast_idx,	44,	3)

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

	writel_relaxed(idx, ale->params.ale_regs + ALE_TABLE_CONTROL);

	for (i = 0; i < ALE_ENTRY_WORDS; i++)
		ale_entry[i] = readl_relaxed(ale->params.ale_regs +
					     ALE_TABLE + 4 * i);

	return idx;
}

static int cpsw_ale_write(struct cpsw_ale *ale, int idx, u32 *ale_entry)
{
	int i;

	WARN_ON(idx > ale->params.ale_entries);

	for (i = 0; i < ALE_ENTRY_WORDS; i++)
		writel_relaxed(ale_entry[i], ale->params.ale_regs +
			       ALE_TABLE + 4 * i);

	writel_relaxed(idx | ALE_TABLE_WRITE, ale->params.ale_regs +
		       ALE_TABLE_CONTROL);

	return idx;
}

static int cpsw_ale_match_addr(struct cpsw_ale *ale, u8 *addr, u16 vid)
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

static int cpsw_ale_match_vlan(struct cpsw_ale *ale, u16 vid)
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

	mask = cpsw_ale_get_port_mask(ale_entry,
				      ale->port_mask_bits);
	if ((mask & port_mask) == 0)
		return; /* ports dont intersect, not interested */
	mask &= ~port_mask;

	/* free if only remaining port is host port */
	if (mask)
		cpsw_ale_set_port_mask(ale_entry, mask,
				       ale->port_mask_bits);
	else
		cpsw_ale_set_entry_type(ale_entry, ALE_TYPE_FREE);
}

int cpsw_ale_flush_multicast(struct cpsw_ale *ale, int port_mask, int vid)
{
	u32 ale_entry[ALE_ENTRY_WORDS];
	int ret, idx;

	for (idx = 0; idx < ale->params.ale_entries; idx++) {
		cpsw_ale_read(ale, idx, ale_entry);
		ret = cpsw_ale_get_entry_type(ale_entry);
		if (ret != ALE_TYPE_ADDR && ret != ALE_TYPE_VLAN_ADDR)
			continue;

		/* if vid passed is -1 then remove all multicast entry from
		 * the table irrespective of vlan id, if a valid vlan id is
		 * passed then remove only multicast added to that vlan id.
		 * if vlan id doesn't match then move on to next entry.
		 */
		if (vid != -1 && cpsw_ale_get_vlan_id(ale_entry) != vid)
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
EXPORT_SYMBOL_GPL(cpsw_ale_flush_multicast);

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
	cpsw_ale_set_port_num(ale_entry, port, ale->port_num_bits);

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
EXPORT_SYMBOL_GPL(cpsw_ale_add_ucast);

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
EXPORT_SYMBOL_GPL(cpsw_ale_del_ucast);

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

	mask = cpsw_ale_get_port_mask(ale_entry,
				      ale->port_mask_bits);
	port_mask |= mask;
	cpsw_ale_set_port_mask(ale_entry, port_mask,
			       ale->port_mask_bits);

	if (idx < 0)
		idx = cpsw_ale_match_free(ale);
	if (idx < 0)
		idx = cpsw_ale_find_ageable(ale);
	if (idx < 0)
		return -ENOMEM;

	cpsw_ale_write(ale, idx, ale_entry);
	return 0;
}
EXPORT_SYMBOL_GPL(cpsw_ale_add_mcast);

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
		cpsw_ale_set_port_mask(ale_entry, port_mask,
				       ale->port_mask_bits);
	else
		cpsw_ale_set_entry_type(ale_entry, ALE_TYPE_FREE);

	cpsw_ale_write(ale, idx, ale_entry);
	return 0;
}
EXPORT_SYMBOL_GPL(cpsw_ale_del_mcast);

/* ALE NetCP NU switch specific vlan functions */
static void cpsw_ale_set_vlan_mcast(struct cpsw_ale *ale, u32 *ale_entry,
				    int reg_mcast, int unreg_mcast)
{
	int idx;

	/* Set VLAN registered multicast flood mask */
	idx = cpsw_ale_get_vlan_reg_mcast_idx(ale_entry);
	writel(reg_mcast, ale->params.ale_regs + ALE_VLAN_MASK_MUX(idx));

	/* Set VLAN unregistered multicast flood mask */
	idx = cpsw_ale_get_vlan_unreg_mcast_idx(ale_entry);
	writel(unreg_mcast, ale->params.ale_regs + ALE_VLAN_MASK_MUX(idx));
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

	cpsw_ale_set_vlan_untag_force(ale_entry, untag, ale->vlan_field_bits);
	if (!ale->params.nu_switch_ale) {
		cpsw_ale_set_vlan_reg_mcast(ale_entry, reg_mcast,
					    ale->vlan_field_bits);
		cpsw_ale_set_vlan_unreg_mcast(ale_entry, unreg_mcast,
					      ale->vlan_field_bits);
	} else {
		cpsw_ale_set_vlan_mcast(ale, ale_entry, reg_mcast, unreg_mcast);
	}
	cpsw_ale_set_vlan_member_list(ale_entry, port, ale->vlan_field_bits);

	if (idx < 0)
		idx = cpsw_ale_match_free(ale);
	if (idx < 0)
		idx = cpsw_ale_find_ageable(ale);
	if (idx < 0)
		return -ENOMEM;

	cpsw_ale_write(ale, idx, ale_entry);
	return 0;
}
EXPORT_SYMBOL_GPL(cpsw_ale_add_vlan);

int cpsw_ale_del_vlan(struct cpsw_ale *ale, u16 vid, int port_mask)
{
	u32 ale_entry[ALE_ENTRY_WORDS] = {0, 0, 0};
	int idx;

	idx = cpsw_ale_match_vlan(ale, vid);
	if (idx < 0)
		return -ENOENT;

	cpsw_ale_read(ale, idx, ale_entry);

	if (port_mask)
		cpsw_ale_set_vlan_member_list(ale_entry, port_mask,
					      ale->vlan_field_bits);
	else
		cpsw_ale_set_entry_type(ale_entry, ALE_TYPE_FREE);

	cpsw_ale_write(ale, idx, ale_entry);
	return 0;
}
EXPORT_SYMBOL_GPL(cpsw_ale_del_vlan);

void cpsw_ale_set_allmulti(struct cpsw_ale *ale, int allmulti)
{
	u32 ale_entry[ALE_ENTRY_WORDS];
	int type, idx;
	int unreg_mcast = 0;

	/* Only bother doing the work if the setting is actually changing */
	if (ale->allmulti == allmulti)
		return;

	/* Remember the new setting to check against next time */
	ale->allmulti = allmulti;

	for (idx = 0; idx < ale->params.ale_entries; idx++) {
		cpsw_ale_read(ale, idx, ale_entry);
		type = cpsw_ale_get_entry_type(ale_entry);
		if (type != ALE_TYPE_VLAN)
			continue;

		unreg_mcast =
			cpsw_ale_get_vlan_unreg_mcast(ale_entry,
						      ale->vlan_field_bits);
		if (allmulti)
			unreg_mcast |= 1;
		else
			unreg_mcast &= ~1;
		cpsw_ale_set_vlan_unreg_mcast(ale_entry, unreg_mcast,
					      ale->vlan_field_bits);
		cpsw_ale_write(ale, idx, ale_entry);
	}
}
EXPORT_SYMBOL_GPL(cpsw_ale_set_allmulti);

struct ale_control_info {
	const char	*name;
	int		offset, port_offset;
	int		shift, port_shift;
	int		bits;
};

static struct ale_control_info ale_controls[ALE_NUM_CONTROLS] = {
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

	tmp = readl_relaxed(ale->params.ale_regs + offset);
	tmp = (tmp & ~(mask << shift)) | (value << shift);
	writel_relaxed(tmp, ale->params.ale_regs + offset);

	return 0;
}
EXPORT_SYMBOL_GPL(cpsw_ale_control_set);

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

	tmp = readl_relaxed(ale->params.ale_regs + offset) >> shift;
	return tmp & BITMASK(info->bits);
}
EXPORT_SYMBOL_GPL(cpsw_ale_control_get);

static void cpsw_ale_timer(struct timer_list *t)
{
	struct cpsw_ale *ale = from_timer(ale, t, timer);

	cpsw_ale_control_set(ale, 0, ALE_AGEOUT, 1);

	if (ale->ageout) {
		ale->timer.expires = jiffies + ale->ageout;
		add_timer(&ale->timer);
	}
}

void cpsw_ale_start(struct cpsw_ale *ale)
{
	u32 rev, ale_entries;

	rev = readl_relaxed(ale->params.ale_regs + ALE_IDVER);
	if (!ale->params.major_ver_mask)
		ale->params.major_ver_mask = 0xff;
	ale->version =
		(ALE_VERSION_MAJOR(rev, ale->params.major_ver_mask) << 8) |
		 ALE_VERSION_MINOR(rev);
	dev_info(ale->params.dev, "initialized cpsw ale version %d.%d\n",
		 ALE_VERSION_MAJOR(rev, ale->params.major_ver_mask),
		 ALE_VERSION_MINOR(rev));

	if (!ale->params.ale_entries) {
		ale_entries =
			readl_relaxed(ale->params.ale_regs + ALE_STATUS) &
			ALE_STATUS_SIZE_MASK;
		/* ALE available on newer NetCP switches has introduced
		 * a register, ALE_STATUS, to indicate the size of ALE
		 * table which shows the size as a multiple of 1024 entries.
		 * For these, params.ale_entries will be set to zero. So
		 * read the register and update the value of ale_entries.
		 * ALE table on NetCP lite, is much smaller and is indicated
		 * by a value of zero in ALE_STATUS. So use a default value
		 * of ALE_TABLE_SIZE_DEFAULT for this. Caller is expected
		 * to set the value of ale_entries for all other versions
		 * of ALE.
		 */
		if (!ale_entries)
			ale_entries = ALE_TABLE_SIZE_DEFAULT;
		else
			ale_entries *= ALE_TABLE_SIZE_MULTIPLIER;
		ale->params.ale_entries = ale_entries;
	}
	dev_info(ale->params.dev,
		 "ALE Table size %ld\n", ale->params.ale_entries);

	/* set default bits for existing h/w */
	ale->port_mask_bits = 3;
	ale->port_num_bits = 2;
	ale->vlan_field_bits = 3;

	/* Set defaults override for ALE on NetCP NU switch and for version
	 * 1R3
	 */
	if (ale->params.nu_switch_ale) {
		/* Separate registers for unknown vlan configuration.
		 * Also there are N bits, where N is number of ale
		 * ports and shift value should be 0
		 */
		ale_controls[ALE_PORT_UNKNOWN_VLAN_MEMBER].bits =
					ale->params.ale_ports;
		ale_controls[ALE_PORT_UNKNOWN_VLAN_MEMBER].offset =
					ALE_UNKNOWNVLAN_MEMBER;
		ale_controls[ALE_PORT_UNKNOWN_MCAST_FLOOD].bits =
					ale->params.ale_ports;
		ale_controls[ALE_PORT_UNKNOWN_MCAST_FLOOD].shift = 0;
		ale_controls[ALE_PORT_UNKNOWN_MCAST_FLOOD].offset =
					ALE_UNKNOWNVLAN_UNREG_MCAST_FLOOD;
		ale_controls[ALE_PORT_UNKNOWN_REG_MCAST_FLOOD].bits =
					ale->params.ale_ports;
		ale_controls[ALE_PORT_UNKNOWN_REG_MCAST_FLOOD].shift = 0;
		ale_controls[ALE_PORT_UNKNOWN_REG_MCAST_FLOOD].offset =
					ALE_UNKNOWNVLAN_REG_MCAST_FLOOD;
		ale_controls[ALE_PORT_UNTAGGED_EGRESS].bits =
					ale->params.ale_ports;
		ale_controls[ALE_PORT_UNTAGGED_EGRESS].shift = 0;
		ale_controls[ALE_PORT_UNTAGGED_EGRESS].offset =
					ALE_UNKNOWNVLAN_FORCE_UNTAG_EGRESS;
		ale->port_mask_bits = ale->params.ale_ports;
		ale->port_num_bits = ale->params.ale_ports - 1;
		ale->vlan_field_bits = ale->params.ale_ports;
	} else if (ale->version == ALE_VERSION_1R3) {
		ale->port_mask_bits = ale->params.ale_ports;
		ale->port_num_bits = 3;
		ale->vlan_field_bits = ale->params.ale_ports;
	}

	cpsw_ale_control_set(ale, 0, ALE_ENABLE, 1);
	cpsw_ale_control_set(ale, 0, ALE_CLEAR, 1);

	timer_setup(&ale->timer, cpsw_ale_timer, 0);
	if (ale->ageout) {
		ale->timer.expires = jiffies + ale->ageout;
		add_timer(&ale->timer);
	}
}
EXPORT_SYMBOL_GPL(cpsw_ale_start);

void cpsw_ale_stop(struct cpsw_ale *ale)
{
	del_timer_sync(&ale->timer);
}
EXPORT_SYMBOL_GPL(cpsw_ale_stop);

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
EXPORT_SYMBOL_GPL(cpsw_ale_create);

int cpsw_ale_destroy(struct cpsw_ale *ale)
{
	if (!ale)
		return -EINVAL;
	cpsw_ale_control_set(ale, 0, ALE_ENABLE, 0);
	kfree(ale);
	return 0;
}
EXPORT_SYMBOL_GPL(cpsw_ale_destroy);

void cpsw_ale_dump(struct cpsw_ale *ale, u32 *data)
{
	int i;

	for (i = 0; i < ale->params.ale_entries; i++) {
		cpsw_ale_read(ale, i, data);
		data += ALE_ENTRY_WORDS;
	}
}
EXPORT_SYMBOL_GPL(cpsw_ale_dump);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("TI CPSW ALE driver");
MODULE_AUTHOR("Texas Instruments");
