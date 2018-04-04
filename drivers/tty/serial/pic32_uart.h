// SPDX-License-Identifier: GPL-2.0+
/*
 * PIC32 Integrated Serial Driver.
 *
 * Copyright (C) 2015 Microchip Technology, Inc.
 *
 * Authors:
 *   Sorin-Andrei Pistirica <andrei.pistirica@microchip.com>
 */
#ifndef __DT_PIC32_UART_H__
#define __DT_PIC32_UART_H__

#define PIC32_UART_DFLT_BRATE		(9600)
#define PIC32_UART_TX_FIFO_DEPTH	(8)
#define PIC32_UART_RX_FIFO_DEPTH	(8)

#define PIC32_UART_MODE		0x00
#define PIC32_UART_STA		0x10
#define PIC32_UART_TX		0x20
#define PIC32_UART_RX		0x30
#define PIC32_UART_BRG		0x40

struct pic32_console_opt {
	int baud;
	int parity;
	int bits;
	int flow;
};

/* struct pic32_sport - pic32 serial port descriptor
 * @port: uart port descriptor
 * @idx: port index
 * @irq_fault: virtual fault interrupt number
 * @irqflags_fault: flags related to fault irq
 * @irq_fault_name: irq fault name
 * @irq_rx: virtual rx interrupt number
 * @irqflags_rx: flags related to rx irq
 * @irq_rx_name: irq rx name
 * @irq_tx: virtual tx interrupt number
 * @irqflags_tx: : flags related to tx irq
 * @irq_tx_name: irq tx name
 * @cts_gpio: clear to send gpio
 * @dev: device descriptor
 **/
struct pic32_sport {
	struct uart_port port;
	struct pic32_console_opt opt;
	int idx;

	int irq_fault;
	int irqflags_fault;
	const char *irq_fault_name;
	int irq_rx;
	int irqflags_rx;
	const char *irq_rx_name;
	int irq_tx;
	int irqflags_tx;
	const char *irq_tx_name;
	u8 enable_tx_irq;

	bool hw_flow_ctrl;
	int cts_gpio;

	int ref_clk;
	struct clk *clk;

	struct device *dev;
};
#define to_pic32_sport(c) container_of(c, struct pic32_sport, port)
#define pic32_get_port(sport) (&sport->port)
#define pic32_get_opt(sport) (&sport->opt)
#define tx_irq_enabled(sport) (sport->enable_tx_irq)

static inline void pic32_uart_writel(struct pic32_sport *sport,
					u32 reg, u32 val)
{
	struct uart_port *port = pic32_get_port(sport);

	__raw_writel(val, port->membase + reg);
}

static inline u32 pic32_uart_readl(struct pic32_sport *sport, u32 reg)
{
	struct uart_port *port = pic32_get_port(sport);

	return	__raw_readl(port->membase + reg);
}

/* pic32 uart mode register bits */
#define PIC32_UART_MODE_ON        BIT(15)
#define PIC32_UART_MODE_FRZ       BIT(14)
#define PIC32_UART_MODE_SIDL      BIT(13)
#define PIC32_UART_MODE_IREN      BIT(12)
#define PIC32_UART_MODE_RTSMD     BIT(11)
#define PIC32_UART_MODE_RESV1     BIT(10)
#define PIC32_UART_MODE_UEN1      BIT(9)
#define PIC32_UART_MODE_UEN0      BIT(8)
#define PIC32_UART_MODE_WAKE      BIT(7)
#define PIC32_UART_MODE_LPBK      BIT(6)
#define PIC32_UART_MODE_ABAUD     BIT(5)
#define PIC32_UART_MODE_RXINV     BIT(4)
#define PIC32_UART_MODE_BRGH      BIT(3)
#define PIC32_UART_MODE_PDSEL1    BIT(2)
#define PIC32_UART_MODE_PDSEL0    BIT(1)
#define PIC32_UART_MODE_STSEL     BIT(0)

/* pic32 uart status register bits */
#define PIC32_UART_STA_UTXISEL1   BIT(15)
#define PIC32_UART_STA_UTXISEL0   BIT(14)
#define PIC32_UART_STA_UTXINV     BIT(13)
#define PIC32_UART_STA_URXEN      BIT(12)
#define PIC32_UART_STA_UTXBRK     BIT(11)
#define PIC32_UART_STA_UTXEN      BIT(10)
#define PIC32_UART_STA_UTXBF      BIT(9)
#define PIC32_UART_STA_TRMT       BIT(8)
#define PIC32_UART_STA_URXISEL1   BIT(7)
#define PIC32_UART_STA_URXISEL0   BIT(6)
#define PIC32_UART_STA_ADDEN      BIT(5)
#define PIC32_UART_STA_RIDLE      BIT(4)
#define PIC32_UART_STA_PERR       BIT(3)
#define PIC32_UART_STA_FERR       BIT(2)
#define PIC32_UART_STA_OERR       BIT(1)
#define PIC32_UART_STA_URXDA      BIT(0)

#endif /* __DT_PIC32_UART_H__ */
