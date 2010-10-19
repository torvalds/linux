/* arch/arm/mach-msm/gpio_hw.h
 *
 * Copyright (C) 2007 Google, Inc.
 * Author: Brian Swetland <swetland@google.com>
 * Copyright (c) 2008-2010, Code Aurora Forum. All rights reserved.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ARCH_ARM_MACH_MSM_GPIO_HW_H
#define __ARCH_ARM_MACH_MSM_GPIO_HW_H

#include <mach/msm_iomap.h>

/* see 80-VA736-2 Rev C pp 695-751
**
** These are actually the *shadow* gpio registers, since the
** real ones (which allow full access) are only available to the
** ARM9 side of the world.
**
** Since the _BASE need to be page-aligned when we're mapping them
** to virtual addresses, adjust for the additional offset in these
** macros.
*/

#if defined(CONFIG_ARCH_MSM7X30)
#define MSM_GPIO1_REG(off) (MSM_GPIO1_BASE + (off))
#define MSM_GPIO2_REG(off) (MSM_GPIO2_BASE + 0x400 + (off))
#else
#define MSM_GPIO1_REG(off) (MSM_GPIO1_BASE + 0x800 + (off))
#define MSM_GPIO2_REG(off) (MSM_GPIO2_BASE + 0xC00 + (off))
#endif

#if defined(CONFIG_ARCH_MSM7X00A) || defined(CONFIG_ARCH_MSM7X25) ||\
    defined(CONFIG_ARCH_MSM7X27)

/* output value */
#define MSM_GPIO_OUT_0         MSM_GPIO1_REG(0x00)  /* gpio  15-0  */
#define MSM_GPIO_OUT_1         MSM_GPIO2_REG(0x00)  /* gpio  42-16 */
#define MSM_GPIO_OUT_2         MSM_GPIO1_REG(0x04)  /* gpio  67-43 */
#define MSM_GPIO_OUT_3         MSM_GPIO1_REG(0x08)  /* gpio  94-68 */
#define MSM_GPIO_OUT_4         MSM_GPIO1_REG(0x0C)  /* gpio 106-95 */
#define MSM_GPIO_OUT_5         MSM_GPIO1_REG(0x50)  /* gpio 107-121 */

/* same pin map as above, output enable */
#define MSM_GPIO_OE_0          MSM_GPIO1_REG(0x10)
#define MSM_GPIO_OE_1          MSM_GPIO2_REG(0x08)
#define MSM_GPIO_OE_2          MSM_GPIO1_REG(0x14)
#define MSM_GPIO_OE_3          MSM_GPIO1_REG(0x18)
#define MSM_GPIO_OE_4          MSM_GPIO1_REG(0x1C)
#define MSM_GPIO_OE_5          MSM_GPIO1_REG(0x54)

/* same pin map as above, input read */
#define MSM_GPIO_IN_0          MSM_GPIO1_REG(0x34)
#define MSM_GPIO_IN_1          MSM_GPIO2_REG(0x20)
#define MSM_GPIO_IN_2          MSM_GPIO1_REG(0x38)
#define MSM_GPIO_IN_3          MSM_GPIO1_REG(0x3C)
#define MSM_GPIO_IN_4          MSM_GPIO1_REG(0x40)
#define MSM_GPIO_IN_5          MSM_GPIO1_REG(0x44)

/* same pin map as above, 1=edge 0=level interrup */
#define MSM_GPIO_INT_EDGE_0    MSM_GPIO1_REG(0x60)
#define MSM_GPIO_INT_EDGE_1    MSM_GPIO2_REG(0x50)
#define MSM_GPIO_INT_EDGE_2    MSM_GPIO1_REG(0x64)
#define MSM_GPIO_INT_EDGE_3    MSM_GPIO1_REG(0x68)
#define MSM_GPIO_INT_EDGE_4    MSM_GPIO1_REG(0x6C)
#define MSM_GPIO_INT_EDGE_5    MSM_GPIO1_REG(0xC0)

/* same pin map as above, 1=positive 0=negative */
#define MSM_GPIO_INT_POS_0     MSM_GPIO1_REG(0x70)
#define MSM_GPIO_INT_POS_1     MSM_GPIO2_REG(0x58)
#define MSM_GPIO_INT_POS_2     MSM_GPIO1_REG(0x74)
#define MSM_GPIO_INT_POS_3     MSM_GPIO1_REG(0x78)
#define MSM_GPIO_INT_POS_4     MSM_GPIO1_REG(0x7C)
#define MSM_GPIO_INT_POS_5     MSM_GPIO1_REG(0xBC)

/* same pin map as above, interrupt enable */
#define MSM_GPIO_INT_EN_0      MSM_GPIO1_REG(0x80)
#define MSM_GPIO_INT_EN_1      MSM_GPIO2_REG(0x60)
#define MSM_GPIO_INT_EN_2      MSM_GPIO1_REG(0x84)
#define MSM_GPIO_INT_EN_3      MSM_GPIO1_REG(0x88)
#define MSM_GPIO_INT_EN_4      MSM_GPIO1_REG(0x8C)
#define MSM_GPIO_INT_EN_5      MSM_GPIO1_REG(0xB8)

/* same pin map as above, write 1 to clear interrupt */
#define MSM_GPIO_INT_CLEAR_0   MSM_GPIO1_REG(0x90)
#define MSM_GPIO_INT_CLEAR_1   MSM_GPIO2_REG(0x68)
#define MSM_GPIO_INT_CLEAR_2   MSM_GPIO1_REG(0x94)
#define MSM_GPIO_INT_CLEAR_3   MSM_GPIO1_REG(0x98)
#define MSM_GPIO_INT_CLEAR_4   MSM_GPIO1_REG(0x9C)
#define MSM_GPIO_INT_CLEAR_5   MSM_GPIO1_REG(0xB4)

/* same pin map as above, 1=interrupt pending */
#define MSM_GPIO_INT_STATUS_0  MSM_GPIO1_REG(0xA0)
#define MSM_GPIO_INT_STATUS_1  MSM_GPIO2_REG(0x70)
#define MSM_GPIO_INT_STATUS_2  MSM_GPIO1_REG(0xA4)
#define MSM_GPIO_INT_STATUS_3  MSM_GPIO1_REG(0xA8)
#define MSM_GPIO_INT_STATUS_4  MSM_GPIO1_REG(0xAC)
#define MSM_GPIO_INT_STATUS_5  MSM_GPIO1_REG(0xB0)

#endif

#if defined(CONFIG_ARCH_QSD8X50)
/* output value */
#define MSM_GPIO_OUT_0         MSM_GPIO1_REG(0x00)  /* gpio  15-0   */
#define MSM_GPIO_OUT_1         MSM_GPIO2_REG(0x00)  /* gpio  42-16  */
#define MSM_GPIO_OUT_2         MSM_GPIO1_REG(0x04)  /* gpio  67-43  */
#define MSM_GPIO_OUT_3         MSM_GPIO1_REG(0x08)  /* gpio  94-68  */
#define MSM_GPIO_OUT_4         MSM_GPIO1_REG(0x0C)  /* gpio 103-95  */
#define MSM_GPIO_OUT_5         MSM_GPIO1_REG(0x10)  /* gpio 121-104 */
#define MSM_GPIO_OUT_6         MSM_GPIO1_REG(0x14)  /* gpio 152-122 */
#define MSM_GPIO_OUT_7         MSM_GPIO1_REG(0x18)  /* gpio 164-153 */

/* same pin map as above, output enable */
#define MSM_GPIO_OE_0          MSM_GPIO1_REG(0x20)
#define MSM_GPIO_OE_1          MSM_GPIO2_REG(0x08)
#define MSM_GPIO_OE_2          MSM_GPIO1_REG(0x24)
#define MSM_GPIO_OE_3          MSM_GPIO1_REG(0x28)
#define MSM_GPIO_OE_4          MSM_GPIO1_REG(0x2C)
#define MSM_GPIO_OE_5          MSM_GPIO1_REG(0x30)
#define MSM_GPIO_OE_6          MSM_GPIO1_REG(0x34)
#define MSM_GPIO_OE_7          MSM_GPIO1_REG(0x38)

/* same pin map as above, input read */
#define MSM_GPIO_IN_0          MSM_GPIO1_REG(0x50)
#define MSM_GPIO_IN_1          MSM_GPIO2_REG(0x20)
#define MSM_GPIO_IN_2          MSM_GPIO1_REG(0x54)
#define MSM_GPIO_IN_3          MSM_GPIO1_REG(0x58)
#define MSM_GPIO_IN_4          MSM_GPIO1_REG(0x5C)
#define MSM_GPIO_IN_5          MSM_GPIO1_REG(0x60)
#define MSM_GPIO_IN_6          MSM_GPIO1_REG(0x64)
#define MSM_GPIO_IN_7          MSM_GPIO1_REG(0x68)

/* same pin map as above, 1=edge 0=level interrup */
#define MSM_GPIO_INT_EDGE_0    MSM_GPIO1_REG(0x70)
#define MSM_GPIO_INT_EDGE_1    MSM_GPIO2_REG(0x50)
#define MSM_GPIO_INT_EDGE_2    MSM_GPIO1_REG(0x74)
#define MSM_GPIO_INT_EDGE_3    MSM_GPIO1_REG(0x78)
#define MSM_GPIO_INT_EDGE_4    MSM_GPIO1_REG(0x7C)
#define MSM_GPIO_INT_EDGE_5    MSM_GPIO1_REG(0x80)
#define MSM_GPIO_INT_EDGE_6    MSM_GPIO1_REG(0x84)
#define MSM_GPIO_INT_EDGE_7    MSM_GPIO1_REG(0x88)

/* same pin map as above, 1=positive 0=negative */
#define MSM_GPIO_INT_POS_0     MSM_GPIO1_REG(0x90)
#define MSM_GPIO_INT_POS_1     MSM_GPIO2_REG(0x58)
#define MSM_GPIO_INT_POS_2     MSM_GPIO1_REG(0x94)
#define MSM_GPIO_INT_POS_3     MSM_GPIO1_REG(0x98)
#define MSM_GPIO_INT_POS_4     MSM_GPIO1_REG(0x9C)
#define MSM_GPIO_INT_POS_5     MSM_GPIO1_REG(0xA0)
#define MSM_GPIO_INT_POS_6     MSM_GPIO1_REG(0xA4)
#define MSM_GPIO_INT_POS_7     MSM_GPIO1_REG(0xA8)

/* same pin map as above, interrupt enable */
#define MSM_GPIO_INT_EN_0      MSM_GPIO1_REG(0xB0)
#define MSM_GPIO_INT_EN_1      MSM_GPIO2_REG(0x60)
#define MSM_GPIO_INT_EN_2      MSM_GPIO1_REG(0xB4)
#define MSM_GPIO_INT_EN_3      MSM_GPIO1_REG(0xB8)
#define MSM_GPIO_INT_EN_4      MSM_GPIO1_REG(0xBC)
#define MSM_GPIO_INT_EN_5      MSM_GPIO1_REG(0xC0)
#define MSM_GPIO_INT_EN_6      MSM_GPIO1_REG(0xC4)
#define MSM_GPIO_INT_EN_7      MSM_GPIO1_REG(0xC8)

/* same pin map as above, write 1 to clear interrupt */
#define MSM_GPIO_INT_CLEAR_0   MSM_GPIO1_REG(0xD0)
#define MSM_GPIO_INT_CLEAR_1   MSM_GPIO2_REG(0x68)
#define MSM_GPIO_INT_CLEAR_2   MSM_GPIO1_REG(0xD4)
#define MSM_GPIO_INT_CLEAR_3   MSM_GPIO1_REG(0xD8)
#define MSM_GPIO_INT_CLEAR_4   MSM_GPIO1_REG(0xDC)
#define MSM_GPIO_INT_CLEAR_5   MSM_GPIO1_REG(0xE0)
#define MSM_GPIO_INT_CLEAR_6   MSM_GPIO1_REG(0xE4)
#define MSM_GPIO_INT_CLEAR_7   MSM_GPIO1_REG(0xE8)

/* same pin map as above, 1=interrupt pending */
#define MSM_GPIO_INT_STATUS_0  MSM_GPIO1_REG(0xF0)
#define MSM_GPIO_INT_STATUS_1  MSM_GPIO2_REG(0x70)
#define MSM_GPIO_INT_STATUS_2  MSM_GPIO1_REG(0xF4)
#define MSM_GPIO_INT_STATUS_3  MSM_GPIO1_REG(0xF8)
#define MSM_GPIO_INT_STATUS_4  MSM_GPIO1_REG(0xFC)
#define MSM_GPIO_INT_STATUS_5  MSM_GPIO1_REG(0x100)
#define MSM_GPIO_INT_STATUS_6  MSM_GPIO1_REG(0x104)
#define MSM_GPIO_INT_STATUS_7  MSM_GPIO1_REG(0x108)

#endif

#if defined(CONFIG_ARCH_MSM7X30)

/* output value */
#define MSM_GPIO_OUT_0         MSM_GPIO1_REG(0x00)   /* gpio  15-0   */
#define MSM_GPIO_OUT_1         MSM_GPIO2_REG(0x00)   /* gpio  43-16  */
#define MSM_GPIO_OUT_2         MSM_GPIO1_REG(0x04)   /* gpio  67-44  */
#define MSM_GPIO_OUT_3         MSM_GPIO1_REG(0x08)   /* gpio  94-68  */
#define MSM_GPIO_OUT_4         MSM_GPIO1_REG(0x0C)   /* gpio 106-95  */
#define MSM_GPIO_OUT_5         MSM_GPIO1_REG(0x50)   /* gpio 133-107 */
#define MSM_GPIO_OUT_6         MSM_GPIO1_REG(0xC4)   /* gpio 150-134 */
#define MSM_GPIO_OUT_7         MSM_GPIO1_REG(0x214)  /* gpio 181-151 */

/* same pin map as above, output enable */
#define MSM_GPIO_OE_0          MSM_GPIO1_REG(0x10)
#define MSM_GPIO_OE_1          MSM_GPIO2_REG(0x08)
#define MSM_GPIO_OE_2          MSM_GPIO1_REG(0x14)
#define MSM_GPIO_OE_3          MSM_GPIO1_REG(0x18)
#define MSM_GPIO_OE_4          MSM_GPIO1_REG(0x1C)
#define MSM_GPIO_OE_5          MSM_GPIO1_REG(0x54)
#define MSM_GPIO_OE_6          MSM_GPIO1_REG(0xC8)
#define MSM_GPIO_OE_7          MSM_GPIO1_REG(0x218)

/* same pin map as above, input read */
#define MSM_GPIO_IN_0          MSM_GPIO1_REG(0x34)
#define MSM_GPIO_IN_1          MSM_GPIO2_REG(0x20)
#define MSM_GPIO_IN_2          MSM_GPIO1_REG(0x38)
#define MSM_GPIO_IN_3          MSM_GPIO1_REG(0x3C)
#define MSM_GPIO_IN_4          MSM_GPIO1_REG(0x40)
#define MSM_GPIO_IN_5          MSM_GPIO1_REG(0x44)
#define MSM_GPIO_IN_6          MSM_GPIO1_REG(0xCC)
#define MSM_GPIO_IN_7          MSM_GPIO1_REG(0x21C)

/* same pin map as above, 1=edge 0=level interrup */
#define MSM_GPIO_INT_EDGE_0    MSM_GPIO1_REG(0x60)
#define MSM_GPIO_INT_EDGE_1    MSM_GPIO2_REG(0x50)
#define MSM_GPIO_INT_EDGE_2    MSM_GPIO1_REG(0x64)
#define MSM_GPIO_INT_EDGE_3    MSM_GPIO1_REG(0x68)
#define MSM_GPIO_INT_EDGE_4    MSM_GPIO1_REG(0x6C)
#define MSM_GPIO_INT_EDGE_5    MSM_GPIO1_REG(0xC0)
#define MSM_GPIO_INT_EDGE_6    MSM_GPIO1_REG(0xD0)
#define MSM_GPIO_INT_EDGE_7    MSM_GPIO1_REG(0x240)

/* same pin map as above, 1=positive 0=negative */
#define MSM_GPIO_INT_POS_0     MSM_GPIO1_REG(0x70)
#define MSM_GPIO_INT_POS_1     MSM_GPIO2_REG(0x58)
#define MSM_GPIO_INT_POS_2     MSM_GPIO1_REG(0x74)
#define MSM_GPIO_INT_POS_3     MSM_GPIO1_REG(0x78)
#define MSM_GPIO_INT_POS_4     MSM_GPIO1_REG(0x7C)
#define MSM_GPIO_INT_POS_5     MSM_GPIO1_REG(0xBC)
#define MSM_GPIO_INT_POS_6     MSM_GPIO1_REG(0xD4)
#define MSM_GPIO_INT_POS_7     MSM_GPIO1_REG(0x228)

/* same pin map as above, interrupt enable */
#define MSM_GPIO_INT_EN_0      MSM_GPIO1_REG(0x80)
#define MSM_GPIO_INT_EN_1      MSM_GPIO2_REG(0x60)
#define MSM_GPIO_INT_EN_2      MSM_GPIO1_REG(0x84)
#define MSM_GPIO_INT_EN_3      MSM_GPIO1_REG(0x88)
#define MSM_GPIO_INT_EN_4      MSM_GPIO1_REG(0x8C)
#define MSM_GPIO_INT_EN_5      MSM_GPIO1_REG(0xB8)
#define MSM_GPIO_INT_EN_6      MSM_GPIO1_REG(0xD8)
#define MSM_GPIO_INT_EN_7      MSM_GPIO1_REG(0x22C)

/* same pin map as above, write 1 to clear interrupt */
#define MSM_GPIO_INT_CLEAR_0   MSM_GPIO1_REG(0x90)
#define MSM_GPIO_INT_CLEAR_1   MSM_GPIO2_REG(0x68)
#define MSM_GPIO_INT_CLEAR_2   MSM_GPIO1_REG(0x94)
#define MSM_GPIO_INT_CLEAR_3   MSM_GPIO1_REG(0x98)
#define MSM_GPIO_INT_CLEAR_4   MSM_GPIO1_REG(0x9C)
#define MSM_GPIO_INT_CLEAR_5   MSM_GPIO1_REG(0xB4)
#define MSM_GPIO_INT_CLEAR_6   MSM_GPIO1_REG(0xDC)
#define MSM_GPIO_INT_CLEAR_7   MSM_GPIO1_REG(0x230)

/* same pin map as above, 1=interrupt pending */
#define MSM_GPIO_INT_STATUS_0  MSM_GPIO1_REG(0xA0)
#define MSM_GPIO_INT_STATUS_1  MSM_GPIO2_REG(0x70)
#define MSM_GPIO_INT_STATUS_2  MSM_GPIO1_REG(0xA4)
#define MSM_GPIO_INT_STATUS_3  MSM_GPIO1_REG(0xA8)
#define MSM_GPIO_INT_STATUS_4  MSM_GPIO1_REG(0xAC)
#define MSM_GPIO_INT_STATUS_5  MSM_GPIO1_REG(0xB0)
#define MSM_GPIO_INT_STATUS_6  MSM_GPIO1_REG(0xE0)
#define MSM_GPIO_INT_STATUS_7  MSM_GPIO1_REG(0x234)

#endif

#endif
