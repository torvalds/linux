/******************************************************************************
 *
 * Module Name: uttrack - Memory allocation tracking routines (debug only)
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2016, Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

/*
 * These procedures are used for tracking memory leaks in the subsystem, and
 * they get compiled out when the ACPI_DBG_TRACK_ALLOCATIONS is not set.
 *
 * Each memory allocation is tracked via a doubly linked list. Each
 * element contains the caller's component, module name, function name, and
 * line number. acpi_ut_allocate and acpi_ut_allocate_zeroed call
 * acpi_ut_track_allocation to add an element to the list; deletion
 * occurs in the body of acpi_ut_free.
 */

#include <acpi/acpi.h>
#include "accommon.h"

#ifdef ACPI_DBG_TRACK_ALLOCATIONS

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("uttrack")

/* Local prototypes */
static struct acpi_debug_mem_block *acpi_ut_find_allocation(struct
							    acpi_debug_mem_block
							    *allocation);

static acpi_status
acpi_ut_track_allocation(struct acpi_debug_mem_block *address,
			 acpi_size size,
			 u8 alloc_type,
			 u32 component, const char *module, u32 line);

static acpi_status
acpi_ut_remove_allocation(struct acpi_debug_mem_block *address,
			  u32 component, const char *module, u32 line);

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_create_list
 *
 * PARAMETERS:  cache_name      - Ascii name for the cache
 *              object_size     - Size of each cached object
 *              return_cache    - Where the new cache object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a local memory list for tracking purposed
 *
 ******************************************************************************/

acpi_status
acpi_ut_create_list(char *list_name,
		    u16 object_size, struct acpi_memory_list **return_cache)
{
	struct acpi_memory_list *cache;

	cache = acpi_os_allocate(sizeof(struct acpi_memory_list));
	if (!cache) {
		return (AE_NO_MEMORY);
	}

	memset(cache, 0, sizeof(struct acpi_memory_list));

	cache->list_name = list_name;
	cache->object_size = object_size;

	*return_cache = cache;
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_allocate_and_track
 *
 * PARAMETERS:  size                - Size of the allocation
 *              component           - Component type of caller
 *              module              - Source file name of caller
 *              line                - Line number of caller
 *
 * RETURN:      Address of the allocated memory on success, NULL on failure.
 *
 * DESCRIPTION: The subsystem's equivalent of malloc.
 *
 ******************************************************************************/

void *acpi_ut_allocate_and_track(acpi_size size,
				 u32 component, const char *module, u32 line)
{
	struct acpi_debug_mem_block *allocation;
	acpi_status status;

	/* Check for an inadvertent size of zero bytes */

	if (!size) {
		ACPI_WARNING((module, line,
			      "Attempt to allocate zero bytes, allocating 1 byte"));
		size = 1;
	}

	allocation =
	    acpi_os_allocate(size + sizeof(struct acpi_debug_mem_header));
	if (!allocation) {

		/* Report allocation error */

		ACPI_WARNING((module, line,
			      "Could not allocate size %u", (u32)size));

		return (NULL);
	}

	status =
	    acpi_ut_track_allocation(allocation, size, ACPI_MEM_MALLOC,
				     component, module, line);
	if (ACPI_FAILURE(status)) {
		acpi_os_free(allocation);
		return (NULL);
	}

	acpi_gbl_global_list->total_allocated++;
	acpi_gbl_global_list->total_size += (u32)size;
	acpi_gbl_global_list->current_total_size += (u32)size;

	if (acpi_gbl_global_list->current_total_size >
	    acpi_gbl_global_list->max_occupied) {
		acpi_gbl_global_list->max_occupied =
		    acpi_gbl_global_list->current_total_size;
	}

	return ((void *)&allocation->user_space);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_allocate_zeroed_and_track
 *
 * PARAMETERS:  size                - Size of the allocation
 *              component           - Component type of caller
 *              module              - Source file name of caller
 *              line                - Line number of caller
 *
 * RETURN:      Address of the allocated memory on success, NULL on failure.
 *
 * DESCRIPTION: Subsystem equivalent of calloc.
 *
 ******************************************************************************/

void *acpi_ut_allocate_zeroed_and_track(acpi_size size,
					u32 component,
					const char *module, u32 line)
{
	struct acpi_debug_mem_block *allocation;
	acpi_status status;

	/* Check for an inadvertent size of zero bytes */

	if (!size) {
		ACPI_WARNING((module, line,
			      "Attempt to allocate zero bytes, allocating 1 byte"));
		size = 1;
	}

	allocation =
	    acpi_os_allocate_zeroed(size +
				    sizeof(struct acpi_debug_mem_header));
	if (!allocation) {

		/* Report allocation error */

		ACPI_ERROR((module, line,
			    "Could not allocate size %u", (u32)size));
		return (NULL);
	}

	status = acpi_ut_track_allocation(allocation, size,
					  ACPI_MEM_CALLOC, component, module,
					  line);
	if (ACPI_FAILURE(status)) {
		acpi_os_free(allocation);
		return (NULL);
	}

	acpi_gbl_global_list->total_allocated++;
	acpi_gbl_global_list->total_size += (u32)size;
	acpi_gbl_global_list->current_total_size += (u32)size;

	if (acpi_gbl_global_list->current_total_size >
	    acpi_gbl_global_list->max_occupied) {
		acpi_gbl_global_list->max_occupied =
		    acpi_gbl_global_list->current_total_size;
	}

	return ((void *)&allocation->user_space);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_free_and_track
 *
 * PARAMETERS:  allocation          - Address of the memory to deallocate
 *              component           - Component type of caller
 *              module              - Source file name of caller
 *              line                - Line number of caller
 *
 * RETURN:      None
 *
 * DESCRIPTION: Frees the memory at Allocation
 *
 ******************************************************************************/

void
acpi_ut_free_and_track(void *allocation,
		       u32 component, const char *module, u32 line)
{
	struct acpi_debug_mem_block *debug_block;
	acpi_status status;

	ACPI_FUNCTION_TRACE_PTR(ut_free, allocation);

	if (NULL == allocation) {
		ACPI_ERROR((module, line, "Attempt to delete a NULL address"));

		return_VOID;
	}

	debug_block = ACPI_CAST_PTR(struct acpi_debug_mem_block,
				    (((char *)allocation) -
				     sizeof(struct acpi_debug_mem_header)));

	acpi_gbl_global_list->total_freed++;
	acpi_gbl_global_list->current_total_size -= debug_block->size;

	status =
	    acpi_ut_remove_allocation(debug_block, component, module, line);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Could not free memory"));
	}

	acpi_os_free(debug_block);
	ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS, "%p freed (block %p)\n",
			  allocation, debug_block));
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_find_allocation
 *
 * PARAMETERS:  allocation              - Address of allocated memory
 *
 * RETURN:      Three cases:
 *              1) List is empty, NULL is returned.
 *              2) Element was found. Returns Allocation parameter.
 *              3) Element was not found. Returns position where it should be
 *                  inserted into the list.
 *
 * DESCRIPTION: Searches for an element in the global allocation tracking list.
 *              If the element is not found, returns the location within the
 *              list where the element should be inserted.
 *
 *              Note: The list is ordered by larger-to-smaller addresses.
 *
 *              This global list is used to detect memory leaks in ACPICA as
 *              well as other issues such as an attempt to release the same
 *              internal object more than once. Although expensive as far
 *              as cpu time, this list is much more helpful for finding these
 *              types of issues than using memory leak detectors outside of
 *              the ACPICA code.
 *
 ******************************************************************************/

static struct acpi_debug_mem_block *acpi_ut_find_allocation(struct
							    acpi_debug_mem_block
							    *allocation)
{
	struct acpi_debug_mem_block *element;

	element = acpi_gbl_global_list->list_head;
	if (!element) {
		return (NULL);
	}

	/*
	 * Search for the address.
	 *
	 * Note: List is ordered by larger-to-smaller addresses, on the
	 * assumption that a new allocation usually has a larger address
	 * than previous allocations.
	 */
	while (element > allocation) {

		/* Check for end-of-list */

		if (!element->next) {
			return (element);
		}

		element = element->next;
	}

	if (element == allocation) {
		return (element);
	}

	return (element->previous);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_track_allocation
 *
 * PARAMETERS:  allocation          - Address of allocated memory
 *              size                - Size of the allocation
 *              alloc_type          - MEM_MALLOC or MEM_CALLOC
 *              component           - Component type of caller
 *              module              - Source file name of caller
 *              line                - Line number of caller
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Inserts an element into the global allocation tracking list.
 *
 ******************************************************************************/

static acpi_status
acpi_ut_track_allocation(struct acpi_debug_mem_block *allocation,
			 acpi_size size,
			 u8 alloc_type,
			 u32 component, const char *module, u32 line)
{
	struct acpi_memory_list *mem_list;
	struct acpi_debug_mem_block *element;
	acpi_status status = AE_OK;

	ACPI_FUNCTION_TRACE_PTR(ut_track_allocation, allocation);

	if (acpi_gbl_disable_mem_tracking) {
		return_ACPI_STATUS(AE_OK);
	}

	mem_list = acpi_gbl_global_list;
	status = acpi_ut_acquire_mutex(ACPI_MTX_MEMORY);
	if (ACPI_FAILURE(status)) {
		return_ACPI_STATUS(status);
	}

	/*
	 * Search the global list for this address to make sure it is not
	 * already present. This will catch several kinds of problems.
	 */
	element = acpi_ut_find_allocation(allocation);
	if (element == allocation) {
		ACPI_ERROR((AE_INFO,
			    "UtTrackAllocation: Allocation (%p) already present in global list!",
			    allocation));
		goto unlock_and_exit;
	}

	/* Fill in the instance data */

	allocation->size = (u32)size;
	allocation->alloc_type = alloc_type;
	allocation->component = component;
	allocation->line = line;

	strncpy(allocation->module, module, ACPI_MAX_MODULE_NAME);
	allocation->module[ACPI_MAX_MODULE_NAME - 1] = 0;

	if (!element) {

		/* Insert at list head */

		if (mem_list->list_head) {
			((struct acpi_debug_mem_block *)(mem_list->list_head))->
			    previous = allocation;
		}

		allocation->next = mem_list->list_head;
		allocation->previous = NULL;

		mem_list->list_head = allocation;
	} else {
		/* Insert after element */

		allocation->next = element->next;
		allocation->previous = element;

		if (element->next) {
			(element->next)->previous = allocation;
		}

		element->next = allocation;
	}

unlock_and_exit:
	status = acpi_ut_release_mutex(ACPI_MTX_MEMORY);
	return_ACPI_STATUS(status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_remove_allocation
 *
 * PARAMETERS:  allocation          - Address of allocated memory
 *              component           - Component type of caller
 *              module              - Source file name of caller
 *              line                - Line number of caller
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Deletes an element from the global allocation tracking list.
 *
 ******************************************************************************/

static acpi_status
acpi_ut_remove_allocation(struct acpi_debug_mem_block *allocation,
			  u32 component, const char *module, u32 line)
{
	struct acpi_memory_list *mem_list;
	acpi_status status;

	ACPI_FUNCTION_NAME(ut_remove_allocation);

	if (acpi_gbl_disable_mem_tracking) {
		return (AE_OK);
	}

	mem_list = acpi_gbl_global_list;
	if (NULL == mem_list->list_head) {

		/* No allocations! */

		ACPI_ERROR((module, line,
			    "Empty allocation list, nothing to free!"));

		return (AE_OK);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_MEMORY);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/* Unlink */

	if (allocation->previous) {
		(allocation->previous)->next = allocation->next;
	} else {
		mem_list->list_head = allocation->next;
	}

	if (allocation->next) {
		(allocation->next)->previous = allocation->previous;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_ALLOCATIONS, "Freeing %p, size 0%X\n",
			  &allocation->user_space, allocation->size));

	/* Mark the segment as deleted */

	memset(&allocation->user_space, 0xEA, allocation->size);

	status = acpi_ut_release_mutex(ACPI_MTX_MEMORY);
	return (status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_dump_allocation_info
 *
 * PARAMETERS:  None
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print some info about the outstanding allocations.
 *
 ******************************************************************************/

void acpi_ut_dump_allocation_info(void)
{
/*
	struct acpi_memory_list         *mem_list;
*/

	ACPI_FUNCTION_TRACE(ut_dump_allocation_info);

/*
	ACPI_DEBUG_PRINT (TRACE_ALLOCATIONS | TRACE_TABLES,
		("%30s: %4d (%3d Kb)\n", "Current allocations",
		mem_list->current_count,
		ROUND_UP_TO_1K (mem_list->current_size)));

	ACPI_DEBUG_PRINT (TRACE_ALLOCATIONS | TRACE_TABLES,
		("%30s: %4d (%3d Kb)\n", "Max concurrent allocations",
		mem_list->max_concurrent_count,
		ROUND_UP_TO_1K (mem_list->max_concurrent_size)));

	ACPI_DEBUG_PRINT (TRACE_ALLOCATIONS | TRACE_TABLES,
		("%30s: %4d (%3d Kb)\n", "Total (all) internal objects",
		running_object_count,
		ROUND_UP_TO_1K (running_object_size)));

	ACPI_DEBUG_PRINT (TRACE_ALLOCATIONS | TRACE_TABLES,
		("%30s: %4d (%3d Kb)\n", "Total (all) allocations",
		running_alloc_count,
		ROUND_UP_TO_1K (running_alloc_size)));

	ACPI_DEBUG_PRINT (TRACE_ALLOCATIONS | TRACE_TABLES,
		("%30s: %4d (%3d Kb)\n", "Current Nodes",
		acpi_gbl_current_node_count,
		ROUND_UP_TO_1K (acpi_gbl_current_node_size)));

	ACPI_DEBUG_PRINT (TRACE_ALLOCATIONS | TRACE_TABLES,
		("%30s: %4d (%3d Kb)\n", "Max Nodes",
		acpi_gbl_max_concurrent_node_count,
		ROUND_UP_TO_1K ((acpi_gbl_max_concurrent_node_count *
			sizeof (struct acpi_namespace_node)))));
*/
	return_VOID;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_dump_allocations
 *
 * PARAMETERS:  component           - Component(s) to dump info for.
 *              module              - Module to dump info for. NULL means all.
 *
 * RETURN:      None
 *
 * DESCRIPTION: Print a list of all outstanding allocations.
 *
 ******************************************************************************/

void acpi_ut_dump_allocations(u32 component, const char *module)
{
	struct acpi_debug_mem_block *element;
	union acpi_descriptor *descriptor;
	u32 num_outstanding = 0;
	u8 descriptor_type;

	ACPI_FUNCTION_TRACE(ut_dump_allocations);

	if (acpi_gbl_disable_mem_tracking) {
		return_VOID;
	}

	/*
	 * Walk the allocation list.
	 */
	if (ACPI_FAILURE(acpi_ut_acquire_mutex(ACPI_MTX_MEMORY))) {
		return_VOID;
	}

	element = acpi_gbl_global_list->list_head;
	while (element) {
		if ((element->component & component) &&
		    ((module == NULL)
		     || (0 == strcmp(module, element->module)))) {
			descriptor =
			    ACPI_CAST_PTR(union acpi_descriptor,
					  &element->user_space);

			if (element->size <
			    sizeof(struct acpi_common_descriptor)) {
				acpi_os_printf("%p Length 0x%04X %9.9s-%u "
					       "[Not a Descriptor - too small]\n",
					       descriptor, element->size,
					       element->module, element->line);
			} else {
				/* Ignore allocated objects that are in a cache */

				if (ACPI_GET_DESCRIPTOR_TYPE(descriptor) !=
				    ACPI_DESC_TYPE_CACHED) {
					acpi_os_printf
					    ("%p Length 0x%04X %9.9s-%u [%s] ",
					     descriptor, element->size,
					     element->module, element->line,
					     acpi_ut_get_descriptor_name
					     (descriptor));

					/* Validate the descriptor type using Type field and length */

					descriptor_type = 0;	/* Not a valid descriptor type */

					switch (ACPI_GET_DESCRIPTOR_TYPE
						(descriptor)) {
					case ACPI_DESC_TYPE_OPERAND:

						if (element->size ==
						    sizeof(union
							   acpi_operand_object))
						{
							descriptor_type =
							    ACPI_DESC_TYPE_OPERAND;
						}
						break;

					case ACPI_DESC_TYPE_PARSER:

						if (element->size ==
						    sizeof(union
							   acpi_parse_object)) {
							descriptor_type =
							    ACPI_DESC_TYPE_PARSER;
						}
						break;

					case ACPI_DESC_TYPE_NAMED:

						if (element->size ==
						    sizeof(struct
							   acpi_namespace_node))
						{
							descriptor_type =
							    ACPI_DESC_TYPE_NAMED;
						}
						break;

					default:

						break;
					}

					/* Display additional info for the major descriptor types */

					switch (descriptor_type) {
					case ACPI_DESC_TYPE_OPERAND:

						acpi_os_printf
						    ("%12.12s RefCount 0x%04X\n",
						     acpi_ut_get_type_name
						     (descriptor->object.common.
						      type),
						     descriptor->object.common.
						     reference_count);
						break;

					case ACPI_DESC_TYPE_PARSER:

						acpi_os_printf
						    ("AmlOpcode 0x%04hX\n",
						     descriptor->op.asl.
						     aml_opcode);
						break;

					case ACPI_DESC_TYPE_NAMED:

						acpi_os_printf("%4.4s\n",
							       acpi_ut_get_node_name
							       (&descriptor->
								node));
						break;

					default:

						acpi_os_printf("\n");
						break;
					}
				}
			}

			num_outstanding++;
		}

		element = element->next;
	}

	(void)acpi_ut_release_mutex(ACPI_MTX_MEMORY);

	/* Print summary */

	if (!num_outstanding) {
		ACPI_INFO(("No outstanding allocations"));
	} else {
		ACPI_ERROR((AE_INFO, "%u(0x%X) Outstanding allocations",
			    num_outstanding, num_outstanding));
	}

	return_VOID;
}

#endif				/* ACPI_DBG_TRACK_ALLOCATIONS */
