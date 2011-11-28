#ifndef _LTT_WRAPPER_UUID_H
#define _LTT_WRAPPER_UUID_H

/*
 * Copyright (C) 2011 Mathieu Desnoyers (mathieu.desnoyers@efficios.com)
 *
 * Dual LGPL v2.1/GPL v2 license.
 */

#include <linux/version.h>

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,35))
#include <linux/uuid.h>
#else

#include <linux/random.h>

typedef struct {
	__u8 b[16];
} uuid_le;

static inline
void uuid_le_gen(uuid_le *u)
{
	generate_random_uuid(u->b);
}

#endif
#endif /* _LTT_WRAPPER_UUID_H */
