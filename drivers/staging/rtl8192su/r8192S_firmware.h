#ifndef __INC_FIRMWARE_H
#define __INC_FIRMWARE_H


//#define RTL8190_CPU_START_OFFSET	0x80
/* TODO: this definition is TBD */
//#define USB_HWDESC_HEADER_LEN	0

/* It should be double word alignment */
//#if DEV_BUS_TYPE==PCI_INTERFACE
//#define GET_COMMAND_PACKET_FRAG_THRESHOLD(v)	4*(v/4) - 8
//#else
//#define GET_COMMAND_PACKET_FRAG_THRESHOLD(v)	(4*(v/4) - 8 - USB_HWDESC_HEADER_LEN)
//#endif

//typedef enum _firmware_init_step{
//	FW_INIT_STEP0_BOOT = 0,
//	FW_INIT_STEP1_MAIN = 1,
//	FW_INIT_STEP2_DATA = 2,
//}firmware_init_step_e;

//typedef enum _DESC_PACKET_TYPE{
//	DESC_PACKET_TYPE_INIT = 0,
//	DESC_PACKET_TYPE_NORMAL = 1,
//}DESC_PACKET_TYPE;
#define	RTL8192S_FW_PKT_FRAG_SIZE		0xFF00	// 64K


#define 	RTL8190_MAX_FIRMWARE_CODE_SIZE	64000	//64k
#define	MAX_FIRMWARE_CODE_SIZE	0xFF00 // Firmware Local buffer size.
#define 	RTL8190_CPU_START_OFFSET			0x80

#ifdef RTL8192SE
//It should be double word alignment
#define GET_COMMAND_PACKET_FRAG_THRESHOLD(v)	4*(v/4) - 8
#else
#define GET_COMMAND_PACKET_FRAG_THRESHOLD(v)	(4*(v/4) - 8 - USB_HWDESC_HEADER_LEN)
#endif

//typedef enum _DESC_PACKET_TYPE{
//	DESC_PACKET_TYPE_INIT = 0,
//	DESC_PACKET_TYPE_NORMAL = 1,
//}DESC_PACKET_TYPE;

// Forward declaration.
//typedef	struct _ADAPTER	ADAPTER, *PADAPTER;
#ifdef RTL8192S
typedef enum _firmware_init_step{
	FW_INIT_STEP0_IMEM = 0,
	FW_INIT_STEP1_MAIN = 1,
	FW_INIT_STEP2_DATA = 2,
}firmware_init_step_e;
#else
typedef enum _firmware_init_step{
	FW_INIT_STEP0_BOOT = 0,
	FW_INIT_STEP1_MAIN = 1,
	FW_INIT_STEP2_DATA = 2,
}firmware_init_step_e;
#endif

/* due to rtl8192 firmware */
typedef enum _desc_packet_type_e{
	DESC_PACKET_TYPE_INIT = 0,
	DESC_PACKET_TYPE_NORMAL = 1,
}desc_packet_type_e;

typedef enum _firmware_source{
	FW_SOURCE_IMG_FILE = 0,
	FW_SOURCE_HEADER_FILE = 1,
}firmware_source_e, *pfirmware_source_e;


typedef enum _opt_rst_type{
	OPT_SYSTEM_RESET = 0,
	OPT_FIRMWARE_RESET = 1,
}opt_rst_type_e;

/*typedef enum _FIRMWARE_STATUS{
	FW_STATUS_0_INIT = 0,
	FW_STATUS_1_MOVE_BOOT_CODE = 1,
	FW_STATUS_2_MOVE_MAIN_CODE = 2,
	FW_STATUS_3_TURNON_CPU = 3,
	FW_STATUS_4_MOVE_DATA_CODE = 4,
	FW_STATUS_5_READY = 5,
}FIRMWARE_STATUS;
*/
//--------------------------------------------------------------------------------
// RTL8192S Firmware related, Revised by Roger, 2008.12.18.
//--------------------------------------------------------------------------------
typedef  struct _RT_8192S_FIRMWARE_PRIV { //8-bytes alignment required

	//--- long word 0 ----
	u8		signature_0;		//0x12: CE product, 0x92: IT product
	u8		signature_1;		//0x87: CE product, 0x81: IT product
	u8		hci_sel;			//0x81: PCI-AP, 01:PCIe, 02: 92S-U, 0x82: USB-AP, 0x12: 72S-U, 03:SDIO
	u8		chip_version;	//the same value as reigster value
	u8		customer_ID_0;	//customer  ID low byte
	u8		customer_ID_1;	//customer  ID high byte
	u8		rf_config;		//0x11:  1T1R, 0x12: 1T2R, 0x92: 1T2R turbo, 0x22: 2T2R
	u8		usb_ep_num;	// 4: 4EP, 6: 6EP, 11: 11EP

	//--- long word 1 ----
	u8		regulatory_class_0;	//regulatory class bit map 0
	u8		regulatory_class_1;	//regulatory class bit map 1
	u8		regulatory_class_2;	//regulatory class bit map 2
	u8		regulatory_class_3;	//regulatory class bit map 3
	u8		rfintfs;				// 0:SWSI, 1:HWSI, 2:HWPI
	u8		def_nettype;
	u8		rsvd010;
	u8		rsvd011;


	//--- long word 2 ----
	u8		lbk_mode;	//0x00: normal, 0x03: MACLBK, 0x01: PHYLBK
	u8		mp_mode;	// 1: for MP use, 0: for normal driver (to be discussed)
	u8		rsvd020;
	u8		rsvd021;
	u8		rsvd022;
	u8		rsvd023;
	u8		rsvd024;
	u8		rsvd025;

	//--- long word 3 ----
	u8		qos_en;				// QoS enable
	u8		bw_40MHz_en;		// 40MHz BW enable
	u8		AMSDU2AMPDU_en;	// 4181 convert AMSDU to AMPDU, 0: disable
	u8		AMPDU_en;			// 11n AMPDU enable
	u8		rate_control_offload;//FW offloads, 0: driver handles
	u8		aggregation_offload;	// FW offloads, 0: driver handles
	u8		rsvd030;
	u8		rsvd031;


	//--- long word 4 ----
	unsigned char		beacon_offload;			// 1. FW offloads, 0: driver handles
	unsigned char		MLME_offload;			// 2. FW offloads, 0: driver handles
	unsigned char		hwpc_offload;			// 3. FW offloads, 0: driver handles
	unsigned char		tcp_checksum_offload;	// 4. FW offloads, 0: driver handles
	unsigned char		tcp_offload;				// 5. FW offloads, 0: driver handles
	unsigned char		ps_control_offload;		// 6. FW offloads, 0: driver handles
	unsigned char		WWLAN_offload;			// 7. FW offloads, 0: driver handles
	unsigned char		rsvd040;

	//--- long word 5 ----
	u8		tcp_tx_frame_len_L;		//tcp tx packet length low byte
	u8		tcp_tx_frame_len_H;		//tcp tx packet length high byte
	u8		tcp_rx_frame_len_L;		//tcp rx packet length low byte
	u8		tcp_rx_frame_len_H;		//tcp rx packet length high byte
	u8		rsvd050;
	u8		rsvd051;
	u8		rsvd052;
	u8		rsvd053;
}RT_8192S_FIRMWARE_PRIV, *PRT_8192S_FIRMWARE_PRIV;

typedef struct _RT_8192S_FIRMWARE_HDR {//8-byte alinment required

	//--- LONG WORD 0 ----
	u16		Signature;
	u16		Version;		  //0x8000 ~ 0x8FFF for FPGA version, 0x0000 ~ 0x7FFF for ASIC version,
	u32		DMEMSize;    //define the size of boot loader


	//--- LONG WORD 1 ----
	u32		IMG_IMEM_SIZE;    //define the size of FW in IMEM
	u32		IMG_SRAM_SIZE;    //define the size of FW in SRAM

	//--- LONG WORD 2 ----
	u32		FW_PRIV_SIZE;       //define the size of DMEM variable
	u32		Rsvd0;

	//--- LONG WORD 3 ----
	u32		Rsvd1;
	u32		Rsvd2;

	RT_8192S_FIRMWARE_PRIV	FWPriv;

}RT_8192S_FIRMWARE_HDR, *PRT_8192S_FIRMWARE_HDR;

#define	RT_8192S_FIRMWARE_HDR_SIZE	80
#define   RT_8192S_FIRMWARE_HDR_EXCLUDE_PRI_SIZE	32

typedef enum _FIRMWARE_8192S_STATUS{
	FW_STATUS_INIT = 0,
	FW_STATUS_LOAD_IMEM = 1,
	FW_STATUS_LOAD_EMEM = 2,
	FW_STATUS_LOAD_DMEM = 3,
	FW_STATUS_READY = 4,
}FIRMWARE_8192S_STATUS;

#define RTL8190_MAX_FIRMWARE_CODE_SIZE  64000   //64k

typedef struct _rt_firmware{
	firmware_source_e	eFWSource;
	PRT_8192S_FIRMWARE_HDR	pFwHeader;
	FIRMWARE_8192S_STATUS	FWStatus;
	u16             FirmwareVersion;
	u8		FwIMEM[RTL8190_MAX_FIRMWARE_CODE_SIZE];
	u8		FwEMEM[RTL8190_MAX_FIRMWARE_CODE_SIZE];
	u32		FwIMEMLen;
	u32		FwEMEMLen;
	u8		szFwTmpBuffer[164000];
        u32             szFwTmpBufferLen;
	u16		CmdPacketFragThresold;
}rt_firmware, *prt_firmware;

//typedef struct _RT_FIRMWARE_INFO_8192SU{
//	u8		szInfo[16];
//}RT_FIRMWARE_INFO_8192SU, *PRT_FIRMWARE_INFO_8192SU;
bool FirmwareDownload92S(struct net_device *dev);

#endif

