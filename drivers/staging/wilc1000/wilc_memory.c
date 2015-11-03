
#include "wilc_memory.h"

/*!
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
void *WILC_MemoryAlloc(u32 u32Size, tstrWILC_MemoryAttrs *strAttrs,
		       char *pcFileName, u32 u32LineNo)
{
	if (u32Size > 0)
		return kmalloc(u32Size, GFP_ATOMIC);
	else
		return NULL;
}
