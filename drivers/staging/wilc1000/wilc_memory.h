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
 *  @sa		WILC_NEW
 *  @sa		WILC_NEW_EX
 *  @author	syounan
 *  @date	16 Aug 2010
 *  @version	1.0
 */
void *WILC_MemoryAlloc(u32 u32Size, tstrWILC_MemoryAttrs *strAttrs,
		       char *pcFileName, u32 u32LineNo);



/*!
 *  @brief	Frees given block
 *  @param[in]	pvBlock the memory block to be freed
 *  @param[in]	strAttrs Optional attributes, NULL for default
 *  @param[in]	pcFileName file name of the calling code for debugging
 *  @param[in]	u32LineNo line number of the calling code for debugging
 *  @note	It is recommended to use of of the wrapper macros instead of
 *              calling this function directly
 *  @sa		sttrWILC_MemoryAttrs
 *  @sa		WILC_FREE
 *  @sa		WILC_FREE_EX
 *  @sa		WILC_FREE_SET_NULL
 *  @sa		WILC_FREE_IF_TRUE
 *  @author	syounan
 *  @date	16 Aug 2010
 *  @version	1.0
 */
void WILC_MemoryFree(const void *pvBlock, tstrWILC_MemoryAttrs *strAttrs,
			char *pcFileName, u32 u32LineNo);

/*!
 * @brief	standrad malloc wrapper with custom attributes
 */
	#define WILC_MALLOC_EX(__size__, __attrs__) \
	(WILC_MemoryAlloc( \
		 (__size__), __attrs__, NULL, 0))


/*!
 * @brief	standrad free wrapper with custom attributes
 */
	#define WILC_FREE_EX(__ptr__, __attrs__) \
	(WILC_MemoryFree( \
		 (__ptr__), __attrs__, NULL, 0))

/*!
 * @brief	Allocates a block (with custom attributes) of given type and number of
 * elements
 */
#define WILC_NEW_EX(__struct_type__, __n_structs__, __attrs__) \
	((__struct_type__ *)WILC_MALLOC_EX( \
		 sizeof(__struct_type__) * (u32)(__n_structs__), __attrs__))



/*!
 * @brief	Frees a block (with custom attributes) if the pointer expression evaluates
 * to true
 */
#define WILC_FREE_IF_TRUE_EX(__ptr__, __attrs__) do { \
		if (__ptr__ != NULL) { \
			WILC_FREE_EX(__ptr__, __attrs__); \
		} \
} while (0)

/*!
 * @brief	standrad malloc wrapper with default attributes
 */
#define WILC_MALLOC(__size__) \
	WILC_MALLOC_EX(__size__, NULL)



/*!
 * @brief	standrad free wrapper with default attributes
 */
#define WILC_FREE(__ptr__) \
	WILC_FREE_EX(__ptr__, NULL)

/*!
 * @brief	Allocates a block (with default attributes) of given type and number of
 * elements
 */
#define WILC_NEW(__struct_type__, __n_structs__) \
	WILC_NEW_EX(__struct_type__, __n_structs__, NULL)



/*!
 * @brief	Frees a block (with default attributes) if the pointer expression evaluates
 * to true
 */
#define WILC_FREE_IF_TRUE(__ptr__) \
	WILC_FREE_IF_TRUE_EX(__ptr__, NULL)


#endif
