/*
   This is part of rtl8180 OpenSource driver.
   Copyright (C) Andrea Merello 2004-2005  <andreamrl@tiscali.it>
   Released under the terms of GPL (General Public Licence)

   Parts of this driver are based on the GPL part of the
   official realtek driver

   Parts of this driver are based on the rtl8180 driver skeleton
   from Patric Schenke & Andres Salomon

   Parts of this driver are based on the Intel Pro Wireless 2100 GPL driver

   We want to tanks the Authors of those projects and the Ndiswrapper
   project Authors.
*/

#ifndef R8180H
#define R8180H


#define RTL8180_MODULE_NAME "r8180"
#define DMESG(x,a...) printk(KERN_INFO RTL8180_MODULE_NAME ": " x "\n", ## a)
#define DMESGW(x,a...) printk(KERN_WARNING RTL8180_MODULE_NAME ": WW:" x "\n", ## a)
#define DMESGE(x,a...) printk(KERN_WARNING RTL8180_MODULE_NAME ": EE:" x "\n", ## a)

#include <linux/module.h>
#include <linux/kernel.h>
//#include <linux/config.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/rtnetlink.h>	//for rtnl_lock()
#include <linux/wireless.h>
#include <linux/timer.h>
#include <linux/proc_fs.h>	// Necessary because we use the proc fs
#include <linux/if_arp.h>
#include "ieee80211.h"
#include <asm/io.h>
//#include <asm/semaphore.h>

#define EPROM_93c46 0
#define EPROM_93c56 1

#define RTL_IOCTL_WPA_SUPPLICANT		SIOCIWFIRSTPRIV+30

#define DEFAULT_FRAG_THRESHOLD 2342U
#define MIN_FRAG_THRESHOLD     256U
//#define	MAX_FRAG_THRESHOLD     2342U
#define DEFAULT_RTS_THRESHOLD 2342U
#define MIN_RTS_THRESHOLD 0U
#define MAX_RTS_THRESHOLD 2342U
#define DEFAULT_BEACONINTERVAL 0x64U
#define DEFAULT_BEACON_ESSID "Rtl8180"

#define DEFAULT_SSID ""
#define DEFAULT_RETRY_RTS 7
#define DEFAULT_RETRY_DATA 7
#define PRISM_HDR_SIZE 64

#ifdef CONFIG_RTL8185B

#define MGNT_QUEUE						0
#define BK_QUEUE						1
#define BE_QUEUE						2
#define VI_QUEUE						3
#define VO_QUEUE						4
#define HIGH_QUEUE						5
#define BEACON_QUEUE					6

#define LOW_QUEUE						BE_QUEUE
#define NORMAL_QUEUE					MGNT_QUEUE

#define aSifsTime 	10

#define sCrcLng         4
#define sAckCtsLng	112		// bits in ACK and CTS frames
//+by amy 080312
#define RATE_ADAPTIVE_TIMER_PERIOD	300

typedef enum _WIRELESS_MODE {
	WIRELESS_MODE_UNKNOWN = 0x00,
	WIRELESS_MODE_A = 0x01,
	WIRELESS_MODE_B = 0x02,
	WIRELESS_MODE_G = 0x04,
	WIRELESS_MODE_AUTO = 0x08,
} WIRELESS_MODE;

typedef enum _VERSION_8185{
	// RTL8185
	VERSION_8185_UNKNOWN,
	VERSION_8185_C, // C-cut
	VERSION_8185_D, // D-cut
	// RTL8185B
	VERSION_8185B_B, // B-cut
	VERSION_8185B_D, // D-cut
	VERSION_8185B_E, // E-cut
	//RTL8187S-PCIE
	VERSION_8187S_B, // B-cut
	VERSION_8187S_C, // C-cut
	VERSION_8187S_D, // D-cut

}VERSION_8185,*PVERSION_8185;
typedef struct 	ChnlAccessSetting {
	u16 SIFS_Timer;
	u16 DIFS_Timer;
	u16 SlotTimeTimer;
	u16 EIFS_Timer;
	u16 CWminIndex;
	u16 CWmaxIndex;
}*PCHANNEL_ACCESS_SETTING,CHANNEL_ACCESS_SETTING;

typedef enum{
        NIC_8185 = 1,
        NIC_8185B
        } nic_t;

typedef u32 AC_CODING;
#define AC0_BE	0		// ACI: 0x00	// Best Effort
#define AC1_BK	1		// ACI: 0x01	// Background
#define AC2_VI	2		// ACI: 0x10	// Video
#define AC3_VO	3		// ACI: 0x11	// Voice
#define AC_MAX	4		// Max: define total number; Should not to be used as a real enum.

//
// ECWmin/ECWmax field.
// Ref: WMM spec 2.2.2: WME Parameter Element, p.13.
//
typedef	union _ECW{
	u8	charData;
	struct
	{
		u8	ECWmin:4;
		u8	ECWmax:4;
	}f;	// Field
}ECW, *PECW;

//
// ACI/AIFSN Field.
// Ref: WMM spec 2.2.2: WME Parameter Element, p.12.
//
typedef	union _ACI_AIFSN{
	u8	charData;

	struct
	{
		u8	AIFSN:4;
		u8	ACM:1;
		u8	ACI:2;
		u8	Reserved:1;
	}f;	// Field
}ACI_AIFSN, *PACI_AIFSN;

//
// AC Parameters Record Format.
// Ref: WMM spec 2.2.2: WME Parameter Element, p.12.
//
typedef	union _AC_PARAM{
	u32	longData;
	u8	charData[4];

	struct
	{
		ACI_AIFSN	AciAifsn;
		ECW		Ecw;
		u16		TXOPLimit;
	}f;	// Field
}AC_PARAM, *PAC_PARAM;

/* it is a wrong definition. -xiong-2006-11-17
typedef struct ThreeWireReg {
	u16	longData;
	struct {
		u8	enableB;
		u8	data;
		u8	clk;
		u8	read_write;
	} struc;
} ThreeWireReg;
*/

typedef	union _ThreeWire{
	struct _ThreeWireStruc{
		u16		data:1;
		u16		clk:1;
		u16		enableB:1;
		u16		read_write:1;
		u16		resv1:12;
//		u2Byte	resv2:14;
//		u2Byte	ThreeWireEnable:1;
//		u2Byte	resv3:1;
	}struc;
	u16			longData;
}ThreeWireReg;

#endif

typedef struct buffer
{
	struct buffer *next;
	u32 *buf;
	dma_addr_t dma;
} buffer;

//YJ,modified,080828
typedef struct Stats
{
	unsigned long txrdu;
	unsigned long rxrdu;
	unsigned long rxnolast;
	unsigned long rxnodata;
//	unsigned long rxreset;
//	unsigned long rxwrkaround;
	unsigned long rxnopointer;
	unsigned long txnperr;
	unsigned long txresumed;
	unsigned long rxerr;
	unsigned long rxoverflow;
	unsigned long rxint;
	unsigned long txbkpokint;
	unsigned long txbepoking;
	unsigned long txbkperr;
	unsigned long txbeperr;
	unsigned long txnpokint;
	unsigned long txhpokint;
	unsigned long txhperr;
	unsigned long ints;
	unsigned long shints;
	unsigned long txoverflow;
	unsigned long rxdmafail;
	unsigned long txbeacon;
	unsigned long txbeaconerr;
	unsigned long txlpokint;
	unsigned long txlperr;
	unsigned long txretry;//retry number  tony 20060601
	unsigned long rxcrcerrmin;//crc error (0-500)
	unsigned long rxcrcerrmid;//crc error (500-1000)
	unsigned long rxcrcerrmax;//crc error (>1000)
	unsigned long rxicverr;//ICV error
} Stats;

#define MAX_LD_SLOT_NUM 10
#define KEEP_ALIVE_INTERVAL 				20 // in seconds.
#define CHECK_FOR_HANG_PERIOD			2 //be equal to watchdog check time
#define DEFAULT_KEEP_ALIVE_LEVEL			1
#define DEFAULT_SLOT_NUM					2
#define POWER_PROFILE_AC					0
#define POWER_PROFILE_BATTERY			1

typedef struct _link_detect_t
{
	u32				RxFrameNum[MAX_LD_SLOT_NUM];	// number of Rx Frame / CheckForHang_period  to determine link status
	u16				SlotNum;	// number of CheckForHang period to determine link status, default is 2
	u16				SlotIndex;

	u32				NumTxOkInPeriod;  //number of packet transmitted during CheckForHang
	u32				NumRxOkInPeriod;  //number of packet received during CheckForHang

	u8				IdleCount;     // (KEEP_ALIVE_INTERVAL / CHECK_FOR_HANG_PERIOD)
	u32				LastNumTxUnicast;
	u32				LastNumRxUnicast;

	bool				bBusyTraffic;    //when it is set to 1, UI cann't scan at will.
}link_detect_t, *plink_detect_t;

//YJ,modified,080828,end

//by amy for led
//================================================================================
// LED customization.
//================================================================================

typedef	enum _LED_STRATEGY_8185{
	SW_LED_MODE0, //
	SW_LED_MODE1, //
	HW_LED, // HW control 2 LEDs, LED0 and LED1 (there are 4 different control modes)
}LED_STRATEGY_8185, *PLED_STRATEGY_8185;
//by amy for led
//by amy for power save
typedef	enum _LED_CTL_MODE{
	LED_CTL_POWER_ON = 1,
	LED_CTL_LINK = 2,
	LED_CTL_NO_LINK = 3,
	LED_CTL_TX = 4,
	LED_CTL_RX = 5,
	LED_CTL_SITE_SURVEY = 6,
	LED_CTL_POWER_OFF = 7
}LED_CTL_MODE;

typedef	enum _RT_RF_POWER_STATE
{
	eRfOn,
	eRfSleep,
	eRfOff
}RT_RF_POWER_STATE;

enum	_ReasonCode{
	unspec_reason	= 0x1,
	auth_not_valid	= 0x2,
	deauth_lv_ss	= 0x3,
	inactivity		= 0x4,
	ap_overload		= 0x5,
	class2_err		= 0x6,
	class3_err		= 0x7,
	disas_lv_ss		= 0x8,
	asoc_not_auth	= 0x9,

	//----MIC_CHECK
	mic_failure		= 0xe,
	//----END MIC_CHECK

	// Reason code defined in 802.11i D10.0 p.28.
	invalid_IE		= 0x0d,
	four_way_tmout	= 0x0f,
	two_way_tmout	= 0x10,
	IE_dismatch		= 0x11,
	invalid_Gcipher	= 0x12,
	invalid_Pcipher	= 0x13,
	invalid_AKMP	= 0x14,
	unsup_RSNIEver = 0x15,
	invalid_RSNIE	= 0x16,
	auth_802_1x_fail= 0x17,
	ciper_reject		= 0x18,

	// Reason code defined in 7.3.1.7, 802.1e D13.0, p.42. Added by Annie, 2005-11-15.
	QoS_unspec		= 0x20,	// 32
	QAP_bandwidth	= 0x21,	// 33
	poor_condition	= 0x22,	// 34
	no_facility		= 0x23,	// 35
							// Where is 36???
	req_declined	= 0x25,	// 37
	invalid_param	= 0x26,	// 38
	req_not_honored= 0x27,	// 39
	TS_not_created	= 0x2F,	// 47
	DL_not_allowed	= 0x30,	// 48
	dest_not_exist	= 0x31,	// 49
	dest_not_QSTA	= 0x32,	// 50
};
typedef	enum _RT_PS_MODE
{
	eActive,	// Active/Continuous access.
	eMaxPs,		// Max power save mode.
	eFastPs		// Fast power save mode.
}RT_PS_MODE;
//by amy for power save
typedef struct r8180_priv
{
	struct pci_dev *pdev;

	short epromtype;
	int irq;
	struct ieee80211_device *ieee80211;

        short card_8185; /* O: rtl8180, 1:rtl8185 V B/C, 2:rtl8185 V D, 3:rtl8185B */
	short card_8185_Bversion; /* if TCR reports card V B/C this discriminates */
	short phy_ver; /* meaningful for rtl8225 1:A 2:B 3:C */
	short enable_gpio0;
	enum card_type {PCI,MINIPCI,CARDBUS,USB/*rtl8187*/}card_type;
	short hw_plcp_len;
	short plcp_preamble_mode; // 0:auto 1:short 2:long

	spinlock_t irq_lock;
	spinlock_t irq_th_lock;
	spinlock_t tx_lock;
	spinlock_t ps_lock;
	spinlock_t rf_ps_lock;

	u16 irq_mask;
	short irq_enabled;
	struct net_device *dev;
	short chan;
	short sens;
	short max_sens;
	u8 chtxpwr[15]; //channels from 1 to 14, 0 not used
	u8 chtxpwr_ofdm[15]; //channels from 1 to 14, 0 not used
	//u8 challow[15]; //channels from 1 to 14, 0 not used
	u8 channel_plan;  // it's the channel plan index
	short up;
	short crcmon; //if 1 allow bad crc frame reception in monitor mode
	short prism_hdr;

	struct timer_list scan_timer;
	/*short scanpending;
	short stopscan;*/
	spinlock_t scan_lock;
	u8 active_probe;
	//u8 active_scan_num;
	struct semaphore wx_sem;
	struct semaphore rf_state;
	short hw_wep;

	short digphy;
	short antb;
	short diversity;
	u8 cs_treshold;
	short rcr_csense;
	short rf_chip;
	u32 key0[4];
	short (*rf_set_sens)(struct net_device *dev,short sens);
	void (*rf_set_chan)(struct net_device *dev,short ch);
	void (*rf_close)(struct net_device *dev);
	void (*rf_init)(struct net_device *dev);
	void (*rf_sleep)(struct net_device *dev);
	void (*rf_wakeup)(struct net_device *dev);
	//short rate;
	short promisc;
	/*stats*/
	struct Stats stats;
	struct _link_detect_t link_detect;  //YJ,add,080828
	struct iw_statistics wstats;
	struct proc_dir_entry *dir_dev;

	/*RX stuff*/
	u32 *rxring;
	u32 *rxringtail;
	dma_addr_t rxringdma;
	struct buffer *rxbuffer;
	struct buffer *rxbufferhead;
	int rxringcount;
	u16 rxbuffersize;

	struct sk_buff *rx_skb;

	short rx_skb_complete;

	u32 rx_prevlen;

	/*TX stuff*/
/*
	u32 *txlpring;
	u32 *txhpring;
	u32 *txnpring;
	dma_addr_t txlpringdma;
	dma_addr_t txhpringdma;
	dma_addr_t txnpringdma;
	u32 *txlpringtail;
	u32 *txhpringtail;
	u32 *txnpringtail;
	u32 *txlpringhead;
	u32 *txhpringhead;
	u32 *txnpringhead;
	struct buffer *txlpbufs;
	struct buffer *txhpbufs;
	struct buffer *txnpbufs;
	struct buffer *txlpbufstail;
	struct buffer *txhpbufstail;
	struct buffer *txnpbufstail;
*/
	u32 *txmapring;
	u32 *txbkpring;
	u32 *txbepring;
	u32 *txvipring;
	u32 *txvopring;
	u32 *txhpring;
	dma_addr_t txmapringdma;
	dma_addr_t txbkpringdma;
	dma_addr_t txbepringdma;
	dma_addr_t txvipringdma;
	dma_addr_t txvopringdma;
	dma_addr_t txhpringdma;
	u32 *txmapringtail;
	u32 *txbkpringtail;
	u32 *txbepringtail;
	u32 *txvipringtail;
	u32 *txvopringtail;
	u32 *txhpringtail;
	u32 *txmapringhead;
	u32 *txbkpringhead;
	u32 *txbepringhead;
	u32 *txvipringhead;
	u32 *txvopringhead;
	u32 *txhpringhead;
	struct buffer *txmapbufs;
	struct buffer *txbkpbufs;
	struct buffer *txbepbufs;
	struct buffer *txvipbufs;
	struct buffer *txvopbufs;
	struct buffer *txhpbufs;
	struct buffer *txmapbufstail;
	struct buffer *txbkpbufstail;
	struct buffer *txbepbufstail;
	struct buffer *txvipbufstail;
	struct buffer *txvopbufstail;
	struct buffer *txhpbufstail;

	int txringcount;
	int txbuffsize;
	//struct tx_pendingbuf txnp_pending;
	//struct tasklet_struct irq_tx_tasklet;
	struct tasklet_struct irq_rx_tasklet;
	u8 dma_poll_mask;
	//short tx_suspend;

	/* adhoc/master mode stuff */
	u32 *txbeaconringtail;
	dma_addr_t txbeaconringdma;
	u32 *txbeaconring;
	int txbeaconcount;
	struct buffer *txbeaconbufs;
	struct buffer *txbeaconbufstail;
	//char *master_essid;
	//u16 master_beaconinterval;
	//u32 master_beaconsize;
	//u16 beacon_interval;

	u8 retry_data;
	u8 retry_rts;
	u16 rts;

//add for RF power on power off by lizhaoming 080512
	u8	 RegThreeWireMode; // See "Three wire mode" defined above, 2006.05.31, by rcnjko.

//by amy for led
	LED_STRATEGY_8185 LedStrategy;
//by amy for led

//by amy for power save
	struct timer_list watch_dog_timer;
	bool bInactivePs;
	bool bSwRfProcessing;
	RT_RF_POWER_STATE	eInactivePowerState;
	RT_RF_POWER_STATE eRFPowerState;
	u32 RfOffReason;
	bool RFChangeInProgress;
	bool bInHctTest;
	bool SetRFPowerStateInProgress;
	u8   RFProgType;
	bool bLeisurePs;
	RT_PS_MODE dot11PowerSaveMode;
	//u32 NumRxOkInPeriod;   //YJ,del,080828
	//u32 NumTxOkInPeriod;   //YJ,del,080828
	u8   TxPollingTimes;

	bool	bApBufOurFrame;// TRUE if AP buffer our unicast data , we will keep eAwake untill receive data or timeout.
	u8	WaitBufDataBcnCount;
	u8	WaitBufDataTimeOut;

//by amy for power save
//by amy for antenna
	u8 EEPROMSwAntennaDiversity;
	bool EEPROMDefaultAntenna1;
	u8 RegSwAntennaDiversityMechanism;
	bool bSwAntennaDiverity;
	u8 RegDefaultAntenna;
	bool bDefaultAntenna1;
	u8 SignalStrength;
	long Stats_SignalStrength;
	long LastSignalStrengthInPercent; // In percentange, used for smoothing, e.g. Moving Average.
	u8	 SignalQuality; // in 0-100 index.
	long Stats_SignalQuality;
	long RecvSignalPower; // in dBm.
	long Stats_RecvSignalPower;
	u8	 LastRxPktAntenna;	// +by amy 080312 Antenn which received the lasted packet. 0: Aux, 1:Main. Added by Roger, 2008.01.25.
	u32 AdRxOkCnt;
	long AdRxSignalStrength;
	u8 CurrAntennaIndex;			// Index to current Antenna (both Tx and Rx).
	u8 AdTickCount;				// Times of SwAntennaDiversityTimer happened.
	u8 AdCheckPeriod;				// # of period SwAntennaDiversityTimer to check Rx signal strength for SW Antenna Diversity.
	u8 AdMinCheckPeriod;			// Min value of AdCheckPeriod.
	u8 AdMaxCheckPeriod;			// Max value of AdCheckPeriod.
	long AdRxSsThreshold;			// Signal strength threshold to switch antenna.
	long AdMaxRxSsThreshold;			// Max value of AdRxSsThreshold.
	bool bAdSwitchedChecking;		// TRUE if we shall shall check Rx signal strength for last time switching antenna.
	long AdRxSsBeforeSwitched;		// Rx signal strength before we swithed antenna.
	struct timer_list SwAntennaDiversityTimer;
//by amy for antenna
//{by amy 080312
//
	// Crystal calibration.
	// Added by Roger, 2007.12.11.
	//
	bool		bXtalCalibration; // Crystal calibration.
	u8			XtalCal_Xin; // Crystal calibration for Xin. 0~7.5pF
	u8			XtalCal_Xout; // Crystal calibration for Xout. 0~7.5pF
	//
	// Tx power tracking with thermal meter indication.
	// Added by Roger, 2007.12.11.
	//
	bool		bTxPowerTrack; // Tx Power tracking.
	u8			ThermalMeter; // Thermal meter reference indication.
	//
	// Dynamic Initial Gain Adjustment Mechanism. Added by Bruce, 2007-02-14.
	//
	bool				bDigMechanism; // TRUE if DIG is enabled, FALSE ow.
	bool				bRegHighPowerMechanism; // For High Power Mechanism. 061010, by rcnjko.
	u32					FalseAlarmRegValue;
	u8					RegDigOfdmFaUpTh; // Upper threhold of OFDM false alarm, which is used in DIG.
	u8					DIG_NumberFallbackVote;
	u8					DIG_NumberUpgradeVote;
	// For HW antenna diversity, added by Roger, 2008.01.30.
	u32			AdMainAntennaRxOkCnt;		// Main antenna Rx OK count.
	u32			AdAuxAntennaRxOkCnt;		// Aux antenna Rx OK count.
	bool		bHWAdSwitched;				// TRUE if we has switched default antenna by HW evaluation.
	// RF High Power upper/lower threshold.
	u8					RegHiPwrUpperTh;
	u8					RegHiPwrLowerTh;
	// RF RSSI High Power upper/lower Threshold.
	u8					RegRSSIHiPwrUpperTh;
	u8					RegRSSIHiPwrLowerTh;
	// Current CCK RSSI value to determine CCK high power, asked by SD3 DZ, by Bruce, 2007-04-12.
	u8			CurCCKRSSI;
	bool        bCurCCKPkt;
	//
	// High Power Mechanism. Added by amy, 080312.
	//
	bool					bToUpdateTxPwr;
	long					UndecoratedSmoothedSS;
	long					UndercorateSmoothedRxPower;
	u8						RSSI;
	char					RxPower;
	 u8 InitialGain;
	 //For adjust Dig Threshhold during Legacy/Leisure Power Save Mode
	u32				DozePeriodInPast2Sec;
	 // Don't access BB/RF under disable PLL situation.
	u8					InitialGainBackUp;
	 u8 RegBModeGainStage;
//by amy for rate adaptive
    struct timer_list rateadapter_timer;
	u32    RateAdaptivePeriod;
	bool   bEnhanceTxPwr;
	bool   bUpdateARFR;
	int	   ForcedDataRate; // Force Data Rate. 0: Auto, 0x02: 1M ~ 0x6C: 54M.)
	u32     NumTxUnicast; //YJ,add,080828,for keep alive
	u8      keepAliveLevel; //YJ,add,080828,for KeepAlive
	unsigned long 	NumTxOkTotal;
	u16                                 LastRetryCnt;
        u16                                     LastRetryRate;
        unsigned long       LastTxokCnt;
        unsigned long           LastRxokCnt;
        u16                                     CurrRetryCnt;
        unsigned long           LastTxOKBytes;
	unsigned long 		    NumTxOkBytesTotal;
        u8                          LastFailTxRate;
        long                        LastFailTxRateSS;
        u8                          FailTxRateCount;
        u32                         LastTxThroughput;
        //for up rate
        unsigned short          bTryuping;
        u8                                      CurrTxRate;     //the rate before up
        u16                                     CurrRetryRate;
        u16                                     TryupingCount;
        u8                                      TryDownCountLowData;
        u8                                      TryupingCountNoData;

        u8                  CurrentOperaRate;
//by amy for rate adaptive
//by amy 080312}
//	short wq_hurryup;
//	struct workqueue_struct *workqueue;
	struct work_struct reset_wq;
	struct work_struct watch_dog_wq;
	struct work_struct tx_irq_wq;
	short ack_tx_to_ieee;

	u8 PowerProfile;
#ifdef CONFIG_RTL8185B
	u32 CSMethod;
	u8 cck_txpwr_base;
	u8 ofdm_txpwr_base;
	u8 dma_poll_stop_mask;

	//u8 RegThreeWireMode;
	u8 MWIEnable;
	u16 ShortRetryLimit;
	u16 LongRetryLimit;
	u16 EarlyRxThreshold;
	u32 TransmitConfig;
	u32 ReceiveConfig;
	u32 IntrMask;

	struct 	ChnlAccessSetting  ChannelAccessSetting;
#endif
}r8180_priv;

#define MANAGE_PRIORITY 0
#define BK_PRIORITY 1
#define BE_PRIORITY 2
#define VI_PRIORITY 3
#define VO_PRIORITY 4
#define HI_PRIORITY 5
#define BEACON_PRIORITY 6

#define LOW_PRIORITY VI_PRIORITY
#define NORM_PRIORITY VO_PRIORITY
//AC2Queue mapping
#define AC2Q(_ac) (((_ac) == WME_AC_VO) ? VO_PRIORITY : \
		((_ac) == WME_AC_VI) ? VI_PRIORITY : \
		((_ac) == WME_AC_BK) ? BK_PRIORITY : \
		BE_PRIORITY)

short rtl8180_tx(struct net_device *dev,u8* skbuf, int len,int priority,
	short morefrag,short fragdesc,int rate);

u8 read_nic_byte(struct net_device *dev, int x);
u32 read_nic_dword(struct net_device *dev, int x);
u16 read_nic_word(struct net_device *dev, int x) ;
void write_nic_byte(struct net_device *dev, int x,u8 y);
void write_nic_word(struct net_device *dev, int x,u16 y);
void write_nic_dword(struct net_device *dev, int x,u32 y);
void force_pci_posting(struct net_device *dev);

void rtl8180_rtx_disable(struct net_device *);
void rtl8180_rx_enable(struct net_device *);
void rtl8180_tx_enable(struct net_device *);
void rtl8180_start_scanning(struct net_device *dev);
void rtl8180_start_scanning_s(struct net_device *dev);
void rtl8180_stop_scanning(struct net_device *dev);
void rtl8180_disassociate(struct net_device *dev);
//void fix_rx_fifo(struct net_device *dev);
void rtl8180_set_anaparam(struct net_device *dev,u32 a);
void rtl8185_set_anaparam2(struct net_device *dev,u32 a);
void rtl8180_set_hw_wep(struct net_device *dev);
void rtl8180_no_hw_wep(struct net_device *dev);
void rtl8180_update_msr(struct net_device *dev);
//void rtl8180_BSS_create(struct net_device *dev);
void rtl8180_beacon_tx_disable(struct net_device *dev);
void rtl8180_beacon_rx_disable(struct net_device *dev);
void rtl8180_conttx_enable(struct net_device *dev);
void rtl8180_conttx_disable(struct net_device *dev);
int rtl8180_down(struct net_device *dev);
int rtl8180_up(struct net_device *dev);
void rtl8180_commit(struct net_device *dev);
void rtl8180_set_chan(struct net_device *dev,short ch);
void rtl8180_set_master_essid(struct net_device *dev,char *essid);
void rtl8180_update_beacon_security(struct net_device *dev);
void write_phy(struct net_device *dev, u8 adr, u8 data);
void write_phy_cck(struct net_device *dev, u8 adr, u32 data);
void write_phy_ofdm(struct net_device *dev, u8 adr, u32 data);
void rtl8185_tx_antenna(struct net_device *dev, u8 ant);
void rtl8185_rf_pins_enable(struct net_device *dev);
void IBSS_randomize_cell(struct net_device *dev);
void IPSEnter(struct net_device *dev);
void IPSLeave(struct net_device *dev);
int get_curr_tx_free_desc(struct net_device *dev, int priority);
void UpdateInitialGain(struct net_device *dev);
bool SetAntennaConfig87SE(struct net_device *dev, u8  DefaultAnt, bool bAntDiversity);

//#ifdef CONFIG_RTL8185B
void rtl8185b_adapter_start(struct net_device *dev);
void rtl8185b_rx_enable(struct net_device *dev);
void rtl8185b_tx_enable(struct net_device *dev);
void rtl8180_reset(struct net_device *dev);
void rtl8185b_irq_enable(struct net_device *dev);
void fix_rx_fifo(struct net_device *dev);
void fix_tx_fifo(struct net_device *dev);
void rtl8225z2_SetTXPowerLevel(struct net_device *dev, short ch);
#if LINUX_VERSION_CODE >=KERNEL_VERSION(2,6,20)
void rtl8180_rate_adapter(struct work_struct * work);
#else
void rtl8180_rate_adapter(struct net_device *dev);
#endif
//#endif
bool MgntActSet_RF_State(struct net_device *dev, RT_RF_POWER_STATE StateToSet, u32 ChangeSource);

#endif
