/*
 * SiIxxxx <Firmware or Driver>
 *
 * Copyright (C) 2011 Silicon Image Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed .as is. WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
 * PURPOSE.  See the GNU General Public License for more details.
*/
/**
 * @file os_data.h
 *
 * OSAL data types (public)
 *
 * Don't use source control directives!
 * Don't use source control directives!
 * Don't use source control directives!
 *
 *****************************************************************************/

#ifndef _OS_DATA_H
#define _OS_DATA_H

#ifdef PLATFORM3
#error This file is not designed for use on the specified platform.
#endif

#if !defined(__KERNEL__)
#include <sys/time.h>
#include <bits/local_lim.h>
#include <pthread.h>
#else
#include <linux/time.h>
#endif


#define OS_API_NAME_SIZE    16                      /* users can specify OSAL names up to specified size in APIs */
#define OS_PRIV_NAME_SIZE   (OS_API_NAME_SIZE + 16) /* OSAL might internally append suffixes to names. Hence,
                                                     * a bigger size is used to check the size limitations */

#define OS_NAME_SIZE        (OS_PRIV_NAME_SIZE + 16) /* actual size of name-array */


#define SII_OS_STACK_SIZE_MIN    PTHREAD_STACK_MIN   /* stack size during task creation */
/* Forward declaration to help compilation */
struct _SiiOsTaskInfo_t;
#define OS_CURRENT_TASK    ((struct _SiiOsTaskInfo_t *) 0)


/* The typedef below is used ONLY to abstract the pointer object in public APIs */
typedef struct _SiiOsSemInfo_t * SiiOsSemaphore_t;

/* the typedef below is used ONLY to abstract the pointer object in public APIs */
typedef struct _SiiOsQueueInfo_t * SiiOsQueue_t;

/* The typedef below is used ONLY to abstract the pointer object in public APIs */
typedef struct _SiiOsTaskInfo_t * SiiOsTask_t;


typedef struct
{
    struct timeval theTime;
} SiiOsTime_t;

typedef struct _SiiOsTimerInfo_t * SiiOsTimer_t;

#endif /* _OS_DATA_H */
