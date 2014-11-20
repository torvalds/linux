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
 * Copyright (c) 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/fid/fid_lib.c
 *
 * Miscellaneous fid functions.
 *
 * Author: Nikita Danilov <nikita@clusterfs.com>
 * Author: Yury Umanets <umka@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_FID

#include "../../include/linux/libcfs/libcfs.h"
#include <linux/module.h>
#include "../include/lustre/lustre_idl.h"
#include "../include/lustre_fid.h"

/**
 * A cluster-wide range from which fid-sequences are granted to servers and
 * then clients.
 *
 * Fid namespace:
 * <pre>
 * Normal FID:        seq:64 [2^33,2^64-1]      oid:32          ver:32
 * IGIF      :        0:32, ino:32              gen:32          0:32
 * IDIF      :        0:31, 1:1, ost-index:16,  objd:48         0:32
 * </pre>
 *
 * The first 0x400 sequences of normal FID are reserved for special purpose.
 * FID_SEQ_START + 1 is for local file id generation.
 * FID_SEQ_START + 2 is for .lustre directory and its objects
 */
const struct lu_seq_range LUSTRE_SEQ_SPACE_RANGE = {
	FID_SEQ_NORMAL,
	(__u64)~0ULL
};
EXPORT_SYMBOL(LUSTRE_SEQ_SPACE_RANGE);

/* Zero range, used for init and other purposes. */
const struct lu_seq_range LUSTRE_SEQ_ZERO_RANGE = {
	0,
	0
};
EXPORT_SYMBOL(LUSTRE_SEQ_ZERO_RANGE);

/* Lustre Big Fs Lock fid. */
const struct lu_fid LUSTRE_BFL_FID = { .f_seq = FID_SEQ_SPECIAL,
				       .f_oid = FID_OID_SPECIAL_BFL,
				       .f_ver = 0x0000000000000000 };
EXPORT_SYMBOL(LUSTRE_BFL_FID);

/** Special fid for ".lustre" directory */
const struct lu_fid LU_DOT_LUSTRE_FID = { .f_seq = FID_SEQ_DOT_LUSTRE,
					  .f_oid = FID_OID_DOT_LUSTRE,
					  .f_ver = 0x0000000000000000 };
EXPORT_SYMBOL(LU_DOT_LUSTRE_FID);

/** Special fid for "fid" special object in .lustre */
const struct lu_fid LU_OBF_FID = { .f_seq = FID_SEQ_DOT_LUSTRE,
				   .f_oid = FID_OID_DOT_LUSTRE_OBF,
				   .f_ver = 0x0000000000000000 };
EXPORT_SYMBOL(LU_OBF_FID);
