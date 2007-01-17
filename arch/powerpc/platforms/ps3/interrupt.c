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
#include <linux/module.h>
#include <linux/irq.h>

#include <asm/machdep.h>
#include <asm/udbg.h>
#include <asm/ps3.h>
#include <asm/lv1call.h>

#include "platform.h"

#if defined(DEBUG)
#define DBG(fmt...) udbg_printf(fmt)
#else
#define DBG(fmt...) do{if(0)printk(fmt);}while(0)
#endif

/**
 * ps3_alloc_io_irq - Assign a virq to a system bus device.
 * interrupt_id: The device interrupt id read from the system repository.
 * @virq: The assigned Linux virq.
 *
 * An io irq represents a non-virtualized device interrupt.  interrupt_id
 * coresponds to the interrupt number of the interrupt controller.
 */

int ps3_alloc_io_irq(unsigned int interrupt_id, unsigned int *virq)
{
	int result;
	unsigned long outlet;

	result = lv1_construct_io_irq_outlet(interrupt_id, &outlet);

	if (result) {
		pr_debug("%s:%d: lv1_construct_io_irq_outlet failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		return result;
	}

	*virq = irq_create_mapping(NULL, outlet);

	pr_debug("%s:%d: interrupt_id %u => outlet %lu, virq %u\n",
		__func__, __LINE__, interrupt_id, outlet, *virq);

	return 0;
}

int ps3_free_io_irq(unsigned int virq)
{
	int result;

	result = lv1_destruct_io_irq_outlet(virq_to_hw(virq));

	if (!result)
		pr_debug("%s:%d: lv1_destruct_io_irq_outlet failed: %s\n",
			__func__, __LINE__, ps3_result(result));

	irq_dispose_mapping(virq);

	return result;
}

/**
 * ps3_alloc_event_irq - Allocate a virq for use with a system event.
 * @virq: The assigned Linux virq.
 *
 * The virq can be used with lv1_connect_interrupt_event_receive_port() to
 * arrange to receive events, or with ps3_send_event_locally() to signal
 * events.
 */

int ps3_alloc_event_irq(unsigned int *virq)
{
	int result;
	unsigned long outlet;

	result = lv1_construct_event_receive_port(&outlet);

	if (result) {
		pr_debug("%s:%d: lv1_construct_event_receive_port failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		*virq = NO_IRQ;
		return result;
	}

	*virq = irq_create_mapping(NULL, outlet);

	pr_debug("%s:%d: outlet %lu, virq %u\n", __func__, __LINE__, outlet,
		*virq);

	return 0;
}

int ps3_free_event_irq(unsigned int virq)
{
	int result;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);

	result = lv1_destruct_event_receive_port(virq_to_hw(virq));

	if (result)
		pr_debug("%s:%d: lv1_destruct_event_receive_port failed: %s\n",
			__func__, __LINE__, ps3_result(result));

	irq_dispose_mapping(virq);

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

int ps3_send_event_locally(unsigned int virq)
{
	return lv1_send_event_locally(virq_to_hw(virq));
}

/**
 * ps3_connect_event_irq - Assign a virq to a system bus device.
 * @did: The HV device identifier read from the system repository.
 * @interrupt_id: The device interrupt id read from the system repository.
 * @virq: The assigned Linux virq.
 *
 * An event irq represents a virtual device interrupt.  The interrupt_id
 * coresponds to the software interrupt number.
 */

int ps3_connect_event_irq(const struct ps3_device_id *did,
	unsigned int interrupt_id, unsigned int *virq)
{
	int result;

	result = ps3_alloc_event_irq(virq);

	if (result)
		return result;

	result = lv1_connect_interrupt_event_receive_port(did->bus_id,
		did->dev_id, virq_to_hw(*virq), interrupt_id);

	if (result) {
		pr_debug("%s:%d: lv1_connect_interrupt_event_receive_port"
			" failed: %s\n", __func__, __LINE__,
			ps3_result(result));
		ps3_free_event_irq(*virq);
		*virq = NO_IRQ;
		return result;
	}

	pr_debug("%s:%d: interrupt_id %u, virq %u\n", __func__, __LINE__,
		interrupt_id, *virq);

	return 0;
}

int ps3_disconnect_event_irq(const struct ps3_device_id *did,
	unsigned int interrupt_id, unsigned int virq)
{
	int result;

	pr_debug(" -> %s:%d: interrupt_id %u, virq %u\n", __func__, __LINE__,
		interrupt_id, virq);

	result = lv1_disconnect_interrupt_event_receive_port(did->bus_id,
		did->dev_id, virq_to_hw(virq), interrupt_id);

	if (result)
		pr_debug("%s:%d: lv1_disconnect_interrupt_event_receive_port"
			" failed: %s\n", __func__, __LINE__,
			ps3_result(result));

	ps3_free_event_irq(virq);

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

/**
 * ps3_alloc_vuart_irq - Configure the system virtual uart virq.
 * @virt_addr_bmp: The caller supplied virtual uart interrupt bitmap.
 * @virq: The assigned Linux virq.
 *
 * The system supports only a single virtual uart, so multiple calls without
 * freeing the interrupt will return a wrong state error.
 */

int ps3_alloc_vuart_irq(void* virt_addr_bmp, unsigned int *virq)
{
	int result;
	unsigned long outlet;
	unsigned long lpar_addr;

	BUG_ON(!is_kernel_addr((unsigned long)virt_addr_bmp));

	lpar_addr = ps3_mm_phys_to_lpar(__pa(virt_addr_bmp));

	result = lv1_configure_virtual_uart_irq(lpar_addr, &outlet);

	if (result) {
		pr_debug("%s:%d: lv1_configure_virtual_uart_irq failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		return result;
	}

	*virq = irq_create_mapping(NULL, outlet);

	pr_debug("%s:%d: outlet %lu, virq %u\n", __func__, __LINE__,
		outlet, *virq);

	return 0;
}

int ps3_free_vuart_irq(unsigned int virq)
{
	int result;

	result = lv1_deconfigure_virtual_uart_irq();

	if (result) {
		pr_debug("%s:%d: lv1_configure_virtual_uart_irq failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		return result;
	}

	irq_dispose_mapping(virq);

	return result;
}

/**
 * ps3_alloc_spe_irq - Configure an spe virq.
 * @spe_id: The spe_id returned from lv1_construct_logical_spe().
 * @class: The spe interrupt class {0,1,2}.
 * @virq: The assigned Linux virq.
 *
 */

int ps3_alloc_spe_irq(unsigned long spe_id, unsigned int class,
	unsigned int *virq)
{
	int result;
	unsigned long outlet;

	BUG_ON(class > 2);

	result = lv1_get_spe_irq_outlet(spe_id, class, &outlet);

	if (result) {
		pr_debug("%s:%d: lv1_get_spe_irq_outlet failed: %s\n",
			__func__, __LINE__, ps3_result(result));
		return result;
	}

	*virq = irq_create_mapping(NULL, outlet);

	pr_debug("%s:%d: spe_id %lu, class %u, outlet %lu, virq %u\n",
		__func__, __LINE__, spe_id, class, outlet, *virq);

	return 0;
}

int ps3_free_spe_irq(unsigned int virq)
{
	irq_dispose_mapping(virq);
	return 0;
}

#define PS3_INVALID_OUTLET ((irq_hw_number_t)-1)
#define PS3_PLUG_MAX 63

/**
 * struct bmp - a per cpu irq status and mask bitmap structure
 * @status: 256 bit status bitmap indexed by plug
 * @unused_1:
 * @mask: 256 bit mask bitmap indexed by plug
 * @unused_2:
 * @lock:
 * @ipi_debug_brk_mask:
 *
 * The HV mantains per SMT thread mappings of HV outlet to HV plug on
 * behalf of the guest.  These mappings are implemented as 256 bit guest
 * supplied bitmaps indexed by plug number.  The address of the bitmaps are
 * registered with the HV through lv1_configure_irq_state_bitmap().
 *
 * The HV supports 256 plugs per thread, assigned as {0..255}, for a total
 * of 512 plugs supported on a processor.  To simplify the logic this
 * implementation equates HV plug value to linux virq value, constrains each
 * interrupt to have a system wide unique plug number, and limits the range
 * of the plug values to map into the first dword of the bitmaps.  This
 * gives a usable range of plug values of  {NUM_ISA_INTERRUPTS..63}.  Note
 * that there is no constraint on how many in this set an individual thread
 * can aquire.
 */

struct bmp {
	struct {
		unsigned long status;
		unsigned long unused_1[3];
		unsigned long mask;
		unsigned long unused_2[3];
	} __attribute__ ((packed));
	spinlock_t lock;
	unsigned long ipi_debug_brk_mask;
};

/**
 * struct private - a per cpu data structure
 * @node: HV node id
 * @cpu: HV thread id
 * @bmp: an HV bmp structure
 */

struct private {
	unsigned long node;
	unsigned int cpu;
	struct bmp bmp;
};

#if defined(DEBUG)
static void _dump_64_bmp(const char *header, const unsigned long *p, unsigned cpu,
	const char* func, int line)
{
	pr_debug("%s:%d: %s %u {%04lx_%04lx_%04lx_%04lx}\n",
		func, line, header, cpu,
		*p >> 48, (*p >> 32) & 0xffff, (*p >> 16) & 0xffff,
		*p & 0xffff);
}

static void __attribute__ ((unused)) _dump_256_bmp(const char *header,
	const unsigned long *p, unsigned cpu, const char* func, int line)
{
	pr_debug("%s:%d: %s %u {%016lx:%016lx:%016lx:%016lx}\n",
		func, line, header, cpu, p[0], p[1], p[2], p[3]);
}

#define dump_bmp(_x) _dump_bmp(_x, __func__, __LINE__)
static void _dump_bmp(struct private* pd, const char* func, int line)
{
	unsigned long flags;

	spin_lock_irqsave(&pd->bmp.lock, flags);
	_dump_64_bmp("stat", &pd->bmp.status, pd->cpu, func, line);
	_dump_64_bmp("mask", &pd->bmp.mask, pd->cpu, func, line);
	spin_unlock_irqrestore(&pd->bmp.lock, flags);
}

#define dump_mask(_x) _dump_mask(_x, __func__, __LINE__)
static void __attribute__ ((unused)) _dump_mask(struct private* pd,
	const char* func, int line)
{
	unsigned long flags;

	spin_lock_irqsave(&pd->bmp.lock, flags);
	_dump_64_bmp("mask", &pd->bmp.mask, pd->cpu, func, line);
	spin_unlock_irqrestore(&pd->bmp.lock, flags);
}
#else
static void dump_bmp(struct private* pd) {};
#endif /* defined(DEBUG) */

static void chip_mask(unsigned int virq)
{
	unsigned long flags;
	struct private *pd = get_irq_chip_data(virq);

	pr_debug("%s:%d: cpu %u, virq %d\n", __func__, __LINE__, pd->cpu, virq);

	BUG_ON(virq < NUM_ISA_INTERRUPTS);
	BUG_ON(virq > PS3_PLUG_MAX);

	spin_lock_irqsave(&pd->bmp.lock, flags);
	pd->bmp.mask &= ~(0x8000000000000000UL >> virq);
	spin_unlock_irqrestore(&pd->bmp.lock, flags);

	lv1_did_update_interrupt_mask(pd->node, pd->cpu);
}

static void chip_unmask(unsigned int virq)
{
	unsigned long flags;
	struct private *pd = get_irq_chip_data(virq);

	pr_debug("%s:%d: cpu %u, virq %d\n", __func__, __LINE__, pd->cpu, virq);

	BUG_ON(virq < NUM_ISA_INTERRUPTS);
	BUG_ON(virq > PS3_PLUG_MAX);

	spin_lock_irqsave(&pd->bmp.lock, flags);
	pd->bmp.mask |= (0x8000000000000000UL >> virq);
	spin_unlock_irqrestore(&pd->bmp.lock, flags);

	lv1_did_update_interrupt_mask(pd->node, pd->cpu);
}

static void chip_eoi(unsigned int virq)
{
	lv1_end_of_interrupt(virq);
}

static struct irq_chip irq_chip = {
	.typename = "ps3",
	.mask = chip_mask,
	.unmask = chip_unmask,
	.eoi = chip_eoi,
};

static void host_unmap(struct irq_host *h, unsigned int virq)
{
	int result;

	pr_debug("%s:%d: virq %d\n", __func__, __LINE__, virq);

	lv1_disconnect_irq_plug(virq);

	result = set_irq_chip_data(virq, NULL);
	BUG_ON(result);
}

static DEFINE_PER_CPU(struct private, private);

static int host_map(struct irq_host *h, unsigned int virq,
	irq_hw_number_t hwirq)
{
	int result;
	unsigned int cpu;

	pr_debug(" -> %s:%d\n", __func__, __LINE__);
	pr_debug("%s:%d: hwirq %lu => virq %u\n", __func__, __LINE__, hwirq,
		virq);

	/* bind this virq to a cpu */

	preempt_disable();
	cpu = smp_processor_id();
	result = lv1_connect_irq_plug(virq, hwirq);
	preempt_enable();

	if (result) {
		pr_info("%s:%d: lv1_connect_irq_plug failed:"
			" %s\n", __func__, __LINE__, ps3_result(result));
		return -EPERM;
	}

	result = set_irq_chip_data(virq, &per_cpu(private, cpu));
	BUG_ON(result);

	set_irq_chip_and_handler(virq, &irq_chip, handle_fasteoi_irq);

	pr_debug(" <- %s:%d\n", __func__, __LINE__);
	return result;
}

static struct irq_host_ops host_ops = {
	.map = host_map,
	.unmap = host_unmap,
};

void __init ps3_register_ipi_debug_brk(unsigned int cpu, unsigned int virq)
{
	struct private *pd = &per_cpu(private, cpu);

	pd->bmp.ipi_debug_brk_mask = 0x8000000000000000UL >> virq;

	pr_debug("%s:%d: cpu %u, virq %u, mask %lxh\n", __func__, __LINE__,
		cpu, virq, pd->bmp.ipi_debug_brk_mask);
}

static int bmp_get_and_clear_status_bit(struct bmp *m)
{
	unsigned long flags;
	unsigned int bit;
	unsigned long x;

	spin_lock_irqsave(&m->lock, flags);

	/* check for ipi break first to stop this cpu ASAP */

	if (m->status & m->ipi_debug_brk_mask) {
		m->status &= ~m->ipi_debug_brk_mask;
		spin_unlock_irqrestore(&m->lock, flags);
		return __ilog2(m->ipi_debug_brk_mask);
	}

	x = (m->status & m->mask);

	for (bit = NUM_ISA_INTERRUPTS, x <<= bit; x; bit++, x <<= 1)
		if (x & 0x8000000000000000UL) {
			m->status &= ~(0x8000000000000000UL >> bit);
			spin_unlock_irqrestore(&m->lock, flags);
			return bit;
		}

	spin_unlock_irqrestore(&m->lock, flags);

	pr_debug("%s:%d: not found\n", __func__, __LINE__);
	return -1;
}

unsigned int ps3_get_irq(void)
{
	int plug;

	struct private *pd = &__get_cpu_var(private);

	plug = bmp_get_and_clear_status_bit(&pd->bmp);

	if (plug < 1) {
		pr_debug("%s:%d: no plug found: cpu %u\n", __func__, __LINE__,
			pd->cpu);
		dump_bmp(&per_cpu(private, 0));
		dump_bmp(&per_cpu(private, 1));
		return NO_IRQ;
	}

#if defined(DEBUG)
	if (plug < NUM_ISA_INTERRUPTS || plug > PS3_PLUG_MAX) {
		dump_bmp(&per_cpu(private, 0));
		dump_bmp(&per_cpu(private, 1));
		BUG();
	}
#endif
	return plug;
}

void __init ps3_init_IRQ(void)
{
	int result;
	unsigned long node;
	unsigned cpu;
	struct irq_host *host;

	lv1_get_logical_ppe_id(&node);

	host = irq_alloc_host(IRQ_HOST_MAP_NOMAP, 0, &host_ops,
		PS3_INVALID_OUTLET);
	irq_set_default_host(host);
	irq_set_virq_count(PS3_PLUG_MAX + 1);

	for_each_possible_cpu(cpu) {
		struct private *pd = &per_cpu(private, cpu);

		pd->node = node;
		pd->cpu = cpu;
		spin_lock_init(&pd->bmp.lock);

		result = lv1_configure_irq_state_bitmap(node, cpu,
			ps3_mm_phys_to_lpar(__pa(&pd->bmp.status)));

		if (result)
			pr_debug("%s:%d: lv1_configure_irq_state_bitmap failed:"
				" %s\n", __func__, __LINE__,
				ps3_result(result));
	}

	ppc_md.get_irq = ps3_get_irq;
}
