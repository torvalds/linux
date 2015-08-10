#ifndef __WILC_STRUTILS_H__
#define __WILC_STRUTILS_H__

/*!
 *  @file	wilc_strutils.h
 *  @brief	Basic string utilities
 *  @author	syounan
 *  @sa		wilc_oswrapper.h top level OS wrapper file
 *  @date	16 Aug 2010
 *  @version	1.0
 */

#include <linux/types.h>
#include <linux/string.h>
#include "wilc_errorsupport.h"


/*!
 *  @brief	Internal implementation for memory copy
 *  @param[in]	pvTarget the target buffer to which the data is copied into
 *  @param[in]	pvSource pointer to the second memory location
 *  @param[in]	u32Count the size of the data to copy
 *  @note	this function should not be used directly, use WILC_memcpy instead
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
void WILC_memcpy_INTERNAL(void *pvTarget, const void *pvSource, u32 u32Count);






#endif
