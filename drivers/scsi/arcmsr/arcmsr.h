/*
*******************************************************************************
**        O.S   : Linux
**   FILE NAME  : arcmsr.h
**        BY    : Nick Cheng
**   Description: SCSI RAID Device Driver for
**                ARECA RAID Host adapter
*******************************************************************************
** Copyright (C) 2002 - 2005, Areca Technology Corporation All rights reserved.
**
**     Web site: www.areca.com.tw
**       E-mail: support@areca.com.tw
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License version 2 as
** published by the Free Software Foundation.
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
*******************************************************************************
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions
** are met:
** 1. Redistributions of source code must retain the above copyright
**    notice, this list of conditions and the following disclaimer.
** 2. Redistributions in binary form must reproduce the above copyright
**    notice, this list of conditions and the following disclaimer in the
**    documentation and/or other materials provided with the distribution.
** 3. The name of the author may not be used to endorse or promote products
**    derived from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
** IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
** OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
** IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
** INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES(INCLUDING, BUT
** NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION)HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
**(INCLUDING NEGLIGENCE OR OTHERWISE)ARISING IN ANY WAY OUT OF THE USE OF
** THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*******************************************************************************
*/
#include <linux/interrupt.h>
struct device_attribute;
/*The limit of outstanding scsi command that firmware can handle*/
#ifdef CONFIG_XEN
	#define ARCMSR_MAX_FREECCB_NUM	160
#define ARCMSR_MAX_OUTSTANDING_CMD	155
#else
	#define ARCMSR_MAX_FREECCB_NUM	320
#define ARCMSR_MAX_OUTSTANDING_CMD	255
#endif
#define ARCMSR_DRIVER_VERSION		"v1.30.00.04-20140919"
#define ARCMSR_SCSI_INITIATOR_ID						255
#define ARCMSR_MAX_XFER_SECTORS							512
#define ARCMSR_MAX_XFER_SECTORS_B						4096
#define ARCMSR_MAX_XFER_SECTORS_C						304
#define ARCMSR_MAX_TARGETID							17
#define ARCMSR_MAX_TARGETLUN							8
#define ARCMSR_MAX_CMD_PERLUN		                 ARCMSR_MAX_OUTSTANDING_CMD
#define ARCMSR_MAX_QBUFFER							4096
#define ARCMSR_DEFAULT_SG_ENTRIES						38
#define ARCMSR_MAX_HBB_POSTQUEUE						264
#define ARCMSR_MAX_ARC1214_POSTQUEUE	256
#define ARCMSR_MAX_ARC1214_DONEQUEUE	257
#define ARCMSR_MAX_XFER_LEN							0x26000 /* 152K */
#define ARCMSR_CDB_SG_PAGE_LENGTH						256 
#define ARCMST_NUM_MSIX_VECTORS		4
#ifndef PCI_DEVICE_ID_ARECA_1880
#define PCI_DEVICE_ID_ARECA_1880 0x1880
 #endif
#ifndef PCI_DEVICE_ID_ARECA_1214
	#define PCI_DEVICE_ID_ARECA_1214	0x1214
#endif
/*
**********************************************************************************
**
**********************************************************************************
*/
#define ARC_SUCCESS                                                       0
#define ARC_FAILURE                                                       1
/*
*******************************************************************************
**        split 64bits dma addressing
*******************************************************************************
*/
#define dma_addr_hi32(addr)               (uint32_t) ((addr>>16)>>16)
#define dma_addr_lo32(addr)               (uint32_t) (addr & 0xffffffff)
/*
*******************************************************************************
**        MESSAGE CONTROL CODE
*******************************************************************************
*/
struct CMD_MESSAGE
{
      uint32_t HeaderLength;
      uint8_t  Signature[8];
      uint32_t Timeout;
      uint32_t ControlCode;
      uint32_t ReturnCode;
      uint32_t Length;
};
/*
*******************************************************************************
**        IOP Message Transfer Data for user space
*******************************************************************************
*/
#define	ARCMSR_API_DATA_BUFLEN	1032
struct CMD_MESSAGE_FIELD
{
    struct CMD_MESSAGE			cmdmessage;
    uint8_t				messagedatabuffer[ARCMSR_API_DATA_BUFLEN];
};
/* IOP message transfer */
#define ARCMSR_MESSAGE_FAIL			0x0001
/* DeviceType */
#define ARECA_SATA_RAID				0x90000000
/* FunctionCode */
#define FUNCTION_READ_RQBUFFER			0x0801
#define FUNCTION_WRITE_WQBUFFER			0x0802
#define FUNCTION_CLEAR_RQBUFFER			0x0803
#define FUNCTION_CLEAR_WQBUFFER			0x0804
#define FUNCTION_CLEAR_ALLQBUFFER		0x0805
#define FUNCTION_RETURN_CODE_3F			0x0806
#define FUNCTION_SAY_HELLO			0x0807
#define FUNCTION_SAY_GOODBYE			0x0808
#define FUNCTION_FLUSH_ADAPTER_CACHE		0x0809
#define FUNCTION_GET_FIRMWARE_STATUS			0x080A
#define FUNCTION_HARDWARE_RESET			0x080B
/* ARECA IO CONTROL CODE*/
#define ARCMSR_MESSAGE_READ_RQBUFFER       \
	ARECA_SATA_RAID | FUNCTION_READ_RQBUFFER
#define ARCMSR_MESSAGE_WRITE_WQBUFFER      \
	ARECA_SATA_RAID | FUNCTION_WRITE_WQBUFFER
#define ARCMSR_MESSAGE_CLEAR_RQBUFFER      \
	ARECA_SATA_RAID | FUNCTION_CLEAR_RQBUFFER
#define ARCMSR_MESSAGE_CLEAR_WQBUFFER      \
	ARECA_SATA_RAID | FUNCTION_CLEAR_WQBUFFER
#define ARCMSR_MESSAGE_CLEAR_ALLQBUFFER    \
	ARECA_SATA_RAID | FUNCTION_CLEAR_ALLQBUFFER
#define ARCMSR_MESSAGE_RETURN_CODE_3F      \
	ARECA_SATA_RAID | FUNCTION_RETURN_CODE_3F
#define ARCMSR_MESSAGE_SAY_HELLO           \
	ARECA_SATA_RAID | FUNCTION_SAY_HELLO
#define ARCMSR_MESSAGE_SAY_GOODBYE         \
	ARECA_SATA_RAID | FUNCTION_SAY_GOODBYE
#define ARCMSR_MESSAGE_FLUSH_ADAPTER_CACHE \
	ARECA_SATA_RAID | FUNCTION_FLUSH_ADAPTER_CACHE
/* ARECA IOCTL ReturnCode */
#define ARCMSR_MESSAGE_RETURNCODE_OK		0x00000001
#define ARCMSR_MESSAGE_RETURNCODE_ERROR		0x00000006
#define ARCMSR_MESSAGE_RETURNCODE_3F		0x0000003F
#define ARCMSR_MESSAGE_RETURNCODE_BUS_HANG_ON	0x00000088
/*
*************************************************************
**   structure for holding DMA address data
*************************************************************
*/
#define IS_DMA64			(sizeof(dma_addr_t) == 8)
#define IS_SG64_ADDR                0x01000000 /* bit24 */
struct  SG32ENTRY
{
	__le32					length;
	__le32					address;
}__attribute__ ((packed));
struct  SG64ENTRY
{
	__le32					length;
	__le32					address;
	__le32					addresshigh;
}__attribute__ ((packed));
/*
********************************************************************
**      Q Buffer of IOP Message Transfer
********************************************************************
*/
struct QBUFFER
{
	uint32_t      data_len;
	uint8_t       data[124];
};
/*
*******************************************************************************
**      FIRMWARE INFO for Intel IOP R 80331 processor (Type A)
*******************************************************************************
*/
struct FIRMWARE_INFO
{
	uint32_t      signature;		/*0, 00-03*/
	uint32_t      request_len;		/*1, 04-07*/
	uint32_t      numbers_queue;		/*2, 08-11*/
	uint32_t      sdram_size;               /*3, 12-15*/
	uint32_t      ide_channels;		/*4, 16-19*/
	char          vendor[40];		/*5, 20-59*/
	char          model[8];			/*15, 60-67*/
	char          firmware_ver[16];     	/*17, 68-83*/
	char          device_map[16];		/*21, 84-99*/
	uint32_t		cfgVersion;               	/*25,100-103 Added for checking of new firmware capability*/
	uint8_t		cfgSerial[16];           	/*26,104-119*/
	uint32_t		cfgPicStatus;            	/*30,120-123*/	
};
/* signature of set and get firmware config */
#define ARCMSR_SIGNATURE_GET_CONFIG		      0x87974060
#define ARCMSR_SIGNATURE_SET_CONFIG		      0x87974063
/* message code of inbound message register */
#define ARCMSR_INBOUND_MESG0_NOP		      0x00000000
#define ARCMSR_INBOUND_MESG0_GET_CONFIG		      0x00000001
#define ARCMSR_INBOUND_MESG0_SET_CONFIG               0x00000002
#define ARCMSR_INBOUND_MESG0_ABORT_CMD                0x00000003
#define ARCMSR_INBOUND_MESG0_STOP_BGRB                0x00000004
#define ARCMSR_INBOUND_MESG0_FLUSH_CACHE              0x00000005
#define ARCMSR_INBOUND_MESG0_START_BGRB               0x00000006
#define ARCMSR_INBOUND_MESG0_CHK331PENDING            0x00000007
#define ARCMSR_INBOUND_MESG0_SYNC_TIMER               0x00000008
/* doorbell interrupt generator */
#define ARCMSR_INBOUND_DRIVER_DATA_WRITE_OK           0x00000001
#define ARCMSR_INBOUND_DRIVER_DATA_READ_OK            0x00000002
#define ARCMSR_OUTBOUND_IOP331_DATA_WRITE_OK          0x00000001
#define ARCMSR_OUTBOUND_IOP331_DATA_READ_OK           0x00000002
/* ccb areca cdb flag */
#define ARCMSR_CCBPOST_FLAG_SGL_BSIZE                 0x80000000
#define ARCMSR_CCBPOST_FLAG_IAM_BIOS                  0x40000000
#define ARCMSR_CCBREPLY_FLAG_IAM_BIOS                 0x40000000
#define ARCMSR_CCBREPLY_FLAG_ERROR_MODE0              0x10000000
#define ARCMSR_CCBREPLY_FLAG_ERROR_MODE1              0x00000001
/* outbound firmware ok */
#define ARCMSR_OUTBOUND_MESG1_FIRMWARE_OK             0x80000000
/* ARC-1680 Bus Reset*/
#define ARCMSR_ARC1680_BUS_RESET				0x00000003
/* ARC-1880 Bus Reset*/
#define ARCMSR_ARC1880_RESET_ADAPTER				0x00000024
#define ARCMSR_ARC1880_DiagWrite_ENABLE			0x00000080

/*
************************************************************************
**                SPEC. for Areca Type B adapter
************************************************************************
*/
/* ARECA HBB COMMAND for its FIRMWARE */
/* window of "instruction flags" from driver to iop */
#define ARCMSR_DRV2IOP_DOORBELL                       0x00020400
#define ARCMSR_DRV2IOP_DOORBELL_MASK                  0x00020404
/* window of "instruction flags" from iop to driver */
#define ARCMSR_IOP2DRV_DOORBELL                       0x00020408
#define ARCMSR_IOP2DRV_DOORBELL_MASK                  0x0002040C
/* ARECA FLAG LANGUAGE */
/* ioctl transfer */
#define ARCMSR_IOP2DRV_DATA_WRITE_OK                  0x00000001
/* ioctl transfer */
#define ARCMSR_IOP2DRV_DATA_READ_OK                   0x00000002
#define ARCMSR_IOP2DRV_CDB_DONE                       0x00000004
#define ARCMSR_IOP2DRV_MESSAGE_CMD_DONE               0x00000008

#define ARCMSR_DOORBELL_HANDLE_INT		      0x0000000F
#define ARCMSR_DOORBELL_INT_CLEAR_PATTERN   	      0xFF00FFF0
#define ARCMSR_MESSAGE_INT_CLEAR_PATTERN	      0xFF00FFF7
/* (ARCMSR_INBOUND_MESG0_GET_CONFIG<<16)|ARCMSR_DRV2IOP_MESSAGE_CMD_POSTED) */
#define ARCMSR_MESSAGE_GET_CONFIG		      0x00010008
/* (ARCMSR_INBOUND_MESG0_SET_CONFIG<<16)|ARCMSR_DRV2IOP_MESSAGE_CMD_POSTED) */
#define ARCMSR_MESSAGE_SET_CONFIG		      0x00020008
/* (ARCMSR_INBOUND_MESG0_ABORT_CMD<<16)|ARCMSR_DRV2IOP_MESSAGE_CMD_POSTED) */
#define ARCMSR_MESSAGE_ABORT_CMD		      0x00030008
/* (ARCMSR_INBOUND_MESG0_STOP_BGRB<<16)|ARCMSR_DRV2IOP_MESSAGE_CMD_POSTED) */
#define ARCMSR_MESSAGE_STOP_BGRB		      0x00040008
/* (ARCMSR_INBOUND_MESG0_FLUSH_CACHE<<16)|ARCMSR_DRV2IOP_MESSAGE_CMD_POSTED) */
#define ARCMSR_MESSAGE_FLUSH_CACHE                    0x00050008
/* (ARCMSR_INBOUND_MESG0_START_BGRB<<16)|ARCMSR_DRV2IOP_MESSAGE_CMD_POSTED) */
#define ARCMSR_MESSAGE_START_BGRB		      0x00060008
#define ARCMSR_MESSAGE_START_DRIVER_MODE	      0x000E0008
#define ARCMSR_MESSAGE_SET_POST_WINDOW		      0x000F0008
#define ARCMSR_MESSAGE_ACTIVE_EOI_MODE		    0x00100008
/* ARCMSR_OUTBOUND_MESG1_FIRMWARE_OK */
#define ARCMSR_MESSAGE_FIRMWARE_OK		      0x80000000
/* ioctl transfer */
#define ARCMSR_DRV2IOP_DATA_WRITE_OK                  0x00000001
/* ioctl transfer */
#define ARCMSR_DRV2IOP_DATA_READ_OK                   0x00000002
#define ARCMSR_DRV2IOP_CDB_POSTED                     0x00000004
#define ARCMSR_DRV2IOP_MESSAGE_CMD_POSTED             0x00000008
#define ARCMSR_DRV2IOP_END_OF_INTERRUPT		0x00000010

/* data tunnel buffer between user space program and its firmware */
/* user space data to iop 128bytes */
#define ARCMSR_MESSAGE_WBUFFER			      0x0000fe00
/* iop data to user space 128bytes */
#define ARCMSR_MESSAGE_RBUFFER			      0x0000ff00
/* iop message_rwbuffer for message command */
#define ARCMSR_MESSAGE_RWBUFFER			      0x0000fa00
/* 
************************************************************************
**                SPEC. for Areca HBC adapter
************************************************************************
*/
#define ARCMSR_HBC_ISR_THROTTLING_LEVEL		12
#define ARCMSR_HBC_ISR_MAX_DONE_QUEUE		20
/* Host Interrupt Mask */
#define ARCMSR_HBCMU_UTILITY_A_ISR_MASK		0x00000001 /* When clear, the Utility_A interrupt routes to the host.*/
#define ARCMSR_HBCMU_OUTBOUND_DOORBELL_ISR_MASK	0x00000004 /* When clear, the General Outbound Doorbell interrupt routes to the host.*/
#define ARCMSR_HBCMU_OUTBOUND_POSTQUEUE_ISR_MASK	0x00000008 /* When clear, the Outbound Post List FIFO Not Empty interrupt routes to the host.*/
#define ARCMSR_HBCMU_ALL_INTMASKENABLE		0x0000000D /* disable all ISR */
/* Host Interrupt Status */
#define ARCMSR_HBCMU_UTILITY_A_ISR			0x00000001
	/*
	** Set when the Utility_A Interrupt bit is set in the Outbound Doorbell Register.
	** It clears by writing a 1 to the Utility_A bit in the Outbound Doorbell Clear Register or through automatic clearing (if enabled).
	*/
#define ARCMSR_HBCMU_OUTBOUND_DOORBELL_ISR		0x00000004
	/*
	** Set if Outbound Doorbell register bits 30:1 have a non-zero
	** value. This bit clears only when Outbound Doorbell bits
	** 30:1 are ALL clear. Only a write to the Outbound Doorbell
	** Clear register clears bits in the Outbound Doorbell register.
	*/
#define ARCMSR_HBCMU_OUTBOUND_POSTQUEUE_ISR	0x00000008
	/*
	** Set whenever the Outbound Post List Producer/Consumer
	** Register (FIFO) is not empty. It clears when the Outbound
	** Post List FIFO is empty.
	*/
#define ARCMSR_HBCMU_SAS_ALL_INT			0x00000010
	/*
	** This bit indicates a SAS interrupt from a source external to
	** the PCIe core. This bit is not maskable.
	*/
	/* DoorBell*/
#define ARCMSR_HBCMU_DRV2IOP_DATA_WRITE_OK			0x00000002
#define ARCMSR_HBCMU_DRV2IOP_DATA_READ_OK			0x00000004
	/*inbound message 0 ready*/
#define ARCMSR_HBCMU_DRV2IOP_MESSAGE_CMD_DONE		0x00000008
	/*more than 12 request completed in a time*/
#define ARCMSR_HBCMU_DRV2IOP_POSTQUEUE_THROTTLING		0x00000010
#define ARCMSR_HBCMU_IOP2DRV_DATA_WRITE_OK			0x00000002
	/*outbound DATA WRITE isr door bell clear*/
#define ARCMSR_HBCMU_IOP2DRV_DATA_WRITE_DOORBELL_CLEAR	0x00000002
#define ARCMSR_HBCMU_IOP2DRV_DATA_READ_OK			0x00000004
	/*outbound DATA READ isr door bell clear*/
#define ARCMSR_HBCMU_IOP2DRV_DATA_READ_DOORBELL_CLEAR	0x00000004
	/*outbound message 0 ready*/
#define ARCMSR_HBCMU_IOP2DRV_MESSAGE_CMD_DONE		0x00000008
	/*outbound message cmd isr door bell clear*/
#define ARCMSR_HBCMU_IOP2DRV_MESSAGE_CMD_DONE_DOORBELL_CLEAR	0x00000008
	/*ARCMSR_HBAMU_MESSAGE_FIRMWARE_OK*/
#define ARCMSR_HBCMU_MESSAGE_FIRMWARE_OK			0x80000000
/*
*******************************************************************************
**                SPEC. for Areca Type D adapter
*******************************************************************************
*/
#define ARCMSR_ARC1214_CHIP_ID				0x00004
#define ARCMSR_ARC1214_CPU_MEMORY_CONFIGURATION		0x00008
#define ARCMSR_ARC1214_I2_HOST_INTERRUPT_MASK		0x00034
#define ARCMSR_ARC1214_SAMPLE_RESET			0x00100
#define ARCMSR_ARC1214_RESET_REQUEST			0x00108
#define ARCMSR_ARC1214_MAIN_INTERRUPT_STATUS		0x00200
#define ARCMSR_ARC1214_PCIE_F0_INTERRUPT_ENABLE		0x0020C
#define ARCMSR_ARC1214_INBOUND_MESSAGE0			0x00400
#define ARCMSR_ARC1214_INBOUND_MESSAGE1			0x00404
#define ARCMSR_ARC1214_OUTBOUND_MESSAGE0		0x00420
#define ARCMSR_ARC1214_OUTBOUND_MESSAGE1		0x00424
#define ARCMSR_ARC1214_INBOUND_DOORBELL			0x00460
#define ARCMSR_ARC1214_OUTBOUND_DOORBELL		0x00480
#define ARCMSR_ARC1214_OUTBOUND_DOORBELL_ENABLE		0x00484
#define ARCMSR_ARC1214_INBOUND_LIST_BASE_LOW		0x01000
#define ARCMSR_ARC1214_INBOUND_LIST_BASE_HIGH		0x01004
#define ARCMSR_ARC1214_INBOUND_LIST_WRITE_POINTER	0x01018
#define ARCMSR_ARC1214_OUTBOUND_LIST_BASE_LOW		0x01060
#define ARCMSR_ARC1214_OUTBOUND_LIST_BASE_HIGH		0x01064
#define ARCMSR_ARC1214_OUTBOUND_LIST_COPY_POINTER	0x0106C
#define ARCMSR_ARC1214_OUTBOUND_LIST_READ_POINTER	0x01070
#define ARCMSR_ARC1214_OUTBOUND_INTERRUPT_CAUSE		0x01088
#define ARCMSR_ARC1214_OUTBOUND_INTERRUPT_ENABLE	0x0108C
#define ARCMSR_ARC1214_MESSAGE_WBUFFER			0x02000
#define ARCMSR_ARC1214_MESSAGE_RBUFFER			0x02100
#define ARCMSR_ARC1214_MESSAGE_RWBUFFER			0x02200
/* Host Interrupt Mask */
#define ARCMSR_ARC1214_ALL_INT_ENABLE			0x00001010
#define ARCMSR_ARC1214_ALL_INT_DISABLE			0x00000000
/* Host Interrupt Status */
#define ARCMSR_ARC1214_OUTBOUND_DOORBELL_ISR		0x00001000
#define ARCMSR_ARC1214_OUTBOUND_POSTQUEUE_ISR		0x00000010
/* DoorBell*/
#define ARCMSR_ARC1214_DRV2IOP_DATA_IN_READY		0x00000001
#define ARCMSR_ARC1214_DRV2IOP_DATA_OUT_READ		0x00000002
/*inbound message 0 ready*/
#define ARCMSR_ARC1214_IOP2DRV_DATA_WRITE_OK		0x00000001
/*outbound DATA WRITE isr door bell clear*/
#define ARCMSR_ARC1214_IOP2DRV_DATA_READ_OK		0x00000002
/*outbound message 0 ready*/
#define ARCMSR_ARC1214_IOP2DRV_MESSAGE_CMD_DONE		0x02000000
/*outbound message cmd isr door bell clear*/
/*ARCMSR_HBAMU_MESSAGE_FIRMWARE_OK*/
#define ARCMSR_ARC1214_MESSAGE_FIRMWARE_OK		0x80000000
#define ARCMSR_ARC1214_OUTBOUND_LIST_INTERRUPT_CLEAR	0x00000001
/*
*******************************************************************************
**    ARECA SCSI COMMAND DESCRIPTOR BLOCK size 0x1F8 (504)
*******************************************************************************
*/
struct ARCMSR_CDB
{
	uint8_t							Bus;
	uint8_t							TargetID;
	uint8_t							LUN;
	uint8_t							Function;
	uint8_t							CdbLength;
	uint8_t							sgcount;
	uint8_t							Flags;
#define ARCMSR_CDB_FLAG_SGL_BSIZE          0x01
#define ARCMSR_CDB_FLAG_BIOS               0x02
#define ARCMSR_CDB_FLAG_WRITE              0x04
#define ARCMSR_CDB_FLAG_SIMPLEQ            0x00
#define ARCMSR_CDB_FLAG_HEADQ              0x08
#define ARCMSR_CDB_FLAG_ORDEREDQ           0x10

	uint8_t							msgPages;
	uint32_t						msgContext;
	uint32_t						DataLength;
	uint8_t							Cdb[16];
	uint8_t							DeviceStatus;
#define ARCMSR_DEV_CHECK_CONDITION	    0x02
#define ARCMSR_DEV_SELECT_TIMEOUT	    0xF0
#define ARCMSR_DEV_ABORTED		    0xF1
#define ARCMSR_DEV_INIT_FAIL		    0xF2

	uint8_t							SenseData[15];
	union
	{
		struct SG32ENTRY                sg32entry[1];
		struct SG64ENTRY                sg64entry[1];
	} u;
};
/*
*******************************************************************************
**     Messaging Unit (MU) of the Intel R 80331 I/O processor(Type A) and Type B processor
*******************************************************************************
*/
struct MessageUnit_A
{
	uint32_t	resrved0[4];			/*0000 000F*/
	uint32_t	inbound_msgaddr0;		/*0010 0013*/
	uint32_t	inbound_msgaddr1;		/*0014 0017*/
	uint32_t	outbound_msgaddr0;		/*0018 001B*/
	uint32_t	outbound_msgaddr1;		/*001C 001F*/
	uint32_t	inbound_doorbell;		/*0020 0023*/
	uint32_t	inbound_intstatus;		/*0024 0027*/
	uint32_t	inbound_intmask;		/*0028 002B*/
	uint32_t	outbound_doorbell;		/*002C 002F*/
	uint32_t	outbound_intstatus;		/*0030 0033*/
	uint32_t	outbound_intmask;		/*0034 0037*/
	uint32_t	reserved1[2];			/*0038 003F*/
	uint32_t	inbound_queueport;		/*0040 0043*/
	uint32_t	outbound_queueport;     	/*0044 0047*/
	uint32_t	reserved2[2];			/*0048 004F*/
	uint32_t	reserved3[492];			/*0050 07FF 492*/
	uint32_t	reserved4[128];			/*0800 09FF 128*/
	uint32_t	message_rwbuffer[256];		/*0a00 0DFF 256*/
	uint32_t	message_wbuffer[32];		/*0E00 0E7F  32*/
	uint32_t	reserved5[32];			/*0E80 0EFF  32*/
	uint32_t	message_rbuffer[32];		/*0F00 0F7F  32*/
	uint32_t	reserved6[32];			/*0F80 0FFF  32*/
};

struct MessageUnit_B
{
	uint32_t	post_qbuffer[ARCMSR_MAX_HBB_POSTQUEUE];
	uint32_t	done_qbuffer[ARCMSR_MAX_HBB_POSTQUEUE];
	uint32_t	postq_index;
	uint32_t	doneq_index;
	uint32_t		__iomem *drv2iop_doorbell;
	uint32_t		__iomem *drv2iop_doorbell_mask;
	uint32_t		__iomem *iop2drv_doorbell;
	uint32_t		__iomem *iop2drv_doorbell_mask;
	uint32_t		__iomem *message_rwbuffer;
	uint32_t		__iomem *message_wbuffer;
	uint32_t		__iomem *message_rbuffer;
};
/*
*********************************************************************
** LSI
*********************************************************************
*/
struct MessageUnit_C{
	uint32_t	message_unit_status;			/*0000 0003*/
	uint32_t	slave_error_attribute;			/*0004 0007*/
	uint32_t	slave_error_address;			/*0008 000B*/
	uint32_t	posted_outbound_doorbell;		/*000C 000F*/
	uint32_t	master_error_attribute;			/*0010 0013*/
	uint32_t	master_error_address_low;		/*0014 0017*/
	uint32_t	master_error_address_high;		/*0018 001B*/
	uint32_t	hcb_size;				/*001C 001F*/
	uint32_t	inbound_doorbell;			/*0020 0023*/
	uint32_t	diagnostic_rw_data;			/*0024 0027*/
	uint32_t	diagnostic_rw_address_low;		/*0028 002B*/
	uint32_t	diagnostic_rw_address_high;		/*002C 002F*/
	uint32_t	host_int_status;				/*0030 0033*/
	uint32_t	host_int_mask;				/*0034 0037*/
	uint32_t	dcr_data;				/*0038 003B*/
	uint32_t	dcr_address;				/*003C 003F*/
	uint32_t	inbound_queueport;			/*0040 0043*/
	uint32_t	outbound_queueport;			/*0044 0047*/
	uint32_t	hcb_pci_address_low;			/*0048 004B*/
	uint32_t	hcb_pci_address_high;			/*004C 004F*/
	uint32_t	iop_int_status;				/*0050 0053*/
	uint32_t	iop_int_mask;				/*0054 0057*/
	uint32_t	iop_inbound_queue_port;			/*0058 005B*/
	uint32_t	iop_outbound_queue_port;		/*005C 005F*/
	uint32_t	inbound_free_list_index;			/*0060 0063*/
	uint32_t	inbound_post_list_index;			/*0064 0067*/
	uint32_t	outbound_free_list_index;			/*0068 006B*/
	uint32_t	outbound_post_list_index;			/*006C 006F*/
	uint32_t	inbound_doorbell_clear;			/*0070 0073*/
	uint32_t	i2o_message_unit_control;			/*0074 0077*/
	uint32_t	last_used_message_source_address_low;	/*0078 007B*/
	uint32_t	last_used_message_source_address_high;	/*007C 007F*/
	uint32_t	pull_mode_data_byte_count[4];		/*0080 008F*/
	uint32_t	message_dest_address_index;		/*0090 0093*/
	uint32_t	done_queue_not_empty_int_counter_timer;	/*0094 0097*/
	uint32_t	utility_A_int_counter_timer;		/*0098 009B*/
	uint32_t	outbound_doorbell;			/*009C 009F*/
	uint32_t	outbound_doorbell_clear;			/*00A0 00A3*/
	uint32_t	message_source_address_index;		/*00A4 00A7*/
	uint32_t	message_done_queue_index;		/*00A8 00AB*/
	uint32_t	reserved0;				/*00AC 00AF*/
	uint32_t	inbound_msgaddr0;			/*00B0 00B3*/
	uint32_t	inbound_msgaddr1;			/*00B4 00B7*/
	uint32_t	outbound_msgaddr0;			/*00B8 00BB*/
	uint32_t	outbound_msgaddr1;			/*00BC 00BF*/
	uint32_t	inbound_queueport_low;			/*00C0 00C3*/
	uint32_t	inbound_queueport_high;			/*00C4 00C7*/
	uint32_t	outbound_queueport_low;			/*00C8 00CB*/
	uint32_t	outbound_queueport_high;		/*00CC 00CF*/
	uint32_t	iop_inbound_queue_port_low;		/*00D0 00D3*/
	uint32_t	iop_inbound_queue_port_high;		/*00D4 00D7*/
	uint32_t	iop_outbound_queue_port_low;		/*00D8 00DB*/
	uint32_t	iop_outbound_queue_port_high;		/*00DC 00DF*/
	uint32_t	message_dest_queue_port_low;		/*00E0 00E3*/
	uint32_t	message_dest_queue_port_high;		/*00E4 00E7*/
	uint32_t	last_used_message_dest_address_low;	/*00E8 00EB*/
	uint32_t	last_used_message_dest_address_high;	/*00EC 00EF*/
	uint32_t	message_done_queue_base_address_low;	/*00F0 00F3*/
	uint32_t	message_done_queue_base_address_high;	/*00F4 00F7*/
	uint32_t	host_diagnostic;				/*00F8 00FB*/
	uint32_t	write_sequence;				/*00FC 00FF*/
	uint32_t	reserved1[34];				/*0100 0187*/
	uint32_t	reserved2[1950];				/*0188 1FFF*/
	uint32_t	message_wbuffer[32];			/*2000 207F*/
	uint32_t	reserved3[32];				/*2080 20FF*/
	uint32_t	message_rbuffer[32];			/*2100 217F*/
	uint32_t	reserved4[32];				/*2180 21FF*/
	uint32_t	msgcode_rwbuffer[256];			/*2200 23FF*/
};
/*
*********************************************************************
**     Messaging Unit (MU) of Type D processor
*********************************************************************
*/
struct InBound_SRB {
	uint32_t addressLow; /* pointer to SRB block */
	uint32_t addressHigh;
	uint32_t length; /* in DWORDs */
	uint32_t reserved0;
};

struct OutBound_SRB {
	uint32_t addressLow; /* pointer to SRB block */
	uint32_t addressHigh;
};

struct MessageUnit_D {
	struct InBound_SRB	post_qbuffer[ARCMSR_MAX_ARC1214_POSTQUEUE];
	volatile struct OutBound_SRB
				done_qbuffer[ARCMSR_MAX_ARC1214_DONEQUEUE];
	u16 postq_index;
	volatile u16 doneq_index;
	u32 __iomem *chip_id;			/* 0x00004 */
	u32 __iomem *cpu_mem_config;		/* 0x00008 */
	u32 __iomem *i2o_host_interrupt_mask;	/* 0x00034 */
	u32 __iomem *sample_at_reset;		/* 0x00100 */
	u32 __iomem *reset_request;		/* 0x00108 */
	u32 __iomem *host_int_status;		/* 0x00200 */
	u32 __iomem *pcief0_int_enable;		/* 0x0020C */
	u32 __iomem *inbound_msgaddr0;		/* 0x00400 */
	u32 __iomem *inbound_msgaddr1;		/* 0x00404 */
	u32 __iomem *outbound_msgaddr0;		/* 0x00420 */
	u32 __iomem *outbound_msgaddr1;		/* 0x00424 */
	u32 __iomem *inbound_doorbell;		/* 0x00460 */
	u32 __iomem *outbound_doorbell;		/* 0x00480 */
	u32 __iomem *outbound_doorbell_enable;	/* 0x00484 */
	u32 __iomem *inboundlist_base_low;	/* 0x01000 */
	u32 __iomem *inboundlist_base_high;	/* 0x01004 */
	u32 __iomem *inboundlist_write_pointer;	/* 0x01018 */
	u32 __iomem *outboundlist_base_low;	/* 0x01060 */
	u32 __iomem *outboundlist_base_high;	/* 0x01064 */
	u32 __iomem *outboundlist_copy_pointer;	/* 0x0106C */
	u32 __iomem *outboundlist_read_pointer;	/* 0x01070 0x01072 */
	u32 __iomem *outboundlist_interrupt_cause;	/* 0x1088 */
	u32 __iomem *outboundlist_interrupt_enable;	/* 0x108C */
	u32 __iomem *message_wbuffer;		/* 0x2000 */
	u32 __iomem *message_rbuffer;		/* 0x2100 */
	u32 __iomem *msgcode_rwbuffer;		/* 0x2200 */
};
/*
*******************************************************************************
**                 Adapter Control Block
*******************************************************************************
*/
struct AdapterControlBlock
{
	uint32_t  adapter_type;                /* adapter A,B..... */
	#define ACB_ADAPTER_TYPE_A            0x00000001	/* hba I IOP */
	#define ACB_ADAPTER_TYPE_B            0x00000002	/* hbb M IOP */
	#define ACB_ADAPTER_TYPE_C            0x00000004	/* hbc P IOP */
	#define ACB_ADAPTER_TYPE_D            0x00000008	/* hbd A IOP */
	u32				roundup_ccbsize;
	struct pci_dev *		pdev;
	struct Scsi_Host *		host;
	unsigned long			vir2phy_offset;
	struct msix_entry	entries[ARCMST_NUM_MSIX_VECTORS];
	/* Offset is used in making arc cdb physical to virtual calculations */
	uint32_t			outbound_int_enable;
	uint32_t			cdb_phyaddr_hi32;
	uint32_t			reg_mu_acc_handle0;
	spinlock_t                      			eh_lock;
	spinlock_t                      			ccblist_lock;
	spinlock_t			postq_lock;
	spinlock_t			doneq_lock;
	spinlock_t			rqbuffer_lock;
	spinlock_t			wqbuffer_lock;
	union {
		struct MessageUnit_A __iomem *pmuA;
		struct MessageUnit_B 	*pmuB;
		struct MessageUnit_C __iomem *pmuC;
		struct MessageUnit_D 	*pmuD;
	};
	/* message unit ATU inbound base address0 */
	void __iomem *mem_base0;
	void __iomem *mem_base1;
	uint32_t			acb_flags;
	u16			dev_id;
	uint8_t                   		adapter_index;
	#define ACB_F_SCSISTOPADAPTER         	0x0001
	#define ACB_F_MSG_STOP_BGRB     	0x0002
	/* stop RAID background rebuild */
	#define ACB_F_MSG_START_BGRB          	0x0004
	/* stop RAID background rebuild */
	#define ACB_F_IOPDATA_OVERFLOW        	0x0008
	/* iop message data rqbuffer overflow */
	#define ACB_F_MESSAGE_WQBUFFER_CLEARED	0x0010
	/* message clear wqbuffer */
	#define ACB_F_MESSAGE_RQBUFFER_CLEARED  0x0020
	/* message clear rqbuffer */
	#define ACB_F_MESSAGE_WQBUFFER_READED   0x0040
	#define ACB_F_BUS_RESET               	0x0080
	#define ACB_F_BUS_HANG_ON		0x0800/* need hardware reset bus */

	#define ACB_F_IOP_INITED              	0x0100
	/* iop init */
	#define ACB_F_ABORT				0x0200
	#define ACB_F_FIRMWARE_TRAP           		0x0400
	#define ACB_F_MSI_ENABLED		0x1000
	#define ACB_F_MSIX_ENABLED		0x2000
	struct CommandControlBlock *			pccb_pool[ARCMSR_MAX_FREECCB_NUM];
	/* used for memory free */
	struct list_head		ccb_free_list;
	/* head of free ccb list */

	atomic_t			ccboutstandingcount;
	/*The present outstanding command number that in the IOP that
					waiting for being handled by FW*/

	void *				dma_coherent;
	/* dma_coherent used for memory free */
	dma_addr_t			dma_coherent_handle;
	/* dma_coherent_handle used for memory free */
	dma_addr_t				dma_coherent_handle2;
	void				*dma_coherent2;
	unsigned int				uncache_size;
	uint8_t				rqbuffer[ARCMSR_MAX_QBUFFER];
	/* data collection buffer for read from 80331 */
	int32_t				rqbuf_getIndex;
	/* first of read buffer  */
	int32_t				rqbuf_putIndex;
	/* last of read buffer   */
	uint8_t				wqbuffer[ARCMSR_MAX_QBUFFER];
	/* data collection buffer for write to 80331  */
	int32_t				wqbuf_getIndex;
	/* first of write buffer */
	int32_t				wqbuf_putIndex;
	/* last of write buffer  */
	uint8_t				devstate[ARCMSR_MAX_TARGETID][ARCMSR_MAX_TARGETLUN];
	/* id0 ..... id15, lun0...lun7 */
#define ARECA_RAID_GONE               0x55
#define ARECA_RAID_GOOD               0xaa
	uint32_t			num_resets;
	uint32_t			num_aborts;
	uint32_t			signature;
	uint32_t			firm_request_len;
	uint32_t			firm_numbers_queue;
	uint32_t			firm_sdram_size;
	uint32_t			firm_hd_channels;
	uint32_t                           	firm_cfg_version;	
	char			firm_model[12];
	char			firm_version[20];
	char			device_map[20];			/*21,84-99*/
	struct work_struct 		arcmsr_do_message_isr_bh;
	struct timer_list		eternal_timer;
	unsigned short		fw_flag;
				#define	FW_NORMAL	0x0000
				#define	FW_BOG		0x0001
				#define	FW_DEADLOCK	0x0010
	atomic_t 			rq_map_token;
	atomic_t			ante_token_value;
	uint32_t	maxOutstanding;
	int		msix_vector_count;
};/* HW_DEVICE_EXTENSION */
/*
*******************************************************************************
**                   Command Control Block
**             this CCB length must be 32 bytes boundary
*******************************************************************************
*/
struct CommandControlBlock{
	/*x32:sizeof struct_CCB=(32+60)byte, x64:sizeof struct_CCB=(64+60)byte*/
	struct list_head		list;				/*x32: 8byte, x64: 16byte*/
	struct scsi_cmnd		*pcmd;				/*8 bytes pointer of linux scsi command */
	struct AdapterControlBlock	*acb;				/*x32: 4byte, x64: 8byte*/
	uint32_t			cdb_phyaddr;			/*x32: 4byte, x64: 4byte*/
	uint32_t			arc_cdb_size;			/*x32:4byte,x64:4byte*/
	uint16_t			ccb_flags;			/*x32: 2byte, x64: 2byte*/
	#define			CCB_FLAG_READ			0x0000
	#define			CCB_FLAG_WRITE		0x0001
	#define			CCB_FLAG_ERROR		0x0002
	#define			CCB_FLAG_FLUSHCACHE		0x0004
	#define			CCB_FLAG_MASTER_ABORTED	0x0008	
	uint16_t                        	startdone;			/*x32:2byte,x32:2byte*/
	#define			ARCMSR_CCB_DONE   	        	0x0000
	#define			ARCMSR_CCB_START		0x55AA
	#define			ARCMSR_CCB_ABORTED		0xAA55
	#define			ARCMSR_CCB_ILLEGAL		0xFFFF
	#if BITS_PER_LONG == 64
	/*  ======================512+64 bytes========================  */
		uint32_t                        	reserved[5];		/*24 byte*/
	#else
	/*  ======================512+32 bytes========================  */
		uint32_t                        	reserved;		/*8  byte*/
	#endif
	/*  =======================================================   */
	struct ARCMSR_CDB		arcmsr_cdb;
};
/*
*******************************************************************************
**    ARECA SCSI sense data
*******************************************************************************
*/
struct SENSE_DATA
{
	uint8_t				ErrorCode:7;
#define SCSI_SENSE_CURRENT_ERRORS	0x70
#define SCSI_SENSE_DEFERRED_ERRORS	0x71
	uint8_t				Valid:1;
	uint8_t				SegmentNumber;
	uint8_t				SenseKey:4;
	uint8_t				Reserved:1;
	uint8_t				IncorrectLength:1;
	uint8_t				EndOfMedia:1;
	uint8_t				FileMark:1;
	uint8_t				Information[4];
	uint8_t				AdditionalSenseLength;
	uint8_t				CommandSpecificInformation[4];
	uint8_t				AdditionalSenseCode;
	uint8_t				AdditionalSenseCodeQualifier;
	uint8_t				FieldReplaceableUnitCode;
	uint8_t				SenseKeySpecific[3];
};
/*
*******************************************************************************
**  Outbound Interrupt Status Register - OISR
*******************************************************************************
*/
#define     ARCMSR_MU_OUTBOUND_INTERRUPT_STATUS_REG                 0x30
#define     ARCMSR_MU_OUTBOUND_PCI_INT                              0x10
#define     ARCMSR_MU_OUTBOUND_POSTQUEUE_INT                        0x08
#define     ARCMSR_MU_OUTBOUND_DOORBELL_INT                         0x04
#define     ARCMSR_MU_OUTBOUND_MESSAGE1_INT                         0x02
#define     ARCMSR_MU_OUTBOUND_MESSAGE0_INT                         0x01
#define     ARCMSR_MU_OUTBOUND_HANDLE_INT                 \
                    (ARCMSR_MU_OUTBOUND_MESSAGE0_INT      \
                     |ARCMSR_MU_OUTBOUND_MESSAGE1_INT     \
                     |ARCMSR_MU_OUTBOUND_DOORBELL_INT     \
                     |ARCMSR_MU_OUTBOUND_POSTQUEUE_INT    \
                     |ARCMSR_MU_OUTBOUND_PCI_INT)
/*
*******************************************************************************
**  Outbound Interrupt Mask Register - OIMR
*******************************************************************************
*/
#define     ARCMSR_MU_OUTBOUND_INTERRUPT_MASK_REG                   0x34
#define     ARCMSR_MU_OUTBOUND_PCI_INTMASKENABLE                    0x10
#define     ARCMSR_MU_OUTBOUND_POSTQUEUE_INTMASKENABLE              0x08
#define     ARCMSR_MU_OUTBOUND_DOORBELL_INTMASKENABLE               0x04
#define     ARCMSR_MU_OUTBOUND_MESSAGE1_INTMASKENABLE               0x02
#define     ARCMSR_MU_OUTBOUND_MESSAGE0_INTMASKENABLE               0x01
#define     ARCMSR_MU_OUTBOUND_ALL_INTMASKENABLE                    0x1F

extern void arcmsr_write_ioctldata2iop(struct AdapterControlBlock *);
extern uint32_t arcmsr_Read_iop_rqbuffer_data(struct AdapterControlBlock *,
	struct QBUFFER __iomem *);
extern void arcmsr_clear_iop2drv_rqueue_buffer(struct AdapterControlBlock *);
extern struct QBUFFER __iomem *arcmsr_get_iop_rqbuffer(struct AdapterControlBlock *);
extern struct device_attribute *arcmsr_host_attrs[];
extern int arcmsr_alloc_sysfs_attr(struct AdapterControlBlock *);
void arcmsr_free_sysfs_attr(struct AdapterControlBlock *acb);
