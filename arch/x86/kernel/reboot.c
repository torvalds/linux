#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/pm.h>
#include <linux/efi.h>
#include <acpi/reboot.h>
#include <asm/io.h>
#include <asm/apic.h>
#include <asm/desc.h>
#include <asm/hpet.h>
#include <asm/pgtable.h>
#include <asm/proto.h>
#include <asm/reboot_fixups.h>
#include <asm/reboot.h>

#ifdef CONFIG_X86_32
# include <linux/dmi.h>
# include <linux/ctype.h>
# include <linux/mc146818rtc.h>
#else
# include <asm/iommu.h>
#endif

/*
 * Power off function, if any
 */
void (*pm_power_off)(void);
EXPORT_SYMBOL(pm_power_off);

static long no_idt[3];
static int reboot_mode;
enum reboot_type reboot_type = BOOT_KBD;
int reboot_force;

#if defined(CONFIG_X86_32) && defined(CONFIG_SMP)
static int reboot_cpu = -1;
#endif

/* reboot=b[ios] | s[mp] | t[riple] | k[bd] | e[fi] [, [w]arm | [c]old]
   warm   Don't set the cold reboot flag
   cold   Set the cold reboot flag
   bios   Reboot by jumping through the BIOS (only for X86_32)
   smp    Reboot by executing reset on BSP or other CPU (only for X86_32)
   triple Force a triple fault (init)
   kbd    Use the keyboard controller. cold reset (default)
   acpi   Use the RESET_REG in the FADT
   efi    Use efi reset_system runtime service
   force  Avoid anything that could hang.
 */
static int __init reboot_setup(char *str)
{
	for (;;) {
		switch (*str) {
		case 'w':
			reboot_mode = 0x1234;
			break;

		case 'c':
			reboot_mode = 0;
			break;

#ifdef CONFIG_X86_32
#ifdef CONFIG_SMP
		case 's':
			if (isdigit(*(str+1))) {
				reboot_cpu = (int) (*(str+1) - '0');
				if (isdigit(*(str+2)))
					reboot_cpu = reboot_cpu*10 + (int)(*(str+2) - '0');
			}
				/* we will leave sorting out the final value
				   when we are ready to reboot, since we might not
				   have set up boot_cpu_id or smp_num_cpu */
			break;
#endif /* CONFIG_SMP */

		case 'b':
#endif
		case 'a':
		case 'k':
		case 't':
		case 'e':
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


#ifdef CONFIG_X86_32
/*
 * Reboot options and system auto-detection code provided by
 * Dell Inc. so their systems "just work". :-)
 */

/*
 * Some machines require the "reboot=b"  commandline option,
 * this quirk makes that automatic.
 */
static int __init set_bios_reboot(const struct dmi_system_id *d)
{
	if (reboot_type != BOOT_BIOS) {
		reboot_type = BOOT_BIOS;
		printk(KERN_INFO "%s series board detected. Selecting BIOS-method for reboots.\n", d->ident);
	}
	return 0;
}

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
	{       /* Handle problems with rebooting on Dell Optiplex 745's SFF*/
		.callback = set_bios_reboot,
		.ident = "Dell OptiPlex 745",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 745"),
		},
	},
	{       /* Handle problems with rebooting on Dell Optiplex 745's DFF*/
		.callback = set_bios_reboot,
		.ident = "Dell OptiPlex 745",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 745"),
			DMI_MATCH(DMI_BOARD_NAME, "0MM599"),
		},
	},
	{       /* Handle problems with rebooting on Dell Optiplex 745 with 0KW626 */
		.callback = set_bios_reboot,
		.ident = "Dell OptiPlex 745",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 745"),
			DMI_MATCH(DMI_BOARD_NAME, "0KW626"),
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
	{	/* Handle problems with rebooting on HP laptops */
		.callback = set_bios_reboot,
		.ident = "HP Compaq Laptop",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
			DMI_MATCH(DMI_PRODUCT_NAME, "HP Compaq"),
		},
	},
	{ }
};

static int __init reboot_init(void)
{
	dmi_check_system(reboot_dmi_table);
	return 0;
}
core_initcall(reboot_init);

/* The following code and data reboots the machine by switching to real
   mode and jumping to the BIOS reset entry point, as if the CPU has
   really been reset.  The previous version asked the keyboard
   controller to pulse the CPU reset line, which is more thorough, but
   doesn't work with at least one type of 486 motherboard.  It is easy
   to stop this code working; hence the copious comments. */
static unsigned long long
real_mode_gdt_entries [3] =
{
	0x0000000000000000ULL,	/* Null descriptor */
	0x00009a000000ffffULL,	/* 16-bit real-mode 64k code at 0x00000000 */
	0x000092000100ffffULL	/* 16-bit real-mode 64k data at 0x00000100 */
};

static struct desc_ptr
real_mode_gdt = { sizeof (real_mode_gdt_entries) - 1, (long)real_mode_gdt_entries },
real_mode_idt = { 0x3ff, 0 };

/* This is 16-bit protected mode code to disable paging and the cache,
   switch to real mode and jump to the BIOS reset code.

   The instruction that switches to real mode by writing to CR0 must be
   followed immediately by a far jump instruction, which set CS to a
   valid value for real mode, and flushes the prefetch queue to avoid
   running instructions that have already been decoded in protected
   mode.

   Clears all the flags except ET, especially PG (paging), PE
   (protected-mode enable) and TS (task switch for coprocessor state
   save).  Flushes the TLB after paging has been disabled.  Sets CD and
   NW, to disable the cache on a 486, and invalidates the cache.  This
   is more like the state of a 486 after reset.  I don't know if
   something else should be done for other chips.

   More could be done here to set up the registers as if a CPU reset had
   occurred; hopefully real BIOSs don't assume much. */
static unsigned char real_mode_switch [] =
{
	0x66, 0x0f, 0x20, 0xc0,			/*    movl  %cr0,%eax        */
	0x66, 0x83, 0xe0, 0x11,			/*    andl  $0x00000011,%eax */
	0x66, 0x0d, 0x00, 0x00, 0x00, 0x60,	/*    orl   $0x60000000,%eax */
	0x66, 0x0f, 0x22, 0xc0,			/*    movl  %eax,%cr0        */
	0x66, 0x0f, 0x22, 0xd8,			/*    movl  %eax,%cr3        */
	0x66, 0x0f, 0x20, 0xc3,			/*    movl  %cr0,%ebx        */
	0x66, 0x81, 0xe3, 0x00, 0x00, 0x00, 0x60,	/*    andl  $0x60000000,%ebx */
	0x74, 0x02,				/*    jz    f                */
	0x0f, 0x09,				/*    wbinvd                 */
	0x24, 0x10,				/* f: andb  $0x10,al         */
	0x66, 0x0f, 0x22, 0xc0			/*    movl  %eax,%cr0        */
};
static unsigned char jump_to_bios [] =
{
	0xea, 0x00, 0x00, 0xff, 0xff		/*    ljmp  $0xffff,$0x0000  */
};

/*
 * Switch to real mode and then execute the code
 * specified by the code and length parameters.
 * We assume that length will aways be less that 100!
 */
void machine_real_restart(unsigned char *code, int length)
{
	local_irq_disable();

	/* Write zero to CMOS register number 0x0f, which the BIOS POST
	   routine will recognize as telling it to do a proper reboot.  (Well
	   that's what this book in front of me says -- it may only apply to
	   the Phoenix BIOS though, it's not clear).  At the same time,
	   disable NMIs by setting the top bit in the CMOS address register,
	   as we're about to do peculiar things to the CPU.  I'm not sure if
	   `outb_p' is needed instead of just `outb'.  Use it to be on the
	   safe side.  (Yes, CMOS_WRITE does outb_p's. -  Paul G.)
	 */
	spin_lock(&rtc_lock);
	CMOS_WRITE(0x00, 0x8f);
	spin_unlock(&rtc_lock);

	/* Remap the kernel at virtual address zero, as well as offset zero
	   from the kernel segment.  This assumes the kernel segment starts at
	   virtual address PAGE_OFFSET. */
	memcpy(swapper_pg_dir, swapper_pg_dir + KERNEL_PGD_BOUNDARY,
		sizeof(swapper_pg_dir [0]) * KERNEL_PGD_PTRS);

	/*
	 * Use `swapper_pg_dir' as our page directory.
	 */
	load_cr3(swapper_pg_dir);

	/* Write 0x1234 to absolute memory location 0x472.  The BIOS reads
	   this on booting to tell it to "Bypass memory test (also warm
	   boot)".  This seems like a fairly standard thing that gets set by
	   REBOOT.COM programs, and the previous reset routine did this
	   too. */
	*((unsigned short *)0x472) = reboot_mode;

	/* For the switch to real mode, copy some code to low memory.  It has
	   to be in the first 64k because it is running in 16-bit mode, and it
	   has to have the same physical and virtual address, because it turns
	   off paging.  Copy it near the end of the first page, out of the way
	   of BIOS variables. */
	memcpy((void *)(0x1000 - sizeof(real_mode_switch) - 100),
		real_mode_switch, sizeof (real_mode_switch));
	memcpy((void *)(0x1000 - 100), code, length);

	/* Set up the IDT for real mode. */
	load_idt(&real_mode_idt);

	/* Set up a GDT from which we can load segment descriptors for real
	   mode.  The GDT is not used in real mode; it is just needed here to
	   prepare the descriptors. */
	load_gdt(&real_mode_gdt);

	/* Load the data segment registers, and thus the descriptors ready for
	   real mode.  The base address of each segment is 0x100, 16 times the
	   selector value being loaded here.  This is so that the segment
	   registers don't have to be reloaded after switching to real mode:
	   the values are consistent for real mode operation already. */
	__asm__ __volatile__ ("movl $0x0010,%%eax\n"
				"\tmovl %%eax,%%ds\n"
				"\tmovl %%eax,%%es\n"
				"\tmovl %%eax,%%fs\n"
				"\tmovl %%eax,%%gs\n"
				"\tmovl %%eax,%%ss" : : : "eax");

	/* Jump to the 16-bit code that we copied earlier.  It disables paging
	   and the cache, switches to real mode, and jumps to the BIOS reset
	   entry point. */
	__asm__ __volatile__ ("ljmp $0x0008,%0"
				:
				: "i" ((void *)(0x1000 - sizeof (real_mode_switch) - 100)));
}
#ifdef CONFIG_APM_MODULE
EXPORT_SYMBOL(machine_real_restart);
#endif

#endif /* CONFIG_X86_32 */

static inline void kb_wait(void)
{
	int i;

	for (i = 0; i < 0x10000; i++) {
		if ((inb(0x64) & 0x02) == 0)
			break;
		udelay(2);
	}
}

void __attribute__((weak)) mach_reboot_fixups(void)
{
}

static void native_machine_emergency_restart(void)
{
	int i;

	/* Tell the BIOS if we want cold or warm reboot */
	*((unsigned short *)__va(0x472)) = reboot_mode;

	for (;;) {
		/* Could also try the reset bit in the Hammer NB */
		switch (reboot_type) {
		case BOOT_KBD:
			mach_reboot_fixups(); /* for board specific fixups */

			for (i = 0; i < 10; i++) {
				kb_wait();
				udelay(50);
				outb(0xfe, 0x64); /* pulse reset low */
				udelay(50);
			}

		case BOOT_TRIPLE:
			load_idt((const struct desc_ptr *)&no_idt);
			__asm__ __volatile__("int3");

			reboot_type = BOOT_KBD;
			break;

#ifdef CONFIG_X86_32
		case BOOT_BIOS:
			machine_real_restart(jump_to_bios, sizeof(jump_to_bios));

			reboot_type = BOOT_KBD;
			break;
#endif

		case BOOT_ACPI:
			acpi_reboot();
			reboot_type = BOOT_KBD;
			break;


		case BOOT_EFI:
			if (efi_enabled)
				efi.reset_system(reboot_mode ? EFI_RESET_WARM : EFI_RESET_COLD,
						 EFI_SUCCESS, 0, NULL);

			reboot_type = BOOT_KBD;
			break;
		}
	}
}

void native_machine_shutdown(void)
{
	/* Stop the cpus and apics */
#ifdef CONFIG_SMP
	int reboot_cpu_id;

	/* The boot cpu is always logical cpu 0 */
	reboot_cpu_id = 0;

#ifdef CONFIG_X86_32
	/* See if there has been given a command line override */
	if ((reboot_cpu != -1) && (reboot_cpu < NR_CPUS) &&
		cpu_online(reboot_cpu))
		reboot_cpu_id = reboot_cpu;
#endif

	/* Make certain the cpu I'm about to reboot on is online */
	if (!cpu_online(reboot_cpu_id))
		reboot_cpu_id = smp_processor_id();

	/* Make certain I only run on the appropriate processor */
	set_cpus_allowed_ptr(current, &cpumask_of_cpu(reboot_cpu_id));

	/* O.K Now that I'm on the appropriate processor,
	 * stop all of the others.
	 */
	smp_send_stop();
#endif

	lapic_shutdown();

#ifdef CONFIG_X86_IO_APIC
	disable_IO_APIC();
#endif

#ifdef CONFIG_HPET_TIMER
	hpet_disable();
#endif

#ifdef CONFIG_X86_64
	pci_iommu_shutdown();
#endif
}

static void native_machine_restart(char *__unused)
{
	printk("machine restart\n");

	if (!reboot_force)
		machine_shutdown();
	machine_emergency_restart();
}

static void native_machine_halt(void)
{
}

static void native_machine_power_off(void)
{
	if (pm_power_off) {
		if (!reboot_force)
			machine_shutdown();
		pm_power_off();
	}
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
	machine_ops.emergency_restart();
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
