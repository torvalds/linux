/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARCH_CRIS_DMA_H
#define _ASM_ARCH_CRIS_DMA_H

/* Defines for using and allocating dma channels. */

#define MAX_DMA_CHANNELS	12 /* 8 and 10 not used. */

#define NETWORK_ETH_TX_DMA_NBR 0        /* Ethernet 0 out. */
#define NETWORK_ETH_RX_DMA_NBR 1        /* Ethernet 0 in. */

#define IO_PROC_DMA_TX_DMA_NBR 4        /* IO processor DMA0 out. */
#define IO_PROC_DMA_RX_DMA_NBR 5        /* IO processor DMA0 in. */

#define ASYNC_SER3_TX_DMA_NBR 2         /* Asynchronous serial port 3 out. */
#define ASYNC_SER3_RX_DMA_NBR 3         /* Asynchronous serial port 3 in. */

#define ASYNC_SER2_TX_DMA_NBR 6         /* Asynchronous serial port 2 out. */
#define ASYNC_SER2_RX_DMA_NBR 7         /* Asynchronous serial port 2 in. */

#define ASYNC_SER1_TX_DMA_NBR 4         /* Asynchronous serial port 1 out. */
#define ASYNC_SER1_RX_DMA_NBR 5         /* Asynchronous serial port 1 in. */

#define SYNC_SER_TX_DMA_NBR 6           /* Synchronous serial port 0 out. */
#define SYNC_SER_RX_DMA_NBR 7           /* Synchronous serial port 0 in. */

#define ASYNC_SER0_TX_DMA_NBR 0         /* Asynchronous serial port 0 out. */
#define ASYNC_SER0_RX_DMA_NBR 1         /* Asynchronous serial port 0 in. */

#define STRCOP_TX_DMA_NBR 2             /* Stream co-processor out. */
#define STRCOP_RX_DMA_NBR 3             /* Stream co-processor in. */

#define dma_eth0 dma_eth
#define dma_eth1 dma_eth

enum dma_owner {
	dma_eth,
	dma_ser0,
	dma_ser1,
	dma_ser2,
	dma_ser3,
	dma_ser4,
	dma_iop,
	dma_sser,
	dma_strp,
	dma_h264,
	dma_jpeg
};

int crisv32_request_dma(unsigned int dmanr, const char *device_id,
	unsigned options, unsigned bandwidth, enum dma_owner owner);
void crisv32_free_dma(unsigned int dmanr);

/* Masks used by crisv32_request_dma options: */
#define DMA_VERBOSE_ON_ERROR 1
#define DMA_PANIC_ON_ERROR (2|DMA_VERBOSE_ON_ERROR)
#define DMA_INT_MEM 4

#endif /* _ASM_ARCH_CRIS_DMA_H */
