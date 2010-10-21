/*
 * Blackfin On-Chip SPI Driver
 *
 * Copyright 2004-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _SPI_CHANNEL_H_
#define _SPI_CHANNEL_H_

#define MIN_SPI_BAUD_VAL	2

#define BIT_CTL_ENABLE      0x4000
#define BIT_CTL_OPENDRAIN   0x2000
#define BIT_CTL_MASTER      0x1000
#define BIT_CTL_CPOL        0x0800
#define BIT_CTL_CPHA        0x0400
#define BIT_CTL_LSBF        0x0200
#define BIT_CTL_WORDSIZE    0x0100
#define BIT_CTL_EMISO       0x0020
#define BIT_CTL_PSSE        0x0010
#define BIT_CTL_GM          0x0008
#define BIT_CTL_SZ          0x0004
#define BIT_CTL_RXMOD       0x0000
#define BIT_CTL_TXMOD       0x0001
#define BIT_CTL_TIMOD_DMA_TX 0x0003
#define BIT_CTL_TIMOD_DMA_RX 0x0002
#define BIT_CTL_SENDOPT     0x0004
#define BIT_CTL_TIMOD       0x0003

#define BIT_STAT_SPIF       0x0001
#define BIT_STAT_MODF       0x0002
#define BIT_STAT_TXE        0x0004
#define BIT_STAT_TXS        0x0008
#define BIT_STAT_RBSY       0x0010
#define BIT_STAT_RXS        0x0020
#define BIT_STAT_TXCOL      0x0040
#define BIT_STAT_CLR        0xFFFF

#define BIT_STU_SENDOVER    0x0001
#define BIT_STU_RECVFULL    0x0020

#define MAX_CTRL_CS          8  /* cs in spi controller */

/* device.platform_data for SSP controller devices */
struct bfin5xx_spi_master {
	u16 num_chipselect;
	u8 enable_dma;
	u16 pin_req[7];
};

/* spi_board_info.controller_data for SPI slave devices,
 * copied to spi_device.platform_data ... mostly for dma tuning
 */
struct bfin5xx_spi_chip {
	u16 ctl_reg;
	u8 enable_dma;
	u8 bits_per_word;
	u16 cs_chg_udelay; /* Some devices require 16-bit delays */
	/* Value to send if no TX value is supplied, usually 0x0 or 0xFFFF */
	u16 idle_tx_val;
	u8 pio_interrupt; /* Enable spi data irq */
};

#endif /* _SPI_CHANNEL_H_ */
