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
 * Copyright (c) 2004, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_CLASS
#include <obd_class.h>
#include <linux/kmod.h>   /* for request_module() */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pagemap.h>
#include <lprocfs_status.h>
#include <lustre/lustre_idl.h>

static int mea_last_char_hash(int count, char *name, int namelen)
{
	unsigned int c;

	c = name[namelen - 1];
	if (c == 0)
		CWARN("looks like wrong len is passed\n");
	c = c % count;
	return c;
}

static int mea_all_chars_hash(int count, char *name, int namelen)
{
	unsigned int c = 0;

	while (--namelen >= 0)
		c += name[namelen];
	c = c % count;
	return c;
}

int raw_name2idx(int hashtype, int count, const char *name, int namelen)
{
	unsigned int	c = 0;
	int		idx;

	LASSERT(namelen > 0);

	if (filename_is_volatile(name, namelen, &idx)) {
		if ((idx >= 0) && (idx < count))
			return idx;
		goto hashchoice;
	}

	if (count <= 1)
		return 0;

hashchoice:
	switch (hashtype) {
	case MEA_MAGIC_LAST_CHAR:
		c = mea_last_char_hash(count, (char *)name, namelen);
		break;
	case MEA_MAGIC_ALL_CHARS:
		c = mea_all_chars_hash(count, (char *)name, namelen);
		break;
	case MEA_MAGIC_HASH_SEGMENT:
		CERROR("Unsupported hash type MEA_MAGIC_HASH_SEGMENT\n");
		break;
	default:
		CERROR("Unknown hash type 0x%x\n", hashtype);
	}

	LASSERT(c < count);
	return c;
}
EXPORT_SYMBOL(raw_name2idx);

int mea_name2idx(struct lmv_stripe_md *mea, const char *name, int namelen)
{
	unsigned int c;

	LASSERT(mea && mea->mea_count);

	c = raw_name2idx(mea->mea_magic, mea->mea_count, name, namelen);

	LASSERT(c < mea->mea_count);
	return c;
}
EXPORT_SYMBOL(mea_name2idx);
