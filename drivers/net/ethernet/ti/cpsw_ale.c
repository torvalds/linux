// SPDX-License-Identifier: GPL-2.0
/*
 * Texas Instruments N-Port Ethernet Switch Address Lookup Engine
 *
 * Copyright (C) 2012 Texas Instruments
 *
 */
#include <linux/bitmap.h>
#include <linux/if_vlan.h>
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
#define ALE_AGING_TIMER		0x14
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

#define AM65_CPSW_ALE_THREAD_DEF_REG 0x134

/* ALE_AGING_TIMER */
#define ALE_AGING_TIMER_MASK	GENMASK(23, 0)

/**
 * struct ale_entry_fld - The ALE tbl entry field description
 * @start_bit: field start bit
 * @num_bits: field bit length
 * @flags: field flags
 */
struct ale_entry_fld {
	u8 start_bit;
	u8 num_bits;
	u8 flags;
};

enum {
	CPSW_ALE_F_STATUS_REG = BIT(0), /* Status register present */
	CPSW_ALE_F_HW_AUTOAGING = BIT(1), /* HW auto aging */

	CPSW_ALE_F_COUNT
};

/**
 * struct ale_dev_id - The ALE version/SoC specific configuration
 * @dev_id: ALE version/SoC id
 * @features: features supported by ALE
 * @tbl_entries: number of ALE entries
 * @major_ver_mask: mask of ALE Major Version Value in ALE_IDVER reg.
 * @nu_switch_ale: NU Switch ALE
 * @vlan_entry_tbl: ALE vlan entry fields description tbl
 */
struct cpsw_ale_dev_id {
	const char *dev_id;
	u32 features;
	u32 tbl_entries;
	u32 major_ver_mask;
	bool nu_switch_ale;
	const struct ale_entry_fld *vlan_entry_tbl;
};

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

enum {
	ALE_ENT_VID_MEMBER_LIST = 0,
	ALE_ENT_VID_UNREG_MCAST_MSK,
	ALE_ENT_VID_REG_MCAST_MSK,
	ALE_ENT_VID_FORCE_UNTAGGED_MSK,
	ALE_ENT_VID_UNREG_MCAST_IDX,
	ALE_ENT_VID_REG_MCAST_IDX,
	ALE_ENT_VID_LAST,
};

#define ALE_FLD_ALLOWED			BIT(0)
#define ALE_FLD_SIZE_PORT_MASK_BITS	BIT(1)
#define ALE_FLD_SIZE_PORT_NUM_BITS	BIT(2)

#define ALE_ENTRY_FLD(id, start, bits)	\
[id] = {				\
	.start_bit = start,		\
	.num_bits = bits,		\
	.flags = ALE_FLD_ALLOWED,	\
}

#define ALE_ENTRY_FLD_DYN_MSK_SIZE(id, start)	\
[id] = {					\
	.start_bit = start,			\
	.num_bits = 0,				\
	.flags = ALE_FLD_ALLOWED |		\
		 ALE_FLD_SIZE_PORT_MASK_BITS,	\
}

/* dm814x, am3/am4/am5, k2hk */
static const struct ale_entry_fld vlan_entry_cpsw[ALE_ENT_VID_LAST] = {
	ALE_ENTRY_FLD(ALE_ENT_VID_MEMBER_LIST, 0, 3),
	ALE_ENTRY_FLD(ALE_ENT_VID_UNREG_MCAST_MSK, 8, 3),
	ALE_ENTRY_FLD(ALE_ENT_VID_REG_MCAST_MSK, 16, 3),
	ALE_ENTRY_FLD(ALE_ENT_VID_FORCE_UNTAGGED_MSK, 24, 3),
};

/* k2e/k2l, k3 am65/j721e cpsw2g  */
static const struct ale_entry_fld vlan_entry_nu[ALE_ENT_VID_LAST] = {
	ALE_ENTRY_FLD_DYN_MSK_SIZE(ALE_ENT_VID_MEMBER_LIST, 0),
	ALE_ENTRY_FLD(ALE_ENT_VID_UNREG_MCAST_IDX, 20, 3),
	ALE_ENTRY_FLD_DYN_MSK_SIZE(ALE_ENT_VID_FORCE_UNTAGGED_MSK, 24),
	ALE_ENTRY_FLD(ALE_ENT_VID_REG_MCAST_IDX, 44, 3),
};

/* K3 j721e/j7200 cpsw9g/5g, am64x cpsw3g  */
static const struct ale_entry_fld vlan_entry_k3_cpswxg[] = {
	ALE_ENTRY_FLD_DYN_MSK_SIZE(ALE_ENT_VID_MEMBER_LIST, 0),
	ALE_ENTRY_FLD_DYN_MSK_SIZE(ALE_ENT_VID_UNREG_MCAST_MSK, 12),
	ALE_ENTRY_FLD_DYN_MSK_SIZE(ALE_ENT_VID_FORCE_UNTAGGED_MSK, 24),
	ALE_ENTRY_FLD_DYN_MSK_SIZE(ALE_ENT_VID_REG_MCAST_MSK, 36),
};

DEFINE_ALE_FIELD(entry_type,		60,	2)
DEFINE_ALE_FIELD(vlan_id,		48,	12)
DEFINE_ALE_FIELD(mcast_state,		62,	2)
DEFINE_ALE_FIELD1(port_mask,		66)
DEFINE_ALE_FIELD(super,			65,	1)
DEFINE_ALE_FIELD(ucast_type,		62,     2)
DEFINE_ALE_FIELD1(port_num,		66)
DEFINE_ALE_FIELD(blocked,		65,     1)
DEFINE_ALE_FIELD(secure,		64,     1)
DEFINE_ALE_FIELD(mcast,			40,	1)

#define NU_VLAN_UNREG_MCAST_IDX	1

static int cpsw_ale_entry_get_fld(struct cpsw_ale *ale,
				  u32 *ale_entry,
				  const struct ale_entry_fld *entry_tbl,
				  int fld_id)
{
	const struct ale_entry_fld *entry_fld;
	u32 bits;

	if (!ale || !ale_entry)
		return -EINVAL;

	entry_fld = &entry_tbl[fld_id];
	if (!(entry_fld->flags & ALE_FLD_ALLOWED)) {
		dev_err(ale->params.dev, "get: wrong ale fld id %d\n", fld_id);
		return -ENOENT;
	}

	bits = entry_fld->num_bits;
	if (entry_fld->flags & ALE_FLD_SIZE_PORT_MASK_BITS)
		bits = ale->port_mask_bits;

	return cpsw_ale_get_field(ale_entry, entry_fld->start_bit, bits);
}

static void cpsw_ale_entry_set_fld(struct cpsw_ale *ale,
				   u32 *ale_entry,
				   const struct ale_entry_fld *entry_tbl,
				   int fld_id,
				   u32 value)
{
	const struct ale_entry_fld *entry_fld;
	u32 bits;

	if (!ale || !ale_entry)
		return;

	entry_fld = &entry_tbl[fld_id];
	if (!(entry_fld->flags & ALE_FLD_ALLOWED)) {
		dev_err(ale->params.dev, "set: wrong ale fld id %d\n", fld_id);
		return;
	}

	bits = entry_fld->num_bits;
	if (entry_fld->flags & ALE_FLD_SIZE_PORT_MASK_BITS)
		bits = ale->port_mask_bits;

	cpsw_ale_set_field(ale_entry, entry_fld->start_bit, bits, value);
}

static int cpsw_ale_vlan_get_fld(struct cpsw_ale *ale,
				 u32 *ale_entry,
				 int fld_id)
{
	return cpsw_ale_entry_get_fld(ale, ale_entry,
				      ale->vlan_entry_tbl, fld_id);
}

static void cpsw_ale_vlan_set_fld(struct cpsw_ale *ale,
				  u32 *ale_entry,
				  int fld_id,
				  u32 value)
{
	cpsw_ale_entry_set_fld(ale, ale_entry,
			       ale->vlan_entry_tbl, fld_id, value);
}

/* The MAC address field in the ALE entry cannot be macroized as above */
static inline void cpsw_ale_get_addr(u32 *ale_entry, u8 *addr)
{
	int i;

	for (i = 0; i < 6; i++)
		addr[i] = cpsw_ale_get_field(ale_entry, 40 - 8*i, 8);
}

static inline void cpsw_ale_set_addr(u32 *ale_entry, const u8 *addr)
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

static int cpsw_ale_match_addr(struct cpsw_ale *ale, const u8 *addr, u16 vid)
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

			if (cpsw_ale_get_super(ale_entry))
				continue;

			cpsw_ale_get_addr(ale_entry, addr);
			if (!is_broadcast_ether_addr(addr))
				cpsw_ale_flush_mcast(ale, ale_entry, port_mask);
		}

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

int cpsw_ale_add_ucast(struct cpsw_ale *ale, const u8 *addr, int port,
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

int cpsw_ale_del_ucast(struct cpsw_ale *ale, const u8 *addr, int port,
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

int cpsw_ale_add_mcast(struct cpsw_ale *ale, const u8 *addr, int port_mask,
		       int flags, u16 vid, int mcast_state)
{
	u32 ale_entry[ALE_ENTRY_WORDS] = {0, 0, 0};
	int idx, mask;

	idx = cpsw_ale_match_addr(ale, addr, (flags & ALE_VLAN) ? vid : 0);
	if (idx >= 0)
		cpsw_ale_read(ale, idx, ale_entry);

	cpsw_ale_set_vlan_entry_type(ale_entry, flags, vid);

	cpsw_ale_set_addr(ale_entry, addr);
	cpsw_ale_set_super(ale_entry, (flags & ALE_SUPER) ? 1 : 0);
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

int cpsw_ale_del_mcast(struct cpsw_ale *ale, const u8 *addr, int port_mask,
		       int flags, u16 vid)
{
	u32 ale_entry[ALE_ENTRY_WORDS] = {0, 0, 0};
	int mcast_members = 0;
	int idx;

	idx = cpsw_ale_match_addr(ale, addr, (flags & ALE_VLAN) ? vid : 0);
	if (idx < 0)
		return -ENOENT;

	cpsw_ale_read(ale, idx, ale_entry);

	if (port_mask) {
		mcast_members = cpsw_ale_get_port_mask(ale_entry,
						       ale->port_mask_bits);
		mcast_members &= ~port_mask;
	}

	if (mcast_members)
		cpsw_ale_set_port_mask(ale_entry, mcast_members,
				       ale->port_mask_bits);
	else
		cpsw_ale_set_entry_type(ale_entry, ALE_TYPE_FREE);

	cpsw_ale_write(ale, idx, ale_entry);
	return 0;
}

/* ALE NetCP NU switch specific vlan functions */
static void cpsw_ale_set_vlan_mcast(struct cpsw_ale *ale, u32 *ale_entry,
				    int reg_mcast, int unreg_mcast)
{
	int idx;

	/* Set VLAN registered multicast flood mask */
	idx = cpsw_ale_vlan_get_fld(ale, ale_entry,
				    ALE_ENT_VID_REG_MCAST_IDX);
	writel(reg_mcast, ale->params.ale_regs + ALE_VLAN_MASK_MUX(idx));

	/* Set VLAN unregistered multicast flood mask */
	idx = cpsw_ale_vlan_get_fld(ale, ale_entry,
				    ALE_ENT_VID_UNREG_MCAST_IDX);
	writel(unreg_mcast, ale->params.ale_regs + ALE_VLAN_MASK_MUX(idx));
}

static void cpsw_ale_set_vlan_untag(struct cpsw_ale *ale, u32 *ale_entry,
				    u16 vid, int untag_mask)
{
	cpsw_ale_vlan_set_fld(ale, ale_entry,
			      ALE_ENT_VID_FORCE_UNTAGGED_MSK,
			      untag_mask);
	if (untag_mask & ALE_PORT_HOST)
		bitmap_set(ale->p0_untag_vid_mask, vid, 1);
	else
		bitmap_clear(ale->p0_untag_vid_mask, vid, 1);
}

int cpsw_ale_add_vlan(struct cpsw_ale *ale, u16 vid, int port_mask, int untag,
		      int reg_mcast, int unreg_mcast)
{
	u32 ale_entry[ALE_ENTRY_WORDS] = {0, 0, 0};
	int idx;

	idx = cpsw_ale_match_vlan(ale, vid);
	if (idx >= 0)
		cpsw_ale_read(ale, idx, ale_entry);

	cpsw_ale_set_entry_type(ale_entry, ALE_TYPE_VLAN);
	cpsw_ale_set_vlan_id(ale_entry, vid);
	cpsw_ale_set_vlan_untag(ale, ale_entry, vid, untag);

	if (!ale->params.nu_switch_ale) {
		cpsw_ale_vlan_set_fld(ale, ale_entry,
				      ALE_ENT_VID_REG_MCAST_MSK, reg_mcast);
		cpsw_ale_vlan_set_fld(ale, ale_entry,
				      ALE_ENT_VID_UNREG_MCAST_MSK, unreg_mcast);
	} else {
		cpsw_ale_vlan_set_fld(ale, ale_entry,
				      ALE_ENT_VID_UNREG_MCAST_IDX,
				      NU_VLAN_UNREG_MCAST_IDX);
		cpsw_ale_set_vlan_mcast(ale, ale_entry, reg_mcast, unreg_mcast);
	}

	cpsw_ale_vlan_set_fld(ale, ale_entry,
			      ALE_ENT_VID_MEMBER_LIST, port_mask);

	if (idx < 0)
		idx = cpsw_ale_match_free(ale);
	if (idx < 0)
		idx = cpsw_ale_find_ageable(ale);
	if (idx < 0)
		return -ENOMEM;

	cpsw_ale_write(ale, idx, ale_entry);
	return 0;
}

static void cpsw_ale_vlan_del_modify_int(struct cpsw_ale *ale,  u32 *ale_entry,
					 u16 vid, int port_mask)
{
	int reg_mcast, unreg_mcast;
	int members, untag;

	members = cpsw_ale_vlan_get_fld(ale, ale_entry,
					ALE_ENT_VID_MEMBER_LIST);
	members &= ~port_mask;
	if (!members) {
		cpsw_ale_set_vlan_untag(ale, ale_entry, vid, 0);
		cpsw_ale_set_entry_type(ale_entry, ALE_TYPE_FREE);
		return;
	}

	untag = cpsw_ale_vlan_get_fld(ale, ale_entry,
				      ALE_ENT_VID_FORCE_UNTAGGED_MSK);
	reg_mcast = cpsw_ale_vlan_get_fld(ale, ale_entry,
					  ALE_ENT_VID_REG_MCAST_MSK);
	unreg_mcast = cpsw_ale_vlan_get_fld(ale, ale_entry,
					    ALE_ENT_VID_UNREG_MCAST_MSK);
	untag &= members;
	reg_mcast &= members;
	unreg_mcast &= members;

	cpsw_ale_set_vlan_untag(ale, ale_entry, vid, untag);

	if (!ale->params.nu_switch_ale) {
		cpsw_ale_vlan_set_fld(ale, ale_entry,
				      ALE_ENT_VID_REG_MCAST_MSK, reg_mcast);
		cpsw_ale_vlan_set_fld(ale, ale_entry,
				      ALE_ENT_VID_UNREG_MCAST_MSK, unreg_mcast);
	} else {
		cpsw_ale_set_vlan_mcast(ale, ale_entry, reg_mcast,
					unreg_mcast);
	}
	cpsw_ale_vlan_set_fld(ale, ale_entry,
			      ALE_ENT_VID_MEMBER_LIST, members);
}

int cpsw_ale_vlan_del_modify(struct cpsw_ale *ale, u16 vid, int port_mask)
{
	u32 ale_entry[ALE_ENTRY_WORDS] = {0, 0, 0};
	int idx;

	idx = cpsw_ale_match_vlan(ale, vid);
	if (idx < 0)
		return -ENOENT;

	cpsw_ale_read(ale, idx, ale_entry);

	cpsw_ale_vlan_del_modify_int(ale, ale_entry, vid, port_mask);
	cpsw_ale_write(ale, idx, ale_entry);

	return 0;
}

int cpsw_ale_del_vlan(struct cpsw_ale *ale, u16 vid, int port_mask)
{
	u32 ale_entry[ALE_ENTRY_WORDS] = {0, 0, 0};
	int members, idx;

	idx = cpsw_ale_match_vlan(ale, vid);
	if (idx < 0)
		return -ENOENT;

	cpsw_ale_read(ale, idx, ale_entry);

	/* if !port_mask - force remove VLAN (legacy).
	 * Check if there are other VLAN members ports
	 * if no - remove VLAN.
	 * if yes it means same VLAN was added to >1 port in multi port mode, so
	 * remove port_mask ports from VLAN ALE entry excluding Host port.
	 */
	members = cpsw_ale_vlan_get_fld(ale, ale_entry, ALE_ENT_VID_MEMBER_LIST);
	members &= ~port_mask;

	if (!port_mask || !members) {
		/* last port or force remove - remove VLAN */
		cpsw_ale_set_vlan_untag(ale, ale_entry, vid, 0);
		cpsw_ale_set_entry_type(ale_entry, ALE_TYPE_FREE);
	} else {
		port_mask &= ~ALE_PORT_HOST;
		cpsw_ale_vlan_del_modify_int(ale, ale_entry, vid, port_mask);
	}

	cpsw_ale_write(ale, idx, ale_entry);

	return 0;
}

int cpsw_ale_vlan_add_modify(struct cpsw_ale *ale, u16 vid, int port_mask,
			     int untag_mask, int reg_mask, int unreg_mask)
{
	u32 ale_entry[ALE_ENTRY_WORDS] = {0, 0, 0};
	int reg_mcast_members, unreg_mcast_members;
	int vlan_members, untag_members;
	int idx, ret = 0;

	idx = cpsw_ale_match_vlan(ale, vid);
	if (idx >= 0)
		cpsw_ale_read(ale, idx, ale_entry);

	vlan_members = cpsw_ale_vlan_get_fld(ale, ale_entry,
					     ALE_ENT_VID_MEMBER_LIST);
	reg_mcast_members = cpsw_ale_vlan_get_fld(ale, ale_entry,
						  ALE_ENT_VID_REG_MCAST_MSK);
	unreg_mcast_members =
		cpsw_ale_vlan_get_fld(ale, ale_entry,
				      ALE_ENT_VID_UNREG_MCAST_MSK);
	untag_members = cpsw_ale_vlan_get_fld(ale, ale_entry,
					      ALE_ENT_VID_FORCE_UNTAGGED_MSK);

	vlan_members |= port_mask;
	untag_members = (untag_members & ~port_mask) | untag_mask;
	reg_mcast_members = (reg_mcast_members & ~port_mask) | reg_mask;
	unreg_mcast_members = (unreg_mcast_members & ~port_mask) | unreg_mask;

	ret = cpsw_ale_add_vlan(ale, vid, vlan_members, untag_members,
				reg_mcast_members, unreg_mcast_members);
	if (ret) {
		dev_err(ale->params.dev, "Unable to add vlan\n");
		return ret;
	}
	dev_dbg(ale->params.dev, "port mask 0x%x untag 0x%x\n", vlan_members,
		untag_mask);

	return ret;
}

void cpsw_ale_set_unreg_mcast(struct cpsw_ale *ale, int unreg_mcast_mask,
			      bool add)
{
	u32 ale_entry[ALE_ENTRY_WORDS];
	int unreg_members = 0;
	int type, idx;

	for (idx = 0; idx < ale->params.ale_entries; idx++) {
		cpsw_ale_read(ale, idx, ale_entry);
		type = cpsw_ale_get_entry_type(ale_entry);
		if (type != ALE_TYPE_VLAN)
			continue;

		unreg_members =
			cpsw_ale_vlan_get_fld(ale, ale_entry,
					      ALE_ENT_VID_UNREG_MCAST_MSK);
		if (add)
			unreg_members |= unreg_mcast_mask;
		else
			unreg_members &= ~unreg_mcast_mask;
		cpsw_ale_vlan_set_fld(ale, ale_entry,
				      ALE_ENT_VID_UNREG_MCAST_MSK,
				      unreg_members);
		cpsw_ale_write(ale, idx, ale_entry);
	}
}

static void cpsw_ale_vlan_set_unreg_mcast(struct cpsw_ale *ale, u32 *ale_entry,
					  int allmulti)
{
	int unreg_mcast;

	unreg_mcast = cpsw_ale_vlan_get_fld(ale, ale_entry,
					    ALE_ENT_VID_UNREG_MCAST_MSK);
	if (allmulti)
		unreg_mcast |= ALE_PORT_HOST;
	else
		unreg_mcast &= ~ALE_PORT_HOST;

	cpsw_ale_vlan_set_fld(ale, ale_entry,
			      ALE_ENT_VID_UNREG_MCAST_MSK, unreg_mcast);
}

static void
cpsw_ale_vlan_set_unreg_mcast_idx(struct cpsw_ale *ale, u32 *ale_entry,
				  int allmulti)
{
	int unreg_mcast;
	int idx;

	idx = cpsw_ale_vlan_get_fld(ale, ale_entry,
				    ALE_ENT_VID_UNREG_MCAST_IDX);

	unreg_mcast = readl(ale->params.ale_regs + ALE_VLAN_MASK_MUX(idx));

	if (allmulti)
		unreg_mcast |= ALE_PORT_HOST;
	else
		unreg_mcast &= ~ALE_PORT_HOST;

	writel(unreg_mcast, ale->params.ale_regs + ALE_VLAN_MASK_MUX(idx));
}

void cpsw_ale_set_allmulti(struct cpsw_ale *ale, int allmulti, int port)
{
	u32 ale_entry[ALE_ENTRY_WORDS];
	int type, idx;

	for (idx = 0; idx < ale->params.ale_entries; idx++) {
		int vlan_members;

		cpsw_ale_read(ale, idx, ale_entry);
		type = cpsw_ale_get_entry_type(ale_entry);
		if (type != ALE_TYPE_VLAN)
			continue;

		vlan_members = cpsw_ale_vlan_get_fld(ale, ale_entry,
						     ALE_ENT_VID_MEMBER_LIST);

		if (port != -1 && !(vlan_members & BIT(port)))
			continue;

		if (!ale->params.nu_switch_ale)
			cpsw_ale_vlan_set_unreg_mcast(ale, ale_entry, allmulti);
		else
			cpsw_ale_vlan_set_unreg_mcast_idx(ale, ale_entry,
							  allmulti);

		cpsw_ale_write(ale, idx, ale_entry);
	}
}

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
	[ALE_PORT_MACONLY]	= {
		.name		= "mac_only_port_mode",
		.offset		= ALE_PORTCTL,
		.port_offset	= 4,
		.shift		= 11,
		.port_shift	= 0,
		.bits		= 1,
	},
	[ALE_PORT_MACONLY_CAF]	= {
		.name		= "mac_only_port_caf",
		.offset		= ALE_PORTCTL,
		.port_offset	= 4,
		.shift		= 13,
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
	[ALE_DEFAULT_THREAD_ID] = {
		.name		= "default_thread_id",
		.offset		= AM65_CPSW_ALE_THREAD_DEF_REG,
		.port_offset	= 0,
		.shift		= 0,
		.port_shift	= 0,
		.bits		= 6,
	},
	[ALE_DEFAULT_THREAD_ENABLE] = {
		.name		= "default_thread_id_enable",
		.offset		= AM65_CPSW_ALE_THREAD_DEF_REG,
		.port_offset	= 0,
		.shift		= 15,
		.port_shift	= 0,
		.bits		= 1,
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

	if (port < 0 || port >= ale->params.ale_ports)
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

	if (port < 0 || port >= ale->params.ale_ports)
		return -EINVAL;

	offset = info->offset + (port * info->port_offset);
	shift  = info->shift  + (port * info->port_shift);

	tmp = readl_relaxed(ale->params.ale_regs + offset) >> shift;
	return tmp & BITMASK(info->bits);
}

static void cpsw_ale_timer(struct timer_list *t)
{
	struct cpsw_ale *ale = from_timer(ale, t, timer);

	cpsw_ale_control_set(ale, 0, ALE_AGEOUT, 1);

	if (ale->ageout) {
		ale->timer.expires = jiffies + ale->ageout;
		add_timer(&ale->timer);
	}
}

static void cpsw_ale_hw_aging_timer_start(struct cpsw_ale *ale)
{
	u32 aging_timer;

	aging_timer = ale->params.bus_freq / 1000000;
	aging_timer *= ale->params.ale_ageout;

	if (aging_timer & ~ALE_AGING_TIMER_MASK) {
		aging_timer = ALE_AGING_TIMER_MASK;
		dev_warn(ale->params.dev,
			 "ALE aging timer overflow, set to max\n");
	}

	writel(aging_timer, ale->params.ale_regs + ALE_AGING_TIMER);
}

static void cpsw_ale_hw_aging_timer_stop(struct cpsw_ale *ale)
{
	writel(0, ale->params.ale_regs + ALE_AGING_TIMER);
}

static void cpsw_ale_aging_start(struct cpsw_ale *ale)
{
	if (!ale->params.ale_ageout)
		return;

	if (ale->features & CPSW_ALE_F_HW_AUTOAGING) {
		cpsw_ale_hw_aging_timer_start(ale);
		return;
	}

	timer_setup(&ale->timer, cpsw_ale_timer, 0);
	ale->timer.expires = jiffies + ale->ageout;
	add_timer(&ale->timer);
}

static void cpsw_ale_aging_stop(struct cpsw_ale *ale)
{
	if (!ale->params.ale_ageout)
		return;

	if (ale->features & CPSW_ALE_F_HW_AUTOAGING) {
		cpsw_ale_hw_aging_timer_stop(ale);
		return;
	}

	del_timer_sync(&ale->timer);
}

void cpsw_ale_start(struct cpsw_ale *ale)
{
	cpsw_ale_control_set(ale, 0, ALE_ENABLE, 1);
	cpsw_ale_control_set(ale, 0, ALE_CLEAR, 1);

	cpsw_ale_aging_start(ale);
}

void cpsw_ale_stop(struct cpsw_ale *ale)
{
	cpsw_ale_aging_stop(ale);
	cpsw_ale_control_set(ale, 0, ALE_CLEAR, 1);
	cpsw_ale_control_set(ale, 0, ALE_ENABLE, 0);
}

static const struct cpsw_ale_dev_id cpsw_ale_id_match[] = {
	{
		/* am3/4/5, dra7. dm814x, 66ak2hk-gbe */
		.dev_id = "cpsw",
		.tbl_entries = 1024,
		.major_ver_mask = 0xff,
		.vlan_entry_tbl = vlan_entry_cpsw,
	},
	{
		/* 66ak2h_xgbe */
		.dev_id = "66ak2h-xgbe",
		.tbl_entries = 2048,
		.major_ver_mask = 0xff,
		.vlan_entry_tbl = vlan_entry_cpsw,
	},
	{
		.dev_id = "66ak2el",
		.features = CPSW_ALE_F_STATUS_REG,
		.major_ver_mask = 0x7,
		.nu_switch_ale = true,
		.vlan_entry_tbl = vlan_entry_nu,
	},
	{
		.dev_id = "66ak2g",
		.features = CPSW_ALE_F_STATUS_REG,
		.tbl_entries = 64,
		.major_ver_mask = 0x7,
		.nu_switch_ale = true,
		.vlan_entry_tbl = vlan_entry_nu,
	},
	{
		.dev_id = "am65x-cpsw2g",
		.features = CPSW_ALE_F_STATUS_REG | CPSW_ALE_F_HW_AUTOAGING,
		.tbl_entries = 64,
		.major_ver_mask = 0x7,
		.nu_switch_ale = true,
		.vlan_entry_tbl = vlan_entry_nu,
	},
	{
		.dev_id = "j721e-cpswxg",
		.features = CPSW_ALE_F_STATUS_REG | CPSW_ALE_F_HW_AUTOAGING,
		.major_ver_mask = 0x7,
		.vlan_entry_tbl = vlan_entry_k3_cpswxg,
	},
	{ },
};

static const struct
cpsw_ale_dev_id *cpsw_ale_match_id(const struct cpsw_ale_dev_id *id,
				   const char *dev_id)
{
	if (!dev_id)
		return NULL;

	while (id->dev_id) {
		if (strcmp(dev_id, id->dev_id) == 0)
			return id;
		id++;
	}
	return NULL;
}

struct cpsw_ale *cpsw_ale_create(struct cpsw_ale_params *params)
{
	const struct cpsw_ale_dev_id *ale_dev_id;
	struct cpsw_ale *ale;
	u32 rev, ale_entries;

	ale_dev_id = cpsw_ale_match_id(cpsw_ale_id_match, params->dev_id);
	if (!ale_dev_id)
		return ERR_PTR(-EINVAL);

	params->ale_entries = ale_dev_id->tbl_entries;
	params->major_ver_mask = ale_dev_id->major_ver_mask;
	params->nu_switch_ale = ale_dev_id->nu_switch_ale;

	ale = devm_kzalloc(params->dev, sizeof(*ale), GFP_KERNEL);
	if (!ale)
		return ERR_PTR(-ENOMEM);

	ale->p0_untag_vid_mask =
		devm_kmalloc_array(params->dev, BITS_TO_LONGS(VLAN_N_VID),
				   sizeof(unsigned long),
				   GFP_KERNEL);
	if (!ale->p0_untag_vid_mask)
		return ERR_PTR(-ENOMEM);

	ale->params = *params;
	ale->ageout = ale->params.ale_ageout * HZ;
	ale->features = ale_dev_id->features;
	ale->vlan_entry_tbl = ale_dev_id->vlan_entry_tbl;

	rev = readl_relaxed(ale->params.ale_regs + ALE_IDVER);
	ale->version =
		(ALE_VERSION_MAJOR(rev, ale->params.major_ver_mask) << 8) |
		 ALE_VERSION_MINOR(rev);
	dev_info(ale->params.dev, "initialized cpsw ale version %d.%d\n",
		 ALE_VERSION_MAJOR(rev, ale->params.major_ver_mask),
		 ALE_VERSION_MINOR(rev));

	if (ale->features & CPSW_ALE_F_STATUS_REG &&
	    !ale->params.ale_entries) {
		ale_entries =
			readl_relaxed(ale->params.ale_regs + ALE_STATUS) &
			ALE_STATUS_SIZE_MASK;
		/* ALE available on newer NetCP switches has introduced
		 * a register, ALE_STATUS, to indicate the size of ALE
		 * table which shows the size as a multiple of 1024 entries.
		 * For these, params.ale_entries will be set to zero. So
		 * read the register and update the value of ale_entries.
		 * return error if ale_entries is zero in ALE_STATUS.
		 */
		if (!ale_entries)
			return ERR_PTR(-EINVAL);

		ale_entries *= ALE_TABLE_SIZE_MULTIPLIER;
		ale->params.ale_entries = ale_entries;
	}
	dev_info(ale->params.dev,
		 "ALE Table size %ld\n", ale->params.ale_entries);

	/* set default bits for existing h/w */
	ale->port_mask_bits = ale->params.ale_ports;
	ale->port_num_bits = order_base_2(ale->params.ale_ports);
	ale->vlan_field_bits = ale->params.ale_ports;

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
	}

	cpsw_ale_control_set(ale, 0, ALE_CLEAR, 1);
	return ale;
}

void cpsw_ale_dump(struct cpsw_ale *ale, u32 *data)
{
	int i;

	for (i = 0; i < ale->params.ale_entries; i++) {
		cpsw_ale_read(ale, i, data);
		data += ALE_ENTRY_WORDS;
	}
}

u32 cpsw_ale_get_num_entries(struct cpsw_ale *ale)
{
	return ale ? ale->params.ale_entries : 0;
}
