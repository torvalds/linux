/*
 * Copyright (C) 2004 Red Hat, Inc. All rights reserved.
 *
 * This file is released under the GPL.
 *
 * Multipath hardware handler registration.
 */

#ifndef	DM_HW_HANDLER_H
#define	DM_HW_HANDLER_H

#include <linux/device-mapper.h>

#include "dm-mpath.h"

struct hw_handler_type;
struct hw_handler {
	struct hw_handler_type *type;
	void *context;
};

/*
 * Constructs a hardware handler object, takes custom arguments
 */
/* Information about a hardware handler type */
struct hw_handler_type {
	char *name;
	struct module *module;

	int (*create) (struct hw_handler *handler, unsigned int argc,
		       char **argv);
	void (*destroy) (struct hw_handler *hwh);

	void (*pg_init) (struct hw_handler *hwh, unsigned bypassed,
			 struct path *path);
	unsigned (*error) (struct hw_handler *hwh, struct bio *bio);
	int (*status) (struct hw_handler *hwh, status_type_t type,
		       char *result, unsigned int maxlen);
};

/* Register a hardware handler */
int dm_register_hw_handler(struct hw_handler_type *type);

/* Unregister a hardware handler */
int dm_unregister_hw_handler(struct hw_handler_type *type);

/* Returns a registered hardware handler type */
struct hw_handler_type *dm_get_hw_handler(const char *name);

/* Releases a hardware handler  */
void dm_put_hw_handler(struct hw_handler_type *hwht);

/* Default err function */
unsigned dm_scsi_err_handler(struct hw_handler *hwh, struct bio *bio);

/* Error flags for err and dm_pg_init_complete */
#define MP_FAIL_PATH 1
#define MP_BYPASS_PG 2
#define MP_ERROR_IO  4	/* Don't retry this I/O */

#endif
