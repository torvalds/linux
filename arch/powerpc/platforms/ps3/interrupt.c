/*
 *  PS3 interrupt routines.
 *
 *  Copyright (C) 2006 Sony Computer Entertainment Inc.
 *  Copyright 2006 Sony Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
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

#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/irq.h>

#include <asm/machdep.h>
#include <asm/udbg.h>
#include <asm/lv1call.h>
#include <asm/smp.h>

#include "platform.h"

#if defined(DEBUG)
#define DBG udbg_printf
#else
#define DBG pr_debug
#endif

/**
 * struct ps3_bmp - a per cpu irq status and mask bitmap structure
 * @status: 256 bit status bitmap indexed by plug
 * @unused_1:
 * @mask: 256 bit mask bitmap indexed by plug
 * @unused_2:
 * @lock:
 * @ipi_debug_brk_mask:
 *
 * The HV maintains per SMT thread mappings of HV outlet to HV plug on
 * behalf of the guest.  These mappings are implemented as 256 bit guest
 * supplied bitmaps indexed by plug number.  The addresses of the bitmaps
 * are registered with the HV through lv1_configure_irq_state_bitmap().
 * The HV requires that the 512 bits of status + mask not cross a page
 * boundary.  PS3_BMP_MINALIGN is used to define this minimal 64 byte
 * alignment.
 *
 * The HV supports 256 plugs per thread, assigned as {0..255}, for a total
 * of 512 plugs supported on a processor.  To simplify the logic this
 * implementation equates HV plug value to Linux virq value, constrains each
 * interrupt to have a system wide unique plug number, and limits the range
 * of the plug values to map into the first dword of the bitmaps.  This
 * gives a usable range of plug values of  {NUM_ISA_INTERRUPTS..63}.  Note
 * that there is no constraint on how many in this set an individual thread
 * can acquire.
 *
 * The mask is declared as unsigned long so we can use set/clear_bit on it.
 */

#define PS3_BMP_MINALIGN 64

struct ps3_bmp {
	struct {
		u64 status;
		u64 unused_1[3];
		unsigned long mask;
		u64 unused_2[3];
	};
	u64 ipi_debug_brk_mask;
	spinlock_t lock;
};

/**
 * struct ps3_private - a per cpu data structure
 * @bmp: ps3_bmp structure
 * @ppe_id: HV logical_ppe_id
 * @thread_id: HV thread_id
 */

struct ps3_private {
	struct ps3_bmp bmp __attribute__ ((aligned (PS3_BMP_MINALIGN)));
	u64 ppe_id;
	u64 thread_id;
	unsigned long ipi_mask;
};

static DEFINE_PER_CPU(struct ps3_private, ps3_private);

/**
 * ps3_chip_mask - Set an interrupt mask bit in ps3_bmp.
 * @virq: The assigned Linux virq.
 *
 * Sets ps3_bmp.mask and calls lv1_did_update_interrupt_mask().
 */

static void ps3_chip_mask(struct irq_data *d)
{
	struct ps3_private *pd = irq_data_get_irq_chip_data(d);
	unsigned long flags;

	pr_debug("%s:%d: thread_id %llu, virq %d\n", __func__, __LINE__,
		pd->thread_id, d->irq);

	local_irq_save(flags);
	clear_bit(63 - d->irq, &pd->bmp.mask);
	lv1_did_update_interrupt_mask(pd->ppe_id, pd->thread_id);
	local_irq_restore(flags);
}

/**
 * ps3_chip_unmask - Clear an interrupt mask bit in ps3_bmp.
 * @virq: The assigned Linux virq.
 *
 * Clears ps3_bmp.mask and calls lv1_did_update_interrupt_mask().
 */

static void ps3_chip_unmask(struct irq_data *d)
{
	struct ps3_private *pd = irq_data_get_irq_chip_data(d);
	unsigned long flags;

	pr_debug("%s:%d: thread_id %llu, virq %d\n", __func__, __LINE__,
		pd->thread_id, d->irq);

	local_irq_save(flags);
	set_bit(63 - d->irq, &pd->bmp.mask);
	lv1_did_update_interrupt_mask(pd->ppe_id, pd->thread_id);
	local_irq_restore(flags);
}

/**
 * ps3_chip_eoi - HV end-of-interrupt.
 * @virq: The assigned Linux virq.
 *
 * Calls lv1_end_of_interrupt_ext().
 */

static void ps3_chip_eoi(struct irq_data *d)
{
	const struct ps3_private *pd = irq_data_get_irq_chip_data(d);

	/* non-IPIs are EOIed here. */

	if (!test_bit(63 - d->irq, &pd->ipi_mask))
		lv1_end_of_interrupt_ext(pd->ppe_id, pd->thread_id, d->irq);
}

/**
 * ps3_irq_chip - Represents the ps3_bmp as a Linux struct irq_chip.
 */

static struct irq_chip ps3_irq_chip = {
	.name = "ps3",
	.irq_mask = ps3_chip_mask,
	.irq_unmask = ps3_chip_unmask,
	.irq_eoi = ps3_chip_eoi,
};

/**
 * ps3_virq_setup - virq related setup.
 * @cpu: enum ps3_cpu_binding indicating the cpu the interrupt should be
 * serviced on.
 * @outlet: The HV outlet from the various create outlet routines.
 * @virq: The assigned Linux virq.
 *
 * Calls irq_create_mapping() to get a virq and sets the chip data to
 * ps3_private data.
 */

static int ps3_virq_setup(enum ps3_cpu_binding cpu, unsigned long outlet,
			  unsigned int *virq)
{
	int result;
	struct ps3_private *pd;

	/* This defines the default interrupt distribution policy. */

	if (cpu == PS3_BINDING_CPU_ANY)
		cpu = 0;

	pd = &per_cpu(ps3_private, cpu);

	*virq = irq_create_mapping(NULL, outlet);

	if (*virq == NO_IRQ) {
		pr_debug("%s:%d: irq_create_mapping failed: outlet %lu\n",
			__func__, __LINE__, outlet);
		result = -ENOMEM;
		goto fail_create;
	}

	pr_debug("%s:%d: outlet %lu => cpu %u, virq %u\n", __func__, __LINE__,
		outlet, cpu, *virq);

	result = irq_set_chip_data(*virq, pd);

	if (result) {
		pr_debug("%s:%d: irq_set_chip_data failed\n",
			__func__, __LINE__);
		goto fail_set;
	}

	ps3_chip_mask(irq_get_irq_data(*virq));

	return result;

fail_set:
	irq_dispose_mapping(*virq);
fail_create:
	return result;
}

/**
 * ps3_virq_destroy - virq related teardown.
 * @virq: The assigned Linux virq.
 *
 * Clears chip data and calls irq_dispose_mapping() for the virq.
 */

static int ps3_virq_destroy(unsigned int virq)
{
	const struct ps3_private *pd = irq_get_chip_data(virq);

	pr_debug("%s:%d: ppe_id %llu, thread_id %llu, virq %u\n", __func__,
		__LINE__, pd->ppe_id, pd->thread_id, virq);

	irq_set_chip_data(virq, NULL);
	irq_dispose_mapping(virq);

	pr_debug("%s:%d <-\n", __func__, __LINE__);
	return 0;
}

/**
 * ps3_irq_plug_setup - Generic outlet and virq related setup.
 * @cpu: enum ps3_cpu_binding indicating the cpu the interrupt should be
 * serviced on.
 * @outlet: The HV outlet from the various create outlet routines.
 * @virq: The assigned Linux virq.
 *
 * Sets up virq and connects the irq plug.
 */

int ps3_irq_plug_setup(enum ps3_cpu_binding cpu, unsigned long outlet,
	unsigned int *virq)
{
	int result;
	struct ps3_private *pd;

	result = ps3_virq_setup(cpu, outlet, virq);

	if (result) {
		pr_debug("%s:%d: ps3_virq_setup failed\n", __func__, __LINE__);
		goto fail_setup;
	}

	pd = irq_get_chip_data(*virq);

	/* Binds outlet to cpu + virq. */

	result = lv1_connect_irq_plug_ext(pd->ppe_id, pd->thread_id, *virq,
		outlet, 0);

	if (result) {
		pr_info("%s:%d: lv1_connect_irq_plug_ext failed: %s\n",
		__func__, __LINE__, ps3_result(result));
		result = -EPERM;
		goto fail_connect;
	}

	return result;

fail_connect:
	ps3_virq_destroy(*virq);
fail_setup:
	return result;
}
EXPORT_SYMBOL_GPL(ps3_irq_plug_setup);

/**
 * ps3_irq_plug_destroy - Generic outlet and virq related teardown.
 * @virq: The assigned Linux virq.
 *
 * Disconnects the irq plug and tears down virq.
 * Do not call for system bus event interrupts setup with
 * ps3_sb_event_receive_port_setup().
 */

int ps3_irq_plug_destroy(unsigned int virq)
{
	int result;
	const struct ps3_private *pd = irq_get_chip_data(virq);

	pr_debug("%s:%d: ppe_id %llu, thread_id %llu, virq %u\n", __func__,
		__LINE__, pd->ppe_id, pd->thread_id, virq);

	ps3_chip_mask(irq_get_irq_data(virq));

	result = lv1_disconnect_irq_plug_ext(pd->ppe_id, pd->thread_id, virq);

	if (result)
		pr_info("%s:%d: lv1_disconnect_irq_plug_ext failed: %s\n",
		__func__, __LINE__, ps3_result(result));

	ps3_virq_destroy(virq);

	return result;
}
EXPORT_SYMBOL_GPL(ps3_irq_plug_destroy);

/**
 * ps3_event_receive_port_setup - Setup an event receive port.
 * @cpu: enum ps3_cpu_binding indicating the cpu the interrupt should be
 * serviced on.
 * @virq: The assigned Linux virq.
 *
 * The virq can be used with lv1_connect_interrupt_event_receive_port() to
 * arrange to receive interrupts from system-bus devices, or with
 * ps3_send_event_locally() to signal events.
 */

int ps3_event_receive_port_setup(enum ps3_cpu_binding cpu, unsigned int *virq)
{
	int result;
	u64 outlet;

	result = lv1_construct_event_receive_port(&outlet);

	if (result) {
		pr_debug("%s:%d: lv1_construct_event_receive_port failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		*virq = NO_IRQ;
		return result;
	}

	result = ps3_irq_plug_setup(cpu, outlet, virq);
	BUG_ON(result);

	return result;
}
EXPORT_SYMBOL_GPL(ps3_event_receive_port_setup);

/**
 * ps3_event_receive_port_destroy - Destroy an event receive port.
 * @virq: The assigned Linux virq.
 *
 * Since ps3_event_receive_port_destroy destroys the receive port outlet,
 * SB devices need to call disconnect_interrupt_event_receive_port() before
 * this.
 */

int ps3_event_receive_port_destroy(unsigned int virq)
{
	int result;

	pr_debug(" -> %s:%d virq %u\n", __func__, __LINE__, virq);

	ps3_chip_mask(irq_get_irq_data(virq));

	result = lv1_destruct_event_receive_port(virq_to_hw(virq));

	if (result)
		pr_debug("%s:%d: lv1_destruct_event_receive_port failed: %s\n",
			__func__, __LINE__, ps3_result(result));

	/*
	 * Don't call ps3_virq_destroy() here since ps3_smp_cleanup_cpu()
	 * calls from interrupt context (smp_call_function) when kexecing.
	 */

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

int ps3_send_event_locally(unsigned int virq)
{
	return lv1_send_event_locally(virq_to_hw(virq));
}

/**
 * ps3_sb_event_receive_port_setup - Setup a system bus event receive port.
 * @cpu: enum ps3_cpu_binding indicating the cpu the interrupt should be
 * serviced on.
 * @dev: The system bus device instance.
 * @virq: The assigned Linux virq.
 *
 * An event irq represents a virtual device interrupt.  The interrupt_id
 * coresponds to the software interrupt number.
 */

int ps3_sb_event_receive_port_setup(struct ps3_system_bus_device *dev,
	enum ps3_cpu_binding cpu, unsigned int *virq)
{
	/* this should go in system-bus.c */

	int result;

	result = ps3_event_receive_port_setup(cpu, virq);

	if (result)
		return result;

	result = lv1_connect_interrupt_event_receive_port(dev->bus_id,
		dev->dev_id, virq_to_hw(*virq), dev->interrupt_id);

	if (result) {
		pr_debug("%s:%d: lv1_connect_interrupt_event_receive_port"
			" failed: %s\n", __func__, __LINE__,
			ps3_result(result));
		ps3_event_receive_port_destroy(*virq);
		*virq = NO_IRQ;
		return result;
	}

	pr_debug("%s:%d: interrupt_id %u, virq %u\n", __func__, __LINE__,
		dev->interrupt_id, *virq);

	return 0;
}
EXPORT_SYMBOL(ps3_sb_event_receive_port_setup);

int ps3_sb_event_receive_port_destroy(struct ps3_system_bus_device *dev,
	unsigned int virq)
{
	/* this should go in system-bus.c */

	int result;

	pr_debug(" -> %s:%d: interrupt_id %u, virq %u\n", __func__, __LINE__,
		dev->interrupt_id, virq);

	result = lv1_disconnect_interrupt_event_receive_port(dev->bus_id,
		dev->dev_id, virq_to_hw(virq), dev->interrupt_id);

	if (result)
		pr_debug("%s:%d: lv1_disconnect_interrupt_event_receive_port"
			" failed: %s\n", __func__, __LINE__,
			ps3_result(result));

	result = ps3_event_receive_port_destroy(virq);
	BUG_ON(result);

	/*
	 * ps3_event_receive_port_destroy() destroys the IRQ plug,
	 * so don't call ps3_irq_plug_destroy() here.
	 */

	result = ps3_virq_destroy(virq);
	BUG_ON(result);

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;
}
EXPORT_SYMBOL(ps3_sb_event_receive_port_destroy);

/**
 * ps3_io_irq_setup - Setup a system bus io irq.
 * @cpu: enum ps3_cpu_binding indicating the cpu the interrupt should be
 * serviced on.
 * @interrupt_id: The device interrupt id read from the system repository.
 * @virq: The assigned Linux virq.
 *
 * An io irq represents a non-virtualized device interrupt.  interrupt_id
 * coresponds to the interrupt number of the interrupt controller.
 */

int ps3_io_irq_setup(enum ps3_cpu_binding cpu, unsigned int interrupt_id,
	unsigned int *virq)
{
	int result;
	u64 outlet;

	result = lv1_construct_io_irq_outlet(interrupt_id, &outlet);

	if (result) {
		pr_debug("%s:%d: lv1_construct_io_irq_outlet failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		return result;
	}

	result = ps3_irq_plug_setup(cpu, outlet, virq);
	BUG_ON(result);

	return result;
}
EXPORT_SYMBOL_GPL(ps3_io_irq_setup);

int ps3_io_irq_destroy(unsigned int virq)
{
	int result;
	unsigned long outlet = virq_to_hw(virq);

	ps3_chip_mask(irq_get_irq_data(virq));

	/*
	 * lv1_destruct_io_irq_outlet() will destroy the IRQ plug,
	 * so call ps3_irq_plug_destroy() first.
	 */

	result = ps3_irq_plug_destroy(virq);
	BUG_ON(result);

	result = lv1_destruct_io_irq_outlet(outlet);

	if (result)
		pr_debug("%s:%d: lv1_destruct_io_irq_outlet failed: %s\n",
			__func__, __LINE__, ps3_result(result));

	return result;
}
EXPORT_SYMBOL_GPL(ps3_io_irq_destroy);

/**
 * ps3_vuart_irq_setup - Setup the system virtual uart virq.
 * @cpu: enum ps3_cpu_binding indicating the cpu the interrupt should be
 * serviced on.
 * @virt_addr_bmp: The caller supplied virtual uart interrupt bitmap.
 * @virq: The assigned Linux virq.
 *
 * The system supports only a single virtual uart, so multiple calls without
 * freeing the interrupt will return a wrong state error.
 */

int ps3_vuart_irq_setup(enum ps3_cpu_binding cpu, void* virt_addr_bmp,
	unsigned int *virq)
{
	int result;
	u64 outlet;
	u64 lpar_addr;

	BUG_ON(!is_kernel_addr((u64)virt_addr_bmp));

	lpar_addr = ps3_mm_phys_to_lpar(__pa(virt_addr_bmp));

	result = lv1_configure_virtual_uart_irq(lpar_addr, &outlet);

	if (result) {
		pr_debug("%s:%d: lv1_configure_virtual_uart_irq failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		return result;
	}

	result = ps3_irq_plug_setup(cpu, outlet, virq);
	BUG_ON(result);

	return result;
}
EXPORT_SYMBOL_GPL(ps3_vuart_irq_setup);

int ps3_vuart_irq_destroy(unsigned int virq)
{
	int result;

	ps3_chip_mask(irq_get_irq_data(virq));
	result = lv1_deconfigure_virtual_uart_irq();

	if (result) {
		pr_debug("%s:%d: lv1_configure_virtual_uart_irq failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		return result;
	}

	result = ps3_irq_plug_destroy(virq);
	BUG_ON(result);

	return result;
}
EXPORT_SYMBOL_GPL(ps3_vuart_irq_destroy);

/**
 * ps3_spe_irq_setup - Setup an spe virq.
 * @cpu: enum ps3_cpu_binding indicating the cpu the interrupt should be
 * serviced on.
 * @spe_id: The spe_id returned from lv1_construct_logical_spe().
 * @class: The spe interrupt class {0,1,2}.
 * @virq: The assigned Linux virq.
 *
 */

int ps3_spe_irq_setup(enum ps3_cpu_binding cpu, unsigned long spe_id,
	unsigned int class, unsigned int *virq)
{
	int result;
	u64 outlet;

	BUG_ON(class > 2);

	result = lv1_get_spe_irq_outlet(spe_id, class, &outlet);

	if (result) {
		pr_debug("%s:%d: lv1_get_spe_irq_outlet failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		return result;
	}

	result = ps3_irq_plug_setup(cpu, outlet, virq);
	BUG_ON(result);

	return result;
}

int ps3_spe_irq_destroy(unsigned int virq)
{
	int result;

	ps3_chip_mask(irq_get_irq_data(virq));

	result = ps3_irq_plug_destroy(virq);
	BUG_ON(result);

	return result;
}


#define PS3_INVALID_OUTLET ((irq_hw_number_t)-1)
#define PS3_PLUG_MAX 63

#if defined(DEBUG)
static void _dump_64_bmp(const char *header, const u64 *p, unsigned cpu,
	const char* func, int line)
{
	pr_debug("%s:%d: %s %u {%04lx_%04lx_%04lx_%04lx}\n",
		func, line, header, cpu,
		*p >> 48, (*p >> 32) & 0xffff, (*p >> 16) & 0xffff,
		*p & 0xffff);
}

static void __maybe_unused _dump_256_bmp(const char *header,
	const u64 *p, unsigned cpu, const char* func, int line)
{
	pr_debug("%s:%d: %s %u {%016lx:%016lx:%016lx:%016lx}\n",
		func, line, header, cpu, p[0], p[1], p[2], p[3]);
}

#define dump_bmp(_x) _dump_bmp(_x, __func__, __LINE__)
static void _dump_bmp(struct ps3_private* pd, const char* func, int line)
{
	unsigned long flags;

	spin_lock_irqsave(&pd->bmp.lock, flags);
	_dump_64_bmp("stat", &pd->bmp.status, pd->thread_id, func, line);
	_dump_64_bmp("mask", &pd->bmp.mask, pd->thread_id, func, line);
	spin_unlock_irqrestore(&pd->bmp.lock, flags);
}

#define dump_mask(_x) _dump_mask(_x, __func__, __LINE__)
static void __maybe_unused _dump_mask(struct ps3_private *pd,
	const char* func, int line)
{
	unsigned long flags;

	spin_lock_irqsave(&pd->bmp.lock, flags);
	_dump_64_bmp("mask", &pd->bmp.mask, pd->thread_id, func, line);
	spin_unlock_irqrestore(&pd->bmp.lock, flags);
}
#else
static void dump_bmp(struct ps3_private* pd) {};
#endif /* defined(DEBUG) */

static int ps3_host_map(struct irq_host *h, unsigned int virq,
	irq_hw_number_t hwirq)
{
	pr_debug("%s:%d: hwirq %lu, virq %u\n", __func__, __LINE__, hwirq,
		virq);

	irq_set_chip_and_handler(virq, &ps3_irq_chip, handle_fasteoi_irq);

	return 0;
}

static int ps3_host_match(struct irq_host *h, struct device_node *np)
{
	/* Match all */
	return 1;
}

static struct irq_host_ops ps3_host_ops = {
	.map = ps3_host_map,
	.match = ps3_host_match,
};

void __init ps3_register_ipi_debug_brk(unsigned int cpu, unsigned int virq)
{
	struct ps3_private *pd = &per_cpu(ps3_private, cpu);

	pd->bmp.ipi_debug_brk_mask = 0x8000000000000000UL >> virq;

	pr_debug("%s:%d: cpu %u, virq %u, mask %llxh\n", __func__, __LINE__,
		cpu, virq, pd->bmp.ipi_debug_brk_mask);
}

void __init ps3_register_ipi_irq(unsigned int cpu, unsigned int virq)
{
	struct ps3_private *pd = &per_cpu(ps3_private, cpu);

	set_bit(63 - virq, &pd->ipi_mask);

	DBG("%s:%d: cpu %u, virq %u, ipi_mask %lxh\n", __func__, __LINE__,
		cpu, virq, pd->ipi_mask);
}

static unsigned int ps3_get_irq(void)
{
	struct ps3_private *pd = &__get_cpu_var(ps3_private);
	u64 x = (pd->bmp.status & pd->bmp.mask);
	unsigned int plug;

	/* check for ipi break first to stop this cpu ASAP */

	if (x & pd->bmp.ipi_debug_brk_mask)
		x &= pd->bmp.ipi_debug_brk_mask;

	asm volatile("cntlzd %0,%1" : "=r" (plug) : "r" (x));
	plug &= 0x3f;

	if (unlikely(plug == NO_IRQ)) {
		pr_debug("%s:%d: no plug found: thread_id %llu\n", __func__,
			__LINE__, pd->thread_id);
		dump_bmp(&per_cpu(ps3_private, 0));
		dump_bmp(&per_cpu(ps3_private, 1));
		return NO_IRQ;
	}

#if defined(DEBUG)
	if (unlikely(plug < NUM_ISA_INTERRUPTS || plug > PS3_PLUG_MAX)) {
		dump_bmp(&per_cpu(ps3_private, 0));
		dump_bmp(&per_cpu(ps3_private, 1));
		BUG();
	}
#endif

	/* IPIs are EOIed here. */

	if (test_bit(63 - plug, &pd->ipi_mask))
		lv1_end_of_interrupt_ext(pd->ppe_id, pd->thread_id, plug);

	return plug;
}

void __init ps3_init_IRQ(void)
{
	int result;
	unsigned cpu;
	struct irq_host *host;

	host = irq_alloc_host(NULL, IRQ_HOST_MAP_NOMAP, 0, &ps3_host_ops,
		PS3_INVALID_OUTLET);
	irq_set_default_host(host);
	irq_set_virq_count(PS3_PLUG_MAX + 1);

	for_each_possible_cpu(cpu) {
		struct ps3_private *pd = &per_cpu(ps3_private, cpu);

		lv1_get_logical_ppe_id(&pd->ppe_id);
		pd->thread_id = get_hard_smp_processor_id(cpu);
		spin_lock_init(&pd->bmp.lock);

		pr_debug("%s:%d: ppe_id %llu, thread_id %llu, bmp %lxh\n",
			__func__, __LINE__, pd->ppe_id, pd->thread_id,
			ps3_mm_phys_to_lpar(__pa(&pd->bmp)));

		result = lv1_configure_irq_state_bitmap(pd->ppe_id,
			pd->thread_id, ps3_mm_phys_to_lpar(__pa(&pd->bmp)));

		if (result)
			pr_debug("%s:%d: lv1_configure_irq_state_bitmap failed:"
				" %s\n", __func__, __LINE__,
				ps3_result(result));
	}

	ppc_md.get_irq = ps3_get_irq;
}

void ps3_shutdown_IRQ(int cpu)
{
	int result;
	u64 ppe_id;
	u64 thread_id = get_hard_smp_processor_id(cpu);

	lv1_get_logical_ppe_id(&ppe_id);
	result = lv1_configure_irq_state_bitmap(ppe_id, thread_id, 0);

	DBG("%s:%d: lv1_configure_irq_state_bitmap (%llu:%llu/%d) %s\n", __func__,
		__LINE__, ppe_id, thread_id, cpu, ps3_result(result));
}
