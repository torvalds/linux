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
#include "wilc_platform.h"

/* Logging Functions */
#include "wilc_log.h"

/* Error reporting and handling support */
#include "wilc_errorsupport.h"

/* Sleep support */
#include "wilc_sleep.h"

/* Timer support */
#include "wilc_timer.h"

/* Memory support */
#include "wilc_memory.h"

/* String Utilities */
#include "wilc_strutils.h"

/* Message Queue */
#include "wilc_msgqueue.h"

#endif
