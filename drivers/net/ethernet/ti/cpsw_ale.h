/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Texas Instruments N-Port Ethernet Switch Address Lookup Engine APIs
 *
 * Copyright (C) 2012 Texas Instruments
 *
 */
#ifndef __TI_CPSW_ALE_H__
#define __TI_CPSW_ALE_H__

struct reg_fields;

struct cpsw_ale_params {
	struct device		*dev;
	void __iomem		*ale_regs;
	unsigned long		ale_ageout;	/* in secs */
	unsigned long		ale_entries;
	unsigned long		num_policers;
	unsigned long		ale_ports;
	/* NU Switch has specific handling as number of bits in ALE entries
	 * are different than other versions of ALE. Also there are specific
	 * registers for unknown vlan specific fields. So use nu_switch_ale
	 * to identify this hardware.
	 */
	bool			nu_switch_ale;
	const struct reg_field *reg_fields;
	int			num_fields;
	const char		*dev_id;
	unsigned long		bus_freq;
};

struct ale_entry_fld;
struct regmap;

enum ale_fields {
	MINOR_VER,
	MAJOR_VER,
	ALE_ENTRIES,
	ALE_POLICERS,
	POL_PORT_MEN,
	POL_TRUNK_ID,
	POL_PORT_NUM,
	POL_PRI_MEN,
	POL_PRI_VAL,
	POL_OUI_MEN,
	POL_OUI_INDEX,
	POL_DST_MEN,
	POL_DST_INDEX,
	POL_SRC_MEN,
	POL_SRC_INDEX,
	POL_OVLAN_MEN,
	POL_OVLAN_INDEX,
	POL_IVLAN_MEN,
	POL_IVLAN_INDEX,
	POL_ETHERTYPE_MEN,
	POL_ETHERTYPE_INDEX,
	POL_IPSRC_MEN,
	POL_IPSRC_INDEX,
	POL_IPDST_MEN,
	POL_IPDST_INDEX,
	POL_EN,
	POL_RED_DROP_EN,
	POL_YELLOW_DROP_EN,
	POL_YELLOW_THRESH,
	POL_POL_MATCH_MODE,
	POL_PRIORITY_THREAD_EN,
	POL_MAC_ONLY_DEF_DIS,
	POL_TEST_CLR,
	POL_TEST_CLR_RED,
	POL_TEST_CLR_YELLOW,
	POL_TEST_CLR_SELECTED,
	POL_TEST_ENTRY,
	POL_STATUS_HIT,
	POL_STATUS_HIT_RED,
	POL_STATUS_HIT_YELLOW,
	ALE_DEFAULT_THREAD_EN,
	ALE_DEFAULT_THREAD_VAL,
	ALE_THREAD_CLASS_INDEX,
	ALE_THREAD_ENABLE,
	ALE_THREAD_VALUE,
	/* terminator */
	ALE_FIELDS_MAX,
};

struct cpsw_ale {
	struct cpsw_ale_params	params;
	struct timer_list	timer;
	struct regmap		*regmap;
	struct regmap_field	*fields[ALE_FIELDS_MAX];
	unsigned long		ageout;
	u32			version;
	u32			features;
	/* These bits are different on NetCP NU Switch ALE */
	u32			port_mask_bits;
	u32			port_num_bits;
	u32			vlan_field_bits;
	unsigned long		*p0_untag_vid_mask;
	const struct ale_entry_fld *vlan_entry_tbl;
};

enum cpsw_ale_control {
	/* global */
	ALE_ENABLE,
	ALE_CLEAR,
	ALE_AGEOUT,
	ALE_P0_UNI_FLOOD,
	ALE_VLAN_NOLEARN,
	ALE_NO_PORT_VLAN,
	ALE_OUI_DENY,
	ALE_BYPASS,
	ALE_RATE_LIMIT_TX,
	ALE_VLAN_AWARE,
	ALE_AUTH_ENABLE,
	ALE_RATE_LIMIT,
	/* port controls */
	ALE_PORT_STATE,
	ALE_PORT_DROP_UNTAGGED,
	ALE_PORT_DROP_UNKNOWN_VLAN,
	ALE_PORT_NOLEARN,
	ALE_PORT_NO_SA_UPDATE,
	ALE_PORT_UNKNOWN_VLAN_MEMBER,
	ALE_PORT_UNKNOWN_MCAST_FLOOD,
	ALE_PORT_UNKNOWN_REG_MCAST_FLOOD,
	ALE_PORT_UNTAGGED_EGRESS,
	ALE_PORT_MACONLY,
	ALE_PORT_MACONLY_CAF,
	ALE_PORT_BCAST_LIMIT,
	ALE_PORT_MCAST_LIMIT,
	ALE_DEFAULT_THREAD_ID,
	ALE_DEFAULT_THREAD_ENABLE,
	ALE_NUM_CONTROLS,
};

enum cpsw_ale_port_state {
	ALE_PORT_STATE_DISABLE	= 0x00,
	ALE_PORT_STATE_BLOCK	= 0x01,
	ALE_PORT_STATE_LEARN	= 0x02,
	ALE_PORT_STATE_FORWARD	= 0x03,
};

/* ALE unicast entry flags - passed into cpsw_ale_add_ucast() */
#define ALE_SECURE			BIT(0)
#define ALE_BLOCKED			BIT(1)
#define ALE_SUPER			BIT(2)
#define ALE_VLAN			BIT(3)

#define ALE_PORT_HOST			BIT(0)
#define ALE_PORT_1			BIT(1)
#define ALE_PORT_2			BIT(2)

#define ALE_MCAST_FWD			0
#define ALE_MCAST_BLOCK_LEARN_FWD	1
#define ALE_MCAST_FWD_LEARN		2
#define ALE_MCAST_FWD_2			3

#define ALE_ENTRY_BITS		68
#define ALE_ENTRY_WORDS	DIV_ROUND_UP(ALE_ENTRY_BITS, 32)

struct cpsw_ale *cpsw_ale_create(struct cpsw_ale_params *params);

void cpsw_ale_start(struct cpsw_ale *ale);
void cpsw_ale_stop(struct cpsw_ale *ale);

int cpsw_ale_flush_multicast(struct cpsw_ale *ale, int port_mask, int vid);
int cpsw_ale_add_ucast(struct cpsw_ale *ale, const u8 *addr, int port,
		       int flags, u16 vid);
int cpsw_ale_del_ucast(struct cpsw_ale *ale, const u8 *addr, int port,
		       int flags, u16 vid);
int cpsw_ale_add_mcast(struct cpsw_ale *ale, const u8 *addr, int port_mask,
		       int flags, u16 vid, int mcast_state);
int cpsw_ale_del_mcast(struct cpsw_ale *ale, const u8 *addr, int port_mask,
		       int flags, u16 vid);
int cpsw_ale_add_vlan(struct cpsw_ale *ale, u16 vid, int port, int untag,
			int reg_mcast, int unreg_mcast);
int cpsw_ale_del_vlan(struct cpsw_ale *ale, u16 vid, int port);
void cpsw_ale_set_allmulti(struct cpsw_ale *ale, int allmulti, int port);
int cpsw_ale_rx_ratelimit_bc(struct cpsw_ale *ale, int port, unsigned int ratelimit_pps);
int cpsw_ale_rx_ratelimit_mc(struct cpsw_ale *ale, int port, unsigned int ratelimit_pps);

int cpsw_ale_control_get(struct cpsw_ale *ale, int port, int control);
int cpsw_ale_control_set(struct cpsw_ale *ale, int port,
			 int control, int value);
void cpsw_ale_dump(struct cpsw_ale *ale, u32 *data);
void cpsw_ale_restore(struct cpsw_ale *ale, u32 *data);
u32 cpsw_ale_get_num_entries(struct cpsw_ale *ale);

static inline int cpsw_ale_get_vlan_p0_untag(struct cpsw_ale *ale, u16 vid)
{
	return test_bit(vid, ale->p0_untag_vid_mask);
}

int cpsw_ale_vlan_add_modify(struct cpsw_ale *ale, u16 vid, int port_mask,
			     int untag_mask, int reg_mcast, int unreg_mcast);
int cpsw_ale_vlan_del_modify(struct cpsw_ale *ale, u16 vid, int port_mask);
void cpsw_ale_set_unreg_mcast(struct cpsw_ale *ale, int unreg_mcast_mask,
			      bool add);
void cpsw_ale_classifier_setup_default(struct cpsw_ale *ale, int num_rx_ch);

#endif
