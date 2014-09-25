/*
 * Copyright (C) 2007 Google, Inc.
 * Author: Robert Love <rlove@google.com>
 * Copyright (c) 2011, Code Aurora Forum. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __DRIVERS_SERIAL_MSM_SERIAL_H
#define __DRIVERS_SERIAL_MSM_SERIAL_H

#define UART_MR1			0x0000

#define UART_MR1_AUTO_RFR_LEVEL0	0x3F
#define UART_MR1_AUTO_RFR_LEVEL1	0x3FF00
#define UART_MR1_RX_RDY_CTL    		(1 << 7)
#define UART_MR1_CTS_CTL       		(1 << 6)

#define UART_MR2			0x0004
#define UART_MR2_ERROR_MODE		(1 << 6)
#define UART_MR2_BITS_PER_CHAR		0x30
#define UART_MR2_BITS_PER_CHAR_5	(0x0 << 4)
#define UART_MR2_BITS_PER_CHAR_6	(0x1 << 4)
#define UART_MR2_BITS_PER_CHAR_7	(0x2 << 4)
#define UART_MR2_BITS_PER_CHAR_8	(0x3 << 4)
#define UART_MR2_STOP_BIT_LEN_ONE	(0x1 << 2)
#define UART_MR2_STOP_BIT_LEN_TWO	(0x3 << 2)
#define UART_MR2_PARITY_MODE_NONE	0x0
#define UART_MR2_PARITY_MODE_ODD	0x1
#define UART_MR2_PARITY_MODE_EVEN	0x2
#define UART_MR2_PARITY_MODE_SPACE	0x3
#define UART_MR2_PARITY_MODE		0x3

#define UART_CSR			0x0008

#define UART_TF		0x000C
#define UARTDM_TF	0x0070

#define UART_CR				0x0010
#define UART_CR_CMD_NULL		(0 << 4)
#define UART_CR_CMD_RESET_RX		(1 << 4)
#define UART_CR_CMD_RESET_TX		(2 << 4)
#define UART_CR_CMD_RESET_ERR		(3 << 4)
#define UART_CR_CMD_RESET_BREAK_INT	(4 << 4)
#define UART_CR_CMD_START_BREAK		(5 << 4)
#define UART_CR_CMD_STOP_BREAK		(6 << 4)
#define UART_CR_CMD_RESET_CTS		(7 << 4)
#define UART_CR_CMD_RESET_STALE_INT	(8 << 4)
#define UART_CR_CMD_PACKET_MODE		(9 << 4)
#define UART_CR_CMD_MODE_RESET		(12 << 4)
#define UART_CR_CMD_SET_RFR		(13 << 4)
#define UART_CR_CMD_RESET_RFR		(14 << 4)
#define UART_CR_CMD_PROTECTION_EN	(16 << 4)
#define UART_CR_CMD_STALE_EVENT_ENABLE	(80 << 4)
#define UART_CR_CMD_FORCE_STALE		(4 << 8)
#define UART_CR_CMD_RESET_TX_READY	(3 << 8)
#define UART_CR_TX_DISABLE		(1 << 3)
#define UART_CR_TX_ENABLE		(1 << 2)
#define UART_CR_RX_DISABLE		(1 << 1)
#define UART_CR_RX_ENABLE		(1 << 0)

#define UART_IMR		0x0014
#define UART_IMR_TXLEV		(1 << 0)
#define UART_IMR_RXSTALE	(1 << 3)
#define UART_IMR_RXLEV		(1 << 4)
#define UART_IMR_DELTA_CTS	(1 << 5)
#define UART_IMR_CURRENT_CTS	(1 << 6)

#define UART_IPR_RXSTALE_LAST		0x20
#define UART_IPR_STALE_LSB		0x1F
#define UART_IPR_STALE_TIMEOUT_MSB	0x3FF80

#define UART_IPR	0x0018
#define UART_TFWR	0x001C
#define UART_RFWR	0x0020
#define UART_HCR	0x0024

#define UART_MREG		0x0028
#define UART_NREG		0x002C
#define UART_DREG		0x0030
#define UART_MNDREG		0x0034
#define UART_IRDA		0x0038
#define UART_MISR_MODE		0x0040
#define UART_MISR_RESET		0x0044
#define UART_MISR_EXPORT	0x0048
#define UART_MISR_VAL		0x004C
#define UART_TEST_CTRL		0x0050

#define UART_SR			0x0008
#define UART_SR_HUNT_CHAR	(1 << 7)
#define UART_SR_RX_BREAK	(1 << 6)
#define UART_SR_PAR_FRAME_ERR	(1 << 5)
#define UART_SR_OVERRUN		(1 << 4)
#define UART_SR_TX_EMPTY	(1 << 3)
#define UART_SR_TX_READY	(1 << 2)
#define UART_SR_RX_FULL		(1 << 1)
#define UART_SR_RX_READY	(1 << 0)

#define UART_RF			0x000C
#define UARTDM_RF		0x0070
#define UART_MISR		0x0010
#define UART_ISR		0x0014
#define UART_ISR_TX_READY	(1 << 7)

#define UARTDM_RXFS		0x50
#define UARTDM_RXFS_BUF_SHIFT	0x7
#define UARTDM_RXFS_BUF_MASK	0x7

#define UARTDM_DMEN		0x3C
#define UARTDM_DMEN_RX_SC_ENABLE BIT(5)
#define UARTDM_DMEN_TX_SC_ENABLE BIT(4)

#define UARTDM_DMRX		0x34
#define UARTDM_NCF_TX		0x40
#define UARTDM_RX_TOTAL_SNAP	0x38

#define UART_TO_MSM(uart_port)	((struct msm_port *) uart_port)

static inline
void msm_write(struct uart_port *port, unsigned int val, unsigned int off)
{
	writel_relaxed(val, port->membase + off);
}

static inline
unsigned int msm_read(struct uart_port *port, unsigned int off)
{
	return readl_relaxed(port->membase + off);
}

/*
 * Setup the MND registers to use the TCXO clock.
 */
static inline void msm_serial_set_mnd_regs_tcxo(struct uart_port *port)
{
	msm_write(port, 0x06, UART_MREG);
	msm_write(port, 0xF1, UART_NREG);
	msm_write(port, 0x0F, UART_DREG);
	msm_write(port, 0x1A, UART_MNDREG);
	port->uartclk = 1843200;
}

/*
 * Setup the MND registers to use the TCXO clock divided by 4.
 */
static inline void msm_serial_set_mnd_regs_tcxoby4(struct uart_port *port)
{
	msm_write(port, 0x18, UART_MREG);
	msm_write(port, 0xF6, UART_NREG);
	msm_write(port, 0x0F, UART_DREG);
	msm_write(port, 0x0A, UART_MNDREG);
	port->uartclk = 1843200;
}

static inline
void msm_serial_set_mnd_regs_from_uartclk(struct uart_port *port)
{
	if (port->uartclk == 19200000)
		msm_serial_set_mnd_regs_tcxo(port);
	else if (port->uartclk == 4800000)
		msm_serial_set_mnd_regs_tcxoby4(port);
}

/*
 * TROUT has a specific defect that makes it report it's uartclk
 * as 19.2Mhz (TCXO) when it's actually 4.8Mhz (TCXO/4). This special
 * cases TROUT to use the right clock.
 */
#ifdef CONFIG_MACH_TROUT
#define msm_serial_set_mnd_regs msm_serial_set_mnd_regs_tcxoby4
#else
#define msm_serial_set_mnd_regs msm_serial_set_mnd_regs_from_uartclk
#endif

#endif	/* __DRIVERS_SERIAL_MSM_SERIAL_H */
