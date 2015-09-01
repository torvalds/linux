
#define _CRT_SECURE_NO_DEPRECATE

#include "wilc_strutils.h"


/*!
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
s32 WILC_memcmp(const void *pvArg1, const void *pvArg2, u32 u32Count)
{
	return memcmp(pvArg1, pvArg2, u32Count);
}


/*!
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
void WILC_memcpy_INTERNAL(void *pvTarget, const void *pvSource, u32 u32Count)
{
	memcpy(pvTarget, pvSource, u32Count);
}

/*!
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
void *WILC_memset(void *pvTarget, u8 u8SetValue, u32 u32Count)
{
	return memset(pvTarget, u8SetValue, u32Count);
}

/*!
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
char *WILC_strncpy(char *pcTarget, const char *pcSource,
			u32 u32Count)
{
	return strncpy(pcTarget, pcSource, u32Count);
}

s32 WILC_strncmp(const char *pcStr1, const char *pcStr2,
			 u32 u32Count)
{
	s32 s32Result;

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
u32 WILC_strlen(const char *pcStr)
{
	return (u32)strlen(pcStr);
}
