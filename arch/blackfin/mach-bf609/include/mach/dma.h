/* mach/dma.h - arch-specific DMA defines
 *
 * Copyright 2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _MACH_DMA_H_
#define _MACH_DMA_H_

#define CH_SPORT0_TX                   0
#define CH_SPORT0_RX                   1
#define CH_SPORT1_TX                   2
#define CH_SPORT1_RX                   3
#define CH_SPORT2_TX                   4
#define CH_SPORT2_RX                   5
#define CH_SPI0_TX                     6
#define CH_SPI0_RX                     7
#define CH_SPI1_TX                     8
#define CH_SPI1_RX                     9
#define CH_RSI                        10
#define CH_SDU                        11
#define CH_LP0                        13
#define CH_LP1                        14
#define CH_LP2                        15
#define CH_LP3                        16
#define CH_UART0_TX                   17
#define CH_UART0_RX                   18
#define CH_UART1_TX                   19
#define CH_UART1_RX                   20
#define CH_MEM_STREAM0_SRC_CRC0      21
#define CH_MEM_STREAM0_SRC           CH_MEM_STREAM0_SRC_CRC0
#define CH_MEM_STREAM0_DEST_CRC0     22
#define CH_MEM_STREAM0_DEST          CH_MEM_STREAM0_DEST_CRC0
#define CH_MEM_STREAM1_SRC_CRC1      23
#define CH_MEM_STREAM1_SRC           CH_MEM_STREAM1_SRC_CRC1
#define CH_MEM_STREAM1_DEST_CRC1     24
#define CH_MEM_STREAM1_DEST          CH_MEM_STREAM1_DEST_CRC1
#define CH_MEM_STREAM2_SRC           25
#define CH_MEM_STREAM2_DEST          26
#define CH_MEM_STREAM3_SRC           27
#define CH_MEM_STREAM3_DEST          28
#define CH_EPPI0_CH0                  29
#define CH_EPPI0_CH1                  30
#define CH_EPPI1_CH0                  31
#define CH_EPPI1_CH1                  32
#define CH_EPPI2_CH0                  33
#define CH_EPPI2_CH1                  34
#define CH_PIXC_CH0                   35
#define CH_PIXC_CH1                   36
#define CH_PIXC_CH2                   37
#define CH_PVP_CPDOB                  38
#define CH_PVP_CPDOC                  39
#define CH_PVP_CPSTAT                 40
#define CH_PVP_CPCI                   41
#define CH_PVP_MPDO                   42
#define CH_PVP_MPDI                   43
#define CH_PVP_MPSTAT                 44
#define CH_PVP_MPCI                   45
#define CH_PVP_CPDOA                  46

#define MAX_DMA_CHANNELS 47
#define MAX_DMA_SUSPEND_CHANNELS 0
#define DMA_MMR_SIZE_32

#define bfin_read_MDMA_S0_CONFIG bfin_read_MDMA0_SRC_CRC0_CONFIG
#define bfin_write_MDMA_S0_CONFIG bfin_write_MDMA0_SRC_CRC0_CONFIG
#define bfin_read_MDMA_S0_IRQ_STATUS bfin_read_MDMA0_SRC_CRC0_IRQ_STATUS
#define bfin_write_MDMA_S0_IRQ_STATUS bfin_write_MDMA0_SRC_CRC0_IRQ_STATUS
#define bfin_write_MDMA_S0_START_ADDR bfin_write_MDMA0_SRC_CRC0_START_ADDR
#define bfin_write_MDMA_S0_X_COUNT bfin_write_MDMA0_SRC_CRC0_X_COUNT
#define bfin_write_MDMA_S0_X_MODIFY bfin_write_MDMA0_SRC_CRC0_X_MODIFY
#define bfin_write_MDMA_S0_Y_COUNT bfin_write_MDMA0_SRC_CRC0_Y_COUNT
#define bfin_write_MDMA_S0_Y_MODIFY bfin_write_MDMA0_SRC_CRC0_Y_MODIFY
#define bfin_read_MDMA_D0_CONFIG bfin_read_MDMA0_DEST_CRC0_CONFIG
#define bfin_write_MDMA_D0_CONFIG bfin_write_MDMA0_DEST_CRC0_CONFIG
#define bfin_read_MDMA_D0_IRQ_STATUS bfin_read_MDMA0_DEST_CRC0_IRQ_STATUS
#define bfin_write_MDMA_D0_IRQ_STATUS bfin_write_MDMA0_DEST_CRC0_IRQ_STATUS
#define bfin_write_MDMA_D0_START_ADDR bfin_write_MDMA0_DEST_CRC0_START_ADDR
#define bfin_write_MDMA_D0_X_COUNT bfin_write_MDMA0_DEST_CRC0_X_COUNT
#define bfin_write_MDMA_D0_X_MODIFY bfin_write_MDMA0_DEST_CRC0_X_MODIFY
#define bfin_write_MDMA_D0_Y_COUNT bfin_write_MDMA0_DEST_CRC0_Y_COUNT
#define bfin_write_MDMA_D0_Y_MODIFY bfin_write_MDMA0_DEST_CRC0_Y_MODIFY

#define bfin_read_MDMA_S1_CONFIG bfin_read_MDMA1_SRC_CRC1_CONFIG
#define bfin_write_MDMA_S1_CONFIG bfin_write_MDMA1_SRC_CRC1_CONFIG
#define bfin_read_MDMA_D1_CONFIG bfin_read_MDMA1_DEST_CRC1_CONFIG
#define bfin_write_MDMA_D1_CONFIG bfin_write_MDMA1_DEST_CRC1_CONFIG
#define bfin_read_MDMA_D1_IRQ_STATUS bfin_read_MDMA1_DEST_CRC1_IRQ_STATUS
#define bfin_write_MDMA_D1_IRQ_STATUS bfin_write_MDMA1_DEST_CRC1_IRQ_STATUS

#define bfin_read_MDMA_S3_CONFIG bfin_read_MDMA3_SRC_CONFIG
#define bfin_write_MDMA_S3_CONFIG bfin_write_MDMA3_SRC_CONFIG
#define bfin_read_MDMA_S3_IRQ_STATUS bfin_read_MDMA3_SRC_IRQ_STATUS
#define bfin_write_MDMA_S3_IRQ_STATUS bfin_write_MDMA3_SRC_IRQ_STATUS
#define bfin_write_MDMA_S3_START_ADDR bfin_write_MDMA3_SRC_START_ADDR
#define bfin_write_MDMA_S3_X_COUNT bfin_write_MDMA3_SRC_X_COUNT
#define bfin_write_MDMA_S3_X_MODIFY bfin_write_MDMA3_SRC_X_MODIFY
#define bfin_write_MDMA_S3_Y_COUNT bfin_write_MDMA3_SRC_Y_COUNT
#define bfin_write_MDMA_S3_Y_MODIFY bfin_write_MDMA3_SRC_Y_MODIFY
#define bfin_read_MDMA_D3_CONFIG bfin_read_MDMA3_DEST_CONFIG
#define bfin_write_MDMA_D3_CONFIG bfin_write_MDMA3_DEST_CONFIG
#define bfin_read_MDMA_D3_IRQ_STATUS bfin_read_MDMA3_DEST_IRQ_STATUS
#define bfin_write_MDMA_D3_IRQ_STATUS bfin_write_MDMA3_DEST_IRQ_STATUS
#define bfin_write_MDMA_D3_START_ADDR bfin_write_MDMA3_DEST_START_ADDR
#define bfin_write_MDMA_D3_X_COUNT bfin_write_MDMA3_DEST_X_COUNT
#define bfin_write_MDMA_D3_X_MODIFY bfin_write_MDMA3_DEST_X_MODIFY
#define bfin_write_MDMA_D3_Y_COUNT bfin_write_MDMA3_DEST_Y_COUNT
#define bfin_write_MDMA_D3_Y_MODIFY bfin_write_MDMA3_DEST_Y_MODIFY

#define MDMA_S0_NEXT_DESC_PTR MDMA0_SRC_CRC0_NEXT_DESC_PTR
#define MDMA_D0_NEXT_DESC_PTR MDMA0_DEST_CRC0_NEXT_DESC_PTR
#define MDMA_S1_NEXT_DESC_PTR MDMA1_SRC_CRC1_NEXT_DESC_PTR
#define MDMA_D1_NEXT_DESC_PTR MDMA1_DEST_CRC1_NEXT_DESC_PTR

#endif
