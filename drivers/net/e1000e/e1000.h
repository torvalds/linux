/*******************************************************************************

  Intel PRO/1000 Linux driver
  Copyright(c) 1999 - 2008 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  Linux NICS <linux.nics@intel.com>
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

/* Linux PRO/1000 Ethernet Driver main header file */

#ifndef _E1000_H_
#define _E1000_H_

#include <linux/types.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/io.h>
#include <linux/netdevice.h>

#include "hw.h"

struct e1000_info;

#define e_printk(level, adapter, format, arg...) \
	printk(level "%s: %s: " format, pci_name(adapter->pdev), \
	       adapter->netdev->name, ## arg)

#ifdef DEBUG
#define e_dbg(format, arg...) \
	e_printk(KERN_DEBUG , adapter, format, ## arg)
#else
#define e_dbg(format, arg...) do { (void)(adapter); } while (0)
#endif

#define e_err(format, arg...) \
	e_printk(KERN_ERR, adapter, format, ## arg)
#define e_info(format, arg...) \
	e_printk(KERN_INFO, adapter, format, ## arg)
#define e_warn(format, arg...) \
	e_printk(KERN_WARNING, adapter, format, ## arg)
#define e_notice(format, arg...) \
	e_printk(KERN_NOTICE, adapter, format, ## arg)


/* Interrupt modes, as used by the IntMode parameter */
#define E1000E_INT_MODE_LEGACY		0
#define E1000E_INT_MODE_MSI		1
#define E1000E_INT_MODE_MSIX		2

/* Tx/Rx descriptor defines */
#define E1000_DEFAULT_TXD		256
#define E1000_MAX_TXD			4096
#define E1000_MIN_TXD			64

#define E1000_DEFAULT_RXD		256
#define E1000_MAX_RXD			4096
#define E1000_MIN_RXD			64

#define E1000_MIN_ITR_USECS		10 /* 100000 irq/sec */
#define E1000_MAX_ITR_USECS		10000 /* 100    irq/sec */

/* Early Receive defines */
#define E1000_ERT_2048			0x100

#define E1000_FC_PAUSE_TIME		0x0680 /* 858 usec */

/* How many Tx Descriptors do we need to call netif_wake_queue ? */
/* How many Rx Buffers do we bundle into one write to the hardware ? */
#define E1000_RX_BUFFER_WRITE		16 /* Must be power of 2 */

#define AUTO_ALL_MODES			0
#define E1000_EEPROM_APME		0x0400

#define E1000_MNG_VLAN_NONE		(-1)

/* Number of packet split data buffers (not including the header buffer) */
#define PS_PAGE_BUFFERS			(MAX_PS_BUFFERS - 1)

#define DEFAULT_JUMBO			9234

/* BM/HV Specific Registers */
#define BM_PORT_CTRL_PAGE                 769

#define PHY_UPPER_SHIFT                   21
#define BM_PHY_REG(page, reg) \
	(((reg) & MAX_PHY_REG_ADDRESS) |\
	 (((page) & 0xFFFF) << PHY_PAGE_SHIFT) |\
	 (((reg) & ~MAX_PHY_REG_ADDRESS) << (PHY_UPPER_SHIFT - PHY_PAGE_SHIFT)))

/* PHY Wakeup Registers and defines */
#define BM_RCTL         PHY_REG(BM_WUC_PAGE, 0)
#define BM_WUC          PHY_REG(BM_WUC_PAGE, 1)
#define BM_WUFC         PHY_REG(BM_WUC_PAGE, 2)
#define BM_WUS          PHY_REG(BM_WUC_PAGE, 3)
#define BM_RAR_L(_i)    (BM_PHY_REG(BM_WUC_PAGE, 16 + ((_i) << 2)))
#define BM_RAR_M(_i)    (BM_PHY_REG(BM_WUC_PAGE, 17 + ((_i) << 2)))
#define BM_RAR_H(_i)    (BM_PHY_REG(BM_WUC_PAGE, 18 + ((_i) << 2)))
#define BM_RAR_CTRL(_i) (BM_PHY_REG(BM_WUC_PAGE, 19 + ((_i) << 2)))
#define BM_MTA(_i)      (BM_PHY_REG(BM_WUC_PAGE, 128 + ((_i) << 1)))

#define BM_RCTL_UPE           0x0001          /* Unicast Promiscuous Mode */
#define BM_RCTL_MPE           0x0002          /* Multicast Promiscuous Mode */
#define BM_RCTL_MO_SHIFT      3               /* Multicast Offset Shift */
#define BM_RCTL_MO_MASK       (3 << 3)        /* Multicast Offset Mask */
#define BM_RCTL_BAM           0x0020          /* Broadcast Accept Mode */
#define BM_RCTL_PMCF          0x0040          /* Pass MAC Control Frames */
#define BM_RCTL_RFCE          0x0080          /* Rx Flow Control Enable */

#define HV_SCC_UPPER		PHY_REG(778, 16) /* Single Collision Count */
#define HV_SCC_LOWER		PHY_REG(778, 17)
#define HV_ECOL_UPPER		PHY_REG(778, 18) /* Excessive Collision Count */
#define HV_ECOL_LOWER		PHY_REG(778, 19)
#define HV_MCC_UPPER		PHY_REG(778, 20) /* Multiple Collision Count */
#define HV_MCC_LOWER		PHY_REG(778, 21)
#define HV_LATECOL_UPPER	PHY_REG(778, 23) /* Late Collision Count */
#define HV_LATECOL_LOWER	PHY_REG(778, 24)
#define HV_COLC_UPPER		PHY_REG(778, 25) /* Collision Count */
#define HV_COLC_LOWER		PHY_REG(778, 26)
#define HV_DC_UPPER		PHY_REG(778, 27) /* Defer Count */
#define HV_DC_LOWER		PHY_REG(778, 28)
#define HV_TNCRS_UPPER		PHY_REG(778, 29) /* Transmit with no CRS */
#define HV_TNCRS_LOWER		PHY_REG(778, 30)

#define E1000_FCRTV_PCH     0x05F40 /* PCH Flow Control Refresh Timer Value */

/* BM PHY Copper Specific Status */
#define BM_CS_STATUS                      17
#define BM_CS_STATUS_LINK_UP              0x0400
#define BM_CS_STATUS_RESOLVED             0x0800
#define BM_CS_STATUS_SPEED_MASK           0xC000
#define BM_CS_STATUS_SPEED_1000           0x8000

/* 82577 Mobile Phy Status Register */
#define HV_M_STATUS                       26
#define HV_M_STATUS_AUTONEG_COMPLETE      0x1000
#define HV_M_STATUS_SPEED_MASK            0x0300
#define HV_M_STATUS_SPEED_1000            0x0200
#define HV_M_STATUS_LINK_UP               0x0040

enum e1000_boards {
	board_82571,
	board_82572,
	board_82573,
	board_82574,
	board_82583,
	board_80003es2lan,
	board_ich8lan,
	board_ich9lan,
	board_ich10lan,
	board_pchlan,
};

struct e1000_queue_stats {
	u64 packets;
	u64 bytes;
};

struct e1000_ps_page {
	struct page *page;
	u64 dma; /* must be u64 - written to hw */
};

/*
 * wrappers around a pointer to a socket buffer,
 * so a DMA handle can be stored along with the buffer
 */
struct e1000_buffer {
	dma_addr_t dma;
	struct sk_buff *skb;
	union {
		/* Tx */
		struct {
			unsigned long time_stamp;
			u16 length;
			u16 next_to_watch;
		};
		/* Rx */
		/* arrays of page information for packet split */
		struct e1000_ps_page *ps_pages;
	};
	struct page *page;
};

struct e1000_ring {
	void *desc;			/* pointer to ring memory  */
	dma_addr_t dma;			/* phys address of ring    */
	unsigned int size;		/* length of ring in bytes */
	unsigned int count;		/* number of desc. in ring */

	u16 next_to_use;
	u16 next_to_clean;

	u16 head;
	u16 tail;

	/* array of buffer information structs */
	struct e1000_buffer *buffer_info;

	char name[IFNAMSIZ + 5];
	u32 ims_val;
	u32 itr_val;
	u16 itr_register;
	int set_itr;

	struct sk_buff *rx_skb_top;

	struct e1000_queue_stats stats;
};

/* PHY register snapshot values */
struct e1000_phy_regs {
	u16 bmcr;		/* basic mode control register    */
	u16 bmsr;		/* basic mode status register     */
	u16 advertise;		/* auto-negotiation advertisement */
	u16 lpa;		/* link partner ability register  */
	u16 expansion;		/* auto-negotiation expansion reg */
	u16 ctrl1000;		/* 1000BASE-T control register    */
	u16 stat1000;		/* 1000BASE-T status register     */
	u16 estatus;		/* extended status register       */
};

/* board specific private data structure */
struct e1000_adapter {
	struct timer_list watchdog_timer;
	struct timer_list phy_info_timer;
	struct timer_list blink_timer;

	struct work_struct reset_task;
	struct work_struct watchdog_task;

	const struct e1000_info *ei;

	struct vlan_group *vlgrp;
	u32 bd_number;
	u32 rx_buffer_len;
	u16 mng_vlan_id;
	u16 link_speed;
	u16 link_duplex;
	u16 eeprom_vers;

	/* track device up/down/testing state */
	unsigned long state;

	/* Interrupt Throttle Rate */
	u32 itr;
	u32 itr_setting;
	u16 tx_itr;
	u16 rx_itr;

	/*
	 * Tx
	 */
	struct e1000_ring *tx_ring /* One per active queue */
						____cacheline_aligned_in_smp;

	struct napi_struct napi;

	unsigned long tx_queue_len;
	unsigned int restart_queue;
	u32 txd_cmd;

	bool detect_tx_hung;
	u8 tx_timeout_factor;

	u32 tx_int_delay;
	u32 tx_abs_int_delay;

	unsigned int total_tx_bytes;
	unsigned int total_tx_packets;
	unsigned int total_rx_bytes;
	unsigned int total_rx_packets;

	/* Tx stats */
	u64 tpt_old;
	u64 colc_old;
	u32 gotc;
	u64 gotc_old;
	u32 tx_timeout_count;
	u32 tx_fifo_head;
	u32 tx_head_addr;
	u32 tx_fifo_size;
	u32 tx_dma_failed;

	/*
	 * Rx
	 */
	bool (*clean_rx) (struct e1000_adapter *adapter,
			  int *work_done, int work_to_do)
						____cacheline_aligned_in_smp;
	void (*alloc_rx_buf) (struct e1000_adapter *adapter,
			      int cleaned_count);
	struct e1000_ring *rx_ring;

	u32 rx_int_delay;
	u32 rx_abs_int_delay;

	/* Rx stats */
	u64 hw_csum_err;
	u64 hw_csum_good;
	u64 rx_hdr_split;
	u32 gorc;
	u64 gorc_old;
	u32 alloc_rx_buff_failed;
	u32 rx_dma_failed;

	unsigned int rx_ps_pages;
	u16 rx_ps_bsize0;
	u32 max_frame_size;
	u32 min_frame_size;

	/* OS defined structs */
	struct net_device *netdev;
	struct pci_dev *pdev;
	struct net_device_stats net_stats;

	/* structs defined in e1000_hw.h */
	struct e1000_hw hw;

	struct e1000_hw_stats stats;
	struct e1000_phy_info phy_info;
	struct e1000_phy_stats phy_stats;

	/* Snapshot of PHY registers */
	struct e1000_phy_regs phy_regs;

	struct e1000_ring test_tx_ring;
	struct e1000_ring test_rx_ring;
	u32 test_icr;

	u32 msg_enable;
	struct msix_entry *msix_entries;
	int int_mode;
	u32 eiac_mask;

	u32 eeprom_wol;
	u32 wol;
	u32 pba;
	u32 max_hw_frame_size;

	bool fc_autoneg;

	unsigned long led_status;

	unsigned int flags;
	unsigned int flags2;
	struct work_struct downshift_task;
	struct work_struct update_phy_task;
	struct work_struct led_blink_task;
};

struct e1000_info {
	enum e1000_mac_type	mac;
	unsigned int		flags;
	unsigned int            flags2;
	u32			pba;
	u32			max_hw_frame_size;
	s32			(*get_variants)(struct e1000_adapter *);
	struct e1000_mac_operations *mac_ops;
	struct e1000_phy_operations *phy_ops;
	struct e1000_nvm_operations *nvm_ops;
};

/* hardware capability, feature, and workaround flags */
#define FLAG_HAS_AMT                      (1 << 0)
#define FLAG_HAS_FLASH                    (1 << 1)
#define FLAG_HAS_HW_VLAN_FILTER           (1 << 2)
#define FLAG_HAS_WOL                      (1 << 3)
#define FLAG_HAS_ERT                      (1 << 4)
#define FLAG_HAS_CTRLEXT_ON_LOAD          (1 << 5)
#define FLAG_HAS_SWSM_ON_LOAD             (1 << 6)
#define FLAG_HAS_JUMBO_FRAMES             (1 << 7)
#define FLAG_READ_ONLY_NVM                (1 << 8)
#define FLAG_IS_ICH                       (1 << 9)
#define FLAG_HAS_MSIX                     (1 << 10)
#define FLAG_HAS_SMART_POWER_DOWN         (1 << 11)
#define FLAG_IS_QUAD_PORT_A               (1 << 12)
#define FLAG_IS_QUAD_PORT                 (1 << 13)
#define FLAG_TIPG_MEDIUM_FOR_80003ESLAN   (1 << 14)
#define FLAG_APME_IN_WUC                  (1 << 15)
#define FLAG_APME_IN_CTRL3                (1 << 16)
#define FLAG_APME_CHECK_PORT_B            (1 << 17)
#define FLAG_DISABLE_FC_PAUSE_TIME        (1 << 18)
#define FLAG_NO_WAKE_UCAST                (1 << 19)
#define FLAG_MNG_PT_ENABLED               (1 << 20)
#define FLAG_RESET_OVERWRITES_LAA         (1 << 21)
#define FLAG_TARC_SPEED_MODE_BIT          (1 << 22)
#define FLAG_TARC_SET_BIT_ZERO            (1 << 23)
#define FLAG_RX_NEEDS_RESTART             (1 << 24)
#define FLAG_LSC_GIG_SPEED_DROP           (1 << 25)
#define FLAG_SMART_POWER_DOWN             (1 << 26)
#define FLAG_MSI_ENABLED                  (1 << 27)
#define FLAG_RX_CSUM_ENABLED              (1 << 28)
#define FLAG_TSO_FORCE                    (1 << 29)
#define FLAG_RX_RESTART_NOW               (1 << 30)
#define FLAG_MSI_TEST_FAILED              (1 << 31)

/* CRC Stripping defines */
#define FLAG2_CRC_STRIPPING               (1 << 0)
#define FLAG2_HAS_PHY_WAKEUP              (1 << 1)
#define FLAG2_IS_DISCARDING               (1 << 2)

#define E1000_RX_DESC_PS(R, i)	    \
	(&(((union e1000_rx_desc_packet_split *)((R).desc))[i]))
#define E1000_GET_DESC(R, i, type)	(&(((struct type *)((R).desc))[i]))
#define E1000_RX_DESC(R, i)		E1000_GET_DESC(R, i, e1000_rx_desc)
#define E1000_TX_DESC(R, i)		E1000_GET_DESC(R, i, e1000_tx_desc)
#define E1000_CONTEXT_DESC(R, i)	E1000_GET_DESC(R, i, e1000_context_desc)

enum e1000_state_t {
	__E1000_TESTING,
	__E1000_RESETTING,
	__E1000_DOWN
};

enum latency_range {
	lowest_latency = 0,
	low_latency = 1,
	bulk_latency = 2,
	latency_invalid = 255
};

extern char e1000e_driver_name[];
extern const char e1000e_driver_version[];

extern void e1000e_check_options(struct e1000_adapter *adapter);
extern void e1000e_set_ethtool_ops(struct net_device *netdev);

extern int e1000e_up(struct e1000_adapter *adapter);
extern void e1000e_down(struct e1000_adapter *adapter);
extern void e1000e_reinit_locked(struct e1000_adapter *adapter);
extern void e1000e_reset(struct e1000_adapter *adapter);
extern void e1000e_power_up_phy(struct e1000_adapter *adapter);
extern int e1000e_setup_rx_resources(struct e1000_adapter *adapter);
extern int e1000e_setup_tx_resources(struct e1000_adapter *adapter);
extern void e1000e_free_rx_resources(struct e1000_adapter *adapter);
extern void e1000e_free_tx_resources(struct e1000_adapter *adapter);
extern void e1000e_update_stats(struct e1000_adapter *adapter);
extern bool e1000_has_link(struct e1000_adapter *adapter);
extern void e1000e_set_interrupt_capability(struct e1000_adapter *adapter);
extern void e1000e_reset_interrupt_capability(struct e1000_adapter *adapter);

extern unsigned int copybreak;

extern char *e1000e_get_hw_dev_name(struct e1000_hw *hw);

extern struct e1000_info e1000_82571_info;
extern struct e1000_info e1000_82572_info;
extern struct e1000_info e1000_82573_info;
extern struct e1000_info e1000_82574_info;
extern struct e1000_info e1000_82583_info;
extern struct e1000_info e1000_ich8_info;
extern struct e1000_info e1000_ich9_info;
extern struct e1000_info e1000_ich10_info;
extern struct e1000_info e1000_pch_info;
extern struct e1000_info e1000_es2_info;

extern s32 e1000e_read_pba_num(struct e1000_hw *hw, u32 *pba_num);

extern s32  e1000e_commit_phy(struct e1000_hw *hw);

extern bool e1000e_enable_mng_pass_thru(struct e1000_hw *hw);

extern bool e1000e_get_laa_state_82571(struct e1000_hw *hw);
extern void e1000e_set_laa_state_82571(struct e1000_hw *hw, bool state);

extern void e1000e_write_protect_nvm_ich8lan(struct e1000_hw *hw);
extern void e1000e_set_kmrn_lock_loss_workaround_ich8lan(struct e1000_hw *hw,
						 bool state);
extern void e1000e_igp3_phy_powerdown_workaround_ich8lan(struct e1000_hw *hw);
extern void e1000e_gig_downshift_workaround_ich8lan(struct e1000_hw *hw);
extern void e1000e_disable_gig_wol_ich8lan(struct e1000_hw *hw);

extern s32 e1000e_check_for_copper_link(struct e1000_hw *hw);
extern s32 e1000e_check_for_fiber_link(struct e1000_hw *hw);
extern s32 e1000e_check_for_serdes_link(struct e1000_hw *hw);
extern s32 e1000e_setup_led_generic(struct e1000_hw *hw);
extern s32 e1000e_cleanup_led_generic(struct e1000_hw *hw);
extern s32 e1000e_led_on_generic(struct e1000_hw *hw);
extern s32 e1000e_led_off_generic(struct e1000_hw *hw);
extern s32 e1000e_get_bus_info_pcie(struct e1000_hw *hw);
extern s32 e1000e_get_speed_and_duplex_copper(struct e1000_hw *hw, u16 *speed, u16 *duplex);
extern s32 e1000e_get_speed_and_duplex_fiber_serdes(struct e1000_hw *hw, u16 *speed, u16 *duplex);
extern s32 e1000e_disable_pcie_master(struct e1000_hw *hw);
extern s32 e1000e_get_auto_rd_done(struct e1000_hw *hw);
extern s32 e1000e_id_led_init(struct e1000_hw *hw);
extern void e1000e_clear_hw_cntrs_base(struct e1000_hw *hw);
extern s32 e1000e_setup_fiber_serdes_link(struct e1000_hw *hw);
extern s32 e1000e_copper_link_setup_m88(struct e1000_hw *hw);
extern s32 e1000e_copper_link_setup_igp(struct e1000_hw *hw);
extern s32 e1000e_setup_link(struct e1000_hw *hw);
extern void e1000e_clear_vfta(struct e1000_hw *hw);
extern void e1000e_init_rx_addrs(struct e1000_hw *hw, u16 rar_count);
extern void e1000e_update_mc_addr_list_generic(struct e1000_hw *hw,
					       u8 *mc_addr_list,
					       u32 mc_addr_count,
					       u32 rar_used_count,
					       u32 rar_count);
extern void e1000e_rar_set(struct e1000_hw *hw, u8 *addr, u32 index);
extern s32 e1000e_set_fc_watermarks(struct e1000_hw *hw);
extern void e1000e_set_pcie_no_snoop(struct e1000_hw *hw, u32 no_snoop);
extern s32 e1000e_get_hw_semaphore(struct e1000_hw *hw);
extern s32 e1000e_valid_led_default(struct e1000_hw *hw, u16 *data);
extern void e1000e_config_collision_dist(struct e1000_hw *hw);
extern s32 e1000e_config_fc_after_link_up(struct e1000_hw *hw);
extern s32 e1000e_force_mac_fc(struct e1000_hw *hw);
extern s32 e1000e_blink_led(struct e1000_hw *hw);
extern void e1000e_write_vfta(struct e1000_hw *hw, u32 offset, u32 value);
extern void e1000e_reset_adaptive(struct e1000_hw *hw);
extern void e1000e_update_adaptive(struct e1000_hw *hw);

extern s32 e1000e_setup_copper_link(struct e1000_hw *hw);
extern s32 e1000e_get_phy_id(struct e1000_hw *hw);
extern void e1000e_put_hw_semaphore(struct e1000_hw *hw);
extern s32 e1000e_check_reset_block_generic(struct e1000_hw *hw);
extern s32 e1000e_phy_force_speed_duplex_igp(struct e1000_hw *hw);
extern s32 e1000e_get_cable_length_igp_2(struct e1000_hw *hw);
extern s32 e1000e_get_phy_info_igp(struct e1000_hw *hw);
extern s32 e1000e_read_phy_reg_igp(struct e1000_hw *hw, u32 offset, u16 *data);
extern s32 e1000e_read_phy_reg_igp_locked(struct e1000_hw *hw, u32 offset,
                                          u16 *data);
extern s32 e1000e_phy_hw_reset_generic(struct e1000_hw *hw);
extern s32 e1000e_set_d3_lplu_state(struct e1000_hw *hw, bool active);
extern s32 e1000e_write_phy_reg_igp(struct e1000_hw *hw, u32 offset, u16 data);
extern s32 e1000e_write_phy_reg_igp_locked(struct e1000_hw *hw, u32 offset,
                                           u16 data);
extern s32 e1000e_phy_sw_reset(struct e1000_hw *hw);
extern s32 e1000e_phy_force_speed_duplex_m88(struct e1000_hw *hw);
extern s32 e1000e_get_cfg_done(struct e1000_hw *hw);
extern s32 e1000e_get_cable_length_m88(struct e1000_hw *hw);
extern s32 e1000e_get_phy_info_m88(struct e1000_hw *hw);
extern s32 e1000e_read_phy_reg_m88(struct e1000_hw *hw, u32 offset, u16 *data);
extern s32 e1000e_write_phy_reg_m88(struct e1000_hw *hw, u32 offset, u16 data);
extern s32 e1000e_phy_init_script_igp3(struct e1000_hw *hw);
extern enum e1000_phy_type e1000e_get_phy_type_from_id(u32 phy_id);
extern s32 e1000e_determine_phy_address(struct e1000_hw *hw);
extern s32 e1000e_write_phy_reg_bm(struct e1000_hw *hw, u32 offset, u16 data);
extern s32 e1000e_read_phy_reg_bm(struct e1000_hw *hw, u32 offset, u16 *data);
extern s32 e1000e_read_phy_reg_bm2(struct e1000_hw *hw, u32 offset, u16 *data);
extern s32 e1000e_write_phy_reg_bm2(struct e1000_hw *hw, u32 offset, u16 data);
extern void e1000e_phy_force_speed_duplex_setup(struct e1000_hw *hw, u16 *phy_ctrl);
extern s32 e1000e_write_kmrn_reg(struct e1000_hw *hw, u32 offset, u16 data);
extern s32 e1000e_write_kmrn_reg_locked(struct e1000_hw *hw, u32 offset,
                                        u16 data);
extern s32 e1000e_read_kmrn_reg(struct e1000_hw *hw, u32 offset, u16 *data);
extern s32 e1000e_read_kmrn_reg_locked(struct e1000_hw *hw, u32 offset,
                                       u16 *data);
extern s32 e1000e_phy_has_link_generic(struct e1000_hw *hw, u32 iterations,
			       u32 usec_interval, bool *success);
extern s32 e1000e_phy_reset_dsp(struct e1000_hw *hw);
extern s32 e1000e_read_phy_reg_mdic(struct e1000_hw *hw, u32 offset, u16 *data);
extern s32 e1000e_write_phy_reg_mdic(struct e1000_hw *hw, u32 offset, u16 data);
extern s32 e1000e_check_downshift(struct e1000_hw *hw);
extern s32 e1000_read_phy_reg_hv(struct e1000_hw *hw, u32 offset, u16 *data);
extern s32 e1000_read_phy_reg_hv_locked(struct e1000_hw *hw, u32 offset,
                                        u16 *data);
extern s32 e1000_write_phy_reg_hv(struct e1000_hw *hw, u32 offset, u16 data);
extern s32 e1000_write_phy_reg_hv_locked(struct e1000_hw *hw, u32 offset,
                                         u16 data);
extern s32 e1000_set_mdio_slow_mode_hv(struct e1000_hw *hw, bool slow);
extern s32 e1000_link_stall_workaround_hv(struct e1000_hw *hw);
extern s32 e1000_copper_link_setup_82577(struct e1000_hw *hw);
extern s32 e1000_check_polarity_82577(struct e1000_hw *hw);
extern s32 e1000_get_phy_info_82577(struct e1000_hw *hw);
extern s32 e1000_phy_force_speed_duplex_82577(struct e1000_hw *hw);
extern s32 e1000_get_cable_length_82577(struct e1000_hw *hw);

static inline s32 e1000_phy_hw_reset(struct e1000_hw *hw)
{
	return hw->phy.ops.reset_phy(hw);
}

static inline s32 e1000_check_reset_block(struct e1000_hw *hw)
{
	return hw->phy.ops.check_reset_block(hw);
}

static inline s32 e1e_rphy(struct e1000_hw *hw, u32 offset, u16 *data)
{
	return hw->phy.ops.read_phy_reg(hw, offset, data);
}

static inline s32 e1e_wphy(struct e1000_hw *hw, u32 offset, u16 data)
{
	return hw->phy.ops.write_phy_reg(hw, offset, data);
}

static inline s32 e1000_get_cable_length(struct e1000_hw *hw)
{
	return hw->phy.ops.get_cable_length(hw);
}

extern s32 e1000e_acquire_nvm(struct e1000_hw *hw);
extern s32 e1000e_write_nvm_spi(struct e1000_hw *hw, u16 offset, u16 words, u16 *data);
extern s32 e1000e_update_nvm_checksum_generic(struct e1000_hw *hw);
extern s32 e1000e_poll_eerd_eewr_done(struct e1000_hw *hw, int ee_reg);
extern s32 e1000e_read_nvm_eerd(struct e1000_hw *hw, u16 offset, u16 words, u16 *data);
extern s32 e1000e_validate_nvm_checksum_generic(struct e1000_hw *hw);
extern void e1000e_release_nvm(struct e1000_hw *hw);
extern void e1000e_reload_nvm(struct e1000_hw *hw);
extern s32 e1000e_read_mac_addr(struct e1000_hw *hw);

static inline s32 e1000_validate_nvm_checksum(struct e1000_hw *hw)
{
	return hw->nvm.ops.validate_nvm(hw);
}

static inline s32 e1000e_update_nvm_checksum(struct e1000_hw *hw)
{
	return hw->nvm.ops.update_nvm(hw);
}

static inline s32 e1000_read_nvm(struct e1000_hw *hw, u16 offset, u16 words, u16 *data)
{
	return hw->nvm.ops.read_nvm(hw, offset, words, data);
}

static inline s32 e1000_write_nvm(struct e1000_hw *hw, u16 offset, u16 words, u16 *data)
{
	return hw->nvm.ops.write_nvm(hw, offset, words, data);
}

static inline s32 e1000_get_phy_info(struct e1000_hw *hw)
{
	return hw->phy.ops.get_phy_info(hw);
}

static inline s32 e1000e_check_mng_mode(struct e1000_hw *hw)
{
	return hw->mac.ops.check_mng_mode(hw);
}

extern bool e1000e_check_mng_mode_generic(struct e1000_hw *hw);
extern bool e1000e_enable_tx_pkt_filtering(struct e1000_hw *hw);
extern s32 e1000e_mng_write_dhcp_info(struct e1000_hw *hw, u8 *buffer, u16 length);

static inline u32 __er32(struct e1000_hw *hw, unsigned long reg)
{
	return readl(hw->hw_addr + reg);
}

static inline void __ew32(struct e1000_hw *hw, unsigned long reg, u32 val)
{
	writel(val, hw->hw_addr + reg);
}

#endif /* _E1000_H_ */
