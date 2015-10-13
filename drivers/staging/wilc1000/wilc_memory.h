#ifndef __WILC_MEMORY_H__
#define __WILC_MEMORY_H__

/*!
 *  @file	wilc_memory.h
 *  @brief	Memory OS wrapper functionality
 *  @author	syounan
 *  @sa		wilc_oswrapper.h top level OS wrapper file
 *  @date	16 Aug 2010
 *  @version	1.0
 */

#include <linux/types.h>
#include <linux/slab.h>

/*!
 *  @struct             tstrWILC_MemoryAttrs
 *  @brief		Memory API options
 *  @author		syounan
 *  @date		16 Aug 2010
 *  @version		1.0
 */
typedef struct {
} tstrWILC_MemoryAttrs;

/*!
 *  @brief	Allocates a given size of bytes
 *  @param[in]	u32Size size of memory in bytes to be allocated
 *  @param[in]	strAttrs Optional attributes, NULL for default
 *              if not NULL, pAllocationPool should point to the pool to use for
 *              this allocation. if NULL memory will be allocated directly from
 *              the system
 *  @param[in]	pcFileName file name of the calling code for debugging
 *  @param[in]	u32LineNo line number of the calling code for debugging
 *  @return	The new allocated block, NULL if allocation fails
 *  @note	It is recommended to use of of the wrapper macros instead of
 *              calling this function directly
 *  @sa		sttrWILC_MemoryAttrs
 *  @sa		WILC_MALLOC
 *  @sa		WILC_MALLOC_EX
 *  @author	syounan
 *  @date	16 Aug 2010
 *  @version	1.0
 */
void *WILC_MemoryAlloc(u32 u32Size, tstrWILC_MemoryAttrs *strAttrs,
		       char *pcFileName, u32 u32LineNo);

/*!
 * @brief	standrad malloc wrapper with custom attributes
 */
	#define WILC_MALLOC_EX(__size__, __attrs__) \
	(WILC_MemoryAlloc( \
		 (__size__), __attrs__, NULL, 0))


/*!
 * @brief	standrad malloc wrapper with default attributes
 */
#define WILC_MALLOC(__size__) \
	WILC_MALLOC_EX(__size__, NULL)





#endif
