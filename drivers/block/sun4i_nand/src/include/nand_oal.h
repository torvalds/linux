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
