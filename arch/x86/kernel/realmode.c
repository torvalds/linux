#include <linux/io.h>
#include <linux/memblock.h>

#include <asm/cacheflush.h>
#include <asm/pgtable.h>
#include <asm/realmode.h>

struct real_mode_header *real_mode_header;

void __init setup_real_mode(void)
{
	phys_addr_t mem;
	u16 real_mode_seg;
	u32 *rel;
	u32 count;
	u32 *ptr;
	u16 *seg;
	int i;
	unsigned char *base;

	size_t size = PAGE_ALIGN(real_mode_blob_end - real_mode_blob);

	/* Has to be in very low memory so we can execute real-mode AP code. */
	mem = memblock_find_in_range(0, 1<<20, size, PAGE_SIZE);
	if (!mem)
		panic("Cannot allocate trampoline\n");

	base = __va(mem);
	memblock_reserve(mem, size);
	real_mode_header = (struct real_mode_header *) base;

	printk(KERN_DEBUG "Base memory trampoline at [%p] %llx size %zu\n",
	       base, (unsigned long long)mem, size);

	memcpy(base, real_mode_blob, size);

	real_mode_seg = __pa(base) >> 4;
	rel = (u32 *) real_mode_relocs;

	/* 16-bit segment relocations. */
	count = rel[0];
	rel = &rel[1];
	for (i = 0; i < count; i++) {
		seg = (u16 *) (base + rel[i]);
		*seg = real_mode_seg;
	}

	/* 32-bit linear relocations. */
	count = rel[i];
	rel =  &rel[i + 1];
	for (i = 0; i < count; i++) {
		ptr = (u32 *) (base + rel[i]);
		*ptr += __pa(base);
	}

#ifdef CONFIG_X86_32
	*((u32 *)__va(real_mode_header->startup_32_smp)) = __pa(startup_32_smp);
	*((u32 *)__va(real_mode_header->boot_gdt)) = __pa(boot_gdt);
#else
	*((u64 *) __va(real_mode_header->startup_64_smp)) =
		(u64)secondary_startup_64;

	*((u64 *) __va(real_mode_header->level3_ident_pgt)) =
		__pa(level3_ident_pgt) + _KERNPG_TABLE;

	*((u64 *) __va(real_mode_header->level3_kernel_pgt)) =
		__pa(level3_kernel_pgt) + _KERNPG_TABLE;
#endif
}

/*
 * set_real_mode_permissions() gets called very early, to guarantee the
 * availability of low memory.  This is before the proper kernel page
 * tables are set up, so we cannot set page permissions in that
 * function.  Thus, we use an arch_initcall instead.
 */
static int __init set_real_mode_permissions(void)
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

	return 0;
}

arch_initcall(set_real_mode_permissions);
