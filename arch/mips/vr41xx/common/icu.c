// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  icu.c, Interrupt Control Unit routines for the NEC VR4100 series.
 *
 *  Copyright (C) 2001-2002  MontaVista Software Inc.
 *    Author: Yoichi Yuasa <source@mvista.com>
 *  Copyright (C) 2003-2006  Yoichi Yuasa <yuasa@linux-mips.org>
 */
/*
 * Changes:
 *  MontaVista Software Inc. <source@mvista.com>
 *  - New creation, NEC VR4122 and VR4131 are supported.
 *  - Added support for NEC VR4111 and VR4121.
 *
 *  Yoichi Yuasa <yuasa@linux-mips.org>
 *  - Coped with INTASSIGN of NEC VR4133.
 */
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/irq.h>
#include <linux/smp.h>
#include <linux/types.h>

#include <asm/cpu.h>
#include <asm/io.h>
#include <asm/vr41xx/irq.h>
#include <asm/vr41xx/vr41xx.h>

static void __iomem *icu1_base;
static void __iomem *icu2_base;

static unsigned char sysint1_assign[16] = {
	0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
static unsigned char sysint2_assign[16] = {
	2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

#define ICU1_TYPE1_BASE 0x0b000080UL
#define ICU2_TYPE1_BASE 0x0b000200UL

#define ICU1_TYPE2_BASE 0x0f000080UL
#define ICU2_TYPE2_BASE 0x0f0000a0UL

#define ICU1_SIZE	0x20
#define ICU2_SIZE	0x1c

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
#define MMACINTREG	0x12
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

#define INT_TO_IRQ(x)		((x) + 2)	/* Int0-4 -> IRQ2-6 */

#define icu1_read(offset)		readw(icu1_base + (offset))
#define icu1_write(offset, value)	writew((value), icu1_base + (offset))

#define icu2_read(offset)		readw(icu2_base + (offset))
#define icu2_write(offset, value)	writew((value), icu2_base + (offset))

#define INTASSIGN_MAX	4
#define INTASSIGN_MASK	0x0007

static inline uint16_t icu1_set(uint8_t offset, uint16_t set)
{
	uint16_t data;

	data = icu1_read(offset);
	data |= set;
	icu1_write(offset, data);

	return data;
}

static inline uint16_t icu1_clear(uint8_t offset, uint16_t clear)
{
	uint16_t data;

	data = icu1_read(offset);
	data &= ~clear;
	icu1_write(offset, data);

	return data;
}

static inline uint16_t icu2_set(uint8_t offset, uint16_t set)
{
	uint16_t data;

	data = icu2_read(offset);
	data |= set;
	icu2_write(offset, data);

	return data;
}

static inline uint16_t icu2_clear(uint8_t offset, uint16_t clear)
{
	uint16_t data;

	data = icu2_read(offset);
	data &= ~clear;
	icu2_write(offset, data);

	return data;
}

void vr41xx_enable_piuint(uint16_t mask)
{
	struct irq_desc *desc = irq_to_desc(PIU_IRQ);
	unsigned long flags;

	if (current_cpu_type() == CPU_VR4111 ||
	    current_cpu_type() == CPU_VR4121) {
		raw_spin_lock_irqsave(&desc->lock, flags);
		icu1_set(MPIUINTREG, mask);
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_enable_piuint);

void vr41xx_disable_piuint(uint16_t mask)
{
	struct irq_desc *desc = irq_to_desc(PIU_IRQ);
	unsigned long flags;

	if (current_cpu_type() == CPU_VR4111 ||
	    current_cpu_type() == CPU_VR4121) {
		raw_spin_lock_irqsave(&desc->lock, flags);
		icu1_clear(MPIUINTREG, mask);
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_disable_piuint);

void vr41xx_enable_aiuint(uint16_t mask)
{
	struct irq_desc *desc = irq_to_desc(AIU_IRQ);
	unsigned long flags;

	if (current_cpu_type() == CPU_VR4111 ||
	    current_cpu_type() == CPU_VR4121) {
		raw_spin_lock_irqsave(&desc->lock, flags);
		icu1_set(MAIUINTREG, mask);
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_enable_aiuint);

void vr41xx_disable_aiuint(uint16_t mask)
{
	struct irq_desc *desc = irq_to_desc(AIU_IRQ);
	unsigned long flags;

	if (current_cpu_type() == CPU_VR4111 ||
	    current_cpu_type() == CPU_VR4121) {
		raw_spin_lock_irqsave(&desc->lock, flags);
		icu1_clear(MAIUINTREG, mask);
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_disable_aiuint);

void vr41xx_enable_kiuint(uint16_t mask)
{
	struct irq_desc *desc = irq_to_desc(KIU_IRQ);
	unsigned long flags;

	if (current_cpu_type() == CPU_VR4111 ||
	    current_cpu_type() == CPU_VR4121) {
		raw_spin_lock_irqsave(&desc->lock, flags);
		icu1_set(MKIUINTREG, mask);
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_enable_kiuint);

void vr41xx_disable_kiuint(uint16_t mask)
{
	struct irq_desc *desc = irq_to_desc(KIU_IRQ);
	unsigned long flags;

	if (current_cpu_type() == CPU_VR4111 ||
	    current_cpu_type() == CPU_VR4121) {
		raw_spin_lock_irqsave(&desc->lock, flags);
		icu1_clear(MKIUINTREG, mask);
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_disable_kiuint);

void vr41xx_enable_macint(uint16_t mask)
{
	struct irq_desc *desc = irq_to_desc(ETHERNET_IRQ);
	unsigned long flags;

	raw_spin_lock_irqsave(&desc->lock, flags);
	icu1_set(MMACINTREG, mask);
	raw_spin_unlock_irqrestore(&desc->lock, flags);
}

EXPORT_SYMBOL(vr41xx_enable_macint);

void vr41xx_disable_macint(uint16_t mask)
{
	struct irq_desc *desc = irq_to_desc(ETHERNET_IRQ);
	unsigned long flags;

	raw_spin_lock_irqsave(&desc->lock, flags);
	icu1_clear(MMACINTREG, mask);
	raw_spin_unlock_irqrestore(&desc->lock, flags);
}

EXPORT_SYMBOL(vr41xx_disable_macint);

void vr41xx_enable_dsiuint(uint16_t mask)
{
	struct irq_desc *desc = irq_to_desc(DSIU_IRQ);
	unsigned long flags;

	raw_spin_lock_irqsave(&desc->lock, flags);
	icu1_set(MDSIUINTREG, mask);
	raw_spin_unlock_irqrestore(&desc->lock, flags);
}

EXPORT_SYMBOL(vr41xx_enable_dsiuint);

void vr41xx_disable_dsiuint(uint16_t mask)
{
	struct irq_desc *desc = irq_to_desc(DSIU_IRQ);
	unsigned long flags;

	raw_spin_lock_irqsave(&desc->lock, flags);
	icu1_clear(MDSIUINTREG, mask);
	raw_spin_unlock_irqrestore(&desc->lock, flags);
}

EXPORT_SYMBOL(vr41xx_disable_dsiuint);

void vr41xx_enable_firint(uint16_t mask)
{
	struct irq_desc *desc = irq_to_desc(FIR_IRQ);
	unsigned long flags;

	raw_spin_lock_irqsave(&desc->lock, flags);
	icu2_set(MFIRINTREG, mask);
	raw_spin_unlock_irqrestore(&desc->lock, flags);
}

EXPORT_SYMBOL(vr41xx_enable_firint);

void vr41xx_disable_firint(uint16_t mask)
{
	struct irq_desc *desc = irq_to_desc(FIR_IRQ);
	unsigned long flags;

	raw_spin_lock_irqsave(&desc->lock, flags);
	icu2_clear(MFIRINTREG, mask);
	raw_spin_unlock_irqrestore(&desc->lock, flags);
}

EXPORT_SYMBOL(vr41xx_disable_firint);

void vr41xx_enable_pciint(void)
{
	struct irq_desc *desc = irq_to_desc(PCI_IRQ);
	unsigned long flags;

	if (current_cpu_type() == CPU_VR4122 ||
	    current_cpu_type() == CPU_VR4131 ||
	    current_cpu_type() == CPU_VR4133) {
		raw_spin_lock_irqsave(&desc->lock, flags);
		icu2_write(MPCIINTREG, PCIINT0);
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_enable_pciint);

void vr41xx_disable_pciint(void)
{
	struct irq_desc *desc = irq_to_desc(PCI_IRQ);
	unsigned long flags;

	if (current_cpu_type() == CPU_VR4122 ||
	    current_cpu_type() == CPU_VR4131 ||
	    current_cpu_type() == CPU_VR4133) {
		raw_spin_lock_irqsave(&desc->lock, flags);
		icu2_write(MPCIINTREG, 0);
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_disable_pciint);

void vr41xx_enable_scuint(void)
{
	struct irq_desc *desc = irq_to_desc(SCU_IRQ);
	unsigned long flags;

	if (current_cpu_type() == CPU_VR4122 ||
	    current_cpu_type() == CPU_VR4131 ||
	    current_cpu_type() == CPU_VR4133) {
		raw_spin_lock_irqsave(&desc->lock, flags);
		icu2_write(MSCUINTREG, SCUINT0);
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_enable_scuint);

void vr41xx_disable_scuint(void)
{
	struct irq_desc *desc = irq_to_desc(SCU_IRQ);
	unsigned long flags;

	if (current_cpu_type() == CPU_VR4122 ||
	    current_cpu_type() == CPU_VR4131 ||
	    current_cpu_type() == CPU_VR4133) {
		raw_spin_lock_irqsave(&desc->lock, flags);
		icu2_write(MSCUINTREG, 0);
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_disable_scuint);

void vr41xx_enable_csiint(uint16_t mask)
{
	struct irq_desc *desc = irq_to_desc(CSI_IRQ);
	unsigned long flags;

	if (current_cpu_type() == CPU_VR4122 ||
	    current_cpu_type() == CPU_VR4131 ||
	    current_cpu_type() == CPU_VR4133) {
		raw_spin_lock_irqsave(&desc->lock, flags);
		icu2_set(MCSIINTREG, mask);
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_enable_csiint);

void vr41xx_disable_csiint(uint16_t mask)
{
	struct irq_desc *desc = irq_to_desc(CSI_IRQ);
	unsigned long flags;

	if (current_cpu_type() == CPU_VR4122 ||
	    current_cpu_type() == CPU_VR4131 ||
	    current_cpu_type() == CPU_VR4133) {
		raw_spin_lock_irqsave(&desc->lock, flags);
		icu2_clear(MCSIINTREG, mask);
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_disable_csiint);

void vr41xx_enable_bcuint(void)
{
	struct irq_desc *desc = irq_to_desc(BCU_IRQ);
	unsigned long flags;

	if (current_cpu_type() == CPU_VR4122 ||
	    current_cpu_type() == CPU_VR4131 ||
	    current_cpu_type() == CPU_VR4133) {
		raw_spin_lock_irqsave(&desc->lock, flags);
		icu2_write(MBCUINTREG, BCUINTR);
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_enable_bcuint);

void vr41xx_disable_bcuint(void)
{
	struct irq_desc *desc = irq_to_desc(BCU_IRQ);
	unsigned long flags;

	if (current_cpu_type() == CPU_VR4122 ||
	    current_cpu_type() == CPU_VR4131 ||
	    current_cpu_type() == CPU_VR4133) {
		raw_spin_lock_irqsave(&desc->lock, flags);
		icu2_write(MBCUINTREG, 0);
		raw_spin_unlock_irqrestore(&desc->lock, flags);
	}
}

EXPORT_SYMBOL(vr41xx_disable_bcuint);

static void disable_sysint1_irq(struct irq_data *d)
{
	icu1_clear(MSYSINT1REG, 1 << SYSINT1_IRQ_TO_PIN(d->irq));
}

static void enable_sysint1_irq(struct irq_data *d)
{
	icu1_set(MSYSINT1REG, 1 << SYSINT1_IRQ_TO_PIN(d->irq));
}

static struct irq_chip sysint1_irq_type = {
	.name		= "SYSINT1",
	.irq_mask	= disable_sysint1_irq,
	.irq_unmask	= enable_sysint1_irq,
};

static void disable_sysint2_irq(struct irq_data *d)
{
	icu2_clear(MSYSINT2REG, 1 << SYSINT2_IRQ_TO_PIN(d->irq));
}

static void enable_sysint2_irq(struct irq_data *d)
{
	icu2_set(MSYSINT2REG, 1 << SYSINT2_IRQ_TO_PIN(d->irq));
}

static struct irq_chip sysint2_irq_type = {
	.name		= "SYSINT2",
	.irq_mask	= disable_sysint2_irq,
	.irq_unmask	= enable_sysint2_irq,
};

static inline int set_sysint1_assign(unsigned int irq, unsigned char assign)
{
	struct irq_desc *desc = irq_to_desc(irq);
	uint16_t intassign0, intassign1;
	unsigned int pin;

	pin = SYSINT1_IRQ_TO_PIN(irq);

	raw_spin_lock_irq(&desc->lock);

	intassign0 = icu1_read(INTASSIGN0);
	intassign1 = icu1_read(INTASSIGN1);

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
		raw_spin_unlock_irq(&desc->lock);
		return -EINVAL;
	}

	sysint1_assign[pin] = assign;
	icu1_write(INTASSIGN0, intassign0);
	icu1_write(INTASSIGN1, intassign1);

	raw_spin_unlock_irq(&desc->lock);

	return 0;
}

static inline int set_sysint2_assign(unsigned int irq, unsigned char assign)
{
	struct irq_desc *desc = irq_to_desc(irq);
	uint16_t intassign2, intassign3;
	unsigned int pin;

	pin = SYSINT2_IRQ_TO_PIN(irq);

	raw_spin_lock_irq(&desc->lock);

	intassign2 = icu1_read(INTASSIGN2);
	intassign3 = icu1_read(INTASSIGN3);

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
		raw_spin_unlock_irq(&desc->lock);
		return -EINVAL;
	}

	sysint2_assign[pin] = assign;
	icu1_write(INTASSIGN2, intassign2);
	icu1_write(INTASSIGN3, intassign3);

	raw_spin_unlock_irq(&desc->lock);

	return 0;
}

int vr41xx_set_intassign(unsigned int irq, unsigned char intassign)
{
	int retval = -EINVAL;

	if (current_cpu_type() != CPU_VR4133)
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

static int icu_get_irq(unsigned int irq)
{
	uint16_t pend1, pend2;
	uint16_t mask1, mask2;
	int i;

	pend1 = icu1_read(SYSINT1REG);
	mask1 = icu1_read(MSYSINT1REG);

	pend2 = icu2_read(SYSINT2REG);
	mask2 = icu2_read(MSYSINT2REG);

	mask1 &= pend1;
	mask2 &= pend2;

	if (mask1) {
		for (i = 0; i < 16; i++) {
			if (irq == INT_TO_IRQ(sysint1_assign[i]) && (mask1 & (1 << i)))
				return SYSINT1_IRQ(i);
		}
	}

	if (mask2) {
		for (i = 0; i < 16; i++) {
			if (irq == INT_TO_IRQ(sysint2_assign[i]) && (mask2 & (1 << i)))
				return SYSINT2_IRQ(i);
		}
	}

	printk(KERN_ERR "spurious ICU interrupt: %04x,%04x\n", pend1, pend2);

	atomic_inc(&irq_err_count);

	return -1;
}

static int __init vr41xx_icu_init(void)
{
	unsigned long icu1_start, icu2_start;
	int i;

	switch (current_cpu_type()) {
	case CPU_VR4111:
	case CPU_VR4121:
		icu1_start = ICU1_TYPE1_BASE;
		icu2_start = ICU2_TYPE1_BASE;
		break;
	case CPU_VR4122:
	case CPU_VR4131:
	case CPU_VR4133:
		icu1_start = ICU1_TYPE2_BASE;
		icu2_start = ICU2_TYPE2_BASE;
		break;
	default:
		printk(KERN_ERR "ICU: Unexpected CPU of NEC VR4100 series\n");
		return -ENODEV;
	}

	if (request_mem_region(icu1_start, ICU1_SIZE, "ICU") == NULL)
		return -EBUSY;

	if (request_mem_region(icu2_start, ICU2_SIZE, "ICU") == NULL) {
		release_mem_region(icu1_start, ICU1_SIZE);
		return -EBUSY;
	}

	icu1_base = ioremap(icu1_start, ICU1_SIZE);
	if (icu1_base == NULL) {
		release_mem_region(icu1_start, ICU1_SIZE);
		release_mem_region(icu2_start, ICU2_SIZE);
		return -ENOMEM;
	}

	icu2_base = ioremap(icu2_start, ICU2_SIZE);
	if (icu2_base == NULL) {
		iounmap(icu1_base);
		release_mem_region(icu1_start, ICU1_SIZE);
		release_mem_region(icu2_start, ICU2_SIZE);
		return -ENOMEM;
	}

	icu1_write(MSYSINT1REG, 0);
	icu1_write(MGIUINTLREG, 0xffff);

	icu2_write(MSYSINT2REG, 0);
	icu2_write(MGIUINTHREG, 0xffff);

	for (i = SYSINT1_IRQ_BASE; i <= SYSINT1_IRQ_LAST; i++)
		irq_set_chip_and_handler(i, &sysint1_irq_type,
					 handle_level_irq);

	for (i = SYSINT2_IRQ_BASE; i <= SYSINT2_IRQ_LAST; i++)
		irq_set_chip_and_handler(i, &sysint2_irq_type,
					 handle_level_irq);

	cascade_irq(INT0_IRQ, icu_get_irq);
	cascade_irq(INT1_IRQ, icu_get_irq);
	cascade_irq(INT2_IRQ, icu_get_irq);
	cascade_irq(INT3_IRQ, icu_get_irq);
	cascade_irq(INT4_IRQ, icu_get_irq);

	return 0;
}

core_initcall(vr41xx_icu_init);
