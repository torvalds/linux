// SPDX-License-Identifier: GPL-2.0
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/memblock.h>
#include <linux/mem_encrypt.h>
#include <linux/pgtable.h>

#include <asm/set_memory.h>
#include <asm/realmode.h>
#include <asm/tlbflush.h>
#include <asm/crash.h>
#include <asm/sev-es.h>

struct real_mode_header *real_mode_header;
u32 *trampoline_cr4_features;

/* Hold the pgd entry used on booting additional CPUs */
pgd_t trampoline_pgd_entry;

void __init reserve_real_mode(void)
{
	phys_addr_t mem;
	size_t size = real_mode_size_needed();

	if (!size)
		return;

	WARN_ON(slab_is_available());

	/* Has to be under 1M so we can execute real-mode AP code. */
	mem = memblock_find_in_range(0, 1<<20, size, PAGE_SIZE);
	if (!mem) {
		pr_info("No sub-1M memory is available for the trampoline\n");
		return;
	}

	memblock_reserve(mem, size);
	set_real_mode_mem(mem);
	crash_reserve_low_1M();
}

static void sme_sev_setup_real_mode(struct trampoline_header *th)
{
#ifdef CONFIG_AMD_MEM_ENCRYPT
	if (sme_active())
		th->flags |= TH_FLAGS_SME_ACTIVE;

	if (sev_es_active()) {
		/*
		 * Skip the call to verify_cpu() in secondary_startup_64 as it
		 * will cause #VC exceptions when the AP can't handle them yet.
		 */
		th->start = (u64) secondary_startup_64_no_verify;

		if (sev_es_setup_ap_jump_table(real_mode_header))
			panic("Failed to get/update SEV-ES AP Jump Table");
	}
#endif
}

static void __init setup_real_mode(void)
{
	u16 real_mode_seg;
	const u32 *rel;
	u32 count;
	unsigned char *base;
	unsigned long phys_base;
	struct trampoline_header *trampoline_header;
	size_t size = PAGE_ALIGN(real_mode_blob_end - real_mode_blob);
#ifdef CONFIG_X86_64
	u64 *trampoline_pgd;
	u64 efer;
#endif

	base = (unsigned char *)real_mode_header;

	/*
	 * If SME is active, the trampoline area will need to be in
	 * decrypted memory in order to bring up other processors
	 * successfully. This is not needed for SEV.
	 */
	if (sme_active())
		set_memory_decrypted((unsigned long)base, size >> PAGE_SHIFT);

	memcpy(base, real_mode_blob, size);

	phys_base = __pa(base);
	real_mode_seg = phys_base >> 4;

	rel = (u32 *) real_mode_relocs;

	/* 16-bit segment relocations. */
	count = *rel++;
	while (count--) {
		u16 *seg = (u16 *) (base + *rel++);
		*seg = real_mode_seg;
	}

	/* 32-bit linear relocations. */
	count = *rel++;
	while (count--) {
		u32 *ptr = (u32 *) (base + *rel++);
		*ptr += phys_base;
	}

	/* Must be perfomed *after* relocation. */
	trampoline_header = (struct trampoline_header *)
		__va(real_mode_header->trampoline_header);

#ifdef CONFIG_X86_32
	trampoline_header->start = __pa_symbol(startup_32_smp);
	trampoline_header->gdt_limit = __BOOT_DS + 7;
	trampoline_header->gdt_base = __pa_symbol(boot_gdt);
#else
	/*
	 * Some AMD processors will #GP(0) if EFER.LMA is set in WRMSR
	 * so we need to mask it out.
	 */
	rdmsrl(MSR_EFER, efer);
	trampoline_header->efer = efer & ~EFER_LMA;

	trampoline_header->start = (u64) secondary_startup_64;
	trampoline_cr4_features = &trampoline_header->cr4;
	*trampoline_cr4_features = mmu_cr4_features;

	trampoline_header->flags = 0;

	trampoline_pgd = (u64 *) __va(real_mode_header->trampoline_pgd);
	trampoline_pgd[0] = trampoline_pgd_entry.pgd;
	trampoline_pgd[511] = init_top_pgt[511].pgd;
#endif

	sme_sev_setup_real_mode(trampoline_header);
}

/*
 * reserve_real_mode() gets called very early, to guarantee the
 * availability of low memory. This is before the proper kernel page
 * tables are set up, so we cannot set page permissions in that
 * function. Also trampoline code will be executed by APs so we
 * need to mark it executable at do_pre_smp_initcalls() at least,
 * thus run it as a early_initcall().
 */
static void __init set_real_mode_permissions(void)
{
	unsigned char *base = (unsigned char *) real_mode_header;
	size_t size = PAGE_ALIGN(real_mode_blob_end - real_mode_blob);

	size_t ro_size =
		PAGE_ALIGN(real_mode_header->ro_end) -
		__pa(base);

	size_t text_size =
		PAGE_ALIGN(real_mode_header->ro_end) -
		real_mode_header->text_start;

	unsigned long text_start =
		(unsigned long) __va(real_mode_header->text_start);

	set_memory_nx((unsigned long) base, size >> PAGE_SHIFT);
	set_memory_ro((unsigned long) base, ro_size >> PAGE_SHIFT);
	set_memory_x((unsigned long) text_start, text_size >> PAGE_SHIFT);
}

static int __init init_real_mode(void)
{
	if (!real_mode_header)
		panic("Real mode trampoline was not allocated");

	setup_real_mode();
	set_real_mode_permissions();

	return 0;
}
early_initcall(init_real_mode);
