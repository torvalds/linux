/*
 * Drivers for CSR SiRFprimaII onboard UARTs.
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */
#include <linux/bitops.h>

/* UART Register Offset Define */
#define SIRFUART_LINE_CTRL			0x0040
#define SIRFUART_TX_RX_EN			0x004c
#define SIRFUART_DIVISOR			0x0050
#define SIRFUART_INT_EN				0x0054
#define SIRFUART_INT_STATUS			0x0058
#define SIRFUART_TX_DMA_IO_CTRL			0x0100
#define SIRFUART_TX_DMA_IO_LEN			0x0104
#define SIRFUART_TX_FIFO_CTRL			0x0108
#define SIRFUART_TX_FIFO_LEVEL_CHK		0x010C
#define SIRFUART_TX_FIFO_OP			0x0110
#define SIRFUART_TX_FIFO_STATUS			0x0114
#define SIRFUART_TX_FIFO_DATA			0x0118
#define SIRFUART_RX_DMA_IO_CTRL			0x0120
#define SIRFUART_RX_DMA_IO_LEN			0x0124
#define SIRFUART_RX_FIFO_CTRL			0x0128
#define SIRFUART_RX_FIFO_LEVEL_CHK		0x012C
#define SIRFUART_RX_FIFO_OP			0x0130
#define SIRFUART_RX_FIFO_STATUS			0x0134
#define SIRFUART_RX_FIFO_DATA			0x0138
#define SIRFUART_AFC_CTRL			0x0140
#define SIRFUART_SWH_DMA_IO			0x0148

/* UART Line Control Register */
#define SIRFUART_DATA_BIT_LEN_MASK		0x3
#define SIRFUART_DATA_BIT_LEN_5			BIT(0)
#define SIRFUART_DATA_BIT_LEN_6			1
#define SIRFUART_DATA_BIT_LEN_7			2
#define SIRFUART_DATA_BIT_LEN_8			3
#define SIRFUART_STOP_BIT_LEN_1			0
#define SIRFUART_STOP_BIT_LEN_2			BIT(2)
#define SIRFUART_PARITY_EN			BIT(3)
#define SIRFUART_EVEN_BIT			BIT(4)
#define SIRFUART_STICK_BIT_MASK			(7 << 3)
#define SIRFUART_STICK_BIT_NONE			(0 << 3)
#define SIRFUART_STICK_BIT_EVEN			BIT(3)
#define SIRFUART_STICK_BIT_ODD			(3 << 3)
#define SIRFUART_STICK_BIT_MARK			(5 << 3)
#define SIRFUART_STICK_BIT_SPACE		(7 << 3)
#define SIRFUART_SET_BREAK			BIT(6)
#define SIRFUART_LOOP_BACK			BIT(7)
#define SIRFUART_PARITY_MASK			(7 << 3)
#define SIRFUART_DUMMY_READ			BIT(16)

#define SIRFSOC_UART_RX_TIMEOUT(br, to)	(((br) * (((to) + 999) / 1000)) / 1000)
#define SIRFUART_RECV_TIMEOUT_MASK	(0xFFFF << 16)
#define SIRFUART_RECV_TIMEOUT(x)	(((x) & 0xFFFF) << 16)

/* UART Auto Flow Control */
#define SIRFUART_AFC_RX_THD_MASK		0x000000FF
#define SIRFUART_AFC_RX_EN			BIT(8)
#define SIRFUART_AFC_TX_EN			BIT(9)
#define SIRFUART_CTS_CTRL			BIT(10)
#define SIRFUART_RTS_CTRL			BIT(11)
#define SIRFUART_CTS_IN_STATUS			BIT(12)
#define SIRFUART_RTS_OUT_STATUS			BIT(13)

/* UART Interrupt Enable Register */
#define SIRFUART_RX_DONE_INT			BIT(0)
#define SIRFUART_TX_DONE_INT			BIT(1)
#define SIRFUART_RX_OFLOW_INT			BIT(2)
#define SIRFUART_TX_ALLOUT_INT			BIT(3)
#define SIRFUART_RX_IO_DMA_INT			BIT(4)
#define SIRFUART_TX_IO_DMA_INT			BIT(5)
#define SIRFUART_RXFIFO_FULL_INT		BIT(6)
#define SIRFUART_TXFIFO_EMPTY_INT		BIT(7)
#define SIRFUART_RXFIFO_THD_INT			BIT(8)
#define SIRFUART_TXFIFO_THD_INT			BIT(9)
#define SIRFUART_FRM_ERR_INT			BIT(10)
#define SIRFUART_RXD_BREAK_INT			BIT(11)
#define SIRFUART_RX_TIMEOUT_INT			BIT(12)
#define SIRFUART_PARITY_ERR_INT			BIT(13)
#define SIRFUART_CTS_INT_EN			BIT(14)
#define SIRFUART_RTS_INT_EN			BIT(15)

/* UART Interrupt Status Register */
#define SIRFUART_RX_DONE			BIT(0)
#define SIRFUART_TX_DONE			BIT(1)
#define SIRFUART_RX_OFLOW			BIT(2)
#define SIRFUART_TX_ALL_EMPTY			BIT(3)
#define SIRFUART_DMA_IO_RX_DONE			BIT(4)
#define SIRFUART_DMA_IO_TX_DONE			BIT(5)
#define SIRFUART_RXFIFO_FULL			BIT(6)
#define SIRFUART_TXFIFO_EMPTY			BIT(7)
#define SIRFUART_RXFIFO_THD_REACH		BIT(8)
#define SIRFUART_TXFIFO_THD_REACH		BIT(9)
#define SIRFUART_FRM_ERR			BIT(10)
#define SIRFUART_RXD_BREAK			BIT(11)
#define SIRFUART_RX_TIMEOUT			BIT(12)
#define SIRFUART_PARITY_ERR			BIT(13)
#define SIRFUART_CTS_CHANGE			BIT(14)
#define SIRFUART_RTS_CHANGE			BIT(15)
#define SIRFUART_PLUG_IN			BIT(16)

#define SIRFUART_ERR_INT_STAT					\
				(SIRFUART_RX_OFLOW |		\
				SIRFUART_FRM_ERR |		\
				SIRFUART_RXD_BREAK |		\
				SIRFUART_PARITY_ERR)
#define SIRFUART_ERR_INT_EN					\
				(SIRFUART_RX_OFLOW_INT |	\
				SIRFUART_FRM_ERR_INT |		\
				SIRFUART_RXD_BREAK_INT |	\
				SIRFUART_PARITY_ERR_INT)
#define SIRFUART_TX_INT_EN	SIRFUART_TXFIFO_EMPTY_INT
#define SIRFUART_RX_IO_INT_EN					\
				(SIRFUART_RX_TIMEOUT_INT |	\
				SIRFUART_RXFIFO_THD_INT |	\
				SIRFUART_RXFIFO_FULL_INT |	\
				SIRFUART_ERR_INT_EN)

/* UART FIFO Register */
#define SIRFUART_TX_FIFO_STOP			0x0
#define SIRFUART_TX_FIFO_RESET			0x1
#define SIRFUART_TX_FIFO_START			0x2
#define SIRFUART_RX_FIFO_STOP			0x0
#define SIRFUART_RX_FIFO_RESET			0x1
#define SIRFUART_RX_FIFO_START			0x2
#define SIRFUART_TX_MODE_DMA			0
#define SIRFUART_TX_MODE_IO			1
#define SIRFUART_RX_MODE_DMA			0
#define SIRFUART_RX_MODE_IO			1

#define SIRFUART_RX_EN				0x1
#define SIRFUART_TX_EN				0x2

/* Generic Definitions */
#define SIRFSOC_UART_NAME			"ttySiRF"
#define SIRFSOC_UART_MAJOR			0
#define SIRFSOC_UART_MINOR			0
#define SIRFUART_PORT_NAME			"sirfsoc-uart"
#define SIRFUART_MAP_SIZE			0x200
#define SIRFSOC_UART_NR				3
#define SIRFSOC_PORT_TYPE			0xa5

/* Baud Rate Calculation */
#define SIRF_MIN_SAMPLE_DIV			0xf
#define SIRF_MAX_SAMPLE_DIV			0x3f
#define SIRF_IOCLK_DIV_MAX			0xffff
#define SIRF_SAMPLE_DIV_SHIFT			16
#define SIRF_IOCLK_DIV_MASK			0xffff
#define SIRF_SAMPLE_DIV_MASK			0x3f0000
#define SIRF_BAUD_RATE_SUPPORT_NR		18

/* For Fast Baud Rate Calculation */
struct sirfsoc_baudrate_to_regv {
	unsigned int baud_rate;
	unsigned int reg_val;
};

struct sirfsoc_uart_port {
	unsigned char			hw_flow_ctrl;
	unsigned char			ms_enabled;

	struct uart_port		port;
	struct pinctrl			*p;
};

/* Hardware Flow Control */
#define SIRFUART_AFC_CTRL_RX_THD	0x70

/* Register Access Control */
#define portaddr(port, reg)		((port)->membase + (reg))
#define rd_regb(port, reg)		(__raw_readb(portaddr(port, reg)))
#define rd_regl(port, reg)		(__raw_readl(portaddr(port, reg)))
#define wr_regb(port, reg, val)		__raw_writeb(val, portaddr(port, reg))
#define wr_regl(port, reg, val)		__raw_writel(val, portaddr(port, reg))

/* UART Port Mask */
#define SIRFUART_FIFOLEVEL_MASK(port)	((port->line == 1) ? (0x1f) : (0x7f))
#define SIRFUART_FIFOFULL_MASK(port)	((port->line == 1) ? (0x20) : (0x80))
#define SIRFUART_FIFOEMPTY_MASK(port)	((port->line == 1) ? (0x40) : (0x100))

/* I/O Mode */
#define SIRFSOC_UART_IO_RX_MAX_CNT		256
#define SIRFSOC_UART_IO_TX_REASONABLE_CNT	6
