// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: pswalk - Parser routines to walk parsed op tree(s)
 *
 * Copyright (C) 2000 - 2018, Intel Corp.
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
void acpi_ps_delete_parse_tree(union acpi_parse_object *subtree_root)
{
	union acpi_parse_object *op = subtree_root;
	union acpi_parse_object *next = NULL;
	union acpi_parse_object *parent = NULL;

	ACPI_FUNCTION_TRACE_PTR(ps_delete_parse_tree, subtree_root);

	/* Visit all nodes in the subtree */

	while (op) {

		/* Check if we are not ascending */

		if (op != parent) {

			/* Look for an argument or child of the current op */

			next = acpi_ps_get_arg(op, 0);
			if (next) {

				/* Still going downward in tree (Op is not completed yet) */

				op = next;
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
			op = parent;
		}
	}

	return_VOID;
}
