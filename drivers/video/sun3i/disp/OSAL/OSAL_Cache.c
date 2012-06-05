/*
 * drivers/video/sun3i/disp/OSAL/OSAL_Cache.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
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

#include "OSAL.h"

/* 刷新标记位 */
#define  CACHE_FLUSH_I_CACHE_REGION				0  /* 清除I-cache中代表主存中一块区域的cache行 			*/
#define  CACHE_FLUSH_D_CACHE_REGION				1  /* 清除D-cache中代表主存中一块区域的cache行 			*/
#define  CACHE_FLUSH_CACHE_REGION				2  /* 清除D-cache和I-cache中代表主存中一块区域的cache行 */
#define  CACHE_CLEAN_D_CACHE_REGION				3  /* 清理D-cache中代表主存中一块区域的cache行 			*/
#define  CACHE_CLEAN_FLUSH_D_CACHE_REGION	 	4  /* 清理并清除D-cache中代表主存中一块区域的cache行 	*/
#define  CACHE_CLEAN_FLUSH_CACHE_REGION			5  /* 清理并清除D-cache，接下来解除I-cache 				*/

/*
*******************************************************************************
*                     OSAL_CacheRangeFlush
*
* Description:
*    Cache操作
*
* Parameters:
*    Address    :  要被刷新的虚拟起始地址
*    Length     :  被刷新的大小
*    Flags      :  刷新标记位
*
* Return value:
*    void
*
* note:
*    void
*
*******************************************************************************
*/
void OSAL_CacheRangeFlush(void*Address, __u32 Length, __u32 Flags)
{

    //flush_cache_range(NULL, (unsigned long)Address, ((unsigned long)Address)+Length);
        if(Address == NULL || Length == 0)
        {
            return;
        }

        switch(Flags)
        {
        case CACHE_FLUSH_I_CACHE_REGION:

            break;

        case CACHE_FLUSH_D_CACHE_REGION:
           // flush_cach
            break;

        case CACHE_FLUSH_CACHE_REGION:

            break;

        case CACHE_CLEAN_D_CACHE_REGION:
            //clean_dcache_area((unsigned long)Address, Length);
            break;

        case CACHE_CLEAN_FLUSH_D_CACHE_REGION:

            break;

        case CACHE_CLEAN_FLUSH_CACHE_REGION:

            break;

        default:

            break;
        }
        return;
}



