/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/clk.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/mxc_sim_interface.h>
#include <linux/spinlock.h>
#include <linux/time.h>

#include <linux/io.h>

#define DRIVER_NAME "mxc_sim"
#define SIM_INTERNAL_CLK	(0)
#define SIM_RFU			(-1)

/* Transmit and receive buffer sizes */
#define SIM_XMT_BUFFER_SIZE	(300)
#define SIM_RCV_BUFFER_SIZE	(400)

#define SIM_TX_FIFO_DEPTH	(16)
#define SIM_RX_FIFO_DEPTH	(285)

#define TX_FIFO_THRESHOLD	(0x04)
#define RX_FIFO_THRESHOLD	(250)

/* Interface character references */
#define SIM_IFC_TXI(letter, number) (letter + number * 4)
#define SIM_IFC_TA1   SIM_IFC_TXI(0, 0)
#define SIM_IFC_TB1   SIM_IFC_TXI(0, 1)
#define SIM_IFC_TC1   SIM_IFC_TXI(0, 2)
#define SIM_IFC_TD1   SIM_IFC_TXI(0, 3)
#define SIM_IFC_TA2   SIM_IFC_TXI(1, 0)
#define SIM_IFC_TB2   SIM_IFC_TXI(1, 1)
#define SIM_IFC_TC2   SIM_IFC_TXI(1, 2)
#define SIM_IFC_TD2   SIM_IFC_TXI(1, 3)
#define SIM_IFC_TA3   SIM_IFC_TXI(2, 0)
#define SIM_IFC_TB3   SIM_IFC_TXI(2, 1)
#define SIM_IFC_TC3   SIM_IFC_TXI(2, 2)
#define SIM_IFC_TD3   SIM_IFC_TXI(2, 3)
#define SIM_IFC_TA4   SIM_IFC_TXI(3, 0)
#define SIM_IFC_TB4   SIM_IFC_TXI(3, 1)
#define SIM_IFC_TC4   SIM_IFC_TXI(3, 2)
#define SIM_IFC_TD4   SIM_IFC_TXI(3, 3)

/* ATR and OPS states */
#define SIM_STATE_REMOVED		(0)
#define SIM_STATE_DETECTED		(1)
#define SIM_STATE_ATR_RECEIVING		(2)
#define SIM_STATE_ATR_RECEIVED		(3)
#define SIM_STATE_XMTING		(4)
#define SIM_STATE_XMT_DONE		(5)
#define SIM_STATE_XMT_ERROR		(6)
#define SIM_STATE_RECEIVING		(7)
#define SIM_STATE_RECEIVE_DONE		(8)
#define SIM_STATE_RECEIVE_ERROR		(9)

/* Definitions of the offset of the SIM hardware registers */
#define PORT1_CNTL			(0x00)
#define SETUP				(0x04)
#define PORT1_DETECT			(0x08)
#define PORT1_XMT_BUF			(0x0C)
#define PORT1_RCV_BUF			(0x10)
#define PORT0_CNTL			(0x14)
#define CNTL				(0x18)
#define CLK_PRESCALER			(0x1C)
#define RCV_THRESHOLD			(0x20)
#define ENABLE				(0x24)
#define XMT_STATUS			(0x28)
#define RCV_STATUS			(0x2C)
#define INT_MASK			(0x30)
#define PORTO_XMT_BUF			(0x34)
#define PORT0_RCV_BUF			(0x38)
#define PORT0_DETECT			(0x3C)
#define DATA_FORMAT			(0x40)
#define XMT_THRESHOLD			(0x44)
#define GUARD_CNTL			(0x48)
#define OD_CONFIG			(0x4C)
#define RESET_CNTL			(0x50)
#define CHAR_WAIT			(0x54)
#define GPCNT				(0x58)
#define DIVISOR				(0x5C)
#define BWT				(0x60)
#define BGT				(0x64)
#define BWT_H				(0x68)
#define XMT_FIFO_STAT			(0x6C)
#define RCV_FIFO_CNT			(0x70)
#define RCV_FIFO_WPTR			(0x74)
#define RCV_FIFO_RPTR			(0x78)

/* SIM port[0|1]_cntl register bits */
#define SIM_PORT_CNTL_SFPD		(1 << 7)
#define SIM_PORT_CNTL_3VOLT		(1 << 6)
#define SIM_PORT_CNTL_SCSP		(1 << 5)
#define SIM_PORT_CNTL_SCEN		(1 << 4)
#define SIM_PORT_CNTL_SRST		(1 << 3)
#define SIM_PORT_CNTL_STEN		(1 << 2)
#define SIM_PORT_CNTL_SVEN		(1 << 1)
#define SIM_PORT_CNTL_SAPD		(1 << 0)

/* SIM od_config register bits */
#define SIM_OD_CONFIG_OD_P1		(1 << 1)
#define SIM_OD_CONFIG_OD_P0		(1 << 0)

/* SIM enable register bits */
#define SIM_ENABLE_XMTEN		(1 << 1)
#define SIM_ENABLE_RCVEN		(1 << 0)

/* SIM int_mask register bits */
#define SIM_INT_MASK_RFEM		(1 << 13)
#define SIM_INT_MASK_BGTM		(1 << 12)
#define SIM_INT_MASK_BWTM		(1 << 11)
#define SIM_INT_MASK_RTM		(1 << 10)
#define SIM_INT_MASK_CWTM		(1 << 9)
#define SIM_INT_MASK_GPCM		(1 << 8)
#define SIM_INT_MASK_TDTFM		(1 << 7)
#define SIM_INT_MASK_TFOM		(1 << 6)
#define SIM_INT_MASK_XTM		(1 << 5)
#define SIM_INT_MASK_TFEIM		(1 << 4)
#define SIM_INT_MASK_ETCIM		(1 << 3)
#define SIM_INT_MASK_OIM		(1 << 2)
#define SIM_INT_MASK_TCIM		(1 << 1)
#define SIM_INT_MASK_RIM		(1 << 0)

/* SIM xmt_status register bits */
#define SIM_XMT_STATUS_GPCNT		(1 << 8)
#define SIM_XMT_STATUS_TDTF		(1 << 7)
#define SIM_XMT_STATUS_TFO		(1 << 6)
#define SIM_XMT_STATUS_TC		(1 << 5)
#define SIM_XMT_STATUS_ETC		(1 << 4)
#define SIM_XMT_STATUS_TFE		(1 << 3)
#define SIM_XMT_STATUS_XTE		(1 << 0)

/* SIM rcv_status register bits */
#define SIM_RCV_STATUS_BGT		(1 << 11)
#define SIM_RCV_STATUS_BWT		(1 << 10)
#define SIM_RCV_STATUS_RTE		(1 << 9)
#define SIM_RCV_STATUS_CWT		(1 << 8)
#define SIM_RCV_STATUS_CRCOK		(1 << 7)
#define SIM_RCV_STATUS_LRCOK		(1 << 6)
#define SIM_RCV_STATUS_RDRF		(1 << 5)
#define SIM_RCV_STATUS_RFD		(1 << 4)
#define SIM_RCV_STATUS_RFE		(1 << 1)
#define SIM_RCV_STATUS_OEF		(1 << 0)

/* SIM cntl register bits */
#define SIM_CNTL_BWTEN			(1 << 15)
#define SIM_CNTL_XMT_CRC_LRC		(1 << 14)
#define SIM_CNTL_CRCEN			(1 << 13)
#define SIM_CNTL_LRCEN			(1 << 12)
#define SIM_CNTL_CWTEN			(1 << 11)
#define SIM_CNTL_SAMPLE12		(1 << 4)
#define SIM_CNTL_ONACK			(1 << 3)
#define SIM_CNTL_ANACK			(1 << 2)
#define SIM_CNTL_ICM			(1 << 1)
#define SIM_CNTL_GPCNT_CLK_SEL(x)	((x&0x03) << 9)
#define SIM_CNTL_GPCNT_CLK_SEL_MASK	(0x03 << 9)
#define SIM_CNTL_BAUD_SEL(x)		((x&0x07) << 6)
#define SIM_CNTL_BAUD_SEL_MASK		(0x07 << 6)

/* SIM rcv_threshold register bits */
#define SIM_RCV_THRESHOLD_RTH(x)	((x&0x0f) << 9)
#define SIM_RCV_THRESHOLD_RTH_MASK	(0x0f << 9)
#define SIM_RCV_THRESHOLD_RDT(x)	((x&0x1ff) << 0)
#define SIM_RCV_THRESHOLD_RDT_MASK	(0x1ff << 0)

/* SIM xmt_threshold register bits */
#define SIM_XMT_THRESHOLD_XTH(x)	((x&0x0f) << 4)
#define SIM_XMT_THRESHOLD_XTH_MASK	(0x0f << 4)
#define SIM_XMT_THRESHOLD_TDT(x)	((x&0x0f) << 0)
#define SIM_XMT_THRESHOLD_TDT_MASK	(0x0f << 0)

/* SIM guard_cntl register bits */
#define SIM_GUARD_CNTL_RCVR11		(1 << 8)
#define SIM_GIARD_CNTL_GETU(x)		(x&0xff)
#define SIM_GIARD_CNTL_GETU_MASK	(0xff)

/* SIM port[0|]_detect register bits */
#define SIM_PORT_DETECT_SPDS		(1 << 3)
#define SIM_PORT_DETECT_SPDP		(1 << 2)
#define SIM_PORT_DETECT_SDI		(1 << 1)
#define SIM_PORT_DETECT_SDIM		(1 << 0)

/* SIM RESET_CNTL register bits*/
#define SIM_RESET_CNTL_FLUSH_RCV	(1 << 0)
#define SIM_RESET_CNTL_FLUSH_XMT	(1 << 1)
#define SIM_RESET_CNTL_SOFT_RESET	(1 << 2)
#define SIM_RESET_CNTL_KILL_CLOCK	(1 << 3)
#define SIM_RESET_CNTL_DOZE		(1 << 4)
#define SIM_RESET_CNTL_STOP		(1 << 5)
#define SIM_RESET_CNTL_DEBUG		(1 << 6)

/*SIM receive buffer register error status*/
#define SIM_REC_CWT_ERROR		(1 << 10)
#define SIM_REC_FRAME_ERROR		(1 << 9)
#define SIM_REC_PARITY_ERROR		(1 << 8)

#define SIM_EMV_NACK_THRESHOLD		(5)
#define EMV_T0_BGT			(16)
#define EMV_T1_BGT			(22)
#define ATR_THRESHOLD_MAX		(100)
#define ATR_MAX_CWT			(10080)
#define ATR_MAX_DURATION		(20160)
#define FCLK_FREQ			(5000000)
#define SIM_BLOCK_CLK_FREQ		(60000000)

#define ATR_TIMEOUT			(5)
#define TX_TIMEOUT			(10)
#define RX_TIMEOUT			(100)
#define RESET_RETRY_TIMES		(5)

/* Main SIM driver structure */
struct sim_t{
	s32 present;
	u8 open_cnt;
	int state;
	struct clk *clk;
	struct resource *res;
	void __iomem *ioaddr;
	int ipb_irq;
	int dat_irq;

	/* error code occured during transfer */
	int errval;
	int protocol_type;
	sim_timing_t timing_data;
	sim_baud_t baud_rate;
	int timeout;
	u8 nack_threshold;
	u8 nack_enable;
	u32 expected_rcv_cnt;
	u8 is_fixed_len_rec;

	/* remaining bytes to transmit for the current transfer */
	u32 xmt_remaining;
	/* transmit position */
	u32 xmt_pos;
	/* receive position / number of bytes received */
	u32 rcv_count;
	u8 rcv_buffer[SIM_RCV_BUFFER_SIZE];
	u8 xmt_buffer[SIM_XMT_BUFFER_SIZE];
	/* transfer completion notifier */
	struct completion xfer_done;
	/* async notifier for card and ATR detection */
	struct fasync_struct *fasync;
	/* Platform specific data */
	struct mxc_sim_platform_data *plat_data;
	bool last_is_tx;
	u16 rcv_head;
	spinlock_t lock;
	u32 clk_rate;
};

static struct miscdevice sim_dev;

static void sim_data_reset(struct sim_t *sim)
{
	sim->errval = SIM_OK;
	sim->protocol_type = 0;
	sim->timeout = 0;
	sim->nack_threshold = SIM_EMV_NACK_THRESHOLD;
	sim->nack_enable = 0;
	memset(&sim->timing_data, 0, sizeof(sim->timing_data));
	memset(&sim->baud_rate, 0, sizeof(sim->baud_rate));

	sim->xmt_remaining = 0;
	sim->xmt_pos = 0;
	sim->rcv_count = 0;
	sim->rcv_head = 0;
	sim->last_is_tx = false;
	memset(sim->rcv_buffer, 0, SIM_RCV_BUFFER_SIZE);
	memset(sim->xmt_buffer, 0, SIM_XMT_BUFFER_SIZE);

	init_completion(&sim->xfer_done);
};

static void sim_set_nack(struct sim_t *sim, u8 enable)
{
	u32 reg_val;

	reg_val = __raw_readl(sim->ioaddr + CNTL);
	/*Disable overrun NACK setting for now*/
	reg_val &= ~(SIM_CNTL_ONACK);

	if (enable) {
		reg_val |= SIM_CNTL_ANACK;
		__raw_writel(reg_val, sim->ioaddr + CNTL);
		reg_val = __raw_readl(sim->ioaddr + XMT_THRESHOLD);
		reg_val &= ~(SIM_XMT_THRESHOLD_XTH_MASK);
		reg_val |= SIM_XMT_THRESHOLD_XTH(sim->nack_threshold);
		__raw_writel(reg_val, sim->ioaddr + XMT_THRESHOLD);
	} else {
		reg_val &= ~SIM_CNTL_ANACK;
		__raw_writel(reg_val, sim->ioaddr + CNTL);
	}

	sim->nack_enable = enable;
}

static void sim_set_tx(struct sim_t *sim, u8 enable)
{
	u32 reg_data;

	reg_data = __raw_readl(sim->ioaddr + ENABLE);
	if (enable)
		reg_data |= SIM_ENABLE_XMTEN;
	else
		reg_data &= ~SIM_ENABLE_XMTEN;

	__raw_writel(reg_data, sim->ioaddr + ENABLE);
}

static void sim_set_rx(struct sim_t *sim, u8 enable)
{
	u32 reg_data;
	reg_data = __raw_readl(sim->ioaddr + ENABLE);
	if (enable)
		reg_data |= SIM_ENABLE_RCVEN;
	else
		reg_data &= ~SIM_ENABLE_RCVEN;

	__raw_writel(reg_data, sim->ioaddr + ENABLE);
}

static void sim_set_waiting_timers(struct sim_t *sim, u8 enable)
{
	u32 reg_val;
	reg_val = __raw_readl(sim->ioaddr + CNTL);
	if (enable) {
		if (sim->timing_data.cwt)
			reg_val |= (SIM_CNTL_CWTEN);
		if (sim->timing_data.bwt || sim->timing_data.bgt)
			reg_val |= (SIM_CNTL_BWTEN);
	} else {
		reg_val &= ~(SIM_CNTL_CWTEN | SIM_CNTL_BWTEN);
	}
	__raw_writel(reg_val, sim->ioaddr + CNTL);
}

static int sim_reset_module(struct sim_t *sim)
{
	u32 reg_val;
	s8 timeout = RESET_RETRY_TIMES;

	reg_val = __raw_readl(sim->ioaddr + RESET_CNTL);
	reg_val |= (SIM_RESET_CNTL_SOFT_RESET);
	__raw_writel(reg_val, sim->ioaddr + RESET_CNTL);

	while (__raw_readl(sim->ioaddr + RESET_CNTL) & SIM_RESET_CNTL_SOFT_RESET) {
		usleep_range(1, 3);
		if (timeout-- <= 0) {
			pr_err("SIM module reset timeout\n");
			return -EINVAL;
		}
	}
	return 0;
}

static void sim_receive_atr_set(struct sim_t *sim)
{
	u32 reg_data;

	/*Enable RX*/
	__raw_writel(SIM_ENABLE_RCVEN, sim->ioaddr + ENABLE);

	/*Receive fifo threshold = 1 to trigger GPC timer in irq handler*/
	reg_data = SIM_RCV_THRESHOLD_RTH(0) | SIM_RCV_THRESHOLD_RDT(1);
	__raw_writel(reg_data, sim->ioaddr + RCV_THRESHOLD);

	/* Clear the interrupt status*/
	reg_data = __raw_readl(sim->ioaddr + XMT_STATUS);
	reg_data |= SIM_XMT_STATUS_GPCNT;
	__raw_writel(reg_data, sim->ioaddr + XMT_STATUS);

	reg_data = __raw_readl(sim->ioaddr + RCV_STATUS);
	reg_data |= (SIM_RCV_STATUS_CWT | SIM_RCV_STATUS_RDRF);
	__raw_writel(reg_data, sim->ioaddr + RCV_STATUS);

	/*Set the cwt timer.Refer the setting of ATR on EMV4.3 book*/
	__raw_writel(ATR_MAX_CWT, sim->ioaddr + CHAR_WAIT);

	/*Set the baud rate to be 1/372. Refer the setting of ATR on EMV4.3 book*/
	reg_data = __raw_readl(sim->ioaddr + CNTL);
	reg_data &= ~SIM_CNTL_BAUD_SEL_MASK;
	reg_data |= SIM_CNTL_BAUD_SEL(0);

	/*
	 *Set the GPT timer disabled.
	 *KILL_CLOCK is reset to 0 by default, ANACK is disabled by default.
	 */
	reg_data &= ~SIM_CNTL_GPCNT_CLK_SEL_MASK;
	reg_data |= (SIM_CNTL_GPCNT_CLK_SEL(0) | SIM_CNTL_CWTEN);

	/*Enable ICM mode*/
	reg_data |= SIM_CNTL_ICM;

	/*Enable Sample12*/
	reg_data |= SIM_CNTL_SAMPLE12;
	__raw_writel(reg_data, sim->ioaddr + CNTL);

	/*Disable NACK*/
	sim_set_nack(sim, 0);

	/*Set 12 ETUS*/
	__raw_writel(0, sim->ioaddr + GUARD_CNTL);

	sim->errval = 0;
	sim->rcv_count = 0;
	sim->state = SIM_STATE_ATR_RECEIVING;

	/*Enable the Rx threshold interrupt and cwt interrupt,disalbe the GPC interrupt*/
	reg_data = __raw_readl(sim->ioaddr + INT_MASK);
	reg_data &= ~(SIM_INT_MASK_CWTM | SIM_INT_MASK_RIM);
	reg_data |= SIM_INT_MASK_GPCM;
	__raw_writel(reg_data, sim->ioaddr + INT_MASK);
}

static int32_t sim_check_rec_data(u32 *reg_data)
{
	s32 err = 0;

	if (*reg_data & SIM_REC_CWT_ERROR)
		err |= SIM_ERROR_CWT;

	if (*reg_data & SIM_REC_FRAME_ERROR)
		err |= SIM_ERROR_FRAME;

	if (*reg_data & SIM_REC_PARITY_ERROR)
		err |= SIM_ERROR_PARITY;

	return err;
}

static void sim_xmt_fill_fifo(struct sim_t *sim)
{
	u32 reg_data;
	u32 bytesleft, i;

	reg_data = __raw_readl(sim->ioaddr + XMT_FIFO_STAT);
	bytesleft = SIM_TX_FIFO_DEPTH - ((reg_data >> 8) & 0x0F);

	if (bytesleft > sim->xmt_remaining)
		bytesleft = sim->xmt_remaining;

	for (i = 0; i < bytesleft; i++) {
		__raw_writel(sim->xmt_buffer[sim->xmt_pos],
			     sim->ioaddr + PORT1_XMT_BUF);
		sim->xmt_pos++;
	};
	sim->xmt_remaining -= bytesleft;
};

static void sim_rcv_read_fifo(struct sim_t *sim)
{
	u16 i, count;
	u32 reg_data;

	count  = __raw_readl(sim->ioaddr + RCV_FIFO_CNT);

	spin_lock(&sim->lock);
	for (i = 0; i < count; i++) {
		reg_data = __raw_readl(sim->ioaddr + PORT1_RCV_BUF);
		sim->errval |= sim_check_rec_data(&reg_data);

		/* T1 mode and t0 mode no parity error, T1 mode SIM module will not produce NACK be
		 * NACK is disabled. T0 mode to ensure there is no parity error for the current byte
		 */
		if (!(sim->nack_enable && (reg_data & SIM_REC_PARITY_ERROR))) {
			sim->rcv_buffer[sim->rcv_head + sim->rcv_count] = (u8)reg_data;
			sim->rcv_count++;
		}
		if (sim->rcv_head + sim->rcv_count >= SIM_RCV_BUFFER_SIZE) {
			pr_err("The software fifo is full,head %d, cnt%d\n", sim->rcv_head, sim->rcv_count);
			break;
		}
	}
	spin_unlock(&sim->lock);
}

static void sim_tx_irq_enable(struct sim_t *sim)
{
	u32 reg_val;
	/*Clear the status and enable the related interrupt*/
	reg_val = __raw_readl(sim->ioaddr + XMT_STATUS);
	__raw_writel(reg_val, sim->ioaddr + XMT_STATUS);
	reg_val = __raw_readl(sim->ioaddr + RCV_STATUS);
	__raw_writel(reg_val, sim->ioaddr + RCV_STATUS);

	reg_val = __raw_readl(sim->ioaddr + INT_MASK);
	/*
	 *Disable CWT , BWT interrupt when transmitting, it would
	 *be enabled when rx is enabled just after tx completes
	 *The timer will be enabled.
	 */
	reg_val |= SIM_INT_MASK_CWTM | SIM_INT_MASK_BWTM;
	reg_val |= SIM_INT_MASK_RIM | SIM_INT_MASK_RTM;

	if (sim->xmt_remaining != 0)
		reg_val &= ~SIM_INT_MASK_TDTFM;
	else{
		reg_val &= ~SIM_INT_MASK_TCIM;
		/*Enable transmit early complete interrupt.*/
		reg_val &= ~SIM_INT_MASK_ETCIM;
	}

	/*NACK interrupt is enabled only when T0 mode*/
	if (sim->protocol_type == SIM_PROTOCOL_T0 || sim->nack_enable != 0)
		reg_val &= ~SIM_INT_MASK_XTM;
	else
		reg_val |= SIM_INT_MASK_XTM;
	__raw_writel(reg_val, sim->ioaddr + INT_MASK);
}

static void sim_tx_irq_disable(struct sim_t *sim)
{
	u32 reg_val;
	/*Disable the NACK interruptand TX related interrupt*/
	reg_val = __raw_readl(sim->ioaddr + INT_MASK);
	reg_val |= (SIM_INT_MASK_TDTFM | SIM_INT_MASK_TCIM | SIM_INT_MASK_XTM | SIM_INT_MASK_ETCIM);
	__raw_writel(reg_val, sim->ioaddr + INT_MASK);
}

static void sim_rx_irq_enable(struct sim_t *sim)
{
	u32 reg_data;
	/*Clear status and enable interrupt
	 *It is suggested by Tengda from IC team. TX may have CWT status so clear it
	 */
	if (sim->last_is_tx)
		__raw_writel(SIM_RCV_STATUS_CWT, sim->ioaddr + RCV_STATUS);

	reg_data = __raw_readl(sim->ioaddr + INT_MASK);
	reg_data |= (SIM_INT_MASK_TCIM | SIM_INT_MASK_TDTFM | SIM_INT_MASK_XTM);
	reg_data &= ~(SIM_INT_MASK_RIM | SIM_INT_MASK_CWTM | SIM_INT_MASK_BWTM);

	if (sim->protocol_type == SIM_PROTOCOL_T0 || sim->nack_enable != 0)
		reg_data &= ~SIM_INT_MASK_RTM;
	else
		reg_data |= SIM_INT_MASK_RTM;

	__raw_writel(reg_data, sim->ioaddr + INT_MASK);
}

static void sim_rx_irq_disable(struct sim_t *sim)
{
	u32 reg_val;
	reg_val = __raw_readl(sim->ioaddr + INT_MASK);
	reg_val |= (SIM_INT_MASK_RIM | SIM_INT_MASK_CWTM | SIM_INT_MASK_BWTM | SIM_INT_MASK_RTM);
	__raw_writel(reg_val, sim->ioaddr + INT_MASK);
}

static irqreturn_t sim_irq_handler(int irq, void *dev_id)
{
	u32 reg_data, tx_status, rx_status;

	struct sim_t *sim = (struct sim_t *) dev_id;

	tx_status  = __raw_readl(sim->ioaddr + XMT_STATUS);
	rx_status  = __raw_readl(sim->ioaddr + RCV_STATUS);
	__raw_writel(tx_status, sim->ioaddr + XMT_STATUS);
	__raw_writel(rx_status, sim->ioaddr + RCV_STATUS);

	if (sim->state == SIM_STATE_ATR_RECEIVING) {
		if ((rx_status & SIM_RCV_STATUS_RDRF) &&
			(__raw_readl(sim->ioaddr + RCV_THRESHOLD) == 0x01)) {

			/*Enable GPC interrupt and disable the rx full interrupt*/
			reg_data = __raw_readl(sim->ioaddr + INT_MASK);
			reg_data &= ~(SIM_INT_MASK_GPCM);
			reg_data |= SIM_INT_MASK_RIM;
			__raw_writel(reg_data, sim->ioaddr + INT_MASK);
			sim_rcv_read_fifo(sim);

			/*ATR each recieved byte will cost 12 ETU, so get the remaining etus*/
			reg_data = ATR_MAX_DURATION - sim->rcv_count * 12;
			__raw_writel(reg_data, sim->ioaddr + GPCNT);

			reg_data = __raw_readl(sim->ioaddr + CNTL);
			reg_data &= ~SIM_CNTL_GPCNT_CLK_SEL_MASK;
			reg_data |= SIM_CNTL_GPCNT_CLK_SEL(3);
			__raw_writel(reg_data, sim->ioaddr + CNTL);

			/*Receive fifo threshold set to max value*/
			reg_data = SIM_RCV_THRESHOLD_RTH(0) | SIM_RCV_THRESHOLD_RDT(ATR_THRESHOLD_MAX);
			__raw_writel(reg_data, sim->ioaddr + RCV_THRESHOLD);
		}
		if ((rx_status & SIM_RCV_STATUS_CWT) ||
			(tx_status & SIM_XMT_STATUS_GPCNT)) {

			/*Disable the GPCNT timer and CWT timer right now*/
			reg_data = __raw_readl(sim->ioaddr + CNTL);
			reg_data &= ~(SIM_CNTL_GPCNT_CLK_SEL_MASK | SIM_CNTL_CWTEN);
			__raw_writel(reg_data, sim->ioaddr + CNTL);

			reg_data = __raw_readl(sim->ioaddr + INT_MASK);
			reg_data |= (SIM_INT_MASK_GPCM | SIM_INT_MASK_CWTM);
			__raw_writel(reg_data, sim->ioaddr + INT_MASK);

			if (tx_status & SIM_XMT_STATUS_GPCNT)
				sim->errval |= SIM_ERROR_ATR_TIMEROUT;

			if (rx_status & SIM_RCV_STATUS_CWT)
				sim->errval |= SIM_ERROR_CWT;

			sim_rcv_read_fifo(sim);
			sim->state = SIM_STATE_ATR_RECEIVED;

			complete(&sim->xfer_done);
		}
	}

	else if (sim->state == SIM_STATE_XMTING) {
		/*The CWT BWT expire should not happen when in the transmitting state*/
		if (tx_status & SIM_XMT_STATUS_ETC) {
			/*Once the transmit frame is completed, need to enable RX immedially*/
			sim_set_rx(sim, 1);
		}
		if (tx_status & SIM_XMT_STATUS_XTE) {
			/*Disable TX*/
			sim_set_tx(sim, 0);
			/*Disalbe the timers*/
			sim_set_waiting_timers(sim, 0);
			/*Disable the NACK interruptand TX related interrupt*/
			sim_tx_irq_disable(sim);

			/*Update the state and status*/
			sim->errval |= SIM_ERROR_NACK_THRESHOLD;
			sim->state = SIM_STATE_XMT_ERROR;

			complete(&sim->xfer_done);
		} else if (tx_status & SIM_XMT_STATUS_TDTF && sim->xmt_remaining != 0) {
			sim_xmt_fill_fifo(sim);
			if (sim->xmt_remaining == 0) {
				/*Disable TX threshold interrupt and enable tx complete interrupt*/
				reg_data = __raw_readl(sim->ioaddr + INT_MASK);
				reg_data |= SIM_INT_MASK_TDTFM;
				/*Enable transmit complete and early transmit complete interrupt*/
				reg_data &= ~(SIM_INT_MASK_TCIM | SIM_INT_MASK_ETCIM);
				__raw_writel(reg_data, sim->ioaddr + INT_MASK);
			}
		} else if (tx_status & SIM_XMT_STATUS_TC && sim->xmt_remaining == 0) {
			/*Disable the NACK interruptand TX related interrupt*/
			sim_tx_irq_disable(sim);
			sim_set_rx(sim, 1);
			/*Update the state and status*/
			sim->state = SIM_STATE_XMT_DONE;
			complete(&sim->xfer_done);
		}
	}

	/*
	 * It takes some time to change from SIM_STATE_XMT_DONE to SIM_STATE_RECEIVING
	 * RX would only be enabled after state becomes SIM_STATE_RECEIVING
	 */
	else if (sim->state == SIM_STATE_RECEIVING) {
		if (rx_status & SIM_RCV_STATUS_RTE) {
			/*Disable RX*/
			sim_set_rx(sim, 0);
			/*Disable the BWT timer and CWT timer right now*/
			sim_set_waiting_timers(sim, 0);
			/*Disable the interrupt right now*/
			sim_rx_irq_disable(sim);
			/*Should we read the fifo or just flush the fifo?*/
			sim_rcv_read_fifo(sim);
			sim->errval = SIM_ERROR_NACK_THRESHOLD;
			sim->state = SIM_STATE_RECEIVE_ERROR;
			complete(&sim->xfer_done);
		}

		if (rx_status & SIM_RCV_STATUS_RDRF) {
			sim_rcv_read_fifo(sim);
			if (sim->is_fixed_len_rec &&
				sim->rcv_count >= sim->expected_rcv_cnt) {

				/*Disable the BWT timer and CWT timer right now*/
				sim_rx_irq_disable(sim);
				/*Add the state judgement to ensure the maybe complete has been impletment in the above "if" case*/
				if (sim->state == SIM_STATE_RECEIVING) {
					sim->state = SIM_STATE_RECEIVE_DONE;
					complete(&sim->xfer_done);
				}
			}
		}

		if ((rx_status & SIM_RCV_STATUS_CWT) ||
			(rx_status & SIM_RCV_STATUS_BWT) ||
			(rx_status & SIM_RCV_STATUS_BGT)) {

			/*Disable the BWT timer and CWT timer right now*/
			sim_set_waiting_timers(sim, 0);
			sim_rx_irq_disable(sim);

			if (rx_status & SIM_RCV_STATUS_BWT) {
				sim->errval |= SIM_ERROR_BWT;
			}
			if (rx_status & SIM_RCV_STATUS_CWT)
				sim->errval |= SIM_ERROR_CWT;
			if (rx_status & SIM_RCV_STATUS_BGT)
				sim->errval |= SIM_ERROR_BGT;

			sim_rcv_read_fifo(sim);
			/*Add the state judgement to ensure the maybe complete has been impletment in the above "if" case*/
			if (sim->state == SIM_STATE_RECEIVING) {
				sim->state = SIM_STATE_RECEIVE_DONE;
				complete(&sim->xfer_done);
			}
		}
	}

	else if (rx_status & SIM_RCV_STATUS_RDRF) {
		pr_err("unexpected  status %d\n", sim->state);
		sim_rcv_read_fifo(sim);
	}

	return IRQ_HANDLED;
};

static void sim_start(struct sim_t *sim)
{
	u32 reg_data, clk_rate, clk_div = 0;
	pr_debug("%s entering.\n", __func__);

	__raw_writel(0, sim->ioaddr + SETUP);

	/* ~ 5 MHz */
	clk_rate = clk_get_rate(sim->clk);
	clk_div = (clk_rate + sim->clk_rate - 1) / sim->clk_rate;
	__raw_writel(clk_div, sim->ioaddr + CLK_PRESCALER);

	/*Set the port pin to be open drained*/
	reg_data = __raw_readl(sim->ioaddr + OD_CONFIG);
	reg_data |= SIM_OD_CONFIG_OD_P0;
	__raw_writel(reg_data, sim->ioaddr + OD_CONFIG);
	reg_data = __raw_readl(sim->ioaddr + PORT0_CNTL);

	/*One pin mode*/
	reg_data |= SIM_PORT_CNTL_3VOLT | SIM_PORT_CNTL_STEN;
	__raw_writel(reg_data, sim->ioaddr + PORT0_CNTL);

	/* presense detect */
	pr_debug("%s p0_det is 0x%x \n", __func__,
		 __raw_readl(sim->ioaddr + PORT0_DETECT));
	if (__raw_readl(sim->ioaddr + PORT0_DETECT) & SIM_PORT_DETECT_SPDP) {
		reg_data = __raw_readl(sim->ioaddr + PORT0_DETECT);
		reg_data &= ~SIM_PORT_DETECT_SPDS;
		__raw_writel(reg_data, sim->ioaddr + PORT0_DETECT);
		sim->present = SIM_PRESENT_REMOVED;
		sim->state = SIM_STATE_REMOVED;
	} else {
		reg_data = __raw_readl(sim->ioaddr + PORT0_DETECT);
		reg_data |= SIM_PORT_DETECT_SPDS;
		__raw_writel(reg_data, sim->ioaddr + PORT0_DETECT);
		sim->present = SIM_PRESENT_DETECTED;
		sim->state = SIM_STATE_DETECTED;
	};

	/*enable card interrupt. clear interrupt status*/
	reg_data = __raw_readl(sim->ioaddr + PORT0_DETECT);
	reg_data |= SIM_PORT_DETECT_SDI;
	reg_data |= SIM_PORT_DETECT_SDIM;
	__raw_writel(reg_data, sim->ioaddr + PORT0_DETECT);
};

static void sim_activate(struct sim_t *sim)
{
	u32 reg_data;
	pr_debug("%s Activate on the sim port.\n", __func__);
	/* activate on sequence */
	if (sim->present != SIM_PRESENT_REMOVED) {
		/*Disable Reset pin*/
		reg_data = __raw_readl(sim->ioaddr + PORT0_CNTL);
		reg_data &= ~SIM_PORT_CNTL_SRST;
		__raw_writel(reg_data, sim->ioaddr + PORT0_CNTL);

		/*Enable VCC pin*/
		reg_data = __raw_readl(sim->ioaddr + PORT0_CNTL);
		reg_data |= SIM_PORT_CNTL_SVEN;
		__raw_writel(reg_data, sim->ioaddr + PORT0_CNTL);
		msleep(10);
		/*Enable clock pin*/
		reg_data = __raw_readl(sim->ioaddr + PORT0_CNTL);
		reg_data |= SIM_PORT_CNTL_SCEN;
		__raw_writel(reg_data, sim->ioaddr + PORT0_CNTL);
		msleep(10);
	} else {
		pr_err("No card%s\n", __func__);
	}
}

static void sim_cold_reset_sequency(struct sim_t *sim)
{
	u32 reg_data;

	reg_data = __raw_readl(sim->ioaddr + PORT0_CNTL);
	reg_data &= ~SIM_PORT_CNTL_SRST;
	__raw_writel(reg_data, sim->ioaddr + PORT0_CNTL);

	reg_data = __raw_readl(sim->ioaddr + PORT0_CNTL);
	reg_data |= SIM_PORT_CNTL_SVEN;
	__raw_writel(reg_data, sim->ioaddr + PORT0_CNTL);
	mdelay(9);

	reg_data = __raw_readl(sim->ioaddr + PORT0_CNTL);
	reg_data |= SIM_PORT_CNTL_SCEN;
	__raw_writel(reg_data, sim->ioaddr + PORT0_CNTL);

	mdelay(8);
	udelay(600);

	reg_data = __raw_readl(sim->ioaddr + PORT0_CNTL);
	reg_data |= SIM_PORT_CNTL_SRST;
	__raw_writel(reg_data, sim->ioaddr + PORT0_CNTL);
};

static void sim_deactivate(struct sim_t *sim)
{
	u32 reg_data;

	pr_debug("%s entering.\n", __func__);
	/* Auto powdown to implement the deactivate sequence */
	if (sim->present != SIM_PRESENT_REMOVED) {
		reg_data = __raw_readl(sim->ioaddr + PORT0_CNTL);
		reg_data |= SIM_PORT_CNTL_SAPD;
		__raw_writel(reg_data, sim->ioaddr + PORT0_CNTL);
		reg_data |= SIM_PORT_CNTL_SFPD;
		__raw_writel(reg_data, sim->ioaddr + PORT0_CNTL);
	} else {
		pr_err("No card%s\n", __func__);
	}

	sim->present = SIM_PRESENT_REMOVED;
};

static void sim_cold_reset(struct sim_t *sim)
{
	if (sim->present != SIM_PRESENT_REMOVED) {
		sim->state = SIM_STATE_DETECTED;
		sim->present = SIM_PRESENT_DETECTED;
		sim_cold_reset_sequency(sim);
		sim_receive_atr_set(sim);
	} else {
		pr_err("No card%s\n", __func__);
	}
};

static void sim_warm_reset_sequency(struct sim_t *sim)
{
	u32 reg_data;

	reg_data = __raw_readl(sim->ioaddr + PORT0_CNTL);
	reg_data |= (SIM_PORT_CNTL_SRST | SIM_PORT_CNTL_SVEN | SIM_PORT_CNTL_SCEN);
	__raw_writel(reg_data, sim->ioaddr + PORT0_CNTL);

	udelay(20);

	reg_data &= ~SIM_PORT_CNTL_SRST;
	__raw_writel(reg_data, sim->ioaddr + PORT0_CNTL);

	mdelay(8);
	udelay(600);

	reg_data |= SIM_PORT_CNTL_SRST;
	__raw_writel(reg_data, sim->ioaddr + PORT0_CNTL);
}

static void sim_warm_reset(struct sim_t *sim)
{
	if (sim->present != SIM_PRESENT_REMOVED) {
		sim_data_reset(sim);
		sim_warm_reset_sequency(sim);
		sim_receive_atr_set(sim);
	} else {
		pr_err("No card%s\n", __func__);
	}
};


static int sim_card_lock(struct sim_t *sim)
{
	int errval;

	/* place holder for true physcial locking */
	if (sim->present != SIM_PRESENT_REMOVED)
		errval = SIM_OK;
	else
		errval = -SIM_E_NOCARD;
	return errval;
};

static int sim_card_eject(struct sim_t *sim)
{
	int errval;

	pr_debug("%s entering.\n", __func__);
	/* place holder for true physcial locking */
	if (sim->present != SIM_PRESENT_REMOVED)
		errval = SIM_OK;
	else
		errval = -SIM_E_NOCARD;
	return errval;
};

static int sim_check_baud_rate(sim_baud_t *baud_rate)
{
	/*
	 * The valid value is decribed in the 8.3.3.1 in EMV 4.3
	 */
	if (baud_rate->fi == 1 && (baud_rate->di == 1 ||
					baud_rate->di == 2 || baud_rate->di == 3))
		return 0;

	return -EINVAL;
}

static int sim_set_baud_rate(struct sim_t *sim)
{
	u32 reg_data;
	reg_data = __raw_readl(sim->ioaddr + CNTL);
	reg_data &= ~(SIM_CNTL_BAUD_SEL_MASK);

	switch (sim->baud_rate.di) {
	case 1:
		reg_data |= SIM_CNTL_BAUD_SEL(0);
		break;
	case 2:
		reg_data |= SIM_CNTL_BAUD_SEL(1);
		break;
	case 3:
		reg_data |= SIM_CNTL_BAUD_SEL(2);
		break;
	default:
		pr_err("Invalid baud Di, Using default 372 / 1\n");
		reg_data |= SIM_CNTL_BAUD_SEL(0);
		break;
	}

	__raw_writel(reg_data, sim->ioaddr + CNTL);

	return 0;
}

static int sim_check_timing_data(sim_timing_t *timing_data)
{
	if (timing_data->wwt > 0xFFFF ||
		timing_data->cwt > 0xFFFF ||
		timing_data->bgt > 0xFFFF ||
		timing_data->cgt > 0xFF) {
		/*Check whether the counter is out of the scope of SIM IP*/
		pr_err("The timing value is out of scope of IP\n");
		return -EINVAL;
	}

	return 0;
}

static void sim_set_timer_counter(struct sim_t *sim)
{
	if (sim->timing_data.wwt != 0 &&
		sim->protocol_type == SIM_PROTOCOL_T0) {
		sim->timing_data.cwt = sim->timing_data.wwt;
		sim->timing_data.bwt = sim->timing_data.wwt;
	}


	if (sim->timing_data.bgt != 0) {
		__raw_writel(sim->timing_data.bgt, sim->ioaddr + BGT);
	}

	if (sim->timing_data.cwt != 0)
		__raw_writel(sim->timing_data.cwt, sim->ioaddr + CHAR_WAIT);

	if (sim->timing_data.bwt != 0) {

		__raw_writel(sim->timing_data.bwt & 0x0000FFFF, sim->ioaddr + BWT);
		__raw_writel((sim->timing_data.bwt >> 16) & 0x0000FFFF,
				sim->ioaddr + BWT_H);
	}

	if (sim->timing_data.cgt == 0xFF && sim->protocol_type == SIM_PROTOCOL_T0)
		/* From EMV4.3 , CGT =0xFF in T0 mode means 12 ETU.
		 * Set register to be 12 ETU for transmitting and receiving.
		 */
		__raw_writel(0 , sim->ioaddr + GUARD_CNTL);
	else if (sim->timing_data.cgt == 0xFF && sim->protocol_type == SIM_PROTOCOL_T1)
		/* From EMV4.3 , CGT =0xFF in T1 mode means 11 ETU.
		 * Set register to be 12 ETU for transmitting and receiving.
		 */
		__raw_writel(0x1FF , sim->ioaddr + GUARD_CNTL);

	/*For the T1 mode, use 11ETU to receive.*/
	else if (sim->protocol_type == SIM_PROTOCOL_T1)
		__raw_writel((sim->timing_data.cgt | SIM_GUARD_CNTL_RCVR11), sim->ioaddr + GUARD_CNTL);

	else
		/*sim->protocol_type == SIM_PROTOCOL_T0*/
		__raw_writel(sim->timing_data.cgt, sim->ioaddr + GUARD_CNTL);
}

static void sim_xmt_start(struct sim_t *sim)
{
	u32 reg_val;

	/*Set TX threshold if there are remaing data*/
	if (sim->xmt_remaining != 0) {
		reg_val = __raw_readl(sim->ioaddr + XMT_THRESHOLD);
		reg_val &= ~SIM_XMT_THRESHOLD_TDT_MASK;
		reg_val |= SIM_XMT_THRESHOLD_TDT(TX_FIFO_THRESHOLD);
		__raw_writel(reg_val, sim->ioaddr + XMT_THRESHOLD);
	}
	sim_tx_irq_enable(sim);

	/*Enable  BWT, CWT timers*/
	sim_set_waiting_timers(sim, 1);

	/*Enable TX*/
	sim_set_tx(sim, 1);

	/*Disalbe RX*/
	sim_set_rx(sim, 0);
}

static void sim_flush_fifo(struct sim_t *sim, u8 flush_tx, u8 flush_rx)
{
	u32 reg_val;

	reg_val = __raw_readl(sim->ioaddr + RESET_CNTL);

	if (flush_tx)
		reg_val |= SIM_RESET_CNTL_FLUSH_XMT;
	if (flush_rx)
		reg_val |= SIM_RESET_CNTL_FLUSH_RCV;
	__raw_writel(reg_val, sim->ioaddr + RESET_CNTL);

	usleep_range(2, 3);

	if (flush_tx)
		reg_val &= ~(SIM_RESET_CNTL_FLUSH_XMT);
	if (flush_rx)
		reg_val &= ~(SIM_RESET_CNTL_FLUSH_RCV);
	__raw_writel(reg_val, sim->ioaddr + RESET_CNTL);
}

static void sim_change_rcv_threshold(struct sim_t *sim)
{
	u32 rx_threshold = 0;
	u32 reg_val = 0;

	if (sim->is_fixed_len_rec) {
		rx_threshold = sim->expected_rcv_cnt - sim->rcv_count;
		reg_val = __raw_readl(sim->ioaddr + RCV_THRESHOLD);
		reg_val &= ~(SIM_RCV_THRESHOLD_RDT_MASK);
		reg_val |= SIM_RCV_THRESHOLD_RDT(rx_threshold);
		__raw_writel(reg_val, sim->ioaddr + RCV_THRESHOLD);
	}
}

static void sim_start_rcv(struct sim_t *sim)
{
	sim_set_baud_rate(sim);
	if (sim->protocol_type == SIM_PROTOCOL_T0)
		sim_set_nack(sim, 1);
	else if (sim->protocol_type == SIM_PROTOCOL_T1)
		sim_set_nack(sim, 0);

	/*Set RX threshold*/
	if (sim->protocol_type == SIM_PROTOCOL_T0)
		__raw_writel(SIM_RCV_THRESHOLD_RTH(sim->nack_threshold) |
				SIM_RCV_THRESHOLD_RDT(RX_FIFO_THRESHOLD), sim->ioaddr + RCV_THRESHOLD);
	 else
		__raw_writel(SIM_RCV_THRESHOLD_RDT(RX_FIFO_THRESHOLD), sim->ioaddr + RCV_THRESHOLD);

	/*Clear status and enable interrupt*/
	sim_rx_irq_enable(sim);

	/*Disalbe TX and Enable Rx*/
	sim_set_rx(sim, 1);
	sim_set_tx(sim, 0);
}

static void sim_polling_delay(struct sim_t *sim, u32 delay)
{
	u32 reg_data;

	/*Reset the timer*/
	reg_data = __raw_readl(sim->ioaddr + CNTL);
	reg_data &= ~SIM_CNTL_GPCNT_CLK_SEL_MASK;
	reg_data |= SIM_CNTL_GPCNT_CLK_SEL(0);
	__raw_writel(reg_data, sim->ioaddr + CNTL);

	/*Clear the interrupt status*/
	__raw_writel(SIM_XMT_STATUS_GPCNT, sim->ioaddr + XMT_STATUS);

	/*Disable timer interrupt*/
	reg_data = __raw_readl(sim->ioaddr + INT_MASK);
	reg_data |= SIM_INT_MASK_GPCM;
	__raw_writel(reg_data, sim->ioaddr + INT_MASK);

	__raw_writel(delay, sim->ioaddr + GPCNT);

	/*Set the ETU as clock source and start timer*/
	reg_data = __raw_readl(sim->ioaddr + CNTL);
	reg_data &= ~SIM_CNTL_GPCNT_CLK_SEL_MASK;
	reg_data |= SIM_CNTL_GPCNT_CLK_SEL(3);
	__raw_writel(reg_data, sim->ioaddr + CNTL);

	/*Loop for timeout*/
	while (!(__raw_readl(sim->ioaddr + XMT_STATUS) & SIM_XMT_STATUS_GPCNT))
		usleep_range(10, 20);
	__raw_writel(SIM_XMT_STATUS_GPCNT, sim->ioaddr + XMT_STATUS);
}

static long sim_ioctl(struct file *file,
		     unsigned int cmd, unsigned long arg)
{
	int ret, errval = SIM_OK;
	unsigned long timeout;
	u32 reg_data;
	u32 delay;
	u32 copy_cnt, val;
	unsigned long flags;

	struct sim_t *sim = (struct sim_t *) file->private_data;

	pr_debug("%s entering.\n", __func__);
	switch (cmd) {

	case SIM_IOCTL_GET_ATR:
		if (sim->present != SIM_PRESENT_DETECTED) {
			pr_err("NO card ...\n");
			errval = -SIM_E_NOCARD;
			break;
		};
		sim->timeout = ATR_TIMEOUT * HZ;
		val = 0;
		ret = copy_to_user(&(((sim_atr_t *)arg)->size), &val,
					sizeof((((sim_atr_t *)arg)->size)));

		timeout = wait_for_completion_interruptible_timeout(&sim->xfer_done,
									sim->timeout);
		/*Disalbe the GPCNT timer and CWT timer right now*/
		reg_data = __raw_readl(sim->ioaddr + CNTL);
		reg_data &= ~(SIM_CNTL_GPCNT_CLK_SEL_MASK | SIM_CNTL_CWTEN);
		__raw_writel(reg_data, sim->ioaddr + CNTL);

		reg_data = __raw_readl(sim->ioaddr + INT_MASK);
		reg_data |= (SIM_INT_MASK_GPCM | SIM_INT_MASK_CWTM);
		__raw_writel(reg_data, sim->ioaddr + INT_MASK);

		if (timeout == 0) {
			pr_err("ATR timeout\n");
			errval = -SIM_E_TIMEOUT;
			break;
		}

		ret = copy_to_user(&(((sim_atr_t *)arg)->size), &sim->rcv_count,
					sizeof(sim->rcv_count));
		if (ret) {
			pr_err("ATR ACCESS rcv_count Error, %d\n", ret);
			errval = -SIM_E_ACCESS;
			break;
		}

		ret = copy_to_user(((sim_atr_t *)arg)->atr_buffer, sim->rcv_buffer,
					sim->rcv_count);
		if (ret) {
			pr_err("ATR ACCESS buffer Error %d %d\n", sim->rcv_count, ret);
			errval = -SIM_E_ACCESS;
			break;
		}

		ret = copy_to_user(&(((sim_atr_t *)arg)->errval), &sim->errval,
					sizeof(sim->errval));
		if (ret) {
			pr_err("ATR ACCESS Error\n");
			errval = -SIM_E_ACCESS;
			break;
		}
		sim->rcv_count = 0;
		sim->rcv_head = 0;
		sim->errval = 0;

		break;

	case SIM_IOCTL_DEACTIVATE:

		sim_deactivate(sim);
		break;

	case SIM_IOCTL_COLD_RESET:
		sim->present = SIM_PRESENT_REMOVED;
		sim->state = SIM_STATE_REMOVED;
		sim_reset_module(sim);
		sim_data_reset(sim);
		sim_start(sim);
		sim_activate(sim);
		sim_cold_reset(sim);

		break;

	case SIM_IOCTL_WARM_RESET:
		sim_warm_reset(sim);
		break;

	case SIM_IOCTL_XMT:
		ret = copy_from_user(&sim->xmt_remaining, &(((sim_xmt_t *)arg)->xmt_length),
				     sizeof(uint32_t));
		if (ret || sim->xmt_remaining > SIM_XMT_BUFFER_SIZE) {
			pr_err("copy error or to big buffer\n");
			errval = -EINVAL;
			break;
		}
		ret = copy_from_user(sim->xmt_buffer, (((sim_xmt_t *)arg)->xmt_buffer),
				     sim->xmt_remaining);

		if (ret) {
			pr_err("Copy Error\n");
			errval = ret;
			break;
		}
		/*Flush the tx rx fifo*/
		sim_flush_fifo(sim, 1, 1);
		sim->xmt_pos = 0;
		sim->rcv_count = 0;
		sim->rcv_head = 0;
		sim->errval = 0;

		sim_xmt_fill_fifo(sim);
		sim_set_baud_rate(sim);
		if (sim->protocol_type == SIM_PROTOCOL_T0)
			sim_set_nack(sim, 1);
		else if (sim->protocol_type == SIM_PROTOCOL_T1)
			sim_set_nack(sim, 0);
		else {
			pr_err("Invalid protocol not T0 or T1\n");
			errval = -EINVAL;
			break;
		}

		sim_set_timer_counter(sim);
		sim_xmt_start(sim);
		sim->state = SIM_STATE_XMTING;

		sim->timeout = TX_TIMEOUT * HZ;
		timeout = wait_for_completion_interruptible_timeout(&sim->xfer_done,
									sim->timeout);
		if (timeout == 0) {
			/*Disable the NACK interruptand TX related interrupt*/
			sim_tx_irq_disable(sim);
			pr_err("tx timeout\n");
		}

		if (timeout == 0 || sim->state == SIM_STATE_XMT_ERROR) {
			pr_err("TX error\n");
			/*Disable timers*/
			sim_set_waiting_timers(sim, 0);
			/*Disable TX*/
			sim_set_tx(sim, 0);
			/*Flush the tx fifos*/
			sim_flush_fifo(sim, 1, 0);
			if (timeout == 0)
				errval = -SIM_E_TIMEOUT;
			else
				errval = -SIM_E_NACK;

			ret = copy_to_user(&(((sim_atr_t *)arg)->errval), &sim->errval,
						sizeof(sim->errval));
			sim->errval = 0;
			break;
		}

		/*Copy the error status to user space*/
		ret = copy_to_user(&(((sim_atr_t *)arg)->errval), &sim->errval,
						sizeof(sim->errval));
		sim->last_is_tx = true;
		/*Start RX*/
		sim->rcv_count = 0;
		sim->errval = 0;
		sim->state = SIM_STATE_RECEIVING;
		sim_start_rcv(sim);

		break;

	case SIM_IOCTL_RCV:
		if (sim->present != SIM_PRESENT_DETECTED) {
			errval = -SIM_E_NOCARD;
			break;
		}
		sim->is_fixed_len_rec = 0;
		val = 0;
		ret = copy_from_user(&sim->expected_rcv_cnt, &(((sim_rcv_t *)arg)->rcv_length),
					sizeof(sim->expected_rcv_cnt));

		/*Set the length to be 0 at first*/
		ret = copy_to_user(&(((sim_rcv_t *)arg)->rcv_length), &val,
					sizeof(val));

		/*Set error value to be 0 at first*/
		ret = copy_to_user(&(((sim_rcv_t *)arg)->errval), &val,
					sizeof(val));

		if (sim->expected_rcv_cnt != 0)
			sim->is_fixed_len_rec = 1;

		if (sim->is_fixed_len_rec && sim->rcv_count >= sim->expected_rcv_cnt)
			goto copy_data;

		if (sim->state != SIM_STATE_RECEIVING) {
			sim_set_timer_counter(sim);
			/*Enable CWT BWT*/
			sim_set_waiting_timers(sim, 1);
			sim->state = SIM_STATE_RECEIVING;
			sim_start_rcv(sim);
		}

		spin_lock_irqsave(&sim->lock, flags);
		if (sim->is_fixed_len_rec && sim->rcv_count < sim->expected_rcv_cnt)
			sim_change_rcv_threshold(sim);
		spin_unlock_irqrestore(&sim->lock, flags);
		sim->timeout = RX_TIMEOUT * HZ;
		timeout = wait_for_completion_interruptible_timeout(&sim->xfer_done,
									sim->timeout);

		if (timeout == 0) {
			pr_err("Receiving timeout\n");
			sim_set_waiting_timers(sim, 0);
			sim_rx_irq_disable(sim);
			errval = -SIM_E_TIMEOUT;
			break;
		}

copy_data:
		if (sim->is_fixed_len_rec)
			copy_cnt = sim->rcv_count >= sim->expected_rcv_cnt ? sim->expected_rcv_cnt : sim->rcv_count;
		else
			copy_cnt = sim->rcv_count;

		ret = copy_to_user(&(((sim_rcv_t *)arg)->rcv_length), &copy_cnt,
					sizeof(copy_cnt));
		if (ret) {
			pr_err("ATR ACCESS Error\n");
			errval = -SIM_E_ACCESS;
			break;
		}

		ret = copy_to_user(((sim_rcv_t *)arg)->rcv_buffer, &sim->rcv_buffer[sim->rcv_head],
					copy_cnt);
		if (ret) {
			pr_err("ATR ACCESS Error\n");
			errval = -SIM_E_ACCESS;
			break;
		}

		ret = copy_to_user(&(((sim_rcv_t *)arg)->errval), &sim->errval,
						sizeof(sim->errval));
		if (ret) {
			pr_err("ATR ACCESS Error\n");
			errval = -SIM_E_ACCESS;
			break;
		}
		/*Reset the receiving count and errval*/
		spin_lock_irqsave(&sim->lock, flags);
		sim->rcv_head += copy_cnt;
		sim->rcv_count -= copy_cnt;
		sim->errval = 0;
		spin_unlock_irqrestore(&sim->lock, flags);

		sim->last_is_tx = false;

		break;

	case SIM_IOCTL_SET_PROTOCOL:
		ret = copy_from_user(&sim->protocol_type, (int *)arg,
				     sizeof(int));
		if (ret)
			errval = -SIM_E_ACCESS;
		break;

	case SIM_IOCTL_SET_TIMING:
		ret = copy_from_user(&sim->timing_data, (sim_timing_t *)arg,
				     sizeof(sim_timing_t));
		if (ret) {
			pr_err("Copy Error\n");
			errval = ret;
			break;
		}

		ret = sim_check_timing_data(&sim->timing_data);

		if (ret)
			errval = ret;

		break;

	case SIM_IOCTL_SET_BAUD:
		ret = copy_from_user(&sim->baud_rate, (sim_baud_t *)arg,
					sizeof(sim_baud_t));

		if (ret) {
			pr_err("Copy Error\n");
			errval = ret;
			break;
		}

		sim_check_baud_rate(&sim->baud_rate);

		break;
	case SIM_IOCTL_WAIT:
		ret = copy_from_user(&delay, (unsigned int *)arg,
					sizeof(unsigned int));

		if (ret) {
			pr_err("\nWait Copy Error\n");
			errval = ret;
			break;
		}

		sim_polling_delay(sim, delay);
		break;

	case SIM_IOCTL_GET_PRESENSE:
		if (put_user(sim->present, (int *)arg))
			errval = -SIM_E_ACCESS;
		break;

	case SIM_IOCTL_CARD_LOCK:
		errval = sim_card_lock(sim);
		break;

	case SIM_IOCTL_CARD_EJECT:
		errval = sim_card_eject(sim);
		break;

	};

	return errval;
};

static int sim_open(struct inode *inode, struct file *file)
{
	int errval = SIM_OK;
	struct sim_t *sim = dev_get_drvdata(sim_dev.parent);

	file->private_data = sim;
	spin_lock_init(&sim->lock);

	pr_debug("%s entering.\n", __func__);
	if (!sim->ioaddr) {
		errval = -ENOMEM;
		return errval;
	}

	if (!sim->open_cnt)
		clk_prepare_enable(sim->clk);

	sim->open_cnt = 1;

	errval = sim_reset_module(sim);
	sim_data_reset(sim);

	return errval;
};

static int sim_release(struct inode *inode, struct file *file)
{
	u32 reg_data;
	struct sim_t *sim = (struct sim_t *) file->private_data;

	/* disable presense detection */
	reg_data = __raw_readl(sim->ioaddr + PORT0_DETECT);
	__raw_writel(reg_data | SIM_PORT_DETECT_SDIM,
		     sim->ioaddr + PORT0_DETECT);

	if (sim->present != SIM_PRESENT_REMOVED)
		sim_deactivate(sim);


	if (sim->open_cnt)
		clk_disable_unprepare(sim->clk);

	sim->open_cnt = 0;

	pr_err("exit %s\n", __func__);

	return 0;
};

static const struct file_operations sim_fops = {
	.owner = THIS_MODULE,
	.open = sim_open,
	.release = sim_release,
	.unlocked_ioctl = sim_ioctl,
};

static struct miscdevice sim_dev = {
	MISC_DYNAMIC_MINOR,
	"mxc_sim",
	&sim_fops
};

static struct platform_device_id imx_sim_devtype[] = {
	{
		.name = "imx7d-sim",
		.driver_data = 0,
	}, {
		/* sentinel */
	}
};

static const struct of_device_id sim_imx_dt_ids[] = {
	{ .compatible = "fsl,imx7d-sim",
		.data = &imx_sim_devtype[0], },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, mlb150_imx_dt_ids);

static int sim_probe(struct platform_device *pdev)
{
	int ret = 0;
	const struct of_device_id *of_id;
	struct sim_t *sim = NULL;

	sim = devm_kzalloc(&pdev->dev, sizeof(struct sim_t),
				GFP_KERNEL);
	if (!sim) {
		dev_err(&pdev->dev, "can't allocate enough memory\n");
		return -ENOMEM;
	}


	of_id = of_match_device(sim_imx_dt_ids, &pdev->dev);
	if (of_id)
		pdev->id_entry = of_id->data;
	else
		return -EINVAL;

	sim->clk_rate = FCLK_FREQ;
	sim->open_cnt = 0;

	sim->res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!sim->res) {
		pr_err("Can't get the MEMORY\n");
		return -ENOMEM;
	}
	sim->ioaddr = devm_request_and_ioremap(&pdev->dev, sim->res);
	dev_dbg(&pdev->dev, "mapped base address: 0x%08x\n", (u32)sim->ioaddr);
	if (IS_ERR(sim->ioaddr)) {
		dev_err(&pdev->dev,
			"failed to get ioremap base\n");
		ret = PTR_ERR(sim->ioaddr);
		return ret;
	}

	/* request the sim clk and sim_serial_clk */
	sim->clk = devm_clk_get(&pdev->dev, "sim");
	if (IS_ERR(sim->clk)) {
		ret = PTR_ERR(sim->clk);
		pr_err("Get CLK ERROR !\n");
		return ret;
	}
	pr_debug("sim clock:%lu\n", clk_get_rate(sim->clk));

	sim->ipb_irq = platform_get_irq(pdev, 0);
	if (sim->ipb_irq < 0) {
		dev_err(&pdev->dev, "No ipb irq line provided\n");
		return -ENOENT;
	}
	if (devm_request_irq(&pdev->dev, sim->ipb_irq, sim_irq_handler,
				0, "mxc_sim_ipb", sim)) {
		dev_err(&pdev->dev, "can't claim irq %d\n", sim->ipb_irq);
		return -ENOENT;
	}

	platform_set_drvdata(pdev, sim);

	/*
	 *@todo: Need to figure a better way if possible.
	 */
	sim_dev.parent = &(pdev->dev);

	misc_register(&sim_dev);

	return 0;
}

static int sim_remove(struct platform_device *pdev)
{
	struct sim_t *sim = platform_get_drvdata(pdev);

	if (sim->open_cnt)
		clk_disable_unprepare(sim->clk);

	misc_deregister(&sim_dev);

	return 0;
}

#ifdef CONFIG_PM
static int sim_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct sim_t *sim = platform_get_drvdata(pdev);

	if (sim->open_cnt)
		clk_disable_unprepare(sim->clk);

	return 0;
}

static int sim_resume(struct platform_device *pdev)
{
	struct sim_t *sim = platform_get_drvdata(pdev);

	if (sim->open_cnt)
		clk_prepare_enable(sim->clk);

	return 0;
}
#else
#define sim_suspend NULL
#define sim_resume NULL
#endif

static struct platform_driver sim_driver = {
	.driver = {
			.name = DRIVER_NAME,
			.owner = THIS_MODULE,
			.of_match_table = sim_imx_dt_ids,
			},
	.probe = sim_probe,
	.remove = sim_remove,
	.suspend = sim_suspend,
	.resume = sim_resume,
	.id_table = imx_sim_devtype,
};

static int __init sim_drv_init(void)
{
	return platform_driver_register(&sim_driver);
}

static void __exit sim_drv_exit(void)
{
	platform_driver_unregister(&sim_driver);
}

module_init(sim_drv_init);
module_exit(sim_drv_exit);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("MXC SIM Driver");
MODULE_LICENSE("GPL");

