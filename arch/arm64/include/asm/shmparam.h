/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_SHMPARAM_H
#define __ASM_SHMPARAM_H

/*
 * For IPC syscalls from compat tasks, we need to use the legacy 16k
 * alignment value. Since we don't have aliasing D-caches, the rest of
 * the time we can safely use PAGE_SIZE.
 */
#define COMPAT_SHMLBA	(4 * PAGE_SIZE)

#include <asm-generic/shmparam.h>

#endif /* __ASM_SHMPARAM_H */
