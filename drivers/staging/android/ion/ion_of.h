/*
 * Based on work from:
 *   Andrew Andrianov <andrew@ncrmnt.org>
 *   Google
 *   The Linux Foundation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ION_OF_H
#define _ION_OF_H

struct ion_of_heap {
	const char *compat;
	int heap_id;
	int type;
	const char *name;
	int align;
};

#define PLATFORM_HEAP(_compat, _id, _type, _name) \
{ \
	.compat = _compat, \
	.heap_id = _id, \
	.type = _type, \
	.name = _name, \
	.align = PAGE_SIZE, \
}

struct ion_platform_data *ion_parse_dt(struct platform_device *pdev,
					struct ion_of_heap *compatible);

void ion_destroy_platform_data(struct ion_platform_data *data);

#endif
