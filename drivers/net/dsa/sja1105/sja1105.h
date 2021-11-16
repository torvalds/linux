/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018, Sensor-Technik Wiedemann GmbH
 * Copyright (c) 2018-2019, Vladimir Oltean <olteanv@gmail.com>
 */
#ifndef _SJA1105_H
#define _SJA1105_H

#include <linux/ptp_clock_kernel.h>
#include <linux/timecounter.h>
#include <linux/dsa/sja1105.h>
#include <linux/dsa/8021q.h>
#include <net/dsa.h>
#include <linux/mutex.h>
#include "sja1105_static_config.h"

#define SJA1105ET_FDB_BIN_SIZE		4
/* The hardware value is in multiples of 10 ms.
 * The passed parameter is in multiples of 1 ms.
 */
#define SJA1105_AGEING_TIME_MS(ms)	((ms) / 10)
#define SJA1105_NUM_L2_POLICERS		SJA1110_MAX_L2_POLICING_COUNT

/* Calculated assuming 1Gbps, where the clock has 125 MHz (8 ns period)
 * To avoid floating point operations, we'll multiply the degrees by 10
 * to get a "phase" and get 1 decimal point precision.
 */
#define SJA1105_RGMII_DELAY_PS_TO_PHASE(ps) \
	(((ps) * 360) / 800)
#define SJA1105_RGMII_DELAY_PHASE_TO_PS(phase) \
	((800 * (phase)) / 360)
#define SJA1105_RGMII_DELAY_PHASE_TO_HW(phase) \
	(((phase) - 738) / 9)
#define SJA1105_RGMII_DELAY_PS_TO_HW(ps) \
	SJA1105_RGMII_DELAY_PHASE_TO_HW(SJA1105_RGMII_DELAY_PS_TO_PHASE(ps))

/* Valid range in degrees is a value between 73.8 and 101.7
 * in 0.9 degree increments
 */
#define SJA1105_RGMII_DELAY_MIN_PS \
	SJA1105_RGMII_DELAY_PHASE_TO_PS(738)
#define SJA1105_RGMII_DELAY_MAX_PS \
	SJA1105_RGMII_DELAY_PHASE_TO_PS(1017)

typedef enum {
	SPI_READ = 0,
	SPI_WRITE = 1,
} sja1105_spi_rw_mode_t;

#include "sja1105_tas.h"
#include "sja1105_ptp.h"

enum sja1105_stats_area {
	MAC,
	HL1,
	HL2,
	ETHER,
	__MAX_SJA1105_STATS_AREA,
};

/* Keeps the different addresses between E/T and P/Q/R/S */
struct sja1105_regs {
	u64 device_id;
	u64 prod_id;
	u64 status;
	u64 port_control;
	u64 rgu;
	u64 vl_status;
	u64 config;
	u64 rmii_pll1;
	u64 ptppinst;
	u64 ptppindur;
	u64 ptp_control;
	u64 ptpclkval;
	u64 ptpclkrate;
	u64 ptpclkcorp;
	u64 ptpsyncts;
	u64 ptpschtm;
	u64 ptpegr_ts[SJA1105_MAX_NUM_PORTS];
	u64 pad_mii_tx[SJA1105_MAX_NUM_PORTS];
	u64 pad_mii_rx[SJA1105_MAX_NUM_PORTS];
	u64 pad_mii_id[SJA1105_MAX_NUM_PORTS];
	u64 cgu_idiv[SJA1105_MAX_NUM_PORTS];
	u64 mii_tx_clk[SJA1105_MAX_NUM_PORTS];
	u64 mii_rx_clk[SJA1105_MAX_NUM_PORTS];
	u64 mii_ext_tx_clk[SJA1105_MAX_NUM_PORTS];
	u64 mii_ext_rx_clk[SJA1105_MAX_NUM_PORTS];
	u64 rgmii_tx_clk[SJA1105_MAX_NUM_PORTS];
	u64 rmii_ref_clk[SJA1105_MAX_NUM_PORTS];
	u64 rmii_ext_tx_clk[SJA1105_MAX_NUM_PORTS];
	u64 stats[__MAX_SJA1105_STATS_AREA][SJA1105_MAX_NUM_PORTS];
	u64 mdio_100base_tx;
	u64 mdio_100base_t1;
	u64 pcs_base[SJA1105_MAX_NUM_PORTS];
};

struct sja1105_mdio_private {
	struct sja1105_private *priv;
};

enum {
	SJA1105_SPEED_AUTO,
	SJA1105_SPEED_10MBPS,
	SJA1105_SPEED_100MBPS,
	SJA1105_SPEED_1000MBPS,
	SJA1105_SPEED_2500MBPS,
	SJA1105_SPEED_MAX,
};

enum sja1105_internal_phy_t {
	SJA1105_NO_PHY		= 0,
	SJA1105_PHY_BASE_TX,
	SJA1105_PHY_BASE_T1,
};

struct sja1105_info {
	u64 device_id;
	/* Needed for distinction between P and R, and between Q and S
	 * (since the parts with/without SGMII share the same
	 * switch core and device_id)
	 */
	u64 part_no;
	/* E/T and P/Q/R/S have partial timestamps of different sizes.
	 * They must be reconstructed on both families anyway to get the full
	 * 64-bit values back.
	 */
	int ptp_ts_bits;
	/* Also SPI commands are of different sizes to retrieve
	 * the egress timestamps.
	 */
	int ptpegr_ts_bytes;
	int num_cbs_shapers;
	int max_frame_mem;
	int num_ports;
	bool multiple_cascade_ports;
	enum dsa_tag_protocol tag_proto;
	const struct sja1105_dynamic_table_ops *dyn_ops;
	const struct sja1105_table_ops *static_ops;
	const struct sja1105_regs *regs;
	bool can_limit_mcast_flood;
	int (*reset_cmd)(struct dsa_switch *ds);
	int (*setup_rgmii_delay)(const void *ctx, int port);
	/* Prototypes from include/net/dsa.h */
	int (*fdb_add_cmd)(struct dsa_switch *ds, int port,
			   const unsigned char *addr, u16 vid);
	int (*fdb_del_cmd)(struct dsa_switch *ds, int port,
			   const unsigned char *addr, u16 vid);
	void (*ptp_cmd_packing)(u8 *buf, struct sja1105_ptp_cmd *cmd,
				enum packing_op op);
	bool (*rxtstamp)(struct dsa_switch *ds, int port, struct sk_buff *skb);
	void (*txtstamp)(struct dsa_switch *ds, int port, struct sk_buff *skb);
	int (*clocking_setup)(struct sja1105_private *priv);
	int (*pcs_mdio_read)(struct mii_bus *bus, int phy, int reg);
	int (*pcs_mdio_write)(struct mii_bus *bus, int phy, int reg, u16 val);
	int (*disable_microcontroller)(struct sja1105_private *priv);
	const char *name;
	bool supports_mii[SJA1105_MAX_NUM_PORTS];
	bool supports_rmii[SJA1105_MAX_NUM_PORTS];
	bool supports_rgmii[SJA1105_MAX_NUM_PORTS];
	bool supports_sgmii[SJA1105_MAX_NUM_PORTS];
	bool supports_2500basex[SJA1105_MAX_NUM_PORTS];
	enum sja1105_internal_phy_t internal_phy[SJA1105_MAX_NUM_PORTS];
	const u64 port_speed[SJA1105_SPEED_MAX];
};

enum sja1105_key_type {
	SJA1105_KEY_BCAST,
	SJA1105_KEY_TC,
	SJA1105_KEY_VLAN_UNAWARE_VL,
	SJA1105_KEY_VLAN_AWARE_VL,
};

struct sja1105_key {
	enum sja1105_key_type type;

	union {
		/* SJA1105_KEY_TC */
		struct {
			int pcp;
		} tc;

		/* SJA1105_KEY_VLAN_UNAWARE_VL */
		/* SJA1105_KEY_VLAN_AWARE_VL */
		struct {
			u64 dmac;
			u16 vid;
			u16 pcp;
		} vl;
	};
};

enum sja1105_rule_type {
	SJA1105_RULE_BCAST_POLICER,
	SJA1105_RULE_TC_POLICER,
	SJA1105_RULE_VL,
};

enum sja1105_vl_type {
	SJA1105_VL_NONCRITICAL,
	SJA1105_VL_RATE_CONSTRAINED,
	SJA1105_VL_TIME_TRIGGERED,
};

struct sja1105_rule {
	struct list_head list;
	unsigned long cookie;
	unsigned long port_mask;
	struct sja1105_key key;
	enum sja1105_rule_type type;

	/* Action */
	union {
		/* SJA1105_RULE_BCAST_POLICER */
		struct {
			int sharindx;
		} bcast_pol;

		/* SJA1105_RULE_TC_POLICER */
		struct {
			int sharindx;
		} tc_pol;

		/* SJA1105_RULE_VL */
		struct {
			enum sja1105_vl_type type;
			unsigned long destports;
			int sharindx;
			int maxlen;
			int ipv;
			u64 base_time;
			u64 cycle_time;
			int num_entries;
			struct action_gate_entry *entries;
			struct flow_stats stats;
		} vl;
	};
};

struct sja1105_flow_block {
	struct list_head rules;
	bool l2_policer_used[SJA1105_NUM_L2_POLICERS];
	int num_virtual_links;
};

struct sja1105_private {
	struct sja1105_static_config static_config;
	int rgmii_rx_delay_ps[SJA1105_MAX_NUM_PORTS];
	int rgmii_tx_delay_ps[SJA1105_MAX_NUM_PORTS];
	phy_interface_t phy_mode[SJA1105_MAX_NUM_PORTS];
	bool fixed_link[SJA1105_MAX_NUM_PORTS];
	unsigned long ucast_egress_floods;
	unsigned long bcast_egress_floods;
	const struct sja1105_info *info;
	size_t max_xfer_len;
	struct spi_device *spidev;
	struct dsa_switch *ds;
	u16 bridge_pvid[SJA1105_MAX_NUM_PORTS];
	u16 tag_8021q_pvid[SJA1105_MAX_NUM_PORTS];
	struct sja1105_flow_block flow_block;
	struct sja1105_port ports[SJA1105_MAX_NUM_PORTS];
	/* Serializes transmission of management frames so that
	 * the switch doesn't confuse them with one another.
	 */
	struct mutex mgmt_lock;
	/* Serializes access to the dynamic config interface */
	struct mutex dynamic_config_lock;
	struct devlink_region **regions;
	struct sja1105_cbs_entry *cbs;
	struct mii_bus *mdio_base_t1;
	struct mii_bus *mdio_base_tx;
	struct mii_bus *mdio_pcs;
	struct dw_xpcs *xpcs[SJA1105_MAX_NUM_PORTS];
	struct sja1105_tagger_data tagger_data;
	struct sja1105_ptp_data ptp_data;
	struct sja1105_tas_data tas_data;
};

#include "sja1105_dynamic_config.h"

struct sja1105_spi_message {
	u64 access;
	u64 read_count;
	u64 address;
};

/* From sja1105_main.c */
enum sja1105_reset_reason {
	SJA1105_VLAN_FILTERING = 0,
	SJA1105_RX_HWTSTAMPING,
	SJA1105_AGEING_TIME,
	SJA1105_SCHEDULING,
	SJA1105_BEST_EFFORT_POLICING,
	SJA1105_VIRTUAL_LINKS,
};

int sja1105_static_config_reload(struct sja1105_private *priv,
				 enum sja1105_reset_reason reason);
int sja1105_vlan_filtering(struct dsa_switch *ds, int port, bool enabled,
			   struct netlink_ext_ack *extack);
void sja1105_frame_memory_partitioning(struct sja1105_private *priv);

/* From sja1105_mdio.c */
int sja1105_mdiobus_register(struct dsa_switch *ds);
void sja1105_mdiobus_unregister(struct dsa_switch *ds);
int sja1105_pcs_mdio_read(struct mii_bus *bus, int phy, int reg);
int sja1105_pcs_mdio_write(struct mii_bus *bus, int phy, int reg, u16 val);
int sja1110_pcs_mdio_read(struct mii_bus *bus, int phy, int reg);
int sja1110_pcs_mdio_write(struct mii_bus *bus, int phy, int reg, u16 val);

/* From sja1105_devlink.c */
int sja1105_devlink_setup(struct dsa_switch *ds);
void sja1105_devlink_teardown(struct dsa_switch *ds);
int sja1105_devlink_info_get(struct dsa_switch *ds,
			     struct devlink_info_req *req,
			     struct netlink_ext_ack *extack);

/* From sja1105_spi.c */
int sja1105_xfer_buf(const struct sja1105_private *priv,
		     sja1105_spi_rw_mode_t rw, u64 reg_addr,
		     u8 *buf, size_t len);
int sja1105_xfer_u32(const struct sja1105_private *priv,
		     sja1105_spi_rw_mode_t rw, u64 reg_addr, u32 *value,
		     struct ptp_system_timestamp *ptp_sts);
int sja1105_xfer_u64(const struct sja1105_private *priv,
		     sja1105_spi_rw_mode_t rw, u64 reg_addr, u64 *value,
		     struct ptp_system_timestamp *ptp_sts);
int static_config_buf_prepare_for_upload(struct sja1105_private *priv,
					 void *config_buf, int buf_len);
int sja1105_static_config_upload(struct sja1105_private *priv);
int sja1105_inhibit_tx(const struct sja1105_private *priv,
		       unsigned long port_bitmap, bool tx_inhibited);

extern const struct sja1105_info sja1105e_info;
extern const struct sja1105_info sja1105t_info;
extern const struct sja1105_info sja1105p_info;
extern const struct sja1105_info sja1105q_info;
extern const struct sja1105_info sja1105r_info;
extern const struct sja1105_info sja1105s_info;
extern const struct sja1105_info sja1110a_info;
extern const struct sja1105_info sja1110b_info;
extern const struct sja1105_info sja1110c_info;
extern const struct sja1105_info sja1110d_info;

/* From sja1105_clocking.c */

typedef enum {
	XMII_MAC = 0,
	XMII_PHY = 1,
} sja1105_mii_role_t;

typedef enum {
	XMII_MODE_MII		= 0,
	XMII_MODE_RMII		= 1,
	XMII_MODE_RGMII		= 2,
	XMII_MODE_SGMII		= 3,
} sja1105_phy_interface_t;

int sja1105pqrs_setup_rgmii_delay(const void *ctx, int port);
int sja1110_setup_rgmii_delay(const void *ctx, int port);
int sja1105_clocking_setup_port(struct sja1105_private *priv, int port);
int sja1105_clocking_setup(struct sja1105_private *priv);
int sja1110_disable_microcontroller(struct sja1105_private *priv);

/* From sja1105_ethtool.c */
void sja1105_get_ethtool_stats(struct dsa_switch *ds, int port, u64 *data);
void sja1105_get_strings(struct dsa_switch *ds, int port,
			 u32 stringset, u8 *data);
int sja1105_get_sset_count(struct dsa_switch *ds, int port, int sset);

/* From sja1105_dynamic_config.c */
int sja1105_dynamic_config_read(struct sja1105_private *priv,
				enum sja1105_blk_idx blk_idx,
				int index, void *entry);
int sja1105_dynamic_config_write(struct sja1105_private *priv,
				 enum sja1105_blk_idx blk_idx,
				 int index, void *entry, bool keep);

enum sja1105_iotag {
	SJA1105_C_TAG = 0, /* Inner VLAN header */
	SJA1105_S_TAG = 1, /* Outer VLAN header */
};

enum sja1110_vlan_type {
	SJA1110_VLAN_INVALID = 0,
	SJA1110_VLAN_C_TAG = 1, /* Single inner VLAN tag */
	SJA1110_VLAN_S_TAG = 2, /* Single outer VLAN tag */
	SJA1110_VLAN_D_TAG = 3, /* Double tagged, use outer tag for lookup */
};

enum sja1110_shaper_type {
	SJA1110_LEAKY_BUCKET_SHAPER = 0,
	SJA1110_CBS_SHAPER = 1,
};

u8 sja1105et_fdb_hash(struct sja1105_private *priv, const u8 *addr, u16 vid);
int sja1105et_fdb_add(struct dsa_switch *ds, int port,
		      const unsigned char *addr, u16 vid);
int sja1105et_fdb_del(struct dsa_switch *ds, int port,
		      const unsigned char *addr, u16 vid);
int sja1105pqrs_fdb_add(struct dsa_switch *ds, int port,
			const unsigned char *addr, u16 vid);
int sja1105pqrs_fdb_del(struct dsa_switch *ds, int port,
			const unsigned char *addr, u16 vid);

/* From sja1105_flower.c */
int sja1105_cls_flower_del(struct dsa_switch *ds, int port,
			   struct flow_cls_offload *cls, bool ingress);
int sja1105_cls_flower_add(struct dsa_switch *ds, int port,
			   struct flow_cls_offload *cls, bool ingress);
int sja1105_cls_flower_stats(struct dsa_switch *ds, int port,
			     struct flow_cls_offload *cls, bool ingress);
void sja1105_flower_setup(struct dsa_switch *ds);
void sja1105_flower_teardown(struct dsa_switch *ds);
struct sja1105_rule *sja1105_rule_find(struct sja1105_private *priv,
				       unsigned long cookie);

#endif
