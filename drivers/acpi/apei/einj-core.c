// SPDX-License-Identifier: GPL-2.0-only
/*
 * APEI Error INJection support
 *
 * EINJ provides a hardware error injection mechanism, this is useful
 * for debugging and testing of other APEI and RAS features.
 *
 * For more information about EINJ, please refer to ACPI Specification
 * version 4.0, section 17.5.
 *
 * Copyright 2009-2010 Intel Corp.
 *   Author: Huang Ying <ying.huang@intel.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/nmi.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/device/faux.h>
#include <linux/unaligned.h>

#include "apei-internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "EINJ: " fmt

#define SLEEP_UNIT_MIN		1000			/* 1ms */
#define SLEEP_UNIT_MAX		5000			/* 5ms */
/* Firmware should respond within 1 seconds */
#define FIRMWARE_TIMEOUT	(1 * USEC_PER_SEC)
#define COMPONENT_LEN		16
#define ACPI65_EINJV2_SUPP	BIT(30)
#define ACPI5_VENDOR_BIT	BIT(31)
#define MEM_ERROR_MASK		(ACPI_EINJ_MEMORY_CORRECTABLE | \
				ACPI_EINJ_MEMORY_UNCORRECTABLE | \
				ACPI_EINJ_MEMORY_FATAL)
#define CXL_ERROR_MASK		(ACPI_EINJ_CXL_CACHE_CORRECTABLE | \
				ACPI_EINJ_CXL_CACHE_UNCORRECTABLE | \
				ACPI_EINJ_CXL_CACHE_FATAL | \
				ACPI_EINJ_CXL_MEM_CORRECTABLE | \
				ACPI_EINJ_CXL_MEM_UNCORRECTABLE | \
				ACPI_EINJ_CXL_MEM_FATAL)

/*
 * ACPI version 5 provides a SET_ERROR_TYPE_WITH_ADDRESS action.
 */
static int acpi5;

struct syndrome_array {
	union {
		u8	acpi_id[COMPONENT_LEN];
		u8	device_id[COMPONENT_LEN];
		u8	pcie_sbdf[COMPONENT_LEN];
		u8	vendor_id[COMPONENT_LEN];
	} comp_id;
	union {
		u8	proc_synd[COMPONENT_LEN];
		u8	mem_synd[COMPONENT_LEN];
		u8	pcie_synd[COMPONENT_LEN];
		u8	vendor_synd[COMPONENT_LEN];
	} comp_synd;
};

struct einjv2_extension_struct {
	u32 length;
	u16 revision;
	u16 component_arr_count;
	struct syndrome_array component_arr[] __counted_by(component_arr_count);
};

struct set_error_type_with_address {
	u32	type;
	u32	vendor_extension;
	u32	flags;
	u32	apicid;
	u64	memory_address;
	u64	memory_address_range;
	u32	pcie_sbdf;
	struct	einjv2_extension_struct einjv2_struct;
};
enum {
	SETWA_FLAGS_APICID = 1,
	SETWA_FLAGS_MEM = 2,
	SETWA_FLAGS_PCIE_SBDF = 4,
	SETWA_FLAGS_EINJV2 = 8,
};

/*
 * Vendor extensions for platform specific operations
 */
struct vendor_error_type_extension {
	u32	length;
	u32	pcie_sbdf;
	u16	vendor_id;
	u16	device_id;
	u8	rev_id;
	u8	reserved[3];
};

static u32 notrigger;

static u32 vendor_flags;
static struct debugfs_blob_wrapper vendor_blob;
static struct debugfs_blob_wrapper vendor_errors;
static char vendor_dev[64];

static u32 max_nr_components;
static u32 available_error_type;
static u32 available_error_type_v2;
static struct syndrome_array *syndrome_data;

/*
 * Some BIOSes allow parameters to the SET_ERROR_TYPE entries in the
 * EINJ table through an unpublished extension. Use with caution as
 * most will ignore the parameter and make their own choice of address
 * for error injection.  This extension is used only if
 * param_extension module parameter is specified.
 */
struct einj_parameter {
	u64 type;
	u64 reserved1;
	u64 reserved2;
	u64 param1;
	u64 param2;
};

#define EINJ_OP_BUSY			0x1
#define EINJ_STATUS_SUCCESS		0x0
#define EINJ_STATUS_FAIL		0x1
#define EINJ_STATUS_INVAL		0x2

#define EINJ_TAB_ENTRY(tab)						\
	((struct acpi_whea_header *)((char *)(tab) +			\
				    sizeof(struct acpi_table_einj)))

static bool param_extension;
module_param(param_extension, bool, 0);

static struct acpi_table_einj *einj_tab;

static struct apei_resources einj_resources;

static struct apei_exec_ins_type einj_ins_type[] = {
	[ACPI_EINJ_READ_REGISTER] = {
		.flags = APEI_EXEC_INS_ACCESS_REGISTER,
		.run   = apei_exec_read_register,
	},
	[ACPI_EINJ_READ_REGISTER_VALUE] = {
		.flags = APEI_EXEC_INS_ACCESS_REGISTER,
		.run   = apei_exec_read_register_value,
	},
	[ACPI_EINJ_WRITE_REGISTER] = {
		.flags = APEI_EXEC_INS_ACCESS_REGISTER,
		.run   = apei_exec_write_register,
	},
	[ACPI_EINJ_WRITE_REGISTER_VALUE] = {
		.flags = APEI_EXEC_INS_ACCESS_REGISTER,
		.run   = apei_exec_write_register_value,
	},
	[ACPI_EINJ_NOOP] = {
		.flags = 0,
		.run   = apei_exec_noop,
	},
};

/*
 * Prevent EINJ interpreter to run simultaneously, because the
 * corresponding firmware implementation may not work properly when
 * invoked simultaneously.
 */
static DEFINE_MUTEX(einj_mutex);

/*
 * Exported APIs use this flag to exit early if einj_probe() failed.
 */
bool einj_initialized __ro_after_init;

static void __iomem *einj_param;
static u32 v5param_size;
static u32 v66param_size;
static bool is_v2;

static void einj_exec_ctx_init(struct apei_exec_context *ctx)
{
	apei_exec_ctx_init(ctx, einj_ins_type, ARRAY_SIZE(einj_ins_type),
			   EINJ_TAB_ENTRY(einj_tab), einj_tab->entries);
}

static int __einj_get_available_error_type(u32 *type, int einj_action)
{
	struct apei_exec_context ctx;
	int rc;

	einj_exec_ctx_init(&ctx);
	rc = apei_exec_run(&ctx, einj_action);
	if (rc)
		return rc;
	*type = apei_exec_ctx_get_output(&ctx);

	return 0;
}

/* Get error injection capabilities of the platform */
int einj_get_available_error_type(u32 *type, int einj_action)
{
	int rc;

	mutex_lock(&einj_mutex);
	rc = __einj_get_available_error_type(type, einj_action);
	mutex_unlock(&einj_mutex);

	return rc;
}

static int einj_get_available_error_types(u32 *type1, u32 *type2)
{
	int rc;

	rc = einj_get_available_error_type(type1, ACPI_EINJ_GET_ERROR_TYPE);
	if (rc)
		return rc;
	if (*type1 & ACPI65_EINJV2_SUPP) {
		rc = einj_get_available_error_type(type2,
						   ACPI_EINJV2_GET_ERROR_TYPE);
		if (rc)
			return rc;
	}

	return 0;
}

static int einj_timedout(u64 *t)
{
	if ((s64)*t < SLEEP_UNIT_MIN) {
		pr_warn(FW_WARN "Firmware does not respond in time\n");
		return 1;
	}
	*t -= SLEEP_UNIT_MIN;
	usleep_range(SLEEP_UNIT_MIN, SLEEP_UNIT_MAX);

	return 0;
}

static void get_oem_vendor_struct(u64 paddr, int offset,
				  struct vendor_error_type_extension *v)
{
	unsigned long vendor_size;
	u64 target_pa = paddr + offset + sizeof(struct vendor_error_type_extension);

	vendor_size = v->length - sizeof(struct vendor_error_type_extension);

	if (vendor_size)
		vendor_errors.data = acpi_os_map_memory(target_pa, vendor_size);

	if (vendor_errors.data)
		vendor_errors.size = vendor_size;
}

static void check_vendor_extension(u64 paddr,
				   struct set_error_type_with_address *v5param)
{
	int	offset = v5param->vendor_extension;
	struct	vendor_error_type_extension v;
	struct vendor_error_type_extension __iomem *p;
	u32	sbdf;

	if (!offset)
		return;
	p = acpi_os_map_iomem(paddr + offset, sizeof(*p));
	if (!p)
		return;
	memcpy_fromio(&v, p, sizeof(v));
	get_oem_vendor_struct(paddr, offset, &v);
	sbdf = v.pcie_sbdf;
	sprintf(vendor_dev, "%x:%x:%x.%x vendor_id=%x device_id=%x rev_id=%x\n",
		sbdf >> 24, (sbdf >> 16) & 0xff,
		(sbdf >> 11) & 0x1f, (sbdf >> 8) & 0x7,
		 v.vendor_id, v.device_id, v.rev_id);
	acpi_os_unmap_iomem(p, sizeof(v));
}

static u32 einjv2_init(struct einjv2_extension_struct *e)
{
	if (e->revision != 1) {
		pr_info("Unknown v2 extension revision %u\n", e->revision);
		return 0;
	}
	if (e->length < sizeof(*e) || e->length > PAGE_SIZE) {
		pr_info(FW_BUG "Bad1 v2 extension length %u\n", e->length);
		return 0;
	}
	if ((e->length - sizeof(*e)) % sizeof(e->component_arr[0])) {
		pr_info(FW_BUG "Bad2 v2 extension length %u\n", e->length);
		return 0;
	}

	return (e->length - sizeof(*e)) / sizeof(e->component_arr[0]);
}

static void __iomem *einj_get_parameter_address(void)
{
	int i;
	u64 pa_v4 = 0, pa_v5 = 0;
	struct acpi_whea_header *entry;

	entry = EINJ_TAB_ENTRY(einj_tab);
	for (i = 0; i < einj_tab->entries; i++) {
		if (entry->action == ACPI_EINJ_SET_ERROR_TYPE &&
		    entry->instruction == ACPI_EINJ_WRITE_REGISTER &&
		    entry->register_region.space_id ==
		    ACPI_ADR_SPACE_SYSTEM_MEMORY)
			pa_v4 = get_unaligned(&entry->register_region.address);
		if (entry->action == ACPI_EINJ_SET_ERROR_TYPE_WITH_ADDRESS &&
		    entry->instruction == ACPI_EINJ_WRITE_REGISTER &&
		    entry->register_region.space_id ==
		    ACPI_ADR_SPACE_SYSTEM_MEMORY)
			pa_v5 = get_unaligned(&entry->register_region.address);
		entry++;
	}
	if (pa_v5) {
		struct set_error_type_with_address v5param;
		struct set_error_type_with_address __iomem *p;

		v5param_size = sizeof(v5param);
		p = acpi_os_map_iomem(pa_v5, sizeof(*p));
		if (p) {
			memcpy_fromio(&v5param, p, v5param_size);
			acpi5 = 1;
			check_vendor_extension(pa_v5, &v5param);
			if (available_error_type & ACPI65_EINJV2_SUPP) {
				struct einjv2_extension_struct *e;

				e = &v5param.einjv2_struct;
				max_nr_components = einjv2_init(e);

				/* remap including einjv2_extension_struct */
				acpi_os_unmap_iomem(p, v5param_size);
				v66param_size = v5param_size - sizeof(*e) + e->length;
				p = acpi_os_map_iomem(pa_v5, v66param_size);
			}

			return p;
		}
	}
	if (param_extension && pa_v4) {
		struct einj_parameter v4param;
		struct einj_parameter __iomem *p;

		p = acpi_os_map_iomem(pa_v4, sizeof(*p));
		if (!p)
			return NULL;
		memcpy_fromio(&v4param, p, sizeof(v4param));
		if (v4param.reserved1 || v4param.reserved2) {
			acpi_os_unmap_iomem(p, sizeof(v4param));
			return NULL;
		}
		return p;
	}

	return NULL;
}

/* do sanity check to trigger table */
static int einj_check_trigger_header(struct acpi_einj_trigger *trigger_tab)
{
	if (trigger_tab->header_size != sizeof(struct acpi_einj_trigger))
		return -EINVAL;
	if (trigger_tab->table_size > PAGE_SIZE ||
	    trigger_tab->table_size < trigger_tab->header_size)
		return -EINVAL;
	if (trigger_tab->entry_count !=
	    (trigger_tab->table_size - trigger_tab->header_size) /
	    sizeof(struct acpi_einj_entry))
		return -EINVAL;

	return 0;
}

static struct acpi_generic_address *einj_get_trigger_parameter_region(
	struct acpi_einj_trigger *trigger_tab, u64 param1, u64 param2)
{
	int i;
	struct acpi_whea_header *entry;

	entry = (struct acpi_whea_header *)
		((char *)trigger_tab + sizeof(struct acpi_einj_trigger));
	for (i = 0; i < trigger_tab->entry_count; i++) {
		if (entry->action == ACPI_EINJ_TRIGGER_ERROR &&
		entry->instruction <= ACPI_EINJ_WRITE_REGISTER_VALUE &&
		entry->register_region.space_id ==
			ACPI_ADR_SPACE_SYSTEM_MEMORY &&
		(entry->register_region.address & param2) == (param1 & param2))
			return &entry->register_region;
		entry++;
	}

	return NULL;
}
/* Execute instructions in trigger error action table */
static int __einj_error_trigger(u64 trigger_paddr, u32 type,
				u64 param1, u64 param2)
{
	struct acpi_einj_trigger trigger_tab;
	struct acpi_einj_trigger *full_trigger_tab;
	struct apei_exec_context trigger_ctx;
	struct apei_resources trigger_resources;
	struct acpi_whea_header *trigger_entry;
	struct resource *r;
	u32 table_size;
	int rc = -EIO;
	struct acpi_generic_address *trigger_param_region = NULL;
	struct acpi_einj_trigger __iomem *p = NULL;

	r = request_mem_region(trigger_paddr, sizeof(trigger_tab),
			       "APEI EINJ Trigger Table");
	if (!r) {
		pr_err("Can not request [mem %#010llx-%#010llx] for Trigger table\n",
		       (unsigned long long)trigger_paddr,
		       (unsigned long long)trigger_paddr +
			    sizeof(trigger_tab) - 1);
		goto out;
	}
	p = ioremap_cache(trigger_paddr, sizeof(*p));
	if (!p) {
		pr_err("Failed to map trigger table!\n");
		goto out_rel_header;
	}
	memcpy_fromio(&trigger_tab, p, sizeof(trigger_tab));
	rc = einj_check_trigger_header(&trigger_tab);
	if (rc) {
		pr_warn(FW_BUG "Invalid trigger error action table.\n");
		goto out_rel_header;
	}

	/* No action structures in the TRIGGER_ERROR table, nothing to do */
	if (!trigger_tab.entry_count)
		goto out_rel_header;

	rc = -EIO;
	table_size = trigger_tab.table_size;
	full_trigger_tab = kmalloc(table_size, GFP_KERNEL);
	if (!full_trigger_tab)
		goto out_rel_header;
	r = request_mem_region(trigger_paddr + sizeof(trigger_tab),
			       table_size - sizeof(trigger_tab),
			       "APEI EINJ Trigger Table");
	if (!r) {
		pr_err("Can not request [mem %#010llx-%#010llx] for Trigger Table Entry\n",
		       (unsigned long long)trigger_paddr + sizeof(trigger_tab),
		       (unsigned long long)trigger_paddr + table_size - 1);
		goto out_free_trigger_tab;
	}
	iounmap(p);
	p = ioremap_cache(trigger_paddr, table_size);
	if (!p) {
		pr_err("Failed to map trigger table!\n");
		goto out_rel_entry;
	}
	memcpy_fromio(full_trigger_tab, p, table_size);
	trigger_entry = (struct acpi_whea_header *)
		((char *)full_trigger_tab + sizeof(struct acpi_einj_trigger));
	apei_resources_init(&trigger_resources);
	apei_exec_ctx_init(&trigger_ctx, einj_ins_type,
			   ARRAY_SIZE(einj_ins_type),
			   trigger_entry, trigger_tab.entry_count);
	rc = apei_exec_collect_resources(&trigger_ctx, &trigger_resources);
	if (rc)
		goto out_fini;
	rc = apei_resources_sub(&trigger_resources, &einj_resources);
	if (rc)
		goto out_fini;
	/*
	 * Some firmware will access target address specified in
	 * param1 to trigger the error when injecting memory error.
	 * This will cause resource conflict with regular memory.  So
	 * remove it from trigger table resources.
	 */
	if ((param_extension || acpi5) && (type & MEM_ERROR_MASK) && param2) {
		struct apei_resources addr_resources;

		apei_resources_init(&addr_resources);
		trigger_param_region = einj_get_trigger_parameter_region(
			full_trigger_tab, param1, param2);
		if (trigger_param_region) {
			rc = apei_resources_add(&addr_resources,
				trigger_param_region->address,
				trigger_param_region->bit_width/8, true);
			if (rc)
				goto out_fini;
			rc = apei_resources_sub(&trigger_resources,
					&addr_resources);
		}
		apei_resources_fini(&addr_resources);
		if (rc)
			goto out_fini;
	}
	rc = apei_resources_request(&trigger_resources, "APEI EINJ Trigger");
	if (rc)
		goto out_fini;
	rc = apei_exec_pre_map_gars(&trigger_ctx);
	if (rc)
		goto out_release;

	rc = apei_exec_run(&trigger_ctx, ACPI_EINJ_TRIGGER_ERROR);

	apei_exec_post_unmap_gars(&trigger_ctx);
out_release:
	apei_resources_release(&trigger_resources);
out_fini:
	apei_resources_fini(&trigger_resources);
out_rel_entry:
	release_mem_region(trigger_paddr + sizeof(trigger_tab),
			   table_size - sizeof(trigger_tab));
out_free_trigger_tab:
	kfree(full_trigger_tab);
out_rel_header:
	release_mem_region(trigger_paddr, sizeof(trigger_tab));
out:
	if (p)
		iounmap(p);

	return rc;
}

static bool is_end_of_list(u8 *val)
{
	for (int i = 0; i < COMPONENT_LEN; ++i) {
		if (val[i] != 0xFF)
			return false;
	}
	return true;
}
static int __einj_error_inject(u32 type, u32 flags, u64 param1, u64 param2,
			       u64 param3, u64 param4)
{
	struct apei_exec_context ctx;
	u32 param_size = is_v2 ? v66param_size : v5param_size;
	u64 val, trigger_paddr, timeout = FIRMWARE_TIMEOUT;
	int i, rc;

	einj_exec_ctx_init(&ctx);

	rc = apei_exec_run_optional(&ctx, ACPI_EINJ_BEGIN_OPERATION);
	if (rc)
		return rc;
	apei_exec_ctx_set_input(&ctx, type);
	if (acpi5) {
		struct set_error_type_with_address *v5param;

		v5param = kmalloc(param_size, GFP_KERNEL);
		if (!v5param)
			return -ENOMEM;

		memcpy_fromio(v5param, einj_param, param_size);
		v5param->type = type;
		if (type & ACPI5_VENDOR_BIT) {
			switch (vendor_flags) {
			case SETWA_FLAGS_APICID:
				v5param->apicid = param1;
				break;
			case SETWA_FLAGS_MEM:
				v5param->memory_address = param1;
				v5param->memory_address_range = param2;
				break;
			case SETWA_FLAGS_PCIE_SBDF:
				v5param->pcie_sbdf = param1;
				break;
			}
			v5param->flags = vendor_flags;
		} else if (flags) {
			v5param->flags = flags;
			v5param->memory_address = param1;
			v5param->memory_address_range = param2;

			if (is_v2) {
				for (i = 0; i < max_nr_components; i++) {
					if (is_end_of_list(syndrome_data[i].comp_id.acpi_id))
						break;
					v5param->einjv2_struct.component_arr[i].comp_id =
						syndrome_data[i].comp_id;
					v5param->einjv2_struct.component_arr[i].comp_synd =
						syndrome_data[i].comp_synd;
				}
				v5param->einjv2_struct.component_arr_count = i;
			} else {
				v5param->apicid = param3;
				v5param->pcie_sbdf = param4;
			}
		} else {
			switch (type) {
			case ACPI_EINJ_PROCESSOR_CORRECTABLE:
			case ACPI_EINJ_PROCESSOR_UNCORRECTABLE:
			case ACPI_EINJ_PROCESSOR_FATAL:
				v5param->apicid = param1;
				v5param->flags = SETWA_FLAGS_APICID;
				break;
			case ACPI_EINJ_MEMORY_CORRECTABLE:
			case ACPI_EINJ_MEMORY_UNCORRECTABLE:
			case ACPI_EINJ_MEMORY_FATAL:
				v5param->memory_address = param1;
				v5param->memory_address_range = param2;
				v5param->flags = SETWA_FLAGS_MEM;
				break;
			case ACPI_EINJ_PCIX_CORRECTABLE:
			case ACPI_EINJ_PCIX_UNCORRECTABLE:
			case ACPI_EINJ_PCIX_FATAL:
				v5param->pcie_sbdf = param1;
				v5param->flags = SETWA_FLAGS_PCIE_SBDF;
				break;
			}
		}
		memcpy_toio(einj_param, v5param, param_size);
		kfree(v5param);
	} else {
		rc = apei_exec_run(&ctx, ACPI_EINJ_SET_ERROR_TYPE);
		if (rc)
			return rc;
		if (einj_param) {
			struct einj_parameter v4param;

			memcpy_fromio(&v4param, einj_param, sizeof(v4param));
			v4param.param1 = param1;
			v4param.param2 = param2;
			memcpy_toio(einj_param, &v4param, sizeof(v4param));
		}
	}
	rc = apei_exec_run(&ctx, ACPI_EINJ_EXECUTE_OPERATION);
	if (rc)
		return rc;
	for (;;) {
		rc = apei_exec_run(&ctx, ACPI_EINJ_CHECK_BUSY_STATUS);
		if (rc)
			return rc;
		val = apei_exec_ctx_get_output(&ctx);
		if (!(val & EINJ_OP_BUSY))
			break;
		if (einj_timedout(&timeout))
			return -EIO;
	}
	rc = apei_exec_run(&ctx, ACPI_EINJ_GET_COMMAND_STATUS);
	if (rc)
		return rc;
	val = apei_exec_ctx_get_output(&ctx);
	if (val == EINJ_STATUS_FAIL)
		return -EBUSY;
	else if (val == EINJ_STATUS_INVAL)
		return -EINVAL;

	/*
	 * The error is injected into the platform successfully, then it needs
	 * to trigger the error.
	 */
	rc = apei_exec_run(&ctx, ACPI_EINJ_GET_TRIGGER_TABLE);
	if (rc)
		return rc;
	trigger_paddr = apei_exec_ctx_get_output(&ctx);
	if (notrigger == 0) {
		rc = __einj_error_trigger(trigger_paddr, type, param1, param2);
		if (rc)
			return rc;
	}
	rc = apei_exec_run_optional(&ctx, ACPI_EINJ_END_OPERATION);

	return rc;
}

/* Allow almost all types of address except MMIO. */
static bool is_allowed_range(u64 base_addr, u64 size)
{
	int i;
	/*
	 * MMIO region is usually claimed with IORESOURCE_MEM + IORES_DESC_NONE.
	 * However, IORES_DESC_NONE is treated like a wildcard when we check if
	 * region intersects with known resource. So do an allow list check for
	 * IORES_DESCs that definitely or most likely not MMIO.
	 */
	int non_mmio_desc[] = {
		IORES_DESC_CRASH_KERNEL,
		IORES_DESC_ACPI_TABLES,
		IORES_DESC_ACPI_NV_STORAGE,
		IORES_DESC_PERSISTENT_MEMORY,
		IORES_DESC_PERSISTENT_MEMORY_LEGACY,
		/* Treat IORES_DESC_DEVICE_PRIVATE_MEMORY as MMIO. */
		IORES_DESC_RESERVED,
		IORES_DESC_SOFT_RESERVED,
	};

	if (region_intersects(base_addr, size, IORESOURCE_SYSTEM_RAM, IORES_DESC_NONE)
			      == REGION_INTERSECTS)
		return true;

	for (i = 0; i < ARRAY_SIZE(non_mmio_desc); ++i) {
		if (region_intersects(base_addr, size, IORESOURCE_MEM, non_mmio_desc[i])
				      == REGION_INTERSECTS)
			return true;
	}

	if (arch_is_platform_page(base_addr))
		return true;

	return false;
}

/* Inject the specified hardware error */
int einj_error_inject(u32 type, u32 flags, u64 param1, u64 param2, u64 param3,
		      u64 param4)
{
	int rc;
	u64 base_addr, size;

	/* If user manually set "flags", make sure it is legal */
	if (flags && (flags & ~(SETWA_FLAGS_APICID | SETWA_FLAGS_MEM |
		      SETWA_FLAGS_PCIE_SBDF | SETWA_FLAGS_EINJV2)))
		return -EINVAL;

	/* check if type is a valid EINJv2 error type */
	if (is_v2) {
		if (!(type & available_error_type_v2))
			return -EINVAL;
	}
	/*
	 * We need extra sanity checks for memory errors.
	 * Other types leap directly to injection.
	 */

	/* ensure param1/param2 existed */
	if (!(param_extension || acpi5))
		goto inject;

	/* ensure injection is memory related */
	if (type & ACPI5_VENDOR_BIT) {
		if (vendor_flags != SETWA_FLAGS_MEM)
			goto inject;
	} else if (!(type & MEM_ERROR_MASK) && !(flags & SETWA_FLAGS_MEM)) {
		goto inject;
	}

	/*
	 * Injections targeting a CXL 1.0/1.1 port have to be injected
	 * via the einj_cxl_rch_error_inject() path as that does the proper
	 * validation of the given RCRB base (MMIO) address.
	 */
	if (einj_is_cxl_error_type(type) && (flags & SETWA_FLAGS_MEM))
		return -EINVAL;

	/*
	 * Disallow crazy address masks that give BIOS leeway to pick
	 * injection address almost anywhere. Insist on page or
	 * better granularity and that target address is normal RAM or
	 * as long as is not MMIO.
	 */
	base_addr = param1 & param2;
	size = ~param2 + 1;

	if ((param2 & PAGE_MASK) != PAGE_MASK)
		return -EINVAL;

	if (!is_allowed_range(base_addr, size))
		return -EINVAL;

	if (is_zero_pfn(base_addr >> PAGE_SHIFT))
		return -EADDRINUSE;

inject:
	mutex_lock(&einj_mutex);
	rc = __einj_error_inject(type, flags, param1, param2, param3, param4);
	mutex_unlock(&einj_mutex);

	return rc;
}

int einj_cxl_rch_error_inject(u32 type, u32 flags, u64 param1, u64 param2,
			      u64 param3, u64 param4)
{
	int rc;

	if (!(einj_is_cxl_error_type(type) && (flags & SETWA_FLAGS_MEM)))
		return -EINVAL;

	mutex_lock(&einj_mutex);
	rc = __einj_error_inject(type, flags, param1, param2, param3, param4);
	mutex_unlock(&einj_mutex);

	return rc;
}

static u32 error_type;
static u32 error_flags;
static u64 error_param1;
static u64 error_param2;
static u64 error_param3;
static u64 error_param4;
static struct dentry *einj_debug_dir;
static char einj_buf[32];
static bool einj_v2_enabled;
static struct { u32 mask; const char *str; } const einj_error_type_string[] = {
	{ BIT(0), "Processor Correctable" },
	{ BIT(1), "Processor Uncorrectable non-fatal" },
	{ BIT(2), "Processor Uncorrectable fatal" },
	{ BIT(3), "Memory Correctable" },
	{ BIT(4), "Memory Uncorrectable non-fatal" },
	{ BIT(5), "Memory Uncorrectable fatal" },
	{ BIT(6), "PCI Express Correctable" },
	{ BIT(7), "PCI Express Uncorrectable non-fatal" },
	{ BIT(8), "PCI Express Uncorrectable fatal" },
	{ BIT(9), "Platform Correctable" },
	{ BIT(10), "Platform Uncorrectable non-fatal" },
	{ BIT(11), "Platform Uncorrectable fatal"},
	{ BIT(31), "Vendor Defined Error Types" },
};

static struct { u32 mask; const char *str; } const einjv2_error_type_string[] = {
	{ BIT(0), "EINJV2 Processor Error" },
	{ BIT(1), "EINJV2 Memory Error" },
	{ BIT(2), "EINJV2 PCI Express Error" },
};

static int available_error_type_show(struct seq_file *m, void *v)
{

	for (int pos = 0; pos < ARRAY_SIZE(einj_error_type_string); pos++)
		if (available_error_type & einj_error_type_string[pos].mask)
			seq_printf(m, "0x%08x\t%s\n", einj_error_type_string[pos].mask,
				   einj_error_type_string[pos].str);
	if ((available_error_type & ACPI65_EINJV2_SUPP) && einj_v2_enabled) {
		for (int pos = 0; pos < ARRAY_SIZE(einjv2_error_type_string); pos++) {
			if (available_error_type_v2 & einjv2_error_type_string[pos].mask)
				seq_printf(m, "V2_0x%08x\t%s\n", einjv2_error_type_string[pos].mask,
					   einjv2_error_type_string[pos].str);
		}
	}
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(available_error_type);

static ssize_t error_type_get(struct file *file, char __user *buf,
				size_t count, loff_t *ppos)
{
	return simple_read_from_buffer(buf, count, ppos, einj_buf, strlen(einj_buf));
}

bool einj_is_cxl_error_type(u64 type)
{
	return (type & CXL_ERROR_MASK) && (!(type & ACPI5_VENDOR_BIT));
}

int einj_validate_error_type(u64 type)
{
	u32 tval, vendor;

	/* Only low 32 bits for error type are valid */
	if (type & GENMASK_ULL(63, 32))
		return -EINVAL;

	/*
	 * Vendor defined types have 0x80000000 bit set, and
	 * are not enumerated by ACPI_EINJ_GET_ERROR_TYPE
	 */
	vendor = type & ACPI5_VENDOR_BIT;
	tval = type & GENMASK(30, 0);

	/* Only one error type can be specified */
	if (tval & (tval - 1))
		return -EINVAL;
	if (!vendor)
		if (!(type & (available_error_type | available_error_type_v2)))
			return -EINVAL;

	return 0;
}

static ssize_t error_type_set(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	int rc;
	u64 val;

	/* Leave the last character for the NUL terminator */
	if (count > sizeof(einj_buf) - 1)
		return -EINVAL;

	memset(einj_buf, 0, sizeof(einj_buf));
	if (copy_from_user(einj_buf, buf, count))
		return -EFAULT;

	if (strncmp(einj_buf, "V2_", 3) == 0) {
		if (!sscanf(einj_buf, "V2_%llx", &val))
			return -EINVAL;
		is_v2 = true;
	} else {
		if (!sscanf(einj_buf, "%llx", &val))
			return -EINVAL;
		is_v2 = false;
	}

	rc = einj_validate_error_type(val);
	if (rc)
		return rc;

	error_type = val;

	return count;
}

static const struct file_operations error_type_fops = {
	.read		= error_type_get,
	.write		= error_type_set,
};

static int error_inject_set(void *data, u64 val)
{
	if (!error_type)
		return -EINVAL;

	if (is_v2)
		error_flags |= SETWA_FLAGS_EINJV2;
	else
		error_flags &= ~SETWA_FLAGS_EINJV2;

	return einj_error_inject(error_type, error_flags, error_param1, error_param2,
		error_param3, error_param4);
}

DEFINE_DEBUGFS_ATTRIBUTE(error_inject_fops, NULL, error_inject_set, "%llu\n");

static int einj_check_table(struct acpi_table_einj *einj_tab)
{
	if ((einj_tab->header_length !=
	     (sizeof(struct acpi_table_einj) - sizeof(einj_tab->header)))
	    && (einj_tab->header_length != sizeof(struct acpi_table_einj)))
		return -EINVAL;
	if (einj_tab->header.length < sizeof(struct acpi_table_einj))
		return -EINVAL;
	if (einj_tab->entries !=
	    (einj_tab->header.length - sizeof(struct acpi_table_einj)) /
	    sizeof(struct acpi_einj_entry))
		return -EINVAL;

	return 0;
}

static ssize_t u128_read(struct file *f, char __user *buf, size_t count, loff_t *off)
{
	char output[2 * COMPONENT_LEN + 1];
	u8 *data = f->f_inode->i_private;
	int i;

	if (*off >= sizeof(output))
		return 0;

	for (i = 0; i < COMPONENT_LEN; i++)
		sprintf(output + 2 * i, "%.02x", data[COMPONENT_LEN - i - 1]);
	output[2 * COMPONENT_LEN] = '\n';

	return simple_read_from_buffer(buf, count, off, output, sizeof(output));
}

static ssize_t u128_write(struct file *f, const char __user *buf, size_t count, loff_t *off)
{
	char input[2 + 2 * COMPONENT_LEN + 2];
	u8 *save = f->f_inode->i_private;
	u8 tmp[COMPONENT_LEN];
	char byte[3] = {};
	char *s, *e;
	ssize_t c;
	long val;
	int i;

	/* Require that user supply whole input line in one write(2) syscall */
	if (*off)
		return -EINVAL;

	c = simple_write_to_buffer(input, sizeof(input), off, buf, count);
	if (c < 0)
		return c;

	if (c < 1 || input[c - 1] != '\n')
		return -EINVAL;

	/* Empty line means invalidate this entry */
	if (c == 1) {
		memset(save, 0xff, COMPONENT_LEN);
		return c;
	}

	if (input[0] == '0' && (input[1] == 'x' || input[1] == 'X'))
		s = input + 2;
	else
		s = input;
	e = input + c - 1;

	for (i = 0; i < COMPONENT_LEN; i++) {
		byte[1] = *--e;
		byte[0] = e > s ? *--e : '0';
		if (kstrtol(byte, 16, &val))
			return -EINVAL;
		tmp[i] = val;
		if (e <= s)
			break;
	}
	while (++i < COMPONENT_LEN)
		tmp[i] = 0;

	memcpy(save, tmp, COMPONENT_LEN);

	return c;
}

static const struct file_operations u128_fops = {
	.read	= u128_read,
	.write	= u128_write,
};

static bool setup_einjv2_component_files(void)
{
	char name[32];

	syndrome_data = kcalloc(max_nr_components, sizeof(syndrome_data[0]), GFP_KERNEL);
	if (!syndrome_data)
		return false;

	for (int i = 0; i < max_nr_components; i++) {
		sprintf(name, "component_id%d", i);
		debugfs_create_file(name, 0600, einj_debug_dir,
				    &syndrome_data[i].comp_id, &u128_fops);
		sprintf(name, "component_syndrome%d", i);
		debugfs_create_file(name, 0600, einj_debug_dir,
				    &syndrome_data[i].comp_synd, &u128_fops);
	}

	return true;
}

static int __init einj_probe(struct faux_device *fdev)
{
	int rc;
	acpi_status status;
	struct apei_exec_context ctx;

	status = acpi_get_table(ACPI_SIG_EINJ, 0,
				(struct acpi_table_header **)&einj_tab);
	if (status == AE_NOT_FOUND) {
		pr_debug("EINJ table not found.\n");
		return -ENODEV;
	} else if (ACPI_FAILURE(status)) {
		pr_err("Failed to get EINJ table: %s\n",
				acpi_format_exception(status));
		return -EINVAL;
	}

	rc = einj_check_table(einj_tab);
	if (rc) {
		pr_warn(FW_BUG "Invalid EINJ table.\n");
		goto err_put_table;
	}

	rc = einj_get_available_error_types(&available_error_type, &available_error_type_v2);
	if (rc)
		goto err_put_table;

	rc = -ENOMEM;
	einj_debug_dir = debugfs_create_dir("einj", apei_get_debugfs_dir());

	debugfs_create_file("available_error_type", S_IRUSR, einj_debug_dir,
			    NULL, &available_error_type_fops);
	debugfs_create_file_unsafe("error_type", 0600, einj_debug_dir,
				   NULL, &error_type_fops);
	debugfs_create_file_unsafe("error_inject", 0200, einj_debug_dir,
				   NULL, &error_inject_fops);

	apei_resources_init(&einj_resources);
	einj_exec_ctx_init(&ctx);
	rc = apei_exec_collect_resources(&ctx, &einj_resources);
	if (rc) {
		pr_err("Error collecting EINJ resources.\n");
		goto err_fini;
	}

	rc = apei_resources_request(&einj_resources, "APEI EINJ");
	if (rc) {
		pr_err("Error requesting memory/port resources.\n");
		goto err_fini;
	}

	rc = apei_exec_pre_map_gars(&ctx);
	if (rc) {
		pr_err("Error pre-mapping GARs.\n");
		goto err_release;
	}

	einj_param = einj_get_parameter_address();
	if ((param_extension || acpi5) && einj_param) {
		debugfs_create_x32("flags", S_IRUSR | S_IWUSR, einj_debug_dir,
				   &error_flags);
		debugfs_create_x64("param1", S_IRUSR | S_IWUSR, einj_debug_dir,
				   &error_param1);
		debugfs_create_x64("param2", S_IRUSR | S_IWUSR, einj_debug_dir,
				   &error_param2);
		debugfs_create_x64("param3", S_IRUSR | S_IWUSR, einj_debug_dir,
				   &error_param3);
		debugfs_create_x64("param4", S_IRUSR | S_IWUSR, einj_debug_dir,
				   &error_param4);
		debugfs_create_x32("notrigger", S_IRUSR | S_IWUSR,
				   einj_debug_dir, &notrigger);
		if (available_error_type & ACPI65_EINJV2_SUPP)
			einj_v2_enabled = setup_einjv2_component_files();
	}

	if (vendor_dev[0]) {
		vendor_blob.data = vendor_dev;
		vendor_blob.size = strlen(vendor_dev);
		debugfs_create_blob("vendor", S_IRUSR, einj_debug_dir,
				    &vendor_blob);
		debugfs_create_x32("vendor_flags", S_IRUSR | S_IWUSR,
				   einj_debug_dir, &vendor_flags);
	}

	if (vendor_errors.size)
		debugfs_create_blob("oem_error", 0600, einj_debug_dir,
				    &vendor_errors);

	pr_info("Error INJection is initialized.\n");

	return 0;

err_release:
	apei_resources_release(&einj_resources);
err_fini:
	apei_resources_fini(&einj_resources);
	debugfs_remove_recursive(einj_debug_dir);
err_put_table:
	acpi_put_table((struct acpi_table_header *)einj_tab);

	return rc;
}

static void einj_remove(struct faux_device *fdev)
{
	struct apei_exec_context ctx;

	if (einj_param) {
		acpi_size size;

		if (v66param_size)
			size = v66param_size;
		else if (acpi5)
			size = v5param_size;
		else
			size = sizeof(struct einj_parameter);

		acpi_os_unmap_iomem(einj_param, size);
		if (vendor_errors.size)
			acpi_os_unmap_memory(vendor_errors.data, vendor_errors.size);
	}
	einj_exec_ctx_init(&ctx);
	apei_exec_post_unmap_gars(&ctx);
	apei_resources_release(&einj_resources);
	apei_resources_fini(&einj_resources);
	debugfs_remove_recursive(einj_debug_dir);
	kfree(syndrome_data);
	acpi_put_table((struct acpi_table_header *)einj_tab);
}

static struct faux_device *einj_dev;
static struct faux_device_ops einj_device_ops = {
	.probe = einj_probe,
	.remove = einj_remove,
};

static int __init einj_init(void)
{
	if (acpi_disabled) {
		pr_debug("ACPI disabled.\n");
		return -ENODEV;
	}

	einj_dev = faux_device_create("acpi-einj", NULL, &einj_device_ops);

	if (einj_dev)
		einj_initialized = true;

	return 0;
}

static void __exit einj_exit(void)
{
	faux_device_destroy(einj_dev);
}

module_init(einj_init);
module_exit(einj_exit);

MODULE_AUTHOR("Huang Ying");
MODULE_DESCRIPTION("APEI Error INJection support");
MODULE_LICENSE("GPL");
