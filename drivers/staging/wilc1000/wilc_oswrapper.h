#ifndef __WILC_OSWRAPPER_H__
#define __WILC_OSWRAPPER_H__

/*!
 *  @file	wilc_oswrapper.h
 *  @brief	Top level OS Wrapper, include this file and it will include all
 *              other files as necessary
 *  @author	syounan
 *  @date	10 Aug 2010
 *  @version	1.0
 */

/* OS Wrapper interface version */
#define WILC_OSW_INTERFACE_VER 2

/* Integer Types */
typedef unsigned char WILC_Uint8;
typedef unsigned short WILC_Uint16;
typedef unsigned int WILC_Uint32;
typedef unsigned long long WILC_Uint64;
typedef signed char WILC_Sint8;
typedef signed short WILC_Sint16;
typedef signed int WILC_Sint32;
typedef signed long long WILC_Sint64;

/* Floating types */
typedef float WILC_Float;
typedef double WILC_Double;

/* Boolean type */
typedef enum {
	WILC_FALSE = 0,
	WILC_TRUE = 1
} WILC_Bool;

/* Character types */
typedef char WILC_Char;
typedef WILC_Uint16 WILC_WideChar;

#define WILC_OS_INFINITY (~((WILC_Uint32)0))
#define WILC_NULL ((void *)0)

/* standard min and max macros */
#define WILC_MIN(a, b)  (((a) < (b)) ? (a) : (b))
#define WILC_MAX(a, b)  (((a) > (b)) ? (a) : (b))

/* Os Configuration File */
#include "wilc_osconfig.h"

/* Platform specific include */
#if WILC_PLATFORM == WILC_WIN32
#include "wilc_platform.h"
#elif WILC_PLATFORM == WILC_NU
#include "wilc_platform.h"
#elif WILC_PLATFORM == WILC_MTK
#include "wilc_platform.h"
#elif WILC_PLATFORM == WILC_LINUX
#include "wilc_platform.h"
#elif WILC_PLATFORM == WILC_LINUXKERNEL
#include "wilc_platform.h"
#else
#error "OS not supported"
#endif

/* Logging Functions */
#include "wilc_log.h"

/* Error reporting and handling support */
#include "wilc_errorsupport.h"

/* Thread support */
#ifdef CONFIG_WILC_THREAD_FEATURE
#include "wilc_thread.h"
#endif

/* Semaphore support */
#ifdef CONFIG_WILC_SEMAPHORE_FEATURE
#include "wilc_semaphore.h"
#endif

/* Sleep support */
#ifdef CONFIG_WILC_SLEEP_FEATURE
#include "wilc_sleep.h"
#endif

/* Timer support */
#ifdef CONFIG_WILC_TIMER_FEATURE
#include "wilc_timer.h"
#endif

/* Memory support */
#ifdef CONFIG_WILC_MEMORY_FEATURE
#include "wilc_memory.h"
#endif

/* String Utilities */
#ifdef CONFIG_WILC_STRING_UTILS
#include "wilc_strutils.h"
#endif

/* Message Queue */
#ifdef CONFIG_WILC_MSG_QUEUE_FEATURE
#include "wilc_msgqueue.h"
#endif

/* File operations */
#ifdef CONFIG_WILC_FILE_OPERATIONS_FEATURE
#include "wilc_fileops.h"
#endif

/* Time operations */
#ifdef CONFIG_WILC_TIME_FEATURE
#include "wilc_time.h"
#endif

/* Event support */
#ifdef CONFIG_WILC_EVENT_FEATURE
#include "wilc_event.h"
#endif

/* Socket operations */
#ifdef CONFIG_WILC_SOCKET_FEATURE
#include "wilc_socket.h"
#endif

/* Math operations */
#ifdef CONFIG_WILC_MATH_OPERATIONS_FEATURE
#include "wilc_math.h"
#endif



#endif
