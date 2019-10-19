/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2013 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef __ARC_ASM_SHMPARAM_H
#define __ARC_ASM_SHMPARAM_H

/* Handle upto 2 cache bins */
#define	SHMLBA	(2 * PAGE_SIZE)

/* Enforce SHMLBA in shmat */
#define __ARCH_FORCE_SHMLBA

#endif
