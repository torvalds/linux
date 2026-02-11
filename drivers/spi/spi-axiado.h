/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Axiado SPI controller driver (Host mode only)
 *
 * Copyright (C) 2022-2025 Axiado Corporation (or its affiliates).
 */

#ifndef SPI_AXIADO_H
#define SPI_AXIADO_H

/* Name of this driver */
#define AX_SPI_NAME			"axiado-db-spi"

/* Axiado - SPI Digital Blocks IP design registers */
#define AX_SPI_TX_FAETR			0x18    // TX-FAETR
#define ALMOST_EMPTY_TRESHOLD		0x00	// Programmed threshold value
#define AX_SPI_RX_FAFTR			0x28    // RX-FAETR
#define ALMOST_FULL_TRESHOLD		0x0c	// Programmed threshold value
#define FIFO_DEPTH			256	// 256 bytes

#define AX_SPI_CR1			0x00	// CR1
#define AX_SPI_CR1_CLR			0x00	// CR1 - Clear
#define AX_SPI_CR1_SCR			0x01	// CR1 - controller reset
#define AX_SPI_CR1_SCE			0x02	// CR1 - Controller Enable/Disable
#define AX_SPI_CR1_CPHA			0x08	// CR1 - CPH
#define AX_SPI_CR1_CPOL			0x10	// CR1 - CPO

#define AX_SPI_CR2			0x04	// CR2
#define AX_SPI_CR2_SWD			0x04	// CR2 - Write Enabel/Disable
#define AX_SPI_CR2_SRD			0x08	// CR2 - Read Enable/Disable
#define AX_SPI_CR2_SRI			0x10	// CR2 - Read First Byte Ignore
#define AX_SPI_CR2_HTE			0x40	// CR2 - Host Transmit Enable
#define AX_SPI_CR3			0x08	// CR3
#define AX_SPI_CR3_SDL			0x00	// CR3 - Data lines
#define AX_SPI_CR3_QUAD			0x02	// CR3 - Data lines

/* As per Digital Blocks datasheet clock frequency range
 * Min - 244KHz
 * Max - 62.5MHz
 * SCK Clock Divider Register Values
 */
#define AX_SPI_RX_FBCAR			0x24	// RX_FBCAR
#define AX_SPI_TX_FBCAR			0x14	// TX_FBCAR
#define AX_SPI_SCDR			0x2c	// SCDR
#define AX_SPI_SCD_MIN			0x1fe	// Valid SCD (SCK Clock Divider Register)
#define AX_SPI_SCD_DEFAULT		0x06	// Default SCD (SCK Clock Divider Register)
#define AX_SPI_SCD_MAX			0x00	// Valid SCD (SCK Clock Divider Register)
#define AX_SPI_SCDR_SCS			0x0200	// SCDR - AMBA Bus Clock source

#define AX_SPI_IMR			0x34	// IMR
#define AX_SPI_IMR_CLR			0x00	// IMR - Clear
#define AX_SPI_IMR_TFOM			0x02	// IMR - TFO
#define AX_SPI_IMR_MTCM			0x40	// IMR - MTC
#define AX_SPI_IMR_TFEM			0x10	// IMR - TFE
#define AX_SPI_IMR_RFFM			0x20	// IMR - RFFM

#define AX_SPI_ISR			0x30	// ISR
#define AX_SPI_ISR_CLR			0xff	// ISR - Clear
#define AX_SPI_ISR_MTC			0x40	// ISR - MTC
#define AX_SPI_ISR_TFE			0x10	// ISR - TFE
#define AX_SPI_ISR_RFF			0x20	// ISR - RFF

#define AX_SPI_IVR			0x38	// IVR
#define AX_SPI_IVR_TFOV			0x02	// IVR - TFOV
#define AX_SPI_IVR_MTCV			0x40	// IVR - MTCV
#define AX_SPI_IVR_TFEV			0x10	// IVR - TFEV
#define AX_SPI_IVR_RFFV			0x20	// IVR - RFFV

#define AX_SPI_TXFIFO			0x0c	// TX_FIFO
#define AX_SPI_TX_RX_FBCR		0x10	// TX_RX_FBCR
#define AX_SPI_RXFIFO			0x1c	// RX_FIFO

#define AX_SPI_TS0			0x00	// Target select 0
#define AX_SPI_TS1			0x01	// Target select 1
#define AX_SPI_TS2			0x10	// Target select 2
#define AX_SPI_TS3			0x11	// Target select 3

#define SPI_AUTOSUSPEND_TIMEOUT		3000

/* Default number of chip select lines also used as maximum number of chip select lines */
#define AX_SPI_DEFAULT_NUM_CS		4

/* Default number of command buffer size */
#define AX_SPI_COMMAND_BUFFER_SIZE	16	//Command + address bytes

/* Target select mask
 * 00 – TS0
 * 01 – TS1
 * 10 – TS2
 * 11 – TS3
 */
#define AX_SPI_DEFAULT_TS_MASK		0x03

#define AX_SPI_RX_FIFO_DRAIN_LIMIT	24
#define AX_SPI_TRX_FIFO_TIMEOUT		1000
/**
 * struct ax_spi - This definition defines spi driver instance
 * @regs:					Virtual address of the SPI controller registers
 * @ref_clk:					Pointer to the peripheral clock
 * @pclk:					Pointer to the APB clock
 * @speed_hz:					Current SPI bus clock speed in Hz
 * @txbuf:					Pointer	to the TX buffer
 * @rxbuf:					Pointer to the RX buffer
 * @tx_bytes:					Number of bytes left to transfer
 * @rx_bytes:					Number of bytes requested
 * @tx_fifo_depth:				Depth of the TX FIFO
 * @current_rx_fifo_word:			Buffers the 32-bit word read from RXFIFO
 * @bytes_left_in_current_rx_word:		Bytes to be extracted from current 32-bit word
 * @current_rx_fifo_word_for_irq:		Buffers the 32-bit word read from RXFIFO for IRQ
 * @bytes_left_in_current_rx_word_for_irq:	IRQ bytes to be extracted from current 32-bit word
 * @rx_discard:					Number of bytes to discard
 * @rx_copy_remaining:				Number of bytes to copy
 */
struct ax_spi {
	void __iomem *regs;
	struct clk *ref_clk;
	struct clk *pclk;
	unsigned int clk_rate;
	u32 speed_hz;
	const u8 *tx_buf;
	u8 *rx_buf;
	int tx_bytes;
	int rx_bytes;
	unsigned int tx_fifo_depth;
	u32 current_rx_fifo_word;
	int bytes_left_in_current_rx_word;
	u32 current_rx_fifo_word_for_irq;
	int bytes_left_in_current_rx_word_for_irq;
	int rx_discard;
	int rx_copy_remaining;
};

#endif /* SPI_AXIADO_H */
