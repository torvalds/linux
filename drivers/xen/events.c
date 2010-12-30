/*
 * Xen event channels
 *
 * Xen models interrupts with abstract event channels.  Because each
 * domain gets 1024 event channels, but NR_IRQ is not that large, we
 * must dynamically map irqs<->event channels.  The event channels
 * interface with the rest of the kernel by defining a xen interrupt
 * chip.  When an event is recieved, it is mapped to an irq and sent
 * through the normal interrupt processing path.
 *
 * There are four kinds of events which can be mapped to an event
 * channel:
 *
 * 1. Inter-domain notifications.  This includes all the virtual
 *    device events, since they're driven by front-ends in another domain
 *    (typically dom0).
 * 2. VIRQs, typically used for timers.  These are per-cpu events.
 * 3. IPIs.
 * 4. PIRQs - Hardware interrupts.
 *
 * Jeremy Fitzhardinge <jeremy@xensource.com>, XenSource Inc, 2007
 */

#include <linux/linkage.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/bootmem.h>
#include <linux/slab.h>
#include <linux/irqnr.h>
#include <linux/pci.h>

#include <asm/desc.h>
#include <asm/ptrace.h>
#include <asm/irq.h>
#include <asm/idle.h>
#include <asm/io_apic.h>
#include <asm/sync_bitops.h>
#include <asm/xen/pci.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/hypervisor.h>

#include <xen/xen.h>
#include <xen/hvm.h>
#include <xen/xen-ops.h>
#include <xen/events.h>
#include <xen/interface/xen.h>
#include <xen/interface/event_channel.h>
#include <xen/interface/hvm/hvm_op.h>
#include <xen/interface/hvm/params.h>

/*
 * This lock protects updates to the following mapping and reference-count
 * arrays. The lock does not need to be acquired to read the mapping tables.
 */
static DEFINE_SPINLOCK(irq_mapping_update_lock);

/* IRQ <-> VIRQ mapping. */
static DEFINE_PER_CPU(int [NR_VIRQS], virq_to_irq) = {[0 ... NR_VIRQS-1] = -1};

/* IRQ <-> IPI mapping */
static DEFINE_PER_CPU(int [XEN_NR_IPIS], ipi_to_irq) = {[0 ... XEN_NR_IPIS-1] = -1};

/* Interrupt types. */
enum xen_irq_type {
	IRQT_UNBOUND = 0,
	IRQT_PIRQ,
	IRQT_VIRQ,
	IRQT_IPI,
	IRQT_EVTCHN
};

/*
 * Packed IRQ information:
 * type - enum xen_irq_type
 * event channel - irq->event channel mapping
 * cpu - cpu this event channel is bound to
 * index - type-specific information:
 *    PIRQ - vector, with MSB being "needs EIO", or physical IRQ of the HVM
 *           guest, or GSI (real passthrough IRQ) of the device.
 *    VIRQ - virq number
 *    IPI - IPI vector
 *    EVTCHN -
 */
struct irq_info
{
	enum xen_irq_type type;	/* type */
	unsigned short evtchn;	/* event channel */
	unsigned short cpu;	/* cpu bound */

	union {
		unsigned short virq;
		enum ipi_vector ipi;
		struct {
			unsigned short pirq;
			unsigned short gsi;
			unsigned char vector;
			unsigned char flags;
		} pirq;
	} u;
};
#define PIRQ_NEEDS_EOI	(1 << 0)
#define PIRQ_SHAREABLE	(1 << 1)

static struct irq_info *irq_info;
static int *pirq_to_irq;

static int *evtchn_to_irq;
struct cpu_evtchn_s {
	unsigned long bits[NR_EVENT_CHANNELS/BITS_PER_LONG];
};

static __initdata struct cpu_evtchn_s init_evtchn_mask = {
	.bits[0 ... (NR_EVENT_CHANNELS/BITS_PER_LONG)-1] = ~0ul,
};
static struct cpu_evtchn_s *cpu_evtchn_mask_p = &init_evtchn_mask;

static inline unsigned long *cpu_evtchn_mask(int cpu)
{
	return cpu_evtchn_mask_p[cpu].bits;
}

/* Xen will never allocate port zero for any purpose. */
#define VALID_EVTCHN(chn)	((chn) != 0)

static struct irq_chip xen_dynamic_chip;
static struct irq_chip xen_percpu_chip;
static struct irq_chip xen_pirq_chip;

/* Constructor for packed IRQ information. */
static struct irq_info mk_unbound_info(void)
{
	return (struct irq_info) { .type = IRQT_UNBOUND };
}

static struct irq_info mk_evtchn_info(unsigned short evtchn)
{
	return (struct irq_info) { .type = IRQT_EVTCHN, .evtchn = evtchn,
			.cpu = 0 };
}

static struct irq_info mk_ipi_info(unsigned short evtchn, enum ipi_vector ipi)
{
	return (struct irq_info) { .type = IRQT_IPI, .evtchn = evtchn,
			.cpu = 0, .u.ipi = ipi };
}

static struct irq_info mk_virq_info(unsigned short evtchn, unsigned short virq)
{
	return (struct irq_info) { .type = IRQT_VIRQ, .evtchn = evtchn,
			.cpu = 0, .u.virq = virq };
}

static struct irq_info mk_pirq_info(unsigned short evtchn, unsigned short pirq,
				    unsigned short gsi, unsigned short vector)
{
	return (struct irq_info) { .type = IRQT_PIRQ, .evtchn = evtchn,
			.cpu = 0,
			.u.pirq = { .pirq = pirq, .gsi = gsi, .vector = vector } };
}

/*
 * Accessors for packed IRQ information.
 */
static struct irq_info *info_for_irq(unsigned irq)
{
	return &irq_info[irq];
}

static unsigned int evtchn_from_irq(unsigned irq)
{
	return info_for_irq(irq)->evtchn;
}

unsigned irq_from_evtchn(unsigned int evtchn)
{
	return evtchn_to_irq[evtchn];
}
EXPORT_SYMBOL_GPL(irq_from_evtchn);

static enum ipi_vector ipi_from_irq(unsigned irq)
{
	struct irq_info *info = info_for_irq(irq);

	BUG_ON(info == NULL);
	BUG_ON(info->type != IRQT_IPI);

	return info->u.ipi;
}

static unsigned virq_from_irq(unsigned irq)
{
	struct irq_info *info = info_for_irq(irq);

	BUG_ON(info == NULL);
	BUG_ON(info->type != IRQT_VIRQ);

	return info->u.virq;
}

static unsigned pirq_from_irq(unsigned irq)
{
	struct irq_info *info = info_for_irq(irq);

	BUG_ON(info == NULL);
	BUG_ON(info->type != IRQT_PIRQ);

	return info->u.pirq.pirq;
}

static unsigned gsi_from_irq(unsigned irq)
{
	struct irq_info *info = info_for_irq(irq);

	BUG_ON(info == NULL);
	BUG_ON(info->type != IRQT_PIRQ);

	return info->u.pirq.gsi;
}

static unsigned vector_from_irq(unsigned irq)
{
	struct irq_info *info = info_for_irq(irq);

	BUG_ON(info == NULL);
	BUG_ON(info->type != IRQT_PIRQ);

	return info->u.pirq.vector;
}

static enum xen_irq_type type_from_irq(unsigned irq)
{
	return info_for_irq(irq)->type;
}

static unsigned cpu_from_irq(unsigned irq)
{
	return info_for_irq(irq)->cpu;
}

static unsigned int cpu_from_evtchn(unsigned int evtchn)
{
	int irq = evtchn_to_irq[evtchn];
	unsigned ret = 0;

	if (irq != -1)
		ret = cpu_from_irq(irq);

	return ret;
}

static bool pirq_needs_eoi(unsigned irq)
{
	struct irq_info *info = info_for_irq(irq);

	BUG_ON(info->type != IRQT_PIRQ);

	return info->u.pirq.flags & PIRQ_NEEDS_EOI;
}

static inline unsigned long active_evtchns(unsigned int cpu,
					   struct shared_info *sh,
					   unsigned int idx)
{
	return (sh->evtchn_pending[idx] &
		cpu_evtchn_mask(cpu)[idx] &
		~sh->evtchn_mask[idx]);
}

static void bind_evtchn_to_cpu(unsigned int chn, unsigned int cpu)
{
	int irq = evtchn_to_irq[chn];

	BUG_ON(irq == -1);
#ifdef CONFIG_SMP
	cpumask_copy(irq_to_desc(irq)->affinity, cpumask_of(cpu));
#endif

	clear_bit(chn, cpu_evtchn_mask(cpu_from_irq(irq)));
	set_bit(chn, cpu_evtchn_mask(cpu));

	irq_info[irq].cpu = cpu;
}

static void init_evtchn_cpu_bindings(void)
{
	int i;
#ifdef CONFIG_SMP
	struct irq_desc *desc;

	/* By default all event channels notify CPU#0. */
	for_each_irq_desc(i, desc) {
		cpumask_copy(desc->affinity, cpumask_of(0));
	}
#endif

	for_each_possible_cpu(i)
		memset(cpu_evtchn_mask(i),
		       (i == 0) ? ~0 : 0, sizeof(struct cpu_evtchn_s));

}

static inline void clear_evtchn(int port)
{
	struct shared_info *s = HYPERVISOR_shared_info;
	sync_clear_bit(port, &s->evtchn_pending[0]);
}

static inline void set_evtchn(int port)
{
	struct shared_info *s = HYPERVISOR_shared_info;
	sync_set_bit(port, &s->evtchn_pending[0]);
}

static inline int test_evtchn(int port)
{
	struct shared_info *s = HYPERVISOR_shared_info;
	return sync_test_bit(port, &s->evtchn_pending[0]);
}


/**
 * notify_remote_via_irq - send event to remote end of event channel via irq
 * @irq: irq of event channel to send event to
 *
 * Unlike notify_remote_via_evtchn(), this is safe to use across
 * save/restore. Notifications on a broken connection are silently
 * dropped.
 */
void notify_remote_via_irq(int irq)
{
	int evtchn = evtchn_from_irq(irq);

	if (VALID_EVTCHN(evtchn))
		notify_remote_via_evtchn(evtchn);
}
EXPORT_SYMBOL_GPL(notify_remote_via_irq);

static void mask_evtchn(int port)
{
	struct shared_info *s = HYPERVISOR_shared_info;
	sync_set_bit(port, &s->evtchn_mask[0]);
}

static void unmask_evtchn(int port)
{
	struct shared_info *s = HYPERVISOR_shared_info;
	unsigned int cpu = get_cpu();

	BUG_ON(!irqs_disabled());

	/* Slow path (hypercall) if this is a non-local port. */
	if (unlikely(cpu != cpu_from_evtchn(port))) {
		struct evtchn_unmask unmask = { .port = port };
		(void)HYPERVISOR_event_channel_op(EVTCHNOP_unmask, &unmask);
	} else {
		struct vcpu_info *vcpu_info = __get_cpu_var(xen_vcpu);

		sync_clear_bit(port, &s->evtchn_mask[0]);

		/*
		 * The following is basically the equivalent of
		 * 'hw_resend_irq'. Just like a real IO-APIC we 'lose
		 * the interrupt edge' if the channel is masked.
		 */
		if (sync_test_bit(port, &s->evtchn_pending[0]) &&
		    !sync_test_and_set_bit(port / BITS_PER_LONG,
					   &vcpu_info->evtchn_pending_sel))
			vcpu_info->evtchn_upcall_pending = 1;
	}

	put_cpu();
}

static int get_nr_hw_irqs(void)
{
	int ret = 1;

#ifdef CONFIG_X86_IO_APIC
	ret = get_nr_irqs_gsi();
#endif

	return ret;
}

static int find_unbound_pirq(int type)
{
	int rc, i;
	struct physdev_get_free_pirq op_get_free_pirq;
	op_get_free_pirq.type = type;

	rc = HYPERVISOR_physdev_op(PHYSDEVOP_get_free_pirq, &op_get_free_pirq);
	if (!rc)
		return op_get_free_pirq.pirq;

	for (i = 0; i < nr_irqs; i++) {
		if (pirq_to_irq[i] < 0)
			return i;
	}
	return -1;
}

static int find_unbound_irq(void)
{
	struct irq_data *data;
	int irq, res;
	int start = get_nr_hw_irqs();

	if (start == nr_irqs)
		goto no_irqs;

	/* nr_irqs is a magic value. Must not use it.*/
	for (irq = nr_irqs-1; irq > start; irq--) {
		data = irq_get_irq_data(irq);
		/* only 0->15 have init'd desc; handle irq > 16 */
		if (!data)
			break;
		if (data->chip == &no_irq_chip)
			break;
		if (data->chip != &xen_dynamic_chip)
			continue;
		if (irq_info[irq].type == IRQT_UNBOUND)
			return irq;
	}

	if (irq == start)
		goto no_irqs;

	res = irq_alloc_desc_at(irq, -1);

	if (WARN_ON(res != irq))
		return -1;

	return irq;

no_irqs:
	panic("No available IRQ to bind to: increase nr_irqs!\n");
}

static bool identity_mapped_irq(unsigned irq)
{
	/* identity map all the hardware irqs */
	return irq < get_nr_hw_irqs();
}

static void pirq_unmask_notify(int irq)
{
	struct physdev_eoi eoi = { .irq = pirq_from_irq(irq) };

	if (unlikely(pirq_needs_eoi(irq))) {
		int rc = HYPERVISOR_physdev_op(PHYSDEVOP_eoi, &eoi);
		WARN_ON(rc);
	}
}

static void pirq_query_unmask(int irq)
{
	struct physdev_irq_status_query irq_status;
	struct irq_info *info = info_for_irq(irq);

	BUG_ON(info->type != IRQT_PIRQ);

	irq_status.irq = pirq_from_irq(irq);
	if (HYPERVISOR_physdev_op(PHYSDEVOP_irq_status_query, &irq_status))
		irq_status.flags = 0;

	info->u.pirq.flags &= ~PIRQ_NEEDS_EOI;
	if (irq_status.flags & XENIRQSTAT_needs_eoi)
		info->u.pirq.flags |= PIRQ_NEEDS_EOI;
}

static bool probing_irq(int irq)
{
	struct irq_desc *desc = irq_to_desc(irq);

	return desc && desc->action == NULL;
}

static unsigned int startup_pirq(unsigned int irq)
{
	struct evtchn_bind_pirq bind_pirq;
	struct irq_info *info = info_for_irq(irq);
	int evtchn = evtchn_from_irq(irq);
	int rc;

	BUG_ON(info->type != IRQT_PIRQ);

	if (VALID_EVTCHN(evtchn))
		goto out;

	bind_pirq.pirq = pirq_from_irq(irq);
	/* NB. We are happy to share unless we are probing. */
	bind_pirq.flags = info->u.pirq.flags & PIRQ_SHAREABLE ?
					BIND_PIRQ__WILL_SHARE : 0;
	rc = HYPERVISOR_event_channel_op(EVTCHNOP_bind_pirq, &bind_pirq);
	if (rc != 0) {
		if (!probing_irq(irq))
			printk(KERN_INFO "Failed to obtain physical IRQ %d\n",
			       irq);
		return 0;
	}
	evtchn = bind_pirq.port;

	pirq_query_unmask(irq);

	evtchn_to_irq[evtchn] = irq;
	bind_evtchn_to_cpu(evtchn, 0);
	info->evtchn = evtchn;

out:
	unmask_evtchn(evtchn);
	pirq_unmask_notify(irq);

	return 0;
}

static void shutdown_pirq(unsigned int irq)
{
	struct evtchn_close close;
	struct irq_info *info = info_for_irq(irq);
	int evtchn = evtchn_from_irq(irq);

	BUG_ON(info->type != IRQT_PIRQ);

	if (!VALID_EVTCHN(evtchn))
		return;

	mask_evtchn(evtchn);

	close.port = evtchn;
	if (HYPERVISOR_event_channel_op(EVTCHNOP_close, &close) != 0)
		BUG();

	bind_evtchn_to_cpu(evtchn, 0);
	evtchn_to_irq[evtchn] = -1;
	info->evtchn = 0;
}

static void enable_pirq(unsigned int irq)
{
	startup_pirq(irq);
}

static void disable_pirq(unsigned int irq)
{
}

static void ack_pirq(unsigned int irq)
{
	int evtchn = evtchn_from_irq(irq);

	move_native_irq(irq);

	if (VALID_EVTCHN(evtchn)) {
		mask_evtchn(evtchn);
		clear_evtchn(evtchn);
	}
}

static void end_pirq(unsigned int irq)
{
	int evtchn = evtchn_from_irq(irq);
	struct irq_desc *desc = irq_to_desc(irq);

	if (WARN_ON(!desc))
		return;

	if ((desc->status & (IRQ_DISABLED|IRQ_PENDING)) ==
	    (IRQ_DISABLED|IRQ_PENDING)) {
		shutdown_pirq(irq);
	} else if (VALID_EVTCHN(evtchn)) {
		unmask_evtchn(evtchn);
		pirq_unmask_notify(irq);
	}
}

static int find_irq_by_gsi(unsigned gsi)
{
	int irq;

	for (irq = 0; irq < nr_irqs; irq++) {
		struct irq_info *info = info_for_irq(irq);

		if (info == NULL || info->type != IRQT_PIRQ)
			continue;

		if (gsi_from_irq(irq) == gsi)
			return irq;
	}

	return -1;
}

int xen_allocate_pirq(unsigned gsi, int shareable, char *name)
{
	return xen_map_pirq_gsi(gsi, gsi, shareable, name);
}

/* xen_map_pirq_gsi might allocate irqs from the top down, as a
 * consequence don't assume that the irq number returned has a low value
 * or can be used as a pirq number unless you know otherwise.
 *
 * One notable exception is when xen_map_pirq_gsi is called passing an
 * hardware gsi as argument, in that case the irq number returned
 * matches the gsi number passed as second argument.
 *
 * Note: We don't assign an event channel until the irq actually started
 * up.  Return an existing irq if we've already got one for the gsi.
 */
int xen_map_pirq_gsi(unsigned pirq, unsigned gsi, int shareable, char *name)
{
	int irq = 0;
	struct physdev_irq irq_op;

	spin_lock(&irq_mapping_update_lock);

	if ((pirq > nr_irqs) || (gsi > nr_irqs)) {
		printk(KERN_WARNING "xen_map_pirq_gsi: %s %s is incorrect!\n",
			pirq > nr_irqs ? "pirq" :"",
			gsi > nr_irqs ? "gsi" : "");
		goto out;
	}

	irq = find_irq_by_gsi(gsi);
	if (irq != -1) {
		printk(KERN_INFO "xen_map_pirq_gsi: returning irq %d for gsi %u\n",
		       irq, gsi);
		goto out;	/* XXX need refcount? */
	}

	/* If we are a PV guest, we don't have GSIs (no ACPI passed). Therefore
	 * we are using the !xen_initial_domain() to drop in the function.*/
	if (identity_mapped_irq(gsi) || (!xen_initial_domain() &&
				xen_pv_domain())) {
		irq = gsi;
		irq_alloc_desc_at(irq, -1);
	} else
		irq = find_unbound_irq();

	set_irq_chip_and_handler_name(irq, &xen_pirq_chip,
				      handle_level_irq, name);

	irq_op.irq = irq;
	irq_op.vector = 0;

	/* Only the privileged domain can do this. For non-priv, the pcifront
	 * driver provides a PCI bus that does the call to do exactly
	 * this in the priv domain. */
	if (xen_initial_domain() &&
	    HYPERVISOR_physdev_op(PHYSDEVOP_alloc_irq_vector, &irq_op)) {
		irq_free_desc(irq);
		irq = -ENOSPC;
		goto out;
	}

	irq_info[irq] = mk_pirq_info(0, pirq, gsi, irq_op.vector);
	irq_info[irq].u.pirq.flags |= shareable ? PIRQ_SHAREABLE : 0;
	pirq_to_irq[pirq] = irq;

out:
	spin_unlock(&irq_mapping_update_lock);

	return irq;
}

#ifdef CONFIG_PCI_MSI
#include <linux/msi.h>
#include "../pci/msi.h"

void xen_allocate_pirq_msi(char *name, int *irq, int *pirq, int alloc)
{
	spin_lock(&irq_mapping_update_lock);

	if (alloc & XEN_ALLOC_IRQ) {
		*irq = find_unbound_irq();
		if (*irq == -1)
			goto out;
	}

	if (alloc & XEN_ALLOC_PIRQ) {
		*pirq = find_unbound_pirq(MAP_PIRQ_TYPE_MSI);
		if (*pirq == -1)
			goto out;
	}

	set_irq_chip_and_handler_name(*irq, &xen_pirq_chip,
				      handle_level_irq, name);

	irq_info[*irq] = mk_pirq_info(0, *pirq, 0, 0);
	pirq_to_irq[*pirq] = *irq;

out:
	spin_unlock(&irq_mapping_update_lock);
}

int xen_create_msi_irq(struct pci_dev *dev, struct msi_desc *msidesc, int type)
{
	int irq = -1;
	struct physdev_map_pirq map_irq;
	int rc;
	int pos;
	u32 table_offset, bir;

	memset(&map_irq, 0, sizeof(map_irq));
	map_irq.domid = DOMID_SELF;
	map_irq.type = MAP_PIRQ_TYPE_MSI;
	map_irq.index = -1;
	map_irq.pirq = -1;
	map_irq.bus = dev->bus->number;
	map_irq.devfn = dev->devfn;

	if (type == PCI_CAP_ID_MSIX) {
		pos = pci_find_capability(dev, PCI_CAP_ID_MSIX);

		pci_read_config_dword(dev, msix_table_offset_reg(pos),
					&table_offset);
		bir = (u8)(table_offset & PCI_MSIX_FLAGS_BIRMASK);

		map_irq.table_base = pci_resource_start(dev, bir);
		map_irq.entry_nr = msidesc->msi_attrib.entry_nr;
	}

	spin_lock(&irq_mapping_update_lock);

	irq = find_unbound_irq();

	if (irq == -1)
		goto out;

	rc = HYPERVISOR_physdev_op(PHYSDEVOP_map_pirq, &map_irq);
	if (rc) {
		printk(KERN_WARNING "xen map irq failed %d\n", rc);

		irq_free_desc(irq);

		irq = -1;
		goto out;
	}
	irq_info[irq] = mk_pirq_info(0, map_irq.pirq, 0, map_irq.index);

	set_irq_chip_and_handler_name(irq, &xen_pirq_chip,
			handle_level_irq,
			(type == PCI_CAP_ID_MSIX) ? "msi-x":"msi");

out:
	spin_unlock(&irq_mapping_update_lock);
	return irq;
}
#endif

int xen_destroy_irq(int irq)
{
	struct irq_desc *desc;
	struct physdev_unmap_pirq unmap_irq;
	struct irq_info *info = info_for_irq(irq);
	int rc = -ENOENT;

	spin_lock(&irq_mapping_update_lock);

	desc = irq_to_desc(irq);
	if (!desc)
		goto out;

	if (xen_initial_domain()) {
		unmap_irq.pirq = info->u.pirq.pirq;
		unmap_irq.domid = DOMID_SELF;
		rc = HYPERVISOR_physdev_op(PHYSDEVOP_unmap_pirq, &unmap_irq);
		if (rc) {
			printk(KERN_WARNING "unmap irq failed %d\n", rc);
			goto out;
		}
		pirq_to_irq[info->u.pirq.pirq] = -1;
	}
	irq_info[irq] = mk_unbound_info();

	irq_free_desc(irq);

out:
	spin_unlock(&irq_mapping_update_lock);
	return rc;
}

int xen_vector_from_irq(unsigned irq)
{
	return vector_from_irq(irq);
}

int xen_gsi_from_irq(unsigned irq)
{
	return gsi_from_irq(irq);
}

int xen_irq_from_pirq(unsigned pirq)
{
	return pirq_to_irq[pirq];
}

int bind_evtchn_to_irq(unsigned int evtchn)
{
	int irq;

	spin_lock(&irq_mapping_update_lock);

	irq = evtchn_to_irq[evtchn];

	if (irq == -1) {
		irq = find_unbound_irq();

		set_irq_chip_and_handler_name(irq, &xen_dynamic_chip,
					      handle_fasteoi_irq, "event");

		evtchn_to_irq[evtchn] = irq;
		irq_info[irq] = mk_evtchn_info(evtchn);
	}

	spin_unlock(&irq_mapping_update_lock);

	return irq;
}
EXPORT_SYMBOL_GPL(bind_evtchn_to_irq);

static int bind_ipi_to_irq(unsigned int ipi, unsigned int cpu)
{
	struct evtchn_bind_ipi bind_ipi;
	int evtchn, irq;

	spin_lock(&irq_mapping_update_lock);

	irq = per_cpu(ipi_to_irq, cpu)[ipi];

	if (irq == -1) {
		irq = find_unbound_irq();
		if (irq < 0)
			goto out;

		set_irq_chip_and_handler_name(irq, &xen_percpu_chip,
					      handle_percpu_irq, "ipi");

		bind_ipi.vcpu = cpu;
		if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_ipi,
						&bind_ipi) != 0)
			BUG();
		evtchn = bind_ipi.port;

		evtchn_to_irq[evtchn] = irq;
		irq_info[irq] = mk_ipi_info(evtchn, ipi);
		per_cpu(ipi_to_irq, cpu)[ipi] = irq;

		bind_evtchn_to_cpu(evtchn, cpu);
	}

 out:
	spin_unlock(&irq_mapping_update_lock);
	return irq;
}


int bind_virq_to_irq(unsigned int virq, unsigned int cpu)
{
	struct evtchn_bind_virq bind_virq;
	int evtchn, irq;

	spin_lock(&irq_mapping_update_lock);

	irq = per_cpu(virq_to_irq, cpu)[virq];

	if (irq == -1) {
		irq = find_unbound_irq();

		set_irq_chip_and_handler_name(irq, &xen_percpu_chip,
					      handle_percpu_irq, "virq");

		bind_virq.virq = virq;
		bind_virq.vcpu = cpu;
		if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_virq,
						&bind_virq) != 0)
			BUG();
		evtchn = bind_virq.port;

		evtchn_to_irq[evtchn] = irq;
		irq_info[irq] = mk_virq_info(evtchn, virq);

		per_cpu(virq_to_irq, cpu)[virq] = irq;

		bind_evtchn_to_cpu(evtchn, cpu);
	}

	spin_unlock(&irq_mapping_update_lock);

	return irq;
}

static void unbind_from_irq(unsigned int irq)
{
	struct evtchn_close close;
	int evtchn = evtchn_from_irq(irq);

	spin_lock(&irq_mapping_update_lock);

	if (VALID_EVTCHN(evtchn)) {
		close.port = evtchn;
		if (HYPERVISOR_event_channel_op(EVTCHNOP_close, &close) != 0)
			BUG();

		switch (type_from_irq(irq)) {
		case IRQT_VIRQ:
			per_cpu(virq_to_irq, cpu_from_evtchn(evtchn))
				[virq_from_irq(irq)] = -1;
			break;
		case IRQT_IPI:
			per_cpu(ipi_to_irq, cpu_from_evtchn(evtchn))
				[ipi_from_irq(irq)] = -1;
			break;
		default:
			break;
		}

		/* Closed ports are implicitly re-bound to VCPU0. */
		bind_evtchn_to_cpu(evtchn, 0);

		evtchn_to_irq[evtchn] = -1;
	}

	if (irq_info[irq].type != IRQT_UNBOUND) {
		irq_info[irq] = mk_unbound_info();

		irq_free_desc(irq);
	}

	spin_unlock(&irq_mapping_update_lock);
}

int bind_evtchn_to_irqhandler(unsigned int evtchn,
			      irq_handler_t handler,
			      unsigned long irqflags,
			      const char *devname, void *dev_id)
{
	unsigned int irq;
	int retval;

	irq = bind_evtchn_to_irq(evtchn);
	retval = request_irq(irq, handler, irqflags, devname, dev_id);
	if (retval != 0) {
		unbind_from_irq(irq);
		return retval;
	}

	return irq;
}
EXPORT_SYMBOL_GPL(bind_evtchn_to_irqhandler);

int bind_virq_to_irqhandler(unsigned int virq, unsigned int cpu,
			    irq_handler_t handler,
			    unsigned long irqflags, const char *devname, void *dev_id)
{
	unsigned int irq;
	int retval;

	irq = bind_virq_to_irq(virq, cpu);
	retval = request_irq(irq, handler, irqflags, devname, dev_id);
	if (retval != 0) {
		unbind_from_irq(irq);
		return retval;
	}

	return irq;
}
EXPORT_SYMBOL_GPL(bind_virq_to_irqhandler);

int bind_ipi_to_irqhandler(enum ipi_vector ipi,
			   unsigned int cpu,
			   irq_handler_t handler,
			   unsigned long irqflags,
			   const char *devname,
			   void *dev_id)
{
	int irq, retval;

	irq = bind_ipi_to_irq(ipi, cpu);
	if (irq < 0)
		return irq;

	irqflags |= IRQF_NO_SUSPEND;
	retval = request_irq(irq, handler, irqflags, devname, dev_id);
	if (retval != 0) {
		unbind_from_irq(irq);
		return retval;
	}

	return irq;
}

void unbind_from_irqhandler(unsigned int irq, void *dev_id)
{
	free_irq(irq, dev_id);
	unbind_from_irq(irq);
}
EXPORT_SYMBOL_GPL(unbind_from_irqhandler);

void xen_send_IPI_one(unsigned int cpu, enum ipi_vector vector)
{
	int irq = per_cpu(ipi_to_irq, cpu)[vector];
	BUG_ON(irq < 0);
	notify_remote_via_irq(irq);
}

irqreturn_t xen_debug_interrupt(int irq, void *dev_id)
{
	struct shared_info *sh = HYPERVISOR_shared_info;
	int cpu = smp_processor_id();
	unsigned long *cpu_evtchn = cpu_evtchn_mask(cpu);
	int i;
	unsigned long flags;
	static DEFINE_SPINLOCK(debug_lock);
	struct vcpu_info *v;

	spin_lock_irqsave(&debug_lock, flags);

	printk("\nvcpu %d\n  ", cpu);

	for_each_online_cpu(i) {
		int pending;
		v = per_cpu(xen_vcpu, i);
		pending = (get_irq_regs() && i == cpu)
			? xen_irqs_disabled(get_irq_regs())
			: v->evtchn_upcall_mask;
		printk("%d: masked=%d pending=%d event_sel %0*lx\n  ", i,
		       pending, v->evtchn_upcall_pending,
		       (int)(sizeof(v->evtchn_pending_sel)*2),
		       v->evtchn_pending_sel);
	}
	v = per_cpu(xen_vcpu, cpu);

	printk("\npending:\n   ");
	for (i = ARRAY_SIZE(sh->evtchn_pending)-1; i >= 0; i--)
		printk("%0*lx%s", (int)sizeof(sh->evtchn_pending[0])*2,
		       sh->evtchn_pending[i],
		       i % 8 == 0 ? "\n   " : " ");
	printk("\nglobal mask:\n   ");
	for (i = ARRAY_SIZE(sh->evtchn_mask)-1; i >= 0; i--)
		printk("%0*lx%s",
		       (int)(sizeof(sh->evtchn_mask[0])*2),
		       sh->evtchn_mask[i],
		       i % 8 == 0 ? "\n   " : " ");

	printk("\nglobally unmasked:\n   ");
	for (i = ARRAY_SIZE(sh->evtchn_mask)-1; i >= 0; i--)
		printk("%0*lx%s", (int)(sizeof(sh->evtchn_mask[0])*2),
		       sh->evtchn_pending[i] & ~sh->evtchn_mask[i],
		       i % 8 == 0 ? "\n   " : " ");

	printk("\nlocal cpu%d mask:\n   ", cpu);
	for (i = (NR_EVENT_CHANNELS/BITS_PER_LONG)-1; i >= 0; i--)
		printk("%0*lx%s", (int)(sizeof(cpu_evtchn[0])*2),
		       cpu_evtchn[i],
		       i % 8 == 0 ? "\n   " : " ");

	printk("\nlocally unmasked:\n   ");
	for (i = ARRAY_SIZE(sh->evtchn_mask)-1; i >= 0; i--) {
		unsigned long pending = sh->evtchn_pending[i]
			& ~sh->evtchn_mask[i]
			& cpu_evtchn[i];
		printk("%0*lx%s", (int)(sizeof(sh->evtchn_mask[0])*2),
		       pending, i % 8 == 0 ? "\n   " : " ");
	}

	printk("\npending list:\n");
	for (i = 0; i < NR_EVENT_CHANNELS; i++) {
		if (sync_test_bit(i, sh->evtchn_pending)) {
			int word_idx = i / BITS_PER_LONG;
			printk("  %d: event %d -> irq %d%s%s%s\n",
			       cpu_from_evtchn(i), i,
			       evtchn_to_irq[i],
			       sync_test_bit(word_idx, &v->evtchn_pending_sel)
					     ? "" : " l2-clear",
			       !sync_test_bit(i, sh->evtchn_mask)
					     ? "" : " globally-masked",
			       sync_test_bit(i, cpu_evtchn)
					     ? "" : " locally-masked");
		}
	}

	spin_unlock_irqrestore(&debug_lock, flags);

	return IRQ_HANDLED;
}

static DEFINE_PER_CPU(unsigned, xed_nesting_count);

/*
 * Search the CPUs pending events bitmasks.  For each one found, map
 * the event number to an irq, and feed it into do_IRQ() for
 * handling.
 *
 * Xen uses a two-level bitmap to speed searching.  The first level is
 * a bitset of words which contain pending event bits.  The second
 * level is a bitset of pending events themselves.
 */
static void __xen_evtchn_do_upcall(void)
{
	int cpu = get_cpu();
	struct shared_info *s = HYPERVISOR_shared_info;
	struct vcpu_info *vcpu_info = __get_cpu_var(xen_vcpu);
 	unsigned count;

	do {
		unsigned long pending_words;

		vcpu_info->evtchn_upcall_pending = 0;

		if (__get_cpu_var(xed_nesting_count)++)
			goto out;

#ifndef CONFIG_X86 /* No need for a barrier -- XCHG is a barrier on x86. */
		/* Clear master flag /before/ clearing selector flag. */
		wmb();
#endif
		pending_words = xchg(&vcpu_info->evtchn_pending_sel, 0);
		while (pending_words != 0) {
			unsigned long pending_bits;
			int word_idx = __ffs(pending_words);
			pending_words &= ~(1UL << word_idx);

			while ((pending_bits = active_evtchns(cpu, s, word_idx)) != 0) {
				int bit_idx = __ffs(pending_bits);
				int port = (word_idx * BITS_PER_LONG) + bit_idx;
				int irq = evtchn_to_irq[port];
				struct irq_desc *desc;

				mask_evtchn(port);
				clear_evtchn(port);

				if (irq != -1) {
					desc = irq_to_desc(irq);
					if (desc)
						generic_handle_irq_desc(irq, desc);
				}
			}
		}

		BUG_ON(!irqs_disabled());

		count = __get_cpu_var(xed_nesting_count);
		__get_cpu_var(xed_nesting_count) = 0;
	} while (count != 1 || vcpu_info->evtchn_upcall_pending);

out:

	put_cpu();
}

void xen_evtchn_do_upcall(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	exit_idle();
	irq_enter();

	__xen_evtchn_do_upcall();

	irq_exit();
	set_irq_regs(old_regs);
}

void xen_hvm_evtchn_do_upcall(void)
{
	__xen_evtchn_do_upcall();
}
EXPORT_SYMBOL_GPL(xen_hvm_evtchn_do_upcall);

/* Rebind a new event channel to an existing irq. */
void rebind_evtchn_irq(int evtchn, int irq)
{
	struct irq_info *info = info_for_irq(irq);

	/* Make sure the irq is masked, since the new event channel
	   will also be masked. */
	disable_irq(irq);

	spin_lock(&irq_mapping_update_lock);

	/* After resume the irq<->evtchn mappings are all cleared out */
	BUG_ON(evtchn_to_irq[evtchn] != -1);
	/* Expect irq to have been bound before,
	   so there should be a proper type */
	BUG_ON(info->type == IRQT_UNBOUND);

	evtchn_to_irq[evtchn] = irq;
	irq_info[irq] = mk_evtchn_info(evtchn);

	spin_unlock(&irq_mapping_update_lock);

	/* new event channels are always bound to cpu 0 */
	irq_set_affinity(irq, cpumask_of(0));

	/* Unmask the event channel. */
	enable_irq(irq);
}

/* Rebind an evtchn so that it gets delivered to a specific cpu */
static int rebind_irq_to_cpu(unsigned irq, unsigned tcpu)
{
	struct evtchn_bind_vcpu bind_vcpu;
	int evtchn = evtchn_from_irq(irq);

	/* events delivered via platform PCI interrupts are always
	 * routed to vcpu 0 */
	if (!VALID_EVTCHN(evtchn) ||
		(xen_hvm_domain() && !xen_have_vector_callback))
		return -1;

	/* Send future instances of this interrupt to other vcpu. */
	bind_vcpu.port = evtchn;
	bind_vcpu.vcpu = tcpu;

	/*
	 * If this fails, it usually just indicates that we're dealing with a
	 * virq or IPI channel, which don't actually need to be rebound. Ignore
	 * it, but don't do the xenlinux-level rebind in that case.
	 */
	if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_vcpu, &bind_vcpu) >= 0)
		bind_evtchn_to_cpu(evtchn, tcpu);

	return 0;
}

static int set_affinity_irq(unsigned irq, const struct cpumask *dest)
{
	unsigned tcpu = cpumask_first(dest);

	return rebind_irq_to_cpu(irq, tcpu);
}

int resend_irq_on_evtchn(unsigned int irq)
{
	int masked, evtchn = evtchn_from_irq(irq);
	struct shared_info *s = HYPERVISOR_shared_info;

	if (!VALID_EVTCHN(evtchn))
		return 1;

	masked = sync_test_and_set_bit(evtchn, s->evtchn_mask);
	sync_set_bit(evtchn, s->evtchn_pending);
	if (!masked)
		unmask_evtchn(evtchn);

	return 1;
}

static void enable_dynirq(unsigned int irq)
{
	int evtchn = evtchn_from_irq(irq);

	if (VALID_EVTCHN(evtchn))
		unmask_evtchn(evtchn);
}

static void disable_dynirq(unsigned int irq)
{
	int evtchn = evtchn_from_irq(irq);

	if (VALID_EVTCHN(evtchn))
		mask_evtchn(evtchn);
}

static void ack_dynirq(unsigned int irq)
{
	int evtchn = evtchn_from_irq(irq);

	move_masked_irq(irq);

	if (VALID_EVTCHN(evtchn))
		unmask_evtchn(evtchn);
}

static int retrigger_dynirq(unsigned int irq)
{
	int evtchn = evtchn_from_irq(irq);
	struct shared_info *sh = HYPERVISOR_shared_info;
	int ret = 0;

	if (VALID_EVTCHN(evtchn)) {
		int masked;

		masked = sync_test_and_set_bit(evtchn, sh->evtchn_mask);
		sync_set_bit(evtchn, sh->evtchn_pending);
		if (!masked)
			unmask_evtchn(evtchn);
		ret = 1;
	}

	return ret;
}

static void restore_cpu_pirqs(void)
{
	int pirq, rc, irq, gsi;
	struct physdev_map_pirq map_irq;

	for (pirq = 0; pirq < nr_irqs; pirq++) {
		irq = pirq_to_irq[pirq];
		if (irq == -1)
			continue;

		/* save/restore of PT devices doesn't work, so at this point the
		 * only devices present are GSI based emulated devices */
		gsi = gsi_from_irq(irq);
		if (!gsi)
			continue;

		map_irq.domid = DOMID_SELF;
		map_irq.type = MAP_PIRQ_TYPE_GSI;
		map_irq.index = gsi;
		map_irq.pirq = pirq;

		rc = HYPERVISOR_physdev_op(PHYSDEVOP_map_pirq, &map_irq);
		if (rc) {
			printk(KERN_WARNING "xen map irq failed gsi=%d irq=%d pirq=%d rc=%d\n",
					gsi, irq, pirq, rc);
			irq_info[irq] = mk_unbound_info();
			pirq_to_irq[pirq] = -1;
			continue;
		}

		printk(KERN_DEBUG "xen: --> irq=%d, pirq=%d\n", irq, map_irq.pirq);

		startup_pirq(irq);
	}
}

static void restore_cpu_virqs(unsigned int cpu)
{
	struct evtchn_bind_virq bind_virq;
	int virq, irq, evtchn;

	for (virq = 0; virq < NR_VIRQS; virq++) {
		if ((irq = per_cpu(virq_to_irq, cpu)[virq]) == -1)
			continue;

		BUG_ON(virq_from_irq(irq) != virq);

		/* Get a new binding from Xen. */
		bind_virq.virq = virq;
		bind_virq.vcpu = cpu;
		if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_virq,
						&bind_virq) != 0)
			BUG();
		evtchn = bind_virq.port;

		/* Record the new mapping. */
		evtchn_to_irq[evtchn] = irq;
		irq_info[irq] = mk_virq_info(evtchn, virq);
		bind_evtchn_to_cpu(evtchn, cpu);
	}
}

static void restore_cpu_ipis(unsigned int cpu)
{
	struct evtchn_bind_ipi bind_ipi;
	int ipi, irq, evtchn;

	for (ipi = 0; ipi < XEN_NR_IPIS; ipi++) {
		if ((irq = per_cpu(ipi_to_irq, cpu)[ipi]) == -1)
			continue;

		BUG_ON(ipi_from_irq(irq) != ipi);

		/* Get a new binding from Xen. */
		bind_ipi.vcpu = cpu;
		if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_ipi,
						&bind_ipi) != 0)
			BUG();
		evtchn = bind_ipi.port;

		/* Record the new mapping. */
		evtchn_to_irq[evtchn] = irq;
		irq_info[irq] = mk_ipi_info(evtchn, ipi);
		bind_evtchn_to_cpu(evtchn, cpu);
	}
}

/* Clear an irq's pending state, in preparation for polling on it */
void xen_clear_irq_pending(int irq)
{
	int evtchn = evtchn_from_irq(irq);

	if (VALID_EVTCHN(evtchn))
		clear_evtchn(evtchn);
}
EXPORT_SYMBOL(xen_clear_irq_pending);
void xen_set_irq_pending(int irq)
{
	int evtchn = evtchn_from_irq(irq);

	if (VALID_EVTCHN(evtchn))
		set_evtchn(evtchn);
}

bool xen_test_irq_pending(int irq)
{
	int evtchn = evtchn_from_irq(irq);
	bool ret = false;

	if (VALID_EVTCHN(evtchn))
		ret = test_evtchn(evtchn);

	return ret;
}

/* Poll waiting for an irq to become pending with timeout.  In the usual case,
 * the irq will be disabled so it won't deliver an interrupt. */
void xen_poll_irq_timeout(int irq, u64 timeout)
{
	evtchn_port_t evtchn = evtchn_from_irq(irq);

	if (VALID_EVTCHN(evtchn)) {
		struct sched_poll poll;

		poll.nr_ports = 1;
		poll.timeout = timeout;
		set_xen_guest_handle(poll.ports, &evtchn);

		if (HYPERVISOR_sched_op(SCHEDOP_poll, &poll) != 0)
			BUG();
	}
}
EXPORT_SYMBOL(xen_poll_irq_timeout);
/* Poll waiting for an irq to become pending.  In the usual case, the
 * irq will be disabled so it won't deliver an interrupt. */
void xen_poll_irq(int irq)
{
	xen_poll_irq_timeout(irq, 0 /* no timeout */);
}

void xen_irq_resume(void)
{
	unsigned int cpu, irq, evtchn;
	struct irq_desc *desc;

	init_evtchn_cpu_bindings();

	/* New event-channel space is not 'live' yet. */
	for (evtchn = 0; evtchn < NR_EVENT_CHANNELS; evtchn++)
		mask_evtchn(evtchn);

	/* No IRQ <-> event-channel mappings. */
	for (irq = 0; irq < nr_irqs; irq++)
		irq_info[irq].evtchn = 0; /* zap event-channel binding */

	for (evtchn = 0; evtchn < NR_EVENT_CHANNELS; evtchn++)
		evtchn_to_irq[evtchn] = -1;

	for_each_possible_cpu(cpu) {
		restore_cpu_virqs(cpu);
		restore_cpu_ipis(cpu);
	}

	/*
	 * Unmask any IRQF_NO_SUSPEND IRQs which are enabled. These
	 * are not handled by the IRQ core.
	 */
	for_each_irq_desc(irq, desc) {
		if (!desc->action || !(desc->action->flags & IRQF_NO_SUSPEND))
			continue;
		if (desc->status & IRQ_DISABLED)
			continue;

		evtchn = evtchn_from_irq(irq);
		if (evtchn == -1)
			continue;

		unmask_evtchn(evtchn);
	}

	restore_cpu_pirqs();
}

static struct irq_chip xen_dynamic_chip __read_mostly = {
	.name		= "xen-dyn",

	.disable	= disable_dynirq,
	.mask		= disable_dynirq,
	.unmask		= enable_dynirq,

	.eoi		= ack_dynirq,
	.set_affinity	= set_affinity_irq,
	.retrigger	= retrigger_dynirq,
};

static struct irq_chip xen_pirq_chip __read_mostly = {
	.name		= "xen-pirq",

	.startup	= startup_pirq,
	.shutdown	= shutdown_pirq,

	.enable		= enable_pirq,
	.unmask		= enable_pirq,

	.disable	= disable_pirq,
	.mask		= disable_pirq,

	.ack		= ack_pirq,
	.end		= end_pirq,

	.set_affinity	= set_affinity_irq,

	.retrigger	= retrigger_dynirq,
};

static struct irq_chip xen_percpu_chip __read_mostly = {
	.name		= "xen-percpu",

	.disable	= disable_dynirq,
	.mask		= disable_dynirq,
	.unmask		= enable_dynirq,

	.ack		= ack_dynirq,
};

int xen_set_callback_via(uint64_t via)
{
	struct xen_hvm_param a;
	a.domid = DOMID_SELF;
	a.index = HVM_PARAM_CALLBACK_IRQ;
	a.value = via;
	return HYPERVISOR_hvm_op(HVMOP_set_param, &a);
}
EXPORT_SYMBOL_GPL(xen_set_callback_via);

#ifdef CONFIG_XEN_PVHVM
/* Vector callbacks are better than PCI interrupts to receive event
 * channel notifications because we can receive vector callbacks on any
 * vcpu and we don't need PCI support or APIC interactions. */
void xen_callback_vector(void)
{
	int rc;
	uint64_t callback_via;
	if (xen_have_vector_callback) {
		callback_via = HVM_CALLBACK_VECTOR(XEN_HVM_EVTCHN_CALLBACK);
		rc = xen_set_callback_via(callback_via);
		if (rc) {
			printk(KERN_ERR "Request for Xen HVM callback vector"
					" failed.\n");
			xen_have_vector_callback = 0;
			return;
		}
		printk(KERN_INFO "Xen HVM callback vector for event delivery is "
				"enabled\n");
		/* in the restore case the vector has already been allocated */
		if (!test_bit(XEN_HVM_EVTCHN_CALLBACK, used_vectors))
			alloc_intr_gate(XEN_HVM_EVTCHN_CALLBACK, xen_hvm_callback_vector);
	}
}
#else
void xen_callback_vector(void) {}
#endif

void __init xen_init_IRQ(void)
{
	int i;

	cpu_evtchn_mask_p = kcalloc(nr_cpu_ids, sizeof(struct cpu_evtchn_s),
				    GFP_KERNEL);
	irq_info = kcalloc(nr_irqs, sizeof(*irq_info), GFP_KERNEL);

	/* We are using nr_irqs as the maximum number of pirq available but
	 * that number is actually chosen by Xen and we don't know exactly
	 * what it is. Be careful choosing high pirq numbers. */
	pirq_to_irq = kcalloc(nr_irqs, sizeof(*pirq_to_irq), GFP_KERNEL);
	for (i = 0; i < nr_irqs; i++)
		pirq_to_irq[i] = -1;

	evtchn_to_irq = kcalloc(NR_EVENT_CHANNELS, sizeof(*evtchn_to_irq),
				    GFP_KERNEL);
	for (i = 0; i < NR_EVENT_CHANNELS; i++)
		evtchn_to_irq[i] = -1;

	init_evtchn_cpu_bindings();

	/* No event channels are 'live' right now. */
	for (i = 0; i < NR_EVENT_CHANNELS; i++)
		mask_evtchn(i);

	if (xen_hvm_domain()) {
		xen_callback_vector();
		native_init_IRQ();
		/* pci_xen_hvm_init must be called after native_init_IRQ so that
		 * __acpi_register_gsi can point at the right function */
		pci_xen_hvm_init();
	} else {
		irq_ctx_init(smp_processor_id());
		if (xen_initial_domain())
			xen_setup_pirqs();
	}
}
