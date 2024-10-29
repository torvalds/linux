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
#include <linux/platform_device.h>
#include <linux/unaligned.h>

#include "apei-internal.h"

#undef pr_fmt
#define pr_fmt(fmt) "EINJ: " fmt

#define SLEEP_UNIT_MIN		1000			/* 1ms */
#define SLEEP_UNIT_MAX		5000			/* 5ms */
/* Firmware should respond within 1 seconds */
#define FIRMWARE_TIMEOUT	(1 * USEC_PER_SEC)
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

struct set_error_type_with_address {
	u32	type;
	u32	vendor_extension;
	u32	flags;
	u32	apicid;
	u64	memory_address;
	u64	memory_address_range;
	u32	pcie_sbdf;
};
enum {
	SETWA_FLAGS_APICID = 1,
	SETWA_FLAGS_MEM = 2,
	SETWA_FLAGS_PCIE_SBDF = 4,
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

static void *einj_param;

static void einj_exec_ctx_init(struct apei_exec_context *ctx)
{
	apei_exec_ctx_init(ctx, einj_ins_type, ARRAY_SIZE(einj_ins_type),
			   EINJ_TAB_ENTRY(einj_tab), einj_tab->entries);
}

static int __einj_get_available_error_type(u32 *type)
{
	struct apei_exec_context ctx;
	int rc;

	einj_exec_ctx_init(&ctx);
	rc = apei_exec_run(&ctx, ACPI_EINJ_GET_ERROR_TYPE);
	if (rc)
		return rc;
	*type = apei_exec_ctx_get_output(&ctx);

	return 0;
}

/* Get error injection capabilities of the platform */
int einj_get_available_error_type(u32 *type)
{
	int rc;

	mutex_lock(&einj_mutex);
	rc = __einj_get_available_error_type(type);
	mutex_unlock(&einj_mutex);

	return rc;
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
	struct	vendor_error_type_extension *v;
	u32	sbdf;

	if (!offset)
		return;
	v = acpi_os_map_iomem(paddr + offset, sizeof(*v));
	if (!v)
		return;
	get_oem_vendor_struct(paddr, offset, v);
	sbdf = v->pcie_sbdf;
	sprintf(vendor_dev, "%x:%x:%x.%x vendor_id=%x device_id=%x rev_id=%x\n",
		sbdf >> 24, (sbdf >> 16) & 0xff,
		(sbdf >> 11) & 0x1f, (sbdf >> 8) & 0x7,
		 v->vendor_id, v->device_id, v->rev_id);
	acpi_os_unmap_iomem(v, sizeof(*v));
}

static void *einj_get_parameter_address(void)
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
		struct set_error_type_with_address *v5param;

		v5param = acpi_os_map_iomem(pa_v5, sizeof(*v5param));
		if (v5param) {
			acpi5 = 1;
			check_vendor_extension(pa_v5, v5param);
			return v5param;
		}
	}
	if (param_extension && pa_v4) {
		struct einj_parameter *v4param;

		v4param = acpi_os_map_iomem(pa_v4, sizeof(*v4param));
		if (!v4param)
			return NULL;
		if (v4param->reserved1 || v4param->reserved2) {
			acpi_os_unmap_iomem(v4param, sizeof(*v4param));
			return NULL;
		}
		return v4param;
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
	struct acpi_einj_trigger *trigger_tab = NULL;
	struct apei_exec_context trigger_ctx;
	struct apei_resources trigger_resources;
	struct acpi_whea_header *trigger_entry;
	struct resource *r;
	u32 table_size;
	int rc = -EIO;
	struct acpi_generic_address *trigger_param_region = NULL;

	r = request_mem_region(trigger_paddr, sizeof(*trigger_tab),
			       "APEI EINJ Trigger Table");
	if (!r) {
		pr_err("Can not request [mem %#010llx-%#010llx] for Trigger table\n",
		       (unsigned long long)trigger_paddr,
		       (unsigned long long)trigger_paddr +
			    sizeof(*trigger_tab) - 1);
		goto out;
	}
	trigger_tab = ioremap_cache(trigger_paddr, sizeof(*trigger_tab));
	if (!trigger_tab) {
		pr_err("Failed to map trigger table!\n");
		goto out_rel_header;
	}
	rc = einj_check_trigger_header(trigger_tab);
	if (rc) {
		pr_warn(FW_BUG "Invalid trigger error action table.\n");
		goto out_rel_header;
	}

	/* No action structures in the TRIGGER_ERROR table, nothing to do */
	if (!trigger_tab->entry_count)
		goto out_rel_header;

	rc = -EIO;
	table_size = trigger_tab->table_size;
	r = request_mem_region(trigger_paddr + sizeof(*trigger_tab),
			       table_size - sizeof(*trigger_tab),
			       "APEI EINJ Trigger Table");
	if (!r) {
		pr_err("Can not request [mem %#010llx-%#010llx] for Trigger Table Entry\n",
		       (unsigned long long)trigger_paddr + sizeof(*trigger_tab),
		       (unsigned long long)trigger_paddr + table_size - 1);
		goto out_rel_header;
	}
	iounmap(trigger_tab);
	trigger_tab = ioremap_cache(trigger_paddr, table_size);
	if (!trigger_tab) {
		pr_err("Failed to map trigger table!\n");
		goto out_rel_entry;
	}
	trigger_entry = (struct acpi_whea_header *)
		((char *)trigger_tab + sizeof(struct acpi_einj_trigger));
	apei_resources_init(&trigger_resources);
	apei_exec_ctx_init(&trigger_ctx, einj_ins_type,
			   ARRAY_SIZE(einj_ins_type),
			   trigger_entry, trigger_tab->entry_count);
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
			trigger_tab, param1, param2);
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
	release_mem_region(trigger_paddr + sizeof(*trigger_tab),
			   table_size - sizeof(*trigger_tab));
out_rel_header:
	release_mem_region(trigger_paddr, sizeof(*trigger_tab));
out:
	if (trigger_tab)
		iounmap(trigger_tab);

	return rc;
}

static int __einj_error_inject(u32 type, u32 flags, u64 param1, u64 param2,
			       u64 param3, u64 param4)
{
	struct apei_exec_context ctx;
	u64 val, trigger_paddr, timeout = FIRMWARE_TIMEOUT;
	int rc;

	einj_exec_ctx_init(&ctx);

	rc = apei_exec_run_optional(&ctx, ACPI_EINJ_BEGIN_OPERATION);
	if (rc)
		return rc;
	apei_exec_ctx_set_input(&ctx, type);
	if (acpi5) {
		struct set_error_type_with_address *v5param = einj_param;

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
			v5param->apicid = param3;
			v5param->pcie_sbdf = param4;
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
	} else {
		rc = apei_exec_run(&ctx, ACPI_EINJ_SET_ERROR_TYPE);
		if (rc)
			return rc;
		if (einj_param) {
			struct einj_parameter *v4param = einj_param;

			v4param->param1 = param1;
			v4param->param2 = param2;
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

/* Inject the specified hardware error */
int einj_error_inject(u32 type, u32 flags, u64 param1, u64 param2, u64 param3,
		      u64 param4)
{
	int rc;
	u64 base_addr, size;

	/* If user manually set "flags", make sure it is legal */
	if (flags && (flags &
		~(SETWA_FLAGS_APICID|SETWA_FLAGS_MEM|SETWA_FLAGS_PCIE_SBDF)))
		return -EINVAL;

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
	 * NVDIMM.
	 */
	base_addr = param1 & param2;
	size = ~param2 + 1;

	if (((param2 & PAGE_MASK) != PAGE_MASK) ||
	    ((region_intersects(base_addr, size, IORESOURCE_SYSTEM_RAM, IORES_DESC_NONE)
				!= REGION_INTERSECTS) &&
	     (region_intersects(base_addr, size, IORESOURCE_MEM, IORES_DESC_PERSISTENT_MEMORY)
				!= REGION_INTERSECTS) &&
	     (region_intersects(base_addr, size, IORESOURCE_MEM, IORES_DESC_SOFT_RESERVED)
				!= REGION_INTERSECTS) &&
	     !arch_is_platform_page(base_addr)))
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

static int available_error_type_show(struct seq_file *m, void *v)
{
	int rc;
	u32 error_type = 0;

	rc = einj_get_available_error_type(&error_type);
	if (rc)
		return rc;
	for (int pos = 0; pos < ARRAY_SIZE(einj_error_type_string); pos++)
		if (error_type & einj_error_type_string[pos].mask)
			seq_printf(m, "0x%08x\t%s\n", einj_error_type_string[pos].mask,
				   einj_error_type_string[pos].str);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(available_error_type);

static int error_type_get(void *data, u64 *val)
{
	*val = error_type;

	return 0;
}

bool einj_is_cxl_error_type(u64 type)
{
	return (type & CXL_ERROR_MASK) && (!(type & ACPI5_VENDOR_BIT));
}

int einj_validate_error_type(u64 type)
{
	u32 tval, vendor, available_error_type = 0;
	int rc;

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
	if (!vendor) {
		rc = einj_get_available_error_type(&available_error_type);
		if (rc)
			return rc;
		if (!(type & available_error_type))
			return -EINVAL;
	}

	return 0;
}

static int error_type_set(void *data, u64 val)
{
	int rc;

	rc = einj_validate_error_type(val);
	if (rc)
		return rc;

	error_type = val;

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(error_type_fops, error_type_get, error_type_set,
			 "0x%llx\n");

static int error_inject_set(void *data, u64 val)
{
	if (!error_type)
		return -EINVAL;

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

static int __init einj_probe(struct platform_device *pdev)
{
	int rc;
	acpi_status status;
	struct apei_exec_context ctx;

	if (acpi_disabled) {
		pr_debug("ACPI disabled.\n");
		return -ENODEV;
	}

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

static void __exit einj_remove(struct platform_device *pdev)
{
	struct apei_exec_context ctx;

	if (einj_param) {
		acpi_size size = (acpi5) ?
			sizeof(struct set_error_type_with_address) :
			sizeof(struct einj_parameter);

		acpi_os_unmap_iomem(einj_param, size);
		if (vendor_errors.size)
			acpi_os_unmap_memory(vendor_errors.data, vendor_errors.size);
	}
	einj_exec_ctx_init(&ctx);
	apei_exec_post_unmap_gars(&ctx);
	apei_resources_release(&einj_resources);
	apei_resources_fini(&einj_resources);
	debugfs_remove_recursive(einj_debug_dir);
	acpi_put_table((struct acpi_table_header *)einj_tab);
}

static struct platform_device *einj_dev;
/*
 * einj_remove() lives in .exit.text. For drivers registered via
 * platform_driver_probe() this is ok because they cannot get unbound at
 * runtime. So mark the driver struct with __refdata to prevent modpost
 * triggering a section mismatch warning.
 */
static struct platform_driver einj_driver __refdata = {
	.remove_new = __exit_p(einj_remove),
	.driver = {
		.name = "acpi-einj",
	},
};

static int __init einj_init(void)
{
	struct platform_device_info einj_dev_info = {
		.name = "acpi-einj",
		.id = -1,
	};
	int rc;

	einj_dev = platform_device_register_full(&einj_dev_info);
	if (IS_ERR(einj_dev))
		return PTR_ERR(einj_dev);

	rc = platform_driver_probe(&einj_driver, einj_probe);
	einj_initialized = rc == 0;

	return 0;
}

static void __exit einj_exit(void)
{
	if (einj_initialized)
		platform_driver_unregister(&einj_driver);

	platform_device_unregister(einj_dev);
}

module_init(einj_init);
module_exit(einj_exit);

MODULE_AUTHOR("Huang Ying");
MODULE_DESCRIPTION("APEI Error INJection support");
MODULE_LICENSE("GPL");
