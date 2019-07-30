/* SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2018, Sensor-Technik Wiedemann GmbH
 * Copyright (c) 2018-2019, Vladimir Oltean <olteanv@gmail.com>
 */
#ifndef _SJA1105_H
#define _SJA1105_H

#include <linux/dsa/sja1105.h>
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

/* Keeps the different addresses between E/T and P/Q/R/S */
struct sja1105_regs {
	u64 device_id;
	u64 prod_id;
	u64 status;
	u64 port_control;
	u64 rgu;
	u64 config;
	u64 rmii_pll1;
	u64 pad_mii_tx[SJA1105_NUM_PORTS];
	u64 cgu_idiv[SJA1105_NUM_PORTS];
	u64 rgmii_pad_mii_tx[SJA1105_NUM_PORTS];
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
	u64 qlevel[SJA1105_NUM_PORTS];
};

struct sja1105_info {
	u64 device_id;
	/* Needed for distinction between P and R, and between Q and S
	 * (since the parts with/without SGMII share the same
	 * switch core and device_id)
	 */
	u64 part_no;
	const struct sja1105_dynamic_table_ops *dyn_ops;
	const struct sja1105_table_ops *static_ops;
	const struct sja1105_regs *regs;
	int (*reset_cmd)(const void *ctx, const void *data);
	int (*setup_rgmii_delay)(const void *ctx, int port);
	const char *name;
};

struct sja1105_private {
	struct sja1105_static_config static_config;
	bool rgmii_rx_delay[SJA1105_NUM_PORTS];
	bool rgmii_tx_delay[SJA1105_NUM_PORTS];
	const struct sja1105_info *info;
	struct gpio_desc *reset_gpio;
	struct spi_device *spidev;
	struct dsa_switch *ds;
	struct sja1105_port ports[SJA1105_NUM_PORTS];
	/* Serializes transmission of management frames so that
	 * the switch doesn't confuse them with one another.
	 */
	struct mutex mgmt_lock;
};

#include "sja1105_dynamic_config.h"

struct sja1105_spi_message {
	u64 access;
	u64 read_count;
	u64 address;
};

typedef enum {
	SPI_READ = 0,
	SPI_WRITE = 1,
} sja1105_spi_rw_mode_t;

/* From sja1105_spi.c */
int sja1105_spi_send_packed_buf(const struct sja1105_private *priv,
				sja1105_spi_rw_mode_t rw, u64 reg_addr,
				void *packed_buf, size_t size_bytes);
int sja1105_spi_send_int(const struct sja1105_private *priv,
			 sja1105_spi_rw_mode_t rw, u64 reg_addr,
			 u64 *value, u64 size_bytes);
int sja1105_spi_send_long_packed_buf(const struct sja1105_private *priv,
				     sja1105_spi_rw_mode_t rw, u64 base_addr,
				     void *packed_buf, u64 buf_len);
int sja1105_static_config_upload(struct sja1105_private *priv);

extern struct sja1105_info sja1105e_info;
extern struct sja1105_info sja1105t_info;
extern struct sja1105_info sja1105p_info;
extern struct sja1105_info sja1105q_info;
extern struct sja1105_info sja1105r_info;
extern struct sja1105_info sja1105s_info;

/* From sja1105_clocking.c */

typedef enum {
	XMII_MAC = 0,
	XMII_PHY = 1,
} sja1105_mii_role_t;

typedef enum {
	XMII_MODE_MII		= 0,
	XMII_MODE_RMII		= 1,
	XMII_MODE_RGMII		= 2,
} sja1105_phy_interface_t;

typedef enum {
	SJA1105_SPEED_10MBPS	= 3,
	SJA1105_SPEED_100MBPS	= 2,
	SJA1105_SPEED_1000MBPS	= 1,
	SJA1105_SPEED_AUTO	= 0,
} sja1105_speed_t;

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

u8 sja1105_fdb_hash(struct sja1105_private *priv, const u8 *addr, u16 vid);

/* Common implementations for the static and dynamic configs */
size_t sja1105_l2_forwarding_entry_packing(void *buf, void *entry_ptr,
					   enum packing_op op);
size_t sja1105pqrs_l2_lookup_entry_packing(void *buf, void *entry_ptr,
					   enum packing_op op);
size_t sja1105et_l2_lookup_entry_packing(void *buf, void *entry_ptr,
					 enum packing_op op);
size_t sja1105_vlan_lookup_entry_packing(void *buf, void *entry_ptr,
					 enum packing_op op);
size_t sja1105pqrs_mac_config_entry_packing(void *buf, void *entry_ptr,
					    enum packing_op op);

#endif
