// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: pswalk - Parser routines to walk parsed op tree(s)
 *
 * Copyright (C) 2000 - 2020, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acparser.h"

#define _COMPONENT          ACPI_PARSER
ACPI_MODULE_NAME("pswalk")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ps_delete_parse_tree
 *
 * PARAMETERS:  subtree_root        - Root of tree (or subtree) to delete
 *
 * RETURN:      None
 *
 * DESCRIPTION: Delete a portion of or an entire parse tree.
 *
 ******************************************************************************/
#include "amlcode.h"
void acpi_ps_delete_parse_tree(union acpi_parse_object *subtree_root)
{
	union acpi_parse_object *op = subtree_root;
	union acpi_parse_object *next = NULL;
	union acpi_parse_object *parent = NULL;
	u32 level = 0;

	ACPI_FUNCTION_TRACE_PTR(ps_delete_parse_tree, subtree_root);

	ACPI_DEBUG_PRINT((ACPI_DB_PARSE_TREES, " root %p\n", subtree_root));

	/* Visit all nodes in the subtree */

	while (op) {
		if (op != parent) {

			/* This is the descending case */

			if (ACPI_IS_DEBUG_ENABLED
			    (ACPI_LV_PARSE_TREES, _COMPONENT)) {

				/* This debug option will print the entire parse tree */

				acpi_os_printf("      %*.s%s %p", (level * 4),
					       " ",
					       acpi_ps_get_opcode_name(op->
								       common.
								       aml_opcode),
					       op);

				if (op->named.aml_opcode == AML_INT_NAMEPATH_OP) {
					acpi_os_printf("  %4.4s",
						       op->common.value.string);
				}
				if (op->named.aml_opcode == AML_STRING_OP) {
					acpi_os_printf("  %s",
						       op->common.value.string);
				}
				acpi_os_printf("\n");
			}

			/* Look for an argument or child of the current op */

			next = acpi_ps_get_arg(op, 0);
			if (next) {

				/* Still going downward in tree (Op is not completed yet) */

				op = next;
				level++;
				continue;
			}
		}

		/* No more children, this Op is complete. */

		next = op->common.next;
		parent = op->common.parent;

		acpi_ps_free_op(op);

		/* If we are back to the starting point, the walk is complete. */

		if (op == subtree_root) {
			return_VOID;
		}

		if (next) {
			op = next;
		} else {
			level--;
			op = parent;
		}
	}

	return_VOID;
}
