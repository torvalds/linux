/*
*************************************************************************************
*                         			eBsp
*					   Operation System Adapter Layer
*
*				(c) Copyright 2006-2010, All winners Co,Ld.
*							All	Rights Reserved
*
* File Name 	: OSAL_Cache.h
*
* Author 		: javen
*
* Description 	: Cache操作
*
* History 		:
*      <author>    		<time>       	<version >    		<desc>
*       javen     	   2010-09-07          1.0         create this word
*
*************************************************************************************
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



