/*
 * System Abstraction Layer (SAL) interface routines.
 *
 * Copyright (C) 1998, 1999, 2001, 2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/string.h>

#include <asm/delay.h>
#include <asm/page.h>
#include <asm/sal.h>
#include <asm/pal.h>

 __cacheline_aligned DEFINE_SPINLOCK(sal_lock);
unsigned long sal_platform_features;

unsigned short sal_revision;
unsigned short sal_version;

#define SAL_MAJOR(x) ((x) >> 8)
#define SAL_MINOR(x) ((x) & 0xff)

static struct {
	void *addr;	/* function entry point */
	void *gpval;	/* gp value to use */
} pdesc;

static long
default_handler (void)
{
	return -1;
}

ia64_sal_handler ia64_sal = (ia64_sal_handler) default_handler;
ia64_sal_desc_ptc_t *ia64_ptc_domain_info;

const char *
ia64_sal_strerror (long status)
{
	const char *str;
	switch (status) {
	      case 0: str = "Call completed without error"; break;
	      case 1: str = "Effect a warm boot of the system to complete "
			      "the update"; break;
	      case -1: str = "Not implemented"; break;
	      case -2: str = "Invalid argument"; break;
	      case -3: str = "Call completed with error"; break;
	      case -4: str = "Virtual address not registered"; break;
	      case -5: str = "No information available"; break;
	      case -6: str = "Insufficient space to add the entry"; break;
	      case -7: str = "Invalid entry_addr value"; break;
	      case -8: str = "Invalid interrupt vector"; break;
	      case -9: str = "Requested memory not available"; break;
	      case -10: str = "Unable to write to the NVM device"; break;
	      case -11: str = "Invalid partition type specified"; break;
	      case -12: str = "Invalid NVM_Object id specified"; break;
	      case -13: str = "NVM_Object already has the maximum number "
				"of partitions"; break;
	      case -14: str = "Insufficient space in partition for the "
				"requested write sub-function"; break;
	      case -15: str = "Insufficient data buffer space for the "
				"requested read record sub-function"; break;
	      case -16: str = "Scratch buffer required for the write/delete "
				"sub-function"; break;
	      case -17: str = "Insufficient space in the NVM_Object for the "
				"requested create sub-function"; break;
	      case -18: str = "Invalid value specified in the partition_rec "
				"argument"; break;
	      case -19: str = "Record oriented I/O not supported for this "
				"partition"; break;
	      case -20: str = "Bad format of record to be written or "
				"required keyword variable not "
				"specified"; break;
	      default: str = "Unknown SAL status code"; break;
	}
	return str;
}

void __init
ia64_sal_handler_init (void *entry_point, void *gpval)
{
	/* fill in the SAL procedure descriptor and point ia64_sal to it: */
	pdesc.addr = entry_point;
	pdesc.gpval = gpval;
	ia64_sal = (ia64_sal_handler) &pdesc;
}

static void __init
check_versions (struct ia64_sal_systab *systab)
{
	sal_revision = (systab->sal_rev_major << 8) | systab->sal_rev_minor;
	sal_version = (systab->sal_b_rev_major << 8) | systab->sal_b_rev_minor;

	/* Check for broken firmware */
	if ((sal_revision == SAL_VERSION_CODE(49, 29))
	    && (sal_version == SAL_VERSION_CODE(49, 29)))
	{
		/*
		 * Old firmware for zx2000 prototypes have this weird version number,
		 * reset it to something sane.
		 */
		sal_revision = SAL_VERSION_CODE(2, 8);
		sal_version = SAL_VERSION_CODE(0, 0);
	}
}

static void __init
sal_desc_entry_point (void *p)
{
	struct ia64_sal_desc_entry_point *ep = p;
	ia64_pal_handler_init(__va(ep->pal_proc));
	ia64_sal_handler_init(__va(ep->sal_proc), __va(ep->gp));
}

#ifdef CONFIG_SMP
static void __init
set_smp_redirect (int flag)
{
#ifndef CONFIG_HOTPLUG_CPU
	if (no_int_routing)
		smp_int_redirect &= ~flag;
	else
		smp_int_redirect |= flag;
#else
	/*
	 * For CPU Hotplug we dont want to do any chipset supported
	 * interrupt redirection. The reason is this would require that
	 * All interrupts be stopped and hard bind the irq to a cpu.
	 * Later when the interrupt is fired we need to set the redir hint
	 * on again in the vector. This is combersome for something that the
	 * user mode irq balancer will solve anyways.
	 */
	no_int_routing=1;
	smp_int_redirect &= ~flag;
#endif
}
#else
#define set_smp_redirect(flag)	do { } while (0)
#endif

static void __init
sal_desc_platform_feature (void *p)
{
	struct ia64_sal_desc_platform_feature *pf = p;
	sal_platform_features = pf->feature_mask;

	printk(KERN_INFO "SAL Platform features:");
	if (!sal_platform_features) {
		printk(" None\n");
		return;
	}

	if (sal_platform_features & IA64_SAL_PLATFORM_FEATURE_BUS_LOCK)
		printk(" BusLock");
	if (sal_platform_features & IA64_SAL_PLATFORM_FEATURE_IRQ_REDIR_HINT) {
		printk(" IRQ_Redirection");
		set_smp_redirect(SMP_IRQ_REDIRECTION);
	}
	if (sal_platform_features & IA64_SAL_PLATFORM_FEATURE_IPI_REDIR_HINT) {
		printk(" IPI_Redirection");
		set_smp_redirect(SMP_IPI_REDIRECTION);
	}
	if (sal_platform_features & IA64_SAL_PLATFORM_FEATURE_ITC_DRIFT)
		printk(" ITC_Drift");
	printk("\n");
}

#ifdef CONFIG_SMP
static void __init
sal_desc_ap_wakeup (void *p)
{
	struct ia64_sal_desc_ap_wakeup *ap = p;

	switch (ap->mechanism) {
	case IA64_SAL_AP_EXTERNAL_INT:
		ap_wakeup_vector = ap->vector;
		printk(KERN_INFO "SAL: AP wakeup using external interrupt "
				"vector 0x%lx\n", ap_wakeup_vector);
		break;
	default:
		printk(KERN_ERR "SAL: AP wakeup mechanism unsupported!\n");
		break;
	}
}

static void __init
chk_nointroute_opt(void)
{
	char *cp;
	extern char saved_command_line[];

	for (cp = saved_command_line; *cp; ) {
		if (memcmp(cp, "nointroute", 10) == 0) {
			no_int_routing = 1;
			printk ("no_int_routing on\n");
			break;
		} else {
			while (*cp != ' ' && *cp)
				++cp;
			while (*cp == ' ')
				++cp;
		}
	}
}

#else
static void __init sal_desc_ap_wakeup(void *p) { }
#endif

/*
 * HP rx5670 firmware polls for interrupts during SAL_CACHE_FLUSH by reading
 * cr.ivr, but it never writes cr.eoi.  This leaves any interrupt marked as
 * "in-service" and masks other interrupts of equal or lower priority.
 *
 * HP internal defect reports: F1859, F2775, F3031.
 */
static int sal_cache_flush_drops_interrupts;

void __init
check_sal_cache_flush (void)
{
	unsigned long flags;
	int cpu;
	u64 vector, cache_type = 3;
	struct ia64_sal_retval isrv;

	cpu = get_cpu();
	local_irq_save(flags);

	/*
	 * Schedule a timer interrupt, wait until it's reported, and see if
	 * SAL_CACHE_FLUSH drops it.
	 */
	ia64_set_itv(IA64_TIMER_VECTOR);
	ia64_set_itm(ia64_get_itc() + 1000);

	while (!ia64_get_irr(IA64_TIMER_VECTOR))
		cpu_relax();

	SAL_CALL(isrv, SAL_CACHE_FLUSH, cache_type, 0, 0, 0, 0, 0, 0);

	if (isrv.status)
		printk(KERN_ERR "SAL_CAL_FLUSH failed with %ld\n", isrv.status);

	if (ia64_get_irr(IA64_TIMER_VECTOR)) {
		vector = ia64_get_ivr();
		ia64_eoi();
		WARN_ON(vector != IA64_TIMER_VECTOR);
	} else {
		sal_cache_flush_drops_interrupts = 1;
		printk(KERN_ERR "SAL: SAL_CACHE_FLUSH drops interrupts; "
			"PAL_CACHE_FLUSH will be used instead\n");
		ia64_eoi();
	}

	local_irq_restore(flags);
	put_cpu();
}

s64
ia64_sal_cache_flush (u64 cache_type)
{
	struct ia64_sal_retval isrv;

	if (sal_cache_flush_drops_interrupts) {
		unsigned long flags;
		u64 progress;
		s64 rc;

		progress = 0;
		local_irq_save(flags);
		rc = ia64_pal_cache_flush(cache_type,
			PAL_CACHE_FLUSH_INVALIDATE, &progress, NULL);
		local_irq_restore(flags);
		return rc;
	}

	SAL_CALL(isrv, SAL_CACHE_FLUSH, cache_type, 0, 0, 0, 0, 0, 0);
	return isrv.status;
}

void __init
ia64_sal_init (struct ia64_sal_systab *systab)
{
	char *p;
	int i;

	if (!systab) {
		printk(KERN_WARNING "Hmm, no SAL System Table.\n");
		return;
	}

	if (strncmp(systab->signature, "SST_", 4) != 0)
		printk(KERN_ERR "bad signature in system table!");

	check_versions(systab);
#ifdef CONFIG_SMP
	chk_nointroute_opt();
#endif

	/* revisions are coded in BCD, so %x does the job for us */
	printk(KERN_INFO "SAL %x.%x: %.32s %.32s%sversion %x.%x\n",
			SAL_MAJOR(sal_revision), SAL_MINOR(sal_revision),
			systab->oem_id, systab->product_id,
			systab->product_id[0] ? " " : "",
			SAL_MAJOR(sal_version), SAL_MINOR(sal_version));

	p = (char *) (systab + 1);
	for (i = 0; i < systab->entry_count; i++) {
		/*
		 * The first byte of each entry type contains the type
		 * descriptor.
		 */
		switch (*p) {
		case SAL_DESC_ENTRY_POINT:
			sal_desc_entry_point(p);
			break;
		case SAL_DESC_PLATFORM_FEATURE:
			sal_desc_platform_feature(p);
			break;
		case SAL_DESC_PTC:
			ia64_ptc_domain_info = (ia64_sal_desc_ptc_t *)p;
			break;
		case SAL_DESC_AP_WAKEUP:
			sal_desc_ap_wakeup(p);
			break;
		}
		p += SAL_DESC_SIZE(*p);
	}

}

int
ia64_sal_oemcall(struct ia64_sal_retval *isrvp, u64 oemfunc, u64 arg1,
		 u64 arg2, u64 arg3, u64 arg4, u64 arg5, u64 arg6, u64 arg7)
{
	if (oemfunc < IA64_SAL_OEMFUNC_MIN || oemfunc > IA64_SAL_OEMFUNC_MAX)
		return -1;
	SAL_CALL(*isrvp, oemfunc, arg1, arg2, arg3, arg4, arg5, arg6, arg7);
	return 0;
}
EXPORT_SYMBOL(ia64_sal_oemcall);

int
ia64_sal_oemcall_nolock(struct ia64_sal_retval *isrvp, u64 oemfunc, u64 arg1,
			u64 arg2, u64 arg3, u64 arg4, u64 arg5, u64 arg6,
			u64 arg7)
{
	if (oemfunc < IA64_SAL_OEMFUNC_MIN || oemfunc > IA64_SAL_OEMFUNC_MAX)
		return -1;
	SAL_CALL_NOLOCK(*isrvp, oemfunc, arg1, arg2, arg3, arg4, arg5, arg6,
			arg7);
	return 0;
}
EXPORT_SYMBOL(ia64_sal_oemcall_nolock);

int
ia64_sal_oemcall_reentrant(struct ia64_sal_retval *isrvp, u64 oemfunc,
			   u64 arg1, u64 arg2, u64 arg3, u64 arg4, u64 arg5,
			   u64 arg6, u64 arg7)
{
	if (oemfunc < IA64_SAL_OEMFUNC_MIN || oemfunc > IA64_SAL_OEMFUNC_MAX)
		return -1;
	SAL_CALL_REENTRANT(*isrvp, oemfunc, arg1, arg2, arg3, arg4, arg5, arg6,
			   arg7);
	return 0;
}
EXPORT_SYMBOL(ia64_sal_oemcall_reentrant);
