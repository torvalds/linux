/*****************************************************************************
 *                                                                           *
 * File: espi.c                                                              *
 * $Revision: 1.9 $                                                          *
 * $Date: 2005/03/23 07:41:27 $                                              *
 * Description:                                                              *
 *  Ethernet SPI functionality.                                              *
 *  part of the Chelsio 10Gb Ethernet Driver.                                *
 *                                                                           *
 * This program is free software; you can redistribute it and/or modify      *
 * it under the terms of the GNU General Public License, version 2, as       *
 * published by the Free Software Foundation.                                *
 *                                                                           *
 * You should have received a copy of the GNU General Public License along   *
 * with this program; if not, write to the Free Software Foundation, Inc.,   *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.                 *
 *                                                                           *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED    *
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF      *
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.                     *
 *                                                                           *
 * http://www.chelsio.com                                                    *
 *                                                                           *
 * Copyright (c) 2003 - 2005 Chelsio Communications, Inc.                    *
 * All rights reserved.                                                      *
 *                                                                           *
 * Maintainers: maintainers@chelsio.com                                      *
 *                                                                           *
 * Authors: Dimitrios Michailidis   <dm@chelsio.com>                         *
 *          Tina Yang               <tainay@chelsio.com>                     *
 *          Felix Marti             <felix@chelsio.com>                      *
 *          Scott Bardone           <sbardone@chelsio.com>                   *
 *          Kurt Ottaway            <kottaway@chelsio.com>                   *
 *          Frank DiMambro          <frank@chelsio.com>                      *
 *                                                                           *
 * History:                                                                  *
 *                                                                           *
 ****************************************************************************/

#include "common.h"
#include "regs.h"
#include "espi.h"

struct peespi {
	adapter_t *adapter;
	struct espi_intr_counts intr_cnt;
	u32 misc_ctrl;
	spinlock_t lock;
};

#define ESPI_INTR_MASK (F_DIP4ERR | F_RXDROP | F_TXDROP | F_RXOVERFLOW | \
			F_RAMPARITYERR | F_DIP2PARITYERR)
#define MON_MASK  (V_MONITORED_PORT_NUM(3) | F_MONITORED_DIRECTION \
		   | F_MONITORED_INTERFACE)

#define TRICN_CNFG 14
#define TRICN_CMD_READ  0x11
#define TRICN_CMD_WRITE 0x21
#define TRICN_CMD_ATTEMPTS 10

static int tricn_write(adapter_t *adapter, int bundle_addr, int module_addr,
		       int ch_addr, int reg_offset, u32 wr_data)
{
	int busy, attempts = TRICN_CMD_ATTEMPTS;

	t1_write_reg_4(adapter, A_ESPI_CMD_ADDR, V_WRITE_DATA(wr_data) |
		       V_REGISTER_OFFSET(reg_offset) |
		       V_CHANNEL_ADDR(ch_addr) | V_MODULE_ADDR(module_addr) |
		       V_BUNDLE_ADDR(bundle_addr) |
		       V_SPI4_COMMAND(TRICN_CMD_WRITE));
	t1_write_reg_4(adapter, A_ESPI_GOSTAT, 0);

	do {
		busy = t1_read_reg_4(adapter, A_ESPI_GOSTAT) & F_ESPI_CMD_BUSY;
	} while (busy && --attempts);

	if (busy)
		CH_ERR("%s: TRICN write timed out\n", adapter->name);

	return busy;
}

/* 1. Deassert rx_reset_core. */
/* 2. Program TRICN_CNFG registers. */
/* 3. Deassert rx_reset_link */
static int tricn_init(adapter_t *adapter)
{
	int     i               = 0;
	int     sme             = 1;
	int     stat            = 0;
	int     timeout         = 0;
	int     is_ready        = 0;
	int     dynamic_deskew  = 0;

	if (dynamic_deskew)
		sme = 0;


	/* 1 */
	timeout=1000;
	do {
		stat = t1_read_reg_4(adapter, A_ESPI_RX_RESET);
		is_ready = (stat & 0x4);
		timeout--;
		udelay(5);
	} while (!is_ready || (timeout==0));
	t1_write_reg_4(adapter, A_ESPI_RX_RESET, 0x2);
	if (timeout==0)
	{
		CH_ERR("ESPI : ERROR : Timeout tricn_init() \n");
		t1_fatal_err(adapter);
	}

	/* 2 */
	if (sme) {
		tricn_write(adapter, 0, 0, 0, TRICN_CNFG, 0x81);
		tricn_write(adapter, 0, 1, 0, TRICN_CNFG, 0x81);
		tricn_write(adapter, 0, 2, 0, TRICN_CNFG, 0x81);
	}
	for (i=1; i<= 8; i++) tricn_write(adapter, 0, 0, i, TRICN_CNFG, 0xf1);
	for (i=1; i<= 2; i++) tricn_write(adapter, 0, 1, i, TRICN_CNFG, 0xf1);
	for (i=1; i<= 3; i++) tricn_write(adapter, 0, 2, i, TRICN_CNFG, 0xe1);
	for (i=4; i<= 4; i++) tricn_write(adapter, 0, 2, i, TRICN_CNFG, 0xf1);
	for (i=5; i<= 5; i++) tricn_write(adapter, 0, 2, i, TRICN_CNFG, 0xe1);
	for (i=6; i<= 6; i++) tricn_write(adapter, 0, 2, i, TRICN_CNFG, 0xf1);
	for (i=7; i<= 7; i++) tricn_write(adapter, 0, 2, i, TRICN_CNFG, 0x80);
	for (i=8; i<= 8; i++) tricn_write(adapter, 0, 2, i, TRICN_CNFG, 0xf1);

	/* 3 */
	t1_write_reg_4(adapter, A_ESPI_RX_RESET, 0x3);

	return 0;
}

void t1_espi_intr_enable(struct peespi *espi)
{
	u32 enable, pl_intr = t1_read_reg_4(espi->adapter, A_PL_ENABLE);

	/*
	 * Cannot enable ESPI interrupts on T1B because HW asserts the
	 * interrupt incorrectly, namely the driver gets ESPI interrupts
	 * but no data is actually dropped (can verify this reading the ESPI
	 * drop registers).  Also, once the ESPI interrupt is asserted it
	 * cannot be cleared (HW bug).
	 */
	enable = t1_is_T1B(espi->adapter) ? 0 : ESPI_INTR_MASK;
	t1_write_reg_4(espi->adapter, A_ESPI_INTR_ENABLE, enable);
	t1_write_reg_4(espi->adapter, A_PL_ENABLE, pl_intr | F_PL_INTR_ESPI);
}

void t1_espi_intr_clear(struct peespi *espi)
{
	t1_write_reg_4(espi->adapter, A_ESPI_INTR_STATUS, 0xffffffff);
	t1_write_reg_4(espi->adapter, A_PL_CAUSE, F_PL_INTR_ESPI);
}

void t1_espi_intr_disable(struct peespi *espi)
{
	u32 pl_intr = t1_read_reg_4(espi->adapter, A_PL_ENABLE);

	t1_write_reg_4(espi->adapter, A_ESPI_INTR_ENABLE, 0);
	t1_write_reg_4(espi->adapter, A_PL_ENABLE, pl_intr & ~F_PL_INTR_ESPI);
}

int t1_espi_intr_handler(struct peespi *espi)
{
	u32 cnt;
	u32 status = t1_read_reg_4(espi->adapter, A_ESPI_INTR_STATUS);

	if (status & F_DIP4ERR)
		espi->intr_cnt.DIP4_err++;
	if (status & F_RXDROP)
		espi->intr_cnt.rx_drops++;
	if (status & F_TXDROP)
		espi->intr_cnt.tx_drops++;
	if (status & F_RXOVERFLOW)
		espi->intr_cnt.rx_ovflw++;
	if (status & F_RAMPARITYERR)
		espi->intr_cnt.parity_err++;
	if (status & F_DIP2PARITYERR) {
		espi->intr_cnt.DIP2_parity_err++;

		/*
		 * Must read the error count to clear the interrupt
		 * that it causes.
		 */
		cnt = t1_read_reg_4(espi->adapter, A_ESPI_DIP2_ERR_COUNT);
	}

	/*
	 * For T1B we need to write 1 to clear ESPI interrupts.  For T2+ we
	 * write the status as is.
	 */
	if (status && t1_is_T1B(espi->adapter))
		status = 1;
	t1_write_reg_4(espi->adapter, A_ESPI_INTR_STATUS, status);
	return 0;
}

static void espi_setup_for_pm3393(adapter_t *adapter)
{
	u32 wmark = t1_is_T1B(adapter) ? 0x4000 : 0x3200;

	t1_write_reg_4(adapter, A_ESPI_SCH_TOKEN0, 0x1f4);
	t1_write_reg_4(adapter, A_ESPI_SCH_TOKEN1, 0x1f4);
	t1_write_reg_4(adapter, A_ESPI_SCH_TOKEN2, 0x1f4);
	t1_write_reg_4(adapter, A_ESPI_SCH_TOKEN3, 0x1f4);
	t1_write_reg_4(adapter, A_ESPI_RX_FIFO_ALMOST_EMPTY_WATERMARK, 0x100);
	t1_write_reg_4(adapter, A_ESPI_RX_FIFO_ALMOST_FULL_WATERMARK, wmark);
	t1_write_reg_4(adapter, A_ESPI_CALENDAR_LENGTH, 3);
	t1_write_reg_4(adapter, A_ESPI_TRAIN, 0x08000008);
	t1_write_reg_4(adapter, A_PORT_CONFIG,
		       V_RX_NPORTS(1) | V_TX_NPORTS(1));
}

static void espi_setup_for_vsc7321(adapter_t *adapter)
{
	u32 wmark = t1_is_T1B(adapter) ? 0x4000 : 0x3200;

	t1_write_reg_4(adapter, A_ESPI_SCH_TOKEN0, 0x1f4);
	t1_write_reg_4(adapter, A_ESPI_SCH_TOKEN1, 0x1f4);
	t1_write_reg_4(adapter, A_ESPI_SCH_TOKEN2, 0x1f4);
	t1_write_reg_4(adapter, A_ESPI_SCH_TOKEN3, 0x1f4);
	t1_write_reg_4(adapter, A_ESPI_RX_FIFO_ALMOST_EMPTY_WATERMARK, 0x100);
	t1_write_reg_4(adapter, A_ESPI_RX_FIFO_ALMOST_FULL_WATERMARK, wmark);
	t1_write_reg_4(adapter, A_ESPI_CALENDAR_LENGTH, 3);
	t1_write_reg_4(adapter, A_ESPI_TRAIN, 0x08000008);
	t1_write_reg_4(adapter, A_PORT_CONFIG,
		       V_RX_NPORTS(1) | V_TX_NPORTS(1));
}

/*
 * Note that T1B requires at least 2 ports for IXF1010 due to a HW bug.
 */
static void espi_setup_for_ixf1010(adapter_t *adapter, int nports)
{
	t1_write_reg_4(adapter, A_ESPI_CALENDAR_LENGTH, 1);
	if (nports == 4) {
		if (is_T2(adapter)) {
			t1_write_reg_4(adapter, A_ESPI_RX_FIFO_ALMOST_FULL_WATERMARK,
				0xf00);
			t1_write_reg_4(adapter, A_ESPI_RX_FIFO_ALMOST_EMPTY_WATERMARK,
				0x3c0);
		} else {
			t1_write_reg_4(adapter, A_ESPI_RX_FIFO_ALMOST_FULL_WATERMARK,
			       0x7ff);
			t1_write_reg_4(adapter, A_ESPI_RX_FIFO_ALMOST_EMPTY_WATERMARK,
			       0x1ff);
		}
	} else {
		t1_write_reg_4(adapter, A_ESPI_RX_FIFO_ALMOST_FULL_WATERMARK,
			       0x1fff);
		t1_write_reg_4(adapter, A_ESPI_RX_FIFO_ALMOST_EMPTY_WATERMARK,
			       0x7ff);
	}
	t1_write_reg_4(adapter, A_PORT_CONFIG,
		       V_RX_NPORTS(nports) | V_TX_NPORTS(nports));
}

/* T2 Init part --  */
/* 1. Set T_ESPI_MISCCTRL_ADDR */
/* 2. Init ESPI registers. */
/* 3. Init TriCN Hard Macro */
int t1_espi_init(struct peespi *espi, int mac_type, int nports)
{
	u32 status_enable_extra = 0;
	adapter_t *adapter = espi->adapter;
	u32 cnt;
	u32 status, burstval = 0x800100;

	/* Disable ESPI training.  MACs that can handle it enable it below. */
	t1_write_reg_4(adapter, A_ESPI_TRAIN, 0);

	if (is_T2(adapter)) {
		t1_write_reg_4(adapter, A_ESPI_MISC_CONTROL,
			       V_OUT_OF_SYNC_COUNT(4) |
			       V_DIP2_PARITY_ERR_THRES(3) | V_DIP4_THRES(1));
		if (nports == 4) {
			/* T204: maxburst1 = 0x40, maxburst2 = 0x20 */
			burstval = 0x200040;
		}
	}
	t1_write_reg_4(adapter, A_ESPI_MAXBURST1_MAXBURST2, burstval);

	if (mac_type == CHBT_MAC_PM3393)
		espi_setup_for_pm3393(adapter);
	else if (mac_type == CHBT_MAC_VSC7321)
		espi_setup_for_vsc7321(adapter);
	else if (mac_type == CHBT_MAC_IXF1010) {
		status_enable_extra = F_INTEL1010MODE;
		espi_setup_for_ixf1010(adapter, nports);
	} else
		return -1;

	/*
	 * Make sure any pending interrupts from the SPI are
	 * Cleared before enabling the interrupt.
	 */
	t1_write_reg_4(espi->adapter, A_ESPI_INTR_ENABLE, ESPI_INTR_MASK);
	status = t1_read_reg_4(espi->adapter, A_ESPI_INTR_STATUS);
	if (status & F_DIP2PARITYERR) {
		cnt = t1_read_reg_4(espi->adapter, A_ESPI_DIP2_ERR_COUNT);
	}

	/*
	 * For T1B we need to write 1 to clear ESPI interrupts.  For T2+ we
	 * write the status as is.
	 */
	if (status && t1_is_T1B(espi->adapter))
		status = 1;
	t1_write_reg_4(espi->adapter, A_ESPI_INTR_STATUS, status);

	t1_write_reg_4(adapter, A_ESPI_FIFO_STATUS_ENABLE,
		       status_enable_extra | F_RXSTATUSENABLE);

	if (is_T2(adapter)) {
		tricn_init(adapter);
		/*
		 * Always position the control at the 1st port egress IN
		 * (sop,eop) counter to reduce PIOs for T/N210 workaround.
		 */
		espi->misc_ctrl = (t1_read_reg_4(adapter, A_ESPI_MISC_CONTROL)
				   & ~MON_MASK) | (F_MONITORED_DIRECTION
				   | F_MONITORED_INTERFACE);
		t1_write_reg_4(adapter, A_ESPI_MISC_CONTROL, espi->misc_ctrl);
		spin_lock_init(&espi->lock);
	}

	return 0;
}

void t1_espi_destroy(struct peespi *espi)
{
	kfree(espi);
}

struct peespi *t1_espi_create(adapter_t *adapter)
{
	struct peespi *espi = kmalloc(sizeof(*espi), GFP_KERNEL);

	memset(espi, 0, sizeof(*espi));

	if (espi)
		espi->adapter = adapter;
	return espi;
}

void t1_espi_set_misc_ctrl(adapter_t *adapter, u32 val)
{
	struct peespi *espi = adapter->espi;

	if (!is_T2(adapter))
		return;
	spin_lock(&espi->lock);
	espi->misc_ctrl = (val & ~MON_MASK) |
			  (espi->misc_ctrl & MON_MASK);
	t1_write_reg_4(adapter, A_ESPI_MISC_CONTROL, espi->misc_ctrl);
	spin_unlock(&espi->lock);
}

u32 t1_espi_get_mon(adapter_t *adapter, u32 addr, u8 wait)
{
	struct peespi *espi = adapter->espi;
	u32 sel;

	if (!is_T2(adapter))
		return 0;
	sel = V_MONITORED_PORT_NUM((addr & 0x3c) >> 2);
	if (!wait) {
		if (!spin_trylock(&espi->lock))
			return 0;
	}
	else
		spin_lock(&espi->lock);
	if ((sel != (espi->misc_ctrl & MON_MASK))) {
		t1_write_reg_4(adapter, A_ESPI_MISC_CONTROL,
			       ((espi->misc_ctrl & ~MON_MASK) | sel));
		sel = t1_read_reg_4(adapter, A_ESPI_SCH_TOKEN3);
		t1_write_reg_4(adapter, A_ESPI_MISC_CONTROL,
			       espi->misc_ctrl);
	}
	else
		sel = t1_read_reg_4(adapter, A_ESPI_SCH_TOKEN3);
	spin_unlock(&espi->lock);
	return sel;
}
