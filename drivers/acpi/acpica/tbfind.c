// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: tbfind   - find table
 *
 * Copyright (C) 2000 - 2022, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "actables.h"

#define _COMPONENT          ACPI_TABLES
ACPI_MODULE_NAME("tbfind")

/*******************************************************************************
 *
 * FUNCTION:    acpi_tb_find_table
 *
 * PARAMETERS:  signature           - String with ACPI table signature
 *              oem_id              - String with the table OEM ID
 *              oem_table_id        - String with the OEM Table ID
 *              table_index         - Where the table index is returned
 *
 * RETURN:      Status and table index
 *
 * DESCRIPTION: Find an ACPI table (in the RSDT/XSDT) that matches the
 *              Signature, OEM ID and OEM Table ID. Returns an index that can
 *              be used to get the table header or entire table.
 *
 ******************************************************************************/
acpi_status
acpi_tb_find_table(char *signature,
		   char *oem_id, char *oem_table_id, u32 *table_index)
{
	acpi_status status = AE_OK;
	struct acpi_table_header header;
	u32 i;

	ACPI_FUNCTION_TRACE(tb_find_table);

	/* Validate the input table signature */

	if (!acpi_ut_valid_nameseg(signature)) {
		return_ACPI_STATUS(AE_BAD_SIGNATURE);
	}

	/* Don't allow the OEM strings to be too long */

	if ((strlen(oem_id) > ACPI_OEM_ID_SIZE) ||
	    (strlen(oem_table_id) > ACPI_OEM_TABLE_ID_SIZE)) {
		return_ACPI_STATUS(AE_AML_STRING_LIMIT);
	}

	/* Normalize the input strings */

	memset(&header, 0, sizeof(struct acpi_table_header));
	ACPI_COPY_NAMESEG(header.signature, signature);
	strncpy(header.oem_id, oem_id, ACPI_OEM_ID_SIZE);
	strncpy(header.oem_table_id, oem_table_id, ACPI_OEM_TABLE_ID_SIZE);

	/* Search for the table */

	(void)acpi_ut_acquire_mutex(ACPI_MTX_TABLES);
	for (i = 0; i < acpi_gbl_root_table_list.current_table_count; ++i) {
		if (memcmp(&(acpi_gbl_root_table_list.tables[i].signature),
			   header.signature, ACPI_NAMESEG_SIZE)) {

			/* Not the requested table */

			continue;
		}

		/* Table with matching signature has been found */

		if (!acpi_gbl_root_table_list.tables[i].pointer) {

			/* Table is not currently mapped, map it */

			status =
			    acpi_tb_validate_table(&acpi_gbl_root_table_list.
						   tables[i]);
			if (ACPI_FAILURE(status)) {
				goto unlock_and_exit;
			}

			if (!acpi_gbl_root_table_list.tables[i].pointer) {
				continue;
			}
		}

		/* Check for table match on all IDs */

		if (!memcmp
		    (acpi_gbl_root_table_list.tables[i].pointer->signature,
		     header.signature, ACPI_NAMESEG_SIZE) && (!oem_id[0]
							      ||
							      !memcmp
							      (acpi_gbl_root_table_list.
							       tables[i].
							       pointer->oem_id,
							       header.oem_id,
							       ACPI_OEM_ID_SIZE))
		    && (!oem_table_id[0]
			|| !memcmp(acpi_gbl_root_table_list.tables[i].pointer->
				   oem_table_id, header.oem_table_id,
				   ACPI_OEM_TABLE_ID_SIZE))) {
			*table_index = i;

			ACPI_DEBUG_PRINT((ACPI_DB_TABLES,
					  "Found table [%4.4s]\n",
					  header.signature));
			goto unlock_and_exit;
		}
	}
	status = AE_NOT_FOUND;

unlock_and_exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_TABLES);
	return_ACPI_STATUS(status);
}
