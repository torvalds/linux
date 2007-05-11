/*
 *  linux/arch/x86_64/kernel/head64.c -- prepare to run common code
 *
 *  Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 */

#include <linux/init.h>
#include <linux/linkage.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/percpu.h>

#include <asm/processor.h>
#include <asm/proto.h>
#include <asm/smp.h>
#include <asm/bootsetup.h>
#include <asm/setup.h>
#include <asm/desc.h>
#include <asm/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>

static void __init zap_identity_mappings(void)
{
	pgd_t *pgd = pgd_offset_k(0UL);
	pgd_clear(pgd);
	__flush_tlb();
}

/* Don't add a printk in there. printk relies on the PDA which is not initialized 
   yet. */
static void __init clear_bss(void)
{
	memset(__bss_start, 0,
	       (unsigned long) __bss_stop - (unsigned long) __bss_start);
}

#define NEW_CL_POINTER		0x228	/* Relative to real mode data */
#define OLD_CL_MAGIC_ADDR	0x20
#define OLD_CL_MAGIC            0xA33F
#define OLD_CL_OFFSET           0x22

static void __init copy_bootdata(char *real_mode_data)
{
	unsigned long new_data;
	char * command_line;

	memcpy(x86_boot_params, real_mode_data, BOOT_PARAM_SIZE);
	new_data = *(u32 *) (x86_boot_params + NEW_CL_POINTER);
	if (!new_data) {
		if (OLD_CL_MAGIC != *(u16 *)(real_mode_data + OLD_CL_MAGIC_ADDR)) {
			return;
		}
		new_data = __pa(real_mode_data) + *(u16 *)(real_mode_data + OLD_CL_OFFSET);
	}
	command_line = __va(new_data);
	memcpy(boot_command_line, command_line, COMMAND_LINE_SIZE);
}

void __init x86_64_start_kernel(char * real_mode_data)
{
	int i;

	/* clear bss before set_intr_gate with early_idt_handler */
	clear_bss();

	/* Make NULL pointers segfault */
	zap_identity_mappings();

	for (i = 0; i < IDT_ENTRIES; i++)
		set_intr_gate(i, early_idt_handler);
	asm volatile("lidt %0" :: "m" (idt_descr));

	early_printk("Kernel alive\n");

 	for (i = 0; i < NR_CPUS; i++)
 		cpu_pda(i) = &boot_cpu_pda[i];

	pda_init(0);
	copy_bootdata(__va(real_mode_data));
#ifdef CONFIG_SMP
	cpu_set(0, cpu_online_map);
#endif
	start_kernel();
}
