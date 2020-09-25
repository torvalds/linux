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

#define SJA1105_NUM_PORTS		5
#define SJA1105_NUM_TC			8
#define SJA1105ET_FDB_BIN_SIZE		4
/* The hardware value is in multiples of 10 ms.
 * The passed parameter is in multiples of 1 ms.
 */
#define SJA1105_AGEING_TIME_MS(ms)	((ms) / 10)
#define SJA1105_NUM_L2_POLICERS		45

typedef enum {
	SPI_READ = 0,
	SPI_WRITE = 1,
} sja1105_spi_rw_mode_t;

#include "sja1105_tas.h"
#include "sja1105_ptp.h"

/* Keeps the different addresses between E/T and P/Q/R/S */
struct sja1105_regs {
	u64 device_id;
	u64 prod_id;
	u64 status;
	u64 port_control;
	u64 rgu;
	u64 vl_status;
	u64 config;
	u64 sgmii;
	u64 rmii_pll1;
	u64 ptppinst;
	u64 ptppindur;
	u64 ptp_control;
	u64 ptpclkval;
	u64 ptpclkrate;
	u64 ptpclkcorp;
	u64 ptpsyncts;
	u64 ptpschtm;
	u64 ptpegr_ts[SJA1105_NUM_PORTS];
	u64 pad_mii_tx[SJA1105_NUM_PORTS];
	u64 pad_mii_rx[SJA1105_NUM_PORTS];
	u64 pad_mii_id[SJA1105_NUM_PORTS];
	u64 cgu_idiv[SJA1105_NUM_PORTS];
	u64 mii_tx_clk[SJA1105_NUM_PORTS];
	u64 mii_rx_clk[SJA1105_NUM_PORTS];
	u64 mii_ext_tx_clk[SJA1105_NUM_PORTS];
	u64 mii_ext_rx_clk[SJA1105_NUM_PORTS];
	u64 rgmii_tx_clk[SJA1105_NUM_PORTS];
	u64 rmii_ref_clk[SJA1105_NUM_PORTS];
	u64 rmii_ext_tx_clk[SJA1105_NUM_PORTS];
	u64 mac[SJA1105_NUM_PORTS];
	u64 mac_hl1[SJA1105_NUM_PORTS];
	u64 mac_hl2[SJA1105_NUM_PORTS];
	u64 ether_stats[SJA1105_NUM_PORTS];
	u64 qlevel[SJA1105_NUM_PORTS];
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
	const struct sja1105_dynamic_table_ops *dyn_ops;
	const struct sja1105_table_ops *static_ops;
	const struct sja1105_regs *regs;
	/* Both E/T and P/Q/R/S have quirks when it comes to popping the S-Tag
	 * from double-tagged frames. E/T will pop it only when it's equal to
	 * TPID from the General Parameters Table, while P/Q/R/S will only
	 * pop it when it's equal to TPID2.
	 */
	u16 qinq_tpid;
	int (*reset_cmd)(struct dsa_switch *ds);
	int (*setup_rgmii_delay)(const void *ctx, int port);
	/* Prototypes from include/net/dsa.h */
	int (*fdb_add_cmd)(struct dsa_switch *ds, int port,
			   const unsigned char *addr, u16 vid);
	int (*fdb_del_cmd)(struct dsa_switch *ds, int port,
			   const unsigned char *addr, u16 vid);
	void (*ptp_cmd_packing)(u8 *buf, struct sja1105_ptp_cmd *cmd,
				enum packing_op op);
	const char *name;
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

struct sja1105_bridge_vlan {
	struct list_head list;
	int port;
	u16 vid;
	bool pvid;
	bool untagged;
};

enum sja1105_vlan_state {
	SJA1105_VLAN_UNAWARE,
	SJA1105_VLAN_BEST_EFFORT,
	SJA1105_VLAN_FILTERING_FULL,
};

struct sja1105_private {
	struct sja1105_static_config static_config;
	bool rgmii_rx_delay[SJA1105_NUM_PORTS];
	bool rgmii_tx_delay[SJA1105_NUM_PORTS];
	bool best_effort_vlan_filtering;
	const struct sja1105_info *info;
	struct gpio_desc *reset_gpio;
	struct spi_device *spidev;
	struct dsa_switch *ds;
	struct list_head dsa_8021q_vlans;
	struct list_head bridge_vlans;
	struct sja1105_flow_block flow_block;
	struct sja1105_port ports[SJA1105_NUM_PORTS];
	/* Serializes transmission of management frames so that
	 * the switch doesn't confuse them with one another.
	 */
	struct mutex mgmt_lock;
	struct dsa_8021q_context *dsa_8021q_ctx;
	enum sja1105_vlan_state vlan_state;
	struct devlink_region **regions;
	struct sja1105_cbs_entry *cbs;
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
int sja1105_vlan_filtering(struct dsa_switch *ds, int port, bool enabled);
void sja1105_frame_memory_partitioning(struct sja1105_private *priv);

/* From sja1105_devlink.c */
int sja1105_devlink_setup(struct dsa_switch *ds);
void sja1105_devlink_teardown(struct dsa_switch *ds);
int sja1105_devlink_param_get(struct dsa_switch *ds, u32 id,
			      struct devlink_param_gset_ctx *ctx);
int sja1105_devlink_param_set(struct dsa_switch *ds, u32 id,
			      struct devlink_param_gset_ctx *ctx);
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

typedef enum {
	SJA1105_SPEED_10MBPS	= 3,
	SJA1105_SPEED_100MBPS	= 2,
	SJA1105_SPEED_1000MBPS	= 1,
	SJA1105_SPEED_AUTO	= 0,
} sja1105_speed_t;

int sja1105pqrs_setup_rgmii_delay(const void *ctx, int port);
int sja1105_clocking_setup_port(struct sja1105_private *priv, int port);
int sja1105_clocking_setup(struct sja1105_private *priv);

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
