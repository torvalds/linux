#define pr_fmt(fmt) "efi: " fmt

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/time.h>
#include <linux/types.h>
#include <linux/efi.h>
#include <linux/slab.h>
#include <linux/memblock.h>
#include <linux/bootmem.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <asm/efi.h>
#include <asm/uv/uv.h>

#define EFI_MIN_RESERVE 5120

#define EFI_DUMMY_GUID \
	EFI_GUID(0x4424ac57, 0xbe4b, 0x47dd, 0x9e, 0x97, 0xed, 0x50, 0xf0, 0x9f, 0x92, 0xa9)

static efi_char16_t efi_dummy_name[6] = { 'D', 'U', 'M', 'M', 'Y', 0 };

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
	efi.set_variable(efi_dummy_name, &EFI_DUMMY_GUID,
			 EFI_VARIABLE_NON_VOLATILE |
			 EFI_VARIABLE_BOOTSERVICE_ACCESS |
			 EFI_VARIABLE_RUNTIME_ACCESS,
			 0, NULL);
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
		void *dummy = kzalloc(dummy_size, GFP_ATOMIC);

		if (!dummy)
			return EFI_OUT_OF_RESOURCES;

		status = efi.set_variable(efi_dummy_name, &EFI_DUMMY_GUID,
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
 * Helper function for efi_reserve_boot_services() to figure out if we
 * can free regions in efi_free_boot_services().
 *
 * Use this function to ensure we do not free regions owned by somebody
 * else. We must only reserve (and then free) regions:
 *
 * - Not within any part of the kernel
 * - Not the BIOS reserved area (E820_RESERVED, E820_NVS, etc)
 */
static bool can_free_region(u64 start, u64 size)
{
	if (start + size > __pa_symbol(_text) && start <= __pa_symbol(_end))
		return false;

	if (!e820_all_mapped(start, start+size, E820_RAM))
		return false;

	return true;
}

/*
 * The UEFI specification makes it clear that the operating system is free to do
 * whatever it wants with boot services code after ExitBootServices() has been
 * called. Ignoring this recommendation a significant bunch of EFI implementations 
 * continue calling into boot services code (SetVirtualAddressMap). In order to 
 * work around such buggy implementations we reserve boot services region during 
 * EFI init and make sure it stays executable. Then, after SetVirtualAddressMap(), it
* is discarded.
*/
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
		 * with free_bootmem_late() for this region in
		 * efi_free_boot_services(), we must be extremely
		 * careful not to reserve, and subsequently free,
		 * critical regions of memory (like the kernel image) or
		 * those regions that somebody else has already
		 * reserved.
		 *
		 * A good example of a critical region that must not be
		 * freed is page zero (first 4Kb of memory), which may
		 * contain boot services code/data but is marked
		 * E820_RESERVED by trim_bios_range().
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
		 * free_bootmem_late().
		 */
		md->attribute |= EFI_MEMORY_RUNTIME;
	}
}

void __init efi_free_boot_services(void)
{
	efi_memory_desc_t *md;

	for_each_efi_memory_desc(md) {
		unsigned long long start = md->phys_addr;
		unsigned long long size = md->num_pages << EFI_PAGE_SHIFT;

		if (md->type != EFI_BOOT_SERVICES_CODE &&
		    md->type != EFI_BOOT_SERVICES_DATA)
			continue;

		/* Do not free, someone else owns it: */
		if (md->attribute & EFI_MEMORY_RUNTIME)
			continue;

		free_bootmem_late(start, size);
	}

	efi_unmap_memmap();
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
		efi_unmap_memmap();
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
