#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/efi.h>
#include <linux/dmi.h>
#include <linux/sched.h>
#include <linux/tboot.h>
#include <linux/delay.h>
#include <acpi/reboot.h>
#include <asm/io.h>
#include <asm/apic.h>
#include <asm/desc.h>
#include <asm/hpet.h>
#include <asm/pgtable.h>
#include <asm/proto.h>
#include <asm/reboot_fixups.h>
#include <asm/reboot.h>
#include <asm/pci_x86.h>
#include <asm/virtext.h>
#include <asm/cpu.h>
#include <asm/nmi.h>
#include <asm/smp.h>

#include <linux/ctype.h>
#include <linux/mc146818rtc.h>
#include <asm/realmode.h>
#include <asm/x86_init.h>

/*
 * Power off function, if any
 */
void (*pm_power_off)(void);
EXPORT_SYMBOL(pm_power_off);

static const struct desc_ptr no_idt = {};
static int reboot_mode;
enum reboot_type reboot_type = BOOT_ACPI;
int reboot_force;

/*
 * This variable is used privately to keep track of whether or not
 * reboot_type is still set to its default value (i.e., reboot= hasn't
 * been set on the command line).  This is needed so that we can
 * suppress DMI scanning for reboot quirks.  Without it, it's
 * impossible to override a faulty reboot quirk without recompiling.
 */
static int reboot_default = 1;

#ifdef CONFIG_SMP
static int reboot_cpu = -1;
#endif

/*
 * This is set if we need to go through the 'emergency' path.
 * When machine_emergency_restart() is called, we may be on
 * an inconsistent state and won't be able to do a clean cleanup
 */
static int reboot_emergency;

/* This is set by the PCI code if either type 1 or type 2 PCI is detected */
bool port_cf9_safe = false;

/*
 * reboot=b[ios] | s[mp] | t[riple] | k[bd] | e[fi] [, [w]arm | [c]old] | p[ci]
 * warm   Don't set the cold reboot flag
 * cold   Set the cold reboot flag
 * bios   Reboot by jumping through the BIOS
 * smp    Reboot by executing reset on BSP or other CPU
 * triple Force a triple fault (init)
 * kbd    Use the keyboard controller. cold reset (default)
 * acpi   Use the RESET_REG in the FADT
 * efi    Use efi reset_system runtime service
 * pci    Use the so-called "PCI reset register", CF9
 * force  Avoid anything that could hang.
 */
static int __init reboot_setup(char *str)
{
	for (;;) {
		/*
		 * Having anything passed on the command line via
		 * reboot= will cause us to disable DMI checking
		 * below.
		 */
		reboot_default = 0;

		switch (*str) {
		case 'w':
			reboot_mode = 0x1234;
			break;

		case 'c':
			reboot_mode = 0;
			break;

#ifdef CONFIG_SMP
		case 's':
			if (isdigit(*(str+1))) {
				reboot_cpu = (int) (*(str+1) - '0');
				if (isdigit(*(str+2)))
					reboot_cpu = reboot_cpu*10 + (int)(*(str+2) - '0');
			}
			/*
			 * We will leave sorting out the final value
			 * when we are ready to reboot, since we might not
			 * have detected BSP APIC ID or smp_num_cpu
			 */
			break;
#endif /* CONFIG_SMP */

		case 'b':
		case 'a':
		case 'k':
		case 't':
		case 'e':
		case 'p':
			reboot_type = *str;
			break;

		case 'f':
			reboot_force = 1;
			break;
		}

		str = strchr(str, ',');
		if (str)
			str++;
		else
			break;
	}
	return 1;
}

__setup("reboot=", reboot_setup);


/*
 * Reboot options and system auto-detection code provided by
 * Dell Inc. so their systems "just work". :-)
 */

/*
 * Some machines require the "reboot=b" or "reboot=k"  commandline options,
 * this quirk makes that automatic.
 */
static int __init set_bios_reboot(const struct dmi_system_id *d)
{
	if (reboot_type != BOOT_BIOS) {
		reboot_type = BOOT_BIOS;
		pr_info("%s series board detected. Selecting %s-method for reboots.\n",
			"BIOS", d->ident);
	}
	return 0;
}

void __noreturn machine_real_restart(unsigned int type)
{
	local_irq_disable();

	/*
	 * Write zero to CMOS register number 0x0f, which the BIOS POST
	 * routine will recognize as telling it to do a proper reboot.  (Well
	 * that's what this book in front of me says -- it may only apply to
	 * the Phoenix BIOS though, it's not clear).  At the same time,
	 * disable NMIs by setting the top bit in the CMOS address register,
	 * as we're about to do peculiar things to the CPU.  I'm not sure if
	 * `outb_p' is needed instead of just `outb'.  Use it to be on the
	 * safe side.  (Yes, CMOS_WRITE does outb_p's. -  Paul G.)
	 */
	spin_lock(&rtc_lock);
	CMOS_WRITE(0x00, 0x8f);
	spin_unlock(&rtc_lock);

	/*
	 * Switch back to the initial page table.
	 */
#ifdef CONFIG_X86_32
	load_cr3(initial_page_table);
#else
	write_cr3(real_mode_header->trampoline_pgd);
#endif

	/* Jump to the identity-mapped low memory code */
#ifdef CONFIG_X86_32
	asm volatile("jmpl *%0" : :
		     "rm" (real_mode_header->machine_real_restart_asm),
		     "a" (type));
#else
	asm volatile("ljmpl *%0" : :
		     "m" (real_mode_header->machine_real_restart_asm),
		     "D" (type));
#endif
	unreachable();
}
#ifdef CONFIG_APM_MODULE
EXPORT_SYMBOL(machine_real_restart);
#endif

/*
 * Some Apple MacBook and MacBookPro's needs reboot=p to be able to reboot
 */
static int __init set_pci_reboot(const struct dmi_system_id *d)
{
	if (reboot_type != BOOT_CF9) {
		reboot_type = BOOT_CF9;
		pr_info("%s series board detected. Selecting %s-method for reboots.\n",
			"PCI", d->ident);
	}
	return 0;
}

static int __init set_kbd_reboot(const struct dmi_system_id *d)
{
	if (reboot_type != BOOT_KBD) {
		reboot_type = BOOT_KBD;
		pr_info("%s series board detected. Selecting %s-method for reboot.\n",
			"KBD", d->ident);
	}
	return 0;
}

/*
 * This is a single dmi_table handling all reboot quirks.
 */
static struct dmi_system_id __initdata reboot_dmi_table[] = {
	{	/* Handle problems with rebooting on Dell E520's */
		.callback = set_bios_reboot,
		.ident = "Dell E520",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Dell DM061"),
		},
	},
	{	/* Handle problems with rebooting on Dell 1300's */
		.callback = set_bios_reboot,
		.ident = "Dell PowerEdge 1300",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PowerEdge 1300/"),
		},
	},
	{	/* Handle problems with rebooting on Dell 300's */
		.callback = set_bios_reboot,
		.ident = "Dell PowerEdge 300",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PowerEdge 300/"),
		},
	},
	{	/* Handle problems with rebooting on Dell Optiplex 745's SFF */
		.callback = set_bios_reboot,
		.ident = "Dell OptiPlex 745",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 745"),
		},
	},
	{	/* Handle problems with rebooting on Dell Optiplex 745's DFF */
		.callback = set_bios_reboot,
		.ident = "Dell OptiPlex 745",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 745"),
			DMI_MATCH(DMI_BOARD_NAME, "0MM599"),
		},
	},
	{	/* Handle problems with rebooting on Dell Optiplex 745 with 0KW626 */
		.callback = set_bios_reboot,
		.ident = "Dell OptiPlex 745",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 745"),
			DMI_MATCH(DMI_BOARD_NAME, "0KW626"),
		},
	},
	{	/* Handle problems with rebooting on Dell Optiplex 330 with 0KP561 */
		.callback = set_bios_reboot,
		.ident = "Dell OptiPlex 330",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 330"),
			DMI_MATCH(DMI_BOARD_NAME, "0KP561"),
		},
	},
	{	/* Handle problems with rebooting on Dell Optiplex 360 with 0T656F */
		.callback = set_bios_reboot,
		.ident = "Dell OptiPlex 360",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 360"),
			DMI_MATCH(DMI_BOARD_NAME, "0T656F"),
		},
	},
	{	/* Handle problems with rebooting on Dell OptiPlex 760 with 0G919G */
		.callback = set_bios_reboot,
		.ident = "Dell OptiPlex 760",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 760"),
			DMI_MATCH(DMI_BOARD_NAME, "0G919G"),
		},
	},
	{	/* Handle problems with rebooting on Dell 2400's */
		.callback = set_bios_reboot,
		.ident = "Dell PowerEdge 2400",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PowerEdge 2400"),
		},
	},
	{	/* Handle problems with rebooting on Dell T5400's */
		.callback = set_bios_reboot,
		.ident = "Dell Precision T5400",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Precision WorkStation T5400"),
		},
	},
	{	/* Handle problems with rebooting on Dell T7400's */
		.callback = set_bios_reboot,
		.ident = "Dell Precision T7400",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Precision WorkStation T7400"),
		},
	},
	{	/* Handle problems with rebooting on HP laptops */
		.callback = set_bios_reboot,
		.ident = "HP Compaq Laptop",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Compaq"),
		},
	},
	{	/* Handle problems with rebooting on Dell XPS710 */
		.callback = set_bios_reboot,
		.ident = "Dell XPS710",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Dell XPS710"),
		},
	},
	{	/* Handle problems with rebooting on Dell DXP061 */
		.callback = set_bios_reboot,
		.ident = "Dell DXP061",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Dell DXP061"),
		},
	},
	{	/* Handle problems with rebooting on Sony VGN-Z540N */
		.callback = set_bios_reboot,
		.ident = "Sony VGN-Z540N",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "VGN-Z540N"),
		},
	},
	{	/* Handle problems with rebooting on CompuLab SBC-FITPC2 */
		.callback = set_bios_reboot,
		.ident = "CompuLab SBC-FITPC2",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "CompuLab"),
			DMI_MATCH(DMI_PRODUCT_NAME, "SBC-FITPC2"),
		},
	},
	{	/* Handle problems with rebooting on ASUS P4S800 */
		.callback = set_bios_reboot,
		.ident = "ASUS P4S800",
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "ASUSTeK Computer INC."),
			DMI_MATCH(DMI_BOARD_NAME, "P4S800"),
		},
	},

	{	/* Handle reboot issue on Acer Aspire one */
		.callback = set_kbd_reboot,
		.ident = "Acer Aspire One A110",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_NAME, "AOA110"),
		},
	},
	{	/* Handle problems with rebooting on Apple MacBook5 */
		.callback = set_pci_reboot,
		.ident = "Apple MacBook5",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBook5"),
		},
	},
	{	/* Handle problems with rebooting on Apple MacBookPro5 */
		.callback = set_pci_reboot,
		.ident = "Apple MacBookPro5",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro5"),
		},
	},
	{	/* Handle problems with rebooting on Apple Macmini3,1 */
		.callback = set_pci_reboot,
		.ident = "Apple Macmini3,1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Macmini3,1"),
		},
	},
	{	/* Handle problems with rebooting on the iMac9,1. */
		.callback = set_pci_reboot,
		.ident = "Apple iMac9,1",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "iMac9,1"),
		},
	},
	{	/* Handle problems with rebooting on the Latitude E6320. */
		.callback = set_pci_reboot,
		.ident = "Dell Latitude E6320",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Latitude E6320"),
		},
	},
	{	/* Handle problems with rebooting on the Latitude E5420. */
		.callback = set_pci_reboot,
		.ident = "Dell Latitude E5420",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Latitude E5420"),
		},
	},
	{	/* Handle problems with rebooting on the Latitude E6420. */
		.callback = set_pci_reboot,
		.ident = "Dell Latitude E6420",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Latitude E6420"),
		},
	},
	{	/* Handle problems with rebooting on the OptiPlex 990. */
		.callback = set_pci_reboot,
		.ident = "Dell OptiPlex 990",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 990"),
		},
	},
	{	/* Handle problems with rebooting on the Precision M6600. */
		.callback = set_pci_reboot,
		.ident = "Dell OptiPlex 990",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Precision M6600"),
		},
	},
	{ }
};

static int __init reboot_init(void)
{
	/*
	 * Only do the DMI check if reboot_type hasn't been overridden
	 * on the command line
	 */
	if (reboot_default)
		dmi_check_system(reboot_dmi_table);
	return 0;
}
core_initcall(reboot_init);

static inline void kb_wait(void)
{
	int i;

	for (i = 0; i < 0x10000; i++) {
		if ((inb(0x64) & 0x02) == 0)
			break;
		udelay(2);
	}
}

static void vmxoff_nmi(int cpu, struct pt_regs *regs)
{
	cpu_emergency_vmxoff();
}

/* Use NMIs as IPIs to tell all CPUs to disable virtualization */
static void emergency_vmx_disable_all(void)
{
	/* Just make sure we won't change CPUs while doing this */
	local_irq_disable();

	/*
	 * We need to disable VMX on all CPUs before rebooting, otherwise
	 * we risk hanging up the machine, because the CPU ignore INIT
	 * signals when VMX is enabled.
	 *
	 * We can't take any locks and we may be on an inconsistent
	 * state, so we use NMIs as IPIs to tell the other CPUs to disable
	 * VMX and halt.
	 *
	 * For safety, we will avoid running the nmi_shootdown_cpus()
	 * stuff unnecessarily, but we don't have a way to check
	 * if other CPUs have VMX enabled. So we will call it only if the
	 * CPU we are running on has VMX enabled.
	 *
	 * We will miss cases where VMX is not enabled on all CPUs. This
	 * shouldn't do much harm because KVM always enable VMX on all
	 * CPUs anyway. But we can miss it on the small window where KVM
	 * is still enabling VMX.
	 */
	if (cpu_has_vmx() && cpu_vmx_enabled()) {
		/* Disable VMX on this CPU. */
		cpu_vmxoff();

		/* Halt and disable VMX on the other CPUs */
		nmi_shootdown_cpus(vmxoff_nmi);

	}
}


void __attribute__((weak)) mach_reboot_fixups(void)
{
}

/*
 * Windows compatible x86 hardware expects the following on reboot:
 *
 * 1) If the FADT has the ACPI reboot register flag set, try it
 * 2) If still alive, write to the keyboard controller
 * 3) If still alive, write to the ACPI reboot register again
 * 4) If still alive, write to the keyboard controller again
 *
 * If the machine is still alive at this stage, it gives up. We default to
 * following the same pattern, except that if we're still alive after (4) we'll
 * try to force a triple fault and then cycle between hitting the keyboard
 * controller and doing that
 */
static void native_machine_emergency_restart(void)
{
	int i;
	int attempt = 0;
	int orig_reboot_type = reboot_type;

	if (reboot_emergency)
		emergency_vmx_disable_all();

	tboot_shutdown(TB_SHUTDOWN_REBOOT);

	/* Tell the BIOS if we want cold or warm reboot */
	*((unsigned short *)__va(0x472)) = reboot_mode;

	for (;;) {
		/* Could also try the reset bit in the Hammer NB */
		switch (reboot_type) {
		case BOOT_KBD:
			mach_reboot_fixups(); /* For board specific fixups */

			for (i = 0; i < 10; i++) {
				kb_wait();
				udelay(50);
				outb(0xfe, 0x64); /* Pulse reset low */
				udelay(50);
			}
			if (attempt == 0 && orig_reboot_type == BOOT_ACPI) {
				attempt = 1;
				reboot_type = BOOT_ACPI;
			} else {
				reboot_type = BOOT_TRIPLE;
			}
			break;

		case BOOT_TRIPLE:
			load_idt(&no_idt);
			__asm__ __volatile__("int3");

			reboot_type = BOOT_KBD;
			break;

		case BOOT_BIOS:
			machine_real_restart(MRR_BIOS);

			reboot_type = BOOT_KBD;
			break;

		case BOOT_ACPI:
			acpi_reboot();
			reboot_type = BOOT_KBD;
			break;

		case BOOT_EFI:
			if (efi_enabled)
				efi.reset_system(reboot_mode ?
						 EFI_RESET_WARM :
						 EFI_RESET_COLD,
						 EFI_SUCCESS, 0, NULL);
			reboot_type = BOOT_KBD;
			break;

		case BOOT_CF9:
			port_cf9_safe = true;
			/* Fall through */

		case BOOT_CF9_COND:
			if (port_cf9_safe) {
				u8 cf9 = inb(0xcf9) & ~6;
				outb(cf9|2, 0xcf9); /* Request hard reset */
				udelay(50);
				outb(cf9|6, 0xcf9); /* Actually do the reset */
				udelay(50);
			}
			reboot_type = BOOT_KBD;
			break;
		}
	}
}

void native_machine_shutdown(void)
{
	/* Stop the cpus and apics */
#ifdef CONFIG_SMP

	/* The boot cpu is always logical cpu 0 */
	int reboot_cpu_id = 0;

	/* See if there has been given a command line override */
	if ((reboot_cpu != -1) && (reboot_cpu < nr_cpu_ids) &&
		cpu_online(reboot_cpu))
		reboot_cpu_id = reboot_cpu;

	/* Make certain the cpu I'm about to reboot on is online */
	if (!cpu_online(reboot_cpu_id))
		reboot_cpu_id = smp_processor_id();

	/* Make certain I only run on the appropriate processor */
	set_cpus_allowed_ptr(current, cpumask_of(reboot_cpu_id));

	/*
	 * O.K Now that I'm on the appropriate processor, stop all of the
	 * others. Also disable the local irq to not receive the per-cpu
	 * timer interrupt which may trigger scheduler's load balance.
	 */
	local_irq_disable();
	stop_other_cpus();
#endif

	lapic_shutdown();

#ifdef CONFIG_X86_IO_APIC
	disable_IO_APIC();
#endif

#ifdef CONFIG_HPET_TIMER
	hpet_disable();
#endif

#ifdef CONFIG_X86_64
	x86_platform.iommu_shutdown();
#endif
}

static void __machine_emergency_restart(int emergency)
{
	reboot_emergency = emergency;
	machine_ops.emergency_restart();
}

static void native_machine_restart(char *__unused)
{
	pr_notice("machine restart\n");

	if (!reboot_force)
		machine_shutdown();
	__machine_emergency_restart(0);
}

static void native_machine_halt(void)
{
	/* Stop other cpus and apics */
	machine_shutdown();

	tboot_shutdown(TB_SHUTDOWN_HALT);

	stop_this_cpu(NULL);
}

static void native_machine_power_off(void)
{
	if (pm_power_off) {
		if (!reboot_force)
			machine_shutdown();
		pm_power_off();
	}
	/* A fallback in case there is no PM info available */
	tboot_shutdown(TB_SHUTDOWN_HALT);
}

struct machine_ops machine_ops = {
	.power_off = native_machine_power_off,
	.shutdown = native_machine_shutdown,
	.emergency_restart = native_machine_emergency_restart,
	.restart = native_machine_restart,
	.halt = native_machine_halt,
#ifdef CONFIG_KEXEC
	.crash_shutdown = native_machine_crash_shutdown,
#endif
};

void machine_power_off(void)
{
	machine_ops.power_off();
}

void machine_shutdown(void)
{
	machine_ops.shutdown();
}

void machine_emergency_restart(void)
{
	__machine_emergency_restart(1);
}

void machine_restart(char *cmd)
{
	machine_ops.restart(cmd);
}

void machine_halt(void)
{
	machine_ops.halt();
}

#ifdef CONFIG_KEXEC
void machine_crash_shutdown(struct pt_regs *regs)
{
	machine_ops.crash_shutdown(regs);
}
#endif


#if defined(CONFIG_SMP)

/* This keeps a track of which one is crashing cpu. */
static int crashing_cpu;
static nmi_shootdown_cb shootdown_callback;

static atomic_t waiting_for_crash_ipi;

static int crash_nmi_callback(unsigned int val, struct pt_regs *regs)
{
	int cpu;

	cpu = raw_smp_processor_id();

	/*
	 * Don't do anything if this handler is invoked on crashing cpu.
	 * Otherwise, system will completely hang. Crashing cpu can get
	 * an NMI if system was initially booted with nmi_watchdog parameter.
	 */
	if (cpu == crashing_cpu)
		return NMI_HANDLED;
	local_irq_disable();

	shootdown_callback(cpu, regs);

	atomic_dec(&waiting_for_crash_ipi);
	/* Assume hlt works */
	halt();
	for (;;)
		cpu_relax();

	return NMI_HANDLED;
}

static void smp_send_nmi_allbutself(void)
{
	apic->send_IPI_allbutself(NMI_VECTOR);
}

/*
 * Halt all other CPUs, calling the specified function on each of them
 *
 * This function can be used to halt all other CPUs on crash
 * or emergency reboot time. The function passed as parameter
 * will be called inside a NMI handler on all CPUs.
 */
void nmi_shootdown_cpus(nmi_shootdown_cb callback)
{
	unsigned long msecs;
	local_irq_disable();

	/* Make a note of crashing cpu. Will be used in NMI callback. */
	crashing_cpu = safe_smp_processor_id();

	shootdown_callback = callback;

	atomic_set(&waiting_for_crash_ipi, num_online_cpus() - 1);
	/* Would it be better to replace the trap vector here? */
	if (register_nmi_handler(NMI_LOCAL, crash_nmi_callback,
				 NMI_FLAG_FIRST, "crash"))
		return;		/* Return what? */
	/*
	 * Ensure the new callback function is set before sending
	 * out the NMI
	 */
	wmb();

	smp_send_nmi_allbutself();

	msecs = 1000; /* Wait at most a second for the other cpus to stop */
	while ((atomic_read(&waiting_for_crash_ipi) > 0) && msecs) {
		mdelay(1);
		msecs--;
	}

	/* Leave the nmi callback set */
}
#else /* !CONFIG_SMP */
void nmi_shootdown_cpus(nmi_shootdown_cb callback)
{
	/* No other CPUs to shoot down */
}
#endif
