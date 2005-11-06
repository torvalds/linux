/******************************************************************************
 *
 * Module Name: utcache - local cache allocation routines
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2005, R. Byron Moore
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

#include <acpi/acpi.h>

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utcache")

#ifdef ACPI_USE_LOCAL_CACHE
/*******************************************************************************
 *
 * FUNCTION:    acpi_os_create_cache
 *
 * PARAMETERS:  cache_name      - Ascii name for the cache
 *              object_size     - Size of each cached object
 *              max_depth       - Maximum depth of the cache (in objects)
 *              return_cache    - Where the new cache object is returned
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Create a cache object
 *
 ******************************************************************************/
acpi_status
acpi_os_create_cache(char *cache_name,
		     u16 object_size,
		     u16 max_depth, struct acpi_memory_list **return_cache)
{
	struct acpi_memory_list *cache;

	ACPI_FUNCTION_ENTRY();

	if (!cache_name || !return_cache || (object_size < 16)) {
		return (AE_BAD_PARAMETER);
	}

	/* Create the cache object */

	cache = acpi_os_allocate(sizeof(struct acpi_memory_list));
	if (!cache) {
		return (AE_NO_MEMORY);
	}

	/* Populate the cache object and return it */

	ACPI_MEMSET(cache, 0, sizeof(struct acpi_memory_list));
	cache->link_offset = 8;
	cache->list_name = cache_name;
	cache->object_size = object_size;
	cache->max_depth = max_depth;

	*return_cache = cache;
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_os_purge_cache
 *
 * PARAMETERS:  Cache           - Handle to cache object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Free all objects within the requested cache.
 *
 ******************************************************************************/

acpi_status acpi_os_purge_cache(struct acpi_memory_list * cache)
{
	char *next;

	ACPI_FUNCTION_ENTRY();

	if (!cache) {
		return (AE_BAD_PARAMETER);
	}

	/* Walk the list of objects in this cache */

	while (cache->list_head) {
		/* Delete and unlink one cached state object */

		next = *(ACPI_CAST_INDIRECT_PTR(char,
						&(((char *)cache->
						   list_head)[cache->
							      link_offset])));
		ACPI_MEM_FREE(cache->list_head);

		cache->list_head = next;
		cache->current_depth--;
	}

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_os_delete_cache
 *
 * PARAMETERS:  Cache           - Handle to cache object
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Free all objects within the requested cache and delete the
 *              cache object.
 *
 ******************************************************************************/

acpi_status acpi_os_delete_cache(struct acpi_memory_list * cache)
{
	acpi_status status;

	ACPI_FUNCTION_ENTRY();

	/* Purge all objects in the cache */

	status = acpi_os_purge_cache(cache);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/* Now we can delete the cache object */

	acpi_os_free(cache);
	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_os_release_object
 *
 * PARAMETERS:  Cache       - Handle to cache object
 *              Object      - The object to be released
 *
 * RETURN:      None
 *
 * DESCRIPTION: Release an object to the specified cache.  If cache is full,
 *              the object is deleted.
 *
 ******************************************************************************/

acpi_status
acpi_os_release_object(struct acpi_memory_list * cache, void *object)
{
	acpi_status status;

	ACPI_FUNCTION_ENTRY();

	if (!cache || !object) {
		return (AE_BAD_PARAMETER);
	}

	/* If cache is full, just free this object */

	if (cache->current_depth >= cache->max_depth) {
		ACPI_MEM_FREE(object);
		ACPI_MEM_TRACKING(cache->total_freed++);
	}

	/* Otherwise put this object back into the cache */

	else {
		status = acpi_ut_acquire_mutex(ACPI_MTX_CACHES);
		if (ACPI_FAILURE(status)) {
			return (status);
		}

		/* Mark the object as cached */

		ACPI_MEMSET(object, 0xCA, cache->object_size);
		ACPI_SET_DESCRIPTOR_TYPE(object, ACPI_DESC_TYPE_CACHED);

		/* Put the object at the head of the cache list */

		*(ACPI_CAST_INDIRECT_PTR(char,
					 &(((char *)object)[cache->
							    link_offset]))) =
		    cache->list_head;
		cache->list_head = object;
		cache->current_depth++;

		(void)acpi_ut_release_mutex(ACPI_MTX_CACHES);
	}

	return (AE_OK);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_os_acquire_object
 *
 * PARAMETERS:  Cache           - Handle to cache object
 *
 * RETURN:      the acquired object.  NULL on error
 *
 * DESCRIPTION: Get an object from the specified cache.  If cache is empty,
 *              the object is allocated.
 *
 ******************************************************************************/

void *acpi_os_acquire_object(struct acpi_memory_list *cache)
{
	acpi_status status;
	void *object;

	ACPI_FUNCTION_NAME("os_acquire_object");

	if (!cache) {
		return (NULL);
	}

	status = acpi_ut_acquire_mutex(ACPI_MTX_CACHES);
	if (ACPI_FAILURE(status)) {
		return (NULL);
	}

	ACPI_MEM_TRACKING(cache->requests++);

	/* Check the cache first */

	if (cache->list_head) {
		/* There is an object available, use it */

		object = cache->list_head;
		cache->list_head = *(ACPI_CAST_INDIRECT_PTR(char,
							    &(((char *)
							       object)[cache->
								       link_offset])));

		cache->current_depth--;

		ACPI_MEM_TRACKING(cache->hits++);
		ACPI_MEM_TRACKING(ACPI_DEBUG_PRINT((ACPI_DB_EXEC,
						    "Object %p from %s cache\n",
						    object, cache->list_name)));

		status = acpi_ut_release_mutex(ACPI_MTX_CACHES);
		if (ACPI_FAILURE(status)) {
			return (NULL);
		}

		/* Clear (zero) the previously used Object */

		ACPI_MEMSET(object, 0, cache->object_size);
	} else {
		/* The cache is empty, create a new object */

		ACPI_MEM_TRACKING(cache->total_allocated++);

		/* Avoid deadlock with ACPI_MEM_CALLOCATE */

		status = acpi_ut_release_mutex(ACPI_MTX_CACHES);
		if (ACPI_FAILURE(status)) {
			return (NULL);
		}

		object = ACPI_MEM_CALLOCATE(cache->object_size);
		if (!object) {
			return (NULL);
		}
	}

	return (object);
}
#endif				/* ACPI_USE_LOCAL_CACHE */
