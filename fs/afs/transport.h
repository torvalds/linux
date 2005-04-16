/* transport.h: AFS transport management
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _LINUX_AFS_TRANSPORT_H
#define _LINUX_AFS_TRANSPORT_H

#include "types.h"
#include <rxrpc/transport.h>

/* the cache manager transport endpoint */
extern struct rxrpc_transport *afs_transport;

#endif /* _LINUX_AFS_TRANSPORT_H */
