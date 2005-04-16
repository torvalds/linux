/* cache.h: AFS local cache management interface
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_AFS_CACHE_H
#define _LINUX_AFS_CACHE_H

#undef AFS_CACHING_SUPPORT

#include <linux/mm.h>
#ifdef AFS_CACHING_SUPPORT
#include <linux/cachefs.h>
#endif
#include "types.h"

#ifdef __KERNEL__

#endif /* __KERNEL__ */

#endif /* _LINUX_AFS_CACHE_H */
