/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright (c) 2016-2018, NXP Semiconductors
 * Copyright (c) 2018-2019, Vladimir Oltean <olteanv@gmail.com>
 */
#ifndef _SJA1105_STATIC_CONFIG_H
#define _SJA1105_STATIC_CONFIG_H

#include <linux/packing.h>
#include <linux/types.h>
#include <asm/types.h>

#define SJA1105_NUM_PORTS				5
#define SJA1110_NUM_PORTS				11
#define SJA1105_MAX_NUM_PORTS				SJA1110_NUM_PORTS
#define SJA1105_NUM_TC					8

#define SJA1105_SIZE_SPI_MSG_HEADER			4
#define SJA1105_SIZE_SPI_MSG_MAXLEN			(64 * 4)
#define SJA1105_SIZE_DEVICE_ID				4
#define SJA1105_SIZE_TABLE_HEADER			12
#define SJA1105_SIZE_SCHEDULE_ENTRY			8
#define SJA1110_SIZE_SCHEDULE_ENTRY			12
#define SJA1105_SIZE_SCHEDULE_ENTRY_POINTS_ENTRY	4
#define SJA1110_SIZE_SCHEDULE_ENTRY_POINTS_ENTRY	8
#define SJA1105_SIZE_VL_LOOKUP_ENTRY			12
#define SJA1105_SIZE_VL_POLICING_ENTRY			8
#define SJA1105_SIZE_VL_FORWARDING_ENTRY		4
#define SJA1105_SIZE_L2_POLICING_ENTRY			8
#define SJA1105_SIZE_VLAN_LOOKUP_ENTRY			8
#define SJA1110_SIZE_VLAN_LOOKUP_ENTRY			12
#define SJA1105_SIZE_L2_FORWARDING_ENTRY		8
#define SJA1105_SIZE_L2_FORWARDING_PARAMS_ENTRY		12
#define SJA1105_SIZE_RETAGGING_ENTRY			8
#define SJA1105_SIZE_XMII_PARAMS_ENTRY			4
#define SJA1110_SIZE_XMII_PARAMS_ENTRY			8
#define SJA1105_SIZE_SCHEDULE_PARAMS_ENTRY		12
#define SJA1105_SIZE_SCHEDULE_ENTRY_POINTS_PARAMS_ENTRY	4
#define SJA1105_SIZE_VL_FORWARDING_PARAMS_ENTRY         12
#define SJA1105ET_SIZE_L2_LOOKUP_ENTRY			12
#define SJA1105ET_SIZE_MAC_CONFIG_ENTRY			28
#define SJA1105ET_SIZE_L2_LOOKUP_PARAMS_ENTRY		4
#define SJA1105ET_SIZE_GENERAL_PARAMS_ENTRY		40
#define SJA1105ET_SIZE_AVB_PARAMS_ENTRY			12
#define SJA1105ET_SIZE_CBS_ENTRY			16
#define SJA1105PQRS_SIZE_L2_LOOKUP_ENTRY		20
#define SJA1110_SIZE_L2_LOOKUP_ENTRY			24
#define SJA1105PQRS_SIZE_MAC_CONFIG_ENTRY		32
#define SJA1105PQRS_SIZE_L2_LOOKUP_PARAMS_ENTRY		16
#define SJA1110_SIZE_L2_LOOKUP_PARAMS_ENTRY		28
#define SJA1105PQRS_SIZE_GENERAL_PARAMS_ENTRY		44
#define SJA1110_SIZE_GENERAL_PARAMS_ENTRY		56
#define SJA1105PQRS_SIZE_AVB_PARAMS_ENTRY		16
#define SJA1105PQRS_SIZE_CBS_ENTRY			20
#define SJA1110_SIZE_PCP_REMAPPING_ENTRY		4

/* UM10944.pdf Page 11, Table 2. Configuration Blocks */
enum {
	BLKID_SCHEDULE					= 0x00,
	BLKID_SCHEDULE_ENTRY_POINTS			= 0x01,
	BLKID_VL_LOOKUP					= 0x02,
	BLKID_VL_POLICING				= 0x03,
	BLKID_VL_FORWARDING				= 0x04,
	BLKID_L2_LOOKUP					= 0x05,
	BLKID_L2_POLICING				= 0x06,
	BLKID_VLAN_LOOKUP				= 0x07,
	BLKID_L2_FORWARDING				= 0x08,
	BLKID_MAC_CONFIG				= 0x09,
	BLKID_SCHEDULE_PARAMS				= 0x0A,
	BLKID_SCHEDULE_ENTRY_POINTS_PARAMS		= 0x0B,
	BLKID_VL_FORWARDING_PARAMS			= 0x0C,
	BLKID_L2_LOOKUP_PARAMS				= 0x0D,
	BLKID_L2_FORWARDING_PARAMS			= 0x0E,
	BLKID_AVB_PARAMS				= 0x10,
	BLKID_GENERAL_PARAMS				= 0x11,
	BLKID_RETAGGING					= 0x12,
	BLKID_CBS					= 0x13,
	BLKID_PCP_REMAPPING				= 0x1C,
	BLKID_XMII_PARAMS				= 0x4E,
};

enum sja1105_blk_idx {
	BLK_IDX_SCHEDULE = 0,
	BLK_IDX_SCHEDULE_ENTRY_POINTS,
	BLK_IDX_VL_LOOKUP,
	BLK_IDX_VL_POLICING,
	BLK_IDX_VL_FORWARDING,
	BLK_IDX_L2_LOOKUP,
	BLK_IDX_L2_POLICING,
	BLK_IDX_VLAN_LOOKUP,
	BLK_IDX_L2_FORWARDING,
	BLK_IDX_MAC_CONFIG,
	BLK_IDX_SCHEDULE_PARAMS,
	BLK_IDX_SCHEDULE_ENTRY_POINTS_PARAMS,
	BLK_IDX_VL_FORWARDING_PARAMS,
	BLK_IDX_L2_LOOKUP_PARAMS,
	BLK_IDX_L2_FORWARDING_PARAMS,
	BLK_IDX_AVB_PARAMS,
	BLK_IDX_GENERAL_PARAMS,
	BLK_IDX_RETAGGING,
	BLK_IDX_CBS,
	BLK_IDX_XMII_PARAMS,
	BLK_IDX_PCP_REMAPPING,
	BLK_IDX_MAX,
	/* Fake block indices that are only valid for dynamic access */
	BLK_IDX_MGMT_ROUTE,
	BLK_IDX_MAX_DYN,
	BLK_IDX_INVAL = -1,
};

#define SJA1105_MAX_SCHEDULE_COUNT			1024
#define SJA1110_MAX_SCHEDULE_COUNT			4096
#define SJA1105_MAX_SCHEDULE_ENTRY_POINTS_COUNT		2048
#define SJA1105_MAX_VL_LOOKUP_COUNT			1024
#define SJA1110_MAX_VL_LOOKUP_COUNT			4096
#define SJA1105_MAX_VL_POLICING_COUNT			1024
#define SJA1110_MAX_VL_POLICING_COUNT			4096
#define SJA1105_MAX_VL_FORWARDING_COUNT			1024
#define SJA1110_MAX_VL_FORWARDING_COUNT			4096
#define SJA1105_MAX_L2_LOOKUP_COUNT			1024
#define SJA1105_MAX_L2_POLICING_COUNT			45
#define SJA1110_MAX_L2_POLICING_COUNT			110
#define SJA1105_MAX_VLAN_LOOKUP_COUNT			4096
#define SJA1105_MAX_L2_FORWARDING_COUNT			13
#define SJA1110_MAX_L2_FORWARDING_COUNT			19
#define SJA1105_MAX_MAC_CONFIG_COUNT			5
#define SJA1110_MAX_MAC_CONFIG_COUNT			11
#define SJA1105_MAX_SCHEDULE_PARAMS_COUNT		1
#define SJA1105_MAX_SCHEDULE_ENTRY_POINTS_PARAMS_COUNT	1
#define SJA1105_MAX_VL_FORWARDING_PARAMS_COUNT		1
#define SJA1105_MAX_L2_LOOKUP_PARAMS_COUNT		1
#define SJA1105_MAX_L2_FORWARDING_PARAMS_COUNT		1
#define SJA1105_MAX_GENERAL_PARAMS_COUNT		1
#define SJA1105_MAX_RETAGGING_COUNT			32
#define SJA1105_MAX_XMII_PARAMS_COUNT			1
#define SJA1105_MAX_AVB_PARAMS_COUNT			1
#define SJA1105ET_MAX_CBS_COUNT				10
#define SJA1105PQRS_MAX_CBS_COUNT			16
#define SJA1110_MAX_CBS_COUNT				80
#define SJA1110_MAX_PCP_REMAPPING_COUNT			11

#define SJA1105_MAX_FRAME_MEMORY			929
#define SJA1110_MAX_FRAME_MEMORY			1820
#define SJA1105_FRAME_MEMORY_RETAGGING_OVERHEAD		19
#define SJA1105_VL_FRAME_MEMORY				100

#define SJA1105E_DEVICE_ID				0x9C00000Cull
#define SJA1105T_DEVICE_ID				0x9E00030Eull
#define SJA1105PR_DEVICE_ID				0xAF00030Eull
#define SJA1105QS_DEVICE_ID				0xAE00030Eull
#define SJA1110_DEVICE_ID				0xB700030Full

#define SJA1105ET_PART_NO				0x9A83
#define SJA1105P_PART_NO				0x9A84
#define SJA1105Q_PART_NO				0x9A85
#define SJA1105R_PART_NO				0x9A86
#define SJA1105S_PART_NO				0x9A87
#define SJA1110A_PART_NO				0x1110
#define SJA1110B_PART_NO				0x1111
#define SJA1110C_PART_NO				0x1112
#define SJA1110D_PART_NO				0x1113

#define SJA1110_ACU			0x1c4400
#define SJA1110_RGU			0x1c6000
#define SJA1110_CGU			0x1c6400

#define SJA1110_SPI_ADDR(x)		((x) / 4)
#define SJA1110_ACU_ADDR(x)		(SJA1110_ACU + SJA1110_SPI_ADDR(x))
#define SJA1110_CGU_ADDR(x)		(SJA1110_CGU + SJA1110_SPI_ADDR(x))
#define SJA1110_RGU_ADDR(x)		(SJA1110_RGU + SJA1110_SPI_ADDR(x))

#define SJA1105_RSV_ADDR		0xffffffffffffffffull

struct sja1105_schedule_entry {
	u64 winstindex;
	u64 winend;
	u64 winst;
	u64 destports;
	u64 setvalid;
	u64 txen;
	u64 resmedia_en;
	u64 resmedia;
	u64 vlindex;
	u64 delta;
};

struct sja1105_schedule_params_entry {
	u64 subscheind[8];
};

struct sja1105_general_params_entry {
	u64 vllupformat;
	u64 mirr_ptacu;
	u64 switchid;
	u64 hostprio;
	u64 mac_fltres1;
	u64 mac_fltres0;
	u64 mac_flt1;
	u64 mac_flt0;
	u64 incl_srcpt1;
	u64 incl_srcpt0;
	u64 send_meta1;
	u64 send_meta0;
	u64 casc_port;
	u64 host_port;
	u64 mirr_port;
	u64 vlmarker;
	u64 vlmask;
	u64 tpid;
	u64 ignore2stf;
	u64 tpid2;
	/* P/Q/R/S only */
	u64 queue_ts;
	u64 egrmirrvid;
	u64 egrmirrpcp;
	u64 egrmirrdei;
	u64 replay_port;
	/* SJA1110 only */
	u64 tte_en;
	u64 tdmaconfigidx;
	u64 header_type;
};

struct sja1105_schedule_entry_points_entry {
	u64 subschindx;
	u64 delta;
	u64 address;
};

struct sja1105_schedule_entry_points_params_entry {
	u64 clksrc;
	u64 actsubsch;
};

struct sja1105_vlan_lookup_entry {
	u64 ving_mirr;
	u64 vegr_mirr;
	u64 vmemb_port;
	u64 vlan_bc;
	u64 tag_port;
	u64 vlanid;
	u64 type_entry; /* SJA1110 only */
};

struct sja1105_l2_lookup_entry {
	u64 vlanid;
	u64 macaddr;
	u64 destports;
	u64 enfport;
	u64 index;
	/* P/Q/R/S only */
	u64 mask_iotag;
	u64 mask_vlanid;
	u64 mask_macaddr;
	u64 mask_srcport;
	u64 iotag;
	u64 srcport;
	u64 lockeds;
	union {
		/* LOCKEDS=1: Static FDB entries */
		struct {
			/* TSREG is deprecated in SJA1110, TRAP is supported only
			 * in SJA1110.
			 */
			u64 trap;
			u64 tsreg;
			u64 mirrvlan;
			u64 takets;
			u64 mirr;
			u64 retag;
		};
		/* LOCKEDS=0: Dynamically learned FDB entries */
		struct {
			u64 touched;
			u64 age;
		};
	};
};

struct sja1105_l2_lookup_params_entry {
	u64 maxaddrp[SJA1105_MAX_NUM_PORTS]; /* P/Q/R/S only */
	u64 start_dynspc;    /* P/Q/R/S only */
	u64 drpnolearn;      /* P/Q/R/S only */
	u64 use_static;      /* P/Q/R/S only */
	u64 owr_dyn;         /* P/Q/R/S only */
	u64 learn_once;      /* P/Q/R/S only */
	u64 maxage;          /* Shared */
	u64 dyn_tbsz;        /* E/T only */
	u64 poly;            /* E/T only */
	u64 shared_learn;    /* Shared */
	u64 no_enf_hostprt;  /* Shared */
	u64 no_mgmt_learn;   /* Shared */
};

struct sja1105_l2_forwarding_entry {
	u64 bc_domain;
	u64 reach_port;
	u64 fl_domain;
	/* This is actually max(SJA1105_NUM_TC, SJA1105_MAX_NUM_PORTS) */
	u64 vlan_pmap[SJA1105_MAX_NUM_PORTS];
	bool type_egrpcp2outputq;
};

struct sja1105_l2_forwarding_params_entry {
	u64 max_dynp;
	u64 part_spc[8];
};

struct sja1105_l2_policing_entry {
	u64 sharindx;
	u64 smax;
	u64 rate;
	u64 maxlen;
	u64 partition;
};

struct sja1105_avb_params_entry {
	u64 cas_master;
	u64 destmeta;
	u64 srcmeta;
};

struct sja1105_mac_config_entry {
	u64 top[8];
	u64 base[8];
	u64 enabled[8];
	u64 ifg;
	u64 speed;
	u64 tp_delin;
	u64 tp_delout;
	u64 maxage;
	u64 vlanprio;
	u64 vlanid;
	u64 ing_mirr;
	u64 egr_mirr;
	u64 drpnona664;
	u64 drpdtag;
	u64 drpuntag;
	u64 retag;
	u64 dyn_learn;
	u64 egress;
	u64 ingress;
};

struct sja1105_retagging_entry {
	u64 egr_port;
	u64 ing_port;
	u64 vlan_ing;
	u64 vlan_egr;
	u64 do_not_learn;
	u64 use_dest_ports;
	u64 destports;
};

struct sja1105_cbs_entry {
	u64 port; /* Not used for SJA1110 */
	u64 prio; /* Not used for SJA1110 */
	u64 credit_hi;
	u64 credit_lo;
	u64 send_slope;
	u64 idle_slope;
};

struct sja1105_xmii_params_entry {
	u64 phy_mac[SJA1105_MAX_NUM_PORTS];
	u64 xmii_mode[SJA1105_MAX_NUM_PORTS];
	/* The SJA1110 insists being a snowflake, and requires SGMII,
	 * 2500base-x and internal MII ports connected to the 100base-TX PHY to
	 * set this bit. We set it unconditionally from the high-level logic,
	 * and only sja1110_xmii_params_entry_packing writes it to the static
	 * config. I have no better name for it than "special".
	 */
	u64 special[SJA1105_MAX_NUM_PORTS];
};

struct sja1110_pcp_remapping_entry {
	u64 egrpcp[SJA1105_NUM_TC];
};

enum {
	SJA1105_VL_FORMAT_PSFP		= 0,
	SJA1105_VL_FORMAT_ARINC664	= 1,
};

struct sja1105_vl_lookup_entry {
	u64 format;
	u64 port;
	union {
		/* SJA1105_VL_FORMAT_PSFP */
		struct {
			u64 destports;
			u64 iscritical;
			u64 macaddr;
			u64 vlanid;
			u64 vlanprior;
		};
		/* SJA1105_VL_FORMAT_ARINC664 */
		struct {
			u64 egrmirr;
			u64 ingrmirr;
			u64 vlid;
		};
	};
	/* Not part of hardware structure */
	unsigned long flow_cookie;
};

struct sja1105_vl_policing_entry {
	u64 type;
	u64 maxlen;
	u64 sharindx;
	u64 bag;
	u64 jitter;
};

struct sja1105_vl_forwarding_entry {
	u64 type;
	u64 priority;
	u64 partition;
	u64 destports;
};

struct sja1105_vl_forwarding_params_entry {
	u64 partspc[8];
	u64 debugen;
};

struct sja1105_table_header {
	u64 block_id;
	u64 len;
	u64 crc;
};

struct sja1105_table_ops {
	size_t (*packing)(void *buf, void *entry_ptr, enum packing_op op);
	size_t unpacked_entry_size;
	size_t packed_entry_size;
	size_t max_entry_count;
};

struct sja1105_table {
	const struct sja1105_table_ops *ops;
	size_t entry_count;
	void *entries;
};

struct sja1105_static_config {
	u64 device_id;
	struct sja1105_table tables[BLK_IDX_MAX];
};

extern const struct sja1105_table_ops sja1105e_table_ops[BLK_IDX_MAX];
extern const struct sja1105_table_ops sja1105t_table_ops[BLK_IDX_MAX];
extern const struct sja1105_table_ops sja1105p_table_ops[BLK_IDX_MAX];
extern const struct sja1105_table_ops sja1105q_table_ops[BLK_IDX_MAX];
extern const struct sja1105_table_ops sja1105r_table_ops[BLK_IDX_MAX];
extern const struct sja1105_table_ops sja1105s_table_ops[BLK_IDX_MAX];
extern const struct sja1105_table_ops sja1110_table_ops[BLK_IDX_MAX];

size_t sja1105_table_header_packing(void *buf, void *hdr, enum packing_op op);
void
sja1105_table_header_pack_with_crc(void *buf, struct sja1105_table_header *hdr);
size_t
sja1105_static_config_get_length(const struct sja1105_static_config *config);

typedef enum {
	SJA1105_CONFIG_OK = 0,
	SJA1105_TTETHERNET_NOT_SUPPORTED,
	SJA1105_INCORRECT_TTETHERNET_CONFIGURATION,
	SJA1105_INCORRECT_VIRTUAL_LINK_CONFIGURATION,
	SJA1105_MISSING_L2_POLICING_TABLE,
	SJA1105_MISSING_L2_FORWARDING_TABLE,
	SJA1105_MISSING_L2_FORWARDING_PARAMS_TABLE,
	SJA1105_MISSING_GENERAL_PARAMS_TABLE,
	SJA1105_MISSING_VLAN_TABLE,
	SJA1105_MISSING_XMII_TABLE,
	SJA1105_MISSING_MAC_TABLE,
	SJA1105_OVERCOMMITTED_FRAME_MEMORY,
} sja1105_config_valid_t;

extern const char *sja1105_static_config_error_msg[];

sja1105_config_valid_t
sja1105_static_config_check_valid(const struct sja1105_static_config *config,
				  int max_mem);
void
sja1105_static_config_pack(void *buf, struct sja1105_static_config *config);
int sja1105_static_config_init(struct sja1105_static_config *config,
			       const struct sja1105_table_ops *static_ops,
			       u64 device_id);
void sja1105_static_config_free(struct sja1105_static_config *config);

int sja1105_table_delete_entry(struct sja1105_table *table, int i);
int sja1105_table_resize(struct sja1105_table *table, size_t new_count);

u32 sja1105_crc32(const void *buf, size_t len);

void sja1105_pack(void *buf, const u64 *val, int start, int end, size_t len);
void sja1105_unpack(const void *buf, u64 *val, int start, int end, size_t len);
void sja1105_packing(void *buf, u64 *val, int start, int end,
		     size_t len, enum packing_op op);

/* Common implementations for the static and dynamic configs */
size_t sja1105pqrs_general_params_entry_packing(void *buf, void *entry_ptr,
						enum packing_op op);
size_t sja1110_general_params_entry_packing(void *buf, void *entry_ptr,
					    enum packing_op op);
size_t sja1105pqrs_l2_lookup_params_entry_packing(void *buf, void *entry_ptr,
						  enum packing_op op);
size_t sja1110_l2_lookup_params_entry_packing(void *buf, void *entry_ptr,
					      enum packing_op op);
size_t sja1105_l2_forwarding_entry_packing(void *buf, void *entry_ptr,
					   enum packing_op op);
size_t sja1110_l2_forwarding_entry_packing(void *buf, void *entry_ptr,
					   enum packing_op op);
size_t sja1105pqrs_l2_lookup_entry_packing(void *buf, void *entry_ptr,
					   enum packing_op op);
size_t sja1105et_l2_lookup_entry_packing(void *buf, void *entry_ptr,
					 enum packing_op op);
size_t sja1110_l2_lookup_entry_packing(void *buf, void *entry_ptr,
				       enum packing_op op);
size_t sja1105_vlan_lookup_entry_packing(void *buf, void *entry_ptr,
					 enum packing_op op);
size_t sja1110_vlan_lookup_entry_packing(void *buf, void *entry_ptr,
					 enum packing_op op);
size_t sja1105_retagging_entry_packing(void *buf, void *entry_ptr,
				       enum packing_op op);
size_t sja1110_retagging_entry_packing(void *buf, void *entry_ptr,
				       enum packing_op op);
size_t sja1105pqrs_mac_config_entry_packing(void *buf, void *entry_ptr,
					    enum packing_op op);
size_t sja1110_mac_config_entry_packing(void *buf, void *entry_ptr,
					enum packing_op op);
size_t sja1105pqrs_avb_params_entry_packing(void *buf, void *entry_ptr,
					    enum packing_op op);
size_t sja1105_vl_lookup_entry_packing(void *buf, void *entry_ptr,
				       enum packing_op op);
size_t sja1110_vl_lookup_entry_packing(void *buf, void *entry_ptr,
				       enum packing_op op);
size_t sja1110_vl_policing_entry_packing(void *buf, void *entry_ptr,
					 enum packing_op op);
size_t sja1110_xmii_params_entry_packing(void *buf, void *entry_ptr,
					 enum packing_op op);
size_t sja1110_l2_policing_entry_packing(void *buf, void *entry_ptr,
					 enum packing_op op);
size_t sja1110_l2_forwarding_params_entry_packing(void *buf, void *entry_ptr,
						  enum packing_op op);

#endif
