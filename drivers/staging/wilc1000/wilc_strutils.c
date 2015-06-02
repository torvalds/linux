
#define _CRT_SECURE_NO_DEPRECATE

#include "wilc_oswrapper.h"


/*!
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
WILC_Sint32 WILC_memcmp(const void *pvArg1, const void *pvArg2, WILC_Uint32 u32Count)
{
	return memcmp(pvArg1, pvArg2, u32Count);
}


/*!
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
void WILC_memcpy_INTERNAL(void *pvTarget, const void *pvSource, WILC_Uint32 u32Count)
{
	memcpy(pvTarget, pvSource, u32Count);
}

/*!
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
void *WILC_memset(void *pvTarget, u8 u8SetValue, WILC_Uint32 u32Count)
{
	return memset(pvTarget, u8SetValue, u32Count);
}

/*!
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
WILC_Char *WILC_strncpy(WILC_Char *pcTarget, const WILC_Char *pcSource,
			WILC_Uint32 u32Count)
{
	return strncpy(pcTarget, pcSource, u32Count);
}

WILC_Sint32 WILC_strncmp(const WILC_Char *pcStr1, const WILC_Char *pcStr2,
			 WILC_Uint32 u32Count)
{
	WILC_Sint32 s32Result;

	if (pcStr1 == NULL && pcStr2 == NULL)	{
		s32Result = 0;
	} else if (pcStr1 == NULL)	   {
		s32Result = -1;
	} else if (pcStr2 == NULL)	   {
		s32Result = 1;
	} else {
		s32Result = strncmp(pcStr1, pcStr2, u32Count);
		if (s32Result < 0) {
			s32Result = -1;
		} else if (s32Result > 0)    {
			s32Result = 1;
		}
	}

	return s32Result;
}

/*!
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
WILC_Uint32 WILC_strlen(const WILC_Char *pcStr)
{
	return (WILC_Uint32)strlen(pcStr);
}
