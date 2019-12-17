// SPDX-License-Identifier: GPL-2.0
#include <linux/string.h>
#include <linux/elf.h>
#include <asm/sections.h>
#include <asm/setup.h>
#include <asm/kexec.h>
#include <asm/sclp.h>
#include <asm/diag.h>
#include <asm/uv.h>
#include "compressed/decompressor.h"
#include "boot.h"

extern char __boot_data_start[], __boot_data_end[];
extern char __boot_data_preserved_start[], __boot_data_preserved_end[];
unsigned long __bootdata_preserved(__kaslr_offset);

/*
 * Some code and data needs to stay below 2 GB, even when the kernel would be
 * relocated above 2 GB, because it has to use 31 bit addresses.
 * Such code and data is part of the .dma section, and its location is passed
 * over to the decompressed / relocated kernel via the .boot.preserved.data
 * section.
 */
extern char _sdma[], _edma[];
extern char _stext_dma[], _etext_dma[];
extern struct exception_table_entry _start_dma_ex_table[];
extern struct exception_table_entry _stop_dma_ex_table[];
unsigned long __bootdata_preserved(__sdma) = __pa(&_sdma);
unsigned long __bootdata_preserved(__edma) = __pa(&_edma);
unsigned long __bootdata_preserved(__stext_dma) = __pa(&_stext_dma);
unsigned long __bootdata_preserved(__etext_dma) = __pa(&_etext_dma);
struct exception_table_entry *
	__bootdata_preserved(__start_dma_ex_table) = _start_dma_ex_table;
struct exception_table_entry *
	__bootdata_preserved(__stop_dma_ex_table) = _stop_dma_ex_table;

int _diag210_dma(struct diag210 *addr);
int _diag26c_dma(void *req, void *resp, enum diag26c_sc subcode);
int _diag14_dma(unsigned long rx, unsigned long ry1, unsigned long subcode);
void _diag0c_dma(struct hypfs_diag0c_entry *entry);
void _diag308_reset_dma(void);
struct diag_ops __bootdata_preserved(diag_dma_ops) = {
	.diag210 = _diag210_dma,
	.diag26c = _diag26c_dma,
	.diag14 = _diag14_dma,
	.diag0c = _diag0c_dma,
	.diag308_reset = _diag308_reset_dma
};
static struct diag210 _diag210_tmp_dma __section(.dma.data);
struct diag210 *__bootdata_preserved(__diag210_tmp_dma) = &_diag210_tmp_dma;
void _swsusp_reset_dma(void);
unsigned long __bootdata_preserved(__swsusp_reset_dma) = __pa(_swsusp_reset_dma);

void error(char *x)
{
	sclp_early_printk("\n\n");
	sclp_early_printk(x);
	sclp_early_printk("\n\n -- System halted");

	disabled_wait();
}

#ifdef CONFIG_KERNEL_UNCOMPRESSED
unsigned long mem_safe_offset(void)
{
	return vmlinux.default_lma + vmlinux.image_size + vmlinux.bss_size;
}
#endif

static void rescue_initrd(unsigned long addr)
{
	if (!IS_ENABLED(CONFIG_BLK_DEV_INITRD))
		return;
	if (!INITRD_START || !INITRD_SIZE)
		return;
	if (addr <= INITRD_START)
		return;
	memmove((void *)addr, (void *)INITRD_START, INITRD_SIZE);
	INITRD_START = addr;
}

static void copy_bootdata(void)
{
	if (__boot_data_end - __boot_data_start != vmlinux.bootdata_size)
		error(".boot.data section size mismatch");
	memcpy((void *)vmlinux.bootdata_off, __boot_data_start, vmlinux.bootdata_size);
	if (__boot_data_preserved_end - __boot_data_preserved_start != vmlinux.bootdata_preserved_size)
		error(".boot.preserved.data section size mismatch");
	memcpy((void *)vmlinux.bootdata_preserved_off, __boot_data_preserved_start, vmlinux.bootdata_preserved_size);
}

static void handle_relocs(unsigned long offset)
{
	Elf64_Rela *rela_start, *rela_end, *rela;
	int r_type, r_sym, rc;
	Elf64_Addr loc, val;
	Elf64_Sym *dynsym;

	rela_start = (Elf64_Rela *) vmlinux.rela_dyn_start;
	rela_end = (Elf64_Rela *) vmlinux.rela_dyn_end;
	dynsym = (Elf64_Sym *) vmlinux.dynsym_start;
	for (rela = rela_start; rela < rela_end; rela++) {
		loc = rela->r_offset + offset;
		val = rela->r_addend;
		r_sym = ELF64_R_SYM(rela->r_info);
		if (r_sym) {
			if (dynsym[r_sym].st_shndx != SHN_UNDEF)
				val += dynsym[r_sym].st_value + offset;
		} else {
			/*
			 * 0 == undefined symbol table index (STN_UNDEF),
			 * used for R_390_RELATIVE, only add KASLR offset
			 */
			val += offset;
		}
		r_type = ELF64_R_TYPE(rela->r_info);
		rc = arch_kexec_do_relocs(r_type, (void *) loc, val, 0);
		if (rc)
			error("Unknown relocation type");
	}
}

static void clear_bss_section(void)
{
	memset((void *)vmlinux.default_lma + vmlinux.image_size, 0, vmlinux.bss_size);
}

void startup_kernel(void)
{
	unsigned long random_lma;
	unsigned long safe_addr;
	void *img;

	store_ipl_parmblock();
	safe_addr = mem_safe_offset();
	safe_addr = read_ipl_report(safe_addr);
	uv_query_info();
	rescue_initrd(safe_addr);
	sclp_early_read_info();
	setup_boot_command_line();
	parse_boot_command_line();
	setup_memory_end();
	detect_memory();

	random_lma = __kaslr_offset = 0;
	if (IS_ENABLED(CONFIG_RANDOMIZE_BASE) && kaslr_enabled) {
		random_lma = get_random_base(safe_addr);
		if (random_lma) {
			__kaslr_offset = random_lma - vmlinux.default_lma;
			img = (void *)vmlinux.default_lma;
			vmlinux.default_lma += __kaslr_offset;
			vmlinux.entry += __kaslr_offset;
			vmlinux.bootdata_off += __kaslr_offset;
			vmlinux.bootdata_preserved_off += __kaslr_offset;
			vmlinux.rela_dyn_start += __kaslr_offset;
			vmlinux.rela_dyn_end += __kaslr_offset;
			vmlinux.dynsym_start += __kaslr_offset;
		}
	}

	if (!IS_ENABLED(CONFIG_KERNEL_UNCOMPRESSED)) {
		img = decompress_kernel();
		memmove((void *)vmlinux.default_lma, img, vmlinux.image_size);
	} else if (__kaslr_offset)
		memcpy((void *)vmlinux.default_lma, img, vmlinux.image_size);

	clear_bss_section();
	copy_bootdata();
	if (IS_ENABLED(CONFIG_RELOCATABLE))
		handle_relocs(__kaslr_offset);

	if (__kaslr_offset) {
		/*
		 * Save KASLR offset for early dumps, before vmcore_info is set.
		 * Mark as uneven to distinguish from real vmcore_info pointer.
		 */
		S390_lowcore.vmcore_info = __kaslr_offset | 0x1UL;
		/* Clear non-relocated kernel */
		if (IS_ENABLED(CONFIG_KERNEL_UNCOMPRESSED))
			memset(img, 0, vmlinux.image_size);
	}
	vmlinux.entry();
}
