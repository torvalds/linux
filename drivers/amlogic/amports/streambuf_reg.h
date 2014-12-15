/*
 * AMLOGIC Audio/Video streaming port driver.
 *
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

#ifndef STREAMBUF_REG_H
#define STREAMBUF_REG_H

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
#define HEVC_STREAM_REG_BASE        HEVC_STREAM_START_ADDR
#endif

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#define VLD_MEM_VIFIFO_REG_BASE     P_VLD_MEM_VIFIFO_START_PTR
#define AIU_MEM_AIFIFO_REG_BASE     P_AIU_MEM_AIFIFO_START_PTR
#else
#define VLD_MEM_VIFIFO_REG_BASE     VLD_MEM_VIFIFO_START_PTR
#define AIU_MEM_AIFIFO_REG_BASE     AIU_MEM_AIFIFO_START_PTR
#endif

#define START_PTR   0
#define CURR_PTR    1
#define END_PTR     2
#define BYTES_AVAIL 3
#define CONTROL     4
#define WP          5
#define RP          6
#define LEVEL       7
#define BUF_CTRL    8

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6
#define _WRITE_ST_REG(r, val) \
    __raw_writel(val, (volatile void __iomem *)(buf->reg_base+(r<<2)))
#define _WRITE_ST_REG_BITS(r, val, s, e) \
    __raw_writel((((_READ_ST_REG(r) & (((1L<<(e)-1)<<(s))-1)<<(s)))|((unsigned)((val)&((1L<<(e))-1))<<(s))), (volatile void __iomem *)(buf->reg_base+(r<<2)))
#define _SET_ST_REG_MASK(r, val) \
    __raw_writel(_READ_ST_REG(r)| (val), (volatile void __iomem *)(buf->reg_base+(r<<2)))
#define _CLR_ST_REG_MASK(r, val) \
    __raw_writel(_READ_ST_REG(r)&~(val), (volatile void __iomem *)(buf->reg_base+(r<<2)))
#define _READ_ST_REG(r) \
    (__raw_readl((volatile void __iomem *)(buf->reg_base+(r<<2))))

#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON6TVD
#define _READ_VDEC2_ST_REG(r) \
    (__raw_readl((volatile void __iomem *)(buf->reg_base + DOS_REG_ADDR(VDEC2_VLD_MEM_VIFIFO_START_PTR) - DOS_REG_ADDR(VLD_MEM_VIFIFO_START_PTR) + (r<<2))))
#define _WRITE_VDEC2_ST_REG(r, val) \
    __raw_writel(val, (volatile void __iomem *)(buf->reg_base + DOS_REG_ADDR(VDEC2_VLD_MEM_VIFIFO_START_PTR) - DOS_REG_ADDR(VLD_MEM_VIFIFO_START_PTR) + (r<<2)))
#endif

#define MEM_BUFCTRL_MANUAL		(1<<1)
#define MEM_BUFCTRL_INIT		(1<<0)
#define MEM_LEVEL_CNT_BIT       18
#define MEM_FIFO_CNT_BIT        16
#define MEM_FILL_ON_LEVEL		(1<<10)
#define MEM_CTRL_EMPTY_EN		(1<<2)
#define MEM_CTRL_FILL_EN		(1<<1)
#define MEM_CTRL_INIT			(1<<0)

#else
#define _WRITE_ST_REG(r, val)             WRITE_MPEG_REG(buf->reg_base + (r), (val))
#define _WRITE_ST_REG_BITS(r, val, s, e)  WRITE_MPEG_REG(buf->reg_base + (r), (val), (s), (e))
#define _SET_ST_REG_MASK(r, val)          SET_MPEG_REG_MASK(buf->reg_base + (r), (val))
#define _CLR_ST_REG_MASK(r, val)          CLEAR_MPEG_REG_MASK(buf->reg_base + (r), (val))
#define _READ_ST_REG(r)                   READ_MPEG_REG(buf->reg_base + (r))
#endif

#endif /* STREAMBUF_REG_H */

