// SPDX-License-Identifier: GPL-2.0

#include <linux/linkage.h>
#include <linux/types.h>

#include <asm/desc.h>
#include <asm/init.h>
#include <asm/setup.h>
#include <asm/sev.h>
#include <asm/trapnr.h>

/*
 * Data structures and code used for IDT setup in head_64.S. The bringup-IDT is
 * used until the idt_table takes over. On the boot CPU this happens in
 * x86_64_start_kernel(), on secondary CPUs in start_secondary(). In both cases
 * this happens in the functions called from head_64.S.
 *
 * The idt_table can't be used that early because all the code modifying it is
 * in idt.c and can be instrumented by tracing or KASAN, which both don't work
 * during early CPU bringup. Also the idt_table has the runtime vectors
 * configured which require certain CPU state to be setup already (like TSS),
 * which also hasn't happened yet in early CPU bringup.
 */
static gate_desc bringup_idt_table[NUM_EXCEPTION_VECTORS] __page_aligned_data;

/* This may run while still in the direct mapping */
void startup_64_load_idt(void *vc_handler)
{
	struct desc_ptr desc = {
		.address = (unsigned long)rip_rel_ptr(bringup_idt_table),
		.size    = sizeof(bringup_idt_table) - 1,
	};
	struct idt_data data;
	gate_desc idt_desc;

	/* @vc_handler is set only for a VMM Communication Exception */
	if (vc_handler) {
		init_idt_data(&data, X86_TRAP_VC, vc_handler);
		idt_init_desc(&idt_desc, &data);
		native_write_idt_entry((gate_desc *)desc.address, X86_TRAP_VC, &idt_desc);
	}

	native_load_idt(&desc);
}

/*
 * Setup boot CPU state needed before kernel switches to virtual addresses.
 */
void __init startup_64_setup_gdt_idt(void)
{
	struct gdt_page *gp = rip_rel_ptr((void *)(__force unsigned long)&gdt_page);
	void *handler = NULL;

	struct desc_ptr startup_gdt_descr = {
		.address = (unsigned long)gp->gdt,
		.size    = GDT_SIZE - 1,
	};

	/* Load GDT */
	native_load_gdt(&startup_gdt_descr);

	/* New GDT is live - reload data segment registers */
	asm volatile("movl %%eax, %%ds\n"
		     "movl %%eax, %%ss\n"
		     "movl %%eax, %%es\n" : : "a"(__KERNEL_DS) : "memory");

	if (IS_ENABLED(CONFIG_AMD_MEM_ENCRYPT))
		handler = rip_rel_ptr(vc_no_ghcb);

	startup_64_load_idt(handler);
}
