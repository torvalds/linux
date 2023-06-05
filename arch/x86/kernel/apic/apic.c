// SPDX-License-Identifier: GPL-2.0-only
/*
 *	Local APIC handling, local APIC timers
 *
 *	(c) 1999, 2000, 2009 Ingo Molnar <mingo@redhat.com>
 *
 *	Fixes
 *	Maciej W. Rozycki	:	Bits for genuine 82489DX APICs;
 *					thanks to Eric Gilmore
 *					and Rolf G. Tews
 *					for testing these extensively.
 *	Maciej W. Rozycki	:	Various updates and fixes.
 *	Mikael Pettersson	:	Power Management for UP-APIC.
 *	Pavel Machek and
 *	Mikael Pettersson	:	PM converted to driver model.
 */

#include <linux/perf_event.h>
#include <linux/kernel_stat.h>
#include <linux/mc146818rtc.h>
#include <linux/acpi_pmtmr.h>
#include <linux/clockchips.h>
#include <linux/interrupt.h>
#include <linux/memblock.h>
#include <linux/ftrace.h>
#include <linux/ioport.h>
#include <linux/export.h>
#include <linux/syscore_ops.h>
#include <linux/delay.h>
#include <linux/timex.h>
#include <linux/i8253.h>
#include <linux/dmar.h>
#include <linux/init.h>
#include <linux/cpu.h>
#include <linux/dmi.h>
#include <linux/smp.h>
#include <linux/mm.h>

#include <asm/trace/irq_vectors.h>
#include <asm/irq_remapping.h>
#include <asm/pc-conf-reg.h>
#include <asm/perf_event.h>
#include <asm/x86_init.h>
#include <linux/atomic.h>
#include <asm/barrier.h>
#include <asm/mpspec.h>
#include <asm/i8259.h>
#include <asm/proto.h>
#include <asm/traps.h>
#include <asm/apic.h>
#include <asm/acpi.h>
#include <asm/io_apic.h>
#include <asm/desc.h>
#include <asm/hpet.h>
#include <asm/mtrr.h>
#include <asm/time.h>
#include <asm/smp.h>
#include <asm/mce.h>
#include <asm/tsc.h>
#include <asm/hypervisor.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include <asm/irq_regs.h>
#include <asm/cpu.h>

unsigned int num_processors;

unsigned disabled_cpus;

/* Processor that is doing the boot up */
unsigned int boot_cpu_physical_apicid __ro_after_init = -1U;
EXPORT_SYMBOL_GPL(boot_cpu_physical_apicid);

u8 boot_cpu_apic_version __ro_after_init;

/*
 * The highest APIC ID seen during enumeration.
 */
static unsigned int max_physical_apicid;

/*
 * Bitmask of physically existing CPUs:
 */
physid_mask_t phys_cpu_present_map;

/*
 * Processor to be disabled specified by kernel parameter
 * disable_cpu_apicid=<int>, mostly used for the kdump 2nd kernel to
 * avoid undefined behaviour caused by sending INIT from AP to BSP.
 */
static unsigned int disabled_cpu_apicid __ro_after_init = BAD_APICID;

/*
 * This variable controls which CPUs receive external NMIs.  By default,
 * external NMIs are delivered only to the BSP.
 */
static int apic_extnmi __ro_after_init = APIC_EXTNMI_BSP;

/*
 * Hypervisor supports 15 bits of APIC ID in MSI Extended Destination ID
 */
static bool virt_ext_dest_id __ro_after_init;

/*
 * Map cpu index to physical APIC ID
 */
DEFINE_EARLY_PER_CPU_READ_MOSTLY(u16, x86_cpu_to_apicid, BAD_APICID);
DEFINE_EARLY_PER_CPU_READ_MOSTLY(u16, x86_bios_cpu_apicid, BAD_APICID);
DEFINE_EARLY_PER_CPU_READ_MOSTLY(u32, x86_cpu_to_acpiid, U32_MAX);
EXPORT_EARLY_PER_CPU_SYMBOL(x86_cpu_to_apicid);
EXPORT_EARLY_PER_CPU_SYMBOL(x86_bios_cpu_apicid);
EXPORT_EARLY_PER_CPU_SYMBOL(x86_cpu_to_acpiid);

#ifdef CONFIG_X86_32

/*
 * On x86_32, the mapping between cpu and logical apicid may vary
 * depending on apic in use.  The following early percpu variable is
 * used for the mapping.  This is where the behaviors of x86_64 and 32
 * actually diverge.  Let's keep it ugly for now.
 */
DEFINE_EARLY_PER_CPU_READ_MOSTLY(int, x86_cpu_to_logical_apicid, BAD_APICID);

/* Local APIC was disabled by the BIOS and enabled by the kernel */
static int enabled_via_apicbase __ro_after_init;

/*
 * Handle interrupt mode configuration register (IMCR).
 * This register controls whether the interrupt signals
 * that reach the BSP come from the master PIC or from the
 * local APIC. Before entering Symmetric I/O Mode, either
 * the BIOS or the operating system must switch out of
 * PIC Mode by changing the IMCR.
 */
static inline void imcr_pic_to_apic(void)
{
	/* NMI and 8259 INTR go through APIC */
	pc_conf_set(PC_CONF_MPS_IMCR, 0x01);
}

static inline void imcr_apic_to_pic(void)
{
	/* NMI and 8259 INTR go directly to BSP */
	pc_conf_set(PC_CONF_MPS_IMCR, 0x00);
}
#endif

/*
 * Knob to control our willingness to enable the local APIC.
 *
 * +1=force-enable
 */
static int force_enable_local_apic __initdata;

/*
 * APIC command line parameters
 */
static int __init parse_lapic(char *arg)
{
	if (IS_ENABLED(CONFIG_X86_32) && !arg)
		force_enable_local_apic = 1;
	else if (arg && !strncmp(arg, "notscdeadline", 13))
		setup_clear_cpu_cap(X86_FEATURE_TSC_DEADLINE_TIMER);
	return 0;
}
early_param("lapic", parse_lapic);

#ifdef CONFIG_X86_64
static int apic_calibrate_pmtmr __initdata;
static __init int setup_apicpmtimer(char *s)
{
	apic_calibrate_pmtmr = 1;
	notsc_setup(NULL);
	return 1;
}
__setup("apicpmtimer", setup_apicpmtimer);
#endif

unsigned long mp_lapic_addr __ro_after_init;
int disable_apic __ro_after_init;
/* Disable local APIC timer from the kernel commandline or via dmi quirk */
static int disable_apic_timer __initdata;
/* Local APIC timer works in C2 */
int local_apic_timer_c2_ok __ro_after_init;
EXPORT_SYMBOL_GPL(local_apic_timer_c2_ok);

/*
 * Debug level, exported for io_apic.c
 */
int apic_verbosity __ro_after_init;

int pic_mode __ro_after_init;

/* Have we found an MP table */
int smp_found_config __ro_after_init;

static struct resource lapic_resource = {
	.name = "Local APIC",
	.flags = IORESOURCE_MEM | IORESOURCE_BUSY,
};

unsigned int lapic_timer_period = 0;

static void apic_pm_activate(void);

static unsigned long apic_phys __ro_after_init;

/*
 * Get the LAPIC version
 */
static inline int lapic_get_version(void)
{
	return GET_APIC_VERSION(apic_read(APIC_LVR));
}

/*
 * Check, if the APIC is integrated or a separate chip
 */
static inline int lapic_is_integrated(void)
{
	return APIC_INTEGRATED(lapic_get_version());
}

/*
 * Check, whether this is a modern or a first generation APIC
 */
static int modern_apic(void)
{
	/* AMD systems use old APIC versions, so check the CPU */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_AMD &&
	    boot_cpu_data.x86 >= 0xf)
		return 1;

	/* Hygon systems use modern APIC */
	if (boot_cpu_data.x86_vendor == X86_VENDOR_HYGON)
		return 1;

	return lapic_get_version() >= 0x14;
}

/*
 * right after this call apic become NOOP driven
 * so apic->write/read doesn't do anything
 */
static void __init apic_disable(void)
{
	pr_info("APIC: switched to apic NOOP\n");
	apic = &apic_noop;
}

void native_apic_wait_icr_idle(void)
{
	while (apic_read(APIC_ICR) & APIC_ICR_BUSY)
		cpu_relax();
}

u32 native_safe_apic_wait_icr_idle(void)
{
	u32 send_status;
	int timeout;

	timeout = 0;
	do {
		send_status = apic_read(APIC_ICR) & APIC_ICR_BUSY;
		if (!send_status)
			break;
		inc_irq_stat(icr_read_retry_count);
		udelay(100);
	} while (timeout++ < 1000);

	return send_status;
}

void native_apic_icr_write(u32 low, u32 id)
{
	unsigned long flags;

	local_irq_save(flags);
	apic_write(APIC_ICR2, SET_XAPIC_DEST_FIELD(id));
	apic_write(APIC_ICR, low);
	local_irq_restore(flags);
}

u64 native_apic_icr_read(void)
{
	u32 icr1, icr2;

	icr2 = apic_read(APIC_ICR2);
	icr1 = apic_read(APIC_ICR);

	return icr1 | ((u64)icr2 << 32);
}

#ifdef CONFIG_X86_32
/**
 * get_physical_broadcast - Get number of physical broadcast IDs
 */
int get_physical_broadcast(void)
{
	return modern_apic() ? 0xff : 0xf;
}
#endif

/**
 * lapic_get_maxlvt - get the maximum number of local vector table entries
 */
int lapic_get_maxlvt(void)
{
	/*
	 * - we always have APIC integrated on 64bit mode
	 * - 82489DXs do not report # of LVT entries
	 */
	return lapic_is_integrated() ? GET_APIC_MAXLVT(apic_read(APIC_LVR)) : 2;
}

/*
 * Local APIC timer
 */

/* Clock divisor */
#define APIC_DIVISOR 16
#define TSC_DIVISOR  8

/* i82489DX specific */
#define		I82489DX_BASE_DIVIDER		(((0x2) << 18))

/*
 * This function sets up the local APIC timer, with a timeout of
 * 'clocks' APIC bus clock. During calibration we actually call
 * this function twice on the boot CPU, once with a bogus timeout
 * value, second time for real. The other (noncalibrating) CPUs
 * call this function only once, with the real, calibrated value.
 *
 * We do reads before writes even if unnecessary, to get around the
 * P5 APIC double write bug.
 */
static void __setup_APIC_LVTT(unsigned int clocks, int oneshot, int irqen)
{
	unsigned int lvtt_value, tmp_value;

	lvtt_value = LOCAL_TIMER_VECTOR;
	if (!oneshot)
		lvtt_value |= APIC_LVT_TIMER_PERIODIC;
	else if (boot_cpu_has(X86_FEATURE_TSC_DEADLINE_TIMER))
		lvtt_value |= APIC_LVT_TIMER_TSCDEADLINE;

	/*
	 * The i82489DX APIC uses bit 18 and 19 for the base divider.  This
	 * overlaps with bit 18 on integrated APICs, but is not documented
	 * in the SDM. No problem though. i82489DX equipped systems do not
	 * have TSC deadline timer.
	 */
	if (!lapic_is_integrated())
		lvtt_value |= I82489DX_BASE_DIVIDER;

	if (!irqen)
		lvtt_value |= APIC_LVT_MASKED;

	apic_write(APIC_LVTT, lvtt_value);

	if (lvtt_value & APIC_LVT_TIMER_TSCDEADLINE) {
		/*
		 * See Intel SDM: TSC-Deadline Mode chapter. In xAPIC mode,
		 * writing to the APIC LVTT and TSC_DEADLINE MSR isn't serialized.
		 * According to Intel, MFENCE can do the serialization here.
		 */
		asm volatile("mfence" : : : "memory");
		return;
	}

	/*
	 * Divide PICLK by 16
	 */
	tmp_value = apic_read(APIC_TDCR);
	apic_write(APIC_TDCR,
		(tmp_value & ~(APIC_TDR_DIV_1 | APIC_TDR_DIV_TMBASE)) |
		APIC_TDR_DIV_16);

	if (!oneshot)
		apic_write(APIC_TMICT, clocks / APIC_DIVISOR);
}

/*
 * Setup extended LVT, AMD specific
 *
 * Software should use the LVT offsets the BIOS provides.  The offsets
 * are determined by the subsystems using it like those for MCE
 * threshold or IBS.  On K8 only offset 0 (APIC500) and MCE interrupts
 * are supported. Beginning with family 10h at least 4 offsets are
 * available.
 *
 * Since the offsets must be consistent for all cores, we keep track
 * of the LVT offsets in software and reserve the offset for the same
 * vector also to be used on other cores. An offset is freed by
 * setting the entry to APIC_EILVT_MASKED.
 *
 * If the BIOS is right, there should be no conflicts. Otherwise a
 * "[Firmware Bug]: ..." error message is generated. However, if
 * software does not properly determines the offsets, it is not
 * necessarily a BIOS bug.
 */

static atomic_t eilvt_offsets[APIC_EILVT_NR_MAX];

static inline int eilvt_entry_is_changeable(unsigned int old, unsigned int new)
{
	return (old & APIC_EILVT_MASKED)
		|| (new == APIC_EILVT_MASKED)
		|| ((new & ~APIC_EILVT_MASKED) == old);
}

static unsigned int reserve_eilvt_offset(int offset, unsigned int new)
{
	unsigned int rsvd, vector;

	if (offset >= APIC_EILVT_NR_MAX)
		return ~0;

	rsvd = atomic_read(&eilvt_offsets[offset]);
	do {
		vector = rsvd & ~APIC_EILVT_MASKED;	/* 0: unassigned */
		if (vector && !eilvt_entry_is_changeable(vector, new))
			/* may not change if vectors are different */
			return rsvd;
	} while (!atomic_try_cmpxchg(&eilvt_offsets[offset], &rsvd, new));

	rsvd = new & ~APIC_EILVT_MASKED;
	if (rsvd && rsvd != vector)
		pr_info("LVT offset %d assigned for vector 0x%02x\n",
			offset, rsvd);

	return new;
}

/*
 * If mask=1, the LVT entry does not generate interrupts while mask=0
 * enables the vector. See also the BKDGs. Must be called with
 * preemption disabled.
 */

int setup_APIC_eilvt(u8 offset, u8 vector, u8 msg_type, u8 mask)
{
	unsigned long reg = APIC_EILVTn(offset);
	unsigned int new, old, reserved;

	new = (mask << 16) | (msg_type << 8) | vector;
	old = apic_read(reg);
	reserved = reserve_eilvt_offset(offset, new);

	if (reserved != new) {
		pr_err(FW_BUG "cpu %d, try to use APIC%lX (LVT offset %d) for "
		       "vector 0x%x, but the register is already in use for "
		       "vector 0x%x on another cpu\n",
		       smp_processor_id(), reg, offset, new, reserved);
		return -EINVAL;
	}

	if (!eilvt_entry_is_changeable(old, new)) {
		pr_err(FW_BUG "cpu %d, try to use APIC%lX (LVT offset %d) for "
		       "vector 0x%x, but the register is already in use for "
		       "vector 0x%x on this cpu\n",
		       smp_processor_id(), reg, offset, new, old);
		return -EBUSY;
	}

	apic_write(reg, new);

	return 0;
}
EXPORT_SYMBOL_GPL(setup_APIC_eilvt);

/*
 * Program the next event, relative to now
 */
static int lapic_next_event(unsigned long delta,
			    struct clock_event_device *evt)
{
	apic_write(APIC_TMICT, delta);
	return 0;
}

static int lapic_next_deadline(unsigned long delta,
			       struct clock_event_device *evt)
{
	u64 tsc;

	/* This MSR is special and need a special fence: */
	weak_wrmsr_fence();

	tsc = rdtsc();
	wrmsrl(MSR_IA32_TSC_DEADLINE, tsc + (((u64) delta) * TSC_DIVISOR));
	return 0;
}

static int lapic_timer_shutdown(struct clock_event_device *evt)
{
	unsigned int v;

	/* Lapic used as dummy for broadcast ? */
	if (evt->features & CLOCK_EVT_FEAT_DUMMY)
		return 0;

	v = apic_read(APIC_LVTT);
	v |= (APIC_LVT_MASKED | LOCAL_TIMER_VECTOR);
	apic_write(APIC_LVTT, v);
	apic_write(APIC_TMICT, 0);
	return 0;
}

static inline int
lapic_timer_set_periodic_oneshot(struct clock_event_device *evt, bool oneshot)
{
	/* Lapic used as dummy for broadcast ? */
	if (evt->features & CLOCK_EVT_FEAT_DUMMY)
		return 0;

	__setup_APIC_LVTT(lapic_timer_period, oneshot, 1);
	return 0;
}

static int lapic_timer_set_periodic(struct clock_event_device *evt)
{
	return lapic_timer_set_periodic_oneshot(evt, false);
}

static int lapic_timer_set_oneshot(struct clock_event_device *evt)
{
	return lapic_timer_set_periodic_oneshot(evt, true);
}

/*
 * Local APIC timer broadcast function
 */
static void lapic_timer_broadcast(const struct cpumask *mask)
{
#ifdef CONFIG_SMP
	apic->send_IPI_mask(mask, LOCAL_TIMER_VECTOR);
#endif
}


/*
 * The local apic timer can be used for any function which is CPU local.
 */
static struct clock_event_device lapic_clockevent = {
	.name				= "lapic",
	.features			= CLOCK_EVT_FEAT_PERIODIC |
					  CLOCK_EVT_FEAT_ONESHOT | CLOCK_EVT_FEAT_C3STOP
					  | CLOCK_EVT_FEAT_DUMMY,
	.shift				= 32,
	.set_state_shutdown		= lapic_timer_shutdown,
	.set_state_periodic		= lapic_timer_set_periodic,
	.set_state_oneshot		= lapic_timer_set_oneshot,
	.set_state_oneshot_stopped	= lapic_timer_shutdown,
	.set_next_event			= lapic_next_event,
	.broadcast			= lapic_timer_broadcast,
	.rating				= 100,
	.irq				= -1,
};
static DEFINE_PER_CPU(struct clock_event_device, lapic_events);

static const struct x86_cpu_id deadline_match[] __initconst = {
	X86_MATCH_INTEL_FAM6_MODEL_STEPPINGS(HASWELL_X, X86_STEPPINGS(0x2, 0x2), 0x3a), /* EP */
	X86_MATCH_INTEL_FAM6_MODEL_STEPPINGS(HASWELL_X, X86_STEPPINGS(0x4, 0x4), 0x0f), /* EX */

	X86_MATCH_INTEL_FAM6_MODEL( BROADWELL_X,	0x0b000020),

	X86_MATCH_INTEL_FAM6_MODEL_STEPPINGS(BROADWELL_D, X86_STEPPINGS(0x2, 0x2), 0x00000011),
	X86_MATCH_INTEL_FAM6_MODEL_STEPPINGS(BROADWELL_D, X86_STEPPINGS(0x3, 0x3), 0x0700000e),
	X86_MATCH_INTEL_FAM6_MODEL_STEPPINGS(BROADWELL_D, X86_STEPPINGS(0x4, 0x4), 0x0f00000c),
	X86_MATCH_INTEL_FAM6_MODEL_STEPPINGS(BROADWELL_D, X86_STEPPINGS(0x5, 0x5), 0x0e000003),

	X86_MATCH_INTEL_FAM6_MODEL_STEPPINGS(SKYLAKE_X, X86_STEPPINGS(0x3, 0x3), 0x01000136),
	X86_MATCH_INTEL_FAM6_MODEL_STEPPINGS(SKYLAKE_X, X86_STEPPINGS(0x4, 0x4), 0x02000014),
	X86_MATCH_INTEL_FAM6_MODEL_STEPPINGS(SKYLAKE_X, X86_STEPPINGS(0x5, 0xf), 0),

	X86_MATCH_INTEL_FAM6_MODEL( HASWELL,		0x22),
	X86_MATCH_INTEL_FAM6_MODEL( HASWELL_L,		0x20),
	X86_MATCH_INTEL_FAM6_MODEL( HASWELL_G,		0x17),

	X86_MATCH_INTEL_FAM6_MODEL( BROADWELL,		0x25),
	X86_MATCH_INTEL_FAM6_MODEL( BROADWELL_G,	0x17),

	X86_MATCH_INTEL_FAM6_MODEL( SKYLAKE_L,		0xb2),
	X86_MATCH_INTEL_FAM6_MODEL( SKYLAKE,		0xb2),

	X86_MATCH_INTEL_FAM6_MODEL( KABYLAKE_L,		0x52),
	X86_MATCH_INTEL_FAM6_MODEL( KABYLAKE,		0x52),

	{},
};

static __init bool apic_validate_deadline_timer(void)
{
	const struct x86_cpu_id *m;
	u32 rev;

	if (!boot_cpu_has(X86_FEATURE_TSC_DEADLINE_TIMER))
		return false;
	if (boot_cpu_has(X86_FEATURE_HYPERVISOR))
		return true;

	m = x86_match_cpu(deadline_match);
	if (!m)
		return true;

	rev = (u32)m->driver_data;

	if (boot_cpu_data.microcode >= rev)
		return true;

	setup_clear_cpu_cap(X86_FEATURE_TSC_DEADLINE_TIMER);
	pr_err(FW_BUG "TSC_DEADLINE disabled due to Errata; "
	       "please update microcode to version: 0x%x (or later)\n", rev);
	return false;
}

/*
 * Setup the local APIC timer for this CPU. Copy the initialized values
 * of the boot CPU and register the clock event in the framework.
 */
static void setup_APIC_timer(void)
{
	struct clock_event_device *levt = this_cpu_ptr(&lapic_events);

	if (this_cpu_has(X86_FEATURE_ARAT)) {
		lapic_clockevent.features &= ~CLOCK_EVT_FEAT_C3STOP;
		/* Make LAPIC timer preferable over percpu HPET */
		lapic_clockevent.rating = 150;
	}

	memcpy(levt, &lapic_clockevent, sizeof(*levt));
	levt->cpumask = cpumask_of(smp_processor_id());

	if (this_cpu_has(X86_FEATURE_TSC_DEADLINE_TIMER)) {
		levt->name = "lapic-deadline";
		levt->features &= ~(CLOCK_EVT_FEAT_PERIODIC |
				    CLOCK_EVT_FEAT_DUMMY);
		levt->set_next_event = lapic_next_deadline;
		clockevents_config_and_register(levt,
						tsc_khz * (1000 / TSC_DIVISOR),
						0xF, ~0UL);
	} else
		clockevents_register_device(levt);
}

/*
 * Install the updated TSC frequency from recalibration at the TSC
 * deadline clockevent devices.
 */
static void __lapic_update_tsc_freq(void *info)
{
	struct clock_event_device *levt = this_cpu_ptr(&lapic_events);

	if (!this_cpu_has(X86_FEATURE_TSC_DEADLINE_TIMER))
		return;

	clockevents_update_freq(levt, tsc_khz * (1000 / TSC_DIVISOR));
}

void lapic_update_tsc_freq(void)
{
	/*
	 * The clockevent device's ->mult and ->shift can both be
	 * changed. In order to avoid races, schedule the frequency
	 * update code on each CPU.
	 */
	on_each_cpu(__lapic_update_tsc_freq, NULL, 0);
}

/*
 * In this functions we calibrate APIC bus clocks to the external timer.
 *
 * We want to do the calibration only once since we want to have local timer
 * irqs synchronous. CPUs connected by the same APIC bus have the very same bus
 * frequency.
 *
 * This was previously done by reading the PIT/HPET and waiting for a wrap
 * around to find out, that a tick has elapsed. I have a box, where the PIT
 * readout is broken, so it never gets out of the wait loop again. This was
 * also reported by others.
 *
 * Monitoring the jiffies value is inaccurate and the clockevents
 * infrastructure allows us to do a simple substitution of the interrupt
 * handler.
 *
 * The calibration routine also uses the pm_timer when possible, as the PIT
 * happens to run way too slow (factor 2.3 on my VAIO CoreDuo, which goes
 * back to normal later in the boot process).
 */

#define LAPIC_CAL_LOOPS		(HZ/10)

static __initdata int lapic_cal_loops = -1;
static __initdata long lapic_cal_t1, lapic_cal_t2;
static __initdata unsigned long long lapic_cal_tsc1, lapic_cal_tsc2;
static __initdata unsigned long lapic_cal_pm1, lapic_cal_pm2;
static __initdata unsigned long lapic_cal_j1, lapic_cal_j2;

/*
 * Temporary interrupt handler and polled calibration function.
 */
static void __init lapic_cal_handler(struct clock_event_device *dev)
{
	unsigned long long tsc = 0;
	long tapic = apic_read(APIC_TMCCT);
	unsigned long pm = acpi_pm_read_early();

	if (boot_cpu_has(X86_FEATURE_TSC))
		tsc = rdtsc();

	switch (lapic_cal_loops++) {
	case 0:
		lapic_cal_t1 = tapic;
		lapic_cal_tsc1 = tsc;
		lapic_cal_pm1 = pm;
		lapic_cal_j1 = jiffies;
		break;

	case LAPIC_CAL_LOOPS:
		lapic_cal_t2 = tapic;
		lapic_cal_tsc2 = tsc;
		if (pm < lapic_cal_pm1)
			pm += ACPI_PM_OVRRUN;
		lapic_cal_pm2 = pm;
		lapic_cal_j2 = jiffies;
		break;
	}
}

static int __init
calibrate_by_pmtimer(long deltapm, long *delta, long *deltatsc)
{
	const long pm_100ms = PMTMR_TICKS_PER_SEC / 10;
	const long pm_thresh = pm_100ms / 100;
	unsigned long mult;
	u64 res;

#ifndef CONFIG_X86_PM_TIMER
	return -1;
#endif

	apic_printk(APIC_VERBOSE, "... PM-Timer delta = %ld\n", deltapm);

	/* Check, if the PM timer is available */
	if (!deltapm)
		return -1;

	mult = clocksource_hz2mult(PMTMR_TICKS_PER_SEC, 22);

	if (deltapm > (pm_100ms - pm_thresh) &&
	    deltapm < (pm_100ms + pm_thresh)) {
		apic_printk(APIC_VERBOSE, "... PM-Timer result ok\n");
		return 0;
	}

	res = (((u64)deltapm) *  mult) >> 22;
	do_div(res, 1000000);
	pr_warn("APIC calibration not consistent "
		"with PM-Timer: %ldms instead of 100ms\n", (long)res);

	/* Correct the lapic counter value */
	res = (((u64)(*delta)) * pm_100ms);
	do_div(res, deltapm);
	pr_info("APIC delta adjusted to PM-Timer: "
		"%lu (%ld)\n", (unsigned long)res, *delta);
	*delta = (long)res;

	/* Correct the tsc counter value */
	if (boot_cpu_has(X86_FEATURE_TSC)) {
		res = (((u64)(*deltatsc)) * pm_100ms);
		do_div(res, deltapm);
		apic_printk(APIC_VERBOSE, "TSC delta adjusted to "
					  "PM-Timer: %lu (%ld)\n",
					(unsigned long)res, *deltatsc);
		*deltatsc = (long)res;
	}

	return 0;
}

static int __init lapic_init_clockevent(void)
{
	if (!lapic_timer_period)
		return -1;

	/* Calculate the scaled math multiplication factor */
	lapic_clockevent.mult = div_sc(lapic_timer_period/APIC_DIVISOR,
					TICK_NSEC, lapic_clockevent.shift);
	lapic_clockevent.max_delta_ns =
		clockevent_delta2ns(0x7FFFFFFF, &lapic_clockevent);
	lapic_clockevent.max_delta_ticks = 0x7FFFFFFF;
	lapic_clockevent.min_delta_ns =
		clockevent_delta2ns(0xF, &lapic_clockevent);
	lapic_clockevent.min_delta_ticks = 0xF;

	return 0;
}

bool __init apic_needs_pit(void)
{
	/*
	 * If the frequencies are not known, PIT is required for both TSC
	 * and apic timer calibration.
	 */
	if (!tsc_khz || !cpu_khz)
		return true;

	/* Is there an APIC at all or is it disabled? */
	if (!boot_cpu_has(X86_FEATURE_APIC) || disable_apic)
		return true;

	/*
	 * If interrupt delivery mode is legacy PIC or virtual wire without
	 * configuration, the local APIC timer wont be set up. Make sure
	 * that the PIT is initialized.
	 */
	if (apic_intr_mode == APIC_PIC ||
	    apic_intr_mode == APIC_VIRTUAL_WIRE_NO_CONFIG)
		return true;

	/* Virt guests may lack ARAT, but still have DEADLINE */
	if (!boot_cpu_has(X86_FEATURE_ARAT))
		return true;

	/* Deadline timer is based on TSC so no further PIT action required */
	if (boot_cpu_has(X86_FEATURE_TSC_DEADLINE_TIMER))
		return false;

	/* APIC timer disabled? */
	if (disable_apic_timer)
		return true;
	/*
	 * The APIC timer frequency is known already, no PIT calibration
	 * required. If unknown, let the PIT be initialized.
	 */
	return lapic_timer_period == 0;
}

static int __init calibrate_APIC_clock(void)
{
	struct clock_event_device *levt = this_cpu_ptr(&lapic_events);
	u64 tsc_perj = 0, tsc_start = 0;
	unsigned long jif_start;
	unsigned long deltaj;
	long delta, deltatsc;
	int pm_referenced = 0;

	if (boot_cpu_has(X86_FEATURE_TSC_DEADLINE_TIMER))
		return 0;

	/*
	 * Check if lapic timer has already been calibrated by platform
	 * specific routine, such as tsc calibration code. If so just fill
	 * in the clockevent structure and return.
	 */
	if (!lapic_init_clockevent()) {
		apic_printk(APIC_VERBOSE, "lapic timer already calibrated %d\n",
			    lapic_timer_period);
		/*
		 * Direct calibration methods must have an always running
		 * local APIC timer, no need for broadcast timer.
		 */
		lapic_clockevent.features &= ~CLOCK_EVT_FEAT_DUMMY;
		return 0;
	}

	apic_printk(APIC_VERBOSE, "Using local APIC timer interrupts.\n"
		    "calibrating APIC timer ...\n");

	/*
	 * There are platforms w/o global clockevent devices. Instead of
	 * making the calibration conditional on that, use a polling based
	 * approach everywhere.
	 */
	local_irq_disable();

	/*
	 * Setup the APIC counter to maximum. There is no way the lapic
	 * can underflow in the 100ms detection time frame
	 */
	__setup_APIC_LVTT(0xffffffff, 0, 0);

	/*
	 * Methods to terminate the calibration loop:
	 *  1) Global clockevent if available (jiffies)
	 *  2) TSC if available and frequency is known
	 */
	jif_start = READ_ONCE(jiffies);

	if (tsc_khz) {
		tsc_start = rdtsc();
		tsc_perj = div_u64((u64)tsc_khz * 1000, HZ);
	}

	/*
	 * Enable interrupts so the tick can fire, if a global
	 * clockevent device is available
	 */
	local_irq_enable();

	while (lapic_cal_loops <= LAPIC_CAL_LOOPS) {
		/* Wait for a tick to elapse */
		while (1) {
			if (tsc_khz) {
				u64 tsc_now = rdtsc();
				if ((tsc_now - tsc_start) >= tsc_perj) {
					tsc_start += tsc_perj;
					break;
				}
			} else {
				unsigned long jif_now = READ_ONCE(jiffies);

				if (time_after(jif_now, jif_start)) {
					jif_start = jif_now;
					break;
				}
			}
			cpu_relax();
		}

		/* Invoke the calibration routine */
		local_irq_disable();
		lapic_cal_handler(NULL);
		local_irq_enable();
	}

	local_irq_disable();

	/* Build delta t1-t2 as apic timer counts down */
	delta = lapic_cal_t1 - lapic_cal_t2;
	apic_printk(APIC_VERBOSE, "... lapic delta = %ld\n", delta);

	deltatsc = (long)(lapic_cal_tsc2 - lapic_cal_tsc1);

	/* we trust the PM based calibration if possible */
	pm_referenced = !calibrate_by_pmtimer(lapic_cal_pm2 - lapic_cal_pm1,
					&delta, &deltatsc);

	lapic_timer_period = (delta * APIC_DIVISOR) / LAPIC_CAL_LOOPS;
	lapic_init_clockevent();

	apic_printk(APIC_VERBOSE, "..... delta %ld\n", delta);
	apic_printk(APIC_VERBOSE, "..... mult: %u\n", lapic_clockevent.mult);
	apic_printk(APIC_VERBOSE, "..... calibration result: %u\n",
		    lapic_timer_period);

	if (boot_cpu_has(X86_FEATURE_TSC)) {
		apic_printk(APIC_VERBOSE, "..... CPU clock speed is "
			    "%ld.%04ld MHz.\n",
			    (deltatsc / LAPIC_CAL_LOOPS) / (1000000 / HZ),
			    (deltatsc / LAPIC_CAL_LOOPS) % (1000000 / HZ));
	}

	apic_printk(APIC_VERBOSE, "..... host bus clock speed is "
		    "%u.%04u MHz.\n",
		    lapic_timer_period / (1000000 / HZ),
		    lapic_timer_period % (1000000 / HZ));

	/*
	 * Do a sanity check on the APIC calibration result
	 */
	if (lapic_timer_period < (1000000 / HZ)) {
		local_irq_enable();
		pr_warn("APIC frequency too slow, disabling apic timer\n");
		return -1;
	}

	levt->features &= ~CLOCK_EVT_FEAT_DUMMY;

	/*
	 * PM timer calibration failed or not turned on so lets try APIC
	 * timer based calibration, if a global clockevent device is
	 * available.
	 */
	if (!pm_referenced && global_clock_event) {
		apic_printk(APIC_VERBOSE, "... verify APIC timer\n");

		/*
		 * Setup the apic timer manually
		 */
		levt->event_handler = lapic_cal_handler;
		lapic_timer_set_periodic(levt);
		lapic_cal_loops = -1;

		/* Let the interrupts run */
		local_irq_enable();

		while (lapic_cal_loops <= LAPIC_CAL_LOOPS)
			cpu_relax();

		/* Stop the lapic timer */
		local_irq_disable();
		lapic_timer_shutdown(levt);

		/* Jiffies delta */
		deltaj = lapic_cal_j2 - lapic_cal_j1;
		apic_printk(APIC_VERBOSE, "... jiffies delta = %lu\n", deltaj);

		/* Check, if the jiffies result is consistent */
		if (deltaj >= LAPIC_CAL_LOOPS-2 && deltaj <= LAPIC_CAL_LOOPS+2)
			apic_printk(APIC_VERBOSE, "... jiffies result ok\n");
		else
			levt->features |= CLOCK_EVT_FEAT_DUMMY;
	}
	local_irq_enable();

	if (levt->features & CLOCK_EVT_FEAT_DUMMY) {
		pr_warn("APIC timer disabled due to verification failure\n");
		return -1;
	}

	return 0;
}

/*
 * Setup the boot APIC
 *
 * Calibrate and verify the result.
 */
void __init setup_boot_APIC_clock(void)
{
	/*
	 * The local apic timer can be disabled via the kernel
	 * commandline or from the CPU detection code. Register the lapic
	 * timer as a dummy clock event source on SMP systems, so the
	 * broadcast mechanism is used. On UP systems simply ignore it.
	 */
	if (disable_apic_timer) {
		pr_info("Disabling APIC timer\n");
		/* No broadcast on UP ! */
		if (num_possible_cpus() > 1) {
			lapic_clockevent.mult = 1;
			setup_APIC_timer();
		}
		return;
	}

	if (calibrate_APIC_clock()) {
		/* No broadcast on UP ! */
		if (num_possible_cpus() > 1)
			setup_APIC_timer();
		return;
	}

	/*
	 * If nmi_watchdog is set to IO_APIC, we need the
	 * PIT/HPET going.  Otherwise register lapic as a dummy
	 * device.
	 */
	lapic_clockevent.features &= ~CLOCK_EVT_FEAT_DUMMY;

	/* Setup the lapic or request the broadcast */
	setup_APIC_timer();
	amd_e400_c1e_apic_setup();
}

void setup_secondary_APIC_clock(void)
{
	setup_APIC_timer();
	amd_e400_c1e_apic_setup();
}

/*
 * The guts of the apic timer interrupt
 */
static void local_apic_timer_interrupt(void)
{
	struct clock_event_device *evt = this_cpu_ptr(&lapic_events);

	/*
	 * Normally we should not be here till LAPIC has been initialized but
	 * in some cases like kdump, its possible that there is a pending LAPIC
	 * timer interrupt from previous kernel's context and is delivered in
	 * new kernel the moment interrupts are enabled.
	 *
	 * Interrupts are enabled early and LAPIC is setup much later, hence
	 * its possible that when we get here evt->event_handler is NULL.
	 * Check for event_handler being NULL and discard the interrupt as
	 * spurious.
	 */
	if (!evt->event_handler) {
		pr_warn("Spurious LAPIC timer interrupt on cpu %d\n",
			smp_processor_id());
		/* Switch it off */
		lapic_timer_shutdown(evt);
		return;
	}

	/*
	 * the NMI deadlock-detector uses this.
	 */
	inc_irq_stat(apic_timer_irqs);

	evt->event_handler(evt);
}

/*
 * Local APIC timer interrupt. This is the most natural way for doing
 * local interrupts, but local timer interrupts can be emulated by
 * broadcast interrupts too. [in case the hw doesn't support APIC timers]
 *
 * [ if a single-CPU system runs an SMP kernel then we call the local
 *   interrupt as well. Thus we cannot inline the local irq ... ]
 */
DEFINE_IDTENTRY_SYSVEC(sysvec_apic_timer_interrupt)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	ack_APIC_irq();
	trace_local_timer_entry(LOCAL_TIMER_VECTOR);
	local_apic_timer_interrupt();
	trace_local_timer_exit(LOCAL_TIMER_VECTOR);

	set_irq_regs(old_regs);
}

/*
 * Local APIC start and shutdown
 */

/**
 * clear_local_APIC - shutdown the local APIC
 *
 * This is called, when a CPU is disabled and before rebooting, so the state of
 * the local APIC has no dangling leftovers. Also used to cleanout any BIOS
 * leftovers during boot.
 */
void clear_local_APIC(void)
{
	int maxlvt;
	u32 v;

	/* APIC hasn't been mapped yet */
	if (!x2apic_mode && !apic_phys)
		return;

	maxlvt = lapic_get_maxlvt();
	/*
	 * Masking an LVT entry can trigger a local APIC error
	 * if the vector is zero. Mask LVTERR first to prevent this.
	 */
	if (maxlvt >= 3) {
		v = ERROR_APIC_VECTOR; /* any non-zero vector will do */
		apic_write(APIC_LVTERR, v | APIC_LVT_MASKED);
	}
	/*
	 * Careful: we have to set masks only first to deassert
	 * any level-triggered sources.
	 */
	v = apic_read(APIC_LVTT);
	apic_write(APIC_LVTT, v | APIC_LVT_MASKED);
	v = apic_read(APIC_LVT0);
	apic_write(APIC_LVT0, v | APIC_LVT_MASKED);
	v = apic_read(APIC_LVT1);
	apic_write(APIC_LVT1, v | APIC_LVT_MASKED);
	if (maxlvt >= 4) {
		v = apic_read(APIC_LVTPC);
		apic_write(APIC_LVTPC, v | APIC_LVT_MASKED);
	}

	/* lets not touch this if we didn't frob it */
#ifdef CONFIG_X86_THERMAL_VECTOR
	if (maxlvt >= 5) {
		v = apic_read(APIC_LVTTHMR);
		apic_write(APIC_LVTTHMR, v | APIC_LVT_MASKED);
	}
#endif
#ifdef CONFIG_X86_MCE_INTEL
	if (maxlvt >= 6) {
		v = apic_read(APIC_LVTCMCI);
		if (!(v & APIC_LVT_MASKED))
			apic_write(APIC_LVTCMCI, v | APIC_LVT_MASKED);
	}
#endif

	/*
	 * Clean APIC state for other OSs:
	 */
	apic_write(APIC_LVTT, APIC_LVT_MASKED);
	apic_write(APIC_LVT0, APIC_LVT_MASKED);
	apic_write(APIC_LVT1, APIC_LVT_MASKED);
	if (maxlvt >= 3)
		apic_write(APIC_LVTERR, APIC_LVT_MASKED);
	if (maxlvt >= 4)
		apic_write(APIC_LVTPC, APIC_LVT_MASKED);

	/* Integrated APIC (!82489DX) ? */
	if (lapic_is_integrated()) {
		if (maxlvt > 3)
			/* Clear ESR due to Pentium errata 3AP and 11AP */
			apic_write(APIC_ESR, 0);
		apic_read(APIC_ESR);
	}
}

/**
 * apic_soft_disable - Clears and software disables the local APIC on hotplug
 *
 * Contrary to disable_local_APIC() this does not touch the enable bit in
 * MSR_IA32_APICBASE. Clearing that bit on systems based on the 3 wire APIC
 * bus would require a hardware reset as the APIC would lose track of bus
 * arbitration. On systems with FSB delivery APICBASE could be disabled,
 * but it has to be guaranteed that no interrupt is sent to the APIC while
 * in that state and it's not clear from the SDM whether it still responds
 * to INIT/SIPI messages. Stay on the safe side and use software disable.
 */
void apic_soft_disable(void)
{
	u32 value;

	clear_local_APIC();

	/* Soft disable APIC (implies clearing of registers for 82489DX!). */
	value = apic_read(APIC_SPIV);
	value &= ~APIC_SPIV_APIC_ENABLED;
	apic_write(APIC_SPIV, value);
}

/**
 * disable_local_APIC - clear and disable the local APIC
 */
void disable_local_APIC(void)
{
	/* APIC hasn't been mapped yet */
	if (!x2apic_mode && !apic_phys)
		return;

	apic_soft_disable();

#ifdef CONFIG_X86_32
	/*
	 * When LAPIC was disabled by the BIOS and enabled by the kernel,
	 * restore the disabled state.
	 */
	if (enabled_via_apicbase) {
		unsigned int l, h;

		rdmsr(MSR_IA32_APICBASE, l, h);
		l &= ~MSR_IA32_APICBASE_ENABLE;
		wrmsr(MSR_IA32_APICBASE, l, h);
	}
#endif
}

/*
 * If Linux enabled the LAPIC against the BIOS default disable it down before
 * re-entering the BIOS on shutdown.  Otherwise the BIOS may get confused and
 * not power-off.  Additionally clear all LVT entries before disable_local_APIC
 * for the case where Linux didn't enable the LAPIC.
 */
void lapic_shutdown(void)
{
	unsigned long flags;

	if (!boot_cpu_has(X86_FEATURE_APIC) && !apic_from_smp_config())
		return;

	local_irq_save(flags);

#ifdef CONFIG_X86_32
	if (!enabled_via_apicbase)
		clear_local_APIC();
	else
#endif
		disable_local_APIC();


	local_irq_restore(flags);
}

/**
 * sync_Arb_IDs - synchronize APIC bus arbitration IDs
 */
void __init sync_Arb_IDs(void)
{
	/*
	 * Unsupported on P4 - see Intel Dev. Manual Vol. 3, Ch. 8.6.1 And not
	 * needed on AMD.
	 */
	if (modern_apic() || boot_cpu_data.x86_vendor == X86_VENDOR_AMD)
		return;

	/*
	 * Wait for idle.
	 */
	apic_wait_icr_idle();

	apic_printk(APIC_DEBUG, "Synchronizing Arb IDs.\n");
	apic_write(APIC_ICR, APIC_DEST_ALLINC |
			APIC_INT_LEVELTRIG | APIC_DM_INIT);
}

enum apic_intr_mode_id apic_intr_mode __ro_after_init;

static int __init __apic_intr_mode_select(void)
{
	/* Check kernel option */
	if (disable_apic) {
		pr_info("APIC disabled via kernel command line\n");
		return APIC_PIC;
	}

	/* Check BIOS */
#ifdef CONFIG_X86_64
	/* On 64-bit, the APIC must be integrated, Check local APIC only */
	if (!boot_cpu_has(X86_FEATURE_APIC)) {
		disable_apic = 1;
		pr_info("APIC disabled by BIOS\n");
		return APIC_PIC;
	}
#else
	/* On 32-bit, the APIC may be integrated APIC or 82489DX */

	/* Neither 82489DX nor integrated APIC ? */
	if (!boot_cpu_has(X86_FEATURE_APIC) && !smp_found_config) {
		disable_apic = 1;
		return APIC_PIC;
	}

	/* If the BIOS pretends there is an integrated APIC ? */
	if (!boot_cpu_has(X86_FEATURE_APIC) &&
		APIC_INTEGRATED(boot_cpu_apic_version)) {
		disable_apic = 1;
		pr_err(FW_BUG "Local APIC %d not detected, force emulation\n",
				       boot_cpu_physical_apicid);
		return APIC_PIC;
	}
#endif

	/* Check MP table or ACPI MADT configuration */
	if (!smp_found_config) {
		disable_ioapic_support();
		if (!acpi_lapic) {
			pr_info("APIC: ACPI MADT or MP tables are not detected\n");
			return APIC_VIRTUAL_WIRE_NO_CONFIG;
		}
		return APIC_VIRTUAL_WIRE;
	}

#ifdef CONFIG_SMP
	/* If SMP should be disabled, then really disable it! */
	if (!setup_max_cpus) {
		pr_info("APIC: SMP mode deactivated\n");
		return APIC_SYMMETRIC_IO_NO_ROUTING;
	}

	if (read_apic_id() != boot_cpu_physical_apicid) {
		panic("Boot APIC ID in local APIC unexpected (%d vs %d)",
		     read_apic_id(), boot_cpu_physical_apicid);
		/* Or can we switch back to PIC here? */
	}
#endif

	return APIC_SYMMETRIC_IO;
}

/* Select the interrupt delivery mode for the BSP */
void __init apic_intr_mode_select(void)
{
	apic_intr_mode = __apic_intr_mode_select();
}

/*
 * An initial setup of the virtual wire mode.
 */
void __init init_bsp_APIC(void)
{
	unsigned int value;

	/*
	 * Don't do the setup now if we have a SMP BIOS as the
	 * through-I/O-APIC virtual wire mode might be active.
	 */
	if (smp_found_config || !boot_cpu_has(X86_FEATURE_APIC))
		return;

	/*
	 * Do not trust the local APIC being empty at bootup.
	 */
	clear_local_APIC();

	/*
	 * Enable APIC.
	 */
	value = apic_read(APIC_SPIV);
	value &= ~APIC_VECTOR_MASK;
	value |= APIC_SPIV_APIC_ENABLED;

#ifdef CONFIG_X86_32
	/* This bit is reserved on P4/Xeon and should be cleared */
	if ((boot_cpu_data.x86_vendor == X86_VENDOR_INTEL) &&
	    (boot_cpu_data.x86 == 15))
		value &= ~APIC_SPIV_FOCUS_DISABLED;
	else
#endif
		value |= APIC_SPIV_FOCUS_DISABLED;
	value |= SPURIOUS_APIC_VECTOR;
	apic_write(APIC_SPIV, value);

	/*
	 * Set up the virtual wire mode.
	 */
	apic_write(APIC_LVT0, APIC_DM_EXTINT);
	value = APIC_DM_NMI;
	if (!lapic_is_integrated())		/* 82489DX */
		value |= APIC_LVT_LEVEL_TRIGGER;
	if (apic_extnmi == APIC_EXTNMI_NONE)
		value |= APIC_LVT_MASKED;
	apic_write(APIC_LVT1, value);
}

static void __init apic_bsp_setup(bool upmode);

/* Init the interrupt delivery mode for the BSP */
void __init apic_intr_mode_init(void)
{
	bool upmode = IS_ENABLED(CONFIG_UP_LATE_INIT);

	switch (apic_intr_mode) {
	case APIC_PIC:
		pr_info("APIC: Keep in PIC mode(8259)\n");
		return;
	case APIC_VIRTUAL_WIRE:
		pr_info("APIC: Switch to virtual wire mode setup\n");
		break;
	case APIC_VIRTUAL_WIRE_NO_CONFIG:
		pr_info("APIC: Switch to virtual wire mode setup with no configuration\n");
		upmode = true;
		break;
	case APIC_SYMMETRIC_IO:
		pr_info("APIC: Switch to symmetric I/O mode setup\n");
		break;
	case APIC_SYMMETRIC_IO_NO_ROUTING:
		pr_info("APIC: Switch to symmetric I/O mode setup in no SMP routine\n");
		break;
	}

	default_setup_apic_routing();

	if (x86_platform.apic_post_init)
		x86_platform.apic_post_init();

	apic_bsp_setup(upmode);
}

static void lapic_setup_esr(void)
{
	unsigned int oldvalue, value, maxlvt;

	if (!lapic_is_integrated()) {
		pr_info("No ESR for 82489DX.\n");
		return;
	}

	if (apic->disable_esr) {
		/*
		 * Something untraceable is creating bad interrupts on
		 * secondary quads ... for the moment, just leave the
		 * ESR disabled - we can't do anything useful with the
		 * errors anyway - mbligh
		 */
		pr_info("Leaving ESR disabled.\n");
		return;
	}

	maxlvt = lapic_get_maxlvt();
	if (maxlvt > 3)		/* Due to the Pentium erratum 3AP. */
		apic_write(APIC_ESR, 0);
	oldvalue = apic_read(APIC_ESR);

	/* enables sending errors */
	value = ERROR_APIC_VECTOR;
	apic_write(APIC_LVTERR, value);

	/*
	 * spec says clear errors after enabling vector.
	 */
	if (maxlvt > 3)
		apic_write(APIC_ESR, 0);
	value = apic_read(APIC_ESR);
	if (value != oldvalue)
		apic_printk(APIC_VERBOSE, "ESR value before enabling "
			"vector: 0x%08x  after: 0x%08x\n",
			oldvalue, value);
}

#define APIC_IR_REGS		APIC_ISR_NR
#define APIC_IR_BITS		(APIC_IR_REGS * 32)
#define APIC_IR_MAPSIZE		(APIC_IR_BITS / BITS_PER_LONG)

union apic_ir {
	unsigned long	map[APIC_IR_MAPSIZE];
	u32		regs[APIC_IR_REGS];
};

static bool apic_check_and_ack(union apic_ir *irr, union apic_ir *isr)
{
	int i, bit;

	/* Read the IRRs */
	for (i = 0; i < APIC_IR_REGS; i++)
		irr->regs[i] = apic_read(APIC_IRR + i * 0x10);

	/* Read the ISRs */
	for (i = 0; i < APIC_IR_REGS; i++)
		isr->regs[i] = apic_read(APIC_ISR + i * 0x10);

	/*
	 * If the ISR map is not empty. ACK the APIC and run another round
	 * to verify whether a pending IRR has been unblocked and turned
	 * into a ISR.
	 */
	if (!bitmap_empty(isr->map, APIC_IR_BITS)) {
		/*
		 * There can be multiple ISR bits set when a high priority
		 * interrupt preempted a lower priority one. Issue an ACK
		 * per set bit.
		 */
		for_each_set_bit(bit, isr->map, APIC_IR_BITS)
			ack_APIC_irq();
		return true;
	}

	return !bitmap_empty(irr->map, APIC_IR_BITS);
}

/*
 * After a crash, we no longer service the interrupts and a pending
 * interrupt from previous kernel might still have ISR bit set.
 *
 * Most probably by now the CPU has serviced that pending interrupt and it
 * might not have done the ack_APIC_irq() because it thought, interrupt
 * came from i8259 as ExtInt. LAPIC did not get EOI so it does not clear
 * the ISR bit and cpu thinks it has already serviced the interrupt. Hence
 * a vector might get locked. It was noticed for timer irq (vector
 * 0x31). Issue an extra EOI to clear ISR.
 *
 * If there are pending IRR bits they turn into ISR bits after a higher
 * priority ISR bit has been acked.
 */
static void apic_pending_intr_clear(void)
{
	union apic_ir irr, isr;
	unsigned int i;

	/* 512 loops are way oversized and give the APIC a chance to obey. */
	for (i = 0; i < 512; i++) {
		if (!apic_check_and_ack(&irr, &isr))
			return;
	}
	/* Dump the IRR/ISR content if that failed */
	pr_warn("APIC: Stale IRR: %256pb ISR: %256pb\n", irr.map, isr.map);
}

/**
 * setup_local_APIC - setup the local APIC
 *
 * Used to setup local APIC while initializing BSP or bringing up APs.
 * Always called with preemption disabled.
 */
static void setup_local_APIC(void)
{
	int cpu = smp_processor_id();
	unsigned int value;

	if (disable_apic) {
		disable_ioapic_support();
		return;
	}

	/*
	 * If this comes from kexec/kcrash the APIC might be enabled in
	 * SPIV. Soft disable it before doing further initialization.
	 */
	value = apic_read(APIC_SPIV);
	value &= ~APIC_SPIV_APIC_ENABLED;
	apic_write(APIC_SPIV, value);

#ifdef CONFIG_X86_32
	/* Pound the ESR really hard over the head with a big hammer - mbligh */
	if (lapic_is_integrated() && apic->disable_esr) {
		apic_write(APIC_ESR, 0);
		apic_write(APIC_ESR, 0);
		apic_write(APIC_ESR, 0);
		apic_write(APIC_ESR, 0);
	}
#endif
	/*
	 * Double-check whether this APIC is really registered.
	 * This is meaningless in clustered apic mode, so we skip it.
	 */
	BUG_ON(!apic->apic_id_registered());

	/*
	 * Intel recommends to set DFR, LDR and TPR before enabling
	 * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
	 * document number 292116).  So here it goes...
	 */
	apic->init_apic_ldr();

#ifdef CONFIG_X86_32
	if (apic->dest_mode_logical) {
		int logical_apicid, ldr_apicid;

		/*
		 * APIC LDR is initialized.  If logical_apicid mapping was
		 * initialized during get_smp_config(), make sure it matches
		 * the actual value.
		 */
		logical_apicid = early_per_cpu(x86_cpu_to_logical_apicid, cpu);
		ldr_apicid = GET_APIC_LOGICAL_ID(apic_read(APIC_LDR));
		if (logical_apicid != BAD_APICID)
			WARN_ON(logical_apicid != ldr_apicid);
		/* Always use the value from LDR. */
		early_per_cpu(x86_cpu_to_logical_apicid, cpu) = ldr_apicid;
	}
#endif

	/*
	 * Set Task Priority to 'accept all except vectors 0-31'.  An APIC
	 * vector in the 16-31 range could be delivered if TPR == 0, but we
	 * would think it's an exception and terrible things will happen.  We
	 * never change this later on.
	 */
	value = apic_read(APIC_TASKPRI);
	value &= ~APIC_TPRI_MASK;
	value |= 0x10;
	apic_write(APIC_TASKPRI, value);

	/* Clear eventually stale ISR/IRR bits */
	apic_pending_intr_clear();

	/*
	 * Now that we are all set up, enable the APIC
	 */
	value = apic_read(APIC_SPIV);
	value &= ~APIC_VECTOR_MASK;
	/*
	 * Enable APIC
	 */
	value |= APIC_SPIV_APIC_ENABLED;

#ifdef CONFIG_X86_32
	/*
	 * Some unknown Intel IO/APIC (or APIC) errata is biting us with
	 * certain networking cards. If high frequency interrupts are
	 * happening on a particular IOAPIC pin, plus the IOAPIC routing
	 * entry is masked/unmasked at a high rate as well then sooner or
	 * later IOAPIC line gets 'stuck', no more interrupts are received
	 * from the device. If focus CPU is disabled then the hang goes
	 * away, oh well :-(
	 *
	 * [ This bug can be reproduced easily with a level-triggered
	 *   PCI Ne2000 networking cards and PII/PIII processors, dual
	 *   BX chipset. ]
	 */
	/*
	 * Actually disabling the focus CPU check just makes the hang less
	 * frequent as it makes the interrupt distribution model be more
	 * like LRU than MRU (the short-term load is more even across CPUs).
	 */

	/*
	 * - enable focus processor (bit==0)
	 * - 64bit mode always use processor focus
	 *   so no need to set it
	 */
	value &= ~APIC_SPIV_FOCUS_DISABLED;
#endif

	/*
	 * Set spurious IRQ vector
	 */
	value |= SPURIOUS_APIC_VECTOR;
	apic_write(APIC_SPIV, value);

	perf_events_lapic_init();

	/*
	 * Set up LVT0, LVT1:
	 *
	 * set up through-local-APIC on the boot CPU's LINT0. This is not
	 * strictly necessary in pure symmetric-IO mode, but sometimes
	 * we delegate interrupts to the 8259A.
	 */
	/*
	 * TODO: set up through-local-APIC from through-I/O-APIC? --macro
	 */
	value = apic_read(APIC_LVT0) & APIC_LVT_MASKED;
	if (!cpu && (pic_mode || !value || skip_ioapic_setup)) {
		value = APIC_DM_EXTINT;
		apic_printk(APIC_VERBOSE, "enabled ExtINT on CPU#%d\n", cpu);
	} else {
		value = APIC_DM_EXTINT | APIC_LVT_MASKED;
		apic_printk(APIC_VERBOSE, "masked ExtINT on CPU#%d\n", cpu);
	}
	apic_write(APIC_LVT0, value);

	/*
	 * Only the BSP sees the LINT1 NMI signal by default. This can be
	 * modified by apic_extnmi= boot option.
	 */
	if ((!cpu && apic_extnmi != APIC_EXTNMI_NONE) ||
	    apic_extnmi == APIC_EXTNMI_ALL)
		value = APIC_DM_NMI;
	else
		value = APIC_DM_NMI | APIC_LVT_MASKED;

	/* Is 82489DX ? */
	if (!lapic_is_integrated())
		value |= APIC_LVT_LEVEL_TRIGGER;
	apic_write(APIC_LVT1, value);

#ifdef CONFIG_X86_MCE_INTEL
	/* Recheck CMCI information after local APIC is up on CPU #0 */
	if (!cpu)
		cmci_recheck();
#endif
}

static void end_local_APIC_setup(void)
{
	lapic_setup_esr();

#ifdef CONFIG_X86_32
	{
		unsigned int value;
		/* Disable the local apic timer */
		value = apic_read(APIC_LVTT);
		value |= (APIC_LVT_MASKED | LOCAL_TIMER_VECTOR);
		apic_write(APIC_LVTT, value);
	}
#endif

	apic_pm_activate();
}

/*
 * APIC setup function for application processors. Called from smpboot.c
 */
void apic_ap_setup(void)
{
	setup_local_APIC();
	end_local_APIC_setup();
}

#ifdef CONFIG_X86_X2APIC
int x2apic_mode;
EXPORT_SYMBOL_GPL(x2apic_mode);

enum {
	X2APIC_OFF,
	X2APIC_DISABLED,
	/* All states below here have X2APIC enabled */
	X2APIC_ON,
	X2APIC_ON_LOCKED
};
static int x2apic_state;

static bool x2apic_hw_locked(void)
{
	u64 ia32_cap;
	u64 msr;

	ia32_cap = x86_read_arch_cap_msr();
	if (ia32_cap & ARCH_CAP_XAPIC_DISABLE) {
		rdmsrl(MSR_IA32_XAPIC_DISABLE_STATUS, msr);
		return (msr & LEGACY_XAPIC_DISABLED);
	}
	return false;
}

static void __x2apic_disable(void)
{
	u64 msr;

	if (!boot_cpu_has(X86_FEATURE_APIC))
		return;

	rdmsrl(MSR_IA32_APICBASE, msr);
	if (!(msr & X2APIC_ENABLE))
		return;
	/* Disable xapic and x2apic first and then reenable xapic mode */
	wrmsrl(MSR_IA32_APICBASE, msr & ~(X2APIC_ENABLE | XAPIC_ENABLE));
	wrmsrl(MSR_IA32_APICBASE, msr & ~X2APIC_ENABLE);
	printk_once(KERN_INFO "x2apic disabled\n");
}

static void __x2apic_enable(void)
{
	u64 msr;

	rdmsrl(MSR_IA32_APICBASE, msr);
	if (msr & X2APIC_ENABLE)
		return;
	wrmsrl(MSR_IA32_APICBASE, msr | X2APIC_ENABLE);
	printk_once(KERN_INFO "x2apic enabled\n");
}

static int __init setup_nox2apic(char *str)
{
	if (x2apic_enabled()) {
		int apicid = native_apic_msr_read(APIC_ID);

		if (apicid >= 255) {
			pr_warn("Apicid: %08x, cannot enforce nox2apic\n",
				apicid);
			return 0;
		}
		if (x2apic_hw_locked()) {
			pr_warn("APIC locked in x2apic mode, can't disable\n");
			return 0;
		}
		pr_warn("x2apic already enabled.\n");
		__x2apic_disable();
	}
	setup_clear_cpu_cap(X86_FEATURE_X2APIC);
	x2apic_state = X2APIC_DISABLED;
	x2apic_mode = 0;
	return 0;
}
early_param("nox2apic", setup_nox2apic);

/* Called from cpu_init() to enable x2apic on (secondary) cpus */
void x2apic_setup(void)
{
	/*
	 * Try to make the AP's APIC state match that of the BSP,  but if the
	 * BSP is unlocked and the AP is locked then there is a state mismatch.
	 * Warn about the mismatch in case a GP fault occurs due to a locked AP
	 * trying to be turned off.
	 */
	if (x2apic_state != X2APIC_ON_LOCKED && x2apic_hw_locked())
		pr_warn("x2apic lock mismatch between BSP and AP.\n");
	/*
	 * If x2apic is not in ON or LOCKED state, disable it if already enabled
	 * from BIOS.
	 */
	if (x2apic_state < X2APIC_ON) {
		__x2apic_disable();
		return;
	}
	__x2apic_enable();
}

static __init void x2apic_disable(void)
{
	u32 x2apic_id, state = x2apic_state;

	x2apic_mode = 0;
	x2apic_state = X2APIC_DISABLED;

	if (state != X2APIC_ON)
		return;

	x2apic_id = read_apic_id();
	if (x2apic_id >= 255)
		panic("Cannot disable x2apic, id: %08x\n", x2apic_id);

	if (x2apic_hw_locked()) {
		pr_warn("Cannot disable locked x2apic, id: %08x\n", x2apic_id);
		return;
	}

	__x2apic_disable();
	register_lapic_address(mp_lapic_addr);
}

static __init void x2apic_enable(void)
{
	if (x2apic_state != X2APIC_OFF)
		return;

	x2apic_mode = 1;
	x2apic_state = X2APIC_ON;
	__x2apic_enable();
}

static __init void try_to_enable_x2apic(int remap_mode)
{
	if (x2apic_state == X2APIC_DISABLED)
		return;

	if (remap_mode != IRQ_REMAP_X2APIC_MODE) {
		u32 apic_limit = 255;

		/*
		 * Using X2APIC without IR is not architecturally supported
		 * on bare metal but may be supported in guests.
		 */
		if (!x86_init.hyper.x2apic_available()) {
			pr_info("x2apic: IRQ remapping doesn't support X2APIC mode\n");
			x2apic_disable();
			return;
		}

		/*
		 * If the hypervisor supports extended destination ID in
		 * MSI, that increases the maximum APIC ID that can be
		 * used for non-remapped IRQ domains.
		 */
		if (x86_init.hyper.msi_ext_dest_id()) {
			virt_ext_dest_id = 1;
			apic_limit = 32767;
		}

		/*
		 * Without IR, all CPUs can be addressed by IOAPIC/MSI only
		 * in physical mode, and CPUs with an APIC ID that cannot
		 * be addressed must not be brought online.
		 */
		x2apic_set_max_apicid(apic_limit);
		x2apic_phys = 1;
	}
	x2apic_enable();
}

void __init check_x2apic(void)
{
	if (x2apic_enabled()) {
		pr_info("x2apic: enabled by BIOS, switching to x2apic ops\n");
		x2apic_mode = 1;
		if (x2apic_hw_locked())
			x2apic_state = X2APIC_ON_LOCKED;
		else
			x2apic_state = X2APIC_ON;
	} else if (!boot_cpu_has(X86_FEATURE_X2APIC)) {
		x2apic_state = X2APIC_DISABLED;
	}
}
#else /* CONFIG_X86_X2APIC */
void __init check_x2apic(void)
{
	if (!apic_is_x2apic_enabled())
		return;
	/*
	 * Checkme: Can we simply turn off x2APIC here instead of disabling the APIC?
	 */
	pr_err("Kernel does not support x2APIC, please recompile with CONFIG_X86_X2APIC.\n");
	pr_err("Disabling APIC, expect reduced performance and functionality.\n");

	disable_apic = 1;
	setup_clear_cpu_cap(X86_FEATURE_APIC);
}

static inline void try_to_enable_x2apic(int remap_mode) { }
static inline void __x2apic_enable(void) { }
#endif /* !CONFIG_X86_X2APIC */

void __init enable_IR_x2apic(void)
{
	unsigned long flags;
	int ret, ir_stat;

	if (skip_ioapic_setup) {
		pr_info("Not enabling interrupt remapping due to skipped IO-APIC setup\n");
		return;
	}

	ir_stat = irq_remapping_prepare();
	if (ir_stat < 0 && !x2apic_supported())
		return;

	ret = save_ioapic_entries();
	if (ret) {
		pr_info("Saving IO-APIC state failed: %d\n", ret);
		return;
	}

	local_irq_save(flags);
	legacy_pic->mask_all();
	mask_ioapic_entries();

	/* If irq_remapping_prepare() succeeded, try to enable it */
	if (ir_stat >= 0)
		ir_stat = irq_remapping_enable();
	/* ir_stat contains the remap mode or an error code */
	try_to_enable_x2apic(ir_stat);

	if (ir_stat < 0)
		restore_ioapic_entries();
	legacy_pic->restore_mask();
	local_irq_restore(flags);
}

#ifdef CONFIG_X86_64
/*
 * Detect and enable local APICs on non-SMP boards.
 * Original code written by Keir Fraser.
 * On AMD64 we trust the BIOS - if it says no APIC it is likely
 * not correctly set up (usually the APIC timer won't work etc.)
 */
static int __init detect_init_APIC(void)
{
	if (!boot_cpu_has(X86_FEATURE_APIC)) {
		pr_info("No local APIC present\n");
		return -1;
	}

	mp_lapic_addr = APIC_DEFAULT_PHYS_BASE;
	return 0;
}
#else

static int __init apic_verify(void)
{
	u32 features, h, l;

	/*
	 * The APIC feature bit should now be enabled
	 * in `cpuid'
	 */
	features = cpuid_edx(1);
	if (!(features & (1 << X86_FEATURE_APIC))) {
		pr_warn("Could not enable APIC!\n");
		return -1;
	}
	set_cpu_cap(&boot_cpu_data, X86_FEATURE_APIC);
	mp_lapic_addr = APIC_DEFAULT_PHYS_BASE;

	/* The BIOS may have set up the APIC at some other address */
	if (boot_cpu_data.x86 >= 6) {
		rdmsr(MSR_IA32_APICBASE, l, h);
		if (l & MSR_IA32_APICBASE_ENABLE)
			mp_lapic_addr = l & MSR_IA32_APICBASE_BASE;
	}

	pr_info("Found and enabled local APIC!\n");
	return 0;
}

int __init apic_force_enable(unsigned long addr)
{
	u32 h, l;

	if (disable_apic)
		return -1;

	/*
	 * Some BIOSes disable the local APIC in the APIC_BASE
	 * MSR. This can only be done in software for Intel P6 or later
	 * and AMD K7 (Model > 1) or later.
	 */
	if (boot_cpu_data.x86 >= 6) {
		rdmsr(MSR_IA32_APICBASE, l, h);
		if (!(l & MSR_IA32_APICBASE_ENABLE)) {
			pr_info("Local APIC disabled by BIOS -- reenabling.\n");
			l &= ~MSR_IA32_APICBASE_BASE;
			l |= MSR_IA32_APICBASE_ENABLE | addr;
			wrmsr(MSR_IA32_APICBASE, l, h);
			enabled_via_apicbase = 1;
		}
	}
	return apic_verify();
}

/*
 * Detect and initialize APIC
 */
static int __init detect_init_APIC(void)
{
	/* Disabled by kernel option? */
	if (disable_apic)
		return -1;

	switch (boot_cpu_data.x86_vendor) {
	case X86_VENDOR_AMD:
		if ((boot_cpu_data.x86 == 6 && boot_cpu_data.x86_model > 1) ||
		    (boot_cpu_data.x86 >= 15))
			break;
		goto no_apic;
	case X86_VENDOR_HYGON:
		break;
	case X86_VENDOR_INTEL:
		if (boot_cpu_data.x86 == 6 || boot_cpu_data.x86 == 15 ||
		    (boot_cpu_data.x86 == 5 && boot_cpu_has(X86_FEATURE_APIC)))
			break;
		goto no_apic;
	default:
		goto no_apic;
	}

	if (!boot_cpu_has(X86_FEATURE_APIC)) {
		/*
		 * Over-ride BIOS and try to enable the local APIC only if
		 * "lapic" specified.
		 */
		if (!force_enable_local_apic) {
			pr_info("Local APIC disabled by BIOS -- "
				"you can enable it with \"lapic\"\n");
			return -1;
		}
		if (apic_force_enable(APIC_DEFAULT_PHYS_BASE))
			return -1;
	} else {
		if (apic_verify())
			return -1;
	}

	apic_pm_activate();

	return 0;

no_apic:
	pr_info("No local APIC present or hardware disabled\n");
	return -1;
}
#endif

/**
 * init_apic_mappings - initialize APIC mappings
 */
void __init init_apic_mappings(void)
{
	unsigned int new_apicid;

	if (apic_validate_deadline_timer())
		pr_info("TSC deadline timer available\n");

	if (x2apic_mode) {
		boot_cpu_physical_apicid = read_apic_id();
		return;
	}

	/* If no local APIC can be found return early */
	if (!smp_found_config && detect_init_APIC()) {
		/* lets NOP'ify apic operations */
		pr_info("APIC: disable apic facility\n");
		apic_disable();
	} else {
		apic_phys = mp_lapic_addr;

		/*
		 * If the system has ACPI MADT tables or MP info, the LAPIC
		 * address is already registered.
		 */
		if (!acpi_lapic && !smp_found_config)
			register_lapic_address(apic_phys);
	}

	/*
	 * Fetch the APIC ID of the BSP in case we have a
	 * default configuration (or the MP table is broken).
	 */
	new_apicid = read_apic_id();
	if (boot_cpu_physical_apicid != new_apicid) {
		boot_cpu_physical_apicid = new_apicid;
		/*
		 * yeah -- we lie about apic_version
		 * in case if apic was disabled via boot option
		 * but it's not a problem for SMP compiled kernel
		 * since apic_intr_mode_select is prepared for such
		 * a case and disable smp mode
		 */
		boot_cpu_apic_version = GET_APIC_VERSION(apic_read(APIC_LVR));
	}
}

void __init register_lapic_address(unsigned long address)
{
	mp_lapic_addr = address;

	if (!x2apic_mode) {
		set_fixmap_nocache(FIX_APIC_BASE, address);
		apic_printk(APIC_VERBOSE, "mapped APIC to %16lx (%16lx)\n",
			    APIC_BASE, address);
	}
	if (boot_cpu_physical_apicid == -1U) {
		boot_cpu_physical_apicid  = read_apic_id();
		boot_cpu_apic_version = GET_APIC_VERSION(apic_read(APIC_LVR));
	}
}

/*
 * Local APIC interrupts
 */

/*
 * Common handling code for spurious_interrupt and spurious_vector entry
 * points below. No point in allowing the compiler to inline it twice.
 */
static noinline void handle_spurious_interrupt(u8 vector)
{
	u32 v;

	trace_spurious_apic_entry(vector);

	inc_irq_stat(irq_spurious_count);

	/*
	 * If this is a spurious interrupt then do not acknowledge
	 */
	if (vector == SPURIOUS_APIC_VECTOR) {
		/* See SDM vol 3 */
		pr_info("Spurious APIC interrupt (vector 0xFF) on CPU#%d, should never happen.\n",
			smp_processor_id());
		goto out;
	}

	/*
	 * If it is a vectored one, verify it's set in the ISR. If set,
	 * acknowledge it.
	 */
	v = apic_read(APIC_ISR + ((vector & ~0x1f) >> 1));
	if (v & (1 << (vector & 0x1f))) {
		pr_info("Spurious interrupt (vector 0x%02x) on CPU#%d. Acked\n",
			vector, smp_processor_id());
		ack_APIC_irq();
	} else {
		pr_info("Spurious interrupt (vector 0x%02x) on CPU#%d. Not pending!\n",
			vector, smp_processor_id());
	}
out:
	trace_spurious_apic_exit(vector);
}

/**
 * spurious_interrupt - Catch all for interrupts raised on unused vectors
 * @regs:	Pointer to pt_regs on stack
 * @vector:	The vector number
 *
 * This is invoked from ASM entry code to catch all interrupts which
 * trigger on an entry which is routed to the common_spurious idtentry
 * point.
 */
DEFINE_IDTENTRY_IRQ(spurious_interrupt)
{
	handle_spurious_interrupt(vector);
}

DEFINE_IDTENTRY_SYSVEC(sysvec_spurious_apic_interrupt)
{
	handle_spurious_interrupt(SPURIOUS_APIC_VECTOR);
}

/*
 * This interrupt should never happen with our APIC/SMP architecture
 */
DEFINE_IDTENTRY_SYSVEC(sysvec_error_interrupt)
{
	static const char * const error_interrupt_reason[] = {
		"Send CS error",		/* APIC Error Bit 0 */
		"Receive CS error",		/* APIC Error Bit 1 */
		"Send accept error",		/* APIC Error Bit 2 */
		"Receive accept error",		/* APIC Error Bit 3 */
		"Redirectable IPI",		/* APIC Error Bit 4 */
		"Send illegal vector",		/* APIC Error Bit 5 */
		"Received illegal vector",	/* APIC Error Bit 6 */
		"Illegal register address",	/* APIC Error Bit 7 */
	};
	u32 v, i = 0;

	trace_error_apic_entry(ERROR_APIC_VECTOR);

	/* First tickle the hardware, only then report what went on. -- REW */
	if (lapic_get_maxlvt() > 3)	/* Due to the Pentium erratum 3AP. */
		apic_write(APIC_ESR, 0);
	v = apic_read(APIC_ESR);
	ack_APIC_irq();
	atomic_inc(&irq_err_count);

	apic_printk(APIC_DEBUG, KERN_DEBUG "APIC error on CPU%d: %02x",
		    smp_processor_id(), v);

	v &= 0xff;
	while (v) {
		if (v & 0x1)
			apic_printk(APIC_DEBUG, KERN_CONT " : %s", error_interrupt_reason[i]);
		i++;
		v >>= 1;
	}

	apic_printk(APIC_DEBUG, KERN_CONT "\n");

	trace_error_apic_exit(ERROR_APIC_VECTOR);
}

/**
 * connect_bsp_APIC - attach the APIC to the interrupt system
 */
static void __init connect_bsp_APIC(void)
{
#ifdef CONFIG_X86_32
	if (pic_mode) {
		/*
		 * Do not trust the local APIC being empty at bootup.
		 */
		clear_local_APIC();
		/*
		 * PIC mode, enable APIC mode in the IMCR, i.e.  connect BSP's
		 * local APIC to INT and NMI lines.
		 */
		apic_printk(APIC_VERBOSE, "leaving PIC mode, "
				"enabling APIC mode.\n");
		imcr_pic_to_apic();
	}
#endif
}

/**
 * disconnect_bsp_APIC - detach the APIC from the interrupt system
 * @virt_wire_setup:	indicates, whether virtual wire mode is selected
 *
 * Virtual wire mode is necessary to deliver legacy interrupts even when the
 * APIC is disabled.
 */
void disconnect_bsp_APIC(int virt_wire_setup)
{
	unsigned int value;

#ifdef CONFIG_X86_32
	if (pic_mode) {
		/*
		 * Put the board back into PIC mode (has an effect only on
		 * certain older boards).  Note that APIC interrupts, including
		 * IPIs, won't work beyond this point!  The only exception are
		 * INIT IPIs.
		 */
		apic_printk(APIC_VERBOSE, "disabling APIC mode, "
				"entering PIC mode.\n");
		imcr_apic_to_pic();
		return;
	}
#endif

	/* Go back to Virtual Wire compatibility mode */

	/* For the spurious interrupt use vector F, and enable it */
	value = apic_read(APIC_SPIV);
	value &= ~APIC_VECTOR_MASK;
	value |= APIC_SPIV_APIC_ENABLED;
	value |= 0xf;
	apic_write(APIC_SPIV, value);

	if (!virt_wire_setup) {
		/*
		 * For LVT0 make it edge triggered, active high,
		 * external and enabled
		 */
		value = apic_read(APIC_LVT0);
		value &= ~(APIC_MODE_MASK | APIC_SEND_PENDING |
			APIC_INPUT_POLARITY | APIC_LVT_REMOTE_IRR |
			APIC_LVT_LEVEL_TRIGGER | APIC_LVT_MASKED);
		value |= APIC_LVT_REMOTE_IRR | APIC_SEND_PENDING;
		value = SET_APIC_DELIVERY_MODE(value, APIC_MODE_EXTINT);
		apic_write(APIC_LVT0, value);
	} else {
		/* Disable LVT0 */
		apic_write(APIC_LVT0, APIC_LVT_MASKED);
	}

	/*
	 * For LVT1 make it edge triggered, active high,
	 * nmi and enabled
	 */
	value = apic_read(APIC_LVT1);
	value &= ~(APIC_MODE_MASK | APIC_SEND_PENDING |
			APIC_INPUT_POLARITY | APIC_LVT_REMOTE_IRR |
			APIC_LVT_LEVEL_TRIGGER | APIC_LVT_MASKED);
	value |= APIC_LVT_REMOTE_IRR | APIC_SEND_PENDING;
	value = SET_APIC_DELIVERY_MODE(value, APIC_MODE_NMI);
	apic_write(APIC_LVT1, value);
}

/*
 * The number of allocated logical CPU IDs. Since logical CPU IDs are allocated
 * contiguously, it equals to current allocated max logical CPU ID plus 1.
 * All allocated CPU IDs should be in the [0, nr_logical_cpuids) range,
 * so the maximum of nr_logical_cpuids is nr_cpu_ids.
 *
 * NOTE: Reserve 0 for BSP.
 */
static int nr_logical_cpuids = 1;

/*
 * Used to store mapping between logical CPU IDs and APIC IDs.
 */
static int cpuid_to_apicid[] = {
	[0 ... NR_CPUS - 1] = -1,
};

bool arch_match_cpu_phys_id(int cpu, u64 phys_id)
{
	return phys_id == cpuid_to_apicid[cpu];
}

#ifdef CONFIG_SMP
/**
 * apic_id_is_primary_thread - Check whether APIC ID belongs to a primary thread
 * @apicid: APIC ID to check
 */
bool apic_id_is_primary_thread(unsigned int apicid)
{
	u32 mask;

	if (smp_num_siblings == 1)
		return true;
	/* Isolate the SMT bit(s) in the APICID and check for 0 */
	mask = (1U << (fls(smp_num_siblings) - 1)) - 1;
	return !(apicid & mask);
}
#endif

/*
 * Should use this API to allocate logical CPU IDs to keep nr_logical_cpuids
 * and cpuid_to_apicid[] synchronized.
 */
static int allocate_logical_cpuid(int apicid)
{
	int i;

	/*
	 * cpuid <-> apicid mapping is persistent, so when a cpu is up,
	 * check if the kernel has allocated a cpuid for it.
	 */
	for (i = 0; i < nr_logical_cpuids; i++) {
		if (cpuid_to_apicid[i] == apicid)
			return i;
	}

	/* Allocate a new cpuid. */
	if (nr_logical_cpuids >= nr_cpu_ids) {
		WARN_ONCE(1, "APIC: NR_CPUS/possible_cpus limit of %u reached. "
			     "Processor %d/0x%x and the rest are ignored.\n",
			     nr_cpu_ids, nr_logical_cpuids, apicid);
		return -EINVAL;
	}

	cpuid_to_apicid[nr_logical_cpuids] = apicid;
	return nr_logical_cpuids++;
}

int generic_processor_info(int apicid, int version)
{
	int cpu, max = nr_cpu_ids;
	bool boot_cpu_detected = physid_isset(boot_cpu_physical_apicid,
				phys_cpu_present_map);

	/*
	 * boot_cpu_physical_apicid is designed to have the apicid
	 * returned by read_apic_id(), i.e, the apicid of the
	 * currently booting-up processor. However, on some platforms,
	 * it is temporarily modified by the apicid reported as BSP
	 * through MP table. Concretely:
	 *
	 * - arch/x86/kernel/mpparse.c: MP_processor_info()
	 * - arch/x86/mm/amdtopology.c: amd_numa_init()
	 *
	 * This function is executed with the modified
	 * boot_cpu_physical_apicid. So, disabled_cpu_apicid kernel
	 * parameter doesn't work to disable APs on kdump 2nd kernel.
	 *
	 * Since fixing handling of boot_cpu_physical_apicid requires
	 * another discussion and tests on each platform, we leave it
	 * for now and here we use read_apic_id() directly in this
	 * function, generic_processor_info().
	 */
	if (disabled_cpu_apicid != BAD_APICID &&
	    disabled_cpu_apicid != read_apic_id() &&
	    disabled_cpu_apicid == apicid) {
		int thiscpu = num_processors + disabled_cpus;

		pr_warn("APIC: Disabling requested cpu."
			" Processor %d/0x%x ignored.\n", thiscpu, apicid);

		disabled_cpus++;
		return -ENODEV;
	}

	/*
	 * If boot cpu has not been detected yet, then only allow upto
	 * nr_cpu_ids - 1 processors and keep one slot free for boot cpu
	 */
	if (!boot_cpu_detected && num_processors >= nr_cpu_ids - 1 &&
	    apicid != boot_cpu_physical_apicid) {
		int thiscpu = max + disabled_cpus - 1;

		pr_warn("APIC: NR_CPUS/possible_cpus limit of %i almost"
			" reached. Keeping one slot for boot cpu."
			"  Processor %d/0x%x ignored.\n", max, thiscpu, apicid);

		disabled_cpus++;
		return -ENODEV;
	}

	if (num_processors >= nr_cpu_ids) {
		int thiscpu = max + disabled_cpus;

		pr_warn("APIC: NR_CPUS/possible_cpus limit of %i reached. "
			"Processor %d/0x%x ignored.\n", max, thiscpu, apicid);

		disabled_cpus++;
		return -EINVAL;
	}

	if (apicid == boot_cpu_physical_apicid) {
		/*
		 * x86_bios_cpu_apicid is required to have processors listed
		 * in same order as logical cpu numbers. Hence the first
		 * entry is BSP, and so on.
		 * boot_cpu_init() already hold bit 0 in cpu_present_mask
		 * for BSP.
		 */
		cpu = 0;

		/* Logical cpuid 0 is reserved for BSP. */
		cpuid_to_apicid[0] = apicid;
	} else {
		cpu = allocate_logical_cpuid(apicid);
		if (cpu < 0) {
			disabled_cpus++;
			return -EINVAL;
		}
	}

	/*
	 * Validate version
	 */
	if (version == 0x0) {
		pr_warn("BIOS bug: APIC version is 0 for CPU %d/0x%x, fixing up to 0x10\n",
			cpu, apicid);
		version = 0x10;
	}

	if (version != boot_cpu_apic_version) {
		pr_warn("BIOS bug: APIC version mismatch, boot CPU: %x, CPU %d: version %x\n",
			boot_cpu_apic_version, cpu, version);
	}

	if (apicid > max_physical_apicid)
		max_physical_apicid = apicid;

#if defined(CONFIG_SMP) || defined(CONFIG_X86_64)
	early_per_cpu(x86_cpu_to_apicid, cpu) = apicid;
	early_per_cpu(x86_bios_cpu_apicid, cpu) = apicid;
#endif
#ifdef CONFIG_X86_32
	early_per_cpu(x86_cpu_to_logical_apicid, cpu) =
		apic->x86_32_early_logical_apicid(cpu);
#endif
	set_cpu_possible(cpu, true);
	physid_set(apicid, phys_cpu_present_map);
	set_cpu_present(cpu, true);
	num_processors++;

	return cpu;
}

int hard_smp_processor_id(void)
{
	return read_apic_id();
}

void __irq_msi_compose_msg(struct irq_cfg *cfg, struct msi_msg *msg,
			   bool dmar)
{
	memset(msg, 0, sizeof(*msg));

	msg->arch_addr_lo.base_address = X86_MSI_BASE_ADDRESS_LOW;
	msg->arch_addr_lo.dest_mode_logical = apic->dest_mode_logical;
	msg->arch_addr_lo.destid_0_7 = cfg->dest_apicid & 0xFF;

	msg->arch_data.delivery_mode = APIC_DELIVERY_MODE_FIXED;
	msg->arch_data.vector = cfg->vector;

	msg->address_hi = X86_MSI_BASE_ADDRESS_HIGH;
	/*
	 * Only the IOMMU itself can use the trick of putting destination
	 * APIC ID into the high bits of the address. Anything else would
	 * just be writing to memory if it tried that, and needs IR to
	 * address APICs which can't be addressed in the normal 32-bit
	 * address range at 0xFFExxxxx. That is typically just 8 bits, but
	 * some hypervisors allow the extended destination ID field in bits
	 * 5-11 to be used, giving support for 15 bits of APIC IDs in total.
	 */
	if (dmar)
		msg->arch_addr_hi.destid_8_31 = cfg->dest_apicid >> 8;
	else if (virt_ext_dest_id && cfg->dest_apicid < 0x8000)
		msg->arch_addr_lo.virt_destid_8_14 = cfg->dest_apicid >> 8;
	else
		WARN_ON_ONCE(cfg->dest_apicid > 0xFF);
}

u32 x86_msi_msg_get_destid(struct msi_msg *msg, bool extid)
{
	u32 dest = msg->arch_addr_lo.destid_0_7;

	if (extid)
		dest |= msg->arch_addr_hi.destid_8_31 << 8;
	return dest;
}
EXPORT_SYMBOL_GPL(x86_msi_msg_get_destid);

#ifdef CONFIG_X86_64
void __init acpi_wake_cpu_handler_update(wakeup_cpu_handler handler)
{
	struct apic **drv;

	for (drv = __apicdrivers; drv < __apicdrivers_end; drv++)
		(*drv)->wakeup_secondary_cpu_64 = handler;
}
#endif

/*
 * Override the generic EOI implementation with an optimized version.
 * Only called during early boot when only one CPU is active and with
 * interrupts disabled, so we know this does not race with actual APIC driver
 * use.
 */
void __init apic_set_eoi_write(void (*eoi_write)(u32 reg, u32 v))
{
	struct apic **drv;

	for (drv = __apicdrivers; drv < __apicdrivers_end; drv++) {
		/* Should happen once for each apic */
		WARN_ON((*drv)->eoi_write == eoi_write);
		(*drv)->native_eoi_write = (*drv)->eoi_write;
		(*drv)->eoi_write = eoi_write;
	}
}

static void __init apic_bsp_up_setup(void)
{
#ifdef CONFIG_X86_64
	apic_write(APIC_ID, apic->set_apic_id(boot_cpu_physical_apicid));
#else
	/*
	 * Hack: In case of kdump, after a crash, kernel might be booting
	 * on a cpu with non-zero lapic id. But boot_cpu_physical_apicid
	 * might be zero if read from MP tables. Get it from LAPIC.
	 */
# ifdef CONFIG_CRASH_DUMP
	boot_cpu_physical_apicid = read_apic_id();
# endif
#endif
	physid_set_mask_of_physid(boot_cpu_physical_apicid, &phys_cpu_present_map);
}

/**
 * apic_bsp_setup - Setup function for local apic and io-apic
 * @upmode:		Force UP mode (for APIC_init_uniprocessor)
 */
static void __init apic_bsp_setup(bool upmode)
{
	connect_bsp_APIC();
	if (upmode)
		apic_bsp_up_setup();
	setup_local_APIC();

	enable_IO_APIC();
	end_local_APIC_setup();
	irq_remap_enable_fault_handling();
	setup_IO_APIC();
	lapic_update_legacy_vectors();
}

#ifdef CONFIG_UP_LATE_INIT
void __init up_late_init(void)
{
	if (apic_intr_mode == APIC_PIC)
		return;

	/* Setup local timer */
	x86_init.timers.setup_percpu_clockev();
}
#endif

/*
 * Power management
 */
#ifdef CONFIG_PM

static struct {
	/*
	 * 'active' is true if the local APIC was enabled by us and
	 * not the BIOS; this signifies that we are also responsible
	 * for disabling it before entering apm/acpi suspend
	 */
	int active;
	/* r/w apic fields */
	unsigned int apic_id;
	unsigned int apic_taskpri;
	unsigned int apic_ldr;
	unsigned int apic_dfr;
	unsigned int apic_spiv;
	unsigned int apic_lvtt;
	unsigned int apic_lvtpc;
	unsigned int apic_lvt0;
	unsigned int apic_lvt1;
	unsigned int apic_lvterr;
	unsigned int apic_tmict;
	unsigned int apic_tdcr;
	unsigned int apic_thmr;
	unsigned int apic_cmci;
} apic_pm_state;

static int lapic_suspend(void)
{
	unsigned long flags;
	int maxlvt;

	if (!apic_pm_state.active)
		return 0;

	maxlvt = lapic_get_maxlvt();

	apic_pm_state.apic_id = apic_read(APIC_ID);
	apic_pm_state.apic_taskpri = apic_read(APIC_TASKPRI);
	apic_pm_state.apic_ldr = apic_read(APIC_LDR);
	apic_pm_state.apic_dfr = apic_read(APIC_DFR);
	apic_pm_state.apic_spiv = apic_read(APIC_SPIV);
	apic_pm_state.apic_lvtt = apic_read(APIC_LVTT);
	if (maxlvt >= 4)
		apic_pm_state.apic_lvtpc = apic_read(APIC_LVTPC);
	apic_pm_state.apic_lvt0 = apic_read(APIC_LVT0);
	apic_pm_state.apic_lvt1 = apic_read(APIC_LVT1);
	apic_pm_state.apic_lvterr = apic_read(APIC_LVTERR);
	apic_pm_state.apic_tmict = apic_read(APIC_TMICT);
	apic_pm_state.apic_tdcr = apic_read(APIC_TDCR);
#ifdef CONFIG_X86_THERMAL_VECTOR
	if (maxlvt >= 5)
		apic_pm_state.apic_thmr = apic_read(APIC_LVTTHMR);
#endif
#ifdef CONFIG_X86_MCE_INTEL
	if (maxlvt >= 6)
		apic_pm_state.apic_cmci = apic_read(APIC_LVTCMCI);
#endif

	local_irq_save(flags);

	/*
	 * Mask IOAPIC before disabling the local APIC to prevent stale IRR
	 * entries on some implementations.
	 */
	mask_ioapic_entries();

	disable_local_APIC();

	irq_remapping_disable();

	local_irq_restore(flags);
	return 0;
}

static void lapic_resume(void)
{
	unsigned int l, h;
	unsigned long flags;
	int maxlvt;

	if (!apic_pm_state.active)
		return;

	local_irq_save(flags);

	/*
	 * IO-APIC and PIC have their own resume routines.
	 * We just mask them here to make sure the interrupt
	 * subsystem is completely quiet while we enable x2apic
	 * and interrupt-remapping.
	 */
	mask_ioapic_entries();
	legacy_pic->mask_all();

	if (x2apic_mode) {
		__x2apic_enable();
	} else {
		/*
		 * Make sure the APICBASE points to the right address
		 *
		 * FIXME! This will be wrong if we ever support suspend on
		 * SMP! We'll need to do this as part of the CPU restore!
		 */
		if (boot_cpu_data.x86 >= 6) {
			rdmsr(MSR_IA32_APICBASE, l, h);
			l &= ~MSR_IA32_APICBASE_BASE;
			l |= MSR_IA32_APICBASE_ENABLE | mp_lapic_addr;
			wrmsr(MSR_IA32_APICBASE, l, h);
		}
	}

	maxlvt = lapic_get_maxlvt();
	apic_write(APIC_LVTERR, ERROR_APIC_VECTOR | APIC_LVT_MASKED);
	apic_write(APIC_ID, apic_pm_state.apic_id);
	apic_write(APIC_DFR, apic_pm_state.apic_dfr);
	apic_write(APIC_LDR, apic_pm_state.apic_ldr);
	apic_write(APIC_TASKPRI, apic_pm_state.apic_taskpri);
	apic_write(APIC_SPIV, apic_pm_state.apic_spiv);
	apic_write(APIC_LVT0, apic_pm_state.apic_lvt0);
	apic_write(APIC_LVT1, apic_pm_state.apic_lvt1);
#ifdef CONFIG_X86_THERMAL_VECTOR
	if (maxlvt >= 5)
		apic_write(APIC_LVTTHMR, apic_pm_state.apic_thmr);
#endif
#ifdef CONFIG_X86_MCE_INTEL
	if (maxlvt >= 6)
		apic_write(APIC_LVTCMCI, apic_pm_state.apic_cmci);
#endif
	if (maxlvt >= 4)
		apic_write(APIC_LVTPC, apic_pm_state.apic_lvtpc);
	apic_write(APIC_LVTT, apic_pm_state.apic_lvtt);
	apic_write(APIC_TDCR, apic_pm_state.apic_tdcr);
	apic_write(APIC_TMICT, apic_pm_state.apic_tmict);
	apic_write(APIC_ESR, 0);
	apic_read(APIC_ESR);
	apic_write(APIC_LVTERR, apic_pm_state.apic_lvterr);
	apic_write(APIC_ESR, 0);
	apic_read(APIC_ESR);

	irq_remapping_reenable(x2apic_mode);

	local_irq_restore(flags);
}

/*
 * This device has no shutdown method - fully functioning local APICs
 * are needed on every CPU up until machine_halt/restart/poweroff.
 */

static struct syscore_ops lapic_syscore_ops = {
	.resume		= lapic_resume,
	.suspend	= lapic_suspend,
};

static void apic_pm_activate(void)
{
	apic_pm_state.active = 1;
}

static int __init init_lapic_sysfs(void)
{
	/* XXX: remove suspend/resume procs if !apic_pm_state.active? */
	if (boot_cpu_has(X86_FEATURE_APIC))
		register_syscore_ops(&lapic_syscore_ops);

	return 0;
}

/* local apic needs to resume before other devices access its registers. */
core_initcall(init_lapic_sysfs);

#else	/* CONFIG_PM */

static void apic_pm_activate(void) { }

#endif	/* CONFIG_PM */

#ifdef CONFIG_X86_64

static int multi_checked;
static int multi;

static int set_multi(const struct dmi_system_id *d)
{
	if (multi)
		return 0;
	pr_info("APIC: %s detected, Multi Chassis\n", d->ident);
	multi = 1;
	return 0;
}

static const struct dmi_system_id multi_dmi_table[] = {
	{
		.callback = set_multi,
		.ident = "IBM System Summit2",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "IBM"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Summit2"),
		},
	},
	{}
};

static void dmi_check_multi(void)
{
	if (multi_checked)
		return;

	dmi_check_system(multi_dmi_table);
	multi_checked = 1;
}

/*
 * apic_is_clustered_box() -- Check if we can expect good TSC
 *
 * Thus far, the major user of this is IBM's Summit2 series:
 * Clustered boxes may have unsynced TSC problems if they are
 * multi-chassis.
 * Use DMI to check them
 */
int apic_is_clustered_box(void)
{
	dmi_check_multi();
	return multi;
}
#endif

/*
 * APIC command line parameters
 */
static int __init setup_disableapic(char *arg)
{
	disable_apic = 1;
	setup_clear_cpu_cap(X86_FEATURE_APIC);
	return 0;
}
early_param("disableapic", setup_disableapic);

/* same as disableapic, for compatibility */
static int __init setup_nolapic(char *arg)
{
	return setup_disableapic(arg);
}
early_param("nolapic", setup_nolapic);

static int __init parse_lapic_timer_c2_ok(char *arg)
{
	local_apic_timer_c2_ok = 1;
	return 0;
}
early_param("lapic_timer_c2_ok", parse_lapic_timer_c2_ok);

static int __init parse_disable_apic_timer(char *arg)
{
	disable_apic_timer = 1;
	return 0;
}
early_param("noapictimer", parse_disable_apic_timer);

static int __init parse_nolapic_timer(char *arg)
{
	disable_apic_timer = 1;
	return 0;
}
early_param("nolapic_timer", parse_nolapic_timer);

static int __init apic_set_verbosity(char *arg)
{
	if (!arg)  {
#ifdef CONFIG_X86_64
		skip_ioapic_setup = 0;
		return 0;
#endif
		return -EINVAL;
	}

	if (strcmp("debug", arg) == 0)
		apic_verbosity = APIC_DEBUG;
	else if (strcmp("verbose", arg) == 0)
		apic_verbosity = APIC_VERBOSE;
#ifdef CONFIG_X86_64
	else {
		pr_warn("APIC Verbosity level %s not recognised"
			" use apic=verbose or apic=debug\n", arg);
		return -EINVAL;
	}
#endif

	return 0;
}
early_param("apic", apic_set_verbosity);

static int __init lapic_insert_resource(void)
{
	if (!apic_phys)
		return -1;

	/* Put local APIC into the resource map. */
	lapic_resource.start = apic_phys;
	lapic_resource.end = lapic_resource.start + PAGE_SIZE - 1;
	insert_resource(&iomem_resource, &lapic_resource);

	return 0;
}

/*
 * need call insert after e820__reserve_resources()
 * that is using request_resource
 */
late_initcall(lapic_insert_resource);

static int __init apic_set_disabled_cpu_apicid(char *arg)
{
	if (!arg || !get_option(&arg, &disabled_cpu_apicid))
		return -EINVAL;

	return 0;
}
early_param("disable_cpu_apicid", apic_set_disabled_cpu_apicid);

static int __init apic_set_extnmi(char *arg)
{
	if (!arg)
		return -EINVAL;

	if (!strncmp("all", arg, 3))
		apic_extnmi = APIC_EXTNMI_ALL;
	else if (!strncmp("none", arg, 4))
		apic_extnmi = APIC_EXTNMI_NONE;
	else if (!strncmp("bsp", arg, 3))
		apic_extnmi = APIC_EXTNMI_BSP;
	else {
		pr_warn("Unknown external NMI delivery mode `%s' ignored\n", arg);
		return -EINVAL;
	}

	return 0;
}
early_param("apic_extnmi", apic_set_extnmi);
