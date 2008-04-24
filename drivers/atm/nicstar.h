/******************************************************************************
 *
 * nicstar.h
 *
 * Header file for the nicstar device driver.
 *
 * Author: Rui Prior (rprior@inescn.pt)
 * PowerPC support by Jay Talbott (jay_talbott@mcg.mot.com) April 1999
 *
 * (C) INESC 1998
 *
 ******************************************************************************/


#ifndef _LINUX_NICSTAR_H_
#define _LINUX_NICSTAR_H_


/* Includes *******************************************************************/

#include <linux/types.h>
#include <linux/pci.h>
#include <linux/uio.h>
#include <linux/skbuff.h>
#include <linux/atmdev.h>
#include <linux/atm_nicstar.h>


/* Options ********************************************************************/

#define NS_MAX_CARDS 4		/* Maximum number of NICStAR based cards
				   controlled by the device driver. Must
                                   be <= 5 */

#undef RCQ_SUPPORT		/* Do not define this for now */

#define NS_TST_NUM_ENTRIES 2340	/* + 1 for return */
#define NS_TST_RESERVED 340	/* N. entries reserved for UBR/ABR/VBR */

#define NS_SMBUFSIZE 48		/* 48, 96, 240 or 2048 */
#define NS_LGBUFSIZE 16384	/* 2048, 4096, 8192 or 16384 */
#define NS_RSQSIZE 8192		/* 2048, 4096 or 8192 */
#define NS_VPIBITS 2		/* 0, 1, 2, or 8 */

#define NS_MAX_RCTSIZE 4096	/* Number of entries. 4096 or 16384.
                                   Define 4096 only if (all) your card(s)
				   have 32K x 32bit SRAM, in which case
				   setting this to 16384 will just waste a
				   lot of memory.
				   Setting this to 4096 for a card with
				   128K x 32bit SRAM will limit the maximum
				   VCI. */

/*#define NS_PCI_LATENCY 64*/	/* Must be a multiple of 32 */

	/* Number of buffers initially allocated */
#define NUM_SB 32	/* Must be even */
#define NUM_LB 24	/* Must be even */
#define NUM_HB 8	/* Pre-allocated huge buffers */
#define NUM_IOVB 48	/* Iovec buffers */

	/* Lower level for count of buffers */
#define MIN_SB 8	/* Must be even */
#define MIN_LB 8	/* Must be even */
#define MIN_HB 6
#define MIN_IOVB 8

	/* Upper level for count of buffers */
#define MAX_SB 64	/* Must be even, <= 508 */
#define MAX_LB 48	/* Must be even, <= 508 */
#define MAX_HB 10
#define MAX_IOVB 80

	/* These are the absolute maximum allowed for the ioctl() */
#define TOP_SB 256	/* Must be even, <= 508 */
#define TOP_LB 128	/* Must be even, <= 508 */
#define TOP_HB 64
#define TOP_IOVB 256


#define MAX_TBD_PER_VC 1	/* Number of TBDs before a TSR */
#define MAX_TBD_PER_SCQ 10	/* Only meaningful for variable rate SCQs */

#undef ENABLE_TSQFIE

#define SCQFULL_TIMEOUT (5 * HZ)

#define NS_POLL_PERIOD (HZ)

#define PCR_TOLERANCE (1.0001)



/* ESI stuff ******************************************************************/

#define NICSTAR_EPROM_MAC_ADDR_OFFSET 0x6C
#define NICSTAR_EPROM_MAC_ADDR_OFFSET_ALT 0xF6


/* #defines *******************************************************************/

#define NS_IOREMAP_SIZE 4096

/*
 * BUF_XX distinguish the Rx buffers depending on their (small/large) size.
 * BUG_SM and BUG_LG are both used by the driver and the device.
 * BUF_NONE is only used by the driver.
 */
#define BUF_SM		0x00000000	/* These two are used for push_rxbufs() */
#define BUF_LG		0x00000001	/* CMD, Write_FreeBufQ, LBUF bit */
#define BUF_NONE 	0xffffffff	/* Software only: */

#define NS_HBUFSIZE 65568	/* Size of max. AAL5 PDU */
#define NS_MAX_IOVECS (2 + (65568 - NS_SMBUFSIZE) / \
                       (NS_LGBUFSIZE - (NS_LGBUFSIZE % 48)))
#define NS_IOVBUFSIZE (NS_MAX_IOVECS * (sizeof(struct iovec)))

#define NS_SMBUFSIZE_USABLE (NS_SMBUFSIZE - NS_SMBUFSIZE % 48)
#define NS_LGBUFSIZE_USABLE (NS_LGBUFSIZE - NS_LGBUFSIZE % 48)

#define NS_AAL0_HEADER (ATM_AAL0_SDU - ATM_CELL_PAYLOAD)	/* 4 bytes */

#define NS_SMSKBSIZE (NS_SMBUFSIZE + NS_AAL0_HEADER)
#define NS_LGSKBSIZE (NS_SMBUFSIZE + NS_LGBUFSIZE)


/* NICStAR structures located in host memory **********************************/



/* RSQ - Receive Status Queue 
 *
 * Written by the NICStAR, read by the device driver.
 */

typedef struct ns_rsqe
{
   u32 word_1;
   u32 buffer_handle;
   u32 final_aal5_crc32;
   u32 word_4;
} ns_rsqe;

#define ns_rsqe_vpi(ns_rsqep) \
        ((le32_to_cpu((ns_rsqep)->word_1) & 0x00FF0000) >> 16)
#define ns_rsqe_vci(ns_rsqep) \
        (le32_to_cpu((ns_rsqep)->word_1) & 0x0000FFFF)

#define NS_RSQE_VALID      0x80000000
#define NS_RSQE_NZGFC      0x00004000
#define NS_RSQE_EOPDU      0x00002000
#define NS_RSQE_BUFSIZE    0x00001000
#define NS_RSQE_CONGESTION 0x00000800
#define NS_RSQE_CLP        0x00000400
#define NS_RSQE_CRCERR     0x00000200

#define NS_RSQE_BUFSIZE_SM 0x00000000
#define NS_RSQE_BUFSIZE_LG 0x00001000

#define ns_rsqe_valid(ns_rsqep) \
        (le32_to_cpu((ns_rsqep)->word_4) & NS_RSQE_VALID)
#define ns_rsqe_nzgfc(ns_rsqep) \
        (le32_to_cpu((ns_rsqep)->word_4) & NS_RSQE_NZGFC)
#define ns_rsqe_eopdu(ns_rsqep) \
        (le32_to_cpu((ns_rsqep)->word_4) & NS_RSQE_EOPDU)
#define ns_rsqe_bufsize(ns_rsqep) \
        (le32_to_cpu((ns_rsqep)->word_4) & NS_RSQE_BUFSIZE)
#define ns_rsqe_congestion(ns_rsqep) \
        (le32_to_cpu((ns_rsqep)->word_4) & NS_RSQE_CONGESTION)
#define ns_rsqe_clp(ns_rsqep) \
        (le32_to_cpu((ns_rsqep)->word_4) & NS_RSQE_CLP)
#define ns_rsqe_crcerr(ns_rsqep) \
        (le32_to_cpu((ns_rsqep)->word_4) & NS_RSQE_CRCERR)

#define ns_rsqe_cellcount(ns_rsqep) \
        (le32_to_cpu((ns_rsqep)->word_4) & 0x000001FF)
#define ns_rsqe_init(ns_rsqep) \
        ((ns_rsqep)->word_4 = cpu_to_le32(0x00000000)) 

#define NS_RSQ_NUM_ENTRIES (NS_RSQSIZE / 16)
#define NS_RSQ_ALIGNMENT NS_RSQSIZE



/* RCQ - Raw Cell Queue
 *
 * Written by the NICStAR, read by the device driver.
 */

typedef struct cell_payload
{
   u32 word[12];
} cell_payload;

typedef struct ns_rcqe
{
   u32 word_1;
   u32 word_2;
   u32 word_3;
   u32 word_4;
   cell_payload payload;
} ns_rcqe;

#define NS_RCQE_SIZE 64		/* bytes */

#define ns_rcqe_islast(ns_rcqep) \
        (le32_to_cpu((ns_rcqep)->word_2) != 0x00000000)
#define ns_rcqe_cellheader(ns_rcqep) \
        (le32_to_cpu((ns_rcqep)->word_1))
#define ns_rcqe_nextbufhandle(ns_rcqep) \
        (le32_to_cpu((ns_rcqep)->word_2))



/* SCQ - Segmentation Channel Queue 
 *
 * Written by the device driver, read by the NICStAR.
 */

typedef struct ns_scqe
{
   u32 word_1;
   u32 word_2;
   u32 word_3;
   u32 word_4;
} ns_scqe;

   /* NOTE: SCQ entries can be either a TBD (Transmit Buffer Descriptors)
            or TSR (Transmit Status Requests) */

#define NS_SCQE_TYPE_TBD 0x00000000
#define NS_SCQE_TYPE_TSR 0x80000000


#define NS_TBD_EOPDU 0x40000000
#define NS_TBD_AAL0  0x00000000
#define NS_TBD_AAL34 0x04000000
#define NS_TBD_AAL5  0x08000000

#define NS_TBD_VPI_MASK 0x0FF00000
#define NS_TBD_VCI_MASK 0x000FFFF0
#define NS_TBD_VC_MASK (NS_TBD_VPI_MASK | NS_TBD_VCI_MASK)

#define NS_TBD_VPI_SHIFT 20
#define NS_TBD_VCI_SHIFT 4

#define ns_tbd_mkword_1(flags, m, n, buflen) \
      (cpu_to_le32((flags) | (m) << 23 | (n) << 16 | (buflen)))
#define ns_tbd_mkword_1_novbr(flags, buflen) \
      (cpu_to_le32((flags) | (buflen) | 0x00810000))
#define ns_tbd_mkword_3(control, pdulen) \
      (cpu_to_le32((control) << 16 | (pdulen)))
#define ns_tbd_mkword_4(gfc, vpi, vci, pt, clp) \
      (cpu_to_le32((gfc) << 28 | (vpi) << 20 | (vci) << 4 | (pt) << 1 | (clp)))


#define NS_TSR_INTENABLE 0x20000000

#define NS_TSR_SCDISVBR 0xFFFF		/* Use as scdi for VBR SCD */

#define ns_tsr_mkword_1(flags) \
        (cpu_to_le32(NS_SCQE_TYPE_TSR | (flags)))
#define ns_tsr_mkword_2(scdi, scqi) \
        (cpu_to_le32((scdi) << 16 | 0x00008000 | (scqi)))

#define ns_scqe_is_tsr(ns_scqep) \
        (le32_to_cpu((ns_scqep)->word_1) & NS_SCQE_TYPE_TSR)

#define VBR_SCQ_NUM_ENTRIES 512
#define VBR_SCQSIZE 8192
#define CBR_SCQ_NUM_ENTRIES 64
#define CBR_SCQSIZE 1024

#define NS_SCQE_SIZE 16



/* TSQ - Transmit Status Queue
 *
 * Written by the NICStAR, read by the device driver.
 */

typedef struct ns_tsi
{
   u32 word_1;
   u32 word_2;
} ns_tsi;

   /* NOTE: The first word can be a status word copied from the TSR which
            originated the TSI, or a timer overflow indicator. In this last
	    case, the value of the first word is all zeroes. */

#define NS_TSI_EMPTY          0x80000000
#define NS_TSI_TIMESTAMP_MASK 0x00FFFFFF

#define ns_tsi_isempty(ns_tsip) \
        (le32_to_cpu((ns_tsip)->word_2) & NS_TSI_EMPTY)
#define ns_tsi_gettimestamp(ns_tsip) \
        (le32_to_cpu((ns_tsip)->word_2) & NS_TSI_TIMESTAMP_MASK)

#define ns_tsi_init(ns_tsip) \
        ((ns_tsip)->word_2 = cpu_to_le32(NS_TSI_EMPTY))


#define NS_TSQSIZE 8192
#define NS_TSQ_NUM_ENTRIES 1024
#define NS_TSQ_ALIGNMENT 8192


#define NS_TSI_SCDISVBR NS_TSR_SCDISVBR

#define ns_tsi_tmrof(ns_tsip) \
        (le32_to_cpu((ns_tsip)->word_1) == 0x00000000)
#define ns_tsi_getscdindex(ns_tsip) \
        ((le32_to_cpu((ns_tsip)->word_1) & 0xFFFF0000) >> 16)
#define ns_tsi_getscqpos(ns_tsip) \
        (le32_to_cpu((ns_tsip)->word_1) & 0x00007FFF)



/* NICStAR structures located in local SRAM ***********************************/



/* RCT - Receive Connection Table
 *
 * Written by both the NICStAR and the device driver.
 */

typedef struct ns_rcte
{
   u32 word_1;
   u32 buffer_handle;
   u32 dma_address;
   u32 aal5_crc32;
} ns_rcte;

#define NS_RCTE_BSFB            0x00200000  /* Rev. D only */
#define NS_RCTE_NZGFC           0x00100000
#define NS_RCTE_CONNECTOPEN     0x00080000
#define NS_RCTE_AALMASK         0x00070000
#define NS_RCTE_AAL0            0x00000000
#define NS_RCTE_AAL34           0x00010000
#define NS_RCTE_AAL5            0x00020000
#define NS_RCTE_RCQ             0x00030000
#define NS_RCTE_RAWCELLINTEN    0x00008000
#define NS_RCTE_RXCONSTCELLADDR 0x00004000
#define NS_RCTE_BUFFVALID       0x00002000
#define NS_RCTE_FBDSIZE         0x00001000
#define NS_RCTE_EFCI            0x00000800
#define NS_RCTE_CLP             0x00000400
#define NS_RCTE_CRCERROR        0x00000200
#define NS_RCTE_CELLCOUNT_MASK  0x000001FF

#define NS_RCTE_FBDSIZE_SM 0x00000000
#define NS_RCTE_FBDSIZE_LG 0x00001000

#define NS_RCT_ENTRY_SIZE 4	/* Number of dwords */

   /* NOTE: We could make macros to contruct the first word of the RCTE,
            but that doesn't seem to make much sense... */



/* FBD - Free Buffer Descriptor
 *
 * Written by the device driver using via the command register.
 */

typedef struct ns_fbd
{
   u32 buffer_handle;
   u32 dma_address;
} ns_fbd;




/* TST - Transmit Schedule Table
 *
 * Written by the device driver.
 */

typedef u32 ns_tste;

#define NS_TST_OPCODE_MASK 0x60000000

#define NS_TST_OPCODE_NULL     0x00000000 /* Insert null cell */
#define NS_TST_OPCODE_FIXED    0x20000000 /* Cell from a fixed rate channel */
#define NS_TST_OPCODE_VARIABLE 0x40000000
#define NS_TST_OPCODE_END      0x60000000 /* Jump */

#define ns_tste_make(opcode, sramad) (opcode | sramad)

   /* NOTE:

      - When the opcode is FIXED, sramad specifies the SRAM address of the
        SCD for that fixed rate channel.
      - When the opcode is END, sramad specifies the SRAM address of the
        location of the next TST entry to read.
    */



/* SCD - Segmentation Channel Descriptor
 *
 * Written by both the device driver and the NICStAR
 */

typedef struct ns_scd
{
   u32 word_1;
   u32 word_2;
   u32 partial_aal5_crc;
   u32 reserved;
   ns_scqe cache_a;
   ns_scqe cache_b;
} ns_scd;

#define NS_SCD_BASE_MASK_VAR 0xFFFFE000		/* Variable rate */
#define NS_SCD_BASE_MASK_FIX 0xFFFFFC00		/* Fixed rate */
#define NS_SCD_TAIL_MASK_VAR 0x00001FF0
#define NS_SCD_TAIL_MASK_FIX 0x000003F0
#define NS_SCD_HEAD_MASK_VAR 0x00001FF0
#define NS_SCD_HEAD_MASK_FIX 0x000003F0
#define NS_SCD_XMITFOREVER   0x02000000

   /* NOTE: There are other fields in word 2 of the SCD, but as they should
            not be needed in the device driver they are not defined here. */




/* NICStAR local SRAM memory map **********************************************/


#define NS_RCT           0x00000
#define NS_RCT_32_END    0x03FFF
#define NS_RCT_128_END   0x0FFFF
#define NS_UNUSED_32     0x04000
#define NS_UNUSED_128    0x10000
#define NS_UNUSED_END    0x1BFFF
#define NS_TST_FRSCD     0x1C000
#define NS_TST_FRSCD_END 0x1E7DB
#define NS_VRSCD2        0x1E7DC
#define NS_VRSCD2_END    0x1E7E7
#define NS_VRSCD1        0x1E7E8
#define NS_VRSCD1_END    0x1E7F3
#define NS_VRSCD0        0x1E7F4
#define NS_VRSCD0_END    0x1E7FF
#define NS_RXFIFO        0x1E800
#define NS_RXFIFO_END    0x1F7FF
#define NS_SMFBQ         0x1F800
#define NS_SMFBQ_END     0x1FBFF
#define NS_LGFBQ         0x1FC00
#define NS_LGFBQ_END     0x1FFFF



/* NISCtAR operation registers ************************************************/


/* See Section 3.4 of `IDT77211 NICStAR User Manual' from www.idt.com */

enum ns_regs
{
   DR0   = 0x00,      /* Data Register 0 R/W*/
   DR1   = 0x04,      /* Data Register 1 W */
   DR2   = 0x08,      /* Data Register 2 W */
   DR3   = 0x0C,      /* Data Register 3 W */
   CMD   = 0x10,      /* Command W */
   CFG   = 0x14,      /* Configuration R/W */
   STAT  = 0x18,      /* Status R/W */
   RSQB  = 0x1C,      /* Receive Status Queue Base W */
   RSQT  = 0x20,      /* Receive Status Queue Tail R */
   RSQH  = 0x24,      /* Receive Status Queue Head W */
   CDC   = 0x28,      /* Cell Drop Counter R/clear */
   VPEC  = 0x2C,      /* VPI/VCI Lookup Error Count R/clear */
   ICC   = 0x30,      /* Invalid Cell Count R/clear */
   RAWCT = 0x34,      /* Raw Cell Tail R */
   TMR   = 0x38,      /* Timer R */
   TSTB  = 0x3C,      /* Transmit Schedule Table Base R/W */
   TSQB  = 0x40,      /* Transmit Status Queue Base W */
   TSQT  = 0x44,      /* Transmit Status Queue Tail R */
   TSQH  = 0x48,      /* Transmit Status Queue Head W */
   GP    = 0x4C,      /* General Purpose R/W */
   VPM   = 0x50       /* VPI/VCI Mask W */
};


/* NICStAR commands issued to the CMD register ********************************/


/* Top 4 bits are command opcode, lower 28 are parameters. */

#define NS_CMD_NO_OPERATION         0x00000000
        /* params always 0 */

#define NS_CMD_OPENCLOSE_CONNECTION 0x20000000
        /* b19{1=open,0=close} b18-2{SRAM addr} */

#define NS_CMD_WRITE_SRAM           0x40000000
        /* b18-2{SRAM addr} b1-0{burst size} */

#define NS_CMD_READ_SRAM            0x50000000
        /* b18-2{SRAM addr} */

#define NS_CMD_WRITE_FREEBUFQ       0x60000000
        /* b0{large buf indicator} */

#define NS_CMD_READ_UTILITY         0x80000000
        /* b8{1=select UTL_CS1} b9{1=select UTL_CS0} b7-0{bus addr} */

#define NS_CMD_WRITE_UTILITY        0x90000000
        /* b8{1=select UTL_CS1} b9{1=select UTL_CS0} b7-0{bus addr} */

#define NS_CMD_OPEN_CONNECTION (NS_CMD_OPENCLOSE_CONNECTION | 0x00080000)
#define NS_CMD_CLOSE_CONNECTION NS_CMD_OPENCLOSE_CONNECTION


/* NICStAR configuration bits *************************************************/

#define NS_CFG_SWRST          0x80000000    /* Software Reset */
#define NS_CFG_RXPATH         0x20000000    /* Receive Path Enable */
#define NS_CFG_SMBUFSIZE_MASK 0x18000000    /* Small Receive Buffer Size */
#define NS_CFG_LGBUFSIZE_MASK 0x06000000    /* Large Receive Buffer Size */
#define NS_CFG_EFBIE          0x01000000    /* Empty Free Buffer Queue
                                               Interrupt Enable */
#define NS_CFG_RSQSIZE_MASK   0x00C00000    /* Receive Status Queue Size */
#define NS_CFG_ICACCEPT       0x00200000    /* Invalid Cell Accept */
#define NS_CFG_IGNOREGFC      0x00100000    /* Ignore General Flow Control */
#define NS_CFG_VPIBITS_MASK   0x000C0000    /* VPI/VCI Bits Size Select */
#define NS_CFG_RCTSIZE_MASK   0x00030000    /* Receive Connection Table Size */
#define NS_CFG_VCERRACCEPT    0x00008000    /* VPI/VCI Error Cell Accept */
#define NS_CFG_RXINT_MASK     0x00007000    /* End of Receive PDU Interrupt
                                               Handling */
#define NS_CFG_RAWIE          0x00000800    /* Raw Cell Qu' Interrupt Enable */
#define NS_CFG_RSQAFIE        0x00000400    /* Receive Queue Almost Full
                                               Interrupt Enable */
#define NS_CFG_RXRM           0x00000200    /* Receive RM Cells */
#define NS_CFG_TMRROIE        0x00000080    /* Timer Roll Over Interrupt
                                               Enable */
#define NS_CFG_TXEN           0x00000020    /* Transmit Operation Enable */
#define NS_CFG_TXIE           0x00000010    /* Transmit Status Interrupt
                                               Enable */
#define NS_CFG_TXURIE         0x00000008    /* Transmit Under-run Interrupt
                                               Enable */
#define NS_CFG_UMODE          0x00000004    /* Utopia Mode (cell/byte) Select */
#define NS_CFG_TSQFIE         0x00000002    /* Transmit Status Queue Full
                                               Interrupt Enable */
#define NS_CFG_PHYIE          0x00000001    /* PHY Interrupt Enable */

#define NS_CFG_SMBUFSIZE_48    0x00000000
#define NS_CFG_SMBUFSIZE_96    0x08000000
#define NS_CFG_SMBUFSIZE_240   0x10000000
#define NS_CFG_SMBUFSIZE_2048  0x18000000

#define NS_CFG_LGBUFSIZE_2048  0x00000000
#define NS_CFG_LGBUFSIZE_4096  0x02000000
#define NS_CFG_LGBUFSIZE_8192  0x04000000
#define NS_CFG_LGBUFSIZE_16384 0x06000000

#define NS_CFG_RSQSIZE_2048 0x00000000
#define NS_CFG_RSQSIZE_4096 0x00400000
#define NS_CFG_RSQSIZE_8192 0x00800000

#define NS_CFG_VPIBITS_0 0x00000000
#define NS_CFG_VPIBITS_1 0x00040000
#define NS_CFG_VPIBITS_2 0x00080000
#define NS_CFG_VPIBITS_8 0x000C0000

#define NS_CFG_RCTSIZE_4096_ENTRIES  0x00000000
#define NS_CFG_RCTSIZE_8192_ENTRIES  0x00010000
#define NS_CFG_RCTSIZE_16384_ENTRIES 0x00020000

#define NS_CFG_RXINT_NOINT   0x00000000
#define NS_CFG_RXINT_NODELAY 0x00001000
#define NS_CFG_RXINT_314US   0x00002000
#define NS_CFG_RXINT_624US   0x00003000
#define NS_CFG_RXINT_899US   0x00004000


/* NICStAR STATus bits ********************************************************/

#define NS_STAT_SFBQC_MASK 0xFF000000   /* hi 8 bits Small Buffer Queue Count */
#define NS_STAT_LFBQC_MASK 0x00FF0000   /* hi 8 bits Large Buffer Queue Count */
#define NS_STAT_TSIF       0x00008000   /* Transmit Status Queue Indicator */
#define NS_STAT_TXICP      0x00004000   /* Transmit Incomplete PDU */
#define NS_STAT_TSQF       0x00001000   /* Transmit Status Queue Full */
#define NS_STAT_TMROF      0x00000800   /* Timer Overflow */
#define NS_STAT_PHYI       0x00000400   /* PHY Device Interrupt */
#define NS_STAT_CMDBZ      0x00000200   /* Command Busy */
#define NS_STAT_SFBQF      0x00000100   /* Small Buffer Queue Full */
#define NS_STAT_LFBQF      0x00000080   /* Large Buffer Queue Full */
#define NS_STAT_RSQF       0x00000040   /* Receive Status Queue Full */
#define NS_STAT_EOPDU      0x00000020   /* End of PDU */
#define NS_STAT_RAWCF      0x00000010   /* Raw Cell Flag */
#define NS_STAT_SFBQE      0x00000008   /* Small Buffer Queue Empty */
#define NS_STAT_LFBQE      0x00000004   /* Large Buffer Queue Empty */
#define NS_STAT_RSQAF      0x00000002   /* Receive Status Queue Almost Full */

#define ns_stat_sfbqc_get(stat) (((stat) & NS_STAT_SFBQC_MASK) >> 23)
#define ns_stat_lfbqc_get(stat) (((stat) & NS_STAT_LFBQC_MASK) >> 15)



/* #defines which depend on other #defines ************************************/


#define NS_TST0 NS_TST_FRSCD
#define NS_TST1 (NS_TST_FRSCD + NS_TST_NUM_ENTRIES + 1)

#define NS_FRSCD (NS_TST1 + NS_TST_NUM_ENTRIES + 1)
#define NS_FRSCD_SIZE 12	/* 12 dwords */
#define NS_FRSCD_NUM ((NS_TST_FRSCD_END + 1 - NS_FRSCD) / NS_FRSCD_SIZE)

#if (NS_SMBUFSIZE == 48)
#define NS_CFG_SMBUFSIZE NS_CFG_SMBUFSIZE_48
#elif (NS_SMBUFSIZE == 96)
#define NS_CFG_SMBUFSIZE NS_CFG_SMBUFSIZE_96
#elif (NS_SMBUFSIZE == 240)
#define NS_CFG_SMBUFSIZE NS_CFG_SMBUFSIZE_240
#elif (NS_SMBUFSIZE == 2048)
#define NS_CFG_SMBUFSIZE NS_CFG_SMBUFSIZE_2048
#else
#error NS_SMBUFSIZE is incorrect in nicstar.h
#endif /* NS_SMBUFSIZE */

#if (NS_LGBUFSIZE == 2048)
#define NS_CFG_LGBUFSIZE NS_CFG_LGBUFSIZE_2048
#elif (NS_LGBUFSIZE == 4096)
#define NS_CFG_LGBUFSIZE NS_CFG_LGBUFSIZE_4096
#elif (NS_LGBUFSIZE == 8192)
#define NS_CFG_LGBUFSIZE NS_CFG_LGBUFSIZE_8192
#elif (NS_LGBUFSIZE == 16384)
#define NS_CFG_LGBUFSIZE NS_CFG_LGBUFSIZE_16384
#else
#error NS_LGBUFSIZE is incorrect in nicstar.h
#endif /* NS_LGBUFSIZE */

#if (NS_RSQSIZE == 2048)
#define NS_CFG_RSQSIZE NS_CFG_RSQSIZE_2048
#elif (NS_RSQSIZE == 4096)
#define NS_CFG_RSQSIZE NS_CFG_RSQSIZE_4096
#elif (NS_RSQSIZE == 8192)
#define NS_CFG_RSQSIZE NS_CFG_RSQSIZE_8192
#else
#error NS_RSQSIZE is incorrect in nicstar.h
#endif /* NS_RSQSIZE */

#if (NS_VPIBITS == 0)
#define NS_CFG_VPIBITS NS_CFG_VPIBITS_0
#elif (NS_VPIBITS == 1)
#define NS_CFG_VPIBITS NS_CFG_VPIBITS_1
#elif (NS_VPIBITS == 2)
#define NS_CFG_VPIBITS NS_CFG_VPIBITS_2
#elif (NS_VPIBITS == 8)
#define NS_CFG_VPIBITS NS_CFG_VPIBITS_8
#else
#error NS_VPIBITS is incorrect in nicstar.h
#endif /* NS_VPIBITS */

#ifdef RCQ_SUPPORT
#define NS_CFG_RAWIE_OPT NS_CFG_RAWIE
#else
#define NS_CFG_RAWIE_OPT 0x00000000
#endif /* RCQ_SUPPORT */

#ifdef ENABLE_TSQFIE
#define NS_CFG_TSQFIE_OPT NS_CFG_TSQFIE
#else
#define NS_CFG_TSQFIE_OPT 0x00000000
#endif /* ENABLE_TSQFIE */


/* PCI stuff ******************************************************************/

#ifndef PCI_VENDOR_ID_IDT
#define PCI_VENDOR_ID_IDT 0x111D
#endif /* PCI_VENDOR_ID_IDT */

#ifndef PCI_DEVICE_ID_IDT_IDT77201
#define PCI_DEVICE_ID_IDT_IDT77201 0x0001
#endif /* PCI_DEVICE_ID_IDT_IDT77201 */



/* Device driver structures ***************************************************/


struct ns_skb_cb {
	u32 buf_type;			/* BUF_SM/BUF_LG/BUF_NONE */
};

#define NS_SKB_CB(skb)	((struct ns_skb_cb *)((skb)->cb))

typedef struct tsq_info
{
   void *org;
   ns_tsi *base;
   ns_tsi *next;
   ns_tsi *last;
} tsq_info;


typedef struct scq_info
{
   void *org;
   ns_scqe *base;
   ns_scqe *last;
   ns_scqe *next;
   volatile ns_scqe *tail;		/* Not related to the nicstar register */
   unsigned num_entries;
   struct sk_buff **skb;		/* Pointer to an array of pointers
                                           to the sk_buffs used for tx */
   u32 scd;				/* SRAM address of the corresponding
                                           SCD */
   int tbd_count;			/* Only meaningful on variable rate */
   wait_queue_head_t scqfull_waitq;
   volatile char full;			/* SCQ full indicator */
   spinlock_t lock;			/* SCQ spinlock */
} scq_info;



typedef struct rsq_info
{
   void *org;
   ns_rsqe *base;
   ns_rsqe *next;
   ns_rsqe *last;
} rsq_info;


typedef struct skb_pool
{
   volatile int count;			/* number of buffers in the queue */
   struct sk_buff_head queue;
} skb_pool;

/* NOTE: for small and large buffer pools, the count is not used, as the
         actual value used for buffer management is the one read from the
	 card. */


typedef struct vc_map
{
   volatile unsigned int tx:1;				/* TX vc? */
   volatile unsigned int rx:1;				/* RX vc? */
   struct atm_vcc *tx_vcc, *rx_vcc;
   struct sk_buff *rx_iov;		/* RX iovector skb */
   scq_info *scq;			/* To keep track of the SCQ */
   u32 cbr_scd;				/* SRAM address of the corresponding
               				   SCD. 0x00000000 for UBR/VBR/ABR */
   int tbd_count;
} vc_map;


struct ns_skb_data
{
	struct atm_vcc *vcc;
	int iovcnt;
};

#define NS_SKB(skb) (((struct ns_skb_data *) (skb)->cb))


typedef struct ns_dev
{
   int index;				/* Card ID to the device driver */
   int sram_size;			/* In k x 32bit words. 32 or 128 */
   void __iomem *membase;		/* Card's memory base address */
   unsigned long max_pcr;
   int rct_size;			/* Number of entries */
   int vpibits;
   int vcibits;
   struct pci_dev *pcidev;
   struct atm_dev *atmdev;
   tsq_info tsq;
   rsq_info rsq;
   scq_info *scq0, *scq1, *scq2;	/* VBR SCQs */
   skb_pool sbpool;			/* Small buffers */
   skb_pool lbpool;			/* Large buffers */
   skb_pool hbpool;			/* Pre-allocated huge buffers */
   skb_pool iovpool;			/* iovector buffers */
   volatile int efbie;			/* Empty free buf. queue int. enabled */
   volatile u32 tst_addr;		/* SRAM address of the TST in use */
   volatile int tst_free_entries;
   vc_map vcmap[NS_MAX_RCTSIZE];
   vc_map *tste2vc[NS_TST_NUM_ENTRIES];
   vc_map *scd2vc[NS_FRSCD_NUM];
   buf_nr sbnr;
   buf_nr lbnr;
   buf_nr hbnr;
   buf_nr iovnr;
   int sbfqc;
   int lbfqc;
   u32 sm_handle;
   u32 sm_addr;
   u32 lg_handle;
   u32 lg_addr;
   struct sk_buff *rcbuf;		/* Current raw cell buffer */
   u32 rawch;				/* Raw cell queue head */
   unsigned intcnt;			/* Interrupt counter */
   spinlock_t int_lock;		/* Interrupt lock */
   spinlock_t res_lock;		/* Card resource lock */
} ns_dev;


   /* NOTE: Each tste2vc entry relates a given TST entry to the corresponding
            CBR vc. If the entry is not allocated, it must be NULL.
	    
	    There are two TSTs so the driver can modify them on the fly
	    without stopping the transmission.
	    
	    scd2vc allows us to find out unused fixed rate SCDs, because
	    they must have a NULL pointer here. */


#endif /* _LINUX_NICSTAR_H_ */
