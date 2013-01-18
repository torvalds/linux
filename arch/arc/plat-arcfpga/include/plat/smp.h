/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Rajeshwar Ranga: Interrupt Distribution Unit API's
 */

#ifndef __PLAT_ARCFPGA_SMP_H
#define __PLAT_ARCFPGA_SMP_H

#ifdef CONFIG_SMP

#include <linux/types.h>
#include <asm/arcregs.h>

#define ARC_AUX_IDU_REG_CMD		0x2000
#define ARC_AUX_IDU_REG_PARAM		0x2001

#define ARC_AUX_XTL_REG_CMD		0x2002
#define ARC_AUX_XTL_REG_PARAM		0x2003

#define ARC_REG_MP_BCR			0x2021

#define ARC_XTL_CMD_WRITE_PC		0x04
#define ARC_XTL_CMD_CLEAR_HALT		0x02

/*
 * Build Configuration Register which identifies the sub-components
 */
struct bcr_mp {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int mp_arch:16, pad:5, sdu:1, idu:1, scu:1, ver:8;
#else
	unsigned int ver:8, scu:1, idu:1, sdu:1, pad:5, mp_arch:16;
#endif
};

/* IDU supports 256 common interrupts */
#define NR_IDU_IRQS			256

/*
 * The Aux Regs layout is same bit-by-bit in both BE/LE modes.
 * However when casted as a bitfield encoded "C" struct, gcc treats it as
 * memory, generating different code for BE/LE, requiring strcture adj (see
 * include/asm/arcregs.h)
 *
 * However when manually "carving" the value for a Aux, no special handling
 * of BE is needed because of the property discribed above
 */
#define IDU_SET_COMMAND(irq, cmd)			\
do {							\
	uint32_t __val;					\
	__val = (((irq & 0xFF) << 8) | (cmd & 0xFF));	\
	write_aux_reg(ARC_AUX_IDU_REG_CMD, __val);	\
} while (0)

#define IDU_SET_PARAM(par)  write_aux_reg(ARC_AUX_IDU_REG_PARAM, par)
#define IDU_GET_PARAM()     read_aux_reg(ARC_AUX_IDU_REG_PARAM)

/* IDU Commands */
#define IDU_DISABLE			0x00
#define IDU_ENABLE			0x01
#define IDU_IRQ_CLEAR			0x02
#define IDU_IRQ_ASSERT			0x03
#define IDU_IRQ_WMODE			0x04
#define IDU_IRQ_STATUS			0x05
#define IDU_IRQ_ACK			0x06
#define IDU_IRQ_PEND			0x07
#define IDU_IRQ_RMODE			0x08
#define IDU_IRQ_WBITMASK		0x09
#define IDU_IRQ_RBITMASK		0x0A

#define idu_enable()		IDU_SET_COMMAND(0, IDU_ENABLE)
#define idu_disable()		IDU_SET_COMMAND(0, IDU_DISABLE)

#define idu_irq_assert(irq)	IDU_SET_COMMAND((irq), IDU_IRQ_ASSERT)
#define idu_irq_clear(irq)	IDU_SET_COMMAND((irq), IDU_IRQ_CLEAR)

/* IDU Interrupt Mode - Destination Encoding */
#define IDU_IRQ_MOD_DISABLE		0x00
#define IDU_IRQ_MOD_ROUND_RECP		0x01
#define IDU_IRQ_MOD_TCPU_FIRSTRECP	0x02
#define IDU_IRQ_MOD_TCPU_ALLRECP	0x03

/* IDU Interrupt Mode  - Triggering Mode */
#define IDU_IRQ_MODE_LEVEL_TRIG		0x00
#define IDU_IRQ_MODE_PULSE_TRIG		0x01

#define IDU_IRQ_MODE_PARAM(dest_mode, trig_mode)   \
	(((trig_mode & 0x01) << 15) | (dest_mode & 0xFF))

struct idu_irq_config {
	uint8_t irq;
	uint8_t dest_mode;
	uint8_t trig_mode;
};

struct idu_irq_status {
	uint8_t irq;
	bool enabled;
	bool status;
	bool ack;
	bool pend;
	uint8_t next_rr;
};

extern void idu_irq_set_tgtcpu(uint8_t irq, uint32_t mask);
extern void idu_irq_set_mode(uint8_t irq, uint8_t dest_mode, uint8_t trig_mode);

#endif	/* CONFIG_SMP */

#endif
