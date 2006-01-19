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


#define MAX_CARDS	8
#undef BUSTYPE_PCI


#define OS_InPortByte(port)		inb(port)
#define OS_InPortWord(port)		inw(port)
#define OS_InPortLong(port)		inl(port)
#define OS_OutPortByte(port, value)	outb(value, port)
#define OS_OutPortWord(port, value)	outw(value, port)
#define OS_OutPortLong(port, value)	outl(value, port)


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


#define MAX_CDBLEN  12

#define SCAM_LEV_2	1

#define CRCMASK	0xA001

#define     BL_VENDOR_ID      0x104B
#define     FP_DEVICE_ID      0x8130
#define     MM_DEVICE_ID      0x1040


#define FAILURE         0xFFFFFFFFL


typedef unsigned char   UCHAR;
typedef unsigned short  USHORT;
typedef unsigned int    UINT;
typedef unsigned long   ULONG;
typedef unsigned char * PUCHAR;
typedef unsigned short* PUSHORT;
typedef unsigned long * PULONG;
typedef void *          PVOID;


typedef unsigned char           * uchar_ptr;
typedef unsigned short          * ushort_ptr;
typedef unsigned long           * ulong_ptr;


#define s08bits	char
#define s16bits 	short
#define s32bits	long

#define u08bits	unsigned s08bits
#define u16bits	unsigned s16bits
#define u32bits	unsigned s32bits

typedef u08bits 	* pu08bits;
typedef u16bits 	* pu16bits;
typedef u32bits 	* pu32bits;


#define BIT(x)          ((UCHAR)(1<<(x)))    /* single-bit mask in bit position x */
#define BITW(x)          ((USHORT)(1<<(x)))  /* single-bit mask in bit position x */




typedef struct _SCCB *PSCCB;
typedef void (*CALL_BK_FN)(PSCCB);


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

typedef SCCBMGR_INFO *      PSCCBMGR_INFO;


#define SCSI_PARITY_ENA		  0x0001
#define LOW_BYTE_TERM		  0x0010
#define HIGH_BYTE_TERM		  0x0020
#define BUSTYPE_PCI	  0x3

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

/* SCCB struct used for both SCCB and UCB manager compiles! 
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



#define SCCB_INVALID_DIRECTION      0x18  /* Invalid target direction */
#define SCCB_DUPLICATE_SCCB         0x19  /* Duplicate SCCB */
#define SCCB_SCSI_RST               0x35  /* SCSI RESET detected. */


#define SCCB_IN_PROCESS            0x00
#define SCCB_SUCCESS               0x01
#define SCCB_ABORT                 0x02
#define SCCB_NOT_FOUND             0x03
#define SCCB_ERROR                 0x04
#define SCCB_INVALID               0x05

#define SCCB_SIZE sizeof(SCCB)


#define  ORION_FW_REV      3110

#define HARP_REVD    1


#define QUEUE_DEPTH     254+1            /*1 for Normal disconnect 32 for Q'ing. */

#define	MAX_MB_CARDS	4					/* Max. no of cards suppoerted on Mother Board */

#define WIDE_SCSI       1

#define MAX_SCSI_TAR    16
#define MAX_LUN         32
#define LUN_MASK			0x1f

#if defined(HARP_REVA)
#define SG_BUF_CNT      15             /*Number of prefetched elements. */
#else
#define SG_BUF_CNT      16             /*Number of prefetched elements. */
#endif

#define SG_ELEMENT_SIZE 8              /*Eight byte per element. */
#define SG_LOCAL_MASK   0x00000000L
#define SG_ELEMENT_MASK 0xFFFFFFFFL


#define RD_HARPOON(ioport)          OS_InPortByte((u32bits)ioport)
#define RDW_HARPOON(ioport)         OS_InPortWord((u32bits)ioport)
#define RD_HARP32(ioport,offset,data) (data = OS_InPortLong((u32bits)(ioport + offset)))
#define WR_HARPOON(ioport,val)      OS_OutPortByte((u32bits)ioport,(u08bits) val)
#define WRW_HARPOON(ioport,val)       OS_OutPortWord((u32bits)ioport,(u16bits)val)
#define WR_HARP32(ioport,offset,data)  OS_OutPortLong((u32bits)(ioport + offset), data)


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


typedef struct SCCBMgr_tar_info *PSCCBMgr_tar_info;


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
	ULONG		niBaseAddr;							/* Port Address of card */
	UCHAR		niSysConf;							/* Adapter Configuration byte - Byte 16 of eeprom map */
	UCHAR		niScsiConf;							/* SCSI Configuration byte - Byte 17 of eeprom map */
	UCHAR		niScamConf;							/* SCAM Configuration byte - Byte 20 of eeprom map */
	UCHAR		niAdapId;							/* Host Adapter ID - Byte 24 of eerpom map */
	UCHAR		niSyncTbl[MAX_SCSI_TAR / 2];	/* Sync/Wide byte of targets */
	UCHAR		niScamTbl[MAX_SCSI_TAR][4];	/* Compressed Scam name string of Targets */
}NVRAMINFO;

typedef NVRAMINFO *PNVRamInfo;

#define	MODEL_LT		1
#define	MODEL_DL		2
#define	MODEL_LW		3
#define	MODEL_DW		4


typedef struct SCCBcard {
   PSCCB currentSCCB;
   PSCCBMGR_INFO cardInfo;

   ULONG ioPort;

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

typedef struct SCCBcard *PSCCBcard;


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
#define HP_SETUP_ADDR_CNT(port,addr,count) (WRW_HARPOON((port+hp_host_addr_lo), (USHORT)(addr & 0x0000FFFFL)),\
         addr >>= 16,\
         WRW_HARPOON((port+hp_host_addr_hmi), (USHORT)(addr & 0x0000FFFFL)),\
         WR_HARP32(port,hp_xfercnt_0,count),\
         WRW_HARPOON((port+hp_xfer_cnt_lo), (USHORT)(count & 0x0000FFFFL)),\
         count >>= 16,\
         WR_HARPOON(port+hp_xfer_cnt_hi, (count & 0xFF)))

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




void  scsiStartAuto(ULONG port);
static UCHAR FPT_sisyncn(ULONG port, UCHAR p_card, UCHAR syncFlag);
static void  FPT_ssel(ULONG port, UCHAR p_card);
static void  FPT_sres(ULONG port, UCHAR p_card, PSCCBcard pCurrCard);
static void  FPT_shandem(ULONG port, UCHAR p_card,PSCCB pCurrSCCB);
static void  FPT_stsyncn(ULONG port, UCHAR p_card);
static void  FPT_sisyncr(ULONG port,UCHAR sync_pulse, UCHAR offset);
static void  FPT_sssyncv(ULONG p_port, UCHAR p_id, UCHAR p_sync_value,
			 PSCCBMgr_tar_info currTar_Info);
static void  FPT_sresb(ULONG port, UCHAR p_card);
static void  FPT_sxfrp(ULONG p_port, UCHAR p_card);
static void  FPT_schkdd(ULONG port, UCHAR p_card);
static UCHAR FPT_RdStack(ULONG port, UCHAR index);
static void  FPT_WrStack(ULONG portBase, UCHAR index, UCHAR data);
static UCHAR FPT_ChkIfChipInitialized(ULONG ioPort);

static void FPT_SendMsg(ULONG port, UCHAR message);
static void  FPT_queueFlushTargSccb(UCHAR p_card, UCHAR thisTarg,
				    UCHAR error_code);

static void  FPT_sinits(PSCCB p_sccb, UCHAR p_card);
static void  FPT_RNVRamData(PNVRamInfo pNvRamInfo);

static UCHAR FPT_siwidn(ULONG port, UCHAR p_card);
static void  FPT_stwidn(ULONG port, UCHAR p_card);
static void  FPT_siwidr(ULONG port, UCHAR width);


static void  FPT_queueSelectFail(PSCCBcard pCurrCard, UCHAR p_card);
static void  FPT_queueDisconnect(PSCCB p_SCCB, UCHAR p_card);
static void  FPT_queueCmdComplete(PSCCBcard pCurrCard, PSCCB p_SCCB,
				  UCHAR p_card);
static void  FPT_queueSearchSelect(PSCCBcard pCurrCard, UCHAR p_card);
static void  FPT_queueFlushSccb(UCHAR p_card, UCHAR error_code);
static void  FPT_queueAddSccb(PSCCB p_SCCB, UCHAR card);
static UCHAR FPT_queueFindSccb(PSCCB p_SCCB, UCHAR p_card);
static void  FPT_utilUpdateResidual(PSCCB p_SCCB);
static USHORT FPT_CalcCrc16(UCHAR buffer[]);
static UCHAR  FPT_CalcLrc(UCHAR buffer[]);


static void  FPT_Wait1Second(ULONG p_port);
static void  FPT_Wait(ULONG p_port, UCHAR p_delay);
static void  FPT_utilEEWriteOnOff(ULONG p_port,UCHAR p_mode);
static void  FPT_utilEEWrite(ULONG p_port, USHORT ee_data, USHORT ee_addr);
static USHORT FPT_utilEERead(ULONG p_port, USHORT ee_addr);
static USHORT FPT_utilEEReadOrg(ULONG p_port, USHORT ee_addr);
static void  FPT_utilEESendCmdAddr(ULONG p_port, UCHAR ee_cmd, USHORT ee_addr);



static void  FPT_phaseDataOut(ULONG port, UCHAR p_card);
static void  FPT_phaseDataIn(ULONG port, UCHAR p_card);
static void  FPT_phaseCommand(ULONG port, UCHAR p_card);
static void  FPT_phaseStatus(ULONG port, UCHAR p_card);
static void  FPT_phaseMsgOut(ULONG port, UCHAR p_card);
static void  FPT_phaseMsgIn(ULONG port, UCHAR p_card);
static void  FPT_phaseIllegal(ULONG port, UCHAR p_card);

static void  FPT_phaseDecode(ULONG port, UCHAR p_card);
static void  FPT_phaseChkFifo(ULONG port, UCHAR p_card);
static void  FPT_phaseBusFree(ULONG p_port, UCHAR p_card);




static void  FPT_XbowInit(ULONG port, UCHAR scamFlg);
static void  FPT_BusMasterInit(ULONG p_port);
static void  FPT_DiagEEPROM(ULONG p_port);




void  busMstrAbort(ULONG port);
static void  FPT_dataXferProcessor(ULONG port, PSCCBcard pCurrCard);
static void  FPT_busMstrSGDataXferStart(ULONG port, PSCCB pCurrSCCB);
static void  FPT_busMstrDataXferStart(ULONG port, PSCCB pCurrSCCB);
static void  FPT_hostDataXferAbort(ULONG port, UCHAR p_card, PSCCB pCurrSCCB);
static void  FPT_hostDataXferRestart(PSCCB currSCCB);


static UCHAR FPT_SccbMgr_bad_isr(ULONG p_port, UCHAR p_card,
				 PSCCBcard pCurrCard, USHORT p_int);

static void  FPT_SccbMgrTableInitAll(void);
static void  FPT_SccbMgrTableInitCard(PSCCBcard pCurrCard, UCHAR p_card);
static void  FPT_SccbMgrTableInitTarget(UCHAR p_card, UCHAR target);



static void  FPT_scini(UCHAR p_card, UCHAR p_our_id, UCHAR p_power_up);

static int   FPT_scarb(ULONG p_port, UCHAR p_sel_type);
static void  FPT_scbusf(ULONG p_port);
static void  FPT_scsel(ULONG p_port);
static void  FPT_scasid(UCHAR p_card, ULONG p_port);
static UCHAR FPT_scxferc(ULONG p_port, UCHAR p_data);
static UCHAR FPT_scsendi(ULONG p_port, UCHAR p_id_string[]);
static UCHAR FPT_sciso(ULONG p_port, UCHAR p_id_string[]);
static void  FPT_scwirod(ULONG p_port, UCHAR p_data_bit);
static void  FPT_scwiros(ULONG p_port, UCHAR p_data_bit);
static UCHAR FPT_scvalq(UCHAR p_quintet);
static UCHAR FPT_scsell(ULONG p_port, UCHAR targ_id);
static void  FPT_scwtsel(ULONG p_port);
static void  FPT_inisci(UCHAR p_card, ULONG p_port, UCHAR p_our_id);
static void  FPT_scsavdi(UCHAR p_card, ULONG p_port);
static UCHAR FPT_scmachid(UCHAR p_card, UCHAR p_id_string[]);


static void  FPT_autoCmdCmplt(ULONG p_port, UCHAR p_card);
static void  FPT_autoLoadDefaultMap(ULONG p_port);



void  OS_start_timer(unsigned long ioport, unsigned long timeout);
void  OS_stop_timer(unsigned long ioport, unsigned long timeout);
void  OS_disable_int(unsigned char intvec);
void  OS_enable_int(unsigned char intvec);
void  OS_delay(unsigned long count);
int   OS_VirtToPhys(u32bits CardHandle, u32bits *physaddr, u32bits *virtaddr);

static SCCBMGR_TAR_INFO FPT_sccbMgrTbl[MAX_CARDS][MAX_SCSI_TAR] = { { { 0 } } };
static SCCBCARD FPT_BL_Card[MAX_CARDS] = { { 0 } };
static SCCBSCAM_INFO FPT_scamInfo[MAX_SCSI_TAR] = { { { 0 } } };
static NVRAMINFO FPT_nvRamInfo[MAX_MB_CARDS] = { { 0 } };


static UCHAR FPT_mbCards = 0;
static UCHAR FPT_scamHAString[] = {0x63, 0x07, 'B', 'U', 'S', 'L', 'O', 'G', 'I', 'C', \
				   ' ', 'B', 'T', '-', '9', '3', '0', \
				   0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, \
				   0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20};

static USHORT FPT_default_intena = 0;


static void (*FPT_s_PhaseTbl[8]) (ULONG, UCHAR)= { 0 };


/*---------------------------------------------------------------------
 *
 * Function: SccbMgr_sense_adapter
 *
 * Description: Setup and/or Search for cards and return info to caller.
 *
 *---------------------------------------------------------------------*/

static int SccbMgr_sense_adapter(PSCCBMGR_INFO pCardInfo)
{
   static UCHAR first_time = 1;

   UCHAR i,j,id,ScamFlg;
   USHORT temp,temp2,temp3,temp4,temp5,temp6;
   ULONG ioport;
	PNVRamInfo pCurrNvRam;

   ioport = pCardInfo->si_baseaddr;


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
      FPT_SccbMgrTableInitAll();
      first_time = 0;
		FPT_mbCards = 0;
      }

	if(FPT_RdStack(ioport, 0) != 0x00) {
		if(FPT_ChkIfChipInitialized(ioport) == 0)
		{
			pCurrNvRam = NULL;
		   WR_HARPOON(ioport+hp_semaphore, 0x00);
			FPT_XbowInit(ioport, 0);             /*Must Init the SCSI before attempting */
			FPT_DiagEEPROM(ioport);
		}
		else
		{
			if(FPT_mbCards < MAX_MB_CARDS) {
				pCurrNvRam = &FPT_nvRamInfo[FPT_mbCards];
				FPT_mbCards++;
				pCurrNvRam->niBaseAddr = ioport;
				FPT_RNVRamData(pCurrNvRam);
			}else
				return((int) FAILURE);
		}
	}else
		pCurrNvRam = NULL;

   WR_HARPOON(ioport+hp_clkctrl_0, CLKCTRL_DEFAULT);
   WR_HARPOON(ioport+hp_sys_ctrl, 0x00);

	if(pCurrNvRam)
		pCardInfo->si_id = pCurrNvRam->niAdapId;
	else
	   pCardInfo->si_id = (UCHAR)(FPT_utilEERead(ioport, (ADAPTER_SCSI_ID/2)) &
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
	      temp = FPT_utilEERead(ioport, (USHORT)((SYNC_RATE_TBL/2)+id));

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
	   i = (UCHAR)(FPT_utilEERead(ioport, (SYSTEM_CONFIG/2)));

	if(pCurrNvRam)
		ScamFlg = pCurrNvRam->niScamConf;
	else
	   ScamFlg = (UCHAR) FPT_utilEERead(ioport, SCAM_CONFIG/2);

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
	   temp = FPT_utilEERead(ioport, (MODEL_NUMB_0/2));
   	pCardInfo->si_card_model[0] = (UCHAR)(temp >> 8);
	   temp = FPT_utilEERead(ioport, (MODEL_NUMB_2/2));

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

   FPT_s_PhaseTbl[0] = FPT_phaseDataOut;
   FPT_s_PhaseTbl[1] = FPT_phaseDataIn;
   FPT_s_PhaseTbl[2] = FPT_phaseIllegal;
   FPT_s_PhaseTbl[3] = FPT_phaseIllegal;
   FPT_s_PhaseTbl[4] = FPT_phaseCommand;
   FPT_s_PhaseTbl[5] = FPT_phaseStatus;
   FPT_s_PhaseTbl[6] = FPT_phaseMsgOut;
   FPT_s_PhaseTbl[7] = FPT_phaseMsgIn;

   pCardInfo->si_present = 0x01;

   return(0);
}


/*---------------------------------------------------------------------
 *
 * Function: SccbMgr_config_adapter
 *
 * Description: Setup adapter for normal operation (hard reset).
 *
 *---------------------------------------------------------------------*/

static ULONG SccbMgr_config_adapter(PSCCBMGR_INFO pCardInfo)
{
   PSCCBcard CurrCard = NULL;
	PNVRamInfo pCurrNvRam;
   UCHAR i,j,thisCard, ScamFlg;
   USHORT temp,sync_bit_map,id;
   ULONG ioport;

   ioport = pCardInfo->si_baseaddr;

   for(thisCard =0; thisCard <= MAX_CARDS; thisCard++) {

      if (thisCard == MAX_CARDS) {

	 return(FAILURE);
         }

      if (FPT_BL_Card[thisCard].ioPort == ioport) {

         CurrCard = &FPT_BL_Card[thisCard];
         FPT_SccbMgrTableInitCard(CurrCard,thisCard);
         break;
         }

      else if (FPT_BL_Card[thisCard].ioPort == 0x00) {

         FPT_BL_Card[thisCard].ioPort = ioport;
         CurrCard = &FPT_BL_Card[thisCard];

			if(FPT_mbCards)
				for(i = 0; i < FPT_mbCards; i++){
					if(CurrCard->ioPort == FPT_nvRamInfo[i].niBaseAddr)
						CurrCard->pNvRamInfo = &FPT_nvRamInfo[i];
				}
         FPT_SccbMgrTableInitCard(CurrCard,thisCard);
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
	   ScamFlg = (UCHAR) FPT_utilEERead(ioport, SCAM_CONFIG/2);
	}


   FPT_BusMasterInit(ioport);
   FPT_XbowInit(ioport, ScamFlg);

   FPT_autoLoadDefaultMap(ioport);


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

      FPT_sresb(ioport,thisCard);

         FPT_scini(thisCard, pCardInfo->si_id, 0);
      }



   if (pCardInfo->si_flags & POST_ALL_UNDERRRUNS)
      CurrCard->globalFlags |= F_NO_FILTER;

	if(pCurrNvRam){
		if(pCurrNvRam->niSysConf & 0x10)
			CurrCard->globalFlags |= F_GREEN_PC;
	}
	else{
	   if (FPT_utilEERead(ioport, (SYSTEM_CONFIG/2)) & GREEN_PC_ENA)
   	   CurrCard->globalFlags |= F_GREEN_PC;
	}

	/* Set global flag to indicate Re-Negotiation to be done on all
		ckeck condition */
	if(pCurrNvRam){
		if(pCurrNvRam->niScsiConf & 0x04)
			CurrCard->globalFlags |= F_DO_RENEGO;
	}
	else{
	   if (FPT_utilEERead(ioport, (SCSI_CONFIG/2)) & RENEGO_ENA)
   	   CurrCard->globalFlags |= F_DO_RENEGO;
	}

	if(pCurrNvRam){
		if(pCurrNvRam->niScsiConf & 0x08)
			CurrCard->globalFlags |= F_CONLUN_IO;
	}
	else{
	   if (FPT_utilEERead(ioport, (SCSI_CONFIG/2)) & CONNIO_ENA)
   	   CurrCard->globalFlags |= F_CONLUN_IO;
	}


   temp = pCardInfo->si_per_targ_no_disc;

   for (i = 0,id = 1; i < MAX_SCSI_TAR; i++, id <<= 1) {

      if (temp & id)
	 FPT_sccbMgrTbl[thisCard][i].TarStatus |= TAR_ALLOW_DISC;
      }

   sync_bit_map = 0x0001;

   for (id = 0; id < (MAX_SCSI_TAR/2); id++) {

		if(pCurrNvRam){
			temp = (USHORT) pCurrNvRam->niSyncTbl[id];
			temp = ((temp & 0x03) + ((temp << 4) & 0xc0)) +
					 (((temp << 4) & 0x0300) + ((temp << 8) & 0xc000));
		}else
	      temp = FPT_utilEERead(ioport, (USHORT)((SYNC_RATE_TBL/2)+id));

      for (i = 0; i < 2; temp >>=8,i++) {

         if (pCardInfo->si_per_targ_init_sync & sync_bit_map) {

            FPT_sccbMgrTbl[thisCard][id*2+i].TarEEValue = (UCHAR)temp;
            }

         else {
	    FPT_sccbMgrTbl[thisCard][id*2+i].TarStatus |= SYNC_SUPPORTED;
            FPT_sccbMgrTbl[thisCard][id*2+i].TarEEValue =
               (UCHAR)(temp & ~EE_SYNC_MASK);
            }

/*         if ((pCardInfo->si_per_targ_wide_nego & sync_bit_map) ||
            (id*2+i >= 8)){
*/
         if (pCardInfo->si_per_targ_wide_nego & sync_bit_map){

            FPT_sccbMgrTbl[thisCard][id*2+i].TarEEValue |= EE_WIDE_SCSI;

            }

         else { /* NARROW SCSI */
            FPT_sccbMgrTbl[thisCard][id*2+i].TarStatus |= WIDE_NEGOCIATED;
            }


	 sync_bit_map <<= 1;



         }
      }

   WR_HARPOON((ioport+hp_semaphore),
      (UCHAR)(RD_HARPOON((ioport+hp_semaphore)) | SCCB_MGR_PRESENT));

   return((ULONG)CurrCard);
}

static void SccbMgr_unload_card(ULONG pCurrCard)
{
	UCHAR i;
	ULONG portBase;
	ULONG regOffset;
	ULONG scamData;
	ULONG *pScamTbl;
	PNVRamInfo pCurrNvRam;

	pCurrNvRam = ((PSCCBcard)pCurrCard)->pNvRamInfo;

	if(pCurrNvRam){
		FPT_WrStack(pCurrNvRam->niBaseAddr, 0, pCurrNvRam->niModel);
		FPT_WrStack(pCurrNvRam->niBaseAddr, 1, pCurrNvRam->niSysConf);
		FPT_WrStack(pCurrNvRam->niBaseAddr, 2, pCurrNvRam->niScsiConf);
		FPT_WrStack(pCurrNvRam->niBaseAddr, 3, pCurrNvRam->niScamConf);
		FPT_WrStack(pCurrNvRam->niBaseAddr, 4, pCurrNvRam->niAdapId);

		for(i = 0; i < MAX_SCSI_TAR / 2; i++)
			FPT_WrStack(pCurrNvRam->niBaseAddr, (UCHAR)(i+5), pCurrNvRam->niSyncTbl[i]);

		portBase = pCurrNvRam->niBaseAddr;

		for(i = 0; i < MAX_SCSI_TAR; i++){
			regOffset = hp_aramBase + 64 + i*4;
			pScamTbl = (ULONG *) &pCurrNvRam->niScamTbl[i];
			scamData = *pScamTbl;
			WR_HARP32(portBase, regOffset, scamData);
		}

	}else{
		FPT_WrStack(((PSCCBcard)pCurrCard)->ioPort, 0, 0);
	}
}


static void FPT_RNVRamData(PNVRamInfo pNvRamInfo)
{
	UCHAR i;
	ULONG portBase;
	ULONG regOffset;
	ULONG scamData;
	ULONG *pScamTbl;

	pNvRamInfo->niModel    = FPT_RdStack(pNvRamInfo->niBaseAddr, 0);
	pNvRamInfo->niSysConf  = FPT_RdStack(pNvRamInfo->niBaseAddr, 1);
	pNvRamInfo->niScsiConf = FPT_RdStack(pNvRamInfo->niBaseAddr, 2);
	pNvRamInfo->niScamConf = FPT_RdStack(pNvRamInfo->niBaseAddr, 3);
	pNvRamInfo->niAdapId   = FPT_RdStack(pNvRamInfo->niBaseAddr, 4);

	for(i = 0; i < MAX_SCSI_TAR / 2; i++)
		pNvRamInfo->niSyncTbl[i] = FPT_RdStack(pNvRamInfo->niBaseAddr, (UCHAR)(i+5));

	portBase = pNvRamInfo->niBaseAddr;

	for(i = 0; i < MAX_SCSI_TAR; i++){
		regOffset = hp_aramBase + 64 + i*4;
		RD_HARP32(portBase, regOffset, scamData);
		pScamTbl = (ULONG *) &pNvRamInfo->niScamTbl[i];
		*pScamTbl = scamData;
	}

}

static UCHAR FPT_RdStack(ULONG portBase, UCHAR index)
{
	WR_HARPOON(portBase + hp_stack_addr, index);
	return(RD_HARPOON(portBase + hp_stack_data));
}

static void FPT_WrStack(ULONG portBase, UCHAR index, UCHAR data)
{
	WR_HARPOON(portBase + hp_stack_addr, index);
	WR_HARPOON(portBase + hp_stack_data, data);
}


static UCHAR FPT_ChkIfChipInitialized(ULONG ioPort)
{
	if((RD_HARPOON(ioPort + hp_arb_id) & 0x0f) != FPT_RdStack(ioPort, 4))
		return(0);
	if((RD_HARPOON(ioPort + hp_clkctrl_0) & CLKCTRL_DEFAULT)
								!= CLKCTRL_DEFAULT)
		return(0);
	if((RD_HARPOON(ioPort + hp_seltimeout) == TO_250ms) ||
		(RD_HARPOON(ioPort + hp_seltimeout) == TO_290ms))
		return(1);
	return(0);

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
static void SccbMgr_start_sccb(ULONG pCurrCard, PSCCB p_Sccb)
{
   ULONG ioport;
   UCHAR thisCard, lun;
	PSCCB pSaveSccb;
   CALL_BK_FN callback;

   thisCard = ((PSCCBcard) pCurrCard)->cardIndex;
   ioport = ((PSCCBcard) pCurrCard)->ioPort;

	if((p_Sccb->TargID > MAX_SCSI_TAR) || (p_Sccb->Lun > MAX_LUN))
	{

		p_Sccb->HostStatus = SCCB_COMPLETE;
		p_Sccb->SccbStatus = SCCB_ERROR;
		callback = (CALL_BK_FN)p_Sccb->SccbCallback;
		if (callback)
			callback(p_Sccb);

		return;
	}

   FPT_sinits(p_Sccb,thisCard);


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
				FPT_queueSelectFail(&FPT_BL_Card[thisCard], thisCard);
				((PSCCBcard) pCurrCard)->currentSCCB = pSaveSccb;
			}
		else
			{
	      FPT_queueAddSccb(p_Sccb,thisCard);
			}
      }

   else if ((RD_HARPOON(ioport+hp_page_ctrl) & G_INT_DISABLE)) {

			if(p_Sccb->OperationCode == RESET_COMMAND)
				{
					pSaveSccb = ((PSCCBcard) pCurrCard)->currentSCCB;
					((PSCCBcard) pCurrCard)->currentSCCB = p_Sccb;
					FPT_queueSelectFail(&FPT_BL_Card[thisCard], thisCard);
					((PSCCBcard) pCurrCard)->currentSCCB = pSaveSccb;
				}
			else
				{
		      FPT_queueAddSccb(p_Sccb,thisCard);
				}
      }

   else {

      MDISABLE_INT(ioport);

		if((((PSCCBcard) pCurrCard)->globalFlags & F_CONLUN_IO) && 
			((FPT_sccbMgrTbl[thisCard][p_Sccb->TargID].TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING))
			lun = p_Sccb->Lun;
		else
			lun = 0;
      if ((((PSCCBcard) pCurrCard)->currentSCCB == NULL) &&
         (FPT_sccbMgrTbl[thisCard][p_Sccb->TargID].TarSelQ_Cnt == 0) &&
         (FPT_sccbMgrTbl[thisCard][p_Sccb->TargID].TarLUNBusy[lun]
         == 0)) {

            ((PSCCBcard) pCurrCard)->currentSCCB = p_Sccb;
	    FPT_ssel(p_Sccb->SccbIOPort,thisCard);
         }

      else {

			if(p_Sccb->OperationCode == RESET_COMMAND)
				{
					pSaveSccb = ((PSCCBcard) pCurrCard)->currentSCCB;
					((PSCCBcard) pCurrCard)->currentSCCB = p_Sccb;
					FPT_queueSelectFail(&FPT_BL_Card[thisCard], thisCard);
					((PSCCBcard) pCurrCard)->currentSCCB = pSaveSccb;
				}
			else
				{
	         	FPT_queueAddSccb(p_Sccb,thisCard);
				}
         }


      MENABLE_INT(ioport);
      }

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
static int SccbMgr_abort_sccb(ULONG pCurrCard, PSCCB p_Sccb)
{
	ULONG ioport;

	UCHAR thisCard;
	CALL_BK_FN callback;
	UCHAR TID;
	PSCCB pSaveSCCB;
	PSCCBMgr_tar_info currTar_Info;


	ioport = ((PSCCBcard) pCurrCard)->ioPort;

	thisCard = ((PSCCBcard)pCurrCard)->cardIndex;

	if (!(RD_HARPOON(ioport+hp_page_ctrl) & G_INT_DISABLE))
	{

		if (FPT_queueFindSccb(p_Sccb,thisCard))
		{

			((PSCCBcard)pCurrCard)->cmdCounter--;

			if (!((PSCCBcard)pCurrCard)->cmdCounter)
				WR_HARPOON(ioport+hp_semaphore,(RD_HARPOON(ioport+hp_semaphore)
					& (UCHAR)(~(SCCB_MGR_ACTIVE | TICKLE_ME)) ));

			p_Sccb->SccbStatus = SCCB_ABORT;
			callback = p_Sccb->SccbCallback;
			callback(p_Sccb);

			return(0);
		}

		else
		{
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
						p_Sccb->Sccb_scsimsg = SMABORT_TAG;

						if(((PSCCBcard) pCurrCard)->currentSCCB == NULL)
						{
							((PSCCBcard) pCurrCard)->currentSCCB = p_Sccb;
							FPT_ssel(ioport, thisCard);
						}
						else
						{
							pSaveSCCB = ((PSCCBcard) pCurrCard)->currentSCCB;
							((PSCCBcard) pCurrCard)->currentSCCB = p_Sccb;
							FPT_queueSelectFail((PSCCBcard) pCurrCard, thisCard);
							((PSCCBcard) pCurrCard)->currentSCCB = pSaveSCCB;
						}
					}
					MENABLE_INT(ioport);
					return(0);
				}
				else
				{
					currTar_Info = &FPT_sccbMgrTbl[thisCard][p_Sccb->TargID];

					if(FPT_BL_Card[thisCard].discQ_Tbl[currTar_Info->LunDiscQ_Idx[p_Sccb->Lun]] 
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
static UCHAR SccbMgr_my_int(ULONG pCurrCard)
{
   ULONG ioport;

   ioport = ((PSCCBcard)pCurrCard)->ioPort;

   if (RD_HARPOON(ioport+hp_int_status) & INT_ASSERTED)
   {
      return(1);
   }

   else

      return(0);
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
static int SccbMgr_isr(ULONG pCurrCard)
{
   PSCCB currSCCB;
   UCHAR thisCard,result,bm_status, bm_int_st;
   USHORT hp_int;
   UCHAR i, target;
   ULONG ioport;

   thisCard = ((PSCCBcard)pCurrCard)->cardIndex;
   ioport = ((PSCCBcard)pCurrCard)->ioPort;

   MDISABLE_INT(ioport);

   if ((bm_int_st=RD_HARPOON(ioport+hp_int_status)) & EXT_STATUS_ON)
		bm_status = RD_HARPOON(ioport+hp_ext_status) & (UCHAR)BAD_EXT_STATUS;
   else
      bm_status = 0;

   WR_HARPOON(ioport+hp_int_mask, (INT_CMD_COMPL | SCSI_INTERRUPT));

   while ((hp_int = RDW_HARPOON((ioport+hp_intstat)) & FPT_default_intena) |
	  bm_status)
     {

       currSCCB = ((PSCCBcard)pCurrCard)->currentSCCB;

      if (hp_int & (FIFO | TIMEOUT | RESET | SCAM_SEL) || bm_status) {
         result = FPT_SccbMgr_bad_isr(ioport,thisCard,((PSCCBcard)pCurrCard),hp_int);
         WRW_HARPOON((ioport+hp_intstat), (FIFO | TIMEOUT | RESET | SCAM_SEL));
         bm_status = 0;

         if (result) {

            MENABLE_INT(ioport);
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

            FPT_phaseChkFifo(ioport, thisCard);

/*         WRW_HARPOON((ioport+hp_intstat),
            (BUS_FREE | ICMD_COMP | ITAR_DISC | XFER_CNT_0));
         */

		 WRW_HARPOON((ioport+hp_intstat), CLR_ALL_INT_1);

         FPT_autoCmdCmplt(ioport,thisCard);

         }


      else if (hp_int & ITAR_DISC)
         {

         if (((PSCCBcard)pCurrCard)->globalFlags & F_HOST_XFER_ACT) {

            FPT_phaseChkFifo(ioport, thisCard);

            }

         if (RD_HARPOON(ioport+hp_gp_reg_1) == SMSAVE_DATA_PTR) {

            WR_HARPOON(ioport+hp_gp_reg_1, 0x00);
            currSCCB->Sccb_XferState |= F_NO_DATA_YET;

            currSCCB->Sccb_savedATC = currSCCB->Sccb_ATC;
            }

         currSCCB->Sccb_scsistat = DISCONNECT_ST;
         FPT_queueDisconnect(currSCCB,thisCard);

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
	       MENABLE_INT(ioport);
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
               FPT_phaseChkFifo(ioport, thisCard);
               }

            if (RD_HARPOON(ioport+hp_gp_reg_1) == SMSAVE_DATA_PTR)
			      {
               WR_HARPOON(ioport+hp_gp_reg_1, 0x00);
               currSCCB->Sccb_XferState |= F_NO_DATA_YET;
               currSCCB->Sccb_savedATC = currSCCB->Sccb_ATC;
               }

            WRW_HARPOON((ioport+hp_intstat), (BUS_FREE | ITAR_DISC));
            currSCCB->Sccb_scsistat = DISCONNECT_ST;
            FPT_queueDisconnect(currSCCB,thisCard);
            }

         FPT_sres(ioport,thisCard,((PSCCBcard)pCurrCard));
         FPT_phaseDecode(ioport,thisCard);

         }


      else if ((hp_int & IDO_STRT) && (!(hp_int & BUS_FREE)))
         {

            WRW_HARPOON((ioport+hp_intstat), (IDO_STRT | XFER_CNT_0));
            FPT_phaseDecode(ioport,thisCard);

         }


      else if ( (hp_int & IUNKWN) || (hp_int & PROG_HLT) )
		   {
	 	   WRW_HARPOON((ioport+hp_intstat), (PHASE | IUNKWN | PROG_HLT));
	 	   if ((RD_HARPOON(ioport+hp_prgmcnt_0) & (UCHAR)0x3f)< (UCHAR)SELCHK)
	    		{
	    		FPT_phaseDecode(ioport,thisCard);
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

         FPT_schkdd(ioport,thisCard);

         }


      else if (hp_int & BUS_FREE) {

         WRW_HARPOON((ioport+hp_intstat), BUS_FREE);

        	if (((PSCCBcard)pCurrCard)->globalFlags & F_HOST_XFER_ACT) {

           	FPT_hostDataXferAbort(ioport,thisCard,currSCCB);
				}

         FPT_phaseBusFree(ioport,thisCard);
			}


      else if (hp_int & ITICKLE) {

         WRW_HARPOON((ioport+hp_intstat), ITICKLE);
         ((PSCCBcard)pCurrCard)->globalFlags |= F_NEW_SCCB_CMD;
         }



      if (((PSCCBcard)pCurrCard)->globalFlags & F_NEW_SCCB_CMD) {


         ((PSCCBcard)pCurrCard)->globalFlags &= ~F_NEW_SCCB_CMD;


         if (((PSCCBcard)pCurrCard)->currentSCCB == NULL) {

            FPT_queueSearchSelect(((PSCCBcard)pCurrCard),thisCard);
            }

         if (((PSCCBcard)pCurrCard)->currentSCCB != NULL) {
            ((PSCCBcard)pCurrCard)->globalFlags &= ~F_NEW_SCCB_CMD;
            FPT_ssel(ioport,thisCard);
            }

         break;

         }

      }  /*end while */

   MENABLE_INT(ioport);

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
static UCHAR FPT_SccbMgr_bad_isr(ULONG p_port, UCHAR p_card,
				 PSCCBcard pCurrCard, USHORT p_int)
{
   UCHAR temp, ScamFlg;
   PSCCBMgr_tar_info currTar_Info;
   PNVRamInfo pCurrNvRam;


   if (RD_HARPOON(p_port+hp_ext_status) &
         (BM_FORCE_OFF | PCI_DEV_TMOUT | BM_PARITY_ERR | PIO_OVERRUN) )
      {

      if (pCurrCard->globalFlags & F_HOST_XFER_ACT)
         {

         FPT_hostDataXferAbort(p_port,p_card, pCurrCard->currentSCCB);
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

         FPT_sxfrp(p_port,p_card);

	     temp = (UCHAR)(RD_HARPOON(p_port+hp_ee_ctrl) &
							(EXT_ARB_ACK | SCSI_TERM_ENA_H));
      	WR_HARPOON(p_port+hp_ee_ctrl, ((UCHAR)temp | SEE_MS | SEE_CS));
         WR_HARPOON(p_port+hp_ee_ctrl, temp);

         if (!(RDW_HARPOON((p_port+hp_intstat)) & (BUS_FREE | RESET)))
            {
            FPT_phaseDecode(p_port,p_card);
            }
         }
      }


   else if (p_int & RESET)
         {

				WR_HARPOON(p_port+hp_clkctrl_0, CLKCTRL_DEFAULT);
				WR_HARPOON(p_port+hp_sys_ctrl, 0x00);
           if (pCurrCard->currentSCCB != NULL) {

               if (pCurrCard->globalFlags & F_HOST_XFER_ACT)

               FPT_hostDataXferAbort(p_port,p_card, pCurrCard->currentSCCB);
               }


           DISABLE_AUTO(p_port);

           FPT_sresb(p_port,p_card);

           while(RD_HARPOON(p_port+hp_scsictrl_0) & SCSI_RST) {}

				pCurrNvRam = pCurrCard->pNvRamInfo;
				if(pCurrNvRam){
					ScamFlg = pCurrNvRam->niScamConf;
				}
				else{
				   ScamFlg = (UCHAR) FPT_utilEERead(p_port, SCAM_CONFIG/2);
				}

           FPT_XbowInit(p_port, ScamFlg);

               FPT_scini(p_card, pCurrCard->ourId, 0);

           return(0xFF);
         }


   else if (p_int & FIFO) {

      WRW_HARPOON((p_port+hp_intstat), FIFO);

      if (pCurrCard->currentSCCB != NULL)
         FPT_sxfrp(p_port,p_card);
      }

   else if (p_int & TIMEOUT)
      {

      DISABLE_AUTO(p_port);

      WRW_HARPOON((p_port+hp_intstat),
		  (PROG_HLT | TIMEOUT | SEL |BUS_FREE | PHASE | IUNKWN));

      pCurrCard->currentSCCB->HostStatus = SCCB_SELECTION_TIMEOUT;


		currTar_Info = &FPT_sccbMgrTbl[p_card][pCurrCard->currentSCCB->TargID];
		if((pCurrCard->globalFlags & F_CONLUN_IO) &&
			((currTar_Info->TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING))
	      currTar_Info->TarLUNBusy[pCurrCard->currentSCCB->Lun] = 0;
		else
	      currTar_Info->TarLUNBusy[0] = 0;


      if (currTar_Info->TarEEValue & EE_SYNC_MASK)
         {
	       currTar_Info->TarSyncCtrl = 0;
         currTar_Info->TarStatus &= ~TAR_SYNC_MASK;
         }

      if (currTar_Info->TarEEValue & EE_WIDE_SCSI)
         {
         currTar_Info->TarStatus &= ~TAR_WIDE_MASK;
         }

      FPT_sssyncv(p_port, pCurrCard->currentSCCB->TargID, NARROW_SCSI,currTar_Info);

      FPT_queueCmdComplete(pCurrCard, pCurrCard->currentSCCB, p_card);

      }

   else if (p_int & SCAM_SEL)
      {

      FPT_scarb(p_port,LEVEL2_TAR);
      FPT_scsel(p_port);
      FPT_scasid(p_card, p_port);

      FPT_scbusf(p_port);

      WRW_HARPOON((p_port+hp_intstat), SCAM_SEL);
      }

   return(0x00);
}


/*---------------------------------------------------------------------
 *
 * Function: SccbMgrTableInit
 *
 * Description: Initialize all Sccb manager data structures.
 *
 *---------------------------------------------------------------------*/

static void FPT_SccbMgrTableInitAll()
{
   UCHAR thisCard;

   for (thisCard = 0; thisCard < MAX_CARDS; thisCard++)
      {
      FPT_SccbMgrTableInitCard(&FPT_BL_Card[thisCard],thisCard);

      FPT_BL_Card[thisCard].ioPort      = 0x00;
      FPT_BL_Card[thisCard].cardInfo    = NULL;
      FPT_BL_Card[thisCard].cardIndex   = 0xFF;
      FPT_BL_Card[thisCard].ourId       = 0x00;
		FPT_BL_Card[thisCard].pNvRamInfo	= NULL;
      }
}


/*---------------------------------------------------------------------
 *
 * Function: SccbMgrTableInit
 *
 * Description: Initialize all Sccb manager data structures.
 *
 *---------------------------------------------------------------------*/

static void FPT_SccbMgrTableInitCard(PSCCBcard pCurrCard, UCHAR p_card)
{
   UCHAR scsiID, qtag;

	for (qtag = 0; qtag < QUEUE_DEPTH; qtag++)
	{
		FPT_BL_Card[p_card].discQ_Tbl[qtag] = NULL;
	}

   for (scsiID = 0; scsiID < MAX_SCSI_TAR; scsiID++)
      {
      FPT_sccbMgrTbl[p_card][scsiID].TarStatus = 0;
      FPT_sccbMgrTbl[p_card][scsiID].TarEEValue = 0;
      FPT_SccbMgrTableInitTarget(p_card, scsiID);
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

static void FPT_SccbMgrTableInitTarget(UCHAR p_card, UCHAR target)
{

	UCHAR lun, qtag;
	PSCCBMgr_tar_info currTar_Info;

	currTar_Info = &FPT_sccbMgrTbl[p_card][target];

	currTar_Info->TarSelQ_Cnt = 0;
	currTar_Info->TarSyncCtrl = 0;

	currTar_Info->TarSelQ_Head = NULL;
	currTar_Info->TarSelQ_Tail = NULL;
	currTar_Info->TarTagQ_Cnt = 0;
	currTar_Info->TarLUN_CA = 0;


	for (lun = 0; lun < MAX_LUN; lun++)
	{
		currTar_Info->TarLUNBusy[lun] = 0;
		currTar_Info->LunDiscQ_Idx[lun] = 0;
	}

	for (qtag = 0; qtag < QUEUE_DEPTH; qtag++)
	{
		if(FPT_BL_Card[p_card].discQ_Tbl[qtag] != NULL)
		{
			if(FPT_BL_Card[p_card].discQ_Tbl[qtag]->TargID == target)
			{
				FPT_BL_Card[p_card].discQ_Tbl[qtag] = NULL;
				FPT_BL_Card[p_card].discQCount--;
			}
		}
	}
}


/*---------------------------------------------------------------------
 *
 * Function: sfetm
 *
 * Description: Read in a message byte from the SCSI bus, and check
 *              for a parity error.
 *
 *---------------------------------------------------------------------*/

static UCHAR FPT_sfm(ULONG port, PSCCB pCurrSCCB)
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
 * Function: FPT_ssel
 *
 * Description: Load up automation and select target device.
 *
 *---------------------------------------------------------------------*/

static void FPT_ssel(ULONG port, UCHAR p_card)
{

   UCHAR auto_loaded, i, target, *theCCB;

   ULONG cdb_reg;
   PSCCBcard CurrCard;
   PSCCB currSCCB;
   PSCCBMgr_tar_info currTar_Info;
   UCHAR lastTag, lun;

   CurrCard = &FPT_BL_Card[p_card];
   currSCCB = CurrCard->currentSCCB;
   target = currSCCB->TargID;
   currTar_Info = &FPT_sccbMgrTbl[p_card][target];
   lastTag = CurrCard->tagQ_Lst;

   ARAM_ACCESS(port);


	if ((currTar_Info->TarStatus & TAR_TAG_Q_MASK) == TAG_Q_REJECT)
		currSCCB->ControlByte &= ~F_USE_CMD_Q;

	if(((CurrCard->globalFlags & F_CONLUN_IO) && 
		((currTar_Info->TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING)))

	   lun = currSCCB->Lun;
	else
		lun = 0;


   if (CurrCard->globalFlags & F_TAG_STARTED)
      {
      if (!(currSCCB->ControlByte & F_USE_CMD_Q))
         {
      	if ((currTar_Info->TarLUN_CA == 0)
      	    && ((currTar_Info->TarStatus & TAR_TAG_Q_MASK)
      	    == TAG_Q_TRYING))
            {

	         if (currTar_Info->TarTagQ_Cnt !=0)
                  {
         		   currTar_Info->TarLUNBusy[lun] = 1;
            		FPT_queueSelectFail(CurrCard,p_card);
					   SGRAM_ACCESS(port);
         		   return;
         		   }

            else {
         		  currTar_Info->TarLUNBusy[lun] = 1;
         		  }

   	      }  /*End non-tagged */

	      else {
	         currTar_Info->TarLUNBusy[lun] = 1;
	         }

	      }  /*!Use cmd Q Tagged */

	   else {
   	     if (currTar_Info->TarLUN_CA == 1)
               {
      	      FPT_queueSelectFail(CurrCard,p_card);
				   SGRAM_ACCESS(port);
      	      return;
	            }

	        currTar_Info->TarLUNBusy[lun] = 1;

   	     }  /*else use cmd Q tagged */

      }  /*if glob tagged started */

   else {
        currTar_Info->TarLUNBusy[lun] = 1;
        }



	if((((CurrCard->globalFlags & F_CONLUN_IO) && 
		((currTar_Info->TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING)) 
		|| (!(currSCCB->ControlByte & F_USE_CMD_Q))))
	{
		if(CurrCard->discQCount >= QUEUE_DEPTH)
		{
			currTar_Info->TarLUNBusy[lun] = 1;
			FPT_queueSelectFail(CurrCard,p_card);
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
			currTar_Info->TarLUNBusy[lun] = 1;
			FPT_queueSelectFail(CurrCard,p_card);
			SGRAM_ACCESS(port);
			return;
		}
	}



   auto_loaded = 0;

   WR_HARPOON(port+hp_select_id, target);
   WR_HARPOON(port+hp_gp_reg_3, target);  /* Use by new automation logic */

   if (currSCCB->OperationCode == RESET_COMMAND) {
      WRW_HARPOON((port+ID_MSG_STRT), (MPM_OP+AMSG_OUT+
               	 (currSCCB->Sccb_idmsg & ~DISC_PRIV)));

      WRW_HARPOON((port+ID_MSG_STRT+2),BRH_OP+ALWAYS+NP);

      currSCCB->Sccb_scsimsg = SMDEV_RESET;

      WR_HARPOON(port+hp_autostart_3, (SELECT+SELCHK_STRT));
      auto_loaded = 1;
      currSCCB->Sccb_scsistat = SELECT_BDR_ST;

      if (currTar_Info->TarEEValue & EE_SYNC_MASK)
         {
	       currTar_Info->TarSyncCtrl = 0;
	      currTar_Info->TarStatus &= ~TAR_SYNC_MASK;
	      }

      if (currTar_Info->TarEEValue & EE_WIDE_SCSI)
         {
      	currTar_Info->TarStatus &= ~TAR_WIDE_MASK;
      	}

      FPT_sssyncv(port, target, NARROW_SCSI,currTar_Info);
      FPT_SccbMgrTableInitTarget(p_card, target);

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
			auto_loaded = 1;
		
		}

   else if (!(currTar_Info->TarStatus & WIDE_NEGOCIATED))  {
      auto_loaded = FPT_siwidn(port,p_card);
      currSCCB->Sccb_scsistat = SELECT_WN_ST;
      }

   else if (!((currTar_Info->TarStatus & TAR_SYNC_MASK)
      == SYNC_SUPPORTED))  {
      auto_loaded = FPT_sisyncn(port,p_card, 0);
      currSCCB->Sccb_scsistat = SELECT_SN_ST;
      }


   if (!auto_loaded)
      {

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

	         currTar_Info->TarLUNBusy[lun] = 1;
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
   	         currTar_Info->TarLUNBusy[lun] = 1;
               FPT_queueSelectFail(CurrCard,p_card);
				   SGRAM_ACCESS(port);
   	         return;
   	         }

            currSCCB->Sccb_scsistat = SELECT_Q_ST;

   	      WR_HARPOON(port+hp_autostart_3, (SELECT+SELCHK_STRT));
            }
         }

      else
         {

         WRW_HARPOON((port+ID_MSG_STRT),BRH_OP+ALWAYS+NTCMD);

      	WRW_HARPOON((port+NON_TAG_ID_MSG),
            (MPM_OP+AMSG_OUT+currSCCB->Sccb_idmsg));

         currSCCB->Sccb_scsistat = SELECT_ST;

         WR_HARPOON(port+hp_autostart_3, (SELECT+SELCHK_STRT));
         }


      theCCB = (UCHAR *)&currSCCB->Cdb[0];

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

   WRW_HARPOON((port+hp_fiforead), (USHORT) 0x00);
   WR_HARPOON(port+hp_xferstat, 0x00);

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
 * Function: FPT_sres
 *
 * Description: Hookup the correct CCB and handle the incoming messages.
 *
 *---------------------------------------------------------------------*/

static void FPT_sres(ULONG port, UCHAR p_card, PSCCBcard pCurrCard)
{

   UCHAR our_target, message, lun = 0, tag, msgRetryCount;


   PSCCBMgr_tar_info currTar_Info;
	PSCCB currSCCB;




	if(pCurrCard->currentSCCB != NULL)
	{
		currTar_Info = &FPT_sccbMgrTbl[p_card][pCurrCard->currentSCCB->TargID];
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
      	currTar_Info->TarLUNBusy[currSCCB->Lun] = 0;
			if(currSCCB->Sccb_scsistat != ABORT_ST)
			{
				pCurrCard->discQCount--;
				pCurrCard->discQ_Tbl[currTar_Info->LunDiscQ_Idx[currSCCB->Lun]] 
													= NULL;
			}
		}
		else
		{
	      currTar_Info->TarLUNBusy[0] = 0;
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

      FPT_queueSelectFail(&FPT_BL_Card[p_card],p_card);
	}

	WRW_HARPOON((port+hp_fiforead), (USHORT) 0x00);


	our_target = (UCHAR)(RD_HARPOON(port+hp_select_id) >> 4);
	currTar_Info = &FPT_sccbMgrTbl[p_card][our_target];


	msgRetryCount = 0;
	do
	{

		currTar_Info = &FPT_sccbMgrTbl[p_card][our_target];
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

			message = FPT_sfm(port,pCurrCard->currentSCCB);
			if (message)
			{

				if (message <= (0x80 | LUN_MASK))
				{
					lun = message & (UCHAR)LUN_MASK;

					if ((currTar_Info->TarStatus & TAR_TAG_Q_MASK) == TAG_Q_TRYING)
					{
						if (currTar_Info->TarTagQ_Cnt != 0)
						{

							if (!(currTar_Info->TarLUN_CA))
							{
								ACCEPT_MSG(port);    /*Release the ACK for ID msg. */


								message = FPT_sfm(port,pCurrCard->currentSCCB);
								if (message)
								{
									ACCEPT_MSG(port);
								}

								else
   								message = 0;

								if(message != 0)
								{
									tag = FPT_sfm(port,pCurrCard->currentSCCB);

									if (!(tag)) 
										message = 0;
								}

							} /*C.A. exists! */

						} /*End Q cnt != 0 */

					} /*End Tag cmds supported! */

				} /*End valid ID message.  */

				else
				{

					ACCEPT_MSG_ATN(port);
				}

			} /* End good id message. */

			else
			{

				message = 0;
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

		if(message == 0)
		{
			msgRetryCount++;
			if(msgRetryCount == 1)
			{
				FPT_SendMsg(port, SMPARITY);
			}
			else
			{
				FPT_SendMsg(port, SMDEV_RESET);

				FPT_sssyncv(port, our_target, NARROW_SCSI,currTar_Info);

				if (FPT_sccbMgrTbl[p_card][our_target].TarEEValue & EE_SYNC_MASK) 
				{
			
					FPT_sccbMgrTbl[p_card][our_target].TarStatus &= ~TAR_SYNC_MASK;

				}

				if (FPT_sccbMgrTbl[p_card][our_target].TarEEValue & EE_WIDE_SCSI) 
				{

					FPT_sccbMgrTbl[p_card][our_target].TarStatus &= ~TAR_WIDE_MASK;
				}


				FPT_queueFlushTargSccb(p_card, our_target, SCCB_COMPLETE);
				FPT_SccbMgrTableInitTarget(p_card,our_target);
				return;
			}
		}
	}while(message == 0);



	if(((pCurrCard->globalFlags & F_CONLUN_IO) &&
		((currTar_Info->TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING)))
	{
		currTar_Info->TarLUNBusy[lun] = 1;
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
		currTar_Info->TarLUNBusy[0] = 1;


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
			FPT_queueFindSccb(pCurrCard->currentSCCB, p_card);
		}
	}


   while (!(RDW_HARPOON((port+hp_intstat)) & (PHASE | RESET)) &&
	  !(RD_HARPOON(port+hp_scsisig) & SCSI_REQ) &&
	  (RD_HARPOON(port+hp_scsisig) & SCSI_BSY)) ;
}

static void FPT_SendMsg(ULONG port, UCHAR message)
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
 * Function: FPT_sdecm
 *
 * Description: Determine the proper responce to the message from the
 *              target device.
 *
 *---------------------------------------------------------------------*/
static void FPT_sdecm(UCHAR message, ULONG port, UCHAR p_card)
{
	PSCCB currSCCB;
	PSCCBcard CurrCard;
	PSCCBMgr_tar_info currTar_Info;

	CurrCard = &FPT_BL_Card[p_card];
	currSCCB = CurrCard->currentSCCB;

	currTar_Info = &FPT_sccbMgrTbl[p_card][currSCCB->TargID];

	if (message == SMREST_DATA_PTR)
	{
		if (!(currSCCB->Sccb_XferState & F_NO_DATA_YET))
		{
			currSCCB->Sccb_ATC = currSCCB->Sccb_savedATC;

			FPT_hostDataXferRestart(currSCCB);
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

				else if ((currSCCB->Sccb_scsistat == SELECT_WN_ST))
				{


					currTar_Info->TarStatus = (currTar_Info->TarStatus &
													~WIDE_ENABLED) | WIDE_NEGOCIATED;

					currTar_Info->TarEEValue &= ~EE_WIDE_SCSI;

				}

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
					currTar_Info->TarLUNBusy[currSCCB->Lun] = 1;
				else
					currTar_Info->TarLUNBusy[0] = 1;


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
		FPT_shandem(port,p_card,currSCCB);
	}

	else if (message == SMIGNORWR)
	{

		ACCEPT_MSG(port);          /* ACK the RESIDUE MSG */

		message = FPT_sfm(port,currSCCB);

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
 * Function: FPT_shandem
 *
 * Description: Decide what to do with the extended message.
 *
 *---------------------------------------------------------------------*/
static void FPT_shandem(ULONG port, UCHAR p_card, PSCCB pCurrSCCB)
{
	UCHAR length,message;

	length = FPT_sfm(port,pCurrSCCB);
	if (length) 
	{

		ACCEPT_MSG(port);
		message = FPT_sfm(port,pCurrSCCB);
		if (message) 
		{

			if (message == SMSYNC) 
			{

				if (length == 0x03)
				{

					ACCEPT_MSG(port);
					FPT_stsyncn(port,p_card);
				}
				else 
				{

					pCurrSCCB->Sccb_scsimsg = SMREJECT;
					ACCEPT_MSG_ATN(port);
				}
			}
			else if (message == SMWDTR) 
			{

				if (length == 0x02)
				{

					ACCEPT_MSG(port);
					FPT_stwidn(port,p_card);
				}
				else 
				{

					pCurrSCCB->Sccb_scsimsg = SMREJECT;
					ACCEPT_MSG_ATN(port);

					WR_HARPOON(port+hp_autostart_1, (AUTO_IMMED+DISCONNECT_START));
				}
			}
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
 * Function: FPT_sisyncn
 *
 * Description: Read in a message byte from the SCSI bus, and check
 *              for a parity error.
 *
 *---------------------------------------------------------------------*/

static UCHAR FPT_sisyncn(ULONG port, UCHAR p_card, UCHAR syncFlag)
{
   PSCCB currSCCB;
   PSCCBMgr_tar_info currTar_Info;

   currSCCB = FPT_BL_Card[p_card].currentSCCB;
   currTar_Info = &FPT_sccbMgrTbl[p_card][currSCCB->TargID];

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


		if(syncFlag == 0)
		{
		   WR_HARPOON(port+hp_autostart_3, (SELECT+SELCHK_STRT));
	      currTar_Info->TarStatus = ((currTar_Info->TarStatus &
   	      ~(UCHAR)TAR_SYNC_MASK) | (UCHAR)SYNC_TRYING);
		}
		else
		{
		   WR_HARPOON(port+hp_autostart_3, (AUTO_IMMED + CMD_ONLY_STRT));
		}


      return(1);
      }

   else {

      currTar_Info->TarStatus |=	 (UCHAR)SYNC_SUPPORTED;
      currTar_Info->TarEEValue &= ~EE_SYNC_MASK;
      return(0);
      }
}



/*---------------------------------------------------------------------
 *
 * Function: FPT_stsyncn
 *
 * Description: The has sent us a Sync Nego message so handle it as
 *              necessary.
 *
 *---------------------------------------------------------------------*/
static void FPT_stsyncn(ULONG port, UCHAR p_card)
{
   UCHAR sync_msg,offset,sync_reg,our_sync_msg;
   PSCCB currSCCB;
   PSCCBMgr_tar_info currTar_Info;

   currSCCB = FPT_BL_Card[p_card].currentSCCB;
   currTar_Info = &FPT_sccbMgrTbl[p_card][currSCCB->TargID];

   sync_msg = FPT_sfm(port,currSCCB);

	if((sync_msg == 0x00) && (currSCCB->Sccb_scsimsg == SMPARITY))
	{
		WR_HARPOON(port+hp_autostart_1, (AUTO_IMMED+DISCONNECT_START));
		return;
	}

   ACCEPT_MSG(port);


   offset = FPT_sfm(port,currSCCB);

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


   if (currTar_Info->TarStatus & WIDE_ENABLED)

      sync_reg |= offset;

   else

      sync_reg |= (offset | NARROW_SCSI);

   FPT_sssyncv(port,currSCCB->TargID,sync_reg,currTar_Info);


   if (currSCCB->Sccb_scsistat == SELECT_SN_ST) {


      ACCEPT_MSG(port);

      currTar_Info->TarStatus = ((currTar_Info->TarStatus &
         ~(UCHAR)TAR_SYNC_MASK) | (UCHAR)SYNC_SUPPORTED);

      WR_HARPOON(port+hp_autostart_1, (AUTO_IMMED+DISCONNECT_START));
      }

   else {


      ACCEPT_MSG_ATN(port);

      FPT_sisyncr(port,sync_msg,offset);

      currTar_Info->TarStatus = ((currTar_Info->TarStatus &
         ~(UCHAR)TAR_SYNC_MASK) | (UCHAR)SYNC_SUPPORTED);
      }
}


/*---------------------------------------------------------------------
 *
 * Function: FPT_sisyncr
 *
 * Description: Answer the targets sync message.
 *
 *---------------------------------------------------------------------*/
static void FPT_sisyncr(ULONG port,UCHAR sync_pulse, UCHAR offset)
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



/*---------------------------------------------------------------------
 *
 * Function: FPT_siwidn
 *
 * Description: Read in a message byte from the SCSI bus, and check
 *              for a parity error.
 *
 *---------------------------------------------------------------------*/

static UCHAR FPT_siwidn(ULONG port, UCHAR p_card)
{
   PSCCB currSCCB;
   PSCCBMgr_tar_info currTar_Info;

   currSCCB = FPT_BL_Card[p_card].currentSCCB;
   currTar_Info = &FPT_sccbMgrTbl[p_card][currSCCB->TargID];

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

      return(1);
      }

   else {

      currTar_Info->TarStatus = ((currTar_Info->TarStatus &
               ~(UCHAR)TAR_WIDE_MASK) | WIDE_NEGOCIATED);

      currTar_Info->TarEEValue &= ~EE_WIDE_SCSI;
      return(0);
      }
}



/*---------------------------------------------------------------------
 *
 * Function: FPT_stwidn
 *
 * Description: The has sent us a Wide Nego message so handle it as
 *              necessary.
 *
 *---------------------------------------------------------------------*/
static void FPT_stwidn(ULONG port, UCHAR p_card)
{
   UCHAR width;
   PSCCB currSCCB;
   PSCCBMgr_tar_info currTar_Info;

   currSCCB = FPT_BL_Card[p_card].currentSCCB;
   currTar_Info = &FPT_sccbMgrTbl[p_card][currSCCB->TargID];

   width = FPT_sfm(port,currSCCB);

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


   FPT_sssyncv(port,currSCCB->TargID,width,currTar_Info);


   if (currSCCB->Sccb_scsistat == SELECT_WN_ST)
	{



      currTar_Info->TarStatus |=	 WIDE_NEGOCIATED;

	   if (!((currTar_Info->TarStatus & TAR_SYNC_MASK) == SYNC_SUPPORTED))
		{
	      ACCEPT_MSG_ATN(port);
		   ARAM_ACCESS(port);
	     	FPT_sisyncn(port,p_card, 1);
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

      FPT_siwidr(port,width);

      currTar_Info->TarStatus |= (WIDE_NEGOCIATED | WIDE_ENABLED);
      }
}


/*---------------------------------------------------------------------
 *
 * Function: FPT_siwidr
 *
 * Description: Answer the targets Wide nego message.
 *
 *---------------------------------------------------------------------*/
static void FPT_siwidr(ULONG port, UCHAR width)
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



/*---------------------------------------------------------------------
 *
 * Function: FPT_sssyncv
 *
 * Description: Write the desired value to the Sync Register for the
 *              ID specified.
 *
 *---------------------------------------------------------------------*/
static void FPT_sssyncv(ULONG p_port, UCHAR p_id, UCHAR p_sync_value,
			PSCCBMgr_tar_info currTar_Info)
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
 * Function: FPT_sresb
 *
 * Description: Reset the desired card's SCSI bus.
 *
 *---------------------------------------------------------------------*/
static void FPT_sresb(ULONG port, UCHAR p_card)
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

   FPT_Wait(port, TO_5ms);

   WRW_HARPOON((port+hp_intstat), CLR_ALL_INT);

   WR_HARPOON(port+hp_int_mask, (RD_HARPOON(port+hp_int_mask) | 0x00));

   for (scsiID = 0; scsiID < MAX_SCSI_TAR; scsiID++)
      {
      currTar_Info = &FPT_sccbMgrTbl[p_card][scsiID];

      if (currTar_Info->TarEEValue & EE_SYNC_MASK)
         {
	      	currTar_Info->TarSyncCtrl = 0;
	      	currTar_Info->TarStatus &= ~TAR_SYNC_MASK;
	      }

      if (currTar_Info->TarEEValue & EE_WIDE_SCSI)
         {
      	currTar_Info->TarStatus &= ~TAR_WIDE_MASK;
      	}

      FPT_sssyncv(port, scsiID, NARROW_SCSI,currTar_Info);

      FPT_SccbMgrTableInitTarget(p_card, scsiID);
      }

   FPT_BL_Card[p_card].scanIndex = 0x00;
   FPT_BL_Card[p_card].currentSCCB = NULL;
   FPT_BL_Card[p_card].globalFlags &= ~(F_TAG_STARTED | F_HOST_XFER_ACT 
													| F_NEW_SCCB_CMD);
   FPT_BL_Card[p_card].cmdCounter  = 0x00;
	FPT_BL_Card[p_card].discQCount = 0x00;
   FPT_BL_Card[p_card].tagQ_Lst = 0x01; 

	for(i = 0; i < QUEUE_DEPTH; i++)
		FPT_BL_Card[p_card].discQ_Tbl[i] = NULL;

   WR_HARPOON(port+hp_page_ctrl,
      (RD_HARPOON(port+hp_page_ctrl) & ~G_INT_DISABLE));

}

/*---------------------------------------------------------------------
 *
 * Function: FPT_ssenss
 *
 * Description: Setup for the Auto Sense command.
 *
 *---------------------------------------------------------------------*/
static void FPT_ssenss(PSCCBcard pCurrCard)
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
 * Function: FPT_sxfrp
 *
 * Description: Transfer data into the bit bucket until the device
 *              decides to switch phase.
 *
 *---------------------------------------------------------------------*/

static void FPT_sxfrp(ULONG p_port, UCHAR p_card)
{
   UCHAR curr_phz;


   DISABLE_AUTO(p_port);

   if (FPT_BL_Card[p_card].globalFlags & F_HOST_XFER_ACT) {

      FPT_hostDataXferAbort(p_port,p_card,FPT_BL_Card[p_card].currentSCCB);

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
 * Function: FPT_schkdd
 *
 * Description: Make sure data has been flushed from both FIFOs and abort
 *              the operations if necessary.
 *
 *---------------------------------------------------------------------*/

static void FPT_schkdd(ULONG port, UCHAR p_card)
{
   USHORT TimeOutLoop;
	UCHAR sPhase;

   PSCCB currSCCB;

   currSCCB = FPT_BL_Card[p_card].currentSCCB;


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


   FPT_hostDataXferAbort(port,p_card,currSCCB);


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
	         FPT_phaseDataIn(port,p_card);
	      	}

	   	else {
	       FPT_phaseDataOut(port,p_card);
	       	}
	   	}
		else
      	{
	   	FPT_sxfrp(port,p_card);
	   	if (!(RDW_HARPOON((port+hp_intstat)) &
		      (BUS_FREE | ICMD_COMP | ITAR_DISC | RESET)))
         {
   		WRW_HARPOON((port+hp_intstat), AUTO_INT);
		   FPT_phaseDecode(port,p_card);
		   }
	   }

   }

   else {
      WR_HARPOON(port+hp_portctrl_0, 0x00);
      }
}


/*---------------------------------------------------------------------
 *
 * Function: FPT_sinits
 *
 * Description: Setup SCCB manager fields in this SCCB.
 *
 *---------------------------------------------------------------------*/

static void FPT_sinits(PSCCB p_sccb, UCHAR p_card)
{
   PSCCBMgr_tar_info currTar_Info;

	if((p_sccb->TargID > MAX_SCSI_TAR) || (p_sccb->Lun > MAX_LUN))
	{
		return;
	}
   currTar_Info = &FPT_sccbMgrTbl[p_card][p_sccb->TargID];

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
   if (((!(FPT_BL_Card[p_card].globalFlags & F_SINGLE_DEVICE)) &&
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


/*---------------------------------------------------------------------
 *
 * Function: Phase Decode
 *
 * Description: Determine the phase and call the appropriate function.
 *
 *---------------------------------------------------------------------*/

static void FPT_phaseDecode(ULONG p_port, UCHAR p_card)
{
   unsigned char phase_ref;
   void (*phase) (ULONG, UCHAR);


   DISABLE_AUTO(p_port);

   phase_ref = (UCHAR) (RD_HARPOON(p_port+hp_scsisig) & S_SCSI_PHZ);

   phase = FPT_s_PhaseTbl[phase_ref];

   (*phase)(p_port, p_card);           /* Call the correct phase func */
}



/*---------------------------------------------------------------------
 *
 * Function: Data Out Phase
 *
 * Description: Start up both the BusMaster and Xbow.
 *
 *---------------------------------------------------------------------*/

static void FPT_phaseDataOut(ULONG port, UCHAR p_card)
{

   PSCCB currSCCB;

   currSCCB = FPT_BL_Card[p_card].currentSCCB;
   if (currSCCB == NULL)
      {
      return;  /* Exit if No SCCB record */
      }

   currSCCB->Sccb_scsistat = DATA_OUT_ST;
   currSCCB->Sccb_XferState &= ~(F_HOST_XFER_DIR | F_NO_DATA_YET);

   WR_HARPOON(port+hp_portctrl_0, SCSI_PORT);

   WRW_HARPOON((port+hp_intstat), XFER_CNT_0);

   WR_HARPOON(port+hp_autostart_0, (END_DATA+END_DATA_START));

   FPT_dataXferProcessor(port, &FPT_BL_Card[p_card]);

   if (currSCCB->Sccb_XferCnt == 0) {


      if ((currSCCB->ControlByte & SCCB_DATA_XFER_OUT) &&
	 (currSCCB->HostStatus == SCCB_COMPLETE))
	 currSCCB->HostStatus = SCCB_DATA_OVER_RUN;

      FPT_sxfrp(port,p_card);
      if (!(RDW_HARPOON((port+hp_intstat)) & (BUS_FREE | RESET)))
	    FPT_phaseDecode(port,p_card);
      }
}


/*---------------------------------------------------------------------
 *
 * Function: Data In Phase
 *
 * Description: Startup the BusMaster and the XBOW.
 *
 *---------------------------------------------------------------------*/

static void FPT_phaseDataIn(ULONG port, UCHAR p_card)
{

   PSCCB currSCCB;

   currSCCB = FPT_BL_Card[p_card].currentSCCB;

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

   FPT_dataXferProcessor(port, &FPT_BL_Card[p_card]);

   if (currSCCB->Sccb_XferCnt == 0) {


      if ((currSCCB->ControlByte & SCCB_DATA_XFER_IN) &&
	 (currSCCB->HostStatus == SCCB_COMPLETE))
	 currSCCB->HostStatus = SCCB_DATA_OVER_RUN;

      FPT_sxfrp(port,p_card);
      if (!(RDW_HARPOON((port+hp_intstat)) & (BUS_FREE | RESET)))
	    FPT_phaseDecode(port,p_card);

      }
}

/*---------------------------------------------------------------------
 *
 * Function: Command Phase
 *
 * Description: Load the CDB into the automation and start it up.
 *
 *---------------------------------------------------------------------*/

static void FPT_phaseCommand(ULONG p_port, UCHAR p_card)
{
   PSCCB currSCCB;
   ULONG cdb_reg;
   UCHAR i;

   currSCCB = FPT_BL_Card[p_card].currentSCCB;

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

static void FPT_phaseStatus(ULONG port, UCHAR p_card)
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

static void FPT_phaseMsgOut(ULONG port, UCHAR p_card)
{
	UCHAR message,scsiID;
	PSCCB currSCCB;
	PSCCBMgr_tar_info currTar_Info;

	currSCCB = FPT_BL_Card[p_card].currentSCCB;

	if (currSCCB != NULL) {

		message = currSCCB->Sccb_scsimsg;
		scsiID = currSCCB->TargID;

		if (message == SMDEV_RESET) 
		{


			currTar_Info = &FPT_sccbMgrTbl[p_card][scsiID];
			currTar_Info->TarSyncCtrl = 0;
			FPT_sssyncv(port, scsiID, NARROW_SCSI,currTar_Info);

			if (FPT_sccbMgrTbl[p_card][scsiID].TarEEValue & EE_SYNC_MASK) 
			{

				FPT_sccbMgrTbl[p_card][scsiID].TarStatus &= ~TAR_SYNC_MASK;

			}

			if (FPT_sccbMgrTbl[p_card][scsiID].TarEEValue & EE_WIDE_SCSI) 
			{

				FPT_sccbMgrTbl[p_card][scsiID].TarStatus &= ~TAR_WIDE_MASK;
			}


			FPT_queueFlushSccb(p_card,SCCB_COMPLETE);
			FPT_SccbMgrTableInitTarget(p_card,scsiID);
		}
		else if (currSCCB->Sccb_scsistat == ABORT_ST)
		{
			currSCCB->HostStatus = SCCB_COMPLETE;
			if(FPT_BL_Card[p_card].discQ_Tbl[currSCCB->Sccb_tag] != NULL)
			{
				FPT_BL_Card[p_card].discQ_Tbl[currSCCB->Sccb_tag] = NULL;
				FPT_sccbMgrTbl[p_card][scsiID].TarTagQ_Cnt--;
			}
					
		}

		else if (currSCCB->Sccb_scsistat < COMMAND_ST) 
		{


			if(message == SMNO_OP)
			{
				currSCCB->Sccb_MGRFlags |= F_DEV_SELECTED;
		
				FPT_ssel(port,p_card);
				return;
			}
		}
		else 
		{


			if (message == SMABORT)

				FPT_queueFlushSccb(p_card,SCCB_COMPLETE);
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

				if((FPT_BL_Card[p_card].globalFlags & F_CONLUN_IO) &&
					((FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING))
					FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[currSCCB->Lun] = 0;
				else
					FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[0] = 0;

				FPT_queueCmdComplete(&FPT_BL_Card[p_card],currSCCB, p_card);
			}

			else 
			{
				FPT_BL_Card[p_card].globalFlags |= F_NEW_SCCB_CMD;
			}
		}

		else 
		{

			FPT_sxfrp(port,p_card);
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
			FPT_sxfrp(port,p_card);
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

static void FPT_phaseMsgIn(ULONG port, UCHAR p_card)
{
	UCHAR message;
	PSCCB currSCCB;

	currSCCB = FPT_BL_Card[p_card].currentSCCB;

	if (FPT_BL_Card[p_card].globalFlags & F_HOST_XFER_ACT) 
	{

		FPT_phaseChkFifo(port, p_card);
	}

	message = RD_HARPOON(port+hp_scsidata_0);
	if ((message == SMDISC) || (message == SMSAVE_DATA_PTR)) 
	{

		WR_HARPOON(port+hp_autostart_1, (AUTO_IMMED+END_DATA_START));

	}

	else 
	{

		message = FPT_sfm(port,currSCCB);
		if (message) 
		{


			FPT_sdecm(message,port,p_card);

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

static void FPT_phaseIllegal(ULONG port, UCHAR p_card)
{
   PSCCB currSCCB;

   currSCCB = FPT_BL_Card[p_card].currentSCCB;

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

static void FPT_phaseChkFifo(ULONG port, UCHAR p_card)
{
   ULONG xfercnt;
   PSCCB currSCCB;

   currSCCB = FPT_BL_Card[p_card].currentSCCB;

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

	      FPT_hostDataXferAbort(port,p_card,currSCCB);

	      FPT_dataXferProcessor(port, &FPT_BL_Card[p_card]);

	      while((!(RD_HARPOON(port+hp_xferstat) & FIFO_EMPTY)) &&
	         (RD_HARPOON(port+hp_ext_status) & BM_CMD_BUSY)) {}

	      }
      }  /*End Data In specific code. */



   GET_XFER_CNT(port,xfercnt);


   WR_HARPOON(port+hp_xfercnt_0, 0x00);


   WR_HARPOON(port+hp_portctrl_0, 0x00);

   currSCCB->Sccb_ATC += (currSCCB->Sccb_XferCnt - xfercnt);

   currSCCB->Sccb_XferCnt = xfercnt;

   if ((RDW_HARPOON((port+hp_intstat)) & PARITY) &&
      (currSCCB->HostStatus == SCCB_COMPLETE)) {

      currSCCB->HostStatus = SCCB_PARITY_ERR;
      WRW_HARPOON((port+hp_intstat), PARITY);
      }


   FPT_hostDataXferAbort(port,p_card,currSCCB);


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
static void FPT_phaseBusFree(ULONG port, UCHAR p_card)
{
   PSCCB currSCCB;

   currSCCB = FPT_BL_Card[p_card].currentSCCB;

   if (currSCCB != NULL)
      {

      DISABLE_AUTO(port);


      if (currSCCB->OperationCode == RESET_COMMAND)
         {

			if((FPT_BL_Card[p_card].globalFlags & F_CONLUN_IO) &&
				((FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING))
	   		 FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[currSCCB->Lun] = 0;
			else
		   	 FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[0] = 0;

	      FPT_queueCmdComplete(&FPT_BL_Card[p_card], currSCCB, p_card);

	      FPT_queueSearchSelect(&FPT_BL_Card[p_card],p_card);

	      }

      else if(currSCCB->Sccb_scsistat == SELECT_SN_ST)
	      {
	      FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarStatus |=
			         (UCHAR)SYNC_SUPPORTED;
	      FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarEEValue &= ~EE_SYNC_MASK;
	      }

      else if(currSCCB->Sccb_scsistat == SELECT_WN_ST)
	      {
	      FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarStatus =
		            (FPT_sccbMgrTbl[p_card][currSCCB->TargID].
		   TarStatus & ~WIDE_ENABLED) | WIDE_NEGOCIATED;

	      FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarEEValue &= ~EE_WIDE_SCSI;
	      }

      else if(currSCCB->Sccb_scsistat == SELECT_Q_ST)
	      {
	      /* Make sure this is not a phony BUS_FREE.  If we were
	      reselected or if BUSY is NOT on then this is a
	      valid BUS FREE.  SRR Wednesday, 5/10/1995.     */

	      if ((!(RD_HARPOON(port+hp_scsisig) & SCSI_BSY)) ||
	         (RDW_HARPOON((port+hp_intstat)) & RSEL))
	         {
	         FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarStatus &= ~TAR_TAG_Q_MASK;
	         FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarStatus |= TAG_Q_REJECT;
	         }

	      else
            {
	         return;
	         }
         }

      else
	      {

	      currSCCB->Sccb_scsistat = BUS_FREE_ST;

         if (!currSCCB->HostStatus)
	         {
	         currSCCB->HostStatus = SCCB_PHASE_SEQUENCE_FAIL;
	         }

			if((FPT_BL_Card[p_card].globalFlags & F_CONLUN_IO) &&
				((FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING))
	   		 FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[currSCCB->Lun] = 0;
			else
		   	 FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[0] = 0;

	      FPT_queueCmdComplete(&FPT_BL_Card[p_card], currSCCB, p_card);
	      return;
	      }


      FPT_BL_Card[p_card].globalFlags |= F_NEW_SCCB_CMD;

      } /*end if !=null */
}




/*---------------------------------------------------------------------
 *
 * Function: Auto Load Default Map
 *
 * Description: Load the Automation RAM with the defualt map values.
 *
 *---------------------------------------------------------------------*/
static void FPT_autoLoadDefaultMap(ULONG p_port)
{
   ULONG map_addr;

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

static void FPT_autoCmdCmplt(ULONG p_port, UCHAR p_card)
{
   PSCCB currSCCB;
   UCHAR status_byte;

   currSCCB = FPT_BL_Card[p_card].currentSCCB;

   status_byte = RD_HARPOON(p_port+hp_gp_reg_0);

   FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarLUN_CA = 0;

   if (status_byte != SSGOOD) {

      if (status_byte == SSQ_FULL) {


			if(((FPT_BL_Card[p_card].globalFlags & F_CONLUN_IO) &&
				((FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING)))
			{
	         FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[currSCCB->Lun] = 1;
				if(FPT_BL_Card[p_card].discQCount != 0)
					FPT_BL_Card[p_card].discQCount--;
				FPT_BL_Card[p_card].discQ_Tbl[FPT_sccbMgrTbl[p_card][currSCCB->TargID].LunDiscQ_Idx[currSCCB->Lun]] = NULL;
			}
			else
			{
	         FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[0] = 1;
				if(currSCCB->Sccb_tag)
				{
					if(FPT_BL_Card[p_card].discQCount != 0)
						FPT_BL_Card[p_card].discQCount--;
					FPT_BL_Card[p_card].discQ_Tbl[currSCCB->Sccb_tag] = NULL;
				}else
				{
					if(FPT_BL_Card[p_card].discQCount != 0)
						FPT_BL_Card[p_card].discQCount--;
					FPT_BL_Card[p_card].discQ_Tbl[FPT_sccbMgrTbl[p_card][currSCCB->TargID].LunDiscQ_Idx[0]] = NULL;
				}
			}

         currSCCB->Sccb_MGRFlags |= F_STATUSLOADED;

         FPT_queueSelectFail(&FPT_BL_Card[p_card],p_card);

         return;
         }

      if(currSCCB->Sccb_scsistat == SELECT_SN_ST)
         {
         FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarStatus |=
            (UCHAR)SYNC_SUPPORTED;

	      FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarEEValue &= ~EE_SYNC_MASK;
         FPT_BL_Card[p_card].globalFlags |= F_NEW_SCCB_CMD;

			if(((FPT_BL_Card[p_card].globalFlags & F_CONLUN_IO) &&
				((FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING)))
			{
	         FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[currSCCB->Lun] = 1;
				if(FPT_BL_Card[p_card].discQCount != 0)
					FPT_BL_Card[p_card].discQCount--;
				FPT_BL_Card[p_card].discQ_Tbl[FPT_sccbMgrTbl[p_card][currSCCB->TargID].LunDiscQ_Idx[currSCCB->Lun]] = NULL;
			}
			else
			{
	         FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[0] = 1;
				if(currSCCB->Sccb_tag)
				{
					if(FPT_BL_Card[p_card].discQCount != 0)
						FPT_BL_Card[p_card].discQCount--;
					FPT_BL_Card[p_card].discQ_Tbl[currSCCB->Sccb_tag] = NULL;
				}else
				{
					if(FPT_BL_Card[p_card].discQCount != 0)
						FPT_BL_Card[p_card].discQCount--;
					FPT_BL_Card[p_card].discQ_Tbl[FPT_sccbMgrTbl[p_card][currSCCB->TargID].LunDiscQ_Idx[0]] = NULL;
				}
			}
         return;

         }

      if(currSCCB->Sccb_scsistat == SELECT_WN_ST)
         {

	      FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarStatus =
	         (FPT_sccbMgrTbl[p_card][currSCCB->TargID].
	         TarStatus & ~WIDE_ENABLED) | WIDE_NEGOCIATED;

	      FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarEEValue &= ~EE_WIDE_SCSI;
         FPT_BL_Card[p_card].globalFlags |= F_NEW_SCCB_CMD;

			if(((FPT_BL_Card[p_card].globalFlags & F_CONLUN_IO) &&
				((FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING)))
			{
	         FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[currSCCB->Lun] = 1;
				if(FPT_BL_Card[p_card].discQCount != 0)
					FPT_BL_Card[p_card].discQCount--;
				FPT_BL_Card[p_card].discQ_Tbl[FPT_sccbMgrTbl[p_card][currSCCB->TargID].LunDiscQ_Idx[currSCCB->Lun]] = NULL;
			}
			else
			{
	         FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[0] = 1;
				if(currSCCB->Sccb_tag)
				{
					if(FPT_BL_Card[p_card].discQCount != 0)
						FPT_BL_Card[p_card].discQCount--;
					FPT_BL_Card[p_card].discQ_Tbl[currSCCB->Sccb_tag] = NULL;
				}else
				{
					if(FPT_BL_Card[p_card].discQCount != 0)
						FPT_BL_Card[p_card].discQCount--;
					FPT_BL_Card[p_card].discQ_Tbl[FPT_sccbMgrTbl[p_card][currSCCB->TargID].LunDiscQ_Idx[0]] = NULL;
				}
			}
         return;
      
         }
     
	   if (status_byte == SSCHECK) 
		{
			if(FPT_BL_Card[p_card].globalFlags & F_DO_RENEGO)
			{
				if (FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarEEValue & EE_SYNC_MASK)
				{
					FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarStatus &= ~TAR_SYNC_MASK;
				}
				if (FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarEEValue & EE_WIDE_SCSI)
				{
					FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarStatus &= ~TAR_WIDE_MASK;
				}
			}
		}

      if (!(currSCCB->Sccb_XferState & F_AUTO_SENSE)) {

         currSCCB->SccbStatus = SCCB_ERROR;
         currSCCB->TargetStatus = status_byte;

         if (status_byte == SSCHECK) {

            FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarLUN_CA
               = 1;
     

            if (currSCCB->RequestSenseLength != NO_AUTO_REQUEST_SENSE) {

               if (currSCCB->RequestSenseLength == 0)
                  currSCCB->RequestSenseLength = 14;

               FPT_ssenss(&FPT_BL_Card[p_card]);
               FPT_BL_Card[p_card].globalFlags |= F_NEW_SCCB_CMD;

 					if(((FPT_BL_Card[p_card].globalFlags & F_CONLUN_IO) &&
						((FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING)))
					{
			         FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[currSCCB->Lun] = 1;
						if(FPT_BL_Card[p_card].discQCount != 0)
							FPT_BL_Card[p_card].discQCount--;
						FPT_BL_Card[p_card].discQ_Tbl[FPT_sccbMgrTbl[p_card][currSCCB->TargID].LunDiscQ_Idx[currSCCB->Lun]] = NULL;
					}
					else
					{
	   		      FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[0] = 1;
						if(currSCCB->Sccb_tag)
						{
							if(FPT_BL_Card[p_card].discQCount != 0)
								FPT_BL_Card[p_card].discQCount--;
							FPT_BL_Card[p_card].discQ_Tbl[currSCCB->Sccb_tag] = NULL;
						}else
						{
							if(FPT_BL_Card[p_card].discQCount != 0)
								FPT_BL_Card[p_card].discQCount--;
							FPT_BL_Card[p_card].discQ_Tbl[FPT_sccbMgrTbl[p_card][currSCCB->TargID].LunDiscQ_Idx[0]] = NULL;
						}
					}
               return;
               }
            }
         }
      }


	if((FPT_BL_Card[p_card].globalFlags & F_CONLUN_IO) &&
		((FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING))
	   FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[currSCCB->Lun] = 0;
	else
	   FPT_sccbMgrTbl[p_card][currSCCB->TargID].TarLUNBusy[0] = 0;


   FPT_queueCmdComplete(&FPT_BL_Card[p_card], currSCCB, p_card);
}

#define SHORT_WAIT   0x0000000F
#define LONG_WAIT    0x0000FFFFL


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

static void FPT_dataXferProcessor(ULONG port, PSCCBcard pCurrCard)
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
         
         FPT_busMstrSGDataXferStart(port, currSCCB);
			}

      else
			{
			if (!(pCurrCard->globalFlags & F_HOST_XFER_ACT))
				{
				pCurrCard->globalFlags |= F_HOST_XFER_ACT;
         
         	FPT_busMstrDataXferStart(port, currSCCB);
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
static void FPT_busMstrSGDataXferStart(ULONG p_port, PSCCB pcurrSCCB)
{
   ULONG count,addr,tmpSGCnt;
   UINT sg_index;
   UCHAR sg_count, i;
   ULONG reg_offset;


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

      tmpSGCnt += *(((ULONG *)pcurrSCCB->DataPointer)+
         (sg_index * 2));

      count |= *(((ULONG *)pcurrSCCB->DataPointer)+
         (sg_index * 2));

      addr = *(((ULONG *)pcurrSCCB->DataPointer)+
         ((sg_index * 2) + 1));


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
static void FPT_busMstrDataXferStart(ULONG p_port, PSCCB pcurrSCCB)
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

   HP_SETUP_ADDR_CNT(p_port,addr,count);


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
static UCHAR FPT_busMstrTimeOut(ULONG p_port)
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
      return(1);
      }

   else {
      return(0);
      }
}


/*---------------------------------------------------------------------
 *
 * Function: Host Data Transfer Abort
 *
 * Description: Abort any in progress transfer.
 *
 *---------------------------------------------------------------------*/
static void FPT_hostDataXferAbort(ULONG port, UCHAR p_card, PSCCB pCurrSCCB)
{

   ULONG timeout;
   ULONG remain_cnt;
   UINT sg_ptr;

   FPT_BL_Card[p_card].globalFlags &= ~F_HOST_XFER_ACT;

   if (pCurrSCCB->Sccb_XferState & F_AUTO_SENSE) {


      if (!(RD_HARPOON(port+hp_int_status) & INT_CMD_COMPL)) {

         WR_HARPOON(port+hp_bm_ctrl, (RD_HARPOON(port+hp_bm_ctrl) | FLUSH_XFER_CNTR));
         timeout = LONG_WAIT;

         while ((RD_HARPOON(port+hp_ext_status) & BM_CMD_BUSY) && timeout--) {}

         WR_HARPOON(port+hp_bm_ctrl, (RD_HARPOON(port+hp_bm_ctrl) & ~FLUSH_XFER_CNTR));

         if (RD_HARPOON(port+hp_ext_status) & BM_CMD_BUSY) {

            if (FPT_busMstrTimeOut(port)) {

               if (pCurrSCCB->HostStatus == 0x00)

                  pCurrSCCB->HostStatus = SCCB_BM_ERR;

               }

            if (RD_HARPOON(port+hp_int_status) & INT_EXT_STATUS) 

               if (RD_HARPOON(port+hp_ext_status) & BAD_EXT_STATUS) 

                  if (pCurrSCCB->HostStatus == 0x00)

                     {
                     pCurrSCCB->HostStatus = SCCB_BM_ERR;
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

            if (remain_cnt > (ULONG)(*(((ULONG *)pCurrSCCB->
               DataPointer) + (sg_ptr * 2)))) {

               remain_cnt -= (ULONG)(*(((ULONG *)pCurrSCCB->
                  DataPointer) + (sg_ptr * 2)));
               }

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

            FPT_busMstrTimeOut(port);
            }

         else {

            if (RD_HARPOON(port+hp_int_status) & INT_EXT_STATUS) {

               if (RD_HARPOON(port+hp_ext_status) & BAD_EXT_STATUS) {

                  if (pCurrSCCB->HostStatus == 0x00) {

                     pCurrSCCB->HostStatus = SCCB_BM_ERR;
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

               FPT_busMstrTimeOut(port);
               }
            }

         if (RD_HARPOON(port+hp_int_status) & INT_EXT_STATUS) {

            if (RD_HARPOON(port+hp_ext_status) & BAD_EXT_STATUS) {

               if (pCurrSCCB->HostStatus == 0x00) {

                  pCurrSCCB->HostStatus = SCCB_BM_ERR;
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

            FPT_busMstrTimeOut(port);
            }
         }


      if (RD_HARPOON(port+hp_int_status) & INT_EXT_STATUS) {

         if (RD_HARPOON(port+hp_ext_status) & BAD_EXT_STATUS) {

            if (pCurrSCCB->HostStatus == 0x00) {

               pCurrSCCB->HostStatus = SCCB_BM_ERR;
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
static void FPT_hostDataXferRestart(PSCCB currSCCB)
{
   ULONG data_count;
   UINT  sg_index;
   ULONG *sg_ptr;

   if (currSCCB->Sccb_XferState & F_SG_XFER) {

      currSCCB->Sccb_XferCnt = 0;

      sg_index = 0xffff;         /*Index by long words into sg list. */
      data_count = 0;            /*Running count of SG xfer counts. */

      sg_ptr = (ULONG *)currSCCB->DataPointer;

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



/*---------------------------------------------------------------------
 *
 * Function: FPT_scini
 *
 * Description: Setup all data structures necessary for SCAM selection.
 *
 *---------------------------------------------------------------------*/

static void FPT_scini(UCHAR p_card, UCHAR p_our_id, UCHAR p_power_up)
{

   UCHAR loser,assigned_id;
   ULONG p_port;

   UCHAR i,k,ScamFlg ;
   PSCCBcard currCard;
	PNVRamInfo pCurrNvRam;

   currCard = &FPT_BL_Card[p_card];
   p_port = currCard->ioPort;
	pCurrNvRam = currCard->pNvRamInfo;


	if(pCurrNvRam){
		ScamFlg = pCurrNvRam->niScamConf;
		i = pCurrNvRam->niSysConf;
	}
	else{
	   ScamFlg = (UCHAR) FPT_utilEERead(p_port, SCAM_CONFIG/2);
	   i = (UCHAR)(FPT_utilEERead(p_port, (SYSTEM_CONFIG/2)));
	}
	if(!(i & 0x02))	/* check if reset bus in AutoSCSI parameter set */
		return;

   FPT_inisci(p_card,p_port, p_our_id);

   /* Force to wait 1 sec after SCSI bus reset. Some SCAM device FW
      too slow to return to SCAM selection */

   /* if (p_power_up)
         FPT_Wait1Second(p_port);
      else
         FPT_Wait(p_port, TO_250ms); */

   FPT_Wait1Second(p_port);

   if ((ScamFlg & SCAM_ENABLED) && (ScamFlg & SCAM_LEVEL2))
      {
      while (!(FPT_scarb(p_port,INIT_SELTD))) {}

      FPT_scsel(p_port);

      do {
         FPT_scxferc(p_port,SYNC_PTRN);
         FPT_scxferc(p_port,DOM_MSTR);
         loser = FPT_scsendi(p_port,&FPT_scamInfo[p_our_id].id_string[0]);
         } while ( loser == 0xFF );

      FPT_scbusf(p_port);

      if ((p_power_up) && (!loser))
         {
         FPT_sresb(p_port,p_card);
         FPT_Wait(p_port, TO_250ms);

         while (!(FPT_scarb(p_port,INIT_SELTD))) {}

         FPT_scsel(p_port);

         do {
            FPT_scxferc(p_port, SYNC_PTRN);
            FPT_scxferc(p_port, DOM_MSTR);
            loser = FPT_scsendi(p_port,&FPT_scamInfo[p_our_id].
               id_string[0]);
            } while ( loser == 0xFF );

         FPT_scbusf(p_port);
         }
      }

   else
      {
      loser = 0;
      }


   if (!loser)
      {

      FPT_scamInfo[p_our_id].state = ID_ASSIGNED;


		if (ScamFlg & SCAM_ENABLED)
		{

	      for (i=0; i < MAX_SCSI_TAR; i++)
  		   {
      	   if ((FPT_scamInfo[i].state == ID_UNASSIGNED) ||
  	      	   (FPT_scamInfo[i].state == ID_UNUSED))
	  	      {
   	     	   if (FPT_scsell(p_port,i))
      	  	   {
            	   FPT_scamInfo[i].state = LEGACY;
  	            	if ((FPT_scamInfo[i].id_string[0] != 0xFF) ||
     	            	(FPT_scamInfo[i].id_string[1] != 0xFA))
	     	         {

   	        	      FPT_scamInfo[i].id_string[0] = 0xFF;
      	        	   FPT_scamInfo[i].id_string[1] = 0xFA;
							if(pCurrNvRam == NULL)
	         	         currCard->globalFlags |= F_UPDATE_EEPROM;
               	}
	  	         }
   	  	   }
      	}

	      FPT_sresb(p_port,p_card);
      	FPT_Wait1Second(p_port);
         while (!(FPT_scarb(p_port,INIT_SELTD))) {}
         FPT_scsel(p_port);
         FPT_scasid(p_card, p_port);
         }

      }

   else if ((loser) && (ScamFlg & SCAM_ENABLED))
      {
      FPT_scamInfo[p_our_id].id_string[0] = SLV_TYPE_CODE0;
      assigned_id = 0;
      FPT_scwtsel(p_port);

      do {
         while (FPT_scxferc(p_port,0x00) != SYNC_PTRN) {}

         i = FPT_scxferc(p_port,0x00);
         if (i == ASSIGN_ID)
            {
            if (!(FPT_scsendi(p_port,&FPT_scamInfo[p_our_id].id_string[0])))
                  {
                  i = FPT_scxferc(p_port,0x00);
                  if (FPT_scvalq(i))
                     {
                     k = FPT_scxferc(p_port,0x00);

                     if (FPT_scvalq(k))
                        {
                        currCard->ourId =
                           ((UCHAR)(i<<3)+(k & (UCHAR)7)) & (UCHAR) 0x3F;
                        FPT_inisci(p_card, p_port, p_our_id);
                        FPT_scamInfo[currCard->ourId].state = ID_ASSIGNED;
                        FPT_scamInfo[currCard->ourId].id_string[0]
                           = SLV_TYPE_CODE0;
                        assigned_id = 1;
                        }
                     }
                  }
            }

         else if (i == SET_P_FLAG)
            {
               if (!(FPT_scsendi(p_port,
                        &FPT_scamInfo[p_our_id].id_string[0])))
                        FPT_scamInfo[p_our_id].id_string[0] |= 0x80;
            }
         }while (!assigned_id);

      while (FPT_scxferc(p_port,0x00) != CFG_CMPLT) {}
      }

   if (ScamFlg & SCAM_ENABLED)
      {
      FPT_scbusf(p_port);
      if (currCard->globalFlags & F_UPDATE_EEPROM)
         {
         FPT_scsavdi(p_card, p_port);
         currCard->globalFlags &= ~F_UPDATE_EEPROM;
         }
      }


/*
   for (i=0,k=0; i < MAX_SCSI_TAR; i++)
      {
      if ((FPT_scamInfo[i].state == ID_ASSIGNED) ||
         (FPT_scamInfo[i].state == LEGACY))
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
 * Function: FPT_scarb
 *
 * Description: Gain control of the bus and wait SCAM select time (250ms)
 *
 *---------------------------------------------------------------------*/

static int FPT_scarb(ULONG p_port, UCHAR p_sel_type)
{
   if (p_sel_type == INIT_SELTD)
      {

      while (RD_HARPOON(p_port+hp_scsisig) & (SCSI_SEL | SCSI_BSY)) {}


      if (RD_HARPOON(p_port+hp_scsisig) & SCSI_SEL)
         return(0);

      if (RD_HARPOON(p_port+hp_scsidata_0) != 00)
         return(0);

      WR_HARPOON(p_port+hp_scsisig, (RD_HARPOON(p_port+hp_scsisig) | SCSI_BSY));

      if (RD_HARPOON(p_port+hp_scsisig) & SCSI_SEL) {

         WR_HARPOON(p_port+hp_scsisig, (RD_HARPOON(p_port+hp_scsisig) &
            ~SCSI_BSY));
         return(0);
         }


      WR_HARPOON(p_port+hp_scsisig, (RD_HARPOON(p_port+hp_scsisig) | SCSI_SEL));

      if (RD_HARPOON(p_port+hp_scsidata_0) != 00) {

         WR_HARPOON(p_port+hp_scsisig, (RD_HARPOON(p_port+hp_scsisig) &
            ~(SCSI_BSY | SCSI_SEL)));
         return(0);
         }
      }


   WR_HARPOON(p_port+hp_clkctrl_0, (RD_HARPOON(p_port+hp_clkctrl_0)
      & ~ACTdeassert));
   WR_HARPOON(p_port+hp_scsireset, SCAM_EN);
   WR_HARPOON(p_port+hp_scsidata_0, 0x00);
   WR_HARPOON(p_port+hp_scsidata_1, 0x00);
   WR_HARPOON(p_port+hp_portctrl_0, SCSI_BUS_EN);

   WR_HARPOON(p_port+hp_scsisig, (RD_HARPOON(p_port+hp_scsisig) | SCSI_MSG));

   WR_HARPOON(p_port+hp_scsisig, (RD_HARPOON(p_port+hp_scsisig)
      & ~SCSI_BSY));

   FPT_Wait(p_port,TO_250ms);

   return(1);
}


/*---------------------------------------------------------------------
 *
 * Function: FPT_scbusf
 *
 * Description: Release the SCSI bus and disable SCAM selection.
 *
 *---------------------------------------------------------------------*/

static void FPT_scbusf(ULONG p_port)
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

   WRW_HARPOON((p_port+hp_intstat), (BUS_FREE | AUTO_INT | SCAM_SEL));

   WR_HARPOON(p_port+hp_page_ctrl,
      (RD_HARPOON(p_port+hp_page_ctrl) & ~G_INT_DISABLE));
}



/*---------------------------------------------------------------------
 *
 * Function: FPT_scasid
 *
 * Description: Assign an ID to all the SCAM devices.
 *
 *---------------------------------------------------------------------*/

static void FPT_scasid(UCHAR p_card, ULONG p_port)
{
   UCHAR temp_id_string[ID_STRING_LENGTH];

   UCHAR i,k,scam_id;
	UCHAR crcBytes[3];
	PNVRamInfo pCurrNvRam;
	ushort_ptr pCrcBytes;

	pCurrNvRam = FPT_BL_Card[p_card].pNvRamInfo;

   i=0;

   while (!i)
      {

      for (k=0; k < ID_STRING_LENGTH; k++)
         {
         temp_id_string[k] = (UCHAR) 0x00;
         }

      FPT_scxferc(p_port,SYNC_PTRN);
      FPT_scxferc(p_port,ASSIGN_ID);

      if (!(FPT_sciso(p_port,&temp_id_string[0])))
         {
			if(pCurrNvRam){
				pCrcBytes = (ushort_ptr)&crcBytes[0];
				*pCrcBytes = FPT_CalcCrc16(&temp_id_string[0]);
				crcBytes[2] = FPT_CalcLrc(&temp_id_string[0]);
				temp_id_string[1] = crcBytes[2];
				temp_id_string[2] = crcBytes[0];
				temp_id_string[3] = crcBytes[1];
				for(k = 4; k < ID_STRING_LENGTH; k++)
					temp_id_string[k] = (UCHAR) 0x00;
			}
         i = FPT_scmachid(p_card,temp_id_string);

         if (i == CLR_PRIORITY)
            {
            FPT_scxferc(p_port,MISC_CODE);
            FPT_scxferc(p_port,CLR_P_FLAG);
            i = 0;  /*Not the last ID yet. */
            }

         else if (i != NO_ID_AVAIL)
            {
            if (i < 8 )
               FPT_scxferc(p_port,ID_0_7);
            else
               FPT_scxferc(p_port,ID_8_F);

            scam_id = (i & (UCHAR) 0x07);


            for (k=1; k < 0x08; k <<= 1)
               if (!( k & i ))
                  scam_id += 0x08;        /*Count number of zeros in DB0-3. */

            FPT_scxferc(p_port,scam_id);

            i = 0;  /*Not the last ID yet. */
            }
         }

      else
         {
         i = 1;
         }

      }  /*End while */

   FPT_scxferc(p_port,SYNC_PTRN);
   FPT_scxferc(p_port,CFG_CMPLT);
}





/*---------------------------------------------------------------------
 *
 * Function: FPT_scsel
 *
 * Description: Select all the SCAM devices.
 *
 *---------------------------------------------------------------------*/

static void FPT_scsel(ULONG p_port)
{

   WR_HARPOON(p_port+hp_scsisig, SCSI_SEL);
   FPT_scwiros(p_port, SCSI_MSG);

   WR_HARPOON(p_port+hp_scsisig, (SCSI_SEL | SCSI_BSY));


   WR_HARPOON(p_port+hp_scsisig, (SCSI_SEL | SCSI_BSY | SCSI_IOBIT | SCSI_CD));
   WR_HARPOON(p_port+hp_scsidata_0, (UCHAR)(RD_HARPOON(p_port+hp_scsidata_0) |
      (UCHAR)(BIT(7)+BIT(6))));


   WR_HARPOON(p_port+hp_scsisig, (SCSI_BSY | SCSI_IOBIT | SCSI_CD));
   FPT_scwiros(p_port, SCSI_SEL);

   WR_HARPOON(p_port+hp_scsidata_0, (UCHAR)(RD_HARPOON(p_port+hp_scsidata_0) &
      ~(UCHAR)BIT(6)));
   FPT_scwirod(p_port, BIT(6));

   WR_HARPOON(p_port+hp_scsisig, (SCSI_SEL | SCSI_BSY | SCSI_IOBIT | SCSI_CD));
}



/*---------------------------------------------------------------------
 *
 * Function: FPT_scxferc
 *
 * Description: Handshake the p_data (DB4-0) across the bus.
 *
 *---------------------------------------------------------------------*/

static UCHAR FPT_scxferc(ULONG p_port, UCHAR p_data)
{
   UCHAR curr_data, ret_data;

   curr_data = p_data | BIT(7) | BIT(5);   /*Start with DB7 & DB5 asserted. */

   WR_HARPOON(p_port+hp_scsidata_0, curr_data);

   curr_data &= ~BIT(7);

   WR_HARPOON(p_port+hp_scsidata_0, curr_data);

   FPT_scwirod(p_port,BIT(7));              /*Wait for DB7 to be released. */
	while (!(RD_HARPOON(p_port+hp_scsidata_0) & BIT(5)));

   ret_data = (RD_HARPOON(p_port+hp_scsidata_0) & (UCHAR) 0x1F);

   curr_data |= BIT(6);

   WR_HARPOON(p_port+hp_scsidata_0, curr_data);

   curr_data &= ~BIT(5);

   WR_HARPOON(p_port+hp_scsidata_0, curr_data);

   FPT_scwirod(p_port,BIT(5));              /*Wait for DB5 to be released. */

   curr_data &= ~(BIT(4)|BIT(3)|BIT(2)|BIT(1)|BIT(0)); /*Release data bits */
   curr_data |= BIT(7);

   WR_HARPOON(p_port+hp_scsidata_0, curr_data);

   curr_data &= ~BIT(6);

   WR_HARPOON(p_port+hp_scsidata_0, curr_data);

   FPT_scwirod(p_port,BIT(6));              /*Wait for DB6 to be released. */

   return(ret_data);
}


/*---------------------------------------------------------------------
 *
 * Function: FPT_scsendi
 *
 * Description: Transfer our Identification string to determine if we
 *              will be the dominant master.
 *
 *---------------------------------------------------------------------*/

static UCHAR FPT_scsendi(ULONG p_port, UCHAR p_id_string[])
{
   UCHAR ret_data,byte_cnt,bit_cnt,defer;

   defer = 0;

   for (byte_cnt = 0; byte_cnt < ID_STRING_LENGTH; byte_cnt++) {

      for (bit_cnt = 0x80; bit_cnt != 0 ; bit_cnt >>= 1) {

         if (defer)
            ret_data = FPT_scxferc(p_port,00);

         else if (p_id_string[byte_cnt] & bit_cnt)

               ret_data = FPT_scxferc(p_port,02);

            else {

               ret_data = FPT_scxferc(p_port,01);
               if (ret_data & 02)
                  defer = 1;
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
 * Function: FPT_sciso
 *
 * Description: Transfer the Identification string.
 *
 *---------------------------------------------------------------------*/

static UCHAR FPT_sciso(ULONG p_port, UCHAR p_id_string[])
{
   UCHAR ret_data,the_data,byte_cnt,bit_cnt;

   the_data = 0;

   for (byte_cnt = 0; byte_cnt < ID_STRING_LENGTH; byte_cnt++) {

      for (bit_cnt = 0; bit_cnt < 8; bit_cnt++) {

         ret_data = FPT_scxferc(p_port,0);

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
					FPT_scxferc(p_port, SYNC_PTRN);
					FPT_scxferc(p_port, ASSIGN_ID);
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
 * Function: FPT_scwirod
 *
 * Description: Sample the SCSI data bus making sure the signal has been
 *              deasserted for the correct number of consecutive samples.
 *
 *---------------------------------------------------------------------*/

static void FPT_scwirod(ULONG p_port, UCHAR p_data_bit)
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
 * Function: FPT_scwiros
 *
 * Description: Sample the SCSI Signal lines making sure the signal has been
 *              deasserted for the correct number of consecutive samples.
 *
 *---------------------------------------------------------------------*/

static void FPT_scwiros(ULONG p_port, UCHAR p_data_bit)
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
 * Function: FPT_scvalq
 *
 * Description: Make sure we received a valid data byte.
 *
 *---------------------------------------------------------------------*/

static UCHAR FPT_scvalq(UCHAR p_quintet)
{
   UCHAR count;

   for (count=1; count < 0x08; count<<=1) {
      if (!(p_quintet & count))
         p_quintet -= 0x80;
      }

   if (p_quintet & 0x18)
      return(0);

   else
      return(1);
}


/*---------------------------------------------------------------------
 *
 * Function: FPT_scsell
 *
 * Description: Select the specified device ID using a selection timeout
 *              less than 4ms.  If somebody responds then it is a legacy
 *              drive and this ID must be marked as such.
 *
 *---------------------------------------------------------------------*/

static UCHAR FPT_scsell(ULONG p_port, UCHAR targ_id)
{
   ULONG i;

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
         FPT_Wait(p_port, TO_250ms);

   DISABLE_AUTO(p_port);

   WR_HARPOON(p_port+hp_addstat,(RD_HARPOON(p_port+hp_addstat) & ~SCAM_TIMER));
   WR_HARPOON(p_port+hp_seltimeout,TO_290ms);

   SGRAM_ACCESS(p_port);

   if (RDW_HARPOON((p_port+hp_intstat)) & (RESET | TIMEOUT) ) {

      WRW_HARPOON((p_port+hp_intstat),
		  (RESET | TIMEOUT | SEL | BUS_FREE | PHASE));

      WR_HARPOON(p_port+hp_page_ctrl,
         (RD_HARPOON(p_port+hp_page_ctrl) & ~G_INT_DISABLE));

      return(0);  /*No legacy device */
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

      return(1);  /*Found one of them oldies! */
      }
}

/*---------------------------------------------------------------------
 *
 * Function: FPT_scwtsel
 *
 * Description: Wait to be selected by another SCAM initiator.
 *
 *---------------------------------------------------------------------*/

static void FPT_scwtsel(ULONG p_port)
{
   while(!(RDW_HARPOON((p_port+hp_intstat)) & SCAM_SEL)) {}
}


/*---------------------------------------------------------------------
 *
 * Function: FPT_inisci
 *
 * Description: Setup the data Structure with the info from the EEPROM.
 *
 *---------------------------------------------------------------------*/

static void FPT_inisci(UCHAR p_card, ULONG p_port, UCHAR p_our_id)
{
   UCHAR i,k,max_id;
   USHORT ee_data;
	PNVRamInfo pCurrNvRam;

	pCurrNvRam = FPT_BL_Card[p_card].pNvRamInfo;

   if (RD_HARPOON(p_port+hp_page_ctrl) & NARROW_SCSI_CARD)
      max_id = 0x08;

   else
      max_id = 0x10;

	if(pCurrNvRam){
		for(i = 0; i < max_id; i++){

			for(k = 0; k < 4; k++)
				FPT_scamInfo[i].id_string[k] = pCurrNvRam->niScamTbl[i][k];
			for(k = 4; k < ID_STRING_LENGTH; k++)
				FPT_scamInfo[i].id_string[k] = (UCHAR) 0x00;

	      if(FPT_scamInfo[i].id_string[0] == 0x00)
      	   FPT_scamInfo[i].state = ID_UNUSED;  /*Default to unused ID. */
	      else
   	      FPT_scamInfo[i].state = ID_UNASSIGNED;  /*Default to unassigned ID. */

		}
	}else {
	   for (i=0; i < max_id; i++)
   	   {
      	for (k=0; k < ID_STRING_LENGTH; k+=2)
	         {
   	      ee_data = FPT_utilEERead(p_port, (USHORT)((EE_SCAMBASE/2) +
      	     (USHORT) (i*((USHORT)ID_STRING_LENGTH/2)) + (USHORT)(k/2)));
         	FPT_scamInfo[i].id_string[k] = (UCHAR) ee_data;
	         ee_data >>= 8;
   	      FPT_scamInfo[i].id_string[k+1] = (UCHAR) ee_data;
      	   }

	      if ((FPT_scamInfo[i].id_string[0] == 0x00) ||
   	       (FPT_scamInfo[i].id_string[0] == 0xFF))

      	   FPT_scamInfo[i].state = ID_UNUSED;  /*Default to unused ID. */

	      else
   	      FPT_scamInfo[i].state = ID_UNASSIGNED;  /*Default to unassigned ID. */

      	}
	}
	for(k = 0; k < ID_STRING_LENGTH; k++)
		FPT_scamInfo[p_our_id].id_string[k] = FPT_scamHAString[k];

}

/*---------------------------------------------------------------------
 *
 * Function: FPT_scmachid
 *
 * Description: Match the Device ID string with our values stored in
 *              the EEPROM.
 *
 *---------------------------------------------------------------------*/

static UCHAR FPT_scmachid(UCHAR p_card, UCHAR p_id_string[])
{

   UCHAR i,k,match;


   for (i=0; i < MAX_SCSI_TAR; i++) {

         match = 1;

         for (k=0; k < ID_STRING_LENGTH; k++)
            {
            if (p_id_string[k] != FPT_scamInfo[i].id_string[k])
               match = 0;
            }

         if (match)
            {
            FPT_scamInfo[i].state = ID_ASSIGNED;
            return(i);
            }

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

      if (FPT_scamInfo[match].state == ID_UNUSED)
         {
         for (k=0; k < ID_STRING_LENGTH; k++)
            {
            FPT_scamInfo[match].id_string[k] = p_id_string[k];
            }

         FPT_scamInfo[match].state = ID_ASSIGNED;

			if(FPT_BL_Card[p_card].pNvRamInfo == NULL)
	         FPT_BL_Card[p_card].globalFlags |= F_UPDATE_EEPROM;
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

      if (FPT_scamInfo[match].state == ID_UNASSIGNED)
         {
         for (k=0; k < ID_STRING_LENGTH; k++)
            {
            FPT_scamInfo[match].id_string[k] = p_id_string[k];
            }

         FPT_scamInfo[match].id_string[0] |= BIT(7);
         FPT_scamInfo[match].state = ID_ASSIGNED;
			if(FPT_BL_Card[p_card].pNvRamInfo == NULL)
	         FPT_BL_Card[p_card].globalFlags |= F_UPDATE_EEPROM;
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
 * Function: FPT_scsavdi
 *
 * Description: Save off the device SCAM ID strings.
 *
 *---------------------------------------------------------------------*/

static void FPT_scsavdi(UCHAR p_card, ULONG p_port)
{
   UCHAR i,k,max_id;
   USHORT ee_data,sum_data;


   sum_data = 0x0000;

   for (i = 1; i < EE_SCAMBASE/2; i++)
      {
      sum_data += FPT_utilEERead(p_port, i);
      }


   FPT_utilEEWriteOnOff(p_port,1);   /* Enable write access to the EEPROM */

   if (RD_HARPOON(p_port+hp_page_ctrl) & NARROW_SCSI_CARD)
      max_id = 0x08;

   else
      max_id = 0x10;

   for (i=0; i < max_id; i++)
      {

      for (k=0; k < ID_STRING_LENGTH; k+=2)
         {
         ee_data = FPT_scamInfo[i].id_string[k+1];
         ee_data <<= 8;
         ee_data |= FPT_scamInfo[i].id_string[k];
         sum_data += ee_data;
         FPT_utilEEWrite(p_port, ee_data, (USHORT)((EE_SCAMBASE/2) +
            (USHORT)(i*((USHORT)ID_STRING_LENGTH/2)) + (USHORT)(k/2)));
         }
      }


   FPT_utilEEWrite(p_port, sum_data, EEPROM_CHECK_SUM/2);
   FPT_utilEEWriteOnOff(p_port,0);   /* Turn off write access */
}

/*---------------------------------------------------------------------
 *
 * Function: FPT_XbowInit
 *
 * Description: Setup the Xbow for normal operation.
 *
 *---------------------------------------------------------------------*/

static void FPT_XbowInit(ULONG port, UCHAR ScamFlg)
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

   FPT_default_intena = RESET | RSEL | PROG_HLT | TIMEOUT |
		    BUS_FREE | XFER_CNT_0 | AUTO_INT;

   if ((ScamFlg & SCAM_ENABLED) && (ScamFlg & SCAM_LEVEL2))
		FPT_default_intena |= SCAM_SEL;

   WRW_HARPOON((port+hp_intena), FPT_default_intena);

   WR_HARPOON(port+hp_seltimeout,TO_290ms);

   /* Turn on SCSI_MODE8 for narrow cards to fix the
      strapping issue with the DUAL CHANNEL card */
   if (RD_HARPOON(port+hp_page_ctrl) & NARROW_SCSI_CARD)
      WR_HARPOON(port+hp_addstat,SCSI_MODE8);

	WR_HARPOON(port+hp_page_ctrl, i);

}


/*---------------------------------------------------------------------
 *
 * Function: FPT_BusMasterInit
 *
 * Description: Initialize the BusMaster for normal operations.
 *
 *---------------------------------------------------------------------*/

static void FPT_BusMasterInit(ULONG p_port)
{


   WR_HARPOON(p_port+hp_sys_ctrl, DRVR_RST);
   WR_HARPOON(p_port+hp_sys_ctrl, 0x00);

   WR_HARPOON(p_port+hp_host_blk_cnt, XFER_BLK64);


   WR_HARPOON(p_port+hp_bm_ctrl, (BMCTRL_DEFAULT));

   WR_HARPOON(p_port+hp_ee_ctrl, (SCSI_TERM_ENA_H));


   RD_HARPOON(p_port+hp_int_status);        /*Clear interrupts. */
   WR_HARPOON(p_port+hp_int_mask, (INT_CMD_COMPL | SCSI_INTERRUPT));
   WR_HARPOON(p_port+hp_page_ctrl, (RD_HARPOON(p_port+hp_page_ctrl) &
      ~SCATTER_EN));
}


/*---------------------------------------------------------------------
 *
 * Function: FPT_DiagEEPROM
 *
 * Description: Verfiy checksum and 'Key' and initialize the EEPROM if
 *              necessary.
 *
 *---------------------------------------------------------------------*/

static void FPT_DiagEEPROM(ULONG p_port)
{
   USHORT index,temp,max_wd_cnt;

   if (RD_HARPOON(p_port+hp_page_ctrl) & NARROW_SCSI_CARD)
      max_wd_cnt = EEPROM_WD_CNT;
   else
      max_wd_cnt = EEPROM_WD_CNT * 2;

   temp = FPT_utilEERead(p_port, FW_SIGNATURE/2);

   if (temp == 0x4641) {

      for (index = 2; index < max_wd_cnt; index++) {

         temp += FPT_utilEERead(p_port, index);

         }

      if (temp == FPT_utilEERead(p_port, EEPROM_CHECK_SUM/2)) {

         return;          /*EEPROM is Okay so return now! */
         }
      }


   FPT_utilEEWriteOnOff(p_port,(UCHAR)1);

   for (index = 0; index < max_wd_cnt; index++) {

      FPT_utilEEWrite(p_port, 0x0000, index);
      }

   temp = 0;

   FPT_utilEEWrite(p_port, 0x4641, FW_SIGNATURE/2);
   temp += 0x4641;
   FPT_utilEEWrite(p_port, 0x3920, MODEL_NUMB_0/2);
   temp += 0x3920;
   FPT_utilEEWrite(p_port, 0x3033, MODEL_NUMB_2/2);
   temp += 0x3033;
   FPT_utilEEWrite(p_port, 0x2020, MODEL_NUMB_4/2);
   temp += 0x2020;
   FPT_utilEEWrite(p_port, 0x70D3, SYSTEM_CONFIG/2);
   temp += 0x70D3;
   FPT_utilEEWrite(p_port, 0x0010, BIOS_CONFIG/2);
   temp += 0x0010;
   FPT_utilEEWrite(p_port, 0x0003, SCAM_CONFIG/2);
   temp += 0x0003;
   FPT_utilEEWrite(p_port, 0x0007, ADAPTER_SCSI_ID/2);
   temp += 0x0007;

   FPT_utilEEWrite(p_port, 0x0000, IGNORE_B_SCAN/2);
   temp += 0x0000;
   FPT_utilEEWrite(p_port, 0x0000, SEND_START_ENA/2);
   temp += 0x0000;
   FPT_utilEEWrite(p_port, 0x0000, DEVICE_ENABLE/2);
   temp += 0x0000;

   FPT_utilEEWrite(p_port, 0x4242, SYNC_RATE_TBL01/2);
   temp += 0x4242;
   FPT_utilEEWrite(p_port, 0x4242, SYNC_RATE_TBL23/2);
   temp += 0x4242;
   FPT_utilEEWrite(p_port, 0x4242, SYNC_RATE_TBL45/2);
   temp += 0x4242;
   FPT_utilEEWrite(p_port, 0x4242, SYNC_RATE_TBL67/2);
   temp += 0x4242;
   FPT_utilEEWrite(p_port, 0x4242, SYNC_RATE_TBL89/2);
   temp += 0x4242;
   FPT_utilEEWrite(p_port, 0x4242, SYNC_RATE_TBLab/2);
   temp += 0x4242;
   FPT_utilEEWrite(p_port, 0x4242, SYNC_RATE_TBLcd/2);
   temp += 0x4242;
   FPT_utilEEWrite(p_port, 0x4242, SYNC_RATE_TBLef/2);
   temp += 0x4242;


   FPT_utilEEWrite(p_port, 0x6C46, 64/2);  /*PRODUCT ID */
   temp += 0x6C46;
   FPT_utilEEWrite(p_port, 0x7361, 66/2);  /* FlashPoint LT   */
   temp += 0x7361;
   FPT_utilEEWrite(p_port, 0x5068, 68/2);
   temp += 0x5068;
   FPT_utilEEWrite(p_port, 0x696F, 70/2);
   temp += 0x696F;
   FPT_utilEEWrite(p_port, 0x746E, 72/2);
   temp += 0x746E;
   FPT_utilEEWrite(p_port, 0x4C20, 74/2);
   temp += 0x4C20;
   FPT_utilEEWrite(p_port, 0x2054, 76/2);
   temp += 0x2054;
   FPT_utilEEWrite(p_port, 0x2020, 78/2);
   temp += 0x2020;

   index = ((EE_SCAMBASE/2)+(7*16));
   FPT_utilEEWrite(p_port, (0x0700+TYPE_CODE0), index);
   temp += (0x0700+TYPE_CODE0);
   index++;
   FPT_utilEEWrite(p_port, 0x5542, index);            /*Vendor ID code */
   temp += 0x5542;                                /* BUSLOGIC      */
   index++;
   FPT_utilEEWrite(p_port, 0x4C53, index);
   temp += 0x4C53;
   index++;
   FPT_utilEEWrite(p_port, 0x474F, index);
   temp += 0x474F;
   index++;
   FPT_utilEEWrite(p_port, 0x4349, index);
   temp += 0x4349;
   index++;
   FPT_utilEEWrite(p_port, 0x5442, index);            /*Vendor unique code */
   temp += 0x5442;                         /* BT- 930           */
   index++;
   FPT_utilEEWrite(p_port, 0x202D, index);
   temp += 0x202D;
   index++;
   FPT_utilEEWrite(p_port, 0x3339, index);
   temp += 0x3339;
   index++;                                 /*Serial #          */
   FPT_utilEEWrite(p_port, 0x2030, index);             /* 01234567         */
   temp += 0x2030;
   index++;
   FPT_utilEEWrite(p_port, 0x5453, index);
   temp += 0x5453;
   index++;
   FPT_utilEEWrite(p_port, 0x5645, index);
   temp += 0x5645;
   index++;
   FPT_utilEEWrite(p_port, 0x2045, index);
   temp += 0x2045;
   index++;
   FPT_utilEEWrite(p_port, 0x202F, index);
   temp += 0x202F;
   index++;
   FPT_utilEEWrite(p_port, 0x4F4A, index);
   temp += 0x4F4A;
   index++;
   FPT_utilEEWrite(p_port, 0x204E, index);
   temp += 0x204E;
   index++;
   FPT_utilEEWrite(p_port, 0x3539, index);
   temp += 0x3539;



   FPT_utilEEWrite(p_port, temp, EEPROM_CHECK_SUM/2);

   FPT_utilEEWriteOnOff(p_port,(UCHAR)0);

}


/*---------------------------------------------------------------------
 *
 * Function: Queue Search Select
 *
 * Description: Try to find a new command to execute.
 *
 *---------------------------------------------------------------------*/

static void FPT_queueSearchSelect(PSCCBcard pCurrCard, UCHAR p_card)
{
   UCHAR scan_ptr, lun;
   PSCCBMgr_tar_info currTar_Info;
	PSCCB pOldSccb;

   scan_ptr = pCurrCard->scanIndex;
	do 
	{
		currTar_Info = &FPT_sccbMgrTbl[p_card][scan_ptr];
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
					if(currTar_Info->TarLUNBusy[lun] == 0)
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
				(currTar_Info->TarLUNBusy[0] == 0))
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

static void FPT_queueSelectFail(PSCCBcard pCurrCard, UCHAR p_card)
{
   UCHAR thisTarg;
   PSCCBMgr_tar_info currTar_Info;

   if (pCurrCard->currentSCCB != NULL)
	  {
	  thisTarg = (UCHAR)(((PSCCB)(pCurrCard->currentSCCB))->TargID);
      currTar_Info = &FPT_sccbMgrTbl[p_card][thisTarg];

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

static void FPT_queueCmdComplete(PSCCBcard pCurrCard, PSCCB p_sccb,
				 UCHAR p_card)
{

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

		 FPT_utilUpdateResidual(p_sccb);
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
      currTar_Info = &FPT_sccbMgrTbl[p_card][p_sccb->TargID];
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


/*---------------------------------------------------------------------
 *
 * Function: Queue Disconnect
 *
 * Description: Add SCCB to our disconnect array.
 *
 *---------------------------------------------------------------------*/
static void FPT_queueDisconnect(PSCCB p_sccb, UCHAR p_card)
{
   PSCCBMgr_tar_info currTar_Info;

	currTar_Info = &FPT_sccbMgrTbl[p_card][p_sccb->TargID];

	if(((FPT_BL_Card[p_card].globalFlags & F_CONLUN_IO) &&
		((currTar_Info->TarStatus & TAR_TAG_Q_MASK) != TAG_Q_TRYING)))
	{
		FPT_BL_Card[p_card].discQ_Tbl[currTar_Info->LunDiscQ_Idx[p_sccb->Lun]] = p_sccb;
	}
	else
	{
		if (p_sccb->Sccb_tag)
		{
			FPT_BL_Card[p_card].discQ_Tbl[p_sccb->Sccb_tag] = p_sccb;
			FPT_sccbMgrTbl[p_card][p_sccb->TargID].TarLUNBusy[0] = 0;
			FPT_sccbMgrTbl[p_card][p_sccb->TargID].TarTagQ_Cnt++;
		}else
		{
			FPT_BL_Card[p_card].discQ_Tbl[currTar_Info->LunDiscQ_Idx[0]] = p_sccb;
		}
	}
	FPT_BL_Card[p_card].currentSCCB = NULL;
}


/*---------------------------------------------------------------------
 *
 * Function: Queue Flush SCCB
 *
 * Description: Flush all SCCB's back to the host driver for this target.
 *
 *---------------------------------------------------------------------*/

static void  FPT_queueFlushSccb(UCHAR p_card, UCHAR error_code)
{
   UCHAR qtag,thisTarg;
   PSCCB currSCCB;
   PSCCBMgr_tar_info currTar_Info;

   currSCCB = FPT_BL_Card[p_card].currentSCCB;
	if(currSCCB != NULL)
	{
	   thisTarg = (UCHAR)currSCCB->TargID;
   	currTar_Info = &FPT_sccbMgrTbl[p_card][thisTarg];

	   for (qtag=0; qtag<QUEUE_DEPTH; qtag++) {

		  if (FPT_BL_Card[p_card].discQ_Tbl[qtag] && 
					(FPT_BL_Card[p_card].discQ_Tbl[qtag]->TargID == thisTarg))
			 {

			 FPT_BL_Card[p_card].discQ_Tbl[qtag]->HostStatus = (UCHAR)error_code;
			
			 FPT_queueCmdComplete(&FPT_BL_Card[p_card],FPT_BL_Card[p_card].discQ_Tbl[qtag], p_card);

			 FPT_BL_Card[p_card].discQ_Tbl[qtag] = NULL;
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

static void  FPT_queueFlushTargSccb(UCHAR p_card, UCHAR thisTarg,
				    UCHAR error_code)
{
   UCHAR qtag;
   PSCCBMgr_tar_info currTar_Info;

   currTar_Info = &FPT_sccbMgrTbl[p_card][thisTarg];

   for (qtag=0; qtag<QUEUE_DEPTH; qtag++) {

	  if (FPT_BL_Card[p_card].discQ_Tbl[qtag] && 
				(FPT_BL_Card[p_card].discQ_Tbl[qtag]->TargID == thisTarg))
		 {

		 FPT_BL_Card[p_card].discQ_Tbl[qtag]->HostStatus = (UCHAR)error_code;

		 FPT_queueCmdComplete(&FPT_BL_Card[p_card],FPT_BL_Card[p_card].discQ_Tbl[qtag], p_card);

		 FPT_BL_Card[p_card].discQ_Tbl[qtag] = NULL;
		 currTar_Info->TarTagQ_Cnt--;

		 }
	  }

}





static void FPT_queueAddSccb(PSCCB p_SCCB, UCHAR p_card)
{
   PSCCBMgr_tar_info currTar_Info;
   currTar_Info = &FPT_sccbMgrTbl[p_card][p_SCCB->TargID];

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

static UCHAR FPT_queueFindSccb(PSCCB p_SCCB, UCHAR p_card)
{
   PSCCB q_ptr;
   PSCCBMgr_tar_info currTar_Info;

   currTar_Info = &FPT_sccbMgrTbl[p_card][p_SCCB->TargID];

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

		 return(1);
		 }

	  else {
		 q_ptr = q_ptr->Sccb_forwardlink;
		 }
	  }


   return(0);

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

static void  FPT_utilUpdateResidual(PSCCB p_SCCB)
{
   ULONG partial_cnt;
   UINT  sg_index;
   ULONG *sg_ptr;

   if (p_SCCB->Sccb_XferState & F_ALL_XFERRED) {

	  p_SCCB->DataLength = 0x0000;
	  }

   else if (p_SCCB->Sccb_XferState & F_SG_XFER) {

		 partial_cnt = 0x0000;

		 sg_index = p_SCCB->Sccb_sgseg;

		 sg_ptr = (ULONG *)p_SCCB->DataPointer;

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

static void FPT_Wait1Second(ULONG p_port)
{
   UCHAR i;

   for(i=0; i < 4; i++) {

	  FPT_Wait(p_port, TO_250ms);

	  if ((RD_HARPOON(p_port+hp_scsictrl_0) & SCSI_RST))
		 break;

	  if((RDW_HARPOON((p_port+hp_intstat)) & SCAM_SEL))
		 break;
	  }
}


/*---------------------------------------------------------------------
 *
 * Function: FPT_Wait
 *
 * Description: Wait the desired delay.
 *
 *---------------------------------------------------------------------*/

static void FPT_Wait(ULONG p_port, UCHAR p_delay)
{
   UCHAR old_timer;
   UCHAR green_flag;

   old_timer = RD_HARPOON(p_port+hp_seltimeout);

   green_flag=RD_HARPOON(p_port+hp_clkctrl_0);
   WR_HARPOON(p_port+hp_clkctrl_0, CLKCTRL_DEFAULT);

   WR_HARPOON(p_port+hp_seltimeout,p_delay);
   WRW_HARPOON((p_port+hp_intstat), TIMEOUT);
   WRW_HARPOON((p_port+hp_intena), (FPT_default_intena & ~TIMEOUT));


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
   WRW_HARPOON((p_port+hp_intena), FPT_default_intena);

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

static void FPT_utilEEWriteOnOff(ULONG p_port,UCHAR p_mode)
{
   UCHAR ee_value;

   ee_value = (UCHAR)(RD_HARPOON(p_port+hp_ee_ctrl) & (EXT_ARB_ACK | SCSI_TERM_ENA_H));

   if (p_mode)

	  FPT_utilEESendCmdAddr(p_port, EWEN, EWEN_ADDR);

   else


	  FPT_utilEESendCmdAddr(p_port, EWDS, EWDS_ADDR);

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

static void FPT_utilEEWrite(ULONG p_port, USHORT ee_data, USHORT ee_addr)
{

   UCHAR ee_value;
   USHORT i;

   ee_value = (UCHAR)((RD_HARPOON(p_port+hp_ee_ctrl) & (EXT_ARB_ACK | SCSI_TERM_ENA_H))|
		   (SEE_MS | SEE_CS));



   FPT_utilEESendCmdAddr(p_port, EE_WRITE, ee_addr);


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

   FPT_Wait(p_port, TO_10ms);

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

static USHORT FPT_utilEERead(ULONG p_port, USHORT ee_addr)
{
   USHORT i, ee_data1, ee_data2;

	i = 0;
	ee_data1 = FPT_utilEEReadOrg(p_port, ee_addr);
	do
	{
		ee_data2 = FPT_utilEEReadOrg(p_port, ee_addr);

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

static USHORT FPT_utilEEReadOrg(ULONG p_port, USHORT ee_addr)
{

   UCHAR ee_value;
   USHORT i, ee_data;

   ee_value = (UCHAR)((RD_HARPOON(p_port+hp_ee_ctrl) & (EXT_ARB_ACK | SCSI_TERM_ENA_H))|
		   (SEE_MS | SEE_CS));


   FPT_utilEESendCmdAddr(p_port, EE_READ, ee_addr);


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

static void FPT_utilEESendCmdAddr(ULONG p_port, UCHAR ee_cmd, USHORT ee_addr)
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

static USHORT FPT_CalcCrc16(UCHAR buffer[])
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

static UCHAR FPT_CalcLrc(UCHAR buffer[])
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


#endif /* CONFIG_SCSI_OMIT_FLASHPOINT */
