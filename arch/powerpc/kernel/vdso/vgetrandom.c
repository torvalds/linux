// SPDX-License-Identifier: GPL-2.0
/*
 * Powerpc userspace implementation of getrandom()
 *
 * Copyright (C) 2024 Christophe Leroy <christophe.leroy@csgroup.eu>, CS GROUP France
 */
#include <linux/time.h>
#include <linux/types.h>

ssize_t __c_kernel_getrandom(void *buffer, size_t len, unsigned int flags, void *opaque_state,
			     size_t opaque_len)
{
	return __cvdso_getrandom(buffer, len, flags, opaque_state, opaque_len);
}
