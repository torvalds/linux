/*
 * arch/powerpc/sysdev/qe_lib/ucc.c
 *
 * QE UCC API Set - UCC specific routines implementations.
 *
 * Copyright (C) 2006 Freescale Semiconductor, Inc. All rights reserved.
 *
 * Authors: 	Shlomi Gridish <gridish@freescale.com>
 * 		Li Yang <leoli@freescale.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/stddef.h>
#include <linux/spinlock.h>
#include <linux/export.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <soc/fsl/qe/immap_qe.h>
#include <soc/fsl/qe/qe.h>
#include <soc/fsl/qe/ucc.h>

#define UCC_TDM_NUM 8
#define RX_SYNC_SHIFT_BASE 30
#define TX_SYNC_SHIFT_BASE 14
#define RX_CLK_SHIFT_BASE 28
#define TX_CLK_SHIFT_BASE 12

int ucc_set_qe_mux_mii_mng(unsigned int ucc_num)
{
	unsigned long flags;

	if (ucc_num > UCC_MAX_NUM - 1)
		return -EINVAL;

	spin_lock_irqsave(&cmxgcr_lock, flags);
	clrsetbits_be32(&qe_immr->qmx.cmxgcr, QE_CMXGCR_MII_ENET_MNG,
		ucc_num << QE_CMXGCR_MII_ENET_MNG_SHIFT);
	spin_unlock_irqrestore(&cmxgcr_lock, flags);

	return 0;
}
EXPORT_SYMBOL(ucc_set_qe_mux_mii_mng);

/* Configure the UCC to either Slow or Fast.
 *
 * A given UCC can be figured to support either "slow" devices (e.g. UART)
 * or "fast" devices (e.g. Ethernet).
 *
 * 'ucc_num' is the UCC number, from 0 - 7.
 *
 * This function also sets the UCC_GUEMR_SET_RESERVED3 bit because that bit
 * must always be set to 1.
 */
int ucc_set_type(unsigned int ucc_num, enum ucc_speed_type speed)
{
	u8 __iomem *guemr;

	/* The GUEMR register is at the same location for both slow and fast
	   devices, so we just use uccX.slow.guemr. */
	switch (ucc_num) {
	case 0: guemr = &qe_immr->ucc1.slow.guemr;
		break;
	case 1: guemr = &qe_immr->ucc2.slow.guemr;
		break;
	case 2: guemr = &qe_immr->ucc3.slow.guemr;
		break;
	case 3: guemr = &qe_immr->ucc4.slow.guemr;
		break;
	case 4: guemr = &qe_immr->ucc5.slow.guemr;
		break;
	case 5: guemr = &qe_immr->ucc6.slow.guemr;
		break;
	case 6: guemr = &qe_immr->ucc7.slow.guemr;
		break;
	case 7: guemr = &qe_immr->ucc8.slow.guemr;
		break;
	default:
		return -EINVAL;
	}

	clrsetbits_8(guemr, UCC_GUEMR_MODE_MASK,
		UCC_GUEMR_SET_RESERVED3 | speed);

	return 0;
}

static void get_cmxucr_reg(unsigned int ucc_num, __be32 __iomem **cmxucr,
	unsigned int *reg_num, unsigned int *shift)
{
	unsigned int cmx = ((ucc_num & 1) << 1) + (ucc_num > 3);

	*reg_num = cmx + 1;
	*cmxucr = &qe_immr->qmx.cmxucr[cmx];
	*shift = 16 - 8 * (ucc_num & 2);
}

int ucc_mux_set_grant_tsa_bkpt(unsigned int ucc_num, int set, u32 mask)
{
	__be32 __iomem *cmxucr;
	unsigned int reg_num;
	unsigned int shift;

	/* check if the UCC number is in range. */
	if (ucc_num > UCC_MAX_NUM - 1)
		return -EINVAL;

	get_cmxucr_reg(ucc_num, &cmxucr, &reg_num, &shift);

	if (set)
		setbits32(cmxucr, mask << shift);
	else
		clrbits32(cmxucr, mask << shift);

	return 0;
}

int ucc_set_qe_mux_rxtx(unsigned int ucc_num, enum qe_clock clock,
	enum comm_dir mode)
{
	__be32 __iomem *cmxucr;
	unsigned int reg_num;
	unsigned int shift;
	u32 clock_bits = 0;

	/* check if the UCC number is in range. */
	if (ucc_num > UCC_MAX_NUM - 1)
		return -EINVAL;

	/* The communications direction must be RX or TX */
	if (!((mode == COMM_DIR_RX) || (mode == COMM_DIR_TX)))
		return -EINVAL;

	get_cmxucr_reg(ucc_num, &cmxucr, &reg_num, &shift);

	switch (reg_num) {
	case 1:
		switch (clock) {
		case QE_BRG1:	clock_bits = 1; break;
		case QE_BRG2:	clock_bits = 2; break;
		case QE_BRG7:	clock_bits = 3; break;
		case QE_BRG8:	clock_bits = 4; break;
		case QE_CLK9:	clock_bits = 5; break;
		case QE_CLK10:	clock_bits = 6; break;
		case QE_CLK11:	clock_bits = 7; break;
		case QE_CLK12:	clock_bits = 8; break;
		case QE_CLK15:	clock_bits = 9; break;
		case QE_CLK16:	clock_bits = 10; break;
		default: break;
		}
		break;
	case 2:
		switch (clock) {
		case QE_BRG5:	clock_bits = 1; break;
		case QE_BRG6:	clock_bits = 2; break;
		case QE_BRG7:	clock_bits = 3; break;
		case QE_BRG8:	clock_bits = 4; break;
		case QE_CLK13:	clock_bits = 5; break;
		case QE_CLK14:	clock_bits = 6; break;
		case QE_CLK19:	clock_bits = 7; break;
		case QE_CLK20:	clock_bits = 8; break;
		case QE_CLK15:	clock_bits = 9; break;
		case QE_CLK16:	clock_bits = 10; break;
		default: break;
		}
		break;
	case 3:
		switch (clock) {
		case QE_BRG9:	clock_bits = 1; break;
		case QE_BRG10:	clock_bits = 2; break;
		case QE_BRG15:	clock_bits = 3; break;
		case QE_BRG16:	clock_bits = 4; break;
		case QE_CLK3:	clock_bits = 5; break;
		case QE_CLK4:	clock_bits = 6; break;
		case QE_CLK17:	clock_bits = 7; break;
		case QE_CLK18:	clock_bits = 8; break;
		case QE_CLK7:	clock_bits = 9; break;
		case QE_CLK8:	clock_bits = 10; break;
		case QE_CLK16:	clock_bits = 11; break;
		default: break;
		}
		break;
	case 4:
		switch (clock) {
		case QE_BRG13:	clock_bits = 1; break;
		case QE_BRG14:	clock_bits = 2; break;
		case QE_BRG15:	clock_bits = 3; break;
		case QE_BRG16:	clock_bits = 4; break;
		case QE_CLK5:	clock_bits = 5; break;
		case QE_CLK6:	clock_bits = 6; break;
		case QE_CLK21:	clock_bits = 7; break;
		case QE_CLK22:	clock_bits = 8; break;
		case QE_CLK7:	clock_bits = 9; break;
		case QE_CLK8:	clock_bits = 10; break;
		case QE_CLK16:	clock_bits = 11; break;
		default: break;
		}
		break;
	default: break;
	}

	/* Check for invalid combination of clock and UCC number */
	if (!clock_bits)
		return -ENOENT;

	if (mode == COMM_DIR_RX)
		shift += 4;

	clrsetbits_be32(cmxucr, QE_CMXUCR_TX_CLK_SRC_MASK << shift,
		clock_bits << shift);

	return 0;
}

static int ucc_get_tdm_common_clk(u32 tdm_num, enum qe_clock clock)
{
	int clock_bits = -EINVAL;

	/*
	 * for TDM[0, 1, 2, 3], TX and RX use  common
	 * clock source BRG3,4 and CLK1,2
	 * for TDM[4, 5, 6, 7], TX and RX use  common
	 * clock source BRG12,13 and CLK23,24
	 */
	switch (tdm_num) {
	case 0:
	case 1:
	case 2:
	case 3:
		switch (clock) {
		case QE_BRG3:
			clock_bits = 1;
			break;
		case QE_BRG4:
			clock_bits = 2;
			break;
		case QE_CLK1:
			clock_bits = 4;
			break;
		case QE_CLK2:
			clock_bits = 5;
			break;
		default:
			break;
		}
		break;
	case 4:
	case 5:
	case 6:
	case 7:
		switch (clock) {
		case QE_BRG12:
			clock_bits = 1;
			break;
		case QE_BRG13:
			clock_bits = 2;
			break;
		case QE_CLK23:
			clock_bits = 4;
			break;
		case QE_CLK24:
			clock_bits = 5;
			break;
		default:
			break;
		}
		break;
	default:
		break;
	}

	return clock_bits;
}

static int ucc_get_tdm_rx_clk(u32 tdm_num, enum qe_clock clock)
{
	int clock_bits = -EINVAL;

	switch (tdm_num) {
	case 0:
		switch (clock) {
		case QE_CLK3:
			clock_bits = 6;
			break;
		case QE_CLK8:
			clock_bits = 7;
			break;
		default:
			break;
		}
		break;
	case 1:
		switch (clock) {
		case QE_CLK5:
			clock_bits = 6;
			break;
		case QE_CLK10:
			clock_bits = 7;
			break;
		default:
			break;
		}
		break;
	case 2:
		switch (clock) {
		case QE_CLK7:
			clock_bits = 6;
			break;
		case QE_CLK12:
			clock_bits = 7;
			break;
		default:
			break;
		}
		break;
	case 3:
		switch (clock) {
		case QE_CLK9:
			clock_bits = 6;
			break;
		case QE_CLK14:
			clock_bits = 7;
			break;
		default:
			break;
		}
		break;
	case 4:
		switch (clock) {
		case QE_CLK11:
			clock_bits = 6;
			break;
		case QE_CLK16:
			clock_bits = 7;
			break;
		default:
			break;
		}
		break;
	case 5:
		switch (clock) {
		case QE_CLK13:
			clock_bits = 6;
			break;
		case QE_CLK18:
			clock_bits = 7;
			break;
		default:
			break;
		}
		break;
	case 6:
		switch (clock) {
		case QE_CLK15:
			clock_bits = 6;
			break;
		case QE_CLK20:
			clock_bits = 7;
			break;
		default:
			break;
		}
		break;
	case 7:
		switch (clock) {
		case QE_CLK17:
			clock_bits = 6;
			break;
		case QE_CLK22:
			clock_bits = 7;
			break;
		default:
			break;
		}
		break;
	}

	return clock_bits;
}

static int ucc_get_tdm_tx_clk(u32 tdm_num, enum qe_clock clock)
{
	int clock_bits = -EINVAL;

	switch (tdm_num) {
	case 0:
		switch (clock) {
		case QE_CLK4:
			clock_bits = 6;
			break;
		case QE_CLK9:
			clock_bits = 7;
			break;
		default:
			break;
		}
		break;
	case 1:
		switch (clock) {
		case QE_CLK6:
			clock_bits = 6;
			break;
		case QE_CLK11:
			clock_bits = 7;
			break;
		default:
			break;
		}
		break;
	case 2:
		switch (clock) {
		case QE_CLK8:
			clock_bits = 6;
			break;
		case QE_CLK13:
			clock_bits = 7;
			break;
		default:
			break;
		}
		break;
	case 3:
		switch (clock) {
		case QE_CLK10:
			clock_bits = 6;
			break;
		case QE_CLK15:
			clock_bits = 7;
			break;
		default:
			break;
		}
		break;
	case 4:
		switch (clock) {
		case QE_CLK12:
			clock_bits = 6;
			break;
		case QE_CLK17:
			clock_bits = 7;
			break;
		default:
			break;
		}
		break;
	case 5:
		switch (clock) {
		case QE_CLK14:
			clock_bits = 6;
			break;
		case QE_CLK19:
			clock_bits = 7;
			break;
		default:
			break;
		}
		break;
	case 6:
		switch (clock) {
		case QE_CLK16:
			clock_bits = 6;
			break;
		case QE_CLK21:
			clock_bits = 7;
			break;
		default:
			break;
		}
		break;
	case 7:
		switch (clock) {
		case QE_CLK18:
			clock_bits = 6;
			break;
		case QE_CLK3:
			clock_bits = 7;
			break;
		default:
			break;
		}
		break;
	}

	return clock_bits;
}

/* tdm_num: TDM A-H port num is 0-7 */
static int ucc_get_tdm_rxtx_clk(enum comm_dir mode, u32 tdm_num,
				enum qe_clock clock)
{
	int clock_bits;

	clock_bits = ucc_get_tdm_common_clk(tdm_num, clock);
	if (clock_bits > 0)
		return clock_bits;
	if (mode == COMM_DIR_RX)
		clock_bits = ucc_get_tdm_rx_clk(tdm_num, clock);
	if (mode == COMM_DIR_TX)
		clock_bits = ucc_get_tdm_tx_clk(tdm_num, clock);
	return clock_bits;
}

static u32 ucc_get_tdm_clk_shift(enum comm_dir mode, u32 tdm_num)
{
	u32 shift;

	shift = (mode == COMM_DIR_RX) ? RX_CLK_SHIFT_BASE : TX_CLK_SHIFT_BASE;
	if (tdm_num < 4)
		shift -= tdm_num * 4;
	else
		shift -= (tdm_num - 4) * 4;

	return shift;
}

int ucc_set_tdm_rxtx_clk(u32 tdm_num, enum qe_clock clock,
			 enum comm_dir mode)
{
	int clock_bits;
	u32 shift;
	struct qe_mux __iomem *qe_mux_reg;
	 __be32 __iomem *cmxs1cr;

	qe_mux_reg = &qe_immr->qmx;

	if (tdm_num > 7 || tdm_num < 0)
		return -EINVAL;

	/* The communications direction must be RX or TX */
	if (mode != COMM_DIR_RX && mode != COMM_DIR_TX)
		return -EINVAL;

	clock_bits = ucc_get_tdm_rxtx_clk(mode, tdm_num, clock);
	if (clock_bits < 0)
		return -EINVAL;

	shift = ucc_get_tdm_clk_shift(mode, tdm_num);

	cmxs1cr = (tdm_num < 4) ? &qe_mux_reg->cmxsi1cr_l :
				  &qe_mux_reg->cmxsi1cr_h;

	qe_clrsetbits32(cmxs1cr, QE_CMXUCR_TX_CLK_SRC_MASK << shift,
			clock_bits << shift);

	return 0;
}

static int ucc_get_tdm_sync_source(u32 tdm_num, enum qe_clock clock,
				   enum comm_dir mode)
{
	int source = -EINVAL;

	if (mode == COMM_DIR_RX && clock == QE_RSYNC_PIN) {
		source = 0;
		return source;
	}
	if (mode == COMM_DIR_TX && clock == QE_TSYNC_PIN) {
		source = 0;
		return source;
	}

	switch (tdm_num) {
	case 0:
	case 1:
		switch (clock) {
		case QE_BRG9:
			source = 1;
			break;
		case QE_BRG10:
			source = 2;
			break;
		default:
			break;
		}
		break;
	case 2:
	case 3:
		switch (clock) {
		case QE_BRG9:
			source = 1;
			break;
		case QE_BRG11:
			source = 2;
			break;
		default:
			break;
		}
		break;
	case 4:
	case 5:
		switch (clock) {
		case QE_BRG13:
			source = 1;
			break;
		case QE_BRG14:
			source = 2;
			break;
		default:
			break;
		}
		break;
	case 6:
	case 7:
		switch (clock) {
		case QE_BRG13:
			source = 1;
			break;
		case QE_BRG15:
			source = 2;
			break;
		default:
			break;
		}
		break;
	}

	return source;
}

static u32 ucc_get_tdm_sync_shift(enum comm_dir mode, u32 tdm_num)
{
	u32 shift;

	shift = (mode == COMM_DIR_RX) ? RX_SYNC_SHIFT_BASE : TX_SYNC_SHIFT_BASE;
	shift -= tdm_num * 2;

	return shift;
}

int ucc_set_tdm_rxtx_sync(u32 tdm_num, enum qe_clock clock,
			  enum comm_dir mode)
{
	int source;
	u32 shift;
	struct qe_mux *qe_mux_reg;

	qe_mux_reg = &qe_immr->qmx;

	if (tdm_num >= UCC_TDM_NUM)
		return -EINVAL;

	/* The communications direction must be RX or TX */
	if (mode != COMM_DIR_RX && mode != COMM_DIR_TX)
		return -EINVAL;

	source = ucc_get_tdm_sync_source(tdm_num, clock, mode);
	if (source < 0)
		return -EINVAL;

	shift = ucc_get_tdm_sync_shift(mode, tdm_num);

	qe_clrsetbits32(&qe_mux_reg->cmxsi1syr,
			QE_CMXUCR_TX_CLK_SRC_MASK << shift,
			source << shift);

	return 0;
}
