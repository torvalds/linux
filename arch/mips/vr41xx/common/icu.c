/*
 *  icu.c, Interrupt Control Unit routines for the NEC VR4100 series.
 *
 *  Copyright (C) 2001-2002  MontaVista Software Inc.
 *    Author: Yoichi Yuasa <yyuasa@mvista.com or source@mvista.com>
 *  Copyright (C) 2003-2004  Yoichi Yuasa <yuasa@hh.iij4u.or.jp>
 *  Copyright (C) 2005 Ralf Baechle (ralf@linux-mips.org)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
/*
 * Changes:
 *  MontaVista Software Inc. <yyuasa@mvista.com> or <source@mvista.com>
 *  - New creation, NEC VR4122 and VR4131 are supported.
 *  - Added support for NEC VR4111 and VR4121.
 *
 *  Yoichi Yuasa <yuasa@hh.iij4u.or.jp>
 *  - Coped with INTASSIGN of NEC VR4133.
 */
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/types.h>

#include <asm/cpu.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/irq_cpu.h>
#include <asm/vr41xx/vr41xx.h>

extern asmlinkage void vr41xx_handle_interrupt(void);

extern void init_vr41xx_giuint_irq(void);
extern void giuint_irq_dispatch(struct pt_regs *regs);

static uint32_t icu1_base;
static uint32_t icu2_base;

static struct irqaction icu_cascade = {
	.handler	= no_action,
	.mask		= CPU_MASK_NONE,
	.name		= "cascade",
};

static unsigned char sysint1_assign[16] = {
	0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static unsigned char sysint2_assign[16] = {
	2, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

#define SYSINT1REG_TYPE1	KSEG1ADDR(0x0b000080)
#define SYSINT2REG_TYPE1	KSEG1ADDR(0x0b000200)

#define SYSINT1REG_TYPE2	KSEG1ADDR(0x0f000080)
#define SYSINT2REG_TYPE2	KSEG1ADDR(0x0f0000a0)

#define SYSINT1REG	0x00
#define PIUINTREG	0x02
#define INTASSIGN0	0x04
#define INTASSIGN1	0x06
#define GIUINTLREG	0x08
#define DSIUINTREG	0x0a
#define MSYSINT1REG	0x0c
#define MPIUINTREG	0x0e
#define MAIUINTREG	0x10
#define MKIUINTREG	0x12
#define MGIUINTLREG	0x14
#define MDSIUINTREG	0x16
#define NMIREG		0x18
#define SOFTREG		0x1a
#define INTASSIGN2	0x1c
#define INTASSIGN3	0x1e

#define SYSINT2REG	0x00
#define GIUINTHREG	0x02
#define FIRINTREG	0x04
#define MSYSINT2REG	0x06
#define MGIUINTHREG	0x08
#define MFIRINTREG	0x0a
#define PCIINTREG	0x0c
 #define PCIINT0	0x0001
#define SCUINTREG	0x0e
 #define SCUINT0	0x0001
#define CSIINTREG	0x10
#define MPCIINTREG	0x12
#define MSCUINTREG	0x14
#define MCSIINTREG	0x16
#define BCUINTREG	0x18
 #define BCUINTR	0x0001
#define MBCUINTREG	0x1a

#define SYSINT1_IRQ_TO_PIN(x)	((x) - SYSINT1_IRQ_BASE)	/* Pin 0-15 */
#define SYSINT2_IRQ_TO_PIN(x)	((x) - SYSINT2_IRQ_BASE)	/* Pin 0-15 */

#define read_icu1(offset)	readw(icu1_base + (offset))
#define write_icu1(val, offset)	writew((val), icu1_base + (offset))

#define read_icu2(offset)	readw(icu2_base + (offset))
#define write_icu2(val, offset)	writew((val), icu2_base + (offset))

#define INTASSIGN_MAX	4
#define INTASSIGN_MASK	0x0007

static inline uint16_t set_icu1(uint8_t offset, uint16_t set)
{
	uint16_t res;

	res = read_icu1(offset);
	res |= set;
	write_icu1(res, offset);

	return res;
}

static inline uint16_t clear_icu1(uint8_t offset, uint16_t clear)
{
	uint16_t res;

	res = read_icu1(offset);
	res &= ~clear;
	write_icu1(res, offset);

	return res;
}

static inline uint16_t set_icu2(uint8_t offset, uint16_t set)
{
	uint16_t res;

	res = read_icu2(offset);
	res |= set;
	write_icu2(res, offset);

	return res;
}

static inline uint16_t clear_icu2(uint8_t offset, uint16_t clear)
{
	uint16_t res;

	res = read_icu2(offset);
	res &= ~clear;
	write_icu2(res, offset);

	return res;
}

/*=======================================================================*/

void vr41xx_enable_piuint(uint16_t mask)
{
	irq_desc_t *desc = irq_desc + PIU_IRQ;
	unsigned long flags;

	if (current_cpu_data.cputype == CPU_VR4111 ||
	    current_cpu_data.cputype == CPU_VR4121) {
		spin_lock_irqsave(&desc->lock, flags);
		set_icu1(MPIUINTREG, mask);
		spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_enable_piuint);

void vr41xx_disable_piuint(uint16_t mask)
{
	irq_desc_t *desc = irq_desc + PIU_IRQ;
	unsigned long flags;

	if (current_cpu_data.cputype == CPU_VR4111 ||
	    current_cpu_data.cputype == CPU_VR4121) {
		spin_lock_irqsave(&desc->lock, flags);
		clear_icu1(MPIUINTREG, mask);
		spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_disable_piuint);

void vr41xx_enable_aiuint(uint16_t mask)
{
	irq_desc_t *desc = irq_desc + AIU_IRQ;
	unsigned long flags;

	if (current_cpu_data.cputype == CPU_VR4111 ||
	    current_cpu_data.cputype == CPU_VR4121) {
		spin_lock_irqsave(&desc->lock, flags);
		set_icu1(MAIUINTREG, mask);
		spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_enable_aiuint);

void vr41xx_disable_aiuint(uint16_t mask)
{
	irq_desc_t *desc = irq_desc + AIU_IRQ;
	unsigned long flags;

	if (current_cpu_data.cputype == CPU_VR4111 ||
	    current_cpu_data.cputype == CPU_VR4121) {
		spin_lock_irqsave(&desc->lock, flags);
		clear_icu1(MAIUINTREG, mask);
		spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_disable_aiuint);

void vr41xx_enable_kiuint(uint16_t mask)
{
	irq_desc_t *desc = irq_desc + KIU_IRQ;
	unsigned long flags;

	if (current_cpu_data.cputype == CPU_VR4111 ||
	    current_cpu_data.cputype == CPU_VR4121) {
		spin_lock_irqsave(&desc->lock, flags);
		set_icu1(MKIUINTREG, mask);
		spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_enable_kiuint);

void vr41xx_disable_kiuint(uint16_t mask)
{
	irq_desc_t *desc = irq_desc + KIU_IRQ;
	unsigned long flags;

	if (current_cpu_data.cputype == CPU_VR4111 ||
	    current_cpu_data.cputype == CPU_VR4121) {
		spin_lock_irqsave(&desc->lock, flags);
		clear_icu1(MKIUINTREG, mask);
		spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_disable_kiuint);

void vr41xx_enable_dsiuint(uint16_t mask)
{
	irq_desc_t *desc = irq_desc + DSIU_IRQ;
	unsigned long flags;

	spin_lock_irqsave(&desc->lock, flags);
	set_icu1(MDSIUINTREG, mask);
	spin_unlock_irqrestore(&desc->lock, flags);
}

EXPORT_SYMBOL(vr41xx_enable_dsiuint);

void vr41xx_disable_dsiuint(uint16_t mask)
{
	irq_desc_t *desc = irq_desc + DSIU_IRQ;
	unsigned long flags;

	spin_lock_irqsave(&desc->lock, flags);
	clear_icu1(MDSIUINTREG, mask);
	spin_unlock_irqrestore(&desc->lock, flags);
}

EXPORT_SYMBOL(vr41xx_disable_dsiuint);

void vr41xx_enable_firint(uint16_t mask)
{
	irq_desc_t *desc = irq_desc + FIR_IRQ;
	unsigned long flags;

	spin_lock_irqsave(&desc->lock, flags);
	set_icu2(MFIRINTREG, mask);
	spin_unlock_irqrestore(&desc->lock, flags);
}

EXPORT_SYMBOL(vr41xx_enable_firint);

void vr41xx_disable_firint(uint16_t mask)
{
	irq_desc_t *desc = irq_desc + FIR_IRQ;
	unsigned long flags;

	spin_lock_irqsave(&desc->lock, flags);
	clear_icu2(MFIRINTREG, mask);
	spin_unlock_irqrestore(&desc->lock, flags);
}

EXPORT_SYMBOL(vr41xx_disable_firint);

void vr41xx_enable_pciint(void)
{
	irq_desc_t *desc = irq_desc + PCI_IRQ;
	unsigned long flags;

	if (current_cpu_data.cputype == CPU_VR4122 ||
	    current_cpu_data.cputype == CPU_VR4131 ||
	    current_cpu_data.cputype == CPU_VR4133) {
		spin_lock_irqsave(&desc->lock, flags);
		write_icu2(PCIINT0, MPCIINTREG);
		spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_enable_pciint);

void vr41xx_disable_pciint(void)
{
	irq_desc_t *desc = irq_desc + PCI_IRQ;
	unsigned long flags;

	if (current_cpu_data.cputype == CPU_VR4122 ||
	    current_cpu_data.cputype == CPU_VR4131 ||
	    current_cpu_data.cputype == CPU_VR4133) {
		spin_lock_irqsave(&desc->lock, flags);
		write_icu2(0, MPCIINTREG);
		spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_disable_pciint);

void vr41xx_enable_scuint(void)
{
	irq_desc_t *desc = irq_desc + SCU_IRQ;
	unsigned long flags;

	if (current_cpu_data.cputype == CPU_VR4122 ||
	    current_cpu_data.cputype == CPU_VR4131 ||
	    current_cpu_data.cputype == CPU_VR4133) {
		spin_lock_irqsave(&desc->lock, flags);
		write_icu2(SCUINT0, MSCUINTREG);
		spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_enable_scuint);

void vr41xx_disable_scuint(void)
{
	irq_desc_t *desc = irq_desc + SCU_IRQ;
	unsigned long flags;

	if (current_cpu_data.cputype == CPU_VR4122 ||
	    current_cpu_data.cputype == CPU_VR4131 ||
	    current_cpu_data.cputype == CPU_VR4133) {
		spin_lock_irqsave(&desc->lock, flags);
		write_icu2(0, MSCUINTREG);
		spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_disable_scuint);

void vr41xx_enable_csiint(uint16_t mask)
{
	irq_desc_t *desc = irq_desc + CSI_IRQ;
	unsigned long flags;

	if (current_cpu_data.cputype == CPU_VR4122 ||
	    current_cpu_data.cputype == CPU_VR4131 ||
	    current_cpu_data.cputype == CPU_VR4133) {
		spin_lock_irqsave(&desc->lock, flags);
		set_icu2(MCSIINTREG, mask);
		spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_enable_csiint);

void vr41xx_disable_csiint(uint16_t mask)
{
	irq_desc_t *desc = irq_desc + CSI_IRQ;
	unsigned long flags;

	if (current_cpu_data.cputype == CPU_VR4122 ||
	    current_cpu_data.cputype == CPU_VR4131 ||
	    current_cpu_data.cputype == CPU_VR4133) {
		spin_lock_irqsave(&desc->lock, flags);
		clear_icu2(MCSIINTREG, mask);
		spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_disable_csiint);

void vr41xx_enable_bcuint(void)
{
	irq_desc_t *desc = irq_desc + BCU_IRQ;
	unsigned long flags;

	if (current_cpu_data.cputype == CPU_VR4122 ||
	    current_cpu_data.cputype == CPU_VR4131 ||
	    current_cpu_data.cputype == CPU_VR4133) {
		spin_lock_irqsave(&desc->lock, flags);
		write_icu2(BCUINTR, MBCUINTREG);
		spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_enable_bcuint);

void vr41xx_disable_bcuint(void)
{
	irq_desc_t *desc = irq_desc + BCU_IRQ;
	unsigned long flags;

	if (current_cpu_data.cputype == CPU_VR4122 ||
	    current_cpu_data.cputype == CPU_VR4131 ||
	    current_cpu_data.cputype == CPU_VR4133) {
		spin_lock_irqsave(&desc->lock, flags);
		write_icu2(0, MBCUINTREG);
		spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_disable_bcuint);

/*=======================================================================*/

static unsigned int startup_sysint1_irq(unsigned int irq)
{
	set_icu1(MSYSINT1REG, (uint16_t)1 << SYSINT1_IRQ_TO_PIN(irq));

	return 0; /* never anything pending */
}

static void shutdown_sysint1_irq(unsigned int irq)
{
	clear_icu1(MSYSINT1REG, (uint16_t)1 << SYSINT1_IRQ_TO_PIN(irq));
}

static void enable_sysint1_irq(unsigned int irq)
{
	set_icu1(MSYSINT1REG, (uint16_t)1 << SYSINT1_IRQ_TO_PIN(irq));
}

#define disable_sysint1_irq	shutdown_sysint1_irq
#define ack_sysint1_irq		shutdown_sysint1_irq

static void end_sysint1_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		set_icu1(MSYSINT1REG, (uint16_t)1 << SYSINT1_IRQ_TO_PIN(irq));
}

static struct hw_interrupt_type sysint1_irq_type = {
	.typename	= "SYSINT1",
	.startup	= startup_sysint1_irq,
	.shutdown	= shutdown_sysint1_irq,
	.enable		= enable_sysint1_irq,
	.disable	= disable_sysint1_irq,
	.ack		= ack_sysint1_irq,
	.end		= end_sysint1_irq,
};

/*=======================================================================*/

static unsigned int startup_sysint2_irq(unsigned int irq)
{
	set_icu2(MSYSINT2REG, (uint16_t)1 << SYSINT2_IRQ_TO_PIN(irq));

	return 0; /* never anything pending */
}

static void shutdown_sysint2_irq(unsigned int irq)
{
	clear_icu2(MSYSINT2REG, (uint16_t)1 << SYSINT2_IRQ_TO_PIN(irq));
}

static void enable_sysint2_irq(unsigned int irq)
{
	set_icu2(MSYSINT2REG, (uint16_t)1 << SYSINT2_IRQ_TO_PIN(irq));
}

#define disable_sysint2_irq	shutdown_sysint2_irq
#define ack_sysint2_irq		shutdown_sysint2_irq

static void end_sysint2_irq(unsigned int irq)
{
	if (!(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS)))
		set_icu2(MSYSINT2REG, (uint16_t)1 << SYSINT2_IRQ_TO_PIN(irq));
}

static struct hw_interrupt_type sysint2_irq_type = {
	.typename	= "SYSINT2",
	.startup	= startup_sysint2_irq,
	.shutdown	= shutdown_sysint2_irq,
	.enable		= enable_sysint2_irq,
	.disable	= disable_sysint2_irq,
	.ack		= ack_sysint2_irq,
	.end		= end_sysint2_irq,
};

/*=======================================================================*/

static inline int set_sysint1_assign(unsigned int irq, unsigned char assign)
{
	irq_desc_t *desc = irq_desc + irq;
	uint16_t intassign0, intassign1;
	unsigned int pin;

	pin = SYSINT1_IRQ_TO_PIN(irq);

	spin_lock_irq(&desc->lock);

	intassign0 = read_icu1(INTASSIGN0);
	intassign1 = read_icu1(INTASSIGN1);

	switch (pin) {
	case 0:
		intassign0 &= ~INTASSIGN_MASK;
		intassign0 |= (uint16_t)assign;
		break;
	case 1:
		intassign0 &= ~(INTASSIGN_MASK << 3);
		intassign0 |= (uint16_t)assign << 3;
		break;
	case 2:
		intassign0 &= ~(INTASSIGN_MASK << 6);
		intassign0 |= (uint16_t)assign << 6;
		break;
	case 3:
		intassign0 &= ~(INTASSIGN_MASK << 9);
		intassign0 |= (uint16_t)assign << 9;
		break;
	case 8:
		intassign0 &= ~(INTASSIGN_MASK << 12);
		intassign0 |= (uint16_t)assign << 12;
		break;
	case 9:
		intassign1 &= ~INTASSIGN_MASK;
		intassign1 |= (uint16_t)assign;
		break;
	case 11:
		intassign1 &= ~(INTASSIGN_MASK << 6);
		intassign1 |= (uint16_t)assign << 6;
		break;
	case 12:
		intassign1 &= ~(INTASSIGN_MASK << 9);
		intassign1 |= (uint16_t)assign << 9;
		break;
	default:
		return -EINVAL;
	}

	sysint1_assign[pin] = assign;
	write_icu1(intassign0, INTASSIGN0);
	write_icu1(intassign1, INTASSIGN1);

	spin_unlock_irq(&desc->lock);

	return 0;
}

static inline int set_sysint2_assign(unsigned int irq, unsigned char assign)
{
	irq_desc_t *desc = irq_desc + irq;
	uint16_t intassign2, intassign3;
	unsigned int pin;

	pin = SYSINT2_IRQ_TO_PIN(irq);

	spin_lock_irq(&desc->lock);

	intassign2 = read_icu1(INTASSIGN2);
	intassign3 = read_icu1(INTASSIGN3);

	switch (pin) {
	case 0:
		intassign2 &= ~INTASSIGN_MASK;
		intassign2 |= (uint16_t)assign;
		break;
	case 1:
		intassign2 &= ~(INTASSIGN_MASK << 3);
		intassign2 |= (uint16_t)assign << 3;
		break;
	case 3:
		intassign2 &= ~(INTASSIGN_MASK << 6);
		intassign2 |= (uint16_t)assign << 6;
		break;
	case 4:
		intassign2 &= ~(INTASSIGN_MASK << 9);
		intassign2 |= (uint16_t)assign << 9;
		break;
	case 5:
		intassign2 &= ~(INTASSIGN_MASK << 12);
		intassign2 |= (uint16_t)assign << 12;
		break;
	case 6:
		intassign3 &= ~INTASSIGN_MASK;
		intassign3 |= (uint16_t)assign;
		break;
	case 7:
		intassign3 &= ~(INTASSIGN_MASK << 3);
		intassign3 |= (uint16_t)assign << 3;
		break;
	case 8:
		intassign3 &= ~(INTASSIGN_MASK << 6);
		intassign3 |= (uint16_t)assign << 6;
		break;
	case 9:
		intassign3 &= ~(INTASSIGN_MASK << 9);
		intassign3 |= (uint16_t)assign << 9;
		break;
	case 10:
		intassign3 &= ~(INTASSIGN_MASK << 12);
		intassign3 |= (uint16_t)assign << 12;
		break;
	default:
		return -EINVAL;
	}

	sysint2_assign[pin] = assign;
	write_icu1(intassign2, INTASSIGN2);
	write_icu1(intassign3, INTASSIGN3);

	spin_unlock_irq(&desc->lock);

	return 0;
}

int vr41xx_set_intassign(unsigned int irq, unsigned char intassign)
{
	int retval = -EINVAL;

	if (current_cpu_data.cputype != CPU_VR4133)
		return -EINVAL;

	if (intassign > INTASSIGN_MAX)
		return -EINVAL;

	if (irq >= SYSINT1_IRQ_BASE && irq <= SYSINT1_IRQ_LAST)
		retval = set_sysint1_assign(irq, intassign);
	else if (irq >= SYSINT2_IRQ_BASE && irq <= SYSINT2_IRQ_LAST)
		retval = set_sysint2_assign(irq, intassign);

	return retval;
}

EXPORT_SYMBOL(vr41xx_set_intassign);

/*=======================================================================*/

asmlinkage void irq_dispatch(unsigned char intnum, struct pt_regs *regs)
{
	uint16_t pend1, pend2;
	uint16_t mask1, mask2;
	int i;

	pend1 = read_icu1(SYSINT1REG);
	mask1 = read_icu1(MSYSINT1REG);

	pend2 = read_icu2(SYSINT2REG);
	mask2 = read_icu2(MSYSINT2REG);

	mask1 &= pend1;
	mask2 &= pend2;

	if (mask1) {
		for (i = 0; i < 16; i++) {
			if (intnum == sysint1_assign[i] &&
			    (mask1 & ((uint16_t)1 << i))) {
				if (i == 8)
					giuint_irq_dispatch(regs);
				else
					do_IRQ(SYSINT1_IRQ(i), regs);
				return;
			}
		}
	}

	if (mask2) {
		for (i = 0; i < 16; i++) {
			if (intnum == sysint2_assign[i] &&
			    (mask2 & ((uint16_t)1 << i))) {
				do_IRQ(SYSINT2_IRQ(i), regs);
				return;
			}
		}
	}

	printk(KERN_ERR "spurious ICU interrupt: %04x,%04x\n", pend1, pend2);

	atomic_inc(&irq_err_count);
}

/*=======================================================================*/

static int __init vr41xx_icu_init(void)
{
	switch (current_cpu_data.cputype) {
	case CPU_VR4111:
	case CPU_VR4121:
		icu1_base = SYSINT1REG_TYPE1;
		icu2_base = SYSINT2REG_TYPE1;
		break;
	case CPU_VR4122:
	case CPU_VR4131:
	case CPU_VR4133:
		icu1_base = SYSINT1REG_TYPE2;
		icu2_base = SYSINT2REG_TYPE2;
		break;
	default:
		printk(KERN_ERR "ICU: Unexpected CPU of NEC VR4100 series\n");
		return -EINVAL;
	}

	write_icu1(0, MSYSINT1REG);
	write_icu1(0xffff, MGIUINTLREG);

	write_icu2(0, MSYSINT2REG);
	write_icu2(0xffff, MGIUINTHREG);

	return 0;
}

early_initcall(vr41xx_icu_init);

/*=======================================================================*/

static inline void init_vr41xx_icu_irq(void)
{
	int i;

	for (i = SYSINT1_IRQ_BASE; i <= SYSINT1_IRQ_LAST; i++)
		irq_desc[i].handler = &sysint1_irq_type;

	for (i = SYSINT2_IRQ_BASE; i <= SYSINT2_IRQ_LAST; i++)
		irq_desc[i].handler = &sysint2_irq_type;

	setup_irq(INT0_CASCADE_IRQ, &icu_cascade);
	setup_irq(INT1_CASCADE_IRQ, &icu_cascade);
	setup_irq(INT2_CASCADE_IRQ, &icu_cascade);
	setup_irq(INT3_CASCADE_IRQ, &icu_cascade);
	setup_irq(INT4_CASCADE_IRQ, &icu_cascade);
}

void __init arch_init_irq(void)
{
	mips_cpu_irq_init(MIPS_CPU_IRQ_BASE);
	init_vr41xx_icu_irq();
	init_vr41xx_giuint_irq();

	set_except_vector(0, vr41xx_handle_interrupt);
}
