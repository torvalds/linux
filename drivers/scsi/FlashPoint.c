/*

  FlashPoint.c -- FlashPoint SCCB Manager for Linux

  This file contains the FlashPoint SCCB Manager from BusLogic's FlashPoint
  Driver Developer's Kit, with minor modifications by Leonard N. Zubkoff for
  Linux compatibility.  It was provided by BusLogic in the form of 16 separate
  source files, which would have unnecessarily cluttered the scsi directory, so
  the individual files have been combined into this single file.

  Copyright 1995-1996 by Mylex Corporation.  All Rights Reserved

  This file is available under both the GNU General Public License
  and a BSD-style copyright; see LICENSE.FlashPoint for details.

*/


#include <linux/config.h>


#ifndef CONFIG_SCSI_OMIT_FLASHPOINT


#define UNIX
#define FW_TYPE		_SCCB_MGR_
#define MAX_CARDS	8
#undef BUSTYPE_PCI


#define OS_InPortByte(port)		inb(port)
#define OS_InPortWord(port)		inw(port)
#define OS_InPortLong(port)		inl(port)
#define OS_OutPortByte(port, value)	outb(value, port)
#define OS_OutPortWord(port, value)	outw(value, port)
#define OS_OutPortLong(port, value)	outl(value, port)
#define OS_Lock(x)
#define OS_UnLock(x)


/*
  Define name replacements for compatibility with the Linux BusLogic Driver.
*/

#define SccbMgr_sense_adapter		FlashPoint_ProbeHostAdapter
#define SccbMgr_config_adapter		FlashPoint_HardwareResetHostAdapter
#define SccbMgr_unload_card		FlashPoint_ReleaseHostAdapter
#define SccbMgr_start_sccb		FlashPoint_StartCCB
#define SccbMgr_abort_sccb		FlashPoint_AbortCCB
#define SccbMgr_my_int			FlashPoint_InterruptPending
#define SccbMgr_isr			FlashPoint_HandleInterrupt


/*
  Define name replacements to avoid kernel namespace pollution.
*/

#define BL_Card				FPT_BL_Card
#define BusMasterInit			FPT_BusMasterInit
#define CalcCrc16			FPT_CalcCrc16
#define CalcLrc				FPT_CalcLrc
#define ChkIfChipInitialized		FPT_ChkIfChipInitialized
#define DiagBusMaster			FPT_DiagBusMaster
#define DiagEEPROM			FPT_DiagEEPROM
#define DiagXbow			FPT_DiagXbow
#define GetTarLun			FPT_GetTarLun
#define RNVRamData			FPT_RNVRamData
#define RdStack				FPT_RdStack
#define SccbMgrTableInitAll		FPT_SccbMgrTableInitAll
#define SccbMgrTableInitCard		FPT_SccbMgrTableInitCard
#define SccbMgrTableInitTarget		FPT_SccbMgrTableInitTarget
#define SccbMgr_bad_isr			FPT_SccbMgr_bad_isr
#define SccbMgr_scsi_reset		FPT_SccbMgr_scsi_reset
#define SccbMgr_timer_expired		FPT_SccbMgr_timer_expired
#define SendMsg				FPT_SendMsg
#define Wait				FPT_Wait
#define Wait1Second			FPT_Wait1Second
#define WrStack				FPT_WrStack
#define XbowInit			FPT_XbowInit
#define autoCmdCmplt			FPT_autoCmdCmplt
#define autoLoadDefaultMap		FPT_autoLoadDefaultMap
#define busMstrDataXferStart		FPT_busMstrDataXferStart
#define busMstrSGDataXferStart		FPT_busMstrSGDataXferStart
#define busMstrTimeOut			FPT_busMstrTimeOut
#define dataXferProcessor		FPT_dataXferProcessor
#define default_intena			FPT_default_intena
#define hostDataXferAbort		FPT_hostDataXferAbort
#define hostDataXferRestart		FPT_hostDataXferRestart
#define inisci				FPT_inisci
#define mbCards				FPT_mbCards
#define nvRamInfo			FPT_nvRamInfo
#define phaseBusFree			FPT_phaseBusFree
#define phaseChkFifo			FPT_phaseChkFifo
#define phaseCommand			FPT_phaseCommand
#define phaseDataIn			FPT_phaseDataIn
#define phaseDataOut			FPT_phaseDataOut
#define phaseDecode			FPT_phaseDecode
#define phaseIllegal			FPT_phaseIllegal
#define phaseMsgIn			FPT_phaseMsgIn
#define phaseMsgOut			FPT_phaseMsgOut
#define phaseStatus			FPT_phaseStatus
#define queueAddSccb			FPT_queueAddSccb
#define queueCmdComplete		FPT_queueCmdComplete
#define queueDisconnect			FPT_queueDisconnect
#define queueFindSccb			FPT_queueFindSccb
#define queueFlushSccb			FPT_queueFlushSccb
#define queueFlushTargSccb		FPT_queueFlushTargSccb
#define queueSearchSelect		FPT_queueSearchSelect
#define queueSelectFail			FPT_queueSelectFail
#define s_PhaseTbl			FPT_s_PhaseTbl
#define scamHAString			FPT_scamHAString
#define scamInfo			FPT_scamInfo
#define scarb				FPT_scarb
#define scasid				FPT_scasid
#define scbusf				FPT_scbusf
#define sccbMgrTbl			FPT_sccbMgrTbl
#define schkdd				FPT_schkdd
#define scini				FPT_scini
#define sciso				FPT_sciso
#define scmachid			FPT_scmachid
#define scsavdi				FPT_scsavdi
#define scsel				FPT_scsel
#define scsell				FPT_scsell
#define scsendi				FPT_scsendi
#define scvalq				FPT_scvalq
#define scwirod				FPT_scwirod
#define scwiros				FPT_scwiros
#define scwtsel				FPT_scwtsel
#define scxferc				FPT_scxferc
#define sdecm				FPT_sdecm
#define sfm				FPT_sfm
#define shandem				FPT_shandem
#define sinits				FPT_sinits
#define sisyncn				FPT_sisyncn
#define sisyncr				FPT_sisyncr
#define siwidn				FPT_siwidn
#define siwidr				FPT_siwidr
#define sres				FPT_sres
#define sresb				FPT_sresb
#define ssel				FPT_ssel
#define ssenss				FPT_ssenss
#define sssyncv				FPT_sssyncv
#define stsyncn				FPT_stsyncn
#define stwidn				FPT_stwidn
#define sxfrp				FPT_sxfrp
#define utilEERead			FPT_utilEERead
#define utilEEReadOrg			FPT_utilEEReadOrg
#define utilEESendCmdAddr		FPT_utilEESendCmdAddr
#define utilEEWrite			FPT_utilEEWrite
#define utilEEWriteOnOff		FPT_utilEEWriteOnOff
#define utilUpdateResidual		FPT_utilUpdateResidual


/*----------------------------------------------------------------------
 *
 *
 *   Copyright 1995-1996 by Mylex Corporation.  All Rights Reserved
 *
 *   This file is available under both the GNU General Public License
 *   and a BSD-style copyright; see LICENSE.FlashPoint for details.
 *
 *   $Workfile:   globals.h  $
 *
 *   Description:  Common shared global defines.
 *
 *   $Date: 1996/09/04 01:26:13 $
 *
 *   $Revision: 1.11 $
 *
 *----------------------------------------------------------------------*/
#ifndef __GLOBALS_H__
#define __GLOBALS_H__

#define _UCB_MGR_  1
#define _SCCB_MGR_ 2

/*#include <osflags.h>*/

#define MAX_CDBLEN  12

#define SCAM_LEV_2	1

#define CRCMASK	0xA001

/*  In your osflags.h file, please ENSURE that only ONE OS FLAG 
    is on at a time !!! Also, please make sure you turn set the 
 	 variable FW_TYPE to either _UCB_MGR_ or _SCCB_MGR_  !!! */

#if defined(DOS) || defined(WIN95_16) || defined(OS2) || defined(OTHER_16)
   #define       COMPILER_16_BIT 1
#elif defined(NETWARE) || defined(NT) || defined(WIN95_32) || defined(UNIX) || defined(OTHER_32) || defined(SOLARIS_REAL_MODE)
   #define       COMPILER_32_BIT 1
#endif


#define     BL_VENDOR_ID      0x104B
#define     FP_DEVICE_ID      0x8130
#define     MM_DEVICE_ID      0x1040


#ifndef FALSE
#define FALSE           0
#endif
#ifndef TRUE
#define TRUE            (!(FALSE))
#endif

#ifndef NULL
#define NULL            0
#endif

#define FAILURE         0xFFFFFFFFL


typedef unsigned char   UCHAR;
typedef unsigned short  USHORT;
typedef unsigned int    UINT;
typedef unsigned long   ULONG;
typedef unsigned char * PUCHAR;
typedef unsigned short* PUSHORT;
typedef unsigned long * PULONG;
typedef void *          PVOID;


#if defined(COMPILER_16_BIT)
typedef unsigned char far       * uchar_ptr;
typedef unsigned short far      * ushort_ptr;
typedef unsigned long far       * ulong_ptr;
#endif  /* 16_BIT_COMPILER */

#if defined(COMPILER_32_BIT)
typedef unsigned char           * uchar_ptr;
typedef unsigned short          * ushort_ptr;
typedef unsigned long           * ulong_ptr;
#endif  /* 32_BIT_COMPILER */


/*	 			NEW TYPE DEFINITIONS (shared with Mylex North)

**  Use following type defines to avoid confusion in 16 and 32-bit
**  environments.  Avoid using 'int' as it denotes 16 bits in 16-bit
**  environment and 32 in 32-bit environments.

*/

#define s08bits	char
#define s16bits 	short
#define s32bits	long

#define u08bits	unsigned s08bits
#define u16bits	unsigned s16bits
#define u32bits	unsigned s32bits

#if defined(COMPILER_16_BIT)

typedef u08bits far 	* pu08bits;
typedef u16bits far 	* pu16bits;
typedef u32bits far	* pu32bits;

#endif	/* COMPILER_16_BIT */

#if defined(COMPILER_32_BIT)

typedef u08bits 	* pu08bits;
typedef u16bits 	* pu16bits;
typedef u32bits 	* pu32bits;

#endif	/* COMPILER_32_BIT */


#define BIT(x)          ((UCHAR)(1<<(x)))    /* single-bit mask in bit position x */
#define BITW(x)          ((USHORT)(1<<(x)))  /* single-bit mask in bit position x */



#if defined(DOS)
/*#include <dos.h>*/
	#undef inportb          /* undefine for Borland Lib */
	#undef inport           /* they may have define I/O function in LIB */
	#undef outportb
	#undef outport

	#define OS_InPortByte(ioport) 		inportb(ioport)
	#define OS_InPortWord(ioport) 		inport(ioport)
	#define OS_InPortLong(ioport)			inportq(ioport, val)
	#define OS_OutPortByte(ioport, val) outportb(ioport, val)
	#define OS_OutPortWord(ioport, val)	outport(ioport, val)
	#define OS_OutPortLong(ioport)		outportq(ioport, val)
#endif	/* DOS */

#if defined(NETWARE) || defined(OTHER_32) ||  defined(OTHER_16)
	extern u08bits	OS_InPortByte(u32bits ioport);
	extern u16bits	OS_InPortWord(u32bits ioport);
	extern u32bits	OS_InPortLong(u32bits ioport);

	extern OS_InPortByteBuffer(u32bits ioport, pu08bits buffer, u32bits count);
	extern OS_InPortWordBuffer(u32bits ioport, pu16bits buffer, u32bits count);
	extern OS_OutPortByte(u32bits ioport, u08bits val);
	extern OS_OutPortWord(u32bits ioport, u16bits val);
	extern OS_OutPortLong(u32bits ioport, u32bits val);
	extern OS_OutPortByteBuffer(u32bits ioport, pu08bits buffer, u32bits count);
	extern OS_OutPortWordBuffer(u32bits ioport, pu16bits buffer, u32bits count);
#endif	/* NETWARE || OTHER_32 || OTHER_16 */

#if defined (NT) || defined(WIN95_32) || defined(WIN95_16)
	#if defined(NT)

		extern __declspec(dllimport) u08bits ScsiPortReadPortUchar(pu08bits ioport);
		extern __declspec(dllimport) u16bits ScsiPortReadPortUshort(pu16bits ioport);
		extern __declspec(dllimport) u32bits ScsiPortReadPortUlong(pu32bits ioport);
		extern __declspec(dllimport) void ScsiPortWritePortUchar(pu08bits ioport, u08bits val);
		extern __declspec(dllimport) void ScsiPortWritePortUshort(pu16bits port, u16bits val);
		extern __declspec(dllimport) void ScsiPortWritePortUlong(pu32bits port, u32bits val);

	#else

		extern u08bits ScsiPortReadPortUchar(pu08bits ioport);
		extern u16bits ScsiPortReadPortUshort(pu16bits ioport);
		extern u32bits ScsiPortReadPortUlong(pu32bits ioport);
		extern void ScsiPortWritePortUchar(pu08bits ioport, u08bits val);
		extern void ScsiPortWritePortUshort(pu16bits port, u16bits val);
		extern void ScsiPortWritePortUlong(pu32bits port, u32bits val);
	#endif


	#define OS_InPortByte(ioport) ScsiPortReadPortUchar((pu08bits) ioport)
	#define OS_InPortWord(ioport) ScsiPortReadPortUshort((pu16bits) ioport)
	#define OS_InPortLong(ioport) ScsiPortReadPortUlong((pu32bits) ioport)

	#define OS_OutPortByte(ioport, val) ScsiPortWritePortUchar((pu08bits) ioport, (u08bits) val)
	#define OS_OutPortWord(ioport, val) ScsiPortWritePortUshort((pu16bits) ioport, (u16bits) val)
	#define OS_OutPortLong(ioport, val) ScsiPortWritePortUlong((pu32bits) ioport, (u32bits) val)
	#define OS_OutPortByteBuffer(ioport, buffer, count) \
		ScsiPortWritePortBufferUchar((pu08bits)&port, (pu08bits) buffer, (u32bits) count)
	#define OS_OutPortWordBuffer(ioport, buffer, count) \
		ScsiPortWritePortBufferUshort((pu16bits)&port, (pu16bits) buffer, (u32bits) count)

	#define OS_Lock(x)
	#define OS_UnLock(x)
#endif /* NT || WIN95_32 || WIN95_16 */

#if defined (UNIX) && !defined(OS_InPortByte)
	#define OS_InPortByte(ioport)    inb((u16bits)ioport)
	#define OS_InPortWord(ioport)    inw((u16bits)ioport)
	#define OS_InPortLong(ioport)    inl((u16bits)ioport)
	#define OS_OutPortByte(ioport,val)  outb((u16bits)ioport, (u08bits)val)
	#define OS_OutPortWord(ioport,val)  outw((u16bits)ioport, (u16bits)val)
	#define OS_OutPortLong(ioport,val)  outl((u16bits)ioport, (u32bits)val)

	#define OS_Lock(x)
	#define OS_UnLock(x)
#endif /* UNIX */


#if defined(OS2)
	extern u08bits	inb(u32bits ioport);
	extern u16bits	inw(u32bits ioport);
	extern void	outb(u32bits ioport, u08bits val);
	extern void	outw(u32bits ioport, u16bits val);

	#define OS_InPortByte(ioport)			inb(ioport)
	#define OS_InPortWord(ioport)			inw(ioport)
	#define OS_OutPortByte(ioport, val)	outb(ioport, val)
	#define OS_OutPortWord(ioport, val)	outw(ioport, val)
	extern u32bits	OS_InPortLong(u32bits ioport);
	extern void	OS_OutPortLong(u32bits ioport, u32bits val);

	#define OS_Lock(x)
	#define OS_UnLock(x)
#endif /* OS2 */

#if defined(SOLARIS_REAL_MODE)

extern unsigned char    inb(unsigned long ioport);
extern unsigned short   inw(unsigned long ioport);

#define OS_InPortByte(ioport)    inb(ioport)
#define OS_InPortWord(ioport)    inw(ioport)

extern void OS_OutPortByte(unsigned long ioport, unsigned char val);
extern void OS_OutPortWord(unsigned long ioport, unsigned short val);
extern unsigned long  OS_InPortLong(unsigned long ioport);
extern void     OS_OutPortLong(unsigned long ioport, unsigned long val);

#define OS_Lock(x)
#define OS_UnLock(x)

#endif  /* SOLARIS_REAL_MODE */

#endif  /* __GLOBALS_H__ */

/*----------------------------------------------------------------------
 *
 *
 *   Copyright 1995-1996 by Mylex Corporation.  All Rights Reserved
 *
 *   This file is available under both the GNU General Public License
 *   and a BSD-style copyright; see LICENSE.FlashPoint for details.
 *
 *   $Workfile:   sccbmgr.h  $
 *
 *   Description:  Common shared SCCB Interface defines and SCCB 
 *						 Manager specifics defines.
 *
 *   $Date: 1996/10/24 23:09:33 $
 *
 *   $Revision: 1.14 $
 *
 *----------------------------------------------------------------------*/

#ifndef __SCCB_H__
#define __SCCB_H__

/*#include <osflags.h>*/
/*#include <globals.h>*/

#if defined(BUGBUG)
#define debug_size 32
#endif

#if defined(DOS)

   typedef struct _SCCB near *PSCCB;
	#if (FW_TYPE == _SCCB_MGR_)
   	typedef void (*CALL_BK_FN)(PSCCB);
	#endif

#elif defined(OS2)

   typedef struct _SCCB far *PSCCB;
	#if (FW_TYPE == _SCCB_MGR_)
   	typedef void (far *CALL_BK_FN)(PSCCB);
	#endif

#else

   typedef struct _SCCB *PSCCB;
	#if (FW_TYPE == _SCCB_MGR_)
   	typedef void (*CALL_BK_FN)(PSCCB);
	#endif

#endif


typedef struct SCCBMgr_info {
   ULONG    si_baseaddr;
   UCHAR    si_present;
   UCHAR    si_intvect;
   UCHAR    si_id;
   UCHAR    si_lun;
   USHORT   si_fw_revision;
   USHORT   si_per_targ_init_sync;
   USHORT   si_per_targ_fast_nego;
   USHORT   si_per_targ_ultra_nego;
   USHORT   si_per_targ_no_disc;
   USHORT   si_per_targ_wide_nego;
   USHORT   si_flags;
   UCHAR    si_card_family;
   UCHAR    si_bustype;
   UCHAR    si_card_model[3];
   UCHAR    si_relative_cardnum;
   UCHAR    si_reserved[4];
   ULONG    si_OS_reserved;
   UCHAR    si_XlatInfo[4];
   ULONG    si_reserved2[5];
   ULONG    si_secondary_range;
} SCCBMGR_INFO;

#if defined(DOS)
   typedef SCCBMGR_INFO *      PSCCBMGR_INFO;
#else
   #if defined (COMPILER_16_BIT)
   typedef SCCBMGR_INFO far *  PSCCBMGR_INFO;
   #else
   typedef SCCBMGR_INFO *      PSCCBMGR_INFO;
   #endif
#endif // defined(DOS)




#if (FW_TYPE==_SCCB_MGR_)
	#define SCSI_PARITY_ENA		  0x0001
	#define LOW_BYTE_TERM		  0x0010
	#define HIGH_BYTE_TERM		  0x0020
	#define BUSTYPE_PCI	  0x3
#endif

#define SUPPORT_16TAR_32LUN	  0x0002
#define SOFT_RESET		  0x0004
#define EXTENDED_TRANSLATION	  0x0008
#define POST_ALL_UNDERRRUNS	  0x0040
#define FLAG_SCAM_ENABLED	  0x0080
#define FLAG_SCAM_LEVEL2	  0x0100




#define HARPOON_FAMILY        0x02


#define ISA_BUS_CARD          0x01
#define EISA_BUS_CARD         0x02
#define PCI_BUS_CARD          0x03
#define VESA_BUS_CARD         0x04

/* SCCB struc used for both SCCB and UCB manager compiles! 
 * The UCB Manager treats the SCCB as it's 'native hardware structure' 
 */


#pragma pack(1)
typedef struct _SCCB {
   UCHAR OperationCode;
   UCHAR ControlByte;
   UCHAR CdbLength;
   UCHAR RequestSenseLength;
   ULONG DataLength;
   ULONG DataPointer;
   UCHAR CcbRes[2];
   UCHAR HostStatus;
   UCHAR TargetStatus;
   UCHAR TargID;
   UCHAR Lun;
   UCHAR Cdb[12];
   UCHAR CcbRes1;
   UCHAR Reserved1;
   ULONG Reserved2;
   ULONG SensePointer;


   CALL_BK_FN SccbCallback;                  /* VOID (*SccbCallback)(); */
   ULONG  SccbIOPort;                        /* Identifies board base port */
   UCHAR  SccbStatus;
   UCHAR  SCCBRes2;
   USHORT SccbOSFlags;


   ULONG   Sccb_XferCnt;            /* actual transfer count */
   ULONG   Sccb_ATC;
   ULONG   SccbVirtDataPtr;         /* virtual addr for OS/2 */
   ULONG   Sccb_res1;
   USHORT  Sccb_MGRFlags;
   USHORT  Sccb_sgseg;
   UCHAR   Sccb_scsimsg;            /* identify msg for selection */
   UCHAR   Sccb_tag;
   UCHAR   Sccb_scsistat;
   UCHAR   Sccb_idmsg;              /* image of last msg in */
   PSCCB   Sccb_forwardlink;
   PSCCB   Sccb_backlink;
   ULONG   Sccb_savedATC;
   UCHAR   Save_Cdb[6];
   UCHAR   Save_CdbLen;
   UCHAR   Sccb_XferState;
   ULONG   Sccb_SGoffset;
#if (FW_TYPE == _UCB_MGR_)
   PUCB    Sccb_ucb_ptr;
#endif
   } SCCB;

#define SCCB_SIZE sizeof(SCCB)

#pragma pack()



#define SCSI_INITIATOR_COMMAND    0x00
#define TARGET_MODE_COMMAND       0x01
#define SCATTER_GATHER_COMMAND    0x02
#define RESIDUAL_COMMAND          0x03
#define RESIDUAL_SG_COMMAND       0x04
#define RESET_COMMAND             0x81


#define F_USE_CMD_Q              0x20     /*Inidcates TAGGED command. */
#define TAG_TYPE_MASK            0xC0     /*Type of tag msg to send. */
#define TAG_Q_MASK               0xE0
#define SCCB_DATA_XFER_OUT       0x10     /* Write */
#define SCCB_DATA_XFER_IN        0x08     /* Read */


#define FOURTEEN_BYTES           0x00     /* Request Sense Buffer size */
#define NO_AUTO_REQUEST_SENSE    0x01     /* No Request Sense Buffer */


#define BUS_FREE_ST     0       
#define SELECT_ST       1
#define SELECT_BDR_ST   2     /* Select w\ Bus Device Reset */
#define SELECT_SN_ST    3     /* Select w\ Sync Nego */
#define SELECT_WN_ST    4     /* Select w\ Wide Data Nego */
#define SELECT_Q_ST     5     /* Select w\ Tagged Q'ing */
#define COMMAND_ST      6
#define DATA_OUT_ST     7
#define DATA_IN_ST      8
#define DISCONNECT_ST   9
#define STATUS_ST       10
#define ABORT_ST        11
#define MESSAGE_ST      12


#define F_HOST_XFER_DIR                0x01
#define F_ALL_XFERRED                  0x02
#define F_SG_XFER                      0x04
#define F_AUTO_SENSE                   0x08
#define F_ODD_BALL_CNT                 0x10
#define F_NO_DATA_YET                  0x80


#define F_STATUSLOADED                 0x01
#define F_MSGLOADED                    0x02
#define F_DEV_SELECTED                 0x04


#define SCCB_COMPLETE               0x00  /* SCCB completed without error */
#define SCCB_DATA_UNDER_RUN         0x0C
#define SCCB_SELECTION_TIMEOUT      0x11  /* Set SCSI selection timed out */
#define SCCB_DATA_OVER_RUN          0x12
#define SCCB_UNEXPECTED_BUS_FREE    0x13  /* Target dropped SCSI BSY */
#define SCCB_PHASE_SEQUENCE_FAIL    0x14  /* Target bus phase sequence failure */

#define SCCB_INVALID_OP_CODE        0x16  /* SCCB invalid operation code */
#define SCCB_INVALID_SCCB           0x1A  /* Invalid SCCB - bad parameter */
#define SCCB_GROSS_FW_ERR           0x27  /* Major problem! */
#define SCCB_BM_ERR                 0x30  /* BusMaster error. */
#define SCCB_PARITY_ERR             0x34  /* SCSI parity error */



#if (FW_TYPE==_UCB_MGR_)  
   #define  HBA_AUTO_SENSE_FAIL        0x1B  
   #define  HBA_TQ_REJECTED            0x1C  
   #define  HBA_UNSUPPORTED_MSG         0x1D  
   #define  HBA_HW_ERROR               0x20  
   #define  HBA_ATN_NOT_RESPONDED      0x21  
   #define  HBA_SCSI_RESET_BY_ADAPTER  0x22
   #define  HBA_SCSI_RESET_BY_TARGET   0x23
   #define  HBA_WRONG_CONNECTION       0x24
   #define  HBA_BUS_DEVICE_RESET       0x25
   #define  HBA_ABORT_QUEUE            0x26

#else // these are not defined in BUDI/UCB

   #define SCCB_INVALID_DIRECTION      0x18  /* Invalid target direction */
   #define SCCB_DUPLICATE_SCCB         0x19  /* Duplicate SCCB */
   #define SCCB_SCSI_RST               0x35  /* SCSI RESET detected. */

#endif // (FW_TYPE==_UCB_MGR_)  


#define SCCB_IN_PROCESS            0x00
#define SCCB_SUCCESS               0x01
#define SCCB_ABORT                 0x02
#define SCCB_NOT_FOUND             0x03
#define SCCB_ERROR                 0x04
#define SCCB_INVALID               0x05

#define SCCB_SIZE sizeof(SCCB)




#if (FW_TYPE == _UCB_MGR_)
	void SccbMgr_start_sccb(CARD_HANDLE pCurrCard, PUCB p_ucb);
	s32bits SccbMgr_abort_sccb(CARD_HANDLE pCurrCard, PUCB p_ucb);
	u08bits SccbMgr_my_int(CARD_HANDLE pCurrCard);
	s32bits SccbMgr_isr(CARD_HANDLE pCurrCard);
	void SccbMgr_scsi_reset(CARD_HANDLE pCurrCard);
	void SccbMgr_timer_expired(CARD_HANDLE pCurrCard);
	void SccbMgr_unload_card(CARD_HANDLE pCurrCard);
	void SccbMgr_restore_foreign_state(CARD_HANDLE pCurrCard);
	void SccbMgr_restore_native_state(CARD_HANDLE pCurrCard);
	void SccbMgr_save_foreign_state(PADAPTER_INFO pAdapterInfo);

#endif


#if (FW_TYPE == _SCCB_MGR_)

 #if defined (DOS)
	int    SccbMgr_sense_adapter(PSCCBMGR_INFO pCardInfo);
	USHORT SccbMgr_config_adapter(PSCCBMGR_INFO pCardInfo);
	void  SccbMgr_start_sccb(USHORT pCurrCard, PSCCB p_SCCB);
	int   SccbMgr_abort_sccb(USHORT pCurrCard, PSCCB p_SCCB);
	UCHAR SccbMgr_my_int(USHORT pCurrCard);
	int   SccbMgr_isr(USHORT pCurrCard);
	void  SccbMgr_scsi_reset(USHORT pCurrCard);
	void  SccbMgr_timer_expired(USHORT pCurrCard);
	USHORT SccbMgr_status(USHORT pCurrCard);
	void SccbMgr_unload_card(USHORT pCurrCard);

 #else    //non-DOS

	int   SccbMgr_sense_adapter(PSCCBMGR_INFO pCardInfo);
	ULONG SccbMgr_config_adapter(PSCCBMGR_INFO pCardInfo);
	void  SccbMgr_start_sccb(ULONG pCurrCard, PSCCB p_SCCB);
	int   SccbMgr_abort_sccb(ULONG pCurrCard, PSCCB p_SCCB);
	UCHAR SccbMgr_my_int(ULONG pCurrCard);
	int   SccbMgr_isr(ULONG pCurrCard);
	void  SccbMgr_scsi_reset(ULONG pCurrCard);
	void  SccbMgr_enable_int(ULONG pCurrCard);
	void  SccbMgr_disable_int(ULONG pCurrCard);
	void  SccbMgr_timer_expired(ULONG pCurrCard);
	void SccbMgr_unload_card(ULONG pCurrCard);

  #endif
#endif  // (FW_TYPE == _SCCB_MGR_)

#endif  /* __SCCB_H__ */

/*----------------------------------------------------------------------
 *
 *
 *   Copyright 1995-1996 by Mylex Corporation.  All Rights Reserved
 *
 *   This file is available under both the GNU General Public License
 *   and a BSD-style copyright; see LICENSE.FlashPoint for details.
 *
 *   $Workfile:   blx30.h  $
 *
 *   Description: This module contains SCCB/UCB Manager implementation
 *                specific stuff.
 *
 *   $Date: 1996/11/13 18:34:22 $
 *
 *   $Revision: 1.10 $
 *
 *----------------------------------------------------------------------*/


#ifndef __blx30_H__
#define __blx30_H__

/*#include <globals.h>*/

#define  ORION_FW_REV      3110




#define HARP_REVD    1


#if defined(DOS)
#define QUEUE_DEPTH     8+1            /*1 for Normal disconnect 0 for Q'ing. */
#else
#define QUEUE_DEPTH     254+1            /*1 for Normal disconnect 32 for Q'ing. */
#endif   // defined(DOS)

#define	MAX_MB_CARDS	4					/* Max. no of cards suppoerted on Mother Board */

#define WIDE_SCSI       1

#if defined(WIDE_SCSI)
   #if defined(DOS)
      #define MAX_SCSI_TAR    16
      #define MAX_LUN         8
		#define LUN_MASK			0x07
   #else
      #define MAX_SCSI_TAR    16
      #define MAX_LUN         32
		#define LUN_MASK			0x1f
	
   #endif
#else
   #define MAX_SCSI_TAR    8
   #define MAX_LUN         8
	#define LUN_MASK			0x07
#endif 

#if defined(HARP_REVA)
#define SG_BUF_CNT      15             /*Number of prefetched elements. */
#else
#define SG_BUF_CNT      16             /*Number of prefetched elements. */
#endif

#define SG_ELEMENT_SIZE 8              /*Eight byte per element. */
#define SG_LOCAL_MASK   0x00000000L
#define SG_ELEMENT_MASK 0xFFFFFFFFL


#if (FW_TYPE == _UCB_MGR_)
	#define OPC_DECODE_NORMAL       0x0f7f
#endif   // _UCB_MGR_



#if defined(DOS)

/*#include <dos.h>*/
	#define RD_HARPOON(ioport)          (OS_InPortByte(ioport))
	#define RDW_HARPOON(ioport)         (OS_InPortWord(ioport))
	#define WR_HARPOON(ioport,val)      (OS_OutPortByte(ioport,val))
	#define WRW_HARPOON(ioport,val)     (OS_OutPortWord(ioport,val))

	#define RD_HARP32(port,offset,data)  asm{db 66h;         \
                                       push ax;             \
                                       mov dx,port;         \
                                       add dx, offset;      \
                                       db 66h;              \
                                       in ax,dx;            \
                                       db 66h;              \
                                       mov word ptr data,ax;\
                                       db 66h;              \
                                       pop ax}

	#define WR_HARP32(port,offset,data) asm{db 66h;          \
                                       push ax;             \
                                       mov dx,port;         \
                                       add dx, offset;      \
                                       db 66h;              \
                                       mov ax,word ptr data;\
                                       db 66h;              \
                                       out dx,ax;           \
                                       db 66h;              \
                                       pop ax}
#endif	/* DOS */

#if defined(NETWARE) || defined(OTHER_32) ||  defined(OTHER_16)
	#define RD_HARPOON(ioport)     OS_InPortByte((unsigned long)ioport)
	#define RDW_HARPOON(ioport)    OS_InPortWord((unsigned long)ioport)
	#define RD_HARP32(ioport,offset,data) (data = OS_InPortLong(ioport + offset))
	#define WR_HARPOON(ioport,val) OS_OutPortByte((ULONG)ioport,(UCHAR) val)
	#define WRW_HARPOON(ioport,val)  OS_OutPortWord((ULONG)ioport,(USHORT)val)
	#define WR_HARP32(ioport,offset,data)  OS_OutPortLong((ioport + offset), data)
#endif	/* NETWARE || OTHER_32 || OTHER_16 */

#if defined(NT) || defined(WIN95_32) || defined(WIN95_16)
	#define RD_HARPOON(ioport)          OS_InPortByte((ULONG)ioport)
	#define RDW_HARPOON(ioport)         OS_InPortWord((ULONG)ioport)
	#define RD_HARP32(ioport,offset,data) (data = OS_InPortLong((ULONG)(ioport + offset)))
	#define WR_HARPOON(ioport,val)      OS_OutPortByte((ULONG)ioport,(UCHAR) val)
	#define WRW_HARPOON(ioport,val)     OS_OutPortWord((ULONG)ioport,(USHORT)val)
	#define WR_HARP32(ioport,offset,data)  OS_OutPortLong((ULONG)(ioport + offset), data)
#endif /* NT || WIN95_32 || WIN95_16 */

#if defined (UNIX)
	#define RD_HARPOON(ioport)          OS_InPortByte((u32bits)ioport)
	#define RDW_HARPOON(ioport)         OS_InPortWord((u32bits)ioport)
	#define RD_HARP32(ioport,offset,data) (data = OS_InPortLong((u32bits)(ioport + offset)))
	#define WR_HARPOON(ioport,val)      OS_OutPortByte((u32bits)ioport,(u08bits) val)
	#define WRW_HARPOON(ioport,val)       OS_OutPortWord((u32bits)ioport,(u16bits)val)
	#define WR_HARP32(ioport,offset,data)  OS_OutPortLong((u32bits)(ioport + offset), data)
#endif /* UNIX */

#if defined(OS2)
	#define RD_HARPOON(ioport)          OS_InPortByte((unsigned long)ioport)
	#define RDW_HARPOON(ioport)         OS_InPortWord((unsigned long)ioport)
	#define RD_HARP32(ioport,offset,data) (data = OS_InPortLong((ULONG)(ioport + offset)))
	#define WR_HARPOON(ioport,val)      OS_OutPortByte((ULONG)ioport,(UCHAR) val)
	#define WRW_HARPOON(ioport,val)       OS_OutPortWord((ULONG)ioport,(USHORT)val)
	#define WR_HARP32(ioport,offset,data)  OS_OutPortLong(((ULONG)(ioport + offset)), data)
#endif /* OS2 */

#if defined(SOLARIS_REAL_MODE)

	#define RD_HARPOON(ioport)          OS_InPortByte((unsigned long)ioport)
	#define RDW_HARPOON(ioport)         OS_InPortWord((unsigned long)ioport)
	#define RD_HARP32(ioport,offset,data) (data = OS_InPortLong((ULONG)(ioport + offset)))
	#define WR_HARPOON(ioport,val)      OS_OutPortByte((ULONG)ioport,(UCHAR) val)
	#define WRW_HARPOON(ioport,val)       OS_OutPortWord((ULONG)ioport,(USHORT)val)
	#define WR_HARP32(ioport,offset,data)  OS_OutPortLong((ULONG)(ioport + offset), (ULONG)data)

#endif  /* SOLARIS_REAL_MODE */

#endif  /* __BLX30_H__ */


/*----------------------------------------------------------------------
 * 
 *
 *   Copyright 1995-1996 by Mylex Corporation.  All Rights Reserved
 *
 *   This file is available under both the GNU General Public License
 *   and a BSD-style copyright; see LICENSE.FlashPoint for details.
 *
 *   $Workfile:   target.h  $
 *
 *   Description:  Definitions for Target related structures
 *
 *   $Date: 1996/12/11 22:06:20 $
 *
 *   $Revision: 1.9 $
 *
 *----------------------------------------------------------------------*/

#ifndef __TARGET__
#define __TARGET__

/*#include <globals.h>*/
/*#include <blx30.h>*/


#define  TAR_SYNC_MASK     (BIT(7)+BIT(6))
#define  SYNC_UNKNOWN      0x00
#define  SYNC_TRYING               BIT(6)
#define  SYNC_SUPPORTED    (BIT(7)+BIT(6))

#define  TAR_WIDE_MASK     (BIT(5)+BIT(4))
#define  WIDE_DISABLED     0x00
#define  WIDE_ENABLED              BIT(4)
#define  WIDE_NEGOCIATED   BIT(5)

#define  TAR_TAG_Q_MASK    (BIT(3)+BIT(2))
#define  TAG_Q_UNKNOWN     0x00
#define  TAG_Q_TRYING              BIT(2)
#define  TAG_Q_REJECT      BIT(3)
#define  TAG_Q_SUPPORTED   (BIT(3)+BIT(2))

#define  TAR_ALLOW_DISC    BIT(0)


#define  EE_SYNC_MASK      (BIT(0)+BIT(1))
#define  EE_SYNC_ASYNC     0x00
#define  EE_SYNC_5MB       BIT(0)
#define  EE_SYNC_10MB      BIT(1)
#define  EE_SYNC_20MB      (BIT(0)+BIT(1))

#define  EE_ALLOW_DISC     BIT(6)
#define  EE_WIDE_SCSI      BIT(7)


#if defined(DOS)
   typedef struct SCCBMgr_tar_info near *PSCCBMgr_tar_info;

#elif defined(OS2)
   typedef struct SCCBMgr_tar_info far *PSCCBMgr_tar_info;

#else
   typedef struct SCCBMgr_tar_info *PSCCBMgr_tar_info;

#endif


typedef struct SCCBMgr_tar_info {

   PSCCB    TarSelQ_Head;
   PSCCB    TarSelQ_Tail;
   UCHAR    TarLUN_CA;        /*Contingent Allgiance */
   UCHAR    TarTagQ_Cnt;
   UCHAR    TarSelQ_Cnt;
   UCHAR    TarStatus;
   UCHAR    TarEEValue;
   UCHAR 	TarSyncCtrl;
   UCHAR 	TarReserved[2];			/* for alignment */ 
   UCHAR 	LunDiscQ_Idx[MAX_LUN];
   UCHAR    TarLUNBusy[MAX_LUN];
} SCCBMGR_TAR_INFO;

typedef struct NVRAMInfo {
	UCHAR		niModel;								/* Model No. of card */
	UCHAR		niCardNo;							/* Card no. */
#if defined(DOS)
	USHORT	niBaseAddr;							/* Port Address of card */
#else
	ULONG		niBaseAddr;							/* Port Address of card */
#endif
	UCHAR		niSysConf;							/* Adapter Configuration byte - Byte 16 of eeprom map */
	UCHAR		niScsiConf;							/* SCSI Configuration byte - Byte 17 of eeprom map */
	UCHAR		niScamConf;							/* SCAM Configuration byte - Byte 20 of eeprom map */
	UCHAR		niAdapId;							/* Host Adapter ID - Byte 24 of eerpom map */
	UCHAR		niSyncTbl[MAX_SCSI_TAR / 2];	/* Sync/Wide byte of targets */
	UCHAR		niScamTbl[MAX_SCSI_TAR][4];	/* Compressed Scam name string of Targets */
}NVRAMINFO;

#if defined(DOS)
typedef NVRAMINFO near *PNVRamInfo;
#elif defined (OS2)
typedef NVRAMINFO far *PNVRamInfo;
#else
typedef NVRAMINFO *PNVRamInfo;
#endif

#define	MODEL_LT		1
#define	MODEL_DL		2
#define	MODEL_LW		3
#define	MODEL_DW		4


typedef struct SCCBcard {
   PSCCB currentSCCB;
#if (FW_TYPE==_SCCB_MGR_)
   PSCCBMGR_INFO cardInfo;
#else
   PADAPTER_INFO cardInfo;
#endif

#if defined(DOS)
   USHORT ioPort;
#else
   ULONG ioPort;
#endif

   USHORT cmdCounter;
   UCHAR  discQCount;
   UCHAR  tagQ_Lst; 
   UCHAR cardIndex;
   UCHAR scanIndex;
   UCHAR globalFlags;
   UCHAR ourId;
   PNVRamInfo pNvRamInfo;
   PSCCB discQ_Tbl[QUEUE_DEPTH]; 
      
}SCCBCARD;

#if defined(DOS)
typedef struct SCCBcard near *PSCCBcard;
#elif defined (OS2)
typedef struct SCCBcard far *PSCCBcard;
#else
typedef struct SCCBcard *PSCCBcard;
#endif


#define F_TAG_STARTED		0x01
#define F_CONLUN_IO			0x02
#define F_DO_RENEGO			0x04
#define F_NO_FILTER			0x08
#define F_GREEN_PC			0x10
#define F_HOST_XFER_ACT		0x20
#define F_NEW_SCCB_CMD		0x40
#define F_UPDATE_EEPROM		0x80


#define  ID_STRING_LENGTH  32
#define  TYPE_CODE0        0x63           /*Level2 Mstr (bits 7-6),  */

#define  TYPE_CODE1        00             /*No ID yet */

#define  SLV_TYPE_CODE0    0xA3           /*Priority Bit set (bits 7-6),  */

#define  ASSIGN_ID   0x00
#define  SET_P_FLAG  0x01
#define  CFG_CMPLT   0x03
#define  DOM_MSTR    0x0F
#define  SYNC_PTRN   0x1F

#define  ID_0_7      0x18
#define  ID_8_F      0x11
#define  ID_10_17    0x12
#define  ID_18_1F    0x0B
#define  MISC_CODE   0x14
#define  CLR_P_FLAG  0x18
#define  LOCATE_ON   0x12
#define  LOCATE_OFF  0x0B

#define  LVL_1_MST   0x00
#define  LVL_2_MST   0x40
#define  DOM_LVL_2   0xC0


#define  INIT_SELTD  0x01
#define  LEVEL2_TAR  0x02


enum scam_id_st { ID0,ID1,ID2,ID3,ID4,ID5,ID6,ID7,ID8,ID9,ID10,ID11,ID12,
                  ID13,ID14,ID15,ID_UNUSED,ID_UNASSIGNED,ID_ASSIGNED,LEGACY,
                  CLR_PRIORITY,NO_ID_AVAIL };

typedef struct SCCBscam_info {

   UCHAR    id_string[ID_STRING_LENGTH];
   enum scam_id_st state;
    
} SCCBSCAM_INFO, *PSCCBSCAM_INFO;

#endif
/*----------------------------------------------------------------------
 *
 *
 *   Copyright 1995-1996 by Mylex Corporation.  All Rights Reserved
 *
 *   This file is available under both the GNU General Public License
 *   and a BSD-style copyright; see LICENSE.FlashPoint for details.
 *
 *   $Workfile:   scsi2.h  $
 *
 *   Description:  Register definitions for HARPOON ASIC.
 *
 *   $Date: 1996/11/13 18:32:57 $
 *
 *   $Revision: 1.4 $
 *
 *----------------------------------------------------------------------*/

#ifndef __SCSI_H__
#define __SCSI_H__



#define  SCSI_TEST_UNIT_READY    0x00
#define  SCSI_REZERO_UNIT        0x01
#define  SCSI_REQUEST_SENSE      0x03
#define  SCSI_FORMAT_UNIT        0x04
#define  SCSI_REASSIGN           0x07
#define  SCSI_READ               0x08
#define  SCSI_WRITE              0x0A
#define  SCSI_SEEK               0x0B
#define  SCSI_INQUIRY            0x12
#define  SCSI_MODE_SELECT        0x15
#define  SCSI_RESERVE_UNIT       0x16
#define  SCSI_RELEASE_UNIT       0x17
#define  SCSI_MODE_SENSE         0x1A
#define  SCSI_START_STOP_UNIT    0x1B
#define  SCSI_SEND_DIAGNOSTIC    0x1D
#define  SCSI_READ_CAPACITY      0x25
#define  SCSI_READ_EXTENDED      0x28
#define  SCSI_WRITE_EXTENDED     0x2A
#define  SCSI_SEEK_EXTENDED      0x2B
#define  SCSI_WRITE_AND_VERIFY   0x2E
#define  SCSI_VERIFY             0x2F
#define  SCSI_READ_DEFECT_DATA   0x37
#define  SCSI_WRITE_BUFFER       0x3B
#define  SCSI_READ_BUFFER        0x3C
#define  SCSI_RECV_DIAGNOSTIC    0x1C
#define  SCSI_READ_LONG          0x3E
#define  SCSI_WRITE_LONG         0x3F
#define  SCSI_LAST_SCSI_CMND     SCSI_WRITE_LONG
#define  SCSI_INVALID_CMND       0xFF



#define  SSGOOD                  0x00
#define  SSCHECK                 0x02
#define  SSCOND_MET              0x04
#define  SSBUSY                  0x08
#define  SSRESERVATION_CONFLICT  0x18
#define  SSCMD_TERM              0x22
#define  SSQ_FULL                0x28


#define  SKNO_SEN                0x00
#define  SKRECOV_ERR             0x01
#define  SKNOT_RDY               0x02
#define  SKMED_ERR               0x03
#define  SKHW_ERR                0x04
#define  SKILL_REQ               0x05
#define  SKUNIT_ATTN             0x06
#define  SKDATA_PROTECT          0x07
#define  SKBLNK_CHK              0x08
#define  SKCPY_ABORT             0x0A
#define  SKABORT_CMD             0x0B
#define  SKEQUAL                 0x0C
#define  SKVOL_OVF               0x0D
#define  SKMIS_CMP               0x0E


#define  SMCMD_COMP              0x00
#define  SMEXT                   0x01
#define  SMSAVE_DATA_PTR         0x02
#define  SMREST_DATA_PTR         0x03
#define  SMDISC                  0x04
#define  SMINIT_DETEC_ERR        0x05
#define  SMABORT                 0x06
#define  SMREJECT                0x07
#define  SMNO_OP                 0x08
#define  SMPARITY                0x09
#define  SMDEV_RESET             0x0C
#define	SMABORT_TAG					0x0D
#define	SMINIT_RECOVERY			0x0F
#define	SMREL_RECOVERY				0x10

#define  SMIDENT                 0x80
#define  DISC_PRIV               0x40


#define  SMSYNC                  0x01
#define  SM10MBS                 0x19     /* 100ns           */
#define  SM5MBS                  0x32     /* 200ns           */
#define  SMOFFSET                0x0F     /* Maxoffset value */
#define  SMWDTR                  0x03
#define  SM8BIT                  0x00
#define  SM16BIT                 0x01
#define  SM32BIT                 0x02
#define  SMIGNORWR               0x23     /* Ignore Wide Residue */


#define  ARBITRATION_DELAY       0x01     /* 2.4us using a 40Mhz clock */
#define  BUS_SETTLE_DELAY        0x01     /* 400ns */
#define  BUS_CLEAR_DELAY         0x01     /* 800ns */



#define  SPHASE_TO               0x0A  /* 10 second timeout waiting for */
#define  SCMD_TO                 0x0F  /* Overall command timeout */



#define  SIX_BYTE_CMD            0x06
#define  TEN_BYTE_CMD            0x0A
#define  TWELVE_BYTE_CMD         0x0C

#define  ASYNC                   0x00
#define  PERI25NS                0x06  /* 25/4ns to next clock for xbow. */
#define  SYNC10MBS               0x19
#define  SYNC5MBS                0x32
#define  MAX_OFFSET              0x0F  /* Maxbyteoffset for Sync Xfers */

#endif
/*----------------------------------------------------------------------
 *  
 *
 *   Copyright 1995-1996 by Mylex Corporation.  All Rights Reserved
 *
 *   This file is available under both the GNU General Public License
 *   and a BSD-style copyright; see LICENSE.FlashPoint for details.
 *
 *   $Workfile:   eeprom.h  $
 *
 *   Description:  Definitions for EEPROM related structures
 *
 *   $Date: 1996/11/13 18:28:39 $
 *
 *   $Revision: 1.4 $
 *
 *----------------------------------------------------------------------*/

#ifndef __EEPROM__
#define __EEPROM__

/*#include <globals.h>*/

#define  EEPROM_WD_CNT     256

#define  EEPROM_CHECK_SUM  0
#define  FW_SIGNATURE      2
#define  MODEL_NUMB_0      4
#define  MODEL_NUMB_1      5
#define  MODEL_NUMB_2      6
#define  MODEL_NUMB_3      7
#define  MODEL_NUMB_4      8
#define  MODEL_NUMB_5      9
#define  IO_BASE_ADDR      10
#define  IRQ_NUMBER        12
#define  PCI_INT_PIN       13
#define  BUS_DELAY         14       /*On time in byte 14 off delay in 15 */
#define  SYSTEM_CONFIG     16
#define  SCSI_CONFIG       17
#define  BIOS_CONFIG       18
#define  SPIN_UP_DELAY     19
#define  SCAM_CONFIG       20
#define  ADAPTER_SCSI_ID   24


#define  IGNORE_B_SCAN     32
#define  SEND_START_ENA    34
#define  DEVICE_ENABLE     36

#define  SYNC_RATE_TBL     38
#define  SYNC_RATE_TBL01   38
#define  SYNC_RATE_TBL23   40
#define  SYNC_RATE_TBL45   42
#define  SYNC_RATE_TBL67   44
#define  SYNC_RATE_TBL89   46
#define  SYNC_RATE_TBLab   48
#define  SYNC_RATE_TBLcd   50
#define  SYNC_RATE_TBLef   52



#define  EE_SCAMBASE      256 



   #define  DOM_MASTER     (BIT(0) + BIT(1))
   #define  SCAM_ENABLED   BIT(2)
   #define  SCAM_LEVEL2    BIT(3)


	#define	RENEGO_ENA		BITW(10)
	#define	CONNIO_ENA		BITW(11)
   #define  GREEN_PC_ENA   BITW(12)


   #define  AUTO_RATE_00   00
   #define  AUTO_RATE_05   01
   #define  AUTO_RATE_10   02
   #define  AUTO_RATE_20   03

   #define  WIDE_NEGO_BIT     BIT(7)
   #define  DISC_ENABLE_BIT   BIT(6)


#endif
/*----------------------------------------------------------------------
 *
 *
 *   Copyright 1995-1996 by Mylex Corporation.  All Rights Reserved
 *
 *   This file is available under both the GNU General Public License
 *   and a BSD-style copyright; see LICENSE.FlashPoint for details.
 *
 *   $Workfile:   harpoon.h  $
 *
 *   Description:  Register definitions for HARPOON ASIC.
 *
 *   $Date: 1997/07/09 21:44:36 $
 *
 *   $Revision: 1.9 $
 *
 *----------------------------------------------------------------------*/


/*#include <globals.h>*/

#ifndef __HARPOON__
#define __HARPOON__


   #define  hp_vendor_id_0       0x00		/* LSB */
      #define  ORION_VEND_0   0x4B
 
   #define  hp_vendor_id_1       0x01		/* MSB */
      #define  ORION_VEND_1   0x10

   #define  hp_device_id_0       0x02		/* LSB */
      #define  ORION_DEV_0    0x30 

   #define  hp_device_id_1       0x03		/* MSB */
      #define  ORION_DEV_1    0x81 

	/* Sub Vendor ID and Sub Device ID only available in
		Harpoon Version 2 and higher */

   #define  hp_sub_vendor_id_0   0x04		/* LSB */
   #define  hp_sub_vendor_id_1   0x05		/* MSB */
   #define  hp_sub_device_id_0   0x06		/* LSB */
   #define  hp_sub_device_id_1   0x07		/* MSB */


   #define  hp_dual_addr_lo      0x08
   #define  hp_dual_addr_lmi     0x09
   #define  hp_dual_addr_hmi     0x0A
   #define  hp_dual_addr_hi      0x0B

   #define  hp_semaphore         0x0C
      #define SCCB_MGR_ACTIVE    BIT(0)
      #define TICKLE_ME          BIT(1)
      #define SCCB_MGR_PRESENT   BIT(3)
      #define BIOS_IN_USE        BIT(4)

   #define  hp_user_defined_D    0x0D

   #define  hp_reserved_E        0x0E

   #define  hp_sys_ctrl          0x0F

      #define  STOP_CLK          BIT(0)      /*Turn off BusMaster Clock */
      #define  DRVR_RST          BIT(1)      /*Firmware Reset to 80C15 chip */
      #define  HALT_MACH         BIT(3)      /*Halt State Machine      */
      #define  HARD_ABORT        BIT(4)      /*Hard Abort              */
      #define  DIAG_MODE         BIT(5)      /*Diagnostic Mode         */

      #define  BM_ABORT_TMOUT    0x50        /*Halt State machine time out */

   #define  hp_sys_cfg           0x10

      #define  DONT_RST_FIFO     BIT(7)      /*Don't reset FIFO      */


   #define  hp_host_ctrl0        0x11

      #define  DUAL_ADDR_MODE    BIT(0)   /*Enable 64-bit addresses */
      #define  IO_MEM_SPACE      BIT(1)   /*I/O Memory Space    */
      #define  RESOURCE_LOCK     BIT(2)   /*Enable Resource Lock */
      #define  IGNOR_ACCESS_ERR  BIT(3)   /*Ignore Access Error */
      #define  HOST_INT_EDGE     BIT(4)   /*Host interrupt level/edge mode sel */
      #define  SIX_CLOCKS        BIT(5)   /*6 Clocks between Strobe   */
      #define  DMA_EVEN_PARITY   BIT(6)   /*Enable DMA Enen Parity */

/*
      #define  BURST_MODE        BIT(0)
*/

   #define  hp_reserved_12       0x12

   #define  hp_host_blk_cnt      0x13

      #define  XFER_BLK1         0x00     /*     0 0 0  1 byte per block*/
      #define  XFER_BLK2         0x01     /*     0 0 1  2 byte per block*/
      #define  XFER_BLK4         0x02     /*     0 1 0  4 byte per block*/
      #define  XFER_BLK8         0x03     /*     0 1 1  8 byte per block*/
      #define  XFER_BLK16        0x04     /*     1 0 0 16 byte per block*/
      #define  XFER_BLK32        0x05     /*     1 0 1 32 byte per block*/
      #define  XFER_BLK64        0x06     /*     1 1 0 64 byte per block*/
   
      #define  BM_THRESHOLD      0x40     /* PCI mode can only xfer 16 bytes*/


   #define  hp_reserved_14       0x14
   #define  hp_reserved_15       0x15
   #define  hp_reserved_16       0x16

   #define  hp_int_mask          0x17

      #define  INT_CMD_COMPL     BIT(0)   /* DMA command complete   */
      #define  INT_EXT_STATUS    BIT(1)   /* Extended Status Set    */
      #define  INT_SCSI          BIT(2)   /* Scsi block interrupt   */
      #define  INT_FIFO_RDY      BIT(4)   /* FIFO data ready        */


   #define  hp_xfer_cnt_lo       0x18
   #define  hp_xfer_cnt_mi       0x19
   #define  hp_xfer_cnt_hi       0x1A
   #define  hp_xfer_cmd          0x1B

      #define  XFER_HOST_DMA     0x00     /*     0 0 0 Transfer Host -> DMA */
      #define  XFER_DMA_HOST     0x01     /*     0 0 1 Transfer DMA  -> Host */
      #define  XFER_HOST_MPU     0x02     /*     0 1 0 Transfer Host -> MPU  */
      #define  XFER_MPU_HOST     0x03     /*     0 1 1 Transfer MPU  -> Host */
      #define  XFER_DMA_MPU      0x04     /*     1 0 0 Transfer DMA  -> MPU  */
      #define  XFER_MPU_DMA      0x05     /*     1 0 1 Transfer MPU  -> DMA  */
      #define  SET_SEMAPHORE     0x06     /*     1 1 0 Set Semaphore         */
      #define  XFER_NOP          0x07     /*     1 1 1 Transfer NOP          */
      #define  XFER_MB_MPU       0x06     /*     1 1 0 Transfer MB -> MPU */
      #define  XFER_MB_DMA       0x07     /*     1 1 1 Transfer MB -> DMA */


      #define  XFER_HOST_AUTO    0x00     /*     0 0 Auto Transfer Size   */
      #define  XFER_HOST_8BIT    0x08     /*     0 1 8 BIT Transfer Size  */
      #define  XFER_HOST_16BIT   0x10     /*     1 0 16 BIT Transfer Size */
      #define  XFER_HOST_32BIT   0x18     /*     1 1 32 BIT Transfer Size */

      #define  XFER_DMA_8BIT     0x20     /*     0 1 8 BIT  Transfer Size */
      #define  XFER_DMA_16BIT    0x40     /*     1 0 16 BIT Transfer Size */

      #define  DISABLE_INT       BIT(7)   /*Do not interrupt at end of cmd. */

      #define  HOST_WRT_CMD      ((DISABLE_INT + XFER_HOST_DMA + XFER_HOST_AUTO + XFER_DMA_8BIT))
      #define  HOST_RD_CMD       ((DISABLE_INT + XFER_DMA_HOST + XFER_HOST_AUTO + XFER_DMA_8BIT))
      #define  WIDE_HOST_WRT_CMD ((DISABLE_INT + XFER_HOST_DMA + XFER_HOST_AUTO + XFER_DMA_16BIT))
      #define  WIDE_HOST_RD_CMD  ((DISABLE_INT + XFER_DMA_HOST + XFER_HOST_AUTO + XFER_DMA_16BIT))

   #define  hp_host_addr_lo      0x1C
   #define  hp_host_addr_lmi     0x1D
   #define  hp_host_addr_hmi     0x1E
   #define  hp_host_addr_hi      0x1F

   #define  hp_pio_data          0x20
   #define  hp_reserved_21       0x21
   #define  hp_ee_ctrl           0x22

      #define  EXT_ARB_ACK       BIT(7)
      #define  SCSI_TERM_ENA_H   BIT(6)   /* SCSI high byte terminator */
      #define  SEE_MS            BIT(5)
      #define  SEE_CS            BIT(3)
      #define  SEE_CLK           BIT(2)
      #define  SEE_DO            BIT(1)
      #define  SEE_DI            BIT(0)

      #define  EE_READ           0x06
      #define  EE_WRITE          0x05
      #define  EWEN              0x04
      #define  EWEN_ADDR         0x03C0
      #define  EWDS              0x04
      #define  EWDS_ADDR         0x0000

   #define  hp_brdctl            0x23

      #define  DAT_7             BIT(7)
      #define  DAT_6             BIT(6)
      #define  DAT_5             BIT(5)
      #define  BRD_STB           BIT(4)
      #define  BRD_CS            BIT(3)
      #define  BRD_WR            BIT(2)

   #define  hp_reserved_24       0x24
   #define  hp_reserved_25       0x25




   #define  hp_bm_ctrl           0x26

      #define  SCSI_TERM_ENA_L   BIT(0)   /*Enable/Disable external terminators */
      #define  FLUSH_XFER_CNTR   BIT(1)   /*Flush transfer counter */
      #define  BM_XFER_MIN_8     BIT(2)   /*Enable bus master transfer of 9 */
      #define  BIOS_ENA          BIT(3)   /*Enable BIOS/FLASH Enable */
      #define  FORCE1_XFER       BIT(5)   /*Always xfer one byte in byte mode */
      #define  FAST_SINGLE       BIT(6)   /*?? */

      #define  BMCTRL_DEFAULT    (FORCE1_XFER|FAST_SINGLE|SCSI_TERM_ENA_L)

   #define  hp_reserved_27       0x27

   #define  hp_sg_addr           0x28
   #define  hp_page_ctrl         0x29

      #define  SCATTER_EN        BIT(0)   
      #define  SGRAM_ARAM        BIT(1)   
      #define  BIOS_SHADOW       BIT(2)   
      #define  G_INT_DISABLE     BIT(3)   /* Enable/Disable all Interrupts */
      #define  NARROW_SCSI_CARD  BIT(4)   /* NARROW/WIDE SCSI config pin */

   #define  hp_reserved_2A       0x2A
   #define  hp_pci_cmd_cfg       0x2B

      #define  IO_SPACE_ENA      BIT(0)   /*enable I/O space */
      #define  MEM_SPACE_ENA     BIT(1)   /*enable memory space */
      #define  BUS_MSTR_ENA      BIT(2)   /*enable bus master operation */
      #define  MEM_WI_ENA        BIT(4)   /*enable Write and Invalidate */
      #define  PAR_ERR_RESP      BIT(6)   /*enable parity error responce. */

   #define  hp_reserved_2C       0x2C

   #define  hp_pci_stat_cfg      0x2D

      #define  DATA_PARITY_ERR   BIT(0)   
      #define  REC_TARGET_ABORT  BIT(4)   /*received Target abort */
      #define  REC_MASTER_ABORT  BIT(5)   /*received Master abort */
      #define  SIG_SYSTEM_ERR    BIT(6)   
      #define  DETECTED_PAR_ERR  BIT(7)   

   #define  hp_reserved_2E       0x2E

   #define  hp_sys_status        0x2F

      #define  SLV_DATA_RDY      BIT(0)   /*Slave data ready */
      #define  XFER_CNT_ZERO     BIT(1)   /*Transfer counter = 0 */
      #define  BM_FIFO_EMPTY     BIT(2)   /*FIFO empty */
      #define  BM_FIFO_FULL      BIT(3)   /*FIFO full */
      #define  HOST_OP_DONE      BIT(4)   /*host operation done */
      #define  DMA_OP_DONE       BIT(5)   /*DMA operation done */
      #define  SLV_OP_DONE       BIT(6)   /*Slave operation done */
      #define  PWR_ON_FLAG       BIT(7)   /*Power on flag */

   #define  hp_reserved_30       0x30

   #define  hp_host_status0      0x31

      #define  HOST_TERM         BIT(5)   /*Host Terminal Count */
      #define  HOST_TRSHLD       BIT(6)   /*Host Threshold      */
      #define  CONNECTED_2_HOST  BIT(7)   /*Connected to Host   */

   #define  hp_reserved_32       0x32

   #define  hp_rev_num           0x33

      #define  REV_A_CONST       0x0E
      #define  REV_B_CONST       0x0E

   #define  hp_stack_data        0x34
   #define  hp_stack_addr        0x35

   #define  hp_ext_status        0x36

      #define  BM_FORCE_OFF      BIT(0)   /*Bus Master is forced to get off */
      #define  PCI_TGT_ABORT     BIT(0)   /*PCI bus master transaction aborted */
      #define  PCI_DEV_TMOUT     BIT(1)   /*PCI Device Time out */
      #define  FIFO_TC_NOT_ZERO  BIT(2)   /*FIFO or transfer counter not zero */
      #define  CHIP_RST_OCCUR    BIT(3)   /*Chip reset occurs */
      #define  CMD_ABORTED       BIT(4)   /*Command aborted */
      #define  BM_PARITY_ERR     BIT(5)   /*parity error on data received   */
      #define  PIO_OVERRUN       BIT(6)   /*Slave data overrun */
      #define  BM_CMD_BUSY       BIT(7)   /*Bus master transfer command busy */
      #define  BAD_EXT_STATUS    (BM_FORCE_OFF | PCI_DEV_TMOUT | CMD_ABORTED | \
                                  BM_PARITY_ERR | PIO_OVERRUN)

   #define  hp_int_status        0x37
      
      #define  BM_CMD_CMPL       BIT(0)   /*Bus Master command complete */
      #define  EXT_STATUS_ON     BIT(1)   /*Extended status is valid */
      #define  SCSI_INTERRUPT    BIT(2)   /*Global indication of a SCSI int. */
      #define  BM_FIFO_RDY       BIT(4)   
      #define  INT_ASSERTED      BIT(5)   /* */
      #define  SRAM_BUSY         BIT(6)   /*Scatter/Gather RAM busy */
      #define  CMD_REG_BUSY      BIT(7)                                       


   #define  hp_fifo_cnt          0x38
   #define  hp_curr_host_cnt     0x39
   #define  hp_reserved_3A       0x3A
   #define  hp_fifo_in_addr      0x3B

   #define  hp_fifo_out_addr     0x3C
   #define  hp_reserved_3D       0x3D
   #define  hp_reserved_3E       0x3E
   #define  hp_reserved_3F       0x3F



   extern USHORT default_intena;

   #define  hp_intena		 0x40

      #define  RESET		 BITW(7)
      #define  PROG_HLT		 BITW(6)  
      #define  PARITY		 BITW(5)
      #define  FIFO		 BITW(4)
      #define  SEL		 BITW(3)
      #define  SCAM_SEL		 BITW(2) 
      #define  RSEL		 BITW(1)
      #define  TIMEOUT		 BITW(0)
      #define  BUS_FREE		 BITW(15)
      #define  XFER_CNT_0	 BITW(14)
      #define  PHASE		 BITW(13)
      #define  IUNKWN		 BITW(12)
      #define  ICMD_COMP	 BITW(11)
      #define  ITICKLE		 BITW(10)
      #define  IDO_STRT		 BITW(9)
      #define  ITAR_DISC	 BITW(8)
      #define  AUTO_INT		 (BITW(12)+BITW(11)+BITW(10)+BITW(9)+BITW(8))
      #define  CLR_ALL_INT	 0xFFFF
      #define  CLR_ALL_INT_1	 0xFF00

   #define  hp_intstat		 0x42

   #define  hp_scsisig           0x44

      #define  SCSI_SEL          BIT(7)
      #define  SCSI_BSY          BIT(6)
      #define  SCSI_REQ          BIT(5)
      #define  SCSI_ACK          BIT(4)
      #define  SCSI_ATN          BIT(3)
      #define  SCSI_CD           BIT(2)
      #define  SCSI_MSG          BIT(1)
      #define  SCSI_IOBIT        BIT(0)

      #define  S_SCSI_PHZ        (BIT(2)+BIT(1)+BIT(0))
      #define  S_CMD_PH          (BIT(2)              )
      #define  S_MSGO_PH         (BIT(2)+BIT(1)       )
      #define  S_STAT_PH         (BIT(2)       +BIT(0))
      #define  S_MSGI_PH         (BIT(2)+BIT(1)+BIT(0))
      #define  S_DATAI_PH        (              BIT(0))
      #define  S_DATAO_PH        0x00
      #define  S_ILL_PH          (       BIT(1)       )

   #define  hp_scsictrl_0        0x45

      #define  NO_ARB            BIT(7)
      #define  SEL_TAR           BIT(6)
      #define  ENA_ATN           BIT(4)
      #define  ENA_RESEL         BIT(2)
      #define  SCSI_RST          BIT(1)
      #define  ENA_SCAM_SEL      BIT(0)



   #define  hp_portctrl_0        0x46

      #define  SCSI_PORT         BIT(7)
      #define  SCSI_INBIT        BIT(6)
      #define  DMA_PORT          BIT(5)
      #define  DMA_RD            BIT(4)
      #define  HOST_PORT         BIT(3)
      #define  HOST_WRT          BIT(2)
      #define  SCSI_BUS_EN       BIT(1)
      #define  START_TO          BIT(0)

   #define  hp_scsireset         0x47

      #define  SCSI_TAR          BIT(7)
      #define  SCSI_INI          BIT(6)
      #define  SCAM_EN           BIT(5)
      #define  ACK_HOLD          BIT(4)
      #define  DMA_RESET         BIT(3)
      #define  HPSCSI_RESET      BIT(2)
      #define  PROG_RESET        BIT(1)
      #define  FIFO_CLR          BIT(0)

   #define  hp_xfercnt_0         0x48
   #define  hp_xfercnt_1         0x49
   #define  hp_xfercnt_2         0x4A
   #define  hp_xfercnt_3         0x4B

   #define  hp_fifodata_0        0x4C
   #define  hp_fifodata_1        0x4D
   #define  hp_addstat           0x4E

      #define  SCAM_TIMER        BIT(7)
      #define  AUTO_RUNNING      BIT(6)
      #define  FAST_SYNC         BIT(5)
      #define  SCSI_MODE8        BIT(3)
      #define  SCSI_PAR_ERR      BIT(0)

   #define  hp_prgmcnt_0         0x4F

      #define  AUTO_PC_MASK      0x3F

   #define  hp_selfid_0          0x50
   #define  hp_selfid_1          0x51
   #define  hp_arb_id            0x52

      #define  ARB_ID            (BIT(3) + BIT(2) + BIT(1) + BIT(0))

   #define  hp_select_id         0x53

      #define  RESEL_ID          (BIT(7) + BIT(6) + BIT(5) + BIT(4))
      #define  SELECT_ID         (BIT(3) + BIT(2) + BIT(1) + BIT(0))

   #define  hp_synctarg_base     0x54
   #define  hp_synctarg_12       0x54
   #define  hp_synctarg_13       0x55
   #define  hp_synctarg_14       0x56
   #define  hp_synctarg_15       0x57

   #define  hp_synctarg_8        0x58
   #define  hp_synctarg_9        0x59
   #define  hp_synctarg_10       0x5A
   #define  hp_synctarg_11       0x5B

   #define  hp_synctarg_4        0x5C
   #define  hp_synctarg_5        0x5D
   #define  hp_synctarg_6        0x5E
   #define  hp_synctarg_7        0x5F

   #define  hp_synctarg_0        0x60
   #define  hp_synctarg_1        0x61
   #define  hp_synctarg_2        0x62
   #define  hp_synctarg_3        0x63

      #define  RATE_20MB         0x00
      #define  RATE_10MB         (              BIT(5))
      #define  RATE_6_6MB        (       BIT(6)       )   
      #define  RATE_5MB          (       BIT(6)+BIT(5))
      #define  RATE_4MB          (BIT(7)              )
      #define  RATE_3_33MB       (BIT(7)       +BIT(5))
      #define  RATE_2_85MB       (BIT(7)+BIT(6)       )
      #define  RATE_2_5MB        (BIT(7)+BIT(5)+BIT(6))
      #define  NEXT_CLK          BIT(5)
      #define  SLOWEST_SYNC      (BIT(7)+BIT(6)+BIT(5))
      #define  NARROW_SCSI       BIT(4)
      #define  SYNC_OFFSET       (BIT(3) + BIT(2) + BIT(1) + BIT(0))
      #define  DEFAULT_ASYNC     0x00
      #define  DEFAULT_OFFSET    0x0F

   #define  hp_autostart_0       0x64
   #define  hp_autostart_1       0x65
   #define  hp_autostart_2       0x66
   #define  hp_autostart_3       0x67



      #define  DISABLE  0x00
      #define  AUTO_IMMED    BIT(5)
      #define  SELECT   BIT(6)
      #define  RESELECT (BIT(6)+BIT(5))
      #define  BUSFREE  BIT(7)
      #define  XFER_0   (BIT(7)+BIT(5))
      #define  END_DATA (BIT(7)+BIT(6))
      #define  MSG_PHZ  (BIT(7)+BIT(6)+BIT(5))

   #define  hp_gp_reg_0          0x68
   #define  hp_gp_reg_1          0x69
   #define  hp_gp_reg_2          0x6A
   #define  hp_gp_reg_3          0x6B

   #define  hp_seltimeout        0x6C


      #define  TO_2ms            0x54      /* 2.0503ms */
      #define  TO_4ms            0x67      /* 3.9959ms */

      #define  TO_5ms            0x03      /* 4.9152ms */
      #define  TO_10ms           0x07      /* 11.xxxms */
      #define  TO_250ms          0x99      /* 250.68ms */
      #define  TO_290ms          0xB1      /* 289.99ms */
      #define  TO_350ms          0xD6      /* 350.62ms */
      #define  TO_417ms          0xFF      /* 417.79ms */

   #define  hp_clkctrl_0         0x6D

      #define  PWR_DWN           BIT(6)
      #define  ACTdeassert       BIT(4)
      #define  ATNonErr          BIT(3)
      #define  CLK_30MHZ         BIT(1)
      #define  CLK_40MHZ         (BIT(1) + BIT(0))
      #define  CLK_50MHZ         BIT(2)

      #define  CLKCTRL_DEFAULT   (ACTdeassert | CLK_40MHZ)

   #define  hp_fiforead          0x6E
   #define  hp_fifowrite         0x6F

   #define  hp_offsetctr         0x70
   #define  hp_xferstat          0x71

      #define  FIFO_FULL         BIT(7)
      #define  FIFO_EMPTY        BIT(6)
      #define  FIFO_MASK         0x3F   /* Mask for the FIFO count value. */
      #define  FIFO_LEN          0x20

   #define  hp_portctrl_1        0x72

      #define  EVEN_HOST_P       BIT(5)
      #define  INVT_SCSI         BIT(4)
      #define  CHK_SCSI_P        BIT(3)
      #define  HOST_MODE8        BIT(0)
      #define  HOST_MODE16       0x00

   #define  hp_xfer_pad          0x73

      #define  ID_UNLOCK         BIT(3)
      #define  XFER_PAD          BIT(2)

   #define  hp_scsidata_0        0x74
   #define  hp_scsidata_1        0x75
   #define  hp_timer_0           0x76
   #define  hp_timer_1           0x77

   #define  hp_reserved_78       0x78
   #define  hp_reserved_79       0x79
   #define  hp_reserved_7A       0x7A
   #define  hp_reserved_7B       0x7B

   #define  hp_reserved_7C       0x7C
   #define  hp_reserved_7D       0x7D
   #define  hp_reserved_7E       0x7E
   #define  hp_reserved_7F       0x7F

   #define  hp_aramBase          0x80
   #define  BIOS_DATA_OFFSET     0x60
   #define  BIOS_RELATIVE_CARD   0x64




      #define  AUTO_LEN 0x80
      #define  AR0      0x00
      #define  AR1      BITW(8)
      #define  AR2      BITW(9)
      #define  AR3      (BITW(9) + BITW(8))
      #define  SDATA    BITW(10)

      #define  NOP_OP   0x00        /* Nop command */

      #define  CRD_OP   BITW(11)     /* Cmp Reg. w/ Data */

      #define  CRR_OP   BITW(12)     /* Cmp Reg. w. Reg. */

      #define  CBE_OP   (BITW(14)+BITW(12)+BITW(11)) /* Cmp SCSI cmd class & Branch EQ */
      
      #define  CBN_OP   (BITW(14)+BITW(13))  /* Cmp SCSI cmd class & Branch NOT EQ */
      
      #define  CPE_OP   (BITW(14)+BITW(11))  /* Cmp SCSI phs & Branch EQ */

      #define  CPN_OP   (BITW(14)+BITW(12))  /* Cmp SCSI phs & Branch NOT EQ */


      #define  ADATA_OUT   0x00     
      #define  ADATA_IN    BITW(8)
      #define  ACOMMAND    BITW(10)
      #define  ASTATUS     (BITW(10)+BITW(8))
      #define  AMSG_OUT    (BITW(10)+BITW(9))
      #define  AMSG_IN     (BITW(10)+BITW(9)+BITW(8))
      #define  AILLEGAL    (BITW(9)+BITW(8))


      #define  BRH_OP   BITW(13)   /* Branch */

      
      #define  ALWAYS   0x00
      #define  EQUAL    BITW(8)
      #define  NOT_EQ   BITW(9)

      #define  TCB_OP   (BITW(13)+BITW(11))    /* Test condition & branch */

      
      #define  ATN_SET     BITW(8)
      #define  ATN_RESET   BITW(9)
      #define  XFER_CNT    (BITW(9)+BITW(8))
      #define  FIFO_0      BITW(10)
      #define  FIFO_NOT0   (BITW(10)+BITW(8))
      #define  T_USE_SYNC0 (BITW(10)+BITW(9))


      #define  MPM_OP   BITW(15)        /* Match phase and move data */

      #define  MDR_OP   (BITW(12)+BITW(11)) /* Move data to Reg. */

      #define  MRR_OP   BITW(14)        /* Move DReg. to Reg. */


      #define  S_IDREG  (BIT(2)+BIT(1)+BIT(0))


      #define  D_AR0    0x00
      #define  D_AR1    BIT(0)
      #define  D_AR2    BIT(1)
      #define  D_AR3    (BIT(1) + BIT(0))
      #define  D_SDATA  BIT(2)
      #define  D_BUCKET (BIT(2) + BIT(1) + BIT(0))


      #define  ADR_OP   (BITW(13)+BITW(12)) /* Logical AND Reg. w. Data */

      #define  ADS_OP   (BITW(14)+BITW(13)+BITW(12)) 

      #define  ODR_OP   (BITW(13)+BITW(12)+BITW(11))  

      #define  ODS_OP   (BITW(14)+BITW(13)+BITW(12)+BITW(11))  

      #define  STR_OP   (BITW(15)+BITW(14)) /* Store to A_Reg. */

      #define  AINT_ENA1   0x00
      #define  AINT_STAT1  BITW(8)
      #define  ASCSI_SIG   BITW(9)
      #define  ASCSI_CNTL  (BITW(9)+BITW(8))
      #define  APORT_CNTL  BITW(10)
      #define  ARST_CNTL   (BITW(10)+BITW(8))
      #define  AXFERCNT0   (BITW(10)+BITW(9))
      #define  AXFERCNT1   (BITW(10)+BITW(9)+BITW(8))
      #define  AXFERCNT2   BITW(11)
      #define  AFIFO_DATA  (BITW(11)+BITW(8))
      #define  ASCSISELID  (BITW(11)+BITW(9))
      #define  ASCSISYNC0  (BITW(11)+BITW(9)+BITW(8))


      #define  RAT_OP      (BITW(14)+BITW(13)+BITW(11))

      #define  SSI_OP      (BITW(15)+BITW(11))


      #define  SSI_ITAR_DISC	(ITAR_DISC >> 8)
      #define  SSI_IDO_STRT	(IDO_STRT >> 8)
      #define  SSI_IDI_STRT	(IDO_STRT >> 8)

      #define  SSI_ICMD_COMP	(ICMD_COMP >> 8)
      #define  SSI_ITICKLE	(ITICKLE >> 8)

      #define  SSI_IUNKWN	(IUNKWN >> 8)
      #define  SSI_INO_CC	(IUNKWN >> 8)
      #define  SSI_IRFAIL	(IUNKWN >> 8)


      #define  NP    0x10     /*Next Phase */
      #define  NTCMD 0x02     /*Non- Tagged Command start */
      #define  CMDPZ 0x04     /*Command phase */
      #define  DINT  0x12     /*Data Out/In interrupt */
      #define  DI    0x13     /*Data Out */
      #define  MI    0x14     /*Message In */
      #define  DC    0x19     /*Disconnect Message */
      #define  ST    0x1D     /*Status Phase */
      #define  UNKNWN 0x24    /*Unknown bus action */
      #define  CC    0x25     /*Command Completion failure */
      #define  TICK  0x26     /*New target reselected us. */
      #define  RFAIL 0x27     /*Reselection failed */
      #define  SELCHK 0x28     /*Select & Check SCSI ID latch reg */


      #define  ID_MSG_STRT    hp_aramBase + 0x00
      #define  NON_TAG_ID_MSG hp_aramBase + 0x06
      #define  CMD_STRT       hp_aramBase + 0x08
      #define  SYNC_MSGS      hp_aramBase + 0x08





      #define  TAG_STRT          0x00
      #define  SELECTION_START   0x00
      #define  DISCONNECT_START  0x10/2
      #define  END_DATA_START    0x14/2
      #define  NONTAG_STRT       0x02/2
      #define  CMD_ONLY_STRT     CMDPZ/2
      #define  TICKLE_STRT     TICK/2
      #define  SELCHK_STRT     SELCHK/2




#define mEEPROM_CLK_DELAY(port) (RD_HARPOON(port+hp_intstat_1))

#define mWAIT_10MS(port) (RD_HARPOON(port+hp_intstat_1))


#define CLR_XFER_CNT(port) (WR_HARPOON(port+hp_xfercnt_0, 0x00))

#define SET_XFER_CNT(port, data) (WR_HARP32(port,hp_xfercnt_0,data))

#define GET_XFER_CNT(port, xfercnt) {RD_HARP32(port,hp_xfercnt_0,xfercnt); xfercnt &= 0xFFFFFF;}
/* #define GET_XFER_CNT(port, xfercnt) (xfercnt = RD_HARPOON(port+hp_xfercnt_2), \
                                 xfercnt <<= 16,\
                                 xfercnt |= RDW_HARPOON((USHORT)(port+hp_xfercnt_0)))
 */
#if defined(DOS)
#define HP_SETUP_ADDR_CNT(port,addr,count) (WRW_HARPOON((USHORT)(port+hp_host_addr_lo), (USHORT)(addr & 0x0000FFFFL)),\
         addr >>= 16,\
         WRW_HARPOON((USHORT)(port+hp_host_addr_hmi), (USHORT)(addr & 0x0000FFFFL)),\
         WR_HARP32(port,hp_xfercnt_0,count),\
         WRW_HARPOON((USHORT)(port+hp_xfer_cnt_lo), (USHORT)(count & 0x0000FFFFL)),\
         count >>= 16,\
         WR_HARPOON(port+hp_xfer_cnt_hi, (count & 0xFF)))
#else
#define HP_SETUP_ADDR_CNT(port,addr,count) (WRW_HARPOON((port+hp_host_addr_lo), (USHORT)(addr & 0x0000FFFFL)),\
         addr >>= 16,\
         WRW_HARPOON((port+hp_host_addr_hmi), (USHORT)(addr & 0x0000FFFFL)),\
         WR_HARP32(port,hp_xfercnt_0,count),\
         WRW_HARPOON((port+hp_xfer_cnt_lo), (USHORT)(count & 0x0000FFFFL)),\
         count >>= 16,\
         WR_HARPOON(port+hp_xfer_cnt_hi, (count & 0xFF)))
#endif

#define ACCEPT_MSG(port) {while(RD_HARPOON(port+hp_scsisig) & SCSI_REQ){}\
                          WR_HARPOON(port+hp_scsisig, S_ILL_PH);}


#define ACCEPT_MSG_ATN(port) {while(RD_HARPOON(port+hp_scsisig) & SCSI_REQ){}\
                          WR_HARPOON(port+hp_scsisig, (S_ILL_PH|SCSI_ATN));}

#define ACCEPT_STAT(port) {while(RD_HARPOON(port+hp_scsisig) & SCSI_REQ){}\
                          WR_HARPOON(port+hp_scsisig, S_ILL_PH);}

#define ACCEPT_STAT_ATN(port) {while(RD_HARPOON(port+hp_scsisig) & SCSI_REQ){}\
                          WR_HARPOON(port+hp_scsisig, (S_ILL_PH|SCSI_ATN));}

#define DISABLE_AUTO(port) (WR_HARPOON(port+hp_scsireset, PROG_RESET),\
                        WR_HARPOON(port+hp_scsireset, 0x00))

#define ARAM_ACCESS(p_port) (WR_HARPOON(p_port+hp_page_ctrl, \
                             (RD_HARPOON(p_port+hp_page_ctrl) | SGRAM_ARAM)))

#define SGRAM_ACCESS(p_port) (WR_HARPOON(p_port+hp_page_ctrl, \
                             (RD_HARPOON(p_port+hp_page_ctrl) & ~SGRAM_ARAM)))

#define MDISABLE_INT(p_port) (WR_HARPOON(p_port+hp_page_ctrl, \
                             (RD_HARPOON(p_port+hp_page_ctrl) | G_INT_DISABLE)))

#define MENABLE_INT(p_port) (WR_HARPOON(p_port+hp_page_ctrl, \
                             (RD_HARPOON(p_port+hp_page_ctrl) & ~G_INT_DISABLE)))



#endif


#if (FW_TYPE==_UCB_MGR_)
void ReadNVRam(PSCCBcard pCurrCard,PUCB p_ucb);
void WriteNVRam(PSCCBcard pCurrCard,PUCB p_ucb);
void UpdateCheckSum(u32bits baseport);
#endif // (FW_TYPE==_UCB_MGR_)

#if defined(DOS)
UCHAR sfm(USHORT port, PSCCB pcurrSCCB);
void  scsiStartAuto(USHORT port);
UCHAR sisyncn(USHORT port, UCHAR p_card, UCHAR syncFlag);
void  ssel(USHORT port, UCHAR p_card);
void  sres(USHORT port, UCHAR p_card, PSCCBcard pCurrCard);
void  sdecm(UCHAR message, USHORT port, UCHAR p_card);
void  shandem(USHORT port, UCHAR p_card,PSCCB pCurrSCCB);
void  stsyncn(USHORT port, UCHAR p_card);
void  sisyncr(USHORT port,UCHAR sync_pulse, UCHAR offset);
void  sssyncv(USHORT p_port, UCHAR p_id, UCHAR p_sync_value, PSCCBMgr_tar_info currTar_Info);
void  sresb(USHORT port, UCHAR p_card);
void  sxfrp(USHORT p_port, UCHAR p_card);
void  schkdd(USHORT port, UCHAR p_card);
UCHAR RdStack(USHORT port, UCHAR index);
void  WrStack(USHORT portBase, UCHAR index, UCHAR data);
UCHAR ChkIfChipInitialized(USHORT ioPort);

#if defined(V302)
UCHAR GetTarLun(USHORT port, UCHAR p_card, UCHAR our_target, PSCCBcard pCurrCard, PUCHAR tag, PUCHAR lun);
#endif

void SendMsg(USHORT port, UCHAR message);
void  queueFlushTargSccb(UCHAR p_card, UCHAR thisTarg, UCHAR error_code);
UCHAR scsellDOS(USHORT p_port, UCHAR targ_id);
#else
UCHAR sfm(ULONG port, PSCCB pcurrSCCB);
void  scsiStartAuto(ULONG port);
UCHAR sisyncn(ULONG port, UCHAR p_card, UCHAR syncFlag);
void  ssel(ULONG port, UCHAR p_card);
void  sres(ULONG port, UCHAR p_card, PSCCBcard pCurrCard);
void  sdecm(UCHAR message, ULONG port, UCHAR p_card);
void  shandem(ULONG port, UCHAR p_card,PSCCB pCurrSCCB);
void  stsyncn(ULONG port, UCHAR p_card);
void  sisyncr(ULONG port,UCHAR sync_pulse, UCHAR offset);
void  sssyncv(ULONG p_port, UCHAR p_id, UCHAR p_sync_value, PSCCBMgr_tar_info currTar_Info);
void  sresb(ULONG port, UCHAR p_card);
void  sxfrp(ULONG p_port, UCHAR p_card);
void  schkdd(ULONG port, UCHAR p_card);
UCHAR RdStack(ULONG port, UCHAR index);
void  WrStack(ULONG portBase, UCHAR index, UCHAR data);
UCHAR ChkIfChipInitialized(ULONG ioPort);

#if defined(V302)
UCHAR GetTarLun(ULONG port, UCHAR p_card, UCHAR our_target, PSCCBcard pCurrCard, PUCHAR tar, PUCHAR lun);
#endif

void SendMsg(ULONG port, UCHAR message);
void  queueFlushTargSccb(UCHAR p_card, UCHAR thisTarg, UCHAR error_code);
#endif

void  ssenss(PSCCBcard pCurrCard);
void  sinits(PSCCB p_sccb, UCHAR p_card);
void  RNVRamData(PNVRamInfo pNvRamInfo);

#if defined(WIDE_SCSI)
   #if defined(DOS)
   UCHAR siwidn(USHORT port, UCHAR p_card);
   void  stwidn(USHORT port, UCHAR p_card);
   void  siwidr(USHORT port, UCHAR width);
   #else
   UCHAR siwidn(ULONG port, UCHAR p_card);
   void  stwidn(ULONG port, UCHAR p_card);
   void  siwidr(ULONG port, UCHAR width);
   #endif
#endif


void  queueSelectFail(PSCCBcard pCurrCard, UCHAR p_card);
void  queueDisconnect(PSCCB p_SCCB, UCHAR p_card);
void  queueCmdComplete(PSCCBcard pCurrCard, PSCCB p_SCCB, UCHAR p_card);
void  queueSearchSelect(PSCCBcard pCurrCard, UCHAR p_card);
void  queueFlushSccb(UCHAR p_card, UCHAR error_code);
void  queueAddSccb(PSCCB p_SCCB, UCHAR card);
UCHAR queueFindSccb(PSCCB p_SCCB, UCHAR p_card);
void  utilUpdateResidual(PSCCB p_SCCB);
USHORT CalcCrc16(UCHAR buffer[]);
UCHAR  CalcLrc(UCHAR buffer[]);


#if defined(DOS)
void  Wait1Second(USHORT p_port);
void  Wait(USHORT p_port, UCHAR p_delay);
void  utilEEWriteOnOff(USHORT p_port,UCHAR p_mode);
void  utilEEWrite(USHORT p_port, USHORT ee_data, USHORT ee_addr);
USHORT utilEERead(USHORT p_port, USHORT ee_addr);
USHORT utilEEReadOrg(USHORT p_port, USHORT ee_addr);
void  utilEESendCmdAddr(USHORT p_port, UCHAR ee_cmd, USHORT ee_addr);
#else
void  Wait1Second(ULONG p_port);
void  Wait(ULONG p_port, UCHAR p_delay);
void  utilEEWriteOnOff(ULONG p_port,UCHAR p_mode);
void  utilEEWrite(ULONG p_port, USHORT ee_data, USHORT ee_addr);
USHORT utilEERead(ULONG p_port, USHORT ee_addr);
USHORT utilEEReadOrg(ULONG p_port, USHORT ee_addr);
void  utilEESendCmdAddr(ULONG p_port, UCHAR ee_cmd, USHORT ee_addr);
#endif



#if defined(OS2)
   void  far phaseDataOut(ULONG port, UCHAR p_card);
   void  far phaseDataIn(ULONG port, UCHAR p_card);
   void  far phaseCommand(ULONG port, UCHAR p_card);
   void  far phaseStatus(ULONG port, UCHAR p_card);
   void  far phaseMsgOut(ULONG port, UCHAR p_card);
   void  far phaseMsgIn(ULONG port, UCHAR p_card);
   void  far phaseIllegal(ULONG port, UCHAR p_card);
#else
   #if defined(DOS)
      void  phaseDataOut(USHORT port, UCHAR p_card);
      void  phaseDataIn(USHORT port, UCHAR p_card);
      void  phaseCommand(USHORT port, UCHAR p_card);
      void  phaseStatus(USHORT port, UCHAR p_card);
      void  phaseMsgOut(USHORT port, UCHAR p_card);
      void  phaseMsgIn(USHORT port, UCHAR p_card);
      void  phaseIllegal(USHORT port, UCHAR p_card);
   #else
      void  phaseDataOut(ULONG port, UCHAR p_card);
      void  phaseDataIn(ULONG port, UCHAR p_card);
      void  phaseCommand(ULONG port, UCHAR p_card);
      void  phaseStatus(ULONG port, UCHAR p_card);
      void  phaseMsgOut(ULONG port, UCHAR p_card);
      void  phaseMsgIn(ULONG port, UCHAR p_card);
      void  phaseIllegal(ULONG port, UCHAR p_card);
   #endif
#endif

#if defined(DOS)
void  phaseDecode(USHORT port, UCHAR p_card);
void  phaseChkFifo(USHORT port, UCHAR p_card);
void  phaseBusFree(USHORT p_port, UCHAR p_card);
#else
void  phaseDecode(ULONG port, UCHAR p_card);
void  phaseChkFifo(ULONG port, UCHAR p_card);
void  phaseBusFree(ULONG p_port, UCHAR p_card);
#endif




#if defined(DOS)
void  XbowInit(USHORT port, UCHAR scamFlg);
void  BusMasterInit(USHORT p_port);
int   DiagXbow(USHORT port);
int   DiagBusMaster(USHORT port);
void  DiagEEPROM(USHORT p_port);
#else
void  XbowInit(ULONG port, UCHAR scamFlg);
void  BusMasterInit(ULONG p_port);
int   DiagXbow(ULONG port);
int   DiagBusMaster(ULONG port);
void  DiagEEPROM(ULONG p_port);
#endif




#if defined(DOS)
void  busMstrAbort(USHORT port);
UCHAR busMstrTimeOut(USHORT port);
void  dataXferProcessor(USHORT port, PSCCBcard pCurrCard);
void  busMstrSGDataXferStart(USHORT port, PSCCB pCurrSCCB);
void  busMstrDataXferStart(USHORT port, PSCCB pCurrSCCB);
void  hostDataXferAbort(USHORT port, UCHAR p_card, PSCCB pCurrSCCB);
#else
void  busMstrAbort(ULONG port);
UCHAR busMstrTimeOut(ULONG port);
void  dataXferProcessor(ULONG port, PSCCBcard pCurrCard);
void  busMstrSGDataXferStart(ULONG port, PSCCB pCurrSCCB);
void  busMstrDataXferStart(ULONG port, PSCCB pCurrSCCB);
void  hostDataXferAbort(ULONG port, UCHAR p_card, PSCCB pCurrSCCB);
#endif
void  hostDataXferRestart(PSCCB currSCCB);


#if defined (DOS)
UCHAR SccbMgr_bad_isr(USHORT p_port, UCHAR p_card, PSCCBcard pCurrCard, USHORT p_int);
#else
UCHAR SccbMgr_bad_isr(ULONG p_port, UCHAR p_card, PSCCBcard pCurrCard, USHORT p_int);

#endif

void  SccbMgrTableInitAll(void);
void  SccbMgrTableInitCard(PSCCBcard pCurrCard, UCHAR p_card);
void  SccbMgrTableInitTarget(UCHAR p_card, UCHAR target);



void  scini(UCHAR p_card, UCHAR p_our_id, UCHAR p_power_up);

#if defined(DOS)
int   scarb(USHORT p_port, UCHAR p_sel_type);
void  scbusf(USHORT p_port);
void  scsel(USHORT p_port);
void  scasid(UCHAR p_card, USHORT p_port);
UCHAR scxferc(USHORT p_port, UCHAR p_data);
UCHAR scsendi(USHORT p_port, UCHAR p_id_string[]);
UCHAR sciso(USHORT p_port, UCHAR p_id_string[]);
void  scwirod(USHORT p_port, UCHAR p_data_bit);
void  scwiros(USHORT p_port, UCHAR p_data_bit);
UCHAR scvalq(UCHAR p_quintet);
UCHAR scsell(USHORT p_port, UCHAR targ_id);
void  scwtsel(USHORT p_port);
void  inisci(UCHAR p_card, USHORT p_port, UCHAR p_our_id);
void  scsavdi(UCHAR p_card, USHORT p_port);
#else
int   scarb(ULONG p_port, UCHAR p_sel_type);
void  scbusf(ULONG p_port);
void  scsel(ULONG p_port);
void  scasid(UCHAR p_card, ULONG p_port);
UCHAR scxferc(ULONG p_port, UCHAR p_data);
UCHAR scsendi(ULONG p_port, UCHAR p_id_string[]);
UCHAR sciso(ULONG p_port, UCHAR p_id_string[]);
void  scwirod(ULONG p_port, UCHAR p_data_bit);
void  scwiros(ULONG p_port, UCHAR p_data_bit);
UCHAR scvalq(UCHAR p_quintet);
UCHAR scsell(ULONG p_port, UCHAR targ_id);
void  scwtsel(ULONG p_port);
void  inisci(UCHAR p_card, ULONG p_port, UCHAR p_our_id);
void  scsavdi(UCHAR p_card, ULONG p_port);
#endif
UCHAR scmachid(UCHAR p_card, UCHAR p_id_string[]);


#if defined(DOS)
void  autoCmdCmplt(USHORT p_port, UCHAR p_card);
void  autoLoadDefaultMap(USHORT p_port);
#else
void  autoCmdCmplt(ULONG p_port, UCHAR p_card);
void  autoLoadDefaultMap(ULONG p_port);
#endif



#if (FW_TYPE==_SCCB_MGR_)
	void  OS_start_timer(unsigned long ioport, unsigned long timeout);
	void  OS_stop_timer(unsigned long ioport, unsigned long timeout);
	void  OS_disable_int(unsigned char intvec);
	void  OS_enable_int(unsigned char intvec);
	void  OS_delay(unsigned long count);
	int   OS_VirtToPhys(u32bits CardHandle, u32bits *physaddr, u32bits *virtaddr);
	#if !(defined(UNIX) || defined(OS2) || defined(SOLARIS_REAL_MODE)) 
	void  OS_Lock(PSCCBMGR_INFO pCardInfo);
	void  OS_UnLock(PSCCBMGR_INFO pCardInfo);
#endif // if FW_TYPE == ...

#endif

extern SCCBCARD BL_Card[MAX_CARDS];
extern SCCBMGR_TAR_INFO sccbMgrTbl[MAX_CARDS][MAX_SCSI_TAR];


#if defined(OS2)
   extern void (far *s_PhaseTbl[8]) (ULONG, UCHAR);
#else
   #if defined(DOS)
      extern void (*s_PhaseTbl[8]) (USHORT, UCHAR);
   #else
      extern void (*s_PhaseTbl[8]) (ULONG, UCHAR);
   #endif
#endif

extern SCCBSCAM_INFO scamInfo[MAX_SCSI_TAR];
extern NVRAMINFO nvRamInfo[MAX_MB_CARDS];
#if defined(DOS) || defined(OS2)
extern UCHAR temp_id_string[ID_STRING_LENGTH];
#endif
extern UCHAR scamHAString[];


extern UCHAR mbCards;
#if defined(BUGBUG)
extern UCHAR debug_int[MAX_CARDS][debug_size];
extern UCHAR debug_index[MAX_CARDS];
void Debug_Load(UCHAR p_card, UCHAR p_bug_data);
#endif

#if (FW_TYPE==_SCCB_MGR_)
#if defined(DOS)
   extern UCHAR first_time;
#endif
#endif /* (FW_TYPE==_SCCB_MGR_) */

#if (FW_TYPE==_UCB_MGR_)
#if defined(DOS)
   extern u08bits first_time;
#endif
#endif /* (FW_TYPE==_UCB_MGR_) */

#if defined(BUGBUG)
void Debug_Load(UCHAR p_card, UCHAR p_bug_data);
#endif

extern unsigned int SccbGlobalFlags;


#ident "$Id: sccb.c 1.18 1997/06/10 16:47:04 mohan Exp $"
/*----------------------------------------------------------------------
 *
 *
 *   Copyright 1995-1996 by Mylex Corporation.  All Rights Reserved
 *
 *   This file is available under both the GNU General Public License
 *   and a BSD-style copyright; see LICENSE.FlashPoint for details.
 *
 *   $Workfile:   sccb.c  $
 *
 *   Description:  Functions relating to handling of the SCCB interface
 *                 between the device driver and the HARPOON.
 *
 *   $Date: 1997/06/10 16:47:04 $
 *
 *   $Revision: 1.18 $
 *
 *----------------------------------------------------------------------*/

/*#include <globals.h>*/

#if (FW_TYPE==_UCB_MGR_)
	/*#include <budi.h>*/
	/*#include <budioctl.h>*/
#endif

/*#include <sccbmgr.h>*/
/*#include <blx30.h>*/
/*#include <target.h>*/
/*#include <eeprom.h>*/
/*#include <scsi2.h>*/
/*#include <harpoon.h>*/



#if (FW_TYPE==_SCCB_MGR_)
#define mOS_Lock(card)    OS_Lock((PSCCBMGR_INFO)(((PSCCBcard)card)->cardInfo))
#define mOS_UnLock(card)  OS_UnLock((PSCCBMGR_INFO)(((PSCCBcard)card)->cardInfo))
#else /* FW_TYPE==_UCB_MGR_ */
#define mOS_Lock(card)    OS_Lock((u32bits)(((PSCCBcard)card)->ioPort))
#define mOS_UnLock(card)  OS_UnLock((u32bits)(((PSCCBcard)card)->ioPort))
#endif


/*
extern SCCBMGR_TAR_INFO sccbMgrTbl[MAX_CARDS][MAX_SCSI_TAR];
extern SCCBCARD BL_Card[MAX_CARDS];

extern NVRAMINFO nvRamInfo[MAX_MB_CARDS];
extern UCHAR mbCards;

#if defined (OS2)
   extern void (far *s_PhaseTbl[8]) (ULONG, UCHAR);
#else
   #if defined(DOS)
      extern void (*s_PhaseTbl[8]) (USHORT, UCHAR);
   #else
      extern void (*s_PhaseTbl[8]) (ULONG, UCHAR);
   #endif
#endif


#if defined(BUGBUG)
extern UCHAR debug_int[MAX_CARDS][debug_size];
extern UCHAR debug_index[MAX_CARDS];
void Debug_Load(UCHAR p_card, UCHAR p_bug_data);
#endif
*/

#if (FW_TYPE==_SCCB_MGR_)

/*---------------------------------------------------------------------
 *
 * Function: SccbMgr_sense_adapter
 *
 * Description: Setup and/or Search for cards and return info to caller.
 *
 *---------------------------------------------------------------------*/

int SccbMgr_sense_adapter(PSCCBMGR_INFO pCardInfo)
{
#if defined(DOS)
#else
   static UCHAR first_time = 1;
#endif

   UCHAR i,j,id,ScamFlg;
   USHORT temp,temp2,temp3,temp4,temp5,temp6;
#if defined(DOS)
   USHORT ioport;
#else
   ULONG ioport;
#endif
	PNVRamInfo pCurrNvRam;

#if defined(DOS)
   ioport = (USHORT)pCardInfo->si_baseaddr;
#else
   ioport = pCardInfo->si_baseaddr;
#endif


   if (RD_HARPOON(ioport+hp_vendor_id_0) != ORION_VEND_0)
      return((int)FAILURE);

   if ((RD_HARPOON(ioport+hp_vendor_id_1) != ORION_VEND_1))
      return((int)FAILURE);

   if ((RD_HARPOON(ioport+hp_device_id_0) != ORION_DEV_0))
      return((int)FAILURE);

   if ((RD_HARPOON(ioport+hp_device_id_1) != ORION_DEV_1))
      return((int)FAILURE);


   if (RD_HARPOON(ioport+hp_rev_num) != 0x0f){

/* For new Harpoon then check for sub_device ID LSB
   the bits(0-3) must be all ZERO for compatible with
   current version of SCCBMgr, else skip this Harpoon
	device. */

	   if (RD_HARPOON(ioport+hp_sub_device_id_0) & 0x0f)
	      return((int)FAILURE);
	}

   if (first_time)
      {
      SccbMgrTableInitAll();
      first_time = 0;
		mbCards = 0;
      }

	if(RdStack(ioport, 0) != 0x00) {
		if(ChkIfChipInitialized(ioport) == FALSE)
		{
			pCurrNvRam = NULL;
		   WR_HARPOON(ioport+hp_semaphore, 0x00);
			XbowInit(ioport, 0);             /*Must Init the SCSI before attempting */
			DiagEEPROM(ioport);
		}
		else
		{
			if(mbCards < MAX_MB_CARDS) {
				pCurrNvRam = &nvRamInfo[mbCards];
				mbCards++;
				pCurrNvRam->niBaseAddr = ioport;
				RNVRamData(pCurrNvRam);
			}else
				return((int) FAILURE);
		}
	}else
		pCurrNvRam = NULL;
#if defined (NO_BIOS_OPTION)
	pCurrNvRam = NULL;
   XbowInit(ioport, 0);                /*Must Init the SCSI before attempting */
   DiagEEPROM(ioport);
#endif  /* No BIOS Option */

   WR_HARPOON(ioport+hp_clkctrl_0, CLKCTRL_DEFAULT);
   WR_HARPOON(ioport+hp_sys_ctrl, 0x00);

	if(pCurrNvRam)
		pCardInfo->si_id = pCurrNvRam->niAdapId;
	else
	   pCardInfo->si_id = (UCHAR)(utilEERead(ioport, (ADAPTER_SCSI_ID/2)) &
   	   (UCHAR)0x0FF);

   pCardInfo->si_lun = 0x00;
   pCardInfo->si_fw_revision = ORION_FW_REV;
   temp2 = 0x0000;
   temp3 = 0x0000;
   temp4 = 0x0000;
   temp5 = 0x0000;
   temp6 = 0x0000;

   for (id = 0; id < (16/2); id++) {

		if(pCurrNvRam){
			temp = (USHORT) pCurrNvRam->niSyncTbl[id];
			temp = ((temp & 0x03) + ((temp << 4) & 0xc0)) +
					 (((temp << 4) & 0x0300) + ((temp << 8) & 0xc000));
		}else
	      temp = utilEERead(ioport, (USHORT)((SYNC_RATE_TBL/2)+id));

      for (i = 0; i < 2; temp >>=8,i++) {

         temp2 >>= 1;
         temp3 >>= 1;
         temp4 >>= 1;
         temp5 >>= 1;
         temp6 >>= 1;
	 switch (temp & 0x3)
	   {
	   case AUTO_RATE_20:	/* Synchronous, 20 mega-transfers/second */
	     temp6 |= 0x8000;	/* Fall through */
	   case AUTO_RATE_10:	/* Synchronous, 10 mega-transfers/second */
	     temp5 |= 0x8000;	/* Fall through */
	   case AUTO_RATE_05:	/* Synchronous, 5 mega-transfers/second */
	     temp2 |= 0x8000;	/* Fall through */
	   case AUTO_RATE_00:	/* Asynchronous */
	     break;
	   }

         if (temp & DISC_ENABLE_BIT)
	   temp3 |= 0x8000;

         if (temp & WIDE_NEGO_BIT)
	   temp4 |= 0x8000;

         }
      }

   pCardInfo->si_per_targ_init_sync = temp2;
   pCardInfo->si_per_targ_no_disc = temp3;
   pCardInfo->si_per_targ_wide_nego = temp4;
   pCardInfo->si_per_targ_fast_nego = temp5;
   pCardInfo->si_per_targ_ultra_nego = temp6;

	if(pCurrNvRam)
		i = pCurrNvRam->niSysConf;
	else
	   i = (UCHAR)(utilEERead(ioport, (SYSTEM_CONFIG/2)));

	if(pCurrNvRam)
		ScamFlg = pCurrNvRam->niScamConf;
	else
	   ScamFlg = (UCHAR) utilEERead(ioport, SCAM_CONFIG/2);

   pCardInfo->si_flags = 0x0000;

   if (i & 0x01)
      pCardInfo->si_flags |= SCSI_PARITY_ENA;

   if (!(i & 0x02))
      pCardInfo->si_flags |= SOFT_RESET;

   if (i & 0x10)
      pCardInfo->si_flags |= EXTENDED_TRANSLATION;

   if (ScamFlg & SCAM_ENABLED)
     pCardInfo->si_flags |= FLAG_SCAM_ENABLED;

   if (ScamFlg & SCAM_LEVEL2)
     pCardInfo->si_flags |= FLAG_SCAM_LEVEL2;

   j = (RD_HARPOON(ioport+hp_bm_ctrl) & ~SCSI_TERM_ENA_L);
   if (i & 0x04) {
      j |= SCSI_TERM_ENA_L;
      }
   WR_HARPOON(ioport+hp_bm_ctrl, j );

   j = (RD_HARPOON(ioport+hp_ee_ctrl) & ~SCSI_TERM_ENA_H);
   if (i & 0x08) {
      j |= SCSI_TERM_ENA_H;
      }
   WR_HARPOON(ioport+hp_ee_ctrl, j );

   if (!(RD_HARPOON(ioport+hp_page_ctrl) & NARROW_SCSI_CARD))

      pCardInfo->si_flags |= SUPPORT_16TAR_32LUN;

   pCardInfo->si_card_family = HARPOON_FAMILY;
   pCardInfo->si_bustype = BUSTYPE_PCI;

	if(pCurrNvRam){
   	pCardInfo->si_card_model[0] = '9';
		switch(pCurrNvRam->niModel & 0x0f){
			case MODEL_LT:
		   	pCardInfo->si_card_model[1] = '3';
		   	pCardInfo->si_card_model[2] = '0';
				break;
			case MODEL_LW:
		   	pCardInfo->si_card_model[1] = '5';
		   	pCardInfo->si_card_model[2] = '0';
				break;
			case MODEL_DL:
		   	pCardInfo->si_card_model[1] = '3';
		   	pCardInfo->si_card_model[2] = '2';
				break;
			case MODEL_DW:
		   	pCardInfo->si_card_model[1] = '5';
		   	pCardInfo->si_card_model[2] = '2';
				break;
		}
	}else{
	   temp = utilEERead(ioport, (MODEL_NUMB_0/2));
   	pCardInfo->si_card_model[0] = (UCHAR)(temp >> 8);
	   temp = utilEERead(ioport, (MODEL_NUMB_2/2));

   	pCardInfo->si_card_model[1] = (UCHAR)(temp & 0x00FF);
	   pCardInfo->si_card_model[2] = (UCHAR)(temp >> 8);
	}

   if (pCardInfo->si_card_model[1] == '3')
     {
       if (RD_HARPOON(ioport+hp_ee_ctrl) & BIT(7))
	 pCardInfo->si_flags |= LOW_BYTE_TERM;
     }
   else if (pCardInfo->si_card_model[2] == '0')
     {
       temp = RD_HARPOON(ioport+hp_xfer_pad);
       WR_HARPOON(ioport+hp_xfer_pad, (temp & ~BIT(4)));
       if (RD_HARPOON(ioport+hp_ee_ctrl) & BIT(7))
	 pCardInfo->si_flags |= LOW_BYTE_TERM;
       WR_HARPOON(ioport+hp_xfer_pad, (temp | BIT(4)));
       if (RD_HARPOON(ioport+hp_ee_ctrl) & BIT(7))
	 pCardInfo->si_flags |= HIGH_BYTE_TERM;
       WR_HARPOON(ioport+hp_xfer_pad, temp);
     }
   else
     {
       temp = RD_HARPOON(ioport+hp_ee_ctrl);
       temp2 = RD_HARPOON(ioport+hp_xfer_pad);
       WR_HARPOON(ioport+hp_ee_ctrl, (temp | SEE_CS));
       WR_HARPOON(ioport+hp_xfer_pad, (temp2 | BIT(4)));
       temp3 = 0;
       for (i = 0; i < 8; i++)
	 {
	   temp3 <<= 1;
	   if (!(RD_HARPOON(ioport+hp_ee_ctrl) & BIT(7)))
	     temp3 |= 1;
	   WR_HARPOON(ioport+hp_xfer_pad, (temp2 & ~BIT(4)));
	   WR_HARPOON(ioport+hp_xfer_pad, (temp2 | BIT(4)));
	 }
       WR_HARPOON(ioport+hp_ee_ctrl, temp);
       WR_HARPOON(ioport+hp_xfer_pad, temp2);
       if (!(temp3 & BIT(7)))
	 pCardInfo->si_flags |= LOW_BYTE_TERM;
       if (!(temp3 & BIT(6)))
	 pCardInfo->si_flags |= HIGH_BYTE_TERM;
     }


   ARAM_ACCESS(ioport);

   for ( i = 0; i < 4; i++ ) {

      pCardInfo->si_XlatInfo[i] =
         RD_HARPOON(ioport+hp_aramBase+BIOS_DATA_OFFSET+i);
      }

	/* return with -1 if no sort, else return with
	   logical card number sorted by BIOS (zero-based) */

	pCardInfo->si_relative_cardnum =
	(UCHAR)(RD_HARPOON(ioport+hp_aramBase+BIOS_RELATIVE_CARD)-1);

   SGRAM_ACCESS(ioport);

   s_PhaseTbl[0] = phaseDataOut;
   s_PhaseTbl[1] = phaseDataIn;
   s_PhaseTbl[2] = phaseIllegal;
   s_PhaseTbl[3] = phaseIllegal;
   s_PhaseTbl[4] = phaseCommand;
   s_PhaseTbl[5] = phaseStatus;
   s_PhaseTbl[6] = phaseMsgOut;
   s_PhaseTbl[7] = phaseMsgIn;

   pCardInfo->si_present = 0x01;

#if defined(BUGBUG)


   for (i = 0; i < MAX_CARDS; i++) {

      for (id=0; id<debug_size; id++)
         debug_int[i][id] =  (UCHAR)0x00;
      debug_index[i] = 0;
      }

#endif

   return(0);
}


/*---------------------------------------------------------------------
 *
 * Function: SccbMgr_config_adapter
 *
 * Description: Setup adapter for normal operation (hard reset).
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
USHORT SccbMgr_config_adapter(PSCCBMGR_INFO pCardInfo)
#else
ULONG SccbMgr_config_adapter(PSCCBMGR_INFO pCardInfo)
#endif
{
   PSCCBcard CurrCard = NULL;
	PNVRamInfo pCurrNvRam;
   UCHAR i,j,thisCard, ScamFlg;
   USHORT temp,sync_bit_map,id;
#if defined(DOS)
   USHORT ioport;
#else
   ULONG ioport;
#endif

#if defined(DOS)
   ioport = (USHORT)pCardInfo->si_baseaddr;
#else
   ioport = pCardInfo->si_baseaddr;
#endif

   for(thisCard =0; thisCard <= MAX_CARDS; thisCard++) {

      if (thisCard == MAX_CARDS) {

	 return(FAILURE);
         }

      if (BL_Card[thisCard].ioPort == ioport) {

         CurrCard = &BL_Card[thisCard];
         SccbMgrTableInitCard(CurrCard,thisCard);
         break;
         }

      else if (BL_Card[thisCard].ioPort == 0x00) {

         BL_Card[thisCard].ioPort = ioport;
         CurrCard = &BL_Card[thisCard];

			if(mbCards)
				for(i = 0; i < mbCards; i++){
					if(CurrCard->ioPort == nvRamInfo[i].niBaseAddr)
						CurrCard->pNvRamInfo = &nvRamInfo[i];
				}
         SccbMgrTableInitCard(CurrCard,thisCard);
         CurrCard->cardIndex = thisCard;
         CurrCard->cardInfo = pCardInfo;

	 break;
         }
      }

	pCurrNvRam = CurrCard->pNvRamInfo;

	if(pCurrNvRam){
		ScamFlg = pCurrNvRam->niScamConf;
	}
	else{
	   ScamFlg = (UCHAR) utilEERead(ioport, SCAM_CONFIG/2);
	}


   BusMasterInit(ioport);
   XbowInit(ioport, ScamFlg);

#if defined (NO_BIOS_OPTION)


   if (DiagXbow(ioport)) return(FAILURE);
   if (DiagBusMaster(ioport)) return(FAILURE);

#endif  /* No BIOS Option */

   autoLoadDefaultMap(ioport);


   for (i = 0,id = 0x01; i != pCardInfo->si_id; i++,id <<= 1){}

   WR_HARPOON(ioport+hp_selfid_0, id);
   WR_HARPOON(ioport+hp_selfid_1, 0x00);
   WR_HARPOON(ioport+hp_arb_id, pCardInfo->si_id);
   CurrCard->ourId = pCardInfo->si_id;

   i = (UCHAR) pCardInfo->si_flags;
   if (i & SCSI_PARITY_ENA)
       WR_HARPOON(ioport+hp_portctrl_1,(HOST_MODE8 | CHK_SCSI_P));

   j = (RD_HARPOON(ioport+hp_bm_ctrl) & ~SCSI_TERM_ENA_L);
   if (i & LOW_BYTE_TERM)
      j |= SCSI_TERM_ENA_L;
   WR_HARPOON(ioport+hp_bm_ctrl, j);

   j = (RD_HARPOON(ioport+hp_ee_ctrl) & ~SCSI_TERM_ENA_H);
   if (i & HIGH_BYTE_TERM)
      j |= SCSI_TERM_ENA_H;
   WR_HARPOON(ioport+hp_ee_ctrl, j );


   if (!(pCardInfo->si_flags & SOFT_RESET)) {

      sresb(ioport,thisCard);

         scini(thisCard, pCardInfo->si_id, 0);
      }



   if (pCardInfo->si_flags & POST_ALL_UNDERRRUNS)
      CurrCard->globalFlags |= F_NO_FILTER;

	if(pCurrNvRam){
		if(pCurrNvRam->niSysConf & 0x10)
			CurrCard->globalFlags |= F_GREEN_PC;
	}
	else{
	   if (utilEERead(ioport, (SYSTEM_CONFIG/2)) & GREEN_PC_ENA)
   	   CurrCard->globalFlags |= F_GREEN_PC;
	}

	/* Set global flag to indicate Re-Negotiation to be done on all
		ckeck condition */
	if(pCurrNvRam){
		if(pCurrNvRam->niScsiConf & 0x04)
			CurrCard->globalFlags |= F_DO_RENEGO;
	}
	else{
	   if (utilEERead(ioport, (SCSI_CONFIG/2)) & RENEGO_ENA)
   	   CurrCard->globalFlags |= F_DO_RENEGO;
	}

	if(pCurrNvRam){
		if(pCurrNvRam->niScsiConf & 0x08)
			CurrCard->globalFlags |= F_CONLUN_IO;
	}
	else{
	   if (utilEERead(ioport, (SCSI_CONFIG/2)) & CONNIO_ENA)
   	   CurrCard->globalFlags |= F_CONLUN_IO;
	}


   temp = pCardInfo->si_per_targ_no_disc;

   for (i = 0,id = 1; i < MAX_SCSI_TAR; i++, id <<= 1) {

      if (temp & id)
	 sccbMgrTbl[thisCard][i].TarStatus |= TAR_ALLOW_DISC;
      }

   sync_bit_map = 0x0001;

   for (id = 0; id < (MAX_SCSI_TAR/2); id++) {

		if(pCurrNvRam){
			temp = (USHORT) pCurrNvRam->niSyncTbl[id];
			temp = ((temp & 0x03) + ((temp << 4) & 0xc0)) +
					 (((temp << 4) & 0x0300) + ((temp << 8) & 0xc000));
		}else
	      temp = utilEERead(ioport, (USHORT)((SYNC_RATE_TBL/2)+id));

      for (i = 0; i < 2; temp >>=8,i++) {

         if (pCardInfo->si_per_targ_init_sync & sync_bit_map) {

            sccbMgrTbl[thisCard][id*2+i].TarEEValue = (UCHAR)temp;
            }

         else {
	    sccbMgrTbl[thisCard][id*2+i].TarStatus |= SYNC_SUPPORTED;
            sccbMgrTbl[thisCard][id*2+i].TarEEValue =
               (UCHAR)(temp & ~EE_SYNC_MASK);
            }

#if defined(WIDE_SCSI)
/*         if ((pCardInfo->si_per_targ_wide_nego & sync_bit_map) ||
            (id*2+i >= 8)){
*/
         if (pCardInfo->si_per_targ_wide_nego & sync_bit_map){

            sccbMgrTbl[thisCard][id*2+i].TarEEValue |= EE_WIDE_SCSI;

            }

         else { /* NARROW SCSI */
            sccbMgrTbl[thisCard][id*2+i].TarStatus |= WIDE_NEGOCIATED;
            }

#else
         sccbMgrTbl[thisCard][id*2+i].TarStatus |= WIDE_NEGOCIATED;
#endif


	 sync_bit_map <<= 1;



         }
      }

   WR_HARPOON((ioport+hp_semaphore),
      (UCHAR)(RD_HARPOON((ioport+hp_semaphore)) | SCCB_MGR_PRESENT));

#if defined(DOS)
   return((USHORT)CurrCard);
#else
   return((ULONG)CurrCard);
#endif
}

#else  			/* end (FW_TYPE==_SCCB_MGR_)  */



STATIC s16bits FP_PresenceCheck(PMGR_INFO pMgrInfo)
{
	PMGR_ENTRYPNTS	pMgr_EntryPnts = &pMgrInfo->mi_Functions;

      pMgr_EntryPnts->UCBMgr_probe_adapter = probe_adapter;
      pMgr_EntryPnts->UCBMgr_init_adapter = init_adapter;
      pMgr_EntryPnts->UCBMgr_start_UCB = SccbMgr_start_sccb;
      pMgr_EntryPnts->UCBMgr_build_UCB = build_UCB;
      pMgr_EntryPnts->UCBMgr_abort_UCB = SccbMgr_abort_sccb;
      pMgr_EntryPnts->UCBMgr_my_int = SccbMgr_my_int;
      pMgr_EntryPnts->UCBMgr_isr = SccbMgr_isr;
      pMgr_EntryPnts->UCBMgr_scsi_reset = SccbMgr_scsi_reset;
      pMgr_EntryPnts->UCBMgr_timer_expired = SccbMgr_timer_expired;
#ifndef NO_IOCTLS
	  pMgr_EntryPnts->UCBMgr_unload_card = SccbMgr_unload_card;
	  pMgr_EntryPnts->UCBMgr_save_foreign_state =
	  									SccbMgr_save_foreign_state;
	  pMgr_EntryPnts->UCBMgr_restore_foreign_state =
	  									SccbMgr_restore_foreign_state;
	  pMgr_EntryPnts->UCBMgr_restore_native_state =
	  									SccbMgr_restore_native_state;
#endif /*NO_IOCTLS*/

      pMgrInfo->mi_SGListFormat=0x01;
      pMgrInfo->mi_DataPtrFormat=0x01;
      pMgrInfo->mi_MaxSGElements= (u16bits) 0xffffffff;
      pMgrInfo->mi_MgrPrivateLen=sizeof(SCCB);
      pMgrInfo->mi_PCIVendorID=BL_VENDOR_ID;
      pMgrInfo->mi_PCIDeviceID=FP_DEVICE_ID;
      pMgrInfo->mi_MgrAttributes= ATTR_IO_MAPPED +
											 ATTR_PHYSICAL_ADDRESS +
											 ATTR_VIRTUAL_ADDRESS +
											 ATTR_OVERLAPPED_IO_IOCTLS_OK;
      pMgrInfo->mi_IoRangeLen = 256;
      return(0);
}



/*---------------------------------------------------------------------
 *
 * Function: probe_adapter
 *
 * Description: Setup and/or Search for cards and return info to caller.
 *
 *---------------------------------------------------------------------*/
STATIC s32bits probe_adapter(PADAPTER_INFO pAdapterInfo)
{
   u16bits temp,temp2,temp3,temp4;
   u08bits i,j,id;

#if defined(DOS)
#else
   static u08bits first_time = 1;
#endif
   BASE_PORT ioport;
	PNVRamInfo pCurrNvRam;

   ioport = (BASE_PORT)pAdapterInfo->ai_baseaddr;



   if (RD_HARPOON(ioport+hp_vendor_id_0) != ORION_VEND_0)
      return(1);

   if ((RD_HARPOON(ioport+hp_vendor_id_1) != ORION_VEND_1))
      return(2);

   if ((RD_HARPOON(ioport+hp_device_id_0) != ORION_DEV_0))
      return(3);

   if ((RD_HARPOON(ioport+hp_device_id_1) != ORION_DEV_1))
      return(4);


   if (RD_HARPOON(ioport+hp_rev_num) != 0x0f){


/* For new Harpoon then check for sub_device ID LSB
   the bits(0-3) must be all ZERO for compatible with
   current version of SCCBMgr, else skip this Harpoon
	device. */

	   if (RD_HARPOON(ioport+hp_sub_device_id_0) & 0x0f)
	      return(5);
	}

   if (first_time) {

      SccbMgrTableInitAll();
      first_time = 0;
		mbCards = 0;
      }

	if(RdStack(ioport, 0) != 0x00) {
		if(ChkIfChipInitialized(ioport) == FALSE)
		{
			pCurrNvRam = NULL;
		   WR_HARPOON(ioport+hp_semaphore, 0x00);
			XbowInit(ioport, 0);                /*Must Init the SCSI before attempting */
			DiagEEPROM(ioport);
		}
		else
		{
			if(mbCards < MAX_MB_CARDS) {
				pCurrNvRam = &nvRamInfo[mbCards];
				mbCards++;
				pCurrNvRam->niBaseAddr = ioport;
				RNVRamData(pCurrNvRam);
			}else
				return((int) FAILURE);
		}
	}else
		pCurrNvRam = NULL;

#if defined (NO_BIOS_OPTION)
	pCurrNvRam = NULL;
   XbowInit(ioport, 0);                /*Must Init the SCSI before attempting */
   DiagEEPROM(ioport);
#endif  /* No BIOS Option */

   WR_HARPOON(ioport+hp_clkctrl_0, CLKCTRL_DEFAULT);
   WR_HARPOON(ioport+hp_sys_ctrl, 0x00);

	if(pCurrNvRam)
		pAdapterInfo->ai_id = pCurrNvRam->niAdapId;
	else
   	pAdapterInfo->ai_id = (u08bits)(utilEERead(ioport, (ADAPTER_SCSI_ID/2)) &
      	(u08bits)0x0FF);

   pAdapterInfo->ai_lun = 0x00;
   pAdapterInfo->ai_fw_revision[0] = '3';
   pAdapterInfo->ai_fw_revision[1] = '1';
   pAdapterInfo->ai_fw_revision[2] = '1';
   pAdapterInfo->ai_fw_revision[3] = ' ';
   pAdapterInfo->ai_NumChannels = 1;

   temp2 = 0x0000;
   temp3 = 0x0000;
   temp4 = 0x0000;

   for (id = 0; id < (16/2); id++) {

		if(pCurrNvRam){
			temp = (USHORT) pCurrNvRam->niSyncTbl[id];
			temp = ((temp & 0x03) + ((temp << 4) & 0xc0)) +
					 (((temp << 4) & 0x0300) + ((temp << 8) & 0xc000));
		}else
	      temp = utilEERead(ioport, (u16bits)((SYNC_RATE_TBL/2)+id));

      for (i = 0; i < 2; temp >>=8,i++) {

         if ((temp & 0x03) != AUTO_RATE_00) {

            temp2 >>= 0x01;
            temp2 |= 0x8000;
            }

         else {
            temp2 >>= 0x01;
            }

         if (temp & DISC_ENABLE_BIT) {

            temp3 >>= 0x01;
            temp3 |= 0x8000;
            }

         else {
            temp3 >>= 0x01;
            }

         if (temp & WIDE_NEGO_BIT) {

            temp4 >>= 0x01;
            temp4 |= 0x8000;
            }

         else {
            temp4 >>= 0x01;
            }

         }
      }

   pAdapterInfo->ai_per_targ_init_sync = temp2;
   pAdapterInfo->ai_per_targ_no_disc = temp3;
   pAdapterInfo->ai_per_targ_wide_nego = temp4;
	if(pCurrNvRam)
		i = pCurrNvRam->niSysConf;
	else
   	i = (u08bits)(utilEERead(ioport, (SYSTEM_CONFIG/2)));

   /*
   ** interrupts always level-triggered for FlashPoint
   */
   pAdapterInfo->ai_stateinfo |= LEVEL_TRIG;

   if (i & 0x01)
      pAdapterInfo->ai_stateinfo |= SCSI_PARITY_ENA;

	if (i & 0x02)	/* SCSI Bus reset in AutoSCSI Set ? */
	{
		if(pCurrNvRam)
		{
			j = pCurrNvRam->niScamConf;
		}
		else
		{
		j = (u08bits) utilEERead(ioport, SCAM_CONFIG/2);
		}
		if(j & SCAM_ENABLED)
		{
			if(j & SCAM_LEVEL2)
			{
				pAdapterInfo->ai_stateinfo |= SCAM2_ENA;
			}
			else
			{
				pAdapterInfo->ai_stateinfo |= SCAM1_ENA;
			}
		}
	}
   j = (RD_HARPOON(ioport+hp_bm_ctrl) & ~SCSI_TERM_ENA_L);
   if (i & 0x04) {
      j |= SCSI_TERM_ENA_L;
      pAdapterInfo->ai_stateinfo |= LOW_BYTE_TERM_ENA;
      }
   WR_HARPOON(ioport+hp_bm_ctrl, j );

   j = (RD_HARPOON(ioport+hp_ee_ctrl) & ~SCSI_TERM_ENA_H);
   if (i & 0x08) {
      j |= SCSI_TERM_ENA_H;
      pAdapterInfo->ai_stateinfo |= HIGH_BYTE_TERM_ENA;
      }
   WR_HARPOON(ioport+hp_ee_ctrl, j );

	if(RD_HARPOON(ioport + hp_page_ctrl) & BIOS_SHADOW)
	{
		pAdapterInfo->ai_FlashRomSize = 64 * 1024;	/* 64k ROM */
	}
	else
	{
		pAdapterInfo->ai_FlashRomSize = 32 * 1024;	/* 32k ROM */
	}

   pAdapterInfo->ai_stateinfo |= (FAST20_ENA | TAG_QUEUE_ENA);
   if (!(RD_HARPOON(ioport+hp_page_ctrl) & NARROW_SCSI_CARD))
	{
      pAdapterInfo->ai_attributes |= (WIDE_CAPABLE | FAST20_CAPABLE
													| SCAM2_CAPABLE
													| TAG_QUEUE_CAPABLE
													| SUPRESS_UNDERRRUNS_CAPABLE
													| SCSI_PARITY_CAPABLE);
		pAdapterInfo->ai_MaxTarg = 16;
		pAdapterInfo->ai_MaxLun  = 32;
	}
	else
	{
      pAdapterInfo->ai_attributes |= (FAST20_CAPABLE | SCAM2_CAPABLE
													| TAG_QUEUE_CAPABLE
													| SUPRESS_UNDERRRUNS_CAPABLE
													| SCSI_PARITY_CAPABLE);
		pAdapterInfo->ai_MaxTarg = 8;
		pAdapterInfo->ai_MaxLun  = 8;
	}

   pAdapterInfo->ai_product_family = HARPOON_FAMILY;
   pAdapterInfo->ai_HBAbustype = BUSTYPE_PCI;

   for (i=0;i<CARD_MODEL_NAMELEN;i++)
   {
      pAdapterInfo->ai_card_model[i]=' '; /* initialize the ai_card_model */
   }

	if(pCurrNvRam){
	pAdapterInfo->ai_card_model[0] = '9';
		switch(pCurrNvRam->niModel & 0x0f){
			case MODEL_LT:
			pAdapterInfo->ai_card_model[1] = '3';
			pAdapterInfo->ai_card_model[2] = '0';
				break;
			case MODEL_LW:
			pAdapterInfo->ai_card_model[1] = '5';
			pAdapterInfo->ai_card_model[2] = '0';
				break;
			case MODEL_DL:
			pAdapterInfo->ai_card_model[1] = '3';
			pAdapterInfo->ai_card_model[2] = '2';
				break;
			case MODEL_DW:
			pAdapterInfo->ai_card_model[1] = '5';
			pAdapterInfo->ai_card_model[2] = '2';
				break;
		}
	}else{
	   temp = utilEERead(ioport, (MODEL_NUMB_0/2));
		pAdapterInfo->ai_card_model[0] = (u08bits)(temp >> 8);
	   temp = utilEERead(ioport, (MODEL_NUMB_2/2));

		pAdapterInfo->ai_card_model[1] = (u08bits)(temp & 0x00FF);
	   pAdapterInfo->ai_card_model[2] = (u08bits)(temp >> 8);
	}



   pAdapterInfo->ai_FiberProductType = 0;

   pAdapterInfo->ai_secondary_range = 0;

   for (i=0;i<WORLD_WIDE_NAMELEN;i++)
   {
      pAdapterInfo->ai_worldwidename[i]='\0';
   }

   for (i=0;i<VENDOR_NAMELEN;i++)
   {
      pAdapterInfo->ai_vendorstring[i]='\0';
   }
   	pAdapterInfo->ai_vendorstring[0]='B';
   	pAdapterInfo->ai_vendorstring[1]='U';
   	pAdapterInfo->ai_vendorstring[2]='S';
   	pAdapterInfo->ai_vendorstring[3]='L';
   	pAdapterInfo->ai_vendorstring[4]='O';
   	pAdapterInfo->ai_vendorstring[5]='G';
   	pAdapterInfo->ai_vendorstring[6]='I';
   	pAdapterInfo->ai_vendorstring[7]='C';

	for (i=0;i<FAMILY_NAMELEN;i++)
	{
	   pAdapterInfo->ai_AdapterFamilyString[i]='\0';
	}
   	pAdapterInfo->ai_AdapterFamilyString[0]='F';
   	pAdapterInfo->ai_AdapterFamilyString[1]='L';
   	pAdapterInfo->ai_AdapterFamilyString[2]='A';
   	pAdapterInfo->ai_AdapterFamilyString[3]='S';
   	pAdapterInfo->ai_AdapterFamilyString[4]='H';
   	pAdapterInfo->ai_AdapterFamilyString[5]='P';
   	pAdapterInfo->ai_AdapterFamilyString[6]='O';
   	pAdapterInfo->ai_AdapterFamilyString[7]='I';
   	pAdapterInfo->ai_AdapterFamilyString[8]='N';
   	pAdapterInfo->ai_AdapterFamilyString[9]='T';

   ARAM_ACCESS(ioport);

   for ( i = 0; i < 4; i++ ) {

      pAdapterInfo->ai_XlatInfo[i] =
         RD_HARPOON(ioport+hp_aramBase+BIOS_DATA_OFFSET+i);
      }

	/* return with -1 if no sort, else return with
	   logical card number sorted by BIOS (zero-based) */


	pAdapterInfo->ai_relative_cardnum = 
      (u08bits)(RD_HARPOON(ioport+hp_aramBase+BIOS_RELATIVE_CARD)-1); 

   SGRAM_ACCESS(ioport);

   s_PhaseTbl[0] = phaseDataOut;
   s_PhaseTbl[1] = phaseDataIn;
   s_PhaseTbl[2] = phaseIllegal;
   s_PhaseTbl[3] = phaseIllegal;
   s_PhaseTbl[4] = phaseCommand;
   s_PhaseTbl[5] = phaseStatus;
   s_PhaseTbl[6] = phaseMsgOut;
   s_PhaseTbl[7] = phaseMsgIn;

   pAdapterInfo->ai_present = 0x01;

#if defined(BUGBUG)


   for (i = 0; i < MAX_CARDS; i++) {

      for (id=0; id<debug_size; id++)
         debug_int[i][id] =  (u08bits)0x00;
      debug_index[i] = 0;
      }

#endif

   return(0);
}





/*---------------------------------------------------------------------
 *
 * Function: init_adapter, exported to BUDI via UCBMgr_init_adapter entry
 *
 *
 * Description: Setup adapter for normal operation (hard reset).
 *
 *---------------------------------------------------------------------*/
STATIC CARD_HANDLE init_adapter(PADAPTER_INFO pCardInfo)
{
   PSCCBcard CurrCard;
	PNVRamInfo pCurrNvRam;
   u08bits i,j,thisCard, ScamFlg;
   u16bits temp,sync_bit_map,id;
   BASE_PORT ioport;

   ioport = (BASE_PORT)pCardInfo->ai_baseaddr;

   for(thisCard =0; thisCard <= MAX_CARDS; thisCard++) {

      if (thisCard == MAX_CARDS) {

         return(FAILURE);
         }

      if (BL_Card[thisCard].ioPort == ioport) {

         CurrCard = &BL_Card[thisCard];
         SccbMgrTableInitCard(CurrCard,thisCard);
         break;
         }

      else if (BL_Card[thisCard].ioPort == 0x00) {

         BL_Card[thisCard].ioPort = ioport;
         CurrCard = &BL_Card[thisCard];

			if(mbCards)
				for(i = 0; i < mbCards; i++){
					if(CurrCard->ioPort == nvRamInfo[i].niBaseAddr)
						CurrCard->pNvRamInfo = &nvRamInfo[i];
				}
         SccbMgrTableInitCard(CurrCard,thisCard);
         CurrCard->cardIndex = thisCard;
         CurrCard->cardInfo = pCardInfo;

         break;
         }
      }

	pCurrNvRam = CurrCard->pNvRamInfo;

   
	if(pCurrNvRam){
		ScamFlg = pCurrNvRam->niScamConf;
	}
	else{
	   ScamFlg = (UCHAR) utilEERead(ioport, SCAM_CONFIG/2);
	}
	

   BusMasterInit(ioport);
   XbowInit(ioport, ScamFlg);

#if defined (NO_BIOS_OPTION)


   if (DiagXbow(ioport)) return(FAILURE);
   if (DiagBusMaster(ioport)) return(FAILURE);

#endif  /* No BIOS Option */

   autoLoadDefaultMap(ioport);


   for (i = 0,id = 0x01; i != pCardInfo->ai_id; i++,id <<= 1){}

   WR_HARPOON(ioport+hp_selfid_0, id);
   WR_HARPOON(ioport+hp_selfid_1, 0x00);
   WR_HARPOON(ioport+hp_arb_id, pCardInfo->ai_id);
   CurrCard->ourId = (unsigned char) pCardInfo->ai_id;

   i = (u08bits) pCardInfo->ai_stateinfo;
   if (i & SCSI_PARITY_ENA)
       WR_HARPOON(ioport+hp_portctrl_1,(HOST_MODE8 | CHK_SCSI_P));

   j = (RD_HARPOON(ioport+hp_bm_ctrl) & ~SCSI_TERM_ENA_L);
   if (i & LOW_BYTE_TERM_ENA)
      j |= SCSI_TERM_ENA_L;
   WR_HARPOON(ioport+hp_bm_ctrl, j);

   j = (RD_HARPOON(ioport+hp_ee_ctrl) & ~SCSI_TERM_ENA_H);
   if (i & HIGH_BYTE_TERM_ENA)
      j |= SCSI_TERM_ENA_H;
   WR_HARPOON(ioport+hp_ee_ctrl, j );


   if (!(pCardInfo->ai_stateinfo & NO_RESET_IN_INIT)) {

      sresb(ioport,thisCard);

         scini(thisCard, (u08bits) pCardInfo->ai_id, 0);
      }



   if (pCardInfo->ai_stateinfo & SUPRESS_UNDERRRUNS_ENA)
      CurrCard->globalFlags |= F_NO_FILTER;

	if(pCurrNvRam){
		if(pCurrNvRam->niSysConf & 0x10)
			CurrCard->globalFlags |= F_GREEN_PC;
	}
	else{
	   if (utilEERead(ioport, (SYSTEM_CONFIG/2)) & GREEN_PC_ENA)
   	   CurrCard->globalFlags |= F_GREEN_PC;
	}

	/* Set global flag to indicate Re-Negotiation to be done on all
		ckeck condition */
	if(pCurrNvRam){
		if(pCurrNvRam->niScsiConf & 0x04)
			CurrCard->globalFlags |= F_DO_RENEGO;
	}
	else{
	   if (utilEERead(ioport, (SCSI_CONFIG/2)) & RENEGO_ENA)
   	   CurrCard->globalFlags |= F_DO_RENEGO;
	}

	if(pCurrNvRam){
		if(pCurrNvRam->niScsiConf & 0x08)
			CurrCard->globalFlags |= F_CONLUN_IO;
	}
	else{
	   if (utilEERead(ioport, (SCSI_CONFIG/2)) & CONNIO_ENA)
   	   CurrCard->globalFlags |= F_CONLUN_IO;
	}

   temp = pCardInfo->ai_per_targ_no_disc;

   for (i = 0,id = 1; i < MAX_SCSI_TAR; i++, id <<= 1) {

      if (temp & id)
         sccbMgrTbl[thisCard][i].TarStatus |= TAR_ALLOW_DISC;
      }

   sync_bit_map = 0x0001;

   for (id = 0; id < (MAX_SCSI_TAR/2); id++){

		if(pCurrNvRam){
			temp = (USHORT) pCurrNvRam->niSyncTbl[id];
			temp = ((temp & 0x03) + ((temp << 4) & 0xc0)) +
					 (((temp << 4) & 0x0300) + ((temp << 8) & 0xc000));
		}else
	      temp = utilEERead(ioport, (u16bits)((SYNC_RATE_TBL/2)+id));

      for (i = 0; i < 2; temp >>=8,i++){

         if (pCardInfo->ai_per_targ_init_sync & sync_bit_map){

            sccbMgrTbl[thisCard][id*2+i].TarEEValue = (u08bits)temp;
            }

         else {
            sccbMgrTbl[thisCard][id*2+i].TarStatus |= SYNC_SUPPORTED;
            sccbMgrTbl[thisCard][id*2+i].TarEEValue =
               (u08bits)(temp & ~EE_SYNC_MASK);
            }

#if defined(WIDE_SCSI)
/*         if ((pCardInfo->ai_per_targ_wide_nego & sync_bit_map) ||
            (id*2+i >= 8)){
*/
         if (pCardInfo->ai_per_targ_wide_nego & sync_bit_map){

            sccbMgrTbl[thisCard][id*2+i].TarEEValue |= EE_WIDE_SCSI;

            }

         else { /* NARROW SCSI */
            sccbMgrTbl[thisCard][id*2+i].TarStatus |= WIDE_NEGOCIATED;
            }

#else
         sccbMgrTbl[thisCard][id*2+i].TarStatus |= WIDE_NEGOCIATED;
#endif


         sync_bit_map <<= 1;
         }
      }


   pCardInfo->ai_SGListFormat=0x01;
   pCardInfo->ai_DataPtrFormat=0x01;
   pCardInfo->ai_AEN_mask &= SCSI_RESET_COMPLETE;

   WR_HARPOON((ioport+hp_semaphore),
      (u08bits)(RD_HARPOON((ioport+hp_semaphore)) | SCCB_MGR_PRESENT));

   return((u32bits)CurrCard);

}


/*---------------------------------------------------------------------
 *
 * Function: build_ucb, exported to BUDI via UCBMgr_build_ucb entry
 *
 * Description: prepare fw portion of ucb. do not start, resource not guaranteed
 *             so don't manipulate anything that's derived from states which
 *             may change
 *
 *---------------------------------------------------------------------*/
void build_UCB(CARD_HANDLE pCurrCard, PUCB p_ucb)
{

   u08bits thisCard;
   u08bits i,j;

   PSCCB p_sccb;


   thisCard = ((PSCCBcard) pCurrCard)->cardIndex;


   p_sccb=(PSCCB)p_ucb->UCB_MgrPrivatePtr;


   p_sccb->Sccb_ucb_ptr=p_ucb;

   switch (p_ucb->UCB_opcode & (OPC_DEVICE_RESET+OPC_XFER_SG+OPC_CHK_RESIDUAL))
   {
      case OPC_DEVICE_RESET:
         p_sccb->OperationCode=RESET_COMMAND;
         break;
      case OPC_XFER_SG:
         p_sccb->OperationCode=SCATTER_GATHER_COMMAND;
         break;
      case OPC_XFER_SG+OPC_CHK_RESIDUAL:
         p_sccb->OperationCode=RESIDUAL_SG_COMMAND;
         break;
      case OPC_CHK_RESIDUAL:

	      p_sccb->OperationCode=RESIDUAL_COMMAND;
	      break;
      default:
	      p_sccb->OperationCode=SCSI_INITIATOR_COMMAND;
	      break;
   }

   if (p_ucb->UCB_opcode & OPC_TQ_ENABLE)
   {
      p_sccb->ControlByte = (u08bits)((p_ucb->UCB_opcode & OPC_TQ_MASK)>>2) | F_USE_CMD_Q;
   }
   else
   {
      p_sccb->ControlByte = 0;
   }


   p_sccb->CdbLength = (u08bits)p_ucb->UCB_cdblen;

   if (p_ucb->UCB_opcode & OPC_NO_AUTO_SENSE)
   {
      p_sccb->RequestSenseLength = 0;
   }
   else
   {
      p_sccb->RequestSenseLength = (unsigned char) p_ucb->UCB_senselen;
   }


   if (p_ucb->UCB_opcode & OPC_XFER_SG)
   {
      p_sccb->DataPointer=p_ucb->UCB_virt_dataptr;
      p_sccb->DataLength = (((u32bits)p_ucb->UCB_NumSgElements)<<3);
   }
   else
   {
      p_sccb->DataPointer=p_ucb->UCB_phys_dataptr;
      p_sccb->DataLength=p_ucb->UCB_datalen;
   };

   p_sccb->HostStatus=0;
   p_sccb->TargetStatus=0;
   p_sccb->TargID=(unsigned char)p_ucb->UCB_targid;
   p_sccb->Lun=(unsigned char) p_ucb->UCB_lun;
   p_sccb->SccbIOPort=((PSCCBcard)pCurrCard)->ioPort;

   j=p_ucb->UCB_cdblen;
   for (i=0;i<j;i++)
   {
      p_sccb->Cdb[i] = p_ucb->UCB_cdb[i];
   }

   p_sccb->SensePointer=p_ucb->UCB_phys_senseptr;

   sinits(p_sccb,thisCard);

}
#ifndef NO_IOCTLS

/*---------------------------------------------------------------------
 *
 * Function: GetDevSyncRate
 *
 *---------------------------------------------------------------------*/
STATIC  int GetDevSyncRate(PSCCBcard pCurrCard,PUCB p_ucb)
{
	struct _SYNC_RATE_INFO * pSyncStr;
   PSCCBMgr_tar_info currTar_Info;
	BASE_PORT ioport;
	u08bits scsiID, j;

#if (FW_TYPE != _SCCB_MGR_)
	if( p_ucb->UCB_targid >= pCurrCard->cardInfo->ai_MaxTarg )
	{
		return(1);
	}
#endif

	ioport  = pCurrCard->ioPort;
	pSyncStr	= (struct _SYNC_RATE_INFO *) p_ucb->UCB_virt_dataptr;
	scsiID = (u08bits) p_ucb->UCB_targid;
   currTar_Info = &sccbMgrTbl[pCurrCard->cardIndex][scsiID];
	j = currTar_Info->TarSyncCtrl;

	switch (currTar_Info->TarEEValue & EE_SYNC_MASK)
	{
		case EE_SYNC_ASYNC:
			pSyncStr->RequestMegaXferRate = 0x00;
			break;
		case EE_SYNC_5MB:
			pSyncStr->RequestMegaXferRate = (j & NARROW_SCSI) ? 50 : 100;
			break;
		case EE_SYNC_10MB:
			pSyncStr->RequestMegaXferRate = (j & NARROW_SCSI) ? 100 : 200;
			break;
		case EE_SYNC_20MB:
			pSyncStr->RequestMegaXferRate = (j & NARROW_SCSI) ? 200 : 400;
			break;
	}

	switch ((j >> 5) & 0x07)
	{
		case 0x00:
			if((j & 0x07) == 0x00)
			{
				pSyncStr->ActualMegaXferRate = 0x00;	/* Async Mode */
			}
			else
			{
				pSyncStr->ActualMegaXferRate = (j & NARROW_SCSI) ? 200 : 400;
			}
			break;
		case 0x01:
			pSyncStr->ActualMegaXferRate = (j & NARROW_SCSI) ? 100 : 200;
			break;
		case 0x02:
			pSyncStr->ActualMegaXferRate = (j & NARROW_SCSI) ? 66 : 122;
			break;
		case 0x03:
			pSyncStr->ActualMegaXferRate = (j & NARROW_SCSI) ? 50 : 100;
			break;
		case 0x04:
			pSyncStr->ActualMegaXferRate = (j & NARROW_SCSI) ? 40 : 80;
			break;
		case 0x05:
			pSyncStr->ActualMegaXferRate = (j & NARROW_SCSI) ? 33 : 66;
			break;
		case 0x06:
			pSyncStr->ActualMegaXferRate = (j & NARROW_SCSI) ? 28 : 56;
			break;
		case 0x07:
			pSyncStr->ActualMegaXferRate = (j & NARROW_SCSI) ? 25 : 50;
			break;
	}
	pSyncStr->NegotiatedOffset = j & 0x0f;

	return(0);
}

/*---------------------------------------------------------------------
 *
 * Function: SetDevSyncRate
 *
 *---------------------------------------------------------------------*/
STATIC int SetDevSyncRate(PSCCBcard pCurrCard, PUCB p_ucb)
{
	struct _SYNC_RATE_INFO * pSyncStr;
   PSCCBMgr_tar_info currTar_Info;
	BASE_PORT ioPort;
	u08bits scsiID, i, j, syncVal;
	u16bits syncOffset, actualXferRate;
	union {
		u08bits tempb[2];
		u16bits tempw;
	}temp2;

#if (FW_TYPE != _SCCB_MGR_)
	if( p_ucb->UCB_targid >= pCurrCard->cardInfo->ai_MaxTarg )
	{
		return(1);
	}
#endif

	ioPort  = pCurrCard->ioPort;
	pSyncStr	= (struct _SYNC_RATE_INFO *) p_ucb->UCB_virt_dataptr;
	scsiID = (u08bits) p_ucb->UCB_targid;
   currTar_Info = &sccbMgrTbl[pCurrCard->cardIndex][scsiID];
	i = RD_HARPOON(ioPort+hp_xfer_pad);		/* Save current value */
	WR_HARPOON(ioPort+hp_xfer_pad, (i | ID_UNLOCK));
	WR_HARPOON(ioPort+hp_select_id, ((scsiID << 4) | scsiID));
	j = RD_HARPOON(ioPort+hp_synctarg_0);
	WR_HARPOON(ioPort+hp_xfer_pad, i);		/* restore value */

	actualXferRate = pSyncStr->ActualMegaXferRate;
	if(!(j & NARROW_SCSI))
	{
		actualXferRate <<= 1;
	}
	if(actualXferRate == 0x00)
	{
		syncVal = EE_SYNC_ASYNC;			/* Async Mode */
	}
	if(actualXferRate == 0x0200)
	{
		syncVal = EE_SYNC_20MB;				/* 20/40 MB Mode */
	}
	if(actualXferRate > 0x0050 && actualXferRate < 0x0200 )
	{
		syncVal = EE_SYNC_10MB;				/* 10/20 MB Mode */
	}
	else
	{
		syncVal = EE_SYNC_5MB;				/* 5/10 MB Mode */
	}
	if(currTar_Info->TarEEValue && EE_SYNC_MASK == syncVal)
		return(0);
	currTar_Info->TarEEValue = (currTar_Info->TarEEValue & !EE_SYNC_MASK)
											| syncVal;
	syncOffset = (SYNC_RATE_TBL + scsiID) / 2;
	temp2.tempw = utilEERead(ioPort, syncOffset);
	if(scsiID & 0x01)
	{
		temp2.tempb[0] = (temp2.tempb[0] & !EE_SYNC_MASK) | syncVal;
	}
	else
	{
		temp2.tempb[1] = (temp2.tempb[1] & !EE_SYNC_MASK) | syncVal;
	}
	utilEEWriteOnOff(ioPort, 1);
	utilEEWrite(ioPort, temp2.tempw, syncOffset);
	utilEEWriteOnOff(ioPort, 0);
	UpdateCheckSum(ioPort);

	return(0);
}
/*---------------------------------------------------------------------
 *
 * Function: GetDevWideMode
 *
 *---------------------------------------------------------------------*/
int GetDevWideMode(PSCCBcard pCurrCard,PUCB p_ucb)
{
	u08bits *pData;

	pData = (u08bits *)p_ucb->UCB_virt_dataptr;
	if(sccbMgrTbl[pCurrCard->cardIndex][p_ucb->UCB_targid].TarEEValue
				& EE_WIDE_SCSI)
	{
		*pData = 1;
	}
	else
	{
		*pData = 0;
	}

	return(0);
}

/*---------------------------------------------------------------------
 *
 * Function: SetDevWideMode
 *
 *---------------------------------------------------------------------*/
int SetDevWideMode(PSCCBcard pCurrCard,PUCB p_ucb)
{
	u08bits *pData;
   PSCCBMgr_tar_info currTar_Info;
	BASE_PORT ioPort;
	u08bits scsiID, scsiWideMode;
	u16bits syncOffset;
	union {
		u08bits tempb[2];
		u16bits tempw;
	}temp2;

#if (FW_TYPE != _SCCB_MGR_)
	if( !(pCurrCard->cardInfo->ai_attributes & WIDE_CAPABLE) )
	{
		return(1);
	}

	if( p_ucb->UCB_targid >= pCurrCard->cardInfo->ai_MaxTarg )
	{
		return(1);
	}
#endif

	ioPort  = pCurrCard->ioPort;
	pData = (u08bits *)p_ucb->UCB_virt_dataptr;
	scsiID = (u08bits) p_ucb->UCB_targid;
	currTar_Info = &sccbMgrTbl[pCurrCard->cardIndex][scsiID];

	if(*pData)
	{
		if(currTar_Info->TarEEValue & EE_WIDE_SCSI)
		{
			return(0);
		}
		else
		{
			scsiWideMode = EE_WIDE_SCSI;
		}
	}
	else
	{
		if(!(currTar_Info->TarEEValue & EE_WIDE_SCSI))
		{
			return(0);
		}
		else
		{
			scsiWideMode = 0;
		}
	}
	currTar_Info->TarEEValue = (currTar_Info->TarEEValue & !EE_WIDE_SCSI)
											| scsiWideMode;

	syncOffset = (SYNC_RATE_TBL + scsiID) / 2;
	temp2.tempw = utilEERead(ioPort, syncOffset);
	if(scsiID & 0x01)
	{
		temp2.tempb[0] = (temp2.tempb[0] & !EE_WIDE_SCSI) | scsiWideMode;
	}
	else
	{
		temp2.tempb[1] = (temp2.tempb[1] & !EE_WIDE_SCSI) | scsiWideMode;
	}
	utilEEWriteOnOff(ioPort, 1);
	utilEEWrite(ioPort, temp2.tempw, syncOffset);
	utilEEWriteOnOff(ioPort, 0);
	UpdateCheckSum(ioPort);

	return(0);
}

/*---------------------------------------------------------------------
 *
 * Function: ReadNVRam
 *
 *---------------------------------------------------------------------*/
void ReadNVRam(PSCCBcard pCurrCard,PUCB p_ucb)
{
	u08bits *pdata;
	u16bits i,numwrds,numbytes,offset,temp;
	u08bits OneMore = FALSE;
#if defined(DOS)
	u16bits ioport;
#else
	u32bits ioport;
#endif

	numbytes = (u16bits) p_ucb->UCB_datalen;
	ioport  = pCurrCard->ioPort;
   pdata   = (u08bits *) p_ucb->UCB_virt_dataptr;
	offset  = (u16bits) (p_ucb->UCB_IOCTLParams[0]);



   if (offset & 0x1)
	{
	    *((u16bits*) pdata) = utilEERead(ioport,(u16bits)((offset - 1) / 2)); /* 16 bit read */
		 *pdata = *(pdata + 1);
		 ++offset;
   	 ++pdata;
		 --numbytes;
	}

	numwrds = numbytes / 2;
	if (numbytes & 1)
	 	OneMore = TRUE;

	for (i = 0; i < numwrds; i++)
	{
   	*((u16bits*) pdata) = utilEERead(ioport,(u16bits)(offset / 2));
		pdata += 2;
		offset += 2;
   }
	if (OneMore)
	{
		--pdata;
		-- offset;
   	temp = utilEERead(ioport,(u16bits)(offset / 2));
		*pdata = (u08bits) (temp);
	}

} /* end proc ReadNVRam */


/*---------------------------------------------------------------------
 *
 * Function: WriteNVRam
 *
 *---------------------------------------------------------------------*/
void WriteNVRam(PSCCBcard pCurrCard,PUCB p_ucb)
{
	u08bits *pdata;
	u16bits i,numwrds,numbytes,offset, eeprom_end;
	u08bits OneMore = FALSE;
	union {
		u08bits  tempb[2];
		u16bits  tempw;
	} temp2;

#if defined(DOS)
	u16bits ioport;
#else
	u32bits ioport;
#endif

	numbytes = (u16bits) p_ucb->UCB_datalen;
	ioport  = pCurrCard->ioPort;
   pdata   = (u08bits *) p_ucb->UCB_virt_dataptr;
	offset  = (u16bits) (p_ucb->UCB_IOCTLParams[0]);

   if (RD_HARPOON(ioport+hp_page_ctrl) & NARROW_SCSI_CARD)
      eeprom_end = 512;
   else
      eeprom_end = 768;
	
	if(offset > eeprom_end)
		return;

	if((offset + numbytes) > eeprom_end)
		numbytes = eeprom_end - offset;

    utilEEWriteOnOff(ioport,1);   /* Enable write access to the EEPROM */



   if (offset & 0x1)
	{
	    temp2.tempw = utilEERead(ioport,(u16bits)((offset - 1) / 2)); /* 16 bit read */
		 temp2.tempb[1] = *pdata;
	    utilEEWrite(ioport, temp2.tempw, (u16bits)((offset -1) / 2));
		 *pdata = *(pdata + 1);
		 ++offset;
   	 ++pdata;
		 --numbytes;
	}

	numwrds = numbytes / 2;
	if (numbytes & 1)
	 	OneMore = TRUE;

	for (i = 0; i < numwrds; i++)
	{
   	utilEEWrite(ioport, *((pu16bits)pdata),(u16bits)(offset / 2));
		pdata += 2;
		offset += 2;
   }
	if (OneMore)
	{

   	temp2.tempw = utilEERead(ioport,(u16bits)(offset / 2));
		temp2.tempb[0] = *pdata;
   	utilEEWrite(ioport, temp2.tempw, (u16bits)(offset / 2));
	}
   utilEEWriteOnOff(ioport,0);   /* Turn off write access */
   UpdateCheckSum((u32bits)ioport);

} /* end proc WriteNVRam */



/*---------------------------------------------------------------------
 *
 * Function: UpdateCheckSum
 *
 * Description: Update Check Sum in EEPROM
 *
 *---------------------------------------------------------------------*/


void UpdateCheckSum(u32bits baseport)
{
	USHORT i,sum_data, eeprom_end;

	sum_data = 0x0000;


   if (RD_HARPOON(baseport+hp_page_ctrl) & NARROW_SCSI_CARD)
      eeprom_end = 512;
   else
      eeprom_end = 768;

	for (i = 1; i < eeprom_end/2; i++)
	{
		sum_data += utilEERead(baseport, i);
	}

   utilEEWriteOnOff(baseport,1);   /* Enable write access to the EEPROM */

   utilEEWrite(baseport, sum_data, EEPROM_CHECK_SUM/2);
   utilEEWriteOnOff(baseport,0);   /* Turn off write access */
}

void SccbMgr_save_foreign_state(PADAPTER_INFO pAdapterInfo)
{
}


void SccbMgr_restore_foreign_state(CARD_HANDLE pCurrCard)
{
}

void SccbMgr_restore_native_state(CARD_HANDLE pCurrCard)
{
}

#endif /* NO_IOCTLS */

#endif /* (FW_TYPE==_UCB_MGR_)   */

#ifndef NO_IOCTLS
#if (FW_TYPE==_UCB_MGR_)
void SccbMgr_unload_card(CARD_HANDLE pCurrCard)
#else
#if defined(DOS)
void SccbMgr_unload_card(USHORT pCurrCard)
#else
void SccbMgr_unload_card(ULONG pCurrCard)
#endif
#endif
{
	UCHAR i;
#if defined(DOS)
	USHORT portBase;
	USHORT regOffset;
#else
	ULONG portBase;
	ULONG regOffset;
#endif
	ULONG scamData;
#if defined(OS2)
	ULONG far *pScamTbl;
#else
	ULONG *pScamTbl;
#endif
	PNVRamInfo pCurrNvRam;

	pCurrNvRam = ((PSCCBcard)pCurrCard)->pNvRamInfo;

	if(pCurrNvRam){
		WrStack(pCurrNvRam->niBaseAddr, 0, pCurrNvRam->niModel);
		WrStack(pCurrNvRam->niBaseAddr, 1, pCurrNvRam->niSysConf);
		WrStack(pCurrNvRam->niBaseAddr, 2, pCurrNvRam->niScsiConf);
		WrStack(pCurrNvRam->niBaseAddr, 3, pCurrNvRam->niScamConf);
		WrStack(pCurrNvRam->niBaseAddr, 4, pCurrNvRam->niAdapId);

		for(i = 0; i < MAX_SCSI_TAR / 2; i++)
			WrStack(pCurrNvRam->niBaseAddr, (UCHAR)(i+5), pCurrNvRam->niSyncTbl[i]);

		portBase = pCurrNvRam->niBaseAddr;

		for(i = 0; i < MAX_SCSI_TAR; i++){
			regOffset = hp_aramBase + 64 + i*4;
#if defined(OS2)
			pScamTbl = (ULONG far *) &pCurrNvRam->niScamTbl[i];
#else
			pScamTbl = (ULONG *) &pCurrNvRam->niScamTbl[i];
#endif
			scamData = *pScamTbl;
			WR_HARP32(portBase, regOffset, scamData);
		}

	}else{
		WrStack(((PSCCBcard)pCurrCard)->ioPort, 0, 0);
	}
}
#endif /* NO_IOCTLS */


void RNVRamData(PNVRamInfo pNvRamInfo)
{
	UCHAR i;
#if defined(DOS)
	USHORT portBase;
	USHORT regOffset;
#else
	ULONG portBase;
	ULONG regOffset;
#endif
	ULONG scamData;
#if defined (OS2)
	ULONG far *pScamTbl;
#else
	ULONG *pScamTbl;
#endif

	pNvRamInfo->niModel    = RdStack(pNvRamInfo->niBaseAddr, 0);
	pNvRamInfo->niSysConf  = RdStack(pNvRamInfo->niBaseAddr, 1);
	pNvRamInfo->niScsiConf = RdStack(pNvRamInfo->niBaseAddr, 2);
	pNvRamInfo->niScamConf = RdStack(pNvRamInfo->niBaseAddr, 3);
	pNvRamInfo->niAdapId   = RdStack(pNvRamInfo->niBaseAddr, 4);

	for(i = 0; i < MAX_SCSI_TAR / 2; i++)
		pNvRamInfo->niSyncTbl[i] = RdStack(pNvRamInfo->niBaseAddr, (UCHAR)(i+5));

	portBase = pNvRamInfo->niBaseAddr;

	for(i = 0; i < MAX_SCSI_TAR; i++){
		regOffset = hp_aramBase + 64 + i*4;
		RD_HARP32(portBase, regOffset, scamData);
#if defined(OS2)
		pScamTbl = (ULONG far *) &pNvRamInfo->niScamTbl[i];
#else
		pScamTbl = (ULONG *) &pNvRamInfo->niScamTbl[i];
#endif
		*pScamTbl = scamData;
	}

}

#if defined(DOS)
UCHAR RdStack(USHORT portBase, UCHAR index)
#else
UCHAR RdStack(ULONG portBase, UCHAR index)
#endif
{
	WR_HARPOON(portBase + hp_stack_addr, index);
	return(RD_HARPOON(portBase + hp_stack_data));
}

#if defined(DOS)
void WrStack(USHORT portBase, UCHAR index, UCHAR data)
#else
void WrStack(ULONG portBase, UCHAR index, UCHAR data)
#endif
{
	WR_HARPOON(portBase + hp_stack_addr, index);
	WR_HARPOON(portBase + hp_stack_data, data);
}


#if (FW_TYPE==_UCB_MGR_)
u08bits ChkIfChipInitialized(BASE_PORT ioPort)
#else
#if defined(DOS)
UCHAR ChkIfChipInitialized(USHORT ioPort)
#else
UCHAR ChkIfChipInitialized(ULONG ioPort)
#endif
#endif
{
	if((RD_HARPOON(ioPort + hp_arb_id) & 0x0f) != RdStack(ioPort, 4))
		return(FALSE);
	if((RD_HARPOON(ioPort + hp_clkctrl_0) & CLKCTRL_DEFAULT)
								!= CLKCTRL_DEFAULT)
		return(FALSE);
	if((RD_HARPOON(ioPort + hp_seltimeout) == TO_250ms) ||
		(RD_HARPOON(ioPort + hp_seltimeout) == TO_290ms))
		return(TRUE);
	return(FALSE);

}
/*---------------------------------------------------------------------
 *
 * Function: SccbMgr_start_sccb
 *
 * Description: Start a command pointed to by p_Sccb. When the
 *              command is completed it will be returned via the
 *              callback function.
 *
 *---------------------------------------------------------------------*/
#if (FW_TYPE==_UCB_MGR_)
void SccbMgr_start_sccb(CARD_HANDLE pCurrCard, PUCB p_ucb)
#else
#if defined(DOS)
void SccbMgr_start_sccb(USHORT pCurrCard, PSCCB p_Sccb)
#else
void SccbMgr_start_sccb(ULONG pCurrCard, PSCCB p_Sccb)
#endif
#endif
{
#if defined(DOS)
   USHORT ioport;
#else
   ULONG ioport;
#endif
   UCHAR thisCard, lun;
	PSCCB pSaveSccb;
   CALL_BK_FN callback;

#if (FW_TYPE==_UCB_MGR_)
   PSCCB p_Sccb;
#endif

   mOS_Lock((PSCCBcard)pCurrCard);
   thisCard = ((PSCCBcard) pCurrCard)->cardIndex;
   ioport = ((PSCCBcard) pCurrCard)->ioPort;

#if (FW_TYPE==_UCB_MGR_)
   p_Sccb = (PSCCB)p_ucb->UCB_MgrPrivatePtr;
#endif

	if((p_Sccb->TargID > MAX_SCSI_TAR) || (p_Sccb->Lun > MAX_LUN))
	{

#if (FW_TYPE==_UCB_MGR_)
		p_ucb->UCB_hbastat = SCCB_COMPLETE;
		p_ucb->UCB_status=SCCB_ERROR;
		callback = (CALL_BK_FN)p_ucb->UCB_callback;
		if (callback)
			callback(p_ucb);
#endif

#if (FW_TYPE==_SCCB_MGR_)
		p_Sccb->HostStatus = SCCB_COMPLETE;
		p_Sccb->SccbStatus = SCCB_ERROR;
		callback = (CALL_BK_FN)p_Sccb->SccbCallback;
		if (callback)
			callback(p_Sccb);
#endif

		mOS_UnLock((PSCCBcard)pCurrCard);
		return;
	}

#if (FW_TYPE==_SCCB_MGR_)
   sinits(p_Sccb,thisCard);
#endif


#if (FW_TYPE==_UCB_MGR_)
#ifndef NO_IOCTLS

   if (p_ucb->UCB_opcode & OPC_IOCTL)
	{

		switch (p_ucb->UCB_IOCTLCommand) 
		{
			case READ_NVRAM:
				ReadNVRam((PSCCBcard)pCurrCard,p_ucb);
				p_ucb->UCB_status=UCB_SUCCESS;
				callback = (CALL_BK_FN)p_ucb->UCB_callback;
				if (callback)
					callback(p_ucb);
				mOS_UnLock((PSCCBcard)pCurrCard);
				return;

			case WRITE_NVRAM:
				WriteNVRam((PSCCBcard)pCurrCard,p_ucb);
				p_ucb->UCB_status=UCB_SUCCESS;
				callback = (CALL_BK_FN)p_ucb->UCB_callback;
				if (callback)
					callback(p_ucb);
				mOS_UnLock((PSCCBcard)pCurrCard);
				return;

			case SEND_SCSI_PASSTHRU:
#if (FW_TYPE != _SCCB_MGR_)
				if( p_ucb->UCB_targid >=
				    ((PSCCBcard)pCurrCard)->cardInfo->ai_MaxTarg )
				{
					p_ucb->UCB_status = UCB_ERROR;
					p_ucb->UCB_hbastat = HASTAT_HW_ERROR;
					callback = (CALL_BK_FN)p_ucb->UCB_callback;
					if (callback)
						callback(p_ucb);
					mOS_UnLock((PSCCBcard)pCurrCard);
					return;
				}
#endif
				break;

			case HARD_RESET:
				p_ucb->UCB_status = UCB_INVALID;
				callback = (CALL_BK_FN)p_ucb->UCB_callback;
				if (callback)
					callback(p_ucb);
				mOS_UnLock((PSCCBcard)pCurrCard);
				return;
			case GET_DEVICE_SYNCRATE:
				if( !GetDevSyncRate((PSCCBcard)pCurrCard,p_ucb) )
				{
					p_ucb->UCB_status = UCB_SUCCESS;
				}
				else
				{
					p_ucb->UCB_status = UCB_ERROR;
					p_ucb->UCB_hbastat = HASTAT_HW_ERROR;
				}
				callback = (CALL_BK_FN)p_ucb->UCB_callback;
				if (callback)
					callback(p_ucb);
				mOS_UnLock((PSCCBcard)pCurrCard);
				return;
			case SET_DEVICE_SYNCRATE:
				if( !SetDevSyncRate((PSCCBcard)pCurrCard,p_ucb) )
				{
					p_ucb->UCB_status = UCB_SUCCESS;
				}
				else
				{
					p_ucb->UCB_status = UCB_ERROR;
					p_ucb->UCB_hbastat = HASTAT_HW_ERROR;
				}
				callback = (CALL_BK_FN)p_ucb->UCB_callback;
				if (callback)
					callback(p_ucb);
				mOS_UnLock((PSCCBcard)pCurrCard);
				return;
			case GET_WIDE_MODE:
				if( !GetDevWideMode((PSCCBcard)pCurrCard,p_ucb) )
				{
					p_ucb->UCB_status = UCB_SUCCESS;
				}
				else
				{
					p_ucb->UCB_status = UCB_ERROR;
					p_ucb->UCB_hbastat = HASTAT_HW_ERROR;
				}
				callback = (CALL_BK_FN)p_ucb->UCB_callback;
				if (callback)
					callback(p_ucb);
				mOS_UnLock((PSCCBcard)pCurrCard);
				return;
			case SET_WIDE_MODE:
				if( !SetDevWideMode((PSCCBcard)pCurrCard,p_ucb) )
				{
					p_ucb->UCB_status = UCB_SUCCESS;
				}
				else
				{
					p_ucb->UCB_status = UCB_ERROR;
					p_ucb->UCB_hbastat = HASTAT_HW_ERROR;
				}
				callback = (CALL_BK_FN)p_ucb->UCB_callback;
				if (callback)
					callback(p_ucb);
				mOS_UnLock((PSCCBcard)pCurrCard);
				return;
			default:
				p_ucb->UCB_status=UCB_INVALID;
				callback = (CALL_BK_FN)p_ucb->UCB_callback;
				if (callback)
					callback(p_ucb);
				mOS_UnLock((PSCCBcard)pCurrCard);
				return;
		}
	}
#endif /* NO_IOCTLS */
#endif /* (FW_TYPE==_UCB_MGR_) */


   if (!((PSCCBcard) pCurrCard)->cmdCounter)
      {
      WR_HARPOON(ioport+hp_semaphore, (RD_HARPOON(ioport+hp_semaphore)
         | SCCB_MGR_ACTIVE));

      if (((PSCCBcard) pCurrCard)->globalFlags & F_GREEN_PC)
         {
		 WR_HARPOON(ioport+hp_clkctrl_0, CLKCTRL_DEFAULT);
		 WR_HARPOON(ioport+hp_sys_ctrl, 0x00);
         }
      }

   ((PSCCBcard)pCurrCard)->cmdCounter++;

   if (RD_HARPOON(ioport+hp_semaphore) & BIOS_IN_USE) {

      WR_HARPOON(ioport+hp_semaphore, (RD_HARPOON(ioport+hp_semaphore)
         | TICKLE_ME));
		if(p_Sccb->OperationCode == RESET_COMMAND)
			{
				pSaveSccb = ((PSCCBcard) pCurrCard)->currentSCCB;
				((PSCCBcard) pCurrCard)->currentSCCB = p_Sccb;
				queueSelectFail(&BL_Card[thisCard], thisCard);
				((PSCCBcard) pCurrCard)->currentSCCB = pSaveSccb;
			}
		else
			{
	      queueAddSccb(p_Sccb,thisCard);
			}
      }

   else if ((RD_HARPOON(ioport+hp_page_ctrl) & G_INT_DISABLE)) {

			if(p_Sccb->OperationCode == RESET_COMMAND)
				{
					pSaveSccb = ((PSCCBcard) pCurrCard)->currentSCCB;
					((PSCCBcard) pCurrCard)->currentSCCB = p_Sccb;
					queueSelectFail(&BL_Card[thisCard], thisCard);
					((PSCCBcard) pCurrCard)->currentSCCB = pSaveSccb;
				}
			else
				{
		      queueAddSccb(p_Sccb,thisCard);
				}
      }

   else {

      MDISABLE_INT(ioport);

		if((((PSCCBcard) pCurrCard)->globalFlags & F_CONLUN_IO) && 
			((sccbMgrTbl[thisCard][p_Sccb->TargID].TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING))
			lun = p_Sccb->Lun;
		else
			lun = 0;
      if ((((PSCCBcard) pCurrCard)->currentSCCB == NULL) &&
         (sccbMgrTbl[thisCard][p_Sccb->TargID].TarSelQ_Cnt == 0) &&
         (sccbMgrTbl[thisCard][p_Sccb->TargID].TarLUNBusy[lun]
         == FALSE)) {

            ((PSCCBcard) pCurrCard)->currentSCCB = p_Sccb;
			   mOS_UnLock((PSCCBcard)pCurrCard);
#if defined(DOS)
            ssel((USHORT)p_Sccb->SccbIOPort,thisCard);
#else
	    ssel(p_Sccb->SccbIOPort,thisCard);
#endif
			   mOS_Lock((PSCCBcard)pCurrCard);
         }

      else {

			if(p_Sccb->OperationCode == RESET_COMMAND)
				{
					pSaveSccb = ((PSCCBcard) pCurrCard)->currentSCCB;
					((PSCCBcard) pCurrCard)->currentSCCB = p_Sccb;
					queueSelectFail(&BL_Card[thisCard], thisCard);
					((PSCCBcard) pCurrCard)->currentSCCB = pSaveSccb;
				}
			else
				{
	         	queueAddSccb(p_Sccb,thisCard);
				}
         }


      MENABLE_INT(ioport);
      }

   mOS_UnLock((PSCCBcard)pCurrCard);
}


/*---------------------------------------------------------------------
 *
 * Function: SccbMgr_abort_sccb
 *
 * Description: Abort the command pointed to by p_Sccb.  When the
 *              command is completed it will be returned via the
 *              callback function.
 *
 *---------------------------------------------------------------------*/
#if (FW_TYPE==_UCB_MGR_)
s32bits SccbMgr_abort_sccb(CARD_HANDLE pCurrCard, PUCB p_ucb)
#else
#if defined(DOS)
int SccbMgr_abort_sccb(USHORT pCurrCard, PSCCB p_Sccb)
#else
int SccbMgr_abort_sccb(ULONG pCurrCard, PSCCB p_Sccb)
#endif
#endif

{
#if defined(DOS)
	USHORT ioport;
#else
	ULONG ioport;
#endif

	UCHAR thisCard;
	CALL_BK_FN callback;
	UCHAR TID;
	PSCCB pSaveSCCB;
	PSCCBMgr_tar_info currTar_Info;


#if (FW_TYPE==_UCB_MGR_)
	PSCCB    p_Sccb;
	p_Sccb=(PSCCB)p_ucb->UCB_MgrPrivatePtr;
#endif

	ioport = ((PSCCBcard) pCurrCard)->ioPort;

	thisCard = ((PSCCBcard)pCurrCard)->cardIndex;

	mOS_Lock((PSCCBcard)pCurrCard);

	if (RD_HARPOON(ioport+hp_page_ctrl) & G_INT_DISABLE)
	{
		mOS_UnLock((PSCCBcard)pCurrCard);
	}

	else
	{

		if (queueFindSccb(p_Sccb,thisCard))
		{

			mOS_UnLock((PSCCBcard)pCurrCard);

			((PSCCBcard)pCurrCard)->cmdCounter--;

			if (!((PSCCBcard)pCurrCard)->cmdCounter)
				WR_HARPOON(ioport+hp_semaphore,(RD_HARPOON(ioport+hp_semaphore)
					& (UCHAR)(~(SCCB_MGR_ACTIVE | TICKLE_ME)) ));

#if (FW_TYPE==_SCCB_MGR_)
			p_Sccb->SccbStatus = SCCB_ABORT;
			callback = p_Sccb->SccbCallback;
			callback(p_Sccb);
#else
			p_ucb->UCB_status=SCCB_ABORT;
			callback = (CALL_BK_FN)p_ucb->UCB_callback;
			callback(p_ucb);
#endif

			return(0);
		}

		else
		{
			mOS_UnLock((PSCCBcard)pCurrCard);

			if (((PSCCBcard)pCurrCard)->currentSCCB == p_Sccb)
			{
				p_Sccb->SccbStatus = SCCB_ABORT;
				return(0);

			}

			else
			{

				TID = p_Sccb->TargID;


				if(p_Sccb->Sccb_tag)
				{
					MDISABLE_INT(ioport);
					if (((PSCCBcard) pCurrCard)->discQ_Tbl[p_Sccb->Sccb_tag]==p_Sccb)
					{
						p_Sccb->SccbStatus = SCCB_ABORT;
						p_Sccb->Sccb_scsistat = ABORT_ST;
#if (FW_TYPE==_UCB_MGR_)
						p_ucb->UCB_status=SCCB_ABORT;
#endif
						p_Sccb->Sccb_scsimsg = SMABORT_TAG;

						if(((PSCCBcard) pCurrCard)->currentSCCB == NULL)
						{
							((PSCCBcard) pCurrCard)->currentSCCB = p_Sccb;
							ssel(ioport, thisCard);
						}
						else
						{
							pSaveSCCB = ((PSCCBcard) pCurrCard)->currentSCCB;
							((PSCCBcard) pCurrCard)->currentSCCB = p_Sccb;
							queueSelectFail((PSCCBcard) pCurrCard, thisCard);
							((PSCCBcard) pCurrCard)->currentSCCB = pSaveSCCB;
						}
					}
					MENABLE_INT(ioport);
					return(0);
				}
				else
				{
					currTar_Info = &sccbMgrTbl[thisCard][p_Sccb->TargID];

					if(BL_Card[thisCard].discQ_Tbl[currTar_Info->LunDiscQ_Idx[p_Sccb->Lun]] 
							== p_Sccb)
					{
						p_Sccb->SccbStatus = SCCB_ABORT;
						return(0);
					}
				}
			}
		}
	}
	return(-1);
}


/*---------------------------------------------------------------------
 *
 * Function: SccbMgr_my_int
 *
 * Description: Do a quick check to determine if there is a pending
 *              interrupt for this card and disable the IRQ Pin if so.
 *
 *---------------------------------------------------------------------*/
#if (FW_TYPE==_UCB_MGR_)
u08bits SccbMgr_my_int(CARD_HANDLE pCurrCard)
#else
#if defined(DOS)
UCHAR SccbMgr_my_int(USHORT pCurrCard)
#else
UCHAR SccbMgr_my_int(ULONG pCurrCard)
#endif
#endif
{
#if defined(DOS)
   USHORT ioport;
#else
   ULONG ioport;
#endif

   ioport = ((PSCCBcard)pCurrCard)->ioPort;

   if (RD_HARPOON(ioport+hp_int_status) & INT_ASSERTED)
   {

#if defined(DOS)
      MDISABLE_INT(ioport);
#endif

      return(TRUE);
   }

   else

      return(FALSE);
}



/*---------------------------------------------------------------------
 *
 * Function: SccbMgr_isr
 *
 * Description: This is our entry point when an interrupt is generated
 *              by the card and the upper level driver passes it on to
 *              us.
 *
 *---------------------------------------------------------------------*/
#if (FW_TYPE==_UCB_MGR_)
s32bits SccbMgr_isr(CARD_HANDLE pCurrCard)
#else
#if defined(DOS)
int SccbMgr_isr(USHORT pCurrCard)
#else
int SccbMgr_isr(ULONG pCurrCard)
#endif
#endif
{
   PSCCB currSCCB;
   UCHAR thisCard,result,bm_status, bm_int_st;
   USHORT hp_int;
   UCHAR i, target;
#if defined(DOS)
   USHORT ioport;
#else
   ULONG ioport;
#endif

   mOS_Lock((PSCCBcard)pCurrCard);

   thisCard = ((PSCCBcard)pCurrCard)->cardIndex;
   ioport = ((PSCCBcard)pCurrCard)->ioPort;

   MDISABLE_INT(ioport);

#if defined(BUGBUG)
   WR_HARPOON(ioport+hp_user_defined_D, RD_HARPOON(ioport+hp_int_status));
#endif

   if ((bm_int_st=RD_HARPOON(ioport+hp_int_status)) & EXT_STATUS_ON)
		bm_status = RD_HARPOON(ioport+hp_ext_status) & (UCHAR)BAD_EXT_STATUS;
   else
      bm_status = 0;

   WR_HARPOON(ioport+hp_int_mask, (INT_CMD_COMPL | SCSI_INTERRUPT));

   mOS_UnLock((PSCCBcard)pCurrCard);

   while ((hp_int = RDW_HARPOON((ioport+hp_intstat)) & default_intena) |
	  bm_status)
     {

       currSCCB = ((PSCCBcard)pCurrCard)->currentSCCB;

#if defined(BUGBUG)
   Debug_Load(thisCard,(UCHAR) 0XFF);
   Debug_Load(thisCard,bm_int_st);

   Debug_Load(thisCard,hp_int_0);
   Debug_Load(thisCard,hp_int_1);
#endif


      if (hp_int & (FIFO | TIMEOUT | RESET | SCAM_SEL) || bm_status) {
         result = SccbMgr_bad_isr(ioport,thisCard,((PSCCBcard)pCurrCard),hp_int);
         WRW_HARPOON((ioport+hp_intstat), (FIFO | TIMEOUT | RESET | SCAM_SEL));
         bm_status = 0;

         if (result) {

			   mOS_Lock((PSCCBcard)pCurrCard);
            MENABLE_INT(ioport);
			   mOS_UnLock((PSCCBcard)pCurrCard);
            return(result);
            }
         }


      else if (hp_int & ICMD_COMP) {

         if ( !(hp_int & BUS_FREE) ) {
            /* Wait for the BusFree before starting a new command.  We
               must also check for being reselected since the BusFree
               may not show up if another device reselects us in 1.5us or
               less.  SRR Wednesday, 3/8/1995.
	         */
	   while (!(RDW_HARPOON((ioport+hp_intstat)) & (BUS_FREE | RSEL))) ;
	 }

         if (((PSCCBcard)pCurrCard)->globalFlags & F_HOST_XFER_ACT)

            phaseChkFifo(ioport, thisCard);

/*         WRW_HARPOON((ioport+hp_intstat),
            (BUS_FREE | ICMD_COMP | ITAR_DISC | XFER_CNT_0));
         */

		 WRW_HARPOON((ioport+hp_intstat), CLR_ALL_INT_1);

         autoCmdCmplt(ioport,thisCard);

         }


      else if (hp_int & ITAR_DISC)
         {

         if (((PSCCBcard)pCurrCard)->globalFlags & F_HOST_XFER_ACT) {

            phaseChkFifo(ioport, thisCard);

            }

         if (RD_HARPOON(ioport+hp_gp_reg_1) == SMSAVE_DATA_PTR) {

            WR_HARPOON(ioport+hp_gp_reg_1, 0x00);
            currSCCB->Sccb_XferState |= F_NO_DATA_YET;

            currSCCB->Sccb_savedATC = currSCCB->Sccb_ATC;
            }

         currSCCB->Sccb_scsistat = DISCONNECT_ST;
         queueDisconnect(currSCCB,thisCard);

            /* Wait for the BusFree before starting a new command.  We
               must also check for being reselected since the BusFree
               may not show up if another device reselects us in 1.5us or
               less.  SRR Wednesday, 3/8/1995.
             */
	   while (!(RDW_HARPOON((ioport+hp_intstat)) & (BUS_FREE | RSEL)) &&
		  !((RDW_HARPOON((ioport+hp_intstat)) & PHASE) &&
		    RD_HARPOON((ioport+hp_scsisig)) ==
		    (SCSI_BSY | SCSI_REQ | SCSI_CD | SCSI_MSG | SCSI_IOBIT))) ;

	   /*
	     The additional loop exit condition above detects a timing problem
	     with the revision D/E harpoon chips.  The caller should reset the
	     host adapter to recover when 0xFE is returned.
	   */
	   if (!(RDW_HARPOON((ioport+hp_intstat)) & (BUS_FREE | RSEL)))
	     {
	       mOS_Lock((PSCCBcard)pCurrCard);
	       MENABLE_INT(ioport);
	       mOS_UnLock((PSCCBcard)pCurrCard);
	       return 0xFE;
	     }

         WRW_HARPOON((ioport+hp_intstat), (BUS_FREE | ITAR_DISC));


         ((PSCCBcard)pCurrCard)->globalFlags |= F_NEW_SCCB_CMD;

      	}


      else if (hp_int & RSEL) {

         WRW_HARPOON((ioport+hp_intstat), (PROG_HLT | RSEL | PHASE | BUS_FREE));

         if (RDW_HARPOON((ioport+hp_intstat)) & ITAR_DISC)
		      {
            if (((PSCCBcard)pCurrCard)->globalFlags & F_HOST_XFER_ACT)
			      {
               phaseChkFifo(ioport, thisCard);
               }

            if (RD_HARPOON(ioport+hp_gp_reg_1) == SMSAVE_DATA_PTR)
			      {
               WR_HARPOON(ioport+hp_gp_reg_1, 0x00);
               currSCCB->Sccb_XferState |= F_NO_DATA_YET;
               currSCCB->Sccb_savedATC = currSCCB->Sccb_ATC;
               }

            WRW_HARPOON((ioport+hp_intstat), (BUS_FREE | ITAR_DISC));
            currSCCB->Sccb_scsistat = DISCONNECT_ST;
            queueDisconnect(currSCCB,thisCard);
            }

         sres(ioport,thisCard,((PSCCBcard)pCurrCard));
         phaseDecode(ioport,thisCard);

         }


      else if ((hp_int & IDO_STRT) && (!(hp_int & BUS_FREE)))
         {

            WRW_HARPOON((ioport+hp_intstat), (IDO_STRT | XFER_CNT_0));
            phaseDecode(ioport,thisCard);

         }


      else if ( (hp_int & IUNKWN) || (hp_int & PROG_HLT) )
		   {
	 	   WRW_HARPOON((ioport+hp_intstat), (PHASE | IUNKWN | PROG_HLT));
	 	   if ((RD_HARPOON(ioport+hp_prgmcnt_0) & (UCHAR)0x3f)< (UCHAR)SELCHK)
	    		{
	    		phaseDecode(ioport,thisCard);
	    		}
	 	   else
	    		{
   /* Harpoon problem some SCSI target device respond to selection
   with short BUSY pulse (<400ns) this will make the Harpoon is not able
   to latch the correct Target ID into reg. x53.
   The work around require to correct this reg. But when write to this
   reg. (0x53) also increment the FIFO write addr reg (0x6f), thus we
   need to read this reg first then restore it later. After update to 0x53 */

	    		i = (UCHAR)(RD_HARPOON(ioport+hp_fifowrite));
	    		target = (UCHAR)(RD_HARPOON(ioport+hp_gp_reg_3));
	    		WR_HARPOON(ioport+hp_xfer_pad, (UCHAR) ID_UNLOCK);
	    		WR_HARPOON(ioport+hp_select_id, (UCHAR)(target | target<<4));
	    		WR_HARPOON(ioport+hp_xfer_pad, (UCHAR) 0x00);
	    		WR_HARPOON(ioport+hp_fifowrite, i);
	    		WR_HARPOON(ioport+hp_autostart_3, (AUTO_IMMED+TAG_STRT));
	    		}
	 	   }

      else if (hp_int & XFER_CNT_0) {

         WRW_HARPOON((ioport+hp_intstat), XFER_CNT_0);

         schkdd(ioport,thisCard);

         }


      else if (hp_int & BUS_FREE) {

         WRW_HARPOON((ioport+hp_intstat), BUS_FREE);

        	if (((PSCCBcard)pCurrCard)->globalFlags & F_HOST_XFER_ACT) {

           	hostDataXferAbort(ioport,thisCard,currSCCB);
				}

         phaseBusFree(ioport,thisCard);
			}


      else if (hp_int & ITICKLE) {

         WRW_HARPOON((ioport+hp_intstat), ITICKLE);
         ((PSCCBcard)pCurrCard)->globalFlags |= F_NEW_SCCB_CMD;
         }



      if (((PSCCBcard)pCurrCard)->globalFlags & F_NEW_SCCB_CMD) {


         ((PSCCBcard)pCurrCard)->globalFlags &= ~F_NEW_SCCB_CMD;


         if (((PSCCBcard)pCurrCard)->currentSCCB == NULL) {

            queueSearchSelect(((PSCCBcard)pCurrCard),thisCard);
            }

         if (((PSCCBcard)pCurrCard)->currentSCCB != NULL) {
            ((PSCCBcard)pCurrCard)->globalFlags &= ~F_NEW_SCCB_CMD;
            ssel(ioport,thisCard);
            }

         break;

         }

      }  /*end while */

   mOS_Lock((PSCCBcard)pCurrCard);
   MENABLE_INT(ioport);
   mOS_UnLock((PSCCBcard)pCurrCard);

   return(0);
}

/*---------------------------------------------------------------------
 *
 * Function: Sccb_bad_isr
 *
 * Description: Some type of interrupt has occurred which is slightly
 *              out of the ordinary.  We will now decode it fully, in
 *              this routine.  This is broken up in an attempt to save
 *              processing time.
 *
 *---------------------------------------------------------------------*/
#if defined(DOS)
UCHAR SccbMgr_bad_isr(USHORT p_port, UCHAR p_card, PSCCBcard pCurrCard, USHORT p_int)
#else
UCHAR SccbMgr_bad_isr(ULONG p_port, UCHAR p_card, PSCCBcard pCurrCard, USHORT p_int)
#endif
{
#if defined(HARP_REVX)
   ULONG timer;
#endif
UCHAR temp, ScamFlg;
PSCCBMgr_tar_info currTar_Info;
PNVRamInfo pCurrNvRam;


   if (RD_HARPOON(p_port+hp_ext_status) &
         (BM_FORCE_OFF | PCI_DEV_TMOUT | BM_PARITY_ERR | PIO_OVERRUN) )
      {

      if (pCurrCard->globalFlags & F_HOST_XFER_ACT)
         {

         hostDataXferAbort(p_port,p_card, pCurrCard->currentSCCB);
         }

      if (RD_HARPOON(p_port+hp_pci_stat_cfg) & REC_MASTER_ABORT)

         {
         WR_HARPOON(p_port+hp_pci_stat_cfg,
            (RD_HARPOON(p_port+hp_pci_stat_cfg) & ~REC_MASTER_ABORT));

         WR_HARPOON(p_port+hp_host_blk_cnt, 0x00);

         }

      if (pCurrCard->currentSCCB != NULL)
         {

         if (!pCurrCard->currentSCCB->HostStatus)
            pCurrCard->currentSCCB->HostStatus = SCCB_BM_ERR;

         sxfrp(p_port,p_card);

	     temp = (UCHAR)(RD_HARPOON(p_port+hp_ee_ctrl) &
							(EXT_ARB_ACK | SCSI_TERM_ENA_H));
      	WR_HARPOON(p_port+hp_ee_ctrl, ((UCHAR)temp | SEE_MS | SEE_CS));
         WR_HARPOON(p_port+hp_ee_ctrl, temp);

         if (!(RDW_HARPOON((p_port+hp_intstat)) & (BUS_FREE | RESET)))
            {
            phaseDecode(p_port,p_card);
            }
         }
      }


   else if (p_int & RESET)
         {

				WR_HARPOON(p_port+hp_clkctrl_0, CLKCTRL_DEFAULT);
				WR_HARPOON(p_port+hp_sys_ctrl, 0x00);
           if (pCurrCard->currentSCCB != NULL) {

               if (pCurrCard->globalFlags & F_HOST_XFER_ACT)

               hostDataXferAbort(p_port,p_card, pCurrCard->currentSCCB);
               }


           DISABLE_AUTO(p_port);

           sresb(p_port,p_card);

           while(RD_HARPOON(p_port+hp_scsictrl_0) & SCSI_RST) {}

				pCurrNvRam = pCurrCard->pNvRamInfo;
				if(pCurrNvRam){
					ScamFlg = pCurrNvRam->niScamConf;
				}
				else{
				   ScamFlg = (UCHAR) utilEERead(p_port, SCAM_CONFIG/2);
				}

           XbowInit(p_port, ScamFlg);

               scini(p_card, pCurrCard->ourId, 0);

           return(0xFF);
         }


   else if (p_int & FIFO) {

      WRW_HARPOON((p_port+hp_intstat), FIFO);

#if defined(HARP_REVX)

      for (timer=0x00FFFFFFL; timer != 0x00000000L; timer--) {

         if (RD_HARPOON(p_port+hp_xferstat) & FIFO_EMPTY)
            break;

         if (RDW_HARPOON((p_port+hp_intstat)) & BUS_FREE)
            break;
         }


      if ( (RD_HARPOON(p_port+hp_xferstat) & FIFO_EMPTY) &&
           (RD_HARPOON(p_port+hp_fiforead) !=
            RD_HARPOON(p_port+hp_fifowrite)) &&
           (RD_HARPOON(p_port+hp_xfercnt_0))
         )

            WR_HARPOON((p_port+hp_xferstat), 0x01);

/*      else
 */
/*         sxfrp(p_port,p_card);
 */
#else
      if (pCurrCard->currentSCCB != NULL)
         sxfrp(p_port,p_card);
#endif
      }

   else if (p_int & TIMEOUT)
      {

      DISABLE_AUTO(p_port);

      WRW_HARPOON((p_port+hp_intstat),
		  (PROG_HLT | TIMEOUT | SEL |BUS_FREE | PHASE | IUNKWN));

      pCurrCard->currentSCCB->HostStatus = SCCB_SELECTION_TIMEOUT;


		currTar_Info = &sccbMgrTbl[p_card][pCurrCard->currentSCCB->TargID];
		if((pCurrCard->globalFlags & F_CONLUN_IO) &&
			((currTar_Info->TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING))
	      currTar_Info->TarLUNBusy[pCurrCard->currentSCCB->Lun] = FALSE;
		else
	      currTar_Info->TarLUNBusy[0] = FALSE;


      if (currTar_Info->TarEEValue & EE_SYNC_MASK)
         {
	       currTar_Info->TarSyncCtrl = 0;
         currTar_Info->TarStatus &= ~TAR_SYNC_MASK;
         }

      if (currTar_Info->TarEEValue & EE_WIDE_SCSI)
         {
         currTar_Info->TarStatus &= ~TAR_WIDE_MASK;
         }

      sssyncv(p_port, pCurrCard->currentSCCB->TargID, NARROW_SCSI,currTar_Info);

      queueCmdComplete(pCurrCard, pCurrCard->currentSCCB, p_card);

      }

#if defined(SCAM_LEV_2)

   else if (p_int & SCAM_SEL)
      {

      scarb(p_port,LEVEL2_TAR);
      scsel(p_port);
      scasid(p_card, p_port);

      scbusf(p_port);

      WRW_HARPOON((p_port+hp_intstat), SCAM_SEL);
      }
#endif

   return(0x00);
}


/*---------------------------------------------------------------------
 *
 * Function: SccbMgr_scsi_reset
 *
 * Description: A SCSI bus reset will be generated and all outstanding
 *              Sccbs will be returned via the callback.
 *
 *---------------------------------------------------------------------*/
#if (FW_TYPE==_UCB_MGR_)
void SccbMgr_scsi_reset(CARD_HANDLE pCurrCard)
#else
#if defined(DOS)
void SccbMgr_scsi_reset(USHORT pCurrCard)
#else
void SccbMgr_scsi_reset(ULONG pCurrCard)
#endif
#endif
{
   UCHAR thisCard;

   thisCard = ((PSCCBcard)pCurrCard)->cardIndex;

   mOS_Lock((PSCCBcard)pCurrCard);

   if (((PSCCBcard) pCurrCard)->globalFlags & F_GREEN_PC)
      {
      WR_HARPOON(((PSCCBcard) pCurrCard)->ioPort+hp_clkctrl_0, CLKCTRL_DEFAULT);
      WR_HARPOON(((PSCCBcard) pCurrCard)->ioPort+hp_sys_ctrl, 0x00);
      }

   sresb(((PSCCBcard)pCurrCard)->ioPort,thisCard);

   if (RD_HARPOON(((PSCCBcard)pCurrCard)->ioPort+hp_ext_status) & BM_CMD_BUSY)
      {
      WR_HARPOON(((PSCCBcard) pCurrCard)->ioPort+hp_page_ctrl,
         (RD_HARPOON(((PSCCBcard) pCurrCard)->ioPort+hp_page_ctrl)
         & ~SCATTER_EN));

      WR_HARPOON(((PSCCBcard) pCurrCard)->ioPort+hp_sg_addr,0x00);

      ((PSCCBcard) pCurrCard)->globalFlags &= ~F_HOST_XFER_ACT;
      busMstrTimeOut(((PSCCBcard) pCurrCard)->ioPort);

      WR_HARPOON(((PSCCBcard) pCurrCard)->ioPort+hp_int_mask,
         (INT_CMD_COMPL | SCSI_INTERRUPT));
      }

/*
      if (utilEERead(((PSCCBcard)pCurrCard)->ioPort, (SCAM_CONFIG/2))
            & SCAM_ENABLED)
*/
         scini(thisCard, ((PSCCBcard)pCurrCard)->ourId, 0);

#if (FW_TYPE==_UCB_MGR_)
   ((PSCCBcard)pCurrCard)->cardInfo->ai_AEN_routine(0x01,pCurrCard,0,0,0,0);
#endif

   mOS_UnLock((PSCCBcard)pCurrCard);
}


/*---------------------------------------------------------------------
 *
 * Function: SccbMgr_timer_expired
 *
 * Description: This function allow me to kill my own job that has not
 *              yet completed, and has cause a timeout to occur.  This
 *              timeout has caused the upper level driver to call this
 *              function.
 *
 *---------------------------------------------------------------------*/

#if (FW_TYPE==_UCB_MGR_)
void SccbMgr_timer_expired(CARD_HANDLE pCurrCard)
#else
#if defined(DOS)
void SccbMgr_timer_expired(USHORT pCurrCard)
#else
void SccbMgr_timer_expired(ULONG pCurrCard)
#endif
#endif
{
}

#if defined(DOS)
/*---------------------------------------------------------------------
 *
 * Function: SccbMgr_status
 *
 * Description: This function returns the number of outstanding SCCB's.
 *              This is specific to the DOS enviroment, which needs this
 *              to help them keep protected and real mode commands staight.
 *
 *---------------------------------------------------------------------*/

USHORT SccbMgr_status(USHORT pCurrCard)
{
   return(BL_Card[pCurrCard].cmdCounter);
}
#endif

/*---------------------------------------------------------------------
 *
 * Function: SccbMgrTableInit
 *
 * Description: Initialize all Sccb manager data structures.
 *
 *---------------------------------------------------------------------*/

void SccbMgrTableInitAll()
{
   UCHAR thisCard;

   for (thisCard = 0; thisCard < MAX_CARDS; thisCard++)
      {
      SccbMgrTableInitCard(&BL_Card[thisCard],thisCard);

      BL_Card[thisCard].ioPort      = 0x00;
      BL_Card[thisCard].cardInfo    = NULL;
      BL_Card[thisCard].cardIndex   = 0xFF;
      BL_Card[thisCard].ourId       = 0x00;
		BL_Card[thisCard].pNvRamInfo	= NULL;
      }
}


/*---------------------------------------------------------------------
 *
 * Function: SccbMgrTableInit
 *
 * Description: Initialize all Sccb manager data structures.
 *
 *---------------------------------------------------------------------*/

void SccbMgrTableInitCard(PSCCBcard pCurrCard, UCHAR p_card)
{
   UCHAR scsiID, qtag;

	for (qtag = 0; qtag < QUEUE_DEPTH; qtag++)
	{
		BL_Card[p_card].discQ_Tbl[qtag] = NULL;
	}

   for (scsiID = 0; scsiID < MAX_SCSI_TAR; scsiID++)
      {
      sccbMgrTbl[p_card][scsiID].TarStatus = 0;
      sccbMgrTbl[p_card][scsiID].TarEEValue = 0;
      SccbMgrTableInitTarget(p_card, scsiID);
      }

   pCurrCard->scanIndex = 0x00;
   pCurrCard->currentSCCB = NULL;
   pCurrCard->globalFlags = 0x00;
   pCurrCard->cmdCounter  = 0x00;
	pCurrCard->tagQ_Lst = 0x01;
	pCurrCard->discQCount = 0; 


}


/*---------------------------------------------------------------------
 *
 * Function: SccbMgrTableInit
 *
 * Description: Initialize all Sccb manager data structures.
 *
 *---------------------------------------------------------------------*/

void SccbMgrTableInitTarget(UCHAR p_card, UCHAR target)
{

	UCHAR lun, qtag;
	PSCCBMgr_tar_info currTar_Info;

	currTar_Info = &sccbMgrTbl[p_card][target];

	currTar_Info->TarSelQ_Cnt = 0;
	currTar_Info->TarSyncCtrl = 0;

	currTar_Info->TarSelQ_Head = NULL;
	currTar_Info->TarSelQ_Tail = NULL;
	currTar_Info->TarTagQ_Cnt = 0;
	currTar_Info->TarLUN_CA = FALSE;


	for (lun = 0; lun < MAX_LUN; lun++)
	{
		currTar_Info->TarLUNBusy[lun] = FALSE;
		currTar_Info->LunDiscQ_Idx[lun] = 0;
	}

	for (qtag = 0; qtag < QUEUE_DEPTH; qtag++)
	{
		if(BL_Card[p_card].discQ_Tbl[qtag] != NULL)
		{
			if(BL_Card[p_card].discQ_Tbl[qtag]->TargID == target)
			{
				BL_Card[p_card].discQ_Tbl[qtag] = NULL;
				BL_Card[p_card].discQCount--;
			}
		}
	}
}

#if defined(BUGBUG)

/*****************************************************************
 * Save the current byte in the debug array
 *****************************************************************/


void Debug_Load(UCHAR p_card, UCHAR p_bug_data)
{
   debug_int[p_card][debug_index[p_card]] = p_bug_data;
   debug_index[p_card]++;

   if (debug_index[p_card] == debug_size)

      debug_index[p_card] = 0;
}

#endif
#ident "$Id: sccb_dat.c 1.10 1997/02/22 03:16:02 awin Exp $"
/*----------------------------------------------------------------------
 *
 *
 *   Copyright 1995-1996 by Mylex Corporation.  All Rights Reserved
 *
 *   This file is available under both the GNU General Public License
 *   and a BSD-style copyright; see LICENSE.FlashPoint for details.
 *
 *   $Workfile:   sccb_dat.c  $
 *
 *   Description:  Functions relating to handling of the SCCB interface 
 *                 between the device driver and the HARPOON.
 *
 *   $Date: 1997/02/22 03:16:02 $
 *
 *   $Revision: 1.10 $
 *
 *----------------------------------------------------------------------*/

/*#include <globals.h>*/

#if (FW_TYPE==_UCB_MGR_)
	/*#include <budi.h>*/
#endif

/*#include <sccbmgr.h>*/
/*#include <blx30.h>*/
/*#include <target.h>*/
/*#include <harpoon.h>*/

/*
**  IMPORTANT NOTE!!!
**
**  You MUST preassign all data to a valid value or zero.  This is
**  required due to the MS compiler bug under OS/2 and Solaris Real-Mode
**  driver environment.
*/


SCCBMGR_TAR_INFO sccbMgrTbl[MAX_CARDS][MAX_SCSI_TAR] = { { { 0 } } };
SCCBCARD BL_Card[MAX_CARDS] = { { 0 } };
SCCBSCAM_INFO scamInfo[MAX_SCSI_TAR] = { { { 0 } } };
NVRAMINFO nvRamInfo[MAX_MB_CARDS] = { { 0 } };


#if defined(OS2)
void (far *s_PhaseTbl[8]) (ULONG, UCHAR) = { 0 };
UCHAR temp_id_string[ID_STRING_LENGTH] = { 0 };
#elif defined(SOLARIS_REAL_MODE) || defined(__STDC__)
void (*s_PhaseTbl[8]) (ULONG, UCHAR) = { 0 };
#else
void (*s_PhaseTbl[8]) ();
#endif

#if defined(DOS)
UCHAR first_time = 0;
#endif

UCHAR mbCards = 0;
UCHAR scamHAString[] = {0x63, 0x07, 'B', 'U', 'S', 'L', 'O', 'G', 'I', 'C', \
								' ', 'B', 'T', '-', '9', '3', '0', \
								0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, \
								0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};

USHORT default_intena = 0;

#if defined(BUGBUG)
UCHAR    debug_int[MAX_CARDS][debug_size] = { 0 };
UCHAR    debug_index[MAX_CARDS] = { 0 };
UCHAR    reserved_1[3] = { 0 };
#endif
#ident "$Id: scsi.c 1.23 1997/07/09 21:42:54 mohan Exp $"
/*----------------------------------------------------------------------
 *
 *
 *   Copyright 1995-1996 by Mylex Corporation.  All Rights Reserved
 *
 *   This file is available under both the GNU General Public License
 *   and a BSD-style copyright; see LICENSE.FlashPoint for details.
 *
 *   $Workfile:   scsi.c  $
 *
 *   Description:  Functions for handling SCSI bus functions such as
 *                 selection/reselection, sync negotiation, message-in
 *                 decoding.
 *
 *   $Date: 1997/07/09 21:42:54 $
 *
 *   $Revision: 1.23 $
 *
 *----------------------------------------------------------------------*/

/*#include <globals.h>*/

#if (FW_TYPE==_UCB_MGR_)
	/*#include <budi.h>*/
#endif

/*#include <sccbmgr.h>*/
/*#include <blx30.h>*/
/*#include <target.h>*/
/*#include <scsi2.h>*/
/*#include <eeprom.h>*/
/*#include <harpoon.h>*/


/*
extern SCCBCARD BL_Card[MAX_CARDS];
extern SCCBMGR_TAR_INFO sccbMgrTbl[MAX_CARDS][MAX_SCSI_TAR];
#if defined(BUGBUG)
void Debug_Load(UCHAR p_card, UCHAR p_bug_data);
#endif
*/

/*---------------------------------------------------------------------
 *
 * Function: sfetm
 *
 * Description: Read in a message byte from the SCSI bus, and check
 *              for a parity error.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
UCHAR sfm(USHORT port, PSCCB pCurrSCCB)
#else
UCHAR sfm(ULONG port, PSCCB pCurrSCCB)
#endif
{
	UCHAR message;
	USHORT TimeOutLoop;

	TimeOutLoop = 0;
	while( (!(RD_HARPOON(port+hp_scsisig) & SCSI_REQ)) &&
			(TimeOutLoop++ < 20000) ){}


	WR_HARPOON(port+hp_portctrl_0, SCSI_PORT);

	message = RD_HARPOON(port+hp_scsidata_0);

	WR_HARPOON(port+hp_scsisig, SCSI_ACK + S_MSGI_PH);


	if (TimeOutLoop > 20000)
		message = 0x00;   /* force message byte = 0 if Time Out on Req */

	if ((RDW_HARPOON((port+hp_intstat)) & PARITY) &&
		(RD_HARPOON(port+hp_addstat) & SCSI_PAR_ERR))
	{
		WR_HARPOON(port+hp_scsisig, (SCSI_ACK + S_ILL_PH));
		WR_HARPOON(port+hp_xferstat, 0);
		WR_HARPOON(port+hp_fiforead, 0);
		WR_HARPOON(port+hp_fifowrite, 0);
		if (pCurrSCCB != NULL)
		{
			pCurrSCCB->Sccb_scsimsg = SMPARITY;
		}
		message = 0x00;
		do
		{
			ACCEPT_MSG_ATN(port);
			TimeOutLoop = 0;
			while( (!(RD_HARPOON(port+hp_scsisig) & SCSI_REQ)) &&
				(TimeOutLoop++ < 20000) ){}
			if (TimeOutLoop > 20000)
			{
				WRW_HARPOON((port+hp_intstat), PARITY);
				return(message);
			}
			if ((RD_HARPOON(port+hp_scsisig) & S_SCSI_PHZ) != S_MSGI_PH)
			{
				WRW_HARPOON((port+hp_intstat), PARITY);
				return(message);
			}
			WR_HARPOON(port+hp_portctrl_0, SCSI_PORT);

			RD_HARPOON(port+hp_scsidata_0);

			WR_HARPOON(port+hp_scsisig, (SCSI_ACK + S_ILL_PH));

		}while(1);

	}
	WR_HARPOON(port+hp_scsisig, (SCSI_ACK + S_ILL_PH));
	WR_HARPOON(port+hp_xferstat, 0);
	WR_HARPOON(port+hp_fiforead, 0);
	WR_HARPOON(port+hp_fifowrite, 0);
	return(message);
}


/*---------------------------------------------------------------------
 *
 * Function: ssel
 *
 * Description: Load up automation and select target device.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void ssel(USHORT port, UCHAR p_card)
#else
void ssel(ULONG port, UCHAR p_card)
#endif
{

#if defined(DOS)
   UCHAR auto_loaded, i, target, *theCCB;
#elif defined(OS2)
   UCHAR auto_loaded, i, target;
   UCHAR far *theCCB;
#else
   UCHAR auto_loaded, i, target, *theCCB;
#endif

#if defined(DOS)
   USHORT cdb_reg;
#else
   ULONG cdb_reg;
#endif
   PSCCBcard CurrCard;
   PSCCB currSCCB;
   PSCCBMgr_tar_info currTar_Info;
   UCHAR lastTag, lun;

   CurrCard = &BL_Card[p_card];
   currSCCB = CurrCard->currentSCCB;
   target = currSCCB->TargID;
   currTar_Info = &sccbMgrTbl[p_card][target];
   lastTag = CurrCard->tagQ_Lst;

   ARAM_ACCESS(port);


	if ((currTar_Info->TarStatus & TAR_TAG_Q_MASK) == TAG_Q_REJECT)
		currSCCB->ControlByte &= ~F_USE_CMD_Q;

	if(((CurrCard->globalFlags & F_CONLUN_IO) && 
		((currTar_Info->TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING)))

	   lun = currSCCB->Lun;
	else
		lun = 0;


#if defined(DOS)
   currTar_Info->TarLUNBusy[lun] = TRUE;

#else

   if (CurrCard->globalFlags & F_TAG_STARTED)
      {
      if (!(currSCCB->ControlByte & F_USE_CMD_Q))
         {
      	if ((currTar_Info->TarLUN_CA == FALSE)
      	    && ((currTar_Info->TarStatus & TAR_TAG_Q_MASK)
      	    == TAG_Q_TRYING))
            {

	         if (currTar_Info->TarTagQ_Cnt !=0)
                  {
         		   currTar_Info->TarLUNBusy[lun] = TRUE;
            		queueSelectFail(CurrCard,p_card);
					   SGRAM_ACCESS(port);
         		   return;
         		   }

            else {
         		  currTar_Info->TarLUNBusy[lun] = TRUE;
         		  }

   	      }  /*End non-tagged */

	      else {
	         currTar_Info->TarLUNBusy[lun] = TRUE;
	         }

	      }  /*!Use cmd Q Tagged */

	   else {
   	     if (currTar_Info->TarLUN_CA == TRUE)
               {
      	      queueSelectFail(CurrCard,p_card);
				   SGRAM_ACCESS(port);
      	      return;
	            }

	        currTar_Info->TarLUNBusy[lun] = TRUE;

   	     }  /*else use cmd Q tagged */

      }  /*if glob tagged started */

   else {
        currTar_Info->TarLUNBusy[lun] = TRUE;
        }

#endif /* DOS */



	if((((CurrCard->globalFlags & F_CONLUN_IO) && 
		((currTar_Info->TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING)) 
		|| (!(currSCCB->ControlByte & F_USE_CMD_Q))))
	{
		if(CurrCard->discQCount >= QUEUE_DEPTH)
		{
			currTar_Info->TarLUNBusy[lun] = TRUE;
			queueSelectFail(CurrCard,p_card);
			SGRAM_ACCESS(port);
			return;
		}
		for (i = 1; i < QUEUE_DEPTH; i++)
		{
			if (++lastTag >= QUEUE_DEPTH) lastTag = 1;
			if (CurrCard->discQ_Tbl[lastTag] == NULL)
			{
				CurrCard->tagQ_Lst = lastTag;
				currTar_Info->LunDiscQ_Idx[lun] = lastTag;
				CurrCard->discQ_Tbl[lastTag] = currSCCB;
				CurrCard->discQCount++;
				break;
			}
		}
		if(i == QUEUE_DEPTH)
		{
			currTar_Info->TarLUNBusy[lun] = TRUE;
			queueSelectFail(CurrCard,p_card);
			SGRAM_ACCESS(port);
			return;
		}
	}



   auto_loaded = FALSE;

   WR_HARPOON(port+hp_select_id, target);
   WR_HARPOON(port+hp_gp_reg_3, target);  /* Use by new automation logic */

   if (currSCCB->OperationCode == RESET_COMMAND) {
      WRW_HARPOON((port+ID_MSG_STRT), (MPM_OP+AMSG_OUT+
               	 (currSCCB->Sccb_idmsg & ~DISC_PRIV)));

      WRW_HARPOON((port+ID_MSG_STRT+2),BRH_OP+ALWAYS+NP);

      currSCCB->Sccb_scsimsg = SMDEV_RESET;

      WR_HARPOON(port+hp_autostart_3, (SELECT+SELCHK_STRT));
      auto_loaded = TRUE;
      currSCCB->Sccb_scsistat = SELECT_BDR_ST;

      if (currTar_Info->TarEEValue & EE_SYNC_MASK)
         {
	       currTar_Info->TarSyncCtrl = 0;
	      currTar_Info->TarStatus &= ~TAR_SYNC_MASK;
	      }

#if defined(WIDE_SCSI)

      if (currTar_Info->TarEEValue & EE_WIDE_SCSI)
         {
      	currTar_Info->TarStatus &= ~TAR_WIDE_MASK;
      	}
#endif

      sssyncv(port, target, NARROW_SCSI,currTar_Info);
      SccbMgrTableInitTarget(p_card, target);

      }

		else if(currSCCB->Sccb_scsistat == ABORT_ST)
		{
			WRW_HARPOON((port+ID_MSG_STRT), (MPM_OP+AMSG_OUT+
								(currSCCB->Sccb_idmsg & ~DISC_PRIV)));

      WRW_HARPOON((port+ID_MSG_STRT+2),BRH_OP+ALWAYS+CMDPZ);

			WRW_HARPOON((port+SYNC_MSGS+0), (MPM_OP+AMSG_OUT+
								(((UCHAR)(currSCCB->ControlByte & TAG_TYPE_MASK)
								>> 6) | (UCHAR)0x20)));
			WRW_HARPOON((port+SYNC_MSGS+2),
							(MPM_OP+AMSG_OUT+currSCCB->Sccb_tag));
			WRW_HARPOON((port+SYNC_MSGS+4), (BRH_OP+ALWAYS+NP ));

			WR_HARPOON(port+hp_autostart_3, (SELECT+SELCHK_STRT));
			auto_loaded = TRUE;
		
		}

#if defined(WIDE_SCSI)


   else if (!(currTar_Info->TarStatus & WIDE_NEGOCIATED))  {
      auto_loaded = siwidn(port,p_card);
      currSCCB->Sccb_scsistat = SELECT_WN_ST;
      }

#endif


   else if (!((currTar_Info->TarStatus & TAR_SYNC_MASK)
      == SYNC_SUPPORTED))  {
      auto_loaded = sisyncn(port,p_card, FALSE);
      currSCCB->Sccb_scsistat = SELECT_SN_ST;
      }


   if (!auto_loaded)
      {

#if !defined(DOS)
      if (currSCCB->ControlByte & F_USE_CMD_Q)
         {

         CurrCard->globalFlags |= F_TAG_STARTED;

         if ((currTar_Info->TarStatus & TAR_TAG_Q_MASK)
            == TAG_Q_REJECT)
            {
            currSCCB->ControlByte &= ~F_USE_CMD_Q;

            /* Fix up the start instruction with a jump to
               Non-Tag-CMD handling */
            WRW_HARPOON((port+ID_MSG_STRT),BRH_OP+ALWAYS+NTCMD);

            WRW_HARPOON((port+NON_TAG_ID_MSG),
	                     (MPM_OP+AMSG_OUT+currSCCB->Sccb_idmsg));

	         WR_HARPOON(port+hp_autostart_3, (SELECT+SELCHK_STRT));

	         /* Setup our STATE so we know what happend when
               the wheels fall off. */
            currSCCB->Sccb_scsistat = SELECT_ST;

	         currTar_Info->TarLUNBusy[lun] = TRUE;
            }

         else
            {
            WRW_HARPOON((port+ID_MSG_STRT), (MPM_OP+AMSG_OUT+currSCCB->Sccb_idmsg));

            WRW_HARPOON((port+ID_MSG_STRT+2), (MPM_OP+AMSG_OUT+
                        (((UCHAR)(currSCCB->ControlByte & TAG_TYPE_MASK)
                        >> 6) | (UCHAR)0x20)));

				for (i = 1; i < QUEUE_DEPTH; i++)
				{
					if (++lastTag >= QUEUE_DEPTH) lastTag = 1;
					if (CurrCard->discQ_Tbl[lastTag] == NULL)
					{
						WRW_HARPOON((port+ID_MSG_STRT+6),
							(MPM_OP+AMSG_OUT+lastTag));
						CurrCard->tagQ_Lst = lastTag;
						currSCCB->Sccb_tag = lastTag;
						CurrCard->discQ_Tbl[lastTag] = currSCCB;
						CurrCard->discQCount++;
						break;
					}
				}


            if ( i == QUEUE_DEPTH )
               {
   	         currTar_Info->TarLUNBusy[lun] = TRUE;
               queueSelectFail(CurrCard,p_card);
				   SGRAM_ACCESS(port);
   	         return;
   	         }

            currSCCB->Sccb_scsistat = SELECT_Q_ST;

   	      WR_HARPOON(port+hp_autostart_3, (SELECT+SELCHK_STRT));
            }
         }

      else
         {
#endif   /* !DOS */

         WRW_HARPOON((port+ID_MSG_STRT),BRH_OP+ALWAYS+NTCMD);

      	WRW_HARPOON((port+NON_TAG_ID_MSG),
            (MPM_OP+AMSG_OUT+currSCCB->Sccb_idmsg));

         currSCCB->Sccb_scsistat = SELECT_ST;

         WR_HARPOON(port+hp_autostart_3, (SELECT+SELCHK_STRT));
#if !defined(DOS)
         }
#endif


#if defined(OS2)
      theCCB = (UCHAR far *)&currSCCB->Cdb[0];
#else
      theCCB = (UCHAR *)&currSCCB->Cdb[0];
#endif

      cdb_reg = port + CMD_STRT;

      for (i=0; i < currSCCB->CdbLength; i++)
         {
         WRW_HARPOON(cdb_reg, (MPM_OP + ACOMMAND + *theCCB));
         cdb_reg +=2;
         theCCB++;
         }

      if (currSCCB->CdbLength != TWELVE_BYTE_CMD)
         WRW_HARPOON(cdb_reg, (BRH_OP+ALWAYS+    NP));

      }  /* auto_loaded */

#if defined(WIDE_SCSI)
   WRW_HARPOON((port+hp_fiforead), (USHORT) 0x00);
   WR_HARPOON(port+hp_xferstat, 0x00);
#endif

   WRW_HARPOON((port+hp_intstat), (PROG_HLT | TIMEOUT | SEL | BUS_FREE));

   WR_HARPOON(port+hp_portctrl_0,(SCSI_PORT));


   if (!(currSCCB->Sccb_MGRFlags & F_DEV_SELECTED))
      {
      WR_HARPOON(port+hp_scsictrl_0, (SEL_TAR | ENA_ATN | ENA_RESEL | ENA_SCAM_SEL));
      }
   else
      {

/*      auto_loaded =  (RD_HARPOON(port+hp_autostart_3) & (UCHAR)0x1F);
      auto_loaded |= AUTO_IMMED; */
      auto_loaded = AUTO_IMMED;

      DISABLE_AUTO(port);

      WR_HARPOON(port+hp_autostart_3, auto_loaded);
      }

   SGRAM_ACCESS(port);
}


/*---------------------------------------------------------------------
 *
 * Function: sres
 *
 * Description: Hookup the correct CCB and handle the incoming messages.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void sres(USHORT port, UCHAR p_card, PSCCBcard pCurrCard)
#else
void sres(ULONG port, UCHAR p_card, PSCCBcard pCurrCard)
#endif
{

#if defined(V302)
#ifdef DOS
   UCHAR our_target,message, msgRetryCount;
   extern UCHAR lun, tag;
#else
   UCHAR our_target,message,lun,tag, msgRetryCount;
#endif

#else  /* V302 */
   UCHAR our_target, message, lun = 0, tag, msgRetryCount;
#endif /* V302 */


   PSCCBMgr_tar_info currTar_Info;
	PSCCB currSCCB;




	if(pCurrCard->currentSCCB != NULL)
	{
		currTar_Info = &sccbMgrTbl[p_card][pCurrCard->currentSCCB->TargID];
		DISABLE_AUTO(port);


		WR_HARPOON((port+hp_scsictrl_0),(ENA_RESEL | ENA_SCAM_SEL));


		currSCCB = pCurrCard->currentSCCB;
		if(currSCCB->Sccb_scsistat == SELECT_WN_ST)
		{
			currTar_Info->TarStatus &= ~TAR_WIDE_MASK;
			currSCCB->Sccb_scsistat = BUS_FREE_ST;
		}
		if(currSCCB->Sccb_scsistat == SELECT_SN_ST)
		{
			currTar_Info->TarStatus &= ~TAR_SYNC_MASK;
			currSCCB->Sccb_scsistat = BUS_FREE_ST;
		}
		if(((pCurrCard->globalFlags & F_CONLUN_IO) &&
			((currTar_Info->TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING)))
		{
      	currTar_Info->TarLUNBusy[currSCCB->Lun] = FALSE;
			if(currSCCB->Sccb_scsistat != ABORT_ST)
			{
				pCurrCard->discQCount--;
				pCurrCard->discQ_Tbl[currTar_Info->LunDiscQ_Idx[currSCCB->Lun]] 
													= NULL;
			}
		}
		else
		{
	      currTar_Info->TarLUNBusy[0] = FALSE;
			if(currSCCB->Sccb_tag)
			{
				if(currSCCB->Sccb_scsistat != ABORT_ST)
				{
					pCurrCard->discQCount--;
					pCurrCard->discQ_Tbl[currSCCB->Sccb_tag] = NULL;
				}
			}else
			{
				if(currSCCB->Sccb_scsistat != ABORT_ST)
				{
					pCurrCard->discQCount--;
					pCurrCard->discQ_Tbl[currTar_Info->LunDiscQ_Idx[0]] = NULL;
				}
			}
		}

      queueSelectFail(&BL_Card[p_card],p_card);
	}

#if defined(WIDE_SCSI)
	WRW_HARPOON((port+hp_fiforead), (USHORT) 0x00);
#endif


	our_target = (UCHAR)(RD_HARPOON(port+hp_select_id) >> 4);
	currTar_Info = &sccbMgrTbl[p_card][our_target];


	msgRetryCount = 0;
	do
	{

#if defined(V302)

		message = GetTarLun(port, p_card, our_target, pCurrCard, &tag, &lun);

#else /* V302 */

		currTar_Info = &sccbMgrTbl[p_card][our_target];
		tag = 0;


		while(!(RD_HARPOON(port+hp_scsisig) & SCSI_REQ))
		{
			if (! (RD_HARPOON(port+hp_scsisig) & SCSI_BSY))
			{

				WRW_HARPOON((port+hp_intstat), PHASE);
				return;
			}
		}

		WRW_HARPOON((port+hp_intstat), PHASE);
		if ((RD_HARPOON(port+hp_scsisig) & S_SCSI_PHZ) == S_MSGI_PH)
		{

			message = sfm(port,pCurrCard->currentSCCB);
			if (message)
			{

				if (message <= (0x80 | LUN_MASK))
				{
					lun = message & (UCHAR)LUN_MASK;

#if !defined(DOS)
					if ((currTar_Info->TarStatus & TAR_TAG_Q_MASK) == TAG_Q_TRYING)
					{
						if (currTar_Info->TarTagQ_Cnt != 0)
						{

							if (!(currTar_Info->TarLUN_CA))
							{
								ACCEPT_MSG(port);    /*Release the ACK for ID msg. */


								message = sfm(port,pCurrCard->currentSCCB);
								if (message)
								{
									ACCEPT_MSG(port);
								}

								else
   								message = FALSE;

								if(message != FALSE)
								{
									tag = sfm(port,pCurrCard->currentSCCB);

									if (!(tag)) 
										message = FALSE;
								}

							} /*C.A. exists! */

						} /*End Q cnt != 0 */

					} /*End Tag cmds supported! */
#endif /* !DOS */

				} /*End valid ID message.  */

				else
				{

					ACCEPT_MSG_ATN(port);
				}

			} /* End good id message. */

			else
			{

				message = FALSE;
			}
		}
		else
		{
			ACCEPT_MSG_ATN(port);

		   while (!(RDW_HARPOON((port+hp_intstat)) & (PHASE | RESET)) &&
			  !(RD_HARPOON(port+hp_scsisig) & SCSI_REQ) &&
			  (RD_HARPOON(port+hp_scsisig) & SCSI_BSY)) ;

			return;
		}
	
#endif /* V302 */

		if(message == FALSE)
		{
			msgRetryCount++;
			if(msgRetryCount == 1)
			{
				SendMsg(port, SMPARITY);
			}
			else
			{
				SendMsg(port, SMDEV_RESET);

				sssyncv(port, our_target, NARROW_SCSI,currTar_Info);

				if (sccbMgrTbl[p_card][our_target].TarEEValue & EE_SYNC_MASK) 
				{
			
					sccbMgrTbl[p_card][our_target].TarStatus &= ~TAR_SYNC_MASK;

				}

				if (sccbMgrTbl[p_card][our_target].TarEEValue & EE_WIDE_SCSI) 
				{

					sccbMgrTbl[p_card][our_target].TarStatus &= ~TAR_WIDE_MASK;
				}


				queueFlushTargSccb(p_card, our_target, SCCB_COMPLETE);
				SccbMgrTableInitTarget(p_card,our_target);
				return;
			}
		}
	}while(message == FALSE);



	if(((pCurrCard->globalFlags & F_CONLUN_IO) &&
		((currTar_Info->TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING)))
	{
		currTar_Info->TarLUNBusy[lun] = TRUE;
		pCurrCard->currentSCCB = pCurrCard->discQ_Tbl[currTar_Info->LunDiscQ_Idx[lun]];
		if(pCurrCard->currentSCCB != NULL)
		{
			ACCEPT_MSG(port);
		}
		else 
		{
			ACCEPT_MSG_ATN(port);
		}
	}
	else
	{
		currTar_Info->TarLUNBusy[0] = TRUE;


		if (tag)
		{
			if (pCurrCard->discQ_Tbl[tag] != NULL)
			{
				pCurrCard->currentSCCB = pCurrCard->discQ_Tbl[tag];
		 		currTar_Info->TarTagQ_Cnt--;
				ACCEPT_MSG(port);
			}
			else
			{
			ACCEPT_MSG_ATN(port);
			}
		}else
		{
			pCurrCard->currentSCCB = pCurrCard->discQ_Tbl[currTar_Info->LunDiscQ_Idx[0]];
			if(pCurrCard->currentSCCB != NULL)
			{
				ACCEPT_MSG(port);
			}
			else 
			{
				ACCEPT_MSG_ATN(port);
			}
		}
	}

	if(pCurrCard->currentSCCB != NULL)
	{
		if(pCurrCard->currentSCCB->Sccb_scsistat == ABORT_ST)
		{
		/* During Abort Tag command, the target could have got re-selected
			and completed the command. Check the select Q and remove the CCB
			if it is in the Select Q */
			queueFindSccb(pCurrCard->currentSCCB, p_card);
		}
	}


   while (!(RDW_HARPOON((port+hp_intstat)) & (PHASE | RESET)) &&
	  !(RD_HARPOON(port+hp_scsisig) & SCSI_REQ) &&
	  (RD_HARPOON(port+hp_scsisig) & SCSI_BSY)) ;
}

#if defined(V302)

#if defined(DOS)
UCHAR GetTarLun(USHORT port, UCHAR p_card, UCHAR our_target, PSCCBcard pCurrCard, PUCHAR tag, PUCHAR lun)
#else
UCHAR GetTarLun(ULONG port, UCHAR p_card, UCHAR our_target, PSCCBcard pCurrCard, PUCHAR tag, PUCHAR lun)
#endif
{
   UCHAR message;
   PSCCBMgr_tar_info currTar_Info;


	currTar_Info = &sccbMgrTbl[p_card][our_target];
	*tag = 0;


	while(!(RD_HARPOON(port+hp_scsisig) & SCSI_REQ))
	{
		if (! (RD_HARPOON(port+hp_scsisig) & SCSI_BSY))
		{

			WRW_HARPOON((port+hp_intstat), PHASE);
			return(TRUE);
		}
	}

	WRW_HARPOON((port+hp_intstat), PHASE);
	if ((RD_HARPOON(port+hp_scsisig) & S_SCSI_PHZ) == S_MSGI_PH)
	{

		message = sfm(port,pCurrCard->currentSCCB);
		if (message)
		{

			if (message <= (0x80 | LUN_MASK))
			{
				*lun = message & (UCHAR)LUN_MASK;

#if !defined(DOS)
				if ((currTar_Info->TarStatus & TAR_TAG_Q_MASK) == TAG_Q_TRYING)
				{
					if (currTar_Info->TarTagQ_Cnt != 0)
					{

						if (!(currTar_Info->TarLUN_CA))
						{
							ACCEPT_MSG(port);    /*Release the ACK for ID msg. */


							message = sfm(port,pCurrCard->currentSCCB);
							if (message)
							{
								ACCEPT_MSG(port);
							}

							else
   							return(FALSE);

							*tag = sfm(port,pCurrCard->currentSCCB);

							if (!(*tag)) return(FALSE);

						} /*C.A. exists! */

					} /*End Q cnt != 0 */

				} /*End Tag cmds supported! */
#endif /* !DOS */

			} /*End valid ID message.  */

			else
			{

				ACCEPT_MSG_ATN(port);
			}

		} /* End good id message. */

		else
		{

			return(FALSE);
		}
	}
	else
	{
		ACCEPT_MSG_ATN(port);
		return(TRUE);
	}
	return(TRUE);
}

#endif /* V302 */

#if defined(DOS)
void SendMsg(USHORT port, UCHAR message)
#else
void SendMsg(ULONG port, UCHAR message)
#endif
{
	while(!(RD_HARPOON(port+hp_scsisig) & SCSI_REQ))
	{
		if (! (RD_HARPOON(port+hp_scsisig) & SCSI_BSY))
		{

			WRW_HARPOON((port+hp_intstat), PHASE);
			return;
		}
	}

	WRW_HARPOON((port+hp_intstat), PHASE);
	if ((RD_HARPOON(port+hp_scsisig) & S_SCSI_PHZ) == S_MSGO_PH)
	{
		WRW_HARPOON((port+hp_intstat), (BUS_FREE | PHASE | XFER_CNT_0));


		WR_HARPOON(port+hp_portctrl_0, SCSI_BUS_EN);

		WR_HARPOON(port+hp_scsidata_0,message);

		WR_HARPOON(port+hp_scsisig, (SCSI_ACK + S_ILL_PH));

		ACCEPT_MSG(port);

		WR_HARPOON(port+hp_portctrl_0, 0x00);

		if ((message == SMABORT) || (message == SMDEV_RESET) ||
				(message == SMABORT_TAG) )
		{
			while(!(RDW_HARPOON((port+hp_intstat)) & (BUS_FREE | PHASE))) {}

			if (RDW_HARPOON((port+hp_intstat)) & BUS_FREE)
			{
			WRW_HARPOON((port+hp_intstat), BUS_FREE);
			}
		}
	}
}

/*---------------------------------------------------------------------
 *
 * Function: sdecm
 *
 * Description: Determine the proper responce to the message from the
 *              target device.
 *
 *---------------------------------------------------------------------*/
#if defined(DOS)
void sdecm(UCHAR message, USHORT port, UCHAR p_card)
#else
void sdecm(UCHAR message, ULONG port, UCHAR p_card)
#endif
{
	PSCCB currSCCB;
	PSCCBcard CurrCard;
	PSCCBMgr_tar_info currTar_Info;

	CurrCard = &BL_Card[p_card];
	currSCCB = CurrCard->currentSCCB;

	currTar_Info = &sccbMgrTbl[p_card][currSCCB->TargID];

	if (message == SMREST_DATA_PTR)
	{
		if (!(currSCCB->Sccb_XferState & F_NO_DATA_YET))
		{
			currSCCB->Sccb_ATC = currSCCB->Sccb_savedATC;

			hostDataXferRestart(currSCCB);
		}

		ACCEPT_MSG(port);
		WR_HARPOON(port+hp_autostart_1, (AUTO_IMMED+DISCONNECT_START));
	}

	else if (message == SMCMD_COMP)
	{


		if (currSCCB->Sccb_scsistat == SELECT_Q_ST)
		{
			currTar_Info->TarStatus &= ~(UCHAR)TAR_TAG_Q_MASK;
			currTar_Info->TarStatus |= (UCHAR)TAG_Q_REJECT;
		}

		ACCEPT_MSG(port);

	}

	else if ((message == SMNO_OP) || (message >= SMIDENT) 
			|| (message == SMINIT_RECOVERY) || (message == SMREL_RECOVERY))
	{

		ACCEPT_MSG(port);
		WR_HARPOON(port+hp_autostart_1, (AUTO_IMMED+DISCONNECT_START));
	}

	else if (message == SMREJECT)
	{

		if ((currSCCB->Sccb_scsistat == SELECT_SN_ST) ||
				(currSCCB->Sccb_scsistat == SELECT_WN_ST) ||
				((currTar_Info->TarStatus & TAR_SYNC_MASK) == SYNC_TRYING ) ||
				((currTar_Info->TarStatus & TAR_TAG_Q_MASK) == TAG_Q_TRYING ) )

		{
			WRW_HARPOON((port+hp_intstat), BUS_FREE);

			ACCEPT_MSG(port);


			while ((!(RD_HARPOON(port+hp_scsisig) & SCSI_REQ)) &&
				(!(RDW_HARPOON((port+hp_intstat)) & BUS_FREE))) {}

			if(currSCCB->Lun == 0x00)
			{
				if ((currSCCB->Sccb_scsistat == SELECT_SN_ST))
				{

					currTar_Info->TarStatus |= (UCHAR)SYNC_SUPPORTED;

					currTar_Info->TarEEValue &= ~EE_SYNC_MASK;
				}

#if defined(WIDE_SCSI)
				else if ((currSCCB->Sccb_scsistat == SELECT_WN_ST))
				{


					currTar_Info->TarStatus = (currTar_Info->TarStatus &
													~WIDE_ENABLED) | WIDE_NEGOCIATED;

					currTar_Info->TarEEValue &= ~EE_WIDE_SCSI;

				}
#endif

				else if ((currTar_Info->TarStatus & TAR_TAG_Q_MASK) == TAG_Q_TRYING )
				{
					currTar_Info->TarStatus = (currTar_Info->TarStatus &
													~(UCHAR)TAR_TAG_Q_MASK) | TAG_Q_REJECT;


					currSCCB->ControlByte &= ~F_USE_CMD_Q;
					CurrCard->discQCount--;
					CurrCard->discQ_Tbl[currSCCB->Sccb_tag] = NULL;
					currSCCB->Sccb_tag = 0x00;

				}
			}

			if (RDW_HARPOON((port+hp_intstat)) & BUS_FREE)
			{


				if(currSCCB->Lun == 0x00)
				{
					WRW_HARPOON((port+hp_intstat), BUS_FREE);
					CurrCard->globalFlags |= F_NEW_SCCB_CMD;
				}
			}

			else 
			{

				if((CurrCard->globalFlags & F_CONLUN_IO) &&
					((currTar_Info->TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING))
					currTar_Info->TarLUNBusy[currSCCB->Lun] = TRUE;
				else
					currTar_Info->TarLUNBusy[0] = TRUE;


				currSCCB->ControlByte &= ~(UCHAR)F_USE_CMD_Q;

				WR_HARPOON(port+hp_autostart_1, (AUTO_IMMED+DISCONNECT_START));

			}
		}

		else
		{
			ACCEPT_MSG(port);

			while ((!(RD_HARPOON(port+hp_scsisig) & SCSI_REQ)) &&
				(!(RDW_HARPOON((port+hp_intstat)) & BUS_FREE))) {}
	
			if (!(RDW_HARPOON((port+hp_intstat)) & BUS_FREE))
			{
				WR_HARPOON(port+hp_autostart_1, (AUTO_IMMED+DISCONNECT_START));
			}
		}
	}

	else if (message == SMEXT)
	{

		ACCEPT_MSG(port);
		shandem(port,p_card,currSCCB);
	}

	else if (message == SMIGNORWR)
	{

		ACCEPT_MSG(port);          /* ACK the RESIDUE MSG */

		message = sfm(port,currSCCB);

		if(currSCCB->Sccb_scsimsg != SMPARITY)
			ACCEPT_MSG(port);
		WR_HARPOON(port+hp_autostart_1, (AUTO_IMMED+DISCONNECT_START));
	}


	else
	{

		currSCCB->HostStatus = SCCB_PHASE_SEQUENCE_FAIL;
		currSCCB->Sccb_scsimsg = SMREJECT;

		ACCEPT_MSG_ATN(port);
		WR_HARPOON(port+hp_autostart_1, (AUTO_IMMED+DISCONNECT_START));
	}
}


/*---------------------------------------------------------------------
 *
 * Function: shandem
 *
 * Description: Decide what to do with the extended message.
 *
 *---------------------------------------------------------------------*/
#if defined(DOS)
void shandem(USHORT port, UCHAR p_card, PSCCB pCurrSCCB)
#else
void shandem(ULONG port, UCHAR p_card, PSCCB pCurrSCCB)
#endif
{
	UCHAR length,message;

	length = sfm(port,pCurrSCCB);
	if (length) 
	{

		ACCEPT_MSG(port);
		message = sfm(port,pCurrSCCB);
		if (message) 
		{

			if (message == SMSYNC) 
			{

				if (length == 0x03)
				{

					ACCEPT_MSG(port);
					stsyncn(port,p_card);
				}
				else 
				{

					pCurrSCCB->Sccb_scsimsg = SMREJECT;
					ACCEPT_MSG_ATN(port);
				}
			}
#if defined(WIDE_SCSI)
			else if (message == SMWDTR) 
			{

				if (length == 0x02)
				{

					ACCEPT_MSG(port);
					stwidn(port,p_card);
				}
				else 
				{

					pCurrSCCB->Sccb_scsimsg = SMREJECT;
					ACCEPT_MSG_ATN(port);

					WR_HARPOON(port+hp_autostart_1, (AUTO_IMMED+DISCONNECT_START));
				}
			}
#endif
			else 
			{

				pCurrSCCB->Sccb_scsimsg = SMREJECT;
				ACCEPT_MSG_ATN(port);

				WR_HARPOON(port+hp_autostart_1, (AUTO_IMMED+DISCONNECT_START));
			}
		}
		else
		{
			if(pCurrSCCB->Sccb_scsimsg != SMPARITY)
				ACCEPT_MSG(port);
			WR_HARPOON(port+hp_autostart_1, (AUTO_IMMED+DISCONNECT_START));
		}
	}else
	{
			if(pCurrSCCB->Sccb_scsimsg == SMPARITY)
				WR_HARPOON(port+hp_autostart_1, (AUTO_IMMED+DISCONNECT_START));
	}
}


/*---------------------------------------------------------------------
 *
 * Function: sisyncn
 *
 * Description: Read in a message byte from the SCSI bus, and check
 *              for a parity error.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
UCHAR sisyncn(USHORT port, UCHAR p_card, UCHAR syncFlag)
#else
UCHAR sisyncn(ULONG port, UCHAR p_card, UCHAR syncFlag)
#endif
{
   PSCCB currSCCB;
   PSCCBMgr_tar_info currTar_Info;

   currSCCB = BL_Card[p_card].currentSCCB;
   currTar_Info = &sccbMgrTbl[p_card][currSCCB->TargID];

   if (!((currTar_Info->TarStatus & TAR_SYNC_MASK) == SYNC_TRYING)) {


      WRW_HARPOON((port+ID_MSG_STRT),
                 (MPM_OP+AMSG_OUT+(currSCCB->Sccb_idmsg & ~(UCHAR)DISC_PRIV)));

      WRW_HARPOON((port+ID_MSG_STRT+2),BRH_OP+ALWAYS+CMDPZ);

      WRW_HARPOON((port+SYNC_MSGS+0), (MPM_OP+AMSG_OUT+SMEXT ));
      WRW_HARPOON((port+SYNC_MSGS+2), (MPM_OP+AMSG_OUT+0x03  ));
      WRW_HARPOON((port+SYNC_MSGS+4), (MPM_OP+AMSG_OUT+SMSYNC));


      if ((currTar_Info->TarEEValue & EE_SYNC_MASK) == EE_SYNC_20MB)

	 WRW_HARPOON((port+SYNC_MSGS+6), (MPM_OP+AMSG_OUT+ 12));

      else if ((currTar_Info->TarEEValue & EE_SYNC_MASK) == EE_SYNC_10MB)

	 WRW_HARPOON((port+SYNC_MSGS+6), (MPM_OP+AMSG_OUT+ 25));

      else if ((currTar_Info->TarEEValue & EE_SYNC_MASK) == EE_SYNC_5MB)

	 WRW_HARPOON((port+SYNC_MSGS+6), (MPM_OP+AMSG_OUT+ 50));

      else
	 WRW_HARPOON((port+SYNC_MSGS+6), (MPM_OP+AMSG_OUT+ 00));


      WRW_HARPOON((port+SYNC_MSGS+8), (RAT_OP                ));
      WRW_HARPOON((port+SYNC_MSGS+10),(MPM_OP+AMSG_OUT+DEFAULT_OFFSET));
      WRW_HARPOON((port+SYNC_MSGS+12),(BRH_OP+ALWAYS+NP      ));


		if(syncFlag == FALSE)
		{
		   WR_HARPOON(port+hp_autostart_3, (SELECT+SELCHK_STRT));
	      currTar_Info->TarStatus = ((currTar_Info->TarStatus &
   	      ~(UCHAR)TAR_SYNC_MASK) | (UCHAR)SYNC_TRYING);
		}
		else
		{
		   WR_HARPOON(port+hp_autostart_3, (AUTO_IMMED + CMD_ONLY_STRT));
		}


      return(TRUE);
      }

   else {

      currTar_Info->TarStatus |=	 (UCHAR)SYNC_SUPPORTED;
      currTar_Info->TarEEValue &= ~EE_SYNC_MASK;
      return(FALSE);
      }
}



/*---------------------------------------------------------------------
 *
 * Function: stsyncn
 *
 * Description: The has sent us a Sync Nego message so handle it as
 *              necessary.
 *
 *---------------------------------------------------------------------*/
#if defined(DOS)
void stsyncn(USHORT port, UCHAR p_card)
#else
void stsyncn(ULONG port, UCHAR p_card)
#endif
{
   UCHAR sync_msg,offset,sync_reg,our_sync_msg;
   PSCCB currSCCB;
   PSCCBMgr_tar_info currTar_Info;

   currSCCB = BL_Card[p_card].currentSCCB;
   currTar_Info = &sccbMgrTbl[p_card][currSCCB->TargID];

   sync_msg = sfm(port,currSCCB);

	if((sync_msg == 0x00) && (currSCCB->Sccb_scsimsg == SMPARITY))
	{
		WR_HARPOON(port+hp_autostart_1, (AUTO_IMMED+DISCONNECT_START));
		return;
	}

   ACCEPT_MSG(port);


   offset = sfm(port,currSCCB);

	if((offset == 0x00) && (currSCCB->Sccb_scsimsg == SMPARITY))
	{
		WR_HARPOON(port+hp_autostart_1, (AUTO_IMMED+DISCONNECT_START));
		return;
	}

   if ((currTar_Info->TarEEValue & EE_SYNC_MASK) == EE_SYNC_20MB)

      our_sync_msg = 12;              /* Setup our Message to 20mb/s */

   else if ((currTar_Info->TarEEValue & EE_SYNC_MASK) == EE_SYNC_10MB)

      our_sync_msg = 25;              /* Setup our Message to 10mb/s */

   else if ((currTar_Info->TarEEValue & EE_SYNC_MASK) == EE_SYNC_5MB)

      our_sync_msg = 50;              /* Setup our Message to 5mb/s */
   else

      our_sync_msg = 0;               /* Message = Async */

   if (sync_msg < our_sync_msg) {
      sync_msg = our_sync_msg;    /*if faster, then set to max. */
      }

   if (offset == ASYNC)
      sync_msg = ASYNC;

   if (offset > MAX_OFFSET)
      offset = MAX_OFFSET;

   sync_reg = 0x00;

   if (sync_msg > 12)

      sync_reg = 0x20;        /* Use 10MB/s */

   if (sync_msg > 25)

      sync_reg = 0x40;        /* Use 6.6MB/s */

   if (sync_msg > 38)

      sync_reg = 0x60;        /* Use 5MB/s */

   if (sync_msg > 50)

      sync_reg = 0x80;        /* Use 4MB/s */

   if (sync_msg > 62)

      sync_reg = 0xA0;        /* Use 3.33MB/s */

   if (sync_msg > 75)

      sync_reg = 0xC0;        /* Use 2.85MB/s */

   if (sync_msg > 87)

      sync_reg = 0xE0;        /* Use 2.5MB/s */

   if (sync_msg > 100) {

      sync_reg = 0x00;        /* Use ASYNC */
      offset = 0x00;
      }


#if defined(WIDE_SCSI)
   if (currTar_Info->TarStatus & WIDE_ENABLED)

      sync_reg |= offset;

   else

      sync_reg |= (offset | NARROW_SCSI);

#else
   sync_reg |= (offset | NARROW_SCSI);
#endif

   sssyncv(port,currSCCB->TargID,sync_reg,currTar_Info);


   if (currSCCB->Sccb_scsistat == SELECT_SN_ST) {


      ACCEPT_MSG(port);

      currTar_Info->TarStatus = ((currTar_Info->TarStatus &
         ~(UCHAR)TAR_SYNC_MASK) | (UCHAR)SYNC_SUPPORTED);

      WR_HARPOON(port+hp_autostart_1, (AUTO_IMMED+DISCONNECT_START));
      }

   else {


      ACCEPT_MSG_ATN(port);

      sisyncr(port,sync_msg,offset);

      currTar_Info->TarStatus = ((currTar_Info->TarStatus &
         ~(UCHAR)TAR_SYNC_MASK) | (UCHAR)SYNC_SUPPORTED);
      }
}


/*---------------------------------------------------------------------
 *
 * Function: sisyncr
 *
 * Description: Answer the targets sync message.
 *
 *---------------------------------------------------------------------*/
#if defined(DOS)
void sisyncr(USHORT port,UCHAR sync_pulse, UCHAR offset)
#else
void sisyncr(ULONG port,UCHAR sync_pulse, UCHAR offset)
#endif
{
   ARAM_ACCESS(port);
   WRW_HARPOON((port+SYNC_MSGS+0), (MPM_OP+AMSG_OUT+SMEXT ));
   WRW_HARPOON((port+SYNC_MSGS+2), (MPM_OP+AMSG_OUT+0x03  ));
   WRW_HARPOON((port+SYNC_MSGS+4), (MPM_OP+AMSG_OUT+SMSYNC));
   WRW_HARPOON((port+SYNC_MSGS+6), (MPM_OP+AMSG_OUT+sync_pulse));
   WRW_HARPOON((port+SYNC_MSGS+8), (RAT_OP                ));
   WRW_HARPOON((port+SYNC_MSGS+10),(MPM_OP+AMSG_OUT+offset));
   WRW_HARPOON((port+SYNC_MSGS+12),(BRH_OP+ALWAYS+NP      ));
   SGRAM_ACCESS(port);

   WR_HARPOON(port+hp_portctrl_0, SCSI_PORT);
   WRW_HARPOON((port+hp_intstat), CLR_ALL_INT_1);

   WR_HARPOON(port+hp_autostart_3, (AUTO_IMMED+CMD_ONLY_STRT));

   while (!(RDW_HARPOON((port+hp_intstat)) & (BUS_FREE | AUTO_INT))) {}
}



#if defined(WIDE_SCSI)

/*---------------------------------------------------------------------
 *
 * Function: siwidn
 *
 * Description: Read in a message byte from the SCSI bus, and check
 *              for a parity error.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
UCHAR siwidn(USHORT port, UCHAR p_card)
#else
UCHAR siwidn(ULONG port, UCHAR p_card)
#endif
{
   PSCCB currSCCB;
   PSCCBMgr_tar_info currTar_Info;

   currSCCB = BL_Card[p_card].currentSCCB;
   currTar_Info = &sccbMgrTbl[p_card][currSCCB->TargID];

   if (!((currTar_Info->TarStatus & TAR_WIDE_MASK) == WIDE_NEGOCIATED)) {


      WRW_HARPOON((port+ID_MSG_STRT),
	              (MPM_OP+AMSG_OUT+(currSCCB->Sccb_idmsg & ~(UCHAR)DISC_PRIV)));

      WRW_HARPOON((port+ID_MSG_STRT+2),BRH_OP+ALWAYS+CMDPZ);

      WRW_HARPOON((port+SYNC_MSGS+0), (MPM_OP+AMSG_OUT+SMEXT ));
      WRW_HARPOON((port+SYNC_MSGS+2), (MPM_OP+AMSG_OUT+0x02  ));
      WRW_HARPOON((port+SYNC_MSGS+4), (MPM_OP+AMSG_OUT+SMWDTR));
      WRW_HARPOON((port+SYNC_MSGS+6), (RAT_OP                ));
      WRW_HARPOON((port+SYNC_MSGS+8), (MPM_OP+AMSG_OUT+ SM16BIT));
      WRW_HARPOON((port+SYNC_MSGS+10),(BRH_OP+ALWAYS+NP      ));

      WR_HARPOON(port+hp_autostart_3, (SELECT+SELCHK_STRT));


      currTar_Info->TarStatus = ((currTar_Info->TarStatus &
         ~(UCHAR)TAR_WIDE_MASK) | (UCHAR)WIDE_ENABLED);

      return(TRUE);
      }

   else {

      currTar_Info->TarStatus = ((currTar_Info->TarStatus &
               ~(UCHAR)TAR_WIDE_MASK) | WIDE_NEGOCIATED);

      currTar_Info->TarEEValue &= ~EE_WIDE_SCSI;
      return(FALSE);
      }
}



/*---------------------------------------------------------------------
 *
 * Function: stwidn
 *
 * Description: The has sent us a Wide Nego message so handle it as
 *              necessary.
 *
 *---------------------------------------------------------------------*/
#if defined(DOS)
void stwidn(USHORT port, UCHAR p_card)
#else
void stwidn(ULONG port, UCHAR p_card)
#endif
{
   UCHAR width;
   PSCCB currSCCB;
   PSCCBMgr_tar_info currTar_Info;

   currSCCB = BL_Card[p_card].currentSCCB;
   currTar_Info = &sccbMgrTbl[p_card][currSCCB->TargID];

   width = sfm(port,currSCCB);

	if((width == 0x00) && (currSCCB->Sccb_scsimsg == SMPARITY))
	{
		WR_HARPOON(port+hp_autostart_1, (AUTO_IMMED+DISCONNECT_START));
		return;
	}


   if (!(currTar_Info->TarEEValue & EE_WIDE_SCSI))
      width = 0;

   if (width) {
      currTar_Info->TarStatus |= WIDE_ENABLED;
      width = 0;
      }
   else {
      width = NARROW_SCSI;
      currTar_Info->TarStatus &= ~WIDE_ENABLED;
      }


   sssyncv(port,currSCCB->TargID,width,currTar_Info);


   if (currSCCB->Sccb_scsistat == SELECT_WN_ST)
	{



      currTar_Info->TarStatus |=	 WIDE_NEGOCIATED;

	   if (!((currTar_Info->TarStatus & TAR_SYNC_MASK) == SYNC_SUPPORTED))
		{
	      ACCEPT_MSG_ATN(port);
		   ARAM_ACCESS(port);
	     	sisyncn(port,p_card, TRUE);
	      currSCCB->Sccb_scsistat = SELECT_SN_ST;
		   SGRAM_ACCESS(port);
		}
		else
		{
	      ACCEPT_MSG(port);
  		   WR_HARPOON(port+hp_autostart_1, (AUTO_IMMED+DISCONNECT_START));
		}
   }

   else {


      ACCEPT_MSG_ATN(port);

      if (currTar_Info->TarEEValue & EE_WIDE_SCSI)
      	 width = SM16BIT;
      else
      	 width = SM8BIT;

      siwidr(port,width);

      currTar_Info->TarStatus |= (WIDE_NEGOCIATED | WIDE_ENABLED);
      }
}


/*---------------------------------------------------------------------
 *
 * Function: siwidr
 *
 * Description: Answer the targets Wide nego message.
 *
 *---------------------------------------------------------------------*/
#if defined(DOS)
void siwidr(USHORT port, UCHAR width)
#else
void siwidr(ULONG port, UCHAR width)
#endif
{
   ARAM_ACCESS(port);
   WRW_HARPOON((port+SYNC_MSGS+0), (MPM_OP+AMSG_OUT+SMEXT ));
   WRW_HARPOON((port+SYNC_MSGS+2), (MPM_OP+AMSG_OUT+0x02  ));
   WRW_HARPOON((port+SYNC_MSGS+4), (MPM_OP+AMSG_OUT+SMWDTR));
   WRW_HARPOON((port+SYNC_MSGS+6), (RAT_OP                ));
   WRW_HARPOON((port+SYNC_MSGS+8),(MPM_OP+AMSG_OUT+width));
   WRW_HARPOON((port+SYNC_MSGS+10),(BRH_OP+ALWAYS+NP      ));
   SGRAM_ACCESS(port);

   WR_HARPOON(port+hp_portctrl_0, SCSI_PORT);
   WRW_HARPOON((port+hp_intstat), CLR_ALL_INT_1);

   WR_HARPOON(port+hp_autostart_3, (AUTO_IMMED+CMD_ONLY_STRT));

   while (!(RDW_HARPOON((port+hp_intstat)) & (BUS_FREE | AUTO_INT))) {}
}

#endif



/*---------------------------------------------------------------------
 *
 * Function: sssyncv
 *
 * Description: Write the desired value to the Sync Register for the
 *              ID specified.
 *
 *---------------------------------------------------------------------*/
#if defined(DOS)
void sssyncv(USHORT p_port, UCHAR p_id, UCHAR p_sync_value,PSCCBMgr_tar_info currTar_Info)
#else
void sssyncv(ULONG p_port, UCHAR p_id, UCHAR p_sync_value,PSCCBMgr_tar_info currTar_Info)
#endif
{
   UCHAR index;

   index = p_id;

   switch (index) {

      case 0:
	 index = 12;             /* hp_synctarg_0 */
	 break;
      case 1:
	 index = 13;             /* hp_synctarg_1 */
	 break;
      case 2:
	 index = 14;             /* hp_synctarg_2 */
	 break;
      case 3:
	 index = 15;             /* hp_synctarg_3 */
	 break;
      case 4:
	 index = 8;              /* hp_synctarg_4 */
	 break;
      case 5:
	 index = 9;              /* hp_synctarg_5 */
	 break;
      case 6:
	 index = 10;             /* hp_synctarg_6 */
	 break;
      case 7:
	 index = 11;             /* hp_synctarg_7 */
	 break;
      case 8:
	 index = 4;              /* hp_synctarg_8 */
	 break;
      case 9:
	 index = 5;              /* hp_synctarg_9 */
	 break;
      case 10:
	 index = 6;              /* hp_synctarg_10 */
	 break;
      case 11:
	 index = 7;              /* hp_synctarg_11 */
	 break;
      case 12:
	 index = 0;              /* hp_synctarg_12 */
	 break;
      case 13:
	 index = 1;              /* hp_synctarg_13 */
	 break;
      case 14:
	 index = 2;              /* hp_synctarg_14 */
	 break;
      case 15:
	 index = 3;              /* hp_synctarg_15 */

      }

   WR_HARPOON(p_port+hp_synctarg_base+index, p_sync_value);

	currTar_Info->TarSyncCtrl = p_sync_value;
}


/*---------------------------------------------------------------------
 *
 * Function: sresb
 *
 * Description: Reset the desired card's SCSI bus.
 *
 *---------------------------------------------------------------------*/
#if defined(DOS)
void sresb(USHORT port, UCHAR p_card)
#else
void sresb(ULONG port, UCHAR p_card)
#endif
{
   UCHAR scsiID, i;

   PSCCBMgr_tar_info currTar_Info;

   WR_HARPOON(port+hp_page_ctrl,
      (RD_HARPOON(port+hp_page_ctrl) | G_INT_DISABLE));
   WRW_HARPOON((port+hp_intstat), CLR_ALL_INT);

   WR_HARPOON(port+hp_scsictrl_0, SCSI_RST);

   scsiID = RD_HARPOON(port+hp_seltimeout);
   WR_HARPOON(port+hp_seltimeout,TO_5ms);
   WRW_HARPOON((port+hp_intstat), TIMEOUT);

   WR_HARPOON(port+hp_portctrl_0,(SCSI_PORT | START_TO));

   while (!(RDW_HARPOON((port+hp_intstat)) & TIMEOUT)) {}

   WR_HARPOON(port+hp_seltimeout,scsiID);

   WR_HARPOON(port+hp_scsictrl_0, ENA_SCAM_SEL);

   Wait(port, TO_5ms);

   WRW_HARPOON((port+hp_intstat), CLR_ALL_INT);

   WR_HARPOON(port+hp_int_mask, (RD_HARPOON(port+hp_int_mask) | 0x00));

   for (scsiID = 0; scsiID < MAX_SCSI_TAR; scsiID++)
      {
      currTar_Info = &sccbMgrTbl[p_card][scsiID];

      if (currTar_Info->TarEEValue & EE_SYNC_MASK)
         {
	      	currTar_Info->TarSyncCtrl = 0;
	      	currTar_Info->TarStatus &= ~TAR_SYNC_MASK;
	      }

      if (currTar_Info->TarEEValue & EE_WIDE_SCSI)
         {
      	currTar_Info->TarStatus &= ~TAR_WIDE_MASK;
      	}

      sssyncv(port, scsiID, NARROW_SCSI,currTar_Info);

      SccbMgrTableInitTarget(p_card, scsiID);
      }

   BL_Card[p_card].scanIndex = 0x00;
   BL_Card[p_card].currentSCCB = NULL;
   BL_Card[p_card].globalFlags &= ~(F_TAG_STARTED | F_HOST_XFER_ACT 
													| F_NEW_SCCB_CMD);
   BL_Card[p_card].cmdCounter  = 0x00;
	BL_Card[p_card].discQCount = 0x00;
   BL_Card[p_card].tagQ_Lst = 0x01; 

	for(i = 0; i < QUEUE_DEPTH; i++)
		BL_Card[p_card].discQ_Tbl[i] = NULL;

   WR_HARPOON(port+hp_page_ctrl,
      (RD_HARPOON(port+hp_page_ctrl) & ~G_INT_DISABLE));

}

/*---------------------------------------------------------------------
 *
 * Function: ssenss
 *
 * Description: Setup for the Auto Sense command.
 *
 *---------------------------------------------------------------------*/
void ssenss(PSCCBcard pCurrCard)
{
   UCHAR i;
   PSCCB currSCCB;

   currSCCB = pCurrCard->currentSCCB;


   currSCCB->Save_CdbLen = currSCCB->CdbLength;

   for (i = 0; i < 6; i++) {

      currSCCB->Save_Cdb[i] = currSCCB->Cdb[i];
      }

   currSCCB->CdbLength = SIX_BYTE_CMD;
   currSCCB->Cdb[0]    = SCSI_REQUEST_SENSE;
   currSCCB->Cdb[1]    = currSCCB->Cdb[1] & (UCHAR)0xE0; /*Keep LUN. */
   currSCCB->Cdb[2]    = 0x00;
   currSCCB->Cdb[3]    = 0x00;
   currSCCB->Cdb[4]    = currSCCB->RequestSenseLength;
   currSCCB->Cdb[5]    = 0x00;

   currSCCB->Sccb_XferCnt = (unsigned long)currSCCB->RequestSenseLength;

   currSCCB->Sccb_ATC = 0x00;

   currSCCB->Sccb_XferState |= F_AUTO_SENSE;

   currSCCB->Sccb_XferState &= ~F_SG_XFER;

   currSCCB->Sccb_idmsg = currSCCB->Sccb_idmsg & ~(UCHAR)DISC_PRIV;

   currSCCB->ControlByte = 0x00;

   currSCCB->Sccb_MGRFlags &= F_STATUSLOADED;
}



/*---------------------------------------------------------------------
 *
 * Function: sxfrp
 *
 * Description: Transfer data into the bit bucket until the device
 *              decides to switch phase.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void sxfrp(USHORT p_port, UCHAR p_card)
#else
void sxfrp(ULONG p_port, UCHAR p_card)
#endif
{
   UCHAR curr_phz;


   DISABLE_AUTO(p_port);

   if (BL_Card[p_card].globalFlags & F_HOST_XFER_ACT) {

      hostDataXferAbort(p_port,p_card,BL_Card[p_card].currentSCCB);

      }

   /* If the Automation handled the end of the transfer then do not
      match the phase or we will get out of sync with the ISR.       */

   if (RDW_HARPOON((p_port+hp_intstat)) & (BUS_FREE | XFER_CNT_0 | AUTO_INT))
      return;

   WR_HARPOON(p_port+hp_xfercnt_0, 0x00);

   curr_phz = RD_HARPOON(p_port+hp_scsisig) & (UCHAR)S_SCSI_PHZ;

   WRW_HARPOON((p_port+hp_intstat), XFER_CNT_0);


   WR_HARPOON(p_port+hp_scsisig, curr_phz);

   while ( !(RDW_HARPOON((p_port+hp_intstat)) & (BUS_FREE | RESET)) &&
      (curr_phz == (RD_HARPOON(p_port+hp_scsisig) & (UCHAR)S_SCSI_PHZ)) )
      {
      if (curr_phz & (UCHAR)SCSI_IOBIT)
         {
      	WR_HARPOON(p_port+hp_portctrl_0, (SCSI_PORT | HOST_PORT | SCSI_INBIT));

	      if (!(RD_HARPOON(p_port+hp_xferstat) & FIFO_EMPTY))
            {
	         RD_HARPOON(p_port+hp_fifodata_0);
	         }
	      }
      else
         {
      	WR_HARPOON(p_port+hp_portctrl_0, (SCSI_PORT | HOST_PORT | HOST_WRT));
   	   if (RD_HARPOON(p_port+hp_xferstat) & FIFO_EMPTY)
            {
	         WR_HARPOON(p_port+hp_fifodata_0,0xFA);
	         }
	      }
      } /* End of While loop for padding data I/O phase */

      while ( !(RDW_HARPOON((p_port+hp_intstat)) & (BUS_FREE | RESET)))
         {
         if (RD_HARPOON(p_port+hp_scsisig) & SCSI_REQ)
      	   break;
         }

      WR_HARPOON(p_port+hp_portctrl_0, (SCSI_PORT | HOST_PORT | SCSI_INBIT));
      while (!(RD_HARPOON(p_port+hp_xferstat) & FIFO_EMPTY))
         {
         RD_HARPOON(p_port+hp_fifodata_0);
         }

      if ( !(RDW_HARPOON((p_port+hp_intstat)) & (BUS_FREE | RESET)))
         {
         WR_HARPOON(p_port+hp_autostart_0, (AUTO_IMMED+DISCONNECT_START));
         while (!(RDW_HARPOON((p_port+hp_intstat)) & AUTO_INT)) {}

         if (RDW_HARPOON((p_port+hp_intstat)) & (ICMD_COMP | ITAR_DISC))
   	   while (!(RDW_HARPOON((p_port+hp_intstat)) & (BUS_FREE | RSEL))) ;
         }
}


/*---------------------------------------------------------------------
 *
 * Function: schkdd
 *
 * Description: Make sure data has been flushed from both FIFOs and abort
 *              the operations if necessary.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void schkdd(USHORT port, UCHAR p_card)
#else
void schkdd(ULONG port, UCHAR p_card)
#endif
{
   USHORT TimeOutLoop;
	UCHAR sPhase;

   PSCCB currSCCB;

   currSCCB = BL_Card[p_card].currentSCCB;


   if ((currSCCB->Sccb_scsistat != DATA_OUT_ST) &&
       (currSCCB->Sccb_scsistat != DATA_IN_ST)) {
      return;
      }



   if (currSCCB->Sccb_XferState & F_ODD_BALL_CNT)
      {

      currSCCB->Sccb_ATC += (currSCCB->Sccb_XferCnt-1);

      currSCCB->Sccb_XferCnt = 1;

      currSCCB->Sccb_XferState &= ~F_ODD_BALL_CNT;
      WRW_HARPOON((port+hp_fiforead), (USHORT) 0x00);
      WR_HARPOON(port+hp_xferstat, 0x00);
      }

   else
      {

      currSCCB->Sccb_ATC += currSCCB->Sccb_XferCnt;

      currSCCB->Sccb_XferCnt = 0;
      }

   if ((RDW_HARPOON((port+hp_intstat)) & PARITY) &&
      (currSCCB->HostStatus == SCCB_COMPLETE)) {

      currSCCB->HostStatus = SCCB_PARITY_ERR;
      WRW_HARPOON((port+hp_intstat), PARITY);
      }


   hostDataXferAbort(port,p_card,currSCCB);


   while (RD_HARPOON(port+hp_scsisig) & SCSI_ACK) {}

   TimeOutLoop = 0;

   while(RD_HARPOON(port+hp_xferstat) & FIFO_EMPTY)
      {
      if (RDW_HARPOON((port+hp_intstat)) & BUS_FREE) {
	      return;
   	   }
      if (RD_HARPOON(port+hp_offsetctr) & (UCHAR)0x1F) {
	      break;
   	   }
      if (RDW_HARPOON((port+hp_intstat)) & RESET) {
	      return;
   	   }
      if ((RD_HARPOON(port+hp_scsisig) & SCSI_REQ) || (TimeOutLoop++>0x3000) )
   	   break;
      }

	sPhase = RD_HARPOON(port+hp_scsisig) & (SCSI_BSY | S_SCSI_PHZ);
   if ((!(RD_HARPOON(port+hp_xferstat) & FIFO_EMPTY))                     ||
      (RD_HARPOON(port+hp_offsetctr) & (UCHAR)0x1F)                       ||
      (sPhase == (SCSI_BSY | S_DATAO_PH)) ||
      (sPhase == (SCSI_BSY | S_DATAI_PH)))
      {

	   WR_HARPOON(port+hp_portctrl_0, SCSI_PORT);

	   if (!(currSCCB->Sccb_XferState & F_ALL_XFERRED))
         {
	      if (currSCCB->Sccb_XferState & F_HOST_XFER_DIR) {
	         phaseDataIn(port,p_card);
	      	}

	   	else {
	       phaseDataOut(port,p_card);
	       	}
	   	}
		else
      	{
	   	sxfrp(port,p_card);
	   	if (!(RDW_HARPOON((port+hp_intstat)) &
		      (BUS_FREE | ICMD_COMP | ITAR_DISC | RESET)))
         {
   		WRW_HARPOON((port+hp_intstat), AUTO_INT);
		   phaseDecode(port,p_card);
		   }
	   }

   }

   else {
      WR_HARPOON(port+hp_portctrl_0, 0x00);
      }
}


/*---------------------------------------------------------------------
 *
 * Function: sinits
 *
 * Description: Setup SCCB manager fields in this SCCB.
 *
 *---------------------------------------------------------------------*/

void sinits(PSCCB p_sccb, UCHAR p_card)
{
   PSCCBMgr_tar_info currTar_Info;

	if((p_sccb->TargID > MAX_SCSI_TAR) || (p_sccb->Lun > MAX_LUN))
	{
		return;
	}
   currTar_Info = &sccbMgrTbl[p_card][p_sccb->TargID];

   p_sccb->Sccb_XferState     = 0x00;
   p_sccb->Sccb_XferCnt       = p_sccb->DataLength;

   if ((p_sccb->OperationCode == SCATTER_GATHER_COMMAND) ||
      (p_sccb->OperationCode == RESIDUAL_SG_COMMAND)) {

      p_sccb->Sccb_SGoffset   = 0;
      p_sccb->Sccb_XferState  = F_SG_XFER;
      p_sccb->Sccb_XferCnt    = 0x00;
      }

   if (p_sccb->DataLength == 0x00)

      p_sccb->Sccb_XferState |= F_ALL_XFERRED;

   if (p_sccb->ControlByte & F_USE_CMD_Q)
      {
      if ((currTar_Info->TarStatus & TAR_TAG_Q_MASK) == TAG_Q_REJECT)
         p_sccb->ControlByte &= ~F_USE_CMD_Q;

      else
	      currTar_Info->TarStatus |= TAG_Q_TRYING;
      }

/*      For !single SCSI device in system  & device allow Disconnect
	or command is tag_q type then send Cmd with Disconnect Enable
	else send Cmd with Disconnect Disable */

/*
   if (((!(BL_Card[p_card].globalFlags & F_SINGLE_DEVICE)) &&
      (currTar_Info->TarStatus & TAR_ALLOW_DISC)) ||
      (currTar_Info->TarStatus & TAG_Q_TRYING)) {
*/
   if ((currTar_Info->TarStatus & TAR_ALLOW_DISC) ||
      (currTar_Info->TarStatus & TAG_Q_TRYING)) {
      p_sccb->Sccb_idmsg      = (UCHAR)(SMIDENT | DISC_PRIV) | p_sccb->Lun;
      }

   else {

      p_sccb->Sccb_idmsg      = (UCHAR)SMIDENT | p_sccb->Lun;
      }

   p_sccb->HostStatus         = 0x00;
   p_sccb->TargetStatus       = 0x00;
   p_sccb->Sccb_tag           = 0x00;
   p_sccb->Sccb_MGRFlags      = 0x00;
   p_sccb->Sccb_sgseg         = 0x00;
   p_sccb->Sccb_ATC           = 0x00;
   p_sccb->Sccb_savedATC      = 0x00;
/*
   p_sccb->SccbVirtDataPtr    = 0x00;
   p_sccb->Sccb_forwardlink   = NULL;
   p_sccb->Sccb_backlink      = NULL;
 */
   p_sccb->Sccb_scsistat      = BUS_FREE_ST;
   p_sccb->SccbStatus         = SCCB_IN_PROCESS;
   p_sccb->Sccb_scsimsg       = SMNO_OP;

}


#ident "$Id: phase.c 1.11 1997/01/31 02:08:49 mohan Exp $"
/*----------------------------------------------------------------------
 *
 *
 *   Copyright 1995-1996 by Mylex Corporation.  All Rights Reserved
 *
 *   This file is available under both the GNU General Public License
 *   and a BSD-style copyright; see LICENSE.FlashPoint for details.
 *
 *   $Workfile:   phase.c  $
 *
 *   Description:  Functions to initially handle the SCSI bus phase when
 *                 the target asserts request (and the automation is not
 *                 enabled to handle the situation).
 *
 *   $Date: 1997/01/31 02:08:49 $
 *
 *   $Revision: 1.11 $
 *
 *----------------------------------------------------------------------*/

/*#include <globals.h>*/

#if (FW_TYPE==_UCB_MGR_)
	/*#include <budi.h>*/
#endif

/*#include <sccbmgr.h>*/
/*#include <blx30.h>*/
/*#include <target.h>*/
/*#include <scsi2.h>*/
/*#include <harpoon.h>*/


/*
extern SCCBCARD BL_Card[MAX_CARDS];
extern SCCBMGR_TAR_INFO sccbMgrTbl[MAX_CARDS][MAX_SCSI_TAR];

#if defined(OS2)
   extern void (far *s_PhaseTbl[8]) (ULONG, UCHAR);
#else
   #if defined(DOS)
      extern void (*s_PhaseTbl[8]) (USHORT, UCHAR);
   #else
      extern void (*s_PhaseTbl[8]) (ULONG, UCHAR);
   #endif
#endif
*/

/*---------------------------------------------------------------------
 *
 * Function: Phase Decode
 *
 * Description: Determine the phase and call the appropriate function.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void phaseDecode(USHORT p_port, UCHAR p_card)
#else
void phaseDecode(ULONG p_port, UCHAR p_card)
#endif
{
   unsigned char phase_ref;
#if defined(OS2)
   void (far *phase) (ULONG, UCHAR);
#else
   #if defined(DOS)
      void (*phase) (USHORT, UCHAR);
   #else
      void (*phase) (ULONG, UCHAR);
   #endif
#endif


   DISABLE_AUTO(p_port);

   phase_ref = (UCHAR) (RD_HARPOON(p_port+hp_scsisig) & S_SCSI_PHZ);

   phase = s_PhaseTbl[phase_ref];

   (*phase)(p_port, p_card);           /* Call the correct phase func */
}



/*---------------------------------------------------------------------
 *
 * Function: Data Out Phase
 *
 * Description: Start up both the BusMaster and Xbow.
 *
 *---------------------------------------------------------------------*/

#if defined(OS2)
void far phaseDataOut(ULONG port, UCHAR p_card)
#else
#if defined(DOS)
void phaseDataOut(USHORT port, UCHAR p_card)
#else
void phaseDataOut(ULONG port, UCHAR p_card)
#endif
#endif
{

   PSCCB currSCCB;

   currSCCB = BL_Card[p_card].currentSCCB;
   if (currSCCB == NULL)
      {
      return;  /* Exit if No SCCB record */
      }

   currSCCB->Sccb_scsistat = DATA_OUT_ST;
   currSCCB->Sccb_XferState &= ~(F_HOST_XFER_DIR | F_NO_DATA_YET);

   WR_HARPOON(port+hp_portctrl_0, SCSI_PORT);

   WRW_HARPOON((port+hp_intstat), XFER_CNT_0);

   WR_HARPOON(port+hp_autostart_0, (END_DATA+END_DATA_START));

   dataXferProcessor(port, &BL_Card[p_card]);

#if defined(NOBUGBUG)
   if (RDW_HARPOON((port+hp_intstat)) & XFER_CNT_0)
      WRW_HARPOON((port+hp_intstat), XFER_CNT_0);

#endif


   if (currSCCB->Sccb_XferCnt == 0) {


      if ((currSCCB->ControlByte & SCCB_DATA_XFER_OUT) &&
	 (currSCCB->HostStatus == SCCB_COMPLETE))
	 currSCCB->HostStatus = SCCB_DATA_OVER_RUN;

      sxfrp(port,p_card);
      if (!(RDW_HARPOON((port+hp_intstat)) & (BUS_FREE | RESET)))
	    phaseDecode(port,p_card);
      }
}


/*---------------------------------------------------------------------
 *
 * Function: Data In Phase
 *
 * Description: Startup the BusMaster and the XBOW.
 *
 *---------------------------------------------------------------------*/

#if defined(OS2)
void far phaseDataIn(ULONG port, UCHAR p_card)
#else
#if defined(DOS)
void phaseDataIn(USHORT port, UCHAR p_card)
#else
void phaseDataIn(ULONG port, UCHAR p_card)
#endif
#endif
{

   PSCCB currSCCB;

   currSCCB = BL_Card[p_card].currentSCCB;

   if (currSCCB == NULL)
      {
      return;  /* Exit if No SCCB record */
      }


   currSCCB->Sccb_scsistat = DATA_IN_ST;
   currSCCB->Sccb_XferState |= F_HOST_XFER_DIR;
   currSCCB->Sccb_XferState &= ~F_NO_DATA_YET;

   WR_HARPOON(port+hp_portctrl_0, SCSI_PORT);

   WRW_HARPOON((port+hp_intstat), XFER_CNT_0);

   WR_HARPOON(port+hp_autostart_0, (END_DATA+END_DATA_START));

   dataXferProcessor(port, &BL_Card[p_card]);

   if (currSCCB->Sccb_XferCnt == 0) {


      if ((currSCCB->ControlByte & SCCB_DATA_XFER_IN) &&
	 (currSCCB->HostStatus == SCCB_COMPLETE))
	 currSCCB->HostStatus = SCCB_DATA_OVER_RUN;

      sxfrp(port,p_card);
      if (!(RDW_HARPOON((port+hp_intstat)) & (BUS_FREE | RESET)))
	    phaseDecode(port,p_card);

      }
}

/*---------------------------------------------------------------------
 *
 * Function: Command Phase
 *
 * Description: Load the CDB into the automation and start it up.
 *
 *---------------------------------------------------------------------*/

#if defined(OS2)
void far phaseCommand(ULONG p_port, UCHAR p_card)
#else
#if defined(DOS)
void phaseCommand(USHORT p_port, UCHAR p_card)
#else
void phaseCommand(ULONG p_port, UCHAR p_card)
#endif
#endif
{
   PSCCB currSCCB;
#if defined(DOS)
   USHORT cdb_reg;
#else
   ULONG cdb_reg;
#endif
   UCHAR i;

   currSCCB = BL_Card[p_card].currentSCCB;

   if (currSCCB->OperationCode == RESET_COMMAND) {

      currSCCB->HostStatus = SCCB_PHASE_SEQUENCE_FAIL;
      currSCCB->CdbLength = SIX_BYTE_CMD;
      }

   WR_HARPOON(p_port+hp_scsisig, 0x00);

   ARAM_ACCESS(p_port);


   cdb_reg = p_port + CMD_STRT;

   for (i=0; i < currSCCB->CdbLength; i++) {

      if (currSCCB->OperationCode == RESET_COMMAND)

	 WRW_HARPOON(cdb_reg, (MPM_OP + ACOMMAND + 0x00));

      else
	 WRW_HARPOON(cdb_reg, (MPM_OP + ACOMMAND + currSCCB->Cdb[i]));
      cdb_reg +=2;
      }

   if (currSCCB->CdbLength != TWELVE_BYTE_CMD)
      WRW_HARPOON(cdb_reg, (BRH_OP+ALWAYS+    NP));

   WR_HARPOON(p_port+hp_portctrl_0,(SCSI_PORT));

   currSCCB->Sccb_scsistat = COMMAND_ST;

   WR_HARPOON(p_port+hp_autostart_3, (AUTO_IMMED | CMD_ONLY_STRT));
   SGRAM_ACCESS(p_port);
}


/*---------------------------------------------------------------------
 *
 * Function: Status phase
 *
 * Description: Bring in the status and command complete message bytes
 *
 *---------------------------------------------------------------------*/

#if defined(OS2)
void far phaseStatus(ULONG port, UCHAR p_card)
#else
#if defined(DOS)
void phaseStatus(USHORT port, UCHAR p_card)
#else
void phaseStatus(ULONG port, UCHAR p_card)
#endif
#endif
{
   /* Start-up the automation to finish off this command and let the
      isr handle the interrupt for command complete when it comes in.
      We could wait here for the interrupt to be generated?
    */

   WR_HARPOON(port+hp_scsisig, 0x00);

   WR_HARPOON(port+hp_autostart_0, (AUTO_IMMED+END_DATA_START));
}


/*---------------------------------------------------------------------
 *
 * Function: Phase Message Out
 *
 * Description: Send out our message (if we have one) and handle whatever
 *              else is involed.
 *
 *---------------------------------------------------------------------*/

#if defined(OS2)
void far phaseMsgOut(ULONG port, UCHAR p_card)
#else
#if defined(DOS)
void phaseMsgOut(USHORT port, UCHAR p_card)
#else
void phaseMsgOut(ULONG port, UCHAR p_card)
#endif
#endif
{
	UCHAR message,scsiID;
	PSCCB currSCCB;
	PSCCBMgr_tar_info currTar_Info;

	currSCCB = BL_Card[p_card].currentSCCB;

	if (currSCCB != NULL) {

		message = currSCCB->Sccb_scsimsg;
		scsiID = currSCCB->TargID;

		if (message == SMDEV_RESET) 
		{


			currTar_Info = &sccbMgrTbl[p_card][scsiID];
			currTar_Info->TarSyncCtrl = 0;
			sssyncv(port, scsiID, NARROW_SCSI,currTar_Info);

			if (sccbMgrTbl[p_card][scsiID].TarEEValue & EE_SYNC_MASK) 
			{

				sccbMgrTbl[p_card][scsiID].TarStatus &= ~TAR_SYNC_MASK;

			}

			if (sccbMgrTbl[p_card][scsiID].TarEEValue & EE_WIDE_SCSI) 
			{

				sccbMgrTbl[p_card][scsiID].TarStatus &= ~TAR_WIDE_MASK;
			}


			queueFlushSccb(p_card,SCCB_COMPLETE);
			SccbMgrTableInitTarget(p_card,scsiID);
		}
		else if (currSCCB->Sccb_scsistat == ABORT_ST)
		{
			currSCCB->HostStatus = SCCB_COMPLETE;
			if(BL_Card[p_card].discQ_Tbl[currSCCB->Sccb_tag] != NULL)
			{
				BL_Card[p_card].discQ_Tbl[currSCCB->Sccb_tag] = NULL;
				sccbMgrTbl[p_card][scsiID].TarTagQ_Cnt--;
			}
					
		}

		else if (currSCCB->Sccb_scsistat < COMMAND_ST) 
		{


			if(message == SMNO_OP)
			{
				currSCCB->Sccb_MGRFlags |= F_DEV_SELECTED;
		
				ssel(port,p_card);
				return;
			}
		}
		else 
		{


			if (message == SMABORT)

				queueFlushSccb(p_card,SCCB_COMPLETE);
		}

	}
	else 
	{
		message = SMABORT;
	}

	WRW_HARPOON((port+hp_intstat), (BUS_FREE | PHASE | XFER_CNT_0));


	WR_HARPOON(port+hp_portctrl_0, SCSI_BUS_EN);

	WR_HARPOON(port+hp_scsidata_0,message);

	WR_HARPOON(port+hp_scsisig, (SCSI_ACK + S_ILL_PH));

	ACCEPT_MSG(port);

	WR_HARPOON(port+hp_portctrl_0, 0x00);

	if ((message == SMABORT) || (message == SMDEV_RESET) || 
				(message == SMABORT_TAG) ) 
	{

		while(!(RDW_HARPOON((port+hp_intstat)) & (BUS_FREE | PHASE))) {}

		if (RDW_HARPOON((port+hp_intstat)) & BUS_FREE) 
		{
			WRW_HARPOON((port+hp_intstat), BUS_FREE);

			if (currSCCB != NULL) 
			{

				if((BL_Card[p_card].globalFlags & F_CONLUN_IO) &&
					((sccbMgrTbl[p_card][currSCCB->TargID].TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING))
					sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[currSCCB->Lun] = FALSE;
				else
					sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[0] = FALSE;

				queueCmdComplete(&BL_Card[p_card],currSCCB, p_card);
			}

			else 
			{
				BL_Card[p_card].globalFlags |= F_NEW_SCCB_CMD;
			}
		}

		else 
		{

			sxfrp(port,p_card);
		}
	}

	else 
	{

		if(message == SMPARITY)
		{
			currSCCB->Sccb_scsimsg = SMNO_OP;
			WR_HARPOON(port+hp_autostart_1, (AUTO_IMMED+DISCONNECT_START));
		}
		else
		{
			sxfrp(port,p_card);
		}
	}
}


/*---------------------------------------------------------------------
 *
 * Function: Message In phase
 *
 * Description: Bring in the message and determine what to do with it.
 *
 *---------------------------------------------------------------------*/

#if defined(OS2)
void far phaseMsgIn(ULONG port, UCHAR p_card)
#else
#if defined(DOS)
void phaseMsgIn(USHORT port, UCHAR p_card)
#else
void phaseMsgIn(ULONG port, UCHAR p_card)
#endif
#endif
{
	UCHAR message;
	PSCCB currSCCB;

	currSCCB = BL_Card[p_card].currentSCCB;

	if (BL_Card[p_card].globalFlags & F_HOST_XFER_ACT) 
	{

		phaseChkFifo(port, p_card);
	}

	message = RD_HARPOON(port+hp_scsidata_0);
	if ((message == SMDISC) || (message == SMSAVE_DATA_PTR)) 
	{

		WR_HARPOON(port+hp_autostart_1, (AUTO_IMMED+END_DATA_START));

	}

	else 
	{

		message = sfm(port,currSCCB);
		if (message) 
		{


			sdecm(message,port,p_card);

		}
		else
		{
			if(currSCCB->Sccb_scsimsg != SMPARITY)
				ACCEPT_MSG(port);
			WR_HARPOON(port+hp_autostart_1, (AUTO_IMMED+DISCONNECT_START));
		}
	}

}


/*---------------------------------------------------------------------
 *
 * Function: Illegal phase
 *
 * Description: Target switched to some illegal phase, so all we can do
 *              is report an error back to the host (if that is possible)
 *              and send an ABORT message to the misbehaving target.
 *
 *---------------------------------------------------------------------*/

#if defined(OS2)
void far phaseIllegal(ULONG port, UCHAR p_card)
#else
#if defined(DOS)
void phaseIllegal(USHORT port, UCHAR p_card)
#else
void phaseIllegal(ULONG port, UCHAR p_card)
#endif
#endif
{
   PSCCB currSCCB;

   currSCCB = BL_Card[p_card].currentSCCB;

   WR_HARPOON(port+hp_scsisig, RD_HARPOON(port+hp_scsisig));
   if (currSCCB != NULL) {

      currSCCB->HostStatus = SCCB_PHASE_SEQUENCE_FAIL;
      currSCCB->Sccb_scsistat = ABORT_ST;
      currSCCB->Sccb_scsimsg = SMABORT;
      }

   ACCEPT_MSG_ATN(port);
}



/*---------------------------------------------------------------------
 *
 * Function: Phase Check FIFO
 *
 * Description: Make sure data has been flushed from both FIFOs and abort
 *              the operations if necessary.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void phaseChkFifo(USHORT port, UCHAR p_card)
#else
void phaseChkFifo(ULONG port, UCHAR p_card)
#endif
{
   ULONG xfercnt;
   PSCCB currSCCB;

   currSCCB = BL_Card[p_card].currentSCCB;

   if (currSCCB->Sccb_scsistat == DATA_IN_ST)
      {

      while((!(RD_HARPOON(port+hp_xferstat) & FIFO_EMPTY)) &&
	      (RD_HARPOON(port+hp_ext_status) & BM_CMD_BUSY)) {}


      if (!(RD_HARPOON(port+hp_xferstat) & FIFO_EMPTY))
         {
	      currSCCB->Sccb_ATC += currSCCB->Sccb_XferCnt;

	      currSCCB->Sccb_XferCnt = 0;

	      if ((RDW_HARPOON((port+hp_intstat)) & PARITY) &&
	            (currSCCB->HostStatus == SCCB_COMPLETE))
            {
	         currSCCB->HostStatus = SCCB_PARITY_ERR;
	         WRW_HARPOON((port+hp_intstat), PARITY);
	         }

	      hostDataXferAbort(port,p_card,currSCCB);

	      dataXferProcessor(port, &BL_Card[p_card]);

	      while((!(RD_HARPOON(port+hp_xferstat) & FIFO_EMPTY)) &&
	         (RD_HARPOON(port+hp_ext_status) & BM_CMD_BUSY)) {}

	      }
      }  /*End Data In specific code. */



#if defined(DOS)
   asm { mov dx,port;
      add dx,hp_xfercnt_2;
      in  al,dx;
      dec dx;
      xor ah,ah;
      mov word ptr xfercnt+2,ax;
      in  al,dx;
      dec dx;
      mov ah,al;
      in  al,dx;
      mov word ptr xfercnt,ax;
      }
#else
   GET_XFER_CNT(port,xfercnt);
#endif


   WR_HARPOON(port+hp_xfercnt_0, 0x00);


   WR_HARPOON(port+hp_portctrl_0, 0x00);

   currSCCB->Sccb_ATC += (currSCCB->Sccb_XferCnt - xfercnt);

   currSCCB->Sccb_XferCnt = xfercnt;

   if ((RDW_HARPOON((port+hp_intstat)) & PARITY) &&
      (currSCCB->HostStatus == SCCB_COMPLETE)) {

      currSCCB->HostStatus = SCCB_PARITY_ERR;
      WRW_HARPOON((port+hp_intstat), PARITY);
      }


   hostDataXferAbort(port,p_card,currSCCB);


   WR_HARPOON(port+hp_fifowrite, 0x00);
   WR_HARPOON(port+hp_fiforead, 0x00);
   WR_HARPOON(port+hp_xferstat, 0x00);

   WRW_HARPOON((port+hp_intstat), XFER_CNT_0);
}


/*---------------------------------------------------------------------
 *
 * Function: Phase Bus Free
 *
 * Description: We just went bus free so figure out if it was
 *              because of command complete or from a disconnect.
 *
 *---------------------------------------------------------------------*/
#if defined(DOS)
void phaseBusFree(USHORT port, UCHAR p_card)
#else
void phaseBusFree(ULONG port, UCHAR p_card)
#endif
{
   PSCCB currSCCB;

   currSCCB = BL_Card[p_card].currentSCCB;

   if (currSCCB != NULL)
      {

      DISABLE_AUTO(port);


      if (currSCCB->OperationCode == RESET_COMMAND)
         {

			if((BL_Card[p_card].globalFlags & F_CONLUN_IO) &&
				((sccbMgrTbl[p_card][currSCCB->TargID].TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING))
	   		 sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[currSCCB->Lun] = FALSE;
			else
		   	 sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[0] = FALSE;

	      queueCmdComplete(&BL_Card[p_card], currSCCB, p_card);

	      queueSearchSelect(&BL_Card[p_card],p_card);

	      }

      else if(currSCCB->Sccb_scsistat == SELECT_SN_ST)
	      {
	      sccbMgrTbl[p_card][currSCCB->TargID].TarStatus |=
			         (UCHAR)SYNC_SUPPORTED;
	      sccbMgrTbl[p_card][currSCCB->TargID].TarEEValue &= ~EE_SYNC_MASK;
	      }

      else if(currSCCB->Sccb_scsistat == SELECT_WN_ST)
	      {
	      sccbMgrTbl[p_card][currSCCB->TargID].TarStatus =
		            (sccbMgrTbl[p_card][currSCCB->TargID].
		   TarStatus & ~WIDE_ENABLED) | WIDE_NEGOCIATED;

	      sccbMgrTbl[p_card][currSCCB->TargID].TarEEValue &= ~EE_WIDE_SCSI;
	      }

#if !defined(DOS)
      else if(currSCCB->Sccb_scsistat == SELECT_Q_ST)
	      {
	      /* Make sure this is not a phony BUS_FREE.  If we were
	      reselected or if BUSY is NOT on then this is a
	      valid BUS FREE.  SRR Wednesday, 5/10/1995.     */

	      if ((!(RD_HARPOON(port+hp_scsisig) & SCSI_BSY)) ||
	         (RDW_HARPOON((port+hp_intstat)) & RSEL))
	         {
	         sccbMgrTbl[p_card][currSCCB->TargID].TarStatus &= ~TAR_TAG_Q_MASK;
	         sccbMgrTbl[p_card][currSCCB->TargID].TarStatus |= TAG_Q_REJECT;
	         }

	      else
            {
	         return;
	         }
         }
#endif

      else
	      {

	      currSCCB->Sccb_scsistat = BUS_FREE_ST;

         if (!currSCCB->HostStatus)
	         {
	         currSCCB->HostStatus = SCCB_PHASE_SEQUENCE_FAIL;
	         }

			if((BL_Card[p_card].globalFlags & F_CONLUN_IO) &&
				((sccbMgrTbl[p_card][currSCCB->TargID].TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING))
	   		 sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[currSCCB->Lun] = FALSE;
			else
		   	 sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[0] = FALSE;

	      queueCmdComplete(&BL_Card[p_card], currSCCB, p_card);
	      return;
	      }


      BL_Card[p_card].globalFlags |= F_NEW_SCCB_CMD;

      } /*end if !=null */
}




#ident "$Id: automate.c 1.14 1997/01/31 02:11:46 mohan Exp $"
/*----------------------------------------------------------------------
 *
 *
 *   Copyright 1995-1996 by Mylex Corporation.  All Rights Reserved
 *
 *   This file is available under both the GNU General Public License
 *   and a BSD-style copyright; see LICENSE.FlashPoint for details.
 *
 *   $Workfile:   automate.c  $
 *
 *   Description:  Functions relating to programming the automation of
 *                 the HARPOON.
 *
 *   $Date: 1997/01/31 02:11:46 $
 *
 *   $Revision: 1.14 $
 *
 *----------------------------------------------------------------------*/

/*#include <globals.h>*/

#if (FW_TYPE==_UCB_MGR_)
	/*#include <budi.h>*/
#endif

/*#include <sccbmgr.h>*/
/*#include <blx30.h>*/
/*#include <target.h>*/
/*#include <scsi2.h>*/
/*#include <harpoon.h>*/

/*
extern SCCBCARD BL_Card[MAX_CARDS];
extern SCCBMGR_TAR_INFO sccbMgrTbl[MAX_CARDS][MAX_SCSI_TAR];
extern SCCBCARD BL_Card[MAX_CARDS];
*/

/*---------------------------------------------------------------------
 *
 * Function: Auto Load Default Map
 *
 * Description: Load the Automation RAM with the defualt map values.
 *
 *---------------------------------------------------------------------*/
#if defined(DOS)
void autoLoadDefaultMap(USHORT p_port)
#else
void autoLoadDefaultMap(ULONG p_port)
#endif
{
#if defined(DOS)
   USHORT map_addr;
#else
   ULONG map_addr;
#endif

   ARAM_ACCESS(p_port);
   map_addr = p_port + hp_aramBase;

   WRW_HARPOON(map_addr, (MPM_OP+AMSG_OUT+ 0xC0));  /*ID MESSAGE */
   map_addr +=2;
   WRW_HARPOON(map_addr, (MPM_OP+AMSG_OUT+ 0x20));  /*SIMPLE TAG QUEUEING MSG */
   map_addr +=2;
   WRW_HARPOON(map_addr, RAT_OP);                   /*RESET ATTENTION */
   map_addr +=2;
   WRW_HARPOON(map_addr, (MPM_OP+AMSG_OUT+ 0x00));  /*TAG ID MSG */
   map_addr +=2;
   WRW_HARPOON(map_addr, (MPM_OP+ACOMMAND+ 0x00));  /*CDB BYTE 0 */
   map_addr +=2;
   WRW_HARPOON(map_addr, (MPM_OP+ACOMMAND+ 0x00));  /*CDB BYTE 1 */
   map_addr +=2;
   WRW_HARPOON(map_addr, (MPM_OP+ACOMMAND+ 0x00));  /*CDB BYTE 2 */
   map_addr +=2;
   WRW_HARPOON(map_addr, (MPM_OP+ACOMMAND+ 0x00));  /*CDB BYTE 3 */
   map_addr +=2;
   WRW_HARPOON(map_addr, (MPM_OP+ACOMMAND+ 0x00));  /*CDB BYTE 4 */
   map_addr +=2;
   WRW_HARPOON(map_addr, (MPM_OP+ACOMMAND+ 0x00));  /*CDB BYTE 5 */
   map_addr +=2;
   WRW_HARPOON(map_addr, (MPM_OP+ACOMMAND+ 0x00));  /*CDB BYTE 6 */
   map_addr +=2;
   WRW_HARPOON(map_addr, (MPM_OP+ACOMMAND+ 0x00));  /*CDB BYTE 7 */
   map_addr +=2;
   WRW_HARPOON(map_addr, (MPM_OP+ACOMMAND+ 0x00));  /*CDB BYTE 8 */
   map_addr +=2;
   WRW_HARPOON(map_addr, (MPM_OP+ACOMMAND+ 0x00));  /*CDB BYTE 9 */
   map_addr +=2;
   WRW_HARPOON(map_addr, (MPM_OP+ACOMMAND+ 0x00));  /*CDB BYTE 10 */
   map_addr +=2;
   WRW_HARPOON(map_addr, (MPM_OP+ACOMMAND+ 0x00));  /*CDB BYTE 11 */
   map_addr +=2;
   WRW_HARPOON(map_addr, (CPE_OP+ADATA_OUT+ DINT)); /*JUMP IF DATA OUT */
   map_addr +=2;
   WRW_HARPOON(map_addr, (TCB_OP+FIFO_0+ DI));     /*JUMP IF NO DATA IN FIFO */
   map_addr +=2;                                   /*This means AYNC DATA IN */
   WRW_HARPOON(map_addr, (SSI_OP+   SSI_IDO_STRT)); /*STOP AND INTERRUPT */
   map_addr +=2;
   WRW_HARPOON(map_addr, (CPE_OP+ADATA_IN+DINT));   /*JUMP IF NOT DATA IN PHZ */
   map_addr +=2;
   WRW_HARPOON(map_addr, (CPN_OP+AMSG_IN+  ST));    /*IF NOT MSG IN CHECK 4 DATA IN */
   map_addr +=2;
   WRW_HARPOON(map_addr, (CRD_OP+SDATA+    0x02));  /*SAVE DATA PTR MSG? */
   map_addr +=2;
   WRW_HARPOON(map_addr, (BRH_OP+NOT_EQ+   DC));    /*GO CHECK FOR DISCONNECT MSG */
   map_addr +=2;
   WRW_HARPOON(map_addr, (MRR_OP+SDATA+    D_AR1)); /*SAVE DATA PTRS MSG */
   map_addr +=2;
   WRW_HARPOON(map_addr, (CPN_OP+AMSG_IN+  ST));    /*IF NOT MSG IN CHECK DATA IN */
   map_addr +=2;
   WRW_HARPOON(map_addr, (CRD_OP+SDATA+    0x04));  /*DISCONNECT MSG? */
   map_addr +=2;
   WRW_HARPOON(map_addr, (BRH_OP+NOT_EQ+   UNKNWN));/*UKNKNOWN MSG */
   map_addr +=2;
   WRW_HARPOON(map_addr, (MRR_OP+SDATA+    D_BUCKET));/*XFER DISCONNECT MSG */
   map_addr +=2;
   WRW_HARPOON(map_addr, (SSI_OP+          SSI_ITAR_DISC));/*STOP AND INTERRUPT */
   map_addr +=2;
   WRW_HARPOON(map_addr, (CPN_OP+ASTATUS+  UNKNWN));/*JUMP IF NOT STATUS PHZ. */
   map_addr +=2;
   WRW_HARPOON(map_addr, (MRR_OP+SDATA+  D_AR0));   /*GET STATUS BYTE */
   map_addr +=2;
   WRW_HARPOON(map_addr, (CPN_OP+AMSG_IN+  CC));    /*ERROR IF NOT MSG IN PHZ */
   map_addr +=2;
   WRW_HARPOON(map_addr, (CRD_OP+SDATA+    0x00));  /*CHECK FOR CMD COMPLETE MSG. */
   map_addr +=2;
   WRW_HARPOON(map_addr, (BRH_OP+NOT_EQ+   CC));    /*ERROR IF NOT CMD COMPLETE MSG. */
   map_addr +=2;
   WRW_HARPOON(map_addr, (MRR_OP+SDATA+  D_BUCKET));/*GET CMD COMPLETE MSG */
   map_addr +=2;
   WRW_HARPOON(map_addr, (SSI_OP+       SSI_ICMD_COMP));/*END OF COMMAND */
   map_addr +=2;

   WRW_HARPOON(map_addr, (SSI_OP+ SSI_IUNKWN));  /*RECEIVED UNKNOWN MSG BYTE */
   map_addr +=2;
   WRW_HARPOON(map_addr, (SSI_OP+ SSI_INO_CC));  /*NO COMMAND COMPLETE AFTER STATUS */
   map_addr +=2;
   WRW_HARPOON(map_addr, (SSI_OP+ SSI_ITICKLE)); /*BIOS Tickled the Mgr */
   map_addr +=2;
   WRW_HARPOON(map_addr, (SSI_OP+ SSI_IRFAIL));  /*EXPECTED ID/TAG MESSAGES AND */
   map_addr +=2;                             /* DIDN'T GET ONE */
   WRW_HARPOON(map_addr, (CRR_OP+AR3+  S_IDREG)); /* comp SCSI SEL ID & AR3*/
   map_addr +=2;
   WRW_HARPOON(map_addr, (BRH_OP+EQUAL+   0x00));    /*SEL ID OK then Conti. */
   map_addr +=2;
   WRW_HARPOON(map_addr, (SSI_OP+ SSI_INO_CC));  /*NO COMMAND COMPLETE AFTER STATUS */



   SGRAM_ACCESS(p_port);
}

/*---------------------------------------------------------------------
 *
 * Function: Auto Command Complete
 *
 * Description: Post command back to host and find another command
 *              to execute.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void autoCmdCmplt(USHORT p_port, UCHAR p_card)
#else
void autoCmdCmplt(ULONG p_port, UCHAR p_card)
#endif
{
   PSCCB currSCCB;
   UCHAR status_byte;

   currSCCB = BL_Card[p_card].currentSCCB;

   status_byte = RD_HARPOON(p_port+hp_gp_reg_0);

   sccbMgrTbl[p_card][currSCCB->TargID].TarLUN_CA = FALSE;

   if (status_byte != SSGOOD) {

      if (status_byte == SSQ_FULL) {


			if(((BL_Card[p_card].globalFlags & F_CONLUN_IO) &&
				((sccbMgrTbl[p_card][currSCCB->TargID].TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING)))
			{
	         sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[currSCCB->Lun] = TRUE;
				if(BL_Card[p_card].discQCount != 0)
					BL_Card[p_card].discQCount--;
				BL_Card[p_card].discQ_Tbl[sccbMgrTbl[p_card][currSCCB->TargID].LunDiscQ_Idx[currSCCB->Lun]] = NULL;
			}
			else
			{
	         sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[0] = TRUE;
				if(currSCCB->Sccb_tag)
				{
					if(BL_Card[p_card].discQCount != 0)
						BL_Card[p_card].discQCount--;
					BL_Card[p_card].discQ_Tbl[currSCCB->Sccb_tag] = NULL;
				}else
				{
					if(BL_Card[p_card].discQCount != 0)
						BL_Card[p_card].discQCount--;
					BL_Card[p_card].discQ_Tbl[sccbMgrTbl[p_card][currSCCB->TargID].LunDiscQ_Idx[0]] = NULL;
				}
			}

         currSCCB->Sccb_MGRFlags |= F_STATUSLOADED;

         queueSelectFail(&BL_Card[p_card],p_card);

         return;
         }

      if(currSCCB->Sccb_scsistat == SELECT_SN_ST)
         {
         sccbMgrTbl[p_card][currSCCB->TargID].TarStatus |=
            (UCHAR)SYNC_SUPPORTED;

	      sccbMgrTbl[p_card][currSCCB->TargID].TarEEValue &= ~EE_SYNC_MASK;
         BL_Card[p_card].globalFlags |= F_NEW_SCCB_CMD;

			if(((BL_Card[p_card].globalFlags & F_CONLUN_IO) &&
				((sccbMgrTbl[p_card][currSCCB->TargID].TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING)))
			{
	         sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[currSCCB->Lun] = TRUE;
				if(BL_Card[p_card].discQCount != 0)
					BL_Card[p_card].discQCount--;
				BL_Card[p_card].discQ_Tbl[sccbMgrTbl[p_card][currSCCB->TargID].LunDiscQ_Idx[currSCCB->Lun]] = NULL;
			}
			else
			{
	         sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[0] = TRUE;
				if(currSCCB->Sccb_tag)
				{
					if(BL_Card[p_card].discQCount != 0)
						BL_Card[p_card].discQCount--;
					BL_Card[p_card].discQ_Tbl[currSCCB->Sccb_tag] = NULL;
				}else
				{
					if(BL_Card[p_card].discQCount != 0)
						BL_Card[p_card].discQCount--;
					BL_Card[p_card].discQ_Tbl[sccbMgrTbl[p_card][currSCCB->TargID].LunDiscQ_Idx[0]] = NULL;
				}
			}
         return;

         }

      if(currSCCB->Sccb_scsistat == SELECT_WN_ST)
         {

	      sccbMgrTbl[p_card][currSCCB->TargID].TarStatus =
	         (sccbMgrTbl[p_card][currSCCB->TargID].
	         TarStatus & ~WIDE_ENABLED) | WIDE_NEGOCIATED;

	      sccbMgrTbl[p_card][currSCCB->TargID].TarEEValue &= ~EE_WIDE_SCSI;
         BL_Card[p_card].globalFlags |= F_NEW_SCCB_CMD;

			if(((BL_Card[p_card].globalFlags & F_CONLUN_IO) &&
				((sccbMgrTbl[p_card][currSCCB->TargID].TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING)))
			{
	         sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[currSCCB->Lun] = TRUE;
				if(BL_Card[p_card].discQCount != 0)
					BL_Card[p_card].discQCount--;
				BL_Card[p_card].discQ_Tbl[sccbMgrTbl[p_card][currSCCB->TargID].LunDiscQ_Idx[currSCCB->Lun]] = NULL;
			}
			else
			{
	         sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[0] = TRUE;
				if(currSCCB->Sccb_tag)
				{
					if(BL_Card[p_card].discQCount != 0)
						BL_Card[p_card].discQCount--;
					BL_Card[p_card].discQ_Tbl[currSCCB->Sccb_tag] = NULL;
				}else
				{
					if(BL_Card[p_card].discQCount != 0)
						BL_Card[p_card].discQCount--;
					BL_Card[p_card].discQ_Tbl[sccbMgrTbl[p_card][currSCCB->TargID].LunDiscQ_Idx[0]] = NULL;
				}
			}
         return;
      
         }
     
	   if (status_byte == SSCHECK) 
		{
			if(BL_Card[p_card].globalFlags & F_DO_RENEGO)
			{
				if (sccbMgrTbl[p_card][currSCCB->TargID].TarEEValue & EE_SYNC_MASK)
				{
					sccbMgrTbl[p_card][currSCCB->TargID].TarStatus &= ~TAR_SYNC_MASK;
				}
				if (sccbMgrTbl[p_card][currSCCB->TargID].TarEEValue & EE_WIDE_SCSI)
				{
					sccbMgrTbl[p_card][currSCCB->TargID].TarStatus &= ~TAR_WIDE_MASK;
				}
			}
		}

      if (!(currSCCB->Sccb_XferState & F_AUTO_SENSE)) {

         currSCCB->SccbStatus = SCCB_ERROR;
         currSCCB->TargetStatus = status_byte;

         if (status_byte == SSCHECK) {

            sccbMgrTbl[p_card][currSCCB->TargID].TarLUN_CA
               = TRUE;
     

#if (FW_TYPE==_SCCB_MGR_)
            if (currSCCB->RequestSenseLength != NO_AUTO_REQUEST_SENSE) {

               if (currSCCB->RequestSenseLength == 0)
                  currSCCB->RequestSenseLength = 14;

               ssenss(&BL_Card[p_card]);
               BL_Card[p_card].globalFlags |= F_NEW_SCCB_CMD;

 					if(((BL_Card[p_card].globalFlags & F_CONLUN_IO) &&
						((sccbMgrTbl[p_card][currSCCB->TargID].TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING)))
					{
			         sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[currSCCB->Lun] = TRUE;
						if(BL_Card[p_card].discQCount != 0)
							BL_Card[p_card].discQCount--;
						BL_Card[p_card].discQ_Tbl[sccbMgrTbl[p_card][currSCCB->TargID].LunDiscQ_Idx[currSCCB->Lun]] = NULL;
					}
					else
					{
	   		      sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[0] = TRUE;
						if(currSCCB->Sccb_tag)
						{
							if(BL_Card[p_card].discQCount != 0)
								BL_Card[p_card].discQCount--;
							BL_Card[p_card].discQ_Tbl[currSCCB->Sccb_tag] = NULL;
						}else
						{
							if(BL_Card[p_card].discQCount != 0)
								BL_Card[p_card].discQCount--;
							BL_Card[p_card].discQ_Tbl[sccbMgrTbl[p_card][currSCCB->TargID].LunDiscQ_Idx[0]] = NULL;
						}
					}
               return;
               }
#else
				   if ((!(currSCCB->Sccb_ucb_ptr->UCB_opcode & OPC_NO_AUTO_SENSE)) &&
			   		(currSCCB->RequestSenseLength))
				   {
				   	ssenss(&BL_Card[p_card]);
				      BL_Card[p_card].globalFlags |= F_NEW_SCCB_CMD;

						if(((BL_Card[p_card].globalFlags & F_CONLUN_IO) &&
							((sccbMgrTbl[p_card][currSCCB->TargID].TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING)))
						{
	      			   sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[currSCCB->Lun] = TRUE;
							if(BL_Card[p_card].discQCount != 0)
								BL_Card[p_card].discQCount--;
							BL_Card[p_card].discQ_Tbl[sccbMgrTbl[p_card][currSCCB->TargID].LunDiscQ_Idx[currSCCB->Lun]] = NULL;
						}
						else
						{
	      			   sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[0] = TRUE;
							if(currSCCB->Sccb_tag)
							{
								if(BL_Card[p_card].discQCount != 0)
									BL_Card[p_card].discQCount--;
								BL_Card[p_card].discQ_Tbl[currSCCB->Sccb_tag] = NULL;
							}else
							{
								if(BL_Card[p_card].discQCount != 0)
									BL_Card[p_card].discQCount--;
								BL_Card[p_card].discQ_Tbl[sccbMgrTbl[p_card][currSCCB->TargID].LunDiscQ_Idx[0]] = NULL;
							}
						}
				      return;
				   }

#endif
            }
         }
      }


	if((BL_Card[p_card].globalFlags & F_CONLUN_IO) &&
		((sccbMgrTbl[p_card][currSCCB->TargID].TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING))
	   sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[currSCCB->Lun] = FALSE;
	else
	   sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[0] = FALSE;


   queueCmdComplete(&BL_Card[p_card], currSCCB, p_card);
}
#ident "$Id: busmstr.c 1.8 1997/01/31 02:10:27 mohan Exp $"
/*----------------------------------------------------------------------
 *
 *
 *   Copyright 1995-1996 by Mylex Corporation.  All Rights Reserved
 *
 *   This file is available under both the GNU General Public License
 *   and a BSD-style copyright; see LICENSE.FlashPoint for details.
 *
 *   $Workfile:   busmstr.c  $
 *
 *   Description:  Functions to start, stop, and abort BusMaster operations.
 *
 *   $Date: 1997/01/31 02:10:27 $
 *
 *   $Revision: 1.8 $
 *
 *----------------------------------------------------------------------*/

/*#include <globals.h>*/

#if (FW_TYPE==_UCB_MGR_)
	/*#include <budi.h>*/
#endif

/*#include <sccbmgr.h>*/
/*#include <blx30.h>*/
/*#include <target.h>*/
/*#include <scsi2.h>*/
/*#include <harpoon.h>*/


/*
extern SCCBCARD BL_Card[MAX_CARDS];
extern SCCBMGR_TAR_INFO sccbMgrTbl[MAX_CARDS][MAX_SCSI_TAR];
*/

#define SHORT_WAIT   0x0000000F
#define LONG_WAIT    0x0000FFFFL

#if defined(BUGBUG)
void Debug_Load(UCHAR p_card, UCHAR p_bug_data);
#endif

/*---------------------------------------------------------------------
 *
 * Function: Data Transfer Processor
 *
 * Description: This routine performs two tasks.
 *              (1) Start data transfer by calling HOST_DATA_XFER_START
 *              function.  Once data transfer is started, (2) Depends
 *              on the type of data transfer mode Scatter/Gather mode
 *              or NON Scatter/Gather mode.  In NON Scatter/Gather mode,
 *              this routine checks Sccb_MGRFlag (F_HOST_XFER_ACT bit) for
 *              data transfer done.  In Scatter/Gather mode, this routine
 *              checks bus master command complete and dual rank busy
 *              bit to keep chaining SC transfer command.  Similarly,
 *              in Scatter/Gather mode, it checks Sccb_MGRFlag
 *              (F_HOST_XFER_ACT bit) for data transfer done.
 *              
 *---------------------------------------------------------------------*/

#if defined(DOS)
void dataXferProcessor(USHORT port, PSCCBcard pCurrCard)
#else
void dataXferProcessor(ULONG port, PSCCBcard pCurrCard)
#endif
{
   PSCCB currSCCB;

   currSCCB = pCurrCard->currentSCCB;

      if (currSCCB->Sccb_XferState & F_SG_XFER)
			{
			if (pCurrCard->globalFlags & F_HOST_XFER_ACT)

				{
		   	currSCCB->Sccb_sgseg += (UCHAR)SG_BUF_CNT;
         	currSCCB->Sccb_SGoffset = 0x00; 
 				}
			pCurrCard->globalFlags |= F_HOST_XFER_ACT;
         
         busMstrSGDataXferStart(port, currSCCB);
			}

      else
			{
			if (!(pCurrCard->globalFlags & F_HOST_XFER_ACT))
				{
				pCurrCard->globalFlags |= F_HOST_XFER_ACT;
         
         	busMstrDataXferStart(port, currSCCB);
         	}
			}
}


/*---------------------------------------------------------------------
 *
 * Function: BusMaster Scatter Gather Data Transfer Start
 *
 * Description:
 *
 *---------------------------------------------------------------------*/
#if defined(DOS)
void busMstrSGDataXferStart(USHORT p_port, PSCCB pcurrSCCB)
#else
void busMstrSGDataXferStart(ULONG p_port, PSCCB pcurrSCCB)
#endif
{
   ULONG count,addr,tmpSGCnt;
   UINT sg_index;
   UCHAR sg_count, i;
#if defined(DOS)
   USHORT reg_offset;
#else
   ULONG reg_offset;
#endif


   if (pcurrSCCB->Sccb_XferState & F_HOST_XFER_DIR) {

      count =  ((ULONG) HOST_RD_CMD)<<24;
      }

   else {
      count =  ((ULONG) HOST_WRT_CMD)<<24;
      }

   sg_count = 0;
   tmpSGCnt = 0;
   sg_index = pcurrSCCB->Sccb_sgseg;
   reg_offset = hp_aramBase;


	i = (UCHAR) (RD_HARPOON(p_port+hp_page_ctrl) & ~(SGRAM_ARAM|SCATTER_EN));


	WR_HARPOON(p_port+hp_page_ctrl, i);

   while ((sg_count < (UCHAR)SG_BUF_CNT) &&
      ((ULONG)(sg_index * (UINT)SG_ELEMENT_SIZE) < pcurrSCCB->DataLength) ) {

#if defined(COMPILER_16_BIT) && !defined(DOS)
      tmpSGCnt += *(((ULONG far *)pcurrSCCB->DataPointer)+
         (sg_index * 2));

      count |= *(((ULONG far *)pcurrSCCB->DataPointer)+
         (sg_index * 2));

      addr = *(((ULONG far *)pcurrSCCB->DataPointer)+
         ((sg_index * 2) + 1));

#else
      tmpSGCnt += *(((ULONG *)pcurrSCCB->DataPointer)+
         (sg_index * 2));

      count |= *(((ULONG *)pcurrSCCB->DataPointer)+
         (sg_index * 2));

      addr = *(((ULONG *)pcurrSCCB->DataPointer)+
         ((sg_index * 2) + 1));
#endif


      if ((!sg_count) && (pcurrSCCB->Sccb_SGoffset)) {

         addr += ((count & 0x00FFFFFFL) - pcurrSCCB->Sccb_SGoffset);
         count = (count & 0xFF000000L) | pcurrSCCB->Sccb_SGoffset;

         tmpSGCnt = count & 0x00FFFFFFL;
         }

      WR_HARP32(p_port,reg_offset,addr);
      reg_offset +=4;

      WR_HARP32(p_port,reg_offset,count);
      reg_offset +=4;

      count &= 0xFF000000L;
      sg_index++;
      sg_count++;

      } /*End While */

   pcurrSCCB->Sccb_XferCnt = tmpSGCnt;

   WR_HARPOON(p_port+hp_sg_addr,(sg_count<<4));

   if (pcurrSCCB->Sccb_XferState & F_HOST_XFER_DIR) {

      WR_HARP32(p_port,hp_xfercnt_0,tmpSGCnt);


      WR_HARPOON(p_port+hp_portctrl_0,(DMA_PORT | SCSI_PORT | SCSI_INBIT));
      WR_HARPOON(p_port+hp_scsisig, S_DATAI_PH);
      }

   else {


      if ((!(RD_HARPOON(p_port+hp_synctarg_0) & NARROW_SCSI)) &&
         (tmpSGCnt & 0x000000001))
         {

         pcurrSCCB->Sccb_XferState |= F_ODD_BALL_CNT;
         tmpSGCnt--;
         }


      WR_HARP32(p_port,hp_xfercnt_0,tmpSGCnt);

      WR_HARPOON(p_port+hp_portctrl_0,(SCSI_PORT | DMA_PORT | DMA_RD));
      WR_HARPOON(p_port+hp_scsisig, S_DATAO_PH);
      }


   WR_HARPOON(p_port+hp_page_ctrl, (UCHAR) (i | SCATTER_EN));

}


/*---------------------------------------------------------------------
 *
 * Function: BusMaster Data Transfer Start
 *
 * Description: 
 *
 *---------------------------------------------------------------------*/
#if defined(DOS)
void busMstrDataXferStart(USHORT p_port, PSCCB pcurrSCCB)
#else
void busMstrDataXferStart(ULONG p_port, PSCCB pcurrSCCB)
#endif
{
   ULONG addr,count;

   if (!(pcurrSCCB->Sccb_XferState & F_AUTO_SENSE)) {

      count = pcurrSCCB->Sccb_XferCnt;

      addr = (ULONG) pcurrSCCB->DataPointer + pcurrSCCB->Sccb_ATC;
      }

   else {
      addr = pcurrSCCB->SensePointer;
      count = pcurrSCCB->RequestSenseLength;

      }

#if defined(DOS)
   asm { mov dx,p_port;
         mov ax,word ptr count;
         add dx,hp_xfer_cnt_lo;
         out dx,al;
         inc dx;
         xchg ah,al
         out dx,al;
         inc dx;
         mov ax,word ptr count+2;
         out dx,al;
         inc dx;
         inc dx;
         mov ax,word ptr addr;
         out dx,al;
         inc dx;
         xchg ah,al
         out dx,al;
         inc dx;
         mov ax,word ptr addr+2;
         out dx,al;
         inc dx;
         xchg ah,al
         out dx,al;
         }

   WR_HARP32(p_port,hp_xfercnt_0,count);

#else
   HP_SETUP_ADDR_CNT(p_port,addr,count);
#endif


   if (pcurrSCCB->Sccb_XferState & F_HOST_XFER_DIR) {

      WR_HARPOON(p_port+hp_portctrl_0,(DMA_PORT | SCSI_PORT | SCSI_INBIT));
      WR_HARPOON(p_port+hp_scsisig, S_DATAI_PH);

      WR_HARPOON(p_port+hp_xfer_cmd,
         (XFER_DMA_HOST | XFER_HOST_AUTO | XFER_DMA_8BIT));
      }

   else {

      WR_HARPOON(p_port+hp_portctrl_0,(SCSI_PORT | DMA_PORT | DMA_RD));
      WR_HARPOON(p_port+hp_scsisig, S_DATAO_PH);

      WR_HARPOON(p_port+hp_xfer_cmd,
         (XFER_HOST_DMA | XFER_HOST_AUTO | XFER_DMA_8BIT));

      }
}


/*---------------------------------------------------------------------
 *
 * Function: BusMaster Timeout Handler
 *
 * Description: This function is called after a bus master command busy time
 *               out is detected.  This routines issue halt state machine
 *               with a software time out for command busy.  If command busy
 *               is still asserted at the end of the time out, it issues
 *               hard abort with another software time out.  It hard abort
 *               command busy is also time out, it'll just give up.
 *
 *---------------------------------------------------------------------*/
#if defined(DOS)
UCHAR busMstrTimeOut(USHORT p_port)
#else
UCHAR busMstrTimeOut(ULONG p_port)
#endif
{
   ULONG timeout;

   timeout = LONG_WAIT;

   WR_HARPOON(p_port+hp_sys_ctrl, HALT_MACH);

   while ((!(RD_HARPOON(p_port+hp_ext_status) & CMD_ABORTED)) && timeout--) {}

   
   
   if (RD_HARPOON(p_port+hp_ext_status) & BM_CMD_BUSY) {
      WR_HARPOON(p_port+hp_sys_ctrl, HARD_ABORT);

      timeout = LONG_WAIT;
      while ((RD_HARPOON(p_port+hp_ext_status) & BM_CMD_BUSY) && timeout--) {}
      }

   RD_HARPOON(p_port+hp_int_status);           /*Clear command complete */

   if (RD_HARPOON(p_port+hp_ext_status) & BM_CMD_BUSY) {
      return(TRUE);
      }

   else {
      return(FALSE);
      }
}


/*---------------------------------------------------------------------
 *
 * Function: Host Data Transfer Abort
 *
 * Description: Abort any in progress transfer.
 *
 *---------------------------------------------------------------------*/
#if defined(DOS)
void hostDataXferAbort(USHORT port, UCHAR p_card, PSCCB pCurrSCCB)
#else
void hostDataXferAbort(ULONG port, UCHAR p_card, PSCCB pCurrSCCB)
#endif
{

   ULONG timeout;
   ULONG remain_cnt;
   UINT sg_ptr;

   BL_Card[p_card].globalFlags &= ~F_HOST_XFER_ACT;

   if (pCurrSCCB->Sccb_XferState & F_AUTO_SENSE) {


      if (!(RD_HARPOON(port+hp_int_status) & INT_CMD_COMPL)) {

         WR_HARPOON(port+hp_bm_ctrl, (RD_HARPOON(port+hp_bm_ctrl) | FLUSH_XFER_CNTR));
         timeout = LONG_WAIT;

         while ((RD_HARPOON(port+hp_ext_status) & BM_CMD_BUSY) && timeout--) {}

         WR_HARPOON(port+hp_bm_ctrl, (RD_HARPOON(port+hp_bm_ctrl) & ~FLUSH_XFER_CNTR));

         if (RD_HARPOON(port+hp_ext_status) & BM_CMD_BUSY) {

            if (busMstrTimeOut(port)) {

               if (pCurrSCCB->HostStatus == 0x00)

                  pCurrSCCB->HostStatus = SCCB_BM_ERR;

               }

            if (RD_HARPOON(port+hp_int_status) & INT_EXT_STATUS) 

               if (RD_HARPOON(port+hp_ext_status) & BAD_EXT_STATUS) 

                  if (pCurrSCCB->HostStatus == 0x00)

                     {
                     pCurrSCCB->HostStatus = SCCB_BM_ERR;
#if defined(BUGBUG)
                     WR_HARPOON(port+hp_dual_addr_lo,
                        RD_HARPOON(port+hp_ext_status));
#endif
                     }
            }
         }
      }

   else if (pCurrSCCB->Sccb_XferCnt) {

      if (pCurrSCCB->Sccb_XferState & F_SG_XFER) {


              WR_HARPOON(port+hp_page_ctrl, (RD_HARPOON(port+hp_page_ctrl) &
            ~SCATTER_EN));

         WR_HARPOON(port+hp_sg_addr,0x00);

         sg_ptr = pCurrSCCB->Sccb_sgseg + SG_BUF_CNT;

         if (sg_ptr > (UINT)(pCurrSCCB->DataLength / SG_ELEMENT_SIZE)) {

            sg_ptr = (UINT)(pCurrSCCB->DataLength / SG_ELEMENT_SIZE);
            }

         remain_cnt = pCurrSCCB->Sccb_XferCnt;

         while (remain_cnt < 0x01000000L) {

            sg_ptr--;

#if defined(COMPILER_16_BIT) && !defined(DOS)
            if (remain_cnt > (ULONG)(*(((ULONG far *)pCurrSCCB->
               DataPointer) + (sg_ptr * 2)))) {

               remain_cnt -= (ULONG)(*(((ULONG far *)pCurrSCCB->
                  DataPointer) + (sg_ptr * 2)));
               }

#else
            if (remain_cnt > (ULONG)(*(((ULONG *)pCurrSCCB->
               DataPointer) + (sg_ptr * 2)))) {

               remain_cnt -= (ULONG)(*(((ULONG *)pCurrSCCB->
                  DataPointer) + (sg_ptr * 2)));
               }
#endif

            else {

               break;
               }
            }



         if (remain_cnt < 0x01000000L) {


            pCurrSCCB->Sccb_SGoffset = remain_cnt;

            pCurrSCCB->Sccb_sgseg = (USHORT)sg_ptr;


            if ((ULONG)(sg_ptr * SG_ELEMENT_SIZE) == pCurrSCCB->DataLength 
                && (remain_cnt == 0))

               pCurrSCCB->Sccb_XferState |= F_ALL_XFERRED;
            }

         else {


            if (pCurrSCCB->HostStatus == 0x00) {

               pCurrSCCB->HostStatus = SCCB_GROSS_FW_ERR;
               }
            }
         }


      if (!(pCurrSCCB->Sccb_XferState & F_HOST_XFER_DIR)) {


         if (RD_HARPOON(port+hp_ext_status) & BM_CMD_BUSY) {

            busMstrTimeOut(port);
            }

         else {

            if (RD_HARPOON(port+hp_int_status) & INT_EXT_STATUS) {

               if (RD_HARPOON(port+hp_ext_status) & BAD_EXT_STATUS) {

                  if (pCurrSCCB->HostStatus == 0x00) {

                     pCurrSCCB->HostStatus = SCCB_BM_ERR;
#if defined(BUGBUG)
                     WR_HARPOON(port+hp_dual_addr_lo,
                        RD_HARPOON(port+hp_ext_status));
#endif
                     }
                  }
               }

            }
         }

      else {


         if ((RD_HARPOON(port+hp_fifo_cnt)) >= BM_THRESHOLD) {

            timeout = SHORT_WAIT;

            while ((RD_HARPOON(port+hp_ext_status) & BM_CMD_BUSY) &&
               ((RD_HARPOON(port+hp_fifo_cnt)) >= BM_THRESHOLD) &&
               timeout--) {}
            }

         if (RD_HARPOON(port+hp_ext_status) & BM_CMD_BUSY) {

            WR_HARPOON(port+hp_bm_ctrl, (RD_HARPOON(port+hp_bm_ctrl) |
               FLUSH_XFER_CNTR));

            timeout = LONG_WAIT;

            while ((RD_HARPOON(port+hp_ext_status) & BM_CMD_BUSY) &&
               timeout--) {}

            WR_HARPOON(port+hp_bm_ctrl, (RD_HARPOON(port+hp_bm_ctrl) &
               ~FLUSH_XFER_CNTR));


            if (RD_HARPOON(port+hp_ext_status) & BM_CMD_BUSY) {

               if (pCurrSCCB->HostStatus == 0x00) {

                  pCurrSCCB->HostStatus = SCCB_BM_ERR;
                  }

               busMstrTimeOut(port);
               }
            }

         if (RD_HARPOON(port+hp_int_status) & INT_EXT_STATUS) {

            if (RD_HARPOON(port+hp_ext_status) & BAD_EXT_STATUS) {

               if (pCurrSCCB->HostStatus == 0x00) {

                  pCurrSCCB->HostStatus = SCCB_BM_ERR;
#if defined(BUGBUG)
                  WR_HARPOON(port+hp_dual_addr_lo,
                     RD_HARPOON(port+hp_ext_status));
#endif
                  }
               }
            }
         }

      }

   else {


      if (RD_HARPOON(port+hp_ext_status) & BM_CMD_BUSY) {

         timeout = LONG_WAIT;

         while ((RD_HARPOON(port+hp_ext_status) & BM_CMD_BUSY) && timeout--) {}

         if (RD_HARPOON(port+hp_ext_status) & BM_CMD_BUSY) {

            if (pCurrSCCB->HostStatus == 0x00) {

               pCurrSCCB->HostStatus = SCCB_BM_ERR;
               }

            busMstrTimeOut(port);
            }
         }


      if (RD_HARPOON(port+hp_int_status) & INT_EXT_STATUS) {

         if (RD_HARPOON(port+hp_ext_status) & BAD_EXT_STATUS) {

            if (pCurrSCCB->HostStatus == 0x00) {

               pCurrSCCB->HostStatus = SCCB_BM_ERR;
#if defined(BUGBUG)
               WR_HARPOON(port+hp_dual_addr_lo,
                  RD_HARPOON(port+hp_ext_status));
#endif
               }
            }

         }

      if (pCurrSCCB->Sccb_XferState & F_SG_XFER) {

         WR_HARPOON(port+hp_page_ctrl, (RD_HARPOON(port+hp_page_ctrl) &
                 ~SCATTER_EN));

         WR_HARPOON(port+hp_sg_addr,0x00);

         pCurrSCCB->Sccb_sgseg += SG_BUF_CNT;

         pCurrSCCB->Sccb_SGoffset = 0x00; 


         if ((ULONG)(pCurrSCCB->Sccb_sgseg * SG_ELEMENT_SIZE) >=
            pCurrSCCB->DataLength) {

            pCurrSCCB->Sccb_XferState |= F_ALL_XFERRED;

            pCurrSCCB->Sccb_sgseg = (USHORT)(pCurrSCCB->DataLength / SG_ELEMENT_SIZE);

            }
         }

      else {

         if (!(pCurrSCCB->Sccb_XferState & F_AUTO_SENSE))

            pCurrSCCB->Sccb_XferState |= F_ALL_XFERRED;
         }
      }

   WR_HARPOON(port+hp_int_mask,(INT_CMD_COMPL | SCSI_INTERRUPT));
}



/*---------------------------------------------------------------------
 *
 * Function: Host Data Transfer Restart
 *
 * Description: Reset the available count due to a restore data
 *              pointers message.
 *
 *---------------------------------------------------------------------*/
void hostDataXferRestart(PSCCB currSCCB)
{
   ULONG data_count;
   UINT  sg_index;
#if defined(COMPILER_16_BIT) && !defined(DOS)
   ULONG far *sg_ptr;
#else
   ULONG *sg_ptr;
#endif

   if (currSCCB->Sccb_XferState & F_SG_XFER) {

      currSCCB->Sccb_XferCnt = 0;

      sg_index = 0xffff;         /*Index by long words into sg list. */
      data_count = 0;            /*Running count of SG xfer counts. */

#if defined(COMPILER_16_BIT) && !defined(DOS)
      sg_ptr = (ULONG far *)currSCCB->DataPointer;
#else
      sg_ptr = (ULONG *)currSCCB->DataPointer;
#endif

      while (data_count < currSCCB->Sccb_ATC) {

         sg_index++;
         data_count += *(sg_ptr+(sg_index * 2));
         }

      if (data_count == currSCCB->Sccb_ATC) {

         currSCCB->Sccb_SGoffset = 0;
         sg_index++;
         }

      else {
         currSCCB->Sccb_SGoffset = data_count - currSCCB->Sccb_ATC;
         }

      currSCCB->Sccb_sgseg = (USHORT)sg_index;
      }

   else {
      currSCCB->Sccb_XferCnt = currSCCB->DataLength - currSCCB->Sccb_ATC;
      }
}
#ident "$Id: scam.c 1.17 1997/03/20 23:49:37 mohan Exp $"
/*----------------------------------------------------------------------
 *
 *
 *   Copyright 1995-1996 by Mylex Corporation.  All Rights Reserved
 *
 *   This file is available under both the GNU General Public License
 *   and a BSD-style copyright; see LICENSE.FlashPoint for details.
 *
 *   $Workfile:   scam.c  $
 *
 *   Description:  Functions relating to handling of the SCAM selection
 *                 and the determination of the SCSI IDs to be assigned
 *                 to all perspective SCSI targets.
 *
 *   $Date: 1997/03/20 23:49:37 $
 *
 *   $Revision: 1.17 $
 *
 *----------------------------------------------------------------------*/

/*#include <globals.h>*/

#if (FW_TYPE==_UCB_MGR_)
	/*#include <budi.h>*/
#endif

/*#include <sccbmgr.h>*/
/*#include <blx30.h>*/
/*#include <target.h>*/
/*#include <scsi2.h>*/
/*#include <eeprom.h>*/
/*#include <harpoon.h>*/



/*
extern SCCBMGR_TAR_INFO sccbMgrTbl[MAX_CARDS][MAX_SCSI_TAR];
extern SCCBCARD BL_Card[MAX_CARDS];
extern SCCBSCAM_INFO scamInfo[MAX_SCSI_TAR];
extern NVRAMINFO nvRamInfo[MAX_MB_CARDS];
#if defined(DOS) || defined(OS2)
extern UCHAR temp_id_string[ID_STRING_LENGTH];
#endif
extern UCHAR scamHAString[];
*/
/*---------------------------------------------------------------------
 *
 * Function: scini
 *
 * Description: Setup all data structures necessary for SCAM selection.
 *
 *---------------------------------------------------------------------*/

void scini(UCHAR p_card, UCHAR p_our_id, UCHAR p_power_up)
{

#if defined(SCAM_LEV_2)
   UCHAR loser,assigned_id;
#endif
#if defined(DOS)

   USHORT p_port;
#else
   ULONG p_port;
#endif

   UCHAR i,k,ScamFlg ;
   PSCCBcard currCard;
	PNVRamInfo pCurrNvRam;

   currCard = &BL_Card[p_card];
   p_port = currCard->ioPort;
	pCurrNvRam = currCard->pNvRamInfo;


	if(pCurrNvRam){
		ScamFlg = pCurrNvRam->niScamConf;
		i = pCurrNvRam->niSysConf;
	}
	else{
	   ScamFlg = (UCHAR) utilEERead(p_port, SCAM_CONFIG/2);
	   i = (UCHAR)(utilEERead(p_port, (SYSTEM_CONFIG/2)));
	}
	if(!(i & 0x02))	/* check if reset bus in AutoSCSI parameter set */
		return;

   inisci(p_card,p_port, p_our_id);

   /* Force to wait 1 sec after SCSI bus reset. Some SCAM device FW
      too slow to return to SCAM selection */

   /* if (p_power_up)
         Wait1Second(p_port);
      else
         Wait(p_port, TO_250ms); */

   Wait1Second(p_port);

#if defined(SCAM_LEV_2)

   if ((ScamFlg & SCAM_ENABLED) && (ScamFlg & SCAM_LEVEL2))
      {
      while (!(scarb(p_port,INIT_SELTD))) {}

      scsel(p_port);

      do {
         scxferc(p_port,SYNC_PTRN);
         scxferc(p_port,DOM_MSTR);
         loser = scsendi(p_port,&scamInfo[p_our_id].id_string[0]);
         } while ( loser == 0xFF );

      scbusf(p_port);

      if ((p_power_up) && (!loser))
         {
         sresb(p_port,p_card);
         Wait(p_port, TO_250ms);

         while (!(scarb(p_port,INIT_SELTD))) {}

         scsel(p_port);

         do {
            scxferc(p_port, SYNC_PTRN);
            scxferc(p_port, DOM_MSTR);
            loser = scsendi(p_port,&scamInfo[p_our_id].
               id_string[0]);
            } while ( loser == 0xFF );

         scbusf(p_port);
         }
      }

   else
      {
      loser = FALSE;
      }


   if (!loser)
      {

#endif  /* SCAM_LEV_2 */

      scamInfo[p_our_id].state = ID_ASSIGNED;


		if (ScamFlg & SCAM_ENABLED)
		{

	      for (i=0; i < MAX_SCSI_TAR; i++)
  		   {
      	   if ((scamInfo[i].state == ID_UNASSIGNED) ||
  	      	   (scamInfo[i].state == ID_UNUSED))
	  	      {
   	     	   if (scsell(p_port,i))
      	  	   {
            	   scamInfo[i].state = LEGACY;
  	            	if ((scamInfo[i].id_string[0] != 0xFF) ||
     	            	(scamInfo[i].id_string[1] != 0xFA))
	     	         {

   	        	      scamInfo[i].id_string[0] = 0xFF;
      	        	   scamInfo[i].id_string[1] = 0xFA;
							if(pCurrNvRam == NULL)
	         	         currCard->globalFlags |= F_UPDATE_EEPROM;
               	}
	  	         }
   	  	   }
      	}

	      sresb(p_port,p_card);
      	Wait1Second(p_port);
         while (!(scarb(p_port,INIT_SELTD))) {}
         scsel(p_port);
         scasid(p_card, p_port);
         }

#if defined(SCAM_LEV_2)

      }

   else if ((loser) && (ScamFlg & SCAM_ENABLED))
      {
      scamInfo[p_our_id].id_string[0] = SLV_TYPE_CODE0;
      assigned_id = FALSE;
      scwtsel(p_port);

      do {
         while (scxferc(p_port,0x00) != SYNC_PTRN) {}

         i = scxferc(p_port,0x00);
         if (i == ASSIGN_ID)
            {
            if (!(scsendi(p_port,&scamInfo[p_our_id].id_string[0])))
                  {
                  i = scxferc(p_port,0x00);
                  if (scvalq(i))
                     {
                     k = scxferc(p_port,0x00);

                     if (scvalq(k))
                        {
                        currCard->ourId =
                           ((UCHAR)(i<<3)+(k & (UCHAR)7)) & (UCHAR) 0x3F;
                        inisci(p_card, p_port, p_our_id);
                        scamInfo[currCard->ourId].state = ID_ASSIGNED;
                        scamInfo[currCard->ourId].id_string[0]
                           = SLV_TYPE_CODE0;
                        assigned_id = TRUE;
                        }
                     }
                  }
            }

         else if (i == SET_P_FLAG)
            {
               if (!(scsendi(p_port,
                        &scamInfo[p_our_id].id_string[0])))
                        scamInfo[p_our_id].id_string[0] |= 0x80;
            }
         }while (!assigned_id);

      while (scxferc(p_port,0x00) != CFG_CMPLT) {}
      }

#endif   /* SCAM_LEV_2 */
   if (ScamFlg & SCAM_ENABLED)
      {
      scbusf(p_port);
      if (currCard->globalFlags & F_UPDATE_EEPROM)
         {
         scsavdi(p_card, p_port);
         currCard->globalFlags &= ~F_UPDATE_EEPROM;
         }
      }


#if defined(DOS)
   for (i=0; i < MAX_SCSI_TAR; i++)
   {
     	if (((ScamFlg & SCAM_ENABLED) && (scamInfo[i].state == LEGACY))
			|| (i != p_our_id))
        	{
         scsellDOS(p_port,i);
  	      }
  	}
#endif

/*
   for (i=0,k=0; i < MAX_SCSI_TAR; i++)
      {
      if ((scamInfo[i].state == ID_ASSIGNED) ||
         (scamInfo[i].state == LEGACY))
         k++;
      }

   if (k==2)
      currCard->globalFlags |= F_SINGLE_DEVICE;
   else
      currCard->globalFlags &= ~F_SINGLE_DEVICE;
*/
}


/*---------------------------------------------------------------------
 *
 * Function: scarb
 *
 * Description: Gain control of the bus and wait SCAM select time (250ms)
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
int scarb(USHORT p_port, UCHAR p_sel_type)
#else
int scarb(ULONG p_port, UCHAR p_sel_type)
#endif
{
   if (p_sel_type == INIT_SELTD)
      {

      while (RD_HARPOON(p_port+hp_scsisig) & (SCSI_SEL | SCSI_BSY)) {}


      if (RD_HARPOON(p_port+hp_scsisig) & SCSI_SEL)
         return(FALSE);

      if (RD_HARPOON(p_port+hp_scsidata_0) != 00)
         return(FALSE);

      WR_HARPOON(p_port+hp_scsisig, (RD_HARPOON(p_port+hp_scsisig) | SCSI_BSY));

      if (RD_HARPOON(p_port+hp_scsisig) & SCSI_SEL) {

         WR_HARPOON(p_port+hp_scsisig, (RD_HARPOON(p_port+hp_scsisig) &
            ~SCSI_BSY));
         return(FALSE);
         }


      WR_HARPOON(p_port+hp_scsisig, (RD_HARPOON(p_port+hp_scsisig) | SCSI_SEL));

      if (RD_HARPOON(p_port+hp_scsidata_0) != 00) {

         WR_HARPOON(p_port+hp_scsisig, (RD_HARPOON(p_port+hp_scsisig) &
            ~(SCSI_BSY | SCSI_SEL)));
         return(FALSE);
         }
      }


   WR_HARPOON(p_port+hp_clkctrl_0, (RD_HARPOON(p_port+hp_clkctrl_0)
      & ~ACTdeassert));
   WR_HARPOON(p_port+hp_scsireset, SCAM_EN);
   WR_HARPOON(p_port+hp_scsidata_0, 0x00);
#if defined(WIDE_SCSI)
   WR_HARPOON(p_port+hp_scsidata_1, 0x00);
#endif
   WR_HARPOON(p_port+hp_portctrl_0, SCSI_BUS_EN);

   WR_HARPOON(p_port+hp_scsisig, (RD_HARPOON(p_port+hp_scsisig) | SCSI_MSG));

   WR_HARPOON(p_port+hp_scsisig, (RD_HARPOON(p_port+hp_scsisig)
      & ~SCSI_BSY));

   Wait(p_port,TO_250ms);

   return(TRUE);
}


/*---------------------------------------------------------------------
 *
 * Function: scbusf
 *
 * Description: Release the SCSI bus and disable SCAM selection.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void scbusf(USHORT p_port)
#else
void scbusf(ULONG p_port)
#endif
{
   WR_HARPOON(p_port+hp_page_ctrl,
      (RD_HARPOON(p_port+hp_page_ctrl) | G_INT_DISABLE));


   WR_HARPOON(p_port+hp_scsidata_0, 0x00);

   WR_HARPOON(p_port+hp_portctrl_0, (RD_HARPOON(p_port+hp_portctrl_0)
      & ~SCSI_BUS_EN));

   WR_HARPOON(p_port+hp_scsisig, 0x00);


   WR_HARPOON(p_port+hp_scsireset,  (RD_HARPOON(p_port+hp_scsireset)
      & ~SCAM_EN));

   WR_HARPOON(p_port+hp_clkctrl_0, (RD_HARPOON(p_port+hp_clkctrl_0)
      | ACTdeassert));

#if defined(SCAM_LEV_2)
   WRW_HARPOON((p_port+hp_intstat), (BUS_FREE | AUTO_INT | SCAM_SEL));
#else
   WRW_HARPOON((p_port+hp_intstat), (BUS_FREE | AUTO_INT));
#endif

   WR_HARPOON(p_port+hp_page_ctrl,
      (RD_HARPOON(p_port+hp_page_ctrl) & ~G_INT_DISABLE));
}



/*---------------------------------------------------------------------
 *
 * Function: scasid
 *
 * Description: Assign an ID to all the SCAM devices.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void scasid(UCHAR p_card, USHORT p_port)
#else
void scasid(UCHAR p_card, ULONG p_port)
#endif
{
#if defined(DOS) || defined(OS2)
   /* Use external defined in global space area, instead of Stack
      space. WIN/95 DOS doesnot work TINY mode. The OS doesnot intialize
      SS equal to DS. Thus the array allocated on stack doesnot get
      access correctly */
#else
   UCHAR temp_id_string[ID_STRING_LENGTH];
#endif

   UCHAR i,k,scam_id;
	UCHAR crcBytes[3];
	PNVRamInfo pCurrNvRam;
	ushort_ptr pCrcBytes;

	pCurrNvRam = BL_Card[p_card].pNvRamInfo;

   i=FALSE;

   while (!i)
      {

      for (k=0; k < ID_STRING_LENGTH; k++)
         {
         temp_id_string[k] = (UCHAR) 0x00;
         }

      scxferc(p_port,SYNC_PTRN);
      scxferc(p_port,ASSIGN_ID);

      if (!(sciso(p_port,&temp_id_string[0])))
         {
			if(pCurrNvRam){
				pCrcBytes = (ushort_ptr)&crcBytes[0];
				*pCrcBytes = CalcCrc16(&temp_id_string[0]);
				crcBytes[2] = CalcLrc(&temp_id_string[0]);
				temp_id_string[1] = crcBytes[2];
				temp_id_string[2] = crcBytes[0];
				temp_id_string[3] = crcBytes[1];
				for(k = 4; k < ID_STRING_LENGTH; k++)
					temp_id_string[k] = (UCHAR) 0x00;
			}
         i = scmachid(p_card,temp_id_string);

         if (i == CLR_PRIORITY)
            {
            scxferc(p_port,MISC_CODE);
            scxferc(p_port,CLR_P_FLAG);
            i = FALSE;  /*Not the last ID yet. */
            }

         else if (i != NO_ID_AVAIL)
            {
            if (i < 8 )
               scxferc(p_port,ID_0_7);
            else
               scxferc(p_port,ID_8_F);

            scam_id = (i & (UCHAR) 0x07);


            for (k=1; k < 0x08; k <<= 1)
               if (!( k & i ))
                  scam_id += 0x08;        /*Count number of zeros in DB0-3. */

            scxferc(p_port,scam_id);

            i = FALSE;  /*Not the last ID yet. */
            }
         }

      else
         {
         i = TRUE;
         }

      }  /*End while */

   scxferc(p_port,SYNC_PTRN);
   scxferc(p_port,CFG_CMPLT);
}





/*---------------------------------------------------------------------
 *
 * Function: scsel
 *
 * Description: Select all the SCAM devices.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void scsel(USHORT p_port)
#else
void scsel(ULONG p_port)
#endif
{

   WR_HARPOON(p_port+hp_scsisig, SCSI_SEL);
   scwiros(p_port, SCSI_MSG);

   WR_HARPOON(p_port+hp_scsisig, (SCSI_SEL | SCSI_BSY));


   WR_HARPOON(p_port+hp_scsisig, (SCSI_SEL | SCSI_BSY | SCSI_IOBIT | SCSI_CD));
   WR_HARPOON(p_port+hp_scsidata_0, (UCHAR)(RD_HARPOON(p_port+hp_scsidata_0) |
      (UCHAR)(BIT(7)+BIT(6))));


   WR_HARPOON(p_port+hp_scsisig, (SCSI_BSY | SCSI_IOBIT | SCSI_CD));
   scwiros(p_port, SCSI_SEL);

   WR_HARPOON(p_port+hp_scsidata_0, (UCHAR)(RD_HARPOON(p_port+hp_scsidata_0) &
      ~(UCHAR)BIT(6)));
   scwirod(p_port, BIT(6));

   WR_HARPOON(p_port+hp_scsisig, (SCSI_SEL | SCSI_BSY | SCSI_IOBIT | SCSI_CD));
}



/*---------------------------------------------------------------------
 *
 * Function: scxferc
 *
 * Description: Handshake the p_data (DB4-0) across the bus.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
UCHAR scxferc(USHORT p_port, UCHAR p_data)
#else
UCHAR scxferc(ULONG p_port, UCHAR p_data)
#endif
{
   UCHAR curr_data, ret_data;

   curr_data = p_data | BIT(7) | BIT(5);   /*Start with DB7 & DB5 asserted. */

   WR_HARPOON(p_port+hp_scsidata_0, curr_data);

   curr_data &= ~BIT(7);

   WR_HARPOON(p_port+hp_scsidata_0, curr_data);

   scwirod(p_port,BIT(7));              /*Wait for DB7 to be released. */
	while (!(RD_HARPOON(p_port+hp_scsidata_0) & BIT(5)));

   ret_data = (RD_HARPOON(p_port+hp_scsidata_0) & (UCHAR) 0x1F);

   curr_data |= BIT(6);

   WR_HARPOON(p_port+hp_scsidata_0, curr_data);

   curr_data &= ~BIT(5);

   WR_HARPOON(p_port+hp_scsidata_0, curr_data);

   scwirod(p_port,BIT(5));              /*Wait for DB5 to be released. */

   curr_data &= ~(BIT(4)|BIT(3)|BIT(2)|BIT(1)|BIT(0)); /*Release data bits */
   curr_data |= BIT(7);

   WR_HARPOON(p_port+hp_scsidata_0, curr_data);

   curr_data &= ~BIT(6);

   WR_HARPOON(p_port+hp_scsidata_0, curr_data);

   scwirod(p_port,BIT(6));              /*Wait for DB6 to be released. */

   return(ret_data);
}


/*---------------------------------------------------------------------
 *
 * Function: scsendi
 *
 * Description: Transfer our Identification string to determine if we
 *              will be the dominant master.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
UCHAR scsendi(USHORT p_port, UCHAR p_id_string[])
#else
UCHAR scsendi(ULONG p_port, UCHAR p_id_string[])
#endif
{
   UCHAR ret_data,byte_cnt,bit_cnt,defer;

   defer = FALSE;

   for (byte_cnt = 0; byte_cnt < ID_STRING_LENGTH; byte_cnt++) {

      for (bit_cnt = 0x80; bit_cnt != 0 ; bit_cnt >>= 1) {

         if (defer)
            ret_data = scxferc(p_port,00);

         else if (p_id_string[byte_cnt] & bit_cnt)

               ret_data = scxferc(p_port,02);

            else {

               ret_data = scxferc(p_port,01);
               if (ret_data & 02)
                  defer = TRUE;
               }

         if ((ret_data & 0x1C) == 0x10)
            return(0x00);  /*End of isolation stage, we won! */

         if (ret_data & 0x1C)
            return(0xFF);

         if ((defer) && (!(ret_data & 0x1F)))
            return(0x01);  /*End of isolation stage, we lost. */

         } /*bit loop */

      } /*byte loop */

   if (defer)
      return(0x01);  /*We lost */
   else
      return(0);  /*We WON! Yeeessss! */
}



/*---------------------------------------------------------------------
 *
 * Function: sciso
 *
 * Description: Transfer the Identification string.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
UCHAR sciso(USHORT p_port, UCHAR p_id_string[])
#else
UCHAR sciso(ULONG p_port, UCHAR p_id_string[])
#endif
{
   UCHAR ret_data,the_data,byte_cnt,bit_cnt;

   the_data = 0;

   for (byte_cnt = 0; byte_cnt < ID_STRING_LENGTH; byte_cnt++) {

      for (bit_cnt = 0; bit_cnt < 8; bit_cnt++) {

         ret_data = scxferc(p_port,0);

         if (ret_data & 0xFC)
            return(0xFF);

         else {

            the_data <<= 1;
            if (ret_data & BIT(1)) {
               the_data |= 1;
               }
            }

         if ((ret_data & 0x1F) == 0)
	   {
/*
				if(bit_cnt != 0 || bit_cnt != 8)
				{
					byte_cnt = 0;
					bit_cnt = 0;
					scxferc(p_port, SYNC_PTRN);
					scxferc(p_port, ASSIGN_ID);
					continue;
				}
*/
            if (byte_cnt)
               return(0x00);
            else
               return(0xFF);
	   }

         } /*bit loop */

      p_id_string[byte_cnt] = the_data;

      } /*byte loop */

   return(0);
}



/*---------------------------------------------------------------------
 *
 * Function: scwirod
 *
 * Description: Sample the SCSI data bus making sure the signal has been
 *              deasserted for the correct number of consecutive samples.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void scwirod(USHORT p_port, UCHAR p_data_bit)
#else
void scwirod(ULONG p_port, UCHAR p_data_bit)
#endif
{
   UCHAR i;

   i = 0;
   while ( i < MAX_SCSI_TAR ) {

      if (RD_HARPOON(p_port+hp_scsidata_0) & p_data_bit)

         i = 0;

      else

         i++;

      }
}



/*---------------------------------------------------------------------
 *
 * Function: scwiros
 *
 * Description: Sample the SCSI Signal lines making sure the signal has been
 *              deasserted for the correct number of consecutive samples.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void scwiros(USHORT p_port, UCHAR p_data_bit)
#else
void scwiros(ULONG p_port, UCHAR p_data_bit)
#endif
{
   UCHAR i;

   i = 0;
   while ( i < MAX_SCSI_TAR ) {

      if (RD_HARPOON(p_port+hp_scsisig) & p_data_bit)

         i = 0;

      else

         i++;

      }
}


/*---------------------------------------------------------------------
 *
 * Function: scvalq
 *
 * Description: Make sure we received a valid data byte.
 *
 *---------------------------------------------------------------------*/

UCHAR scvalq(UCHAR p_quintet)
{
   UCHAR count;

   for (count=1; count < 0x08; count<<=1) {
      if (!(p_quintet & count))
         p_quintet -= 0x80;
      }

   if (p_quintet & 0x18)
      return(FALSE);

   else
      return(TRUE);
}


/*---------------------------------------------------------------------
 *
 * Function: scsell
 *
 * Description: Select the specified device ID using a selection timeout
 *              less than 4ms.  If somebody responds then it is a legacy
 *              drive and this ID must be marked as such.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
UCHAR scsell(USHORT p_port, UCHAR targ_id)
#else
UCHAR scsell(ULONG p_port, UCHAR targ_id)
#endif
{
#if defined(DOS)
   USHORT i;
#else
   ULONG i;
#endif

   WR_HARPOON(p_port+hp_page_ctrl,
      (RD_HARPOON(p_port+hp_page_ctrl) | G_INT_DISABLE));

   ARAM_ACCESS(p_port);

   WR_HARPOON(p_port+hp_addstat,(RD_HARPOON(p_port+hp_addstat) | SCAM_TIMER));
   WR_HARPOON(p_port+hp_seltimeout,TO_4ms);


   for (i = p_port+CMD_STRT; i < p_port+CMD_STRT+12; i+=2) {
      WRW_HARPOON(i, (MPM_OP+ACOMMAND));
      }
   WRW_HARPOON(i, (BRH_OP+ALWAYS+    NP));

   WRW_HARPOON((p_port+hp_intstat),
	       (RESET | TIMEOUT | SEL | BUS_FREE | AUTO_INT));

   WR_HARPOON(p_port+hp_select_id, targ_id);

   WR_HARPOON(p_port+hp_portctrl_0, SCSI_PORT);
   WR_HARPOON(p_port+hp_autostart_3, (SELECT | CMD_ONLY_STRT));
   WR_HARPOON(p_port+hp_scsictrl_0, (SEL_TAR | ENA_RESEL));


   while (!(RDW_HARPOON((p_port+hp_intstat)) &
	    (RESET | PROG_HLT | TIMEOUT | AUTO_INT))) {}

   if (RDW_HARPOON((p_port+hp_intstat)) & RESET)
         Wait(p_port, TO_250ms);

   DISABLE_AUTO(p_port);

   WR_HARPOON(p_port+hp_addstat,(RD_HARPOON(p_port+hp_addstat) & ~SCAM_TIMER));
   WR_HARPOON(p_port+hp_seltimeout,TO_290ms);

   SGRAM_ACCESS(p_port);

   if (RDW_HARPOON((p_port+hp_intstat)) & (RESET | TIMEOUT) ) {

      WRW_HARPOON((p_port+hp_intstat),
		  (RESET | TIMEOUT | SEL | BUS_FREE | PHASE));

      WR_HARPOON(p_port+hp_page_ctrl,
         (RD_HARPOON(p_port+hp_page_ctrl) & ~G_INT_DISABLE));

      return(FALSE);  /*No legacy device */
      }

   else {

      while(!(RDW_HARPOON((p_port+hp_intstat)) & BUS_FREE)) {
				if (RD_HARPOON(p_port+hp_scsisig) & SCSI_REQ)
					{
					WR_HARPOON(p_port+hp_scsisig, (SCSI_ACK + S_ILL_PH));
      			ACCEPT_MSG(p_port);
					}
		}

      WRW_HARPOON((p_port+hp_intstat), CLR_ALL_INT_1);

      WR_HARPOON(p_port+hp_page_ctrl,
         (RD_HARPOON(p_port+hp_page_ctrl) & ~G_INT_DISABLE));

      return(TRUE);  /*Found one of them oldies! */
      }
}

#if defined(DOS)
/*---------------------------------------------------------------------
 *
 * Function: scsell for DOS
 *
 * Description: Select the specified device ID using a selection timeout
 *              less than 2ms.  This was specially required to solve
 *              the problem with Plextor 12X CD-ROM drive. This drive
 *					 was responding the Selection at the end of 4ms and 
 *					 hanging the system.
 *
 *---------------------------------------------------------------------*/

UCHAR scsellDOS(USHORT p_port, UCHAR targ_id)
{
   USHORT i;

   WR_HARPOON(p_port+hp_page_ctrl,
      (RD_HARPOON(p_port+hp_page_ctrl) | G_INT_DISABLE));

   ARAM_ACCESS(p_port);

   WR_HARPOON(p_port+hp_addstat,(RD_HARPOON(p_port+hp_addstat) | SCAM_TIMER));
   WR_HARPOON(p_port+hp_seltimeout,TO_2ms);


   for (i = p_port+CMD_STRT; i < p_port+CMD_STRT+12; i+=2) {
      WRW_HARPOON(i, (MPM_OP+ACOMMAND));
      }
   WRW_HARPOON(i, (BRH_OP+ALWAYS+    NP));

   WRW_HARPOON((p_port+hp_intstat),
	       (RESET | TIMEOUT | SEL | BUS_FREE | AUTO_INT));

   WR_HARPOON(p_port+hp_select_id, targ_id);

   WR_HARPOON(p_port+hp_portctrl_0, SCSI_PORT);
   WR_HARPOON(p_port+hp_autostart_3, (SELECT | CMD_ONLY_STRT));
   WR_HARPOON(p_port+hp_scsictrl_0, (SEL_TAR | ENA_RESEL));


   while (!(RDW_HARPOON((p_port+hp_intstat)) &
	    (RESET | PROG_HLT | TIMEOUT | AUTO_INT))) {}

   if (RDW_HARPOON((p_port+hp_intstat)) & RESET)
         Wait(p_port, TO_250ms);

   DISABLE_AUTO(p_port);

   WR_HARPOON(p_port+hp_addstat,(RD_HARPOON(p_port+hp_addstat) & ~SCAM_TIMER));
   WR_HARPOON(p_port+hp_seltimeout,TO_290ms);

   SGRAM_ACCESS(p_port);

   if (RDW_HARPOON((p_port+hp_intstat)) & (RESET | TIMEOUT) ) {

      WRW_HARPOON((p_port+hp_intstat),
		  (RESET | TIMEOUT | SEL | BUS_FREE | PHASE));

      WR_HARPOON(p_port+hp_page_ctrl,
         (RD_HARPOON(p_port+hp_page_ctrl) & ~G_INT_DISABLE));

      return(FALSE);  /*No legacy device */
      }

   else {

      while(!(RDW_HARPOON((p_port+hp_intstat)) & BUS_FREE)) {
				if (RD_HARPOON(p_port+hp_scsisig) & SCSI_REQ)
					{
					WR_HARPOON(p_port+hp_scsisig, (SCSI_ACK + S_ILL_PH));
      			ACCEPT_MSG(p_port);
					}
		}

      WRW_HARPOON((p_port+hp_intstat), CLR_ALL_INT_1);

      WR_HARPOON(p_port+hp_page_ctrl,
         (RD_HARPOON(p_port+hp_page_ctrl) & ~G_INT_DISABLE));

      return(TRUE);  /*Found one of them oldies! */
      }
}
#endif  /* DOS */

/*---------------------------------------------------------------------
 *
 * Function: scwtsel
 *
 * Description: Wait to be selected by another SCAM initiator.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void scwtsel(USHORT p_port)
#else
void scwtsel(ULONG p_port)
#endif
{
   while(!(RDW_HARPOON((p_port+hp_intstat)) & SCAM_SEL)) {}
}


/*---------------------------------------------------------------------
 *
 * Function: inisci
 *
 * Description: Setup the data Structure with the info from the EEPROM.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void inisci(UCHAR p_card, USHORT p_port, UCHAR p_our_id)
#else
void inisci(UCHAR p_card, ULONG p_port, UCHAR p_our_id)
#endif
{
   UCHAR i,k,max_id;
   USHORT ee_data;
	PNVRamInfo pCurrNvRam;

	pCurrNvRam = BL_Card[p_card].pNvRamInfo;

   if (RD_HARPOON(p_port+hp_page_ctrl) & NARROW_SCSI_CARD)
      max_id = 0x08;

   else
      max_id = 0x10;

	if(pCurrNvRam){
		for(i = 0; i < max_id; i++){

			for(k = 0; k < 4; k++)
				scamInfo[i].id_string[k] = pCurrNvRam->niScamTbl[i][k];
			for(k = 4; k < ID_STRING_LENGTH; k++)
				scamInfo[i].id_string[k] = (UCHAR) 0x00;

	      if(scamInfo[i].id_string[0] == 0x00)
      	   scamInfo[i].state = ID_UNUSED;  /*Default to unused ID. */
	      else
   	      scamInfo[i].state = ID_UNASSIGNED;  /*Default to unassigned ID. */

		}
	}else {
	   for (i=0; i < max_id; i++)
   	   {
      	for (k=0; k < ID_STRING_LENGTH; k+=2)
	         {
   	      ee_data = utilEERead(p_port, (USHORT)((EE_SCAMBASE/2) +
      	     (USHORT) (i*((USHORT)ID_STRING_LENGTH/2)) + (USHORT)(k/2)));
         	scamInfo[i].id_string[k] = (UCHAR) ee_data;
	         ee_data >>= 8;
   	      scamInfo[i].id_string[k+1] = (UCHAR) ee_data;
      	   }

	      if ((scamInfo[i].id_string[0] == 0x00) ||
   	       (scamInfo[i].id_string[0] == 0xFF))

      	   scamInfo[i].state = ID_UNUSED;  /*Default to unused ID. */

	      else
   	      scamInfo[i].state = ID_UNASSIGNED;  /*Default to unassigned ID. */

      	}
	}
	for(k = 0; k < ID_STRING_LENGTH; k++)
		scamInfo[p_our_id].id_string[k] = scamHAString[k];

}

/*---------------------------------------------------------------------
 *
 * Function: scmachid
 *
 * Description: Match the Device ID string with our values stored in
 *              the EEPROM.
 *
 *---------------------------------------------------------------------*/

UCHAR scmachid(UCHAR p_card, UCHAR p_id_string[])
{

   UCHAR i,k,match;


   for (i=0; i < MAX_SCSI_TAR; i++) {

#if !defined(SCAM_LEV_2)
      if (scamInfo[i].state == ID_UNASSIGNED)
         {
#endif
         match = TRUE;

         for (k=0; k < ID_STRING_LENGTH; k++)
            {
            if (p_id_string[k] != scamInfo[i].id_string[k])
               match = FALSE;
            }

         if (match)
            {
            scamInfo[i].state = ID_ASSIGNED;
            return(i);
            }

#if !defined(SCAM_LEV_2)
         }
#endif

      }



   if (p_id_string[0] & BIT(5))
      i = 8;
   else
      i = MAX_SCSI_TAR;

   if (((p_id_string[0] & 0x06) == 0x02) || ((p_id_string[0] & 0x06) == 0x04))
      match = p_id_string[1] & (UCHAR) 0x1F;
   else
      match = 7;

   while (i > 0)
      {
      i--;

      if (scamInfo[match].state == ID_UNUSED)
         {
         for (k=0; k < ID_STRING_LENGTH; k++)
            {
            scamInfo[match].id_string[k] = p_id_string[k];
            }

         scamInfo[match].state = ID_ASSIGNED;

			if(BL_Card[p_card].pNvRamInfo == NULL)
	         BL_Card[p_card].globalFlags |= F_UPDATE_EEPROM;
         return(match);

         }


      match--;

      if (match == 0xFF)
	{
         if (p_id_string[0] & BIT(5))
            match = 7;
         else
            match = MAX_SCSI_TAR-1;
	}
      }



   if (p_id_string[0] & BIT(7))
      {
      return(CLR_PRIORITY);
      }


   if (p_id_string[0] & BIT(5))
      i = 8;
   else
      i = MAX_SCSI_TAR;

   if (((p_id_string[0] & 0x06) == 0x02) || ((p_id_string[0] & 0x06) == 0x04))
      match = p_id_string[1] & (UCHAR) 0x1F;
   else
      match = 7;

   while (i > 0)
      {

      i--;

      if (scamInfo[match].state == ID_UNASSIGNED)
         {
         for (k=0; k < ID_STRING_LENGTH; k++)
            {
            scamInfo[match].id_string[k] = p_id_string[k];
            }

         scamInfo[match].id_string[0] |= BIT(7);
         scamInfo[match].state = ID_ASSIGNED;
			if(BL_Card[p_card].pNvRamInfo == NULL)
	         BL_Card[p_card].globalFlags |= F_UPDATE_EEPROM;
         return(match);

         }


      match--;

      if (match == 0xFF)
	{
         if (p_id_string[0] & BIT(5))
            match = 7;
         else
            match = MAX_SCSI_TAR-1;
	}
      }

   return(NO_ID_AVAIL);
}


/*---------------------------------------------------------------------
 *
 * Function: scsavdi
 *
 * Description: Save off the device SCAM ID strings.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void scsavdi(UCHAR p_card, USHORT p_port)
#else
void scsavdi(UCHAR p_card, ULONG p_port)
#endif
{
   UCHAR i,k,max_id;
   USHORT ee_data,sum_data;


   sum_data = 0x0000;

   for (i = 1; i < EE_SCAMBASE/2; i++)
      {
      sum_data += utilEERead(p_port, i);
      }


   utilEEWriteOnOff(p_port,1);   /* Enable write access to the EEPROM */

   if (RD_HARPOON(p_port+hp_page_ctrl) & NARROW_SCSI_CARD)
      max_id = 0x08;

   else
      max_id = 0x10;

   for (i=0; i < max_id; i++)
      {

      for (k=0; k < ID_STRING_LENGTH; k+=2)
         {
         ee_data = scamInfo[i].id_string[k+1];
         ee_data <<= 8;
         ee_data |= scamInfo[i].id_string[k];
         sum_data += ee_data;
         utilEEWrite(p_port, ee_data, (USHORT)((EE_SCAMBASE/2) +
            (USHORT)(i*((USHORT)ID_STRING_LENGTH/2)) + (USHORT)(k/2)));
         }
      }


   utilEEWrite(p_port, sum_data, EEPROM_CHECK_SUM/2);
   utilEEWriteOnOff(p_port,0);   /* Turn off write access */
}
#ident "$Id: diagnose.c 1.10 1997/06/10 16:51:47 mohan Exp $"
/*----------------------------------------------------------------------
 *
 *
 *   Copyright 1995-1996 by Mylex Corporation.  All Rights Reserved
 *
 *   This file is available under both the GNU General Public License
 *   and a BSD-style copyright; see LICENSE.FlashPoint for details.
 *
 *   $Workfile:   diagnose.c  $
 *
 *   Description:  Diagnostic funtions for testing the integrity of
 *                 the HARPOON.
 *
 *   $Date: 1997/06/10 16:51:47 $
 *
 *   $Revision: 1.10 $
 *
 *----------------------------------------------------------------------*/

/*#include <globals.h>*/

#if (FW_TYPE==_UCB_MGR_)
	/*#include <budi.h>*/
#endif

/*#include <sccbmgr.h>*/
/*#include <blx30.h>*/
/*#include <target.h>*/
/*#include <eeprom.h>*/
/*#include <harpoon.h>*/

/*---------------------------------------------------------------------
 *
 * Function: XbowInit
 *
 * Description: Setup the Xbow for normal operation.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void XbowInit(USHORT port, UCHAR ScamFlg)
#else
void XbowInit(ULONG port, UCHAR ScamFlg)
#endif
{
UCHAR i;

	i = RD_HARPOON(port+hp_page_ctrl);
	WR_HARPOON(port+hp_page_ctrl, (UCHAR) (i | G_INT_DISABLE));

   WR_HARPOON(port+hp_scsireset,0x00);
   WR_HARPOON(port+hp_portctrl_1,HOST_MODE8);

   WR_HARPOON(port+hp_scsireset,(DMA_RESET | HPSCSI_RESET | PROG_RESET | \
				 FIFO_CLR));

   WR_HARPOON(port+hp_scsireset,SCSI_INI);

   WR_HARPOON(port+hp_clkctrl_0,CLKCTRL_DEFAULT);

   WR_HARPOON(port+hp_scsisig,0x00);         /*  Clear any signals we might */
   WR_HARPOON(port+hp_scsictrl_0,ENA_SCAM_SEL);

   WRW_HARPOON((port+hp_intstat), CLR_ALL_INT);

#if defined(SCAM_LEV_2)
   default_intena = RESET | RSEL | PROG_HLT | TIMEOUT |
		    BUS_FREE | XFER_CNT_0 | AUTO_INT;

   if ((ScamFlg & SCAM_ENABLED) && (ScamFlg & SCAM_LEVEL2))
		default_intena |= SCAM_SEL;

#else
   default_intena = RESET | RSEL | PROG_HLT | TIMEOUT |
		    BUS_FREE | XFER_CNT_0 | AUTO_INT;
#endif
   WRW_HARPOON((port+hp_intena), default_intena);

   WR_HARPOON(port+hp_seltimeout,TO_290ms);

   /* Turn on SCSI_MODE8 for narrow cards to fix the
      strapping issue with the DUAL CHANNEL card */
   if (RD_HARPOON(port+hp_page_ctrl) & NARROW_SCSI_CARD)
      WR_HARPOON(port+hp_addstat,SCSI_MODE8);

#if defined(NO_BIOS_OPTION)

   WR_HARPOON(port+hp_synctarg_0,NARROW_SCSI);
   WR_HARPOON(port+hp_synctarg_1,NARROW_SCSI);
   WR_HARPOON(port+hp_synctarg_2,NARROW_SCSI);
   WR_HARPOON(port+hp_synctarg_3,NARROW_SCSI);
   WR_HARPOON(port+hp_synctarg_4,NARROW_SCSI);
   WR_HARPOON(port+hp_synctarg_5,NARROW_SCSI);
   WR_HARPOON(port+hp_synctarg_6,NARROW_SCSI);
   WR_HARPOON(port+hp_synctarg_7,NARROW_SCSI);
   WR_HARPOON(port+hp_synctarg_8,NARROW_SCSI);
   WR_HARPOON(port+hp_synctarg_9,NARROW_SCSI);
   WR_HARPOON(port+hp_synctarg_10,NARROW_SCSI);
   WR_HARPOON(port+hp_synctarg_11,NARROW_SCSI);
   WR_HARPOON(port+hp_synctarg_12,NARROW_SCSI);
   WR_HARPOON(port+hp_synctarg_13,NARROW_SCSI);
   WR_HARPOON(port+hp_synctarg_14,NARROW_SCSI);
   WR_HARPOON(port+hp_synctarg_15,NARROW_SCSI);

#endif
	WR_HARPOON(port+hp_page_ctrl, i);

}


/*---------------------------------------------------------------------
 *
 * Function: BusMasterInit
 *
 * Description: Initialize the BusMaster for normal operations.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void BusMasterInit(USHORT p_port)
#else
void BusMasterInit(ULONG p_port)
#endif
{


   WR_HARPOON(p_port+hp_sys_ctrl, DRVR_RST);
   WR_HARPOON(p_port+hp_sys_ctrl, 0x00);

   WR_HARPOON(p_port+hp_host_blk_cnt, XFER_BLK64);


   WR_HARPOON(p_port+hp_bm_ctrl, (BMCTRL_DEFAULT));

   WR_HARPOON(p_port+hp_ee_ctrl, (SCSI_TERM_ENA_H));


#if defined(NT)

   WR_HARPOON(p_port+hp_pci_cmd_cfg, (RD_HARPOON(p_port+hp_pci_cmd_cfg)
      & ~MEM_SPACE_ENA));

#endif

   RD_HARPOON(p_port+hp_int_status);        /*Clear interrupts. */
   WR_HARPOON(p_port+hp_int_mask, (INT_CMD_COMPL | SCSI_INTERRUPT));
   WR_HARPOON(p_port+hp_page_ctrl, (RD_HARPOON(p_port+hp_page_ctrl) &
      ~SCATTER_EN));
}


/*---------------------------------------------------------------------
 *
 * Function: DiagXbow
 *
 * Description: Test Xbow integrity.  Non-zero return indicates an error.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
int DiagXbow(USHORT port)
#else
int DiagXbow(ULONG port)
#endif
{
   unsigned char fifo_cnt,loop_cnt;

   unsigned char fifodata[5];
   fifodata[0] = 0x00;
   fifodata[1] = 0xFF;
   fifodata[2] = 0x55;
   fifodata[3] = 0xAA;
   fifodata[4] = 0x00;


   WRW_HARPOON((port+hp_intstat), CLR_ALL_INT);
   WRW_HARPOON((port+hp_intena), 0x0000);

   WR_HARPOON(port+hp_seltimeout,TO_5ms);

   WR_HARPOON(port+hp_portctrl_0,START_TO);


   for(fifodata[4] = 0x01; fifodata[4] != (UCHAR) 0; fifodata[4] = fifodata[4] << 1) {

      WR_HARPOON(port+hp_selfid_0,fifodata[4]);
      WR_HARPOON(port+hp_selfid_1,fifodata[4]);

      if ((RD_HARPOON(port+hp_selfid_0) != fifodata[4]) ||
          (RD_HARPOON(port+hp_selfid_1) != fifodata[4]))
         return(1);
      }


   for(loop_cnt = 0; loop_cnt < 4; loop_cnt++) {

      WR_HARPOON(port+hp_portctrl_0,(HOST_PORT | HOST_WRT | START_TO));


      for (fifo_cnt = 0; fifo_cnt < FIFO_LEN; fifo_cnt++) {

         WR_HARPOON(port+hp_fifodata_0, fifodata[loop_cnt]);
         }


      if (!(RD_HARPOON(port+hp_xferstat) & FIFO_FULL))
         return(1);


      WR_HARPOON(port+hp_portctrl_0,(HOST_PORT | START_TO));

      for (fifo_cnt = 0; fifo_cnt < FIFO_LEN; fifo_cnt++) {

         if (RD_HARPOON(port+hp_fifodata_0) != fifodata[loop_cnt])
            return(1);
         }


      if (!(RD_HARPOON(port+hp_xferstat) & FIFO_EMPTY))
         return(1);
      }


   while(!(RDW_HARPOON((port+hp_intstat)) & TIMEOUT)) {}


   WR_HARPOON(port+hp_seltimeout,TO_290ms);

   WRW_HARPOON((port+hp_intstat), CLR_ALL_INT);

   WRW_HARPOON((port+hp_intena), default_intena);

   return(0);
}


/*---------------------------------------------------------------------
 *
 * Function: DiagBusMaster
 *
 * Description: Test BusMaster integrity.  Non-zero return indicates an
 *              error.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
int DiagBusMaster(USHORT port)
#else
int DiagBusMaster(ULONG port)
#endif
{
   UCHAR testdata;

   for(testdata = (UCHAR) 1; testdata != (UCHAR)0; testdata = testdata << 1) {

      WR_HARPOON(port+hp_xfer_cnt_lo,testdata);
      WR_HARPOON(port+hp_xfer_cnt_mi,testdata);
      WR_HARPOON(port+hp_xfer_cnt_hi,testdata);
      WR_HARPOON(port+hp_host_addr_lo,testdata);
      WR_HARPOON(port+hp_host_addr_lmi,testdata);
      WR_HARPOON(port+hp_host_addr_hmi,testdata);
      WR_HARPOON(port+hp_host_addr_hi,testdata);

      if ((RD_HARPOON(port+hp_xfer_cnt_lo) != testdata)   ||
          (RD_HARPOON(port+hp_xfer_cnt_mi) != testdata)   ||
          (RD_HARPOON(port+hp_xfer_cnt_hi) != testdata)   ||
          (RD_HARPOON(port+hp_host_addr_lo) != testdata)  ||
          (RD_HARPOON(port+hp_host_addr_lmi) != testdata) ||
          (RD_HARPOON(port+hp_host_addr_hmi) != testdata) ||
          (RD_HARPOON(port+hp_host_addr_hi) != testdata))

         return(1);
      }
   RD_HARPOON(port+hp_int_status);        /*Clear interrupts. */
   return(0);
}



/*---------------------------------------------------------------------
 *
 * Function: DiagEEPROM
 *
 * Description: Verfiy checksum and 'Key' and initialize the EEPROM if
 *              necessary.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void DiagEEPROM(USHORT p_port)
#else
void DiagEEPROM(ULONG p_port)
#endif

{
   USHORT index,temp,max_wd_cnt;

   if (RD_HARPOON(p_port+hp_page_ctrl) & NARROW_SCSI_CARD)
      max_wd_cnt = EEPROM_WD_CNT;
   else
      max_wd_cnt = EEPROM_WD_CNT * 2;

   temp = utilEERead(p_port, FW_SIGNATURE/2);

   if (temp == 0x4641) {

      for (index = 2; index < max_wd_cnt; index++) {

         temp += utilEERead(p_port, index);

         }

      if (temp == utilEERead(p_port, EEPROM_CHECK_SUM/2)) {

         return;          /*EEPROM is Okay so return now! */
         }
      }


   utilEEWriteOnOff(p_port,(UCHAR)1);

   for (index = 0; index < max_wd_cnt; index++) {

      utilEEWrite(p_port, 0x0000, index);
      }

   temp = 0;

   utilEEWrite(p_port, 0x4641, FW_SIGNATURE/2);
   temp += 0x4641;
   utilEEWrite(p_port, 0x3920, MODEL_NUMB_0/2);
   temp += 0x3920;
   utilEEWrite(p_port, 0x3033, MODEL_NUMB_2/2);
   temp += 0x3033;
   utilEEWrite(p_port, 0x2020, MODEL_NUMB_4/2);
   temp += 0x2020;
   utilEEWrite(p_port, 0x70D3, SYSTEM_CONFIG/2);
   temp += 0x70D3;
   utilEEWrite(p_port, 0x0010, BIOS_CONFIG/2);
   temp += 0x0010;
   utilEEWrite(p_port, 0x0003, SCAM_CONFIG/2);
   temp += 0x0003;
   utilEEWrite(p_port, 0x0007, ADAPTER_SCSI_ID/2);
   temp += 0x0007;

   utilEEWrite(p_port, 0x0000, IGNORE_B_SCAN/2);
   temp += 0x0000;
   utilEEWrite(p_port, 0x0000, SEND_START_ENA/2);
   temp += 0x0000;
   utilEEWrite(p_port, 0x0000, DEVICE_ENABLE/2);
   temp += 0x0000;

   utilEEWrite(p_port, 0x4242, SYNC_RATE_TBL01/2);
   temp += 0x4242;
   utilEEWrite(p_port, 0x4242, SYNC_RATE_TBL23/2);
   temp += 0x4242;
   utilEEWrite(p_port, 0x4242, SYNC_RATE_TBL45/2);
   temp += 0x4242;
   utilEEWrite(p_port, 0x4242, SYNC_RATE_TBL67/2);
   temp += 0x4242;
   utilEEWrite(p_port, 0x4242, SYNC_RATE_TBL89/2);
   temp += 0x4242;
   utilEEWrite(p_port, 0x4242, SYNC_RATE_TBLab/2);
   temp += 0x4242;
   utilEEWrite(p_port, 0x4242, SYNC_RATE_TBLcd/2);
   temp += 0x4242;
   utilEEWrite(p_port, 0x4242, SYNC_RATE_TBLef/2);
   temp += 0x4242;


   utilEEWrite(p_port, 0x6C46, 64/2);  /*PRODUCT ID */
   temp += 0x6C46;
   utilEEWrite(p_port, 0x7361, 66/2);  /* FlashPoint LT   */
   temp += 0x7361;
   utilEEWrite(p_port, 0x5068, 68/2);
   temp += 0x5068;
   utilEEWrite(p_port, 0x696F, 70/2);
   temp += 0x696F;
   utilEEWrite(p_port, 0x746E, 72/2);
   temp += 0x746E;
   utilEEWrite(p_port, 0x4C20, 74/2);
   temp += 0x4C20;
   utilEEWrite(p_port, 0x2054, 76/2);
   temp += 0x2054;
   utilEEWrite(p_port, 0x2020, 78/2);
   temp += 0x2020;

   index = ((EE_SCAMBASE/2)+(7*16));
   utilEEWrite(p_port, (0x0700+TYPE_CODE0), index);
   temp += (0x0700+TYPE_CODE0);
   index++;
   utilEEWrite(p_port, 0x5542, index);            /*Vendor ID code */
   temp += 0x5542;                                /* BUSLOGIC      */
   index++;
   utilEEWrite(p_port, 0x4C53, index);
   temp += 0x4C53;
   index++;
   utilEEWrite(p_port, 0x474F, index);
   temp += 0x474F;
   index++;
   utilEEWrite(p_port, 0x4349, index);
   temp += 0x4349;
   index++;
   utilEEWrite(p_port, 0x5442, index);            /*Vendor unique code */
   temp += 0x5442;                         /* BT- 930           */
   index++;
   utilEEWrite(p_port, 0x202D, index);
   temp += 0x202D;
   index++;
   utilEEWrite(p_port, 0x3339, index);
   temp += 0x3339;
   index++;                                 /*Serial #          */
   utilEEWrite(p_port, 0x2030, index);             /* 01234567         */
   temp += 0x2030;
   index++;
   utilEEWrite(p_port, 0x5453, index);
   temp += 0x5453;
   index++;
   utilEEWrite(p_port, 0x5645, index);
   temp += 0x5645;
   index++;
   utilEEWrite(p_port, 0x2045, index);
   temp += 0x2045;
   index++;
   utilEEWrite(p_port, 0x202F, index);
   temp += 0x202F;
   index++;
   utilEEWrite(p_port, 0x4F4A, index);
   temp += 0x4F4A;
   index++;
   utilEEWrite(p_port, 0x204E, index);
   temp += 0x204E;
   index++;
   utilEEWrite(p_port, 0x3539, index);
   temp += 0x3539;



   utilEEWrite(p_port, temp, EEPROM_CHECK_SUM/2);

   utilEEWriteOnOff(p_port,(UCHAR)0);

}

#ident "$Id: utility.c 1.23 1997/06/10 16:55:06 mohan Exp $"
/*----------------------------------------------------------------------
 *
 *
 *   Copyright 1995-1996 by Mylex Corporation.  All Rights Reserved
 *
 *   This file is available under both the GNU General Public License
 *   and a BSD-style copyright; see LICENSE.FlashPoint for details.
 *
 *   $Workfile:   utility.c  $
 *
 *   Description:  Utility functions relating to queueing and EEPROM
 *                 manipulation and any other garbage functions.
 *
 *   $Date: 1997/06/10 16:55:06 $
 *
 *   $Revision: 1.23 $
 *
 *----------------------------------------------------------------------*/
/*#include <globals.h>*/

#if (FW_TYPE==_UCB_MGR_)
	/*#include <budi.h>*/
#endif

/*#include <sccbmgr.h>*/
/*#include <blx30.h>*/
/*#include <target.h>*/
/*#include <scsi2.h>*/
/*#include <harpoon.h>*/


/*
extern SCCBCARD BL_Card[MAX_CARDS];
extern SCCBMGR_TAR_INFO sccbMgrTbl[MAX_CARDS][MAX_SCSI_TAR];
extern unsigned int SccbGlobalFlags;
*/

/*---------------------------------------------------------------------
 *
 * Function: Queue Search Select
 *
 * Description: Try to find a new command to execute.
 *
 *---------------------------------------------------------------------*/

void queueSearchSelect(PSCCBcard pCurrCard, UCHAR p_card)
{
   UCHAR scan_ptr, lun;
   PSCCBMgr_tar_info currTar_Info;
	PSCCB pOldSccb;

   scan_ptr = pCurrCard->scanIndex;
	do 
	{
		currTar_Info = &sccbMgrTbl[p_card][scan_ptr];
		if((pCurrCard->globalFlags & F_CONLUN_IO) && 
			((currTar_Info->TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING))
		{
			if (currTar_Info->TarSelQ_Cnt != 0)
			{

				scan_ptr++;
				if (scan_ptr == MAX_SCSI_TAR)
					scan_ptr = 0;
				
				for(lun=0; lun < MAX_LUN; lun++)
				{
					if(currTar_Info->TarLUNBusy[lun] == FALSE)
					{

						pCurrCard->currentSCCB = currTar_Info->TarSelQ_Head;
						pOldSccb = NULL;

						while((pCurrCard->currentSCCB != NULL) &&
								 (lun != pCurrCard->currentSCCB->Lun))
						{
							pOldSccb = pCurrCard->currentSCCB;
							pCurrCard->currentSCCB = (PSCCB)(pCurrCard->currentSCCB)->
																	Sccb_forwardlink;
						}
						if(pCurrCard->currentSCCB == NULL)
							continue;
						if(pOldSccb != NULL)
						{
							pOldSccb->Sccb_forwardlink = (PSCCB)(pCurrCard->currentSCCB)->
																	Sccb_forwardlink;
							pOldSccb->Sccb_backlink = (PSCCB)(pCurrCard->currentSCCB)->
																	Sccb_backlink;
							currTar_Info->TarSelQ_Cnt--;
						}
						else
						{
							currTar_Info->TarSelQ_Head = (PSCCB)(pCurrCard->currentSCCB)->Sccb_forwardlink;
					
							if (currTar_Info->TarSelQ_Head == NULL)
							{
								currTar_Info->TarSelQ_Tail = NULL;
								currTar_Info->TarSelQ_Cnt = 0;
							}
							else
							{
								currTar_Info->TarSelQ_Cnt--;
								currTar_Info->TarSelQ_Head->Sccb_backlink = (PSCCB)NULL;
							}
						}
					pCurrCard->scanIndex = scan_ptr;

					pCurrCard->globalFlags |= F_NEW_SCCB_CMD;

					break;
					}
				}
			}

			else 
			{
				scan_ptr++;
				if (scan_ptr == MAX_SCSI_TAR) {
					scan_ptr = 0;
				}
			}

		}
		else
		{
			if ((currTar_Info->TarSelQ_Cnt != 0) &&
				(currTar_Info->TarLUNBusy[0] == FALSE))
			{

				pCurrCard->currentSCCB = currTar_Info->TarSelQ_Head;

				currTar_Info->TarSelQ_Head = (PSCCB)(pCurrCard->currentSCCB)->Sccb_forwardlink;

				if (currTar_Info->TarSelQ_Head == NULL)
				{
					currTar_Info->TarSelQ_Tail = NULL;
					currTar_Info->TarSelQ_Cnt = 0;
				}
				else
				{
					currTar_Info->TarSelQ_Cnt--;
					currTar_Info->TarSelQ_Head->Sccb_backlink = (PSCCB)NULL;
				}

				scan_ptr++;
				if (scan_ptr == MAX_SCSI_TAR)
					scan_ptr = 0;

				pCurrCard->scanIndex = scan_ptr;

				pCurrCard->globalFlags |= F_NEW_SCCB_CMD;

				break;
			}

			else 
			{
				scan_ptr++;
				if (scan_ptr == MAX_SCSI_TAR) 
				{
					scan_ptr = 0;
				}
			}
		}
	} while (scan_ptr != pCurrCard->scanIndex);
}


/*---------------------------------------------------------------------
 *
 * Function: Queue Select Fail
 *
 * Description: Add the current SCCB to the head of the Queue.
 *
 *---------------------------------------------------------------------*/

void queueSelectFail(PSCCBcard pCurrCard, UCHAR p_card)
{
   UCHAR thisTarg;
   PSCCBMgr_tar_info currTar_Info;

   if (pCurrCard->currentSCCB != NULL)
	  {
	  thisTarg = (UCHAR)(((PSCCB)(pCurrCard->currentSCCB))->TargID);
      currTar_Info = &sccbMgrTbl[p_card][thisTarg];

      pCurrCard->currentSCCB->Sccb_backlink = (PSCCB)NULL;

      pCurrCard->currentSCCB->Sccb_forwardlink = currTar_Info->TarSelQ_Head;

	  if (currTar_Info->TarSelQ_Cnt == 0)
		 {
		 currTar_Info->TarSelQ_Tail = pCurrCard->currentSCCB;
		 }

	  else
		 {
		 currTar_Info->TarSelQ_Head->Sccb_backlink = pCurrCard->currentSCCB;
		 }


	  currTar_Info->TarSelQ_Head = pCurrCard->currentSCCB;

	  pCurrCard->currentSCCB = NULL;
	  currTar_Info->TarSelQ_Cnt++;
	  }
}
/*---------------------------------------------------------------------
 *
 * Function: Queue Command Complete
 *
 * Description: Call the callback function with the current SCCB.
 *
 *---------------------------------------------------------------------*/

void queueCmdComplete(PSCCBcard pCurrCard, PSCCB p_sccb, UCHAR p_card)
{

#if (FW_TYPE==_UCB_MGR_)

   u08bits SCSIcmd;
   CALL_BK_FN callback;
   PSCCBMgr_tar_info currTar_Info;

   PUCB p_ucb;
   p_ucb=p_sccb->Sccb_ucb_ptr;

   SCSIcmd = p_sccb->Cdb[0];


   if (!(p_sccb->Sccb_XferState & F_ALL_XFERRED))
   {

      if ((p_ucb->UCB_opcode & OPC_CHK_UNDER_OVER_RUN)                     &&
         (p_sccb->HostStatus == SCCB_COMPLETE)                             &&
         (p_sccb->TargetStatus != SSCHECK))

         if ((SCSIcmd == SCSI_READ)             ||
             (SCSIcmd == SCSI_WRITE)            ||
             (SCSIcmd == SCSI_READ_EXTENDED)    ||
             (SCSIcmd == SCSI_WRITE_EXTENDED)   ||
             (SCSIcmd == SCSI_WRITE_AND_VERIFY) ||
             (SCSIcmd == SCSI_START_STOP_UNIT)  ||
             (pCurrCard->globalFlags & F_NO_FILTER)
            )
               p_sccb->HostStatus = SCCB_DATA_UNDER_RUN;
   }

   p_ucb->UCB_status=SCCB_SUCCESS;

   if ((p_ucb->UCB_hbastat=p_sccb->HostStatus) || (p_ucb->UCB_scsistat=p_sccb->TargetStatus))
   {
      p_ucb->UCB_status=SCCB_ERROR;
   }

   if ((p_sccb->OperationCode == RESIDUAL_SG_COMMAND) ||
      (p_sccb->OperationCode == RESIDUAL_COMMAND))
   {

         utilUpdateResidual(p_sccb);

         p_ucb->UCB_datalen=p_sccb->DataLength;
   }

   pCurrCard->cmdCounter--;
   if (!pCurrCard->cmdCounter)
   {

      if (pCurrCard->globalFlags & F_GREEN_PC)
      {
         WR_HARPOON(pCurrCard->ioPort+hp_clkctrl_0,(PWR_DWN | CLKCTRL_DEFAULT));
         WR_HARPOON(pCurrCard->ioPort+hp_sys_ctrl, STOP_CLK);
      }

      WR_HARPOON(pCurrCard->ioPort+hp_semaphore,
      (RD_HARPOON(pCurrCard->ioPort+hp_semaphore) & ~SCCB_MGR_ACTIVE));
   }

	if(pCurrCard->discQCount != 0)
	{
      currTar_Info = &sccbMgrTbl[p_card][p_sccb->TargID];
		if(((pCurrCard->globalFlags & F_CONLUN_IO) &&
			((currTar_Info->TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING)))
		{
			pCurrCard->discQCount--;
			pCurrCard->discQ_Tbl[currTar_Info->LunDiscQ_Idx[p_sccb->Lun]] = NULL;
		}
		else
		{
			if(p_sccb->Sccb_tag)
			{
				pCurrCard->discQCount--;
				pCurrCard->discQ_Tbl[p_sccb->Sccb_tag] = NULL;
			}else
			{
				pCurrCard->discQCount--;
				pCurrCard->discQ_Tbl[currTar_Info->LunDiscQ_Idx[0]] = NULL;
			}
		}

	}
   callback = (CALL_BK_FN)p_ucb->UCB_callback;
   callback(p_ucb);
   pCurrCard->globalFlags |= F_NEW_SCCB_CMD;
   pCurrCard->currentSCCB = NULL;
}




#else

   UCHAR i, SCSIcmd;
   CALL_BK_FN callback;
   PSCCBMgr_tar_info currTar_Info;

   SCSIcmd = p_sccb->Cdb[0];


   if (!(p_sccb->Sccb_XferState & F_ALL_XFERRED)) {

	  if ((p_sccb->ControlByte & (SCCB_DATA_XFER_OUT | SCCB_DATA_XFER_IN)) &&
		 (p_sccb->HostStatus == SCCB_COMPLETE)                             &&
		 (p_sccb->TargetStatus != SSCHECK))

		 if ((SCSIcmd == SCSI_READ)             ||
			 (SCSIcmd == SCSI_WRITE)            ||
			 (SCSIcmd == SCSI_READ_EXTENDED)    ||
			 (SCSIcmd == SCSI_WRITE_EXTENDED)   ||
			 (SCSIcmd == SCSI_WRITE_AND_VERIFY) ||
			 (SCSIcmd == SCSI_START_STOP_UNIT)  ||
			 (pCurrCard->globalFlags & F_NO_FILTER)
			)
			   p_sccb->HostStatus = SCCB_DATA_UNDER_RUN;
	  }


	if(p_sccb->SccbStatus == SCCB_IN_PROCESS)
	{
	   if (p_sccb->HostStatus || p_sccb->TargetStatus)
		  p_sccb->SccbStatus = SCCB_ERROR;
	   else
		  p_sccb->SccbStatus = SCCB_SUCCESS;
	}

   if (p_sccb->Sccb_XferState & F_AUTO_SENSE) {

	  p_sccb->CdbLength = p_sccb->Save_CdbLen;
	  for (i=0; i < 6; i++) {
		 p_sccb->Cdb[i] = p_sccb->Save_Cdb[i];
		 }
	  }

   if ((p_sccb->OperationCode == RESIDUAL_SG_COMMAND) ||
	  (p_sccb->OperationCode == RESIDUAL_COMMAND)) {

		 utilUpdateResidual(p_sccb);
		 }

   pCurrCard->cmdCounter--;
   if (!pCurrCard->cmdCounter) {

	  if (pCurrCard->globalFlags & F_GREEN_PC) {
		 WR_HARPOON(pCurrCard->ioPort+hp_clkctrl_0,(PWR_DWN | CLKCTRL_DEFAULT));
		 WR_HARPOON(pCurrCard->ioPort+hp_sys_ctrl, STOP_CLK);
		 }

	  WR_HARPOON(pCurrCard->ioPort+hp_semaphore,
	  (RD_HARPOON(pCurrCard->ioPort+hp_semaphore) & ~SCCB_MGR_ACTIVE));

	  }

	if(pCurrCard->discQCount != 0)
	{
      currTar_Info = &sccbMgrTbl[p_card][p_sccb->TargID];
		if(((pCurrCard->globalFlags & F_CONLUN_IO) &&
			((currTar_Info->TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING)))
		{
			pCurrCard->discQCount--;
			pCurrCard->discQ_Tbl[currTar_Info->LunDiscQ_Idx[p_sccb->Lun]] = NULL;
		}
		else
		{
			if(p_sccb->Sccb_tag)
			{
				pCurrCard->discQCount--;
				pCurrCard->discQ_Tbl[p_sccb->Sccb_tag] = NULL;
			}else
			{
				pCurrCard->discQCount--;
				pCurrCard->discQ_Tbl[currTar_Info->LunDiscQ_Idx[0]] = NULL;
			}
		}

	}

	callback = (CALL_BK_FN)p_sccb->SccbCallback;
   callback(p_sccb);
   pCurrCard->globalFlags |= F_NEW_SCCB_CMD;
   pCurrCard->currentSCCB = NULL;
}
#endif /* ( if FW_TYPE==...) */


/*---------------------------------------------------------------------
 *
 * Function: Queue Disconnect
 *
 * Description: Add SCCB to our disconnect array.
 *
 *---------------------------------------------------------------------*/
void queueDisconnect(PSCCB p_sccb, UCHAR p_card)
{
   PSCCBMgr_tar_info currTar_Info;

	currTar_Info = &sccbMgrTbl[p_card][p_sccb->TargID];

	if(((BL_Card[p_card].globalFlags & F_CONLUN_IO) &&
		((currTar_Info->TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING)))
	{
		BL_Card[p_card].discQ_Tbl[currTar_Info->LunDiscQ_Idx[p_sccb->Lun]] = p_sccb;
	}
	else
	{
		if (p_sccb->Sccb_tag)
		{
			BL_Card[p_card].discQ_Tbl[p_sccb->Sccb_tag] = p_sccb;
			sccbMgrTbl[p_card][p_sccb->TargID].TarLUNBusy[0] = FALSE;
			sccbMgrTbl[p_card][p_sccb->TargID].TarTagQ_Cnt++;
		}else
		{
			BL_Card[p_card].discQ_Tbl[currTar_Info->LunDiscQ_Idx[0]] = p_sccb;
		}
	}
	BL_Card[p_card].currentSCCB = NULL;
}


/*---------------------------------------------------------------------
 *
 * Function: Queue Flush SCCB
 *
 * Description: Flush all SCCB's back to the host driver for this target.
 *
 *---------------------------------------------------------------------*/

void  queueFlushSccb(UCHAR p_card, UCHAR error_code)
{
   UCHAR qtag,thisTarg;
   PSCCB currSCCB;
   PSCCBMgr_tar_info currTar_Info;

   currSCCB = BL_Card[p_card].currentSCCB;
	if(currSCCB != NULL)
	{
	   thisTarg = (UCHAR)currSCCB->TargID;
   	currTar_Info = &sccbMgrTbl[p_card][thisTarg];

	   for (qtag=0; qtag<QUEUE_DEPTH; qtag++) {

		  if (BL_Card[p_card].discQ_Tbl[qtag] && 
					(BL_Card[p_card].discQ_Tbl[qtag]->TargID == thisTarg))
			 {

			 BL_Card[p_card].discQ_Tbl[qtag]->HostStatus = (UCHAR)error_code;
			
			 queueCmdComplete(&BL_Card[p_card],BL_Card[p_card].discQ_Tbl[qtag], p_card);

			 BL_Card[p_card].discQ_Tbl[qtag] = NULL;
			 currTar_Info->TarTagQ_Cnt--;

			 }
		  }
	}

}

/*---------------------------------------------------------------------
 *
 * Function: Queue Flush Target SCCB
 *
 * Description: Flush all SCCB's back to the host driver for this target.
 *
 *---------------------------------------------------------------------*/

void  queueFlushTargSccb(UCHAR p_card, UCHAR thisTarg, UCHAR error_code)
{
   UCHAR qtag;
   PSCCBMgr_tar_info currTar_Info;

   currTar_Info = &sccbMgrTbl[p_card][thisTarg];

   for (qtag=0; qtag<QUEUE_DEPTH; qtag++) {

	  if (BL_Card[p_card].discQ_Tbl[qtag] && 
				(BL_Card[p_card].discQ_Tbl[qtag]->TargID == thisTarg))
		 {

		 BL_Card[p_card].discQ_Tbl[qtag]->HostStatus = (UCHAR)error_code;

		 queueCmdComplete(&BL_Card[p_card],BL_Card[p_card].discQ_Tbl[qtag], p_card);

		 BL_Card[p_card].discQ_Tbl[qtag] = NULL;
		 currTar_Info->TarTagQ_Cnt--;

		 }
	  }

}





void queueAddSccb(PSCCB p_SCCB, UCHAR p_card)
{
   PSCCBMgr_tar_info currTar_Info;
   currTar_Info = &sccbMgrTbl[p_card][p_SCCB->TargID];

   p_SCCB->Sccb_forwardlink = NULL;

   p_SCCB->Sccb_backlink = currTar_Info->TarSelQ_Tail;

   if (currTar_Info->TarSelQ_Cnt == 0) {

	  currTar_Info->TarSelQ_Head = p_SCCB;
	  }

   else {

	  currTar_Info->TarSelQ_Tail->Sccb_forwardlink = p_SCCB;
	  }


   currTar_Info->TarSelQ_Tail = p_SCCB;
   currTar_Info->TarSelQ_Cnt++;
}


/*---------------------------------------------------------------------
 *
 * Function: Queue Find SCCB
 *
 * Description: Search the target select Queue for this SCCB, and
 *              remove it if found.
 *
 *---------------------------------------------------------------------*/

UCHAR queueFindSccb(PSCCB p_SCCB, UCHAR p_card)
{
   PSCCB q_ptr;
   PSCCBMgr_tar_info currTar_Info;

   currTar_Info = &sccbMgrTbl[p_card][p_SCCB->TargID];

   q_ptr = currTar_Info->TarSelQ_Head;

   while(q_ptr != NULL) {

	  if (q_ptr == p_SCCB) {


		 if (currTar_Info->TarSelQ_Head == q_ptr) {

			currTar_Info->TarSelQ_Head = q_ptr->Sccb_forwardlink;
			}

		 if (currTar_Info->TarSelQ_Tail == q_ptr) {

			currTar_Info->TarSelQ_Tail = q_ptr->Sccb_backlink;
			}

		 if (q_ptr->Sccb_forwardlink != NULL) {
			q_ptr->Sccb_forwardlink->Sccb_backlink = q_ptr->Sccb_backlink;
			}

		 if (q_ptr->Sccb_backlink != NULL) {
			q_ptr->Sccb_backlink->Sccb_forwardlink = q_ptr->Sccb_forwardlink;
			}

		 currTar_Info->TarSelQ_Cnt--;

		 return(TRUE);
		 }

	  else {
		 q_ptr = q_ptr->Sccb_forwardlink;
		 }
	  }


   return(FALSE);

}


/*---------------------------------------------------------------------
 *
 * Function: Utility Update Residual Count
 *
 * Description: Update the XferCnt to the remaining byte count.
 *              If we transferred all the data then just write zero.
 *              If Non-SG transfer then report Total Cnt - Actual Transfer
 *              Cnt.  For SG transfers add the count fields of all
 *              remaining SG elements, as well as any partial remaining
 *              element.
 *
 *---------------------------------------------------------------------*/

void  utilUpdateResidual(PSCCB p_SCCB)
{
   ULONG partial_cnt;
   UINT  sg_index;
#if defined(COMPILER_16_BIT) && !defined(DOS)
   ULONG far *sg_ptr;
#else
   ULONG *sg_ptr;
#endif

   if (p_SCCB->Sccb_XferState & F_ALL_XFERRED) {

	  p_SCCB->DataLength = 0x0000;
	  }

   else if (p_SCCB->Sccb_XferState & F_SG_XFER) {

		 partial_cnt = 0x0000;

		 sg_index = p_SCCB->Sccb_sgseg;

#if defined(COMPILER_16_BIT) && !defined(DOS)
		 sg_ptr = (ULONG far *)p_SCCB->DataPointer;
#else
		 sg_ptr = (ULONG *)p_SCCB->DataPointer;
#endif

		 if (p_SCCB->Sccb_SGoffset) {

			partial_cnt = p_SCCB->Sccb_SGoffset;
			sg_index++;
			}

		 while ( ((ULONG)sg_index * (ULONG)SG_ELEMENT_SIZE) <
			p_SCCB->DataLength ) {

			partial_cnt += *(sg_ptr+(sg_index * 2));
			sg_index++;
			}

		 p_SCCB->DataLength = partial_cnt;
		 }

	  else {

		 p_SCCB->DataLength -= p_SCCB->Sccb_ATC;
		 }
}


/*---------------------------------------------------------------------
 *
 * Function: Wait 1 Second
 *
 * Description: Wait for 1 second.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void Wait1Second(USHORT p_port)
#else
void Wait1Second(ULONG p_port)
#endif
{
   UCHAR i;

   for(i=0; i < 4; i++) {

	  Wait(p_port, TO_250ms);

	  if ((RD_HARPOON(p_port+hp_scsictrl_0) & SCSI_RST))
		 break;

	  if((RDW_HARPOON((p_port+hp_intstat)) & SCAM_SEL))
		 break;
	  }
}


/*---------------------------------------------------------------------
 *
 * Function: Wait
 *
 * Description: Wait the desired delay.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void Wait(USHORT p_port, UCHAR p_delay)
#else
void Wait(ULONG p_port, UCHAR p_delay)
#endif
{
   UCHAR old_timer;
   UCHAR green_flag;

   old_timer = RD_HARPOON(p_port+hp_seltimeout);

   green_flag=RD_HARPOON(p_port+hp_clkctrl_0);
   WR_HARPOON(p_port+hp_clkctrl_0, CLKCTRL_DEFAULT);

   WR_HARPOON(p_port+hp_seltimeout,p_delay);
   WRW_HARPOON((p_port+hp_intstat), TIMEOUT);
   WRW_HARPOON((p_port+hp_intena), (default_intena & ~TIMEOUT));


   WR_HARPOON(p_port+hp_portctrl_0,
	  (RD_HARPOON(p_port+hp_portctrl_0) | START_TO));

   while (!(RDW_HARPOON((p_port+hp_intstat)) & TIMEOUT)) {

	  if ((RD_HARPOON(p_port+hp_scsictrl_0) & SCSI_RST))
		 break;

	  if ((RDW_HARPOON((p_port+hp_intstat)) & SCAM_SEL))
		 break;
	  }

   WR_HARPOON(p_port+hp_portctrl_0,
	  (RD_HARPOON(p_port+hp_portctrl_0) & ~START_TO));

   WRW_HARPOON((p_port+hp_intstat), TIMEOUT);
   WRW_HARPOON((p_port+hp_intena), default_intena);

   WR_HARPOON(p_port+hp_clkctrl_0,green_flag);

   WR_HARPOON(p_port+hp_seltimeout,old_timer);
}


/*---------------------------------------------------------------------
 *
 * Function: Enable/Disable Write to EEPROM
 *
 * Description: The EEPROM must first be enabled for writes
 *              A total of 9 clocks are needed.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void utilEEWriteOnOff(USHORT p_port,UCHAR p_mode)
#else
void utilEEWriteOnOff(ULONG p_port,UCHAR p_mode)
#endif
{
   UCHAR ee_value;

   ee_value = (UCHAR)(RD_HARPOON(p_port+hp_ee_ctrl) & (EXT_ARB_ACK | SCSI_TERM_ENA_H));

   if (p_mode)

	  utilEESendCmdAddr(p_port, EWEN, EWEN_ADDR);

   else


	  utilEESendCmdAddr(p_port, EWDS, EWDS_ADDR);

   WR_HARPOON(p_port+hp_ee_ctrl, (ee_value | SEE_MS)); /*Turn off CS */
   WR_HARPOON(p_port+hp_ee_ctrl, ee_value);       /*Turn off Master Select */
}


/*---------------------------------------------------------------------
 *
 * Function: Write EEPROM
 *
 * Description: Write a word to the EEPROM at the specified
 *              address.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void utilEEWrite(USHORT p_port, USHORT ee_data, USHORT ee_addr)
#else
void utilEEWrite(ULONG p_port, USHORT ee_data, USHORT ee_addr)
#endif
{

   UCHAR ee_value;
   USHORT i;

   ee_value = (UCHAR)((RD_HARPOON(p_port+hp_ee_ctrl) & (EXT_ARB_ACK | SCSI_TERM_ENA_H))|
		   (SEE_MS | SEE_CS));



   utilEESendCmdAddr(p_port, EE_WRITE, ee_addr);


   ee_value |= (SEE_MS + SEE_CS);

   for(i = 0x8000; i != 0; i>>=1) {

	  if (i & ee_data)
	 ee_value |= SEE_DO;
	  else
	 ee_value &= ~SEE_DO;

	  WR_HARPOON(p_port+hp_ee_ctrl, ee_value);
	  WR_HARPOON(p_port+hp_ee_ctrl, ee_value);
	  ee_value |= SEE_CLK;          /* Clock  data! */
	  WR_HARPOON(p_port+hp_ee_ctrl, ee_value);
	  WR_HARPOON(p_port+hp_ee_ctrl, ee_value);
	  ee_value &= ~SEE_CLK;
	  WR_HARPOON(p_port+hp_ee_ctrl, ee_value);
	  WR_HARPOON(p_port+hp_ee_ctrl, ee_value);
	  }
   ee_value &= (EXT_ARB_ACK | SCSI_TERM_ENA_H);
   WR_HARPOON(p_port+hp_ee_ctrl, (ee_value | SEE_MS));

   Wait(p_port, TO_10ms);

   WR_HARPOON(p_port+hp_ee_ctrl, (ee_value | SEE_MS | SEE_CS)); /* Set CS to EEPROM */
   WR_HARPOON(p_port+hp_ee_ctrl, (ee_value | SEE_MS));       /* Turn off CS */
   WR_HARPOON(p_port+hp_ee_ctrl, ee_value);       /* Turn off Master Select */
}

/*---------------------------------------------------------------------
 *
 * Function: Read EEPROM
 *
 * Description: Read a word from the EEPROM at the desired
 *              address.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
USHORT utilEERead(USHORT p_port, USHORT ee_addr)
#else
USHORT utilEERead(ULONG p_port, USHORT ee_addr)
#endif
{
   USHORT i, ee_data1, ee_data2;

	i = 0;
	ee_data1 = utilEEReadOrg(p_port, ee_addr);
	do
	{
		ee_data2 = utilEEReadOrg(p_port, ee_addr);

		if(ee_data1 == ee_data2)
			return(ee_data1);

		ee_data1 = ee_data2;
		i++;

	}while(i < 4);

	return(ee_data1);
}

/*---------------------------------------------------------------------
 *
 * Function: Read EEPROM Original 
 *
 * Description: Read a word from the EEPROM at the desired
 *              address.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
USHORT utilEEReadOrg(USHORT p_port, USHORT ee_addr)
#else
USHORT utilEEReadOrg(ULONG p_port, USHORT ee_addr)
#endif
{

   UCHAR ee_value;
   USHORT i, ee_data;

   ee_value = (UCHAR)((RD_HARPOON(p_port+hp_ee_ctrl) & (EXT_ARB_ACK | SCSI_TERM_ENA_H))|
		   (SEE_MS | SEE_CS));


   utilEESendCmdAddr(p_port, EE_READ, ee_addr);


   ee_value |= (SEE_MS + SEE_CS);
   ee_data = 0;

   for(i = 1; i <= 16; i++) {

	  ee_value |= SEE_CLK;          /* Clock  data! */
	  WR_HARPOON(p_port+hp_ee_ctrl, ee_value);
	  WR_HARPOON(p_port+hp_ee_ctrl, ee_value);
	  ee_value &= ~SEE_CLK;
	  WR_HARPOON(p_port+hp_ee_ctrl, ee_value);
	  WR_HARPOON(p_port+hp_ee_ctrl, ee_value);

	  ee_data <<= 1;

	  if (RD_HARPOON(p_port+hp_ee_ctrl) & SEE_DI)
		 ee_data |= 1;
	  }

   ee_value &= ~(SEE_MS + SEE_CS);
   WR_HARPOON(p_port+hp_ee_ctrl, (ee_value | SEE_MS)); /*Turn off CS */
   WR_HARPOON(p_port+hp_ee_ctrl, ee_value);   /*Turn off Master Select */

   return(ee_data);
}


/*---------------------------------------------------------------------
 *
 * Function: Send EE command and Address to the EEPROM
 *
 * Description: Transfers the correct command and sends the address
 *              to the eeprom.
 *
 *---------------------------------------------------------------------*/

#if defined(DOS)
void utilEESendCmdAddr(USHORT p_port, UCHAR ee_cmd, USHORT ee_addr)
#else
void utilEESendCmdAddr(ULONG p_port, UCHAR ee_cmd, USHORT ee_addr)
#endif
{
   UCHAR ee_value;
   UCHAR narrow_flg;

   USHORT i;


   narrow_flg= (UCHAR)(RD_HARPOON(p_port+hp_page_ctrl) & NARROW_SCSI_CARD);


   ee_value = SEE_MS;
   WR_HARPOON(p_port+hp_ee_ctrl, ee_value);

   ee_value |= SEE_CS;                             /* Set CS to EEPROM */
   WR_HARPOON(p_port+hp_ee_ctrl, ee_value);


   for(i = 0x04; i != 0; i>>=1) {

	  if (i & ee_cmd)
		 ee_value |= SEE_DO;
	  else
		 ee_value &= ~SEE_DO;

	  WR_HARPOON(p_port+hp_ee_ctrl, ee_value);
	  WR_HARPOON(p_port+hp_ee_ctrl, ee_value);
	  ee_value |= SEE_CLK;                         /* Clock  data! */
	  WR_HARPOON(p_port+hp_ee_ctrl, ee_value);
	  WR_HARPOON(p_port+hp_ee_ctrl, ee_value);
	  ee_value &= ~SEE_CLK;
	  WR_HARPOON(p_port+hp_ee_ctrl, ee_value);
	  WR_HARPOON(p_port+hp_ee_ctrl, ee_value);
	  }


   if (narrow_flg)
	  i = 0x0080;

   else
	  i = 0x0200;


   while (i != 0) {

	  if (i & ee_addr)
		 ee_value |= SEE_DO;
	  else
		 ee_value &= ~SEE_DO;

	  WR_HARPOON(p_port+hp_ee_ctrl, ee_value);
	  WR_HARPOON(p_port+hp_ee_ctrl, ee_value);
	  ee_value |= SEE_CLK;                         /* Clock  data! */
	  WR_HARPOON(p_port+hp_ee_ctrl, ee_value);
	  WR_HARPOON(p_port+hp_ee_ctrl, ee_value);
	  ee_value &= ~SEE_CLK;
	  WR_HARPOON(p_port+hp_ee_ctrl, ee_value);
	  WR_HARPOON(p_port+hp_ee_ctrl, ee_value);

	  i >>= 1;
	  }
}

USHORT CalcCrc16(UCHAR buffer[])
{
   USHORT crc=0;
	int i,j;
   USHORT ch;
   for (i=0; i < ID_STRING_LENGTH; i++)
   {
      ch = (USHORT) buffer[i];
	   for(j=0; j < 8; j++)
	   {
		   if ((crc ^ ch) & 1)
            crc = (crc >> 1) ^ CRCMASK;
		   else
            crc >>= 1;
		   ch >>= 1;
	   }
   }
	return(crc);
}

UCHAR CalcLrc(UCHAR buffer[])
{
	int i;
	UCHAR lrc;
	lrc = 0;
	for(i = 0; i < ID_STRING_LENGTH; i++)
		lrc ^= buffer[i];
	return(lrc);
}



/*
  The following inline definitions avoid type conflicts.
*/

static inline unsigned char
FlashPoint__ProbeHostAdapter(struct FlashPoint_Info *FlashPointInfo)
{
  return FlashPoint_ProbeHostAdapter((PSCCBMGR_INFO) FlashPointInfo);
}


static inline FlashPoint_CardHandle_T
FlashPoint__HardwareResetHostAdapter(struct FlashPoint_Info *FlashPointInfo)
{
  return FlashPoint_HardwareResetHostAdapter((PSCCBMGR_INFO) FlashPointInfo);
}

static inline void
FlashPoint__ReleaseHostAdapter(FlashPoint_CardHandle_T CardHandle)
{
  FlashPoint_ReleaseHostAdapter(CardHandle);
}


static inline void
FlashPoint__StartCCB(FlashPoint_CardHandle_T CardHandle, struct BusLogic_CCB *CCB)
{
  FlashPoint_StartCCB(CardHandle, (PSCCB) CCB);
}


static inline void
FlashPoint__AbortCCB(FlashPoint_CardHandle_T CardHandle, struct BusLogic_CCB *CCB)
{
  FlashPoint_AbortCCB(CardHandle, (PSCCB) CCB);
}


static inline boolean
FlashPoint__InterruptPending(FlashPoint_CardHandle_T CardHandle)
{
  return FlashPoint_InterruptPending(CardHandle);
}


static inline int
FlashPoint__HandleInterrupt(FlashPoint_CardHandle_T CardHandle)
{
  return FlashPoint_HandleInterrupt(CardHandle);
}


#define FlashPoint_ProbeHostAdapter	    FlashPoint__ProbeHostAdapter
#define FlashPoint_HardwareResetHostAdapter FlashPoint__HardwareResetHostAdapter
#define FlashPoint_ReleaseHostAdapter	    FlashPoint__ReleaseHostAdapter
#define FlashPoint_StartCCB		    FlashPoint__StartCCB
#define FlashPoint_AbortCCB		    FlashPoint__AbortCCB
#define FlashPoint_InterruptPending	    FlashPoint__InterruptPending
#define FlashPoint_HandleInterrupt	    FlashPoint__HandleInterrupt


/*
  FlashPoint_InquireTargetInfo returns the Synchronous Period, Synchronous
  Offset, and Wide Transfers Active information for TargetID on CardHandle.
*/

void FlashPoint_InquireTargetInfo(FlashPoint_CardHandle_T CardHandle,
				  int TargetID,
				  unsigned char *SynchronousPeriod,
				  unsigned char *SynchronousOffset,
				  unsigned char *WideTransfersActive)
{
  SCCBMGR_TAR_INFO *TargetInfo =
    &sccbMgrTbl[((SCCBCARD *)CardHandle)->cardIndex][TargetID];
  if ((TargetInfo->TarSyncCtrl & SYNC_OFFSET) > 0)
    {
      *SynchronousPeriod = 5 * ((TargetInfo->TarSyncCtrl >> 5) + 1);
      *SynchronousOffset = TargetInfo->TarSyncCtrl & SYNC_OFFSET;
    }
  else
    {
      *SynchronousPeriod = 0;
      *SynchronousOffset = 0;
    }
  *WideTransfersActive = (TargetInfo->TarSyncCtrl & NARROW_SCSI ? 0 : 1);
}


#else  /* CONFIG_SCSI_OMIT_FLASHPOINT */


/*
  Define prototypes for the FlashPoint SCCB Manager Functions.
*/

extern unsigned char FlashPoint_ProbeHostAdapter(struct FlashPoint_Info *);
extern FlashPoint_CardHandle_T
       FlashPoint_HardwareResetHostAdapter(struct FlashPoint_Info *);
extern void FlashPoint_StartCCB(FlashPoint_CardHandle_T, struct BusLogic_CCB *);
extern int FlashPoint_AbortCCB(FlashPoint_CardHandle_T, struct BusLogic_CCB *);
extern boolean FlashPoint_InterruptPending(FlashPoint_CardHandle_T);
extern int FlashPoint_HandleInterrupt(FlashPoint_CardHandle_T);
extern void FlashPoint_ReleaseHostAdapter(FlashPoint_CardHandle_T);
extern void FlashPoint_InquireTargetInfo(FlashPoint_CardHandle_T,
					 int, unsigned char *,
					 unsigned char *, unsigned char *);


#endif /* CONFIG_SCSI_OMIT_FLASHPOINT */
