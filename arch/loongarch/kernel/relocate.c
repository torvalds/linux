// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Kernel relocation at boot time
 *
 * Copyright (C) 2023 Loongson Technology Corporation Limited
 */

#include <linux/elf.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/panic_notifier.h>
#include <linux/start_kernel.h>
#include <asm/bootinfo.h>
#include <asm/early_ioremap.h>
#include <asm/inst.h>
#include <asm/sections.h>
#include <asm/setup.h>

#define RELOCATED(x) ((void *)((long)x + reloc_offset))
#define RELOCATED_KASLR(x) ((void *)((long)x + random_offset))

static unsigned long reloc_offset;

static inline void __init relocate_relative(void)
{
	Elf64_Rela *rela, *rela_end;
	rela = (Elf64_Rela *)&__rela_dyn_begin;
	rela_end = (Elf64_Rela *)&__rela_dyn_end;

	for ( ; rela < rela_end; rela++) {
		Elf64_Addr addr = rela->r_offset;
		Elf64_Addr relocated_addr = rela->r_addend;

		if (rela->r_info != R_LARCH_RELATIVE)
			continue;

		if (relocated_addr >= VMLINUX_LOAD_ADDRESS)
			relocated_addr = (Elf64_Addr)RELOCATED(relocated_addr);

		*(Elf64_Addr *)RELOCATED(addr) = relocated_addr;
	}
}

static inline void __init relocate_absolute(long random_offset)
{
	void *begin, *end;
	struct rela_la_abs *p;

	begin = RELOCATED_KASLR(&__la_abs_begin);
	end   = RELOCATED_KASLR(&__la_abs_end);

	for (p = begin; (void *)p < end; p++) {
		long v = p->symvalue;
		uint32_t lu12iw, ori, lu32id, lu52id;
		union loongarch_instruction *insn = (void *)p - p->offset;

		lu12iw = (v >> 12) & 0xfffff;
		ori    = v & 0xfff;
		lu32id = (v >> 32) & 0xfffff;
		lu52id = v >> 52;

		insn[0].reg1i20_format.immediate = lu12iw;
		insn[1].reg2i12_format.immediate = ori;
		insn[2].reg1i20_format.immediate = lu32id;
		insn[3].reg2i12_format.immediate = lu52id;
	}
}

#ifdef CONFIG_RANDOMIZE_BASE
static inline __init unsigned long rotate_xor(unsigned long hash,
					      const void *area, size_t size)
{
	size_t i, diff;
	const typeof(hash) *ptr = PTR_ALIGN(area, sizeof(hash));

	diff = (void *)ptr - area;
	if (size < diff + sizeof(hash))
		return hash;

	size = ALIGN_DOWN(size - diff, sizeof(hash));

	for (i = 0; i < size / sizeof(hash); i++) {
		/* Rotate by odd number of bits and XOR. */
		hash = (hash << ((sizeof(hash) * 8) - 7)) | (hash >> 7);
		hash ^= ptr[i];
	}

	return hash;
}

static inline __init unsigned long get_random_boot(void)
{
	unsigned long hash = 0;
	unsigned long entropy = random_get_entropy();

	/* Attempt to create a simple but unpredictable starting entropy. */
	hash = rotate_xor(hash, linux_banner, strlen(linux_banner));

	/* Add in any runtime entropy we can get */
	hash = rotate_xor(hash, &entropy, sizeof(entropy));

	return hash;
}

static inline __init bool kaslr_disabled(void)
{
	char *str;
	const char *builtin_cmdline = CONFIG_CMDLINE;

	str = strstr(builtin_cmdline, "nokaslr");
	if (str == builtin_cmdline || (str > builtin_cmdline && *(str - 1) == ' '))
		return true;

	str = strstr(boot_command_line, "nokaslr");
	if (str == boot_command_line || (str > boot_command_line && *(str - 1) == ' '))
		return true;

	return false;
}

/* Choose a new address for the kernel */
static inline void __init *determine_relocation_address(void)
{
	unsigned long kernel_length;
	unsigned long random_offset;
	void *destination = _text;

	if (kaslr_disabled())
		return destination;

	kernel_length = (long)_end - (long)_text;

	random_offset = get_random_boot() << 16;
	random_offset &= (CONFIG_RANDOMIZE_BASE_MAX_OFFSET - 1);
	if (random_offset < kernel_length)
		random_offset += ALIGN(kernel_length, 0xffff);

	return RELOCATED_KASLR(destination);
}

static inline int __init relocation_addr_valid(void *location_new)
{
	if ((unsigned long)location_new & 0x00000ffff)
		return 0; /* Inappropriately aligned new location */

	if ((unsigned long)location_new < (unsigned long)_end)
		return 0; /* New location overlaps original kernel */

	return 1;
}
#endif

static inline void __init update_reloc_offset(unsigned long *addr, long random_offset)
{
	unsigned long *new_addr = (unsigned long *)RELOCATED_KASLR(addr);

	*new_addr = (unsigned long)reloc_offset;
}

unsigned long __init relocate_kernel(void)
{
	unsigned long kernel_length;
	unsigned long random_offset = 0;
	void *location_new = _text; /* Default to original kernel start */
	char *cmdline = early_ioremap(fw_arg1, COMMAND_LINE_SIZE); /* Boot command line is passed in fw_arg1 */

	strscpy(boot_command_line, cmdline, COMMAND_LINE_SIZE);

#ifdef CONFIG_RANDOMIZE_BASE
	location_new = determine_relocation_address();

	/* Sanity check relocation address */
	if (relocation_addr_valid(location_new))
		random_offset = (unsigned long)location_new - (unsigned long)(_text);
#endif
	reloc_offset = (unsigned long)_text - VMLINUX_LOAD_ADDRESS;

	if (random_offset) {
		kernel_length = (long)(_end) - (long)(_text);

		/* Copy the kernel to it's new location */
		memcpy(location_new, _text, kernel_length);

		/* Sync the caches ready for execution of new kernel */
		__asm__ __volatile__ (
			"ibar 0 \t\n"
			"dbar 0 \t\n"
			::: "memory");

		reloc_offset += random_offset;

		/* The current thread is now within the relocated kernel */
		__current_thread_info = RELOCATED_KASLR(__current_thread_info);

		update_reloc_offset(&reloc_offset, random_offset);
	}

	if (reloc_offset)
		relocate_relative();

	relocate_absolute(random_offset);

	return random_offset;
}

/*
 * Show relocation information on panic.
 */
static void show_kernel_relocation(const char *level)
{
	if (reloc_offset > 0) {
		printk(level);
		pr_cont("Kernel relocated by 0x%lx\n", reloc_offset);
		pr_cont(" .text @ 0x%px\n", _text);
		pr_cont(" .data @ 0x%px\n", _sdata);
		pr_cont(" .bss  @ 0x%px\n", __bss_start);
	}
}

static int kernel_location_notifier_fn(struct notifier_block *self,
				       unsigned long v, void *p)
{
	show_kernel_relocation(KERN_EMERG);
	return NOTIFY_DONE;
}

static struct notifier_block kernel_location_notifier = {
	.notifier_call = kernel_location_notifier_fn
};

static int __init register_kernel_offset_dumper(void)
{
	atomic_notifier_chain_register(&panic_notifier_list,
				       &kernel_location_notifier);
	return 0;
}

arch_initcall(register_kernel_offset_dumper);
