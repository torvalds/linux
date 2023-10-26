/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andrea.merello@gmail.com>, et al.
 *
 * Contact Information: wlanfae <wlanfae@realtek.com>
 */
#ifndef _RTL_CORE_H
#define _RTL_CORE_H

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/rtnetlink.h>
#include <linux/wireless.h>
#include <linux/timer.h>
#include <linux/proc_fs.h>
#include <linux/if_arp.h>
#include <linux/random.h>
#include <linux/io.h>

/* Need this defined before including local include files */
#define DRV_NAME "rtl819xE"

#include "../rtllib.h"

#include "../dot11d.h"

#include "r8192E_firmware.h"
#include "r8192E_hw.h"

#include "r8190P_def.h"
#include "r8192E_dev.h"

#include "rtl_eeprom.h"
#include "rtl_ps.h"
#include "rtl_pci.h"
#include "rtl_cam.h"

#define DRV_COPYRIGHT		\
	"Copyright(c) 2008 - 2010 Realsil Semiconductor Corporation"
#define DRV_AUTHOR  "<wlanfae@realtek.com>"
#define DRV_VERSION  "0014.0401.2010"

#define TOTAL_CAM_ENTRY		32
#define CAM_CONTENT_COUNT	8

#define HAL_HW_PCI_REVISION_ID_8192PCIE		0x01
#define HAL_HW_PCI_REVISION_ID_8192SE	0x10

#define RTLLIB_WATCH_DOG_TIME		2000

#define MAX_DEV_ADDR_SIZE		8  /*support till 64 bit bus width OS*/
#define MAX_FIRMWARE_INFORMATION_SIZE   32
#define MAX_802_11_HEADER_LENGTH	(40 + MAX_FIRMWARE_INFORMATION_SIZE)
#define ENCRYPTION_MAX_OVERHEAD		128
#define MAX_FRAGMENT_COUNT		8
#define MAX_TRANSMIT_BUFFER_SIZE	\
	(1600 + (MAX_802_11_HEADER_LENGTH + ENCRYPTION_MAX_OVERHEAD) *	\
	 MAX_FRAGMENT_COUNT)

#define CMDPACKET_FRAG_SIZE (4 * (MAX_TRANSMIT_BUFFER_SIZE / 4) - 8)

#define DEFAULT_FRAG_THRESHOLD	2342U
#define MIN_FRAG_THRESHOLD	256U
#define DEFAULT_BEACONINTERVAL	0x64U

#define DEFAULT_RETRY_RTS	7
#define DEFAULT_RETRY_DATA	7

#define	PHY_RSSI_SLID_WIN_MAX			100

#define TX_BB_GAIN_TABLE_LEN			37
#define CCK_TX_BB_GAIN_TABLE_LEN		23

#define CHANNEL_PLAN_LEN			10
#define S_CRC_LEN				4

#define NIC_SEND_HANG_THRESHOLD_NORMAL		4
#define NIC_SEND_HANG_THRESHOLD_POWERSAVE	8

#define MAX_TX_QUEUE				9

#define MAX_RX_COUNT				64
#define MAX_TX_QUEUE_COUNT			9

extern int hwwep;

enum nic_t {
	NIC_UNKNOWN     = 0,
	NIC_8192E       = 1,
	NIC_8190P       = 2,
	NIC_8192SE      = 4,
	NIC_8192CE	= 5,
	NIC_8192CU	= 6,
	NIC_8192DE	= 7,
	NIC_8192DU	= 8,
};

enum rt_eeprom_type {
	EEPROM_93C46,
	EEPROM_93C56,
};

enum dcmg_txcmd_op {
	TXCMD_TXRA_HISTORY_CTRL		= 0xFF900000,
	TXCMD_RESET_TX_PKT_BUFF		= 0xFF900001,
	TXCMD_RESET_RX_PKT_BUFF		= 0xFF900002,
	TXCMD_SET_TX_DURATION		= 0xFF900003,
	TXCMD_SET_RX_RSSI		= 0xFF900004,
	TXCMD_SET_TX_PWR_TRACKING	= 0xFF900005,
	TXCMD_XXXX_CTRL,
};

enum rt_customer_id {
	RT_CID_DEFAULT	  = 0,
	RT_CID_TOSHIBA	  = 9,
	RT_CID_819X_NETCORE     = 10,
};

enum reset_type {
	RESET_TYPE_NORESET = 0x00,
	RESET_TYPE_SILENT = 0x02
};

struct rt_stats {
	unsigned long received_rate_histogram[4][32];
	unsigned long txbytesunicast;
	unsigned long rxbytesunicast;
	unsigned long txretrycount;
	u8	last_packet_rate;
	unsigned long slide_signal_strength[100];
	unsigned long slide_evm[100];
	unsigned long	slide_rssi_total;
	unsigned long slide_evm_total;
	long signal_strength;
	long last_signal_strength_inpercent;
	long	recv_signal_power;
	u8 rx_rssi_percentage[4];
	u8 rx_evm_percentage[2];
	u32 slide_beacon_pwdb[100];
	u32 slide_beacon_total;
	u32	CurrentShowTxate;
};

struct init_gain {
	u8	xaagccore1;
	u8	xbagccore1;
	u8	xcagccore1;
	u8	xdagccore1;
	u8	cca;

};

struct tx_ring {
	u32 *desc;
	u8 nStuckCount;
	struct tx_ring *next;
} __packed;

struct rtl8192_tx_ring {
	struct tx_desc *desc;
	dma_addr_t dma;
	unsigned int idx;
	unsigned int entries;
	struct sk_buff_head queue;
};

struct r8192_priv {
	struct pci_dev *pdev;
	struct pci_dev *bridge_pdev;

	bool		bfirst_after_down;
	bool		being_init_adapter;

	int		irq;
	short	irq_enabled;

	short	up;
	short	up_first_time;
	struct delayed_work		update_beacon_wq;
	struct delayed_work		watch_dog_wq;
	struct delayed_work		txpower_tracking_wq;
	struct delayed_work		rfpath_check_wq;
	struct delayed_work		gpio_change_rf_wq;
	struct rtllib_device			*rtllib;

	struct work_struct				reset_wq;

	enum rt_customer_id customer_id;

	enum ht_channel_width current_chnl_bw;
	struct bb_reg_definition phy_reg_def[4];
	struct rate_adaptive rate_adaptive;

	struct rt_firmware *fw_info;
	enum rtl819x_loopback loopback_mode;

	struct timer_list			watch_dog_timer;
	struct timer_list			fsync_timer;
	struct timer_list			gpio_polling_timer;

	spinlock_t				irq_th_lock;
	spinlock_t				tx_lock;
	spinlock_t				rf_ps_lock;
	spinlock_t				ps_lock;

	struct sk_buff_head		skb_queue;

	struct tasklet_struct		irq_rx_tasklet;
	struct tasklet_struct		irq_tx_tasklet;
	struct tasklet_struct		irq_prepare_beacon_tasklet;

	struct mutex				wx_mutex;
	struct mutex				rf_mutex;
	struct mutex				mutex;

	struct rt_stats stats;
	struct iw_statistics			wstats;

	u8 (*rf_set_chan)(struct net_device *dev, u8 ch);

	struct rx_desc *rx_ring;
	struct sk_buff	*rx_buf[MAX_RX_COUNT];
	dma_addr_t	rx_ring_dma;
	unsigned int	rx_idx;
	int		rxringcount;
	u16		rxbuffersize;

	u64 last_rx_desc_tsf;

	u32 receive_config;
	u8		retry_data;
	u8		retry_rts;
	u16		rts;

	struct rtl8192_tx_ring tx_ring[MAX_TX_QUEUE_COUNT];
	int		 txringcount;
	atomic_t	tx_pending[0x10];

	u16 short_retry_limit;
	u16 long_retry_limit;

	bool		hw_radio_off;
	bool		blinked_ingpio;
	u8		polling_timer_on;

	/**********************************************************/
	struct work_struct qos_activate;

	short	promisc;

	short	chan;

	u32 irq_mask[2];

	u8 rf_mode;
	enum nic_t card_8192;
	u8 card_8192_version;

	u8 ic_cut;
	char nick[IW_ESSID_MAX_SIZE + 1];
	u8 check_roaming_cnt;

	u32 silent_reset_rx_slot_index;
	u32 silent_reset_rx_stuck_event[MAX_SILENT_RESET_RX_SLOT_NUM];

	u16 basic_rate;
	u8 short_preamble;
	u8 dot11_current_preamble_mode;
	u8 slot_time;

	bool autoload_fail_flag;

	short	epromtype;
	u16 eeprom_vid;
	u16 eeprom_did;
	u8 eeprom_customer_id;
	u16 eeprom_chnl_plan;

	u8 eeprom_tx_pwr_level_cck[14];
	u8 eeprom_tx_pwr_level_ofdm24g[14];
	u16 eeprom_ant_pwr_diff;
	u8 eeprom_thermal_meter;
	u8 eeprom_crystal_cap;

	u8 eeprom_legacy_ht_tx_pwr_diff;

	u8 crystal_cap;
	u8 thermal_meter[2];

	u8 sw_chnl_in_progress;
	u8 sw_chnl_stage;
	u8 sw_chnl_step;
	u8 set_bw_mode_in_progress;

	u8 n_cur_40mhz_prime_sc;

	u32 rf_reg_0value[4];
	u8 num_total_rf_path;
	bool brfpath_rxenable[4];

	bool tx_pwr_data_read_from_eeprom;

	u16 chnl_plan;
	u8 hw_rf_off_action;

	bool rf_change_in_progress;
	bool set_rf_pwr_state_in_progress;

	u8 cck_pwr_enl;
	u16 tssi_13dBm;
	u32 pwr_track;
	u8 cck_present_attn_20m_def;
	u8 cck_present_attn_40m_def;
	s8 cck_present_attn_diff;
	s8 cck_present_attn;
	long undecorated_smoothed_pwdb;

	u32 mcs_tx_pwr_level_org_offset[6];
	u8 tx_pwr_level_cck[14];
	u8 tx_pwr_level_ofdm_24g[14];
	u8 legacy_ht_tx_pwr_diff;
	u8 antenna_tx_pwr_diff[3];

	bool		dynamic_tx_high_pwr;
	bool		dynamic_tx_low_pwr;
	bool		last_dtp_flag_high;
	bool		last_dtp_flag_low;

	u8		rfa_txpowertrackingindex;
	u8		rfa_txpowertrackingindex_real;
	u8		rfa_txpowertracking_default;
	bool		btxpower_tracking;
	bool		bcck_in_ch14;

	u8		txpower_count;
	bool		tx_pwr_tracking_init;

	u8		ofdm_index[2];
	u8		cck_index;

	u8		rec_cck_20m_idx;
	u8		rec_cck_40m_idx;

	struct init_gain initgain_backup;
	u8		def_initial_gain[4];
	bool		bis_any_nonbepkts;
	bool		bcurrent_turbo_EDCA;
	bool		bis_cur_rdlstate;

	u32		rate_record;
	u32		rate_count_diff_rec;
	u32		continue_diff_count;
	bool		bswitch_fsync;
	u8		framesync;

	u16		tx_counter;
	u16		rx_ctr;
};

extern const struct ethtool_ops rtl819x_ethtool_ops;

u8 rtl92e_readb(struct net_device *dev, int x);
u32 rtl92e_readl(struct net_device *dev, int x);
u16 rtl92e_readw(struct net_device *dev, int x);
void rtl92e_writeb(struct net_device *dev, int x, u8 y);
void rtl92e_writew(struct net_device *dev, int x, u16 y);
void rtl92e_writel(struct net_device *dev, int x, u32 y);

void force_pci_posting(struct net_device *dev);

void rtl92e_rx_enable(struct net_device *dev);
void rtl92e_tx_enable(struct net_device *dev);

void rtl92e_hw_sleep_wq(void *data);
void rtl92e_commit(struct net_device *dev);

void rtl92e_check_rfctrl_gpio_timer(struct timer_list *t);

void rtl92e_hw_wakeup_wq(void *data);

void rtl92e_reset_desc_ring(struct net_device *dev);
void rtl92e_set_wireless_mode(struct net_device *dev, u8 wireless_mode);
void rtl92e_irq_enable(struct net_device *dev);
void rtl92e_config_rate(struct net_device *dev, u16 *rate_config);
void rtl92e_irq_disable(struct net_device *dev);

void rtl92e_update_rx_pkt_timestamp(struct net_device *dev,
				    struct rtllib_rx_stats *stats);
long rtl92e_translate_to_dbm(struct r8192_priv *priv, u8 signal_strength_index);
void rtl92e_update_rx_statistics(struct r8192_priv *priv,
				 struct rtllib_rx_stats *pprevious_stats);
u8 rtl92e_evm_db_to_percent(s8 value);
u8 rtl92e_rx_db_to_percent(s8 antpower);
void rtl92e_copy_mpdu_stats(struct rtllib_rx_stats *psrc_stats,
			    struct rtllib_rx_stats *ptarget_stats);
bool rtl92e_enable_nic(struct net_device *dev);

bool rtl92e_set_rf_state(struct net_device *dev,
			 enum rt_rf_power_state state_to_set,
			 RT_RF_CHANGE_SOURCE change_source);
#endif
