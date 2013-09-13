/*
 * Copyright (C) 2013 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARC_ASM_SHMPARAM_H
#define __ARC_ASM_SHMPARAM_H

/* Handle upto 2 cache bins */
#define	SHMLBA	(2 * PAGE_SIZE)

/* Enforce SHMLBA in shmat */
#define __ARCH_FORCE_SHMLBA

#endif
