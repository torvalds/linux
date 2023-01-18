// SPDX-License-Identifier: GPL-2.0-only
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

#define pr_fmt(fmt) "xen:" KBUILD_MODNAME ": " fmt

#include <linux/linkage.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/moduleparam.h>
#include <linux/string.h>
#include <linux/memblock.h>
#include <linux/slab.h>
#include <linux/irqnr.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/cpuhotplug.h>
#include <linux/atomic.h>
#include <linux/ktime.h>

#ifdef CONFIG_X86
#include <asm/desc.h>
#include <asm/ptrace.h>
#include <asm/idtentry.h>
#include <asm/irq.h>
#include <asm/io_apic.h>
#include <asm/i8259.h>
#include <asm/xen/cpuid.h>
#include <asm/xen/pci.h>
#endif
#include <asm/sync_bitops.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/hypervisor.h>
#include <xen/page.h>

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
#include <xen/interface/vcpu.h>
#include <xen/xenbus.h>
#include <asm/hw_irq.h>

#include "events_internal.h"

#undef MODULE_PARAM_PREFIX
#define MODULE_PARAM_PREFIX "xen."

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
struct irq_info {
	struct list_head list;
	struct list_head eoi_list;
	short refcnt;
	u8 spurious_cnt;
	u8 is_accounted;
	short type;		/* type: IRQT_* */
	u8 mask_reason;		/* Why is event channel masked */
#define EVT_MASK_REASON_EXPLICIT	0x01
#define EVT_MASK_REASON_TEMPORARY	0x02
#define EVT_MASK_REASON_EOI_PENDING	0x04
	u8 is_active;		/* Is event just being handled? */
	unsigned irq;
	evtchn_port_t evtchn;   /* event channel */
	unsigned short cpu;     /* cpu bound */
	unsigned short eoi_cpu; /* EOI must happen on this cpu-1 */
	unsigned int irq_epoch; /* If eoi_cpu valid: irq_epoch of event */
	u64 eoi_time;           /* Time in jiffies when to EOI. */
	raw_spinlock_t lock;

	union {
		unsigned short virq;
		enum ipi_vector ipi;
		struct {
			unsigned short pirq;
			unsigned short gsi;
			unsigned char vector;
			unsigned char flags;
			uint16_t domid;
		} pirq;
		struct xenbus_device *interdomain;
	} u;
};

#define PIRQ_NEEDS_EOI	(1 << 0)
#define PIRQ_SHAREABLE	(1 << 1)
#define PIRQ_MSI_GROUP	(1 << 2)

static uint __read_mostly event_loop_timeout = 2;
module_param(event_loop_timeout, uint, 0644);

static uint __read_mostly event_eoi_delay = 10;
module_param(event_eoi_delay, uint, 0644);

const struct evtchn_ops *evtchn_ops;

/*
 * This lock protects updates to the following mapping and reference-count
 * arrays. The lock does not need to be acquired to read the mapping tables.
 */
static DEFINE_MUTEX(irq_mapping_update_lock);

/*
 * Lock protecting event handling loop against removing event channels.
 * Adding of event channels is no issue as the associated IRQ becomes active
 * only after everything is setup (before request_[threaded_]irq() the handler
 * can't be entered for an event, as the event channel will be unmasked only
 * then).
 */
static DEFINE_RWLOCK(evtchn_rwlock);

/*
 * Lock hierarchy:
 *
 * irq_mapping_update_lock
 *   evtchn_rwlock
 *     IRQ-desc lock
 *       percpu eoi_list_lock
 *         irq_info->lock
 */

static LIST_HEAD(xen_irq_list_head);

/* IRQ <-> VIRQ mapping. */
static DEFINE_PER_CPU(int [NR_VIRQS], virq_to_irq) = {[0 ... NR_VIRQS-1] = -1};

/* IRQ <-> IPI mapping */
static DEFINE_PER_CPU(int [XEN_NR_IPIS], ipi_to_irq) = {[0 ... XEN_NR_IPIS-1] = -1};

/* Event channel distribution data */
static atomic_t channels_on_cpu[NR_CPUS];

static int **evtchn_to_irq;
#ifdef CONFIG_X86
static unsigned long *pirq_eoi_map;
#endif
static bool (*pirq_needs_eoi)(unsigned irq);

#define EVTCHN_ROW(e)  (e / (PAGE_SIZE/sizeof(**evtchn_to_irq)))
#define EVTCHN_COL(e)  (e % (PAGE_SIZE/sizeof(**evtchn_to_irq)))
#define EVTCHN_PER_ROW (PAGE_SIZE / sizeof(**evtchn_to_irq))

/* Xen will never allocate port zero for any purpose. */
#define VALID_EVTCHN(chn)	((chn) != 0)

static struct irq_info *legacy_info_ptrs[NR_IRQS_LEGACY];

static struct irq_chip xen_dynamic_chip;
static struct irq_chip xen_lateeoi_chip;
static struct irq_chip xen_percpu_chip;
static struct irq_chip xen_pirq_chip;
static void enable_dynirq(struct irq_data *data);
static void disable_dynirq(struct irq_data *data);

static DEFINE_PER_CPU(unsigned int, irq_epoch);

static void clear_evtchn_to_irq_row(int *evtchn_row)
{
	unsigned col;

	for (col = 0; col < EVTCHN_PER_ROW; col++)
		WRITE_ONCE(evtchn_row[col], -1);
}

static void clear_evtchn_to_irq_all(void)
{
	unsigned row;

	for (row = 0; row < EVTCHN_ROW(xen_evtchn_max_channels()); row++) {
		if (evtchn_to_irq[row] == NULL)
			continue;
		clear_evtchn_to_irq_row(evtchn_to_irq[row]);
	}
}

static int set_evtchn_to_irq(evtchn_port_t evtchn, unsigned int irq)
{
	unsigned row;
	unsigned col;
	int *evtchn_row;

	if (evtchn >= xen_evtchn_max_channels())
		return -EINVAL;

	row = EVTCHN_ROW(evtchn);
	col = EVTCHN_COL(evtchn);

	if (evtchn_to_irq[row] == NULL) {
		/* Unallocated irq entries return -1 anyway */
		if (irq == -1)
			return 0;

		evtchn_row = (int *) __get_free_pages(GFP_KERNEL, 0);
		if (evtchn_row == NULL)
			return -ENOMEM;

		clear_evtchn_to_irq_row(evtchn_row);

		/*
		 * We've prepared an empty row for the mapping. If a different
		 * thread was faster inserting it, we can drop ours.
		 */
		if (cmpxchg(&evtchn_to_irq[row], NULL, evtchn_row) != NULL)
			free_page((unsigned long) evtchn_row);
	}

	WRITE_ONCE(evtchn_to_irq[row][col], irq);
	return 0;
}

int get_evtchn_to_irq(evtchn_port_t evtchn)
{
	if (evtchn >= xen_evtchn_max_channels())
		return -1;
	if (evtchn_to_irq[EVTCHN_ROW(evtchn)] == NULL)
		return -1;
	return READ_ONCE(evtchn_to_irq[EVTCHN_ROW(evtchn)][EVTCHN_COL(evtchn)]);
}

/* Get info for IRQ */
static struct irq_info *info_for_irq(unsigned irq)
{
	if (irq < nr_legacy_irqs())
		return legacy_info_ptrs[irq];
	else
		return irq_get_chip_data(irq);
}

static void set_info_for_irq(unsigned int irq, struct irq_info *info)
{
	if (irq < nr_legacy_irqs())
		legacy_info_ptrs[irq] = info;
	else
		irq_set_chip_data(irq, info);
}

/* Per CPU channel accounting */
static void channels_on_cpu_dec(struct irq_info *info)
{
	if (!info->is_accounted)
		return;

	info->is_accounted = 0;

	if (WARN_ON_ONCE(info->cpu >= nr_cpu_ids))
		return;

	WARN_ON_ONCE(!atomic_add_unless(&channels_on_cpu[info->cpu], -1 , 0));
}

static void channels_on_cpu_inc(struct irq_info *info)
{
	if (WARN_ON_ONCE(info->cpu >= nr_cpu_ids))
		return;

	if (WARN_ON_ONCE(!atomic_add_unless(&channels_on_cpu[info->cpu], 1,
					    INT_MAX)))
		return;

	info->is_accounted = 1;
}

/* Constructors for packed IRQ information. */
static int xen_irq_info_common_setup(struct irq_info *info,
				     unsigned irq,
				     enum xen_irq_type type,
				     evtchn_port_t evtchn,
				     unsigned short cpu)
{
	int ret;

	BUG_ON(info->type != IRQT_UNBOUND && info->type != type);

	info->type = type;
	info->irq = irq;
	info->evtchn = evtchn;
	info->cpu = cpu;
	info->mask_reason = EVT_MASK_REASON_EXPLICIT;
	raw_spin_lock_init(&info->lock);

	ret = set_evtchn_to_irq(evtchn, irq);
	if (ret < 0)
		return ret;

	irq_clear_status_flags(irq, IRQ_NOREQUEST|IRQ_NOAUTOEN);

	return xen_evtchn_port_setup(evtchn);
}

static int xen_irq_info_evtchn_setup(unsigned irq,
				     evtchn_port_t evtchn,
				     struct xenbus_device *dev)
{
	struct irq_info *info = info_for_irq(irq);
	int ret;

	ret = xen_irq_info_common_setup(info, irq, IRQT_EVTCHN, evtchn, 0);
	info->u.interdomain = dev;
	if (dev)
		atomic_inc(&dev->event_channels);

	return ret;
}

static int xen_irq_info_ipi_setup(unsigned cpu,
				  unsigned irq,
				  evtchn_port_t evtchn,
				  enum ipi_vector ipi)
{
	struct irq_info *info = info_for_irq(irq);

	info->u.ipi = ipi;

	per_cpu(ipi_to_irq, cpu)[ipi] = irq;

	return xen_irq_info_common_setup(info, irq, IRQT_IPI, evtchn, 0);
}

static int xen_irq_info_virq_setup(unsigned cpu,
				   unsigned irq,
				   evtchn_port_t evtchn,
				   unsigned virq)
{
	struct irq_info *info = info_for_irq(irq);

	info->u.virq = virq;

	per_cpu(virq_to_irq, cpu)[virq] = irq;

	return xen_irq_info_common_setup(info, irq, IRQT_VIRQ, evtchn, 0);
}

static int xen_irq_info_pirq_setup(unsigned irq,
				   evtchn_port_t evtchn,
				   unsigned pirq,
				   unsigned gsi,
				   uint16_t domid,
				   unsigned char flags)
{
	struct irq_info *info = info_for_irq(irq);

	info->u.pirq.pirq = pirq;
	info->u.pirq.gsi = gsi;
	info->u.pirq.domid = domid;
	info->u.pirq.flags = flags;

	return xen_irq_info_common_setup(info, irq, IRQT_PIRQ, evtchn, 0);
}

static void xen_irq_info_cleanup(struct irq_info *info)
{
	set_evtchn_to_irq(info->evtchn, -1);
	xen_evtchn_port_remove(info->evtchn, info->cpu);
	info->evtchn = 0;
	channels_on_cpu_dec(info);
}

/*
 * Accessors for packed IRQ information.
 */
evtchn_port_t evtchn_from_irq(unsigned irq)
{
	const struct irq_info *info = NULL;

	if (likely(irq < nr_irqs))
		info = info_for_irq(irq);
	if (!info)
		return 0;

	return info->evtchn;
}

unsigned int irq_from_evtchn(evtchn_port_t evtchn)
{
	return get_evtchn_to_irq(evtchn);
}
EXPORT_SYMBOL_GPL(irq_from_evtchn);

int irq_from_virq(unsigned int cpu, unsigned int virq)
{
	return per_cpu(virq_to_irq, cpu)[virq];
}

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

unsigned int cpu_from_evtchn(evtchn_port_t evtchn)
{
	int irq = get_evtchn_to_irq(evtchn);
	unsigned ret = 0;

	if (irq != -1)
		ret = cpu_from_irq(irq);

	return ret;
}

static void do_mask(struct irq_info *info, u8 reason)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&info->lock, flags);

	if (!info->mask_reason)
		mask_evtchn(info->evtchn);

	info->mask_reason |= reason;

	raw_spin_unlock_irqrestore(&info->lock, flags);
}

static void do_unmask(struct irq_info *info, u8 reason)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&info->lock, flags);

	info->mask_reason &= ~reason;

	if (!info->mask_reason)
		unmask_evtchn(info->evtchn);

	raw_spin_unlock_irqrestore(&info->lock, flags);
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

static void bind_evtchn_to_cpu(evtchn_port_t evtchn, unsigned int cpu,
			       bool force_affinity)
{
	int irq = get_evtchn_to_irq(evtchn);
	struct irq_info *info = info_for_irq(irq);

	BUG_ON(irq == -1);

	if (IS_ENABLED(CONFIG_SMP) && force_affinity) {
		struct irq_data *data = irq_get_irq_data(irq);

		irq_data_update_affinity(data, cpumask_of(cpu));
		irq_data_update_effective_affinity(data, cpumask_of(cpu));
	}

	xen_evtchn_port_bind_to_cpu(evtchn, cpu, info->cpu);

	channels_on_cpu_dec(info);
	info->cpu = cpu;
	channels_on_cpu_inc(info);
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
	evtchn_port_t evtchn = evtchn_from_irq(irq);

	if (VALID_EVTCHN(evtchn))
		notify_remote_via_evtchn(evtchn);
}
EXPORT_SYMBOL_GPL(notify_remote_via_irq);

struct lateeoi_work {
	struct delayed_work delayed;
	spinlock_t eoi_list_lock;
	struct list_head eoi_list;
};

static DEFINE_PER_CPU(struct lateeoi_work, lateeoi);

static void lateeoi_list_del(struct irq_info *info)
{
	struct lateeoi_work *eoi = &per_cpu(lateeoi, info->eoi_cpu);
	unsigned long flags;

	spin_lock_irqsave(&eoi->eoi_list_lock, flags);
	list_del_init(&info->eoi_list);
	spin_unlock_irqrestore(&eoi->eoi_list_lock, flags);
}

static void lateeoi_list_add(struct irq_info *info)
{
	struct lateeoi_work *eoi = &per_cpu(lateeoi, info->eoi_cpu);
	struct irq_info *elem;
	u64 now = get_jiffies_64();
	unsigned long delay;
	unsigned long flags;

	if (now < info->eoi_time)
		delay = info->eoi_time - now;
	else
		delay = 1;

	spin_lock_irqsave(&eoi->eoi_list_lock, flags);

	if (list_empty(&eoi->eoi_list)) {
		list_add(&info->eoi_list, &eoi->eoi_list);
		mod_delayed_work_on(info->eoi_cpu, system_wq,
				    &eoi->delayed, delay);
	} else {
		list_for_each_entry_reverse(elem, &eoi->eoi_list, eoi_list) {
			if (elem->eoi_time <= info->eoi_time)
				break;
		}
		list_add(&info->eoi_list, &elem->eoi_list);
	}

	spin_unlock_irqrestore(&eoi->eoi_list_lock, flags);
}

static void xen_irq_lateeoi_locked(struct irq_info *info, bool spurious)
{
	evtchn_port_t evtchn;
	unsigned int cpu;
	unsigned int delay = 0;

	evtchn = info->evtchn;
	if (!VALID_EVTCHN(evtchn) || !list_empty(&info->eoi_list))
		return;

	if (spurious) {
		struct xenbus_device *dev = info->u.interdomain;
		unsigned int threshold = 1;

		if (dev && dev->spurious_threshold)
			threshold = dev->spurious_threshold;

		if ((1 << info->spurious_cnt) < (HZ << 2)) {
			if (info->spurious_cnt != 0xFF)
				info->spurious_cnt++;
		}
		if (info->spurious_cnt > threshold) {
			delay = 1 << (info->spurious_cnt - 1 - threshold);
			if (delay > HZ)
				delay = HZ;
			if (!info->eoi_time)
				info->eoi_cpu = smp_processor_id();
			info->eoi_time = get_jiffies_64() + delay;
			if (dev)
				atomic_add(delay, &dev->jiffies_eoi_delayed);
		}
		if (dev)
			atomic_inc(&dev->spurious_events);
	} else {
		info->spurious_cnt = 0;
	}

	cpu = info->eoi_cpu;
	if (info->eoi_time &&
	    (info->irq_epoch == per_cpu(irq_epoch, cpu) || delay)) {
		lateeoi_list_add(info);
		return;
	}

	info->eoi_time = 0;

	/* is_active hasn't been reset yet, do it now. */
	smp_store_release(&info->is_active, 0);
	do_unmask(info, EVT_MASK_REASON_EOI_PENDING);
}

static void xen_irq_lateeoi_worker(struct work_struct *work)
{
	struct lateeoi_work *eoi;
	struct irq_info *info;
	u64 now = get_jiffies_64();
	unsigned long flags;

	eoi = container_of(to_delayed_work(work), struct lateeoi_work, delayed);

	read_lock_irqsave(&evtchn_rwlock, flags);

	while (true) {
		spin_lock(&eoi->eoi_list_lock);

		info = list_first_entry_or_null(&eoi->eoi_list, struct irq_info,
						eoi_list);

		if (info == NULL || now < info->eoi_time) {
			spin_unlock(&eoi->eoi_list_lock);
			break;
		}

		list_del_init(&info->eoi_list);

		spin_unlock(&eoi->eoi_list_lock);

		info->eoi_time = 0;

		xen_irq_lateeoi_locked(info, false);
	}

	if (info)
		mod_delayed_work_on(info->eoi_cpu, system_wq,
				    &eoi->delayed, info->eoi_time - now);

	read_unlock_irqrestore(&evtchn_rwlock, flags);
}

static void xen_cpu_init_eoi(unsigned int cpu)
{
	struct lateeoi_work *eoi = &per_cpu(lateeoi, cpu);

	INIT_DELAYED_WORK(&eoi->delayed, xen_irq_lateeoi_worker);
	spin_lock_init(&eoi->eoi_list_lock);
	INIT_LIST_HEAD(&eoi->eoi_list);
}

void xen_irq_lateeoi(unsigned int irq, unsigned int eoi_flags)
{
	struct irq_info *info;
	unsigned long flags;

	read_lock_irqsave(&evtchn_rwlock, flags);

	info = info_for_irq(irq);

	if (info)
		xen_irq_lateeoi_locked(info, eoi_flags & XEN_EOI_FLAG_SPURIOUS);

	read_unlock_irqrestore(&evtchn_rwlock, flags);
}
EXPORT_SYMBOL_GPL(xen_irq_lateeoi);

static void xen_irq_init(unsigned irq)
{
	struct irq_info *info;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (info == NULL)
		panic("Unable to allocate metadata for IRQ%d\n", irq);

	info->type = IRQT_UNBOUND;
	info->refcnt = -1;

	set_info_for_irq(irq, info);
	/*
	 * Interrupt affinity setting can be immediate. No point
	 * in delaying it until an interrupt is handled.
	 */
	irq_set_status_flags(irq, IRQ_MOVE_PCNTXT);

	INIT_LIST_HEAD(&info->eoi_list);
	list_add_tail(&info->list, &xen_irq_list_head);
}

static int __must_check xen_allocate_irqs_dynamic(int nvec)
{
	int i, irq = irq_alloc_descs(-1, 0, nvec, -1);

	if (irq >= 0) {
		for (i = 0; i < nvec; i++)
			xen_irq_init(irq + i);
	}

	return irq;
}

static inline int __must_check xen_allocate_irq_dynamic(void)
{

	return xen_allocate_irqs_dynamic(1);
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
	if (gsi < nr_legacy_irqs())
		irq = gsi;
	else
		irq = irq_alloc_desc_at(gsi, -1);

	xen_irq_init(irq);

	return irq;
}

static void xen_free_irq(unsigned irq)
{
	struct irq_info *info = info_for_irq(irq);
	unsigned long flags;

	if (WARN_ON(!info))
		return;

	write_lock_irqsave(&evtchn_rwlock, flags);

	if (!list_empty(&info->eoi_list))
		lateeoi_list_del(info);

	list_del(&info->list);

	set_info_for_irq(irq, NULL);

	WARN_ON(info->refcnt > 0);

	write_unlock_irqrestore(&evtchn_rwlock, flags);

	kfree(info);

	/* Legacy IRQ descriptors are managed by the arch. */
	if (irq < nr_legacy_irqs())
		return;

	irq_free_desc(irq);
}

static void xen_evtchn_close(evtchn_port_t port)
{
	struct evtchn_close close;

	close.port = port;
	if (HYPERVISOR_event_channel_op(EVTCHNOP_close, &close) != 0)
		BUG();
}

/* Not called for lateeoi events. */
static void event_handler_exit(struct irq_info *info)
{
	smp_store_release(&info->is_active, 0);
	clear_evtchn(info->evtchn);
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

static void eoi_pirq(struct irq_data *data)
{
	struct irq_info *info = info_for_irq(data->irq);
	evtchn_port_t evtchn = info ? info->evtchn : 0;
	struct physdev_eoi eoi = { .irq = pirq_from_irq(data->irq) };
	int rc = 0;

	if (!VALID_EVTCHN(evtchn))
		return;

	event_handler_exit(info);

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
	evtchn_port_t evtchn = evtchn_from_irq(irq);
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
		pr_warn("Failed to obtain physical IRQ %d\n", irq);
		return 0;
	}
	evtchn = bind_pirq.port;

	pirq_query_unmask(irq);

	rc = set_evtchn_to_irq(evtchn, irq);
	if (rc)
		goto err;

	info->evtchn = evtchn;
	bind_evtchn_to_cpu(evtchn, 0, false);

	rc = xen_evtchn_port_setup(evtchn);
	if (rc)
		goto err;

out:
	do_unmask(info, EVT_MASK_REASON_EXPLICIT);

	eoi_pirq(irq_get_irq_data(irq));

	return 0;

err:
	pr_err("irq%d: Failed to set port to irq mapping (%d)\n", irq, rc);
	xen_evtchn_close(evtchn);
	return 0;
}

static unsigned int startup_pirq(struct irq_data *data)
{
	return __startup_pirq(data->irq);
}

static void shutdown_pirq(struct irq_data *data)
{
	unsigned int irq = data->irq;
	struct irq_info *info = info_for_irq(irq);
	evtchn_port_t evtchn = evtchn_from_irq(irq);

	BUG_ON(info->type != IRQT_PIRQ);

	if (!VALID_EVTCHN(evtchn))
		return;

	do_mask(info, EVT_MASK_REASON_EXPLICIT);
	xen_evtchn_close(evtchn);
	xen_irq_info_cleanup(info);
}

static void enable_pirq(struct irq_data *data)
{
	enable_dynirq(data);
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

static void __unbind_from_irq(unsigned int irq)
{
	evtchn_port_t evtchn = evtchn_from_irq(irq);
	struct irq_info *info = info_for_irq(irq);

	if (info->refcnt > 0) {
		info->refcnt--;
		if (info->refcnt != 0)
			return;
	}

	if (VALID_EVTCHN(evtchn)) {
		unsigned int cpu = cpu_from_irq(irq);
		struct xenbus_device *dev;

		xen_evtchn_close(evtchn);

		switch (type_from_irq(irq)) {
		case IRQT_VIRQ:
			per_cpu(virq_to_irq, cpu)[virq_from_irq(irq)] = -1;
			break;
		case IRQT_IPI:
			per_cpu(ipi_to_irq, cpu)[ipi_from_irq(irq)] = -1;
			break;
		case IRQT_EVTCHN:
			dev = info->u.interdomain;
			if (dev)
				atomic_dec(&dev->event_channels);
			break;
		default:
			break;
		}

		xen_irq_info_cleanup(info);
	}

	xen_free_irq(irq);
}

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
	int irq;
	struct physdev_irq irq_op;
	int ret;

	mutex_lock(&irq_mapping_update_lock);

	irq = xen_irq_from_gsi(gsi);
	if (irq != -1) {
		pr_info("%s: returning irq %d for gsi %u\n",
			__func__, irq, gsi);
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

	ret = xen_irq_info_pirq_setup(irq, 0, pirq, gsi, DOMID_SELF,
			       shareable ? PIRQ_SHAREABLE : 0);
	if (ret < 0) {
		__unbind_from_irq(irq);
		irq = ret;
		goto out;
	}

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
			     int pirq, int nvec, const char *name, domid_t domid)
{
	int i, irq, ret;

	mutex_lock(&irq_mapping_update_lock);

	irq = xen_allocate_irqs_dynamic(nvec);
	if (irq < 0)
		goto out;

	for (i = 0; i < nvec; i++) {
		irq_set_chip_and_handler_name(irq + i, &xen_pirq_chip, handle_edge_irq, name);

		ret = xen_irq_info_pirq_setup(irq + i, 0, pirq + i, 0, domid,
					      i == 0 ? 0 : PIRQ_MSI_GROUP);
		if (ret < 0)
			goto error_irq;
	}

	ret = irq_set_msi_desc(irq, msidesc);
	if (ret < 0)
		goto error_irq;
out:
	mutex_unlock(&irq_mapping_update_lock);
	return irq;
error_irq:
	while (nvec--)
		__unbind_from_irq(irq + nvec);
	mutex_unlock(&irq_mapping_update_lock);
	return ret;
}
#endif

int xen_destroy_irq(int irq)
{
	struct physdev_unmap_pirq unmap_irq;
	struct irq_info *info = info_for_irq(irq);
	int rc = -ENOENT;

	mutex_lock(&irq_mapping_update_lock);

	/*
	 * If trying to remove a vector in a MSI group different
	 * than the first one skip the PIRQ unmap unless this vector
	 * is the first one in the group.
	 */
	if (xen_initial_domain() && !(info->u.pirq.flags & PIRQ_MSI_GROUP)) {
		unmap_irq.pirq = info->u.pirq.pirq;
		unmap_irq.domid = info->u.pirq.domid;
		rc = HYPERVISOR_physdev_op(PHYSDEVOP_unmap_pirq, &unmap_irq);
		/* If another domain quits without making the pci_disable_msix
		 * call, the Xen hypervisor takes care of freeing the PIRQs
		 * (free_domain_pirqs).
		 */
		if ((rc == -ESRCH && info->u.pirq.domid != DOMID_SELF))
			pr_info("domain %d does not have %d anymore\n",
				info->u.pirq.domid, info->u.pirq.pirq);
		else if (rc) {
			pr_warn("unmap irq failed %d\n", rc);
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

static int bind_evtchn_to_irq_chip(evtchn_port_t evtchn, struct irq_chip *chip,
				   struct xenbus_device *dev)
{
	int irq;
	int ret;

	if (evtchn >= xen_evtchn_max_channels())
		return -ENOMEM;

	mutex_lock(&irq_mapping_update_lock);

	irq = get_evtchn_to_irq(evtchn);

	if (irq == -1) {
		irq = xen_allocate_irq_dynamic();
		if (irq < 0)
			goto out;

		irq_set_chip_and_handler_name(irq, chip,
					      handle_edge_irq, "event");

		ret = xen_irq_info_evtchn_setup(irq, evtchn, dev);
		if (ret < 0) {
			__unbind_from_irq(irq);
			irq = ret;
			goto out;
		}
		/*
		 * New interdomain events are initially bound to vCPU0 This
		 * is required to setup the event channel in the first
		 * place and also important for UP guests because the
		 * affinity setting is not invoked on them so nothing would
		 * bind the channel.
		 */
		bind_evtchn_to_cpu(evtchn, 0, false);
	} else {
		struct irq_info *info = info_for_irq(irq);
		WARN_ON(info == NULL || info->type != IRQT_EVTCHN);
	}

out:
	mutex_unlock(&irq_mapping_update_lock);

	return irq;
}

int bind_evtchn_to_irq(evtchn_port_t evtchn)
{
	return bind_evtchn_to_irq_chip(evtchn, &xen_dynamic_chip, NULL);
}
EXPORT_SYMBOL_GPL(bind_evtchn_to_irq);

int bind_evtchn_to_irq_lateeoi(evtchn_port_t evtchn)
{
	return bind_evtchn_to_irq_chip(evtchn, &xen_lateeoi_chip, NULL);
}
EXPORT_SYMBOL_GPL(bind_evtchn_to_irq_lateeoi);

static int bind_ipi_to_irq(unsigned int ipi, unsigned int cpu)
{
	struct evtchn_bind_ipi bind_ipi;
	evtchn_port_t evtchn;
	int ret, irq;

	mutex_lock(&irq_mapping_update_lock);

	irq = per_cpu(ipi_to_irq, cpu)[ipi];

	if (irq == -1) {
		irq = xen_allocate_irq_dynamic();
		if (irq < 0)
			goto out;

		irq_set_chip_and_handler_name(irq, &xen_percpu_chip,
					      handle_percpu_irq, "ipi");

		bind_ipi.vcpu = xen_vcpu_nr(cpu);
		if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_ipi,
						&bind_ipi) != 0)
			BUG();
		evtchn = bind_ipi.port;

		ret = xen_irq_info_ipi_setup(cpu, irq, evtchn, ipi);
		if (ret < 0) {
			__unbind_from_irq(irq);
			irq = ret;
			goto out;
		}
		/*
		 * Force the affinity mask to the target CPU so proc shows
		 * the correct target.
		 */
		bind_evtchn_to_cpu(evtchn, cpu, true);
	} else {
		struct irq_info *info = info_for_irq(irq);
		WARN_ON(info == NULL || info->type != IRQT_IPI);
	}

 out:
	mutex_unlock(&irq_mapping_update_lock);
	return irq;
}

static int bind_interdomain_evtchn_to_irq_chip(struct xenbus_device *dev,
					       evtchn_port_t remote_port,
					       struct irq_chip *chip)
{
	struct evtchn_bind_interdomain bind_interdomain;
	int err;

	bind_interdomain.remote_dom  = dev->otherend_id;
	bind_interdomain.remote_port = remote_port;

	err = HYPERVISOR_event_channel_op(EVTCHNOP_bind_interdomain,
					  &bind_interdomain);

	return err ? : bind_evtchn_to_irq_chip(bind_interdomain.local_port,
					       chip, dev);
}

int bind_interdomain_evtchn_to_irq_lateeoi(struct xenbus_device *dev,
					   evtchn_port_t remote_port)
{
	return bind_interdomain_evtchn_to_irq_chip(dev, remote_port,
						   &xen_lateeoi_chip);
}
EXPORT_SYMBOL_GPL(bind_interdomain_evtchn_to_irq_lateeoi);

static int find_virq(unsigned int virq, unsigned int cpu, evtchn_port_t *evtchn)
{
	struct evtchn_status status;
	evtchn_port_t port;
	int rc = -ENOENT;

	memset(&status, 0, sizeof(status));
	for (port = 0; port < xen_evtchn_max_channels(); port++) {
		status.dom = DOMID_SELF;
		status.port = port;
		rc = HYPERVISOR_event_channel_op(EVTCHNOP_status, &status);
		if (rc < 0)
			continue;
		if (status.status != EVTCHNSTAT_virq)
			continue;
		if (status.u.virq == virq && status.vcpu == xen_vcpu_nr(cpu)) {
			*evtchn = port;
			break;
		}
	}
	return rc;
}

/**
 * xen_evtchn_nr_channels - number of usable event channel ports
 *
 * This may be less than the maximum supported by the current
 * hypervisor ABI. Use xen_evtchn_max_channels() for the maximum
 * supported.
 */
unsigned xen_evtchn_nr_channels(void)
{
        return evtchn_ops->nr_channels();
}
EXPORT_SYMBOL_GPL(xen_evtchn_nr_channels);

int bind_virq_to_irq(unsigned int virq, unsigned int cpu, bool percpu)
{
	struct evtchn_bind_virq bind_virq;
	evtchn_port_t evtchn = 0;
	int irq, ret;

	mutex_lock(&irq_mapping_update_lock);

	irq = per_cpu(virq_to_irq, cpu)[virq];

	if (irq == -1) {
		irq = xen_allocate_irq_dynamic();
		if (irq < 0)
			goto out;

		if (percpu)
			irq_set_chip_and_handler_name(irq, &xen_percpu_chip,
						      handle_percpu_irq, "virq");
		else
			irq_set_chip_and_handler_name(irq, &xen_dynamic_chip,
						      handle_edge_irq, "virq");

		bind_virq.virq = virq;
		bind_virq.vcpu = xen_vcpu_nr(cpu);
		ret = HYPERVISOR_event_channel_op(EVTCHNOP_bind_virq,
						&bind_virq);
		if (ret == 0)
			evtchn = bind_virq.port;
		else {
			if (ret == -EEXIST)
				ret = find_virq(virq, cpu, &evtchn);
			BUG_ON(ret < 0);
		}

		ret = xen_irq_info_virq_setup(cpu, irq, evtchn, virq);
		if (ret < 0) {
			__unbind_from_irq(irq);
			irq = ret;
			goto out;
		}

		/*
		 * Force the affinity mask for percpu interrupts so proc
		 * shows the correct target.
		 */
		bind_evtchn_to_cpu(evtchn, cpu, percpu);
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
	mutex_lock(&irq_mapping_update_lock);
	__unbind_from_irq(irq);
	mutex_unlock(&irq_mapping_update_lock);
}

static int bind_evtchn_to_irqhandler_chip(evtchn_port_t evtchn,
					  irq_handler_t handler,
					  unsigned long irqflags,
					  const char *devname, void *dev_id,
					  struct irq_chip *chip)
{
	int irq, retval;

	irq = bind_evtchn_to_irq_chip(evtchn, chip, NULL);
	if (irq < 0)
		return irq;
	retval = request_irq(irq, handler, irqflags, devname, dev_id);
	if (retval != 0) {
		unbind_from_irq(irq);
		return retval;
	}

	return irq;
}

int bind_evtchn_to_irqhandler(evtchn_port_t evtchn,
			      irq_handler_t handler,
			      unsigned long irqflags,
			      const char *devname, void *dev_id)
{
	return bind_evtchn_to_irqhandler_chip(evtchn, handler, irqflags,
					      devname, dev_id,
					      &xen_dynamic_chip);
}
EXPORT_SYMBOL_GPL(bind_evtchn_to_irqhandler);

int bind_evtchn_to_irqhandler_lateeoi(evtchn_port_t evtchn,
				      irq_handler_t handler,
				      unsigned long irqflags,
				      const char *devname, void *dev_id)
{
	return bind_evtchn_to_irqhandler_chip(evtchn, handler, irqflags,
					      devname, dev_id,
					      &xen_lateeoi_chip);
}
EXPORT_SYMBOL_GPL(bind_evtchn_to_irqhandler_lateeoi);

static int bind_interdomain_evtchn_to_irqhandler_chip(
		struct xenbus_device *dev, evtchn_port_t remote_port,
		irq_handler_t handler, unsigned long irqflags,
		const char *devname, void *dev_id, struct irq_chip *chip)
{
	int irq, retval;

	irq = bind_interdomain_evtchn_to_irq_chip(dev, remote_port, chip);
	if (irq < 0)
		return irq;

	retval = request_irq(irq, handler, irqflags, devname, dev_id);
	if (retval != 0) {
		unbind_from_irq(irq);
		return retval;
	}

	return irq;
}

int bind_interdomain_evtchn_to_irqhandler_lateeoi(struct xenbus_device *dev,
						  evtchn_port_t remote_port,
						  irq_handler_t handler,
						  unsigned long irqflags,
						  const char *devname,
						  void *dev_id)
{
	return bind_interdomain_evtchn_to_irqhandler_chip(dev,
				remote_port, handler, irqflags, devname,
				dev_id, &xen_lateeoi_chip);
}
EXPORT_SYMBOL_GPL(bind_interdomain_evtchn_to_irqhandler_lateeoi);

int bind_virq_to_irqhandler(unsigned int virq, unsigned int cpu,
			    irq_handler_t handler,
			    unsigned long irqflags, const char *devname, void *dev_id)
{
	int irq, retval;

	irq = bind_virq_to_irq(virq, cpu, irqflags & IRQF_PERCPU);
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
	struct irq_info *info = info_for_irq(irq);

	if (WARN_ON(!info))
		return;
	free_irq(irq, dev_id);
	unbind_from_irq(irq);
}
EXPORT_SYMBOL_GPL(unbind_from_irqhandler);

/**
 * xen_set_irq_priority() - set an event channel priority.
 * @irq:irq bound to an event channel.
 * @priority: priority between XEN_IRQ_PRIORITY_MAX and XEN_IRQ_PRIORITY_MIN.
 */
int xen_set_irq_priority(unsigned irq, unsigned priority)
{
	struct evtchn_set_priority set_priority;

	set_priority.port = evtchn_from_irq(irq);
	set_priority.priority = priority;

	return HYPERVISOR_event_channel_op(EVTCHNOP_set_priority,
					   &set_priority);
}
EXPORT_SYMBOL_GPL(xen_set_irq_priority);

int evtchn_make_refcounted(evtchn_port_t evtchn)
{
	int irq = get_evtchn_to_irq(evtchn);
	struct irq_info *info;

	if (irq == -1)
		return -ENOENT;

	info = info_for_irq(irq);

	if (!info)
		return -ENOENT;

	WARN_ON(info->refcnt != -1);

	info->refcnt = 1;

	return 0;
}
EXPORT_SYMBOL_GPL(evtchn_make_refcounted);

int evtchn_get(evtchn_port_t evtchn)
{
	int irq;
	struct irq_info *info;
	int err = -ENOENT;

	if (evtchn >= xen_evtchn_max_channels())
		return -EINVAL;

	mutex_lock(&irq_mapping_update_lock);

	irq = get_evtchn_to_irq(evtchn);
	if (irq == -1)
		goto done;

	info = info_for_irq(irq);

	if (!info)
		goto done;

	err = -EINVAL;
	if (info->refcnt <= 0 || info->refcnt == SHRT_MAX)
		goto done;

	info->refcnt++;
	err = 0;
 done:
	mutex_unlock(&irq_mapping_update_lock);

	return err;
}
EXPORT_SYMBOL_GPL(evtchn_get);

void evtchn_put(evtchn_port_t evtchn)
{
	int irq = get_evtchn_to_irq(evtchn);
	if (WARN_ON(irq == -1))
		return;
	unbind_from_irq(irq);
}
EXPORT_SYMBOL_GPL(evtchn_put);

void xen_send_IPI_one(unsigned int cpu, enum ipi_vector vector)
{
	int irq;

#ifdef CONFIG_X86
	if (unlikely(vector == XEN_NMI_VECTOR)) {
		int rc =  HYPERVISOR_vcpu_op(VCPUOP_send_nmi, xen_vcpu_nr(cpu),
					     NULL);
		if (rc < 0)
			printk(KERN_WARNING "Sending nmi to CPU%d failed (rc:%d)\n", cpu, rc);
		return;
	}
#endif
	irq = per_cpu(ipi_to_irq, cpu)[vector];
	BUG_ON(irq < 0);
	notify_remote_via_irq(irq);
}

struct evtchn_loop_ctrl {
	ktime_t timeout;
	unsigned count;
	bool defer_eoi;
};

void handle_irq_for_port(evtchn_port_t port, struct evtchn_loop_ctrl *ctrl)
{
	int irq;
	struct irq_info *info;
	struct xenbus_device *dev;

	irq = get_evtchn_to_irq(port);
	if (irq == -1)
		return;

	/*
	 * Check for timeout every 256 events.
	 * We are setting the timeout value only after the first 256
	 * events in order to not hurt the common case of few loop
	 * iterations. The 256 is basically an arbitrary value.
	 *
	 * In case we are hitting the timeout we need to defer all further
	 * EOIs in order to ensure to leave the event handling loop rather
	 * sooner than later.
	 */
	if (!ctrl->defer_eoi && !(++ctrl->count & 0xff)) {
		ktime_t kt = ktime_get();

		if (!ctrl->timeout) {
			kt = ktime_add_ms(kt,
					  jiffies_to_msecs(event_loop_timeout));
			ctrl->timeout = kt;
		} else if (kt > ctrl->timeout) {
			ctrl->defer_eoi = true;
		}
	}

	info = info_for_irq(irq);
	if (xchg_acquire(&info->is_active, 1))
		return;

	dev = (info->type == IRQT_EVTCHN) ? info->u.interdomain : NULL;
	if (dev)
		atomic_inc(&dev->events);

	if (ctrl->defer_eoi) {
		info->eoi_cpu = smp_processor_id();
		info->irq_epoch = __this_cpu_read(irq_epoch);
		info->eoi_time = get_jiffies_64() + event_eoi_delay;
	}

	generic_handle_irq(irq);
}

static int __xen_evtchn_do_upcall(void)
{
	struct vcpu_info *vcpu_info = __this_cpu_read(xen_vcpu);
	int ret = vcpu_info->evtchn_upcall_pending ? IRQ_HANDLED : IRQ_NONE;
	int cpu = smp_processor_id();
	struct evtchn_loop_ctrl ctrl = { 0 };

	read_lock(&evtchn_rwlock);

	do {
		vcpu_info->evtchn_upcall_pending = 0;

		xen_evtchn_handle_events(cpu, &ctrl);

		BUG_ON(!irqs_disabled());

		virt_rmb(); /* Hypervisor can set upcall pending. */

	} while (vcpu_info->evtchn_upcall_pending);

	read_unlock(&evtchn_rwlock);

	/*
	 * Increment irq_epoch only now to defer EOIs only for
	 * xen_irq_lateeoi() invocations occurring from inside the loop
	 * above.
	 */
	__this_cpu_inc(irq_epoch);

	return ret;
}

void xen_evtchn_do_upcall(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	irq_enter();

	__xen_evtchn_do_upcall();

	irq_exit();
	set_irq_regs(old_regs);
}

int xen_hvm_evtchn_do_upcall(void)
{
	return __xen_evtchn_do_upcall();
}
EXPORT_SYMBOL_GPL(xen_hvm_evtchn_do_upcall);

/* Rebind a new event channel to an existing irq. */
void rebind_evtchn_irq(evtchn_port_t evtchn, int irq)
{
	struct irq_info *info = info_for_irq(irq);

	if (WARN_ON(!info))
		return;

	/* Make sure the irq is masked, since the new event channel
	   will also be masked. */
	disable_irq(irq);

	mutex_lock(&irq_mapping_update_lock);

	/* After resume the irq<->evtchn mappings are all cleared out */
	BUG_ON(get_evtchn_to_irq(evtchn) != -1);
	/* Expect irq to have been bound before,
	   so there should be a proper type */
	BUG_ON(info->type == IRQT_UNBOUND);

	(void)xen_irq_info_evtchn_setup(irq, evtchn, NULL);

	mutex_unlock(&irq_mapping_update_lock);

	bind_evtchn_to_cpu(evtchn, info->cpu, false);

	/* Unmask the event channel. */
	enable_irq(irq);
}

/* Rebind an evtchn so that it gets delivered to a specific cpu */
static int xen_rebind_evtchn_to_cpu(struct irq_info *info, unsigned int tcpu)
{
	struct evtchn_bind_vcpu bind_vcpu;
	evtchn_port_t evtchn = info ? info->evtchn : 0;

	if (!VALID_EVTCHN(evtchn))
		return -1;

	if (!xen_support_evtchn_rebind())
		return -1;

	/* Send future instances of this interrupt to other vcpu. */
	bind_vcpu.port = evtchn;
	bind_vcpu.vcpu = xen_vcpu_nr(tcpu);

	/*
	 * Mask the event while changing the VCPU binding to prevent
	 * it being delivered on an unexpected VCPU.
	 */
	do_mask(info, EVT_MASK_REASON_TEMPORARY);

	/*
	 * If this fails, it usually just indicates that we're dealing with a
	 * virq or IPI channel, which don't actually need to be rebound. Ignore
	 * it, but don't do the xenlinux-level rebind in that case.
	 */
	if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_vcpu, &bind_vcpu) >= 0)
		bind_evtchn_to_cpu(evtchn, tcpu, false);

	do_unmask(info, EVT_MASK_REASON_TEMPORARY);

	return 0;
}

/*
 * Find the CPU within @dest mask which has the least number of channels
 * assigned. This is not precise as the per cpu counts can be modified
 * concurrently.
 */
static unsigned int select_target_cpu(const struct cpumask *dest)
{
	unsigned int cpu, best_cpu = UINT_MAX, minch = UINT_MAX;

	for_each_cpu_and(cpu, dest, cpu_online_mask) {
		unsigned int curch = atomic_read(&channels_on_cpu[cpu]);

		if (curch < minch) {
			minch = curch;
			best_cpu = cpu;
		}
	}

	/*
	 * Catch the unlikely case that dest contains no online CPUs. Can't
	 * recurse.
	 */
	if (best_cpu == UINT_MAX)
		return select_target_cpu(cpu_online_mask);

	return best_cpu;
}

static int set_affinity_irq(struct irq_data *data, const struct cpumask *dest,
			    bool force)
{
	unsigned int tcpu = select_target_cpu(dest);
	int ret;

	ret = xen_rebind_evtchn_to_cpu(info_for_irq(data->irq), tcpu);
	if (!ret)
		irq_data_update_effective_affinity(data, cpumask_of(tcpu));

	return ret;
}

static void enable_dynirq(struct irq_data *data)
{
	struct irq_info *info = info_for_irq(data->irq);
	evtchn_port_t evtchn = info ? info->evtchn : 0;

	if (VALID_EVTCHN(evtchn))
		do_unmask(info, EVT_MASK_REASON_EXPLICIT);
}

static void disable_dynirq(struct irq_data *data)
{
	struct irq_info *info = info_for_irq(data->irq);
	evtchn_port_t evtchn = info ? info->evtchn : 0;

	if (VALID_EVTCHN(evtchn))
		do_mask(info, EVT_MASK_REASON_EXPLICIT);
}

static void ack_dynirq(struct irq_data *data)
{
	struct irq_info *info = info_for_irq(data->irq);
	evtchn_port_t evtchn = info ? info->evtchn : 0;

	if (VALID_EVTCHN(evtchn))
		event_handler_exit(info);
}

static void mask_ack_dynirq(struct irq_data *data)
{
	disable_dynirq(data);
	ack_dynirq(data);
}

static void lateeoi_ack_dynirq(struct irq_data *data)
{
	struct irq_info *info = info_for_irq(data->irq);
	evtchn_port_t evtchn = info ? info->evtchn : 0;

	if (VALID_EVTCHN(evtchn)) {
		do_mask(info, EVT_MASK_REASON_EOI_PENDING);
		/*
		 * Don't call event_handler_exit().
		 * Need to keep is_active non-zero in order to ignore re-raised
		 * events after cpu affinity changes while a lateeoi is pending.
		 */
		clear_evtchn(evtchn);
	}
}

static void lateeoi_mask_ack_dynirq(struct irq_data *data)
{
	struct irq_info *info = info_for_irq(data->irq);
	evtchn_port_t evtchn = info ? info->evtchn : 0;

	if (VALID_EVTCHN(evtchn)) {
		do_mask(info, EVT_MASK_REASON_EXPLICIT);
		event_handler_exit(info);
	}
}

static int retrigger_dynirq(struct irq_data *data)
{
	struct irq_info *info = info_for_irq(data->irq);
	evtchn_port_t evtchn = info ? info->evtchn : 0;

	if (!VALID_EVTCHN(evtchn))
		return 0;

	do_mask(info, EVT_MASK_REASON_TEMPORARY);
	set_evtchn(evtchn);
	do_unmask(info, EVT_MASK_REASON_TEMPORARY);

	return 1;
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
			pr_warn("xen map irq failed gsi=%d irq=%d pirq=%d rc=%d\n",
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
	evtchn_port_t evtchn;
	int virq, irq;

	for (virq = 0; virq < NR_VIRQS; virq++) {
		if ((irq = per_cpu(virq_to_irq, cpu)[virq]) == -1)
			continue;

		BUG_ON(virq_from_irq(irq) != virq);

		/* Get a new binding from Xen. */
		bind_virq.virq = virq;
		bind_virq.vcpu = xen_vcpu_nr(cpu);
		if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_virq,
						&bind_virq) != 0)
			BUG();
		evtchn = bind_virq.port;

		/* Record the new mapping. */
		(void)xen_irq_info_virq_setup(cpu, irq, evtchn, virq);
		/* The affinity mask is still valid */
		bind_evtchn_to_cpu(evtchn, cpu, false);
	}
}

static void restore_cpu_ipis(unsigned int cpu)
{
	struct evtchn_bind_ipi bind_ipi;
	evtchn_port_t evtchn;
	int ipi, irq;

	for (ipi = 0; ipi < XEN_NR_IPIS; ipi++) {
		if ((irq = per_cpu(ipi_to_irq, cpu)[ipi]) == -1)
			continue;

		BUG_ON(ipi_from_irq(irq) != ipi);

		/* Get a new binding from Xen. */
		bind_ipi.vcpu = xen_vcpu_nr(cpu);
		if (HYPERVISOR_event_channel_op(EVTCHNOP_bind_ipi,
						&bind_ipi) != 0)
			BUG();
		evtchn = bind_ipi.port;

		/* Record the new mapping. */
		(void)xen_irq_info_ipi_setup(cpu, irq, evtchn, ipi);
		/* The affinity mask is still valid */
		bind_evtchn_to_cpu(evtchn, cpu, false);
	}
}

/* Clear an irq's pending state, in preparation for polling on it */
void xen_clear_irq_pending(int irq)
{
	struct irq_info *info = info_for_irq(irq);
	evtchn_port_t evtchn = info ? info->evtchn : 0;

	if (VALID_EVTCHN(evtchn))
		event_handler_exit(info);
}
EXPORT_SYMBOL(xen_clear_irq_pending);
void xen_set_irq_pending(int irq)
{
	evtchn_port_t evtchn = evtchn_from_irq(irq);

	if (VALID_EVTCHN(evtchn))
		set_evtchn(evtchn);
}

bool xen_test_irq_pending(int irq)
{
	evtchn_port_t evtchn = evtchn_from_irq(irq);
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
	unsigned int cpu;
	struct irq_info *info;

	/* New event-channel space is not 'live' yet. */
	xen_evtchn_resume();

	/* No IRQ <-> event-channel mappings. */
	list_for_each_entry(info, &xen_irq_list_head, list) {
		/* Zap event-channel binding */
		info->evtchn = 0;
		/* Adjust accounting */
		channels_on_cpu_dec(info);
	}

	clear_evtchn_to_irq_all();

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

static struct irq_chip xen_lateeoi_chip __read_mostly = {
	/* The chip name needs to contain "xen-dyn" for irqbalance to work. */
	.name			= "xen-dyn-lateeoi",

	.irq_disable		= disable_dynirq,
	.irq_mask		= disable_dynirq,
	.irq_unmask		= enable_dynirq,

	.irq_ack		= lateeoi_ack_dynirq,
	.irq_mask_ack		= lateeoi_mask_ack_dynirq,

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

#ifdef CONFIG_X86
#ifdef CONFIG_XEN_PVHVM
/* Vector callbacks are better than PCI interrupts to receive event
 * channel notifications because we can receive vector callbacks on any
 * vcpu and we don't need PCI support or APIC interactions. */
void xen_setup_callback_vector(void)
{
	uint64_t callback_via;

	if (xen_have_vector_callback) {
		callback_via = HVM_CALLBACK_VECTOR(HYPERVISOR_CALLBACK_VECTOR);
		if (xen_set_callback_via(callback_via)) {
			pr_err("Request for Xen HVM callback vector failed\n");
			xen_have_vector_callback = false;
		}
	}
}

/*
 * Setup per-vCPU vector-type callbacks. If this setup is unavailable,
 * fallback to the global vector-type callback.
 */
static __init void xen_init_setup_upcall_vector(void)
{
	if (!xen_have_vector_callback)
		return;

	if ((cpuid_eax(xen_cpuid_base() + 4) & XEN_HVM_CPUID_UPCALL_VECTOR) &&
	    !xen_set_upcall_vector(0))
		xen_percpu_upcall = true;
	else if (xen_feature(XENFEAT_hvm_callback_vector))
		xen_setup_callback_vector();
	else
		xen_have_vector_callback = false;
}

int xen_set_upcall_vector(unsigned int cpu)
{
	int rc;
	xen_hvm_evtchn_upcall_vector_t op = {
		.vector = HYPERVISOR_CALLBACK_VECTOR,
		.vcpu = per_cpu(xen_vcpu_id, cpu),
	};

	rc = HYPERVISOR_hvm_op(HVMOP_set_evtchn_upcall_vector, &op);
	if (rc)
		return rc;

	/* Trick toolstack to think we are enlightened. */
	if (!cpu)
		rc = xen_set_callback_via(1);

	return rc;
}

static __init void xen_alloc_callback_vector(void)
{
	if (!xen_have_vector_callback)
		return;

	pr_info("Xen HVM callback vector for event delivery is enabled\n");
	alloc_intr_gate(HYPERVISOR_CALLBACK_VECTOR, asm_sysvec_xen_hvm_callback);
}
#else
void xen_setup_callback_vector(void) {}
static inline void xen_init_setup_upcall_vector(void) {}
int xen_set_upcall_vector(unsigned int cpu) {}
static inline void xen_alloc_callback_vector(void) {}
#endif /* CONFIG_XEN_PVHVM */
#endif /* CONFIG_X86 */

bool xen_fifo_events = true;
module_param_named(fifo_events, xen_fifo_events, bool, 0);

static int xen_evtchn_cpu_prepare(unsigned int cpu)
{
	int ret = 0;

	xen_cpu_init_eoi(cpu);

	if (evtchn_ops->percpu_init)
		ret = evtchn_ops->percpu_init(cpu);

	return ret;
}

static int xen_evtchn_cpu_dead(unsigned int cpu)
{
	int ret = 0;

	if (evtchn_ops->percpu_deinit)
		ret = evtchn_ops->percpu_deinit(cpu);

	return ret;
}

void __init xen_init_IRQ(void)
{
	int ret = -EINVAL;
	evtchn_port_t evtchn;

	if (xen_fifo_events)
		ret = xen_evtchn_fifo_init();
	if (ret < 0) {
		xen_evtchn_2l_init();
		xen_fifo_events = false;
	}

	xen_cpu_init_eoi(smp_processor_id());

	cpuhp_setup_state_nocalls(CPUHP_XEN_EVTCHN_PREPARE,
				  "xen/evtchn:prepare",
				  xen_evtchn_cpu_prepare, xen_evtchn_cpu_dead);

	evtchn_to_irq = kcalloc(EVTCHN_ROW(xen_evtchn_max_channels()),
				sizeof(*evtchn_to_irq), GFP_KERNEL);
	BUG_ON(!evtchn_to_irq);

	/* No event channels are 'live' right now. */
	for (evtchn = 0; evtchn < xen_evtchn_nr_channels(); evtchn++)
		mask_evtchn(evtchn);

	pirq_needs_eoi = pirq_needs_eoi_flag;

#ifdef CONFIG_X86
	if (xen_pv_domain()) {
		if (xen_initial_domain())
			pci_xen_initial_domain();
	}
	xen_init_setup_upcall_vector();
	xen_alloc_callback_vector();


	if (xen_hvm_domain()) {
		native_init_IRQ();
		/* pci_xen_hvm_init must be called after native_init_IRQ so that
		 * __acpi_register_gsi can point at the right function */
		pci_xen_hvm_init();
	} else {
		int rc;
		struct physdev_pirq_eoi_gmfn eoi_gmfn;

		pirq_eoi_map = (void *)__get_free_page(GFP_KERNEL|__GFP_ZERO);
		eoi_gmfn.gmfn = virt_to_gfn(pirq_eoi_map);
		rc = HYPERVISOR_physdev_op(PHYSDEVOP_pirq_eoi_gmfn_v2, &eoi_gmfn);
		if (rc != 0) {
			free_page((unsigned long) pirq_eoi_map);
			pirq_eoi_map = NULL;
		} else
			pirq_needs_eoi = pirq_check_eoi_map;
	}
#endif
}
