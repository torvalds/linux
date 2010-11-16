//------------------------------------------------------------------------------
// Copyright (c) 2009-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//

/* This file contains shared definitions for the host/target endpoint ping test */

#ifndef EPPING_TEST_H_
#define EPPING_TEST_H_

#ifndef ATH_TARGET
#include "athstartpack.h"
#endif

    /* alignment to 4-bytes */
#define EPPING_ALIGNMENT_PAD  (((sizeof(HTC_FRAME_HDR) + 3) & (~0x3)) - sizeof(HTC_FRAME_HDR))

#ifndef A_OFFSETOF
#define A_OFFSETOF(type,field) (int)(&(((type *)NULL)->field))
#endif

#define EPPING_RSVD_FILL                  0xCC

#define HCI_RSVD_EXPECTED_PKT_TYPE_RECV_OFFSET  7 
  
typedef PREPACK struct {    
    A_UINT8     _HCIRsvd[8];           /* reserved for HCI packet header (GMBOX) testing */
    A_UINT8     StreamEcho_h;          /* stream no. to echo this packet on (filled by host) */
    A_UINT8     StreamEchoSent_t;      /* stream no. packet was echoed to (filled by target)
                                          When echoed: StreamEchoSent_t == StreamEcho_h */
    A_UINT8     StreamRecv_t;          /* stream no. that target received this packet on (filled by target) */
    A_UINT8     StreamNo_h;            /* stream number to send on (filled by host) */   
    A_UINT8     Magic_h[4];            /* magic number to filter for this packet on the host*/
    A_UINT8     _rsvd[6];              /* reserved fields that must be set to a "reserved" value
                                          since this packet maps to a 14-byte ethernet frame we want 
                                          to make sure ethertype field is set to something unknown */
                                          
    A_UINT8     _pad[2];               /* padding for alignment */                      
    A_UINT8     TimeStamp[8];          /* timestamp of packet (host or target) */
    A_UINT32    HostContext_h;         /* 4 byte host context, target echos this back */
    A_UINT32    SeqNo;                 /* sequence number (set by host or target) */   
    A_UINT16    Cmd_h;                 /* ping command (filled by host) */  
    A_UINT16    CmdFlags_h;            /* optional flags */
    A_UINT8     CmdBuffer_h[8];        /* buffer for command (host -> target) */
    A_UINT8     CmdBuffer_t[8];        /* buffer for command (target -> host) */  
    A_UINT16    DataLength;            /* length of data */
    A_UINT16    DataCRC;               /* 16 bit CRC of data */
    A_UINT16    HeaderCRC;             /* header CRC (fields : StreamNo_h to end, minus HeaderCRC) */                       
} POSTPACK EPPING_HEADER;

#define EPPING_PING_MAGIC_0               0xAA
#define EPPING_PING_MAGIC_1               0x55
#define EPPING_PING_MAGIC_2               0xCE
#define EPPING_PING_MAGIC_3               0xEC



#define IS_EPPING_PACKET(pPkt)   (((pPkt)->Magic_h[0] == EPPING_PING_MAGIC_0) && \
                                  ((pPkt)->Magic_h[1] == EPPING_PING_MAGIC_1) && \
                                  ((pPkt)->Magic_h[2] == EPPING_PING_MAGIC_2) && \
                                  ((pPkt)->Magic_h[3] == EPPING_PING_MAGIC_3))

#define SET_EPPING_PACKET_MAGIC(pPkt) { (pPkt)->Magic_h[0] = EPPING_PING_MAGIC_0; \
                                        (pPkt)->Magic_h[1] = EPPING_PING_MAGIC_1; \
                                        (pPkt)->Magic_h[2] = EPPING_PING_MAGIC_2; \
                                        (pPkt)->Magic_h[3] = EPPING_PING_MAGIC_3;}
                                                                            
#define CMD_FLAGS_DATA_CRC            (1 << 0)  /* DataCRC field is valid */
#define CMD_FLAGS_DELAY_ECHO          (1 << 1)  /* delay the echo of the packet */
#define CMD_FLAGS_NO_DROP             (1 << 2)  /* do not drop at HTC layer no matter what the stream is */

#define IS_EPING_PACKET_NO_DROP(pPkt)  ((pPkt)->CmdFlags_h & CMD_FLAGS_NO_DROP)

#define EPPING_CMD_ECHO_PACKET          1   /* echo packet test */
#define EPPING_CMD_RESET_RECV_CNT       2   /* reset recv count */
#define EPPING_CMD_CAPTURE_RECV_CNT     3   /* fetch recv count, 4-byte count returned in CmdBuffer_t */
#define EPPING_CMD_NO_ECHO              4   /* non-echo packet test (tx-only) */
#define EPPING_CMD_CONT_RX_START        5   /* continous RX packets, parameters are in CmdBuffer_h */
#define EPPING_CMD_CONT_RX_STOP         6   /* stop continuous RX packet transmission */

    /* test command parameters may be no more than 8 bytes */
typedef PREPACK struct {    
    A_UINT16  BurstCnt;       /* number of packets to burst together (for HTC 2.1 testing) */
    A_UINT16  PacketLength;   /* length of packet to generate including header */      
    A_UINT16  Flags;          /* flags */

#define EPPING_CONT_RX_DATA_CRC     (1 << 0)  /* Add CRC to all data */
#define EPPING_CONT_RX_RANDOM_DATA  (1 << 1)  /* randomize the data pattern */
#define EPPING_CONT_RX_RANDOM_LEN   (1 << 2)  /* randomize the packet lengths */          
} POSTPACK EPPING_CONT_RX_PARAMS;

#define EPPING_HDR_CRC_OFFSET    A_OFFSETOF(EPPING_HEADER,StreamNo_h)
#define EPPING_HDR_BYTES_CRC     (sizeof(EPPING_HEADER) - EPPING_HDR_CRC_OFFSET - (sizeof(A_UINT16)))

#define HCI_TRANSPORT_STREAM_NUM  16  /* this number is higher than the define WMM AC classes so we
                                         can use this to distinguish packets */

#ifndef ATH_TARGET
#include "athendpack.h"
#endif
    
    
#endif /*EPPING_TEST_H_*/
