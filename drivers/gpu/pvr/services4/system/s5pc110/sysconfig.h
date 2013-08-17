/**********************************************************************
 *
 * Copyright(c) 2008 Imagination Technologies Ltd. All rights reserved.
 * 		Samsung Electronics System LSI. modify
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 * 
 * This program is distributed in the hope it will be useful but, except 
 * as otherwise stated in writing, without any warranty; without even the 
 * implied warranty of merchantability or fitness for a particular purpose. 
 * See the GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * 
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * Imagination Technologies Ltd. <gpl-support@imgtec.com>
 * Home Park Estate, Kings Langley, Herts, WD4 8LZ, UK 
 *
 ******************************************************************************/

#if !defined(__SOCCONFIG_H__)
#define __SOCCONFIG_H__

#include "syscommon.h"

#define VS_PRODUCT_NAME	"s5pc110"

extern struct platform_device *gpsPVRLDMDev;

#define SYS_SGX_USSE_COUNT					(1)

#define SGX_REG_SIZE 	0x4000
#define SGX_SP_SIZE		(0x10000-SGX_REG_SIZE)

#if defined(SGX_FEATURE_HOST_PORT)
	
	#define SYS_SGX_HP_SIZE		0x0
	
	#define SYS_SGX_HOSTPORT_BASE_DEVVADDR 0x0
	#if defined(FIX_HW_BRN_22997) && defined(FIX_HW_BRN_23030)
		
		#define SYS_SGX_HOSTPORT_BRN23030_OFFSET 0x0
	#endif
#endif
#endif	
