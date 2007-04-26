/* AFS Cache Manager Service declarations
 *
 * Copyright (C) 2002 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef AFS_CMSERVICE_H
#define AFS_CMSERVICE_H

#include <rxrpc/transport.h>
#include "types.h"

/* cache manager start/stop */
extern int afscm_start(void);
extern void afscm_stop(void);

/* cache manager server functions */
extern int SRXAFSCM_InitCallBackState(struct afs_server *);
extern int SRXAFSCM_CallBack(struct afs_server *, size_t,
			     struct afs_callback[]);
extern int SRXAFSCM_Probe(struct afs_server *);

#endif /* AFS_CMSERVICE_H */
