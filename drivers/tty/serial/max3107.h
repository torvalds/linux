/*
 * max3107.h - spi uart protocol driver header for Maxim 3107
 *
 * Copyright (C) Aavamobile 2009
 * Based on serial_max3100.h by Christian Pellegrin
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _MAX3107_H
#define _MAX3107_H

/* Serial error status definitions */
#define MAX3107_PARITY_ERROR	1
#define MAX3107_FRAME_ERROR	2
#define MAX3107_OVERRUN_ERROR	4
#define MAX3107_ALL_ERRORS	(MAX3107_PARITY_ERROR | \
				 MAX3107_FRAME_ERROR | \
				 MAX3107_OVERRUN_ERROR)

/* GPIO definitions */
#define MAX3107_GPIO_BASE	88
#define MAX3107_GPIO_COUNT	4


/* GPIO connected to chip's reset pin */
#define MAX3107_RESET_GPIO	87


/* Chip reset delay */
#define MAX3107_RESET_DELAY	10

/* Chip wakeup delay */
#define MAX3107_WAKEUP_DELAY	50


/* Sleep mode definitions */
#define MAX3107_DISABLE_FORCED_SLEEP	0
#define MAX3107_ENABLE_FORCED_SLEEP	1
#define MAX3107_DISABLE_AUTOSLEEP	2
#define MAX3107_ENABLE_AUTOSLEEP	3


/* Definitions for register access with SPI transfers
 *
 * SPI transfer format:
 *
 * Master to slave bits xzzzzzzzyyyyyyyy
 * Slave to master bits aaaaaaaabbbbbbbb
 *
 * where:
 * x = 0 for reads, 1 for writes
 * z = register address
 * y = new register value if write, 0 if read
 * a = unspecified
 * b = register value if read, unspecified if write
 */

/* SPI speed */
#define MAX3107_SPI_SPEED	(3125000 * 2)

/* Write bit */
#define MAX3107_WRITE_BIT	(1 << 15)

/* SPI TX data mask */
#define MAX3107_SPI_RX_DATA_MASK	(0x00ff)

/* SPI RX data mask */
#define MAX3107_SPI_TX_DATA_MASK	(0x00ff)

/* Register access masks */
#define MAX3107_RHR_REG			(0x0000) /* RX FIFO */
#define MAX3107_THR_REG			(0x0000) /* TX FIFO */
#define MAX3107_IRQEN_REG		(0x0100) /* IRQ enable */
#define MAX3107_IRQSTS_REG		(0x0200) /* IRQ status */
#define MAX3107_LSR_IRQEN_REG		(0x0300) /* LSR IRQ enable */
#define MAX3107_LSR_IRQSTS_REG		(0x0400) /* LSR IRQ status */
#define MAX3107_SPCHR_IRQEN_REG		(0x0500) /* Special char IRQ enable */
#define MAX3107_SPCHR_IRQSTS_REG	(0x0600) /* Special char IRQ status */
#define MAX3107_STS_IRQEN_REG		(0x0700) /* Status IRQ enable */
#define MAX3107_STS_IRQSTS_REG		(0x0800) /* Status IRQ status */
#define MAX3107_MODE1_REG		(0x0900) /* MODE1 */
#define MAX3107_MODE2_REG		(0x0a00) /* MODE2 */
#define MAX3107_LCR_REG			(0x0b00) /* LCR */
#define MAX3107_RXTO_REG		(0x0c00) /* RX timeout */
#define MAX3107_HDPIXDELAY_REG		(0x0d00) /* Auto transceiver delays */
#define MAX3107_IRDA_REG		(0x0e00) /* IRDA settings */
#define MAX3107_FLOWLVL_REG		(0x0f00) /* Flow control levels */
#define MAX3107_FIFOTRIGLVL_REG		(0x1000) /* FIFO IRQ trigger levels */
#define MAX3107_TXFIFOLVL_REG		(0x1100) /* TX FIFO level */
#define MAX3107_RXFIFOLVL_REG		(0x1200) /* RX FIFO level */
#define MAX3107_FLOWCTRL_REG		(0x1300) /* Flow control */
#define MAX3107_XON1_REG		(0x1400) /* XON1 character */
#define MAX3107_XON2_REG		(0x1500) /* XON2 character */
#define MAX3107_XOFF1_REG		(0x1600) /* XOFF1 character */
#define MAX3107_XOFF2_REG		(0x1700) /* XOFF2 character */
#define MAX3107_GPIOCFG_REG		(0x1800) /* GPIO config */
#define MAX3107_GPIODATA_REG		(0x1900) /* GPIO data */
#define MAX3107_PLLCFG_REG		(0x1a00) /* PLL config */
#define MAX3107_BRGCFG_REG		(0x1b00) /* Baud rate generator conf */
#define MAX3107_BRGDIVLSB_REG		(0x1c00) /* Baud rate divisor LSB */
#define MAX3107_BRGDIVMSB_REG		(0x1d00) /* Baud rate divisor MSB */
#define MAX3107_CLKSRC_REG		(0x1e00) /* Clock source */
#define MAX3107_REVID_REG		(0x1f00) /* Revision identification */

/* IRQ register bits */
#define MAX3107_IRQ_LSR_BIT	(1 << 0) /* LSR interrupt */
#define MAX3107_IRQ_SPCHR_BIT	(1 << 1) /* Special char interrupt */
#define MAX3107_IRQ_STS_BIT	(1 << 2) /* Status interrupt */
#define MAX3107_IRQ_RXFIFO_BIT	(1 << 3) /* RX FIFO interrupt */
#define MAX3107_IRQ_TXFIFO_BIT	(1 << 4) /* TX FIFO interrupt */
#define MAX3107_IRQ_TXEMPTY_BIT	(1 << 5) /* TX FIFO empty interrupt */
#define MAX3107_IRQ_RXEMPTY_BIT	(1 << 6) /* RX FIFO empty interrupt */
#define MAX3107_IRQ_CTS_BIT	(1 << 7) /* CTS interrupt */

/* LSR register bits */
#define MAX3107_LSR_RXTO_BIT	(1 << 0) /* RX timeout */
#define MAX3107_LSR_RXOVR_BIT	(1 << 1) /* RX overrun */
#define MAX3107_LSR_RXPAR_BIT	(1 << 2) /* RX parity error */
#define MAX3107_LSR_FRERR_BIT	(1 << 3) /* Frame error */
#define MAX3107_LSR_RXBRK_BIT	(1 << 4) /* RX break */
#define MAX3107_LSR_RXNOISE_BIT	(1 << 5) /* RX noise */
#define MAX3107_LSR_UNDEF6_BIT	(1 << 6) /* Undefined/not used */
#define MAX3107_LSR_CTS_BIT	(1 << 7) /* CTS pin state */

/* Special character register bits */
#define MAX3107_SPCHR_XON1_BIT		(1 << 0) /* XON1 character */
#define MAX3107_SPCHR_XON2_BIT		(1 << 1) /* XON2 character */
#define MAX3107_SPCHR_XOFF1_BIT		(1 << 2) /* XOFF1 character */
#define MAX3107_SPCHR_XOFF2_BIT		(1 << 3) /* XOFF2 character */
#define MAX3107_SPCHR_BREAK_BIT		(1 << 4) /* RX break */
#define MAX3107_SPCHR_MULTIDROP_BIT	(1 << 5) /* 9-bit multidrop addr char */
#define MAX3107_SPCHR_UNDEF6_BIT	(1 << 6) /* Undefined/not used */
#define MAX3107_SPCHR_UNDEF7_BIT	(1 << 7) /* Undefined/not used */

/* Status register bits */
#define MAX3107_STS_GPIO0_BIT		(1 << 0) /* GPIO 0 interrupt */
#define MAX3107_STS_GPIO1_BIT		(1 << 1) /* GPIO 1 interrupt */
#define MAX3107_STS_GPIO2_BIT		(1 << 2) /* GPIO 2 interrupt */
#define MAX3107_STS_GPIO3_BIT		(1 << 3) /* GPIO 3 interrupt */
#define MAX3107_STS_UNDEF4_BIT		(1 << 4) /* Undefined/not used */
#define MAX3107_STS_CLKREADY_BIT	(1 << 5) /* Clock ready */
#define MAX3107_STS_SLEEP_BIT		(1 << 6) /* Sleep interrupt */
#define MAX3107_STS_UNDEF7_BIT		(1 << 7) /* Undefined/not used */

/* MODE1 register bits */
#define MAX3107_MODE1_RXDIS_BIT		(1 << 0) /* RX disable */
#define MAX3107_MODE1_TXDIS_BIT		(1 << 1) /* TX disable */
#define MAX3107_MODE1_TXHIZ_BIT		(1 << 2) /* TX pin three-state */
#define MAX3107_MODE1_RTSHIZ_BIT	(1 << 3) /* RTS pin three-state */
#define MAX3107_MODE1_TRNSCVCTRL_BIT	(1 << 4) /* Transceiver ctrl enable */
#define MAX3107_MODE1_FORCESLEEP_BIT	(1 << 5) /* Force sleep mode */
#define MAX3107_MODE1_AUTOSLEEP_BIT	(1 << 6) /* Auto sleep enable */
#define MAX3107_MODE1_IRQSEL_BIT	(1 << 7) /* IRQ pin enable */

/* MODE2 register bits */
#define MAX3107_MODE2_RST_BIT		(1 << 0) /* Chip reset */
#define MAX3107_MODE2_FIFORST_BIT	(1 << 1) /* FIFO reset */
#define MAX3107_MODE2_RXTRIGINV_BIT	(1 << 2) /* RX FIFO INT invert */
#define MAX3107_MODE2_RXEMPTINV_BIT	(1 << 3) /* RX FIFO empty INT invert */
#define MAX3107_MODE2_SPCHR_BIT		(1 << 4) /* Special chr detect enable */
#define MAX3107_MODE2_LOOPBACK_BIT	(1 << 5) /* Internal loopback enable */
#define MAX3107_MODE2_MULTIDROP_BIT	(1 << 6) /* 9-bit multidrop enable */
#define MAX3107_MODE2_ECHOSUPR_BIT	(1 << 7) /* ECHO suppression enable */

/* LCR register bits */
#define MAX3107_LCR_LENGTH0_BIT		(1 << 0) /* Word length bit 0 */
#define MAX3107_LCR_LENGTH1_BIT		(1 << 1) /* Word length bit 1
						  *
						  * Word length bits table:
						  * 00 -> 5 bit words
						  * 01 -> 6 bit words
						  * 10 -> 7 bit words
						  * 11 -> 8 bit words
						  */
#define MAX3107_LCR_STOPLEN_BIT		(1 << 2) /* STOP length bit
						  *
						  * STOP length bit table:
						  * 0 -> 1 stop bit
						  * 1 -> 1-1.5 stop bits if
						  *      word length is 5,
						  *      2 stop bits otherwise
						  */
#define MAX3107_LCR_PARITY_BIT		(1 << 3) /* Parity bit enable */
#define MAX3107_LCR_EVENPARITY_BIT	(1 << 4) /* Even parity bit enable */
#define MAX3107_LCR_FORCEPARITY_BIT	(1 << 5) /* 9-bit multidrop parity */
#define MAX3107_LCR_TXBREAK_BIT		(1 << 6) /* TX break enable */
#define MAX3107_LCR_RTS_BIT		(1 << 7) /* RTS pin control */
#define MAX3107_LCR_WORD_LEN_5		(0x0000)
#define MAX3107_LCR_WORD_LEN_6		(0x0001)
#define MAX3107_LCR_WORD_LEN_7		(0x0002)
#define MAX3107_LCR_WORD_LEN_8		(0x0003)


/* IRDA register bits */
#define MAX3107_IRDA_IRDAEN_BIT		(1 << 0) /* IRDA mode enable */
#define MAX3107_IRDA_SIR_BIT		(1 << 1) /* SIR mode enable */
#define MAX3107_IRDA_SHORTIR_BIT	(1 << 2) /* Short SIR mode enable */
#define MAX3107_IRDA_MIR_BIT		(1 << 3) /* MIR mode enable */
#define MAX3107_IRDA_RXINV_BIT		(1 << 4) /* RX logic inversion enable */
#define MAX3107_IRDA_TXINV_BIT		(1 << 5) /* TX logic inversion enable */
#define MAX3107_IRDA_UNDEF6_BIT		(1 << 6) /* Undefined/not used */
#define MAX3107_IRDA_UNDEF7_BIT		(1 << 7) /* Undefined/not used */

/* Flow control trigger level register masks */
#define MAX3107_FLOWLVL_HALT_MASK	(0x000f) /* Flow control halt level */
#define MAX3107_FLOWLVL_RES_MASK	(0x00f0) /* Flow control resume level */
#define MAX3107_FLOWLVL_HALT(words)	((words/8) & 0x000f)
#define MAX3107_FLOWLVL_RES(words)	(((words/8) & 0x000f) << 4)

/* FIFO interrupt trigger level register masks */
#define MAX3107_FIFOTRIGLVL_TX_MASK	(0x000f) /* TX FIFO trigger level */
#define MAX3107_FIFOTRIGLVL_RX_MASK	(0x00f0) /* RX FIFO trigger level */
#define MAX3107_FIFOTRIGLVL_TX(words)	((words/8) & 0x000f)
#define MAX3107_FIFOTRIGLVL_RX(words)	(((words/8) & 0x000f) << 4)

/* Flow control register bits */
#define MAX3107_FLOWCTRL_AUTORTS_BIT	(1 << 0) /* Auto RTS flow ctrl enable */
#define MAX3107_FLOWCTRL_AUTOCTS_BIT	(1 << 1) /* Auto CTS flow ctrl enable */
#define MAX3107_FLOWCTRL_GPIADDR_BIT	(1 << 2) /* Enables that GPIO inputs
						  * are used in conjunction with
						  * XOFF2 for definition of
						  * special character */
#define MAX3107_FLOWCTRL_SWFLOWEN_BIT	(1 << 3) /* Auto SW flow ctrl enable */
#define MAX3107_FLOWCTRL_SWFLOW0_BIT	(1 << 4) /* SWFLOW bit 0 */
#define MAX3107_FLOWCTRL_SWFLOW1_BIT	(1 << 5) /* SWFLOW bit 1
						  *
						  * SWFLOW bits 1 & 0 table:
						  * 00 -> no transmitter flow
						  *       control
						  * 01 -> receiver compares
						  *       XON2 and XOFF2
						  *       and controls
						  *       transmitter
						  * 10 -> receiver compares
						  *       XON1 and XOFF1
						  *       and controls
						  *       transmitter
						  * 11 -> receiver compares
						  *       XON1, XON2, XOFF1 and
						  *       XOFF2 and controls
						  *       transmitter
						  */
#define MAX3107_FLOWCTRL_SWFLOW2_BIT	(1 << 6) /* SWFLOW bit 2 */
#define MAX3107_FLOWCTRL_SWFLOW3_BIT	(1 << 7) /* SWFLOW bit 3
						  *
						  * SWFLOW bits 3 & 2 table:
						  * 00 -> no received flow
						  *       control
						  * 01 -> transmitter generates
						  *       XON2 and XOFF2
						  * 10 -> transmitter generates
						  *       XON1 and XOFF1
						  * 11 -> transmitter generates
						  *       XON1, XON2, XOFF1 and
						  *       XOFF2
						  */

/* GPIO configuration register bits */
#define MAX3107_GPIOCFG_GP0OUT_BIT	(1 << 0) /* GPIO 0 output enable */
#define MAX3107_GPIOCFG_GP1OUT_BIT	(1 << 1) /* GPIO 1 output enable */
#define MAX3107_GPIOCFG_GP2OUT_BIT	(1 << 2) /* GPIO 2 output enable */
#define MAX3107_GPIOCFG_GP3OUT_BIT	(1 << 3) /* GPIO 3 output enable */
#define MAX3107_GPIOCFG_GP0OD_BIT	(1 << 4) /* GPIO 0 open-drain enable */
#define MAX3107_GPIOCFG_GP1OD_BIT	(1 << 5) /* GPIO 1 open-drain enable */
#define MAX3107_GPIOCFG_GP2OD_BIT	(1 << 6) /* GPIO 2 open-drain enable */
#define MAX3107_GPIOCFG_GP3OD_BIT	(1 << 7) /* GPIO 3 open-drain enable */

/* GPIO DATA register bits */
#define MAX3107_GPIODATA_GP0OUT_BIT	(1 << 0) /* GPIO 0 output value */
#define MAX3107_GPIODATA_GP1OUT_BIT	(1 << 1) /* GPIO 1 output value */
#define MAX3107_GPIODATA_GP2OUT_BIT	(1 << 2) /* GPIO 2 output value */
#define MAX3107_GPIODATA_GP3OUT_BIT	(1 << 3) /* GPIO 3 output value */
#define MAX3107_GPIODATA_GP0IN_BIT	(1 << 4) /* GPIO 0 input value */
#define MAX3107_GPIODATA_GP1IN_BIT	(1 << 5) /* GPIO 1 input value */
#define MAX3107_GPIODATA_GP2IN_BIT	(1 << 6) /* GPIO 2 input value */
#define MAX3107_GPIODATA_GP3IN_BIT	(1 << 7) /* GPIO 3 input value */

/* PLL configuration register masks */
#define MAX3107_PLLCFG_PREDIV_MASK	(0x003f) /* PLL predivision value */
#define MAX3107_PLLCFG_PLLFACTOR_MASK	(0x00c0) /* PLL multiplication factor */

/* Baud rate generator configuration register masks and bits */
#define MAX3107_BRGCFG_FRACT_MASK	(0x000f) /* Fractional portion of
						  * Baud rate generator divisor
						  */
#define MAX3107_BRGCFG_2XMODE_BIT	(1 << 4) /* Double baud rate */
#define MAX3107_BRGCFG_4XMODE_BIT	(1 << 5) /* Quadruple baud rate */
#define MAX3107_BRGCFG_UNDEF6_BIT	(1 << 6) /* Undefined/not used */
#define MAX3107_BRGCFG_UNDEF7_BIT	(1 << 7) /* Undefined/not used */

/* Clock source register bits */
#define MAX3107_CLKSRC_INTOSC_BIT	(1 << 0) /* Internal osc enable */
#define MAX3107_CLKSRC_CRYST_BIT	(1 << 1) /* Crystal osc enable */
#define MAX3107_CLKSRC_PLL_BIT		(1 << 2) /* PLL enable */
#define MAX3107_CLKSRC_PLLBYP_BIT	(1 << 3) /* PLL bypass */
#define MAX3107_CLKSRC_EXTCLK_BIT	(1 << 4) /* External clock enable */
#define MAX3107_CLKSRC_UNDEF5_BIT	(1 << 5) /* Undefined/not used */
#define MAX3107_CLKSRC_UNDEF6_BIT	(1 << 6) /* Undefined/not used */
#define MAX3107_CLKSRC_CLK2RTS_BIT	(1 << 7) /* Baud clk to RTS pin */


/* HW definitions */
#define MAX3107_RX_FIFO_SIZE	128
#define MAX3107_TX_FIFO_SIZE	128
#define MAX3107_REVID1		0x00a0
#define MAX3107_REVID2		0x00a1


/* Baud rate generator configuration values for external clock 13MHz */
#define MAX3107_BRG13_B300	(0x0A9400 | 0x05)
#define MAX3107_BRG13_B600	(0x054A00 | 0x03)
#define MAX3107_BRG13_B1200	(0x02A500 | 0x01)
#define MAX3107_BRG13_B2400	(0x015200 | 0x09)
#define MAX3107_BRG13_B4800	(0x00A900 | 0x04)
#define MAX3107_BRG13_B9600	(0x005400 | 0x0A)
#define MAX3107_BRG13_B19200	(0x002A00 | 0x05)
#define MAX3107_BRG13_B38400	(0x001500 | 0x03)
#define MAX3107_BRG13_B57600	(0x000E00 | 0x02)
#define MAX3107_BRG13_B115200	(0x000700 | 0x01)
#define MAX3107_BRG13_B230400	(0x000300 | 0x08)
#define MAX3107_BRG13_B460800	(0x000100 | 0x0c)
#define MAX3107_BRG13_B921600	(0x000100 | 0x1c)

/* Baud rate generator configuration values for external clock 26MHz */
#define MAX3107_BRG26_B300	(0x152800 | 0x0A)
#define MAX3107_BRG26_B600	(0x0A9400 | 0x05)
#define MAX3107_BRG26_B1200	(0x054A00 | 0x03)
#define MAX3107_BRG26_B2400	(0x02A500 | 0x01)
#define MAX3107_BRG26_B4800	(0x015200 | 0x09)
#define MAX3107_BRG26_B9600	(0x00A900 | 0x04)
#define MAX3107_BRG26_B19200	(0x005400 | 0x0A)
#define MAX3107_BRG26_B38400	(0x002A00 | 0x05)
#define MAX3107_BRG26_B57600	(0x001C00 | 0x03)
#define MAX3107_BRG26_B115200	(0x000E00 | 0x02)
#define MAX3107_BRG26_B230400	(0x000700 | 0x01)
#define MAX3107_BRG26_B460800	(0x000300 | 0x08)
#define MAX3107_BRG26_B921600	(0x000100 | 0x0C)

/* Baud rate generator configuration values for internal clock */
#define MAX3107_BRG13_IB300	(0x008000 | 0x00)
#define MAX3107_BRG13_IB600	(0x004000 | 0x00)
#define MAX3107_BRG13_IB1200	(0x002000 | 0x00)
#define MAX3107_BRG13_IB2400	(0x001000 | 0x00)
#define MAX3107_BRG13_IB4800	(0x000800 | 0x00)
#define MAX3107_BRG13_IB9600	(0x000400 | 0x00)
#define MAX3107_BRG13_IB19200	(0x000200 | 0x00)
#define MAX3107_BRG13_IB38400	(0x000100 | 0x00)
#define MAX3107_BRG13_IB57600	(0x000000 | 0x0B)
#define MAX3107_BRG13_IB115200	(0x000000 | 0x05)
#define MAX3107_BRG13_IB230400	(0x000000 | 0x03)
#define MAX3107_BRG13_IB460800	(0x000000 | 0x00)
#define MAX3107_BRG13_IB921600	(0x000000 | 0x00)


struct baud_table {
	int baud;
	u32 new_brg;
};

struct max3107_port {
	/* UART port structure */
	struct uart_port port;

	/* SPI device structure */
	struct spi_device *spi;

#if defined(CONFIG_GPIOLIB)
	/* GPIO chip stucture */
	struct gpio_chip chip;
#endif

	/* Workqueue that does all the magic */
	struct workqueue_struct *workqueue;
	struct work_struct work;

	/* Lock for shared data */
	spinlock_t data_lock;

	/* Device configuration */
	int ext_clk;		/* 1 if external clock used */
	int loopback;		/* Current loopback mode state */
	int baud;			/* Current baud rate */

	/* State flags */
	int suspended;		/* Indicates suspend mode */
	int tx_fifo_empty;	/* Flag for TX FIFO state */
	int rx_enabled;		/* Flag for receiver state */
	int tx_enabled;		/* Flag for transmitter state */

	u16 irqen_reg;		/* Current IRQ enable register value */
	/* Shared data */
	u16 mode1_reg;		/* Current mode1 register value*/
	int mode1_commit;	/* Flag for setting new mode1 register value */
	u16 lcr_reg;		/* Current LCR register value */
	int lcr_commit;		/* Flag for setting new LCR register value */
	u32 brg_cfg;		/* Current Baud rate generator config  */
	int brg_commit;		/* Flag for setting new baud rate generator
				 * config
				 */
	struct baud_table *baud_tbl;
	int handle_irq;		/* Indicates that IRQ should be handled */

	/* Rx buffer and str*/
	u16 *rxbuf;
	u8  *rxstr;
	/* Tx buffer*/
	u16 *txbuf;

	struct max3107_plat *pdata;	/* Platform data */
};

/* Platform data structure */
struct max3107_plat {
	/* Loopback mode enable */
	int loopback;
	/* External clock enable */
	int ext_clk;
	/* Called during the register initialisation */
	void (*init)(struct max3107_port *s);
	/* Called when the port is found and configured */
	int (*configure)(struct max3107_port *s);
	/* HW suspend function */
	void (*hw_suspend) (struct max3107_port *s, int suspend);
	/* Polling mode enable */
	int polled_mode;
	/* Polling period if polling mode enabled */
	int poll_time;
};

extern int max3107_rw(struct max3107_port *s, u8 *tx, u8 *rx, int len);
extern void max3107_hw_susp(struct max3107_port *s, int suspend);
extern int max3107_probe(struct spi_device *spi, struct max3107_plat *pdata);
extern int max3107_remove(struct spi_device *spi);
extern int max3107_suspend(struct spi_device *spi, pm_message_t state);
extern int max3107_resume(struct spi_device *spi);

#endif /* _LINUX_SERIAL_MAX3107_H */
