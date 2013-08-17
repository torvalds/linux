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

#if !defined(__SYSINFO_H__)
#define __SYSINFO_H__

#if defined(PVR_LINUX_USING_WORKQUEUES)
#define MAX_HW_TIME_US				(1000000)
#define WAIT_TRY_COUNT				(20000)
#else
#define MAX_HW_TIME_US				(500000)
#define WAIT_TRY_COUNT				(10000)
#endif


typedef enum _SYS_DEVICE_TYPE_
{
	SYS_DEVICE_SGX						= 0,

	SYS_DEVICE_FORCE_I16 				= 0x7fff

} SYS_DEVICE_TYPE;

#define SYS_DEVICE_COUNT 3 



#define SGX_SP_FIFO_DWSIZE         	123


#define SGX_SP_FIFO_RESERVEBYTES   	(SGX_SP_FIFO_DWSIZE & -4)
#define SGX_SP_FIFO_MAXALLOWEDBYTES	(SGX_SP_FIFO_DWSIZE * 4) - SGX_SP_FIFO_RESERVEBYTES

#define SGX_EXTRACT_FIFO_COUNT(x)   (((x) & SGX_INT_TA_FREEVCOUNT_MASK) >> SGX_INT_TA_FREEVCOUNT_SHIFT)


#endif	
