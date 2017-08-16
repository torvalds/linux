/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, 2015, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef _LUSTRE_HA_H
#define _LUSTRE_HA_H

/** \defgroup ha ha
 *
 * @{
 */

struct obd_import;
struct obd_export;
struct obd_device;
struct ptlrpc_request;

int ptlrpc_replay(struct obd_import *imp);
int ptlrpc_resend(struct obd_import *imp);
void ptlrpc_free_committed(struct obd_import *imp);
void ptlrpc_wake_delayed(struct obd_import *imp);
int ptlrpc_recover_import(struct obd_import *imp, char *new_uuid, int async);
int ptlrpc_set_import_active(struct obd_import *imp, int active);
void ptlrpc_activate_import(struct obd_import *imp);
void ptlrpc_deactivate_import(struct obd_import *imp);
void ptlrpc_invalidate_import(struct obd_import *imp);
void ptlrpc_fail_import(struct obd_import *imp, __u32 conn_cnt);
void ptlrpc_pinger_force(struct obd_import *imp);

/** @} ha */

#endif
