/*
 * Xen event channels
 *
 * Xen models interrupts with abstract event channels.  Because each
 * domain gets 1024 event channels, but NR_IRQ is not that large, we
 * must dynamically map irqs<->event channels.  The event channels
 * interface with the rest of the kernel by defining a xen interrupt
 * chip.  When an event is received, it is mapped to an irq and sent
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

#ifdef CONFIG_X86
#include <asm/desc.h>
#include <asm/ptrace.h>
#include <asm/irq.h>
#include <asm/idle.h>
#include <asm/io_apic.h>
#include <asm/xen/page.h>
#include <asm/xen/pci.h>
#endif
#include <asm/sync_bitops.h>
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
#include <xen/interface/physdev.h>
#include <xen/interface/sched.h>
#include <asm/hw_irq.h>

/*
 * This lock protects updates to the following mapping and reference-count
 * arrays. The lock does not need to be acquired to read the mapping tables.
 */
static DEFINE_MUTEX(irq_mapping_update_lock);

static LIST_HEAD(xen_irq_list_head);

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
 *    PIRQ - physical IRQ, GSI, flags, and owner domain
 *    VIRQ - virq number
 *    IPI - IPI vector
 *    EVTCHN -
 */
struct irq_info {
	struct list_head list;
	int refcnt;
	enum xen_irq_type type;	/* type */
	unsigned irq;
	unsigned short evtchn;	/* event channel */
	unsigned short cpu;	/* cpu bound */

	union {
		unsigned short virq;
		enum ipi_vector ipi;
		struct {
			unsigned short pirq;
			unsigned short gsi;
			unsigned char flags;
			uint16_t domid;
		} pirq;
	} u;
};
#define PIRQ_NEEDS_EOI	(1 << 0)
#define PIRQ_SHAREABLE	(1 << 1)

static int *evtchn_to_irq;
#ifdef CONFIG_X86
static unsigned long *pirq_eoi_map;
#endif
static bool (*pirq_needs_eoi)(unsigned irq);

/*
 * Note sizeof(xen_ulong_t) can be more than sizeof(unsigned long). Be
 * careful to only use bitops which allow for this (e.g
 * test_bit/find_first_bit and friends but not __ffs) and to pass
 * BITS_PER_EVTCHN_WORD as the bitmask length.
 */
#define BITS_PER_EVTCHN_WORD (sizeof(xen_ulong_t)*8)
/*
 * Make a bitmask (i.e. unsigned long *) of a xen_ulong_t
 * array. Primarily to avoid long lines (hence the terse name).
 */
#define BM(x) (unsigned long *)(x)
/* Find the first set bit in a evtchn mask */
#define EVTCHN_FIRST_BIT(w) find_first_bit(BM(&(w)), BITS_PER_EVTCHN_WORD)

static DEFINE_PER_CPU(xen_ulong_t [NR_EVENT_CHANNELS/BITS_PER_EVTCHN_WORD],
		      cpu_evtchn_mask);

/* Xen will never allocate port zero for any purpose. */
#define VALID_EVTCHN(chn)	((chn) != 0)

static struct irq_chip xen_dynamic_chip;
static struct irq_chip xen_percpu_chip;
static struct irq_chip xen_pirq_chip;
static void enable_dynirq(struct irq_data *data);
static void disable_dynirq(struct irq_data *data);

/* Get info for IRQ */
static struct irq_info *info_for_irq(unsigned irq)
{
	return irq_get_handler_data(irq);
}

/* Constructors for packed IRQ information. */
static void xen_irq_info_common_init(struct irq_info *info,
				     unsigned irq,
				     enum xen_irq_type type,
				     unsigned short evtchn,
				     unsigned short cpu)
{

	BUG_ON(info->type != IRQT_UNBOUND && info->type != type);

	info->type = type;
	info->irq = irq;
	info->evtchn = evtchn;
	info->cpu = cpu;

	evtchn_to_irq[evtchn] = irq;

	irq_clear_status_flags(irq, IRQ_NOREQUEST|IRQ_NOAUTOEN);
}

static void xen_irq_info_evtchn_init(unsigned irq,
				     unsigned short evtchn)
{
	struct irq_info *info = info_for_irq(irq);

	xen_irq_info_common_init(info, irq, IRQT_EVTCHN, evtchn, 0);
}

static void xen_irq_info_ipi_init(unsigned cpu,
				  unsigned irq,
				  unsigned short evtchn,
				  enum ipi_vector ipi)
{
	struct irq_info *info = info_for_irq(irq);

	xen_irq_info_common_init(info, irq, IRQT_IPI, evtchn, 0);

	info->u.ipi = ipi;

	per_cpu(ipi_to_irq, cpu)[ipi] = irq;
}

static void xen_irq_info_virq_init(unsigned cpu,
				   unsigned irq,
				   unsigned short evtchn,
				   unsigned short virq)
{
	struct irq_info *info = info_for_irq(irq);

	xen_irq_info_common_init(info, irq, IRQT_VIRQ, evtchn, 0);

	info->u.virq = virq;

	per_cpu(virq_to_irq, cpu)[virq] = irq;
}

static void xen_irq_info_pirq_init(unsigned irq,
				   unsigned short evtchn,
				   unsigned short pirq,
				   unsigned short gsi,
				   uint16_t domid,
				   unsigned char flags)
{
	struct irq_info *info = info_for_irq(irq);

	xen_irq_info_common_init(info, irq, IRQT_PIRQ, evtchn, 0);

	info->u.pirq.pirq = pirq;
	info->u.pirq.gsi = gsi;
	info->u.pirq.domid = domid;
	info->u.pirq.flags = flags;
}

/*
 * Accessors for packed IRQ information.
 */
static unsigned int evtchn_from_irq(unsigned irq)
{
	if (unlikely(WARN(irq < 0 || irq >= nr_irqs, "Invalid irq %d!\n", irq)))
		return 0;

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

#ifdef CONFIG_X86
static bool pirq_check_eoi_map(unsigned irq)
{
	return test_bit(pirq_from_irq(irq), pirq_eoi_map);
}
#endif

static bool pirq_needs_eoi_flag(unsigned irq)
{
	struct irq_info *info = info_for_irq(irq);
	BUG_ON(info->type != IRQT_PIRQ);

	return info->u.pirq.flags & PIRQ_NEEDS_EOI;
}

static inline xen_ulong_t active_evtchns(unsigned int cpu,
					 struct shared_info *sh,
					 unsigned int idx)
{
	return sh->evtchn_pending[idx] &
		per_cpu(cpu_evtchn_mask, cpu)[idx] &
		~sh->evtchn_mask[idx];
}

static void bind_evtchn_to_cpu(unsigned int chn, unsigned int cpu)
{
	int irq = evtchn_to_irq[chn];

	BUG_ON(irq == -1);
#ifdef CONFIG_SMP
	cpumask_copy(irq_to_desc(irq)->irq_data.affinity, cpumask_of(cpu));
#endif

	clear_bit(chn, BM(per_cpu(cpu_evtchn_mask, cpu_from_irq(irq))));
	set_bit(chn, BM(per_cpu(cpu_evtchn_mask, cpu)));

	info_for_irq(irq)->cpu = cpu;
}

static void init_evtchn_cpu_bindings(void)
{
	int i;
#ifdef CONFIG_SMP
	struct irq_info *info;

	/* By default all event channels notify CPU#0. */
	list_for_each_entry(info, &xen_irq_list_head, list) {
		struct irq_desc *desc = irq_to_desc(info->irq);
		cpumask_copy(desc->irq_data.affinity, cpumask_of(0));
	}
#endif

	for_each_possible_cpu(i)
		memset(per_cpu(cpu_evtchn_mask, i),
		       (i == 0) ? ~0 : 0, sizeof(*per_cpu(cpu_evtchn_mask, i)));
}

static inline void clear_evtchn(int port)
{
	struct shared_info *s = HYPERVISOR_shared_info;
	sync_clear_bit(port, BM(&s->evtchn_pending[0]));
}

static inline void set_evtchn(int port)
{
	struct shared_info *s = HYPERVISOR_shared_info;
	sync_set_bit(port, BM(&s->evtchn_pending[0]));
}

static inline int test_evtchn(int port)
{
	struct shared_info *s = HYPERVISOR_shared_info;
	return sync_test_bit(port, BM(&s->evtchn_pending[0]));
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
	sync_set_bit(port, BM(&s->evtchn_mask[0]));
}

static void unmask_evtchn(int port)
{
	struct shared_info *s = HYPERVISOR_shared_info;
	unsigned int cpu = get_cpu();
	int do_hypercall = 0, evtchn_pending = 0;

	BUG_ON(!irqs_disabled());

	if (unlikely((cpu != cpu_from_evtchn(port))))
		do_hypercall = 1;
	else {
		/*
		 * Need to clear the mask before checking pending to
		 * avoid a race with an event becoming pending.
		 *
		 * EVTCHNOP_unmask will only trigger an upcall if the
		 * mask bit was set, so if a hypercall is needed
		 * remask the event.
		 */
		sync_clear_bit(port, BM(&s->evtchn_mask[0]));
		evtchn_pending = sync_test_bit(port, BM(&s->evtchn_pending[0]));

		if (unlikely(evtchn_pending && xen_hvm_domain())) {
			sync_set_bit(port, BM(&s->evtchn_mask[0]));
			do_hypercall = 1;
		}
	}

	/* Slow path (hypercall) if this is a non-local port or if this is
	 * an hvm domain and an event is pending (hvm domains don't have
	 * their own implementation of irq_enable). */
	if (do_hypercall) {
		struct evtchn_unmask unmask = { .port = port };
		(void)HYPERVISOR_event_channel_op(EVTCHNOP_unmask, &unmask);
	} else {
		struct vcpu_info *vcpu_info = __this_cpu_read(xen_vcpu);

		/*
		 * The following is basically the equivalent of
		 * 'hw_resend_irq'. Just like a real IO-APIC we 'lose
		 * the interrupt edge' if the channel is masked.
		 */
		if (evtchn_pending &&
		    !sync_test_and_set_bit(port / BITS_PER_EVTCHN_WORD,
					   BM(&vcpu_info->evtchn_pending_sel)))
			vcpu_info->evtchn_upcall_pending = 1;
	}

	put_cpu();
}

static void xen_irq_init(unsigned irq)
{
	struct irq_info *info;
#ifdef CONFIG_SMP
	struct irq_desc *desc = irq_to_desc(irq);

	/* By default all event channels notify CPU#0. */
	cpumask_copy(desc->irq_data.affinity, cpumask_of(0));
#endif

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (info == NULL)
		panic("Unable to allocate metadata for IRQ%d\n", irq);

	info->type = IRQT_UNBOUND;
	info->refcnt = -1;

	irq_set_handler_data(irq, info);

	list_add_tail(&info->list, &xen_irq_list_head);
}

static int __must_check xen_allocate_irq_dynamic(void)
{
	int first = 0;
	int irq;

#ifdef CONFIG_X86_IO_APIC
	/*
	 * For an HVM guest or domain 0 which see "real" (emulated or
	 * actual respectively) GSIs we allocate dynamic IRQs
	 * e.g. those corresponding to event channels or MSIs
	 * etc. from the range above those "real" GSIs to avoid
	 * collisions.
	 */
	if (xen_initial_domain() || xen_hvm_domain())
		first = get_nr_irqs_gsi();
#endif

	irq = irq_alloc_desc_from(first, -1);

	if (irq >= 0)
		xen_irq_init(irq);

	return irq;
}

static int __must_check xen_allocate_irq_gsi(unsigned gsi)
{
	int irq;

	/*
	 * A PV guest has no concept of a GSI (since it has no ACPI
	 * nor access to/knowledge of the physical APICs). Therefore
	 * all IRQs are dynamically allocated from the entire IRQ
	 * space.
	 */
	if (xen_pv_domain() && !xen_initial_domain())
		return xen_allocate_irq_dynamic();

	/* Legacy IRQ descriptors are already allocated by the arch. */
	if (gsi < NR_IRQS_LEGACY)
		irq = gsi;
	else
		irq = irq_alloc_desc_at(gsi, -1);

	xen_irq_init(irq);

	return irq;
}

static void xen_free_irq(unsigned irq)
{
	struct irq_info *info = irq_get_handler_data(irq);

	if (WARN_ON(!info))
		return;

	list_del(&info->list);

	irq_set_handler_data(irq, NULL);

	WARN_ON(info->refcnt > 0);

	kfree(info);

	/* Legacy IRQ descriptors are managed by the arch. */
	if (irq < NR_IRQS_LEGACY)
		return;

	irq_free_desc(irq);
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

static void eoi_pirq(struct irq_data *data)
{
	int evtchn = evtchn_from_irq(data->irq);
	struct physdev_eoi eoi = { .irq = pirq_from_irq(data->irq) };
	int rc = 0;

	irq_move_irq(data);

	if (VALID_EVTCHN(evtchn))
		clear_evtchn(evtchn);

	if (pirq_needs_eoi(data->irq)) {
		rc = HYPERVISOR_physdev_op(PHYSDEVOP_eoi, &eoi);
		WARN_ON(rc);
	}
}

static void mask_ack_pirq(struct irq_data *data)
{
	disable_dynirq(data);
	eoi_pirq(data);
}

static unsigned int __startup_pirq(unsigned int irq)
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
	eoi_pirq(irq_get_irq_data(irq));

	return 0;
}

static unsigned int startup_pirq(struct irq_data *data)
{
	return __startup_pirq(data->irq);
}

static void shutdown_pirq(struct irq_data *data)
{
	struct evtchn_close close;
	unsigned int irq = data->irq;
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

static void enable_pirq(struct irq_data *data)
{
	startup_pirq(data);
}

static void disable_pirq(struct irq_data *data)
{
	disable_dynirq(data);
}

int xen_irq_from_gsi(unsigned gsi)
{
	struct irq_info *info;

	list_for_each_entry(info, &xen_irq_list_head, list) {
		if (info->type != IRQT_PIRQ)
			continue;

		if (info->u.pirq.gsi == gsi)
			return info->irq;
	}

	return -1;
}
EXPORT_SYMBOL_GPL(xen_irq_from_gsi);

/*
 * Do not make any assumptions regarding the relationship between the
 * IRQ number returned here and the Xen pirq argument.
 *
 * Note: We don't assign an event channel until the irq actually started
 * up.  Return an existing irq if we've already got one for the gsi.
 *
 * Shareable implies level triggered, not shareable implies edge
 * triggered here.
 */
int xen_bind_pirq_gsi_to_irq(unsigned gsi,
			     unsigned pirq, int shareable, char *name)
{
	int irq = -1;
	struct physdev_irq irq_op;

	mutex_lock(&irq_mapping_update_lock);

	irq = xen_irq_from_gsi(gsi);
	if (irq != -1) {
		printk(KERN_INFO "xen_map_pirq_gsi: returning irq %d for gsi %u\n",
		       irq, gsi);
		goto out;
	}

	irq = xen_allocate_irq_gsi(gsi);
	if (irq < 0)
		goto out;

	irq_op.irq = irq;
	irq_op.vector = 0;

	/* Only the privileged domain can do this. For non-priv, the pcifront
	 * driver provides a PCI bus that does the call to do exactly
	 * this in the priv domain. */
	if (xen_initial_domain() &&
	    HYPERVISOR_physdev_op(PHYSDEVOP_alloc_irq_vector, &irq_op)) {
		xen_free_irq(irq);
		irq = -ENOSPC;
		goto out;
	}

	xen_irq_info_pirq_init(irq, 0, pirq, gsi, DOMID_SELF,
			       shareable ? PIRQ_SHAREABLE : 0);

	pirq_query_unmask(irq);
	/* We try to use the handler with the appropriate semantic for the
	 * type of interrupt: if the interrupt is an edge triggered
	 * interrupt we use handle_edge_irq.
	 *
	 * On the other hand if the interrupt is level triggered we use
	 * handle_fasteoi_irq like the native code does for this kind of
	 * interrupts.
	 *
	 * Depending on the Xen version, pirq_needs_eoi might return true
	 * not only for level triggered interrupts but for edge triggered
	 * interrupts too. In any case Xen always honors the eoi mechanism,
	 * not injecting any more pirqs of the same kind if the first one
	 * hasn't received an eoi yet. Therefore using the fasteoi handler
	 * is the right choice either way.
	 */
	if (shareable)
		irq_set_chip_and_handler_name(irq, &xen_pirq_chip,
				handle_fasteoi_irq, name);
	else
		irq_set_chip_and_handler_name(irq, &xen_pirq_chip,
				handle_edge_irq, name);

out:
	mutex_unlock(&irq_mapping_update_lock);

	return irq;
}

#ifdef CONFIG_PCI_MSI
int xen_allocate_pirq_msi(struct pci_dev *dev, struct msi_desc *msidesc)
{
	int rc;
	struct physdev_get_free_pirq op_get_free_pirq;

	op_get_free_pirq.type = MAP_PIRQ_TYPE_MSI;
	rc = HYPERVISOR_physdev_op(PHYSDEVOP_get_free_pirq, &op_get_free_pirq);

	WARN_ONCE(rc == -ENOSYS,
		  "hypervisor does not support the PHYSDEVOP_get_free_pirq interface\n");

	return rc ? -1 : op_get_free_pirq.pirq;
}

int xen_bind_pirq_msi_to_irq(struct pci_dev *dev, struct msi_desc *msidesc,
			     int pirq, const char *name, domid_t domid)
{
	int irq, ret;

	mutex_lock(&irq_mapping_update_lock);

	irq = xen_allocate_irq_dynamic();
	if (irq < 0)
		goto out;

	irq_set_chip_and_handler_name(irq, &xen_pirq_chip, handle_edge_irq,
			name);

	xen_irq_info_pirq_init(irq, 0, pirq, 0, domid, 0);
	ret = irq_set_msi_desc(irq, msidesc);
	if (ret < 0)
		goto error_irq;
out:
	mutex_unlock(&irq_mapping_update_lock);
	return irq;
error_irq:
	mutex_unlock(&irq_mapping_update_lock);
	xen_free_irq(irq);
	return ret;
}
#endif

int xen_destroy_irq(int irq)
{
	struct irq_desc *desc;
	struct physdev_unmap_pirq unmap_irq;
	struct irq_info *info = info_for_irq(irq);
	int rc = -ENOENT;

	mutex_lock(&irq_mapping_update_lock);

	desc = irq_to_desc(irq);
	if (!desc)
		goto out;

	if (xen_initial_domain()) {
		unmap_irq.pirq = info->u.pirq.pirq;
		unmap_irq.domid = info->u.pirq.domid;
		rc = HYPERVISOR_physdev_op(PHYSDEVOP_unmap_pirq, &unmap_irq);
		/* If another domain quits without making the pci_disable_msix
		 * call, the Xen hypervisor takes care of freeing the PIRQs
		 * (free_domain_pirqs).
		 */
		if ((rc == -ESRCH && info->u.pirq.domid != DOMID_SELF))
			printk(KERN_INFO "domain %d does not have %d anymore\n",
				info->u.pirq.domid, info->u.pirq.pirq);
		else if (rc) {
			printk(KERN_WARNING "unmap irq failed %d\n", rc);
			goto out;
		}
	}

	xen_free_irq(irq);

out:
	mutex_unlock(&irq_mapping_update_lock);
	return rc;
}

int xen_irq_from_pirq(unsigned pirq)
{
	int irq;

	struct irq_info *info;

	mutex_lock(&irq_mapping_update_lock);

	list_for_each_entry(info, &xen_irq_list_head, list) {
		if (info->type != IRQT_PIRQ)
			continue;
		irq = info->irq;
		if (info->u.pirq.pirq == pirq)
			goto out;
	}
	irq = -1;
out:
	mutex_unlock(&irq_mapping_update_lock);

	return irq;
}


int xen_pirq_from_irq(unsigned irq)
{
	return pirq_from_irq(irq);
}
EXPORT_SYMBOL_GPL(xen_pirq_from_irq);
int bind_evtchn_to_irq(unsigned int evtchn)
{
	int irq;

	mutex_lock(&irq_mapping_update_lock);

	irq = evtchn_to_irq[evtchn];

	if (irq == -1) {
		irq = xen_allocate_irq_dynamic();
		if (irq < 0)
			goto out;

		irq_set_chip_and_handler_name(irq, &xen_dynamic_chip,
					      handle_edge_irq, "event");

		xen_irq_info_evtchn_init(irq, evtchn);
	} else {
		struct irq_info *info = info_for_irq(irq);
		WARN_ON(info == NULL || info->type != IRQT_EVTCHN);
	}

out:
	mutex_unlock(&irq_mapping_update_lock);

	return irq;
}
EXPORT_SYMBOL_GPL(bind_evtchn_to_irq);

static int bind_ipi_to_irq(unsigned int ipi, unsigned int cpu)
{
	struct evtchn_bind_ipi bind_ipi;
	int evtchn, irq;

	mutex_lock(&irq_mapping_update_lock);

	irq = per_cpu(ipi_to_irq, cpu)[ipi];

	if (irq == -1) {
		irq = xen_allocate_irq_dynamic();
		if (irq < 0)
			goto out;

		irq_set_chip_and_handler_name(irq, &xen_percpu_chip,
					      handle_percpu_irq, "ipi");

		bind_ipi.vcpu = cpu;
		if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_ipi,
						&bind_ipi) != 0)
			BUG();
		evtchn = bind_ipi.port;

		xen_irq_info_ipi_init(cpu, irq, evtchn, ipi);

		bind_evtchn_to_cpu(evtchn, cpu);
	} else {
		struct irq_info *info = info_for_irq(irq);
		WARN_ON(info == NULL || info->type != IRQT_IPI);
	}

 out:
	mutex_unlock(&irq_mapping_update_lock);
	return irq;
}

static int bind_interdomain_evtchn_to_irq(unsigned int remote_domain,
					  unsigned int remote_port)
{
	struct evtchn_bind_interdomain bind_interdomain;
	int err;

	bind_interdomain.remote_dom  = remote_domain;
	bind_interdomain.remote_port = remote_port;

	err = HYPERVISOR_event_channel_op(EVTCHNOP_bind_interdomain,
					  &bind_interdomain);

	return err ? : bind_evtchn_to_irq(bind_interdomain.local_port);
}

static int find_virq(unsigned int virq, unsigned int cpu)
{
	struct evtchn_status status;
	int port, rc = -ENOENT;

	memset(&status, 0, sizeof(status));
	for (port = 0; port <= NR_EVENT_CHANNELS; port++) {
		status.dom = DOMID_SELF;
		status.port = port;
		rc = HYPERVISOR_event_channel_op(EVTCHNOP_status, &status);
		if (rc < 0)
			continue;
		if (status.status != EVTCHNSTAT_virq)
			continue;
		if (status.u.virq == virq && status.vcpu == cpu) {
			rc = port;
			break;
		}
	}
	return rc;
}

int bind_virq_to_irq(unsigned int virq, unsigned int cpu)
{
	struct evtchn_bind_virq bind_virq;
	int evtchn, irq, ret;

	mutex_lock(&irq_mapping_update_lock);

	irq = per_cpu(virq_to_irq, cpu)[virq];

	if (irq == -1) {
		irq = xen_allocate_irq_dynamic();
		if (irq < 0)
			goto out;

		irq_set_chip_and_handler_name(irq, &xen_percpu_chip,
					      handle_percpu_irq, "virq");

		bind_virq.virq = virq;
		bind_virq.vcpu = cpu;
		ret = HYPERVISOR_event_channel_op(EVTCHNOP_bind_virq,
						&bind_virq);
		if (ret == 0)
			evtchn = bind_virq.port;
		else {
			if (ret == -EEXIST)
				ret = find_virq(virq, cpu);
			BUG_ON(ret < 0);
			evtchn = ret;
		}

		xen_irq_info_virq_init(cpu, irq, evtchn, virq);

		bind_evtchn_to_cpu(evtchn, cpu);
	} else {
		struct irq_info *info = info_for_irq(irq);
		WARN_ON(info == NULL || info->type != IRQT_VIRQ);
	}

out:
	mutex_unlock(&irq_mapping_update_lock);

	return irq;
}

static void unbind_from_irq(unsigned int irq)
{
	struct evtchn_close close;
	int evtchn = evtchn_from_irq(irq);
	struct irq_info *info = irq_get_handler_data(irq);

	if (WARN_ON(!info))
		return;

	mutex_lock(&irq_mapping_update_lock);

	if (info->refcnt > 0) {
		info->refcnt--;
		if (info->refcnt != 0)
			goto done;
	}

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

	BUG_ON(info_for_irq(irq)->type == IRQT_UNBOUND);

	xen_free_irq(irq);

 done:
	mutex_unlock(&irq_mapping_update_lock);
}

int bind_evtchn_to_irqhandler(unsigned int evtchn,
			      irq_handler_t handler,
			      unsigned long irqflags,
			      const char *devname, void *dev_id)
{
	int irq, retval;

	irq = bind_evtchn_to_irq(evtchn);
	if (irq < 0)
		return irq;
	retval = request_irq(irq, handler, irqflags, devname, dev_id);
	if (retval != 0) {
		unbind_from_irq(irq);
		return retval;
	}

	return irq;
}
EXPORT_SYMBOL_GPL(bind_evtchn_to_irqhandler);

int bind_interdomain_evtchn_to_irqhandler(unsigned int remote_domain,
					  unsigned int remote_port,
					  irq_handler_t handler,
					  unsigned long irqflags,
					  const char *devname,
					  void *dev_id)
{
	int irq, retval;

	irq = bind_interdomain_evtchn_to_irq(remote_domain, remote_port);
	if (irq < 0)
		return irq;

	retval = request_irq(irq, handler, irqflags, devname, dev_id);
	if (retval != 0) {
		unbind_from_irq(irq);
		return retval;
	}

	return irq;
}
EXPORT_SYMBOL_GPL(bind_interdomain_evtchn_to_irqhandler);

int bind_virq_to_irqhandler(unsigned int virq, unsigned int cpu,
			    irq_handler_t handler,
			    unsigned long irqflags, const char *devname, void *dev_id)
{
	int irq, retval;

	irq = bind_virq_to_irq(virq, cpu);
	if (irq < 0)
		return irq;
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

	irqflags |= IRQF_NO_SUSPEND | IRQF_FORCE_RESUME | IRQF_EARLY_RESUME;
	retval = request_irq(irq, handler, irqflags, devname, dev_id);
	if (retval != 0) {
		unbind_from_irq(irq);
		return retval;
	}

	return irq;
}

void unbind_from_irqhandler(unsigned int irq, void *dev_id)
{
	struct irq_info *info = irq_get_handler_data(irq);

	if (WARN_ON(!info))
		return;
	free_irq(irq, dev_id);
	unbind_from_irq(irq);
}
EXPORT_SYMBOL_GPL(unbind_from_irqhandler);

int evtchn_make_refcounted(unsigned int evtchn)
{
	int irq = evtchn_to_irq[evtchn];
	struct irq_info *info;

	if (irq == -1)
		return -ENOENT;

	info = irq_get_handler_data(irq);

	if (!info)
		return -ENOENT;

	WARN_ON(info->refcnt != -1);

	info->refcnt = 1;

	return 0;
}
EXPORT_SYMBOL_GPL(evtchn_make_refcounted);

int evtchn_get(unsigned int evtchn)
{
	int irq;
	struct irq_info *info;
	int err = -ENOENT;

	if (evtchn >= NR_EVENT_CHANNELS)
		return -EINVAL;

	mutex_lock(&irq_mapping_update_lock);

	irq = evtchn_to_irq[evtchn];
	if (irq == -1)
		goto done;

	info = irq_get_handler_data(irq);

	if (!info)
		goto done;

	err = -EINVAL;
	if (info->refcnt <= 0)
		goto done;

	info->refcnt++;
	err = 0;
 done:
	mutex_unlock(&irq_mapping_update_lock);

	return err;
}
EXPORT_SYMBOL_GPL(evtchn_get);

void evtchn_put(unsigned int evtchn)
{
	int irq = evtchn_to_irq[evtchn];
	if (WARN_ON(irq == -1))
		return;
	unbind_from_irq(irq);
}
EXPORT_SYMBOL_GPL(evtchn_put);

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
	xen_ulong_t *cpu_evtchn = per_cpu(cpu_evtchn_mask, cpu);
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
		printk("%d: masked=%d pending=%d event_sel %0*"PRI_xen_ulong"\n  ", i,
		       pending, v->evtchn_upcall_pending,
		       (int)(sizeof(v->evtchn_pending_sel)*2),
		       v->evtchn_pending_sel);
	}
	v = per_cpu(xen_vcpu, cpu);

	printk("\npending:\n   ");
	for (i = ARRAY_SIZE(sh->evtchn_pending)-1; i >= 0; i--)
		printk("%0*"PRI_xen_ulong"%s",
		       (int)sizeof(sh->evtchn_pending[0])*2,
		       sh->evtchn_pending[i],
		       i % 8 == 0 ? "\n   " : " ");
	printk("\nglobal mask:\n   ");
	for (i = ARRAY_SIZE(sh->evtchn_mask)-1; i >= 0; i--)
		printk("%0*"PRI_xen_ulong"%s",
		       (int)(sizeof(sh->evtchn_mask[0])*2),
		       sh->evtchn_mask[i],
		       i % 8 == 0 ? "\n   " : " ");

	printk("\nglobally unmasked:\n   ");
	for (i = ARRAY_SIZE(sh->evtchn_mask)-1; i >= 0; i--)
		printk("%0*"PRI_xen_ulong"%s",
		       (int)(sizeof(sh->evtchn_mask[0])*2),
		       sh->evtchn_pending[i] & ~sh->evtchn_mask[i],
		       i % 8 == 0 ? "\n   " : " ");

	printk("\nlocal cpu%d mask:\n   ", cpu);
	for (i = (NR_EVENT_CHANNELS/BITS_PER_EVTCHN_WORD)-1; i >= 0; i--)
		printk("%0*"PRI_xen_ulong"%s", (int)(sizeof(cpu_evtchn[0])*2),
		       cpu_evtchn[i],
		       i % 8 == 0 ? "\n   " : " ");

	printk("\nlocally unmasked:\n   ");
	for (i = ARRAY_SIZE(sh->evtchn_mask)-1; i >= 0; i--) {
		xen_ulong_t pending = sh->evtchn_pending[i]
			& ~sh->evtchn_mask[i]
			& cpu_evtchn[i];
		printk("%0*"PRI_xen_ulong"%s",
		       (int)(sizeof(sh->evtchn_mask[0])*2),
		       pending, i % 8 == 0 ? "\n   " : " ");
	}

	printk("\npending list:\n");
	for (i = 0; i < NR_EVENT_CHANNELS; i++) {
		if (sync_test_bit(i, BM(sh->evtchn_pending))) {
			int word_idx = i / BITS_PER_EVTCHN_WORD;
			printk("  %d: event %d -> irq %d%s%s%s\n",
			       cpu_from_evtchn(i), i,
			       evtchn_to_irq[i],
			       sync_test_bit(word_idx, BM(&v->evtchn_pending_sel))
					     ? "" : " l2-clear",
			       !sync_test_bit(i, BM(sh->evtchn_mask))
					     ? "" : " globally-masked",
			       sync_test_bit(i, BM(cpu_evtchn))
					     ? "" : " locally-masked");
		}
	}

	spin_unlock_irqrestore(&debug_lock, flags);

	return IRQ_HANDLED;
}

static DEFINE_PER_CPU(unsigned, xed_nesting_count);
static DEFINE_PER_CPU(unsigned int, current_word_idx);
static DEFINE_PER_CPU(unsigned int, current_bit_idx);

/*
 * Mask out the i least significant bits of w
 */
#define MASK_LSBS(w, i) (w & ((~((xen_ulong_t)0UL)) << i))

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
	int start_word_idx, start_bit_idx;
	int word_idx, bit_idx;
	int i, irq;
	int cpu = get_cpu();
	struct shared_info *s = HYPERVISOR_shared_info;
	struct vcpu_info *vcpu_info = __this_cpu_read(xen_vcpu);
	unsigned count;

	do {
		xen_ulong_t pending_words;
		xen_ulong_t pending_bits;
		struct irq_desc *desc;

		vcpu_info->evtchn_upcall_pending = 0;

		if (__this_cpu_inc_return(xed_nesting_count) - 1)
			goto out;

		/*
		 * Master flag must be cleared /before/ clearing
		 * selector flag. xchg_xen_ulong must contain an
		 * appropriate barrier.
		 */
		if ((irq = per_cpu(virq_to_irq, cpu)[VIRQ_TIMER]) != -1) {
			int evtchn = evtchn_from_irq(irq);
			word_idx = evtchn / BITS_PER_LONG;
			pending_bits = evtchn % BITS_PER_LONG;
			if (active_evtchns(cpu, s, word_idx) & (1ULL << pending_bits)) {
				desc = irq_to_desc(irq);
				if (desc)
					generic_handle_irq_desc(irq, desc);
			}
		}

		pending_words = xchg_xen_ulong(&vcpu_info->evtchn_pending_sel, 0);

		start_word_idx = __this_cpu_read(current_word_idx);
		start_bit_idx = __this_cpu_read(current_bit_idx);

		word_idx = start_word_idx;

		for (i = 0; pending_words != 0; i++) {
			xen_ulong_t words;

			words = MASK_LSBS(pending_words, word_idx);

			/*
			 * If we masked out all events, wrap to beginning.
			 */
			if (words == 0) {
				word_idx = 0;
				bit_idx = 0;
				continue;
			}
			word_idx = EVTCHN_FIRST_BIT(words);

			pending_bits = active_evtchns(cpu, s, word_idx);
			bit_idx = 0; /* usually scan entire word from start */
			if (word_idx == start_word_idx) {
				/* We scan the starting word in two parts */
				if (i == 0)
					/* 1st time: start in the middle */
					bit_idx = start_bit_idx;
				else
					/* 2nd time: mask bits done already */
					bit_idx &= (1UL << start_bit_idx) - 1;
			}

			do {
				xen_ulong_t bits;
				int port;

				bits = MASK_LSBS(pending_bits, bit_idx);

				/* If we masked out all events, move on. */
				if (bits == 0)
					break;

				bit_idx = EVTCHN_FIRST_BIT(bits);

				/* Process port. */
				port = (word_idx * BITS_PER_EVTCHN_WORD) + bit_idx;
				irq = evtchn_to_irq[port];

				if (irq != -1) {
					desc = irq_to_desc(irq);
					if (desc)
						generic_handle_irq_desc(irq, desc);
				}

				bit_idx = (bit_idx + 1) % BITS_PER_EVTCHN_WORD;

				/* Next caller starts at last processed + 1 */
				__this_cpu_write(current_word_idx,
						 bit_idx ? word_idx :
						 (word_idx+1) % BITS_PER_EVTCHN_WORD);
				__this_cpu_write(current_bit_idx, bit_idx);
			} while (bit_idx != 0);

			/* Scan start_l1i twice; all others once. */
			if ((word_idx != start_word_idx) || (i != 0))
				pending_words &= ~(1UL << word_idx);

			word_idx = (word_idx + 1) % BITS_PER_EVTCHN_WORD;
		}

		BUG_ON(!irqs_disabled());

		count = __this_cpu_read(xed_nesting_count);
		__this_cpu_write(xed_nesting_count, 0);
	} while (count != 1 || vcpu_info->evtchn_upcall_pending);

out:

	put_cpu();
}

void xen_evtchn_do_upcall(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	irq_enter();
#ifdef CONFIG_X86
	exit_idle();
#endif

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

	if (WARN_ON(!info))
		return;

	/* Make sure the irq is masked, since the new event channel
	   will also be masked. */
	disable_irq(irq);

	mutex_lock(&irq_mapping_update_lock);

	/* After resume the irq<->evtchn mappings are all cleared out */
	BUG_ON(evtchn_to_irq[evtchn] != -1);
	/* Expect irq to have been bound before,
	   so there should be a proper type */
	BUG_ON(info->type == IRQT_UNBOUND);

	xen_irq_info_evtchn_init(irq, evtchn);

	mutex_unlock(&irq_mapping_update_lock);

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

	if (!VALID_EVTCHN(evtchn))
		return -1;

	/*
	 * Events delivered via platform PCI interrupts are always
	 * routed to vcpu 0 and hence cannot be rebound.
	 */
	if (xen_hvm_domain() && !xen_have_vector_callback)
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

static int set_affinity_irq(struct irq_data *data, const struct cpumask *dest,
			    bool force)
{
	unsigned tcpu = cpumask_first(dest);

	return rebind_irq_to_cpu(data->irq, tcpu);
}

int resend_irq_on_evtchn(unsigned int irq)
{
	int masked, evtchn = evtchn_from_irq(irq);
	struct shared_info *s = HYPERVISOR_shared_info;

	if (!VALID_EVTCHN(evtchn))
		return 1;

	masked = sync_test_and_set_bit(evtchn, BM(s->evtchn_mask));
	sync_set_bit(evtchn, BM(s->evtchn_pending));
	if (!masked)
		unmask_evtchn(evtchn);

	return 1;
}

static void enable_dynirq(struct irq_data *data)
{
	int evtchn = evtchn_from_irq(data->irq);

	if (VALID_EVTCHN(evtchn))
		unmask_evtchn(evtchn);
}

static void disable_dynirq(struct irq_data *data)
{
	int evtchn = evtchn_from_irq(data->irq);

	if (VALID_EVTCHN(evtchn))
		mask_evtchn(evtchn);
}

static void ack_dynirq(struct irq_data *data)
{
	int evtchn = evtchn_from_irq(data->irq);

	irq_move_irq(data);

	if (VALID_EVTCHN(evtchn))
		clear_evtchn(evtchn);
}

static void mask_ack_dynirq(struct irq_data *data)
{
	disable_dynirq(data);
	ack_dynirq(data);
}

static int retrigger_dynirq(struct irq_data *data)
{
	int evtchn = evtchn_from_irq(data->irq);
	struct shared_info *sh = HYPERVISOR_shared_info;
	int ret = 0;

	if (VALID_EVTCHN(evtchn)) {
		int masked;

		masked = sync_test_and_set_bit(evtchn, BM(sh->evtchn_mask));
		sync_set_bit(evtchn, BM(sh->evtchn_pending));
		if (!masked)
			unmask_evtchn(evtchn);
		ret = 1;
	}

	return ret;
}

static void restore_pirqs(void)
{
	int pirq, rc, irq, gsi;
	struct physdev_map_pirq map_irq;
	struct irq_info *info;

	list_for_each_entry(info, &xen_irq_list_head, list) {
		if (info->type != IRQT_PIRQ)
			continue;

		pirq = info->u.pirq.pirq;
		gsi = info->u.pirq.gsi;
		irq = info->irq;

		/* save/restore of PT devices doesn't work, so at this point the
		 * only devices present are GSI based emulated devices */
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
			xen_free_irq(irq);
			continue;
		}

		printk(KERN_DEBUG "xen: --> irq=%d, pirq=%d\n", irq, map_irq.pirq);

		__startup_pirq(irq);
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
		xen_irq_info_virq_init(cpu, irq, evtchn, virq);
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
		xen_irq_info_ipi_init(cpu, irq, evtchn, ipi);
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

/* Check whether the IRQ line is shared with other guests. */
int xen_test_irq_shared(int irq)
{
	struct irq_info *info = info_for_irq(irq);
	struct physdev_irq_status_query irq_status;

	if (WARN_ON(!info))
		return -ENOENT;

	irq_status.irq = info->u.pirq.pirq;

	if (HYPERVISOR_physdev_op(PHYSDEVOP_irq_status_query, &irq_status))
		return 0;
	return !(irq_status.flags & XENIRQSTAT_shared);
}
EXPORT_SYMBOL_GPL(xen_test_irq_shared);

void xen_irq_resume(void)
{
	unsigned int cpu, evtchn;
	struct irq_info *info;

	init_evtchn_cpu_bindings();

	/* New event-channel space is not 'live' yet. */
	for (evtchn = 0; evtchn < NR_EVENT_CHANNELS; evtchn++)
		mask_evtchn(evtchn);

	/* No IRQ <-> event-channel mappings. */
	list_for_each_entry(info, &xen_irq_list_head, list)
		info->evtchn = 0; /* zap event-channel binding */

	for (evtchn = 0; evtchn < NR_EVENT_CHANNELS; evtchn++)
		evtchn_to_irq[evtchn] = -1;

	for_each_possible_cpu(cpu) {
		restore_cpu_virqs(cpu);
		restore_cpu_ipis(cpu);
	}

	restore_pirqs();
}

static struct irq_chip xen_dynamic_chip __read_mostly = {
	.name			= "xen-dyn",

	.irq_disable		= disable_dynirq,
	.irq_mask		= disable_dynirq,
	.irq_unmask		= enable_dynirq,

	.irq_ack		= ack_dynirq,
	.irq_mask_ack		= mask_ack_dynirq,

	.irq_set_affinity	= set_affinity_irq,
	.irq_retrigger		= retrigger_dynirq,
};

static struct irq_chip xen_pirq_chip __read_mostly = {
	.name			= "xen-pirq",

	.irq_startup		= startup_pirq,
	.irq_shutdown		= shutdown_pirq,
	.irq_enable		= enable_pirq,
	.irq_disable		= disable_pirq,

	.irq_mask		= disable_dynirq,
	.irq_unmask		= enable_dynirq,

	.irq_ack		= eoi_pirq,
	.irq_eoi		= eoi_pirq,
	.irq_mask_ack		= mask_ack_pirq,

	.irq_set_affinity	= set_affinity_irq,

	.irq_retrigger		= retrigger_dynirq,
};

static struct irq_chip xen_percpu_chip __read_mostly = {
	.name			= "xen-percpu",

	.irq_disable		= disable_dynirq,
	.irq_mask		= disable_dynirq,
	.irq_unmask		= enable_dynirq,

	.irq_ack		= ack_dynirq,
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
		callback_via = HVM_CALLBACK_VECTOR(HYPERVISOR_CALLBACK_VECTOR);
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
		if (!test_bit(HYPERVISOR_CALLBACK_VECTOR, used_vectors))
			alloc_intr_gate(HYPERVISOR_CALLBACK_VECTOR,
					xen_hvm_callback_vector);
	}
}
#else
void xen_callback_vector(void) {}
#endif

void __init xen_init_IRQ(void)
{
	int i;

	evtchn_to_irq = kcalloc(NR_EVENT_CHANNELS, sizeof(*evtchn_to_irq),
				    GFP_KERNEL);
	BUG_ON(!evtchn_to_irq);
	for (i = 0; i < NR_EVENT_CHANNELS; i++)
		evtchn_to_irq[i] = -1;

	init_evtchn_cpu_bindings();

	/* No event channels are 'live' right now. */
	for (i = 0; i < NR_EVENT_CHANNELS; i++)
		mask_evtchn(i);

	pirq_needs_eoi = pirq_needs_eoi_flag;

#ifdef CONFIG_X86
	if (xen_hvm_domain()) {
		xen_callback_vector();
		native_init_IRQ();
		/* pci_xen_hvm_init must be called after native_init_IRQ so that
		 * __acpi_register_gsi can point at the right function */
		pci_xen_hvm_init();
	} else {
		int rc;
		struct physdev_pirq_eoi_gmfn eoi_gmfn;

		irq_ctx_init(smp_processor_id());
		if (xen_initial_domain())
			pci_xen_initial_domain();

		pirq_eoi_map = (void *)__get_free_page(GFP_KERNEL|__GFP_ZERO);
		eoi_gmfn.gmfn = virt_to_mfn(pirq_eoi_map);
		rc = HYPERVISOR_physdev_op(PHYSDEVOP_pirq_eoi_gmfn_v2, &eoi_gmfn);
		if (rc != 0) {
			free_page((unsigned long) pirq_eoi_map);
			pirq_eoi_map = NULL;
		} else
			pirq_needs_eoi = pirq_check_eoi_map;
	}
#endif
}
