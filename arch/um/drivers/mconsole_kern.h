/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2001, 2002 Jeff Dike (jdike@karaya.com)
 */

#ifndef __MCONSOLE_KERN_H__
#define __MCONSOLE_KERN_H__

#include <linux/list.h>
#include "mconsole.h"

struct mconsole_entry {
	struct list_head list;
	struct mc_request request;
};

/* All these methods are called in process context. */
struct mc_device {
	struct list_head list;
	char *name;
	int (*config)(char *, char **);
	int (*get_config)(char *, char *, int, char **);
	int (*id)(char **, int *, int *);
	int (*remove)(int, char **);
};

#define CONFIG_CHUNK(str, size, current, chunk, end) \
do { \
	current += strlen(chunk); \
	if(current >= size) \
		str = NULL; \
	if(str != NULL){ \
		strcpy(str, chunk); \
		str += strlen(chunk); \
	} \
	if(end) \
		current++; \
} while(0)

#ifdef CONFIG_MCONSOLE

extern void mconsole_register_dev(struct mc_device *new);

#else

static inline void mconsole_register_dev(struct mc_device *new)
{
}

#endif

#endif
