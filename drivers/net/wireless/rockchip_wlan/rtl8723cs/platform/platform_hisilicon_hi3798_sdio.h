/******************************************************************************
 *
 * Copyright(c) 2017 - 2018 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/
#ifndef __PLATFORM_HISILICON_HI3798_SDIO_H__
#define __PLATFORM_HISILICON_HI3798_SDIO_H__

typedef unsigned int	HI_U32;

typedef int		HI_S32;

#define HI_SUCCESS	0
#define HI_FAILURE	(-1)

extern HI_S32 HI_DRV_GPIO_SetDirBit(HI_U32 u32GpioNo, HI_U32 u32DirBit);
extern HI_S32 HI_DRV_GPIO_WriteBit(HI_U32 u32GpioNo, HI_U32 u32BitValue);

#endif /* __PLATFORM_HISILICON_HI3798_SDIO_H__ */
