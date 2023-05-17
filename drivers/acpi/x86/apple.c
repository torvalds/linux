// SPDX-License-Identifier: GPL-2.0-only
/*
 * apple.c - Apple ACPI quirks
 * Copyright (C) 2017 Lukas Wunner <lukas@wunner.de>
 */

#include <linux/acpi.h>
#include <linux/bitmap.h>
#include <linux/platform_data/x86/apple.h>
#include <linux/uuid.h>
#include "../internal.h"

/* Apple _DSM device properties GUID */
static const guid_t apple_prp_guid =
	GUID_INIT(0xa0b5b7c6, 0x1318, 0x441c,
		  0xb0, 0xc9, 0xfe, 0x69, 0x5e, 0xaf, 0x94, 0x9b);

/**
 * acpi_extract_apple_properties - retrieve and convert Apple _DSM properties
 * @adev: ACPI device for which to retrieve the properties
 *
 * Invoke Apple's custom _DSM once to check the protocol version and once more
 * to retrieve the properties.  They are marshalled up in a single package as
 * alternating key/value elements, unlike _DSD which stores them as a package
 * of 2-element packages.  Convert to _DSD format and make them available under
 * the primary fwnode.
 */
void acpi_extract_apple_properties(struct acpi_device *adev)
{
	unsigned int i, j = 0, newsize = 0, numprops, numvalid;
	union acpi_object *props, *newprops;
	unsigned long *valid = NULL;
	void *free_space;

	if (!x86_apple_machine)
		return;

	props = acpi_evaluate_dsm_typed(adev->handle, &apple_prp_guid, 1, 0,
					NULL, ACPI_TYPE_BUFFER);
	if (!props)
		return;

	if (!props->buffer.length)
		goto out_free;

	if (props->buffer.pointer[0] != 3) {
		acpi_handle_info(adev->handle, FW_INFO
				 "unsupported properties version %*ph\n",
				 props->buffer.length, props->buffer.pointer);
		goto out_free;
	}

	ACPI_FREE(props);
	props = acpi_evaluate_dsm_typed(adev->handle, &apple_prp_guid, 1, 1,
					NULL, ACPI_TYPE_PACKAGE);
	if (!props)
		return;

	numprops = props->package.count / 2;
	if (!numprops)
		goto out_free;

	valid = bitmap_zalloc(numprops, GFP_KERNEL);
	if (!valid)
		goto out_free;

	/* newsize = key length + value length of each tuple */
	for (i = 0; i < numprops; i++) {
		union acpi_object *key = &props->package.elements[i * 2];
		union acpi_object *val = &props->package.elements[i * 2 + 1];

		if ( key->type != ACPI_TYPE_STRING ||
		    (val->type != ACPI_TYPE_INTEGER &&
		     val->type != ACPI_TYPE_BUFFER &&
		     val->type != ACPI_TYPE_STRING))
			continue; /* skip invalid properties */

		__set_bit(i, valid);
		newsize += key->string.length + 1;
		if ( val->type == ACPI_TYPE_BUFFER)
			newsize += val->buffer.length;
		else if (val->type == ACPI_TYPE_STRING)
			newsize += val->string.length + 1;
	}

	numvalid = bitmap_weight(valid, numprops);
	if (numprops > numvalid)
		acpi_handle_info(adev->handle, FW_INFO
				 "skipped %u properties: wrong type\n",
				 numprops - numvalid);
	if (numvalid == 0)
		goto out_free;

	/* newsize += top-level package + 3 objects for each key/value tuple */
	newsize	+= (1 + 3 * numvalid) * sizeof(union acpi_object);
	newprops = ACPI_ALLOCATE_ZEROED(newsize);
	if (!newprops)
		goto out_free;

	/* layout: top-level package | packages | key/value tuples | strings */
	newprops->type = ACPI_TYPE_PACKAGE;
	newprops->package.count = numvalid;
	newprops->package.elements = &newprops[1];
	free_space = &newprops[1 + 3 * numvalid];

	for_each_set_bit(i, valid, numprops) {
		union acpi_object *key = &props->package.elements[i * 2];
		union acpi_object *val = &props->package.elements[i * 2 + 1];
		unsigned int k = 1 + numvalid + j * 2; /* index into newprops */
		unsigned int v = k + 1;

		newprops[1 + j].type = ACPI_TYPE_PACKAGE;
		newprops[1 + j].package.count = 2;
		newprops[1 + j].package.elements = &newprops[k];

		newprops[k].type = ACPI_TYPE_STRING;
		newprops[k].string.length = key->string.length;
		newprops[k].string.pointer = free_space;
		memcpy(free_space, key->string.pointer, key->string.length);
		free_space += key->string.length + 1;

		newprops[v].type = val->type;
		if (val->type == ACPI_TYPE_INTEGER) {
			newprops[v].integer.value = val->integer.value;
		} else if (val->type == ACPI_TYPE_STRING) {
			newprops[v].string.length = val->string.length;
			newprops[v].string.pointer = free_space;
			memcpy(free_space, val->string.pointer,
			       val->string.length);
			free_space += val->string.length + 1;
		} else {
			newprops[v].buffer.length = val->buffer.length;
			newprops[v].buffer.pointer = free_space;
			memcpy(free_space, val->buffer.pointer,
			       val->buffer.length);
			free_space += val->buffer.length;
		}
		j++; /* count valid properties */
	}
	WARN_ON(free_space != (void *)newprops + newsize);

	adev->data.pointer = newprops;
	acpi_data_add_props(&adev->data, &apple_prp_guid, newprops);

out_free:
	ACPI_FREE(props);
	bitmap_free(valid);
}
