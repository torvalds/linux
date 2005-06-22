/*
 * Extensible Firmware Interface
 *
 * Based on Extensible Firmware Interface Specification version 0.9 April 30, 1999
 *
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999-2003 Hewlett-Packard Co.
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Stephane Eranian <eranian@hpl.hp.com>
 *
 * All EFI Runtime Services are not implemented yet as EFI only
 * supports physical mode addressing on SoftSDV. This is to be fixed
 * in a future version.  --drummond 1999-07-20
 *
 * Implemented EFI runtime services and virtual mode calls.  --davidm
 *
 * Goutham Rao: <goutham.rao@intel.com>
 *	Skip non-WB memory and ignore empty memory ranges.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/time.h>
#include <linux/efi.h>

#include <asm/io.h>
#include <asm/kregs.h>
#include <asm/meminit.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/mca.h>

#define EFI_DEBUG	0

extern efi_status_t efi_call_phys (void *, ...);

struct efi efi;
EXPORT_SYMBOL(efi);
static efi_runtime_services_t *runtime;
static unsigned long mem_limit = ~0UL, max_addr = ~0UL;

#define efi_call_virt(f, args...)	(*(f))(args)

#define STUB_GET_TIME(prefix, adjust_arg)							  \
static efi_status_t										  \
prefix##_get_time (efi_time_t *tm, efi_time_cap_t *tc)						  \
{												  \
	struct ia64_fpreg fr[6];								  \
	efi_time_cap_t *atc = NULL;								  \
	efi_status_t ret;									  \
												  \
	if (tc)											  \
		atc = adjust_arg(tc);								  \
	ia64_save_scratch_fpregs(fr);								  \
	ret = efi_call_##prefix((efi_get_time_t *) __va(runtime->get_time), adjust_arg(tm), atc); \
	ia64_load_scratch_fpregs(fr);								  \
	return ret;										  \
}

#define STUB_SET_TIME(prefix, adjust_arg)							\
static efi_status_t										\
prefix##_set_time (efi_time_t *tm)								\
{												\
	struct ia64_fpreg fr[6];								\
	efi_status_t ret;									\
												\
	ia64_save_scratch_fpregs(fr);								\
	ret = efi_call_##prefix((efi_set_time_t *) __va(runtime->set_time), adjust_arg(tm));	\
	ia64_load_scratch_fpregs(fr);								\
	return ret;										\
}

#define STUB_GET_WAKEUP_TIME(prefix, adjust_arg)						\
static efi_status_t										\
prefix##_get_wakeup_time (efi_bool_t *enabled, efi_bool_t *pending, efi_time_t *tm)		\
{												\
	struct ia64_fpreg fr[6];								\
	efi_status_t ret;									\
												\
	ia64_save_scratch_fpregs(fr);								\
	ret = efi_call_##prefix((efi_get_wakeup_time_t *) __va(runtime->get_wakeup_time),	\
				adjust_arg(enabled), adjust_arg(pending), adjust_arg(tm));	\
	ia64_load_scratch_fpregs(fr);								\
	return ret;										\
}

#define STUB_SET_WAKEUP_TIME(prefix, adjust_arg)						\
static efi_status_t										\
prefix##_set_wakeup_time (efi_bool_t enabled, efi_time_t *tm)					\
{												\
	struct ia64_fpreg fr[6];								\
	efi_time_t *atm = NULL;									\
	efi_status_t ret;									\
												\
	if (tm)											\
		atm = adjust_arg(tm);								\
	ia64_save_scratch_fpregs(fr);								\
	ret = efi_call_##prefix((efi_set_wakeup_time_t *) __va(runtime->set_wakeup_time),	\
				enabled, atm);							\
	ia64_load_scratch_fpregs(fr);								\
	return ret;										\
}

#define STUB_GET_VARIABLE(prefix, adjust_arg)						\
static efi_status_t									\
prefix##_get_variable (efi_char16_t *name, efi_guid_t *vendor, u32 *attr,		\
		       unsigned long *data_size, void *data)				\
{											\
	struct ia64_fpreg fr[6];							\
	u32 *aattr = NULL;									\
	efi_status_t ret;								\
											\
	if (attr)									\
		aattr = adjust_arg(attr);						\
	ia64_save_scratch_fpregs(fr);							\
	ret = efi_call_##prefix((efi_get_variable_t *) __va(runtime->get_variable),	\
				adjust_arg(name), adjust_arg(vendor), aattr,		\
				adjust_arg(data_size), adjust_arg(data));		\
	ia64_load_scratch_fpregs(fr);							\
	return ret;									\
}

#define STUB_GET_NEXT_VARIABLE(prefix, adjust_arg)						\
static efi_status_t										\
prefix##_get_next_variable (unsigned long *name_size, efi_char16_t *name, efi_guid_t *vendor)	\
{												\
	struct ia64_fpreg fr[6];								\
	efi_status_t ret;									\
												\
	ia64_save_scratch_fpregs(fr);								\
	ret = efi_call_##prefix((efi_get_next_variable_t *) __va(runtime->get_next_variable),	\
				adjust_arg(name_size), adjust_arg(name), adjust_arg(vendor));	\
	ia64_load_scratch_fpregs(fr);								\
	return ret;										\
}

#define STUB_SET_VARIABLE(prefix, adjust_arg)						\
static efi_status_t									\
prefix##_set_variable (efi_char16_t *name, efi_guid_t *vendor, unsigned long attr,	\
		       unsigned long data_size, void *data)				\
{											\
	struct ia64_fpreg fr[6];							\
	efi_status_t ret;								\
											\
	ia64_save_scratch_fpregs(fr);							\
	ret = efi_call_##prefix((efi_set_variable_t *) __va(runtime->set_variable),	\
				adjust_arg(name), adjust_arg(vendor), attr, data_size,	\
				adjust_arg(data));					\
	ia64_load_scratch_fpregs(fr);							\
	return ret;									\
}

#define STUB_GET_NEXT_HIGH_MONO_COUNT(prefix, adjust_arg)					\
static efi_status_t										\
prefix##_get_next_high_mono_count (u32 *count)							\
{												\
	struct ia64_fpreg fr[6];								\
	efi_status_t ret;									\
												\
	ia64_save_scratch_fpregs(fr);								\
	ret = efi_call_##prefix((efi_get_next_high_mono_count_t *)				\
				__va(runtime->get_next_high_mono_count), adjust_arg(count));	\
	ia64_load_scratch_fpregs(fr);								\
	return ret;										\
}

#define STUB_RESET_SYSTEM(prefix, adjust_arg)					\
static void									\
prefix##_reset_system (int reset_type, efi_status_t status,			\
		       unsigned long data_size, efi_char16_t *data)		\
{										\
	struct ia64_fpreg fr[6];						\
	efi_char16_t *adata = NULL;						\
										\
	if (data)								\
		adata = adjust_arg(data);					\
										\
	ia64_save_scratch_fpregs(fr);						\
	efi_call_##prefix((efi_reset_system_t *) __va(runtime->reset_system),	\
			  reset_type, status, data_size, adata);		\
	/* should not return, but just in case... */				\
	ia64_load_scratch_fpregs(fr);						\
}

#define phys_ptr(arg)	((__typeof__(arg)) ia64_tpa(arg))

STUB_GET_TIME(phys, phys_ptr)
STUB_SET_TIME(phys, phys_ptr)
STUB_GET_WAKEUP_TIME(phys, phys_ptr)
STUB_SET_WAKEUP_TIME(phys, phys_ptr)
STUB_GET_VARIABLE(phys, phys_ptr)
STUB_GET_NEXT_VARIABLE(phys, phys_ptr)
STUB_SET_VARIABLE(phys, phys_ptr)
STUB_GET_NEXT_HIGH_MONO_COUNT(phys, phys_ptr)
STUB_RESET_SYSTEM(phys, phys_ptr)

#define id(arg)	arg

STUB_GET_TIME(virt, id)
STUB_SET_TIME(virt, id)
STUB_GET_WAKEUP_TIME(virt, id)
STUB_SET_WAKEUP_TIME(virt, id)
STUB_GET_VARIABLE(virt, id)
STUB_GET_NEXT_VARIABLE(virt, id)
STUB_SET_VARIABLE(virt, id)
STUB_GET_NEXT_HIGH_MONO_COUNT(virt, id)
STUB_RESET_SYSTEM(virt, id)

void
efi_gettimeofday (struct timespec *ts)
{
	efi_time_t tm;

	memset(ts, 0, sizeof(ts));
	if ((*efi.get_time)(&tm, NULL) != EFI_SUCCESS)
		return;

	ts->tv_sec = mktime(tm.year, tm.month, tm.day, tm.hour, tm.minute, tm.second);
	ts->tv_nsec = tm.nanosecond;
}

static int
is_available_memory (efi_memory_desc_t *md)
{
	if (!(md->attribute & EFI_MEMORY_WB))
		return 0;

	switch (md->type) {
	      case EFI_LOADER_CODE:
	      case EFI_LOADER_DATA:
	      case EFI_BOOT_SERVICES_CODE:
	      case EFI_BOOT_SERVICES_DATA:
	      case EFI_CONVENTIONAL_MEMORY:
		return 1;
	}
	return 0;
}

/*
 * Trim descriptor MD so its starts at address START_ADDR.  If the descriptor covers
 * memory that is normally available to the kernel, issue a warning that some memory
 * is being ignored.
 */
static void
trim_bottom (efi_memory_desc_t *md, u64 start_addr)
{
	u64 num_skipped_pages;

	if (md->phys_addr >= start_addr || !md->num_pages)
		return;

	num_skipped_pages = (start_addr - md->phys_addr) >> EFI_PAGE_SHIFT;
	if (num_skipped_pages > md->num_pages)
		num_skipped_pages = md->num_pages;

	if (is_available_memory(md))
		printk(KERN_NOTICE "efi.%s: ignoring %luKB of memory at 0x%lx due to granule hole "
		       "at 0x%lx\n", __FUNCTION__,
		       (num_skipped_pages << EFI_PAGE_SHIFT) >> 10,
		       md->phys_addr, start_addr - IA64_GRANULE_SIZE);
	/*
	 * NOTE: Don't set md->phys_addr to START_ADDR because that could cause the memory
	 * descriptor list to become unsorted.  In such a case, md->num_pages will be
	 * zero, so the Right Thing will happen.
	 */
	md->phys_addr += num_skipped_pages << EFI_PAGE_SHIFT;
	md->num_pages -= num_skipped_pages;
}

static void
trim_top (efi_memory_desc_t *md, u64 end_addr)
{
	u64 num_dropped_pages, md_end_addr;

	md_end_addr = md->phys_addr + (md->num_pages << EFI_PAGE_SHIFT);

	if (md_end_addr <= end_addr || !md->num_pages)
		return;

	num_dropped_pages = (md_end_addr - end_addr) >> EFI_PAGE_SHIFT;
	if (num_dropped_pages > md->num_pages)
		num_dropped_pages = md->num_pages;

	if (is_available_memory(md))
		printk(KERN_NOTICE "efi.%s: ignoring %luKB of memory at 0x%lx due to granule hole "
		       "at 0x%lx\n", __FUNCTION__,
		       (num_dropped_pages << EFI_PAGE_SHIFT) >> 10,
		       md->phys_addr, end_addr);
	md->num_pages -= num_dropped_pages;
}

/*
 * Walks the EFI memory map and calls CALLBACK once for each EFI memory descriptor that
 * has memory that is available for OS use.
 */
void
efi_memmap_walk (efi_freemem_callback_t callback, void *arg)
{
	int prev_valid = 0;
	struct range {
		u64 start;
		u64 end;
	} prev, curr;
	void *efi_map_start, *efi_map_end, *p, *q;
	efi_memory_desc_t *md, *check_md;
	u64 efi_desc_size, start, end, granule_addr, last_granule_addr, first_non_wb_addr = 0;
	unsigned long total_mem = 0;

	efi_map_start = __va(ia64_boot_param->efi_memmap);
	efi_map_end   = efi_map_start + ia64_boot_param->efi_memmap_size;
	efi_desc_size = ia64_boot_param->efi_memdesc_size;

	for (p = efi_map_start; p < efi_map_end; p += efi_desc_size) {
		md = p;

		/* skip over non-WB memory descriptors; that's all we're interested in... */
		if (!(md->attribute & EFI_MEMORY_WB))
			continue;

		/*
		 * granule_addr is the base of md's first granule.
		 * [granule_addr - first_non_wb_addr) is guaranteed to
		 * be contiguous WB memory.
		 */
		granule_addr = GRANULEROUNDDOWN(md->phys_addr);
		first_non_wb_addr = max(first_non_wb_addr, granule_addr);

		if (first_non_wb_addr < md->phys_addr) {
			trim_bottom(md, granule_addr + IA64_GRANULE_SIZE);
			granule_addr = GRANULEROUNDDOWN(md->phys_addr);
			first_non_wb_addr = max(first_non_wb_addr, granule_addr);
		}

		for (q = p; q < efi_map_end; q += efi_desc_size) {
			check_md = q;

			if ((check_md->attribute & EFI_MEMORY_WB) &&
			    (check_md->phys_addr == first_non_wb_addr))
				first_non_wb_addr += check_md->num_pages << EFI_PAGE_SHIFT;
			else
				break;		/* non-WB or hole */
		}

		last_granule_addr = GRANULEROUNDDOWN(first_non_wb_addr);
		if (last_granule_addr < md->phys_addr + (md->num_pages << EFI_PAGE_SHIFT))
			trim_top(md, last_granule_addr);

		if (is_available_memory(md)) {
			if (md->phys_addr + (md->num_pages << EFI_PAGE_SHIFT) >= max_addr) {
				if (md->phys_addr >= max_addr)
					continue;
				md->num_pages = (max_addr - md->phys_addr) >> EFI_PAGE_SHIFT;
				first_non_wb_addr = max_addr;
			}

			if (total_mem >= mem_limit)
				continue;

			if (total_mem + (md->num_pages << EFI_PAGE_SHIFT) > mem_limit) {
				unsigned long limit_addr = md->phys_addr;

				limit_addr += mem_limit - total_mem;
				limit_addr = GRANULEROUNDDOWN(limit_addr);

				if (md->phys_addr > limit_addr)
					continue;

				md->num_pages = (limit_addr - md->phys_addr) >>
				                EFI_PAGE_SHIFT;
				first_non_wb_addr = max_addr = md->phys_addr +
				              (md->num_pages << EFI_PAGE_SHIFT);
			}
			total_mem += (md->num_pages << EFI_PAGE_SHIFT);

			if (md->num_pages == 0)
				continue;

			curr.start = PAGE_OFFSET + md->phys_addr;
			curr.end   = curr.start + (md->num_pages << EFI_PAGE_SHIFT);

			if (!prev_valid) {
				prev = curr;
				prev_valid = 1;
			} else {
				if (curr.start < prev.start)
					printk(KERN_ERR "Oops: EFI memory table not ordered!\n");

				if (prev.end == curr.start) {
					/* merge two consecutive memory ranges */
					prev.end = curr.end;
				} else {
					start = PAGE_ALIGN(prev.start);
					end = prev.end & PAGE_MASK;
					if ((end > start) && (*callback)(start, end, arg) < 0)
						return;
					prev = curr;
				}
			}
		}
	}
	if (prev_valid) {
		start = PAGE_ALIGN(prev.start);
		end = prev.end & PAGE_MASK;
		if (end > start)
			(*callback)(start, end, arg);
	}
}

/*
 * Walk the EFI memory map to pull out leftover pages in the lower
 * memory regions which do not end up in the regular memory map and
 * stick them into the uncached allocator
 *
 * The regular walk function is significantly more complex than the
 * uncached walk which means it really doesn't make sense to try and
 * marge the two.
 */
void __init
efi_memmap_walk_uc (efi_freemem_callback_t callback)
{
	void *efi_map_start, *efi_map_end, *p;
	efi_memory_desc_t *md;
	u64 efi_desc_size, start, end;

	efi_map_start = __va(ia64_boot_param->efi_memmap);
	efi_map_end = efi_map_start + ia64_boot_param->efi_memmap_size;
	efi_desc_size = ia64_boot_param->efi_memdesc_size;

	for (p = efi_map_start; p < efi_map_end; p += efi_desc_size) {
		md = p;
		if (md->attribute == EFI_MEMORY_UC) {
			start = PAGE_ALIGN(md->phys_addr);
			end = PAGE_ALIGN((md->phys_addr+(md->num_pages << EFI_PAGE_SHIFT)) & PAGE_MASK);
			if ((*callback)(start, end, NULL) < 0)
				return;
		}
	}
}


/*
 * Look for the PAL_CODE region reported by EFI and maps it using an
 * ITR to enable safe PAL calls in virtual mode.  See IA-64 Processor
 * Abstraction Layer chapter 11 in ADAG
 */

void *
efi_get_pal_addr (void)
{
	void *efi_map_start, *efi_map_end, *p;
	efi_memory_desc_t *md;
	u64 efi_desc_size;
	int pal_code_count = 0;
	u64 vaddr, mask;

	efi_map_start = __va(ia64_boot_param->efi_memmap);
	efi_map_end   = efi_map_start + ia64_boot_param->efi_memmap_size;
	efi_desc_size = ia64_boot_param->efi_memdesc_size;

	for (p = efi_map_start; p < efi_map_end; p += efi_desc_size) {
		md = p;
		if (md->type != EFI_PAL_CODE)
			continue;

		if (++pal_code_count > 1) {
			printk(KERN_ERR "Too many EFI Pal Code memory ranges, dropped @ %lx\n",
			       md->phys_addr);
			continue;
		}
		/*
		 * The only ITLB entry in region 7 that is used is the one installed by
		 * __start().  That entry covers a 64MB range.
		 */
		mask  = ~((1 << KERNEL_TR_PAGE_SHIFT) - 1);
		vaddr = PAGE_OFFSET + md->phys_addr;

		/*
		 * We must check that the PAL mapping won't overlap with the kernel
		 * mapping.
		 *
		 * PAL code is guaranteed to be aligned on a power of 2 between 4k and
		 * 256KB and that only one ITR is needed to map it. This implies that the
		 * PAL code is always aligned on its size, i.e., the closest matching page
		 * size supported by the TLB. Therefore PAL code is guaranteed never to
		 * cross a 64MB unless it is bigger than 64MB (very unlikely!).  So for
		 * now the following test is enough to determine whether or not we need a
		 * dedicated ITR for the PAL code.
		 */
		if ((vaddr & mask) == (KERNEL_START & mask)) {
			printk(KERN_INFO "%s: no need to install ITR for PAL code\n",
			       __FUNCTION__);
			continue;
		}

		if (md->num_pages << EFI_PAGE_SHIFT > IA64_GRANULE_SIZE)
			panic("Woah!  PAL code size bigger than a granule!");

#if EFI_DEBUG
		mask  = ~((1 << IA64_GRANULE_SHIFT) - 1);

		printk(KERN_INFO "CPU %d: mapping PAL code [0x%lx-0x%lx) into [0x%lx-0x%lx)\n",
			smp_processor_id(), md->phys_addr,
			md->phys_addr + (md->num_pages << EFI_PAGE_SHIFT),
			vaddr & mask, (vaddr & mask) + IA64_GRANULE_SIZE);
#endif
		return __va(md->phys_addr);
	}
	printk(KERN_WARNING "%s: no PAL-code memory-descriptor found",
	       __FUNCTION__);
	return NULL;
}

void
efi_map_pal_code (void)
{
	void *pal_vaddr = efi_get_pal_addr ();
	u64 psr;

	if (!pal_vaddr)
		return;

	/*
	 * Cannot write to CRx with PSR.ic=1
	 */
	psr = ia64_clear_ic();
	ia64_itr(0x1, IA64_TR_PALCODE, GRANULEROUNDDOWN((unsigned long) pal_vaddr),
		 pte_val(pfn_pte(__pa(pal_vaddr) >> PAGE_SHIFT, PAGE_KERNEL)),
		 IA64_GRANULE_SHIFT);
	ia64_set_psr(psr);		/* restore psr */
	ia64_srlz_i();
}

void __init
efi_init (void)
{
	void *efi_map_start, *efi_map_end;
	efi_config_table_t *config_tables;
	efi_char16_t *c16;
	u64 efi_desc_size;
	char *cp, *end, vendor[100] = "unknown";
	extern char saved_command_line[];
	int i;

	/* it's too early to be able to use the standard kernel command line support... */
	for (cp = saved_command_line; *cp; ) {
		if (memcmp(cp, "mem=", 4) == 0) {
			cp += 4;
			mem_limit = memparse(cp, &end);
			if (end != cp)
				break;
			cp = end;
		} else if (memcmp(cp, "max_addr=", 9) == 0) {
			cp += 9;
			max_addr = GRANULEROUNDDOWN(memparse(cp, &end));
			if (end != cp)
				break;
			cp = end;
		} else {
			while (*cp != ' ' && *cp)
				++cp;
			while (*cp == ' ')
				++cp;
		}
	}
	if (max_addr != ~0UL)
		printk(KERN_INFO "Ignoring memory above %luMB\n", max_addr >> 20);

	efi.systab = __va(ia64_boot_param->efi_systab);

	/*
	 * Verify the EFI Table
	 */
	if (efi.systab == NULL)
		panic("Woah! Can't find EFI system table.\n");
	if (efi.systab->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE)
		panic("Woah! EFI system table signature incorrect\n");
	if ((efi.systab->hdr.revision ^ EFI_SYSTEM_TABLE_REVISION) >> 16 != 0)
		printk(KERN_WARNING "Warning: EFI system table major version mismatch: "
		       "got %d.%02d, expected %d.%02d\n",
		       efi.systab->hdr.revision >> 16, efi.systab->hdr.revision & 0xffff,
		       EFI_SYSTEM_TABLE_REVISION >> 16, EFI_SYSTEM_TABLE_REVISION & 0xffff);

	config_tables = __va(efi.systab->tables);

	/* Show what we know for posterity */
	c16 = __va(efi.systab->fw_vendor);
	if (c16) {
		for (i = 0;i < (int) sizeof(vendor) && *c16; ++i)
			vendor[i] = *c16++;
		vendor[i] = '\0';
	}

	printk(KERN_INFO "EFI v%u.%.02u by %s:",
	       efi.systab->hdr.revision >> 16, efi.systab->hdr.revision & 0xffff, vendor);

	for (i = 0; i < (int) efi.systab->nr_tables; i++) {
		if (efi_guidcmp(config_tables[i].guid, MPS_TABLE_GUID) == 0) {
			efi.mps = __va(config_tables[i].table);
			printk(" MPS=0x%lx", config_tables[i].table);
		} else if (efi_guidcmp(config_tables[i].guid, ACPI_20_TABLE_GUID) == 0) {
			efi.acpi20 = __va(config_tables[i].table);
			printk(" ACPI 2.0=0x%lx", config_tables[i].table);
		} else if (efi_guidcmp(config_tables[i].guid, ACPI_TABLE_GUID) == 0) {
			efi.acpi = __va(config_tables[i].table);
			printk(" ACPI=0x%lx", config_tables[i].table);
		} else if (efi_guidcmp(config_tables[i].guid, SMBIOS_TABLE_GUID) == 0) {
			efi.smbios = __va(config_tables[i].table);
			printk(" SMBIOS=0x%lx", config_tables[i].table);
		} else if (efi_guidcmp(config_tables[i].guid, SAL_SYSTEM_TABLE_GUID) == 0) {
			efi.sal_systab = __va(config_tables[i].table);
			printk(" SALsystab=0x%lx", config_tables[i].table);
		} else if (efi_guidcmp(config_tables[i].guid, HCDP_TABLE_GUID) == 0) {
			efi.hcdp = __va(config_tables[i].table);
			printk(" HCDP=0x%lx", config_tables[i].table);
		}
	}
	printk("\n");

	runtime = __va(efi.systab->runtime);
	efi.get_time = phys_get_time;
	efi.set_time = phys_set_time;
	efi.get_wakeup_time = phys_get_wakeup_time;
	efi.set_wakeup_time = phys_set_wakeup_time;
	efi.get_variable = phys_get_variable;
	efi.get_next_variable = phys_get_next_variable;
	efi.set_variable = phys_set_variable;
	efi.get_next_high_mono_count = phys_get_next_high_mono_count;
	efi.reset_system = phys_reset_system;

	efi_map_start = __va(ia64_boot_param->efi_memmap);
	efi_map_end   = efi_map_start + ia64_boot_param->efi_memmap_size;
	efi_desc_size = ia64_boot_param->efi_memdesc_size;

#if EFI_DEBUG
	/* print EFI memory map: */
	{
		efi_memory_desc_t *md;
		void *p;

		for (i = 0, p = efi_map_start; p < efi_map_end; ++i, p += efi_desc_size) {
			md = p;
			printk("mem%02u: type=%u, attr=0x%lx, range=[0x%016lx-0x%016lx) (%luMB)\n",
			       i, md->type, md->attribute, md->phys_addr,
			       md->phys_addr + (md->num_pages << EFI_PAGE_SHIFT),
			       md->num_pages >> (20 - EFI_PAGE_SHIFT));
		}
	}
#endif

	efi_map_pal_code();
	efi_enter_virtual_mode();
}

void
efi_enter_virtual_mode (void)
{
	void *efi_map_start, *efi_map_end, *p;
	efi_memory_desc_t *md;
	efi_status_t status;
	u64 efi_desc_size;

	efi_map_start = __va(ia64_boot_param->efi_memmap);
	efi_map_end   = efi_map_start + ia64_boot_param->efi_memmap_size;
	efi_desc_size = ia64_boot_param->efi_memdesc_size;

	for (p = efi_map_start; p < efi_map_end; p += efi_desc_size) {
		md = p;
		if (md->attribute & EFI_MEMORY_RUNTIME) {
			/*
			 * Some descriptors have multiple bits set, so the order of
			 * the tests is relevant.
			 */
			if (md->attribute & EFI_MEMORY_WB) {
				md->virt_addr = (u64) __va(md->phys_addr);
			} else if (md->attribute & EFI_MEMORY_UC) {
				md->virt_addr = (u64) ioremap(md->phys_addr, 0);
			} else if (md->attribute & EFI_MEMORY_WC) {
#if 0
				md->virt_addr = ia64_remap(md->phys_addr, (_PAGE_A | _PAGE_P
									   | _PAGE_D
									   | _PAGE_MA_WC
									   | _PAGE_PL_0
									   | _PAGE_AR_RW));
#else
				printk(KERN_INFO "EFI_MEMORY_WC mapping\n");
				md->virt_addr = (u64) ioremap(md->phys_addr, 0);
#endif
			} else if (md->attribute & EFI_MEMORY_WT) {
#if 0
				md->virt_addr = ia64_remap(md->phys_addr, (_PAGE_A | _PAGE_P
									   | _PAGE_D | _PAGE_MA_WT
									   | _PAGE_PL_0
									   | _PAGE_AR_RW));
#else
				printk(KERN_INFO "EFI_MEMORY_WT mapping\n");
				md->virt_addr = (u64) ioremap(md->phys_addr, 0);
#endif
			}
		}
	}

	status = efi_call_phys(__va(runtime->set_virtual_address_map),
			       ia64_boot_param->efi_memmap_size,
			       efi_desc_size, ia64_boot_param->efi_memdesc_version,
			       ia64_boot_param->efi_memmap);
	if (status != EFI_SUCCESS) {
		printk(KERN_WARNING "warning: unable to switch EFI into virtual mode "
		       "(status=%lu)\n", status);
		return;
	}

	/*
	 * Now that EFI is in virtual mode, we call the EFI functions more efficiently:
	 */
	efi.get_time = virt_get_time;
	efi.set_time = virt_set_time;
	efi.get_wakeup_time = virt_get_wakeup_time;
	efi.set_wakeup_time = virt_set_wakeup_time;
	efi.get_variable = virt_get_variable;
	efi.get_next_variable = virt_get_next_variable;
	efi.set_variable = virt_set_variable;
	efi.get_next_high_mono_count = virt_get_next_high_mono_count;
	efi.reset_system = virt_reset_system;
}

/*
 * Walk the EFI memory map looking for the I/O port range.  There can only be one entry of
 * this type, other I/O port ranges should be described via ACPI.
 */
u64
efi_get_iobase (void)
{
	void *efi_map_start, *efi_map_end, *p;
	efi_memory_desc_t *md;
	u64 efi_desc_size;

	efi_map_start = __va(ia64_boot_param->efi_memmap);
	efi_map_end   = efi_map_start + ia64_boot_param->efi_memmap_size;
	efi_desc_size = ia64_boot_param->efi_memdesc_size;

	for (p = efi_map_start; p < efi_map_end; p += efi_desc_size) {
		md = p;
		if (md->type == EFI_MEMORY_MAPPED_IO_PORT_SPACE) {
			if (md->attribute & EFI_MEMORY_UC)
				return md->phys_addr;
		}
	}
	return 0;
}

u32
efi_mem_type (unsigned long phys_addr)
{
	void *efi_map_start, *efi_map_end, *p;
	efi_memory_desc_t *md;
	u64 efi_desc_size;

	efi_map_start = __va(ia64_boot_param->efi_memmap);
	efi_map_end   = efi_map_start + ia64_boot_param->efi_memmap_size;
	efi_desc_size = ia64_boot_param->efi_memdesc_size;

	for (p = efi_map_start; p < efi_map_end; p += efi_desc_size) {
		md = p;

		if (phys_addr - md->phys_addr < (md->num_pages << EFI_PAGE_SHIFT))
			 return md->type;
	}
	return 0;
}

u64
efi_mem_attributes (unsigned long phys_addr)
{
	void *efi_map_start, *efi_map_end, *p;
	efi_memory_desc_t *md;
	u64 efi_desc_size;

	efi_map_start = __va(ia64_boot_param->efi_memmap);
	efi_map_end   = efi_map_start + ia64_boot_param->efi_memmap_size;
	efi_desc_size = ia64_boot_param->efi_memdesc_size;

	for (p = efi_map_start; p < efi_map_end; p += efi_desc_size) {
		md = p;

		if (phys_addr - md->phys_addr < (md->num_pages << EFI_PAGE_SHIFT))
			return md->attribute;
	}
	return 0;
}
EXPORT_SYMBOL(efi_mem_attributes);

int
valid_phys_addr_range (unsigned long phys_addr, unsigned long *size)
{
	void *efi_map_start, *efi_map_end, *p;
	efi_memory_desc_t *md;
	u64 efi_desc_size;

	efi_map_start = __va(ia64_boot_param->efi_memmap);
	efi_map_end   = efi_map_start + ia64_boot_param->efi_memmap_size;
	efi_desc_size = ia64_boot_param->efi_memdesc_size;

	for (p = efi_map_start; p < efi_map_end; p += efi_desc_size) {
		md = p;

		if (phys_addr - md->phys_addr < (md->num_pages << EFI_PAGE_SHIFT)) {
			if (!(md->attribute & EFI_MEMORY_WB))
				return 0;

			if (*size > md->phys_addr + (md->num_pages << EFI_PAGE_SHIFT) - phys_addr)
				*size = md->phys_addr + (md->num_pages << EFI_PAGE_SHIFT) - phys_addr;
			return 1;
		}
	}
	return 0;
}

int __init
efi_uart_console_only(void)
{
	efi_status_t status;
	char *s, name[] = "ConOut";
	efi_guid_t guid = EFI_GLOBAL_VARIABLE_GUID;
	efi_char16_t *utf16, name_utf16[32];
	unsigned char data[1024];
	unsigned long size = sizeof(data);
	struct efi_generic_dev_path *hdr, *end_addr;
	int uart = 0;

	/* Convert to UTF-16 */
	utf16 = name_utf16;
	s = name;
	while (*s)
		*utf16++ = *s++ & 0x7f;
	*utf16 = 0;

	status = efi.get_variable(name_utf16, &guid, NULL, &size, data);
	if (status != EFI_SUCCESS) {
		printk(KERN_ERR "No EFI %s variable?\n", name);
		return 0;
	}

	hdr = (struct efi_generic_dev_path *) data;
	end_addr = (struct efi_generic_dev_path *) ((u8 *) data + size);
	while (hdr < end_addr) {
		if (hdr->type == EFI_DEV_MSG &&
		    hdr->sub_type == EFI_DEV_MSG_UART)
			uart = 1;
		else if (hdr->type == EFI_DEV_END_PATH ||
			  hdr->type == EFI_DEV_END_PATH2) {
			if (!uart)
				return 0;
			if (hdr->sub_type == EFI_DEV_END_ENTIRE)
				return 1;
			uart = 0;
		}
		hdr = (struct efi_generic_dev_path *) ((u8 *) hdr + hdr->length);
	}
	printk(KERN_ERR "Malformed %s value\n", name);
	return 0;
}
