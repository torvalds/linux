/*
 * Copyright (C) 2014 Hisilicon Co. Ltd.
 * Copyright (C) 2015 ARM Ltd.
 *
 * Author: Xuzixin <Xuzixin@hisilicon.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef MALI_HIKEY_HI6220_REGISTERS_GPU_H
#define MALI_HIKEY_HI6220_REGISTERS_GPU_H 1

#include <linux/mm.h>

#define SOC_G3D_S_BASE_ADDR		0xF4080000 /* G3D ctrl base addr */
#define SOC_MEDIA_SCTRL_BASE_ADDR	0xF4410000 /* media ctrl base addr */
#define REG_MEDIA_SC_IOSIZE		PAGE_ALIGN(SZ_4K)
#define SOC_PMCTRL_BASE_ADDR		0xF7032000 /* pm ctrl base addr */
#define REG_PMCTRL_IOSIZE		PAGE_ALIGN(SZ_4K)
#define SOC_AO_SCTRL_BASE_ADDR		0xF7800000 /* ao ctrl base addr */
#define SOC_PERI_SCTRL_BASE_ADDR	0xF7030000 /* peri ctrl base addr */
#define REG_SC_ON_IOSIZE		PAGE_ALIGN(SZ_8K)
#define REG_SC_OFF_IOSIZE		PAGE_ALIGN(SZ_4K)

/* ----------------------------------------------------------------------------
 * MEDIA SCTRL
 */

#define SOC_MEDIA_SCTRL_SC_MEDIA_SUBSYS_CTRL5_ADDR(base) ((base) + (0x51C))
#define SOC_MEDIA_SCTRL_SC_MEDIA_CLKCFG0_ADDR(base)   ((base) + (0xCBC))
#define SOC_MEDIA_SCTRL_SC_MEDIA_CLKCFG2_ADDR(base)   ((base) + (0xCC4))
#define SOC_MEDIA_SCTRL_SC_MEDIA_CLKEN_ADDR(base)     ((base) + (0x520))
#define SOC_MEDIA_SCTRL_SC_MEDIA_CLKDIS_ADDR(base)    ((base) + (0x524))
#define SOC_MEDIA_SCTRL_SC_MEDIA_RSTEN_ADDR(base)     ((base) + (0x52C))
#define SOC_MEDIA_SCTRL_SC_MEDIA_RSTDIS_ADDR(base)    ((base) + (0x530))
#define SOC_MEDIA_SCTRL_SC_MEDIA_RST_STAT_ADDR(base)  ((base) + (0x534))

/* ----------------------------------------------------------------------------
 * AO SCTRL,only bit 1 is necessary for GPU.
 */

#define SOC_AO_SCTRL_SC_PW_CLKEN0_ADDR(base)          ((base) + (0x800))
#define SOC_AO_SCTRL_SC_PW_CLKDIS0_ADDR(base)         ((base) + (0x804))
#define SOC_AO_SCTRL_SC_PW_CLK_STAT0_ADDR(base)       ((base) + (0x808))
#define SOC_AO_SCTRL_SC_PW_RSTEN0_ADDR(base)          ((base) + (0x810))
#define SOC_AO_SCTRL_SC_PW_RSTDIS0_ADDR(base)         ((base) + (0x814))
#define SOC_AO_SCTRL_SC_PW_RST_STAT0_ADDR(base)       ((base) + (0x818))
#define SOC_AO_SCTRL_SC_PW_ISOEN0_ADDR(base)          ((base) + (0x820))
#define SOC_AO_SCTRL_SC_PW_ISODIS0_ADDR(base)         ((base) + (0x824))
#define SOC_AO_SCTRL_SC_PW_ISO_STAT0_ADDR(base)       ((base) + (0x828))
#define SOC_AO_SCTRL_SC_PW_MTCMOS_EN0_ADDR(base)      ((base) + (0x830))
#define SOC_AO_SCTRL_SC_PW_MTCMOS_DIS0_ADDR(base)     ((base) + (0x834))
#define SOC_AO_SCTRL_SC_PW_MTCMOS_STAT0_ADDR(base)    ((base) + (0x838))

/* ----------------------------------------------------------------------------
 * PERI SCTRL,only bit 10 is necessary for GPU.
 */

#define SOC_PERI_SCTRL_SC_PERIPH_CLKEN12_ADDR(base)   ((base) + (0x270))
#define SOC_PERI_SCTRL_SC_PERIPH_CLKSTAT12_ADDR(base) ((base) + (0x278))

#endif /* MALI_HIKEY_HI6220_REGISTERS_GPU_H */
