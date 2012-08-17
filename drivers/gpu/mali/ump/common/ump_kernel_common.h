/*
 * Copyright (C) 2010-2012 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __UMP_KERNEL_COMMON_H__
#define __UMP_KERNEL_COMMON_H__

#include "ump_kernel_types.h"
#include "ump_kernel_interface.h"
#include "ump_kernel_descriptor_mapping.h"
#include "ump_kernel_memory_backend.h"


#ifdef DEBUG
	extern int ump_debug_level;
	#define UMP_DEBUG_PRINT(args) _mali_osk_dbgmsg args
	#define UMP_DEBUG_CODE(args) args
	#define DBG_MSG(level,args)  do { /* args should be in brackets */ \
		((level) <=  ump_debug_level)?\
		UMP_DEBUG_PRINT(("UMP<" #level ">: ")), \
		UMP_DEBUG_PRINT(args):0; \
		} while (0)

	#define DBG_MSG_IF(level,condition,args) /* args should be in brackets */ \
		if((condition)&&((level) <=  ump_debug_level)) {\
		UMP_DEBUG_PRINT(("UMP<" #level ">: ")); \
		UMP_DEBUG_PRINT(args); \
		}

	#define DBG_MSG_ELSE(level,args) /* args should be in brackets */ \
		else if((level) <=  ump_debug_level) { \
		UMP_DEBUG_PRINT(("UMP<" #level ">: ")); \
		UMP_DEBUG_PRINT(args); \
		}

	#define DEBUG_ASSERT_POINTER(pointer) do  {if( (pointer)== NULL) MSG_ERR(("NULL pointer " #pointer)); } while(0)
	#define DEBUG_ASSERT(condition) do  {if(!(condition)) MSG_ERR(("ASSERT failed: " #condition)); } while(0)
#else /* DEBUG */
	#define UMP_DEBUG_PRINT(args) do {} while(0)
	#define UMP_DEBUG_CODE(args)
	#define DBG_MSG(level,args) do {} while(0)
	#define DBG_MSG_IF(level,condition,args) do {} while(0)
	#define DBG_MSG_ELSE(level,args) do {} while(0)
	#define DEBUG_ASSERT(condition) do {} while(0)
	#define DEBUG_ASSERT_POINTER(pointer) do  {} while(0)
#endif /* DEBUG */

#define MSG_ERR(args) do{ /* args should be in brackets */ \
	 _mali_osk_dbgmsg("UMP: ERR: %s\n" ,__FILE__); \
	 _mali_osk_dbgmsg( "           %s()%4d\n", __FUNCTION__, __LINE__) ; \
	 _mali_osk_dbgmsg args ; \
	 _mali_osk_dbgmsg("\n"); \
	} while(0)

#define MSG(args) do{ /* args should be in brackets */ \
		 _mali_osk_dbgmsg("UMP: "); \
		 _mali_osk_dbgmsg args; \
		} while (0)



/*
 * This struct is used to store per session data.
 * A session is created when someone open() the device, and
 * closed when someone close() it or the user space application terminates.
 */
typedef struct ump_session_data
{
	_mali_osk_list_t list_head_session_memory_list;  /**< List of ump allocations made by the process (elements are ump_session_memory_list_element) */
	_mali_osk_list_t list_head_session_memory_mappings_list; /**< List of ump_memory_allocations mapped in */
	int api_version;
	_mali_osk_lock_t * lock;
	ump_descriptor_mapping * cookies_map; /**< Secure mapping of cookies from _ump_ukk_map_mem() */
	int cache_operations_ongoing;
	int has_pending_level1_cache_flush;
} ump_session_data;



/*
 * This struct is used to track the UMP memory references a session has.
 * We need to track this in order to be able to clean up after user space processes
 * which don't do it themself (e.g. due to a crash or premature termination).
 */
typedef struct ump_session_memory_list_element
{
	struct ump_dd_mem * mem;
	_mali_osk_list_t list;
} ump_session_memory_list_element;



/*
 * Device specific data, created when device driver is loaded, and then kept as the global variable device.
 */
typedef struct ump_dev
{
	_mali_osk_lock_t * secure_id_map_lock;
	ump_descriptor_mapping * secure_id_map;
	ump_memory_backend * backend;
} ump_dev;



extern int ump_debug_level;
extern struct ump_dev device;

_mali_osk_errcode_t ump_kernel_constructor(void);
void ump_kernel_destructor(void);
int map_errcode( _mali_osk_errcode_t err );

/**
 * variables from user space cannot be dereferenced from kernel space; tagging them
 * with __user allows the GCC compiler to generate a warning. Other compilers may
 * not support this so we define it here as an empty macro if the compiler doesn't
 * define it.
 */
#ifndef __user
#define __user
#endif

#endif /* __UMP_KERNEL_COMMON_H__ */
