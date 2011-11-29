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


#ifndef _BRIDGE_CHAIN_GMAC_0_RX_REG_REG_H_
#define _BRIDGE_CHAIN_GMAC_0_RX_REG_REG_H_

#define GMAC_RX_0_DESC_START_ADDRESS_ADDRESS     0x00000000
#define GMAC_RX_0_DESC_START_ADDRESS_OFFSET      0x00000000
#define GMAC_RX_0_DESC_START_ADDRESS_ADDRESS_MSB 31
#define GMAC_RX_0_DESC_START_ADDRESS_ADDRESS_LSB 0
#define GMAC_RX_0_DESC_START_ADDRESS_ADDRESS_MASK 0xffffffff
#define GMAC_RX_0_DESC_START_ADDRESS_ADDRESS_GET(x) (((x) & GMAC_RX_0_DESC_START_ADDRESS_ADDRESS_MASK) >> GMAC_RX_0_DESC_START_ADDRESS_ADDRESS_LSB)
#define GMAC_RX_0_DESC_START_ADDRESS_ADDRESS_SET(x) (((x) << GMAC_RX_0_DESC_START_ADDRESS_ADDRESS_LSB) & GMAC_RX_0_DESC_START_ADDRESS_ADDRESS_MASK)

#define GMAC_RX_0_DMA_START_ADDRESS              0x00000004
#define GMAC_RX_0_DMA_START_OFFSET               0x00000004
#define GMAC_RX_0_DMA_START_RESTART_MSB          4
#define GMAC_RX_0_DMA_START_RESTART_LSB          4
#define GMAC_RX_0_DMA_START_RESTART_MASK         0x00000010
#define GMAC_RX_0_DMA_START_RESTART_GET(x)       (((x) & GMAC_RX_0_DMA_START_RESTART_MASK) >> GMAC_RX_0_DMA_START_RESTART_LSB)
#define GMAC_RX_0_DMA_START_RESTART_SET(x)       (((x) << GMAC_RX_0_DMA_START_RESTART_LSB) & GMAC_RX_0_DMA_START_RESTART_MASK)
#define GMAC_RX_0_DMA_START_START_MSB            0
#define GMAC_RX_0_DMA_START_START_LSB            0
#define GMAC_RX_0_DMA_START_START_MASK           0x00000001
#define GMAC_RX_0_DMA_START_START_GET(x)         (((x) & GMAC_RX_0_DMA_START_START_MASK) >> GMAC_RX_0_DMA_START_START_LSB)
#define GMAC_RX_0_DMA_START_START_SET(x)         (((x) << GMAC_RX_0_DMA_START_START_LSB) & GMAC_RX_0_DMA_START_START_MASK)

#define GMAC_RX_0_BURST_SIZE_ADDRESS             0x00000008
#define GMAC_RX_0_BURST_SIZE_OFFSET              0x00000008
#define GMAC_RX_0_BURST_SIZE_BURST_MSB           1
#define GMAC_RX_0_BURST_SIZE_BURST_LSB           0
#define GMAC_RX_0_BURST_SIZE_BURST_MASK          0x00000003
#define GMAC_RX_0_BURST_SIZE_BURST_GET(x)        (((x) & GMAC_RX_0_BURST_SIZE_BURST_MASK) >> GMAC_RX_0_BURST_SIZE_BURST_LSB)
#define GMAC_RX_0_BURST_SIZE_BURST_SET(x)        (((x) << GMAC_RX_0_BURST_SIZE_BURST_LSB) & GMAC_RX_0_BURST_SIZE_BURST_MASK)

#define GMAC_RX_0_PKT_OFFSET_ADDRESS             0x0000000c
#define GMAC_RX_0_PKT_OFFSET_OFFSET              0x0000000c
#define GMAC_RX_0_PKT_OFFSET_OFFSET_MSB          7
#define GMAC_RX_0_PKT_OFFSET_OFFSET_LSB          0
#define GMAC_RX_0_PKT_OFFSET_OFFSET_MASK         0x000000ff
#define GMAC_RX_0_PKT_OFFSET_OFFSET_GET(x)       (((x) & GMAC_RX_0_PKT_OFFSET_OFFSET_MASK) >> GMAC_RX_0_PKT_OFFSET_OFFSET_LSB)
#define GMAC_RX_0_PKT_OFFSET_OFFSET_SET(x)       (((x) << GMAC_RX_0_PKT_OFFSET_OFFSET_LSB) & GMAC_RX_0_PKT_OFFSET_OFFSET_MASK)

#define GMAC_RX_0_CHECKSUM_ADDRESS               0x00000010
#define GMAC_RX_0_CHECKSUM_OFFSET                0x00000010
#define GMAC_RX_0_CHECKSUM_UDP_MSB               1
#define GMAC_RX_0_CHECKSUM_UDP_LSB               1
#define GMAC_RX_0_CHECKSUM_UDP_MASK              0x00000002
#define GMAC_RX_0_CHECKSUM_UDP_GET(x)            (((x) & GMAC_RX_0_CHECKSUM_UDP_MASK) >> GMAC_RX_0_CHECKSUM_UDP_LSB)
#define GMAC_RX_0_CHECKSUM_UDP_SET(x)            (((x) << GMAC_RX_0_CHECKSUM_UDP_LSB) & GMAC_RX_0_CHECKSUM_UDP_MASK)
#define GMAC_RX_0_CHECKSUM_TCP_MSB               0
#define GMAC_RX_0_CHECKSUM_TCP_LSB               0
#define GMAC_RX_0_CHECKSUM_TCP_MASK              0x00000001
#define GMAC_RX_0_CHECKSUM_TCP_GET(x)            (((x) & GMAC_RX_0_CHECKSUM_TCP_MASK) >> GMAC_RX_0_CHECKSUM_TCP_LSB)
#define GMAC_RX_0_CHECKSUM_TCP_SET(x)            (((x) << GMAC_RX_0_CHECKSUM_TCP_LSB) & GMAC_RX_0_CHECKSUM_TCP_MASK)

#define GMAC_RX_0_DBG_RX_ADDRESS                 0x00000014
#define GMAC_RX_0_DBG_RX_OFFSET                  0x00000014
#define GMAC_RX_0_DBG_RX_STATE_MSB               3
#define GMAC_RX_0_DBG_RX_STATE_LSB               0
#define GMAC_RX_0_DBG_RX_STATE_MASK              0x0000000f
#define GMAC_RX_0_DBG_RX_STATE_GET(x)            (((x) & GMAC_RX_0_DBG_RX_STATE_MASK) >> GMAC_RX_0_DBG_RX_STATE_LSB)
#define GMAC_RX_0_DBG_RX_STATE_SET(x)            (((x) << GMAC_RX_0_DBG_RX_STATE_LSB) & GMAC_RX_0_DBG_RX_STATE_MASK)

#define GMAC_RX_0_DBG_RX_CUR_ADDR_ADDRESS        0x00000018
#define GMAC_RX_0_DBG_RX_CUR_ADDR_OFFSET         0x00000018
#define GMAC_RX_0_DBG_RX_CUR_ADDR_ADDR_MSB       31
#define GMAC_RX_0_DBG_RX_CUR_ADDR_ADDR_LSB       0
#define GMAC_RX_0_DBG_RX_CUR_ADDR_ADDR_MASK      0xffffffff
#define GMAC_RX_0_DBG_RX_CUR_ADDR_ADDR_GET(x)    (((x) & GMAC_RX_0_DBG_RX_CUR_ADDR_ADDR_MASK) >> GMAC_RX_0_DBG_RX_CUR_ADDR_ADDR_LSB)
#define GMAC_RX_0_DBG_RX_CUR_ADDR_ADDR_SET(x)    (((x) << GMAC_RX_0_DBG_RX_CUR_ADDR_ADDR_LSB) & GMAC_RX_0_DBG_RX_CUR_ADDR_ADDR_MASK)

#define GMAC_RX_0_DATA_SWAP_ADDRESS              0x0000001c
#define GMAC_RX_0_DATA_SWAP_OFFSET               0x0000001c
#define GMAC_RX_0_DATA_SWAP_SWAPD_MSB            1
#define GMAC_RX_0_DATA_SWAP_SWAPD_LSB            1
#define GMAC_RX_0_DATA_SWAP_SWAPD_MASK           0x00000002
#define GMAC_RX_0_DATA_SWAP_SWAPD_GET(x)         (((x) & GMAC_RX_0_DATA_SWAP_SWAPD_MASK) >> GMAC_RX_0_DATA_SWAP_SWAPD_LSB)
#define GMAC_RX_0_DATA_SWAP_SWAPD_SET(x)         (((x) << GMAC_RX_0_DATA_SWAP_SWAPD_LSB) & GMAC_RX_0_DATA_SWAP_SWAPD_MASK)
#define GMAC_RX_0_DATA_SWAP_SWAP_MSB             0
#define GMAC_RX_0_DATA_SWAP_SWAP_LSB             0
#define GMAC_RX_0_DATA_SWAP_SWAP_MASK            0x00000001
#define GMAC_RX_0_DATA_SWAP_SWAP_GET(x)          (((x) & GMAC_RX_0_DATA_SWAP_SWAP_MASK) >> GMAC_RX_0_DATA_SWAP_SWAP_LSB)
#define GMAC_RX_0_DATA_SWAP_SWAP_SET(x)          (((x) << GMAC_RX_0_DATA_SWAP_SWAP_LSB) & GMAC_RX_0_DATA_SWAP_SWAP_MASK)


#ifndef __ASSEMBLER__

typedef struct bridge_chain_gmac_0_rx_reg_reg_s {
  volatile unsigned int gmac_rx_0_desc_start_address;
  volatile unsigned int gmac_rx_0_dma_start;
  volatile unsigned int gmac_rx_0_burst_size;
  volatile unsigned int gmac_rx_0_pkt_offset;
  volatile unsigned int gmac_rx_0_checksum;
  volatile unsigned int gmac_rx_0_dbg_rx;
  volatile unsigned int gmac_rx_0_dbg_rx_cur_addr;
  volatile unsigned int gmac_rx_0_data_swap;
} bridge_chain_gmac_0_rx_reg_reg_t;

#endif /* __ASSEMBLER__ */

#endif /* _BRIDGE_CHAIN_GMAC_0_RX_REG_H_ */
