#ifndef __WINBOND_WBHAL_S_H
#define __WINBOND_WBHAL_S_H

#include <linux/types.h>
#include <linux/if_ether.h> /* for ETH_ALEN */

//[20040722 WK]
#define HAL_LED_SET_MASK		0x001c	//20060901 Extend
#define HAL_LED_SET_SHIFT		2

//supported RF type
#define RF_MAXIM_2825		0
#define RF_MAXIM_2827		1
#define RF_MAXIM_2828		2
#define RF_MAXIM_2829		3
#define RF_MAXIM_V1			15
#define RF_AIROHA_2230		16
#define RF_AIROHA_7230		17
#define RF_AIROHA_2230S		18	// 20060420 Add this
// #define RF_RFMD_2959		32	// 20060626 Remove all about RFMD
#define RF_WB_242			33
#define RF_WB_242_1			34	// 20060619.5 Add
#define RF_DECIDE_BY_INF	255

//----------------------------------------------------------------
// The follow define connect to upper layer
//	User must modify for connection between HAL and upper layer
//----------------------------------------------------------------




/////////////////////////////////////////////////////////////////////////////////////////////////////
//================================================================================================
// Common define
//================================================================================================
#define HAL_USB_MODE_BURST( _H )	(_H->SoftwareSet & 0x20 ) // Bit 5 20060901 Modify

// Scan interval
#define SCAN_MAX_CHNL_TIME				(50)

// For TxL2 Frame typr recognise
#define FRAME_TYPE_802_3_DATA			0
#define FRAME_TYPE_802_11_MANAGEMENT		1
#define FRAME_TYPE_802_11_MANAGEMENT_CHALLENGE  2
#define FRAME_TYPE_802_11_CONTROL		3
#define FRAME_TYPE_802_11_DATA			4
#define FRAME_TYPE_PROMISCUOUS			5

// The follow definition is used for convert the frame--------------------
#define DOT_11_SEQUENCE_OFFSET		22 //Sequence control offset
#define DOT_3_TYPE_OFFSET			12
#define DOT_11_MAC_HEADER_SIZE		24
#define DOT_11_SNAP_SIZE			6
#define DOT_11_TYPE_OFFSET			30 //The start offset of 802.11 Frame. Type encapsulatuin.
#define DEFAULT_SIFSTIME			10
#define DEFAULT_FRAGMENT_THRESHOLD		2346 // No fragment
#define DEFAULT_MSDU_LIFE_TIME			0xffff

#define LONG_PREAMBLE_PLUS_PLCPHEADER_TIME						(144+48)
#define SHORT_PREAMBLE_PLUS_PLCPHEADER_TIME 					(72+24)
#define PREAMBLE_PLUS_SIGNAL_PLUS_SIGNALEXTENSION				(16+4+6)
#define Tsym 4

//  Frame Type of Bits (2, 3)---------------------------------------------
#define MAC_TYPE_MANAGEMENT			0x00
#define MAC_TYPE_CONTROL			0x04
#define MAC_TYPE_DATA				0x08
#define MASK_FRAGMENT_NUMBER		0x000F
#define SEQUENCE_NUMBER_SHIFT		4

#define  HAL_WOL_TYPE_WAKEUP_FRAME		0x01
#define  HAL_WOL_TYPE_MAGIC_PACKET		0x02

// 20040106 ADDED
#define HAL_KEYTYPE_WEP40                       0
#define HAL_KEYTYPE_WEP104                      1
#define HAL_KEYTYPE_TKIP                        2 // 128 bit key
#define HAL_KEYTYPE_AES_CCMP                    3 // 128 bit key

// For VM state
enum {
	VM_STOP = 0,
	VM_RUNNING,
	VM_COMPLETED
};

//-----------------------------------------------------
// Normal Key table format
//-----------------------------------------------------
// The order of KEY index is MAPPING_KEY_START_INDEX > GROUP_KEY_START_INDEX
#define MAX_KEY_TABLE				24	// 24 entry for storing key data
#define GROUP_KEY_START_INDEX		4
#define MAPPING_KEY_START_INDEX		8

//--------------------------------------------------------
// 			 Descriptor
//--------------------------------------------------------
#define MAX_DESCRIPTOR_BUFFER_INDEX	8	// Have to multiple of 2
//#define FLAG_ERROR_TX_MASK			cpu_to_le32(0x000000bf) //20061009 marked by anson's endian
#define FLAG_ERROR_TX_MASK			0x000000bf  //20061009 anson's endian
//#define FLAG_ERROR_RX_MASK			0x00000c3f
//#define FLAG_ERROR_RX_MASK			cpu_to_le32(0x0000083f)	//20061009 marked by anson's endian
									//Don't care replay error,
												//it is handled by S/W
#define FLAG_ERROR_RX_MASK			0x0000083f	//20060926 anson's endian

#define FLAG_BAND_RX_MASK			0x10000000	//Bit 28

typedef struct _R00_DESCRIPTOR
{
	union
	{
		u32	value;
		#ifdef _BIG_ENDIAN_  //20060926 anson's endian
		struct
		{
			u32	R00_packet_or_buffer_status:1;
			u32	R00_packet_in_fifo:1;
			u32	R00_RESERVED:2;
			u32	R00_receive_byte_count:12;
			u32	R00_receive_time_index:16;
		};
		#else
		struct
		{
			u32	R00_receive_time_index:16;
			u32	R00_receive_byte_count:12;
			u32	R00_RESERVED:2;
			u32	R00_packet_in_fifo:1;
			u32	R00_packet_or_buffer_status:1;
		};
		#endif
	};
} R00_DESCRIPTOR, *PR00_DESCRIPTOR;

typedef struct _T00_DESCRIPTOR
{
	union
	{
		u32	value;
		#ifdef _BIG_ENDIAN_  //20061009 anson's endian
		struct
		{
			u32	T00_first_mpdu:1; // for hardware use
			u32	T00_last_mpdu:1; // for hardware use
			u32	T00_IsLastMpdu:1;// 0: not   1:Yes for software used
			u32	T00_IgnoreResult:1;// The same mechanism with T00 setting. 050111 Modify for TS
			u32	T00_RESERVED_ID:2;//3 bit ID reserved
			u32	T00_tx_packet_id:4;//930519.4.e 930810.3.c
			u32	T00_RESERVED:4;
			u32	T00_header_length:6;
			u32	T00_frame_length:12;
		};
		#else
		struct
		{
			u32	T00_frame_length:12;
			u32	T00_header_length:6;
			u32	T00_RESERVED:4;
			u32	T00_tx_packet_id:4;//930519.4.e 930810.3.c
			u32	T00_RESERVED_ID:2;//3 bit ID reserved
			u32	T00_IgnoreResult:1;// The same mechanism with T00 setting. 050111 Modify for TS
			u32	T00_IsLastMpdu:1;// 0: not   1:Yes for software used
			u32	T00_last_mpdu:1; // for hardware use
			u32	T00_first_mpdu:1; // for hardware use
		};
		#endif
	};
} T00_DESCRIPTOR, *PT00_DESCRIPTOR;

typedef struct _R01_DESCRIPTOR
{
	union
	{
		u32	value;
		#ifdef _BIG_ENDIAN_ //20060926 add by anson's endian
		struct
		{
			u32	R01_RESERVED:3;
			u32	R01_mod_type:1;
			u32	R01_pre_type:1;
			u32	R01_data_rate:3;
			u32	R01_AGC_state:8;
			u32	R01_LNA_state:2;
			u32	R01_decryption_method:2;
			u32	R01_mic_error:1;
			u32	R01_replay:1;
			u32	R01_broadcast_frame:1;
			u32	R01_multicast_frame:1;
			u32	R01_directed_frame:1;
			u32	R01_receive_frame_antenna_selection:1;
			u32	R01_frame_receive_during_atim_window:1;
			u32	R01_protocol_version_error:1;
			u32	R01_authentication_frame_icv_error:1;
			u32	R01_null_key_to_authentication_frame:1;
			u32	R01_icv_error:1;
			u32	R01_crc_error:1;
		};
		#else
		struct
		{
			u32	R01_crc_error:1;
			u32	R01_icv_error:1;
			u32	R01_null_key_to_authentication_frame:1;
			u32	R01_authentication_frame_icv_error:1;
			u32	R01_protocol_version_error:1;
			u32	R01_frame_receive_during_atim_window:1;
			u32	R01_receive_frame_antenna_selection:1;
			u32	R01_directed_frame:1;
			u32	R01_multicast_frame:1;
			u32	R01_broadcast_frame:1;
			u32	R01_replay:1;
			u32	R01_mic_error:1;
			u32	R01_decryption_method:2;
			u32	R01_LNA_state:2;
			u32	R01_AGC_state:8;
			u32	R01_data_rate:3;
			u32	R01_pre_type:1;
			u32	R01_mod_type:1;
			u32	R01_RESERVED:3;
		};
		#endif
	};
} R01_DESCRIPTOR, *PR01_DESCRIPTOR;

typedef struct _T01_DESCRIPTOR
{
	union
	{
		u32	value;
		#ifdef _BIG_ENDIAN_ //20061009 anson's endian
		struct
		{
			u32	T01_rts_cts_duration:16;
			u32	T01_fall_back_rate:3;
			u32	T01_add_rts:1;
			u32	T01_add_cts:1;
			u32	T01_modulation_type:1;
			u32	T01_plcp_header_length:1;
			u32	T01_transmit_rate:3;
			u32	T01_wep_id:2;
			u32	T01_add_challenge_text:1;
			u32	T01_inhibit_crc:1;
			u32	T01_loop_back_wep_mode:1;
			u32	T01_retry_abort_ebable:1;
		};
		#else
		struct
		{
			u32	T01_retry_abort_ebable:1;
			u32	T01_loop_back_wep_mode:1;
			u32	T01_inhibit_crc:1;
			u32	T01_add_challenge_text:1;
			u32	T01_wep_id:2;
			u32	T01_transmit_rate:3;
			u32	T01_plcp_header_length:1;
			u32	T01_modulation_type:1;
			u32	T01_add_cts:1;
			u32	T01_add_rts:1;
			u32	T01_fall_back_rate:3;
			u32	T01_rts_cts_duration:16;
		};
		#endif
	};
} T01_DESCRIPTOR, *PT01_DESCRIPTOR;

typedef struct _T02_DESCRIPTOR
{
	union
	{
		u32	value;
		#ifdef _BIG_ENDIAN_  //20061009 add by anson's endian
		struct
		{
			u32	T02_IsLastMpdu:1;// The same mechanism with T00 setting
			u32	T02_IgnoreResult:1;// The same mechanism with T00 setting. 050111 Modify for TS
			u32	T02_RESERVED_ID:2;// The same mechanism with T00 setting
			u32	T02_Tx_PktID:4;
			u32	T02_MPDU_Cnt:4;
			u32	T02_RTS_Cnt:4;
			u32	T02_RESERVED:7;
			u32	T02_transmit_complete:1;
			u32	T02_transmit_abort_due_to_TBTT:1;
			u32	T02_effective_transmission_rate:1;
			u32	T02_transmit_without_encryption_due_to_wep_on_false:1;
			u32	T02_discard_due_to_null_wep_key:1;
			u32	T02_RESERVED_1:1;
			u32	T02_out_of_MaxTxMSDULiftTime:1;
			u32	T02_transmit_abort:1;
			u32	T02_transmit_fail:1;
		};
		#else
		struct
		{
			u32	T02_transmit_fail:1;
			u32	T02_transmit_abort:1;
			u32	T02_out_of_MaxTxMSDULiftTime:1;
			u32	T02_RESERVED_1:1;
			u32	T02_discard_due_to_null_wep_key:1;
			u32	T02_transmit_without_encryption_due_to_wep_on_false:1;
			u32	T02_effective_transmission_rate:1;
			u32	T02_transmit_abort_due_to_TBTT:1;
			u32	T02_transmit_complete:1;
			u32	T02_RESERVED:7;
			u32	T02_RTS_Cnt:4;
			u32	T02_MPDU_Cnt:4;
			u32	T02_Tx_PktID:4;
			u32	T02_RESERVED_ID:2;// The same mechanism with T00 setting
			u32	T02_IgnoreResult:1;// The same mechanism with T00 setting. 050111 Modify for TS
			u32	T02_IsLastMpdu:1;// The same mechanism with T00 setting
		};
		#endif
	};
} T02_DESCRIPTOR, *PT02_DESCRIPTOR;

struct wb35_descriptor {		// Skip length = 8 DWORD
	// ID for descriptor ---, The field doesn't be cleard in the operation of Descriptor definition
	u8	Descriptor_ID;
	//----------------------The above region doesn't be cleared by DESCRIPTOR_RESET------
	u8	RESERVED[3];

	u16	FragmentThreshold;
	u8	InternalUsed;//Only can be used by operation of descriptor definition
	u8	Type;// 0: 802.3	1:802.11 data frame	2:802.11 management frame

	u8	PreambleMode;// 0: short 1:long
	u8	TxRate;
	u8	FragmentCount;
	u8	EapFix; // For speed up key install

	// For R00 and T00 ----------------------------------------------
	union
	{
		R00_DESCRIPTOR	R00;
		T00_DESCRIPTOR	T00;
	};

	// For R01 and T01 ----------------------------------------------
	union
	{
		R01_DESCRIPTOR	R01;
		T01_DESCRIPTOR	T01;
	};

	// For R02 and T02 ----------------------------------------------
	union
	{
		u32			R02;
		T02_DESCRIPTOR	T02;
	};

	// For R03 and T03 ----------------------------------------------
	// For software used
	union
	{
		u32	R03;
		u32	T03;
		struct
		{
			u8	buffer_number;
			u8	buffer_start_index;
			u16	buffer_total_size;
		};
	};

	// For storing the buffer
	u16	buffer_size[ MAX_DESCRIPTOR_BUFFER_INDEX ];
	void*	buffer_address[ MAX_DESCRIPTOR_BUFFER_INDEX ];//931130.4.q

};


#define DEFAULT_NULL_PACKET_COUNT		180000	//20060828.1 Add. 180 seconds

#define MAX_TXVGA_EEPROM	9	//How many word(u16) of EEPROM will be used for TxVGA
#define MAX_RF_PARAMETER	32

typedef struct _TXVGA_FOR_50 {
	u8      ChanNo;
	u8      TxVgaValue;
} TXVGA_FOR_50;


//=====================================================================
// Device related include
//=====================================================================

#include "wbusb_s.h"
#include "wb35reg_s.h"
#include "wb35tx_s.h"
#include "wb35rx_s.h"

// For Hal using ==================================================================
struct hw_data {
	// For compatible with 33
	u32	revision;
	u32	BB3c_cal; // The value for Tx calibration comes from EEPROM
	u32	BB54_cal; // The value for Rx calibration comes from EEPROM


	// For surprise remove
	u32	SurpriseRemove; // 0: Normal 1: Surprise remove
	u8	IsKeyPreSet;
	u8	CalOneTime; // 20060630.1

	u8	VCO_trim;

	// For Fix 1'st DMA bug
	u32	FragCount;
	u32	DMAFix; //V1_DMA_FIX The variable can be removed if driver want to save mem space for V2.

	//===============================================
	// Definition for MAC address
	//===============================================
	u8		PermanentMacAddress[ETH_ALEN + 2]; // The Enthernet addr that are stored in EEPROM.  + 2 to 8-byte alignment
	u8		CurrentMacAddress[ETH_ALEN + 2]; // The Enthernet addr that are in used.  + 2 to 8-byte alignment

	//=====================================================================
	// Definition for 802.11
	//=====================================================================
	u8	*bssid_pointer; // Used by hal_get_bssid for return value
	u8	bssid[8];// Only 6 byte will be used. 8 byte is required for read buffer
	u8	ssid[32];// maximum ssid length is 32 byte

	u16	AID;
	u8	ssid_length;
	u8	Channel;

	u16	ListenInterval;
	u16	CapabilityInformation;

	u16	BeaconPeriod;
	u16	ProbeDelay;

	u8	bss_type;// 0: IBSS_NET or 1:ESS_NET
	u8	preamble;// 0: short preamble, 1: long preamble
	u8	slot_time_select;// 9 or 20 value
	u8	phy_type;// Phy select

	u32	phy_para[MAX_RF_PARAMETER];
	u32	phy_number;

	u32	CurrentRadioSw; // 20060320.2 0:On 1:Off
	u32	CurrentRadioHw; // 20060825 0:On 1:Off

	u8	*power_save_point;  // Used by hal_get_power_save_mode for return value
	u8	cwmin;
	u8	desired_power_save;
	u8	dtim;// Is running dtim
	u8	mapping_key_replace_index;//In Key table, the next index be replaced 931130.4.r

	u16	MaxReceiveLifeTime;
	u16	FragmentThreshold;
	u16	FragmentThreshold_tmp;
	u16	cwmax;

	u8	Key_slot[MAX_KEY_TABLE][8]; //Ownership record for key slot. For Alignment
	u32	Key_content[MAX_KEY_TABLE][12]; // 10DW for each entry + 2 for burst command( Off and On valid bit)
	u8	CurrentDefaultKeyIndex;
	u32	CurrentDefaultKeyLength;

	//========================================================================
	// Variable for each module
	//========================================================================
	struct wb_usb	WbUsb; // Need WbUsb.h
	struct wb35_reg	reg; // Need Wb35Reg.h
	struct wb35_tx	Wb35Tx; // Need Wb35Tx.h
	struct wb35_rx	Wb35Rx; // Need Wb35Rx.h

	struct timer_list	LEDTimer;// For LED

	u32		LEDpoint;// For LED

    u32         dto_tx_retry_count;         // LA20040210_DTO kevin
    u32         dto_tx_frag_count;          // LA20040210_DTO kevin
    u32         rx_ok_count[13];    // index=0: total rx ok
    //u32         rx_ok_bytes[13];    // index=0, total rx ok bytes
    u32         rx_err_count[13];   // index=0: total rx err

	//for Tx debug
	u32			tx_TBTT_start_count;
	u32			tx_ETR_count;
	u32			tx_WepOn_false_count;
	u32			tx_Null_key_count;
	u32			tx_retry_count[8];

	u8		PowerIndexFromEEPROM; // For 2412MHz
	u8		power_index;
	u8		IsWaitJoinComplete;	// TRUE: set join request
	u8		band;

	u16		SoftwareSet;
	u16		Reserved_s;

	u32		IsInitOK; // 0: Driver starting   1: Driver init OK

	// For Phy calibration
    s32		iq_rsdl_gain_tx_d2;
    s32		iq_rsdl_phase_tx_d2;
	u32		txvga_setting_for_cal; // 20060703.1 Add

	u8		TxVgaSettingInEEPROM[ (((MAX_TXVGA_EEPROM*2)+3) & ~0x03) ]; // 20060621 For backup EEPROM value
	u8		TxVgaFor24[16]; // Max is 14, 2 for alignment
	TXVGA_FOR_50	TxVgaFor50[36];	// 35 channels in 5G. 35x2 = 70 byte. 2 for alignments

	u16		Scan_Interval;
	u16		RESERVED6;

	// LED control
	u32		LED_control;
		// LED_control 4 byte: Gray_Led_1[3]		Gray_Led_0[2]		Led[1]			Led[0]
		// Gray_Led
		//		For Led gray setting
		// Led
		//		0: normal control, LED behavior will decide by EEPROM setting
		//		1: Turn off specific LED
		//		2: Always on specific LED
		//		3: slow blinking specific LED
		//		4: fast blinking specific LED
		//		5: WPS led control is set. Led0 is Red, Led1 id Green
		//			Led[1] is parameter for WPS LED mode
		//				 // 1:InProgress  2: Error 3: Session overlap 4: Success 20061108 control

	u32		LED_LinkOn;		//Turn LED on control
	u32		LED_Scanning;	// Let LED in scan process control
	u32		LED_Blinking; // Temp variable for shining
	u32		RxByteCountLast;
	u32		TxByteCountLast;

	atomic_t	SurpriseRemoveCount;

	// For global timer
	u32		time_count;//TICK_TIME_100ms 1 = 100ms

	// For error recover
	u32		HwStop;

	// 20060828.1 for avoid AP disconnect
	u32		NullPacketCount;

};

#endif
