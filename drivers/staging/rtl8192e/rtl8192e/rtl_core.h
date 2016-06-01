/******************************************************************************
 * Copyright(c) 2008 - 2010 Realtek Corporation. All rights reserved.
 *
 * Based on the r8180 driver, which is:
 * Copyright 2004-2005 Andrea Merello <andrea.merello@gmail.com>, et al.
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
******************************************************************************/

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

#define IS_HARDWARE_TYPE_8192SE(_priv)		\
	(((struct r8192_priv *)rtllib_priv(dev))->card_8192 == NIC_8192SE)

#define RTL_PCI_DEVICE(vend, dev, cfg) \
	.vendor = (vend), .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID, \
	.driver_data = (kernel_ulong_t)&(cfg)

#define TOTAL_CAM_ENTRY		32
#define CAM_CONTENT_COUNT	8

#define HAL_HW_PCI_REVISION_ID_8192PCIE		0x01
#define HAL_HW_PCI_REVISION_ID_8192SE	0x10

#define RTL819X_DEFAULT_RF_TYPE		RF_1T2R

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

#define RTL_IOCTL_WPA_SUPPLICANT		(SIOCIWFIRSTPRIV + 30)

#define TxBBGainTableLength			37
#define CCKTxBBGainTableLength			23

#define CHANNEL_PLAN_LEN			10
#define sCrcLng					4

#define NIC_SEND_HANG_THRESHOLD_NORMAL		4
#define NIC_SEND_HANG_THRESHOLD_POWERSAVE	8

#define MAX_TX_QUEUE				9

#define MAX_RX_QUEUE				1

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

enum rt_rf_type_819xu {
	RF_TYPE_MIN = 0,
	RF_8225,
	RF_8256,
	RF_8258,
	RF_6052 = 4,
	RF_PSEUDO_11N = 5,
};

enum rt_customer_id {
	RT_CID_DEFAULT	  = 0,
	RT_CID_8187_ALPHA0      = 1,
	RT_CID_8187_SERCOMM_PS  = 2,
	RT_CID_8187_HW_LED      = 3,
	RT_CID_8187_NETGEAR     = 4,
	RT_CID_WHQL	     = 5,
	RT_CID_819x_CAMEO       = 6,
	RT_CID_819x_RUNTOP      = 7,
	RT_CID_819x_Senao       = 8,
	RT_CID_TOSHIBA	  = 9,
	RT_CID_819x_Netcore     = 10,
	RT_CID_Nettronix	= 11,
	RT_CID_DLINK	    = 12,
	RT_CID_PRONET	   = 13,
	RT_CID_COREGA	   = 14,
	RT_CID_819x_ALPHA       = 15,
	RT_CID_819x_Sitecom     = 16,
	RT_CID_CCX	      = 17,
	RT_CID_819x_Lenovo      = 18,
	RT_CID_819x_QMI	 = 19,
	RT_CID_819x_Edimax_Belkin = 20,
	RT_CID_819x_Sercomm_Belkin = 21,
	RT_CID_819x_CAMEO1 = 22,
	RT_CID_819x_MSI = 23,
	RT_CID_819x_Acer = 24,
	RT_CID_819x_HP	= 27,
	RT_CID_819x_CLEVO = 28,
	RT_CID_819x_Arcadyan_Belkin = 29,
	RT_CID_819x_SAMSUNG = 30,
	RT_CID_819x_WNC_COREGA = 31,
};

enum reset_type {
	RESET_TYPE_NORESET = 0x00,
	RESET_TYPE_NORMAL = 0x01,
	RESET_TYPE_SILENT = 0x02
};

struct rt_stats {
	unsigned long rxrdu;
	unsigned long rxok;
	unsigned long rxdatacrcerr;
	unsigned long rxmgmtcrcerr;
	unsigned long rxcrcerrmin;
	unsigned long rxcrcerrmid;
	unsigned long rxcrcerrmax;
	unsigned long received_rate_histogram[4][32];
	unsigned long received_preamble_GI[2][32];
	unsigned long numpacket_matchbssid;
	unsigned long numpacket_toself;
	unsigned long num_process_phyinfo;
	unsigned long numqry_phystatus;
	unsigned long numqry_phystatusCCK;
	unsigned long numqry_phystatusHT;
	unsigned long received_bwtype[5];
	unsigned long rxoverflow;
	unsigned long rxint;
	unsigned long ints;
	unsigned long shints;
	unsigned long txoverflow;
	unsigned long txbeokint;
	unsigned long txbkokint;
	unsigned long txviokint;
	unsigned long txvookint;
	unsigned long txbeaconokint;
	unsigned long txbeaconerr;
	unsigned long txmanageokint;
	unsigned long txcmdpktokint;
	unsigned long txbytesmulticast;
	unsigned long txbytesbroadcast;
	unsigned long txbytesunicast;
	unsigned long rxbytesunicast;
	unsigned long txretrycount;
	u8	last_packet_rate;
	unsigned long slide_signal_strength[100];
	unsigned long slide_evm[100];
	unsigned long	slide_rssi_total;
	unsigned long slide_evm_total;
	long signal_strength;
	long signal_quality;
	long last_signal_strength_inpercent;
	long	recv_signal_power;
	u8 rx_rssi_percentage[4];
	u8 rx_evm_percentage[2];
	long rxSNRdB[4];
	u32 Slide_Beacon_pwdb[100];
	u32 Slide_Beacon_Total;
	u32	CurrentShowTxate;
};

struct channel_access_setting {
	u16 SIFS_Timer;
	u16 DIFS_Timer;
	u16 SlotTimeTimer;
	u16 EIFS_Timer;
	u16 CWminIndex;
	u16 CWmaxIndex;
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



struct rtl819x_ops {
	enum nic_t nic_type;
	void (*get_eeprom_size)(struct net_device *dev);
	void (*init_adapter_variable)(struct net_device *dev);
	void (*init_before_adapter_start)(struct net_device *dev);
	bool (*initialize_adapter)(struct net_device *dev);
	void (*link_change)(struct net_device *dev);
	void (*tx_fill_descriptor)(struct net_device *dev,
				   struct tx_desc *tx_desc,
				   struct cb_desc *cb_desc,
				   struct sk_buff *skb);
	void (*tx_fill_cmd_descriptor)(struct net_device *dev,
				       struct tx_desc_cmd *entry,
				       struct cb_desc *cb_desc,
				       struct sk_buff *skb);
	bool (*rx_query_status_descriptor)(struct net_device *dev,
					   struct rtllib_rx_stats *stats,
					   struct rx_desc *pdesc,
					   struct sk_buff *skb);
	bool (*rx_command_packet_handler)(struct net_device *dev,
					  struct sk_buff *skb,
					  struct rx_desc *pdesc);
	void (*stop_adapter)(struct net_device *dev, bool reset);
	void (*update_ratr_table)(struct net_device *dev);
	void (*irq_enable)(struct net_device *dev);
	void (*irq_disable)(struct net_device *dev);
	void (*irq_clear)(struct net_device *dev);
	void (*rx_enable)(struct net_device *dev);
	void (*tx_enable)(struct net_device *dev);
	void (*interrupt_recognized)(struct net_device *dev,
				     u32 *p_inta, u32 *p_intb);
	bool (*TxCheckStuckHandler)(struct net_device *dev);
	bool (*RxCheckStuckHandler)(struct net_device *dev);
};

struct r8192_priv {
	struct pci_dev *pdev;
	struct pci_dev *bridge_pdev;

	bool		bfirst_init;
	bool		bfirst_after_down;
	bool		initialized_at_probe;
	bool		being_init_adapter;
	bool		bDriverIsGoingToUnload;

	int		irq;
	short	irq_enabled;

	short	up;
	short	up_first_time;
	struct delayed_work		update_beacon_wq;
	struct delayed_work		watch_dog_wq;
	struct delayed_work		txpower_tracking_wq;
	struct delayed_work		rfpath_check_wq;
	struct delayed_work		gpio_change_rf_wq;

	struct channel_access_setting ChannelAccessSetting;

	struct rtl819x_ops			*ops;
	struct rtllib_device			*rtllib;

	struct work_struct				reset_wq;

	struct log_int_8190 InterruptLog;

	enum rt_customer_id CustomerID;


	enum rt_rf_type_819xu rf_chip;
	enum ht_channel_width CurrentChannelBW;
	struct bb_reg_definition PHYRegDef[4];
	struct rate_adaptive rate_adaptive;

	enum acm_method AcmMethod;

	struct rt_firmware			*pFirmware;
	enum rtl819x_loopback LoopbackMode;

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
	struct semaphore			rf_sem;
	struct mutex				mutex;

	struct rt_stats stats;
	struct iw_statistics			wstats;

	short (*rf_set_sens)(struct net_device *dev, short sens);
	u8 (*rf_set_chan)(struct net_device *dev, u8 ch);

	struct rx_desc *rx_ring[MAX_RX_QUEUE];
	struct sk_buff	*rx_buf[MAX_RX_QUEUE][MAX_RX_COUNT];
	dma_addr_t	rx_ring_dma[MAX_RX_QUEUE];
	unsigned int	rx_idx[MAX_RX_QUEUE];
	int		rxringcount;
	u16		rxbuffersize;

	u64		LastRxDescTSF;

	u32		ReceiveConfig;
	u8		retry_data;
	u8		retry_rts;
	u16		rts;

	struct rtl8192_tx_ring tx_ring[MAX_TX_QUEUE_COUNT];
	int		 txringcount;
	atomic_t	tx_pending[0x10];

	u16		ShortRetryLimit;
	u16		LongRetryLimit;

	bool		bHwRadioOff;
	bool		blinked_ingpio;
	u8		polling_timer_on;

	/**********************************************************/

	enum card_type {
		PCI, MINIPCI,
		CARDBUS, USB
	} card_type;

	struct work_struct qos_activate;

	short	promisc;

	short	chan;
	short	sens;
	short	max_sens;

	u8 ScanDelay;
	bool ps_force;

	u32 irq_mask[2];

	u8 Rf_Mode;
	enum nic_t card_8192;
	u8 card_8192_version;

	u8 rf_type;
	u8 IC_Cut;
	char nick[IW_ESSID_MAX_SIZE + 1];
	u8 check_roaming_cnt;

	u32 SilentResetRxSlotIndex;
	u32 SilentResetRxStuckEvent[MAX_SILENT_RESET_RX_SLOT_NUM];

	u16 basic_rate;
	u8 short_preamble;
	u8 dot11CurrentPreambleMode;
	u8 slot_time;
	u16 SifsTime;

	bool AutoloadFailFlag;

	short	epromtype;
	u16 eeprom_vid;
	u16 eeprom_did;
	u8 eeprom_CustomerID;
	u16 eeprom_ChannelPlan;

	u8 EEPROMTxPowerLevelCCK[14];
	u8 EEPROMTxPowerLevelOFDM24G[14];
	u8 EEPROMRfACCKChnl1TxPwLevel[3];
	u8 EEPROMRfAOfdmChnlTxPwLevel[3];
	u8 EEPROMRfCCCKChnl1TxPwLevel[3];
	u8 EEPROMRfCOfdmChnlTxPwLevel[3];
	u16 EEPROMAntPwDiff;
	u8 EEPROMThermalMeter;
	u8 EEPROMCrystalCap;

	u8 EEPROMLegacyHTTxPowerDiff;

	u8 CrystalCap;
	u8 ThermalMeter[2];

	u8 SwChnlInProgress;
	u8 SwChnlStage;
	u8 SwChnlStep;
	u8 SetBWModeInProgress;

	u8 nCur40MhzPrimeSC;

	u32 RfReg0Value[4];
	u8 NumTotalRFPath;
	bool brfpath_rxenable[4];

	bool bTXPowerDataReadFromEEPORM;

	u16 RegChannelPlan;
	u16 ChannelPlan;

	bool RegRfOff;
	bool isRFOff;
	bool bInPowerSaveMode;
	u8 bHwRfOffAction;

	bool RFChangeInProgress;
	bool SetRFPowerStateInProgress;
	bool bdisable_nic;

	u8 DM_Type;

	u8 CckPwEnl;
	u16 TSSI_13dBm;
	u32 Pwr_Track;
	u8 CCKPresentAttentuation_20Mdefault;
	u8 CCKPresentAttentuation_40Mdefault;
	s8 CCKPresentAttentuation_difference;
	s8 CCKPresentAttentuation;
	long undecorated_smoothed_pwdb;

	u32 MCSTxPowerLevelOriginalOffset[6];
	u8 TxPowerLevelCCK[14];
	u8 TxPowerLevelCCK_A[14];
	u8 TxPowerLevelCCK_C[14];
	u8		TxPowerLevelOFDM24G[14];
	u8		TxPowerLevelOFDM24G_A[14];
	u8		TxPowerLevelOFDM24G_C[14];
	u8		LegacyHTTxPowerDiff;
	s8		RF_C_TxPwDiff;
	u8		AntennaTxPwDiff[3];

	bool		bDynamicTxHighPower;
	bool		bDynamicTxLowPower;
	bool		bLastDTPFlag_High;
	bool		bLastDTPFlag_Low;

	u8		rfa_txpowertrackingindex;
	u8		rfa_txpowertrackingindex_real;
	u8		rfa_txpowertracking_default;
	u8		rfc_txpowertrackingindex;
	u8		rfc_txpowertrackingindex_real;
	bool		btxpower_tracking;
	bool		bcck_in_ch14;

	u8		txpower_count;
	bool		btxpower_trackingInit;

	u8		OFDM_index[2];
	u8		CCK_index;

	u8		Record_CCK_20Mindex;
	u8		Record_CCK_40Mindex;

	struct init_gain initgain_backup;
	u8		DefaultInitialGain[4];
	bool		bis_any_nonbepkts;
	bool		bcurrent_turbo_EDCA;
	bool		bis_cur_rdlstate;

	bool		bfsync_processing;
	u32		rate_record;
	u32		rateCountDiffRecord;
	u32		ContinueDiffCount;
	bool		bswitch_fsync;
	u8		framesync;
	u32		framesyncC34;
	u8		framesyncMonitor;

	u32		reset_count;

	enum reset_type ResetProgress;
	bool		bForcedSilentReset;
	bool		bDisableNormalResetCheck;
	u16		TxCounter;
	u16		RxCounter;
	bool		bResetInProgress;
	bool		force_reset;
	bool		force_lps;

	bool		chan_forced;

	u8		PwrDomainProtect;
	u8		H2CTxCmdSeq;
};

extern const struct ethtool_ops rtl819x_ethtool_ops;

u8 rtl92e_readb(struct net_device *dev, int x);
u32 rtl92e_readl(struct net_device *dev, int x);
u16 rtl92e_readw(struct net_device *dev, int x);
void rtl92e_writeb(struct net_device *dev, int x, u8 y);
void rtl92e_writew(struct net_device *dev, int x, u16 y);
void rtl92e_writel(struct net_device *dev, int x, u32 y);

void force_pci_posting(struct net_device *dev);

void rtl92e_rx_enable(struct net_device *);
void rtl92e_tx_enable(struct net_device *);

void rtl92e_hw_sleep_wq(void *data);
void rtl92e_commit(struct net_device *dev);

void rtl92e_check_rfctrl_gpio_timer(unsigned long data);

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
bool rtl92e_disable_nic(struct net_device *dev);

bool rtl92e_set_rf_state(struct net_device *dev,
			 enum rt_rf_power_state StateToSet,
			 RT_RF_CHANGE_SOURCE ChangeSource);
#endif
