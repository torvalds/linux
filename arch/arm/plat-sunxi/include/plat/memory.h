/*
 * arch/arm/plat-sunxi/include/plat/memory.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Benn Huang <benn@allwinnertech.com>
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

#ifndef __SW_MEMORY_H
#define __SW_MEMORY_H

/* physical offset of RAM */
#define PLAT_PHYS_OFFSET		UL(0x40000000)

/* default ATAG_MEM size */
#define MEM_SIZE	(512*1024*1024)

#ifdef CONFIG_ARCH_SUN5I

#define SW_VE_MEM_BASE                    (PLAT_PHYS_OFFSET + SZ_64M)
#define SW_VE_MEM_SIZE                    (SZ_64M)
#define SW_FB_MEM_BASE                    (SW_VE_MEM_BASE + SW_VE_MEM_SIZE)
#define SW_FB_MEM_SIZE                    (SZ_16M)
#define SW_GPU_MEM_BASE                   (SW_FB_MEM_BASE + SW_FB_MEM_SIZE)
#define SW_GPU_MEM_SIZE                   (SZ_64M)
#define SW_BANK1_SIZE                     (SZ_64M + SW_VE_MEM_SIZE + SW_FB_MEM_SIZE)
#define SW_BANK2_OFFSET                   (SW_BANK1_SIZE + SW_GPU_MEM_SIZE)

#if 0
#error "fixme"
#define SW_VE_MEM_BASE                    (PLAT_PHYS_OFFSET + SZ_64M)
#define SW_VE_MEM_SIZE                    (SZ_64M + SZ_16M)
#define SW_FB_MEM_BASE                    (SW_VE_MEM + SW_MEM_VE_SIZE)
#define SW_FB_MEM_SIZE                    (SZ_32M)
#define SW_GPU_MEM_BASE                   (SW_FB_MEM_BASE + SW_FB_MEM_SIZE)
#define SW_GPU_MEM_SIZE                   (SZ_64M)
#define SW_G2D_MEM_BASE
#define SW_G2D_MEM_SIZE                   0x1000000
#endif

#else

#ifdef CONFIG_ARCH_SUN7I
#define PLAT_MEM_SIZE                   (SZ_512M*2)

#define SYS_CONFIG_MEMBASE             (PLAT_PHYS_OFFSET + SZ_32M + SZ_16M) /* +48M */
#define SYS_CONFIG_MEMSIZE             (SZ_64K) /* 64K */

#define SW_VE_MEM_SIZE                 (SZ_64M + SZ_16M)
#define SW_G2D_MEM_SIZE                0x01000000 /* SZ_16M */
#define SW_FB_MEM_SIZE                 0x02000000 /* SZ_32M */
#define SW_GPU_MEM_SIZE                0x04000000 /* SZ_64M */

/*
 * memory reserved areas.
 */
#define SW_VE_MEM_BASE                 (PLAT_PHYS_OFFSET + SZ_64M)
#define SW_G2D_MEM_BASE                (SW_VE_MEM_BASE + SW_VE_MEM_SIZE)
#define SW_FB_MEM_BASE                 (SW_G2D_MEM_BASE + SW_G2D_MEM_SIZE)
#define SW_GPU_MEM_BASE                (SW_FB_MEM_BASE + SW_FB_MEM_SIZE)

#define HW_RESERVED_MEM_BASE    (SW_VE_MEM_BASE)
#define HW_RESERVED_MEM_SIZE    (SW_VE_MEM_SIZE + SW_G2D_MEM_SIZE + SW_FB_MEM_SIZE + SW_GPU_MEM_SIZE)   /* 232M(DE+VE(CSI)+MP) */

#else

#define SW_FB_MEM_BASE 0UL
#define SW_FB_MEM_SIZE SZ_32M

#endif /* sun7i */
#endif /* sun5i */

#endif



