/*
 * $Id: slicdump.h,v 1.2 2006/03/27 15:09:57 mook Exp $
 *
 * Copyright (c) 2000-2002 Alacritech, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY ALACRITECH, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL ALACRITECH, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * NO LICENSE TO ANY ALACRITECH PATENT CLAIM IS GRANTED BY ANY COPYRIGHT
 * LICENSE TO THIS OR OTHER SOFTWARE. THIS SOFTWARE MAY BE COVERED BY
 * ALACRITECH PATENTS INCLUDING BUT NOT LIMITED TO U.S. PATENT NOS. 6,226,680,
 * 6,247,060, 6,334,153, 6,389,479, 6,393,487, 6,427,171, 6,427,173
 * and 6,434,620.
 * THIS SOFTWARE IS NOT SUBJECT TO THE GNU GENERAL PUBLIC LICENSE (GPL).
 *
 * The views and conclusions contained in the software and
 * documentation are those of the authors and should not be
 * interpreted as representing official policies, either
 * expressed or implied, of Alacritech, Inc.
 */
#ifndef _SLIC_DUMP_H_
#define _SLIC_DUMP_H_

#define DEBUG_SUCCESS   0

/***********************************************************************
 *
 * Utility processor register locations
 *
 **********************************************************************/
#define UTILITY_RESET       0x0
#define UTILITY_ISP_ADDR    0x4     /* Interrupt status Pointer */
#define UTILITY_ISR_ADDR    0x8     /* Interrupt status Register */
#define UTILITY_ICR_ADDR    0xc     /* Interrupt Control Register */
#define UTILITY_CPR_ADDR    0x10    /* Command Pointer Register */
#define UTILITY_DPR_ADDR    0x14    /* Data Pointer Register */
#define UTILITY_DMP_TRQ     0x18    /* Dump queue onto ALU for analyser */
#define UTILITY_UPP_ADDR    0x1c    /* Bits 63-32 of cmd/data pointer */

/***********************************************************************
 *
 * INIC status register bits
 *
 ***********************************************************************/
#define SLIC_ISR_CC         0x10000000  /* Command complete - synchronous */
#define SLIC_ISR_ERR        0x01000000  /* Command Error - synchronous */
#define SLIC_ISR_CMD_MASK   0x11000000  /* Command status mask */
#define SLIC_ISR_TPH        0x00080000  /* Transmit processor halted - async */
#define SLIC_ISR_RPH        0x00040000  /* Receive processor halted - async */

/***********************************************************************
 *
 * INIC Control register values
 *
 ***********************************************************************/
#define SLIC_ICR_OFF        0           /* Interrupts disabled */
#define SLIC_ICR_ON         1           /* Interrupts enabled */
#define SLIC_ICR_MASK       2           /* Interrupts masked */

#define WRITE_DREG(reg, value, flush)                           \
{                                                               \
    writel((value), (reg));                                     \
    if ((flush)) {                                               \
	mb();                                                   \
    }                                                           \
}

/************************************************************************
 *
 * Command Format
 *
 * Each command contains a command byte which is defined as follows:
 *
 *  bits:   7-3     2       1-0
 *      ----------------------------------------------
 *      command     Alt. Proc   Processor
 *
 ************************************************************************/

/*
 * Macro to create the command byte given the command, Alt. Proc, and
 * Processor values.  Note that the macro assumes that the values are
 * preshifted.  That is, the values for alt. proc are 0 for transmit and
 * 4 for receive.
 */
#define COMMAND_BYTE(command, alt_proc, proc) ((command) | (alt_proc) | (proc))

/*
 * Command values
 */
#define CMD_HALT        0x0     /* Send a halt to the INIC */
#define CMD_RUN         0x8     /* Start the halted INIC */
#define CMD_STEP        0x10    /* Single step the inic */
#define CMD_BREAK       0x18    /* Set a breakpoint - 8 byte command */
#define CMD_RESET_BREAK 0x20    /* Reset a breakpoint - 8 byte cmd */
#define CMD_DUMP        0x28    /* Dump INIC memory - 8 byte command */
#define CMD_LOAD        0x30    /* Load INIC memory - 8 byte command */
#define CMD_MAP         0x38    /* Map out a ROM instruction - 8 BC */
#define CMD_CAM_OPS     0x38    /* perform ops on specific CAM */
#define CMD_XMT         0x40    /* Transmit frame */
#define CMD_RCV         0x48    /* Receive frame */

/*
 * Alt. Proc values
 *
 * When the proc value is set to the utility processor, the Alt. Proc
 * specifies which processor handles the debugging.
 */
#define ALT_PROC_TRANSMIT   0x0
#define ALT_PROC_RECEIVE    0x4

/*
 * Proc values
 */
#define PROC_INVALID        0x0
#define PROC_NONE           0x0  /* Gigabit use */
#define PROC_TRANSMIT       0x1
#define PROC_RECEIVE        0x2
#define PROC_UTILITY        0x3

/******************************************************************
 *
 * 8 byte command structure definitions
 *
 ******************************************************************/

/*
 * Break and Reset Break command structure
 */
typedef struct _BREAK {
    uchar     command;    /* Command word defined above */
    uchar     resvd;
    ushort    count;      /* Number of executions before break */
    ulong32   addr;       /* Address of break point */
} BREAK, *PBREAK;

/*
 * Dump and Load command structure
 */
typedef struct _dump_cmd {
    uchar     cmd;        /* Command word defined above */
    uchar     desc;       /* Descriptor values - defined below */
    ushort    count;      /* number of 4 byte words to be transferred */
    ulong32   addr;       /* start address of dump or load */
} dump_cmd_t, *pdump_cmd_t;

/*
 * Receive or Transmit a frame.
 */
typedef struct _RCV_OR_XMT_FRAME {
    uchar     command;    /* Command word defined above */
    uchar     MacId;      /* Mac ID of interface - transmit only */
    ushort    count;      /* Length of frame in bytes */
    ulong32   pad;        /* not used */
} RCV_OR_XMT_FRAME, *PRCV_OR_XMT_FRAME;

/*
 * Values of desc field in DUMP_OR_LOAD structure
 */
#define DESC_RFILE          0x0     /* Register file */
#define DESC_SRAM           0x1     /* SRAM */
#define DESC_DRAM           0x2     /* DRAM */
#define DESC_QUEUE          0x3     /* queues */
#define DESC_REG            0x4     /* General registers (pc, status, etc) */
#define DESC_SENSE          0x5     /* Sense register */

/* Descriptor field definitions for CMD_DUMP_CAM */
#define DUMP_CAM_A              0
#define DUMP_CAM_B              1               /* unused at present */
#define DUMP_CAM_C              2
#define DUMP_CAM_D              3
#define SEARCH_CAM_A            4
#define SEARCH_CAM_C            5

/*
 * Map command to replace a command in ROM with a command in WCS
 */
typedef struct _MAP {
    uchar   command;    /* Command word defined above */
    uchar   not_used[3];
    ushort  map_to;     /* Instruction address in WCS */
    ushort  map_out;    /* Instruction address in ROM */
} MAP, *PMAP;

/*
 * Misc definitions
 */
#define SLIC_MAX_QUEUE       32 /* Total # of queues on the INIC (0-31)*/
#define SLIC_4MAX_REG       512 /* Total # of 4-port file-registers    */
#define SLIC_1MAX_REG       384 /* Total # of file-registers           */
#define SLIC_GBMAX_REG     1024 /* Total # of Gbit file-registers      */
#define SLIC_NUM_REG         32 /* non-file-registers = NUM_REG in tm-simba.h */
#define SLIC_GB_CAMA_SZE     32
#define SLIC_GB_CAMB_SZE     16
#define SLIC_GB_CAMAB_SZE    32
#define SLIC_GB_CAMC_SZE     16
#define SLIC_GB_CAMD_SZE     16
#define SLIC_GB_CAMCD_SZE    32

/*
 * Coredump header structure
 */
typedef struct _CORE_Q {
    ulong32   queueOff;           /* Offset of queue */
    ulong32   queuesize;          /* size of queue */
} CORE_Q;

#define DRIVER_NAME_SIZE    32

typedef struct _sliccore_hdr_t {
    uchar   driver_version[DRIVER_NAME_SIZE];    /* Driver version string */
    ulong32   RcvRegOff;          /* Offset of receive registers */
    ulong32   RcvRegsize;         /* size of receive registers */
    ulong32   XmtRegOff;          /* Offset of transmit registers */
    ulong32   XmtRegsize;         /* size of transmit registers */
    ulong32   FileRegOff;         /* Offset of register file */
    ulong32   FileRegsize;        /* size of register file */
    ulong32   SramOff;            /* Offset of Sram */
    ulong32   Sramsize;           /* size of Sram */
    ulong32   DramOff;            /* Offset of Dram */
    ulong32   Dramsize;           /* size of Dram */
    CORE_Q    queues[SLIC_MAX_QUEUE]; /* size and offsets of queues */
    ulong32   CamAMOff;           /* Offset of CAM A contents */
    ulong32   CamASize;           /* Size of Cam A */
    ulong32   CamBMOff;           /* Offset of CAM B contents */
    ulong32   CamBSize;           /* Size of Cam B */
    ulong32   CamCMOff;           /* Offset of CAM C contents */
    ulong32   CamCSize;           /* Size of Cam C */
    ulong32   CamDMOff;           /* Offset of CAM D contents */
    ulong32   CamDSize;           /* Size of Cam D */
} sliccore_hdr_t, *p_sliccore_hdr_t;

/*
 * definitions needed for our kernel-mode gdb stub.
 */
/***********************************************************************
 *
 * Definitions & Typedefs
 *
 **********************************************************************/
#define BUFMAX      0x20000 /* 128k - size of input/output buffer */
#define BUFMAXP2    5       /* 2**5 (32) 4K pages */

#define IOCTL_SIMBA_BREAK           _IOW('s', 0, unsigned long)
/* #define IOCTL_SIMBA_INIT            _IOW('s', 1, unsigned long) */
#define IOCTL_SIMBA_KILL_TGT_PROC   _IOW('s', 2, unsigned long)

/***********************************************************************
 *
 * Global variables
 *
 ***********************************************************************/

#define THREADRECEIVE   1   /* bit 0 of StoppedThreads */
#define THREADTRANSMIT  2   /* bit 1 of StoppedThreads */
#define THREADBOTH      3   /* bit 0 and 1.. */

#endif  /*  _SLIC_DUMP_H  */
