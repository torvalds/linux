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
 *  @brief	Compares two memory buffers
 *  @param[in]	pvArg1 pointer to the first memory location
 *  @param[in]	pvArg2 pointer to the second memory location
 *  @param[in]	u32Count the size of the memory buffers
 *  @return	0 if the 2 buffers are equal, 1 if pvArg1 is bigger than pvArg2,
 *              -1 if pvArg1 smaller than pvArg2
 *  @note	this function repeats the functionality of standard memcmp
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
s32 WILC_memcmp(const void *pvArg1, const void *pvArg2, u32 u32Count);

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

/*!
 *  @brief	Copies the contents of a memory buffer into another
 *  @param[in]	pvTarget the target buffer to which the data is copied into
 *  @param[in]	pvSource pointer to the second memory location
 *  @param[in]	u32Count the size of the data to copy
 *  @return	WILC_SUCCESS if copy is successfully handeled
 *              WILC_FAIL if copy failed
 *  @note	this function repeats the functionality of standard memcpy,
 *              however memcpy is undefined if the two buffers overlap but this
 *              implementation will check for overlap and report error
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
static WILC_ErrNo WILC_memcpy(void *pvTarget, const void *pvSource, u32 u32Count)
{
	if (
		(((u8 *)pvTarget <= (u8 *)pvSource)
		 && (((u8 *)pvTarget + u32Count) > (u8 *)pvSource))

		|| (((u8 *)pvSource <= (u8 *)pvTarget)
		    && (((u8 *)pvSource + u32Count) > (u8 *)pvTarget))
		) {
		/* ovelapped memory, return Error */
		return WILC_FAIL;
	} else {
		WILC_memcpy_INTERNAL(pvTarget, pvSource, u32Count);
		return WILC_SUCCESS;
	}
}

/*!
 *  @brief	Sets the contents of a memory buffer with the given value
 *  @param[in]	pvTarget the target buffer which contsnts will be set
 *  @param[in]	u8SetValue the value to be used
 *  @param[in]	u32Count the size of the memory buffer
 *  @return	value of pvTarget
 *  @note	this function repeats the functionality of standard memset
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
void *WILC_memset(void *pvTarget, u8 u8SetValue, u32 u32Count);

/*!
 *  @brief	copies the contents of source string into the target string
 *  @param[in]	pcTarget the target string buffer
 *  @param[in]	pcSource the source string the will be copied
 *  @param[in]	u32Count copying will proceed until a null character in pcSource
 *              is encountered or u32Count of bytes copied
 *  @return	value of pcTarget
 *  @note	this function repeats the functionality of standard strncpy
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
char *WILC_strncpy(char *pcTarget, const char *pcSource,
			u32 u32Count);

/*!
 *  @brief	Compares two strings up to u32Count characters
 *  @details	Compares 2 strings reporting which is bigger, NULL is considered
 *              the smallest string, then a zero length string then all other
 *              strings depending on thier ascii characters order with small case
 *              converted to uppder case
 *  @param[in]	pcStr1 the first string, NULL is valid and considered smaller
 *              than any other non-NULL string (incliding zero lenght strings)
 *  @param[in]	pcStr2 the second string, NULL is valid and considered smaller
 *              than any other non-NULL string (incliding zero lenght strings)
 *  @param[in]	u32Count copying will proceed until a null character in pcStr1 or
 *              pcStr2 is encountered or u32Count of bytes copied
 *  @return	0 if the 2 strings are equal, 1 if pcStr1 is bigger than pcStr2,
 *              -1 if pcStr1 smaller than pcStr2
 *  @author	aabozaeid
 *  @date	7 Dec 2010
 *  @version	1.0
 */
s32 WILC_strncmp(const char *pcStr1, const char *pcStr2,
			 u32 u32Count);

/*!
 *  @brief	gets the length of a string
 *  @param[in]	pcStr the string
 *  @return	the length
 *  @note	this function repeats the functionality of standard strlen
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
u32 WILC_strlen(const char *pcStr);

#endif
