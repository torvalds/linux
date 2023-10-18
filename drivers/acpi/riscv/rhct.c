// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022-2023, Ventana Micro Systems Inc
 *	Author: Sunil V L <sunilvl@ventanamicro.com>
 *
 */

#define pr_fmt(fmt)     "ACPI: RHCT: " fmt

#include <linux/acpi.h>
#include <linux/bits.h>

static struct acpi_table_rhct *acpi_get_rhct(void)
{
	static struct acpi_table_header *rhct;
	acpi_status status;

	/*
	 * RHCT will be used at runtime on every CPU, so we
	 * don't need to call acpi_put_table() to release the table mapping.
	 */
	if (!rhct) {
		status = acpi_get_table(ACPI_SIG_RHCT, 0, &rhct);
		if (ACPI_FAILURE(status)) {
			pr_warn_once("No RHCT table found\n");
			return NULL;
		}
	}

	return (struct acpi_table_rhct *)rhct;
}

/*
 * During early boot, the caller should call acpi_get_table() and pass its pointer to
 * these functions(and free up later). At run time, since this table can be used
 * multiple times, NULL may be passed in order to use the cached table.
 */
int acpi_get_riscv_isa(struct acpi_table_header *table, unsigned int cpu, const char **isa)
{
	struct acpi_rhct_node_header *node, *ref_node, *end;
	u32 size_hdr = sizeof(struct acpi_rhct_node_header);
	u32 size_hartinfo = sizeof(struct acpi_rhct_hart_info);
	struct acpi_rhct_hart_info *hart_info;
	struct acpi_rhct_isa_string *isa_node;
	struct acpi_table_rhct *rhct;
	u32 *hart_info_node_offset;
	u32 acpi_cpu_id = get_acpi_id_for_cpu(cpu);

	BUG_ON(acpi_disabled);

	if (!table) {
		rhct = acpi_get_rhct();
		if (!rhct)
			return -ENOENT;
	} else {
		rhct = (struct acpi_table_rhct *)table;
	}

	end = ACPI_ADD_PTR(struct acpi_rhct_node_header, rhct, rhct->header.length);

	for (node = ACPI_ADD_PTR(struct acpi_rhct_node_header, rhct, rhct->node_offset);
	     node < end;
	     node = ACPI_ADD_PTR(struct acpi_rhct_node_header, node, node->length)) {
		if (node->type == ACPI_RHCT_NODE_TYPE_HART_INFO) {
			hart_info = ACPI_ADD_PTR(struct acpi_rhct_hart_info, node, size_hdr);
			hart_info_node_offset = ACPI_ADD_PTR(u32, hart_info, size_hartinfo);
			if (acpi_cpu_id != hart_info->uid)
				continue;

			for (int i = 0; i < hart_info->num_offsets; i++) {
				ref_node = ACPI_ADD_PTR(struct acpi_rhct_node_header,
							rhct, hart_info_node_offset[i]);
				if (ref_node->type == ACPI_RHCT_NODE_TYPE_ISA_STRING) {
					isa_node = ACPI_ADD_PTR(struct acpi_rhct_isa_string,
								ref_node, size_hdr);
					*isa = isa_node->isa;
					return 0;
				}
			}
		}
	}

	return -1;
}

static void acpi_parse_hart_info_cmo_node(struct acpi_table_rhct *rhct,
					  struct acpi_rhct_hart_info *hart_info,
					  u32 *cbom_size, u32 *cboz_size, u32 *cbop_size)
{
	u32 size_hartinfo = sizeof(struct acpi_rhct_hart_info);
	u32 size_hdr = sizeof(struct acpi_rhct_node_header);
	struct acpi_rhct_node_header *ref_node;
	struct acpi_rhct_cmo_node *cmo_node;
	u32 *hart_info_node_offset;

	hart_info_node_offset = ACPI_ADD_PTR(u32, hart_info, size_hartinfo);
	for (int i = 0; i < hart_info->num_offsets; i++) {
		ref_node = ACPI_ADD_PTR(struct acpi_rhct_node_header,
					rhct, hart_info_node_offset[i]);
		if (ref_node->type == ACPI_RHCT_NODE_TYPE_CMO) {
			cmo_node = ACPI_ADD_PTR(struct acpi_rhct_cmo_node,
						ref_node, size_hdr);
			if (cbom_size && cmo_node->cbom_size <= 30) {
				if (!*cbom_size)
					*cbom_size = BIT(cmo_node->cbom_size);
				else if (*cbom_size != BIT(cmo_node->cbom_size))
					pr_warn("CBOM size is not the same across harts\n");
			}

			if (cboz_size && cmo_node->cboz_size <= 30) {
				if (!*cboz_size)
					*cboz_size = BIT(cmo_node->cboz_size);
				else if (*cboz_size != BIT(cmo_node->cboz_size))
					pr_warn("CBOZ size is not the same across harts\n");
			}

			if (cbop_size && cmo_node->cbop_size <= 30) {
				if (!*cbop_size)
					*cbop_size = BIT(cmo_node->cbop_size);
				else if (*cbop_size != BIT(cmo_node->cbop_size))
					pr_warn("CBOP size is not the same across harts\n");
			}
		}
	}
}

/*
 * During early boot, the caller should call acpi_get_table() and pass its pointer to
 * these functions (and free up later). At run time, since this table can be used
 * multiple times, pass NULL so that the table remains in memory.
 */
void acpi_get_cbo_block_size(struct acpi_table_header *table, u32 *cbom_size,
			     u32 *cboz_size, u32 *cbop_size)
{
	u32 size_hdr = sizeof(struct acpi_rhct_node_header);
	struct acpi_rhct_node_header *node, *end;
	struct acpi_rhct_hart_info *hart_info;
	struct acpi_table_rhct *rhct;

	if (acpi_disabled)
		return;

	if (table) {
		rhct = (struct acpi_table_rhct *)table;
	} else {
		rhct = acpi_get_rhct();
		if (!rhct)
			return;
	}

	if (cbom_size)
		*cbom_size = 0;

	if (cboz_size)
		*cboz_size = 0;

	if (cbop_size)
		*cbop_size = 0;

	end = ACPI_ADD_PTR(struct acpi_rhct_node_header, rhct, rhct->header.length);
	for (node = ACPI_ADD_PTR(struct acpi_rhct_node_header, rhct, rhct->node_offset);
	     node < end;
	     node = ACPI_ADD_PTR(struct acpi_rhct_node_header, node, node->length)) {
		if (node->type == ACPI_RHCT_NODE_TYPE_HART_INFO) {
			hart_info = ACPI_ADD_PTR(struct acpi_rhct_hart_info, node, size_hdr);
			acpi_parse_hart_info_cmo_node(rhct, hart_info, cbom_size,
						      cboz_size, cbop_size);
		}
	}
}
