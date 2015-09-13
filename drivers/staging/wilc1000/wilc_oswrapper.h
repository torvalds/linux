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

/* Os Configuration File */
#include "wilc_platform.h"

/* Error reporting and handling support */
#include "wilc_errorsupport.h"

/* Memory support */
#include "wilc_memory.h"


/* Message Queue */
#include "wilc_msgqueue.h"

#endif
