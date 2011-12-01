// ------------------------------------------------------------------
// Copyright (c) 2004-2007 Atheros Corporation.  All rights reserved.
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
// ------------------------------------------------------------------
//===================================================================
// Author(s): ="Atheros"
//===================================================================


#ifndef _BRIDGE_CHAIN_RX_REG_REG_H_
#define _BRIDGE_CHAIN_RX_REG_REG_H_

#define DESC_START_ADDRESS_ADDRESS               0x00000000
#define DESC_START_ADDRESS_OFFSET                0x00000000
#define DESC_START_ADDRESS_ADDRESS_MSB           31
#define DESC_START_ADDRESS_ADDRESS_LSB           0
#define DESC_START_ADDRESS_ADDRESS_MASK          0xffffffff
#define DESC_START_ADDRESS_ADDRESS_GET(x)        (((x) & DESC_START_ADDRESS_ADDRESS_MASK) >> DESC_START_ADDRESS_ADDRESS_LSB)
#define DESC_START_ADDRESS_ADDRESS_SET(x)        (((x) << DESC_START_ADDRESS_ADDRESS_LSB) & DESC_START_ADDRESS_ADDRESS_MASK)

#define DMA_START_ADDRESS                        0x00000004
#define DMA_START_OFFSET                         0x00000004
#define DMA_START_RESTART_MSB                    4
#define DMA_START_RESTART_LSB                    4
#define DMA_START_RESTART_MASK                   0x00000010
#define DMA_START_RESTART_GET(x)                 (((x) & DMA_START_RESTART_MASK) >> DMA_START_RESTART_LSB)
#define DMA_START_RESTART_SET(x)                 (((x) << DMA_START_RESTART_LSB) & DMA_START_RESTART_MASK)
#define DMA_START_START_MSB                      0
#define DMA_START_START_LSB                      0
#define DMA_START_START_MASK                     0x00000001
#define DMA_START_START_GET(x)                   (((x) & DMA_START_START_MASK) >> DMA_START_START_LSB)
#define DMA_START_START_SET(x)                   (((x) << DMA_START_START_LSB) & DMA_START_START_MASK)

#define BURST_SIZE_ADDRESS                       0x00000008
#define BURST_SIZE_OFFSET                        0x00000008
#define BURST_SIZE_BURST_MSB                     1
#define BURST_SIZE_BURST_LSB                     0
#define BURST_SIZE_BURST_MASK                    0x00000003
#define BURST_SIZE_BURST_GET(x)                  (((x) & BURST_SIZE_BURST_MASK) >> BURST_SIZE_BURST_LSB)
#define BURST_SIZE_BURST_SET(x)                  (((x) << BURST_SIZE_BURST_LSB) & BURST_SIZE_BURST_MASK)

#define PKT_OFFSET_ADDRESS                       0x0000000c
#define PKT_OFFSET_OFFSET                        0x0000000c
#define PKT_OFFSET_OFFSET_MSB                    7
#define PKT_OFFSET_OFFSET_LSB                    0
#define PKT_OFFSET_OFFSET_MASK                   0x000000ff
#define PKT_OFFSET_OFFSET_GET(x)                 (((x) & PKT_OFFSET_OFFSET_MASK) >> PKT_OFFSET_OFFSET_LSB)
#define PKT_OFFSET_OFFSET_SET(x)                 (((x) << PKT_OFFSET_OFFSET_LSB) & PKT_OFFSET_OFFSET_MASK)

#define CHECKSUM_ADDRESS                         0x00000010
#define CHECKSUM_OFFSET                          0x00000010
#define CHECKSUM_UDP_MSB                         1
#define CHECKSUM_UDP_LSB                         1
#define CHECKSUM_UDP_MASK                        0x00000002
#define CHECKSUM_UDP_GET(x)                      (((x) & CHECKSUM_UDP_MASK) >> CHECKSUM_UDP_LSB)
#define CHECKSUM_UDP_SET(x)                      (((x) << CHECKSUM_UDP_LSB) & CHECKSUM_UDP_MASK)
#define CHECKSUM_TCP_MSB                         0
#define CHECKSUM_TCP_LSB                         0
#define CHECKSUM_TCP_MASK                        0x00000001
#define CHECKSUM_TCP_GET(x)                      (((x) & CHECKSUM_TCP_MASK) >> CHECKSUM_TCP_LSB)
#define CHECKSUM_TCP_SET(x)                      (((x) << CHECKSUM_TCP_LSB) & CHECKSUM_TCP_MASK)

#define DBG_RX_ADDRESS                           0x00000014
#define DBG_RX_OFFSET                            0x00000014
#define DBG_RX_STATE_MSB                         3
#define DBG_RX_STATE_LSB                         0
#define DBG_RX_STATE_MASK                        0x0000000f
#define DBG_RX_STATE_GET(x)                      (((x) & DBG_RX_STATE_MASK) >> DBG_RX_STATE_LSB)
#define DBG_RX_STATE_SET(x)                      (((x) << DBG_RX_STATE_LSB) & DBG_RX_STATE_MASK)

#define DBG_RX_CUR_ADDR_ADDRESS                  0x00000018
#define DBG_RX_CUR_ADDR_OFFSET                   0x00000018
#define DBG_RX_CUR_ADDR_ADDR_MSB                 31
#define DBG_RX_CUR_ADDR_ADDR_LSB                 0
#define DBG_RX_CUR_ADDR_ADDR_MASK                0xffffffff
#define DBG_RX_CUR_ADDR_ADDR_GET(x)              (((x) & DBG_RX_CUR_ADDR_ADDR_MASK) >> DBG_RX_CUR_ADDR_ADDR_LSB)
#define DBG_RX_CUR_ADDR_ADDR_SET(x)              (((x) << DBG_RX_CUR_ADDR_ADDR_LSB) & DBG_RX_CUR_ADDR_ADDR_MASK)

#define DATA_SWAP_ADDRESS                        0x0000001c
#define DATA_SWAP_OFFSET                         0x0000001c
#define DATA_SWAP_SWAPD_MSB                      1
#define DATA_SWAP_SWAPD_LSB                      1
#define DATA_SWAP_SWAPD_MASK                     0x00000002
#define DATA_SWAP_SWAPD_GET(x)                   (((x) & DATA_SWAP_SWAPD_MASK) >> DATA_SWAP_SWAPD_LSB)
#define DATA_SWAP_SWAPD_SET(x)                   (((x) << DATA_SWAP_SWAPD_LSB) & DATA_SWAP_SWAPD_MASK)
#define DATA_SWAP_SWAP_MSB                       0
#define DATA_SWAP_SWAP_LSB                       0
#define DATA_SWAP_SWAP_MASK                      0x00000001
#define DATA_SWAP_SWAP_GET(x)                    (((x) & DATA_SWAP_SWAP_MASK) >> DATA_SWAP_SWAP_LSB)
#define DATA_SWAP_SWAP_SET(x)                    (((x) << DATA_SWAP_SWAP_LSB) & DATA_SWAP_SWAP_MASK)


#ifndef __ASSEMBLER__

typedef struct bridge_chain_rx_reg_reg_s {
  volatile unsigned int desc_start_address;
  volatile unsigned int dma_start;
  volatile unsigned int burst_size;
  volatile unsigned int pkt_offset;
  volatile unsigned int checksum;
  volatile unsigned int dbg_rx;
  volatile unsigned int dbg_rx_cur_addr;
  volatile unsigned int data_swap;
} bridge_chain_rx_reg_reg_t;

#endif /* __ASSEMBLER__ */

#endif /* _BRIDGE_CHAIN_RX_REG_H_ */
