/*
 *  linux/arch/i386/kernel/reboot.c
 */

#include <linux/mm.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/mc146818rtc.h>
#include <linux/efi.h>
#include <linux/dmi.h>
#include <asm/uaccess.h>
#include <asm/apic.h>
#include "mach_reboot.h"

/*
 * Power off function, if any
 */
void (*pm_power_off)(void);

static int reboot_mode;
static int reboot_thru_bios;

#ifdef CONFIG_SMP
int reboot_smp = 0;
static int reboot_cpu = -1;
/* shamelessly grabbed from lib/vsprintf.c for readability */
#define is_digit(c)	((c) >= '0' && (c) <= '9')
#endif
static int __init reboot_setup(char *str)
{
	while(1) {
		switch (*str) {
		case 'w': /* "warm" reboot (no memory testing etc) */
			reboot_mode = 0x1234;
			break;
		case 'c': /* "cold" reboot (with memory testing etc) */
			reboot_mode = 0x0;
			break;
		case 'b': /* "bios" reboot by jumping through the BIOS */
			reboot_thru_bios = 1;
			break;
		case 'h': /* "hard" reboot by toggling RESET and/or crashing the CPU */
			reboot_thru_bios = 0;
			break;
#ifdef CONFIG_SMP
		case 's': /* "smp" reboot by executing reset on BSP or other CPU*/
			reboot_smp = 1;
			if (is_digit(*(str+1))) {
				reboot_cpu = (int) (*(str+1) - '0');
				if (is_digit(*(str+2))) 
					reboot_cpu = reboot_cpu*10 + (int)(*(str+2) - '0');
			}
				/* we will leave sorting out the final value 
				when we are ready to reboot, since we might not
 				have set up boot_cpu_id or smp_num_cpu */
			break;
#endif
		}
		if((str = strchr(str,',')) != NULL)
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
 * Some machines require the "reboot=b"  commandline option, this quirk makes that automatic.
 */
static int __init set_bios_reboot(struct dmi_system_id *d)
{
	if (!reboot_thru_bios) {
		reboot_thru_bios = 1;
		printk(KERN_INFO "%s series board detected. Selecting BIOS-method for reboots.\n", d->ident);
	}
	return 0;
}

/*
 * Some machines require the "reboot=s"  commandline option, this quirk makes that automatic.
 */
static int __init set_smp_reboot(struct dmi_system_id *d)
{
#ifdef CONFIG_SMP
	if (!reboot_smp) {
		reboot_smp = 1;
		printk(KERN_INFO "%s series board detected. Selecting SMP-method for reboots.\n", d->ident);
	}
#endif
	return 0;
}

/*
 * Some machines require the "reboot=b,s"  commandline option, this quirk makes that automatic.
 */
static int __init set_smp_bios_reboot(struct dmi_system_id *d)
{
	set_smp_reboot(d);
	set_bios_reboot(d);
	return 0;
}

static struct dmi_system_id __initdata reboot_dmi_table[] = {
	{	/* Handle problems with rebooting on Dell 1300's */
		.callback = set_smp_bios_reboot,
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
	{	/* Handle problems with rebooting on Dell 2400's */
		.callback = set_bios_reboot,
		.ident = "Dell PowerEdge 2400",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PowerEdge 2400"),
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

static struct
{
	unsigned short       size __attribute__ ((packed));
	unsigned long long * base __attribute__ ((packed));
}
real_mode_gdt = { sizeof (real_mode_gdt_entries) - 1, real_mode_gdt_entries },
real_mode_idt = { 0x3ff, NULL },
no_idt = { 0, NULL };


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
	unsigned long flags;

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

	spin_lock_irqsave(&rtc_lock, flags);
	CMOS_WRITE(0x00, 0x8f);
	spin_unlock_irqrestore(&rtc_lock, flags);

	/* Remap the kernel at virtual address zero, as well as offset zero
	   from the kernel segment.  This assumes the kernel segment starts at
	   virtual address PAGE_OFFSET. */

	memcpy (swapper_pg_dir, swapper_pg_dir + USER_PGD_PTRS,
		sizeof (swapper_pg_dir [0]) * KERNEL_PGD_PTRS);

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

	memcpy ((void *) (0x1000 - sizeof (real_mode_switch) - 100),
		real_mode_switch, sizeof (real_mode_switch));
	memcpy ((void *) (0x1000 - 100), code, length);

	/* Set up the IDT for real mode. */

	__asm__ __volatile__ ("lidt %0" : : "m" (real_mode_idt));

	/* Set up a GDT from which we can load segment descriptors for real
	   mode.  The GDT is not used in real mode; it is just needed here to
	   prepare the descriptors. */

	__asm__ __volatile__ ("lgdt %0" : : "m" (real_mode_gdt));

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
				: "i" ((void *) (0x1000 - sizeof (real_mode_switch) - 100)));
}

void machine_restart(char * __unused)
{
#ifdef CONFIG_SMP
	int cpuid;
	
	cpuid = GET_APIC_ID(apic_read(APIC_ID));

	if (reboot_smp) {

		/* check to see if reboot_cpu is valid 
		   if its not, default to the BSP */
		if ((reboot_cpu == -1) ||  
		      (reboot_cpu > (NR_CPUS -1))  || 
		      !physid_isset(cpuid, phys_cpu_present_map))
			reboot_cpu = boot_cpu_physical_apicid;

		reboot_smp = 0;  /* use this as a flag to only go through this once*/
		/* re-run this function on the other CPUs
		   it will fall though this section since we have 
		   cleared reboot_smp, and do the reboot if it is the
		   correct CPU, otherwise it halts. */
		if (reboot_cpu != cpuid)
			smp_call_function((void *)machine_restart , NULL, 1, 0);
	}

	/* if reboot_cpu is still -1, then we want a tradional reboot, 
	   and if we are not running on the reboot_cpu,, halt */
	if ((reboot_cpu != -1) && (cpuid != reboot_cpu)) {
		for (;;)
		__asm__ __volatile__ ("hlt");
	}
	/*
	 * Stop all CPUs and turn off local APICs and the IO-APIC, so
	 * other OSs see a clean IRQ state.
	 */
	smp_send_stop();
#endif /* CONFIG_SMP */

	lapic_shutdown();

#ifdef CONFIG_X86_IO_APIC
	disable_IO_APIC();
#endif

	if (!reboot_thru_bios) {
		if (efi_enabled) {
			efi.reset_system(EFI_RESET_COLD, EFI_SUCCESS, 0, NULL);
			__asm__ __volatile__("lidt %0": :"m" (no_idt));
			__asm__ __volatile__("int3");
		}
		/* rebooting needs to touch the page at absolute addr 0 */
		*((unsigned short *)__va(0x472)) = reboot_mode;
		for (;;) {
			mach_reboot();
			/* That didn't work - force a triple fault.. */
			__asm__ __volatile__("lidt %0": :"m" (no_idt));
			__asm__ __volatile__("int3");
		}
	}
	if (efi_enabled)
		efi.reset_system(EFI_RESET_WARM, EFI_SUCCESS, 0, NULL);

	machine_real_restart(jump_to_bios, sizeof(jump_to_bios));
}

EXPORT_SYMBOL(machine_restart);

void machine_halt(void)
{
}

EXPORT_SYMBOL(machine_halt);

void machine_power_off(void)
{
	lapic_shutdown();

	if (efi_enabled)
		efi.reset_system(EFI_RESET_SHUTDOWN, EFI_SUCCESS, 0, NULL);
	if (pm_power_off)
		pm_power_off();
}

EXPORT_SYMBOL(machine_power_off);

