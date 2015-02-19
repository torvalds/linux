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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
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

#define IS_HARDWARE_TYPE_819xP(_priv)		\
	((((struct r8192_priv *)rtllib_priv(dev))->card_8192 == NIC_8190P) || \
	(((struct r8192_priv *)rtllib_priv(dev))->card_8192 == NIC_8192E))
#define IS_HARDWARE_TYPE_8192SE(_priv)		\
	(((struct r8192_priv *)rtllib_priv(dev))->card_8192 == NIC_8192SE)
#define IS_HARDWARE_TYPE_8192CE(_priv)		\
	(((struct r8192_priv *)rtllib_priv(dev))->card_8192 == NIC_8192CE)
#define IS_HARDWARE_TYPE_8192CU(_priv)		\
	(((struct r8192_priv *)rtllib_priv(dev))->card_8192 == NIC_8192CU)
#define IS_HARDWARE_TYPE_8192DE(_priv)		\
	(((struct r8192_priv *)rtllib_priv(dev))->card_8192 == NIC_8192DE)
#define IS_HARDWARE_TYPE_8192DU(_priv)		\
	(((struct r8192_priv *)rtllib_priv(dev))->card_8192 == NIC_8192DU)

#define RTL_PCI_DEVICE(vend, dev, cfg) \
	.vendor = (vend), .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = PCI_ANY_ID , \
	.driver_data = (kernel_ulong_t)&(cfg)

#define RTL_MAX_SCAN_SIZE 128

#define RTL_RATE_MAX		30

#define TOTAL_CAM_ENTRY		32
#define CAM_CONTENT_COUNT	8

#ifndef BIT
#define BIT(_i)				(1<<(_i))
#endif

#define IS_NIC_DOWN(priv)	(!(priv)->up)

#define IS_ADAPTER_SENDS_BEACON(dev) 0

#define IS_UNDER_11N_AES_MODE(_rtllib)		\
	((_rtllib->pHTInfo->bCurrentHTSupport == true) && \
	(_rtllib->pairwise_key_type == KEY_TYPE_CCMP))

#define HAL_MEMORY_MAPPED_IO_RANGE_8190PCI	0x1000
#define HAL_HW_PCI_REVISION_ID_8190PCI			0x00
#define HAL_MEMORY_MAPPED_IO_RANGE_8192PCIE	0x4000
#define HAL_HW_PCI_REVISION_ID_8192PCIE		0x01
#define HAL_MEMORY_MAPPED_IO_RANGE_8192SE	0x4000
#define HAL_HW_PCI_REVISION_ID_8192SE	0x10
#define HAL_HW_PCI_REVISION_ID_8192CE			0x1
#define HAL_MEMORY_MAPPED_IO_RANGE_8192CE	0x4000
#define HAL_HW_PCI_REVISION_ID_8192DE			0x0
#define HAL_MEMORY_MAPPED_IO_RANGE_8192DE	0x4000

#define HAL_HW_PCI_8180_DEVICE_ID			0x8180
#define HAL_HW_PCI_8185_DEVICE_ID			0x8185
#define HAL_HW_PCI_8188_DEVICE_ID			0x8188
#define HAL_HW_PCI_8198_DEVICE_ID			0x8198
#define HAL_HW_PCI_8190_DEVICE_ID			0x8190
#define HAL_HW_PCI_8192_DEVICE_ID			0x8192
#define HAL_HW_PCI_8192SE_DEVICE_ID			0x8192
#define HAL_HW_PCI_8174_DEVICE_ID			0x8174
#define HAL_HW_PCI_8173_DEVICE_ID			0x8173
#define HAL_HW_PCI_8172_DEVICE_ID			0x8172
#define HAL_HW_PCI_8171_DEVICE_ID			0x8171
#define HAL_HW_PCI_0045_DEVICE_ID			0x0045
#define HAL_HW_PCI_0046_DEVICE_ID			0x0046
#define HAL_HW_PCI_0044_DEVICE_ID			0x0044
#define HAL_HW_PCI_0047_DEVICE_ID			0x0047
#define HAL_HW_PCI_700F_DEVICE_ID			0x700F
#define HAL_HW_PCI_701F_DEVICE_ID			0x701F
#define HAL_HW_PCI_DLINK_DEVICE_ID			0x3304
#define HAL_HW_PCI_8192CET_DEVICE_ID			0x8191
#define HAL_HW_PCI_8192CE_DEVICE_ID			0x8178
#define HAL_HW_PCI_8191CE_DEVICE_ID			0x8177
#define HAL_HW_PCI_8188CE_DEVICE_ID			0x8176
#define HAL_HW_PCI_8192CU_DEVICE_ID			0x8191
#define HAL_HW_PCI_8192DE_DEVICE_ID			0x092D
#define HAL_HW_PCI_8192DU_DEVICE_ID			0x092D

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

#define scrclng				4

#define DEFAULT_FRAG_THRESHOLD	2342U
#define MIN_FRAG_THRESHOLD	256U
#define DEFAULT_BEACONINTERVAL	0x64U

#define DEFAULT_SSID		""
#define DEFAULT_RETRY_RTS	7
#define DEFAULT_RETRY_DATA	7
#define PRISM_HDR_SIZE		64

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

enum RTL819x_PHY_PARAM {
	RTL819X_PHY_MACPHY_REG			= 0,
	RTL819X_PHY_MACPHY_REG_PG		= 1,
	RTL8188C_PHY_MACREG			= 2,
	RTL8192C_PHY_MACREG			= 3,
	RTL819X_PHY_REG				= 4,
	RTL819X_PHY_REG_1T2R			= 5,
	RTL819X_PHY_REG_to1T1R			= 6,
	RTL819X_PHY_REG_to1T2R			= 7,
	RTL819X_PHY_REG_to2T2R			= 8,
	RTL819X_PHY_REG_PG			= 9,
	RTL819X_AGC_TAB				= 10,
	RTL819X_PHY_RADIO_A			= 11,
	RTL819X_PHY_RADIO_A_1T			= 12,
	RTL819X_PHY_RADIO_A_2T			= 13,
	RTL819X_PHY_RADIO_B			= 14,
	RTL819X_PHY_RADIO_B_GM			= 15,
	RTL819X_PHY_RADIO_C			= 16,
	RTL819X_PHY_RADIO_D			= 17,
	RTL819X_EEPROM_MAP			= 18,
	RTL819X_EFUSE_MAP			= 19,
};

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
	EEPROM_BOOT_EFUSE,
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

enum rf_step {
	RF_STEP_INIT = 0,
	RF_STEP_NORMAL,
	RF_STEP_MAX
};

enum rt_status {
	RT_STATUS_SUCCESS,
	RT_STATUS_FAILURE,
	RT_STATUS_PENDING,
	RT_STATUS_RESOURCE
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

enum ic_inferiority_8192s {
	IC_INFERIORITY_A	    = 0,
	IC_INFERIORITY_B	    = 1,
};

enum pci_bridge_vendor {
	PCI_BRIDGE_VENDOR_INTEL = 0x0,
	PCI_BRIDGE_VENDOR_ATI,
	PCI_BRIDGE_VENDOR_AMD,
	PCI_BRIDGE_VENDOR_SIS ,
	PCI_BRIDGE_VENDOR_UNKNOWN,
	PCI_BRIDGE_VENDOR_MAX ,
};

struct buffer {
	struct buffer *next;
	u32 *buf;
	dma_addr_t dma;

};

struct rtl_reg_debug {
	unsigned int  cmd;
	struct {
		unsigned char type;
		unsigned char addr;
		unsigned char page;
		unsigned char length;
	} head;
	unsigned char buf[0xff];
};

struct rt_tx_rahis {
	u32	     cck[4];
	u32	     ofdm[8];
	u32	     ht_mcs[4][16];
};

struct rt_smooth_data_4rf {
	char	elements[4][100];
	u32	index;
	u32	TotalNum;
	u32	TotalVal[4];
};

struct rt_stats {
	unsigned long txrdu;
	unsigned long rxrdu;
	unsigned long rxok;
	unsigned long rxframgment;
	unsigned long rxurberr;
	unsigned long rxstaterr;
	unsigned long rxdatacrcerr;
	unsigned long rxmgmtcrcerr;
	unsigned long rxcrcerrmin;
	unsigned long rxcrcerrmid;
	unsigned long rxcrcerrmax;
	unsigned long received_rate_histogram[4][32];
	unsigned long received_preamble_GI[2][32];
	unsigned long	rx_AMPDUsize_histogram[5];
	unsigned long rx_AMPDUnum_histogram[5];
	unsigned long numpacket_matchbssid;
	unsigned long numpacket_toself;
	unsigned long num_process_phyinfo;
	unsigned long numqry_phystatus;
	unsigned long numqry_phystatusCCK;
	unsigned long numqry_phystatusHT;
	unsigned long received_bwtype[5];
	unsigned long txnperr;
	unsigned long txnpdrop;
	unsigned long txresumed;
	unsigned long rxoverflow;
	unsigned long rxint;
	unsigned long txnpokint;
	unsigned long ints;
	unsigned long shints;
	unsigned long txoverflow;
	unsigned long txlpokint;
	unsigned long txlpdrop;
	unsigned long txlperr;
	unsigned long txbeokint;
	unsigned long txbedrop;
	unsigned long txbeerr;
	unsigned long txbkokint;
	unsigned long txbkdrop;
	unsigned long txbkerr;
	unsigned long txviokint;
	unsigned long txvidrop;
	unsigned long txvierr;
	unsigned long txvookint;
	unsigned long txvodrop;
	unsigned long txvoerr;
	unsigned long txbeaconokint;
	unsigned long txbeacondrop;
	unsigned long txbeaconerr;
	unsigned long txmanageokint;
	unsigned long txmanagedrop;
	unsigned long txmanageerr;
	unsigned long txcmdpktokint;
	unsigned long txdatapkt;
	unsigned long txfeedback;
	unsigned long txfeedbackok;
	unsigned long txoktotal;
	unsigned long txokbytestotal;
	unsigned long txokinperiod;
	unsigned long txmulticast;
	unsigned long txbytesmulticast;
	unsigned long txbroadcast;
	unsigned long txbytesbroadcast;
	unsigned long txunicast;
	unsigned long txbytesunicast;
	unsigned long rxbytesunicast;
	unsigned long txfeedbackfail;
	unsigned long txerrtotal;
	unsigned long txerrbytestotal;
	unsigned long txerrmulticast;
	unsigned long txerrbroadcast;
	unsigned long txerrunicast;
	unsigned long txretrycount;
	unsigned long txfeedbackretry;
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
	struct rt_tx_rahis txrate;
	u32 Slide_Beacon_pwdb[100];
	u32 Slide_Beacon_Total;
	struct rt_smooth_data_4rf cck_adc_pwdb;
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

enum two_port_status {
	TWO_PORT_STATUS__DEFAULT_ONLY,
	TWO_PORT_STATUS__EXTENSION_ONLY,
	TWO_PORT_STATUS__EXTENSION_FOLLOW_DEFAULT,
	TWO_PORT_STATUS__DEFAULT_G_EXTENSION_N20,
	TWO_PORT_STATUS__ADHOC,
	TWO_PORT_STATUS__WITHOUT_ANY_ASSOCIATE
};

struct txbbgain_struct {
	long	txbb_iq_amplifygain;
	u32	txbbgain_value;
};

struct ccktxbbgain {
	u8	ccktxbb_valuearray[8];
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
	struct delayed_work		initialgain_operate_wq;
	struct delayed_work		check_hw_scan_wq;
	struct delayed_work		hw_scan_simu_wq;
	struct delayed_work		start_hw_scan_wq;

	struct workqueue_struct		*priv_wq;

	struct channel_access_setting ChannelAccessSetting;

	struct mp_adapter NdisAdapter;

	struct rtl819x_ops			*ops;
	struct rtllib_device			*rtllib;

	struct work_struct				reset_wq;

	struct log_int_8190 InterruptLog;

	enum rt_customer_id CustomerID;


	enum rt_rf_type_819xu rf_chip;
	enum ic_inferiority_8192s IC_Class;
	enum ht_channel_width CurrentChannelBW;
	struct bb_reg_definition PHYRegDef[4];
	struct rate_adaptive rate_adaptive;

	struct ccktxbbgain cck_txbbgain_table[CCKTxBBGainTableLength];
	struct ccktxbbgain cck_txbbgain_ch14_table[CCKTxBBGainTableLength];

	struct txbbgain_struct txbbgain_table[TxBBGainTableLength];

	enum acm_method AcmMethod;

	struct rt_firmware			*pFirmware;
	enum rtl819x_loopback LoopbackMode;

	struct timer_list			watch_dog_timer;
	struct timer_list			fsync_timer;
	struct timer_list			gpio_polling_timer;

	spinlock_t				fw_scan_lock;
	spinlock_t				irq_lock;
	spinlock_t				irq_th_lock;
	spinlock_t				tx_lock;
	spinlock_t				rf_ps_lock;
	spinlock_t				rw_lock;
	spinlock_t				rt_h2c_lock;
	spinlock_t				rf_lock;
	spinlock_t				ps_lock;

	struct sk_buff_head		rx_queue;
	struct sk_buff_head		skb_queue;

	struct tasklet_struct		irq_rx_tasklet;
	struct tasklet_struct		irq_tx_tasklet;
	struct tasklet_struct		irq_prepare_beacon_tasklet;

	struct semaphore			wx_sem;
	struct semaphore			rf_sem;
	struct mutex				mutex;

	struct rt_stats stats;
	struct iw_statistics			wstats;
	struct proc_dir_entry		*dir_dev;

	short (*rf_set_sens)(struct net_device *dev, short sens);
	u8 (*rf_set_chan)(struct net_device *dev, u8 ch);
	void (*rf_close)(struct net_device *dev);
	void (*rf_init)(struct net_device *dev);

	struct rx_desc *rx_ring[MAX_RX_QUEUE];
	struct sk_buff	*rx_buf[MAX_RX_QUEUE][MAX_RX_COUNT];
	dma_addr_t	rx_ring_dma[MAX_RX_QUEUE];
	unsigned int	rx_idx[MAX_RX_QUEUE];
	int		rxringcount;
	u16		rxbuffersize;

	u64		LastRxDescTSF;

	u16		EarlyRxThreshold;
	u32		ReceiveConfig;
	u8		AcmControl;
	u8		RFProgType;
	u8		retry_data;
	u8		retry_rts;
	u16		rts;

	struct rtl8192_tx_ring tx_ring[MAX_TX_QUEUE_COUNT];
	int		 txringcount;
	int		txbuffsize;
	int		txfwbuffersize;
	atomic_t	tx_pending[0x10];

	u16		ShortRetryLimit;
	u16		LongRetryLimit;
	u32		TransmitConfig;
	u8		RegCWinMin;
	u8		keepAliveLevel;

	bool		sw_radio_on;
	bool		bHwRadioOff;
	bool		pwrdown;
	bool		blinked_ingpio;
	u8		polling_timer_on;

	/**********************************************************/

	enum card_type {
		PCI, MINIPCI,
		CARDBUS, USB
	} card_type;

	struct work_struct qos_activate;

	u8 bIbssCoordinator;

	short	promisc;
	short	crcmon;

	int txbeaconcount;

	short	chan;
	short	sens;
	short	max_sens;
	u32 rx_prevlen;

	u8 ScanDelay;
	bool ps_force;

	u32 irq_mask[2];

	u8 Rf_Mode;
	enum nic_t card_8192;
	u8 card_8192_version;

	short	enable_gpio0;

	u8 rf_type;
	u8 IC_Cut;
	char nick[IW_ESSID_MAX_SIZE + 1];

	u8 RegBcnCtrlVal;
	bool bHwAntDiv;

	bool bTKIPinNmodeFromReg;
	bool bWEPinNmodeFromReg;

	bool bLedOpenDrain;

	u8 check_roaming_cnt;

	bool bIgnoreSilentReset;
	u32 SilentResetRxSoltNum;
	u32 SilentResetRxSlotIndex;
	u32 SilentResetRxStuckEvent[MAX_SILENT_RESET_RX_SLOT_NUM];

	void *scan_cmd;
	u8 hwscan_bw_40;

	u16 nrxAMPDU_size;
	u8 nrxAMPDU_aggr_num;

	u32 last_rxdesc_tsf_high;
	u32 last_rxdesc_tsf_low;

	u16 basic_rate;
	u8 short_preamble;
	u8 dot11CurrentPreambleMode;
	u8 slot_time;
	u16 SifsTime;

	u8 RegWirelessMode;

	u8 firmware_version;
	u16 FirmwareSubVersion;
	u16 rf_pathmap;
	bool AutoloadFailFlag;

	u8 RegPciASPM;
	u8 RegAMDPciASPM;
	u8 RegHwSwRfOffD3;
	u8 RegSupportPciASPM;
	bool bSupportASPM;

	u32 RfRegChnlVal[2];

	u8 ShowRateMode;
	u8 RATRTableBitmap;

	u8 EfuseMap[2][HWSET_MAX_SIZE_92S];
	u16 EfuseUsedBytes;
	u8 EfuseUsedPercentage;

	short	epromtype;
	u16 eeprom_vid;
	u16 eeprom_did;
	u16 eeprom_svid;
	u16 eeprom_smid;
	u8 eeprom_CustomerID;
	u16 eeprom_ChannelPlan;
	u8 eeprom_version;

	u8 EEPROMRegulatory;
	u8 EEPROMPwrGroup[2][3];
	u8 EEPROMOptional;

	u8 EEPROMTxPowerLevelCCK[14];
	u8 EEPROMTxPowerLevelOFDM24G[14];
	u8 EEPROMTxPowerLevelOFDM5G[24];
	u8 EEPROMRfACCKChnl1TxPwLevel[3];
	u8 EEPROMRfAOfdmChnlTxPwLevel[3];
	u8 EEPROMRfCCCKChnl1TxPwLevel[3];
	u8 EEPROMRfCOfdmChnlTxPwLevel[3];
	u16 EEPROMTxPowerDiff;
	u16 EEPROMAntPwDiff;
	u8 EEPROMThermalMeter;
	u8 EEPROMPwDiff;
	u8 EEPROMCrystalCap;

	u8 EEPROMBluetoothCoexist;
	u8 EEPROMBluetoothType;
	u8 EEPROMBluetoothAntNum;
	u8 EEPROMBluetoothAntIsolation;
	u8 EEPROMBluetoothRadioShared;


	u8 EEPROMSupportWoWLAN;
	u8 EEPROMBoardType;
	u8 EEPROM_Def_Ver;
	u8 EEPROMHT2T_TxPwr[6];
	u8 EEPROMTSSI_A;
	u8 EEPROMTSSI_B;
	u8 EEPROMTxPowerLevelCCK_V1[3];
	u8 EEPROMLegacyHTTxPowerDiff;

	u8 BluetoothCoexist;

	u8 CrystalCap;
	u8 ThermalMeter[2];

	u16 FwCmdIOMap;
	u32 FwCmdIOParam;

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
	bool bChnlPlanFromHW;

	bool RegRfOff;
	bool isRFOff;
	bool bInPowerSaveMode;
	u8 bHwRfOffAction;

	bool aspm_clkreq_enable;
	u32 pci_bridge_vendor;
	u8 RegHostPciASPMSetting;
	u8 RegDevicePciASPMSetting;

	bool RFChangeInProgress;
	bool SetRFPowerStateInProgress;
	bool bdisable_nic;

	u8 pwrGroupCnt;

	u8 ThermalValue_LCK;
	u8 ThermalValue_IQK;
	bool bRfPiEnable;

	u32 APKoutput[2][2];
	bool bAPKdone;

	long RegE94;
	long RegE9C;
	long RegEB4;
	long RegEBC;

	u32 RegC04;
	u32 Reg874;
	u32 RegC08;
	u32 ADDA_backup[16];
	u32 IQK_MAC_backup[3];

	bool SetFwCmdInProgress;
	u8 CurrentFwCmdIO;

	u8 rssi_level;

	bool bInformFWDriverControlDM;
	u8 PwrGroupHT20[2][14];
	u8 PwrGroupHT40[2][14];

	u8 ThermalValue;
	long EntryMinUndecoratedSmoothedPWDB;
	long EntryMaxUndecoratedSmoothedPWDB;
	u8 DynamicTxHighPowerLvl;
	u8 LastDTPLvl;
	u32 CurrentRATR0;
	struct false_alarm_stats FalseAlmCnt;

	u8 DMFlag;
	u8 DM_Type;

	u8 CckPwEnl;
	u16 TSSI_13dBm;
	u32 Pwr_Track;
	u8 CCKPresentAttentuation_20Mdefault;
	u8 CCKPresentAttentuation_40Mdefault;
	char CCKPresentAttentuation_difference;
	char CCKPresentAttentuation;
	u8 bCckHighPower;
	long undecorated_smoothed_pwdb;
	long undecorated_smoothed_cck_adc_pwdb[4];

	u32 MCSTxPowerLevelOriginalOffset[6];
	u32 CCKTxPowerLevelOriginalOffset;
	u8 TxPowerLevelCCK[14];
	u8 TxPowerLevelCCK_A[14];
	u8 TxPowerLevelCCK_C[14];
	u8		TxPowerLevelOFDM24G[14];
	u8		TxPowerLevelOFDM5G[14];
	u8		TxPowerLevelOFDM24G_A[14];
	u8		TxPowerLevelOFDM24G_C[14];
	u8		LegacyHTTxPowerDiff;
	u8		TxPowerDiff;
	s8		RF_C_TxPwDiff;
	s8		RF_B_TxPwDiff;
	u8		RfTxPwrLevelCck[2][14];
	u8		RfTxPwrLevelOfdm1T[2][14];
	u8		RfTxPwrLevelOfdm2T[2][14];
	u8		AntennaTxPwDiff[3];
	u8		TxPwrHt20Diff[2][14];
	u8		TxPwrLegacyHtDiff[2][14];
	u8		TxPwrSafetyFlag;
	u8		HT2T_TxPwr_A[14];
	u8		HT2T_TxPwr_B[14];
	u8		CurrentCckTxPwrIdx;
	u8		CurrentOfdm24GTxPwrIdx;

	bool		bdynamic_txpower;
	bool		bDynamicTxHighPower;
	bool		bDynamicTxLowPower;
	bool		bLastDTPFlag_High;
	bool		bLastDTPFlag_Low;

	bool		bstore_last_dtpflag;
	bool		bstart_txctrl_bydtp;

	u8		rfa_txpowertrackingindex;
	u8		rfa_txpowertrackingindex_real;
	u8		rfa_txpowertracking_default;
	u8		rfc_txpowertrackingindex;
	u8		rfc_txpowertrackingindex_real;
	u8		rfc_txpowertracking_default;
	bool		btxpower_tracking;
	bool		bcck_in_ch14;

	u8		TxPowerTrackControl;
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

	bool		bCCKinCH14;

	u8		MidHighPwrTHR_L1;
	u8		MidHighPwrTHR_L2;

	bool		bfsync_processing;
	u32		rate_record;
	u32		rateCountDiffRecord;
	u32		ContinueDiffCount;
	bool		bswitch_fsync;
	u8		framesync;
	u32		framesyncC34;
	u8		framesyncMonitor;

	bool		bDMInitialGainEnable;
	bool		MutualAuthenticationFail;

	bool		bDisableFrameBursting;

	u32		reset_count;
	bool		bpbc_pressed;

	u32		txpower_checkcnt;
	u32		txpower_tracking_callback_cnt;
	u8		thermal_read_val[40];
	u8		thermal_readback_index;
	u32		ccktxpower_adjustcnt_not_ch14;
	u32		ccktxpower_adjustcnt_ch14;

	enum reset_type ResetProgress;
	bool		bForcedSilentReset;
	bool		bDisableNormalResetCheck;
	u16		TxCounter;
	u16		RxCounter;
	int		IrpPendingCount;
	bool		bResetInProgress;
	bool		force_reset;
	bool		force_lps;
	u8		InitialGainOperateType;

	bool		chan_forced;
	bool		bSingleCarrier;
	bool		RegBoard;
	bool		bCckContTx;
	bool		bOfdmContTx;
	bool		bStartContTx;
	u8		RegPaModel;
	u8		btMpCckTxPower;
	u8		btMpOfdmTxPower;

	u32		MptActType;
	u32		MptIoOffset;
	u32		MptIoValue;
	u32		MptRfPath;

	u32		MptBandWidth;
	u32		MptRateIndex;
	u8		MptChannelToSw;
	u32	MptRCR;

	u8		PwrDomainProtect;
	u8		H2CTxCmdSeq;


};

extern const struct ethtool_ops rtl819x_ethtool_ops;

void rtl8192_tx_cmd(struct net_device *dev, struct sk_buff *skb);
short rtl8192_tx(struct net_device *dev, struct sk_buff *skb);

u8 read_nic_io_byte(struct net_device *dev, int x);
u32 read_nic_io_dword(struct net_device *dev, int x);
u16 read_nic_io_word(struct net_device *dev, int x) ;
void write_nic_io_byte(struct net_device *dev, int x, u8 y);
void write_nic_io_word(struct net_device *dev, int x, u16 y);
void write_nic_io_dword(struct net_device *dev, int x, u32 y);

u8 read_nic_byte(struct net_device *dev, int x);
u32 read_nic_dword(struct net_device *dev, int x);
u16 read_nic_word(struct net_device *dev, int x) ;
void write_nic_byte(struct net_device *dev, int x, u8 y);
void write_nic_word(struct net_device *dev, int x, u16 y);
void write_nic_dword(struct net_device *dev, int x, u32 y);

void force_pci_posting(struct net_device *dev);

void rtl8192_rx_enable(struct net_device *);
void rtl8192_tx_enable(struct net_device *);

int rtl8192_hard_start_xmit(struct sk_buff *skb, struct net_device *dev);
void rtl8192_hard_data_xmit(struct sk_buff *skb, struct net_device *dev,
			    int rate);
void rtl8192_data_hard_stop(struct net_device *dev);
void rtl8192_data_hard_resume(struct net_device *dev);
void rtl8192_restart(void *data);
void rtl819x_watchdog_wqcallback(void *data);
void rtl8192_hw_sleep_wq(void *data);
void watch_dog_timer_callback(unsigned long data);
void rtl8192_irq_rx_tasklet(struct r8192_priv *priv);
void rtl8192_irq_tx_tasklet(struct r8192_priv *priv);
int rtl8192_down(struct net_device *dev, bool shutdownrf);
int rtl8192_up(struct net_device *dev);
void rtl8192_commit(struct net_device *dev);
void rtl8192_set_chan(struct net_device *dev, short ch);

void check_rfctrl_gpio_timer(unsigned long data);

void rtl8192_hw_wakeup_wq(void *data);
short rtl8192_pci_initdescring(struct net_device *dev);

void rtl8192_cancel_deferred_work(struct r8192_priv *priv);

int _rtl8192_up(struct net_device *dev, bool is_silent_reset);

short rtl8192_is_tx_queue_empty(struct net_device *dev);
void rtl8192_irq_disable(struct net_device *dev);

void rtl8192_tx_timeout(struct net_device *dev);
void rtl8192_pci_resetdescring(struct net_device *dev);
void rtl8192_SetWirelessMode(struct net_device *dev, u8 wireless_mode);
void rtl8192_irq_enable(struct net_device *dev);
void rtl8192_config_rate(struct net_device *dev, u16 *rate_config);
void rtl8192_update_cap(struct net_device *dev, u16 cap);
void rtl8192_irq_disable(struct net_device *dev);

void rtl819x_UpdateRxPktTimeStamp(struct net_device *dev,
				  struct rtllib_rx_stats *stats);
long rtl819x_translate_todbm(struct r8192_priv *priv, u8 signal_strength_index);
void rtl819x_update_rxsignalstatistics8190pci(struct r8192_priv *priv,
				      struct rtllib_rx_stats *pprevious_stats);
u8 rtl819x_evm_dbtopercentage(char value);
void rtl819x_process_cck_rxpathsel(struct r8192_priv *priv,
				   struct rtllib_rx_stats *pprevious_stats);
u8 rtl819x_query_rxpwrpercentage(char antpower);
void rtl8192_record_rxdesc_forlateruse(struct rtllib_rx_stats *psrc_stats,
				       struct rtllib_rx_stats *ptarget_stats);
bool NicIFEnableNIC(struct net_device *dev);
bool NicIFDisableNIC(struct net_device *dev);

bool MgntActSet_RF_State(struct net_device *dev,
			 enum rt_rf_power_state StateToSet,
			 RT_RF_CHANGE_SOURCE ChangeSource,
			 bool	ProtectOrNot);
void ActUpdateChannelAccessSetting(struct net_device *dev,
			   enum wireless_mode WirelessMode,
			   struct channel_access_setting *ChnlAccessSetting);

#endif
