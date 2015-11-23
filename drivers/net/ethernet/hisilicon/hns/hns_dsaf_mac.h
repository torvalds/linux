/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _HNS_DSAF_MAC_H
#define _HNS_DSAF_MAC_H

#include <linux/phy.h>
#include <linux/kernel.h>
#include <linux/if_vlan.h>
#include "hns_dsaf_main.h"

struct dsaf_device;

#define MAC_GMAC_SUPPORTED \
	(SUPPORTED_10baseT_Half \
	| SUPPORTED_10baseT_Full \
	| SUPPORTED_100baseT_Half \
	| SUPPORTED_100baseT_Full \
	| SUPPORTED_Autoneg)

#define MAC_DEFAULT_MTU	(ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN + ETH_DATA_LEN)
#define MAC_MAX_MTU		9600
#define MAC_MIN_MTU		68

#define MAC_DEFAULT_PAUSE_TIME 0xff

#define MAC_GMAC_IDX 0
#define MAC_XGMAC_IDX 1

#define ETH_STATIC_REG	 1
#define ETH_DUMP_REG	 5
/* check mac addr broadcast */
#define MAC_IS_BROADCAST(p)	((*(p) == 0xff) && (*((p) + 1) == 0xff) && \
		(*((p) + 2) == 0xff) &&  (*((p) + 3) == 0xff)  && \
		(*((p) + 4) == 0xff) && (*((p) + 5) == 0xff))

/* check mac addr is 01-00-5e-xx-xx-xx*/
#define MAC_IS_L3_MULTICAST(p) ((*((p) + 0) == 0x01) && \
			(*((p) + 1) == 0x00)   && \
			(*((p) + 2) == 0x5e))

/*check the mac addr is 0 in all bit*/
#define MAC_IS_ALL_ZEROS(p)   ((*(p) == 0) && (*((p) + 1) == 0) && \
	(*((p) + 2) == 0) && (*((p) + 3) == 0) && \
	(*((p) + 4) == 0) && (*((p) + 5) == 0))

/*check mac addr multicast*/
#define MAC_IS_MULTICAST(p)	((*((u8 *)((p) + 0)) & 0x01) ? (1) : (0))

/**< Number of octets (8-bit bytes) in an ethernet address */
#define MAC_NUM_OCTETS_PER_ADDR 6

struct mac_priv {
	void *mac;
};

/* net speed */
enum mac_speed {
	MAC_SPEED_10	= 10,	   /**< 10 Mbps */
	MAC_SPEED_100	= 100,	  /**< 100 Mbps */
	MAC_SPEED_1000  = 1000,	 /**< 1000 Mbps = 1 Gbps */
	MAC_SPEED_10000 = 10000	 /**< 10000 Mbps = 10 Gbps */
};

/*mac interface keyword	*/
enum mac_intf {
	MAC_IF_NONE  = 0x00000000,   /**< interface not invalid */
	MAC_IF_MII   = 0x00010000,   /**< MII interface */
	MAC_IF_RMII  = 0x00020000,   /**< RMII interface */
	MAC_IF_SMII  = 0x00030000,   /**< SMII interface */
	MAC_IF_GMII  = 0x00040000,   /**< GMII interface */
	MAC_IF_RGMII = 0x00050000,   /**< RGMII interface */
	MAC_IF_TBI   = 0x00060000,   /**< TBI interface */
	MAC_IF_RTBI  = 0x00070000,   /**< RTBI interface */
	MAC_IF_SGMII = 0x00080000,   /**< SGMII interface */
	MAC_IF_XGMII = 0x00090000,   /**< XGMII interface */
	MAC_IF_QSGMII = 0x000a0000	/**< QSGMII interface */
};

/*mac mode */
enum mac_mode {
	/**< Invalid Ethernet mode */
	MAC_MODE_INVALID	 = 0,
	/**<	10 Mbps MII   */
	MAC_MODE_MII_10	  = (MAC_IF_MII   | MAC_SPEED_10),
	/**<   100 Mbps MII   */
	MAC_MODE_MII_100	 = (MAC_IF_MII   | MAC_SPEED_100),
	/**<	10 Mbps RMII  */
	MAC_MODE_RMII_10	 = (MAC_IF_RMII  | MAC_SPEED_10),
	/**<   100 Mbps RMII  */
	MAC_MODE_RMII_100	= (MAC_IF_RMII  | MAC_SPEED_100),
	/**<	10 Mbps SMII  */
	MAC_MODE_SMII_10	 = (MAC_IF_SMII  | MAC_SPEED_10),
	/**<   100 Mbps SMII  */
	MAC_MODE_SMII_100	= (MAC_IF_SMII  | MAC_SPEED_100),
	/**<  1000 Mbps GMII  */
	MAC_MODE_GMII_1000   = (MAC_IF_GMII  | MAC_SPEED_1000),
	/**<	10 Mbps RGMII */
	MAC_MODE_RGMII_10	= (MAC_IF_RGMII | MAC_SPEED_10),
	/**<   100 Mbps RGMII */
	MAC_MODE_RGMII_100   = (MAC_IF_RGMII | MAC_SPEED_100),
	/**<  1000 Mbps RGMII */
	MAC_MODE_RGMII_1000  = (MAC_IF_RGMII | MAC_SPEED_1000),
	/**<  1000 Mbps TBI   */
	MAC_MODE_TBI_1000	= (MAC_IF_TBI   | MAC_SPEED_1000),
	/**<  1000 Mbps RTBI  */
	MAC_MODE_RTBI_1000   = (MAC_IF_RTBI  | MAC_SPEED_1000),
	/**<	10 Mbps SGMII */
	MAC_MODE_SGMII_10	= (MAC_IF_SGMII | MAC_SPEED_10),
	/**<   100 Mbps SGMII */
	MAC_MODE_SGMII_100   = (MAC_IF_SGMII | MAC_SPEED_100),
	/**<  1000 Mbps SGMII */
	MAC_MODE_SGMII_1000  = (MAC_IF_SGMII | MAC_SPEED_1000),
	/**< 10000 Mbps XGMII */
	MAC_MODE_XGMII_10000 = (MAC_IF_XGMII | MAC_SPEED_10000),
	/**<  1000 Mbps QSGMII */
	MAC_MODE_QSGMII_1000 = (MAC_IF_QSGMII | MAC_SPEED_1000)
};

/*mac communicate mode*/
enum mac_commom_mode {
	MAC_COMM_MODE_NONE	  = 0, /**< No transmit/receive communication */
	MAC_COMM_MODE_RX		= 1, /**< Only receive communication */
	MAC_COMM_MODE_TX		= 2, /**< Only transmit communication */
	MAC_COMM_MODE_RX_AND_TX = 3  /**< Both tx and rx communication */
};

/*mac statistics */
struct mac_statistics {
	u64  stat_pkts64; /* r-10G tr-DT 64 byte frame counter */
	u64  stat_pkts65to127; /* r-10G 65 to 127 byte frame counter */
	u64  stat_pkts128to255; /* r-10G 128 to 255 byte frame counter */
	u64  stat_pkts256to511; /*r-10G 256 to 511 byte frame counter */
	u64  stat_pkts512to1023;/* r-10G 512 to 1023 byte frame counter */
	u64  stat_pkts1024to1518; /* r-10G 1024 to 1518 byte frame counter */
	u64  stat_pkts1519to1522; /* r-10G 1519 to 1522 byte good frame count*/
	/* Total number of packets that were less than 64 octets */
	/*			long with a wrong CRC.*/
	u64  stat_fragments;
	/* Total number of packets longer than valid maximum length octets */
	u64  stat_jabbers;
	/* number of dropped packets due to internal errors of */
	/*			the MAC Client. */
	u64  stat_drop_events;
	/* Incremented when frames of correct length but with */
	/*			CRC error are received.*/
	u64  stat_crc_align_errors;
	/* Total number of packets that were less than 64 octets */
	/*			long with a good CRC.*/
	u64  stat_undersize_pkts;
	u64  stat_oversize_pkts;  /**< T,B.D*/

	u64  stat_rx_pause;		   /**< Pause MAC Control received */
	u64  stat_tx_pause;		   /**< Pause MAC Control sent */

	u64  in_octets;		/**< Total number of byte received. */
	u64  in_pkts;		/* Total number of packets received.*/
	u64  in_mcast_pkts;	/* Total number of multicast frame received */
	u64  in_bcast_pkts;	/* Total number of broadcast frame received */
				/* Frames received, but discarded due to */
				/* problems within the MAC RX. */
	u64  in_discards;
	u64  in_errors;		/* Number of frames received with error: */
				/*	- FIFO Overflow Error */
				/*	- CRC Error */
				/*	- Frame Too Long Error */
				/*	- Alignment Error */
	u64  out_octets; /*Total number of byte sent. */
	u64  out_pkts;	/**< Total number of packets sent .*/
	u64  out_mcast_pkts; /* Total number of multicast frame sent */
	u64  out_bcast_pkts; /* Total number of multicast frame sent */
	/* Frames received, but discarded due to problems within */
	/*			the MAC TX N/A!.*/
	u64  out_discards;
	u64  out_errors;	/*Number of frames transmitted with error: */
			/*	- FIFO Overflow Error */
			/*	- FIFO Underflow Error */
			/*	 - Other */
};

/*mac para struct ,mac get param from nic or dsaf when initialize*/
struct mac_params {
	char addr[MAC_NUM_OCTETS_PER_ADDR];
	void *vaddr; /*virtual address*/
	struct device *dev;
	u8 mac_id;
	/**< Ethernet operation mode (MAC-PHY interface and speed) */
	enum mac_mode mac_mode;
};

struct mac_info {
	u16 speed;/* The forced speed (lower bits) in */
		/*		 *mbps. Please use */
		/*		 * ethtool_cmd_speed()/_set() to */
		/*		 * access it */
	u8 duplex;		/* Duplex, half or full */
	u8 auto_neg;	/* Enable or disable autonegotiation */
	enum hnae_loop loop_mode;
	u8 tx_pause_en;
	u8 tx_pause_time;
	u8 rx_pause_en;
	u8 pad_and_crc_en;
	u8 promiscuous_en;
	u8 port_en;	 /*port enable*/
};

struct mac_entry_idx {
	u8 addr[MAC_NUM_OCTETS_PER_ADDR];
	u16 vlan_id:12;
	u16 valid:1;
	u16 qos:3;
};

struct mac_hw_stats {
	u64 rx_good_pkts;	/* only for xgmac */
	u64 rx_good_bytes;
	u64 rx_total_pkts;	/* only for xgmac */
	u64 rx_total_bytes;	/* only for xgmac */
	u64 rx_bad_bytes;	/* only for gmac */
	u64 rx_uc_pkts;
	u64 rx_mc_pkts;
	u64 rx_bc_pkts;
	u64 rx_fragment_err;	/* only for xgmac */
	u64 rx_undersize;	/* only for xgmac */
	u64 rx_under_min;
	u64 rx_minto64;		/* only for gmac */
	u64 rx_64bytes;
	u64 rx_65to127;
	u64 rx_128to255;
	u64 rx_256to511;
	u64 rx_512to1023;
	u64 rx_1024to1518;
	u64 rx_1519tomax;
	u64 rx_1519tomax_good;	/* only for xgmac */
	u64 rx_oversize;
	u64 rx_jabber_err;
	u64 rx_fcs_err;
	u64 rx_vlan_pkts;	/* only for gmac */
	u64 rx_data_err;	/* only for gmac */
	u64 rx_align_err;	/* only for gmac */
	u64 rx_long_err;	/* only for gmac */
	u64 rx_pfc_tc0;
	u64 rx_pfc_tc1;		/* only for xgmac */
	u64 rx_pfc_tc2;		/* only for xgmac */
	u64 rx_pfc_tc3;		/* only for xgmac */
	u64 rx_pfc_tc4;		/* only for xgmac */
	u64 rx_pfc_tc5;		/* only for xgmac */
	u64 rx_pfc_tc6;		/* only for xgmac */
	u64 rx_pfc_tc7;		/* only for xgmac */
	u64 rx_unknown_ctrl;
	u64 rx_filter_pkts;	/* only for gmac */
	u64 rx_filter_bytes;	/* only for gmac */
	u64 rx_fifo_overrun_err;/* only for gmac */
	u64 rx_len_err;		/* only for gmac */
	u64 rx_comma_err;	/* only for gmac */
	u64 rx_symbol_err;	/* only for xgmac */
	u64 tx_good_to_sw;	/* only for xgmac */
	u64 tx_bad_to_sw;	/* only for xgmac */
	u64 rx_1731_pkts;	/* only for xgmac */

	u64 tx_good_bytes;
	u64 tx_good_pkts;	/* only for xgmac */
	u64 tx_total_bytes;	/* only for xgmac */
	u64 tx_total_pkts;	/* only for xgmac */
	u64 tx_bad_bytes;	/* only for gmac */
	u64 tx_bad_pkts;	/* only for xgmac */
	u64 tx_uc_pkts;
	u64 tx_mc_pkts;
	u64 tx_bc_pkts;
	u64 tx_undersize;	/* only for xgmac */
	u64 tx_fragment_err;	/* only for xgmac */
	u64 tx_under_min_pkts;	/* only for gmac */
	u64 tx_64bytes;
	u64 tx_65to127;
	u64 tx_128to255;
	u64 tx_256to511;
	u64 tx_512to1023;
	u64 tx_1024to1518;
	u64 tx_1519tomax;
	u64 tx_1519tomax_good;	/* only for xgmac */
	u64 tx_oversize;	/* only for xgmac */
	u64 tx_jabber_err;
	u64 tx_underrun_err;	/* only for gmac */
	u64 tx_vlan;		/* only for gmac */
	u64 tx_crc_err;		/* only for gmac */
	u64 tx_pfc_tc0;
	u64 tx_pfc_tc1;		/* only for xgmac */
	u64 tx_pfc_tc2;		/* only for xgmac */
	u64 tx_pfc_tc3;		/* only for xgmac */
	u64 tx_pfc_tc4;		/* only for xgmac */
	u64 tx_pfc_tc5;		/* only for xgmac */
	u64 tx_pfc_tc6;		/* only for xgmac */
	u64 tx_pfc_tc7;		/* only for xgmac */
	u64 tx_ctrl;		/* only for xgmac */
	u64 tx_1731_pkts;	/* only for xgmac */
	u64 tx_1588_pkts;	/* only for xgmac */
	u64 rx_good_from_sw;	/* only for xgmac */
	u64 rx_bad_from_sw;	/* only for xgmac */
};

struct hns_mac_cb {
	struct device *dev;
	struct dsaf_device *dsaf_dev;
	struct mac_priv priv;
	u8 __iomem *vaddr;
	u8 __iomem *cpld_vaddr;
	u8 __iomem *sys_ctl_vaddr;
	u8 __iomem *serdes_vaddr;
	struct mac_entry_idx addr_entry_idx[DSAF_MAX_VM_NUM];
	u8 sfp_prsnt;
	u8 cpld_led_value;
	u8 mac_id;

	u8 link;
	u8 half_duplex;
	u16 speed;
	u16 max_speed;
	u16 max_frm;
	u16 tx_pause_frm_time;
	u32 if_support;
	u64 txpkt_for_led;
	u64 rxpkt_for_led;
	enum hnae_port_type mac_type;
	phy_interface_t phy_if;
	enum hnae_loop loop_mode;

	struct device_node *phy_node;

	struct mac_hw_stats hw_stats;
};

struct mac_driver {
	/*init Mac when init nic or dsaf*/
	void (*mac_init)(void *mac_drv);
	/*remove mac when remove nic or dsaf*/
	void (*mac_free)(void *mac_drv);
	/*enable mac when enable nic or dsaf*/
	void (*mac_enable)(void *mac_drv, enum mac_commom_mode mode);
	/*disable mac when disable nic or dsaf*/
	void (*mac_disable)(void *mac_drv, enum mac_commom_mode mode);
	/* config mac address*/
	void (*set_mac_addr)(void *mac_drv,	char *mac_addr);
	/*adjust mac mode of port,include speed and duplex*/
	int (*adjust_link)(void *mac_drv, enum mac_speed speed,
			   u32 full_duplex);
	/* config autoegotaite mode of port*/
	void (*set_an_mode)(void *mac_drv, u8 enable);
	/* config loopbank mode */
	int (*config_loopback)(void *mac_drv, enum hnae_loop loop_mode,
			       u8 enable);
	/* config mtu*/
	void (*config_max_frame_length)(void *mac_drv, u16 newval);
	/*config PAD and CRC enable */
	void (*config_pad_and_crc)(void *mac_drv, u8 newval);
	/* config duplex mode*/
	void (*config_half_duplex)(void *mac_drv, u8 newval);
	/*config tx pause time,if pause_time is zero,disable tx pause enable*/
	void (*set_tx_auto_pause_frames)(void *mac_drv, u16 pause_time);
	/*config rx pause enable*/
	void (*set_rx_ignore_pause_frames)(void *mac_drv, u32 enable);
	/* config rx mode for promiscuous*/
	int (*set_promiscuous)(void *mac_drv, u8 enable);
	/* get mac id */
	void (*mac_get_id)(void *mac_drv, u8 *mac_id);
	void (*mac_pausefrm_cfg)(void *mac_drv, u32 rx_en, u32 tx_en);

	void (*autoneg_stat)(void *mac_drv, u32 *enable);
	int (*set_pause_enable)(void *mac_drv, u32 rx_en, u32 tx_en);
	void (*get_pause_enable)(void *mac_drv, u32 *rx_en, u32 *tx_en);
	void (*get_link_status)(void *mac_drv, u32 *link_stat);
	/* get the imporant regs*/
	void (*get_regs)(void *mac_drv, void *data);
	int (*get_regs_count)(void);
	/* get strings name for ethtool statistic */
	void (*get_strings)(u32 stringset, u8 *data);
	/* get the number of strings*/
	int (*get_sset_count)(int stringset);

	/* get the statistic by ethtools*/
	void (*get_ethtool_stats)(void *mac_drv, u64 *data);

	/* get mac information */
	void (*get_info)(void *mac_drv, struct mac_info *mac_info);

	void (*update_stats)(void *mac_drv);

	enum mac_mode mac_mode;
	u8 mac_id;
	struct hns_mac_cb *mac_cb;
	void __iomem *io_base;
	unsigned int mac_en_flg;/*you'd better don't enable mac twice*/
	unsigned int virt_dev_num;
	struct device *dev;
};

struct mac_stats_string {
	char desc[64];
	unsigned long offset;
};

#define MAC_MAKE_MODE(interface, speed) (enum mac_mode)((interface) | (speed))
#define MAC_INTERFACE_FROM_MODE(mode) (enum mac_intf)((mode) & 0xFFFF0000)
#define MAC_SPEED_FROM_MODE(mode) (enum mac_speed)((mode) & 0x0000FFFF)
#define MAC_STATS_FIELD_OFF(field) (offsetof(struct mac_hw_stats, field))

static inline struct mac_driver *hns_mac_get_drv(
	const struct hns_mac_cb *mac_cb)
{
	return (struct mac_driver *)(mac_cb->priv.mac);
}

void *hns_gmac_config(struct hns_mac_cb *mac_cb,
		      struct mac_params *mac_param);
void *hns_xgmac_config(struct hns_mac_cb *mac_cb,
		       struct mac_params *mac_param);

int hns_mac_init(struct dsaf_device *dsaf_dev);
void mac_adjust_link(struct net_device *net_dev);
void hns_mac_get_link_status(struct hns_mac_cb *mac_cb,	u32 *link_status);
int hns_mac_change_vf_addr(struct hns_mac_cb *mac_cb, u32 vmid, char *addr);
int hns_mac_set_multi(struct hns_mac_cb *mac_cb,
		      u32 port_num, char *addr, u8 en);
int hns_mac_vm_config_bc_en(struct hns_mac_cb *mac_cb, u32 vm, u8 en);
void hns_mac_start(struct hns_mac_cb *mac_cb);
void hns_mac_stop(struct hns_mac_cb *mac_cb);
int hns_mac_del_mac(struct hns_mac_cb *mac_cb, u32 vfn, char *mac);
void hns_mac_uninit(struct dsaf_device *dsaf_dev);
void hns_mac_adjust_link(struct hns_mac_cb *mac_cb, int speed, int duplex);
void hns_mac_reset(struct hns_mac_cb *mac_cb);
void hns_mac_get_autoneg(struct hns_mac_cb *mac_cb, u32 *auto_neg);
void hns_mac_get_pauseparam(struct hns_mac_cb *mac_cb, u32 *rx_en, u32 *tx_en);
int hns_mac_set_autoneg(struct hns_mac_cb *mac_cb, u8 enable);
int hns_mac_set_pauseparam(struct hns_mac_cb *mac_cb, u32 rx_en, u32 tx_en);
int hns_mac_set_mtu(struct hns_mac_cb *mac_cb, u32 new_mtu);
int hns_mac_get_port_info(struct hns_mac_cb *mac_cb,
			  u8 *auto_neg, u16 *speed, u8 *duplex);
phy_interface_t hns_mac_get_phy_if(struct hns_mac_cb *mac_cb);
int hns_mac_config_sds_loopback(struct hns_mac_cb *mac_cb, u8 en);
int hns_mac_config_mac_loopback(struct hns_mac_cb *mac_cb,
				enum hnae_loop loop, int en);
void hns_mac_update_stats(struct hns_mac_cb *mac_cb);
void hns_mac_get_stats(struct hns_mac_cb *mac_cb, u64 *data);
void hns_mac_get_strings(struct hns_mac_cb *mac_cb, int stringset, u8 *data);
int hns_mac_get_sset_count(struct hns_mac_cb *mac_cb, int stringset);
void hns_mac_get_regs(struct hns_mac_cb *mac_cb, void *data);
int hns_mac_get_regs_count(struct hns_mac_cb *mac_cb);
void hns_set_led_opt(struct hns_mac_cb *mac_cb);
int hns_cpld_led_set_id(struct hns_mac_cb *mac_cb,
			enum hnae_led_state status);
#endif /* _HNS_DSAF_MAC_H */
