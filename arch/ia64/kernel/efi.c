// SPDX-License-Identifier: GPL-2.0
/*
 * Extensible Firmware Interface
 *
 * Based on Extensible Firmware Interface Specification version 0.9
 * April 30, 1999
 *
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999-2003 Hewlett-Packard Co.
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Stephane Eranian <eranian@hpl.hp.com>
 * (c) Copyright 2006 Hewlett-Packard Development Company, L.P.
 *	Bjorn Helgaas <bjorn.helgaas@hp.com>
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
#include <linux/module.h>
#include <linux/bootmem.h>
#include <linux/crash_dump.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <linux/efi.h>
#include <linux/kexec.h>
#include <linux/mm.h>

#include <asm/io.h>
#include <asm/kregs.h>
#include <asm/meminit.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/mca.h>
#include <asm/setup.h>
#include <asm/tlbflush.h>

#define EFI_DEBUG	0

static __initdata unsigned long palo_phys;

static __initdata efi_config_table_type_t arch_tables[] = {
	{PROCESSOR_ABSTRACTION_LAYER_OVERWRITE_GUID, "PALO", &palo_phys},
	{NULL_GUID, NULL, 0},
};

extern efi_status_t efi_call_phys (void *, ...);

static efi_runtime_services_t *runtime;
static u64 mem_limit = ~0UL, max_addr = ~0UL, min_addr = 0UL;

#define efi_call_virt(f, args...)	(*(f))(args)

#define STUB_GET_TIME(prefix, adjust_arg)				       \
static efi_status_t							       \
prefix##_get_time (efi_time_t *tm, efi_time_cap_t *tc)			       \
{									       \
	struct ia64_fpreg fr[6];					       \
	efi_time_cap_t *atc = NULL;					       \
	efi_status_t ret;						       \
									       \
	if (tc)								       \
		atc = adjust_arg(tc);					       \
	ia64_save_scratch_fpregs(fr);					       \
	ret = efi_call_##prefix((efi_get_time_t *) __va(runtime->get_time),    \
				adjust_arg(tm), atc);			       \
	ia64_load_scratch_fpregs(fr);					       \
	return ret;							       \
}

#define STUB_SET_TIME(prefix, adjust_arg)				       \
static efi_status_t							       \
prefix##_set_time (efi_time_t *tm)					       \
{									       \
	struct ia64_fpreg fr[6];					       \
	efi_status_t ret;						       \
									       \
	ia64_save_scratch_fpregs(fr);					       \
	ret = efi_call_##prefix((efi_set_time_t *) __va(runtime->set_time),    \
				adjust_arg(tm));			       \
	ia64_load_scratch_fpregs(fr);					       \
	return ret;							       \
}

#define STUB_GET_WAKEUP_TIME(prefix, adjust_arg)			       \
static efi_status_t							       \
prefix##_get_wakeup_time (efi_bool_t *enabled, efi_bool_t *pending,	       \
			  efi_time_t *tm)				       \
{									       \
	struct ia64_fpreg fr[6];					       \
	efi_status_t ret;						       \
									       \
	ia64_save_scratch_fpregs(fr);					       \
	ret = efi_call_##prefix(					       \
		(efi_get_wakeup_time_t *) __va(runtime->get_wakeup_time),      \
		adjust_arg(enabled), adjust_arg(pending), adjust_arg(tm));     \
	ia64_load_scratch_fpregs(fr);					       \
	return ret;							       \
}

#define STUB_SET_WAKEUP_TIME(prefix, adjust_arg)			       \
static efi_status_t							       \
prefix##_set_wakeup_time (efi_bool_t enabled, efi_time_t *tm)		       \
{									       \
	struct ia64_fpreg fr[6];					       \
	efi_time_t *atm = NULL;						       \
	efi_status_t ret;						       \
									       \
	if (tm)								       \
		atm = adjust_arg(tm);					       \
	ia64_save_scratch_fpregs(fr);					       \
	ret = efi_call_##prefix(					       \
		(efi_set_wakeup_time_t *) __va(runtime->set_wakeup_time),      \
		enabled, atm);						       \
	ia64_load_scratch_fpregs(fr);					       \
	return ret;							       \
}

#define STUB_GET_VARIABLE(prefix, adjust_arg)				       \
static efi_status_t							       \
prefix##_get_variable (efi_char16_t *name, efi_guid_t *vendor, u32 *attr,      \
		       unsigned long *data_size, void *data)		       \
{									       \
	struct ia64_fpreg fr[6];					       \
	u32 *aattr = NULL;						       \
	efi_status_t ret;						       \
									       \
	if (attr)							       \
		aattr = adjust_arg(attr);				       \
	ia64_save_scratch_fpregs(fr);					       \
	ret = efi_call_##prefix(					       \
		(efi_get_variable_t *) __va(runtime->get_variable),	       \
		adjust_arg(name), adjust_arg(vendor), aattr,		       \
		adjust_arg(data_size), adjust_arg(data));		       \
	ia64_load_scratch_fpregs(fr);					       \
	return ret;							       \
}

#define STUB_GET_NEXT_VARIABLE(prefix, adjust_arg)			       \
static efi_status_t							       \
prefix##_get_next_variable (unsigned long *name_size, efi_char16_t *name,      \
			    efi_guid_t *vendor)				       \
{									       \
	struct ia64_fpreg fr[6];					       \
	efi_status_t ret;						       \
									       \
	ia64_save_scratch_fpregs(fr);					       \
	ret = efi_call_##prefix(					       \
		(efi_get_next_variable_t *) __va(runtime->get_next_variable),  \
		adjust_arg(name_size), adjust_arg(name), adjust_arg(vendor));  \
	ia64_load_scratch_fpregs(fr);					       \
	return ret;							       \
}

#define STUB_SET_VARIABLE(prefix, adjust_arg)				       \
static efi_status_t							       \
prefix##_set_variable (efi_char16_t *name, efi_guid_t *vendor,		       \
		       u32 attr, unsigned long data_size,		       \
		       void *data)					       \
{									       \
	struct ia64_fpreg fr[6];					       \
	efi_status_t ret;						       \
									       \
	ia64_save_scratch_fpregs(fr);					       \
	ret = efi_call_##prefix(					       \
		(efi_set_variable_t *) __va(runtime->set_variable),	       \
		adjust_arg(name), adjust_arg(vendor), attr, data_size,	       \
		adjust_arg(data));					       \
	ia64_load_scratch_fpregs(fr);					       \
	return ret;							       \
}

#define STUB_GET_NEXT_HIGH_MONO_COUNT(prefix, adjust_arg)		       \
static efi_status_t							       \
prefix##_get_next_high_mono_count (u32 *count)				       \
{									       \
	struct ia64_fpreg fr[6];					       \
	efi_status_t ret;						       \
									       \
	ia64_save_scratch_fpregs(fr);					       \
	ret = efi_call_##prefix((efi_get_next_high_mono_count_t *)	       \
				__va(runtime->get_next_high_mono_count),       \
				adjust_arg(count));			       \
	ia64_load_scratch_fpregs(fr);					       \
	return ret;							       \
}

#define STUB_RESET_SYSTEM(prefix, adjust_arg)				       \
static void								       \
prefix##_reset_system (int reset_type, efi_status_t status,		       \
		       unsigned long data_size, efi_char16_t *data)	       \
{									       \
	struct ia64_fpreg fr[6];					       \
	efi_char16_t *adata = NULL;					       \
									       \
	if (data)							       \
		adata = adjust_arg(data);				       \
									       \
	ia64_save_scratch_fpregs(fr);					       \
	efi_call_##prefix(						       \
		(efi_reset_system_t *) __va(runtime->reset_system),	       \
		reset_type, status, data_size, adata);			       \
	/* should not return, but just in case... */			       \
	ia64_load_scratch_fpregs(fr);					       \
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
efi_gettimeofday (struct timespec64 *ts)
{
	efi_time_t tm;

	if ((*efi.get_time)(&tm, NULL) != EFI_SUCCESS) {
		memset(ts, 0, sizeof(*ts));
		return;
	}

	ts->tv_sec = mktime64(tm.year, tm.month, tm.day,
			    tm.hour, tm.minute, tm.second);
	ts->tv_nsec = tm.nanosecond;
}

static int
is_memory_available (efi_memory_desc_t *md)
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

typedef struct kern_memdesc {
	u64 attribute;
	u64 start;
	u64 num_pages;
} kern_memdesc_t;

static kern_memdesc_t *kern_memmap;

#define efi_md_size(md)	(md->num_pages << EFI_PAGE_SHIFT)

static inline u64
kmd_end(kern_memdesc_t *kmd)
{
	return (kmd->start + (kmd->num_pages << EFI_PAGE_SHIFT));
}

static inline u64
efi_md_end(efi_memory_desc_t *md)
{
	return (md->phys_addr + efi_md_size(md));
}

static inline int
efi_wb(efi_memory_desc_t *md)
{
	return (md->attribute & EFI_MEMORY_WB);
}

static inline int
efi_uc(efi_memory_desc_t *md)
{
	return (md->attribute & EFI_MEMORY_UC);
}

static void
walk (efi_freemem_callback_t callback, void *arg, u64 attr)
{
	kern_memdesc_t *k;
	u64 start, end, voff;

	voff = (attr == EFI_MEMORY_WB) ? PAGE_OFFSET : __IA64_UNCACHED_OFFSET;
	for (k = kern_memmap; k->start != ~0UL; k++) {
		if (k->attribute != attr)
			continue;
		start = PAGE_ALIGN(k->start);
		end = (k->start + (k->num_pages << EFI_PAGE_SHIFT)) & PAGE_MASK;
		if (start < end)
			if ((*callback)(start + voff, end + voff, arg) < 0)
				return;
	}
}

/*
 * Walk the EFI memory map and call CALLBACK once for each EFI memory
 * descriptor that has memory that is available for OS use.
 */
void
efi_memmap_walk (efi_freemem_callback_t callback, void *arg)
{
	walk(callback, arg, EFI_MEMORY_WB);
}

/*
 * Walk the EFI memory map and call CALLBACK once for each EFI memory
 * descriptor that has memory that is available for uncached allocator.
 */
void
efi_memmap_walk_uc (efi_freemem_callback_t callback, void *arg)
{
	walk(callback, arg, EFI_MEMORY_UC);
}

/*
 * Look for the PAL_CODE region reported by EFI and map it using an
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
			printk(KERN_ERR "Too many EFI Pal Code memory ranges, "
			       "dropped @ %llx\n", md->phys_addr);
			continue;
		}
		/*
		 * The only ITLB entry in region 7 that is used is the one
		 * installed by __start().  That entry covers a 64MB range.
		 */
		mask  = ~((1 << KERNEL_TR_PAGE_SHIFT) - 1);
		vaddr = PAGE_OFFSET + md->phys_addr;

		/*
		 * We must check that the PAL mapping won't overlap with the
		 * kernel mapping.
		 *
		 * PAL code is guaranteed to be aligned on a power of 2 between
		 * 4k and 256KB and that only one ITR is needed to map it. This
		 * implies that the PAL code is always aligned on its size,
		 * i.e., the closest matching page size supported by the TLB.
		 * Therefore PAL code is guaranteed never to cross a 64MB unless
		 * it is bigger than 64MB (very unlikely!).  So for now the
		 * following test is enough to determine whether or not we need
		 * a dedicated ITR for the PAL code.
		 */
		if ((vaddr & mask) == (KERNEL_START & mask)) {
			printk(KERN_INFO "%s: no need to install ITR for PAL code\n",
			       __func__);
			continue;
		}

		if (efi_md_size(md) > IA64_GRANULE_SIZE)
			panic("Whoa!  PAL code size bigger than a granule!");

#if EFI_DEBUG
		mask  = ~((1 << IA64_GRANULE_SHIFT) - 1);

		printk(KERN_INFO "CPU %d: mapping PAL code "
                       "[0x%lx-0x%lx) into [0x%lx-0x%lx)\n",
                       smp_processor_id(), md->phys_addr,
                       md->phys_addr + efi_md_size(md),
                       vaddr & mask, (vaddr & mask) + IA64_GRANULE_SIZE);
#endif
		return __va(md->phys_addr);
	}
	printk(KERN_WARNING "%s: no PAL-code memory-descriptor found\n",
	       __func__);
	return NULL;
}


static u8 __init palo_checksum(u8 *buffer, u32 length)
{
	u8 sum = 0;
	u8 *end = buffer + length;

	while (buffer < end)
		sum = (u8) (sum + *(buffer++));

	return sum;
}

/*
 * Parse and handle PALO table which is published at:
 * http://www.dig64.org/home/DIG64_PALO_R1_0.pdf
 */
static void __init handle_palo(unsigned long phys_addr)
{
	struct palo_table *palo = __va(phys_addr);
	u8  checksum;

	if (strncmp(palo->signature, PALO_SIG, sizeof(PALO_SIG) - 1)) {
		printk(KERN_INFO "PALO signature incorrect.\n");
		return;
	}

	checksum = palo_checksum((u8 *)palo, palo->length);
	if (checksum) {
		printk(KERN_INFO "PALO checksum incorrect.\n");
		return;
	}

	setup_ptcg_sem(palo->max_tlb_purges, NPTCG_FROM_PALO);
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
	ia64_itr(0x1, IA64_TR_PALCODE,
		 GRANULEROUNDDOWN((unsigned long) pal_vaddr),
		 pte_val(pfn_pte(__pa(pal_vaddr) >> PAGE_SHIFT, PAGE_KERNEL)),
		 IA64_GRANULE_SHIFT);
	ia64_set_psr(psr);		/* restore psr */
}

void __init
efi_init (void)
{
	void *efi_map_start, *efi_map_end;
	efi_char16_t *c16;
	u64 efi_desc_size;
	char *cp, vendor[100] = "unknown";
	int i;

	set_bit(EFI_BOOT, &efi.flags);
	set_bit(EFI_64BIT, &efi.flags);

	/*
	 * It's too early to be able to use the standard kernel command line
	 * support...
	 */
	for (cp = boot_command_line; *cp; ) {
		if (memcmp(cp, "mem=", 4) == 0) {
			mem_limit = memparse(cp + 4, &cp);
		} else if (memcmp(cp, "max_addr=", 9) == 0) {
			max_addr = GRANULEROUNDDOWN(memparse(cp + 9, &cp));
		} else if (memcmp(cp, "min_addr=", 9) == 0) {
			min_addr = GRANULEROUNDDOWN(memparse(cp + 9, &cp));
		} else {
			while (*cp != ' ' && *cp)
				++cp;
			while (*cp == ' ')
				++cp;
		}
	}
	if (min_addr != 0UL)
		printk(KERN_INFO "Ignoring memory below %lluMB\n",
		       min_addr >> 20);
	if (max_addr != ~0UL)
		printk(KERN_INFO "Ignoring memory above %lluMB\n",
		       max_addr >> 20);

	efi.systab = __va(ia64_boot_param->efi_systab);

	/*
	 * Verify the EFI Table
	 */
	if (efi.systab == NULL)
		panic("Whoa! Can't find EFI system table.\n");
	if (efi.systab->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE)
		panic("Whoa! EFI system table signature incorrect\n");
	if ((efi.systab->hdr.revision >> 16) == 0)
		printk(KERN_WARNING "Warning: EFI system table version "
		       "%d.%02d, expected 1.00 or greater\n",
		       efi.systab->hdr.revision >> 16,
		       efi.systab->hdr.revision & 0xffff);

	/* Show what we know for posterity */
	c16 = __va(efi.systab->fw_vendor);
	if (c16) {
		for (i = 0;i < (int) sizeof(vendor) - 1 && *c16; ++i)
			vendor[i] = *c16++;
		vendor[i] = '\0';
	}

	printk(KERN_INFO "EFI v%u.%.02u by %s:",
	       efi.systab->hdr.revision >> 16,
	       efi.systab->hdr.revision & 0xffff, vendor);

	palo_phys      = EFI_INVALID_TABLE_ADDR;

	if (efi_config_init(arch_tables) != 0)
		return;

	if (palo_phys != EFI_INVALID_TABLE_ADDR)
		handle_palo(palo_phys);

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

		for (i = 0, p = efi_map_start; p < efi_map_end;
		     ++i, p += efi_desc_size)
		{
			const char *unit;
			unsigned long size;
			char buf[64];

			md = p;
			size = md->num_pages << EFI_PAGE_SHIFT;

			if ((size >> 40) > 0) {
				size >>= 40;
				unit = "TB";
			} else if ((size >> 30) > 0) {
				size >>= 30;
				unit = "GB";
			} else if ((size >> 20) > 0) {
				size >>= 20;
				unit = "MB";
			} else {
				size >>= 10;
				unit = "KB";
			}

			printk("mem%02d: %s "
			       "range=[0x%016lx-0x%016lx) (%4lu%s)\n",
			       i, efi_md_typeattr_format(buf, sizeof(buf), md),
			       md->phys_addr,
			       md->phys_addr + efi_md_size(md), size, unit);
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
			 * Some descriptors have multiple bits set, so the
			 * order of the tests is relevant.
			 */
			if (md->attribute & EFI_MEMORY_WB) {
				md->virt_addr = (u64) __va(md->phys_addr);
			} else if (md->attribute & EFI_MEMORY_UC) {
				md->virt_addr = (u64) ioremap(md->phys_addr, 0);
			} else if (md->attribute & EFI_MEMORY_WC) {
#if 0
				md->virt_addr = ia64_remap(md->phys_addr,
							   (_PAGE_A |
							    _PAGE_P |
							    _PAGE_D |
							    _PAGE_MA_WC |
							    _PAGE_PL_0 |
							    _PAGE_AR_RW));
#else
				printk(KERN_INFO "EFI_MEMORY_WC mapping\n");
				md->virt_addr = (u64) ioremap(md->phys_addr, 0);
#endif
			} else if (md->attribute & EFI_MEMORY_WT) {
#if 0
				md->virt_addr = ia64_remap(md->phys_addr,
							   (_PAGE_A |
							    _PAGE_P |
							    _PAGE_D |
							    _PAGE_MA_WT |
							    _PAGE_PL_0 |
							    _PAGE_AR_RW));
#else
				printk(KERN_INFO "EFI_MEMORY_WT mapping\n");
				md->virt_addr = (u64) ioremap(md->phys_addr, 0);
#endif
			}
		}
	}

	status = efi_call_phys(__va(runtime->set_virtual_address_map),
			       ia64_boot_param->efi_memmap_size,
			       efi_desc_size,
			       ia64_boot_param->efi_memdesc_version,
			       ia64_boot_param->efi_memmap);
	if (status != EFI_SUCCESS) {
		printk(KERN_WARNING "warning: unable to switch EFI into "
		       "virtual mode (status=%lu)\n", status);
		return;
	}

	set_bit(EFI_RUNTIME_SERVICES, &efi.flags);

	/*
	 * Now that EFI is in virtual mode, we call the EFI functions more
	 * efficiently:
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
 * Walk the EFI memory map looking for the I/O port range.  There can only be
 * one entry of this type, other I/O port ranges should be described via ACPI.
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

static struct kern_memdesc *
kern_memory_descriptor (unsigned long phys_addr)
{
	struct kern_memdesc *md;

	for (md = kern_memmap; md->start != ~0UL; md++) {
		if (phys_addr - md->start < (md->num_pages << EFI_PAGE_SHIFT))
			 return md;
	}
	return NULL;
}

static efi_memory_desc_t *
efi_memory_descriptor (unsigned long phys_addr)
{
	void *efi_map_start, *efi_map_end, *p;
	efi_memory_desc_t *md;
	u64 efi_desc_size;

	efi_map_start = __va(ia64_boot_param->efi_memmap);
	efi_map_end   = efi_map_start + ia64_boot_param->efi_memmap_size;
	efi_desc_size = ia64_boot_param->efi_memdesc_size;

	for (p = efi_map_start; p < efi_map_end; p += efi_desc_size) {
		md = p;

		if (phys_addr - md->phys_addr < efi_md_size(md))
			 return md;
	}
	return NULL;
}

static int
efi_memmap_intersects (unsigned long phys_addr, unsigned long size)
{
	void *efi_map_start, *efi_map_end, *p;
	efi_memory_desc_t *md;
	u64 efi_desc_size;
	unsigned long end;

	efi_map_start = __va(ia64_boot_param->efi_memmap);
	efi_map_end   = efi_map_start + ia64_boot_param->efi_memmap_size;
	efi_desc_size = ia64_boot_param->efi_memdesc_size;

	end = phys_addr + size;

	for (p = efi_map_start; p < efi_map_end; p += efi_desc_size) {
		md = p;
		if (md->phys_addr < end && efi_md_end(md) > phys_addr)
			return 1;
	}
	return 0;
}

int
efi_mem_type (unsigned long phys_addr)
{
	efi_memory_desc_t *md = efi_memory_descriptor(phys_addr);

	if (md)
		return md->type;
	return -EINVAL;
}

u64
efi_mem_attributes (unsigned long phys_addr)
{
	efi_memory_desc_t *md = efi_memory_descriptor(phys_addr);

	if (md)
		return md->attribute;
	return 0;
}
EXPORT_SYMBOL(efi_mem_attributes);

u64
efi_mem_attribute (unsigned long phys_addr, unsigned long size)
{
	unsigned long end = phys_addr + size;
	efi_memory_desc_t *md = efi_memory_descriptor(phys_addr);
	u64 attr;

	if (!md)
		return 0;

	/*
	 * EFI_MEMORY_RUNTIME is not a memory attribute; it just tells
	 * the kernel that firmware needs this region mapped.
	 */
	attr = md->attribute & ~EFI_MEMORY_RUNTIME;
	do {
		unsigned long md_end = efi_md_end(md);

		if (end <= md_end)
			return attr;

		md = efi_memory_descriptor(md_end);
		if (!md || (md->attribute & ~EFI_MEMORY_RUNTIME) != attr)
			return 0;
	} while (md);
	return 0;	/* never reached */
}

u64
kern_mem_attribute (unsigned long phys_addr, unsigned long size)
{
	unsigned long end = phys_addr + size;
	struct kern_memdesc *md;
	u64 attr;

	/*
	 * This is a hack for ioremap calls before we set up kern_memmap.
	 * Maybe we should do efi_memmap_init() earlier instead.
	 */
	if (!kern_memmap) {
		attr = efi_mem_attribute(phys_addr, size);
		if (attr & EFI_MEMORY_WB)
			return EFI_MEMORY_WB;
		return 0;
	}

	md = kern_memory_descriptor(phys_addr);
	if (!md)
		return 0;

	attr = md->attribute;
	do {
		unsigned long md_end = kmd_end(md);

		if (end <= md_end)
			return attr;

		md = kern_memory_descriptor(md_end);
		if (!md || md->attribute != attr)
			return 0;
	} while (md);
	return 0;	/* never reached */
}

int
valid_phys_addr_range (phys_addr_t phys_addr, unsigned long size)
{
	u64 attr;

	/*
	 * /dev/mem reads and writes use copy_to_user(), which implicitly
	 * uses a granule-sized kernel identity mapping.  It's really
	 * only safe to do this for regions in kern_memmap.  For more
	 * details, see Documentation/ia64/aliasing.txt.
	 */
	attr = kern_mem_attribute(phys_addr, size);
	if (attr & EFI_MEMORY_WB || attr & EFI_MEMORY_UC)
		return 1;
	return 0;
}

int
valid_mmap_phys_addr_range (unsigned long pfn, unsigned long size)
{
	unsigned long phys_addr = pfn << PAGE_SHIFT;
	u64 attr;

	attr = efi_mem_attribute(phys_addr, size);

	/*
	 * /dev/mem mmap uses normal user pages, so we don't need the entire
	 * granule, but the entire region we're mapping must support the same
	 * attribute.
	 */
	if (attr & EFI_MEMORY_WB || attr & EFI_MEMORY_UC)
		return 1;

	/*
	 * Intel firmware doesn't tell us about all the MMIO regions, so
	 * in general we have to allow mmap requests.  But if EFI *does*
	 * tell us about anything inside this region, we should deny it.
	 * The user can always map a smaller region to avoid the overlap.
	 */
	if (efi_memmap_intersects(phys_addr, size))
		return 0;

	return 1;
}

pgprot_t
phys_mem_access_prot(struct file *file, unsigned long pfn, unsigned long size,
		     pgprot_t vma_prot)
{
	unsigned long phys_addr = pfn << PAGE_SHIFT;
	u64 attr;

	/*
	 * For /dev/mem mmap, we use user mappings, but if the region is
	 * in kern_memmap (and hence may be covered by a kernel mapping),
	 * we must use the same attribute as the kernel mapping.
	 */
	attr = kern_mem_attribute(phys_addr, size);
	if (attr & EFI_MEMORY_WB)
		return pgprot_cacheable(vma_prot);
	else if (attr & EFI_MEMORY_UC)
		return pgprot_noncached(vma_prot);

	/*
	 * Some chipsets don't support UC access to memory.  If
	 * WB is supported, we prefer that.
	 */
	if (efi_mem_attribute(phys_addr, size) & EFI_MEMORY_WB)
		return pgprot_cacheable(vma_prot);

	return pgprot_noncached(vma_prot);
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
		hdr = (struct efi_generic_dev_path *)((u8 *) hdr + hdr->length);
	}
	printk(KERN_ERR "Malformed %s value\n", name);
	return 0;
}

/*
 * Look for the first granule aligned memory descriptor memory
 * that is big enough to hold EFI memory map. Make sure this
 * descriptor is at least granule sized so it does not get trimmed
 */
struct kern_memdesc *
find_memmap_space (void)
{
	u64	contig_low=0, contig_high=0;
	u64	as = 0, ae;
	void *efi_map_start, *efi_map_end, *p, *q;
	efi_memory_desc_t *md, *pmd = NULL, *check_md;
	u64	space_needed, efi_desc_size;
	unsigned long total_mem = 0;

	efi_map_start = __va(ia64_boot_param->efi_memmap);
	efi_map_end   = efi_map_start + ia64_boot_param->efi_memmap_size;
	efi_desc_size = ia64_boot_param->efi_memdesc_size;

	/*
	 * Worst case: we need 3 kernel descriptors for each efi descriptor
	 * (if every entry has a WB part in the middle, and UC head and tail),
	 * plus one for the end marker.
	 */
	space_needed = sizeof(kern_memdesc_t) *
		(3 * (ia64_boot_param->efi_memmap_size/efi_desc_size) + 1);

	for (p = efi_map_start; p < efi_map_end; pmd = md, p += efi_desc_size) {
		md = p;
		if (!efi_wb(md)) {
			continue;
		}
		if (pmd == NULL || !efi_wb(pmd) ||
		    efi_md_end(pmd) != md->phys_addr) {
			contig_low = GRANULEROUNDUP(md->phys_addr);
			contig_high = efi_md_end(md);
			for (q = p + efi_desc_size; q < efi_map_end;
			     q += efi_desc_size) {
				check_md = q;
				if (!efi_wb(check_md))
					break;
				if (contig_high != check_md->phys_addr)
					break;
				contig_high = efi_md_end(check_md);
			}
			contig_high = GRANULEROUNDDOWN(contig_high);
		}
		if (!is_memory_available(md) || md->type == EFI_LOADER_DATA)
			continue;

		/* Round ends inward to granule boundaries */
		as = max(contig_low, md->phys_addr);
		ae = min(contig_high, efi_md_end(md));

		/* keep within max_addr= and min_addr= command line arg */
		as = max(as, min_addr);
		ae = min(ae, max_addr);
		if (ae <= as)
			continue;

		/* avoid going over mem= command line arg */
		if (total_mem + (ae - as) > mem_limit)
			ae -= total_mem + (ae - as) - mem_limit;

		if (ae <= as)
			continue;

		if (ae - as > space_needed)
			break;
	}
	if (p >= efi_map_end)
		panic("Can't allocate space for kernel memory descriptors");

	return __va(as);
}

/*
 * Walk the EFI memory map and gather all memory available for kernel
 * to use.  We can allocate partial granules only if the unavailable
 * parts exist, and are WB.
 */
unsigned long
efi_memmap_init(u64 *s, u64 *e)
{
	struct kern_memdesc *k, *prev = NULL;
	u64	contig_low=0, contig_high=0;
	u64	as, ae, lim;
	void *efi_map_start, *efi_map_end, *p, *q;
	efi_memory_desc_t *md, *pmd = NULL, *check_md;
	u64	efi_desc_size;
	unsigned long total_mem = 0;

	k = kern_memmap = find_memmap_space();

	efi_map_start = __va(ia64_boot_param->efi_memmap);
	efi_map_end   = efi_map_start + ia64_boot_param->efi_memmap_size;
	efi_desc_size = ia64_boot_param->efi_memdesc_size;

	for (p = efi_map_start; p < efi_map_end; pmd = md, p += efi_desc_size) {
		md = p;
		if (!efi_wb(md)) {
			if (efi_uc(md) &&
			    (md->type == EFI_CONVENTIONAL_MEMORY ||
			     md->type == EFI_BOOT_SERVICES_DATA)) {
				k->attribute = EFI_MEMORY_UC;
				k->start = md->phys_addr;
				k->num_pages = md->num_pages;
				k++;
			}
			continue;
		}
		if (pmd == NULL || !efi_wb(pmd) ||
		    efi_md_end(pmd) != md->phys_addr) {
			contig_low = GRANULEROUNDUP(md->phys_addr);
			contig_high = efi_md_end(md);
			for (q = p + efi_desc_size; q < efi_map_end;
			     q += efi_desc_size) {
				check_md = q;
				if (!efi_wb(check_md))
					break;
				if (contig_high != check_md->phys_addr)
					break;
				contig_high = efi_md_end(check_md);
			}
			contig_high = GRANULEROUNDDOWN(contig_high);
		}
		if (!is_memory_available(md))
			continue;

		/*
		 * Round ends inward to granule boundaries
		 * Give trimmings to uncached allocator
		 */
		if (md->phys_addr < contig_low) {
			lim = min(efi_md_end(md), contig_low);
			if (efi_uc(md)) {
				if (k > kern_memmap &&
				    (k-1)->attribute == EFI_MEMORY_UC &&
				    kmd_end(k-1) == md->phys_addr) {
					(k-1)->num_pages +=
						(lim - md->phys_addr)
						>> EFI_PAGE_SHIFT;
				} else {
					k->attribute = EFI_MEMORY_UC;
					k->start = md->phys_addr;
					k->num_pages = (lim - md->phys_addr)
						>> EFI_PAGE_SHIFT;
					k++;
				}
			}
			as = contig_low;
		} else
			as = md->phys_addr;

		if (efi_md_end(md) > contig_high) {
			lim = max(md->phys_addr, contig_high);
			if (efi_uc(md)) {
				if (lim == md->phys_addr && k > kern_memmap &&
				    (k-1)->attribute == EFI_MEMORY_UC &&
				    kmd_end(k-1) == md->phys_addr) {
					(k-1)->num_pages += md->num_pages;
				} else {
					k->attribute = EFI_MEMORY_UC;
					k->start = lim;
					k->num_pages = (efi_md_end(md) - lim)
						>> EFI_PAGE_SHIFT;
					k++;
				}
			}
			ae = contig_high;
		} else
			ae = efi_md_end(md);

		/* keep within max_addr= and min_addr= command line arg */
		as = max(as, min_addr);
		ae = min(ae, max_addr);
		if (ae <= as)
			continue;

		/* avoid going over mem= command line arg */
		if (total_mem + (ae - as) > mem_limit)
			ae -= total_mem + (ae - as) - mem_limit;

		if (ae <= as)
			continue;
		if (prev && kmd_end(prev) == md->phys_addr) {
			prev->num_pages += (ae - as) >> EFI_PAGE_SHIFT;
			total_mem += ae - as;
			continue;
		}
		k->attribute = EFI_MEMORY_WB;
		k->start = as;
		k->num_pages = (ae - as) >> EFI_PAGE_SHIFT;
		total_mem += ae - as;
		prev = k++;
	}
	k->start = ~0L; /* end-marker */

	/* reserve the memory we are using for kern_memmap */
	*s = (u64)kern_memmap;
	*e = (u64)++k;

	return total_mem;
}

void
efi_initialize_iomem_resources(struct resource *code_resource,
			       struct resource *data_resource,
			       struct resource *bss_resource)
{
	struct resource *res;
	void *efi_map_start, *efi_map_end, *p;
	efi_memory_desc_t *md;
	u64 efi_desc_size;
	char *name;
	unsigned long flags, desc;

	efi_map_start = __va(ia64_boot_param->efi_memmap);
	efi_map_end   = efi_map_start + ia64_boot_param->efi_memmap_size;
	efi_desc_size = ia64_boot_param->efi_memdesc_size;

	res = NULL;

	for (p = efi_map_start; p < efi_map_end; p += efi_desc_size) {
		md = p;

		if (md->num_pages == 0) /* should not happen */
			continue;

		flags = IORESOURCE_MEM | IORESOURCE_BUSY;
		desc = IORES_DESC_NONE;

		switch (md->type) {

			case EFI_MEMORY_MAPPED_IO:
			case EFI_MEMORY_MAPPED_IO_PORT_SPACE:
				continue;

			case EFI_LOADER_CODE:
			case EFI_LOADER_DATA:
			case EFI_BOOT_SERVICES_DATA:
			case EFI_BOOT_SERVICES_CODE:
			case EFI_CONVENTIONAL_MEMORY:
				if (md->attribute & EFI_MEMORY_WP) {
					name = "System ROM";
					flags |= IORESOURCE_READONLY;
				} else if (md->attribute == EFI_MEMORY_UC) {
					name = "Uncached RAM";
				} else {
					name = "System RAM";
					flags |= IORESOURCE_SYSRAM;
				}
				break;

			case EFI_ACPI_MEMORY_NVS:
				name = "ACPI Non-volatile Storage";
				desc = IORES_DESC_ACPI_NV_STORAGE;
				break;

			case EFI_UNUSABLE_MEMORY:
				name = "reserved";
				flags |= IORESOURCE_DISABLED;
				break;

			case EFI_PERSISTENT_MEMORY:
				name = "Persistent Memory";
				desc = IORES_DESC_PERSISTENT_MEMORY;
				break;

			case EFI_RESERVED_TYPE:
			case EFI_RUNTIME_SERVICES_CODE:
			case EFI_RUNTIME_SERVICES_DATA:
			case EFI_ACPI_RECLAIM_MEMORY:
			default:
				name = "reserved";
				break;
		}

		if ((res = kzalloc(sizeof(struct resource),
				   GFP_KERNEL)) == NULL) {
			printk(KERN_ERR
			       "failed to allocate resource for iomem\n");
			return;
		}

		res->name = name;
		res->start = md->phys_addr;
		res->end = md->phys_addr + efi_md_size(md) - 1;
		res->flags = flags;
		res->desc = desc;

		if (insert_resource(&iomem_resource, res) < 0)
			kfree(res);
		else {
			/*
			 * We don't know which region contains
			 * kernel data so we try it repeatedly and
			 * let the resource manager test it.
			 */
			insert_resource(res, code_resource);
			insert_resource(res, data_resource);
			insert_resource(res, bss_resource);
#ifdef CONFIG_KEXEC
                        insert_resource(res, &efi_memmap_res);
                        insert_resource(res, &boot_param_res);
			if (crashk_res.end > crashk_res.start)
				insert_resource(res, &crashk_res);
#endif
		}
	}
}

#ifdef CONFIG_KEXEC
/* find a block of memory aligned to 64M exclude reserved regions
   rsvd_regions are sorted
 */
unsigned long __init
kdump_find_rsvd_region (unsigned long size, struct rsvd_region *r, int n)
{
	int i;
	u64 start, end;
	u64 alignment = 1UL << _PAGE_SIZE_64M;
	void *efi_map_start, *efi_map_end, *p;
	efi_memory_desc_t *md;
	u64 efi_desc_size;

	efi_map_start = __va(ia64_boot_param->efi_memmap);
	efi_map_end   = efi_map_start + ia64_boot_param->efi_memmap_size;
	efi_desc_size = ia64_boot_param->efi_memdesc_size;

	for (p = efi_map_start; p < efi_map_end; p += efi_desc_size) {
		md = p;
		if (!efi_wb(md))
			continue;
		start = ALIGN(md->phys_addr, alignment);
		end = efi_md_end(md);
		for (i = 0; i < n; i++) {
			if (__pa(r[i].start) >= start && __pa(r[i].end) < end) {
				if (__pa(r[i].start) > start + size)
					return start;
				start = ALIGN(__pa(r[i].end), alignment);
				if (i < n-1 &&
				    __pa(r[i+1].start) < start + size)
					continue;
				else
					break;
			}
		}
		if (end > start + size)
			return start;
	}

	printk(KERN_WARNING
	       "Cannot reserve 0x%lx byte of memory for crashdump\n", size);
	return ~0UL;
}
#endif

#ifdef CONFIG_CRASH_DUMP
/* locate the size find a the descriptor at a certain address */
unsigned long __init
vmcore_find_descriptor_size (unsigned long address)
{
	void *efi_map_start, *efi_map_end, *p;
	efi_memory_desc_t *md;
	u64 efi_desc_size;
	unsigned long ret = 0;

	efi_map_start = __va(ia64_boot_param->efi_memmap);
	efi_map_end   = efi_map_start + ia64_boot_param->efi_memmap_size;
	efi_desc_size = ia64_boot_param->efi_memdesc_size;

	for (p = efi_map_start; p < efi_map_end; p += efi_desc_size) {
		md = p;
		if (efi_wb(md) && md->type == EFI_LOADER_DATA
		    && md->phys_addr == address) {
			ret = efi_md_size(md);
			break;
		}
	}

	if (ret == 0)
		printk(KERN_WARNING "Cannot locate EFI vmcore descriptor\n");

	return ret;
}
#endif
