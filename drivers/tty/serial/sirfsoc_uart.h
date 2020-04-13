/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Drivers for CSR SiRFprimaII onboard UARTs.
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 */
#include <linux/bitops.h>
#include <linux/log2.h>
#include <linux/hrtimer.h>
struct sirfsoc_uart_param {
	const char *uart_name;
	const char *port_name;
};

struct sirfsoc_register {
	/* hardware uart specific */
	u32 sirfsoc_line_ctrl;
	u32 sirfsoc_divisor;
	/* uart - usp common */
	u32 sirfsoc_tx_rx_en;
	u32 sirfsoc_int_en_reg;
	u32 sirfsoc_int_st_reg;
	u32 sirfsoc_int_en_clr_reg;
	u32 sirfsoc_tx_dma_io_ctrl;
	u32 sirfsoc_tx_dma_io_len;
	u32 sirfsoc_tx_fifo_ctrl;
	u32 sirfsoc_tx_fifo_level_chk;
	u32 sirfsoc_tx_fifo_op;
	u32 sirfsoc_tx_fifo_status;
	u32 sirfsoc_tx_fifo_data;
	u32 sirfsoc_rx_dma_io_ctrl;
	u32 sirfsoc_rx_dma_io_len;
	u32 sirfsoc_rx_fifo_ctrl;
	u32 sirfsoc_rx_fifo_level_chk;
	u32 sirfsoc_rx_fifo_op;
	u32 sirfsoc_rx_fifo_status;
	u32 sirfsoc_rx_fifo_data;
	u32 sirfsoc_afc_ctrl;
	u32 sirfsoc_swh_dma_io;
	/* hardware usp specific */
	u32 sirfsoc_mode1;
	u32 sirfsoc_mode2;
	u32 sirfsoc_tx_frame_ctrl;
	u32 sirfsoc_rx_frame_ctrl;
	u32 sirfsoc_async_param_reg;
};

typedef u32 (*fifo_full_mask)(struct uart_port *port);
typedef u32 (*fifo_empty_mask)(struct uart_port *port);

struct sirfsoc_fifo_status {
	fifo_full_mask ff_full;
	fifo_empty_mask ff_empty;
};

struct sirfsoc_int_en {
	u32 sirfsoc_rx_done_en;
	u32 sirfsoc_tx_done_en;
	u32 sirfsoc_rx_oflow_en;
	u32 sirfsoc_tx_allout_en;
	u32 sirfsoc_rx_io_dma_en;
	u32 sirfsoc_tx_io_dma_en;
	u32 sirfsoc_rxfifo_full_en;
	u32 sirfsoc_txfifo_empty_en;
	u32 sirfsoc_rxfifo_thd_en;
	u32 sirfsoc_txfifo_thd_en;
	u32 sirfsoc_frm_err_en;
	u32 sirfsoc_rxd_brk_en;
	u32 sirfsoc_rx_timeout_en;
	u32 sirfsoc_parity_err_en;
	u32 sirfsoc_cts_en;
	u32 sirfsoc_rts_en;
};

struct sirfsoc_int_status {
	u32 sirfsoc_rx_done;
	u32 sirfsoc_tx_done;
	u32 sirfsoc_rx_oflow;
	u32 sirfsoc_tx_allout;
	u32 sirfsoc_rx_io_dma;
	u32 sirfsoc_tx_io_dma;
	u32 sirfsoc_rxfifo_full;
	u32 sirfsoc_txfifo_empty;
	u32 sirfsoc_rxfifo_thd;
	u32 sirfsoc_txfifo_thd;
	u32 sirfsoc_frm_err;
	u32 sirfsoc_rxd_brk;
	u32 sirfsoc_rx_timeout;
	u32 sirfsoc_parity_err;
	u32 sirfsoc_cts;
	u32 sirfsoc_rts;
};

enum sirfsoc_uart_type {
	SIRF_REAL_UART,
	SIRF_USP_UART,
};

struct sirfsoc_uart_register {
	struct sirfsoc_register uart_reg;
	struct sirfsoc_int_en uart_int_en;
	struct sirfsoc_int_status uart_int_st;
	struct sirfsoc_fifo_status fifo_status;
	struct sirfsoc_uart_param uart_param;
	enum sirfsoc_uart_type uart_type;
};

static u32 uart_usp_ff_full_mask(struct uart_port *port)
{
	u32 full_bit;

	full_bit = ilog2(port->fifosize);
	return (1 << full_bit);
}

static u32 uart_usp_ff_empty_mask(struct uart_port *port)
{
	u32 empty_bit;

	empty_bit = ilog2(port->fifosize) + 1;
	return (1 << empty_bit);
}

static struct sirfsoc_uart_register sirfsoc_usp = {
	.uart_reg = {
		.sirfsoc_mode1		= 0x0000,
		.sirfsoc_mode2		= 0x0004,
		.sirfsoc_tx_frame_ctrl	= 0x0008,
		.sirfsoc_rx_frame_ctrl	= 0x000c,
		.sirfsoc_tx_rx_en	= 0x0010,
		.sirfsoc_int_en_reg	= 0x0014,
		.sirfsoc_int_st_reg	= 0x0018,
		.sirfsoc_async_param_reg = 0x0024,
		.sirfsoc_tx_dma_io_ctrl	= 0x0100,
		.sirfsoc_tx_dma_io_len	= 0x0104,
		.sirfsoc_tx_fifo_ctrl	= 0x0108,
		.sirfsoc_tx_fifo_level_chk = 0x010c,
		.sirfsoc_tx_fifo_op	= 0x0110,
		.sirfsoc_tx_fifo_status	= 0x0114,
		.sirfsoc_tx_fifo_data	= 0x0118,
		.sirfsoc_rx_dma_io_ctrl	= 0x0120,
		.sirfsoc_rx_dma_io_len	= 0x0124,
		.sirfsoc_rx_fifo_ctrl	= 0x0128,
		.sirfsoc_rx_fifo_level_chk = 0x012c,
		.sirfsoc_rx_fifo_op	= 0x0130,
		.sirfsoc_rx_fifo_status	= 0x0134,
		.sirfsoc_rx_fifo_data	= 0x0138,
		.sirfsoc_int_en_clr_reg = 0x140,
	},
	.uart_int_en = {
		.sirfsoc_rx_done_en	= BIT(0),
		.sirfsoc_tx_done_en	= BIT(1),
		.sirfsoc_rx_oflow_en	= BIT(2),
		.sirfsoc_tx_allout_en	= BIT(3),
		.sirfsoc_rx_io_dma_en	= BIT(4),
		.sirfsoc_tx_io_dma_en	= BIT(5),
		.sirfsoc_rxfifo_full_en	= BIT(6),
		.sirfsoc_txfifo_empty_en = BIT(7),
		.sirfsoc_rxfifo_thd_en	= BIT(8),
		.sirfsoc_txfifo_thd_en	= BIT(9),
		.sirfsoc_frm_err_en	= BIT(10),
		.sirfsoc_rx_timeout_en	= BIT(11),
		.sirfsoc_rxd_brk_en	= BIT(15),
	},
	.uart_int_st = {
		.sirfsoc_rx_done	= BIT(0),
		.sirfsoc_tx_done	= BIT(1),
		.sirfsoc_rx_oflow	= BIT(2),
		.sirfsoc_tx_allout	= BIT(3),
		.sirfsoc_rx_io_dma	= BIT(4),
		.sirfsoc_tx_io_dma	= BIT(5),
		.sirfsoc_rxfifo_full	= BIT(6),
		.sirfsoc_txfifo_empty	= BIT(7),
		.sirfsoc_rxfifo_thd	= BIT(8),
		.sirfsoc_txfifo_thd	= BIT(9),
		.sirfsoc_frm_err	= BIT(10),
		.sirfsoc_rx_timeout	= BIT(11),
		.sirfsoc_rxd_brk	= BIT(15),
	},
	.fifo_status = {
		.ff_full		= uart_usp_ff_full_mask,
		.ff_empty		= uart_usp_ff_empty_mask,
	},
	.uart_param = {
		.uart_name = "ttySiRF",
		.port_name = "sirfsoc-uart",
	},
};

static struct sirfsoc_uart_register sirfsoc_uart = {
	.uart_reg = {
		.sirfsoc_line_ctrl	= 0x0040,
		.sirfsoc_tx_rx_en	= 0x004c,
		.sirfsoc_divisor	= 0x0050,
		.sirfsoc_int_en_reg	= 0x0054,
		.sirfsoc_int_st_reg	= 0x0058,
		.sirfsoc_int_en_clr_reg	= 0x0060,
		.sirfsoc_tx_dma_io_ctrl	= 0x0100,
		.sirfsoc_tx_dma_io_len	= 0x0104,
		.sirfsoc_tx_fifo_ctrl	= 0x0108,
		.sirfsoc_tx_fifo_level_chk = 0x010c,
		.sirfsoc_tx_fifo_op	= 0x0110,
		.sirfsoc_tx_fifo_status	= 0x0114,
		.sirfsoc_tx_fifo_data	= 0x0118,
		.sirfsoc_rx_dma_io_ctrl	= 0x0120,
		.sirfsoc_rx_dma_io_len	= 0x0124,
		.sirfsoc_rx_fifo_ctrl	= 0x0128,
		.sirfsoc_rx_fifo_level_chk = 0x012c,
		.sirfsoc_rx_fifo_op	= 0x0130,
		.sirfsoc_rx_fifo_status	= 0x0134,
		.sirfsoc_rx_fifo_data	= 0x0138,
		.sirfsoc_afc_ctrl	= 0x0140,
		.sirfsoc_swh_dma_io	= 0x0148,
	},
	.uart_int_en = {
		.sirfsoc_rx_done_en	= BIT(0),
		.sirfsoc_tx_done_en	= BIT(1),
		.sirfsoc_rx_oflow_en	= BIT(2),
		.sirfsoc_tx_allout_en	= BIT(3),
		.sirfsoc_rx_io_dma_en	= BIT(4),
		.sirfsoc_tx_io_dma_en	= BIT(5),
		.sirfsoc_rxfifo_full_en	= BIT(6),
		.sirfsoc_txfifo_empty_en = BIT(7),
		.sirfsoc_rxfifo_thd_en	= BIT(8),
		.sirfsoc_txfifo_thd_en	= BIT(9),
		.sirfsoc_frm_err_en	= BIT(10),
		.sirfsoc_rxd_brk_en	= BIT(11),
		.sirfsoc_rx_timeout_en	= BIT(12),
		.sirfsoc_parity_err_en	= BIT(13),
		.sirfsoc_cts_en		= BIT(14),
		.sirfsoc_rts_en		= BIT(15),
	},
	.uart_int_st = {
		.sirfsoc_rx_done	= BIT(0),
		.sirfsoc_tx_done	= BIT(1),
		.sirfsoc_rx_oflow	= BIT(2),
		.sirfsoc_tx_allout	= BIT(3),
		.sirfsoc_rx_io_dma	= BIT(4),
		.sirfsoc_tx_io_dma	= BIT(5),
		.sirfsoc_rxfifo_full	= BIT(6),
		.sirfsoc_txfifo_empty	= BIT(7),
		.sirfsoc_rxfifo_thd	= BIT(8),
		.sirfsoc_txfifo_thd	= BIT(9),
		.sirfsoc_frm_err	= BIT(10),
		.sirfsoc_rxd_brk	= BIT(11),
		.sirfsoc_rx_timeout	= BIT(12),
		.sirfsoc_parity_err	= BIT(13),
		.sirfsoc_cts		= BIT(14),
		.sirfsoc_rts		= BIT(15),
	},
	.fifo_status = {
		.ff_full		= uart_usp_ff_full_mask,
		.ff_empty		= uart_usp_ff_empty_mask,
	},
	.uart_param = {
		.uart_name = "ttySiRF",
		.port_name = "sirfsoc_uart",
	},
};
/* uart io ctrl */
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
#define SIRFUART_AFC_CTRL_RX_THD		0x70
#define SIRFUART_AFC_RX_EN			BIT(8)
#define SIRFUART_AFC_TX_EN			BIT(9)
#define SIRFUART_AFC_CTS_CTRL			BIT(10)
#define SIRFUART_AFC_RTS_CTRL			BIT(11)
#define	SIRFUART_AFC_CTS_STATUS			BIT(12)
#define	SIRFUART_AFC_RTS_STATUS			BIT(13)
/* UART FIFO Register */
#define SIRFUART_FIFO_STOP			0x0
#define SIRFUART_FIFO_RESET			BIT(0)
#define SIRFUART_FIFO_START			BIT(1)

#define SIRFUART_RX_EN				BIT(0)
#define SIRFUART_TX_EN				BIT(1)

#define SIRFUART_IO_MODE			BIT(0)
#define SIRFUART_DMA_MODE			0x0
#define SIRFUART_RX_DMA_FLUSH			0x4

#define SIRFUART_CLEAR_RX_ADDR_EN		0x2
/* Baud Rate Calculation */
#define SIRF_USP_MIN_SAMPLE_DIV			0x1
#define SIRF_MIN_SAMPLE_DIV			0xf
#define SIRF_MAX_SAMPLE_DIV			0x3f
#define SIRF_IOCLK_DIV_MAX			0xffff
#define SIRF_SAMPLE_DIV_SHIFT			16
#define SIRF_IOCLK_DIV_MASK			0xffff
#define SIRF_SAMPLE_DIV_MASK			0x3f0000
#define SIRF_BAUD_RATE_SUPPORT_NR		18

/* USP SPEC */
#define SIRFSOC_USP_ENDIAN_CTRL_LSBF		BIT(4)
#define SIRFSOC_USP_EN				BIT(5)
#define SIRFSOC_USP_MODE2_RXD_DELAY_OFFSET	0
#define SIRFSOC_USP_MODE2_TXD_DELAY_OFFSET	8
#define SIRFSOC_USP_MODE2_CLK_DIVISOR_MASK	0x3ff
#define SIRFSOC_USP_MODE2_CLK_DIVISOR_OFFSET	21
#define SIRFSOC_USP_TX_DATA_LEN_OFFSET		0
#define SIRFSOC_USP_TX_SYNC_LEN_OFFSET		8
#define SIRFSOC_USP_TX_FRAME_LEN_OFFSET		16
#define SIRFSOC_USP_TX_SHIFTER_LEN_OFFSET	24
#define SIRFSOC_USP_TX_CLK_DIVISOR_OFFSET	30
#define SIRFSOC_USP_RX_DATA_LEN_OFFSET		0
#define SIRFSOC_USP_RX_FRAME_LEN_OFFSET		8
#define SIRFSOC_USP_RX_SHIFTER_LEN_OFFSET	16
#define SIRFSOC_USP_RX_CLK_DIVISOR_OFFSET	24
#define SIRFSOC_USP_ASYNC_DIV2_MASK		0x3f
#define SIRFSOC_USP_ASYNC_DIV2_OFFSET		16
#define SIRFSOC_USP_LOOP_BACK_CTRL		BIT(2)
#define SIRFSOC_USP_FRADDR_CLR_EN		BIT(1)
/* USP-UART Common */
#define SIRFSOC_UART_RX_TIMEOUT(br, to)	(((br) * (((to) + 999) / 1000)) / 1000)
#define SIRFUART_RECV_TIMEOUT_VALUE(x)	\
				(((x) > 0xFFFF) ? 0xFFFF : ((x) & 0xFFFF))
#define SIRFUART_USP_RECV_TIMEOUT(x)	(x & 0xFFFF)
#define SIRFUART_UART_RECV_TIMEOUT(x)	((x & 0xFFFF) << 16)

#define SIRFUART_FIFO_THD(port)		(port->fifosize >> 1)
#define SIRFUART_ERR_INT_STAT(unit_st, uart_type)			\
				(uint_st->sirfsoc_rx_oflow |		\
				uint_st->sirfsoc_frm_err |		\
				uint_st->sirfsoc_rxd_brk |		\
				((uart_type != SIRF_REAL_UART) ? \
				 0 : uint_st->sirfsoc_parity_err))
#define SIRFUART_RX_IO_INT_EN(uint_en, uart_type)			\
				(uint_en->sirfsoc_rx_done_en |\
				 uint_en->sirfsoc_rxfifo_thd_en |\
				 uint_en->sirfsoc_rxfifo_full_en |\
				 uint_en->sirfsoc_frm_err_en |\
				 uint_en->sirfsoc_rx_oflow_en |\
				 uint_en->sirfsoc_rxd_brk_en |\
				((uart_type != SIRF_REAL_UART) ? \
				 0 : uint_en->sirfsoc_parity_err_en))
#define SIRFUART_RX_IO_INT_ST(uint_st)				\
				(uint_st->sirfsoc_rxfifo_thd |\
				 uint_st->sirfsoc_rxfifo_full|\
				 uint_st->sirfsoc_rx_done |\
				 uint_st->sirfsoc_rx_timeout)
#define SIRFUART_CTS_INT_ST(uint_st)	(uint_st->sirfsoc_cts)
#define SIRFUART_RX_DMA_INT_EN(uint_en, uart_type)		\
				(uint_en->sirfsoc_frm_err_en |\
				 uint_en->sirfsoc_rx_oflow_en |\
				 uint_en->sirfsoc_rxd_brk_en |\
				((uart_type != SIRF_REAL_UART) ? \
				 0 : uint_en->sirfsoc_parity_err_en))
/* Generic Definitions */
#define SIRFSOC_UART_NAME			"ttySiRF"
#define SIRFSOC_UART_MAJOR			0
#define SIRFSOC_UART_MINOR			0
#define SIRFUART_PORT_NAME			"sirfsoc-uart"
#define SIRFUART_MAP_SIZE			0x200
#define SIRFSOC_UART_NR				11
#define SIRFSOC_PORT_TYPE			0xa5

/* Uart Common Use Macro*/
#define SIRFSOC_RX_DMA_BUF_SIZE		(1024 * 32)
#define BYTES_TO_ALIGN(dma_addr)	((unsigned long)(dma_addr) & 0x3)
/* Uart Fifo Level Chk */
#define SIRFUART_TX_FIFO_SC_OFFSET	0
#define SIRFUART_TX_FIFO_LC_OFFSET	10
#define SIRFUART_TX_FIFO_HC_OFFSET	20
#define SIRFUART_TX_FIFO_CHK_SC(line, value) ((((line) == 1) ? (value & 0x3) :\
				(value & 0x1f)) << SIRFUART_TX_FIFO_SC_OFFSET)
#define SIRFUART_TX_FIFO_CHK_LC(line, value) ((((line) == 1) ? (value & 0x3) :\
				(value & 0x1f)) << SIRFUART_TX_FIFO_LC_OFFSET)
#define SIRFUART_TX_FIFO_CHK_HC(line, value) ((((line) == 1) ? (value & 0x3) :\
				(value & 0x1f)) << SIRFUART_TX_FIFO_HC_OFFSET)

#define SIRFUART_RX_FIFO_CHK_SC SIRFUART_TX_FIFO_CHK_SC
#define	SIRFUART_RX_FIFO_CHK_LC SIRFUART_TX_FIFO_CHK_LC
#define SIRFUART_RX_FIFO_CHK_HC SIRFUART_TX_FIFO_CHK_HC
#define SIRFUART_RX_FIFO_MASK 0x7f
/* Indicate how many buffers used */

/* For Fast Baud Rate Calculation */
struct sirfsoc_baudrate_to_regv {
	unsigned int baud_rate;
	unsigned int reg_val;
};

enum sirfsoc_tx_state {
	TX_DMA_IDLE,
	TX_DMA_RUNNING,
	TX_DMA_PAUSE,
};

struct sirfsoc_rx_buffer {
	struct circ_buf			xmit;
	dma_cookie_t			cookie;
	struct dma_async_tx_descriptor	*desc;
	dma_addr_t			dma_addr;
};

struct sirfsoc_uart_port {
	bool				hw_flow_ctrl;
	bool				ms_enabled;

	struct uart_port		port;
	struct clk			*clk;
	/* for SiRFatlas7, there are SET/CLR for UART_INT_EN */
	bool				is_atlas7;
	struct sirfsoc_uart_register	*uart_reg;
	struct dma_chan			*rx_dma_chan;
	struct dma_chan			*tx_dma_chan;
	dma_addr_t			tx_dma_addr;
	struct dma_async_tx_descriptor	*tx_dma_desc;
	unsigned long			transfer_size;
	enum sirfsoc_tx_state		tx_dma_state;
	unsigned int			cts_gpio;
	unsigned int			rts_gpio;

	struct sirfsoc_rx_buffer	rx_dma_items;
	struct hrtimer			hrt;
	bool				is_hrt_enabled;
	unsigned long			rx_period_time;
	unsigned long			rx_last_pos;
	unsigned long			pio_fetch_cnt;
};

/* Register Access Control */
#define portaddr(port, reg)		((port)->membase + (reg))
#define rd_regl(port, reg)		(__raw_readl(portaddr(port, reg)))
#define wr_regl(port, reg, val)		__raw_writel(val, portaddr(port, reg))

/* UART Port Mask */
#define SIRFUART_FIFOLEVEL_MASK(port)	((port->fifosize - 1) & 0xFFF)
#define SIRFUART_FIFOFULL_MASK(port)	(port->fifosize & 0xFFF)
#define SIRFUART_FIFOEMPTY_MASK(port)	((port->fifosize & 0xFFF) << 1)
