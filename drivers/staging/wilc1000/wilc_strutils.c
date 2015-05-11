
#define _CRT_SECURE_NO_DEPRECATE

#include "wilc_oswrapper.h"

#ifdef CONFIG_WILC_STRING_UTILS


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
void *WILC_memset(void *pvTarget, WILC_Uint8 u8SetValue, WILC_Uint32 u32Count)
{
	return memset(pvTarget, u8SetValue, u32Count);
}

/*!
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
WILC_Char *WILC_strncat(WILC_Char *pcTarget, const WILC_Char *pcSource,
			WILC_Uint32 u32Count)
{
	return strncat(pcTarget, pcSource, u32Count);
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

/*!
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
WILC_Sint32 WILC_strcmp(const WILC_Char *pcStr1, const WILC_Char *pcStr2)
{
	WILC_Sint32 s32Result;

	if (pcStr1 == WILC_NULL && pcStr2 == WILC_NULL)	{
		s32Result = 0;
	} else if (pcStr1 == WILC_NULL)	   {
		s32Result = -1;
	} else if (pcStr2 == WILC_NULL)	   {
		s32Result = 1;
	} else {
		s32Result = strcmp(pcStr1, pcStr2);
		if (s32Result < 0) {
			s32Result = -1;
		} else if (s32Result > 0)    {
			s32Result = 1;
		}
	}

	return s32Result;
}

WILC_Sint32 WILC_strncmp(const WILC_Char *pcStr1, const WILC_Char *pcStr2,
			 WILC_Uint32 u32Count)
{
	WILC_Sint32 s32Result;

	if (pcStr1 == WILC_NULL && pcStr2 == WILC_NULL)	{
		s32Result = 0;
	} else if (pcStr1 == WILC_NULL)	   {
		s32Result = -1;
	} else if (pcStr2 == WILC_NULL)	   {
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

/*
 *  @author	syounan
 *  @date	1 Nov 2010
 *  @version	2.0
 */
WILC_Sint32 WILC_strcmp_IgnoreCase(const WILC_Char *pcStr1, const WILC_Char *pcStr2)
{
	WILC_Sint32 s32Result;

	if (pcStr1 == WILC_NULL && pcStr2 == WILC_NULL)	{
		s32Result = 0;
	} else if (pcStr1 == WILC_NULL)	   {
		s32Result = -1;
	} else if (pcStr2 == WILC_NULL)	   {
		s32Result = 1;
	} else {
		WILC_Char cTestedChar1, cTestedChar2;
		do {
			cTestedChar1 = *pcStr1;
			if ((*pcStr1 >= 'a') && (*pcStr1 <= 'z')) {
				/* turn a lower case character to an upper case one */
				cTestedChar1 -= 32;
			}

			cTestedChar2 = *pcStr2;
			if ((*pcStr2 >= 'a') && (*pcStr2 <= 'z')) {
				/* turn a lower case character to an upper case one */
				cTestedChar2 -= 32;
			}

			pcStr1++;
			pcStr2++;

		} while ((cTestedChar1 == cTestedChar2)
			 && (cTestedChar1 != 0)
			 && (cTestedChar2 != 0));

		if (cTestedChar1 > cTestedChar2) {
			s32Result = 1;
		} else if (cTestedChar1 < cTestedChar2)	   {
			s32Result = -1;
		} else {
			s32Result = 0;
		}
	}

	return s32Result;
}

/*!
 *  @author	aabozaeid
 *  @date	8 Dec 2010
 *  @version	1.0
 */
WILC_Sint32 WILC_strncmp_IgnoreCase(const WILC_Char *pcStr1, const WILC_Char *pcStr2,
				    WILC_Uint32 u32Count)
{
	WILC_Sint32 s32Result;

	if (pcStr1 == WILC_NULL && pcStr2 == WILC_NULL)	{
		s32Result = 0;
	} else if (pcStr1 == WILC_NULL)	   {
		s32Result = -1;
	} else if (pcStr2 == WILC_NULL)	   {
		s32Result = 1;
	} else {
		WILC_Char cTestedChar1, cTestedChar2;
		do {
			cTestedChar1 = *pcStr1;
			if ((*pcStr1 >= 'a') && (*pcStr1 <= 'z')) {
				/* turn a lower case character to an upper case one */
				cTestedChar1 -= 32;
			}

			cTestedChar2 = *pcStr2;
			if ((*pcStr2 >= 'a') && (*pcStr2 <= 'z')) {
				/* turn a lower case character to an upper case one */
				cTestedChar2 -= 32;
			}

			pcStr1++;
			pcStr2++;
			u32Count--;

		} while ((u32Count > 0)
			 && (cTestedChar1 == cTestedChar2)
			 && (cTestedChar1 != 0)
			 && (cTestedChar2 != 0));

		if (cTestedChar1 > cTestedChar2) {
			s32Result = 1;
		} else if (cTestedChar1 < cTestedChar2)	   {
			s32Result = -1;
		} else {
			s32Result = 0;
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

/*!
 *  @author	bfahmy
 *  @date	28 Aug 2010
 *  @version	1.0
 */
WILC_Sint32 WILC_strtoint(const WILC_Char *pcStr)
{
	return (WILC_Sint32)(simple_strtol(pcStr, NULL, 10));
}

/*
 *  @author	syounan
 *  @date	1 Nov 2010
 *  @version	2.0
 */
WILC_ErrNo WILC_snprintf(WILC_Char *pcTarget, WILC_Uint32 u32Size,
			 const WILC_Char *pcFormat, ...)
{
	va_list argptr;
	va_start(argptr, pcFormat);
	if (vsnprintf(pcTarget, u32Size, pcFormat, argptr) < 0)	{
		/* if turncation happens windows does not properly terminate strings */
		pcTarget[u32Size - 1] = 0;
	}
	va_end(argptr);

	/* I find no sane way of detecting errors in windows, so let it all succeed ! */
	return WILC_SUCCESS;
}

#ifdef CONFIG_WILC_EXTENDED_STRING_OPERATIONS

/**
 *  @brief
 *  @details    Searches for the first occurrence of the character c in the first n bytes
 *                              of the string pointed to by the argument str.
 *                              Returns a pointer pointing to the first matching character,
 *                              or null if no match was found.
 *  @param[in]
 *  @return
 *  @note
 *  @author		remil
 *  @date		3 Nov 2010
 *  @version		1.0
 */
WILC_Char *WILC_memchr(const void *str, WILC_Char c, WILC_Sint32 n)
{
	return (WILC_Char *) memchr(str, c, (size_t)n);
}

/**
 *  @brief
 *  @details    Searches for the first occurrence of the character c (an unsigned char)
 *                              in the string pointed to by the argument str.
 *                              The terminating null character is considered to be part of the string.
 *                              Returns a pointer pointing to the first matching character,
 *                              or null if no match was found.
 *  @param[in]
 *  @return
 *  @note
 *  @author		remil
 *  @date		3 Nov 2010
 *  @version		1.0
 */
WILC_Char *WILC_strchr(const WILC_Char *str, WILC_Char c)
{
	return strchr(str, c);
}

/**
 *  @brief
 *  @details    Appends the string pointed to by str2 to the end of the string pointed to by str1.
 *                              The terminating null character of str1 is overwritten.
 *                              Copying stops once the terminating null character of str2 is copied. If overlapping occurs, the result is undefined.
 *                              The argument str1 is returned.
 *  @param[in]  WILC_Char* str1,
 *  @param[in]  WILC_Char* str2,
 *  @return             WILC_Char*
 *  @note
 *  @author		remil
 *  @date		3 Nov 2010
 *  @version		1.0
 */
WILC_Char *WILC_strcat(WILC_Char *str1, const WILC_Char *str2)
{
	return strcat(str1, str2);
}

/**
 *  @brief
 *  @details    Copy pcSource to pcTarget
 *  @param[in]  WILC_Char* pcTarget
 *  @param[in]  const WILC_Char* pcSource
 *  @return             WILC_Char*
 *  @note
 *  @author		remil
 *  @date		3 Nov 2010
 *  @version		1.0
 */
WILC_Char *WILC_strcpy(WILC_Char *pcTarget, const WILC_Char *pcSource)
{
	return strncpy(pcTarget, pcSource, strlen(pcSource));
}

/**
 *  @brief
 *  @details    Finds the first sequence of characters in the string str1 that
 *                              does not contain any character specified in str2.
 *                              Returns the length of this first sequence of characters found that
 *                              do not match with str2.
 *  @param[in]  const WILC_Char *str1
 *  @param[in]  const WILC_Char *str2
 *  @return             WILC_Uint32
 *  @note
 *  @author		remil
 *  @date		3 Nov 2010
 *  @version		1.0
 */
WILC_Uint32 WILC_strcspn(const WILC_Char *str1, const WILC_Char *str2)
{
	return (WILC_Uint32)strcspn(str1, str2);
}
#if 0
/**
 *  @brief
 *  @details    Searches an internal array for the error number errnum and returns a pointer
 *                              to an error message string.
 *                              Returns a pointer to an error message string.
 *  @param[in]  WILC_Sint32 errnum
 *  @return             WILC_Char*
 *  @note
 *  @author		remil
 *  @date		3 Nov 2010
 *  @version		1.0
 */
WILC_Char *WILC_strerror(WILC_Sint32 errnum)
{
	return strerror(errnum);
}
#endif

/**
 *  @brief
 *  @details    Finds the first occurrence of the entire string str2
 *                              (not including the terminating null character) which appears in the string str1.
 *                              Returns a pointer to the first occurrence of str2 in str1.
 *                              If no match was found, then a null pointer is returned.
 *                              If str2 points to a string of zero length, then the argument str1 is returned.
 *  @param[in]  const WILC_Char *str1
 *  @param[in]  const WILC_Char *str2
 *  @return             WILC_Char*
 *  @note
 *  @author		remil
 *  @date		3 Nov 2010
 *  @version		1.0
 */
WILC_Char *WILC_strstr(const WILC_Char *str1, const WILC_Char *str2)
{
	return strstr(str1, str2);
}
#if 0
/**
 *  @brief
 *  @details    Parses the C string str interpreting its content as a floating point
 *                              number and returns its value as a double.
 *                              If endptr is not a null pointer, the function also sets the value pointed
 *                              by endptr to point to the first character after the number.
 *  @param[in]  const WILC_Char* str
 *  @param[in]  WILC_Char** endptr
 *  @return             WILC_Double
 *  @note
 *  @author		remil
 *  @date		11 Nov 2010
 *  @version		1.0
 */
WILC_Double WILC_StringToDouble(const WILC_Char *str, WILC_Char **endptr)
{
	return strtod (str, endptr);
}
#endif

/**
 *  @brief              Parses the C string str interpreting its content as an unsigned integral
 *                              number of the specified base, which is returned as an unsigned long int value.
 *  @details    The function first discards as many whitespace characters as necessary
 *                              until the first non-whitespace character is found.
 *                              Then, starting from this character, takes as many characters as possible
 *                              that are valid following a syntax that depends on the base parameter,
 *                              and interprets them as a numerical value.
 *                              Finally, a pointer to the first character following the integer
 *                              representation in str is stored in the object pointed by endptr.
 *  @param[in]  const WILC_Char *str
 *  @param[in]	WILC_Char **endptr
 *  @param[in]	WILC_Sint32 base
 *  @return             WILC_Uint32
 *  @note
 *  @author		remil
 *  @date		11 Nov 2010
 *  @version		1.0
 */
WILC_Uint32 WILC_StringToUint32(const WILC_Char *str, WILC_Char **endptr, WILC_Sint32 base)
{
	return simple_strtoul(str, endptr, base);
}

#endif

#endif
