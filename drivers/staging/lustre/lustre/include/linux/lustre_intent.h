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
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
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
struct lustre_intent_data {
	int		it_disposition;
	int		it_status;
	__u64		it_lock_handle;
	__u64		it_lock_bits;
	int		it_lock_mode;
	int		it_remote_lock_mode;
	__u64	   it_remote_lock_handle;
	void	   *it_data;
	unsigned int    it_lock_set:1;
};

struct lookup_intent {
	int     it_op;
	int     it_flags;
	int     it_create_mode;
	union {
		struct lustre_intent_data lustre;
	} d;
};

#endif
