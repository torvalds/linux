/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 */

#ifndef VDEC_REG_H
#define VDEC_REG_H

#include <linux/kernel.h>
#include <linux/amlogic/amports/vformat.h>
#include "vdec.h"

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#define READ_AOREG(r) (__raw_readl((volatile void __iomem *)AOBUS_REG_ADDR(r)))
#define WRITE_AOREG(r, val) __raw_writel(val, (volatile void __iomem *)(AOBUS_REG_ADDR(r)))
#define INT_VDEC INT_DOS_MAILBOX_1
#define INT_VDEC2 INT_DOS_MAILBOX_0
#else
#define INT_VDEC INT_MAILBOX_1A
#endif

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#define READ_VREG(r) (__raw_readl((volatile void __iomem *)DOS_REG_ADDR(r)))
//static inline u32 READ_VREG(u32 r)
//{
//    if (((r) > 0x2000) && ((r) < 0x3000) && !vdec_on(2)) dump_stack();
//    return __raw_readl((volatile void __iomem *)DOS_REG_ADDR(r));
//}

#define WRITE_VREG(r, val) __raw_writel(val, (volatile void __iomem *)(DOS_REG_ADDR(r)))
//static inline void WRITE_VREG(u32 r, u32 val)
//{
//    if (((r) > 0x2000) && ((r) < 0x3000) && !vdec_on(2)) dump_stack();
//    __raw_writel(val, (volatile void __iomem *)(DOS_REG_ADDR(r)));
//}

#define WRITE_VREG_BITS(r, val, start, len) \
    WRITE_VREG(r, (READ_VREG(r) & ~(((1L<<(len))-1)<<(start)))|((unsigned)((val)&((1L<<(len))-1)) << (start)))
#define SET_VREG_MASK(r, mask) WRITE_VREG(r, READ_VREG(r) | (mask))
#define CLEAR_VREG_MASK(r, mask) WRITE_VREG(r, READ_VREG(r) & ~(mask))

#define READ_HREG(r) (__raw_readl((volatile void __iomem *)DOS_REG_ADDR(r|0x1000)))
#define WRITE_HREG(r, val) __raw_writel(val, (volatile void __iomem *)DOS_REG_ADDR(r|0x1000))
#define WRITE_HREG_BITS(r, val, start, len) \
    WRITE_HREG(r, (READ_HREG(r) & ~(((1L<<(len))-1)<<(start)))|((unsigned)((val)&((1L<<(len))-1)) << (start)))
#define SET_HREG_MASK(r, mask) WRITE_HREG(r, READ_HREG(r) | (mask))
#define CLEAR_HREG_MASK(r, mask) WRITE_HREG(r, READ_HREG(r) & ~(mask))

#define READ_SEC_REG(r) (__raw_readl((volatile void __iomem *)SECBUS_REG_ADDR(r)))
#define WRITE_SEC_REG(r, val) __raw_writel(val, (volatile void __iomem *)SECBUS_REG_ADDR(r))
#define WRITE_SEC_REG_BITS(r, val, start, len) \
    WRITE_SEC_REG(r, (READ_SEC_REG(r) & ~(((1L<<(len))-1)<<(start)))|((unsigned)((val)&((1L<<(len))-1)) << (start)))
#define ASSIST_MBOX1_CLR_REG VDEC_ASSIST_MBOX1_CLR_REG
#define ASSIST_MBOX1_MASK VDEC_ASSIST_MBOX1_MASK
#define ASSIST_AMR1_INT0 VDEC_ASSIST_AMR1_INT0
#define ASSIST_AMR1_INT1 VDEC_ASSIST_AMR1_INT1
#define ASSIST_AMR1_INT2 VDEC_ASSIST_AMR1_INT2
#define ASSIST_AMR1_INT3 VDEC_ASSIST_AMR1_INT3
#define ASSIST_AMR1_INT4 VDEC_ASSIST_AMR1_INT4
#define ASSIST_AMR1_INT5 VDEC_ASSIST_AMR1_INT5
#define ASSIST_AMR1_INT6 VDEC_ASSIST_AMR1_INT6
#define ASSIST_AMR1_INT7 VDEC_ASSIST_AMR1_INT7
#define ASSIST_AMR1_INT8 VDEC_ASSIST_AMR1_INT8
#define ASSIST_AMR1_INT9 VDEC_ASSIST_AMR1_INT9

#else
#define READ_VREG(r) READ_CBUS_REG(r)
#define WRITE_VREG(r, val) WRITE_CBUS_REG(r, val)
#define WRITE_VREG_BITS(r, val, start, len) WRITE_CBUS_REG_BITS(r, val, start, len)
#define SET_VREG_MASK(r, mask) SET_CBUS_REG_MASK(r, mask)
#define CLEAR_VREG_MASK(r, mask) CLEAR_CBUS_REG_MASK(r, mask)
#endif

#if MESON_CPU_TYPE < MESON_CPU_TYPE_MESON8
#define READ_VCBUS_REG(r) READ_CBUS_REG(r)
#define WRITE_VCBUS_REG(r, val) WRITE_CBUS_REG(r, val)
#define WRITE_VCBUS_REG_BITS(r, val, from, size) WRITE_CBUS_REG_BITS(r, val, from, size)
#define SET_VCBUS_REG_MASK(r, mask) SET_CBUS_REG_MASK(r, mask)
#define CLEAR_VCBUS_REG_MASK(r, mask) CLEAR_CBUS_REG_MASK(r, mask)
#endif

#endif /* VDEC_REG_H */

