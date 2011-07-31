#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/sysdev.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/hpet.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/pm.h>
#include <linux/io.h>

#include <asm/fixmap.h>
#include <asm/i8253.h>
#include <asm/hpet.h>

#define HPET_MASK			CLOCKSOURCE_MASK(32)

/* FSEC = 10^-15
   NSEC = 10^-9 */
#define FSEC_PER_NSEC			1000000L

#define HPET_DEV_USED_BIT		2
#define HPET_DEV_USED			(1 << HPET_DEV_USED_BIT)
#define HPET_DEV_VALID			0x8
#define HPET_DEV_FSB_CAP		0x1000
#define HPET_DEV_PERI_CAP		0x2000

#define EVT_TO_HPET_DEV(evt) container_of(evt, struct hpet_dev, evt)

/*
 * HPET address is set in acpi/boot.c, when an ACPI entry exists
 */
unsigned long				hpet_address;
u8					hpet_blockid; /* OS timer block num */
u8					hpet_msi_disable;

#ifdef CONFIG_PCI_MSI
static unsigned long			hpet_num_timers;
#endif
static void __iomem			*hpet_virt_address;

struct hpet_dev {
	struct clock_event_device	evt;
	unsigned int			num;
	int				cpu;
	unsigned int			irq;
	unsigned int			flags;
	char				name[10];
};

inline unsigned int hpet_readl(unsigned int a)
{
	return readl(hpet_virt_address + a);
}

static inline void hpet_writel(unsigned int d, unsigned int a)
{
	writel(d, hpet_virt_address + a);
}

#ifdef CONFIG_X86_64
#include <asm/pgtable.h>
#endif

static inline void hpet_set_mapping(void)
{
	hpet_virt_address = ioremap_nocache(hpet_address, HPET_MMAP_SIZE);
#ifdef CONFIG_X86_64
	__set_fixmap(VSYSCALL_HPET, hpet_address, PAGE_KERNEL_VSYSCALL_NOCACHE);
#endif
}

static inline void hpet_clear_mapping(void)
{
	iounmap(hpet_virt_address);
	hpet_virt_address = NULL;
}

/*
 * HPET command line enable / disable
 */
static int boot_hpet_disable;
int hpet_force_user;
static int hpet_verbose;

static int __init hpet_setup(char *str)
{
	if (str) {
		if (!strncmp("disable", str, 7))
			boot_hpet_disable = 1;
		if (!strncmp("force", str, 5))
			hpet_force_user = 1;
		if (!strncmp("verbose", str, 7))
			hpet_verbose = 1;
	}
	return 1;
}
__setup("hpet=", hpet_setup);

static int __init disable_hpet(char *str)
{
	boot_hpet_disable = 1;
	return 1;
}
__setup("nohpet", disable_hpet);

static inline int is_hpet_capable(void)
{
	return !boot_hpet_disable && hpet_address;
}

/*
 * HPET timer interrupt enable / disable
 */
static int hpet_legacy_int_enabled;

/**
 * is_hpet_enabled - check whether the hpet timer interrupt is enabled
 */
int is_hpet_enabled(void)
{
	return is_hpet_capable() && hpet_legacy_int_enabled;
}
EXPORT_SYMBOL_GPL(is_hpet_enabled);

static void _hpet_print_config(const char *function, int line)
{
	u32 i, timers, l, h;
	printk(KERN_INFO "hpet: %s(%d):\n", function, line);
	l = hpet_readl(HPET_ID);
	h = hpet_readl(HPET_PERIOD);
	timers = ((l & HPET_ID_NUMBER) >> HPET_ID_NUMBER_SHIFT) + 1;
	printk(KERN_INFO "hpet: ID: 0x%x, PERIOD: 0x%x\n", l, h);
	l = hpet_readl(HPET_CFG);
	h = hpet_readl(HPET_STATUS);
	printk(KERN_INFO "hpet: CFG: 0x%x, STATUS: 0x%x\n", l, h);
	l = hpet_readl(HPET_COUNTER);
	h = hpet_readl(HPET_COUNTER+4);
	printk(KERN_INFO "hpet: COUNTER_l: 0x%x, COUNTER_h: 0x%x\n", l, h);

	for (i = 0; i < timers; i++) {
		l = hpet_readl(HPET_Tn_CFG(i));
		h = hpet_readl(HPET_Tn_CFG(i)+4);
		printk(KERN_INFO "hpet: T%d: CFG_l: 0x%x, CFG_h: 0x%x\n",
		       i, l, h);
		l = hpet_readl(HPET_Tn_CMP(i));
		h = hpet_readl(HPET_Tn_CMP(i)+4);
		printk(KERN_INFO "hpet: T%d: CMP_l: 0x%x, CMP_h: 0x%x\n",
		       i, l, h);
		l = hpet_readl(HPET_Tn_ROUTE(i));
		h = hpet_readl(HPET_Tn_ROUTE(i)+4);
		printk(KERN_INFO "hpet: T%d ROUTE_l: 0x%x, ROUTE_h: 0x%x\n",
		       i, l, h);
	}
}

#define hpet_print_config()					\
do {								\
	if (hpet_verbose)					\
		_hpet_print_config(__FUNCTION__, __LINE__);	\
} while (0)

/*
 * When the hpet driver (/dev/hpet) is enabled, we need to reserve
 * timer 0 and timer 1 in case of RTC emulation.
 */
#ifdef CONFIG_HPET

static void hpet_reserve_msi_timers(struct hpet_data *hd);

static void hpet_reserve_platform_timers(unsigned int id)
{
	struct hpet __iomem *hpet = hpet_virt_address;
	struct hpet_timer __iomem *timer = &hpet->hpet_timers[2];
	unsigned int nrtimers, i;
	struct hpet_data hd;

	nrtimers = ((id & HPET_ID_NUMBER) >> HPET_ID_NUMBER_SHIFT) + 1;

	memset(&hd, 0, sizeof(hd));
	hd.hd_phys_address	= hpet_address;
	hd.hd_address		= hpet;
	hd.hd_nirqs		= nrtimers;
	hpet_reserve_timer(&hd, 0);

#ifdef CONFIG_HPET_EMULATE_RTC
	hpet_reserve_timer(&hd, 1);
#endif

	/*
	 * NOTE that hd_irq[] reflects IOAPIC input pins (LEGACY_8254
	 * is wrong for i8259!) not the output IRQ.  Many BIOS writers
	 * don't bother configuring *any* comparator interrupts.
	 */
	hd.hd_irq[0] = HPET_LEGACY_8254;
	hd.hd_irq[1] = HPET_LEGACY_RTC;

	for (i = 2; i < nrtimers; timer++, i++) {
		hd.hd_irq[i] = (readl(&timer->hpet_config) &
			Tn_INT_ROUTE_CNF_MASK) >> Tn_INT_ROUTE_CNF_SHIFT;
	}

	hpet_reserve_msi_timers(&hd);

	hpet_alloc(&hd);

}
#else
static void hpet_reserve_platform_timers(unsigned int id) { }
#endif

/*
 * Common hpet info
 */
static unsigned long hpet_period;

static void hpet_legacy_set_mode(enum clock_event_mode mode,
			  struct clock_event_device *evt);
static int hpet_legacy_next_event(unsigned long delta,
			   struct clock_event_device *evt);

/*
 * The hpet clock event device
 */
static struct clock_event_device hpet_clockevent = {
	.name		= "hpet",
	.features	= CLOCK_EVT_FEAT_PERIODIC | CLOCK_EVT_FEAT_ONESHOT,
	.set_mode	= hpet_legacy_set_mode,
	.set_next_event = hpet_legacy_next_event,
	.shift		= 32,
	.irq		= 0,
	.rating		= 50,
};

static void hpet_stop_counter(void)
{
	unsigned long cfg = hpet_readl(HPET_CFG);
	cfg &= ~HPET_CFG_ENABLE;
	hpet_writel(cfg, HPET_CFG);
}

static void hpet_reset_counter(void)
{
	hpet_writel(0, HPET_COUNTER);
	hpet_writel(0, HPET_COUNTER + 4);
}

static void hpet_start_counter(void)
{
	unsigned int cfg = hpet_readl(HPET_CFG);
	cfg |= HPET_CFG_ENABLE;
	hpet_writel(cfg, HPET_CFG);
}

static void hpet_restart_counter(void)
{
	hpet_stop_counter();
	hpet_reset_counter();
	hpet_start_counter();
}

static void hpet_resume_device(void)
{
	force_hpet_resume();
}

static void hpet_resume_counter(struct clocksource *cs)
{
	hpet_resume_device();
	hpet_restart_counter();
}

static void hpet_enable_legacy_int(void)
{
	unsigned int cfg = hpet_readl(HPET_CFG);

	cfg |= HPET_CFG_LEGACY;
	hpet_writel(cfg, HPET_CFG);
	hpet_legacy_int_enabled = 1;
}

static void hpet_legacy_clockevent_register(void)
{
	/* Start HPET legacy interrupts */
	hpet_enable_legacy_int();

	/*
	 * The mult factor is defined as (include/linux/clockchips.h)
	 *  mult/2^shift = cyc/ns (in contrast to ns/cyc in clocksource.h)
	 * hpet_period is in units of femtoseconds (per cycle), so
	 *  mult/2^shift = cyc/ns = 10^6/hpet_period
	 *  mult = (10^6 * 2^shift)/hpet_period
	 *  mult = (FSEC_PER_NSEC << hpet_clockevent.shift)/hpet_period
	 */
	hpet_clockevent.mult = div_sc((unsigned long) FSEC_PER_NSEC,
				      hpet_period, hpet_clockevent.shift);
	/* Calculate the min / max delta */
	hpet_clockevent.max_delta_ns = clockevent_delta2ns(0x7FFFFFFF,
							   &hpet_clockevent);
	/* 5 usec minimum reprogramming delta. */
	hpet_clockevent.min_delta_ns = 5000;

	/*
	 * Start hpet with the boot cpu mask and make it
	 * global after the IO_APIC has been initialized.
	 */
	hpet_clockevent.cpumask = cpumask_of(smp_processor_id());
	clockevents_register_device(&hpet_clockevent);
	global_clock_event = &hpet_clockevent;
	printk(KERN_DEBUG "hpet clockevent registered\n");
}

static int hpet_setup_msi_irq(unsigned int irq);

static void hpet_set_mode(enum clock_event_mode mode,
			  struct clock_event_device *evt, int timer)
{
	unsigned int cfg, cmp, now;
	uint64_t delta;

	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		hpet_stop_counter();
		delta = ((uint64_t)(NSEC_PER_SEC/HZ)) * evt->mult;
		delta >>= evt->shift;
		now = hpet_readl(HPET_COUNTER);
		cmp = now + (unsigned int) delta;
		cfg = hpet_readl(HPET_Tn_CFG(timer));
		/* Make sure we use edge triggered interrupts */
		cfg &= ~HPET_TN_LEVEL;
		cfg |= HPET_TN_ENABLE | HPET_TN_PERIODIC |
		       HPET_TN_SETVAL | HPET_TN_32BIT;
		hpet_writel(cfg, HPET_Tn_CFG(timer));
		hpet_writel(cmp, HPET_Tn_CMP(timer));
		udelay(1);
		/*
		 * HPET on AMD 81xx needs a second write (with HPET_TN_SETVAL
		 * cleared) to T0_CMP to set the period. The HPET_TN_SETVAL
		 * bit is automatically cleared after the first write.
		 * (See AMD-8111 HyperTransport I/O Hub Data Sheet,
		 * Publication # 24674)
		 */
		hpet_writel((unsigned int) delta, HPET_Tn_CMP(timer));
		hpet_start_counter();
		hpet_print_config();
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		cfg = hpet_readl(HPET_Tn_CFG(timer));
		cfg &= ~HPET_TN_PERIODIC;
		cfg |= HPET_TN_ENABLE | HPET_TN_32BIT;
		hpet_writel(cfg, HPET_Tn_CFG(timer));
		break;

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		cfg = hpet_readl(HPET_Tn_CFG(timer));
		cfg &= ~HPET_TN_ENABLE;
		hpet_writel(cfg, HPET_Tn_CFG(timer));
		break;

	case CLOCK_EVT_MODE_RESUME:
		if (timer == 0) {
			hpet_enable_legacy_int();
		} else {
			struct hpet_dev *hdev = EVT_TO_HPET_DEV(evt);
			hpet_setup_msi_irq(hdev->irq);
			disable_irq(hdev->irq);
			irq_set_affinity(hdev->irq, cpumask_of(hdev->cpu));
			enable_irq(hdev->irq);
		}
		hpet_print_config();
		break;
	}
}

static int hpet_next_event(unsigned long delta,
			   struct clock_event_device *evt, int timer)
{
	u32 cnt;

	cnt = hpet_readl(HPET_COUNTER);
	cnt += (u32) delta;
	hpet_writel(cnt, HPET_Tn_CMP(timer));

	/*
	 * We need to read back the CMP register on certain HPET
	 * implementations (ATI chipsets) which seem to delay the
	 * transfer of the compare register into the internal compare
	 * logic. With small deltas this might actually be too late as
	 * the counter could already be higher than the compare value
	 * at that point and we would wait for the next hpet interrupt
	 * forever. We found out that reading the CMP register back
	 * forces the transfer so we can rely on the comparison with
	 * the counter register below. If the read back from the
	 * compare register does not match the value we programmed
	 * then we might have a real hardware problem. We can not do
	 * much about it here, but at least alert the user/admin with
	 * a prominent warning.
	 *
	 * An erratum on some chipsets (ICH9,..), results in
	 * comparator read immediately following a write returning old
	 * value. Workaround for this is to read this value second
	 * time, when first read returns old value.
	 *
	 * In fact the write to the comparator register is delayed up
	 * to two HPET cycles so the workaround we tried to restrict
	 * the readback to those known to be borked ATI chipsets
	 * failed miserably. So we give up on optimizations forever
	 * and penalize all HPET incarnations unconditionally.
	 */
	if (unlikely((u32)hpet_readl(HPET_Tn_CMP(timer)) != cnt)) {
		if (hpet_readl(HPET_Tn_CMP(timer)) != cnt)
			printk_once(KERN_WARNING
				"hpet: compare register read back failed.\n");
	}

	return (s32)(hpet_readl(HPET_COUNTER) - cnt) >= 0 ? -ETIME : 0;
}

static void hpet_legacy_set_mode(enum clock_event_mode mode,
			struct clock_event_device *evt)
{
	hpet_set_mode(mode, evt, 0);
}

static int hpet_legacy_next_event(unsigned long delta,
			struct clock_event_device *evt)
{
	return hpet_next_event(delta, evt, 0);
}

/*
 * HPET MSI Support
 */
#ifdef CONFIG_PCI_MSI

static DEFINE_PER_CPU(struct hpet_dev *, cpu_hpet_dev);
static struct hpet_dev	*hpet_devs;

void hpet_msi_unmask(unsigned int irq)
{
	struct hpet_dev *hdev = get_irq_data(irq);
	unsigned int cfg;

	/* unmask it */
	cfg = hpet_readl(HPET_Tn_CFG(hdev->num));
	cfg |= HPET_TN_FSB;
	hpet_writel(cfg, HPET_Tn_CFG(hdev->num));
}

void hpet_msi_mask(unsigned int irq)
{
	unsigned int cfg;
	struct hpet_dev *hdev = get_irq_data(irq);

	/* mask it */
	cfg = hpet_readl(HPET_Tn_CFG(hdev->num));
	cfg &= ~HPET_TN_FSB;
	hpet_writel(cfg, HPET_Tn_CFG(hdev->num));
}

void hpet_msi_write(unsigned int irq, struct msi_msg *msg)
{
	struct hpet_dev *hdev = get_irq_data(irq);

	hpet_writel(msg->data, HPET_Tn_ROUTE(hdev->num));
	hpet_writel(msg->address_lo, HPET_Tn_ROUTE(hdev->num) + 4);
}

void hpet_msi_read(unsigned int irq, struct msi_msg *msg)
{
	struct hpet_dev *hdev = get_irq_data(irq);

	msg->data = hpet_readl(HPET_Tn_ROUTE(hdev->num));
	msg->address_lo = hpet_readl(HPET_Tn_ROUTE(hdev->num) + 4);
	msg->address_hi = 0;
}

static void hpet_msi_set_mode(enum clock_event_mode mode,
				struct clock_event_device *evt)
{
	struct hpet_dev *hdev = EVT_TO_HPET_DEV(evt);
	hpet_set_mode(mode, evt, hdev->num);
}

static int hpet_msi_next_event(unsigned long delta,
				struct clock_event_device *evt)
{
	struct hpet_dev *hdev = EVT_TO_HPET_DEV(evt);
	return hpet_next_event(delta, evt, hdev->num);
}

static int hpet_setup_msi_irq(unsigned int irq)
{
	if (arch_setup_hpet_msi(irq, hpet_blockid)) {
		destroy_irq(irq);
		return -EINVAL;
	}
	return 0;
}

static int hpet_assign_irq(struct hpet_dev *dev)
{
	unsigned int irq;

	irq = create_irq_nr(0, -1);
	if (!irq)
		return -EINVAL;

	set_irq_data(irq, dev);

	if (hpet_setup_msi_irq(irq))
		return -EINVAL;

	dev->irq = irq;
	return 0;
}

static irqreturn_t hpet_interrupt_handler(int irq, void *data)
{
	struct hpet_dev *dev = (struct hpet_dev *)data;
	struct clock_event_device *hevt = &dev->evt;

	if (!hevt->event_handler) {
		printk(KERN_INFO "Spurious HPET timer interrupt on HPET timer %d\n",
				dev->num);
		return IRQ_HANDLED;
	}

	hevt->event_handler(hevt);
	return IRQ_HANDLED;
}

static int hpet_setup_irq(struct hpet_dev *dev)
{

	if (request_irq(dev->irq, hpet_interrupt_handler,
			IRQF_TIMER | IRQF_DISABLED | IRQF_NOBALANCING,
			dev->name, dev))
		return -1;

	disable_irq(dev->irq);
	irq_set_affinity(dev->irq, cpumask_of(dev->cpu));
	enable_irq(dev->irq);

	printk(KERN_DEBUG "hpet: %s irq %d for MSI\n",
			 dev->name, dev->irq);

	return 0;
}

/* This should be called in specific @cpu */
static void init_one_hpet_msi_clockevent(struct hpet_dev *hdev, int cpu)
{
	struct clock_event_device *evt = &hdev->evt;
	uint64_t hpet_freq;

	WARN_ON(cpu != smp_processor_id());
	if (!(hdev->flags & HPET_DEV_VALID))
		return;

	if (hpet_setup_msi_irq(hdev->irq))
		return;

	hdev->cpu = cpu;
	per_cpu(cpu_hpet_dev, cpu) = hdev;
	evt->name = hdev->name;
	hpet_setup_irq(hdev);
	evt->irq = hdev->irq;

	evt->rating = 110;
	evt->features = CLOCK_EVT_FEAT_ONESHOT;
	if (hdev->flags & HPET_DEV_PERI_CAP)
		evt->features |= CLOCK_EVT_FEAT_PERIODIC;

	evt->set_mode = hpet_msi_set_mode;
	evt->set_next_event = hpet_msi_next_event;
	evt->shift = 32;

	/*
	 * The period is a femto seconds value. We need to calculate the
	 * scaled math multiplication factor for nanosecond to hpet tick
	 * conversion.
	 */
	hpet_freq = FSEC_PER_SEC;
	do_div(hpet_freq, hpet_period);
	evt->mult = div_sc((unsigned long) hpet_freq,
				      NSEC_PER_SEC, evt->shift);
	/* Calculate the max delta */
	evt->max_delta_ns = clockevent_delta2ns(0x7FFFFFFF, evt);
	/* 5 usec minimum reprogramming delta. */
	evt->min_delta_ns = 5000;

	evt->cpumask = cpumask_of(hdev->cpu);
	clockevents_register_device(evt);
}

#ifdef CONFIG_HPET
/* Reserve at least one timer for userspace (/dev/hpet) */
#define RESERVE_TIMERS 1
#else
#define RESERVE_TIMERS 0
#endif

static void hpet_msi_capability_lookup(unsigned int start_timer)
{
	unsigned int id;
	unsigned int num_timers;
	unsigned int num_timers_used = 0;
	int i;

	if (hpet_msi_disable)
		return;

	if (boot_cpu_has(X86_FEATURE_ARAT))
		return;
	id = hpet_readl(HPET_ID);

	num_timers = ((id & HPET_ID_NUMBER) >> HPET_ID_NUMBER_SHIFT);
	num_timers++; /* Value read out starts from 0 */
	hpet_print_config();

	hpet_devs = kzalloc(sizeof(struct hpet_dev) * num_timers, GFP_KERNEL);
	if (!hpet_devs)
		return;

	hpet_num_timers = num_timers;

	for (i = start_timer; i < num_timers - RESERVE_TIMERS; i++) {
		struct hpet_dev *hdev = &hpet_devs[num_timers_used];
		unsigned int cfg = hpet_readl(HPET_Tn_CFG(i));

		/* Only consider HPET timer with MSI support */
		if (!(cfg & HPET_TN_FSB_CAP))
			continue;

		hdev->flags = 0;
		if (cfg & HPET_TN_PERIODIC_CAP)
			hdev->flags |= HPET_DEV_PERI_CAP;
		hdev->num = i;

		sprintf(hdev->name, "hpet%d", i);
		if (hpet_assign_irq(hdev))
			continue;

		hdev->flags |= HPET_DEV_FSB_CAP;
		hdev->flags |= HPET_DEV_VALID;
		num_timers_used++;
		if (num_timers_used == num_possible_cpus())
			break;
	}

	printk(KERN_INFO "HPET: %d timers in total, %d timers will be used for per-cpu timer\n",
		num_timers, num_timers_used);
}

#ifdef CONFIG_HPET
static void hpet_reserve_msi_timers(struct hpet_data *hd)
{
	int i;

	if (!hpet_devs)
		return;

	for (i = 0; i < hpet_num_timers; i++) {
		struct hpet_dev *hdev = &hpet_devs[i];

		if (!(hdev->flags & HPET_DEV_VALID))
			continue;

		hd->hd_irq[hdev->num] = hdev->irq;
		hpet_reserve_timer(hd, hdev->num);
	}
}
#endif

static struct hpet_dev *hpet_get_unused_timer(void)
{
	int i;

	if (!hpet_devs)
		return NULL;

	for (i = 0; i < hpet_num_timers; i++) {
		struct hpet_dev *hdev = &hpet_devs[i];

		if (!(hdev->flags & HPET_DEV_VALID))
			continue;
		if (test_and_set_bit(HPET_DEV_USED_BIT,
			(unsigned long *)&hdev->flags))
			continue;
		return hdev;
	}
	return NULL;
}

struct hpet_work_struct {
	struct delayed_work work;
	struct completion complete;
};

static void hpet_work(struct work_struct *w)
{
	struct hpet_dev *hdev;
	int cpu = smp_processor_id();
	struct hpet_work_struct *hpet_work;

	hpet_work = container_of(w, struct hpet_work_struct, work.work);

	hdev = hpet_get_unused_timer();
	if (hdev)
		init_one_hpet_msi_clockevent(hdev, cpu);

	complete(&hpet_work->complete);
}

static int hpet_cpuhp_notify(struct notifier_block *n,
		unsigned long action, void *hcpu)
{
	unsigned long cpu = (unsigned long)hcpu;
	struct hpet_work_struct work;
	struct hpet_dev *hdev = per_cpu(cpu_hpet_dev, cpu);

	switch (action & 0xf) {
	case CPU_ONLINE:
		INIT_DELAYED_WORK_ON_STACK(&work.work, hpet_work);
		init_completion(&work.complete);
		/* FIXME: add schedule_work_on() */
		schedule_delayed_work_on(cpu, &work.work, 0);
		wait_for_completion(&work.complete);
		destroy_timer_on_stack(&work.work.timer);
		break;
	case CPU_DEAD:
		if (hdev) {
			free_irq(hdev->irq, hdev);
			hdev->flags &= ~HPET_DEV_USED;
			per_cpu(cpu_hpet_dev, cpu) = NULL;
		}
		break;
	}
	return NOTIFY_OK;
}
#else

static int hpet_setup_msi_irq(unsigned int irq)
{
	return 0;
}
static void hpet_msi_capability_lookup(unsigned int start_timer)
{
	return;
}

#ifdef CONFIG_HPET
static void hpet_reserve_msi_timers(struct hpet_data *hd)
{
	return;
}
#endif

static int hpet_cpuhp_notify(struct notifier_block *n,
		unsigned long action, void *hcpu)
{
	return NOTIFY_OK;
}

#endif

/*
 * Clock source related code
 */
static cycle_t read_hpet(struct clocksource *cs)
{
	return (cycle_t)hpet_readl(HPET_COUNTER);
}

#ifdef CONFIG_X86_64
static cycle_t __vsyscall_fn vread_hpet(void)
{
	return readl((const void __iomem *)fix_to_virt(VSYSCALL_HPET) + 0xf0);
}
#endif

static struct clocksource clocksource_hpet = {
	.name		= "hpet",
	.rating		= 250,
	.read		= read_hpet,
	.mask		= HPET_MASK,
	.flags		= CLOCK_SOURCE_IS_CONTINUOUS,
	.resume		= hpet_resume_counter,
#ifdef CONFIG_X86_64
	.vread		= vread_hpet,
#endif
};

static int hpet_clocksource_register(void)
{
	u64 start, now;
	u64 hpet_freq;
	cycle_t t1;

	/* Start the counter */
	hpet_restart_counter();

	/* Verify whether hpet counter works */
	t1 = hpet_readl(HPET_COUNTER);
	rdtscll(start);

	/*
	 * We don't know the TSC frequency yet, but waiting for
	 * 200000 TSC cycles is safe:
	 * 4 GHz == 50us
	 * 1 GHz == 200us
	 */
	do {
		rep_nop();
		rdtscll(now);
	} while ((now - start) < 200000UL);

	if (t1 == hpet_readl(HPET_COUNTER)) {
		printk(KERN_WARNING
		       "HPET counter not counting. HPET disabled\n");
		return -ENODEV;
	}

	/*
	 * The definition of mult is (include/linux/clocksource.h)
	 * mult/2^shift = ns/cyc and hpet_period is in units of fsec/cyc
	 * so we first need to convert hpet_period to ns/cyc units:
	 *  mult/2^shift = ns/cyc = hpet_period/10^6
	 *  mult = (hpet_period * 2^shift)/10^6
	 *  mult = (hpet_period << shift)/FSEC_PER_NSEC
	 */

	/* Need to convert hpet_period (fsec/cyc) to cyc/sec:
	 *
	 * cyc/sec = FSEC_PER_SEC/hpet_period(fsec/cyc)
	 * cyc/sec = (FSEC_PER_NSEC * NSEC_PER_SEC)/hpet_period
	 */
	hpet_freq = FSEC_PER_SEC;
	do_div(hpet_freq, hpet_period);
	clocksource_register_hz(&clocksource_hpet, (u32)hpet_freq);

	return 0;
}

/**
 * hpet_enable - Try to setup the HPET timer. Returns 1 on success.
 */
int __init hpet_enable(void)
{
	unsigned int id;
	int i;

	if (!is_hpet_capable())
		return 0;

	hpet_set_mapping();

	/*
	 * Read the period and check for a sane value:
	 */
	hpet_period = hpet_readl(HPET_PERIOD);

	/*
	 * AMD SB700 based systems with spread spectrum enabled use a
	 * SMM based HPET emulation to provide proper frequency
	 * setting. The SMM code is initialized with the first HPET
	 * register access and takes some time to complete. During
	 * this time the config register reads 0xffffffff. We check
	 * for max. 1000 loops whether the config register reads a non
	 * 0xffffffff value to make sure that HPET is up and running
	 * before we go further. A counting loop is safe, as the HPET
	 * access takes thousands of CPU cycles. On non SB700 based
	 * machines this check is only done once and has no side
	 * effects.
	 */
	for (i = 0; hpet_readl(HPET_CFG) == 0xFFFFFFFF; i++) {
		if (i == 1000) {
			printk(KERN_WARNING
			       "HPET config register value = 0xFFFFFFFF. "
			       "Disabling HPET\n");
			goto out_nohpet;
		}
	}

	if (hpet_period < HPET_MIN_PERIOD || hpet_period > HPET_MAX_PERIOD)
		goto out_nohpet;

	/*
	 * Read the HPET ID register to retrieve the IRQ routing
	 * information and the number of channels
	 */
	id = hpet_readl(HPET_ID);
	hpet_print_config();

#ifdef CONFIG_HPET_EMULATE_RTC
	/*
	 * The legacy routing mode needs at least two channels, tick timer
	 * and the rtc emulation channel.
	 */
	if (!(id & HPET_ID_NUMBER))
		goto out_nohpet;
#endif

	if (hpet_clocksource_register())
		goto out_nohpet;

	if (id & HPET_ID_LEGSUP) {
		hpet_legacy_clockevent_register();
		return 1;
	}
	return 0;

out_nohpet:
	hpet_clear_mapping();
	hpet_address = 0;
	return 0;
}

/*
 * Needs to be late, as the reserve_timer code calls kalloc !
 *
 * Not a problem on i386 as hpet_enable is called from late_time_init,
 * but on x86_64 it is necessary !
 */
static __init int hpet_late_init(void)
{
	int cpu;

	if (boot_hpet_disable)
		return -ENODEV;

	if (!hpet_address) {
		if (!force_hpet_address)
			return -ENODEV;

		hpet_address = force_hpet_address;
		hpet_enable();
	}

	if (!hpet_virt_address)
		return -ENODEV;

	if (hpet_readl(HPET_ID) & HPET_ID_LEGSUP)
		hpet_msi_capability_lookup(2);
	else
		hpet_msi_capability_lookup(0);

	hpet_reserve_platform_timers(hpet_readl(HPET_ID));
	hpet_print_config();

	if (hpet_msi_disable)
		return 0;

	if (boot_cpu_has(X86_FEATURE_ARAT))
		return 0;

	for_each_online_cpu(cpu) {
		hpet_cpuhp_notify(NULL, CPU_ONLINE, (void *)(long)cpu);
	}

	/* This notifier should be called after workqueue is ready */
	hotcpu_notifier(hpet_cpuhp_notify, -20);

	return 0;
}
fs_initcall(hpet_late_init);

void hpet_disable(void)
{
	if (is_hpet_capable() && hpet_virt_address) {
		unsigned int cfg = hpet_readl(HPET_CFG);

		if (hpet_legacy_int_enabled) {
			cfg &= ~HPET_CFG_LEGACY;
			hpet_legacy_int_enabled = 0;
		}
		cfg &= ~HPET_CFG_ENABLE;
		hpet_writel(cfg, HPET_CFG);
	}
}

#ifdef CONFIG_HPET_EMULATE_RTC

/* HPET in LegacyReplacement Mode eats up RTC interrupt line. When, HPET
 * is enabled, we support RTC interrupt functionality in software.
 * RTC has 3 kinds of interrupts:
 * 1) Update Interrupt - generate an interrupt, every sec, when RTC clock
 *    is updated
 * 2) Alarm Interrupt - generate an interrupt at a specific time of day
 * 3) Periodic Interrupt - generate periodic interrupt, with frequencies
 *    2Hz-8192Hz (2Hz-64Hz for non-root user) (all freqs in powers of 2)
 * (1) and (2) above are implemented using polling at a frequency of
 * 64 Hz. The exact frequency is a tradeoff between accuracy and interrupt
 * overhead. (DEFAULT_RTC_INT_FREQ)
 * For (3), we use interrupts at 64Hz or user specified periodic
 * frequency, whichever is higher.
 */
#include <linux/mc146818rtc.h>
#include <linux/rtc.h>
#include <asm/rtc.h>

#define DEFAULT_RTC_INT_FREQ	64
#define DEFAULT_RTC_SHIFT	6
#define RTC_NUM_INTS		1

static unsigned long hpet_rtc_flags;
static int hpet_prev_update_sec;
static struct rtc_time hpet_alarm_time;
static unsigned long hpet_pie_count;
static u32 hpet_t1_cmp;
static u32 hpet_default_delta;
static u32 hpet_pie_delta;
static unsigned long hpet_pie_limit;

static rtc_irq_handler irq_handler;

/*
 * Check that the hpet counter c1 is ahead of the c2
 */
static inline int hpet_cnt_ahead(u32 c1, u32 c2)
{
	return (s32)(c2 - c1) < 0;
}

/*
 * Registers a IRQ handler.
 */
int hpet_register_irq_handler(rtc_irq_handler handler)
{
	if (!is_hpet_enabled())
		return -ENODEV;
	if (irq_handler)
		return -EBUSY;

	irq_handler = handler;

	return 0;
}
EXPORT_SYMBOL_GPL(hpet_register_irq_handler);

/*
 * Deregisters the IRQ handler registered with hpet_register_irq_handler()
 * and does cleanup.
 */
void hpet_unregister_irq_handler(rtc_irq_handler handler)
{
	if (!is_hpet_enabled())
		return;

	irq_handler = NULL;
	hpet_rtc_flags = 0;
}
EXPORT_SYMBOL_GPL(hpet_unregister_irq_handler);

/*
 * Timer 1 for RTC emulation. We use one shot mode, as periodic mode
 * is not supported by all HPET implementations for timer 1.
 *
 * hpet_rtc_timer_init() is called when the rtc is initialized.
 */
int hpet_rtc_timer_init(void)
{
	unsigned int cfg, cnt, delta;
	unsigned long flags;

	if (!is_hpet_enabled())
		return 0;

	if (!hpet_default_delta) {
		uint64_t clc;

		clc = (uint64_t) hpet_clockevent.mult * NSEC_PER_SEC;
		clc >>= hpet_clockevent.shift + DEFAULT_RTC_SHIFT;
		hpet_default_delta = clc;
	}

	if (!(hpet_rtc_flags & RTC_PIE) || hpet_pie_limit)
		delta = hpet_default_delta;
	else
		delta = hpet_pie_delta;

	local_irq_save(flags);

	cnt = delta + hpet_readl(HPET_COUNTER);
	hpet_writel(cnt, HPET_T1_CMP);
	hpet_t1_cmp = cnt;

	cfg = hpet_readl(HPET_T1_CFG);
	cfg &= ~HPET_TN_PERIODIC;
	cfg |= HPET_TN_ENABLE | HPET_TN_32BIT;
	hpet_writel(cfg, HPET_T1_CFG);

	local_irq_restore(flags);

	return 1;
}
EXPORT_SYMBOL_GPL(hpet_rtc_timer_init);

/*
 * The functions below are called from rtc driver.
 * Return 0 if HPET is not being used.
 * Otherwise do the necessary changes and return 1.
 */
int hpet_mask_rtc_irq_bit(unsigned long bit_mask)
{
	if (!is_hpet_enabled())
		return 0;

	hpet_rtc_flags &= ~bit_mask;
	return 1;
}
EXPORT_SYMBOL_GPL(hpet_mask_rtc_irq_bit);

int hpet_set_rtc_irq_bit(unsigned long bit_mask)
{
	unsigned long oldbits = hpet_rtc_flags;

	if (!is_hpet_enabled())
		return 0;

	hpet_rtc_flags |= bit_mask;

	if ((bit_mask & RTC_UIE) && !(oldbits & RTC_UIE))
		hpet_prev_update_sec = -1;

	if (!oldbits)
		hpet_rtc_timer_init();

	return 1;
}
EXPORT_SYMBOL_GPL(hpet_set_rtc_irq_bit);

int hpet_set_alarm_time(unsigned char hrs, unsigned char min,
			unsigned char sec)
{
	if (!is_hpet_enabled())
		return 0;

	hpet_alarm_time.tm_hour = hrs;
	hpet_alarm_time.tm_min = min;
	hpet_alarm_time.tm_sec = sec;

	return 1;
}
EXPORT_SYMBOL_GPL(hpet_set_alarm_time);

int hpet_set_periodic_freq(unsigned long freq)
{
	uint64_t clc;

	if (!is_hpet_enabled())
		return 0;

	if (freq <= DEFAULT_RTC_INT_FREQ)
		hpet_pie_limit = DEFAULT_RTC_INT_FREQ / freq;
	else {
		clc = (uint64_t) hpet_clockevent.mult * NSEC_PER_SEC;
		do_div(clc, freq);
		clc >>= hpet_clockevent.shift;
		hpet_pie_delta = clc;
		hpet_pie_limit = 0;
	}
	return 1;
}
EXPORT_SYMBOL_GPL(hpet_set_periodic_freq);

int hpet_rtc_dropped_irq(void)
{
	return is_hpet_enabled();
}
EXPORT_SYMBOL_GPL(hpet_rtc_dropped_irq);

static void hpet_rtc_timer_reinit(void)
{
	unsigned int cfg, delta;
	int lost_ints = -1;

	if (unlikely(!hpet_rtc_flags)) {
		cfg = hpet_readl(HPET_T1_CFG);
		cfg &= ~HPET_TN_ENABLE;
		hpet_writel(cfg, HPET_T1_CFG);
		return;
	}

	if (!(hpet_rtc_flags & RTC_PIE) || hpet_pie_limit)
		delta = hpet_default_delta;
	else
		delta = hpet_pie_delta;

	/*
	 * Increment the comparator value until we are ahead of the
	 * current count.
	 */
	do {
		hpet_t1_cmp += delta;
		hpet_writel(hpet_t1_cmp, HPET_T1_CMP);
		lost_ints++;
	} while (!hpet_cnt_ahead(hpet_t1_cmp, hpet_readl(HPET_COUNTER)));

	if (lost_ints) {
		if (hpet_rtc_flags & RTC_PIE)
			hpet_pie_count += lost_ints;
		if (printk_ratelimit())
			printk(KERN_WARNING "hpet1: lost %d rtc interrupts\n",
				lost_ints);
	}
}

irqreturn_t hpet_rtc_interrupt(int irq, void *dev_id)
{
	struct rtc_time curr_time;
	unsigned long rtc_int_flag = 0;

	hpet_rtc_timer_reinit();
	memset(&curr_time, 0, sizeof(struct rtc_time));

	if (hpet_rtc_flags & (RTC_UIE | RTC_AIE))
		get_rtc_time(&curr_time);

	if (hpet_rtc_flags & RTC_UIE &&
	    curr_time.tm_sec != hpet_prev_update_sec) {
		if (hpet_prev_update_sec >= 0)
			rtc_int_flag = RTC_UF;
		hpet_prev_update_sec = curr_time.tm_sec;
	}

	if (hpet_rtc_flags & RTC_PIE &&
	    ++hpet_pie_count >= hpet_pie_limit) {
		rtc_int_flag |= RTC_PF;
		hpet_pie_count = 0;
	}

	if (hpet_rtc_flags & RTC_AIE &&
	    (curr_time.tm_sec == hpet_alarm_time.tm_sec) &&
	    (curr_time.tm_min == hpet_alarm_time.tm_min) &&
	    (curr_time.tm_hour == hpet_alarm_time.tm_hour))
			rtc_int_flag |= RTC_AF;

	if (rtc_int_flag) {
		rtc_int_flag |= (RTC_IRQF | (RTC_NUM_INTS << 8));
		if (irq_handler)
			irq_handler(rtc_int_flag, dev_id);
	}
	return IRQ_HANDLED;
}
EXPORT_SYMBOL_GPL(hpet_rtc_interrupt);
#endif
