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
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#include <linux/bitops.h>

#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/mipsregs.h>
#include <asm/system.h>

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
 * interrupt source is transparently wired to the apropriate
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

extern asmlinkage void ip27_irq(void);

extern struct bridge_controller *irq_to_bridge[];
extern int irq_to_slot[];

/*
 * use these macros to get the encoded nasid and widget id
 * from the irq value
 */
#define IRQ_TO_BRIDGE(i)		irq_to_bridge[(i)]
#define	SLOT_FROM_PCI_IRQ(i)		irq_to_slot[i]

static inline int alloc_level(int cpu, int irq)
{
	struct hub_data *hub = hub_data(cpu_to_node(cpu));
	struct slice_data *si = cpu_data[cpu].data;
	int level;

	level = find_first_zero_bit(hub->irq_alloc_mask, LEVELS_PER_SLICE);
	if (level >= LEVELS_PER_SLICE)
		panic("Cpu %d flooded with devices\n", cpu);

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

	panic("Could not identify cpu/level for irq %d\n", irq);
}

/*
 * Find first bit set
 */
static int ms1bit(unsigned long x)
{
	int b = 0, s;

	s = 16; if (x >> 16 == 0) s = 0; b += s; x >>= s;
	s =  8; if (x >>  8 == 0) s = 0; b += s; x >>= s;
	s =  4; if (x >>  4 == 0) s = 0; b += s; x >>= s;
	s =  2; if (x >>  2 == 0) s = 0; b += s; x >>= s;
	s =  1; if (x >>  1 == 0) s = 0; b += s;

	return b;
}

/*
 * This code is unnecessarily complex, because we do IRQF_DISABLED
 * intr enabling. Basically, once we grab the set of intrs we need
 * to service, we must mask _all_ these interrupts; firstly, to make
 * sure the same intr does not intr again, causing recursion that
 * can lead to stack overflow. Secondly, we can not just mask the
 * one intr we are do_IRQing, because the non-masked intrs in the
 * first set might intr again, causing multiple servicings of the
 * same intr. This effect is mostly seen for intercpu intrs.
 * Kanoj 05.13.00
 */

static void ip27_do_irq_mask0(void)
{
	int irq, swlevel;
	hubreg_t pend0, mask0;
	cpuid_t cpu = smp_processor_id();
	int pi_int_mask0 =
		(cputoslice(cpu) == 0) ?  PI_INT_MASK0_A : PI_INT_MASK0_B;

	/* copied from Irix intpend0() */
	pend0 = LOCAL_HUB_L(PI_INT_PEND0);
	mask0 = LOCAL_HUB_L(pi_int_mask0);

	pend0 &= mask0;		/* Pick intrs we should look at */
	if (!pend0)
		return;

	swlevel = ms1bit(pend0);
#ifdef CONFIG_SMP
	if (pend0 & (1UL << CPU_RESCHED_A_IRQ)) {
		LOCAL_HUB_CLR_INTR(CPU_RESCHED_A_IRQ);
	} else if (pend0 & (1UL << CPU_RESCHED_B_IRQ)) {
		LOCAL_HUB_CLR_INTR(CPU_RESCHED_B_IRQ);
	} else if (pend0 & (1UL << CPU_CALL_A_IRQ)) {
		LOCAL_HUB_CLR_INTR(CPU_CALL_A_IRQ);
		smp_call_function_interrupt();
	} else if (pend0 & (1UL << CPU_CALL_B_IRQ)) {
		LOCAL_HUB_CLR_INTR(CPU_CALL_B_IRQ);
		smp_call_function_interrupt();
	} else
#endif
	{
		/* "map" swlevel to irq */
		struct slice_data *si = cpu_data[cpu].data;

		irq = si->level_to_irq[swlevel];
		do_IRQ(irq);
	}

	LOCAL_HUB_L(PI_INT_PEND0);
}

static void ip27_do_irq_mask1(void)
{
	int irq, swlevel;
	hubreg_t pend1, mask1;
	cpuid_t cpu = smp_processor_id();
	int pi_int_mask1 = (cputoslice(cpu) == 0) ?  PI_INT_MASK1_A : PI_INT_MASK1_B;
	struct slice_data *si = cpu_data[cpu].data;

	/* copied from Irix intpend0() */
	pend1 = LOCAL_HUB_L(PI_INT_PEND1);
	mask1 = LOCAL_HUB_L(pi_int_mask1);

	pend1 &= mask1;		/* Pick intrs we should look at */
	if (!pend1)
		return;

	swlevel = ms1bit(pend1);
	/* "map" swlevel to irq */
	irq = si->level_to_irq[swlevel];
	LOCAL_HUB_CLR_INTR(swlevel);
	do_IRQ(irq);

	LOCAL_HUB_L(PI_INT_PEND1);
}

static void ip27_prof_timer(void)
{
	panic("CPU %d got a profiling interrupt", smp_processor_id());
}

static void ip27_hub_error(void)
{
	panic("CPU %d got a hub error interrupt", smp_processor_id());
}

static int intr_connect_level(int cpu, int bit)
{
	nasid_t nasid = COMPACT_TO_NASID_NODEID(cpu_to_node(cpu));
	struct slice_data *si = cpu_data[cpu].data;
	unsigned long flags;

	set_bit(bit, si->irq_enable_mask);

	local_irq_save(flags);
	if (!cputoslice(cpu)) {
		REMOTE_HUB_S(nasid, PI_INT_MASK0_A, si->irq_enable_mask[0]);
		REMOTE_HUB_S(nasid, PI_INT_MASK1_A, si->irq_enable_mask[1]);
	} else {
		REMOTE_HUB_S(nasid, PI_INT_MASK0_B, si->irq_enable_mask[0]);
		REMOTE_HUB_S(nasid, PI_INT_MASK1_B, si->irq_enable_mask[1]);
	}
	local_irq_restore(flags);

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
static unsigned int startup_bridge_irq(unsigned int irq)
{
	struct bridge_controller *bc;
	bridgereg_t device;
	bridge_t *bridge;
	int pin, swlevel;
	cpuid_t cpu;

	pin = SLOT_FROM_PCI_IRQ(irq);
	bc = IRQ_TO_BRIDGE(irq);
	bridge = bc->base;

	pr_debug("bridge_startup(): irq= 0x%x  pin=%d\n", irq, pin);
	/*
	 * "map" irq to a swlevel greater than 6 since the first 6 bits
	 * of INT_PEND0 are taken
	 */
	swlevel = find_level(&cpu, irq);
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

        return 0;       /* Never anything pending.  */
}

/* Shutdown one of the (PCI ...) IRQs routes over a bridge.  */
static void shutdown_bridge_irq(unsigned int irq)
{
	struct bridge_controller *bc = IRQ_TO_BRIDGE(irq);
	struct hub_data *hub = hub_data(cpu_to_node(bc->irq_cpu));
	bridge_t *bridge = bc->base;
	int pin, swlevel;
	cpuid_t cpu;

	pr_debug("bridge_shutdown: irq 0x%x\n", irq);
	pin = SLOT_FROM_PCI_IRQ(irq);

	/*
	 * map irq to a swlevel greater than 6 since the first 6 bits
	 * of INT_PEND0 are taken
	 */
	swlevel = find_level(&cpu, irq);
	intr_disconnect_level(cpu, swlevel);

	__clear_bit(swlevel, hub->irq_alloc_mask);

	bridge->b_int_enable &= ~(1 << pin);
	bridge->b_wid_tflush;
}

static inline void enable_bridge_irq(unsigned int irq)
{
	cpuid_t cpu;
	int swlevel;

	swlevel = find_level(&cpu, irq);	/* Criminal offence */
	intr_connect_level(cpu, swlevel);
}

static inline void disable_bridge_irq(unsigned int irq)
{
	cpuid_t cpu;
	int swlevel;

	swlevel = find_level(&cpu, irq);	/* Criminal offence */
	intr_disconnect_level(cpu, swlevel);
}

static struct irq_chip bridge_irq_type = {
	.name		= "bridge",
	.startup	= startup_bridge_irq,
	.shutdown	= shutdown_bridge_irq,
	.ack		= disable_bridge_irq,
	.mask		= disable_bridge_irq,
	.mask_ack	= disable_bridge_irq,
	.unmask		= enable_bridge_irq,
};

void __devinit register_bridge_irq(unsigned int irq)
{
	set_irq_chip_and_handler(irq, &bridge_irq_type, handle_level_irq);
}

int __devinit request_bridge_irq(struct bridge_controller *bc)
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

extern void ip27_rt_timer_interrupt(void);

asmlinkage void plat_irq_dispatch(void)
{
	unsigned long pending = read_c0_cause() & read_c0_status();

	if (pending & CAUSEF_IP4)
		ip27_rt_timer_interrupt();
	else if (pending & CAUSEF_IP2)	/* PI_INT_PEND_0 or CC_PEND_{A|B} */
		ip27_do_irq_mask0();
	else if (pending & CAUSEF_IP3)	/* PI_INT_PEND_1 */
		ip27_do_irq_mask1();
	else if (pending & CAUSEF_IP5)
		ip27_prof_timer();
	else if (pending & CAUSEF_IP6)
		ip27_hub_error();
}

void __init arch_init_irq(void)
{
}

void install_ipi(void)
{
	int slice = LOCAL_HUB_L(PI_CPU_NUM);
	int cpu = smp_processor_id();
	struct slice_data *si = cpu_data[cpu].data;
	struct hub_data *hub = hub_data(cpu_to_node(cpu));
	int resched, call;

	resched = CPU_RESCHED_A_IRQ + slice;
	__set_bit(resched, hub->irq_alloc_mask);
	__set_bit(resched, si->irq_enable_mask);
	LOCAL_HUB_CLR_INTR(resched);

	call = CPU_CALL_A_IRQ + slice;
	__set_bit(call, hub->irq_alloc_mask);
	__set_bit(call, si->irq_enable_mask);
	LOCAL_HUB_CLR_INTR(call);

	if (slice == 0) {
		LOCAL_HUB_S(PI_INT_MASK0_A, si->irq_enable_mask[0]);
		LOCAL_HUB_S(PI_INT_MASK1_A, si->irq_enable_mask[1]);
	} else {
		LOCAL_HUB_S(PI_INT_MASK0_B, si->irq_enable_mask[0]);
		LOCAL_HUB_S(PI_INT_MASK1_B, si->irq_enable_mask[1]);
	}
}
