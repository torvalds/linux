/*
 * MIPS floating point support
 * Copyright (C) 1994-2000 Algorithmics Ltd.
 * http://www.algor.co.uk
 *
 * ########################################################################
 *
 *  This program is free software; you can distribute it and/or modify it
 *  under the terms of the GNU General Public License (Version 2) as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * ########################################################################
 */

/**************************************************************************
 *  Nov 7, 2000
 *  Added preprocessor hacks to map to Linux kernel diagnostics.
 *
 *  Kevin D. Kissell, kevink@mips.com and Carsten Langgaard, carstenl@mips.com
 *  Copyright (C) 2000 MIPS Technologies, Inc.  All rights reserved.
 *************************************************************************/

#include <linux/kernel.h>
#include "ieee754.h"

/*
 * Very naff exception handler (you can plug in your own and
 * override this).
 */

static const char *const rtnames[] = {
	"sp", "dp", "xp", "si", "di"
};

void ieee754_xcpt(struct ieee754xctx *xcp)
{
	printk(KERN_DEBUG "floating point exception in \"%s\", type=%s\n",
		xcp->op, rtnames[xcp->rt]);
}
