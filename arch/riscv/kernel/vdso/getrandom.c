// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Xi Ruoyao <xry111@xry111.site>. All Rights Reserved.
 */
#include <linux/types.h>

ssize_t __vdso_getrandom(void *buffer, size_t len, unsigned int flags, void *opaque_state, size_t opaque_len)
{
	return __cvdso_getrandom(buffer, len, flags, opaque_state, opaque_len);
}
