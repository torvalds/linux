/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 1999 - 2018 Intel Corporation. */

/* Linux PRO/1000 Ethernet Driver main header file */

#ifndef _E1000_H_
#define _E1000_H_

#include <linux/bitops.h>
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/crc32.h>
#include <linux/if_vlan.h>
#include <linux/timecounter.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/ptp_classify.h>
#include <linux/mii.h>
#include <linux/mdio.h>
#include <linux/pm_qos.h>
#include "hw.h"

struct e1000_info;

#define e_dbg(format, arg...) \
	netdev_dbg(hw->adapter->netdev, format, ## arg)
#define e_err(format, arg...) \
	netdev_err(adapter->netdev, format, ## arg)
#define e_info(format, arg...) \
	netdev_info(adapter->netdev, format, ## arg)
#define e_warn(format, arg...) \
	netdev_warn(adapter->netdev, format, ## arg)
#define e_notice(format, arg...) \
	netdev_notice(adapter->netdev, format, ## arg)

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

#define E1000_FC_PAUSE_TIME		0x0680 /* 858 usec */

/* How many Tx Descriptors do we need to call netif_wake_queue ? */
/* How many Rx Buffers do we bundle into one write to the hardware ? */
#define E1000_RX_BUFFER_WRITE		16 /* Must be power of 2 */

#define AUTO_ALL_MODES			0
#define E1000_EEPROM_APME		0x0400

#define E1000_MNG_VLAN_NONE		(-1)

#define DEFAULT_JUMBO			9234

/* Time to wait before putting the device into D3 if there's no link (in ms). */
#define LINK_TIMEOUT		100

/* Count for polling __E1000_RESET condition every 10-20msec.
 * Experimentation has shown the reset can take approximately 210msec.
 */
#define E1000_CHECK_RESET_COUNT		25

#define PCICFG_DESC_RING_STATUS		0xe4
#define FLUSH_DESC_REQUIRED		0x100

/* in the case of WTHRESH, it appears at least the 82571/2 hardware
 * writes back 4 descriptors when WTHRESH=5, and 3 descriptors when
 * WTHRESH=4, so a setting of 5 gives the most efficient bus
 * utilization but to avoid possible Tx stalls, set it to 1
 */
#define E1000_TXDCTL_DMA_BURST_ENABLE                          \
	(E1000_TXDCTL_GRAN | /* set descriptor granularity */  \
	 E1000_TXDCTL_COUNT_DESC |                             \
	 (1u << 16) | /* wthresh must be +1 more than desired */\
	 (1u << 8)  | /* hthresh */                             \
	 0x1f)        /* pthresh */

#define E1000_RXDCTL_DMA_BURST_ENABLE                          \
	(0x01000000 | /* set descriptor granularity */         \
	 (4u << 16) | /* set writeback threshold    */         \
	 (4u << 8)  | /* set prefetch threshold     */         \
	 0x20)        /* set hthresh                */

#define E1000_TIDV_FPD BIT(31)
#define E1000_RDTR_FPD BIT(31)

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
	board_pch2lan,
	board_pch_lpt,
	board_pch_spt,
	board_pch_cnp
};

struct e1000_ps_page {
	struct page *page;
	u64 dma; /* must be u64 - written to hw */
};

/* wrappers around a pointer to a socket buffer,
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
			unsigned int segs;
			unsigned int bytecount;
			u16 mapped_as_page;
		};
		/* Rx */
		struct {
			/* arrays of page information for packet split */
			struct e1000_ps_page *ps_pages;
			struct page *page;
		};
	};
};

struct e1000_ring {
	struct e1000_adapter *adapter;	/* back pointer to adapter */
	void *desc;			/* pointer to ring memory  */
	dma_addr_t dma;			/* phys address of ring    */
	unsigned int size;		/* length of ring in bytes */
	unsigned int count;		/* number of desc. in ring */

	u16 next_to_use;
	u16 next_to_clean;

	void __iomem *head;
	void __iomem *tail;

	/* array of buffer information structs */
	struct e1000_buffer *buffer_info;

	char name[IFNAMSIZ + 5];
	u32 ims_val;
	u32 itr_val;
	void __iomem *itr_register;
	int set_itr;

	struct sk_buff *rx_skb_top;
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

	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
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

	/* Tx - one ring per active queue */
	struct e1000_ring *tx_ring ____cacheline_aligned_in_smp;
	u32 tx_fifo_limit;

	struct napi_struct napi;

	unsigned int uncorr_errors;	/* uncorrectable ECC errors */
	unsigned int corr_errors;	/* correctable ECC errors */
	unsigned int restart_queue;
	u32 txd_cmd;

	bool detect_tx_hung;
	bool tx_hang_recheck;
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
	u32 tx_hwtstamp_timeouts;
	u32 tx_hwtstamp_skipped;

	/* Rx */
	bool (*clean_rx)(struct e1000_ring *ring, int *work_done,
			 int work_to_do) ____cacheline_aligned_in_smp;
	void (*alloc_rx_buf)(struct e1000_ring *ring, int cleaned_count,
			     gfp_t gfp);
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
	u32 rx_hwtstamp_cleared;

	unsigned int rx_ps_pages;
	u16 rx_ps_bsize0;
	u32 max_frame_size;
	u32 min_frame_size;

	/* OS defined structs */
	struct net_device *netdev;
	struct pci_dev *pdev;

	/* structs defined in e1000_hw.h */
	struct e1000_hw hw;

	spinlock_t stats64_lock;	/* protects statistics counters */
	struct e1000_hw_stats stats;
	struct e1000_phy_info phy_info;
	struct e1000_phy_stats phy_stats;

	/* Snapshot of PHY registers */
	struct e1000_phy_regs phy_regs;

	struct e1000_ring test_tx_ring;
	struct e1000_ring test_rx_ring;
	u32 test_icr;

	u32 msg_enable;
	unsigned int num_vectors;
	struct msix_entry *msix_entries;
	int int_mode;
	u32 eiac_mask;

	u32 eeprom_wol;
	u32 wol;
	u32 pba;
	u32 max_hw_frame_size;

	bool fc_autoneg;

	unsigned int flags;
	unsigned int flags2;
	struct work_struct downshift_task;
	struct work_struct update_phy_task;
	struct work_struct print_hang_task;

	int phy_hang_count;

	u16 tx_ring_count;
	u16 rx_ring_count;

	struct hwtstamp_config hwtstamp_config;
	struct delayed_work systim_overflow_work;
	struct sk_buff *tx_hwtstamp_skb;
	unsigned long tx_hwtstamp_start;
	struct work_struct tx_hwtstamp_work;
	spinlock_t systim_lock;	/* protects SYSTIML/H regsters */
	struct cyclecounter cc;
	struct timecounter tc;
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info ptp_clock_info;
	struct pm_qos_request pm_qos_req;
	s32 ptp_delta;

	u16 eee_advert;
};

struct e1000_info {
	enum e1000_mac_type	mac;
	unsigned int		flags;
	unsigned int		flags2;
	u32			pba;
	u32			max_hw_frame_size;
	s32			(*get_variants)(struct e1000_adapter *);
	const struct e1000_mac_operations *mac_ops;
	const struct e1000_phy_operations *phy_ops;
	const struct e1000_nvm_operations *nvm_ops;
};

s32 e1000e_get_base_timinca(struct e1000_adapter *adapter, u32 *timinca);

/* The system time is maintained by a 64-bit counter comprised of the 32-bit
 * SYSTIMH and SYSTIML registers.  How the counter increments (and therefore
 * its resolution) is based on the contents of the TIMINCA register - it
 * increments every incperiod (bits 31:24) clock ticks by incvalue (bits 23:0).
 * For the best accuracy, the incperiod should be as small as possible.  The
 * incvalue is scaled by a factor as large as possible (while still fitting
 * in bits 23:0) so that relatively small clock corrections can be made.
 *
 * As a result, a shift of INCVALUE_SHIFT_n is used to fit a value of
 * INCVALUE_n into the TIMINCA register allowing 32+8+(24-INCVALUE_SHIFT_n)
 * bits to count nanoseconds leaving the rest for fractional nonseconds.
 */
#define INCVALUE_96MHZ		125
#define INCVALUE_SHIFT_96MHZ	17
#define INCPERIOD_SHIFT_96MHZ	2
#define INCPERIOD_96MHZ		(12 >> INCPERIOD_SHIFT_96MHZ)

#define INCVALUE_25MHZ		40
#define INCVALUE_SHIFT_25MHZ	18
#define INCPERIOD_25MHZ		1

#define INCVALUE_24MHZ		125
#define INCVALUE_SHIFT_24MHZ	14
#define INCPERIOD_24MHZ		3

#define INCVALUE_38400KHZ	26
#define INCVALUE_SHIFT_38400KHZ	19
#define INCPERIOD_38400KHZ	1

/* Another drawback of scaling the incvalue by a large factor is the
 * 64-bit SYSTIM register overflows more quickly.  This is dealt with
 * by simply reading the clock before it overflows.
 *
 * Clock	ns bits	Overflows after
 * ~~~~~~	~~~~~~~	~~~~~~~~~~~~~~~
 * 96MHz	47-bit	2^(47-INCPERIOD_SHIFT_96MHz) / 10^9 / 3600 = 9.77 hrs
 * 25MHz	46-bit	2^46 / 10^9 / 3600 = 19.55 hours
 */
#define E1000_SYSTIM_OVERFLOW_PERIOD	(HZ * 60 * 60 * 4)
#define E1000_MAX_82574_SYSTIM_REREADS	50
#define E1000_82574_SYSTIM_EPSILON	(1ULL << 35ULL)

/* hardware capability, feature, and workaround flags */
#define FLAG_HAS_AMT                      BIT(0)
#define FLAG_HAS_FLASH                    BIT(1)
#define FLAG_HAS_HW_VLAN_FILTER           BIT(2)
#define FLAG_HAS_WOL                      BIT(3)
/* reserved BIT(4) */
#define FLAG_HAS_CTRLEXT_ON_LOAD          BIT(5)
#define FLAG_HAS_SWSM_ON_LOAD             BIT(6)
#define FLAG_HAS_JUMBO_FRAMES             BIT(7)
#define FLAG_READ_ONLY_NVM                BIT(8)
#define FLAG_IS_ICH                       BIT(9)
#define FLAG_HAS_MSIX                     BIT(10)
#define FLAG_HAS_SMART_POWER_DOWN         BIT(11)
#define FLAG_IS_QUAD_PORT_A               BIT(12)
#define FLAG_IS_QUAD_PORT                 BIT(13)
#define FLAG_HAS_HW_TIMESTAMP             BIT(14)
#define FLAG_APME_IN_WUC                  BIT(15)
#define FLAG_APME_IN_CTRL3                BIT(16)
#define FLAG_APME_CHECK_PORT_B            BIT(17)
#define FLAG_DISABLE_FC_PAUSE_TIME        BIT(18)
#define FLAG_NO_WAKE_UCAST                BIT(19)
#define FLAG_MNG_PT_ENABLED               BIT(20)
#define FLAG_RESET_OVERWRITES_LAA         BIT(21)
#define FLAG_TARC_SPEED_MODE_BIT          BIT(22)
#define FLAG_TARC_SET_BIT_ZERO            BIT(23)
#define FLAG_RX_NEEDS_RESTART             BIT(24)
#define FLAG_LSC_GIG_SPEED_DROP           BIT(25)
#define FLAG_SMART_POWER_DOWN             BIT(26)
#define FLAG_MSI_ENABLED                  BIT(27)
/* reserved BIT(28) */
#define FLAG_TSO_FORCE                    BIT(29)
#define FLAG_RESTART_NOW                  BIT(30)
#define FLAG_MSI_TEST_FAILED              BIT(31)

#define FLAG2_CRC_STRIPPING               BIT(0)
#define FLAG2_HAS_PHY_WAKEUP              BIT(1)
#define FLAG2_IS_DISCARDING               BIT(2)
#define FLAG2_DISABLE_ASPM_L1             BIT(3)
#define FLAG2_HAS_PHY_STATS               BIT(4)
#define FLAG2_HAS_EEE                     BIT(5)
#define FLAG2_DMA_BURST                   BIT(6)
#define FLAG2_DISABLE_ASPM_L0S            BIT(7)
#define FLAG2_DISABLE_AIM                 BIT(8)
#define FLAG2_CHECK_PHY_HANG              BIT(9)
#define FLAG2_NO_DISABLE_RX               BIT(10)
#define FLAG2_PCIM2PCI_ARBITER_WA         BIT(11)
#define FLAG2_DFLT_CRC_STRIPPING          BIT(12)
#define FLAG2_CHECK_RX_HWTSTAMP           BIT(13)
#define FLAG2_CHECK_SYSTIM_OVERFLOW       BIT(14)

#define E1000_RX_DESC_PS(R, i)	    \
	(&(((union e1000_rx_desc_packet_split *)((R).desc))[i]))
#define E1000_RX_DESC_EXT(R, i)	    \
	(&(((union e1000_rx_desc_extended *)((R).desc))[i]))
#define E1000_GET_DESC(R, i, type)	(&(((struct type *)((R).desc))[i]))
#define E1000_TX_DESC(R, i)		E1000_GET_DESC(R, i, e1000_tx_desc)
#define E1000_CONTEXT_DESC(R, i)	E1000_GET_DESC(R, i, e1000_context_desc)

enum e1000_state_t {
	__E1000_TESTING,
	__E1000_RESETTING,
	__E1000_ACCESS_SHARED_RESOURCE,
	__E1000_DOWN
};

enum latency_range {
	lowest_latency = 0,
	low_latency = 1,
	bulk_latency = 2,
	latency_invalid = 255
};

extern char e1000e_driver_name[];

void e1000e_check_options(struct e1000_adapter *adapter);
void e1000e_set_ethtool_ops(struct net_device *netdev);

int e1000e_open(struct net_device *netdev);
int e1000e_close(struct net_device *netdev);
void e1000e_up(struct e1000_adapter *adapter);
void e1000e_down(struct e1000_adapter *adapter, bool reset);
void e1000e_reinit_locked(struct e1000_adapter *adapter);
void e1000e_reset(struct e1000_adapter *adapter);
void e1000e_power_up_phy(struct e1000_adapter *adapter);
int e1000e_setup_rx_resources(struct e1000_ring *ring);
int e1000e_setup_tx_resources(struct e1000_ring *ring);
void e1000e_free_rx_resources(struct e1000_ring *ring);
void e1000e_free_tx_resources(struct e1000_ring *ring);
void e1000e_get_stats64(struct net_device *netdev,
			struct rtnl_link_stats64 *stats);
void e1000e_set_interrupt_capability(struct e1000_adapter *adapter);
void e1000e_reset_interrupt_capability(struct e1000_adapter *adapter);
void e1000e_get_hw_control(struct e1000_adapter *adapter);
void e1000e_release_hw_control(struct e1000_adapter *adapter);
void e1000e_write_itr(struct e1000_adapter *adapter, u32 itr);

extern unsigned int copybreak;

extern const struct e1000_info e1000_82571_info;
extern const struct e1000_info e1000_82572_info;
extern const struct e1000_info e1000_82573_info;
extern const struct e1000_info e1000_82574_info;
extern const struct e1000_info e1000_82583_info;
extern const struct e1000_info e1000_ich8_info;
extern const struct e1000_info e1000_ich9_info;
extern const struct e1000_info e1000_ich10_info;
extern const struct e1000_info e1000_pch_info;
extern const struct e1000_info e1000_pch2_info;
extern const struct e1000_info e1000_pch_lpt_info;
extern const struct e1000_info e1000_pch_spt_info;
extern const struct e1000_info e1000_pch_cnp_info;
extern const struct e1000_info e1000_es2_info;

void e1000e_ptp_init(struct e1000_adapter *adapter);
void e1000e_ptp_remove(struct e1000_adapter *adapter);

u64 e1000e_read_systim(struct e1000_adapter *adapter,
		       struct ptp_system_timestamp *sts);

static inline s32 e1000_phy_hw_reset(struct e1000_hw *hw)
{
	return hw->phy.ops.reset(hw);
}

static inline s32 e1e_rphy(struct e1000_hw *hw, u32 offset, u16 *data)
{
	return hw->phy.ops.read_reg(hw, offset, data);
}

static inline s32 e1e_rphy_locked(struct e1000_hw *hw, u32 offset, u16 *data)
{
	return hw->phy.ops.read_reg_locked(hw, offset, data);
}

static inline s32 e1e_wphy(struct e1000_hw *hw, u32 offset, u16 data)
{
	return hw->phy.ops.write_reg(hw, offset, data);
}

static inline s32 e1e_wphy_locked(struct e1000_hw *hw, u32 offset, u16 data)
{
	return hw->phy.ops.write_reg_locked(hw, offset, data);
}

void e1000e_reload_nvm_generic(struct e1000_hw *hw);

static inline s32 e1000e_read_mac_addr(struct e1000_hw *hw)
{
	if (hw->mac.ops.read_mac_addr)
		return hw->mac.ops.read_mac_addr(hw);

	return e1000_read_mac_addr_generic(hw);
}

static inline s32 e1000_validate_nvm_checksum(struct e1000_hw *hw)
{
	return hw->nvm.ops.validate(hw);
}

static inline s32 e1000e_update_nvm_checksum(struct e1000_hw *hw)
{
	return hw->nvm.ops.update(hw);
}

static inline s32 e1000_read_nvm(struct e1000_hw *hw, u16 offset, u16 words,
				 u16 *data)
{
	return hw->nvm.ops.read(hw, offset, words, data);
}

static inline s32 e1000_write_nvm(struct e1000_hw *hw, u16 offset, u16 words,
				  u16 *data)
{
	return hw->nvm.ops.write(hw, offset, words, data);
}

static inline s32 e1000_get_phy_info(struct e1000_hw *hw)
{
	return hw->phy.ops.get_info(hw);
}

static inline u32 __er32(struct e1000_hw *hw, unsigned long reg)
{
	return readl(hw->hw_addr + reg);
}

#define er32(reg)	__er32(hw, E1000_##reg)

void __ew32(struct e1000_hw *hw, unsigned long reg, u32 val);

#define ew32(reg, val)	__ew32(hw, E1000_##reg, (val))

#define e1e_flush()	er32(STATUS)

#define E1000_WRITE_REG_ARRAY(a, reg, offset, value) \
	(__ew32((a), (reg + ((offset) << 2)), (value)))

#define E1000_READ_REG_ARRAY(a, reg, offset) \
	(readl((a)->hw_addr + reg + ((offset) << 2)))

#endif /* _E1000_H_ */
