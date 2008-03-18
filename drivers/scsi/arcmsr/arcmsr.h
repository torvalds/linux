/*
*******************************************************************************
**        O.S   : Linux
**   FILE NAME  : arcmsr.h
**        BY    : Erich Chen
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

struct class_device_attribute;
/*The limit of outstanding scsi command that firmware can handle*/
#define ARCMSR_MAX_OUTSTANDING_CMD						256
#define ARCMSR_MAX_FREECCB_NUM							320
#define ARCMSR_DRIVER_VERSION		     "Driver Version 1.20.00.15 2008/02/27"
#define ARCMSR_SCSI_INITIATOR_ID						255
#define ARCMSR_MAX_XFER_SECTORS							512
#define ARCMSR_MAX_XFER_SECTORS_B						4096
#define ARCMSR_MAX_TARGETID							17
#define ARCMSR_MAX_TARGETLUN							8
#define ARCMSR_MAX_CMD_PERLUN		                 ARCMSR_MAX_OUTSTANDING_CMD
#define ARCMSR_MAX_QBUFFER							4096
#define ARCMSR_MAX_SG_ENTRIES							38
#define ARCMSR_MAX_HBB_POSTQUEUE						264
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
struct CMD_MESSAGE_FIELD
{
    struct CMD_MESSAGE			cmdmessage;
    uint8_t				messagedatabuffer[1032];
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
#define ARCMSR_MESSAGE_RETURNCODE_OK              0x00000001
#define ARCMSR_MESSAGE_RETURNCODE_ERROR           0x00000006
#define ARCMSR_MESSAGE_RETURNCODE_3F              0x0000003F
/*
*************************************************************
**   structure for holding DMA address data
*************************************************************
*/
#define IS_SG64_ADDR                0x01000000 /* bit24 */
struct  SG32ENTRY
{
	__le32					length;
	__le32					address;
};
struct  SG64ENTRY
{
	__le32					length;
	__le32					address;
	__le32					addresshigh;
};
struct SGENTRY_UNION
{
	union
	{
		struct SG32ENTRY            sg32entry;
		struct SG64ENTRY            sg64entry;
	}u;
};
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
#define ARCMSR_CCBREPLY_FLAG_ERROR                    0x10000000
/* outbound firmware ok */
#define ARCMSR_OUTBOUND_MESG1_FIRMWARE_OK             0x80000000

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
#define ARCMSR_IOCTL_WBUFFER			      0x0000fe00
/* iop data to user space 128bytes */
#define ARCMSR_IOCTL_RBUFFER			      0x0000ff00
/* iop message_rwbuffer for message command */
#define ARCMSR_MSGCODE_RWBUFFER			      0x0000fa00
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

	uint8_t							Reserved1;
	uint32_t						Context;
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
		struct SG32ENTRY                sg32entry[ARCMSR_MAX_SG_ENTRIES];
		struct SG64ENTRY                sg64entry[ARCMSR_MAX_SG_ENTRIES];
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
	uint32_t	__iomem *drv2iop_doorbell_reg;
	uint32_t	__iomem *drv2iop_doorbell_mask_reg;
	uint32_t	__iomem *iop2drv_doorbell_reg;
	uint32_t	__iomem *iop2drv_doorbell_mask_reg;
	uint32_t	__iomem *msgcode_rwbuffer_reg;
	uint32_t	__iomem *ioctl_wbuffer_reg;
	uint32_t	__iomem *ioctl_rbuffer_reg;
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
	struct pci_dev *		pdev;
	struct Scsi_Host *		host;
	unsigned long			vir2phy_offset;
	/* Offset is used in making arc cdb physical to virtual calculations */
	uint32_t			outbound_int_enable;

	union {
		struct MessageUnit_A __iomem *	pmuA;
		struct MessageUnit_B *		pmuB;
	};
	/* message unit ATU inbound base address0 */

	uint32_t			acb_flags;
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
	#define ACB_F_IOP_INITED              	0x0100
	/* iop init */

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

	uint8_t				rqbuffer[ARCMSR_MAX_QBUFFER];
	/* data collection buffer for read from 80331 */
	int32_t				rqbuf_firstindex;
	/* first of read buffer  */
	int32_t				rqbuf_lastindex;
	/* last of read buffer   */
	uint8_t				wqbuffer[ARCMSR_MAX_QBUFFER];
	/* data collection buffer for write to 80331  */
	int32_t				wqbuf_firstindex;
	/* first of write buffer */
	int32_t				wqbuf_lastindex;
	/* last of write buffer  */
	uint8_t				devstate[ARCMSR_MAX_TARGETID][ARCMSR_MAX_TARGETLUN];
	/* id0 ..... id15, lun0...lun7 */
#define ARECA_RAID_GONE               0x55
#define ARECA_RAID_GOOD               0xaa
	uint32_t			num_resets;
	uint32_t			num_aborts;
	uint32_t			firm_request_len;
	uint32_t			firm_numbers_queue;
	uint32_t			firm_sdram_size;
	uint32_t			firm_hd_channels;
	char				firm_model[12];
	char				firm_version[20];
};/* HW_DEVICE_EXTENSION */
/*
*******************************************************************************
**                   Command Control Block
**             this CCB length must be 32 bytes boundary
*******************************************************************************
*/
struct CommandControlBlock
{
	struct ARCMSR_CDB		arcmsr_cdb;
	/*
	** 0-503 (size of CDB = 504):
	** arcmsr messenger scsi command descriptor size 504 bytes
	*/
	uint32_t			cdb_shifted_phyaddr;
	/* 504-507 */
	uint32_t			reserved1;
	/* 508-511 */
#if BITS_PER_LONG == 64
	/*  ======================512+64 bytes========================  */
	struct list_head		list;
	/* 512-527 16 bytes next/prev ptrs for ccb lists */
	struct scsi_cmnd *		pcmd;
	/* 528-535 8 bytes pointer of linux scsi command */
	struct AdapterControlBlock *	acb;
	/* 536-543 8 bytes pointer of acb */

	uint16_t			ccb_flags;
	/* 544-545 */
	#define		CCB_FLAG_READ			0x0000
	#define		CCB_FLAG_WRITE			0x0001
	#define		CCB_FLAG_ERROR			0x0002
	#define		CCB_FLAG_FLUSHCACHE		0x0004
	#define		CCB_FLAG_MASTER_ABORTED		0x0008
	uint16_t			startdone;
	/* 546-547 */
	#define		ARCMSR_CCB_DONE			0x0000
	#define		ARCMSR_CCB_START		0x55AA
	#define		ARCMSR_CCB_ABORTED		0xAA55
	#define		ARCMSR_CCB_ILLEGAL		0xFFFF
	uint32_t			reserved2[7];
	/* 548-551 552-555 556-559 560-563 564-567 568-571 572-575 */
#else
	/*  ======================512+32 bytes========================  */
	struct list_head		list;
	/* 512-519 8 bytes next/prev ptrs for ccb lists */
	struct scsi_cmnd *		pcmd;
	/* 520-523 4 bytes pointer of linux scsi command */
	struct AdapterControlBlock *	acb;
	/* 524-527 4 bytes pointer of acb */

	uint16_t			ccb_flags;
	/* 528-529 */
	#define		CCB_FLAG_READ			0x0000
	#define		CCB_FLAG_WRITE			0x0001
	#define		CCB_FLAG_ERROR			0x0002
	#define		CCB_FLAG_FLUSHCACHE		0x0004
	#define		CCB_FLAG_MASTER_ABORTED		0x0008
	uint16_t			startdone;
	/* 530-531 */
	#define		ARCMSR_CCB_DONE			0x0000
	#define		ARCMSR_CCB_START		0x55AA
	#define		ARCMSR_CCB_ABORTED		0xAA55
	#define		ARCMSR_CCB_ILLEGAL		0xFFFF
	uint32_t			reserved2[3];
	/* 532-535 536-539 540-543 */
#endif
	/*  ==========================================================  */
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

extern void arcmsr_post_ioctldata2iop(struct AdapterControlBlock *);
extern void arcmsr_iop_message_read(struct AdapterControlBlock *);
extern struct QBUFFER __iomem *arcmsr_get_iop_rqbuffer(struct AdapterControlBlock *);
extern struct class_device_attribute *arcmsr_host_attrs[];
extern int arcmsr_alloc_sysfs_attr(struct AdapterControlBlock *);
void arcmsr_free_sysfs_attr(struct AdapterControlBlock *acb);
