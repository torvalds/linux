/*
 * Extensible Firmware Interface
 *
 * Based on Extensible Firmware Interface Specification version 1.0
 *
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 1999-2002 Hewlett-Packard Co.
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
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/time.h>
#include <linux/spinlock.h>
#include <linux/bootmem.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/efi.h>
#include <linux/kexec.h>

#include <asm/setup.h>
#include <asm/io.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/desc.h>
#include <asm/tlbflush.h>

#define EFI_DEBUG	0
#define PFX 		"EFI: "

extern efi_status_t asmlinkage efi_call_phys(void *, ...);

struct efi efi;
EXPORT_SYMBOL(efi);
static struct efi efi_phys;
struct efi_memory_map memmap;

/*
 * We require an early boot_ioremap mapping mechanism initially
 */
extern void * boot_ioremap(unsigned long, unsigned long);

/*
 * To make EFI call EFI runtime service in physical addressing mode we need
 * prelog/epilog before/after the invocation to disable interrupt, to
 * claim EFI runtime service handler exclusively and to duplicate a memory in
 * low memory space say 0 - 3G.
 */

static unsigned long efi_rt_eflags;
static DEFINE_SPINLOCK(efi_rt_lock);
static pgd_t efi_bak_pg_dir_pointer[2];

static void efi_call_phys_prelog(void)
{
	unsigned long cr4;
	unsigned long temp;
	struct Xgt_desc_struct *cpu_gdt_descr;

	spin_lock(&efi_rt_lock);
	local_irq_save(efi_rt_eflags);

	cpu_gdt_descr = &per_cpu(cpu_gdt_descr, 0);

	/*
	 * If I don't have PSE, I should just duplicate two entries in page
	 * directory. If I have PSE, I just need to duplicate one entry in
	 * page directory.
	 */
	cr4 = read_cr4();

	if (cr4 & X86_CR4_PSE) {
		efi_bak_pg_dir_pointer[0].pgd =
		    swapper_pg_dir[pgd_index(0)].pgd;
		swapper_pg_dir[0].pgd =
		    swapper_pg_dir[pgd_index(PAGE_OFFSET)].pgd;
	} else {
		efi_bak_pg_dir_pointer[0].pgd =
		    swapper_pg_dir[pgd_index(0)].pgd;
		efi_bak_pg_dir_pointer[1].pgd =
		    swapper_pg_dir[pgd_index(0x400000)].pgd;
		swapper_pg_dir[pgd_index(0)].pgd =
		    swapper_pg_dir[pgd_index(PAGE_OFFSET)].pgd;
		temp = PAGE_OFFSET + 0x400000;
		swapper_pg_dir[pgd_index(0x400000)].pgd =
		    swapper_pg_dir[pgd_index(temp)].pgd;
	}

	/*
	 * After the lock is released, the original page table is restored.
	 */
	local_flush_tlb();

	cpu_gdt_descr->address = __pa(cpu_gdt_descr->address);
	load_gdt(cpu_gdt_descr);
}

static void efi_call_phys_epilog(void)
{
	unsigned long cr4;
	struct Xgt_desc_struct *cpu_gdt_descr = &per_cpu(cpu_gdt_descr, 0);

	cpu_gdt_descr->address = (unsigned long)__va(cpu_gdt_descr->address);
	load_gdt(cpu_gdt_descr);

	cr4 = read_cr4();

	if (cr4 & X86_CR4_PSE) {
		swapper_pg_dir[pgd_index(0)].pgd =
		    efi_bak_pg_dir_pointer[0].pgd;
	} else {
		swapper_pg_dir[pgd_index(0)].pgd =
		    efi_bak_pg_dir_pointer[0].pgd;
		swapper_pg_dir[pgd_index(0x400000)].pgd =
		    efi_bak_pg_dir_pointer[1].pgd;
	}

	/*
	 * After the lock is released, the original page table is restored.
	 */
	local_flush_tlb();

	local_irq_restore(efi_rt_eflags);
	spin_unlock(&efi_rt_lock);
}

static efi_status_t
phys_efi_set_virtual_address_map(unsigned long memory_map_size,
				 unsigned long descriptor_size,
				 u32 descriptor_version,
				 efi_memory_desc_t *virtual_map)
{
	efi_status_t status;

	efi_call_phys_prelog();
	status = efi_call_phys(efi_phys.set_virtual_address_map,
				     memory_map_size, descriptor_size,
				     descriptor_version, virtual_map);
	efi_call_phys_epilog();
	return status;
}

static efi_status_t
phys_efi_get_time(efi_time_t *tm, efi_time_cap_t *tc)
{
	efi_status_t status;

	efi_call_phys_prelog();
	status = efi_call_phys(efi_phys.get_time, tm, tc);
	efi_call_phys_epilog();
	return status;
}

inline int efi_set_rtc_mmss(unsigned long nowtime)
{
	int real_seconds, real_minutes;
	efi_status_t 	status;
	efi_time_t 	eft;
	efi_time_cap_t 	cap;

	spin_lock(&efi_rt_lock);
	status = efi.get_time(&eft, &cap);
	spin_unlock(&efi_rt_lock);
	if (status != EFI_SUCCESS)
		panic("Ooops, efitime: can't read time!\n");
	real_seconds = nowtime % 60;
	real_minutes = nowtime / 60;

	if (((abs(real_minutes - eft.minute) + 15)/30) & 1)
		real_minutes += 30;
	real_minutes %= 60;

	eft.minute = real_minutes;
	eft.second = real_seconds;

	if (status != EFI_SUCCESS) {
		printk("Ooops: efitime: can't read time!\n");
		return -1;
	}
	return 0;
}
/*
 * This should only be used during kernel init and before runtime
 * services have been remapped, therefore, we'll need to call in physical
 * mode.  Note, this call isn't used later, so mark it __init.
 */
inline unsigned long __init efi_get_time(void)
{
	efi_status_t status;
	efi_time_t eft;
	efi_time_cap_t cap;

	status = phys_efi_get_time(&eft, &cap);
	if (status != EFI_SUCCESS)
		printk("Oops: efitime: can't read time status: 0x%lx\n",status);

	return mktime(eft.year, eft.month, eft.day, eft.hour,
			eft.minute, eft.second);
}

int is_available_memory(efi_memory_desc_t * md)
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
 * We need to map the EFI memory map again after paging_init().
 */
void __init efi_map_memmap(void)
{
	memmap.map = NULL;

	memmap.map = bt_ioremap((unsigned long) memmap.phys_map,
			(memmap.nr_map * memmap.desc_size));
	if (memmap.map == NULL)
		printk(KERN_ERR PFX "Could not remap the EFI memmap!\n");

	memmap.map_end = memmap.map + (memmap.nr_map * memmap.desc_size);
}

#if EFI_DEBUG
static void __init print_efi_memmap(void)
{
	efi_memory_desc_t *md;
	void *p;
	int i;

	for (p = memmap.map, i = 0; p < memmap.map_end; p += memmap.desc_size, i++) {
		md = p;
		printk(KERN_INFO "mem%02u: type=%u, attr=0x%llx, "
			"range=[0x%016llx-0x%016llx) (%lluMB)\n",
			i, md->type, md->attribute, md->phys_addr,
			md->phys_addr + (md->num_pages << EFI_PAGE_SHIFT),
			(md->num_pages >> (20 - EFI_PAGE_SHIFT)));
	}
}
#endif  /*  EFI_DEBUG  */

/*
 * Walks the EFI memory map and calls CALLBACK once for each EFI
 * memory descriptor that has memory that is available for kernel use.
 */
void efi_memmap_walk(efi_freemem_callback_t callback, void *arg)
{
	int prev_valid = 0;
	struct range {
		unsigned long start;
		unsigned long end;
	} prev, curr;
	efi_memory_desc_t *md;
	unsigned long start, end;
	void *p;

	for (p = memmap.map; p < memmap.map_end; p += memmap.desc_size) {
		md = p;

		if ((md->num_pages == 0) || (!is_available_memory(md)))
			continue;

		curr.start = md->phys_addr;
		curr.end = curr.start + (md->num_pages << EFI_PAGE_SHIFT);

		if (!prev_valid) {
			prev = curr;
			prev_valid = 1;
		} else {
			if (curr.start < prev.start)
				printk(KERN_INFO PFX "Unordered memory map\n");
			if (prev.end == curr.start)
				prev.end = curr.end;
			else {
				start =
				    (unsigned long) (PAGE_ALIGN(prev.start));
				end = (unsigned long) (prev.end & PAGE_MASK);
				if ((end > start)
				    && (*callback) (start, end, arg) < 0)
					return;
				prev = curr;
			}
		}
	}
	if (prev_valid) {
		start = (unsigned long) PAGE_ALIGN(prev.start);
		end = (unsigned long) (prev.end & PAGE_MASK);
		if (end > start)
			(*callback) (start, end, arg);
	}
}

void __init efi_init(void)
{
	efi_config_table_t *config_tables;
	efi_runtime_services_t *runtime;
	efi_char16_t *c16;
	char vendor[100] = "unknown";
	unsigned long num_config_tables;
	int i = 0;

	memset(&efi, 0, sizeof(efi) );
	memset(&efi_phys, 0, sizeof(efi_phys));

	efi_phys.systab = EFI_SYSTAB;
	memmap.phys_map = EFI_MEMMAP;
	memmap.nr_map = EFI_MEMMAP_SIZE/EFI_MEMDESC_SIZE;
	memmap.desc_version = EFI_MEMDESC_VERSION;
	memmap.desc_size = EFI_MEMDESC_SIZE;

	efi.systab = (efi_system_table_t *)
		boot_ioremap((unsigned long) efi_phys.systab,
			sizeof(efi_system_table_t));
	/*
	 * Verify the EFI Table
	 */
	if (efi.systab == NULL)
		printk(KERN_ERR PFX "Woah! Couldn't map the EFI system table.\n");
	if (efi.systab->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE)
		printk(KERN_ERR PFX "Woah! EFI system table signature incorrect\n");
	if ((efi.systab->hdr.revision ^ EFI_SYSTEM_TABLE_REVISION) >> 16 != 0)
		printk(KERN_ERR PFX
		       "Warning: EFI system table major version mismatch: "
		       "got %d.%02d, expected %d.%02d\n",
		       efi.systab->hdr.revision >> 16,
		       efi.systab->hdr.revision & 0xffff,
		       EFI_SYSTEM_TABLE_REVISION >> 16,
		       EFI_SYSTEM_TABLE_REVISION & 0xffff);
	/*
	 * Grab some details from the system table
	 */
	num_config_tables = efi.systab->nr_tables;
	config_tables = (efi_config_table_t *)efi.systab->tables;
	runtime = efi.systab->runtime;

	/*
	 * Show what we know for posterity
	 */
	c16 = (efi_char16_t *) boot_ioremap(efi.systab->fw_vendor, 2);
	if (c16) {
		for (i = 0; i < (sizeof(vendor) - 1) && *c16; ++i)
			vendor[i] = *c16++;
		vendor[i] = '\0';
	} else
		printk(KERN_ERR PFX "Could not map the firmware vendor!\n");

	printk(KERN_INFO PFX "EFI v%u.%.02u by %s \n",
	       efi.systab->hdr.revision >> 16,
	       efi.systab->hdr.revision & 0xffff, vendor);

	/*
	 * Let's see what config tables the firmware passed to us.
	 */
	config_tables = (efi_config_table_t *)
				boot_ioremap((unsigned long) config_tables,
			        num_config_tables * sizeof(efi_config_table_t));

	if (config_tables == NULL)
		printk(KERN_ERR PFX "Could not map EFI Configuration Table!\n");

	efi.mps        = EFI_INVALID_TABLE_ADDR;
	efi.acpi       = EFI_INVALID_TABLE_ADDR;
	efi.acpi20     = EFI_INVALID_TABLE_ADDR;
	efi.smbios     = EFI_INVALID_TABLE_ADDR;
	efi.sal_systab = EFI_INVALID_TABLE_ADDR;
	efi.boot_info  = EFI_INVALID_TABLE_ADDR;
	efi.hcdp       = EFI_INVALID_TABLE_ADDR;
	efi.uga        = EFI_INVALID_TABLE_ADDR;

	for (i = 0; i < num_config_tables; i++) {
		if (efi_guidcmp(config_tables[i].guid, MPS_TABLE_GUID) == 0) {
			efi.mps = config_tables[i].table;
			printk(KERN_INFO " MPS=0x%lx ", config_tables[i].table);
		} else
		    if (efi_guidcmp(config_tables[i].guid, ACPI_20_TABLE_GUID) == 0) {
			efi.acpi20 = config_tables[i].table;
			printk(KERN_INFO " ACPI 2.0=0x%lx ", config_tables[i].table);
		} else
		    if (efi_guidcmp(config_tables[i].guid, ACPI_TABLE_GUID) == 0) {
			efi.acpi = config_tables[i].table;
			printk(KERN_INFO " ACPI=0x%lx ", config_tables[i].table);
		} else
		    if (efi_guidcmp(config_tables[i].guid, SMBIOS_TABLE_GUID) == 0) {
			efi.smbios = config_tables[i].table;
			printk(KERN_INFO " SMBIOS=0x%lx ", config_tables[i].table);
		} else
		    if (efi_guidcmp(config_tables[i].guid, HCDP_TABLE_GUID) == 0) {
			efi.hcdp = config_tables[i].table;
			printk(KERN_INFO " HCDP=0x%lx ", config_tables[i].table);
		} else
		    if (efi_guidcmp(config_tables[i].guid, UGA_IO_PROTOCOL_GUID) == 0) {
			efi.uga = config_tables[i].table;
			printk(KERN_INFO " UGA=0x%lx ", config_tables[i].table);
		}
	}
	printk("\n");

	/*
	 * Check out the runtime services table. We need to map
	 * the runtime services table so that we can grab the physical
	 * address of several of the EFI runtime functions, needed to
	 * set the firmware into virtual mode.
	 */

	runtime = (efi_runtime_services_t *) boot_ioremap((unsigned long)
						runtime,
				      		sizeof(efi_runtime_services_t));
	if (runtime != NULL) {
		/*
	 	 * We will only need *early* access to the following
		 * two EFI runtime services before set_virtual_address_map
		 * is invoked.
 	 	 */
		efi_phys.get_time = (efi_get_time_t *) runtime->get_time;
		efi_phys.set_virtual_address_map =
			(efi_set_virtual_address_map_t *)
				runtime->set_virtual_address_map;
	} else
		printk(KERN_ERR PFX "Could not map the runtime service table!\n");

	/* Map the EFI memory map for use until paging_init() */
	memmap.map = boot_ioremap((unsigned long) EFI_MEMMAP, EFI_MEMMAP_SIZE);
	if (memmap.map == NULL)
		printk(KERN_ERR PFX "Could not map the EFI memory map!\n");

	memmap.map_end = memmap.map + (memmap.nr_map * memmap.desc_size);

#if EFI_DEBUG
	print_efi_memmap();
#endif
}

static inline void __init check_range_for_systab(efi_memory_desc_t *md)
{
	if (((unsigned long)md->phys_addr <= (unsigned long)efi_phys.systab) &&
		((unsigned long)efi_phys.systab < md->phys_addr +
		((unsigned long)md->num_pages << EFI_PAGE_SHIFT))) {
		unsigned long addr;

		addr = md->virt_addr - md->phys_addr +
			(unsigned long)efi_phys.systab;
		efi.systab = (efi_system_table_t *)addr;
	}
}

/*
 * This function will switch the EFI runtime services to virtual mode.
 * Essentially, look through the EFI memmap and map every region that
 * has the runtime attribute bit set in its memory descriptor and update
 * that memory descriptor with the virtual address obtained from ioremap().
 * This enables the runtime services to be called without having to
 * thunk back into physical mode for every invocation.
 */

void __init efi_enter_virtual_mode(void)
{
	efi_memory_desc_t *md;
	efi_status_t status;
	void *p;

	efi.systab = NULL;

	for (p = memmap.map; p < memmap.map_end; p += memmap.desc_size) {
		md = p;

		if (!(md->attribute & EFI_MEMORY_RUNTIME))
			continue;

		md->virt_addr = (unsigned long)ioremap(md->phys_addr,
			md->num_pages << EFI_PAGE_SHIFT);
		if (!(unsigned long)md->virt_addr) {
			printk(KERN_ERR PFX "ioremap of 0x%lX failed\n",
				(unsigned long)md->phys_addr);
		}
		/* update the virtual address of the EFI system table */
		check_range_for_systab(md);
	}

	if (!efi.systab)
		BUG();

	status = phys_efi_set_virtual_address_map(
			memmap.desc_size * memmap.nr_map,
			memmap.desc_size,
			memmap.desc_version,
		       	memmap.phys_map);

	if (status != EFI_SUCCESS) {
		printk (KERN_ALERT "You are screwed! "
			"Unable to switch EFI into virtual mode "
			"(status=%lx)\n", status);
		panic("EFI call to SetVirtualAddressMap() failed!");
	}

	/*
	 * Now that EFI is in virtual mode, update the function
	 * pointers in the runtime service table to the new virtual addresses.
	 */

	efi.get_time = (efi_get_time_t *) efi.systab->runtime->get_time;
	efi.set_time = (efi_set_time_t *) efi.systab->runtime->set_time;
	efi.get_wakeup_time = (efi_get_wakeup_time_t *)
					efi.systab->runtime->get_wakeup_time;
	efi.set_wakeup_time = (efi_set_wakeup_time_t *)
					efi.systab->runtime->set_wakeup_time;
	efi.get_variable = (efi_get_variable_t *)
					efi.systab->runtime->get_variable;
	efi.get_next_variable = (efi_get_next_variable_t *)
					efi.systab->runtime->get_next_variable;
	efi.set_variable = (efi_set_variable_t *)
					efi.systab->runtime->set_variable;
	efi.get_next_high_mono_count = (efi_get_next_high_mono_count_t *)
					efi.systab->runtime->get_next_high_mono_count;
	efi.reset_system = (efi_reset_system_t *)
					efi.systab->runtime->reset_system;
}

void __init
efi_initialize_iomem_resources(struct resource *code_resource,
			       struct resource *data_resource)
{
	struct resource *res;
	efi_memory_desc_t *md;
	void *p;

	for (p = memmap.map; p < memmap.map_end; p += memmap.desc_size) {
		md = p;

		if ((md->phys_addr + (md->num_pages << EFI_PAGE_SHIFT)) >
		    0x100000000ULL)
			continue;
		res = kzalloc(sizeof(struct resource), GFP_ATOMIC);
		switch (md->type) {
		case EFI_RESERVED_TYPE:
			res->name = "Reserved Memory";
			break;
		case EFI_LOADER_CODE:
			res->name = "Loader Code";
			break;
		case EFI_LOADER_DATA:
			res->name = "Loader Data";
			break;
		case EFI_BOOT_SERVICES_DATA:
			res->name = "BootServices Data";
			break;
		case EFI_BOOT_SERVICES_CODE:
			res->name = "BootServices Code";
			break;
		case EFI_RUNTIME_SERVICES_CODE:
			res->name = "Runtime Service Code";
			break;
		case EFI_RUNTIME_SERVICES_DATA:
			res->name = "Runtime Service Data";
			break;
		case EFI_CONVENTIONAL_MEMORY:
			res->name = "Conventional Memory";
			break;
		case EFI_UNUSABLE_MEMORY:
			res->name = "Unusable Memory";
			break;
		case EFI_ACPI_RECLAIM_MEMORY:
			res->name = "ACPI Reclaim";
			break;
		case EFI_ACPI_MEMORY_NVS:
			res->name = "ACPI NVS";
			break;
		case EFI_MEMORY_MAPPED_IO:
			res->name = "Memory Mapped IO";
			break;
		case EFI_MEMORY_MAPPED_IO_PORT_SPACE:
			res->name = "Memory Mapped IO Port Space";
			break;
		default:
			res->name = "Reserved";
			break;
		}
		res->start = md->phys_addr;
		res->end = res->start + ((md->num_pages << EFI_PAGE_SHIFT) - 1);
		res->flags = IORESOURCE_MEM | IORESOURCE_BUSY;
		if (request_resource(&iomem_resource, res) < 0)
			printk(KERN_ERR PFX "Failed to allocate res %s : 0x%lx-0x%lx\n",
				res->name, res->start, res->end);
		/*
		 * We don't know which region contains kernel data so we try
		 * it repeatedly and let the resource manager test it.
		 */
		if (md->type == EFI_CONVENTIONAL_MEMORY) {
			request_resource(res, code_resource);
			request_resource(res, data_resource);
#ifdef CONFIG_KEXEC
			request_resource(res, &crashk_res);
#endif
		}
	}
}

/*
 * Convenience functions to obtain memory types and attributes
 */

u32 efi_mem_type(unsigned long phys_addr)
{
	efi_memory_desc_t *md;
	void *p;

	for (p = memmap.map; p < memmap.map_end; p += memmap.desc_size) {
		md = p;
		if ((md->phys_addr <= phys_addr) && (phys_addr <
			(md->phys_addr + (md-> num_pages << EFI_PAGE_SHIFT)) ))
			return md->type;
	}
	return 0;
}

u64 efi_mem_attributes(unsigned long phys_addr)
{
	efi_memory_desc_t *md;
	void *p;

	for (p = memmap.map; p < memmap.map_end; p += memmap.desc_size) {
		md = p;
		if ((md->phys_addr <= phys_addr) && (phys_addr <
			(md->phys_addr + (md-> num_pages << EFI_PAGE_SHIFT)) ))
			return md->attribute;
	}
	return 0;
}
