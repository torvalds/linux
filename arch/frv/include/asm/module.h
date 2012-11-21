/* module.h: FRV module stuff
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_MODULE_H
#define _ASM_MODULE_H

#include <asm-generic/module.h>

/*
 * Include the architecture version.
 */
#define MODULE_ARCH_VERMAGIC __stringify(PROCESSOR_MODEL_NAME) " "

#endif /* _ASM_MODULE_H */

