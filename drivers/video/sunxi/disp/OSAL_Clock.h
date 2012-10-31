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

#ifndef __OSAL_CLOCK_H__
#define __OSAL_CLOCK_H__

__s32 OSAL_CCMU_SetSrcFreq(__u32 nSclkNo, __u32 nFreq);
__u32 OSAL_CCMU_GetSrcFreq(__u32 nSclkNo);
__hdle OSAL_CCMU_OpenMclk(__s32 nMclkNo);
__s32 OSAL_CCMU_CloseMclk(__hdle hMclk);
__s32 OSAL_CCMU_SetMclkSrc(__hdle hMclk, __u32 nSclkNo);
__s32 OSAL_CCMU_SetMclkDiv(__hdle hMclk, __s32 nDiv);
__s32 OSAL_CCMU_MclkOnOff(__hdle hMclk, __s32 bOnOff);

__s32 OSAL_CCMU_MclkReset(__hdle hMclk, __s32 bReset);

#endif /* __OSAL_CLOCK_H__ */
