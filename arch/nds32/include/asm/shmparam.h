// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef _ASMNDS32_SHMPARAM_H
#define _ASMNDS32_SHMPARAM_H

/*
 * This should be the size of the virtually indexed cache/ways,
 * whichever is greater since the cache aliases every size/ways
 * bytes.
 */
#define	SHMLBA	(4 * SZ_8K)	/* attach addr a multiple of this */

/*
 * Enforce SHMLBA in shmat
 */
#define __ARCH_FORCE_SHMLBA

#endif /* _ASMNDS32_SHMPARAM_H */
