/*
 * drivers/block/sunxi_nand/src/include/nand_oal.h
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

#ifndef 	__NAND_OAL__
#define  	__NAND_OAL__

#include "../../include/type_def.h"
#include "../../nfd/nand_user_cfg.h"
#include <linux/string.h>
#include <linux/slab.h>
//#include "../../sys_include/epdk.h"


//define the memory set interface
#define MEMSET(x,y,z)            			memset((x),(y),(z))

//define the memory copy interface
#define MEMCPY(x,y,z)                   	memcpy((x),(y),(z))

//define the memory alocate interface
#define MALLOC(x)                       	kmalloc((x), GFP_KERNEL)

//define the memory release interface
#define FREE(x,size)                    	kfree((x))
//define the message print interface
#define PRINT(...)							printk(__VA_ARGS__)


#if 0
#ifdef		OS_KERNEL

////define the memory set interface
//#define MEMSET(x,y,z)            			eLIBs_memset(x,y,z)
//
////define the memory copy interface
//#define MEMCPY(x,y,z)                   	eLIBs_memcpy(x,y,z)
//
////define the memory alocate interface
//#define MALLOC(x)                       	esMEMS_Balloc(x)
//
////define the memory release interface
//#define FREE(x,size)                    	esMEMS_Bfree(x,size)
////define the message print interface
//#define PRINT(...)							__inf(__VA_ARGS__)


//define the memory set interface
#define MEMSET(x,y,z)            			memset(x,y,z)

//define the memory copy interface
#define MEMCPY(x,y,z)                   	memcpy(x,y,z)

//define the memory alocate interface
#define MALLOC(x)                       	kmalloc(x, GFP_KERNEL)

//define the memory release interface
#define FREE(x,size)                    	kfree(x)
//define the message print interface
#define PRINT(...)							printk(__VA_ARGS__)
#else
#include "enviroment.h"
//#include "..\\..\\..\\..\\..\\interinc\\efex\\efex_libs.h"

//define the memory set interface
#define MEMSET(x,y,z)                   	kmemset(x,y,z)

//define the memory copy interface
#define MEMCPY(x,y,z)                   	kmemcpy(x,y,z)

//define the memory alocate interface
#define MALLOC(x)                       	kmalloc(x)

//define the memory release interface
#define FREE(x,size)                   		kfree(x)
//define the message print interface
#define PRINT(...)								eFG_printf(__VA_ARGS__)
//#define PRINT(...)
#endif

#endif

#endif
