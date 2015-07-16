
#include "wilc_memory.h"

/*!
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
void *WILC_MemoryAlloc(u32 u32Size, tstrWILC_MemoryAttrs *strAttrs,
		       char *pcFileName, u32 u32LineNo)
{
	if (u32Size > 0) {
		return kmalloc(u32Size, GFP_ATOMIC);
	} else {
		return NULL;
	}
}

/*!
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
void *WILC_MemoryCalloc(u32 u32Size, tstrWILC_MemoryAttrs *strAttrs,
			char *pcFileName, u32 u32LineNo)
{
	return kcalloc(u32Size, 1, GFP_KERNEL);
}

/*!
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
void *WILC_MemoryRealloc(void *pvOldBlock, u32 u32NewSize,
			 tstrWILC_MemoryAttrs *strAttrs, char *pcFileName, u32 u32LineNo)
{
	if (u32NewSize == 0) {
		kfree(pvOldBlock);
		return NULL;
	} else if (pvOldBlock == NULL)	 {
		return kmalloc(u32NewSize, GFP_KERNEL);
	} else {
		return krealloc(pvOldBlock, u32NewSize, GFP_KERNEL);
	}

}

/*!
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
void WILC_MemoryFree(const void *pvBlock, tstrWILC_MemoryAttrs *strAttrs,
		     char *pcFileName, u32 u32LineNo)
{
	kfree(pvBlock);
}
