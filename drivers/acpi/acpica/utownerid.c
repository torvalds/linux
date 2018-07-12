// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/*******************************************************************************
 *
 * Module Name: utownerid - Support for Table/Method Owner IDs
 *
 ******************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"
#include "acnamesp.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utownerid")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_allocate_owner_id
 *
 * PARAMETERS:  owner_id        - Where the new owner ID is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Allocate a table or method owner ID. The owner ID is used to
 *              track objects created by the table or method, to be deleted
 *              when the method exits or the table is unloaded.
 *
 ******************************************************************************/
acpi_status acpi_ut_allocate_owner_id(acpi_owner_id *owner_id)
{
	u32 i;
	u32 j;
	u32 k;
	acpi_status status;

	ACPI_FUNCTION_TRACE(ut_allocate_owner_id);

	/* Guard against multiple allocations of ID to the same location */

	if (*owner_id) {
		ACPI_ERROR((AE_INFO,
			    "Owner ID [0x%2.2X] already exists", *owner_id));
		return_ACPI_STATUS(AE_ALREADY_EXISTS);
	}

	/* Mutex for the global ID mask */

	status = acpi_ut_acquire_mutex(ACPI_MTX_CACHES);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Find a free owner ID, cycle through all possible IDs on repeated
	 * allocations. (ACPI_NUM_OWNERID_MASKS + 1) because first index
	 * may have to be scanned twice.
	 */
	for (i = 0, j = acpi_gbl_last_owner_id_index;
	     i < (ACPI_NUM_OWNERID_MASKS + 1); i++, j++) {
		if (j >= ACPI_NUM_OWNERID_MASKS) {
			j = 0;	/* Wraparound to start of mask array */
		}

		for (k = acpi_gbl_next_owner_id_offset; k < 32; k++) {
			if (acpi_gbl_owner_id_mask[j] == ACPI_UINT32_MAX) {

				/* There are no free IDs in this mask */

				break;
			}

			/*
			 * Note: the u32 cast ensures that 1 is stored as a unsigned
			 * integer. Omitting the cast may result in 1 being stored as an
			 * int. Some compilers or runtime error detection may flag this as
			 * an error.
			 */
			if (!(acpi_gbl_owner_id_mask[j] & ((u32)1 << k))) {
				/*
				 * Found a free ID. The actual ID is the bit index plus one,
				 * making zero an invalid Owner ID. Save this as the last ID
				 * allocated and update the global ID mask.
				 */
				acpi_gbl_owner_id_mask[j] |= ((u32)1 << k);

				acpi_gbl_last_owner_id_index = (u8)j;
				acpi_gbl_next_owner_id_offset = (u8)(k + 1);

				/*
				 * Construct encoded ID from the index and bit position
				 *
				 * Note: Last [j].k (bit 255) is never used and is marked
				 * permanently allocated (prevents +1 overflow)
				 */
				*owner_id =
				    (acpi_owner_id)((k + 1) + ACPI_MUL_32(j));

				ACPI_DEBUG_PRINT((ACPI_DB_VALUES,
						  "Allocated OwnerId: %2.2X\n",
						  (unsigned int)*owner_id));
				goto exit;
			}
		}

		acpi_gbl_next_owner_id_offset = 0;
	}

	/*
	 * All owner_ids have been allocated. This typically should
	 * not happen since the IDs are reused after deallocation. The IDs are
	 * allocated upon table load (one per table) and method execution, and
	 * they are released when a table is unloaded or a method completes
	 * execution.
	 *
	 * If this error happens, there may be very deep nesting of invoked
	 * control methods, or there may be a bug where the IDs are not released.
	 */
	status = AE_OWNER_ID_LIMIT;
	ACPI_ERROR((AE_INFO,
		    "Could not allocate new OwnerId (255 max), AE_OWNER_ID_LIMIT"));

exit:
	(void)acpi_ut_release_mutex(ACPI_MTX_CACHES);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_release_owner_id
 *
 * PARAMETERS:  owner_id_ptr        - Pointer to a previously allocated owner_ID
 *
 * RETURN:      None. No error is returned because we are either exiting a
 *              control method or unloading a table. Either way, we would
 *              ignore any error anyway.
 *
 * DESCRIPTION: Release a table or method owner ID. Valid IDs are 1 - 255
 *
 ******************************************************************************/

void acpi_ut_release_owner_id(acpi_owner_id *owner_id_ptr)
{
	acpi_owner_id owner_id = *owner_id_ptr;
	acpi_status status;
	u32 index;
	u32 bit;

	ACPI_FUNCTION_TRACE_U32(ut_release_owner_id, owner_id);

	/* Always clear the input owner_id (zero is an invalid ID) */

	*owner_id_ptr = 0;

	/* Zero is not a valid owner_ID */

	if (owner_id == 0) {
		ACPI_ERROR((AE_INFO, "Invalid OwnerId: 0x%2.2X", owner_id));
		return_VOID;
	}

	/* Mutex for the global ID mask */

	status = acpi_ut_acquire_mutex(ACPI_MTX_CACHES);
	if (ACPI_FAILURE(status)) {
		return_VOID;
	}

	/* Normalize the ID to zero */

	owner_id--;

	/* Decode ID to index/offset pair */

	index = ACPI_DIV_32(owner_id);
	bit = (u32)1 << ACPI_MOD_32(owner_id);

	/* Free the owner ID only if it is valid */

	if (acpi_gbl_owner_id_mask[index] & bit) {
		acpi_gbl_owner_id_mask[index] ^= bit;
	} else {
		ACPI_ERROR((AE_INFO,
			    "Release of non-allocated OwnerId: 0x%2.2X",
			    owner_id + 1));
	}

	(void)acpi_ut_release_mutex(ACPI_MTX_CACHES);
	return_VOID;
}
