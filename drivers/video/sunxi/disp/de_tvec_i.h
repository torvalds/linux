/*
 * Copyright (C) 2007-2012 Allwinner Technology Co., Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __DE_TVE_I_H__
#define __DE_TVE_I_H__

/*tv encoder registers offset*/
#define TVE_000    (0x000)
#define TVE_004    (0x004)
#define TVE_008    (0x008)
#define TVE_00C    (0x00c)
#define TVE_010    (0x010)
#define TVE_014    (0x014)
#define TVE_018    (0x018)
#define TVE_01C    (0x01c)
#define TVE_020    (0x020)
#define TVE_024    (0x024)
#define TVE_030    (0X030)
#define TVE_034    (0x034)
#define TVE_038    (0x038)
#define TVE_03C    (0x03c)
#define TVE_040    (0x040)
#define TVE_044    (0x044)
#define TVE_048    (0x048)
#define TVE_04C    (0x04c)
#define TVE_100    (0x100)
#define TVE_104    (0x104)
#define TVE_10C    (0x10c)
#define TVE_110    (0x110)
#define TVE_114    (0x114)
#define TVE_118    (0x118)
#define TVE_11C    (0x11c)
#define TVE_124    (0x124)
#define TVE_128    (0x128)
#define TVE_12C    (0x12c)
#define TVE_130    (0x130)
#define TVE_138    (0x138)
#define TVE_13C    (0x13C)

/* pfff, get do { } while (0) around these */
#define TVE_GET_REG_BASE(sel) ((sel)==0?(tve_reg_base0):(tve_reg_base1))

#define TVE_WUINT32(sel,offset,value) (*((volatile __u32 *)( TVE_GET_REG_BASE(sel) + (offset) ))=(value))
#define TVE_RUINT32(sel,offset)	(*((volatile __u32 *)( TVE_GET_REG_BASE(sel) + (offset) )))

#define TVE_SET_BIT(sel,offset,bit) (*((volatile __u32 *)( TVE_GET_REG_BASE(sel) + (offset) )) |= (bit))
#define TVE_CLR_BIT(sel,offset,bit) (*((volatile __u32 *)( TVE_GET_REG_BASE(sel) + (offset) )) &= (~(bit)))
#define TVE_INIT_BIT(sel,offset,c,s) (*((volatile __u32 *)( TVE_GET_REG_BASE(sel) + (offset) )) = \
(((*(volatile __u32 *)( TVE_GET_REG_BASE(sel) + (offset) )) & (~(c))) | (s)))

#endif
