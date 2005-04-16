/*
 * Linux driver for HYSDN cards
 * common definitions for both sides of the bus:
 * - conventions both spoolers must know
 * - channel numbers agreed upon
 *
 * Author    M. Steinkopf
 * Copyright 1999 by M. Steinkopf
 *
 * This software may be used and distributed according to the terms
 * of the GNU General Public License, incorporated herein by reference.
 *
 */

#ifndef __INCE1PC_H__
#define __INCE1PC_H__

/*  basic scalar definitions have same meanning,
 *  but their declaration location depends on environment
 */ 

/*--------------------------------------channel numbers---------------------*/ 
#define CHAN_SYSTEM     0x0001      /* system channel (spooler to spooler) */
#define CHAN_ERRLOG     0x0005      /* error logger */
#define CHAN_CAPI       0x0064      /* CAPI interface */
#define CHAN_NDIS_DATA  0x1001      /* NDIS data transfer */

/*--------------------------------------POF ready msg-----------------------*/ 
	    /* NOTE: after booting POF sends system ready message to PC: */ 
#define RDY_MAGIC       0x52535953UL    /* 'SYSR' reversed */
#define RDY_MAGIC_SIZE  4               /* size in bytes */

#define MAX_N_TOK_BYTES 255

#define MIN_RDY_MSG_SIZE    RDY_MAGIC_SIZE
#define MAX_RDY_MSG_SIZE    (RDY_MAGIC_SIZE+MAX_N_TOK_BYTES)

#define SYSR_TOK_END            0
#define SYSR_TOK_B_CHAN         1   /* nr. of B-Channels;   DataLen=1; def: 2 */
#define SYSR_TOK_FAX_CHAN       2   /* nr. of FAX Channels; DataLen=1; def: 0 */
#define SYSR_TOK_MAC_ADDR       3   /* MAC-Address; DataLen=6; def: auto */
#define SYSR_TOK_ESC            255 /* undefined data size yet */
			    /* default values, if not corrected by token: */ 
#define SYSR_TOK_B_CHAN_DEF     2   /* assume 2 B-Channels */
#define SYSR_TOK_FAX_CHAN_DEF   1   /* assume 1 FAX Channel */

/*  syntax of new SYSR token stream:
 *  channel: CHAN_SYSTEM
 *  msgsize: MIN_RDY_MSG_SIZE <= x <= MAX_RDY_MSG_SIZE
 *           RDY_MAGIC_SIZE   <= x <= (RDY_MAGIC_SIZE+MAX_N_TOK_BYTES)
 *  msg    : 0 1 2 3 {4 5 6 ..}
 *           S Y S R  MAX_N_TOK_BYTES bytes of TokenStream
 *
 *  TokenStream     :=   empty
 *                     | {NonEndTokenChunk} EndToken RotlCRC
 *  NonEndTokenChunk:= NonEndTokenId DataLen [Data]
 *  NonEndTokenId   := 0x01 .. 0xFE                 1 BYTE
 *  DataLen         := 0x00 .. 0xFF                 1 BYTE
 *  Data            := DataLen bytes
 *  EndToken        := 0x00
 *  RotlCRC         := special 1 byte CRC over all NonEndTokenChunk bytes
 *                     s. RotlCRC algorithm
 *
 *  RotlCRC algorithm:
 *      ucSum= 0                        1 uchar
 *      for all NonEndTokenChunk bytes:
 *          ROTL(ucSum,1)               rotate left by 1
 *          ucSum += Char;              add current byte with swap around
 *      RotlCRC= ~ucSum;                invert all bits for result
 *
 *  note:
 *  - for 16-bit FIFO add padding 0 byte to achieve even token data bytes!
 */ 

/*--------------------------------------error logger------------------------*/ 
					    /* note: pof needs final 0 ! */ 
#define ERRLOG_CMD_REQ          "ERRLOG ON"
#define ERRLOG_CMD_REQ_SIZE     10              /* with final 0 byte ! */
#define ERRLOG_CMD_STOP         "ERRLOG OFF"
#define ERRLOG_CMD_STOP_SIZE    11              /* with final 0 byte ! */

#define ERRLOG_ENTRY_SIZE       64      /* sizeof(tErrLogEntry) */
					/* remaining text size = 55 */ 
#define ERRLOG_TEXT_SIZE    (ERRLOG_ENTRY_SIZE-2*4-1)

typedef struct ErrLogEntry_tag {
	
/*00 */ ulong ulErrType;
	
/*04 */ ulong ulErrSubtype;
	
/*08 */ uchar ucTextSize;
	
	/*09 */ uchar ucText[ERRLOG_TEXT_SIZE];
	/* ASCIIZ of len ucTextSize-1 */
	
/*40 */ 
} tErrLogEntry;


#if defined(__TURBOC__)
#if sizeof(tErrLogEntry) != ERRLOG_ENTRY_SIZE
#error size of tErrLogEntry != ERRLOG_ENTRY_SIZE
#endif				/*  */
#endif				/*  */

/*--------------------------------------DPRAM boot spooler------------------*/ 
				/*  this is the struture used between pc and
				 *  hyperstone to exchange boot data
				 */ 
#define DPRAM_SPOOLER_DATA_SIZE 0x20
typedef struct DpramBootSpooler_tag {
	
/*00 */ uchar Len;
	
/*01 */ volatile uchar RdPtr;
	
/*02 */ uchar WrPtr;
	
/*03 */ uchar Data[DPRAM_SPOOLER_DATA_SIZE];
	
/*23 */ 
} tDpramBootSpooler;


#define DPRAM_SPOOLER_MIN_SIZE  5       /* Len+RdPtr+Wrptr+2*data */
#define DPRAM_SPOOLER_DEF_SIZE  0x23    /* current default size   */

/*--------------------------------------HYCARD/ERGO DPRAM SoftUart----------*/ 
				    /* at DPRAM offset 0x1C00: */ 
#define SIZE_RSV_SOFT_UART  0x1B0   /* 432 bytes reserved for SoftUart */


#endif	/* __INCE1PC_H__ */
