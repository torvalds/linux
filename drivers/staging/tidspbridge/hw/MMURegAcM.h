/*
 * MMURegAcM.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * Copyright (C) 2007 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef _MMU_REG_ACM_H
#define _MMU_REG_ACM_H

#include <linux/io.h>
#include <EasiGlobal.h>

#include "MMUAccInt.h"

#if defined(USE_LEVEL_1_MACROS)

#define MMUMMU_SYSCONFIG_READ_REGISTER32(base_address)\
    (_DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_SYSCONFIG_READ_REGISTER32),\
      __raw_readl((base_address)+MMU_MMU_SYSCONFIG_OFFSET))

#define MMUMMU_SYSCONFIG_IDLE_MODE_WRITE32(base_address, value)\
{\
    const u32 offset = MMU_MMU_SYSCONFIG_OFFSET;\
    register u32 data = __raw_readl((base_address)+offset);\
    register u32 new_value = (value);\
    _DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_SYSCONFIG_IDLE_MODE_WRITE32);\
    data &= ~(MMU_MMU_SYSCONFIG_IDLE_MODE_MASK);\
    new_value <<= MMU_MMU_SYSCONFIG_IDLE_MODE_OFFSET;\
    new_value &= MMU_MMU_SYSCONFIG_IDLE_MODE_MASK;\
    new_value |= data;\
    __raw_writel(new_value, base_address+offset);\
}

#define MMUMMU_SYSCONFIG_AUTO_IDLE_WRITE32(base_address, value)\
{\
    const u32 offset = MMU_MMU_SYSCONFIG_OFFSET;\
    register u32 data = __raw_readl((base_address)+offset);\
    register u32 new_value = (value);\
    _DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_SYSCONFIG_AUTO_IDLE_WRITE32);\
    data &= ~(MMU_MMU_SYSCONFIG_AUTO_IDLE_MASK);\
    new_value <<= MMU_MMU_SYSCONFIG_AUTO_IDLE_OFFSET;\
    new_value &= MMU_MMU_SYSCONFIG_AUTO_IDLE_MASK;\
    new_value |= data;\
    __raw_writel(new_value, base_address+offset);\
}

#define MMUMMU_IRQSTATUS_READ_REGISTER32(base_address)\
    (_DEBUG_LEVEL1_EASI(easil1_mmummu_irqstatus_read_register32),\
      __raw_readl((base_address)+MMU_MMU_IRQSTATUS_OFFSET))

#define MMUMMU_IRQSTATUS_WRITE_REGISTER32(base_address, value)\
{\
    const u32 offset = MMU_MMU_IRQSTATUS_OFFSET;\
    register u32 new_value = (value);\
    _DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_IRQSTATUS_WRITE_REGISTER32);\
    __raw_writel(new_value, (base_address)+offset);\
}

#define MMUMMU_IRQENABLE_READ_REGISTER32(base_address)\
    (_DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_IRQENABLE_READ_REGISTER32),\
      __raw_readl((base_address)+MMU_MMU_IRQENABLE_OFFSET))

#define MMUMMU_IRQENABLE_WRITE_REGISTER32(base_address, value)\
{\
    const u32 offset = MMU_MMU_IRQENABLE_OFFSET;\
    register u32 new_value = (value);\
    _DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_IRQENABLE_WRITE_REGISTER32);\
    __raw_writel(new_value, (base_address)+offset);\
}

#define MMUMMU_WALKING_STTWL_RUNNING_READ32(base_address)\
    (_DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_WALKING_STTWL_RUNNING_READ32),\
      (((__raw_readl(((base_address)+(MMU_MMU_WALKING_ST_OFFSET))))\
      & MMU_MMU_WALKING_ST_TWL_RUNNING_MASK) >>\
      MMU_MMU_WALKING_ST_TWL_RUNNING_OFFSET))

#define MMUMMU_CNTLTWL_ENABLE_READ32(base_address)\
    (_DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_CNTLTWL_ENABLE_READ32),\
      (((__raw_readl(((base_address)+(MMU_MMU_CNTL_OFFSET)))) &\
      MMU_MMU_CNTL_TWL_ENABLE_MASK) >>\
      MMU_MMU_CNTL_TWL_ENABLE_OFFSET))

#define MMUMMU_CNTLTWL_ENABLE_WRITE32(base_address, value)\
{\
    const u32 offset = MMU_MMU_CNTL_OFFSET;\
    register u32 data = __raw_readl((base_address)+offset);\
    register u32 new_value = (value);\
    _DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_CNTLTWL_ENABLE_WRITE32);\
    data &= ~(MMU_MMU_CNTL_TWL_ENABLE_MASK);\
    new_value <<= MMU_MMU_CNTL_TWL_ENABLE_OFFSET;\
    new_value &= MMU_MMU_CNTL_TWL_ENABLE_MASK;\
    new_value |= data;\
    __raw_writel(new_value, base_address+offset);\
}

#define MMUMMU_CNTLMMU_ENABLE_WRITE32(base_address, value)\
{\
    const u32 offset = MMU_MMU_CNTL_OFFSET;\
    register u32 data = __raw_readl((base_address)+offset);\
    register u32 new_value = (value);\
    _DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_CNTLMMU_ENABLE_WRITE32);\
    data &= ~(MMU_MMU_CNTL_MMU_ENABLE_MASK);\
    new_value <<= MMU_MMU_CNTL_MMU_ENABLE_OFFSET;\
    new_value &= MMU_MMU_CNTL_MMU_ENABLE_MASK;\
    new_value |= data;\
    __raw_writel(new_value, base_address+offset);\
}

#define MMUMMU_FAULT_AD_READ_REGISTER32(base_address)\
    (_DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_FAULT_AD_READ_REGISTER32),\
      __raw_readl((base_address)+MMU_MMU_FAULT_AD_OFFSET))

#define MMUMMU_TTB_WRITE_REGISTER32(base_address, value)\
{\
    const u32 offset = MMU_MMU_TTB_OFFSET;\
    register u32 new_value = (value);\
    _DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_TTB_WRITE_REGISTER32);\
    __raw_writel(new_value, (base_address)+offset);\
}

#define MMUMMU_LOCK_READ_REGISTER32(base_address)\
    (_DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_LOCK_READ_REGISTER32),\
      __raw_readl((base_address)+MMU_MMU_LOCK_OFFSET))

#define MMUMMU_LOCK_WRITE_REGISTER32(base_address, value)\
{\
    const u32 offset = MMU_MMU_LOCK_OFFSET;\
    register u32 new_value = (value);\
    _DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_LOCK_WRITE_REGISTER32);\
    __raw_writel(new_value, (base_address)+offset);\
}

#define MMUMMU_LOCK_BASE_VALUE_READ32(base_address)\
    (_DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_LOCK_BASE_VALUE_READ32),\
      (((__raw_readl(((base_address)+(MMU_MMU_LOCK_OFFSET)))) &\
      MMU_MMU_LOCK_BASE_VALUE_MASK) >>\
      MMU_MMU_LOCK_BASE_VALUE_OFFSET))

#define MMUMMU_LOCK_BASE_VALUE_WRITE32(base_address, value)\
{\
    const u32 offset = MMU_MMU_LOCK_OFFSET;\
    register u32 data = __raw_readl((base_address)+offset);\
    register u32 new_value = (value);\
    _DEBUG_LEVEL1_EASI(easil1_mmummu_lock_base_value_write32);\
    data &= ~(MMU_MMU_LOCK_BASE_VALUE_MASK);\
    new_value <<= MMU_MMU_LOCK_BASE_VALUE_OFFSET;\
    new_value &= MMU_MMU_LOCK_BASE_VALUE_MASK;\
    new_value |= data;\
    __raw_writel(new_value, base_address+offset);\
}

#define MMUMMU_LOCK_CURRENT_VICTIM_READ32(base_address)\
    (_DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_LOCK_CURRENT_VICTIM_READ32),\
      (((__raw_readl(((base_address)+(MMU_MMU_LOCK_OFFSET)))) &\
      MMU_MMU_LOCK_CURRENT_VICTIM_MASK) >>\
      MMU_MMU_LOCK_CURRENT_VICTIM_OFFSET))

#define MMUMMU_LOCK_CURRENT_VICTIM_WRITE32(base_address, value)\
{\
    const u32 offset = MMU_MMU_LOCK_OFFSET;\
    register u32 data = __raw_readl((base_address)+offset);\
    register u32 new_value = (value);\
    _DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_LOCK_CURRENT_VICTIM_WRITE32);\
    data &= ~(MMU_MMU_LOCK_CURRENT_VICTIM_MASK);\
    new_value <<= MMU_MMU_LOCK_CURRENT_VICTIM_OFFSET;\
    new_value &= MMU_MMU_LOCK_CURRENT_VICTIM_MASK;\
    new_value |= data;\
    __raw_writel(new_value, base_address+offset);\
}

#define MMUMMU_LOCK_CURRENT_VICTIM_SET32(var, value)\
    (_DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_LOCK_CURRENT_VICTIM_SET32),\
      (((var) & ~(MMU_MMU_LOCK_CURRENT_VICTIM_MASK)) |\
      (((value) << MMU_MMU_LOCK_CURRENT_VICTIM_OFFSET) &\
      MMU_MMU_LOCK_CURRENT_VICTIM_MASK)))

#define MMUMMU_LD_TLB_READ_REGISTER32(base_address)\
    (_DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_LD_TLB_READ_REGISTER32),\
      __raw_readl((base_address)+MMU_MMU_LD_TLB_OFFSET))

#define MMUMMU_LD_TLB_WRITE_REGISTER32(base_address, value)\
{\
    const u32 offset = MMU_MMU_LD_TLB_OFFSET;\
    register u32 new_value = (value);\
    _DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_LD_TLB_WRITE_REGISTER32);\
    __raw_writel(new_value, (base_address)+offset);\
}

#define MMUMMU_CAM_WRITE_REGISTER32(base_address, value)\
{\
    const u32 offset = MMU_MMU_CAM_OFFSET;\
    register u32 new_value = (value);\
    _DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_CAM_WRITE_REGISTER32);\
    __raw_writel(new_value, (base_address)+offset);\
}

#define MMUMMU_RAM_WRITE_REGISTER32(base_address, value)\
{\
    const u32 offset = MMU_MMU_RAM_OFFSET;\
    register u32 new_value = (value);\
    _DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_RAM_WRITE_REGISTER32);\
    __raw_writel(new_value, (base_address)+offset);\
}

#define MMUMMU_FLUSH_ENTRY_WRITE_REGISTER32(base_address, value)\
{\
    const u32 offset = MMU_MMU_FLUSH_ENTRY_OFFSET;\
    register u32 new_value = (value);\
    _DEBUG_LEVEL1_EASI(EASIL1_MMUMMU_FLUSH_ENTRY_WRITE_REGISTER32);\
    __raw_writel(new_value, (base_address)+offset);\
}

#endif /* USE_LEVEL_1_MACROS */

#endif /* _MMU_REG_ACM_H */
