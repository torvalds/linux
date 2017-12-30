// SPDX-License-Identifier: GPL-2.0
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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2011, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#ifndef LUSTRE_INTENT_H
#define LUSTRE_INTENT_H

/* intent IT_XXX are defined in lustre/include/obd.h */

struct lookup_intent {
	int		it_op;
	int		it_create_mode;
	__u64		it_flags;
	int		it_disposition;
	int		it_status;
	__u64		it_lock_handle;
	__u64		it_lock_bits;
	int		it_lock_mode;
	int		it_remote_lock_mode;
	__u64	   it_remote_lock_handle;
	struct ptlrpc_request *it_request;
	unsigned int    it_lock_set:1;
};

static inline int it_disposition(struct lookup_intent *it, int flag)
{
	return it->it_disposition & flag;
}

static inline void it_set_disposition(struct lookup_intent *it, int flag)
{
	it->it_disposition |= flag;
}

static inline void it_clear_disposition(struct lookup_intent *it, int flag)
{
	it->it_disposition &= ~flag;
}

#endif
