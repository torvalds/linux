#ifndef __INC_FIRMWARE_H
#define __INC_FIRMWARE_H

#define RTL8190_CPU_START_OFFSET	0x80
/* TODO: this definition is TBD */
//#define USB_HWDESC_HEADER_LEN	0

/* It should be double word alignment */
//#if DEV_BUS_TYPE==PCI_INTERFACE
//#define GET_COMMAND_PACKET_FRAG_THRESHOLD(v)	4*(v/4) - 8
//#else
#define GET_COMMAND_PACKET_FRAG_THRESHOLD(v)	(4*(v/4) - 8 - USB_HWDESC_HEADER_LEN)
//#endif

typedef enum _firmware_init_step{
	FW_INIT_STEP0_BOOT = 0,
	FW_INIT_STEP1_MAIN = 1,
	FW_INIT_STEP2_DATA = 2,
}firmware_init_step_e;

typedef enum _opt_rst_type{
	OPT_SYSTEM_RESET = 0,
	OPT_FIRMWARE_RESET = 1,
}opt_rst_type_e;

/* due to rtl8192 firmware */
typedef enum _desc_packet_type_e{
	DESC_PACKET_TYPE_INIT = 0,
	DESC_PACKET_TYPE_NORMAL = 1,
}desc_packet_type_e;

typedef enum _firmware_source{
	FW_SOURCE_IMG_FILE = 0,
	FW_SOURCE_HEADER_FILE = 1,              //from header file
}firmware_source_e, *pfirmware_source_e;

typedef enum _firmware_status{
	FW_STATUS_0_INIT = 0,
	FW_STATUS_1_MOVE_BOOT_CODE = 1,
	FW_STATUS_2_MOVE_MAIN_CODE = 2,
	FW_STATUS_3_TURNON_CPU = 3,
	FW_STATUS_4_MOVE_DATA_CODE = 4,
	FW_STATUS_5_READY = 5,
}firmware_status_e;

typedef struct _rt_firmare_seg_container {
	u16     seg_size;
	u8      *seg_ptr;
}fw_seg_container, *pfw_seg_container;

#define RTL8190_MAX_FIRMWARE_CODE_SIZE  64000   //64k
#define MAX_FW_INIT_STEP                3
typedef struct _rt_firmware{
	firmware_status_e firmware_status;
	u16               cmdpacket_frag_thresold;
	u8                firmware_buf[MAX_FW_INIT_STEP][RTL8190_MAX_FIRMWARE_CODE_SIZE];
	u16               firmware_buf_size[MAX_FW_INIT_STEP];
}rt_firmware, *prt_firmware;

typedef struct _rt_firmware_info_819xUsb{
	u8		sz_info[16];
}rt_firmware_info_819xUsb, *prt_firmware_info_819xUsb;

#if 0
/* CPU related */
RT_STATUS
CPUCheckMainCodeOKAndTurnOnCPU(
	IN	PADAPTER			Adapter
	);

RT_STATUS
CPUCheckFirmwareReady(
	IN	PADAPTER			Adapter
	);

/* Firmware related */
VOID
FWInitializeParameters(
	IN	PADAPTER		Adapter
	);

RT_STATUS
FWSendDownloadCode(
	IN	PADAPTER		Adapter,
	IN	pu1Byte			CodeVirtualAddrress,
	IN	u4Byte			BufferLen
	);

RT_STATUS
FWSendNullPacket(
	IN	PADAPTER		Adapter,
	IN	u4Byte			Length
	);

RT_STATUS
CmdSendPacket(
	PADAPTER				Adapter,
	PRT_TCB					pTcb,
	PRT_TX_LOCAL_BUFFER 	pBuf,
	u4Byte					BufferLen,
	u4Byte					PacketType,
	BOOLEAN					bLastInitPacket
	);
#endif
#endif

