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

