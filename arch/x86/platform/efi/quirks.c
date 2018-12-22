#define pr_fmt(fmt) "efi: " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/efi.h>
#include <linux/slab.h>
#include <linux/memblock.h>
#include <linux/acpi.h>
#include <linux/dmi.h>

#include <asm/e820/api.h>
#include <asm/efi.h>
#include <asm/uv/uv.h>
#include <asm/cpu_device_id.h>
#include <asm/reboot.h>

#define EFI_MIN_RESERVE 5120

#define EFI_DUMMY_GUID \
	EFI_GUID(0x4424ac57, 0xbe4b, 0x47dd, 0x9e, 0x97, 0xed, 0x50, 0xf0, 0x9f, 0x92, 0xa9)

#define QUARK_CSH_SIGNATURE		0x5f435348	/* _CSH */
#define QUARK_SECURITY_HEADER_SIZE	0x400

/*
 * Header prepended to the standard EFI capsule on Quark systems the are based
 * on Intel firmware BSP.
 * @csh_signature:	Unique identifier to sanity check signed module
 * 			presence ("_CSH").
 * @version:		Current version of CSH used. Should be one for Quark A0.
 * @modulesize:		Size of the entire module including the module header
 * 			and payload.
 * @security_version_number_index: Index of SVN to use for validation of signed
 * 			module.
 * @security_version_number: Used to prevent against roll back of modules.
 * @rsvd_module_id:	Currently unused for Clanton (Quark).
 * @rsvd_module_vendor:	Vendor Identifier. For Intel products value is
 * 			0x00008086.
 * @rsvd_date:		BCD representation of build date as yyyymmdd, where
 * 			yyyy=4 digit year, mm=1-12, dd=1-31.
 * @headersize:		Total length of the header including including any
 * 			padding optionally added by the signing tool.
 * @hash_algo:		What Hash is used in the module signing.
 * @cryp_algo:		What Crypto is used in the module signing.
 * @keysize:		Total length of the key data including including any
 * 			padding optionally added by the signing tool.
 * @signaturesize:	Total length of the signature including including any
 * 			padding optionally added by the signing tool.
 * @rsvd_next_header:	32-bit pointer to the next Secure Boot Module in the
 * 			chain, if there is a next header.
 * @rsvd:		Reserved, padding structure to required size.
 *
 * See also QuartSecurityHeader_t in
 * Quark_EDKII_v1.2.1.1/QuarkPlatformPkg/Include/QuarkBootRom.h
 * from https://downloadcenter.intel.com/download/23197/Intel-Quark-SoC-X1000-Board-Support-Package-BSP
 */
struct quark_security_header {
	u32 csh_signature;
	u32 version;
	u32 modulesize;
	u32 security_version_number_index;
	u32 security_version_number;
	u32 rsvd_module_id;
	u32 rsvd_module_vendor;
	u32 rsvd_date;
	u32 headersize;
	u32 hash_algo;
	u32 cryp_algo;
	u32 keysize;
	u32 signaturesize;
	u32 rsvd_next_header;
	u32 rsvd[2];
};

static const efi_char16_t efi_dummy_name[] = L"DUMMY";

static bool efi_no_storage_paranoia;

/*
 * Some firmware implementations refuse to boot if there's insufficient
 * space in the variable store. The implementation of garbage collection
 * in some FW versions causes stale (deleted) variables to take up space
 * longer than intended and space is only freed once the store becomes
 * almost completely full.
 *
 * Enabling this option disables the space checks in
 * efi_query_variable_store() and forces garbage collection.
 *
 * Only enable this option if deleting EFI variables does not free up
 * space in your variable store, e.g. if despite deleting variables
 * you're unable to create new ones.
 */
static int __init setup_storage_paranoia(char *arg)
{
	efi_no_storage_paranoia = true;
	return 0;
}
early_param("efi_no_storage_paranoia", setup_storage_paranoia);

/*
 * Deleting the dummy variable which kicks off garbage collection
*/
void efi_delete_dummy_variable(void)
{
	efi.set_variable_nonblocking((efi_char16_t *)efi_dummy_name,
				     &EFI_DUMMY_GUID,
				     EFI_VARIABLE_NON_VOLATILE |
				     EFI_VARIABLE_BOOTSERVICE_ACCESS |
				     EFI_VARIABLE_RUNTIME_ACCESS, 0, NULL);
}

/*
 * In the nonblocking case we do not attempt to perform garbage
 * collection if we do not have enough free space. Rather, we do the
 * bare minimum check and give up immediately if the available space
 * is below EFI_MIN_RESERVE.
 *
 * This function is intended to be small and simple because it is
 * invoked from crash handler paths.
 */
static efi_status_t
query_variable_store_nonblocking(u32 attributes, unsigned long size)
{
	efi_status_t status;
	u64 storage_size, remaining_size, max_size;

	status = efi.query_variable_info_nonblocking(attributes, &storage_size,
						     &remaining_size,
						     &max_size);
	if (status != EFI_SUCCESS)
		return status;

	if (remaining_size - size < EFI_MIN_RESERVE)
		return EFI_OUT_OF_RESOURCES;

	return EFI_SUCCESS;
}

/*
 * Some firmware implementations refuse to boot if there's insufficient space
 * in the variable store. Ensure that we never use more than a safe limit.
 *
 * Return EFI_SUCCESS if it is safe to write 'size' bytes to the variable
 * store.
 */
efi_status_t efi_query_variable_store(u32 attributes, unsigned long size,
				      bool nonblocking)
{
	efi_status_t status;
	u64 storage_size, remaining_size, max_size;

	if (!(attributes & EFI_VARIABLE_NON_VOLATILE))
		return 0;

	if (nonblocking)
		return query_variable_store_nonblocking(attributes, size);

	status = efi.query_variable_info(attributes, &storage_size,
					 &remaining_size, &max_size);
	if (status != EFI_SUCCESS)
		return status;

	/*
	 * We account for that by refusing the write if permitting it would
	 * reduce the available space to under 5KB. This figure was provided by
	 * Samsung, so should be safe.
	 */
	if ((remaining_size - size < EFI_MIN_RESERVE) &&
		!efi_no_storage_paranoia) {

		/*
		 * Triggering garbage collection may require that the firmware
		 * generate a real EFI_OUT_OF_RESOURCES error. We can force
		 * that by attempting to use more space than is available.
		 */
		unsigned long dummy_size = remaining_size + 1024;
		void *dummy = kzalloc(dummy_size, GFP_KERNEL);

		if (!dummy)
			return EFI_OUT_OF_RESOURCES;

		status = efi.set_variable((efi_char16_t *)efi_dummy_name,
					  &EFI_DUMMY_GUID,
					  EFI_VARIABLE_NON_VOLATILE |
					  EFI_VARIABLE_BOOTSERVICE_ACCESS |
					  EFI_VARIABLE_RUNTIME_ACCESS,
					  dummy_size, dummy);

		if (status == EFI_SUCCESS) {
			/*
			 * This should have failed, so if it didn't make sure
			 * that we delete it...
			 */
			efi_delete_dummy_variable();
		}

		kfree(dummy);

		/*
		 * The runtime code may now have triggered a garbage collection
		 * run, so check the variable info again
		 */
		status = efi.query_variable_info(attributes, &storage_size,
						 &remaining_size, &max_size);

		if (status != EFI_SUCCESS)
			return status;

		/*
		 * There still isn't enough room, so return an error
		 */
		if (remaining_size - size < EFI_MIN_RESERVE)
			return EFI_OUT_OF_RESOURCES;
	}

	return EFI_SUCCESS;
}
EXPORT_SYMBOL_GPL(efi_query_variable_store);

/*
 * The UEFI specification makes it clear that the operating system is
 * free to do whatever it wants with boot services code after
 * ExitBootServices() has been called. Ignoring this recommendation a
 * significant bunch of EFI implementations continue calling into boot
 * services code (SetVirtualAddressMap). In order to work around such
 * buggy implementations we reserve boot services region during EFI
 * init and make sure it stays executable. Then, after
 * SetVirtualAddressMap(), it is discarded.
 *
 * However, some boot services regions contain data that is required
 * by drivers, so we need to track which memory ranges can never be
 * freed. This is done by tagging those regions with the
 * EFI_MEMORY_RUNTIME attribute.
 *
 * Any driver that wants to mark a region as reserved must use
 * efi_mem_reserve() which will insert a new EFI memory descriptor
 * into efi.memmap (splitting existing regions if necessary) and tag
 * it with EFI_MEMORY_RUNTIME.
 */
void __init efi_arch_mem_reserve(phys_addr_t addr, u64 size)
{
	phys_addr_t new_phys, new_size;
	struct efi_mem_range mr;
	efi_memory_desc_t md;
	int num_entries;
	void *new;

	if (efi_mem_desc_lookup(addr, &md) ||
	    md.type != EFI_BOOT_SERVICES_DATA) {
		pr_err("Failed to lookup EFI memory descriptor for %pa\n", &addr);
		return;
	}

	if (addr + size > md.phys_addr + (md.num_pages << EFI_PAGE_SHIFT)) {
		pr_err("Region spans EFI memory descriptors, %pa\n", &addr);
		return;
	}

	/* No need to reserve regions that will never be freed. */
	if (md.attribute & EFI_MEMORY_RUNTIME)
		return;

	size += addr % EFI_PAGE_SIZE;
	size = round_up(size, EFI_PAGE_SIZE);
	addr = round_down(addr, EFI_PAGE_SIZE);

	mr.range.start = addr;
	mr.range.end = addr + size - 1;
	mr.attribute = md.attribute | EFI_MEMORY_RUNTIME;

	num_entries = efi_memmap_split_count(&md, &mr.range);
	num_entries += efi.memmap.nr_map;

	new_size = efi.memmap.desc_size * num_entries;

	new_phys = efi_memmap_alloc(num_entries);
	if (!new_phys) {
		pr_err("Could not allocate boot services memmap\n");
		return;
	}

	new = early_memremap(new_phys, new_size);
	if (!new) {
		pr_err("Failed to map new boot services memmap\n");
		return;
	}

	efi_memmap_insert(&efi.memmap, new, &mr);
	early_memunmap(new, new_size);

	efi_memmap_install(new_phys, num_entries);
}

/*
 * Helper function for efi_reserve_boot_services() to figure out if we
 * can free regions in efi_free_boot_services().
 *
 * Use this function to ensure we do not free regions owned by somebody
 * else. We must only reserve (and then free) regions:
 *
 * - Not within any part of the kernel
 * - Not the BIOS reserved area (E820_TYPE_RESERVED, E820_TYPE_NVS, etc)
 */
static bool can_free_region(u64 start, u64 size)
{
	if (start + size > __pa_symbol(_text) && start <= __pa_symbol(_end))
		return false;

	if (!e820__mapped_all(start, start+size, E820_TYPE_RAM))
		return false;

	return true;
}

void __init efi_reserve_boot_services(void)
{
	efi_memory_desc_t *md;

	for_each_efi_memory_desc(md) {
		u64 start = md->phys_addr;
		u64 size = md->num_pages << EFI_PAGE_SHIFT;
		bool already_reserved;

		if (md->type != EFI_BOOT_SERVICES_CODE &&
		    md->type != EFI_BOOT_SERVICES_DATA)
			continue;

		already_reserved = memblock_is_region_reserved(start, size);

		/*
		 * Because the following memblock_reserve() is paired
		 * with memblock_free_late() for this region in
		 * efi_free_boot_services(), we must be extremely
		 * careful not to reserve, and subsequently free,
		 * critical regions of memory (like the kernel image) or
		 * those regions that somebody else has already
		 * reserved.
		 *
		 * A good example of a critical region that must not be
		 * freed is page zero (first 4Kb of memory), which may
		 * contain boot services code/data but is marked
		 * E820_TYPE_RESERVED by trim_bios_range().
		 */
		if (!already_reserved) {
			memblock_reserve(start, size);

			/*
			 * If we are the first to reserve the region, no
			 * one else cares about it. We own it and can
			 * free it later.
			 */
			if (can_free_region(start, size))
				continue;
		}

		/*
		 * We don't own the region. We must not free it.
		 *
		 * Setting this bit for a boot services region really
		 * doesn't make sense as far as the firmware is
		 * concerned, but it does provide us with a way to tag
		 * those regions that must not be paired with
		 * memblock_free_late().
		 */
		md->attribute |= EFI_MEMORY_RUNTIME;
	}
}

/*
 * Apart from having VA mappings for EFI boot services code/data regions,
 * (duplicate) 1:1 mappings were also created as a quirk for buggy firmware. So,
 * unmap both 1:1 and VA mappings.
 */
static void __init efi_unmap_pages(efi_memory_desc_t *md)
{
	pgd_t *pgd = efi_mm.pgd;
	u64 pa = md->phys_addr;
	u64 va = md->virt_addr;

	/*
	 * To Do: Remove this check after adding functionality to unmap EFI boot
	 * services code/data regions from direct mapping area because
	 * "efi=old_map" maps EFI regions in swapper_pg_dir.
	 */
	if (efi_enabled(EFI_OLD_MEMMAP))
		return;

	/*
	 * EFI mixed mode has all RAM mapped to access arguments while making
	 * EFI runtime calls, hence don't unmap EFI boot services code/data
	 * regions.
	 */
	if (!efi_is_native())
		return;

	if (kernel_unmap_pages_in_pgd(pgd, pa, md->num_pages))
		pr_err("Failed to unmap 1:1 mapping for 0x%llx\n", pa);

	if (kernel_unmap_pages_in_pgd(pgd, va, md->num_pages))
		pr_err("Failed to unmap VA mapping for 0x%llx\n", va);
}

void __init efi_free_boot_services(void)
{
	phys_addr_t new_phys, new_size;
	efi_memory_desc_t *md;
	int num_entries = 0;
	void *new, *new_md;

	for_each_efi_memory_desc(md) {
		unsigned long long start = md->phys_addr;
		unsigned long long size = md->num_pages << EFI_PAGE_SHIFT;
		size_t rm_size;

		if (md->type != EFI_BOOT_SERVICES_CODE &&
		    md->type != EFI_BOOT_SERVICES_DATA) {
			num_entries++;
			continue;
		}

		/* Do not free, someone else owns it: */
		if (md->attribute & EFI_MEMORY_RUNTIME) {
			num_entries++;
			continue;
		}

		/*
		 * Before calling set_virtual_address_map(), EFI boot services
		 * code/data regions were mapped as a quirk for buggy firmware.
		 * Unmap them from efi_pgd before freeing them up.
		 */
		efi_unmap_pages(md);

		/*
		 * Nasty quirk: if all sub-1MB memory is used for boot
		 * services, we can get here without having allocated the
		 * real mode trampoline.  It's too late to hand boot services
		 * memory back to the memblock allocator, so instead
		 * try to manually allocate the trampoline if needed.
		 *
		 * I've seen this on a Dell XPS 13 9350 with firmware
		 * 1.4.4 with SGX enabled booting Linux via Fedora 24's
		 * grub2-efi on a hard disk.  (And no, I don't know why
		 * this happened, but Linux should still try to boot rather
		 * panicing early.)
		 */
		rm_size = real_mode_size_needed();
		if (rm_size && (start + rm_size) < (1<<20) && size >= rm_size) {
			set_real_mode_mem(start, rm_size);
			start += rm_size;
			size -= rm_size;
		}

		memblock_free_late(start, size);
	}

	if (!num_entries)
		return;

	new_size = efi.memmap.desc_size * num_entries;
	new_phys = efi_memmap_alloc(num_entries);
	if (!new_phys) {
		pr_err("Failed to allocate new EFI memmap\n");
		return;
	}

	new = memremap(new_phys, new_size, MEMREMAP_WB);
	if (!new) {
		pr_err("Failed to map new EFI memmap\n");
		return;
	}

	/*
	 * Build a new EFI memmap that excludes any boot services
	 * regions that are not tagged EFI_MEMORY_RUNTIME, since those
	 * regions have now been freed.
	 */
	new_md = new;
	for_each_efi_memory_desc(md) {
		if (!(md->attribute & EFI_MEMORY_RUNTIME) &&
		    (md->type == EFI_BOOT_SERVICES_CODE ||
		     md->type == EFI_BOOT_SERVICES_DATA))
			continue;

		memcpy(new_md, md, efi.memmap.desc_size);
		new_md += efi.memmap.desc_size;
	}

	memunmap(new);

	if (efi_memmap_install(new_phys, num_entries)) {
		pr_err("Could not install new EFI memmap\n");
		return;
	}
}

/*
 * A number of config table entries get remapped to virtual addresses
 * after entering EFI virtual mode. However, the kexec kernel requires
 * their physical addresses therefore we pass them via setup_data and
 * correct those entries to their respective physical addresses here.
 *
 * Currently only handles smbios which is necessary for some firmware
 * implementation.
 */
int __init efi_reuse_config(u64 tables, int nr_tables)
{
	int i, sz, ret = 0;
	void *p, *tablep;
	struct efi_setup_data *data;

	if (!efi_setup)
		return 0;

	if (!efi_enabled(EFI_64BIT))
		return 0;

	data = early_memremap(efi_setup, sizeof(*data));
	if (!data) {
		ret = -ENOMEM;
		goto out;
	}

	if (!data->smbios)
		goto out_memremap;

	sz = sizeof(efi_config_table_64_t);

	p = tablep = early_memremap(tables, nr_tables * sz);
	if (!p) {
		pr_err("Could not map Configuration table!\n");
		ret = -ENOMEM;
		goto out_memremap;
	}

	for (i = 0; i < efi.systab->nr_tables; i++) {
		efi_guid_t guid;

		guid = ((efi_config_table_64_t *)p)->guid;

		if (!efi_guidcmp(guid, SMBIOS_TABLE_GUID))
			((efi_config_table_64_t *)p)->table = data->smbios;
		p += sz;
	}
	early_memunmap(tablep, nr_tables * sz);

out_memremap:
	early_memunmap(data, sizeof(*data));
out:
	return ret;
}

static const struct dmi_system_id sgi_uv1_dmi[] = {
	{ NULL, "SGI UV1",
		{	DMI_MATCH(DMI_PRODUCT_NAME,	"Stoutland Platform"),
			DMI_MATCH(DMI_PRODUCT_VERSION,	"1.0"),
			DMI_MATCH(DMI_BIOS_VENDOR,	"SGI.COM"),
		}
	},
	{ } /* NULL entry stops DMI scanning */
};

void __init efi_apply_memmap_quirks(void)
{
	/*
	 * Once setup is done earlier, unmap the EFI memory map on mismatched
	 * firmware/kernel architectures since there is no support for runtime
	 * services.
	 */
	if (!efi_runtime_supported()) {
		pr_info("Setup done, disabling due to 32/64-bit mismatch\n");
		efi_memmap_unmap();
	}

	/* UV2+ BIOS has a fix for this issue.  UV1 still needs the quirk. */
	if (dmi_check_system(sgi_uv1_dmi))
		set_bit(EFI_OLD_MEMMAP, &efi.flags);
}

/*
 * For most modern platforms the preferred method of powering off is via
 * ACPI. However, there are some that are known to require the use of
 * EFI runtime services and for which ACPI does not work at all.
 *
 * Using EFI is a last resort, to be used only if no other option
 * exists.
 */
bool efi_reboot_required(void)
{
	if (!acpi_gbl_reduced_hardware)
		return false;

	efi_reboot_quirk_mode = EFI_RESET_WARM;
	return true;
}

bool efi_poweroff_required(void)
{
	return acpi_gbl_reduced_hardware || acpi_no_s5;
}

#ifdef CONFIG_EFI_CAPSULE_QUIRK_QUARK_CSH

static int qrk_capsule_setup_info(struct capsule_info *cap_info, void **pkbuff,
				  size_t hdr_bytes)
{
	struct quark_security_header *csh = *pkbuff;

	/* Only process data block that is larger than the security header */
	if (hdr_bytes < sizeof(struct quark_security_header))
		return 0;

	if (csh->csh_signature != QUARK_CSH_SIGNATURE ||
	    csh->headersize != QUARK_SECURITY_HEADER_SIZE)
		return 1;

	/* Only process data block if EFI header is included */
	if (hdr_bytes < QUARK_SECURITY_HEADER_SIZE +
			sizeof(efi_capsule_header_t))
		return 0;

	pr_debug("Quark security header detected\n");

	if (csh->rsvd_next_header != 0) {
		pr_err("multiple Quark security headers not supported\n");
		return -EINVAL;
	}

	*pkbuff += csh->headersize;
	cap_info->total_size = csh->headersize;

	/*
	 * Update the first page pointer to skip over the CSH header.
	 */
	cap_info->phys[0] += csh->headersize;

	/*
	 * cap_info->capsule should point at a virtual mapping of the entire
	 * capsule, starting at the capsule header. Our image has the Quark
	 * security header prepended, so we cannot rely on the default vmap()
	 * mapping created by the generic capsule code.
	 * Given that the Quark firmware does not appear to care about the
	 * virtual mapping, let's just point cap_info->capsule at our copy
	 * of the capsule header.
	 */
	cap_info->capsule = &cap_info->header;

	return 1;
}

#define ICPU(family, model, quirk_handler) \
	{ X86_VENDOR_INTEL, family, model, X86_FEATURE_ANY, \
	  (unsigned long)&quirk_handler }

static const struct x86_cpu_id efi_capsule_quirk_ids[] = {
	ICPU(5, 9, qrk_capsule_setup_info),	/* Intel Quark X1000 */
	{ }
};

int efi_capsule_setup_info(struct capsule_info *cap_info, void *kbuff,
			   size_t hdr_bytes)
{
	int (*quirk_handler)(struct capsule_info *, void **, size_t);
	const struct x86_cpu_id *id;
	int ret;

	if (hdr_bytes < sizeof(efi_capsule_header_t))
		return 0;

	cap_info->total_size = 0;

	id = x86_match_cpu(efi_capsule_quirk_ids);
	if (id) {
		/*
		 * The quirk handler is supposed to return
		 *  - a value > 0 if the setup should continue, after advancing
		 *    kbuff as needed
		 *  - 0 if not enough hdr_bytes are available yet
		 *  - a negative error code otherwise
		 */
		quirk_handler = (typeof(quirk_handler))id->driver_data;
		ret = quirk_handler(cap_info, &kbuff, hdr_bytes);
		if (ret <= 0)
			return ret;
	}

	memcpy(&cap_info->header, kbuff, sizeof(cap_info->header));

	cap_info->total_size += cap_info->header.imagesize;

	return __efi_capsule_setup_info(cap_info);
}

#endif

/*
 * If any access by any efi runtime service causes a page fault, then,
 * 1. If it's efi_reset_system(), reboot through BIOS.
 * 2. If any other efi runtime service, then
 *    a. Return error status to the efi caller process.
 *    b. Disable EFI Runtime Services forever and
 *    c. Freeze efi_rts_wq and schedule new process.
 *
 * @return: Returns, if the page fault is not handled. This function
 * will never return if the page fault is handled successfully.
 */
void efi_recover_from_page_fault(unsigned long phys_addr)
{
	if (!IS_ENABLED(CONFIG_X86_64))
		return;

	/*
	 * Make sure that an efi runtime service caused the page fault.
	 * "efi_mm" cannot be used to check if the page fault had occurred
	 * in the firmware context because efi=old_map doesn't use efi_pgd.
	 */
	if (efi_rts_work.efi_rts_id == NONE)
		return;

	/*
	 * Address range 0x0000 - 0x0fff is always mapped in the efi_pgd, so
	 * page faulting on these addresses isn't expected.
	 */
	if (phys_addr >= 0x0000 && phys_addr <= 0x0fff)
		return;

	/*
	 * Print stack trace as it might be useful to know which EFI Runtime
	 * Service is buggy.
	 */
	WARN(1, FW_BUG "Page fault caused by firmware at PA: 0x%lx\n",
	     phys_addr);

	/*
	 * Buggy efi_reset_system() is handled differently from other EFI
	 * Runtime Services as it doesn't use efi_rts_wq. Although,
	 * native_machine_emergency_restart() says that machine_real_restart()
	 * could fail, it's better not to compilcate this fault handler
	 * because this case occurs *very* rarely and hence could be improved
	 * on a need by basis.
	 */
	if (efi_rts_work.efi_rts_id == RESET_SYSTEM) {
		pr_info("efi_reset_system() buggy! Reboot through BIOS\n");
		machine_real_restart(MRR_BIOS);
		return;
	}

	/*
	 * Before calling EFI Runtime Service, the kernel has switched the
	 * calling process to efi_mm. Hence, switch back to task_mm.
	 */
	arch_efi_call_virt_teardown();

	/* Signal error status to the efi caller process */
	efi_rts_work.status = EFI_ABORTED;
	complete(&efi_rts_work.efi_rts_comp);

	clear_bit(EFI_RUNTIME_SERVICES, &efi.flags);
	pr_info("Froze efi_rts_wq and disabled EFI Runtime Services\n");

	/*
	 * Call schedule() in an infinite loop, so that any spurious wake ups
	 * will never run efi_rts_wq again.
	 */
	for (;;) {
		set_current_state(TASK_IDLE);
		schedule();
	}

	return;
}
