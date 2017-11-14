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
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * #defines shared between socknal implementation and utilities
 */
#ifndef __UAPI_LNET_SOCKLND_H__
#define __UAPI_LNET_SOCKLND_H__

#define SOCKLND_CONN_NONE     (-1)
#define SOCKLND_CONN_ANY	0
#define SOCKLND_CONN_CONTROL	1
#define SOCKLND_CONN_BULK_IN	2
#define SOCKLND_CONN_BULK_OUT	3
#define SOCKLND_CONN_NTYPES	4

#define SOCKLND_CONN_ACK	SOCKLND_CONN_BULK_IN

#endif
