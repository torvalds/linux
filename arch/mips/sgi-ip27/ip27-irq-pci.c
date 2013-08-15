/*
 * ip27-irq.c: Highlevel interrupt handling for IP27 architecture.
 *
 * Copyright (C) 1999, 2000 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999, 2000 Silicon Graphics, Inc.
 * Copyright (C) 1999 - 2001 Kanoj Sarcar
 */

#undef DEBUG

#include <linux/init.h>
#include <linux/irq.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/timex.h>
#include <linux/smp.h>
#include <linux/random.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#include <linux/bitops.h>

#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/mipsregs.h>

#include <asm/processor.h>
#include <asm/pci/bridge.h>
#include <asm/sn/addrs.h>
#include <asm/sn/agent.h>
#include <asm/sn/arch.h>
#include <asm/sn/hub.h>
#include <asm/sn/intr.h>

/*
 * Linux has a controller-independent x86 interrupt architecture.
 * every controller has a 'controller-template', that is used
 * by the main code to do the right thing. Each driver-visible
 * interrupt source is transparently wired to the appropriate
 * controller. Thus drivers need not be aware of the
 * interrupt-controller.
 *
 * Various interrupt controllers we handle: 8259 PIC, SMP IO-APIC,
 * PIIX4's internal 8259 PIC and SGI's Visual Workstation Cobalt (IO-)APIC.
 * (IO-APICs assumed to be messaging to Pentium local-APICs)
 *
 * the code is designed to be easily extended with new/different
 * interrupt controllers, without having to do assembly magic.
 */

extern struct bridge_controller *irq_to_bridge[];
extern int irq_to_slot[];

/*
 * use these macros to get the encoded nasid and widget id
 * from the irq value
 */
#define IRQ_TO_BRIDGE(i)		irq_to_bridge[(i)]
#define SLOT_FROM_PCI_IRQ(i)		irq_to_slot[i]

static inline int alloc_level(int cpu, int irq)
{
	struct hub_data *hub = hub_data(cpu_to_node(cpu));
	struct slice_data *si = cpu_data[cpu].data;
	int level;

	level = find_first_zero_bit(hub->irq_alloc_mask, LEVELS_PER_SLICE);
	if (level >= LEVELS_PER_SLICE)
		panic("Cpu %d flooded with devices", cpu);

	__set_bit(level, hub->irq_alloc_mask);
	si->level_to_irq[level] = irq;

	return level;
}

static inline int find_level(cpuid_t *cpunum, int irq)
{
	int cpu, i;

	for_each_online_cpu(cpu) {
		struct slice_data *si = cpu_data[cpu].data;

		for (i = BASE_PCI_IRQ; i < LEVELS_PER_SLICE; i++)
			if (si->level_to_irq[i] == irq) {
				*cpunum = cpu;

				return i;
			}
	}

	panic("Could not identify cpu/level for irq %d", irq);
}

static int intr_connect_level(int cpu, int bit)
{
	nasid_t nasid = COMPACT_TO_NASID_NODEID(cpu_to_node(cpu));
	struct slice_data *si = cpu_data[cpu].data;

	set_bit(bit, si->irq_enable_mask);

	if (!cputoslice(cpu)) {
		REMOTE_HUB_S(nasid, PI_INT_MASK0_A, si->irq_enable_mask[0]);
		REMOTE_HUB_S(nasid, PI_INT_MASK1_A, si->irq_enable_mask[1]);
	} else {
		REMOTE_HUB_S(nasid, PI_INT_MASK0_B, si->irq_enable_mask[0]);
		REMOTE_HUB_S(nasid, PI_INT_MASK1_B, si->irq_enable_mask[1]);
	}

	return 0;
}

static int intr_disconnect_level(int cpu, int bit)
{
	nasid_t nasid = COMPACT_TO_NASID_NODEID(cpu_to_node(cpu));
	struct slice_data *si = cpu_data[cpu].data;

	clear_bit(bit, si->irq_enable_mask);

	if (!cputoslice(cpu)) {
		REMOTE_HUB_S(nasid, PI_INT_MASK0_A, si->irq_enable_mask[0]);
		REMOTE_HUB_S(nasid, PI_INT_MASK1_A, si->irq_enable_mask[1]);
	} else {
		REMOTE_HUB_S(nasid, PI_INT_MASK0_B, si->irq_enable_mask[0]);
		REMOTE_HUB_S(nasid, PI_INT_MASK1_B, si->irq_enable_mask[1]);
	}

	return 0;
}

/* Startup one of the (PCI ...) IRQs routes over a bridge.  */
static unsigned int startup_bridge_irq(struct irq_data *d)
{
	struct bridge_controller *bc;
	bridgereg_t device;
	bridge_t *bridge;
	int pin, swlevel;
	cpuid_t cpu;

	pin = SLOT_FROM_PCI_IRQ(d->irq);
	bc = IRQ_TO_BRIDGE(d->irq);
	bridge = bc->base;

	pr_debug("bridge_startup(): irq= 0x%x  pin=%d\n", d->irq, pin);
	/*
	 * "map" irq to a swlevel greater than 6 since the first 6 bits
	 * of INT_PEND0 are taken
	 */
	swlevel = find_level(&cpu, d->irq);
	bridge->b_int_addr[pin].addr = (0x20000 | swlevel | (bc->nasid << 8));
	bridge->b_int_enable |= (1 << pin);
	bridge->b_int_enable |= 0x7ffffe00;	/* more stuff in int_enable */

	/*
	 * Enable sending of an interrupt clear packt to the hub on a high to
	 * low transition of the interrupt pin.
	 *
	 * IRIX sets additional bits in the address which are documented as
	 * reserved in the bridge docs.
	 */
	bridge->b_int_mode |= (1UL << pin);

	/*
	 * We assume the bridge to have a 1:1 mapping between devices
	 * (slots) and intr pins.
	 */
	device = bridge->b_int_device;
	device &= ~(7 << (pin*3));
	device |= (pin << (pin*3));
	bridge->b_int_device = device;

	bridge->b_wid_tflush;

	intr_connect_level(cpu, swlevel);

	return 0;	/* Never anything pending.  */
}

/* Shutdown one of the (PCI ...) IRQs routes over a bridge.  */
static void shutdown_bridge_irq(struct irq_data *d)
{
	struct bridge_controller *bc = IRQ_TO_BRIDGE(d->irq);
	bridge_t *bridge = bc->base;
	int pin, swlevel;
	cpuid_t cpu;

	pr_debug("bridge_shutdown: irq 0x%x\n", d->irq);
	pin = SLOT_FROM_PCI_IRQ(d->irq);

	/*
	 * map irq to a swlevel greater than 6 since the first 6 bits
	 * of INT_PEND0 are taken
	 */
	swlevel = find_level(&cpu, d->irq);
	intr_disconnect_level(cpu, swlevel);

	bridge->b_int_enable &= ~(1 << pin);
	bridge->b_wid_tflush;
}

static inline void enable_bridge_irq(struct irq_data *d)
{
	cpuid_t cpu;
	int swlevel;

	swlevel = find_level(&cpu, d->irq);	/* Criminal offence */
	intr_connect_level(cpu, swlevel);
}

static inline void disable_bridge_irq(struct irq_data *d)
{
	cpuid_t cpu;
	int swlevel;

	swlevel = find_level(&cpu, d->irq);	/* Criminal offence */
	intr_disconnect_level(cpu, swlevel);
}

static struct irq_chip bridge_irq_type = {
	.name		= "bridge",
	.irq_startup	= startup_bridge_irq,
	.irq_shutdown	= shutdown_bridge_irq,
	.irq_mask	= disable_bridge_irq,
	.irq_unmask	= enable_bridge_irq,
};

void register_bridge_irq(unsigned int irq)
{
	irq_set_chip_and_handler(irq, &bridge_irq_type, handle_level_irq);
}

int request_bridge_irq(struct bridge_controller *bc)
{
	int irq = allocate_irqno();
	int swlevel, cpu;
	nasid_t nasid;

	if (irq < 0)
		return irq;

	/*
	 * "map" irq to a swlevel greater than 6 since the first 6 bits
	 * of INT_PEND0 are taken
	 */
	cpu = bc->irq_cpu;
	swlevel = alloc_level(cpu, irq);
	if (unlikely(swlevel < 0)) {
		free_irqno(irq);

		return -EAGAIN;
	}

	/* Make sure it's not already pending when we connect it. */
	nasid = COMPACT_TO_NASID_NODEID(cpu_to_node(cpu));
	REMOTE_HUB_CLR_INTR(nasid, swlevel);

	intr_connect_level(cpu, swlevel);

	register_bridge_irq(irq);

	return irq;
}
