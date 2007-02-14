/*
 * Cell Broadband Engine Performance Monitor
 *
 * (C) Copyright IBM Corporation 2001,2006
 *
 * Author:
 *    David Erb (djerb@us.ibm.com)
 *    Kevin Corry (kevcorry@us.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/interrupt.h>
#include <linux/types.h>
#include <asm/io.h>
#include <asm/irq_regs.h>
#include <asm/machdep.h>
#include <asm/pmc.h>
#include <asm/reg.h>
#include <asm/spu.h>

#include "cbe_regs.h"
#include "interrupt.h"

/*
 * When writing to write-only mmio addresses, save a shadow copy. All of the
 * registers are 32-bit, but stored in the upper-half of a 64-bit field in
 * pmd_regs.
 */

#define WRITE_WO_MMIO(reg, x)					\
	do {							\
		u32 _x = (x);					\
		struct cbe_pmd_regs __iomem *pmd_regs;		\
		struct cbe_pmd_shadow_regs *shadow_regs;	\
		pmd_regs = cbe_get_cpu_pmd_regs(cpu);		\
		shadow_regs = cbe_get_cpu_pmd_shadow_regs(cpu);	\
		out_be64(&(pmd_regs->reg), (((u64)_x) << 32));	\
		shadow_regs->reg = _x;				\
	} while (0)

#define READ_SHADOW_REG(val, reg)				\
	do {							\
		struct cbe_pmd_shadow_regs *shadow_regs;	\
		shadow_regs = cbe_get_cpu_pmd_shadow_regs(cpu);	\
		(val) = shadow_regs->reg;			\
	} while (0)

#define READ_MMIO_UPPER32(val, reg)				\
	do {							\
		struct cbe_pmd_regs __iomem *pmd_regs;		\
		pmd_regs = cbe_get_cpu_pmd_regs(cpu);		\
		(val) = (u32)(in_be64(&pmd_regs->reg) >> 32);	\
	} while (0)

/*
 * Physical counter registers.
 * Each physical counter can act as one 32-bit counter or two 16-bit counters.
 */

u32 cbe_read_phys_ctr(u32 cpu, u32 phys_ctr)
{
	u32 val_in_latch, val = 0;

	if (phys_ctr < NR_PHYS_CTRS) {
		READ_SHADOW_REG(val_in_latch, counter_value_in_latch);

		/* Read the latch or the actual counter, whichever is newer. */
		if (val_in_latch & (1 << phys_ctr)) {
			READ_SHADOW_REG(val, pm_ctr[phys_ctr]);
		} else {
			READ_MMIO_UPPER32(val, pm_ctr[phys_ctr]);
		}
	}

	return val;
}
EXPORT_SYMBOL_GPL(cbe_read_phys_ctr);

void cbe_write_phys_ctr(u32 cpu, u32 phys_ctr, u32 val)
{
	struct cbe_pmd_shadow_regs *shadow_regs;
	u32 pm_ctrl;

	if (phys_ctr < NR_PHYS_CTRS) {
		/* Writing to a counter only writes to a hardware latch.
		 * The new value is not propagated to the actual counter
		 * until the performance monitor is enabled.
		 */
		WRITE_WO_MMIO(pm_ctr[phys_ctr], val);

		pm_ctrl = cbe_read_pm(cpu, pm_control);
		if (pm_ctrl & CBE_PM_ENABLE_PERF_MON) {
			/* The counters are already active, so we need to
			 * rewrite the pm_control register to "re-enable"
			 * the PMU.
			 */
			cbe_write_pm(cpu, pm_control, pm_ctrl);
		} else {
			shadow_regs = cbe_get_cpu_pmd_shadow_regs(cpu);
			shadow_regs->counter_value_in_latch |= (1 << phys_ctr);
		}
	}
}
EXPORT_SYMBOL_GPL(cbe_write_phys_ctr);

/*
 * "Logical" counter registers.
 * These will read/write 16-bits or 32-bits depending on the
 * current size of the counter. Counters 4 - 7 are always 16-bit.
 */

u32 cbe_read_ctr(u32 cpu, u32 ctr)
{
	u32 val;
	u32 phys_ctr = ctr & (NR_PHYS_CTRS - 1);

	val = cbe_read_phys_ctr(cpu, phys_ctr);

	if (cbe_get_ctr_size(cpu, phys_ctr) == 16)
		val = (ctr < NR_PHYS_CTRS) ? (val >> 16) : (val & 0xffff);

	return val;
}
EXPORT_SYMBOL_GPL(cbe_read_ctr);

void cbe_write_ctr(u32 cpu, u32 ctr, u32 val)
{
	u32 phys_ctr;
	u32 phys_val;

	phys_ctr = ctr & (NR_PHYS_CTRS - 1);

	if (cbe_get_ctr_size(cpu, phys_ctr) == 16) {
		phys_val = cbe_read_phys_ctr(cpu, phys_ctr);

		if (ctr < NR_PHYS_CTRS)
			val = (val << 16) | (phys_val & 0xffff);
		else
			val = (val & 0xffff) | (phys_val & 0xffff0000);
	}

	cbe_write_phys_ctr(cpu, phys_ctr, val);
}
EXPORT_SYMBOL_GPL(cbe_write_ctr);

/*
 * Counter-control registers.
 * Each "logical" counter has a corresponding control register.
 */

u32 cbe_read_pm07_control(u32 cpu, u32 ctr)
{
	u32 pm07_control = 0;

	if (ctr < NR_CTRS)
		READ_SHADOW_REG(pm07_control, pm07_control[ctr]);

	return pm07_control;
}
EXPORT_SYMBOL_GPL(cbe_read_pm07_control);

void cbe_write_pm07_control(u32 cpu, u32 ctr, u32 val)
{
	if (ctr < NR_CTRS)
		WRITE_WO_MMIO(pm07_control[ctr], val);
}
EXPORT_SYMBOL_GPL(cbe_write_pm07_control);

/*
 * Other PMU control registers. Most of these are write-only.
 */

u32 cbe_read_pm(u32 cpu, enum pm_reg_name reg)
{
	u32 val = 0;

	switch (reg) {
	case group_control:
		READ_SHADOW_REG(val, group_control);
		break;

	case debug_bus_control:
		READ_SHADOW_REG(val, debug_bus_control);
		break;

	case trace_address:
		READ_MMIO_UPPER32(val, trace_address);
		break;

	case ext_tr_timer:
		READ_SHADOW_REG(val, ext_tr_timer);
		break;

	case pm_status:
		READ_MMIO_UPPER32(val, pm_status);
		break;

	case pm_control:
		READ_SHADOW_REG(val, pm_control);
		break;

	case pm_interval:
		READ_SHADOW_REG(val, pm_interval);
		break;

	case pm_start_stop:
		READ_SHADOW_REG(val, pm_start_stop);
		break;
	}

	return val;
}
EXPORT_SYMBOL_GPL(cbe_read_pm);

void cbe_write_pm(u32 cpu, enum pm_reg_name reg, u32 val)
{
	switch (reg) {
	case group_control:
		WRITE_WO_MMIO(group_control, val);
		break;

	case debug_bus_control:
		WRITE_WO_MMIO(debug_bus_control, val);
		break;

	case trace_address:
		WRITE_WO_MMIO(trace_address, val);
		break;

	case ext_tr_timer:
		WRITE_WO_MMIO(ext_tr_timer, val);
		break;

	case pm_status:
		WRITE_WO_MMIO(pm_status, val);
		break;

	case pm_control:
		WRITE_WO_MMIO(pm_control, val);
		break;

	case pm_interval:
		WRITE_WO_MMIO(pm_interval, val);
		break;

	case pm_start_stop:
		WRITE_WO_MMIO(pm_start_stop, val);
		break;
	}
}
EXPORT_SYMBOL_GPL(cbe_write_pm);

/*
 * Get/set the size of a physical counter to either 16 or 32 bits.
 */

u32 cbe_get_ctr_size(u32 cpu, u32 phys_ctr)
{
	u32 pm_ctrl, size = 0;

	if (phys_ctr < NR_PHYS_CTRS) {
		pm_ctrl = cbe_read_pm(cpu, pm_control);
		size = (pm_ctrl & CBE_PM_16BIT_CTR(phys_ctr)) ? 16 : 32;
	}

	return size;
}
EXPORT_SYMBOL_GPL(cbe_get_ctr_size);

void cbe_set_ctr_size(u32 cpu, u32 phys_ctr, u32 ctr_size)
{
	u32 pm_ctrl;

	if (phys_ctr < NR_PHYS_CTRS) {
		pm_ctrl = cbe_read_pm(cpu, pm_control);
		switch (ctr_size) {
		case 16:
			pm_ctrl |= CBE_PM_16BIT_CTR(phys_ctr);
			break;

		case 32:
			pm_ctrl &= ~CBE_PM_16BIT_CTR(phys_ctr);
			break;
		}
		cbe_write_pm(cpu, pm_control, pm_ctrl);
	}
}
EXPORT_SYMBOL_GPL(cbe_set_ctr_size);

/*
 * Enable/disable the entire performance monitoring unit.
 * When we enable the PMU, all pending writes to counters get committed.
 */

void cbe_enable_pm(u32 cpu)
{
	struct cbe_pmd_shadow_regs *shadow_regs;
	u32 pm_ctrl;

	shadow_regs = cbe_get_cpu_pmd_shadow_regs(cpu);
	shadow_regs->counter_value_in_latch = 0;

	pm_ctrl = cbe_read_pm(cpu, pm_control) | CBE_PM_ENABLE_PERF_MON;
	cbe_write_pm(cpu, pm_control, pm_ctrl);
}
EXPORT_SYMBOL_GPL(cbe_enable_pm);

void cbe_disable_pm(u32 cpu)
{
	u32 pm_ctrl;
	pm_ctrl = cbe_read_pm(cpu, pm_control) & ~CBE_PM_ENABLE_PERF_MON;
	cbe_write_pm(cpu, pm_control, pm_ctrl);
}
EXPORT_SYMBOL_GPL(cbe_disable_pm);

/*
 * Reading from the trace_buffer.
 * The trace buffer is two 64-bit registers. Reading from
 * the second half automatically increments the trace_address.
 */

void cbe_read_trace_buffer(u32 cpu, u64 *buf)
{
	struct cbe_pmd_regs __iomem *pmd_regs = cbe_get_cpu_pmd_regs(cpu);

	*buf++ = in_be64(&pmd_regs->trace_buffer_0_63);
	*buf++ = in_be64(&pmd_regs->trace_buffer_64_127);
}
EXPORT_SYMBOL_GPL(cbe_read_trace_buffer);

/*
 * Enabling/disabling interrupts for the entire performance monitoring unit.
 */

u32 cbe_get_and_clear_pm_interrupts(u32 cpu)
{
	/* Reading pm_status clears the interrupt bits. */
	return cbe_read_pm(cpu, pm_status);
}
EXPORT_SYMBOL_GPL(cbe_get_and_clear_pm_interrupts);

void cbe_enable_pm_interrupts(u32 cpu, u32 thread, u32 mask)
{
	/* Set which node and thread will handle the next interrupt. */
	iic_set_interrupt_routing(cpu, thread, 0);

	/* Enable the interrupt bits in the pm_status register. */
	if (mask)
		cbe_write_pm(cpu, pm_status, mask);
}
EXPORT_SYMBOL_GPL(cbe_enable_pm_interrupts);

void cbe_disable_pm_interrupts(u32 cpu)
{
	cbe_get_and_clear_pm_interrupts(cpu);
	cbe_write_pm(cpu, pm_status, 0);
}
EXPORT_SYMBOL_GPL(cbe_disable_pm_interrupts);

static irqreturn_t cbe_pm_irq(int irq, void *dev_id)
{
	perf_irq(get_irq_regs());
	return IRQ_HANDLED;
}

static int __init cbe_init_pm_irq(void)
{
	unsigned int irq;
	int rc, node;

	if (!machine_is(cell))
		return 0;

	for_each_node(node) {
		irq = irq_create_mapping(NULL, IIC_IRQ_IOEX_PMI |
					       (node << IIC_IRQ_NODE_SHIFT));
		if (irq == NO_IRQ) {
			printk("ERROR: Unable to allocate irq for node %d\n",
			       node);
			return -EINVAL;
		}

		rc = request_irq(irq, cbe_pm_irq,
				 IRQF_DISABLED, "cbe-pmu-0", NULL);
		if (rc) {
			printk("ERROR: Request for irq on node %d failed\n",
			       node);
			return rc;
		}
	}

	return 0;
}
arch_initcall(cbe_init_pm_irq);

void cbe_sync_irq(int node)
{
	unsigned int irq;

	irq = irq_find_mapping(NULL,
			       IIC_IRQ_IOEX_PMI
			       | (node << IIC_IRQ_NODE_SHIFT));

	if (irq == NO_IRQ) {
		printk(KERN_WARNING "ERROR, unable to get existing irq %d " \
		"for node %d\n", irq, node);
		return;
	}

	synchronize_irq(irq);
}
EXPORT_SYMBOL_GPL(cbe_sync_irq);

