/*
   This is part of rtl8187 OpenSource driver.
   Copyright (C) Andrea Merello 2004-2005  <andreamrl@tiscali.it>
   Released under the terms of GPL (General Public Licence)

   Parts of this driver are based on the GPL part of the
   official realtek driver

   Parts of this driver are based on the rtl8192 driver skeleton
   from Patric Schenke & Andres Salomon

   Parts of this driver are based on the Intel Pro Wireless 2100 GPL driver

   We want to tanks the Authors of those projects and the Ndiswrapper
   project Authors.
*/

#ifndef R819xU_H
#define R819xU_H

#include <linux/module.h>
#include <linux/kernel.h>
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
#include <linux/random.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include "ieee80211/rtl819x_HT.h"
#include "ieee80211/ieee80211.h"




#define RTL819xE_MODULE_NAME "rtl819xE"

#define FALSE 0
#define TRUE 1
#define MAX_KEY_LEN     61
#define KEY_BUF_SIZE    5

#define BIT0            0x00000001
#define BIT1            0x00000002
#define BIT2            0x00000004
#define BIT3            0x00000008
#define BIT4            0x00000010
#define BIT5            0x00000020
#define BIT6            0x00000040
#define BIT7            0x00000080
#define BIT8            0x00000100
#define BIT9            0x00000200
#define BIT10           0x00000400
#define BIT11           0x00000800
#define BIT12           0x00001000
#define BIT13           0x00002000
#define BIT14           0x00004000
#define BIT15           0x00008000
#define BIT16           0x00010000
#define BIT17           0x00020000
#define BIT18           0x00040000
#define BIT19           0x00080000
#define BIT20           0x00100000
#define BIT21           0x00200000
#define BIT22           0x00400000
#define BIT23           0x00800000
#define BIT24           0x01000000
#define BIT25           0x02000000
#define BIT26           0x04000000
#define BIT27           0x08000000
#define BIT28           0x10000000
#define BIT29           0x20000000
#define BIT30           0x40000000
#define BIT31           0x80000000
// Rx smooth factor
#define	Rx_Smooth_Factor		20
/* 2007/06/04 MH Define sliding window for RSSI history. */
#define		PHY_RSSI_SLID_WIN_MAX				100
#define		PHY_Beacon_RSSI_SLID_WIN_MAX		10

#define IC_VersionCut_D	0x3
#define IC_VersionCut_E	0x4

#if 0 //we need to use RT_TRACE instead DMESG as RT_TRACE will clearly show debug level wb.
#define DMESG(x,a...) printk(KERN_INFO RTL819xE_MODULE_NAME ": " x "\n", ## a)
#else
#define DMESG(x,a...)
extern u32 rt_global_debug_component;
#define RT_TRACE(component, x, args...) \
do { if(rt_global_debug_component & component) \
	printk(KERN_DEBUG RTL819xE_MODULE_NAME ":" x , \
	       ##args);\
}while(0);

#define COMP_TRACE				BIT0		// For function call tracing.
#define COMP_DBG				BIT1		// Only for temporary debug message.
#define COMP_INIT				BIT2		// during driver initialization / halt / reset.


#define COMP_RECV				BIT3		// Reveive part data path.
#define COMP_SEND				BIT4		// Send part path.
#define COMP_IO					BIT5		// I/O Related. Added by Annie, 2006-03-02.
#define COMP_POWER				BIT6		// 802.11 Power Save mode or System/Device Power state related.
#define COMP_EPROM				BIT7		// 802.11 link related: join/start BSS, leave BSS.
#define COMP_SWBW				BIT8	// For bandwidth switch.
#define COMP_SEC				BIT9// For Security.


#define COMP_TURBO				BIT10	// For Turbo Mode related. By Annie, 2005-10-21.
#define COMP_QOS				BIT11	// For QoS.

#define COMP_RATE				BIT12	// For Rate Adaptive mechanism, 2006.07.02, by rcnjko. #define COMP_EVENTS				0x00000080	// Event handling
#define COMP_RXDESC			        BIT13	// Show Rx desc information for SD3 debug. Added by Annie, 2006-07-15.
#define COMP_PHY				BIT14
#define COMP_DIG				BIT15	// For DIG, 2006.09.25, by rcnjko.
#define COMP_TXAGC				BIT16	// For Tx power, 060928, by rcnjko.
#define COMP_HALDM				BIT17	// For HW Dynamic Mechanism, 061010, by rcnjko.
#define COMP_POWER_TRACKING	                BIT18	//FOR 8190 TX POWER TRACKING
#define COMP_EVENTS			        BIT19	// Event handling

#define COMP_RF					BIT20	// For RF.

/* 11n or 8190 specific code should be put below this line */


#define COMP_FIRMWARE			        BIT21	//for firmware downloading
#define COMP_HT					BIT22	// For 802.11n HT related information. by Emily 2006-8-11

#define COMP_RESET				BIT23
#define COMP_CMDPKT			        BIT24
#define COMP_SCAN				BIT25
#define COMP_IPS				BIT26
#define COMP_DOWN				BIT27  // for rm driver module
#define COMP_INTR 				BIT28  // for interrupt
#define COMP_ERR				BIT31  // for error out, always on
#endif


//
// Queue Select Value in TxDesc
//
#define QSLT_BK                                 0x1
#define QSLT_BE                                 0x0
#define QSLT_VI                                 0x4
#define QSLT_VO                                 0x6
#define QSLT_BEACON                             0x10
#define QSLT_HIGH                               0x11
#define QSLT_MGNT                               0x12
#define QSLT_CMD                                0x13

#define DESC90_RATE1M                           0x00
#define DESC90_RATE2M                           0x01
#define DESC90_RATE5_5M                         0x02
#define DESC90_RATE11M                          0x03
#define DESC90_RATE6M                           0x04
#define DESC90_RATE9M                           0x05
#define DESC90_RATE12M                          0x06
#define DESC90_RATE18M                          0x07
#define DESC90_RATE24M                          0x08
#define DESC90_RATE36M                          0x09
#define DESC90_RATE48M                          0x0a
#define DESC90_RATE54M                          0x0b
#define DESC90_RATEMCS0                         0x00
#define DESC90_RATEMCS1                         0x01
#define DESC90_RATEMCS2                         0x02
#define DESC90_RATEMCS3                         0x03
#define DESC90_RATEMCS4                         0x04
#define DESC90_RATEMCS5                         0x05
#define DESC90_RATEMCS6                         0x06
#define DESC90_RATEMCS7                         0x07
#define DESC90_RATEMCS8                         0x08
#define DESC90_RATEMCS9                         0x09
#define DESC90_RATEMCS10                        0x0a
#define DESC90_RATEMCS11                        0x0b
#define DESC90_RATEMCS12                        0x0c
#define DESC90_RATEMCS13                        0x0d
#define DESC90_RATEMCS14                        0x0e
#define DESC90_RATEMCS15                        0x0f
#define DESC90_RATEMCS32                        0x20

#define RTL819X_DEFAULT_RF_TYPE RF_1T2R
#define EEPROM_Default_LegacyHTTxPowerDiff	0x4
#define IEEE80211_WATCH_DOG_TIME    2000

typedef u32 RT_RF_CHANGE_SOURCE;
#define RF_CHANGE_BY_SW BIT31
#define RF_CHANGE_BY_HW BIT30
#define RF_CHANGE_BY_PS BIT29
#define RF_CHANGE_BY_IPS BIT28
#define RF_CHANGE_BY_INIT	0	// Do not change the RFOff reason. Defined by Bruce, 2008-01-17.

// RF state.
typedef	enum _RT_RF_POWER_STATE {
	eRfOn,
	eRfSleep,
	eRfOff
} RT_RF_POWER_STATE;

typedef enum _RT_JOIN_ACTION {
	RT_JOIN_INFRA = 1,
	RT_JOIN_IBSS  = 2,
	RT_START_IBSS = 3,
	RT_NO_ACTION  = 4,
} RT_JOIN_ACTION;

typedef enum _IPS_CALLBACK_FUNCION {
	IPS_CALLBACK_NONE = 0,
	IPS_CALLBACK_MGNT_LINK_REQUEST = 1,
	IPS_CALLBACK_JOIN_REQUEST = 2,
} IPS_CALLBACK_FUNCION;

typedef struct _RT_POWER_SAVE_CONTROL {
	/* Inactive Power Save(IPS) : Disable RF when disconnected */
	bool			bInactivePs;
	bool			bIPSModeBackup;
	bool			bSwRfProcessing;
	RT_RF_POWER_STATE	eInactivePowerState;
	struct work_struct 	InactivePsWorkItem;
	struct timer_list	InactivePsTimer;

	/* Return point for join action */
	IPS_CALLBACK_FUNCION	ReturnPoint;

	/* Recored Parameters for rescheduled JoinRequest */
	bool			bTmpBssDesc;
	RT_JOIN_ACTION		tmpJoinAction;
	struct ieee80211_network tmpBssDesc;

	/* Recored Parameters for rescheduled MgntLinkRequest */
	bool			bTmpScanOnly;
	bool			bTmpActiveScan;
	bool			bTmpFilterHiddenAP;
	bool			bTmpUpdateParms;
	u8			tmpSsidBuf[33];
	OCTET_STRING		tmpSsid2Scan;
	bool			bTmpSsid2Scan;
	u8			tmpNetworkType;
	u8			tmpChannelNumber;
	u16			tmpBcnPeriod;
	u8			tmpDtimPeriod;
	u16			tmpmCap;
	OCTET_STRING		tmpSuppRateSet;
	u8			tmpSuppRateBuf[MAX_NUM_RATES];
	bool			bTmpSuppRate;
	IbssParms		tmpIbpm;
	bool			bTmpIbpm;

	/*
	 * Leisure Power Save:
	 * Disable RF if connected but traffic is not busy
	 */
	bool			bLeisurePs;
	u32			PowerProfile;
	u8			LpsIdleCount;

	u32			CurPsLevel;
	u32			RegRfPsLevel;

	bool			bFwCtrlLPS;
	u8			FWCtrlPSMode;

	bool			LinkReqInIPSRFOffPgs;
	bool			BufConnectinfoBefore;
} RT_POWER_SAVE_CONTROL, *PRT_POWER_SAVE_CONTROL;

/* For rtl819x */
typedef struct _tx_desc_819x_pci {
        //DWORD 0
        u16	PktSize;
        u8	Offset;
        u8	Reserved1:3;
        u8	CmdInit:1;
        u8	LastSeg:1;
        u8	FirstSeg:1;
        u8	LINIP:1;
        u8	OWN:1;

        //DWORD 1
        u8	TxFWInfoSize;
        u8	RATid:3;
        u8	DISFB:1;
        u8	USERATE:1;
        u8	MOREFRAG:1;
        u8	NoEnc:1;
        u8	PIFS:1;
        u8	QueueSelect:5;
        u8	NoACM:1;
        u8	Resv:2;
        u8	SecCAMID:5;
        u8	SecDescAssign:1;
        u8	SecType:2;

        //DWORD 2
        u16	TxBufferSize;
        u8	PktId:7;
        u8	Resv1:1;
        u8	Reserved2;

        //DWORD 3
	u32 	TxBuffAddr;

	//DWORD 4
	u32	NextDescAddress;

	//DWORD 5,6,7
        u32	Reserved5;
        u32	Reserved6;
        u32	Reserved7;
}tx_desc_819x_pci, *ptx_desc_819x_pci;


typedef struct _tx_desc_cmd_819x_pci {
        //DWORD 0
	u16	PktSize;
	u8	Reserved1;
	u8	CmdType:3;
	u8	CmdInit:1;
	u8	LastSeg:1;
	u8	FirstSeg:1;
	u8	LINIP:1;
	u8	OWN:1;

        //DOWRD 1
	u16	ElementReport;
	u16	Reserved2;

        //DOWRD 2
	u16 	TxBufferSize;
	u16	Reserved3;

       //DWORD 3,4,5
	u32	TxBufferAddr;
	u32	NextDescAddress;
	u32	Reserved4;
	u32	Reserved5;
	u32	Reserved6;
}tx_desc_cmd_819x_pci, *ptx_desc_cmd_819x_pci;


typedef struct _tx_fwinfo_819x_pci {
        //DOWRD 0
        u8		TxRate:7;
        u8		CtsEnable:1;
        u8		RtsRate:7;
        u8		RtsEnable:1;
        u8		TxHT:1;
        u8		Short:1;                //Short PLCP for CCK, or short GI for 11n MCS
        u8		TxBandwidth:1;          // This is used for HT MCS rate only.
        u8		TxSubCarrier:2;         // This is used for legacy OFDM rate only.
        u8		STBC:2;
        u8		AllowAggregation:1;
        u8		RtsHT:1;                //Interpre RtsRate field as high throughput data rate
        u8		RtsShort:1;             //Short PLCP for CCK, or short GI for 11n MCS
        u8		RtsBandwidth:1;         // This is used for HT MCS rate only.
        u8		RtsSubcarrier:2;        // This is used for legacy OFDM rate only.
        u8		RtsSTBC:2;
        u8		EnableCPUDur:1;         //Enable firmware to recalculate and assign packet duration

        //DWORD 1
        u8		RxMF:2;
        u8		RxAMD:3;
        u8		Reserved1:3;
        u8		Reserved2;
        u8		Reserved3;
        u8		Reserved4;

        //u32                Reserved;
}tx_fwinfo_819x_pci, *ptx_fwinfo_819x_pci;

typedef struct _rx_desc_819x_pci{
	//DOWRD 0
	u16			Length:14;
	u16			CRC32:1;
	u16			ICV:1;
	u8			RxDrvInfoSize;
	u8			Shift:2;
	u8			PHYStatus:1;
	u8			SWDec:1;
	u8					LastSeg:1;
	u8					FirstSeg:1;
	u8					EOR:1;
	u8					OWN:1;

	//DWORD 1
	u32			Reserved2;

	//DWORD 2
	u32			Reserved3;

	//DWORD 3
	u32	BufferAddress;

}rx_desc_819x_pci, *prx_desc_819x_pci;

typedef struct _rx_fwinfo_819x_pci{
	//DWORD 0
	u16			Reserved1:12;
	u16			PartAggr:1;
	u16			FirstAGGR:1;
	u16			Reserved2:2;

	u8			RxRate:7;
	u8			RxHT:1;

	u8			BW:1;
	u8			SPLCP:1;
	u8			Reserved3:2;
	u8			PAM:1;
	u8			Mcast:1;
	u8			Bcast:1;
	u8			Reserved4:1;

	//DWORD 1
	u32			TSFL;

}rx_fwinfo_819x_pci, *prx_fwinfo_819x_pci;

#define MAX_DEV_ADDR_SIZE		8  /* support till 64 bit bus width OS */
#define MAX_FIRMWARE_INFORMATION_SIZE   32 /*2006/04/30 by Emily forRTL8190*/
#define MAX_802_11_HEADER_LENGTH        (40 + MAX_FIRMWARE_INFORMATION_SIZE)
#define ENCRYPTION_MAX_OVERHEAD		128
#define MAX_FRAGMENT_COUNT		8
#define MAX_TRANSMIT_BUFFER_SIZE  	(1600+(MAX_802_11_HEADER_LENGTH+ENCRYPTION_MAX_OVERHEAD)*MAX_FRAGMENT_COUNT)

#define scrclng					4		// octets for crc32 (FCS, ICV)
/* 8190 Loopback Mode definition */
typedef enum _rtl819x_loopback{
	RTL819X_NO_LOOPBACK = 0,
	RTL819X_MAC_LOOPBACK = 1,
	RTL819X_DMA_LOOPBACK = 2,
	RTL819X_CCK_LOOPBACK = 3,
}rtl819x_loopback_e;

/* due to rtl8192 firmware */
typedef enum _desc_packet_type_e{
	DESC_PACKET_TYPE_INIT = 0,
	DESC_PACKET_TYPE_NORMAL = 1,
}desc_packet_type_e;

typedef enum _firmware_status{
	FW_STATUS_0_INIT = 0,
	FW_STATUS_1_MOVE_BOOT_CODE = 1,
	FW_STATUS_2_MOVE_MAIN_CODE = 2,
	FW_STATUS_3_TURNON_CPU = 3,
	FW_STATUS_4_MOVE_DATA_CODE = 4,
	FW_STATUS_5_READY = 5,
}firmware_status_e;

typedef struct _rt_firmware{
	firmware_status_e firmware_status;
	u16		  cmdpacket_frag_thresold;
#define RTL8190_MAX_FIRMWARE_CODE_SIZE	64000	//64k
#define MAX_FW_INIT_STEP		3
	u8		  firmware_buf[MAX_FW_INIT_STEP][RTL8190_MAX_FIRMWARE_CODE_SIZE];
	u16		  firmware_buf_size[MAX_FW_INIT_STEP];
}rt_firmware, *prt_firmware;

#define MAX_RECEIVE_BUFFER_SIZE	9100	// Add this to 9100 bytes to receive A-MSDU from RT-AP

/* Firmware Queue Layout */
#define NUM_OF_FIRMWARE_QUEUE		10
#define NUM_OF_PAGES_IN_FW		0x100
#define NUM_OF_PAGE_IN_FW_QUEUE_BE	0x0aa
#define NUM_OF_PAGE_IN_FW_QUEUE_BK	0x007
#define NUM_OF_PAGE_IN_FW_QUEUE_VI	0x024
#define NUM_OF_PAGE_IN_FW_QUEUE_VO	0x007
#define NUM_OF_PAGE_IN_FW_QUEUE_HCCA	0
#define NUM_OF_PAGE_IN_FW_QUEUE_CMD	0x2
#define NUM_OF_PAGE_IN_FW_QUEUE_MGNT	0x10
#define NUM_OF_PAGE_IN_FW_QUEUE_HIGH	0
#define NUM_OF_PAGE_IN_FW_QUEUE_BCN	0x4
#define NUM_OF_PAGE_IN_FW_QUEUE_PUB	0xd
#define APPLIED_RESERVED_QUEUE_IN_FW	0x80000000
#define RSVD_FW_QUEUE_PAGE_BK_SHIFT	0x00
#define RSVD_FW_QUEUE_PAGE_BE_SHIFT	0x08
#define RSVD_FW_QUEUE_PAGE_VI_SHIFT	0x10
#define RSVD_FW_QUEUE_PAGE_VO_SHIFT	0x18
#define RSVD_FW_QUEUE_PAGE_MGNT_SHIFT	0x10
#define RSVD_FW_QUEUE_PAGE_CMD_SHIFT	0x08
#define RSVD_FW_QUEUE_PAGE_BCN_SHIFT	0x00
#define RSVD_FW_QUEUE_PAGE_PUB_SHIFT	0x08

#define DCAM                    0xAC                    // Debug CAM Interface
#define AESMSK_FC               0xB2    // AES Mask register for frame control (0xB2~0xB3). Added by Annie, 2006-03-06.


#define CAM_CONTENT_COUNT       8
#define CFG_VALID               BIT15
#define EPROM_93c46 0
#define EPROM_93c56 1

#define DEFAULT_FRAG_THRESHOLD 2342U
#define MIN_FRAG_THRESHOLD     256U
#define DEFAULT_BEACONINTERVAL 0x64U

#define DEFAULT_RETRY_RTS 7
#define DEFAULT_RETRY_DATA 7

#define		PHY_RSSI_SLID_WIN_MAX				100


typedef enum _WIRELESS_MODE {
	WIRELESS_MODE_UNKNOWN = 0x00,
	WIRELESS_MODE_A = 0x01,
	WIRELESS_MODE_B = 0x02,
	WIRELESS_MODE_G = 0x04,
	WIRELESS_MODE_AUTO = 0x08,
	WIRELESS_MODE_N_24G = 0x10,
	WIRELESS_MODE_N_5G = 0x20
} WIRELESS_MODE;

#define RTL_IOCTL_WPA_SUPPLICANT		SIOCIWFIRSTPRIV+30

typedef struct buffer
{
	struct buffer *next;
	u32 *buf;
	dma_addr_t dma;

} buffer;

typedef struct _rt_9x_tx_rate_history {
	u32             cck[4];
	u32             ofdm[8];
	// HT_MCS[0][]: BW=0 SG=0
	// HT_MCS[1][]: BW=1 SG=0
	// HT_MCS[2][]: BW=0 SG=1
	// HT_MCS[3][]: BW=1 SG=1
	u32             ht_mcs[4][16];
}rt_tx_rahis_t, *prt_tx_rahis_t;

typedef	struct _RT_SMOOTH_DATA_4RF {
	char	elements[4][100];//array to store values
	u32	index;			//index to current array to store
	u32	TotalNum;		//num of valid elements
	u32	TotalVal[4];		//sum of valid elements
}RT_SMOOTH_DATA_4RF, *PRT_SMOOTH_DATA_4RF;

typedef enum _tag_TxCmd_Config_Index{
	TXCMD_TXRA_HISTORY_CTRL				= 0xFF900000,
	TXCMD_RESET_TX_PKT_BUFF				= 0xFF900001,
	TXCMD_RESET_RX_PKT_BUFF				= 0xFF900002,
	TXCMD_SET_TX_DURATION				= 0xFF900003,
	TXCMD_SET_RX_RSSI						= 0xFF900004,
	TXCMD_SET_TX_PWR_TRACKING			= 0xFF900005,
	TXCMD_XXXX_CTRL,
}DCMD_TXCMD_OP;

typedef struct Stats
{
	unsigned long rxrdu;
	unsigned long rxok;
	unsigned long received_rate_histogram[4][32];	//0: Total, 1:OK, 2:CRC, 3:ICV
	unsigned long rxoverflow;
	unsigned long rxint;
	unsigned long txoverflow;
	unsigned long txbeokint;
	unsigned long txbkokint;
	unsigned long txviokint;
	unsigned long txvookint;
	unsigned long txbeaconokint;
	unsigned long txbeaconerr;
	unsigned long txmanageokint;
	unsigned long txcmdpktokint;
	unsigned long txfeedback;
	unsigned long txfeedbackok;
	unsigned long txoktotal;
	unsigned long txbytesunicast;
	unsigned long rxbytesunicast;

	unsigned long slide_signal_strength[100];
	unsigned long slide_evm[100];
	unsigned long	slide_rssi_total;	// For recording sliding window's RSSI value
	unsigned long slide_evm_total;	// For recording sliding window's EVM value
	long signal_strength; // Transformed, in dbm. Beautified signal strength for UI, not correct.
	u8 rx_rssi_percentage[4];
	u8 rx_evm_percentage[2];
	u32 Slide_Beacon_pwdb[100];
	u32 Slide_Beacon_Total;
	RT_SMOOTH_DATA_4RF		cck_adc_pwdb;
} Stats;


// Bandwidth Offset
#define HAL_PRIME_CHNL_OFFSET_DONT_CARE		0
#define HAL_PRIME_CHNL_OFFSET_LOWER			1
#define HAL_PRIME_CHNL_OFFSET_UPPER			2

typedef struct 	ChnlAccessSetting {
	u16 SIFS_Timer;
	u16 DIFS_Timer;
	u16 SlotTimeTimer;
	u16 EIFS_Timer;
	u16 CWminIndex;
	u16 CWmaxIndex;
}*PCHANNEL_ACCESS_SETTING,CHANNEL_ACCESS_SETTING;

typedef struct _BB_REGISTER_DEFINITION{
	u32 rfintfs; 			// set software control: //		0x870~0x877[8 bytes]
	u32 rfintfi; 			// readback data: //		0x8e0~0x8e7[8 bytes]
	u32 rfintfo; 			// output data: //		0x860~0x86f [16 bytes]
	u32 rfintfe; 			// output enable: //		0x860~0x86f [16 bytes]
	u32 rf3wireOffset; 		// LSSI data: //		0x840~0x84f [16 bytes]
	u32 rfLSSI_Select; 		// BB Band Select: //		0x878~0x87f [8 bytes]
	u32 rfTxGainStage;		// Tx gain stage: //		0x80c~0x80f [4 bytes]
	u32 rfHSSIPara1; 		// wire parameter control1 : //		0x820~0x823,0x828~0x82b, 0x830~0x833, 0x838~0x83b [16 bytes]
	u32 rfHSSIPara2; 		// wire parameter control2 : //		0x824~0x827,0x82c~0x82f, 0x834~0x837, 0x83c~0x83f [16 bytes]
	u32 rfSwitchControl; 	//Tx Rx antenna control : //		0x858~0x85f [16 bytes]
	u32 rfAGCControl1; 	//AGC parameter control1 : //		0xc50~0xc53,0xc58~0xc5b, 0xc60~0xc63, 0xc68~0xc6b [16 bytes]
	u32 rfAGCControl2; 	//AGC parameter control2 : //		0xc54~0xc57,0xc5c~0xc5f, 0xc64~0xc67, 0xc6c~0xc6f [16 bytes]
	u32 rfRxIQImbalance; 	//OFDM Rx IQ imbalance matrix : //		0xc14~0xc17,0xc1c~0xc1f, 0xc24~0xc27, 0xc2c~0xc2f [16 bytes]
	u32 rfRxAFE;  			//Rx IQ DC ofset and Rx digital filter, Rx DC notch filter : //		0xc10~0xc13,0xc18~0xc1b, 0xc20~0xc23, 0xc28~0xc2b [16 bytes]
	u32 rfTxIQImbalance; 	//OFDM Tx IQ imbalance matrix //		0xc80~0xc83,0xc88~0xc8b, 0xc90~0xc93, 0xc98~0xc9b [16 bytes]
	u32 rfTxAFE; 			//Tx IQ DC Offset and Tx DFIR type //		0xc84~0xc87,0xc8c~0xc8f, 0xc94~0xc97, 0xc9c~0xc9f [16 bytes]
	u32 rfLSSIReadBack; 	//LSSI RF readback data //		0x8a0~0x8af [16 bytes]
}BB_REGISTER_DEFINITION_T, *PBB_REGISTER_DEFINITION_T;

typedef struct _rate_adaptive
{
	u8				rate_adaptive_disabled;
	u8				ratr_state;
	u16				reserve;

	u32				high_rssi_thresh_for_ra;
	u32				high2low_rssi_thresh_for_ra;
	u8				low2high_rssi_thresh_for_ra40M;
	u32				low_rssi_thresh_for_ra40M;
	u8				low2high_rssi_thresh_for_ra20M;
	u32				low_rssi_thresh_for_ra20M;
	u32				upper_rssi_threshold_ratr;
	u32				middle_rssi_threshold_ratr;
	u32				low_rssi_threshold_ratr;
	u32				low_rssi_threshold_ratr_40M;
	u32				low_rssi_threshold_ratr_20M;
	u8				ping_rssi_enable;	//cosa add for test
	u32				ping_rssi_ratr;	//cosa add for test
	u32				ping_rssi_thresh_for_ra;//cosa add for test
	u32				last_ratr;

} rate_adaptive, *prate_adaptive;
#define TxBBGainTableLength 37
#define	CCKTxBBGainTableLength 23
typedef struct _txbbgain_struct
{
	long	txbb_iq_amplifygain;
	u32	txbbgain_value;
} txbbgain_struct, *ptxbbgain_struct;

typedef struct _ccktxbbgain_struct
{
	//The Value is from a22 to a29 one Byte one time is much Safer
	u8	ccktxbb_valuearray[8];
} ccktxbbgain_struct,*pccktxbbgain_struct;


typedef struct _init_gain
{
	u8				xaagccore1;
	u8				xbagccore1;
	u8				xcagccore1;
	u8				xdagccore1;
	u8				cca;

} init_gain, *pinit_gain;

/* 2007/11/02 MH Define RF mode temporarily for test. */
typedef enum tag_Rf_Operatetion_State
{
    RF_STEP_INIT = 0,
    RF_STEP_NORMAL,
    RF_STEP_MAX
}RF_STEP_E;

typedef enum _RT_STATUS{
	RT_STATUS_SUCCESS,
	RT_STATUS_FAILURE,
	RT_STATUS_PENDING,
	RT_STATUS_RESOURCE
}RT_STATUS,*PRT_STATUS;

typedef enum _RT_CUSTOMER_ID
{
	RT_CID_DEFAULT = 0,
	RT_CID_8187_ALPHA0 = 1,
	RT_CID_8187_SERCOMM_PS = 2,
	RT_CID_8187_HW_LED = 3,
	RT_CID_8187_NETGEAR = 4,
	RT_CID_WHQL = 5,
	RT_CID_819x_CAMEO  = 6,
	RT_CID_819x_RUNTOP = 7,
	RT_CID_819x_Senao = 8,
	RT_CID_TOSHIBA = 9,	// Merge by Jacken, 2008/01/31.
	RT_CID_819x_Netcore = 10,
	RT_CID_Nettronix = 11,
	RT_CID_DLINK = 12,
	RT_CID_PRONET = 13,
	RT_CID_COREGA = 14,
}RT_CUSTOMER_ID, *PRT_CUSTOMER_ID;

/* LED customization. */

typedef	enum _LED_STRATEGY_8190{
	SW_LED_MODE0, // SW control 1 LED via GPIO0. It is default option.
	SW_LED_MODE1, // SW control for PCI Express
	SW_LED_MODE2, // SW control for Cameo.
	SW_LED_MODE3, // SW contorl for RunTop.
	SW_LED_MODE4, // SW control for Netcore
	SW_LED_MODE5, //added by vivi, for led new mode, DLINK
	SW_LED_MODE6, //added by vivi, for led new mode, PRONET
	HW_LED, // HW control 2 LEDs, LED0 and LED1 (there are 4 different control modes)
}LED_STRATEGY_8190, *PLED_STRATEGY_8190;

#define CHANNEL_PLAN_LEN				10

#define sCrcLng 		4

typedef struct _TX_FWINFO_STRUCUTRE{
	//DOWRD 0
	u8			TxRate:7;
	u8			CtsEnable:1;
	u8			RtsRate:7;
	u8			RtsEnable:1;
	u8			TxHT:1;
	u8			Short:1;
	u8			TxBandwidth:1;
	u8			TxSubCarrier:2;
	u8			STBC:2;
	u8			AllowAggregation:1;
	u8			RtsHT:1;
	u8			RtsShort:1;
	u8			RtsBandwidth:1;
	u8			RtsSubcarrier:2;
	u8			RtsSTBC:2;
	u8			EnableCPUDur:1;

	//DWORD 1
	u32			RxMF:2;
	u32			RxAMD:3;
	u32			Reserved1:3;
	u32			TxAGCOffset:4;
	u32			TxAGCSign:1;
	u32			Tx_INFO_RSVD:6;
	u32			PacketID:13;
}TX_FWINFO_T;


typedef struct _TX_FWINFO_8190PCI{
	//DOWRD 0
	u8			TxRate:7;
	u8			CtsEnable:1;
	u8			RtsRate:7;
	u8			RtsEnable:1;
	u8			TxHT:1;
	u8			Short:1;						//Short PLCP for CCK, or short GI for 11n MCS
	u8			TxBandwidth:1;				// This is used for HT MCS rate only.
	u8			TxSubCarrier:2; 			// This is used for legacy OFDM rate only.
	u8			STBC:2;
	u8			AllowAggregation:1;
	u8			RtsHT:1;						//Interpre RtsRate field as high throughput data rate
	u8			RtsShort:1; 				//Short PLCP for CCK, or short GI for 11n MCS
	u8			RtsBandwidth:1; 			// This is used for HT MCS rate only.
	u8			RtsSubcarrier:2;				// This is used for legacy OFDM rate only.
	u8			RtsSTBC:2;
	u8			EnableCPUDur:1; 			//Enable firmware to recalculate and assign packet duration

	//DWORD 1
	u32			RxMF:2;
	u32			RxAMD:3;
	u32			TxPerPktInfoFeedback:1; 	// 1: indicate that the transimission info of this packet should be gathered by Firmware and retured by Rx Cmd.
	u32			Reserved1:2;
	u32			TxAGCOffset:4;		// Only 90 support
	u32			TxAGCSign:1;		// Only 90 support
	u32			RAW_TXD:1;			// MAC will send data in txpktbuffer without any processing,such as CRC check
	u32			Retry_Limit:4;		// CCX Support relative retry limit FW page only support 4 bits now.
	u32			Reserved2:1;
	u32			PacketID:13;

	// DW 2

}TX_FWINFO_8190PCI, *PTX_FWINFO_8190PCI;

typedef struct _phy_ofdm_rx_status_report_819xpci
{
	u8	trsw_gain_X[4];
	u8	pwdb_all;
	u8	cfosho_X[4];
	u8	cfotail_X[4];
	u8	rxevm_X[2];
	u8	rxsnr_X[4];
	u8	pdsnr_X[2];
	u8	csi_current_X[2];
	u8	csi_target_X[2];
	u8	sigevm;
	u8	max_ex_pwr;
	u8	sgi_en;
	u8	rxsc_sgien_exflg;
}phy_sts_ofdm_819xpci_t;

typedef struct _phy_cck_rx_status_report_819xpci
{
	/* For CCK rate descriptor. This is a unsigned 8:1 variable. LSB bit presend
	   0.5. And MSB 7 bts presend a signed value. Range from -64~+63.5. */
	u8	adc_pwdb_X[4];
	u8	sq_rpt;
	u8	cck_agc_rpt;
}phy_sts_cck_819xpci_t;

typedef struct _phy_ofdm_rx_status_rxsc_sgien_exintfflag{
	u8			reserved:4;
	u8			rxsc:2;
	u8			sgi_en:1;
	u8			ex_intf_flag:1;
}phy_ofdm_rx_status_rxsc_sgien_exintfflag;

typedef enum _RT_OP_MODE{
	RT_OP_MODE_AP,
	RT_OP_MODE_INFRASTRUCTURE,
	RT_OP_MODE_IBSS,
	RT_OP_MODE_NO_LINK,
}RT_OP_MODE, *PRT_OP_MODE;


/* 2007/11/02 MH Define RF mode temporarily for test. */
typedef enum tag_Rf_OpType
{
    RF_OP_By_SW_3wire = 0,
    RF_OP_By_FW,
    RF_OP_MAX
}RF_OpType_E;

typedef enum _RESET_TYPE {
	RESET_TYPE_NORESET = 0x00,
	RESET_TYPE_NORMAL = 0x01,
	RESET_TYPE_SILENT = 0x02
} RESET_TYPE;

typedef struct _tx_ring{
	u32 * desc;
	u8 nStuckCount;
	struct _tx_ring * next;
}__attribute__ ((packed)) tx_ring, * ptx_ring;

struct rtl8192_tx_ring {
    tx_desc_819x_pci *desc;
    dma_addr_t dma;
    unsigned int idx;
    unsigned int entries;
    struct sk_buff_head queue;
};

#define NIC_SEND_HANG_THRESHOLD_NORMAL		4
#define NIC_SEND_HANG_THRESHOLD_POWERSAVE 	8
#define MAX_TX_QUEUE				9	// BK, BE, VI, VO, HCCA, MANAGEMENT, COMMAND, HIGH, BEACON.

#define MAX_RX_COUNT                            64
#define MAX_TX_QUEUE_COUNT                      9

typedef struct r8192_priv
{
	struct pci_dev *pdev;
	u8 *mem_start;

	/* maintain info from eeprom */
	short epromtype;
	u16 eeprom_vid;
	u16 eeprom_did;
	u8  eeprom_CustomerID;
	u16  eeprom_ChannelPlan;
	RT_CUSTOMER_ID CustomerID;
	u8	IC_Cut;
	int irq;
	struct ieee80211_device *ieee80211;
#ifdef ENABLE_LPS
	bool ps_force;
	bool force_lps;
	bool bdisable_nic;
#endif
	bool being_init_adapter;
	u8 Rf_Mode;
	u8 card_8192_version; /* if TCR reports card V B/C this discriminates */
	spinlock_t irq_th_lock;
	spinlock_t rf_ps_lock;
        struct mutex mutex;

	short chan;
	short sens;
	/* RX stuff */
        rx_desc_819x_pci *rx_ring;
        dma_addr_t rx_ring_dma;
        unsigned int rx_idx;
        struct sk_buff *rx_buf[MAX_RX_COUNT];
	int rxringcount;
	u16 rxbuffersize;

	/* TX stuff */
        struct rtl8192_tx_ring tx_ring[MAX_TX_QUEUE_COUNT];
	int txringcount;

	struct tasklet_struct irq_rx_tasklet;
	struct tasklet_struct irq_tx_tasklet;
        struct tasklet_struct irq_prepare_beacon_tasklet;

	short up;
	short crcmon; //if 1 allow bad crc frame reception in monitor mode
	struct semaphore wx_sem;
	struct semaphore rf_sem; //used to lock rf write operation added by wb, modified by david
	u8 rf_type; /* 0 means 1T2R, 1 means 2T4R */

	short (*rf_set_sens)(struct net_device *dev, short sens);
	u8 (*rf_set_chan)(struct ieee80211_device *ieee80211, u8 ch);
	short promisc;
	/* stats */
	struct Stats stats;
	struct iw_statistics wstats;
	struct proc_dir_entry *dir_dev;
	struct ieee80211_rx_stats previous_stats;

	/* RX stuff */
	struct sk_buff_head skb_queue;
	struct work_struct qos_activate;

	//2 Tx Related variables
	u16	ShortRetryLimit;
	u16	LongRetryLimit;

	u32     LastRxDescTSFHigh;
	u32     LastRxDescTSFLow;


	//2 Rx Related variables
	u32	ReceiveConfig;

	u8 retry_data;
	u8 retry_rts;

	struct work_struct reset_wq;
	u8	rx_chk_cnt;

//for rtl819xPci
	// Data Rate Config. Added by Annie, 2006-04-13.
	u16	basic_rate;
	u8	short_preamble;
	u8 	slot_time;
	u16 SifsTime;
/* WirelessMode*/
	u8 RegWirelessMode;
/*Firmware*/
	prt_firmware		pFirmware;
	rtl819x_loopback_e	LoopbackMode;
	bool AutoloadFailFlag;
	u16 EEPROMAntPwDiff;		// Antenna gain offset from B/C/D to A
	u8 EEPROMThermalMeter;
	u8 EEPROMCrystalCap;
	u8 EEPROMTxPowerLevelCCK[14];// CCK channel 1~14
	// The following definition is for eeprom 93c56
	u8 EEPROMRfACCKChnl1TxPwLevel[3];	//RF-A CCK Tx Power Level at channel 7
	u8 EEPROMRfAOfdmChnlTxPwLevel[3];//RF-A CCK Tx Power Level at [0],[1],[2] = channel 1,7,13
	u8 EEPROMRfCCCKChnl1TxPwLevel[3];	//RF-C CCK Tx Power Level at channel 7
	u8 EEPROMRfCOfdmChnlTxPwLevel[3];//RF-C CCK Tx Power Level at [0],[1],[2] = channel 1,7,13
	u8 EEPROMTxPowerLevelOFDM24G[14]; // OFDM 2.4G channel 1~14
	u8 EEPROMLegacyHTTxPowerDiff;	// Legacy to HT rate power diff
	bool bTXPowerDataReadFromEEPORM;
/*channel plan*/
	u16 RegChannelPlan; // Channel Plan specifed by user, 15: following setting of EEPROM, 0-14: default channel plan index specified by user.
	u16 ChannelPlan;
/*PS related*/
	// Rf off action for power save
	u8	bHwRfOffAction;	//0:No action, 1:By GPIO, 2:By Disable
/*PHY related*/
	BB_REGISTER_DEFINITION_T	PHYRegDef[4];	//Radio A/B/C/D
	// Read/write are allow for following hardware information variables
	u32	MCSTxPowerLevelOriginalOffset[6];
	u32	CCKTxPowerLevelOriginalOffset;
	u8	TxPowerLevelCCK[14];			// CCK channel 1~14
	u8	TxPowerLevelCCK_A[14];			// RF-A, CCK channel 1~14
	u8 	TxPowerLevelCCK_C[14];
	u8	TxPowerLevelOFDM24G[14];		// OFDM 2.4G channel 1~14
	u8	TxPowerLevelOFDM5G[14];			// OFDM 5G
	u8	TxPowerLevelOFDM24G_A[14];	// RF-A, OFDM 2.4G channel 1~14
	u8	TxPowerLevelOFDM24G_C[14];	// RF-C, OFDM 2.4G channel 1~14
	u8	LegacyHTTxPowerDiff;			// Legacy to HT rate power diff
	u8	AntennaTxPwDiff[3];				// Antenna gain offset, index 0 for B, 1 for C, and 2 for D
	u8	CrystalCap;						// CrystalCap.
	u8	ThermalMeter[2];				// ThermalMeter, index 0 for RFIC0, and 1 for RFIC1
	//05/27/2008 cck power enlarge
	u8	CckPwEnl;
	u16	TSSI_13dBm;
	u32 	Pwr_Track;
	u8				CCKPresentAttentuation_20Mdefault;
	u8				CCKPresentAttentuation_40Mdefault;
	char				CCKPresentAttentuation_difference;
	char				CCKPresentAttentuation;
	// Use to calculate PWBD.
	RT_RF_POWER_STATE		eRFPowerState;
	RT_RF_CHANGE_SOURCE	RfOffReason;
	RT_POWER_SAVE_CONTROL	PowerSaveControl;
	u8	bCckHighPower;
	long	undecorated_smoothed_pwdb;
	long	undecorated_smoothed_cck_adc_pwdb[4];
	//for set channel
	u8	SwChnlInProgress;
	u8 	SwChnlStage;
	u8	SwChnlStep;
	u8	SetBWModeInProgress;
	HT_CHANNEL_WIDTH		CurrentChannelBW;

	// 8190 40MHz mode
	//
	u8	nCur40MhzPrimeSC;	// Control channel sub-carrier
	// Joseph test for shorten RF configuration time.
	// We save RF reg0 in this variable to reduce RF reading.
	//
	u32					RfReg0Value[4];
	u8 					NumTotalRFPath;
	bool 				brfpath_rxenable[4];
//+by amy 080507
	struct timer_list watch_dog_timer;
	u8 watchdog_last_time;
	u8 watchdog_check_reset_cnt;

//+by amy 080515 for dynamic mechenism
	//Add by amy Tx Power Control for Near/Far Range 2008/05/15
	bool	bDynamicTxHighPower;  // Tx high power state
	bool	bDynamicTxLowPower;  // Tx low power state
	bool	bLastDTPFlag_High;
	bool	bLastDTPFlag_Low;

	/* OFDM RSSI. For high power or not */
	u8	phy_check_reg824;
	u32	phy_reg824_bit9;

	//Add by amy for Rate Adaptive
	rate_adaptive rate_adaptive;
	//Add by amy for TX power tracking
	//2008/05/15  Mars OPEN/CLOSE TX POWER TRACKING
	const txbbgain_struct * txbbgain_table;
	u8			   txpower_count;//For 6 sec do tracking again
	bool			   btxpower_trackingInit;
	u8			   OFDM_index;
	u8			   CCK_index;
	u8			   Record_CCK_20Mindex;
	u8			   Record_CCK_40Mindex;
	//2007/09/10 Mars Add CCK TX Power Tracking
	const ccktxbbgain_struct *cck_txbbgain_table;
	const ccktxbbgain_struct *cck_txbbgain_ch14_table;
	u8 rfa_txpowertrackingindex;
	u8 rfa_txpowertrackingindex_real;
	u8 rfa_txpowertracking_default;
	u8 rfc_txpowertrackingindex;
	u8 rfc_txpowertrackingindex_real;
	u8 rfc_txpowertracking_default;
	bool btxpower_tracking;
	bool bcck_in_ch14;

	//For Backup Initial Gain
	init_gain initgain_backup;
	u8 		DefaultInitialGain[4];
	// For EDCA Turbo mode, Added by amy 080515.
	bool		bis_any_nonbepkts;
	bool		bcurrent_turbo_EDCA;

	bool		bis_cur_rdlstate;
	struct timer_list fsync_timer;
	u32 	rate_record;
	u32 	rateCountDiffRecord;
	u32	ContiuneDiffCount;
	bool bswitch_fsync;

	u8	framesync;
	u32 	framesyncC34;
	u8   	framesyncMonitor;

	//by amy for gpio
	bool bHwRadioOff;
	//by amy for ps
	RT_OP_MODE OpMode;
	//by amy for reset_count
	u32 reset_count;

	//by amy for silent reset
	RESET_TYPE	ResetProgress;
	bool		bForcedSilentReset;
	bool		bDisableNormalResetCheck;
	u16		TxCounter;
	u16		RxCounter;
	int		IrpPendingCount;
	bool		bResetInProgress;
	bool		force_reset;
	u8		InitialGainOperateType;

	//define work item by amy 080526
	struct delayed_work update_beacon_wq;
	struct delayed_work watch_dog_wq;
	struct delayed_work txpower_tracking_wq;
	struct delayed_work rfpath_check_wq;
	struct delayed_work gpio_change_rf_wq;
	struct delayed_work initialgain_operate_wq;
	struct workqueue_struct *priv_wq;
}r8192_priv;

bool init_firmware(struct r8192_priv *priv);
u32 read_cam(struct r8192_priv *priv, u8 addr);
void write_cam(struct r8192_priv *priv, u8 addr, u32 data);
u8 read_nic_byte(struct r8192_priv *priv, int x);
u32 read_nic_dword(struct r8192_priv *priv, int x);
u16 read_nic_word(struct r8192_priv *priv, int x) ;
void write_nic_byte(struct r8192_priv *priv, int x,u8 y);
void write_nic_word(struct r8192_priv *priv, int x,u16 y);
void write_nic_dword(struct r8192_priv *priv, int x,u32 y);

int rtl8192_down(struct net_device *dev);
int rtl8192_up(struct net_device *dev);
void rtl8192_commit(struct r8192_priv *priv);
void write_phy(struct net_device *dev, u8 adr, u8 data);
void CamResetAllEntry(struct r8192_priv *priv);
void EnableHWSecurityConfig8192(struct r8192_priv *priv);
void setKey(struct r8192_priv *priv, u8 EntryNo, u8 KeyIndex, u16 KeyType,
	    const u8 *MacAddr, u8 DefaultKey, u32 *KeyContent);
void firmware_init_param(struct r8192_priv *priv);
RT_STATUS cmpk_message_handle_tx(struct r8192_priv *priv, u8 *codevirtualaddress, u32 packettype, u32 buffer_len);

#ifdef ENABLE_IPS
void IPSEnter(struct r8192_priv *priv);
void IPSLeave(struct r8192_priv *priv);
void IPSLeave_wq(struct work_struct *work);
void ieee80211_ips_leave_wq(struct ieee80211_device *ieee80211);
void ieee80211_ips_leave(struct ieee80211_device *ieee80211);
#endif
#ifdef ENABLE_LPS
void LeisurePSEnter(struct ieee80211_device *ieee80211);
void LeisurePSLeave(struct ieee80211_device *ieee80211);
#endif

bool NicIFEnableNIC(struct r8192_priv *priv);
bool NicIFDisableNIC(struct r8192_priv *priv);

void PHY_SetRtl8192eRfOff(struct r8192_priv *priv);
#endif
