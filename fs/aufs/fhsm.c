/*
 * Copyright (C) 2011-2014 Junjiro R. Okajima
 *
 * This program, aufs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 * File-based Hierarchy Storage Management
 */

#include <linux/seq_file.h>
#include "aufs.h"

void au_fhsm_init(struct au_sbinfo *sbinfo)
{
	sbinfo->si_fhsm.fhsm_expire
		= msecs_to_jiffies(AUFS_FHSM_CACHE_DEF_SEC * MSEC_PER_SEC);
}

void au_fhsm_set(struct au_sbinfo *sbinfo, unsigned int sec)
{
	sbinfo->si_fhsm.fhsm_expire
		= msecs_to_jiffies(sec * MSEC_PER_SEC);
}

void au_fhsm_show(struct seq_file *seq, struct au_sbinfo *sbinfo)
{
	unsigned int u;

	if (!au_ftest_si(sbinfo, FHSM))
		return;

	u = jiffies_to_msecs(sbinfo->si_fhsm.fhsm_expire) / MSEC_PER_SEC;
	if (u != AUFS_FHSM_CACHE_DEF_SEC)
		seq_printf(seq, ",fhsm_sec=%u", u);
}
