#ifndef _ASM_ARCH_CRIS_DMA_H
#define _ASM_ARCH_CRIS_DMA_H

/* Defines for using and allocating dma channels. */

#define MAX_DMA_CHANNELS	10

#define NETWORK_ETH0_TX_DMA_NBR 0	/* Ethernet 0 out. */
#define NETWORK_ETH0 RX_DMA_NBR 1	/* Ethernet 0 in. */

#define IO_PROC_DMA0_TX_DMA_NBR 2	/* IO processor DMA0 out. */
#define IO_PROC_DMA0_RX_DMA_NBR 3	/* IO processor DMA0 in. */

#define ATA_TX_DMA_NBR 2		/* ATA interface out. */
#define ATA_RX_DMA_NBR 3		/* ATA interface in. */

#define ASYNC_SER2_TX_DMA_NBR 2		/* Asynchronous serial port 2 out. */
#define ASYNC_SER2_RX_DMA_NBR 3		/* Asynchronous serial port 2 in. */

#define IO_PROC_DMA1_TX_DMA_NBR 4	/* IO processor DMA1 out. */
#define IO_PROC_DMA1_RX_DMA_NBR 5	/* IO processor DMA1 in. */

#define ASYNC_SER1_TX_DMA_NBR 4		/* Asynchronous serial port 1 out. */
#define ASYNC_SER1_RX_DMA_NBR 5		/* Asynchronous serial port 1 in. */

#define SYNC_SER0_TX_DMA_NBR 4		/* Synchronous serial port 0 out. */
#define SYNC_SER0_RX_DMA_NBR 5		/* Synchronous serial port 0 in. */

#define EXTDMA0_TX_DMA_NBR 6		/* External DMA 0 out. */
#define EXTDMA1_RX_DMA_NBR 7		/* External DMA 1 in. */

#define ASYNC_SER0_TX_DMA_NBR 6		/* Asynchronous serial port 0 out. */
#define ASYNC_SER0_RX_DMA_NBR 7		/* Asynchronous serial port 0 in. */

#define SYNC_SER1_TX_DMA_NBR 6		/* Synchronous serial port 1 out. */
#define SYNC_SER1_RX_DMA_NBR 7		/* Synchronous serial port 1 in. */

#define NETWORK_ETH1_TX_DMA_NBR 6	/* Ethernet 1 out. */
#define NETWORK_ETH1_RX_DMA_NBR 7	/* Ethernet 1 in. */

#define EXTDMA2_TX_DMA_NBR 8		/* External DMA 2 out. */
#define EXTDMA3_RX_DMA_NBR 9		/* External DMA 3 in. */

#define STRCOP_TX_DMA_NBR 8		/* Stream co-processor out. */
#define STRCOP_RX_DMA_NBR 9		/* Stream co-processor in. */

#define ASYNC_SER3_TX_DMA_NBR 8		/* Asynchronous serial port 3 out. */
#define ASYNC_SER3_RX_DMA_NBR 9		/* Asynchronous serial port 3 in. */

enum dma_owner {
  dma_eth0,
  dma_eth1,
  dma_iop0,
  dma_iop1,
  dma_ser0,
  dma_ser1,
  dma_ser2,
  dma_ser3,
  dma_sser0,
  dma_sser1,
  dma_ata,
  dma_strp,
  dma_ext0,
  dma_ext1,
  dma_ext2,
  dma_ext3
};

int crisv32_request_dma(unsigned int dmanr, const char *device_id,
			unsigned options, unsigned bandwidth,
			enum dma_owner owner);
void crisv32_free_dma(unsigned int dmanr);

/* Masks used by crisv32_request_dma options: */
#define DMA_VERBOSE_ON_ERROR 1
#define DMA_PANIC_ON_ERROR (2|DMA_VERBOSE_ON_ERROR)
#define DMA_INT_MEM 4

#endif /* _ASM_ARCH_CRIS_DMA_H */
