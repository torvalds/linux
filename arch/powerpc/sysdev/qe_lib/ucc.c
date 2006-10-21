/*
 * arch/powerpc/sysdev/qe_lib/ucc.c
 *
 * QE UCC API Set - UCC specific routines implementations.
 *
 * Copyright (C) 2006 Freescale Semicondutor, Inc. All rights reserved.
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
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/stddef.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <asm/immap_qe.h>
#include <asm/qe.h>
#include <asm/ucc.h>

static DEFINE_SPINLOCK(ucc_lock);

int ucc_set_qe_mux_mii_mng(int ucc_num)
{
	unsigned long flags;

	spin_lock_irqsave(&ucc_lock, flags);
	out_be32(&qe_immr->qmx.cmxgcr,
		 ((in_be32(&qe_immr->qmx.cmxgcr) &
		   ~QE_CMXGCR_MII_ENET_MNG) |
		  (ucc_num << QE_CMXGCR_MII_ENET_MNG_SHIFT)));
	spin_unlock_irqrestore(&ucc_lock, flags);

	return 0;
}

int ucc_set_type(int ucc_num, struct ucc_common *regs,
		 enum ucc_speed_type speed)
{
	u8 guemr = 0;

	/* check if the UCC number is in range. */
	if ((ucc_num > UCC_MAX_NUM - 1) || (ucc_num < 0))
		return -EINVAL;

	guemr = regs->guemr;
	guemr &= ~(UCC_GUEMR_MODE_MASK_RX | UCC_GUEMR_MODE_MASK_TX);
	switch (speed) {
	case UCC_SPEED_TYPE_SLOW:
		guemr |= (UCC_GUEMR_MODE_SLOW_RX | UCC_GUEMR_MODE_SLOW_TX);
		break;
	case UCC_SPEED_TYPE_FAST:
		guemr |= (UCC_GUEMR_MODE_FAST_RX | UCC_GUEMR_MODE_FAST_TX);
		break;
	default:
		return -EINVAL;
	}
	regs->guemr = guemr;

	return 0;
}

int ucc_init_guemr(struct ucc_common *regs)
{
	u8 guemr = 0;

	if (!regs)
		return -EINVAL;

	/* Set bit 3 (which is reserved in the GUEMR register) to 1 */
	guemr = UCC_GUEMR_SET_RESERVED3;

	regs->guemr = guemr;

	return 0;
}

static void get_cmxucr_reg(int ucc_num, volatile u32 ** p_cmxucr, u8 * reg_num,
			   u8 * shift)
{
	switch (ucc_num) {
	case 0: *p_cmxucr = &(qe_immr->qmx.cmxucr1);
		*reg_num = 1;
		*shift = 16;
		break;
	case 2: *p_cmxucr = &(qe_immr->qmx.cmxucr1);
		*reg_num = 1;
		*shift = 0;
		break;
	case 4: *p_cmxucr = &(qe_immr->qmx.cmxucr2);
		*reg_num = 2;
		*shift = 16;
		break;
	case 6: *p_cmxucr = &(qe_immr->qmx.cmxucr2);
		*reg_num = 2;
		*shift = 0;
		break;
	case 1: *p_cmxucr = &(qe_immr->qmx.cmxucr3);
		*reg_num = 3;
		*shift = 16;
		break;
	case 3: *p_cmxucr = &(qe_immr->qmx.cmxucr3);
		*reg_num = 3;
		*shift = 0;
		break;
	case 5: *p_cmxucr = &(qe_immr->qmx.cmxucr4);
		*reg_num = 4;
		*shift = 16;
		break;
	case 7: *p_cmxucr = &(qe_immr->qmx.cmxucr4);
		*reg_num = 4;
		*shift = 0;
		break;
	default:
		break;
	}
}

int ucc_mux_set_grant_tsa_bkpt(int ucc_num, int set, u32 mask)
{
	volatile u32 *p_cmxucr;
	u8 reg_num;
	u8 shift;

	/* check if the UCC number is in range. */
	if ((ucc_num > UCC_MAX_NUM - 1) || (ucc_num < 0))
		return -EINVAL;

	get_cmxucr_reg(ucc_num, &p_cmxucr, &reg_num, &shift);

	if (set)
		out_be32(p_cmxucr, in_be32(p_cmxucr) | (mask << shift));
	else
		out_be32(p_cmxucr, in_be32(p_cmxucr) & ~(mask << shift));

	return 0;
}

int ucc_set_qe_mux_rxtx(int ucc_num, enum qe_clock clock, enum comm_dir mode)
{
	volatile u32 *p_cmxucr;
	u8 reg_num;
	u8 shift;
	u32 clock_bits;
	u32 clock_mask;
	int source = -1;

	/* check if the UCC number is in range. */
	if ((ucc_num > UCC_MAX_NUM - 1) || (ucc_num < 0))
		return -EINVAL;

	if (!((mode == COMM_DIR_RX) || (mode == COMM_DIR_TX))) {
		printk(KERN_ERR
		       "ucc_set_qe_mux_rxtx: bad comm mode type passed.");
		return -EINVAL;
	}

	get_cmxucr_reg(ucc_num, &p_cmxucr, &reg_num, &shift);

	switch (reg_num) {
	case 1:
		switch (clock) {
		case QE_BRG1:	source = 1; break;
		case QE_BRG2:	source = 2; break;
		case QE_BRG7:	source = 3; break;
		case QE_BRG8:	source = 4; break;
		case QE_CLK9:	source = 5; break;
		case QE_CLK10:	source = 6; break;
		case QE_CLK11:	source = 7; break;
		case QE_CLK12:	source = 8; break;
		case QE_CLK15:	source = 9; break;
		case QE_CLK16:	source = 10; break;
		default: 	source = -1; break;
		}
		break;
	case 2:
		switch (clock) {
		case QE_BRG5:	source = 1; break;
		case QE_BRG6:	source = 2; break;
		case QE_BRG7:	source = 3; break;
		case QE_BRG8:	source = 4; break;
		case QE_CLK13:	source = 5; break;
		case QE_CLK14:	source = 6; break;
		case QE_CLK19:	source = 7; break;
		case QE_CLK20:	source = 8; break;
		case QE_CLK15:	source = 9; break;
		case QE_CLK16:	source = 10; break;
		default: 	source = -1; break;
		}
		break;
	case 3:
		switch (clock) {
		case QE_BRG9:	source = 1; break;
		case QE_BRG10:	source = 2; break;
		case QE_BRG15:	source = 3; break;
		case QE_BRG16:	source = 4; break;
		case QE_CLK3:	source = 5; break;
		case QE_CLK4:	source = 6; break;
		case QE_CLK17:	source = 7; break;
		case QE_CLK18:	source = 8; break;
		case QE_CLK7:	source = 9; break;
		case QE_CLK8:	source = 10; break;
		default:	source = -1; break;
		}
		break;
	case 4:
		switch (clock) {
		case QE_BRG13:	source = 1; break;
		case QE_BRG14:	source = 2; break;
		case QE_BRG15:	source = 3; break;
		case QE_BRG16:	source = 4; break;
		case QE_CLK5:	source = 5; break;
		case QE_CLK6:	source = 6; break;
		case QE_CLK21:	source = 7; break;
		case QE_CLK22:	source = 8; break;
		case QE_CLK7:	source = 9; break;
		case QE_CLK8:	source = 10; break;
		default: 	source = -1; break;
		}
		break;
	default:
		source = -1;
		break;
	}

	if (source == -1) {
		printk(KERN_ERR
		     "ucc_set_qe_mux_rxtx: Bad combination of clock and UCC.");
		return -ENOENT;
	}

	clock_bits = (u32) source;
	clock_mask = QE_CMXUCR_TX_CLK_SRC_MASK;
	if (mode == COMM_DIR_RX) {
		clock_bits <<= 4;  /* Rx field is 4 bits to left of Tx field */
		clock_mask <<= 4;  /* Rx field is 4 bits to left of Tx field */
	}
	clock_bits <<= shift;
	clock_mask <<= shift;

	out_be32(p_cmxucr, (in_be32(p_cmxucr) & ~clock_mask) | clock_bits);

	return 0;
}
