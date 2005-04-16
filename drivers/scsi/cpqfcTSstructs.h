/* Copyright(c) 2000, Compaq Computer Corporation
 * Fibre Channel Host Bus Adapter 64-bit, 66MHz PCI 
 * Originally developed and tested on:
 * (front): [chip] Tachyon TS HPFC-5166A/1.2  L2C1090 ...
 *          SP# P225CXCBFIEL6T, Rev XC
 *          SP# 161290-001, Rev XD
 * (back): Board No. 010008-001 A/W Rev X5, FAB REV X5
 *  
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 * Written by Don Zimmerman
*/
#ifndef CPQFCTSSTRUCTS_H
#define CPQFCTSSTRUCTS_H

#include <linux/timer.h>  // timer declaration in our host data
#include <linux/interrupt.h>
#include <asm/atomic.h>
#include "cpqfcTSioctl.h"

#define DbgDelay(secs) { int wait_time; printk( " DbgDelay %ds ", secs); \
                         for( wait_time=jiffies + (secs*HZ); \
		         time_before(jiffies, wait_time) ;) ; }

#define CPQFCTS_DRIVER_VER(maj,min,submin) ((maj<<16)|(min<<8)|(submin))
// don't forget to also change MODULE_DESCRIPTION in cpqfcTSinit.c
#define VER_MAJOR 2
#define VER_MINOR 5
#define VER_SUBMINOR 4

// Macros for kernel (esp. SMP) tracing using a PCI analyzer
// (e.g. x86).
//#define PCI_KERNEL_TRACE
#ifdef PCI_KERNEL_TRACE
#define PCI_TRACE(x) inl( fcChip->Registers.IOBaseL +x);
#define PCI_TRACEO(x,y) outl( x, (fcChip->Registers.IOBaseL +y));
#else

#define PCI_TRACE(x) 
#define PCI_TRACEO(x,y)
#endif

			 
//#define DEBUG_CMND 1   // debug output for Linux Scsi CDBs
//#define DUMMYCMND_DBG 1

//#define DEBUG_CPQFCTS 1
//#undef DEBUG_CPQFCTS 
#ifdef DEBUG_CPQFCTS
#define ENTER(x)	printk("cpqfcts : entering %s()\n", x);
#define LEAVE(x)	printk("cpqfcts : leaving %s()\n", x);
#define DEBUG(x)	x
#else
#define ENTER(x)
#define LEAVE(x)
#define DEBUG(x)
#endif				/* DEBUG_CPQFCTS */

//#define DEBUG_CPQFCTS_PCI 1
//#undef DEBUG_CPQFCTS_PCI
#if DEBUG_CPQFCTS_PCI
#define DEBUG_PCI(x)	x
#else
#define DEBUG_PCI(x)
#endif				/* DEBUG_CPQFCTS_PCI */

#define STACHLITE66_TS12  "Compaq FibreChannel HBA Tachyon TS HPFC-5166A/1.2"
#define STACHLITE66_TS13  "Compaq FibreChannel HBA Tachyon TS HPFC-5166A/1.3"
#define STACHLITE_UNKNOWN "Compaq FibreChannel HBA Tachyon Chip/Board Ver??"
#define SAGILENT_XL2_21   "Agilent FC HBA, Tachyon XL2 HPFC-5200B/2.1"

// PDA is Peripheral Device Address, VSA is Volume Set Addressing
// Linux SCSI parameters
#define CPQFCTS_MAX_TARGET_ID 64

// Note, changing CPQFCTS_MAX_LUN to less than 32 (e.g, 8) will result in
// strange behavior if a box with more than, e.g. 8, is on the loop.
#define CPQFCTS_MAX_LUN 32    // The RA-4x00 supports 32 (Linux SCSI supports 8)
#define CPQFCTS_MAX_CHANNEL 0 // One FC port on cpqfcTS HBA

#define CPQFCTS_CMD_PER_LUN 15 // power of 2 -1, must be >0 
#define CPQFCTS_REQ_QUEUE_LEN (TACH_SEST_LEN/2) // must be < TACH_SEST_LEN

#define LinuxVersionCode(v, p, s) (((v)<<16)+((p)<<8)+(s))
#ifndef DECLARE_MUTEX_LOCKED
#define DECLARE_MUTEX_LOCKED(sem) struct semaphore sem = MUTEX_LOCKED
#endif

#define DEV_NAME "cpqfcTS"

struct SupportedPCIcards
{
  __u16 vendor_id;
  __u16 device_id;
};
			 
// nn:nn denotes bit field
                            // TachyonHeader struct def.
                            // the fields shared with ODB
                            // need to have same value




#ifndef BYTE
//typedef UCHAR BYTE;
typedef __u8 BYTE;
#endif
#ifndef UCHAR
typedef __u8 UCHAR;
#endif
#ifndef LONG
typedef __s32 LONG;
#endif
#ifndef ULONG
typedef __u32 ULONG;
#endif
#ifndef PVOID
typedef void * PVOID;
#endif
#ifndef USHORT
typedef __u16 USHORT;
#endif
#ifndef BOOLEAN
typedef __u8 BOOLEAN;
#endif


// macro for FC-PH reject codes
// payload format for LS_RJT (FC payloads are big endian):
//     byte  0         1         2         3  (MSB)
// DWORD 0   01        00        00        00
// DWORD 1   resvd     code      expl.     vendor

#define LS_RJT_REASON( code, expl) (( code<<8) | (expl <<16))


#define TachLiteSTATUS 0x12

// Fibre Channel EXCHANGE status codes for Tachyon chips/ driver software
// 32-bit ERROR word defines
#define INVALID_ARGS 0x1
#define LNKDWN_OSLS  0x2
#define LNKDWN_LASER 0x4
#define OUTQUE_FULL  0x8
#define DRIVERQ_FULL 0x10
#define SEST_FULL    0x20
#define BAD_ALPA     0x40
#define OVERFLOW     0x80  // inbound CM
#define COUNT_ERROR     0x100  // inbound CM
#define LINKFAIL_RX     0x200  // inbound CM
#define ABORTSEQ_NOTIFY 0x400  // outbound CM
#define LINKFAIL_TX     0x800  // outbound CM
#define HOSTPROG_ERR     0x1000  // outbound CM
#define FRAME_TO         0x2000  // outbound CM
#define INV_ENTRY        0x4000  // outbound CM
#define SESTPROG_ERR     0x8000  // outbound CM
#define OUTBOUND_TIMEOUT 0x10000L // timeout waiting for Tachyon outbound CM
#define INITIATOR_ABORT  0x20000L // initiator exchange timeout or O/S ABORT
#define MEMPOOL_FAIL     0x40000L // O/S memory pool allocation failed
#define FC2_TIMEOUT      0x80000L // driver timeout for lost frames
#define TARGET_ABORT     0x100000L // ABTS received from FC port
#define EXCHANGE_QUEUED  0x200000L // e.g. Link State was LDn on fcStart
#define PORTID_CHANGED   0x400000L // fc Port address changed
#define DEVICE_REMOVED   0x800000L // fc Port address changed
// Several error scenarios result in SEST Exchange frames 
// unexpectedly arriving in the SFQ
#define SFQ_FRAME        0x1000000L // SFQ frames from open Exchange

// Maximum number of Host Bus Adapters (HBA) / controllers supported
// only important for mem allocation dimensions - increase as necessary

#define MAX_ADAPTERS 8
#define MAX_RX_PAYLOAD 1024  // hardware dependent max frame payload
// Tach header struc defines
#define SOFi3 0x7
#define SOFf  0x8
#define SOFn3 0xB
#define EOFn  0x5
#define EOFt  0x6

// FCP R_CTL defines
#define FCP_CMND 0x6
#define FCP_XFER_RDY 0x5
#define FCP_RSP 0x7
#define FCP_RESPONSE 0x777 // (arbitrary #)
#define NEED_FCP_RSP 0x77  // (arbitrary #)
#define FCP_DATA 0x1

#define RESET_TACH 0x100 // Reset Tachyon/TachLite
#define SCSI_IWE 0x2000  // initiator write entry (for SEST)
#define SCSI_IRE 0x3000  // initiator read entry (for SEST)
#define SCSI_TRE 0x400  // target read entry (for SEST)
#define SCSI_TWE 0x500  // target write entry (for SEST)
#define TOGGLE_LASER 0x800
#define LIP 0x900
#define CLEAR_FCPORTS 99 // (arbitrary #) free mem for Logged in ports
#define FMINIT 0x707     // (arbitrary) for Frame Manager Init command

// BLS == Basic Link Service
// ELS == Extended Link Service
#define BLS_NOP 4
#define BLS_ABTS 0x10   // FC-PH Basic Link Service Abort Sequence
#define BLS_ABTS_ACC 0x100  // FC-PH Basic Link Service Abort Sequence Accept
#define BLS_ABTS_RJT 0x101  // FC-PH Basic Link Service Abort Sequence Reject
#define ELS_PLOGI 0x03  // FC-PH Port Login (arbitrary assign)
#define ELS_SCR   0x70  // (arb assign) State Change Registration (Fabric)
#define FCS_NSR   0x72  // (arb assign) Name Service Request (Fabric)
#define ELS_FLOGI 0x44  // (arb assign) Fabric Login
#define ELS_FDISC 0x41  // (arb assign) Fabric Discovery (Login)
#define ELS_PDISC 0x50  // FC-PH2 Port Discovery
#define ELS_ABTX  0x06  // FC-PH Abort Exchange 
#define ELS_LOGO 0x05   // FC-PH Port Logout
#define ELS_PRLI 0x20   // FCP-SCSI Process Login
#define ELS_PRLO 0x21   // FCP-SCSI Process Logout
#define ELS_LOGO_ACC 0x07   // {FC-PH} Port Logout Accept
#define ELS_PLOGI_ACC 0x08  // {FC-PH} Port Login Accept
#define ELS_ACC 0x18        // {FC-PH} (generic) ACCept
#define ELS_PRLI_ACC 0x22  // {FCP-SCSI} Process Login Accept
#define ELS_RJT 0x1000000
#define SCSI_REPORT_LUNS 0x0A0
#define FCP_TARGET_RESET 0x200

#define ELS_LILP_FRAME 0x00000711 // 1st payload word of LILP frame

#define SFQ_UNASSISTED_FCP  1  // ICM, DWord3, "Type" unassisted FCP
#define SFQ_UNKNOWN         0x31 // (arbitrary) ICM, DWord3, "Type" unknown

// these "LINK" bits refer to loop or non-loop
#define LINKACTIVE 0x2    // fcLinkQ type - LINK UP Tachyon FM 'Lup' bit set
#define LINKDOWN 0xf2     // fcLinkQ type - LINK DOWN Tachyon FM 'Ldn' bit set

//#define VOLUME_SET_ADDRESSING 1 // "channel" or "bus" 1

typedef struct      // 32 bytes hdr ONLY (e.g. FCP_DATA buffer for SEST)
{
  ULONG reserved;   // dword 0 (don't use)
  ULONG sof_eof;
  ULONG d_id;       // dword 2 - 31:24 R_CTL, 23:0 D_ID
  ULONG s_id;       // dword 3 - 31:24 CS_CTL, 23:0 S_ID
  ULONG f_ctl;      // dword 4 - 31:24 Type,  23:0 F_CTL
  ULONG seq_cnt;    // dword 5 - 31:24 SEQ_ID, 23:16 DF_CTL, 15:0 SEQ_CNT
  ULONG ox_rx_id;   // dword 6 - 31:16 OX_ID,  15:0 RX_ID
  ULONG ro;         // dword 7 - relative offset
} TachFCHDR;

                    // NOTE!! the following struct MUST be 64 bytes.
typedef struct      // 32 bytes hdr + 32 bytes payload
{
  ULONG reserved;   // dword 0 (don't use - must clear to 0)
  ULONG sof_eof;    // dword 1 - 31:24 SOF:EOF, UAM,CLS, LCr, TFV, TimeStamp
  ULONG d_id;       // dword 2 - 31:24 R_CTL, 23:0 D_ID
  ULONG s_id;       // dword 3 - 31:24 CS_CTL, 23:0 S_ID
  ULONG f_ctl;      // dword 4 - 31:24 Type,  23:0 F_CTL
  ULONG seq_cnt;    // dword 5 - 31:24 SEQ_ID, 23:16 DF_CTL, 15:0 SEQ_CNT
  ULONG ox_rx_id;   // dword 6 - 31:16 OX_ID,  15:0 RX_ID
  ULONG ro;  // dword 7 - relative offset
//---------
  __u32 pl[8];              // dwords 8-15 frame data payload
} TachFCHDR_CMND;


typedef struct      // 32 bytes hdr + 120 bytes payload
{
  ULONG reserved;   // dword 0 (don't use - must clear to 0)
  ULONG sof_eof;    // dword 1 - 31:24 SOF:EOF, UAM,CLS, LCr, TFV, TimeStamp
  ULONG d_id;       // dword 2 - 31:24 R_CTL, 23:0 D_ID
  ULONG s_id;       // dword 3 - 31:24 CS_CTL, 23:0 S_ID
  ULONG f_ctl;      // dword 4 - 31:24 Type,  23:0 F_CTL
  ULONG seq_cnt;    // dword 5 - 31:24 SEQ_ID, 23:16 DF_CTL, 15:0 SEQ_CNT
  ULONG ox_rx_id;   // dword 6 - 31:16 OX_ID,  15:0 RX_ID
  ULONG ro;  // dword 7 - relative offset
//---------
  __u32 pl[30];              // largest necessary payload (for LOGIN cmnds)
} TachFCHDR_GCMND;

typedef struct      // 32 bytes hdr + 64 bytes payload
{
  ULONG reserved;   // dword 0 (don't use)
  ULONG sof_eof;
  ULONG d_id;       // dword 2 - 31:24 R_CTL, 23:0 D_ID
  ULONG s_id;       // dword 3 - 31:24 CS_CTL, 23:0 S_ID
  ULONG f_ctl;      // dword 4 - 31:24 Type,  23:0 F_CTL
  ULONG seq_cnt;    // dword 5 - 31:24 SEQ_ID, 23:16 DF_CTL, 15:0 SEQ_CNT
  ULONG ox_rx_id;   // dword 6 - 31:16 OX_ID,  15:0 RX_ID
  ULONG ro;  // dword 7 - relative offset
//---------
  __u32 pl[18]; // payload for FCP-RSP (response buffer) RA-4x00 is 72bytes
} TachFCHDR_RSP;






// Inbound Message Queue structures...
typedef struct              // each entry 8 words (32 bytes)
{
  ULONG type;               // IMQ completion message types
  ULONG word[7];            // remainder of structure
                            // interpreted by IMQ type
} TachyonIMQE;


// Queues for TachLite not in original Tachyon
// ERQ       - Exchange Request Queue (for outbound commands)
// SFQ       - Single Frame Queue (for incoming frames)

                            // Define Tachyon Outbound Command Que
                            // (Since many Tachyon registers are Read
                            // only, maintain copies for debugging)
                            // most Tach ques need power-of-2 sizes,
                            // where registers are loaded with po2 -1
#define TACH_SEST_LEN 512   // TachLite SEST

#define ELS_EXCHANGES 64    // e.g. PLOGI, RSCN, ...
// define the total number of outstanding (simultaneous) exchanges
#define TACH_MAX_XID (TACH_SEST_LEN + ELS_EXCHANGES)  // ELS exchanges

#define ERQ_LEN 128         // power of 2, max 4096

// Inbound Message Queue structures...
#define IMQ_LEN 512              // minimum 4 entries [(power of 2) - 1]
typedef struct                   // 8 words - 32 bytes
{
  TachyonIMQE QEntry[IMQ_LEN];
  ULONG producerIndex;   // IMQ Producer Index register
                                 // @32 byte align
  ULONG consumerIndex;   // Consumer Index register (in Tachyon)
  ULONG length;          // Length register
  ULONG base;
} TachyonIMQ;                    // @ 32 * IMQ_LEN align



typedef struct           // inbound completion message
{
  ULONG Type;
  ULONG Index;
  ULONG TransferLength;
} TachyonInbCM;



// arbitrary numeric tags for TL structures
#define TL_FCHS 1  // TachLite Fibre Channel Header Structure
#define TL_IWE 2  // initiator write entry (for SEST)
#define TL_TWE 3  // target write entry (for SEST)
#define TL_IRE 4  // initiator read entry (for SEST)
#define TL_TRE 5  // target read entry (for SEST)
#define TL_IRB 6  // I/O request block

                                // for INCOMING frames
#define SFQ_LEN 32              // minimum 32 entries, max 4096

typedef struct                  // Single Frame Que
{
  TachFCHDR_CMND QEntry[SFQ_LEN]; // must be 64 bytes!!
  ULONG producerIndex;   // IMQ Producer Index register
                                 // @32 byte align
  ULONG consumerIndex;   // Consumer Index register (in Tachyon)
  ULONG length;          // Length register
  ULONG base;
} TachLiteSFQ;


typedef struct                 // I/O Request Block flags
{
  UCHAR  BRD : 1;
  UCHAR      : 1; // reserved
  UCHAR  SFA : 1;
  UCHAR  DNC : 1;
  UCHAR  DIN : 1;
  UCHAR  DCM : 1;
  UCHAR  CTS : 1;
  UCHAR  SBV : 1;  // IRB entry valid - IRB'B' only
} IRBflags;

typedef struct                  // I/O Request Block
{                          // Request 'A'
  ULONG Req_A_SFS_Len;     // total frame len (hdr + payload), min 32
  ULONG Req_A_SFS_Addr;    // 32-bit pointer to FCHS struct (to be sent)
  ULONG Req_A_SFS_D_ID;    // 24-bit FC destination (i.e. 8 bit al_pa)
  ULONG Req_A_Trans_ID;    // X_ID (OX_ID or RX_ID) and/or Index in SEST
                           // Request 'B'
  ULONG Req_B_SFS_Len;     // total frame len (hdr + payload), min 32
  ULONG Req_B_SFS_Addr;    // 32-bit pointer to FCHS struct (to be sent)
  ULONG Req_B_SFS_D_ID;    // 24-bit FC destination (i.e. 8 bit al_pa)
  ULONG Req_B_Trans_ID;    // X_ID (OX_ID or RX_ID) and/or Index in SEST
} TachLiteIRB;


typedef struct           // TachLite placeholder for IRBs
{                        // aligned @sizeof(ERQ) for TachLite
                         // MAX commands is sum of SEST len and ERQ
                         // we know that each SEST entry requires an
                         // IRB (ERQ) entry; in addition, we provide
                         // ERQ_LEN
  TachLiteIRB QEntry[ERQ_LEN]; // Base register; entries 32 bytes ea.
  ULONG consumerIndex;   // Consumer Index register
  ULONG producerIndex;   // ERQ Producer Index register
  ULONG length;          // Length register
  ULONG base;            // copy of base ptr for debug
                         // struct is sized for largest expected cmnd (LOGIN)
} TachLiteERQ;

// for now, just 32 bit DMA, eventually 40something, with code changes
#define CPQFCTS_DMA_MASK ((unsigned long) (0x00000000FFFFFFFF))

#define TL_MAX_SG_ELEM_LEN 0x7ffff  // Max buffer length a single S/G entry
				// may represent (a hardware limitation).  The
				// only reason to ever change this is if you
				// want to exercise very-hard-to-reach code in
				// cpqfcTSworker.c:build_SEST_sglist().

#define TL_DANGER_SGPAGES 7  // arbitrary high water mark for # of S/G pages
				// we must exceed to elicit a warning indicative
				// of EXTREMELY large data transfers or 
				// EXTREME memory fragmentation.
				// (means we just used up 2048 S/G elements,
				// Never seen this is real life, only in 
				// testing with tricked up driver.)

#define TL_EXT_SG_PAGE_COUNT 256  // Number of Extended Scatter/Gather a/l PAIRS
                                  // Tachyon register (IOBaseU 0x68)
                                  // power-of-2 value ONLY!  4 min, 256 max

                          // byte len is #Pairs * 2 ULONG/Pair * 4 bytes/ULONG
#define TL_EXT_SG_PAGE_BYTELEN (TL_EXT_SG_PAGE_COUNT *2 *4)



// SEST entry types: IWE, IRE, TWE, TRE
typedef struct 
{
  ULONG Hdr_Len;
  ULONG Hdr_Addr;
  ULONG RSP_Len;
  ULONG RSP_Addr;
  ULONG Buff_Off;
#define USES_EXTENDED_SGLIST(this_sest, x_ID) \
	(!((this_sest)->u[ x_ID ].IWE.Buff_Off & 0x80000000))
  ULONG Link;
  ULONG RX_ID;
  ULONG Data_Len;
  ULONG Exp_RO;
  ULONG Exp_Byte_Cnt;
   // --- extended/local Gather Len/Address pairs
  ULONG GLen1;
  ULONG GAddr1;
  ULONG GLen2;
  ULONG GAddr2;
  ULONG GLen3;
  ULONG GAddr3;
} TachLiteIWE;


typedef struct 
{
  ULONG Seq_Accum;
  ULONG reserved;       // must clear to 0
  ULONG RSP_Len;
  ULONG RSP_Addr;
  ULONG Buff_Off;
  ULONG Buff_Index;           // ULONG 5
  ULONG Exp_RO;
  ULONG Byte_Count;
  ULONG reserved_;      // ULONG 8
  ULONG Exp_Byte_Cnt;
   // --- extended/local Scatter Len/Address pairs
  ULONG SLen1;
  ULONG SAddr1;
  ULONG SLen2;
  ULONG SAddr2;
  ULONG SLen3;
  ULONG SAddr3;
} TachLiteIRE;


typedef struct          // Target Write Entry
{
  ULONG Seq_Accum;      // dword 0
  ULONG reserved;       // dword 1  must clear to 0
  ULONG Remote_Node_ID;
  ULONG reserved1;      // dword 3  must clear to 0
  ULONG Buff_Off;
  ULONG Buff_Index;     // ULONG 5
  ULONG Exp_RO;
  ULONG Byte_Count;
  ULONG reserved_;      // ULONG 8
  ULONG Exp_Byte_Cnt;
   // --- extended/local Scatter Len/Address pairs
  ULONG SLen1;
  ULONG SAddr1;
  ULONG SLen2;
  ULONG SAddr2;
  ULONG SLen3;
  ULONG SAddr3;
} TachLiteTWE;

typedef struct      
{
  ULONG Hdr_Len;
  ULONG Hdr_Addr;
  ULONG RSP_Len;        // DWord 2
  ULONG RSP_Addr;
  ULONG Buff_Off;
  ULONG Buff_Index;     // DWord 5
  ULONG reserved;
  ULONG Data_Len;
  ULONG reserved_;
  ULONG reserved__;
   // --- extended/local Gather Len/Address pairs
  ULONG GLen1;          // DWord A
  ULONG GAddr1;
  ULONG GLen2;
  ULONG GAddr2;
  ULONG GLen3;
  ULONG GAddr3;
} TachLiteTRE;

typedef struct ext_sg_page_ptr_t *PSGPAGES;
typedef struct ext_sg_page_ptr_t 
{
  unsigned char page[TL_EXT_SG_PAGE_BYTELEN * 2]; // 2x for alignment
  dma_addr_t busaddr; 	// need the bus addresses and
  unsigned int maplen;  // lengths for later pci unmapping.
  PSGPAGES next;
} SGPAGES; // linked list of S/G pairs, by Exchange

typedef struct                  // SCSI Exchange State Table
{
  union                         // Entry can be IWE, IRE, TWE, TRE
  {                             // 64 bytes per entry
    TachLiteIWE IWE;
    TachLiteIRE IRE;
    TachLiteTWE TWE;
    TachLiteTRE TRE;
  } u[TACH_SEST_LEN];

  TachFCHDR DataHDR[TACH_SEST_LEN]; // for SEST FCP_DATA frame hdr (no pl)
  TachFCHDR_RSP RspHDR[TACH_SEST_LEN]; // space for SEST FCP_RSP frame
  PSGPAGES sgPages[TACH_SEST_LEN]; // head of linked list of Pool-allocations
  ULONG length;          // Length register
  ULONG base;            // copy of base ptr for debug
} TachSEST;



typedef struct                  // each register has it's own address
                                // and value (used for write-only regs)
{
  void* address;
  volatile ULONG value;
} FCREGISTER;

typedef struct         // Host copy - TachLite Registers
{
  ULONG IOBaseL, IOBaseU;  // I/O port lower and upper TL register addresses
  ULONG MemBase;           // memory mapped register addresses
  void* ReMapMemBase;      // O/S VM reference for MemBase
  ULONG wwn_hi;            // WWN is set once at startup
  ULONG wwn_lo;
  ULONG my_al_pa;          // al_pa received after LIP()
  ULONG ROMCTR;            // flags for on-board RAM/ROM
  ULONG RAMBase;           // on-board RAM (i.e. some Tachlites)
  ULONG SROMBase;          // on-board EEPROM (some Tachlites)
  ULONG PCIMCTR;           // PCI Master Control Reg (has bus width)

  FCREGISTER INTEN;        // copy of interrupt enable mask
  FCREGISTER INTPEND;      // interrupt pending
  FCREGISTER INTSTAT;      // interrupt status
  FCREGISTER SFQconsumerIndex; 
  FCREGISTER ERQproducerIndex; 
  FCREGISTER TYconfig;   // TachYon (chip level)
  FCREGISTER TYcontrol;
  FCREGISTER TYstatus;
  FCREGISTER FMconfig;   // Frame Manager (FC loop level)
  FCREGISTER FMcontrol;
  FCREGISTER FMstatus;
  FCREGISTER FMLinkStatus1;
  FCREGISTER FMLinkStatus2;
  FCREGISTER FMBB_CreditZero;
  FCREGISTER status;
  FCREGISTER ed_tov;     // error detect time-out value
  FCREGISTER rcv_al_pa;  // received arb. loop physical address
  FCREGISTER primitive;  // e.g. LIP(), OPN(), ...
} TL_REGISTERS;



typedef struct 
{
  ULONG ok;
  ULONG invalidArgs;
  ULONG linkDown;
  ULONG linkUp;
  ULONG outQueFull;
  ULONG SESTFull;
  ULONG hpe;    // host programming err (from Tach)
  ULONG FC4aborted; // aborts from Application or upper driver layer
  ULONG FC2aborted; // aborts from our driver's timeouts
  ULONG timeouts;   // our driver timeout (on individual exchanges)
  ULONG logouts;    // explicit - sent LOGO; implicit - device removed
  ULONG retries;
  ULONG linkFailTX;
  ULONG linkFailRX;
  ULONG CntErrors;  // byte count expected != count received (typ. SEST)
  ULONG e_stores;   // elastic store errs
  ULONG resets;     // hard or soft controller resets
  ULONG FMinits;    // TACH Frame Manager Init (e.g. LIPs)
  ULONG lnkQueFull;  // too many LOGIN, loop commands
  ULONG ScsiQueFull; // too many FCP-SCSI inbound frames
  ULONG LossofSignal;   // FM link status 1 regs
  ULONG BadRXChar;   // FM link status 1 regs
  ULONG LossofSync;   // FM link status 1 regs
  ULONG Rx_EOFa;   // FM link status 2 regs (received EOFa)
  ULONG Dis_Frm;   // FM link status 2 regs (discarded frames)
  ULONG Bad_CRC;   // FM link status 2 regs
  ULONG BB0_Timer; //  FM BB_Credit Zero Timer Reg
  ULONG loopBreaks; // infinite loop exits
  ULONG lastBB0timer;  // static accum. buffer needed by Tachlite
} FCSTATS;


typedef struct               // Config Options
{                            // LS Bit first
  USHORT        : 1;           // bit0:
  USHORT  flogi : 1;           // bit1: We sent FLOGI - wait for Fabric logins
  USHORT  fabric: 1;           // bit2: Tachyon detected Fabric (FM stat LG)
  USHORT  LILPin: 1;           // bit3: We can use an FC-AL LILP frame
  USHORT  target: 1;           // bit4: this Port has SCSI target capability
  USHORT  initiator:    1;     // bit5: this Port has SCSI initiator capability
  USHORT  extLoopback:  1;     // bit6: loopback at GBIC
  USHORT  intLoopback:  1;     // bit7: loopback in HP silicon
  USHORT        : 1;           // bit8:
  USHORT        : 1;           // bit9:
  USHORT        : 1;           // bit10:
  USHORT        : 1;           // bit11:
  USHORT        : 1;           // bit12:
  USHORT        : 1;           // bit13:
  USHORT        : 1;           // bit14:
  USHORT        : 1;           // bit15:
} FC_OPTIONS;



typedef struct dyn_mem_pair
{
  void *BaseAllocated;  // address as allocated from O/S;
  unsigned long AlignedAddress; // aligned address (used by Tachyon DMA)
  dma_addr_t dma_handle;
  size_t size;
} ALIGNED_MEM;




// these structs contain only CRUCIAL (stuff we actually use) parameters
// from FC-PH(n) logins.  (Don't save entire LOGIN payload to save mem.)

// Implicit logout happens when the loop goes down - we require PDISC
// to restore.  Explicit logout is when WE decide never to talk to someone,
// or when a target refuses to talk to us, i.e. sends us a LOGO frame or
// LS_RJT reject in response to our PLOGI request.

#define IMPLICIT_LOGOUT 1
#define EXPLICIT_LOGOUT 2

typedef struct 
{
  UCHAR channel; // SCSI "bus"
  UCHAR target;
  UCHAR InqDeviceType;  // byte 0 from SCSI Inquiry response
  UCHAR VolumeSetAddressing;  // FCP-SCSI LUN coding (40h for VSA)
  UCHAR LunMasking;     // True if selective presentation supported
  UCHAR lun[CPQFCTS_MAX_LUN];
} SCSI_NEXUS;


typedef struct        
{
  union 
  {
    UCHAR ucWWN[8];  // a FC 64-bit World Wide Name/ PortID of target
                     // addressing of single target on single loop...
    u64 liWWN;
  } u;

  ULONG port_id;     // a FC 24-bit address of port (lower 8 bits = al_pa)

#define REPORT_LUNS_PL 256  
  UCHAR ReportLunsPayload[REPORT_LUNS_PL];
  
  SCSI_NEXUS ScsiNexus; // LUNs per FC device

  ULONG LOGO_counter; // might try several times before logging out for good
  ULONG LOGO_timer;   // after LIP, ports expecting PDISC must time-out and
                      // LOGOut if successful PDISC not completed in 2 secs

  ULONG concurrent_seq;  // must be 1 or greater
  ULONG rx_data_size;    // e.g. 128, 256, 1024, 2048 per FC-PH spec
  ULONG BB_credit;
  ULONG EE_credit;

  ULONG fcp_info;        // from PRLI (i.e. INITIATOR/ TARGET flags)
                         // flags for login process
  BOOLEAN Originator;    // Login sequence Originated (if false, we
                         // responded to another port's login sequence)
  BOOLEAN plogi;         // PLOGI frame ACCepted (originated or responded)
  BOOLEAN pdisc;         // PDISC frame was ORIGINATED (self-login logic)
  BOOLEAN prli;          // PRLI frame ACCepted (originated or responded)
  BOOLEAN flogi;         // FLOGI frame ACCepted (originated or responded)
  BOOLEAN logo;          // port permanently logged out (invalid login param)
  BOOLEAN flogiReq;      // Fabric login required (set in LIP process)
  UCHAR highest_ver;
  UCHAR lowest_ver;

  
  // when the "target" (actually FC Port) is waiting for login
  // (e.g. after Link reset), set the device_blocked bit;
  // after Port completes login, un-block target.
  UCHAR device_blocked; // see Scsi_Device struct

                    // define singly-linked list of logged-in ports
                    // once a port_id is identified, it is remembered,
                    // even if the port is removed indefinitely
  PVOID pNextPort;  // actually, type PFC_LOGGEDIN_PORT; void for Compiler

} FC_LOGGEDIN_PORT, *PFC_LOGGEDIN_PORT;



// This serves as the ESB (Exchange Status Block),
// and has timeout counter; used for ABORTs
typedef struct                
{                                  // FC-1 X_IDs
  ULONG type;            // ELS_PLOGI, SCSI_IWE, ... (0 if free)
  PFC_LOGGEDIN_PORT pLoggedInPort; // FC device on other end of Exchange
  Scsi_Cmnd *Cmnd;       // Linux SCSI command packet includes S/G list
  ULONG timeOut;         // units of ??, DEC by driver, Abort when 0
  ULONG reTries;         // need one or more retries?
  ULONG status;          // flags indicating errors (0 if none)
  TachLiteIRB IRB;       // I/O Request Block, gets copied to ERQ
  TachFCHDR_GCMND fchs;  // location of IRB's Req_A_SFS_Addr
} FC_EXCHANGE, *PFC_EXCHANGE;

// Unfortunately, Linux limits our kmalloc() allocations to 128k.
// Because of this and the fact that our ScsiRegister allocation
// is also constrained, we move this large structure out for
// allocation after Scsi Register.
// (In other words, this cumbersome indirection is necessary
// because of kernel memory allocation constraints!)

typedef struct // we will allocate this dynamically
{
  FC_EXCHANGE fcExchange[ TACH_MAX_XID ];
} FC_EXCHANGES;











typedef struct
{
  char Name[64]; // name of controller ("HP Tachlite TL Rev2.0, 33MHz, 64bit bus")
  //PVOID  pAdapterDevExt; // back pointer to device object/extension
  ULONG ChipType;        // local numeric key for Tachyon Type / Rev.
  ULONG status;              // our Driver - logical status
  
  TL_REGISTERS Registers;    // reg addresses & host memory copies
                             // FC-4 mapping of 'transaction' to X_IDs
  UCHAR LILPmap[32*4];       // Loop Position Map of ALPAs (late FC-AL only)
  FC_OPTIONS Options;        // e.g. Target, Initiator, loopback...
  UCHAR highest_FCPH_ver;    // FC-PH version limits
  UCHAR lowest_FCPH_ver;     // FC-PH version limits

  FC_EXCHANGES *Exchanges;  
  ULONG fcLsExchangeLRU;       // Least Recently Used counter (Link Service)
  ULONG fcSestExchangeLRU;       // Least Recently Used counter (FCP-SCSI)
  FC_LOGGEDIN_PORT fcPorts;  // linked list of every FC port ever seen
  FCSTATS fcStats;           // FC comm err counters

                             // Host memory QUEUE pointers
  TachLiteERQ *ERQ;          // Exchange Request Que 
  TachyonIMQ *IMQ;           // Inbound Message Que 
  TachLiteSFQ *SFQ;          // Single Frame Queue
  TachSEST *SEST;            // SCSI Exchange State Table

  dma_addr_t exch_dma_handle;

  // these function pointers are for "generic" functions, which are
  // replaced with Host Bus Adapter types at
  // runtime.
  int (*CreateTachyonQues)( void* , int);
  int (*DestroyTachyonQues)( void* , int);
  int (*LaserControl)(void*, int );   // e.g. On/Off
  int (*ResetTachyon)(void*, int );
  void (*FreezeTachyon)(void*, int );
  void (*UnFreezeTachyon)(void*, int );
  int (*InitializeTachyon)(void*, int, int );
  int (*InitializeFrameManager)(void*, int );
  int (*ProcessIMQEntry)(void*);
  int (*ReadWriteWWN)(void*, int ReadWrite);
  int (*ReadWriteNVRAM)(void*, void*, int ReadWrite);

} TACHYON, *PTACHYON;


void cpqfcTSClearLinkStatusCounters(TACHYON * fcChip);

int CpqTsCreateTachLiteQues( void* pHBA, int opcode);
int CpqTsDestroyTachLiteQues( void* , int);
int CpqTsInitializeTachLite( void *pHBA, int opcode1, int opcode2);

int CpqTsProcessIMQEntry(void* pHBA);
int CpqTsResetTachLite(void *pHBA, int type);
void CpqTsFreezeTachlite(void *pHBA, int type);
void CpqTsUnFreezeTachlite(void *pHBA, int type);
int CpqTsInitializeFrameManager(void *pHBA, int);
int CpqTsLaserControl( void* addrBase, int opcode );
int CpqTsReadWriteWWN(void*, int ReadWrite);
int CpqTsReadWriteNVRAM(void*, void* data, int ReadWrite);

void cpqfcTS_WorkTask( struct Scsi_Host *HostAdapter);
void cpqfcTSWorkerThread( void *host);

int cpqfcTS_GetNVRAM_data( UCHAR *wwnbuf, UCHAR *buf );
ULONG cpqfcTS_ReadNVRAM( void* GPIOin, void* GPIOout , USHORT count,
	UCHAR *buf );

BOOLEAN tl_write_i2c_nvram( void* GPIOin, void* GPIOout,
  USHORT startOffset,  // e.g. 0x2f for WWN start
  USHORT count,
  UCHAR *buf );


// define misc functions 
int cpqfcTSGetLPSM( PTACHYON fcChip, char cErrorString[]);
int cpqfcTSDecodeGBICtype( PTACHYON fcChip, char cErrorString[]);
void* fcMemManager( struct pci_dev *pdev,
		ALIGNED_MEM *dyn_mem_pair, ULONG n_alloc, ULONG ab,
                   ULONG ulAlignedAddress, dma_addr_t *dma_handle);

void BigEndianSwap(  UCHAR *source, UCHAR *dest,  USHORT cnt);

//ULONG virt_to_phys( PVOID virtaddr );
                  

// Linux interrupt handler
irqreturn_t cpqfcTS_intr_handler( int irq,void *dev_id,struct pt_regs *regs);
void cpqfcTSheartbeat( unsigned long ptr );



// The biggest Q element we deal with is Aborts - we
// need 4 bytes for x_ID, and a Scsi_Cmnd (~284 bytes)
//#define LINKQ_ITEM_SIZE ((4+sizeof(Scsi_Cmnd)+3)/4)
#define LINKQ_ITEM_SIZE (3*16)
typedef struct
{
  ULONG Type;              // e.g. LINKUP, SFQENTRY, PDISC, BLS_ABTS, ...
  ULONG ulBuff[ LINKQ_ITEM_SIZE ];
} LINKQ_ITEM;

#define FC_LINKQ_DEPTH TACH_MAX_XID
typedef struct
{
  ULONG producer;
  ULONG consumer;  // when producer equals consumer, Q empty

  LINKQ_ITEM Qitem[ FC_LINKQ_DEPTH ];

} FC_LINK_QUE, *PFC_LINK_QUE;


     // DPC routines post to here on Inbound SCSI frames
     // User thread processes
#define FC_SCSIQ_DEPTH 32

typedef struct
{
  int Type;              // e.g. SCSI
  ULONG ulBuff[ 3*16 ];
} SCSIQ_ITEM;

typedef struct
{
  ULONG producer;
  ULONG consumer;  // when producer equals consumer, Q empty

  SCSIQ_ITEM Qitem[ FC_SCSIQ_DEPTH ];

} FC_SCSI_QUE, *PFC_SCSI_QUE;

typedef struct {
	/* This is tacked on to a Scsi_Request in upper_private_data 
	   for pasthrough ioctls, as a place to hold data that can't 
	   be stashed anywhere else in the Scsi_Request.  We differentiate
	   this from _real_ upper_private_data by checking if the virt addr
	   is within our special pool.  */
	ushort bus;
	ushort pdrive;
} cpqfc_passthru_private_t;

#define CPQFC_MAX_PASSTHRU_CMDS 100

#define DYNAMIC_ALLOCATIONS 4  // Tachyon aligned allocations: ERQ,IMQ,SFQ,SEST

// Linux space allocated per HBA (chip state, etc.)
typedef struct 
{
  struct Scsi_Host *HostAdapter; // back pointer to Linux Scsi struct

  TACHYON fcChip;    // All Tachyon registers, Queues, functions
  ALIGNED_MEM dynamic_mem[DYNAMIC_ALLOCATIONS];

  struct pci_dev *PciDev;
  dma_addr_t fcLQ_dma_handle;

  Scsi_Cmnd *LinkDnCmnd[CPQFCTS_REQ_QUEUE_LEN]; // collects Cmnds during LDn
                                                // (for Acceptable targets)
  Scsi_Cmnd *BoardLockCmnd[CPQFCTS_REQ_QUEUE_LEN]; // SEST was full
  
  Scsi_Cmnd *BadTargetCmnd[CPQFCTS_MAX_TARGET_ID]; // missing targets

  u_char HBAnum;     // 0-based host number


  struct timer_list cpqfcTStimer; // FC utility timer for implicit
                                  // logouts, FC protocol timeouts, etc.
  int fcStatsTime;  // Statistics delta reporting time

  struct task_struct *worker_thread; // our kernel thread
  int PortDiscDone;    // set by SendLogins(), cleared by LDn
  
  struct semaphore *TachFrozen;
  struct semaphore *TYOBcomplete;    // handshake for Tach outbound frames
  struct semaphore *fcQueReady;      // FibreChannel work for our kernel thread
  struct semaphore *notify_wt;       // synchronizes kernel thread kill
  struct semaphore *BoardLock;
  
  PFC_LINK_QUE fcLQ;             // the WorkerThread operates on this

  spinlock_t hba_spinlock;           // held/released by WorkerThread
  cpqfc_passthru_private_t *private_data_pool;
  unsigned long *private_data_bits;

} CPQFCHBA;

#define	CPQ_SPINLOCK_HBA( x )   spin_lock(&x->hba_spinlock);
#define CPQ_SPINUNLOCK_HBA(x)   spin_unlock(&x->hba_spinlock);



void cpqfcTSImplicitLogout( CPQFCHBA* cpqfcHBAdata,
		PFC_LOGGEDIN_PORT pFcPort);


void cpqfcTSTerminateExchange( CPQFCHBA*, SCSI_NEXUS *target, int );

PFC_LOGGEDIN_PORT fcPortLoggedIn( 
   CPQFCHBA *cpqfcHBAdata, 
   TachFCHDR_GCMND* fchs, 
   BOOLEAN, 
   BOOLEAN);
void fcProcessLoggedIn( 
   CPQFCHBA *cpqfcHBAdata, TachFCHDR_GCMND* fchs);


ULONG cpqfcTSBuildExchange( 
  CPQFCHBA *cpqfcHBAdata,
  ULONG type, // e.g. PLOGI
  TachFCHDR_GCMND* InFCHS,  // incoming FCHS
  void *Data,               // the CDB, scatter/gather, etc.  
  LONG *ExchangeID );       // allocated exchange ID

ULONG cpqfcTSStartExchange( 
  CPQFCHBA *cpqfcHBAdata,
  LONG ExchangeID );

void cpqfcTSCompleteExchange( 
       struct pci_dev *pcidev,
       PTACHYON fcChip, 
       ULONG exchange_ID);


PFC_LOGGEDIN_PORT  fcFindLoggedInPort( 
  PTACHYON fcChip, 
  Scsi_Cmnd *Cmnd,  // (We want the channel/target/lun Nexus from Cmnd)
  ULONG port_id,  // search linked list for al_pa, or
  UCHAR wwn[8],    // search linked list for WWN, or...
  PFC_LOGGEDIN_PORT *pLastLoggedInPort
);

void cpqfcTSPutLinkQue( 
  CPQFCHBA *cpqfcHBAdata, 
  int Type, 
  void *QueContent);

void fcPutScsiQue( 
  CPQFCHBA *cpqfcHBAdata, 
  int Type, 
  void *QueContent);

void fcLinkQReset(
   CPQFCHBA *);
void fcScsiQReset(
   CPQFCHBA *);
void fcSestReset(
   CPQFCHBA *);

void cpqfc_pci_unmap(struct pci_dev *pcidev, 
	Scsi_Cmnd *cmd, 
	PTACHYON fcChip, 
	ULONG x_ID);

extern const UCHAR valid_al_pa[];
extern const int number_of_al_pa;

#define FCP_RESID_UNDER   0x80000
#define FCP_RESID_OVER    0x40000
#define FCP_SNS_LEN_VALID 0x20000
#define FCP_RSP_LEN_VALID 0x10000

// RSP_CODE definitions (dpANS Fibre Channel Protocol for SCSI, pg 34)
#define FCP_DATA_LEN_NOT_BURST_LEN 0x1000000
#define FCP_CMND_FIELD_INVALID     0x2000000
#define FCP_DATA_RO_NOT_XRDY_RO    0x3000000
#define FCP_TASKFUNCTION_NS        0x4000000
#define FCP_TASKFUNCTION_FAIL      0x5000000

// FCP-SCSI response status struct
typedef struct  // see "TachFCHDR_RSP" definition - 64 bytes
{
  __u32 reserved;
  __u32 reserved1;
  __u32 fcp_status;    // field validity and SCSI status
  __u32 fcp_resid;
  __u32 fcp_sns_len;   // length of FCP_SNS_INFO field
  __u32 fcp_rsp_len;   // length of FCP_RSP_INFO field (expect 8)
  __u32 fcp_rsp_info;  // 4 bytes of FCP protocol response information
  __u32 fcp_rsp_info2; // (4 more bytes, since most implementations use 8)
  __u8  fcp_sns_info[36]; // bytes for SCSI sense (ASC, ASCQ)

} FCP_STATUS_RESPONSE, *PFCP_STATUS_RESPONSE;


// Fabric State Change Registration
typedef struct scrpl
{
  __u32 command;
  __u32 function;
} SCR_PL;

// Fabric Name Service Request
typedef struct nsrpl
{
  __u32 CT_Rev;  // (& IN_ID)   WORD 0
  __u32 FCS_Type;            // WORD 1
  __u32 Command_code;        // WORD 2
  __u32 reason_code;         // WORD 3
  __u32 FCP;                 // WORD 4 (lower byte)
  
} NSR_PL;



// "FC.H"
#define MAX_RX_SIZE		0x800	// Max Receive Buffer Size is 2048
#define MIN_RX_SIZE		0x100	// Min Size is 256, per FC-PLDA Spec
#define	MAX_TARGET_RXIDS	SEST_DEPTH
#define TARGET_RX_SIZE		SEST_BUFFER_LENGTH

#define CLASS_1			0x01
#define CLASS_2			0x02
#define CLASS_3			0x03

#define FC_PH42			0x08
#define FC_PH43			0x09
#define FC_PH3			0x20

#define RR_TOV			2	// Minimum Time for target to wait for
					// PDISC after a LIP.
#define E_D_TOV			2	// Minimum Time to wait for Sequence
					// Completion.
#define R_A_TOV			0	// Minimum Time for Target to wait 
					// before reclaiming resources.
//
//	R_CTL Field
//
//	Routing Bits (31-28)
//
#define FC4_DEVICE_DATA		0x00000000
#define EXT_LINK_DATA		0x20000000
#define FC4_LINK_DATA		0x30000000
#define VIDEO_DATA		0x40000000
#define BASIC_LINK_DATA		0x80000000
#define LINK_CONTROL		0xC0000000
#define ROUTING_MASK		0xF0000000

//
//	Information Bits (27-24)
//
#define UNCAT_INFORMATION	0x00000000
#define SOLICITED_DATA		0x01000000
#define UNSOLICITED_CONTROL	0x02000000
#define SOLICITED_CONTROL	0x03000000
#define UNSOLICITED_DATA	0x04000000
#define DATA_DESCRIPTOR		0x05000000
#define UNSOLICITED_COMMAND	0x06000000
#define COMMAND_STATUS		0x07000000
#define INFO_MASK		0x0F000000
//
//	(Link Control Codes)
//
#define ACK_1			0x00000000
#define ACK_0_OR_N		0x01000000
#define P_RJT			0x02000000 
#define F_RJT			0x03000000 
#define P_BSY			0x04000000
#define FABRIC_BUSY_TO_DF	0x05000000	// Fabric Busy to Data Frame
#define FABRIC_BUSY_TO_LC	0x06000000	// Fabric Busy to Link Ctl Frame
#define LINK_CREDIT_RESET	0x07000000
//
//	(Link Service Command Codes)
//
//#define LS_RJT			0x01000000	// LS Reject

#define LS_ACC			0x02000000	// LS Accept
#define LS_PLOGI		0x03000000	// N_PORT Login
#define LS_FLOGI		0x04000000	// F_PORT Login
#define LS_LOGO			0x05000000	// Logout
#define LS_ABTX			0x06000000	// Abort Exchange
#define LS_RCS			0x07000000	// Read Connection Status
#define LS_RES			0x08000000	// Read Exchange Status
#define LS_RSS			0x09000000	// Read Sequence Status
#define LS_RSI			0x0A000000	// Request Seq Initiative
#define LS_ESTS			0x0B000000	// Establish Steaming
#define LS_ESTC			0x0C000000	// Estimate Credit
#define LS_ADVC			0x0D000000	// Advice Credit
#define LS_RTV			0x0E000000	// Read Timeout Value
#define LS_RLS			0x0F000000	// Read Link Status
#define LS_ECHO			0x10000000	// Echo
#define LS_TEST			0x11000000	// Test
#define LS_RRQ			0x12000000	// Reinstate Rec. Qual.
#define LS_PRLI			0x20000000	// Process Login
#define LS_PRLO			0x21000000	// Process Logout
#define LS_TPRLO		0x24000000	// 3rd Party Process Logout
#define LS_PDISC		0x50000000	// Process Discovery
#define LS_FDISC		0x51000000	// Fabric Discovery
#define LS_ADISC		0x52000000	// Discover Address
#define LS_RNC			0x53000000	// Report Node Capability
#define LS_SCR                  0x62000000      // State Change Registration
#define LS_MASK			0xFF000000	

//
// 	TYPE Bit Masks
//
#define BASIC_LINK_SERVICE	0x00000000
#define EXT_LINK_SERVICE	0x01000000

#define LLC			0x04000000
#define LLC_SNAP		0x05000000
#define SCSI_FCP		0x08000000
#define SCSI_GPP		0x09000000
#define IPI3_MASTER		0x11000000
#define IPI3_SLAVE		0x12000000
#define IPI3_PEER		0x13000000
#define CP_IPI3_MASTER		0x15000000
#define CP_IPI3_SLAVE		0x16000000
#define CP_IPI3_PEER		0x17000000
#define SBCCS_CHANNEL		0x19000000
#define SBCCS_CONTROL		0x1A000000
#define FIBRE_SERVICES		0x20000000
#define FC_FG			0x21000000
#define FC_XS			0x22000000
#define FC_AL			0x23000000
#define SNMP			0x24000000
#define HIPPI_FP		0x40000000
#define TYPE_MASK		0xFF000000

typedef struct {
	UCHAR seq_id_valid;
	UCHAR seq_id;
	USHORT reserved;  // 2 bytes reserved
	ULONG ox_rx_id;
	USHORT low_seq_cnt;
	USHORT high_seq_cnt;
} BA_ACC_PAYLOAD;

typedef struct {
	UCHAR reserved;
	UCHAR reason_code;
	UCHAR reason_explain;
	UCHAR vendor_unique;
} BA_RJT_PAYLOAD;


typedef struct {
	ULONG 	command_code;
	ULONG 	sid;
	USHORT	ox_id;
	USHORT	rx_id;
} RRQ_MESSAGE;

typedef struct {
	ULONG command_code;
	UCHAR vendor;
	UCHAR explain;
	UCHAR reason;
	UCHAR reserved;
} REJECT_MESSAGE;


#define	N_OR_F_PORT		0x1000
#define RANDOM_RELATIVE_OFFSET	0x4000
#define CONTINUOSLY_INCREASING	0x8000

#define CLASS_VALID		0x8000
#define INTERMIX_MODE		0x4000
#define TRANSPARENT_STACKED	0x2000
#define LOCKDOWN_STACKED	0x1000
#define SEQ_DELIVERY		0x800

#define XID_NOT_SUPPORTED	0x00
#define XID_SUPPORTED		0x4000
#define XID_REQUIRED		0xC000

#define ASSOCIATOR_NOT_SUPPORTED	0x00
#define ASSOCIATOR_SUPPORTED	0x1000
#define ASSOCIATOR_REQUIRED	0x3000

#define	INIT_ACK0_SUPPORT	0x800
#define INIT_ACKN_SUPPORT	0x400

#define	RECIP_ACK0_SUPPORT	0x8000
#define RECIP_ACKN_SUPPORT	0x4000

#define X_ID_INTERLOCK		0x2000

#define ERROR_POLICY		0x1800		// Error Policy Supported
#define ERROR_DISCARD		0x00		// Only Discard Supported
#define ERROR_DISC_PROCESS	0x02		// Discard and process supported

#define NODE_ID			0x01
#define IEEE_EXT		0x20

//
// Categories Supported Per Sequence
//
#define	CATEGORIES_PER_SEQUENCE	0x300
#define ONE_CATEGORY_SEQUENCE	0x00		// 1 Category per Sequence
#define TWO_CATEGORY_SEQUENCE	0x01		// 2 Categories per Sequence
#define MANY_CATEGORY_SEQUENCE	0x03		// > 2 Categories/Sequence

typedef struct {

	USHORT initiator_control;
	USHORT service_options;

	USHORT rx_data_size;
	USHORT recipient_control;

	USHORT ee_credit;
	USHORT concurrent_sequences;

	USHORT reserved;
	USHORT open_sequences;

} CLASS_PARAMETERS;

typedef struct {
	ULONG	login_cmd;
	//
	// Common Service Parameters
	//
	struct {

		USHORT bb_credit;
		UCHAR lowest_ver;
		UCHAR highest_ver;

		USHORT bb_rx_size;
		USHORT common_features;

		USHORT rel_offset;
		USHORT concurrent_seq;


		ULONG e_d_tov;
	} cmn_services;

	//
	// Port Name
	//
	UCHAR port_name[8];

	//
	// Node/Fabric Name
	//
	UCHAR node_name[8];

	//
	// Class 1, 2 and 3 Service Parameters
	//
	CLASS_PARAMETERS	class1;
	CLASS_PARAMETERS	class2;
	CLASS_PARAMETERS	class3;

	ULONG reserved[4];

	//
	// Vendor Version Level
	//
	UCHAR		vendor_id[2];
	UCHAR		vendor_version[6];
	ULONG		buffer_size;
	USHORT		rxid_start;
	USHORT		total_rxids;
} LOGIN_PAYLOAD;


typedef struct
{
  ULONG cmd;  // 4 bytes
  UCHAR n_port_identifier[3];
  UCHAR reserved;
  UCHAR port_name[8];
} LOGOUT_PAYLOAD;


//
//	PRLI Request Service Parameter Defines
//
#define PRLI_ACC			0x01
#define PRLI_REQ			0x02
#define ORIG_PROCESS_ASSOC_VALID	0x8000
#define RESP_PROCESS_ASSOC_VALID	0x4000
#define ESTABLISH_PAIR			0x2000
#define DATA_OVERLAY_ALLOWED		0x40
#define	INITIATOR_FUNCTION		0x20
#define	TARGET_FUNCTION			0x10
#define CMD_DATA_MIXED			0x08
#define DATA_RESP_MIXED			0x04
#define READ_XFER_RDY			0x02
#define WRITE_XFER_RDY			0x01

#define RESPONSE_CODE_MASK	0xF00
#define REQUEST_EXECUTED	0x100
#define NO_RESOURCES		0x200
#define INIT_NOT_COMPLETE	0x300
#define IMAGE_DOES_NOT_EXIST	0x400
#define BAD_PREDEFINED_COND	0x500
#define REQ_EXEC_COND		0x600
#define NO_MULTI_PAGE		0x700

typedef struct {
	USHORT	payload_length;
	UCHAR	page_length;
	UCHAR	cmd;


	ULONG	valid;

	ULONG	orig_process_associator;

	ULONG	resp_process_associator;
	
	ULONG	fcp_info;
} PRLI_REQUEST;

typedef struct {

	USHORT	payload_length;
	UCHAR	page_length;
	UCHAR	cmd;

	ULONG	valid;
	ULONG	orig_process_associator;

	ULONG	resp_process_associator;
	ULONG	reserved;
} PRLO_REQUEST;

typedef struct {
	ULONG	cmd;

	ULONG	hard_address;
	
	UCHAR	port_name[8];

	UCHAR	node_name[8];

	ULONG	s_id;
} ADISC_PAYLOAD;

struct ext_sg_entry_t {
	__u32 len:18;		/* buffer length, bits 0-17 */
	__u32 uba:13;		/* upper bus address bits 18-31 */
	__u32 lba;		/* lower bus address bits 0-31 */
}; 


// J. McCarty's LINK.H
//
//	LS_RJT Reason Codes
//

#define INVALID_COMMAND_CODE	0x01
#define LOGICAL_ERROR		0x03
#define LOGICAL_BUSY		0x05
#define PROTOCOL_ERROR		0x07
#define UNABLE_TO_PERFORM	0x09
#define COMMAND_NOT_SUPPORTED	0x0B
#define LS_VENDOR_UNIQUE	0xFF

//
// 	LS_RJT Reason Codes Explanations
//
#define NO_REASON		0x00
#define OPTIONS_ERROR		0x01
#define INITIATOR_CTL_ERROR	0x03
#define RECIPIENT_CTL_ERROR	0x05
#define DATA_FIELD_SIZE_ERROR	0x07
#define CONCURRENT_SEQ_ERROR	0x09
#define CREDIT_ERROR		0x0B
#define INVALID_PORT_NAME	0x0D
#define INVALID_NODE_NAME	0x0E
#define INVALID_CSP		0x0F	// Invalid Service Parameters
#define INVALID_ASSOC_HDR	0x11	// Invalid Association Header
#define ASSOC_HDR_REQUIRED	0x13	// Association Header Required
#define LS_INVALID_S_ID		0x15
#define INVALID_OX_RX_ID	0x17	// Invalid OX_ID RX_ID Combination
#define CMD_IN_PROCESS		0x19
#define INVALID_IDENTIFIER	0x1F	// Invalid N_PORT Identifier
#define INVALID_SEQ_ID		0x21
#define ABT_INVALID_XCHNG	0x23 	// Attempt to Abort an invalid Exchange
#define ABT_INACTIVE_XCHNG	0x25 	// Attempt to Abort an inactive Exchange
#define NEED_REC_QUAL		0x27	// Recovery Qualifier required
#define NO_LOGIN_RESOURCES	0x29	// No resources to support login
#define NO_DATA			0x2A	// Unable to supply requested data
#define	REQUEST_NOT_SUPPORTED	0x2C	// Request Not Supported

//
//	Link Control Codes
//

//
//	P_BSY Action Codes
//
#define SEQUENCE_TERMINATED	0x01000000
#define SEQUENCE_ACTIVE		0x02000000

//
//	P_BSY Reason Codes
//
#define PHYS_NPORT_BUSY		0x010000
#define NPORT_RESOURCE_BUSY	0x020000

//
// 	P_RJT, F_RJT Action Codes
//

#define RETRYABLE_ERROR		0x01000000
#define NON_RETRYABLE_ERROR	0x02000000

//
// 	P_RJT, F_RJT Reason Codes
//
#define INVALID_D_ID		0x010000
#define INVALID_S_ID		0x020000
#define NPORT_NOT_AVAIL_TMP	0x030000
#define NPORT_NOT_AVAIL_PERM	0x040000
#define CLASS_NOT_SUPPORTED	0x050000
#define USAGE_ERROR		0x060000
#define TYPE_NOT_SUPPORTED	0x070000
#define INVAL_LINK_CONTROL	0x080000
#define INVAL_R_CTL		0x090000
#define INVAL_F_CTL		0x0A0000
#define INVAL_OX_ID		0x0B0000
#define INVAL_RX_ID		0x0C0000
#define INVAL_SEQ_ID		0x0D0000
#define INVAL_DF_CTL		0x0E0000
#define INVAL_SEQ_CNT		0x0F0000
#define INVAL_PARAMS		0x100000
#define EXCHANGE_ERROR		0x110000
#define LS_PROTOCOL_ERROR	0x120000
#define INCORRECT_LENGTH	0x130000
#define UNEXPECTED_ACK		0x140000
#define LOGIN_REQ		0x160000
#define EXCESSIVE_SEQ		0x170000
#define NO_EXCHANGE		0x180000
#define SEC_HDR_NOT_SUPPORTED	0x190000
#define NO_FABRIC		0x1A0000
#define P_VENDOR_UNIQUE		0xFF0000

//
// 	BA_RJT Reason Codes
//
#define BA_INVALID_COMMAND	0x00010000
#define BA_LOGICAL_ERROR	0x00030000
#define BA_LOGICAL_BUSY		0x00050000
#define BA_PROTOCOL_ERROR	0x00070000
#define BA_UNABLE_TO_PERFORM	0x00090000

//
// 	BA_RJT Reason Explanation Codes
//
#define BA_NO_REASON		0x00000000
#define BA_INVALID_OX_RX	0x00000300
#define BA_SEQUENCE_ABORTED	0x00000500



#endif /* CPQFCTSSTRUCTS_H	*/

