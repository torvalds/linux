/*
 *	Local APIC handling, local APIC timers
 *
 *	(c) 1999, 2000 Ingo Molnar <mingo@redhat.com>
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

#include <linux/init.h>

#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/bootmem.h>
#include <linux/interrupt.h>
#include <linux/mc146818rtc.h>
#include <linux/kernel_stat.h>
#include <linux/sysdev.h>
#include <linux/module.h>
#include <linux/ioport.h>

#include <asm/atomic.h>
#include <asm/smp.h>
#include <asm/mtrr.h>
#include <asm/mpspec.h>
#include <asm/pgalloc.h>
#include <asm/mach_apic.h>
#include <asm/nmi.h>
#include <asm/idle.h>
#include <asm/proto.h>
#include <asm/timex.h>
#include <asm/hpet.h>
#include <asm/apic.h>

int apic_mapped;
int apic_verbosity;
int apic_runs_main_timer;
int apic_calibrate_pmtmr __initdata;

int disable_apic_timer __initdata;

/* Local APIC timer works in C2? */
int local_apic_timer_c2_ok;
EXPORT_SYMBOL_GPL(local_apic_timer_c2_ok);

static struct resource *ioapic_resources;
static struct resource lapic_resource = {
	.name = "Local APIC",
	.flags = IORESOURCE_MEM | IORESOURCE_BUSY,
};

/*
 * cpu_mask that denotes the CPUs that needs timer interrupt coming in as
 * IPIs in place of local APIC timers
 */
static cpumask_t timer_interrupt_broadcast_ipi_mask;

/* Using APIC to generate smp_local_timer_interrupt? */
int using_apic_timer __read_mostly = 0;

static void apic_pm_activate(void);

void apic_wait_icr_idle(void)
{
	while (apic_read(APIC_ICR) & APIC_ICR_BUSY)
		cpu_relax();
}

unsigned int safe_apic_wait_icr_idle(void)
{
	unsigned int send_status;
	int timeout;

	timeout = 0;
	do {
		send_status = apic_read(APIC_ICR) & APIC_ICR_BUSY;
		if (!send_status)
			break;
		udelay(100);
	} while (timeout++ < 1000);

	return send_status;
}

void enable_NMI_through_LVT0 (void * dummy)
{
	unsigned int v;

	/* unmask and set to NMI */
	v = APIC_DM_NMI;
	apic_write(APIC_LVT0, v);
}

int get_maxlvt(void)
{
	unsigned int v, maxlvt;

	v = apic_read(APIC_LVR);
	maxlvt = GET_APIC_MAXLVT(v);
	return maxlvt;
}

/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves.
 */
void ack_bad_irq(unsigned int irq)
{
	printk("unexpected IRQ trap at vector %02x\n", irq);
	/*
	 * Currently unexpected vectors happen only on SMP and APIC.
	 * We _must_ ack these because every local APIC has only N
	 * irq slots per priority level, and a 'hanging, unacked' IRQ
	 * holds up an irq slot - in excessive cases (when multiple
	 * unexpected vectors occur) that might lock up the APIC
	 * completely.
	 * But don't ack when the APIC is disabled. -AK
	 */
	if (!disable_apic)
		ack_APIC_irq();
}

void clear_local_APIC(void)
{
	int maxlvt;
	unsigned int v;

	maxlvt = get_maxlvt();

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
	apic_write(APIC_ESR, 0);
	apic_read(APIC_ESR);
}

void disconnect_bsp_APIC(int virt_wire_setup)
{
	/* Go back to Virtual Wire compatibility mode */
	unsigned long value;

	/* For the spurious interrupt use vector F, and enable it */
	value = apic_read(APIC_SPIV);
	value &= ~APIC_VECTOR_MASK;
	value |= APIC_SPIV_APIC_ENABLED;
	value |= 0xf;
	apic_write(APIC_SPIV, value);

	if (!virt_wire_setup) {
		/* For LVT0 make it edge triggered, active high, external and enabled */
		value = apic_read(APIC_LVT0);
		value &= ~(APIC_MODE_MASK | APIC_SEND_PENDING |
			APIC_INPUT_POLARITY | APIC_LVT_REMOTE_IRR |
			APIC_LVT_LEVEL_TRIGGER | APIC_LVT_MASKED );
		value |= APIC_LVT_REMOTE_IRR | APIC_SEND_PENDING;
		value = SET_APIC_DELIVERY_MODE(value, APIC_MODE_EXTINT);
		apic_write(APIC_LVT0, value);
	} else {
		/* Disable LVT0 */
		apic_write(APIC_LVT0, APIC_LVT_MASKED);
	}

	/* For LVT1 make it edge triggered, active high, nmi and enabled */
	value = apic_read(APIC_LVT1);
	value &= ~(APIC_MODE_MASK | APIC_SEND_PENDING |
			APIC_INPUT_POLARITY | APIC_LVT_REMOTE_IRR |
			APIC_LVT_LEVEL_TRIGGER | APIC_LVT_MASKED);
	value |= APIC_LVT_REMOTE_IRR | APIC_SEND_PENDING;
	value = SET_APIC_DELIVERY_MODE(value, APIC_MODE_NMI);
	apic_write(APIC_LVT1, value);
}

void disable_local_APIC(void)
{
	unsigned int value;

	clear_local_APIC();

	/*
	 * Disable APIC (implies clearing of registers
	 * for 82489DX!).
	 */
	value = apic_read(APIC_SPIV);
	value &= ~APIC_SPIV_APIC_ENABLED;
	apic_write(APIC_SPIV, value);
}

/*
 * This is to verify that we're looking at a real local APIC.
 * Check these against your board if the CPUs aren't getting
 * started for no apparent reason.
 */
int __init verify_local_APIC(void)
{
	unsigned int reg0, reg1;

	/*
	 * The version register is read-only in a real APIC.
	 */
	reg0 = apic_read(APIC_LVR);
	apic_printk(APIC_DEBUG, "Getting VERSION: %x\n", reg0);
	apic_write(APIC_LVR, reg0 ^ APIC_LVR_MASK);
	reg1 = apic_read(APIC_LVR);
	apic_printk(APIC_DEBUG, "Getting VERSION: %x\n", reg1);

	/*
	 * The two version reads above should print the same
	 * numbers.  If the second one is different, then we
	 * poke at a non-APIC.
	 */
	if (reg1 != reg0)
		return 0;

	/*
	 * Check if the version looks reasonably.
	 */
	reg1 = GET_APIC_VERSION(reg0);
	if (reg1 == 0x00 || reg1 == 0xff)
		return 0;
	reg1 = get_maxlvt();
	if (reg1 < 0x02 || reg1 == 0xff)
		return 0;

	/*
	 * The ID register is read/write in a real APIC.
	 */
	reg0 = apic_read(APIC_ID);
	apic_printk(APIC_DEBUG, "Getting ID: %x\n", reg0);
	apic_write(APIC_ID, reg0 ^ APIC_ID_MASK);
	reg1 = apic_read(APIC_ID);
	apic_printk(APIC_DEBUG, "Getting ID: %x\n", reg1);
	apic_write(APIC_ID, reg0);
	if (reg1 != (reg0 ^ APIC_ID_MASK))
		return 0;

	/*
	 * The next two are just to see if we have sane values.
	 * They're only really relevant if we're in Virtual Wire
	 * compatibility mode, but most boxes are anymore.
	 */
	reg0 = apic_read(APIC_LVT0);
	apic_printk(APIC_DEBUG,"Getting LVT0: %x\n", reg0);
	reg1 = apic_read(APIC_LVT1);
	apic_printk(APIC_DEBUG, "Getting LVT1: %x\n", reg1);

	return 1;
}

void __init sync_Arb_IDs(void)
{
	/* Unsupported on P4 - see Intel Dev. Manual Vol. 3, Ch. 8.6.1 */
	unsigned int ver = GET_APIC_VERSION(apic_read(APIC_LVR));
	if (ver >= 0x14)	/* P4 or higher */
		return;

	/*
	 * Wait for idle.
	 */
	apic_wait_icr_idle();

	apic_printk(APIC_DEBUG, "Synchronizing Arb IDs.\n");
	apic_write(APIC_ICR, APIC_DEST_ALLINC | APIC_INT_LEVELTRIG
				| APIC_DM_INIT);
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
	if (smp_found_config || !cpu_has_apic)
		return;

	value = apic_read(APIC_LVR);

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
	value |= APIC_SPIV_FOCUS_DISABLED;
	value |= SPURIOUS_APIC_VECTOR;
	apic_write(APIC_SPIV, value);

	/*
	 * Set up the virtual wire mode.
	 */
	apic_write(APIC_LVT0, APIC_DM_EXTINT);
	value = APIC_DM_NMI;
	apic_write(APIC_LVT1, value);
}

void __cpuinit setup_local_APIC (void)
{
	unsigned int value, maxlvt;
	int i, j;

	value = apic_read(APIC_LVR);

	BUILD_BUG_ON((SPURIOUS_APIC_VECTOR & 0x0f) != 0x0f);

	/*
	 * Double-check whether this APIC is really registered.
	 * This is meaningless in clustered apic mode, so we skip it.
	 */
	if (!apic_id_registered())
		BUG();

	/*
	 * Intel recommends to set DFR, LDR and TPR before enabling
	 * an APIC.  See e.g. "AP-388 82489DX User's Manual" (Intel
	 * document number 292116).  So here it goes...
	 */
	init_apic_ldr();

	/*
	 * Set Task Priority to 'accept all'. We never change this
	 * later on.
	 */
	value = apic_read(APIC_TASKPRI);
	value &= ~APIC_TPRI_MASK;
	apic_write(APIC_TASKPRI, value);

	/*
	 * After a crash, we no longer service the interrupts and a pending
	 * interrupt from previous kernel might still have ISR bit set.
	 *
	 * Most probably by now CPU has serviced that pending interrupt and
	 * it might not have done the ack_APIC_irq() because it thought,
	 * interrupt came from i8259 as ExtInt. LAPIC did not get EOI so it
	 * does not clear the ISR bit and cpu thinks it has already serivced
	 * the interrupt. Hence a vector might get locked. It was noticed
	 * for timer irq (vector 0x31). Issue an extra EOI to clear ISR.
	 */
	for (i = APIC_ISR_NR - 1; i >= 0; i--) {
		value = apic_read(APIC_ISR + i*0x10);
		for (j = 31; j >= 0; j--) {
			if (value & (1<<j))
				ack_APIC_irq();
		}
	}

	/*
	 * Now that we are all set up, enable the APIC
	 */
	value = apic_read(APIC_SPIV);
	value &= ~APIC_VECTOR_MASK;
	/*
	 * Enable APIC
	 */
	value |= APIC_SPIV_APIC_ENABLED;

	/* We always use processor focus */

	/*
	 * Set spurious IRQ vector
	 */
	value |= SPURIOUS_APIC_VECTOR;
	apic_write(APIC_SPIV, value);

	/*
	 * Set up LVT0, LVT1:
	 *
	 * set up through-local-APIC on the BP's LINT0. This is not
	 * strictly necessary in pure symmetric-IO mode, but sometimes
	 * we delegate interrupts to the 8259A.
	 */
	/*
	 * TODO: set up through-local-APIC from through-I/O-APIC? --macro
	 */
	value = apic_read(APIC_LVT0) & APIC_LVT_MASKED;
	if (!smp_processor_id() && !value) {
		value = APIC_DM_EXTINT;
		apic_printk(APIC_VERBOSE, "enabled ExtINT on CPU#%d\n", smp_processor_id());
	} else {
		value = APIC_DM_EXTINT | APIC_LVT_MASKED;
		apic_printk(APIC_VERBOSE, "masked ExtINT on CPU#%d\n", smp_processor_id());
	}
	apic_write(APIC_LVT0, value);

	/*
	 * only the BP should see the LINT1 NMI signal, obviously.
	 */
	if (!smp_processor_id())
		value = APIC_DM_NMI;
	else
		value = APIC_DM_NMI | APIC_LVT_MASKED;
	apic_write(APIC_LVT1, value);

	{
		unsigned oldvalue;
		maxlvt = get_maxlvt();
		oldvalue = apic_read(APIC_ESR);
		value = ERROR_APIC_VECTOR;      // enables sending errors
		apic_write(APIC_LVTERR, value);
		/*
		 * spec says clear errors after enabling vector.
		 */
		if (maxlvt > 3)
			apic_write(APIC_ESR, 0);
		value = apic_read(APIC_ESR);
		if (value != oldvalue)
			apic_printk(APIC_VERBOSE,
			"ESR value after enabling vector: %08x, after %08x\n",
			oldvalue, value);
	}

	nmi_watchdog_default();
	setup_apic_nmi_watchdog(NULL);
	apic_pm_activate();
}

#ifdef CONFIG_PM

static struct {
	/* 'active' is true if the local APIC was enabled by us and
	   not the BIOS; this signifies that we are also responsible
	   for disabling it before entering apm/acpi suspend */
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
} apic_pm_state;

static int lapic_suspend(struct sys_device *dev, pm_message_t state)
{
	unsigned long flags;
	int maxlvt;

	if (!apic_pm_state.active)
		return 0;

	maxlvt = get_maxlvt();

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
#ifdef CONFIG_X86_MCE_INTEL
	if (maxlvt >= 5)
		apic_pm_state.apic_thmr = apic_read(APIC_LVTTHMR);
#endif
	local_irq_save(flags);
	disable_local_APIC();
	local_irq_restore(flags);
	return 0;
}

static int lapic_resume(struct sys_device *dev)
{
	unsigned int l, h;
	unsigned long flags;
	int maxlvt;

	if (!apic_pm_state.active)
		return 0;

	maxlvt = get_maxlvt();

	local_irq_save(flags);
	rdmsr(MSR_IA32_APICBASE, l, h);
	l &= ~MSR_IA32_APICBASE_BASE;
	l |= MSR_IA32_APICBASE_ENABLE | mp_lapic_addr;
	wrmsr(MSR_IA32_APICBASE, l, h);
	apic_write(APIC_LVTERR, ERROR_APIC_VECTOR | APIC_LVT_MASKED);
	apic_write(APIC_ID, apic_pm_state.apic_id);
	apic_write(APIC_DFR, apic_pm_state.apic_dfr);
	apic_write(APIC_LDR, apic_pm_state.apic_ldr);
	apic_write(APIC_TASKPRI, apic_pm_state.apic_taskpri);
	apic_write(APIC_SPIV, apic_pm_state.apic_spiv);
	apic_write(APIC_LVT0, apic_pm_state.apic_lvt0);
	apic_write(APIC_LVT1, apic_pm_state.apic_lvt1);
#ifdef CONFIG_X86_MCE_INTEL
	if (maxlvt >= 5)
		apic_write(APIC_LVTTHMR, apic_pm_state.apic_thmr);
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
	local_irq_restore(flags);
	return 0;
}

static struct sysdev_class lapic_sysclass = {
	set_kset_name("lapic"),
	.resume		= lapic_resume,
	.suspend	= lapic_suspend,
};

static struct sys_device device_lapic = {
	.id		= 0,
	.cls		= &lapic_sysclass,
};

static void __cpuinit apic_pm_activate(void)
{
	apic_pm_state.active = 1;
}

static int __init init_lapic_sysfs(void)
{
	int error;
	if (!cpu_has_apic)
		return 0;
	/* XXX: remove suspend/resume procs if !apic_pm_state.active? */
	error = sysdev_class_register(&lapic_sysclass);
	if (!error)
		error = sysdev_register(&device_lapic);
	return error;
}
device_initcall(init_lapic_sysfs);

#else	/* CONFIG_PM */

static void apic_pm_activate(void) { }

#endif	/* CONFIG_PM */

static int __init apic_set_verbosity(char *str)
{
	if (str == NULL)  {
		skip_ioapic_setup = 0;
		ioapic_force = 1;
		return 0;
	}
	if (strcmp("debug", str) == 0)
		apic_verbosity = APIC_DEBUG;
	else if (strcmp("verbose", str) == 0)
		apic_verbosity = APIC_VERBOSE;
	else {
		printk(KERN_WARNING "APIC Verbosity level %s not recognised"
				" use apic=verbose or apic=debug\n", str);
		return -EINVAL;
	}

	return 0;
}
early_param("apic", apic_set_verbosity);

/*
 * Detect and enable local APICs on non-SMP boards.
 * Original code written by Keir Fraser.
 * On AMD64 we trust the BIOS - if it says no APIC it is likely
 * not correctly set up (usually the APIC timer won't work etc.)
 */

static int __init detect_init_APIC (void)
{
	if (!cpu_has_apic) {
		printk(KERN_INFO "No local APIC present\n");
		return -1;
	}

	mp_lapic_addr = APIC_DEFAULT_PHYS_BASE;
	boot_cpu_id = 0;
	return 0;
}

#ifdef CONFIG_X86_IO_APIC
static struct resource * __init ioapic_setup_resources(void)
{
#define IOAPIC_RESOURCE_NAME_SIZE 11
	unsigned long n;
	struct resource *res;
	char *mem;
	int i;

	if (nr_ioapics <= 0)
		return NULL;

	n = IOAPIC_RESOURCE_NAME_SIZE + sizeof(struct resource);
	n *= nr_ioapics;

	mem = alloc_bootmem(n);
	res = (void *)mem;

	if (mem != NULL) {
		memset(mem, 0, n);
		mem += sizeof(struct resource) * nr_ioapics;

		for (i = 0; i < nr_ioapics; i++) {
			res[i].name = mem;
			res[i].flags = IORESOURCE_MEM | IORESOURCE_BUSY;
			sprintf(mem,  "IOAPIC %u", i);
			mem += IOAPIC_RESOURCE_NAME_SIZE;
		}
	}

	ioapic_resources = res;

	return res;
}

static int __init ioapic_insert_resources(void)
{
	int i;
	struct resource *r = ioapic_resources;

	if (!r) {
		printk("IO APIC resources could be not be allocated.\n");
		return -1;
	}

	for (i = 0; i < nr_ioapics; i++) {
		insert_resource(&iomem_resource, r);
		r++;
	}

	return 0;
}

/* Insert the IO APIC resources after PCI initialization has occured to handle
 * IO APICS that are mapped in on a BAR in PCI space. */
late_initcall(ioapic_insert_resources);
#endif

void __init init_apic_mappings(void)
{
	unsigned long apic_phys;

	/*
	 * If no local APIC can be found then set up a fake all
	 * zeroes page to simulate the local APIC and another
	 * one for the IO-APIC.
	 */
	if (!smp_found_config && detect_init_APIC()) {
		apic_phys = (unsigned long) alloc_bootmem_pages(PAGE_SIZE);
		apic_phys = __pa(apic_phys);
	} else
		apic_phys = mp_lapic_addr;

	set_fixmap_nocache(FIX_APIC_BASE, apic_phys);
	apic_mapped = 1;
	apic_printk(APIC_VERBOSE,"mapped APIC to %16lx (%16lx)\n", APIC_BASE, apic_phys);

	/* Put local APIC into the resource map. */
	lapic_resource.start = apic_phys;
	lapic_resource.end = lapic_resource.start + PAGE_SIZE - 1;
	insert_resource(&iomem_resource, &lapic_resource);

	/*
	 * Fetch the APIC ID of the BSP in case we have a
	 * default configuration (or the MP table is broken).
	 */
	boot_cpu_id = GET_APIC_ID(apic_read(APIC_ID));

	{
		unsigned long ioapic_phys, idx = FIX_IO_APIC_BASE_0;
		int i;
		struct resource *ioapic_res;

		ioapic_res = ioapic_setup_resources();
		for (i = 0; i < nr_ioapics; i++) {
			if (smp_found_config) {
				ioapic_phys = mp_ioapics[i].mpc_apicaddr;
			} else {
				ioapic_phys = (unsigned long) alloc_bootmem_pages(PAGE_SIZE);
				ioapic_phys = __pa(ioapic_phys);
			}
			set_fixmap_nocache(idx, ioapic_phys);
			apic_printk(APIC_VERBOSE,"mapped IOAPIC to %016lx (%016lx)\n",
					__fix_to_virt(idx), ioapic_phys);
			idx++;

			if (ioapic_res != NULL) {
				ioapic_res->start = ioapic_phys;
				ioapic_res->end = ioapic_phys + (4 * 1024) - 1;
				ioapic_res++;
			}
		}
	}
}

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

#define APIC_DIVISOR 16

static void __setup_APIC_LVTT(unsigned int clocks)
{
	unsigned int lvtt_value, tmp_value;
	int cpu = smp_processor_id();

	lvtt_value = APIC_LVT_TIMER_PERIODIC | LOCAL_TIMER_VECTOR;

	if (cpu_isset(cpu, timer_interrupt_broadcast_ipi_mask))
		lvtt_value |= APIC_LVT_MASKED;

	apic_write(APIC_LVTT, lvtt_value);

	/*
	 * Divide PICLK by 16
	 */
	tmp_value = apic_read(APIC_TDCR);
	apic_write(APIC_TDCR, (tmp_value
				& ~(APIC_TDR_DIV_1 | APIC_TDR_DIV_TMBASE))
				| APIC_TDR_DIV_16);

	apic_write(APIC_TMICT, clocks/APIC_DIVISOR);
}

static void setup_APIC_timer(unsigned int clocks)
{
	unsigned long flags;

	local_irq_save(flags);

	/* wait for irq slice */
	if (hpet_address && hpet_use_timer) {
		u32 trigger = hpet_readl(HPET_T0_CMP);
		while (hpet_readl(HPET_T0_CMP) == trigger)
			/* do nothing */ ;
	} else {
		int c1, c2;
		outb_p(0x00, 0x43);
		c2 = inb_p(0x40);
		c2 |= inb_p(0x40) << 8;
		do {
			c1 = c2;
			outb_p(0x00, 0x43);
			c2 = inb_p(0x40);
			c2 |= inb_p(0x40) << 8;
		} while (c2 - c1 < 300);
	}
	__setup_APIC_LVTT(clocks);
	/* Turn off PIT interrupt if we use APIC timer as main timer.
	   Only works with the PM timer right now
	   TBD fix it for HPET too. */
	if ((pmtmr_ioport != 0) &&
		smp_processor_id() == boot_cpu_id &&
		apic_runs_main_timer == 1 &&
		!cpu_isset(boot_cpu_id, timer_interrupt_broadcast_ipi_mask)) {
		stop_timer_interrupt();
		apic_runs_main_timer++;
	}
	local_irq_restore(flags);
}

/*
 * In this function we calibrate APIC bus clocks to the external
 * timer. Unfortunately we cannot use jiffies and the timer irq
 * to calibrate, since some later bootup code depends on getting
 * the first irq? Ugh.
 *
 * We want to do the calibration only once since we
 * want to have local timer irqs syncron. CPUs connected
 * by the same APIC bus have the very same bus frequency.
 * And we want to have irqs off anyways, no accidental
 * APIC irq that way.
 */

#define TICK_COUNT 100000000

static int __init calibrate_APIC_clock(void)
{
	unsigned apic, apic_start;
	unsigned long tsc, tsc_start;
	int result;
	/*
	 * Put whatever arbitrary (but long enough) timeout
	 * value into the APIC clock, we just want to get the
	 * counter running for calibration.
	 */
	__setup_APIC_LVTT(4000000000);

	apic_start = apic_read(APIC_TMCCT);
#ifdef CONFIG_X86_PM_TIMER
	if (apic_calibrate_pmtmr && pmtmr_ioport) {
		pmtimer_wait(5000);  /* 5ms wait */
		apic = apic_read(APIC_TMCCT);
		result = (apic_start - apic) * 1000L / 5;
	} else
#endif
	{
		rdtscll(tsc_start);

		do {
			apic = apic_read(APIC_TMCCT);
			rdtscll(tsc);
		} while ((tsc - tsc_start) < TICK_COUNT &&
				(apic_start - apic) < TICK_COUNT);

		result = (apic_start - apic) * 1000L * tsc_khz /
					(tsc - tsc_start);
	}
	printk("result %d\n", result);


	printk(KERN_INFO "Detected %d.%03d MHz APIC timer.\n",
		result / 1000 / 1000, result / 1000 % 1000);

	return result * APIC_DIVISOR / HZ;
}

static unsigned int calibration_result;

void __init setup_boot_APIC_clock (void)
{
	if (disable_apic_timer) {
		printk(KERN_INFO "Disabling APIC timer\n");
		return;
	}

	printk(KERN_INFO "Using local APIC timer interrupts.\n");
	using_apic_timer = 1;

	local_irq_disable();

	calibration_result = calibrate_APIC_clock();
	/*
	 * Now set up the timer for real.
	 */
	setup_APIC_timer(calibration_result);

	local_irq_enable();
}

void __cpuinit setup_secondary_APIC_clock(void)
{
	local_irq_disable(); /* FIXME: Do we need this? --RR */
	setup_APIC_timer(calibration_result);
	local_irq_enable();
}

void disable_APIC_timer(void)
{
	if (using_apic_timer) {
		unsigned long v;

		v = apic_read(APIC_LVTT);
		/*
		 * When an illegal vector value (0-15) is written to an LVT
		 * entry and delivery mode is Fixed, the APIC may signal an
		 * illegal vector error, with out regard to whether the mask
		 * bit is set or whether an interrupt is actually seen on input.
		 *
		 * Boot sequence might call this function when the LVTT has
		 * '0' vector value. So make sure vector field is set to
		 * valid value.
		 */
		v |= (APIC_LVT_MASKED | LOCAL_TIMER_VECTOR);
		apic_write(APIC_LVTT, v);
	}
}

void enable_APIC_timer(void)
{
	int cpu = smp_processor_id();

	if (using_apic_timer &&
	    !cpu_isset(cpu, timer_interrupt_broadcast_ipi_mask)) {
		unsigned long v;

		v = apic_read(APIC_LVTT);
		apic_write(APIC_LVTT, v & ~APIC_LVT_MASKED);
	}
}

void switch_APIC_timer_to_ipi(void *cpumask)
{
	cpumask_t mask = *(cpumask_t *)cpumask;
	int cpu = smp_processor_id();

	if (cpu_isset(cpu, mask) &&
	    !cpu_isset(cpu, timer_interrupt_broadcast_ipi_mask)) {
		disable_APIC_timer();
		cpu_set(cpu, timer_interrupt_broadcast_ipi_mask);
	}
}
EXPORT_SYMBOL(switch_APIC_timer_to_ipi);

void smp_send_timer_broadcast_ipi(void)
{
	int cpu = smp_processor_id();
	cpumask_t mask;

	cpus_and(mask, cpu_online_map, timer_interrupt_broadcast_ipi_mask);

	if (cpu_isset(cpu, mask)) {
		cpu_clear(cpu, mask);
		add_pda(apic_timer_irqs, 1);
		smp_local_timer_interrupt();
	}

	if (!cpus_empty(mask)) {
		send_IPI_mask(mask, LOCAL_TIMER_VECTOR);
	}
}

void switch_ipi_to_APIC_timer(void *cpumask)
{
	cpumask_t mask = *(cpumask_t *)cpumask;
	int cpu = smp_processor_id();

	if (cpu_isset(cpu, mask) &&
	    cpu_isset(cpu, timer_interrupt_broadcast_ipi_mask)) {
		cpu_clear(cpu, timer_interrupt_broadcast_ipi_mask);
		enable_APIC_timer();
	}
}
EXPORT_SYMBOL(switch_ipi_to_APIC_timer);

int setup_profiling_timer(unsigned int multiplier)
{
	return -EINVAL;
}

void setup_APIC_extended_lvt(unsigned char lvt_off, unsigned char vector,
			     unsigned char msg_type, unsigned char mask)
{
	unsigned long reg = (lvt_off << 4) + K8_APIC_EXT_LVT_BASE;
	unsigned int  v   = (mask << 16) | (msg_type << 8) | vector;
	apic_write(reg, v);
}

#undef APIC_DIVISOR

/*
 * Local timer interrupt handler. It does both profiling and
 * process statistics/rescheduling.
 *
 * We do profiling in every local tick, statistics/rescheduling
 * happen only every 'profiling multiplier' ticks. The default
 * multiplier is 1 and it can be changed by writing the new multiplier
 * value into /proc/profile.
 */

void smp_local_timer_interrupt(void)
{
	profile_tick(CPU_PROFILING);
#ifdef CONFIG_SMP
	update_process_times(user_mode(get_irq_regs()));
#endif
	if (apic_runs_main_timer > 1 && smp_processor_id() == boot_cpu_id)
		main_timer_handler();
	/*
	 * We take the 'long' return path, and there every subsystem
	 * grabs the appropriate locks (kernel lock/ irq lock).
	 *
	 * We might want to decouple profiling from the 'long path',
	 * and do the profiling totally in assembly.
	 *
	 * Currently this isn't too much of an issue (performance wise),
	 * we can take more than 100K local irqs per second on a 100 MHz P5.
	 */
}

/*
 * Local APIC timer interrupt. This is the most natural way for doing
 * local interrupts, but local timer interrupts can be emulated by
 * broadcast interrupts too. [in case the hw doesn't support APIC timers]
 *
 * [ if a single-CPU system runs an SMP kernel then we call the local
 *   interrupt as well. Thus we cannot inline the local irq ... ]
 */
void smp_apic_timer_interrupt(struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	/*
	 * the NMI deadlock-detector uses this.
	 */
	add_pda(apic_timer_irqs, 1);

	/*
	 * NOTE! We'd better ACK the irq immediately,
	 * because timer handling can be slow.
	 */
	ack_APIC_irq();
	/*
	 * update_process_times() expects us to have done irq_enter().
	 * Besides, if we don't timer interrupts ignore the global
	 * interrupt lock, which is the WrongThing (tm) to do.
	 */
	exit_idle();
	irq_enter();
	smp_local_timer_interrupt();
	irq_exit();
	set_irq_regs(old_regs);
}

/*
 * apic_is_clustered_box() -- Check if we can expect good TSC
 *
 * Thus far, the major user of this is IBM's Summit2 series:
 *
 * Clustered boxes may have unsynced TSC problems if they are
 * multi-chassis. Use available data to take a good guess.
 * If in doubt, go HPET.
 */
__cpuinit int apic_is_clustered_box(void)
{
	int i, clusters, zeros;
	unsigned id;
	DECLARE_BITMAP(clustermap, NUM_APIC_CLUSTERS);

	bitmap_zero(clustermap, NUM_APIC_CLUSTERS);

	for (i = 0; i < NR_CPUS; i++) {
		id = bios_cpu_apicid[i];
		if (id != BAD_APICID)
			__set_bit(APIC_CLUSTERID(id), clustermap);
	}

	/* Problem:  Partially populated chassis may not have CPUs in some of
	 * the APIC clusters they have been allocated.  Only present CPUs have
	 * bios_cpu_apicid entries, thus causing zeroes in the bitmap.  Since
	 * clusters are allocated sequentially, count zeros only if they are
	 * bounded by ones.
	 */
	clusters = 0;
	zeros = 0;
	for (i = 0; i < NUM_APIC_CLUSTERS; i++) {
		if (test_bit(i, clustermap)) {
			clusters += 1 + zeros;
			zeros = 0;
		} else
			++zeros;
	}

	/*
	 * If clusters > 2, then should be multi-chassis.
	 * May have to revisit this when multi-core + hyperthreaded CPUs come
	 * out, but AFAIK this will work even for them.
	 */
	return (clusters > 2);
}

/*
 * This interrupt should _never_ happen with our APIC/SMP architecture
 */
asmlinkage void smp_spurious_interrupt(void)
{
	unsigned int v;
	exit_idle();
	irq_enter();
	/*
	 * Check if this really is a spurious interrupt and ACK it
	 * if it is a vectored one.  Just in case...
	 * Spurious interrupts should not be ACKed.
	 */
	v = apic_read(APIC_ISR + ((SPURIOUS_APIC_VECTOR & ~0x1f) >> 1));
	if (v & (1 << (SPURIOUS_APIC_VECTOR & 0x1f)))
		ack_APIC_irq();

	irq_exit();
}

/*
 * This interrupt should never happen with our APIC/SMP architecture
 */

asmlinkage void smp_error_interrupt(void)
{
	unsigned int v, v1;

	exit_idle();
	irq_enter();
	/* First tickle the hardware, only then report what went on. -- REW */
	v = apic_read(APIC_ESR);
	apic_write(APIC_ESR, 0);
	v1 = apic_read(APIC_ESR);
	ack_APIC_irq();
	atomic_inc(&irq_err_count);

	/* Here is what the APIC error bits mean:
	   0: Send CS error
	   1: Receive CS error
	   2: Send accept error
	   3: Receive accept error
	   4: Reserved
	   5: Send illegal vector
	   6: Received illegal vector
	   7: Illegal register address
	*/
	printk (KERN_DEBUG "APIC error on CPU%d: %02x(%02x)\n",
		smp_processor_id(), v , v1);
	irq_exit();
}

int disable_apic;

/*
 * This initializes the IO-APIC and APIC hardware if this is
 * a UP kernel.
 */
int __init APIC_init_uniprocessor (void)
{
	if (disable_apic) {
		printk(KERN_INFO "Apic disabled\n");
		return -1;
	}
	if (!cpu_has_apic) {
		disable_apic = 1;
		printk(KERN_INFO "Apic disabled by BIOS\n");
		return -1;
	}

	verify_local_APIC();

	phys_cpu_present_map = physid_mask_of_physid(boot_cpu_id);
	apic_write(APIC_ID, SET_APIC_ID(boot_cpu_id));

	setup_local_APIC();

	if (smp_found_config && !skip_ioapic_setup && nr_ioapics)
		setup_IO_APIC();
	else
		nr_ioapics = 0;
	setup_boot_APIC_clock();
	check_nmi_watchdog();
	return 0;
}

static __init int setup_disableapic(char *str)
{
	disable_apic = 1;
	clear_bit(X86_FEATURE_APIC, boot_cpu_data.x86_capability);
	return 0;
}
early_param("disableapic", setup_disableapic);

/* same as disableapic, for compatibility */
static __init int setup_nolapic(char *str)
{
	return setup_disableapic(str);
}
early_param("nolapic", setup_nolapic);

static int __init parse_lapic_timer_c2_ok(char *arg)
{
	local_apic_timer_c2_ok = 1;
	return 0;
}
early_param("lapic_timer_c2_ok", parse_lapic_timer_c2_ok);

static __init int setup_noapictimer(char *str)
{
	if (str[0] != ' ' && str[0] != 0)
		return 0;
	disable_apic_timer = 1;
	return 1;
}

static __init int setup_apicmaintimer(char *str)
{
	apic_runs_main_timer = 1;
	nohpet = 1;
	return 1;
}
__setup("apicmaintimer", setup_apicmaintimer);

static __init int setup_noapicmaintimer(char *str)
{
	apic_runs_main_timer = -1;
	return 1;
}
__setup("noapicmaintimer", setup_noapicmaintimer);

static __init int setup_apicpmtimer(char *s)
{
	apic_calibrate_pmtmr = 1;
	notsc_setup(NULL);
	return setup_apicmaintimer(NULL);
}
__setup("apicpmtimer", setup_apicpmtimer);

__setup("noapictimer", setup_noapictimer);

