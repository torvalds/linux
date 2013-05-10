/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright IBM Corp. 2008
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 */

#ifndef __ASM_PPC_DISASSEMBLE_H__
#define __ASM_PPC_DISASSEMBLE_H__

#include <linux/types.h>

static inline unsigned int get_op(u32 inst)
{
	return inst >> 26;
}

static inline unsigned int get_xop(u32 inst)
{
	return (inst >> 1) & 0x3ff;
}

static inline unsigned int get_sprn(u32 inst)
{
	return ((inst >> 16) & 0x1f) | ((inst >> 6) & 0x3e0);
}

static inline unsigned int get_dcrn(u32 inst)
{
	return ((inst >> 16) & 0x1f) | ((inst >> 6) & 0x3e0);
}

static inline unsigned int get_rt(u32 inst)
{
	return (inst >> 21) & 0x1f;
}

static inline unsigned int get_rs(u32 inst)
{
	return (inst >> 21) & 0x1f;
}

static inline unsigned int get_ra(u32 inst)
{
	return (inst >> 16) & 0x1f;
}

static inline unsigned int get_rb(u32 inst)
{
	return (inst >> 11) & 0x1f;
}

static inline unsigned int get_rc(u32 inst)
{
	return inst & 0x1;
}

static inline unsigned int get_ws(u32 inst)
{
	return (inst >> 11) & 0x1f;
}

static inline unsigned int get_d(u32 inst)
{
	return inst & 0xffff;
}

#endif /* __ASM_PPC_DISASSEMBLE_H__ */
