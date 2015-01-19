/*
 * This confidential and proprietary software may be used only as
 * authorised by a licensing agreement from AMLOGIC, INC.
 * (C) COPYRIGHT 2011 AMLOGIC, INC.
 * ALL RIGHTS RESERVED
 * The entire notice above must be reproduced on all authorised
 * copies and copies may only be made to the extent permitted
 * by a licensing agreement from AMLOGIC, INC.
 */

#ifndef MALI_POWERON_REG_H
#define MALI_POWERON_REG_H

#define MALI_PP_PP_VERSION_MAGIC      0xCD070100UL

#if defined(IO_APB2_BUS_PHY_BASE)
#define WRITE_MALI_REG(reg, val) \
    __raw_writel(val, (volatile void *)(reg - IO_APB2_BUS_PHY_BASE + IO_APB2_BUS_BASE))
#define READ_MALI_REG(reg) \
    __raw_readl((volatile void *)(reg - IO_APB2_BUS_PHY_BASE + IO_APB2_BUS_BASE))
#else
#define WRITE_MALI_REG(reg, val) \
    __raw_writel(val, (volatile void *)(reg - IO_APB_BUS_PHY_BASE + IO_APB_BUS_BASE))
#define READ_MALI_REG(reg) \
    __raw_readl((volatile void *)(reg - IO_APB_BUS_PHY_BASE + IO_APB_BUS_BASE))
#endif

#define MALI_APB_GP_VSCL_START        0xd0060000
#define MALI_APB_GP_VSCL_END          0xd0060004
#define MALI_APB_GP_CMD               0xd0060020
#define MALI_APB_GP_INT_RAWSTAT       0xd0060024
#define MALI_APB_GP_INT_CLEAR         0xd0060028
#define MALI_APB_GP_INT_MASK          0xd006002c
#define MALI_APB_GP_INT_STAT          0xd0060030

#define MALI_MMU_DTE_ADDR             0xd0063000
#define MALI_MMU_STATUS               0xd0063004
#define MALI_MMU_CMD                  0xd0063008
#define MALI_MMU_RAW_STATUS           0xd0064014
#define MALI_MMU_INT_CLEAR            0xd0064018
#define MALI_MMU_INT_MASK             0xd006401c
#define MALI_MMU_INT_STATUS           0xd0064020

#define MALI_PP_MMU_DTE_ADDR          0xd0064000
#define MALI_PP_MMU_STATUS            0xd0064004
#define MALI_PP_MMU_CMD               0xd0064008
#define MALI_PP_MMU_RAW_STATUS        0xd0064014
#define MALI_PP_MMU_INT_CLEAR         0xd0064018
#define MALI_PP_MMU_INT_MASK          0xd006401c
#define MALI_PP_MMU_INT_STATUS        0xd0064020

#define MALI_APB_PP_REND_LIST_ADDR    0xd0068000
#define MALI_APB_PP_REND_RSW_BASE     0xd0068004
#define MALI_APB_PP_REND_VERTEX_BASE  0xd0068008
#define MALI_APB_PPSUBPIXEL_SPECIFIER 0xd0068048
#define MALI_APB_WB0_SOURCE_SELECT    0xd0068100
#define MALI_APB_WB0_TARGET_ADDR      0xd0068104
#define MALI_APB_WB0_TARGET_SCANLINE_LENGTH 0xd0068114

#define MALI_PP_PP_VERSION            0xd0069000
#define MALI_PP_STATUS                0xd0069008
#define MALI_PP_CTRL_MGMT             0xd006900C
#define MALI_PP_INT_RAWSTAT           0xd0069020
#define MALI_PP_INT_CLEAR             0xd0069024
#define MALI_PP_INT_MASK              0xd0069028
#define MALI_PP_INT_STAT              0xd006902C

#endif /* MALI_POWERON_REG_H */
