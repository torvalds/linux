/*
 * Copyright © 2008  Stephen Rothwell IBM Corporation
 *
 * This program is free software; you can redistribute it and/or modify
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */
#ifndef _ASM_POWERPC_ISERIES_ALPACA_H
#define _ASM_POWERPC_ISERIES_ALPACA_H

/*
 * This is the part of the paca that the iSeries hypervisor
 * needs to be statically initialised. Immediately after boot
 * we switch to the normal Linux paca.
 */
struct alpaca {
	struct lppaca *lppaca_ptr;	/* Pointer to LpPaca for PLIC */
	const void *reg_save_ptr;	/* Pointer to LpRegSave for PLIC */
};

#endif /* _ASM_POWERPC_ISERIES_ALPACA_H */
