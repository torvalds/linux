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

#ifndef CONFIG_WILC_STRING_UTILS
#error the feature CONFIG_WILC_STRING_UTILS must be supported to include this file
#endif

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
WILC_Sint32 WILC_memcmp(const void *pvArg1, const void *pvArg2, WILC_Uint32 u32Count);

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
void WILC_memcpy_INTERNAL(void *pvTarget, const void *pvSource, WILC_Uint32 u32Count);

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
static WILC_ErrNo WILC_memcpy(void *pvTarget, const void *pvSource, WILC_Uint32 u32Count)
{
	if (
		(((WILC_Uint8 *)pvTarget <= (WILC_Uint8 *)pvSource)
		 && (((WILC_Uint8 *)pvTarget + u32Count) > (WILC_Uint8 *)pvSource))

		|| (((WILC_Uint8 *)pvSource <= (WILC_Uint8 *)pvTarget)
		    && (((WILC_Uint8 *)pvSource + u32Count) > (WILC_Uint8 *)pvTarget))
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
void *WILC_memset(void *pvTarget, WILC_Uint8 u8SetValue, WILC_Uint32 u32Count);

/*!
 *  @brief	Concatenates the contents of 2 strings up to a given count
 *  @param[in]	pcTarget the target string, its null character will be overwritten
 *              and contents of pcSource will be concatentaed to it
 *  @param[in]	pcSource the source string the will be concatentaed
 *  @param[in]	u32Count copying will proceed until a null character in pcSource
 *              is encountered or u32Count of bytes copied
 *  @return	value of pcTarget
 *  @note	this function repeats the functionality of standard strncat
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
WILC_Char *WILC_strncat(WILC_Char *pcTarget, const WILC_Char *pcSource,
			WILC_Uint32 u32Count);

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
WILC_Char *WILC_strncpy(WILC_Char *pcTarget, const WILC_Char *pcSource,
			WILC_Uint32 u32Count);

/*!
 *  @brief	Compares two strings
 *  @details	Compares 2 strings reporting which is bigger, WILC_NULL is considered
 *              the smallest string, then a zero length string then all other
 *              strings depending on thier ascii characters order
 *  @param[in]	pcStr1 the first string, WILC_NULL is valid and considered smaller
 *              than any other non-NULL string (incliding zero lenght strings)
 *  @param[in]	pcStr2 the second string, WILC_NULL is valid and considered smaller
 *              than any other non-NULL string (incliding zero lenght strings)
 *  @return	0 if the 2 strings are equal, 1 if pcStr1 is bigger than pcStr2,
 *              -1 if pcStr1 smaller than pcStr2
 *  @note	this function repeats the functionality of standard strcmp
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
WILC_Sint32 WILC_strcmp(const WILC_Char *pcStr1, const WILC_Char *pcStr2);

/*!
 *  @brief	Compares two strings up to u32Count characters
 *  @details	Compares 2 strings reporting which is bigger, WILC_NULL is considered
 *              the smallest string, then a zero length string then all other
 *              strings depending on thier ascii characters order with small case
 *              converted to uppder case
 *  @param[in]	pcStr1 the first string, WILC_NULL is valid and considered smaller
 *              than any other non-NULL string (incliding zero lenght strings)
 *  @param[in]	pcStr2 the second string, WILC_NULL is valid and considered smaller
 *              than any other non-NULL string (incliding zero lenght strings)
 *  @param[in]	u32Count copying will proceed until a null character in pcStr1 or
 *              pcStr2 is encountered or u32Count of bytes copied
 *  @return	0 if the 2 strings are equal, 1 if pcStr1 is bigger than pcStr2,
 *              -1 if pcStr1 smaller than pcStr2
 *  @author	aabozaeid
 *  @date	7 Dec 2010
 *  @version	1.0
 */
WILC_Sint32 WILC_strncmp(const WILC_Char *pcStr1, const WILC_Char *pcStr2,
			 WILC_Uint32 u32Count);

/*!
 *  @brief	Compares two strings ignoring the case of its latin letters
 *  @details	Compares 2 strings reporting which is bigger, WILC_NULL is considered
 *              the smallest string, then a zero length string then all other
 *              strings depending on thier ascii characters order with small case
 *              converted to uppder case
 *  @param[in]	pcStr1 the first string, WILC_NULL is valid and considered smaller
 *              than any other non-NULL string (incliding zero lenght strings)
 *  @param[in]	pcStr2 the second string, WILC_NULL is valid and considered smaller
 *              than any other non-NULL string (incliding zero lenght strings)
 *  @return	0 if the 2 strings are equal, 1 if pcStr1 is bigger than pcStr2,
 *              -1 if pcStr1 smaller than pcStr2
 *  @author	syounan
 *  @date	1 Nov 2010
 *  @version	2.0
 */
WILC_Sint32 WILC_strcmp_IgnoreCase(const WILC_Char *pcStr1, const WILC_Char *pcStr2);

/*!
 *  @brief	Compares two strings ignoring the case of its latin letters up to
 *		u32Count characters
 *  @details	Compares 2 strings reporting which is bigger, WILC_NULL is considered
 *              the smallest string, then a zero length string then all other
 *              strings depending on thier ascii characters order with small case
 *              converted to uppder case
 *  @param[in]	pcStr1 the first string, WILC_NULL is valid and considered smaller
 *              than any other non-NULL string (incliding zero lenght strings)
 *  @param[in]	pcStr2 the second string, WILC_NULL is valid and considered smaller
 *              than any other non-NULL string (incliding zero lenght strings)
 *  @param[in]	u32Count copying will proceed until a null character in pcStr1 or
 *              pcStr2 is encountered or u32Count of bytes copied
 *  @return	0 if the 2 strings are equal, 1 if pcStr1 is bigger than pcStr2,
 *              -1 if pcStr1 smaller than pcStr2
 *  @author	aabozaeid
 *  @date	7 Dec 2010
 *  @version	1.0
 */
WILC_Sint32 WILC_strncmp_IgnoreCase(const WILC_Char *pcStr1, const WILC_Char *pcStr2,
				    WILC_Uint32 u32Count);

/*!
 *  @brief	gets the length of a string
 *  @param[in]	pcStr the string
 *  @return	the length
 *  @note	this function repeats the functionality of standard strlen
 *  @author	syounan
 *  @date	18 Aug 2010
 *  @version	1.0
 */
WILC_Uint32 WILC_strlen(const WILC_Char *pcStr);

/*!
 *  @brief	convert string to integer
 *  @param[in]	pcStr the string
 *  @return	the value of string
 *  @note	this function repeats the functionality of the libc atoi
 *  @author	bfahmy
 *  @date	28 Aug 2010
 *  @version	1.0
 */
WILC_Sint32 WILC_strtoint(const WILC_Char *pcStr);

/*!
 *  @brief	print a formatted string into a buffer
 *  @param[in]	pcTarget the buffer where the resulting string is written
 *  @param[in]	u32Size size of the output beffer including the \0 terminating
 *              character
 *  @param[in]	pcFormat format of the string
 *  @return	number of character written or would have been written if the
 *              string were not truncated
 *  @note	this function repeats the functionality of standard snprintf
 *  @author	syounan
 *  @date	1 Nov 2010
 *  @version	2.0
 */
WILC_Sint32 WILC_snprintf(WILC_Char *pcTarget, WILC_Uint32 u32Size,
			  const WILC_Char *pcFormat, ...);


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
WILC_Char *WILC_memchr(const void *str, WILC_Char c, WILC_Sint32 n);

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
WILC_Char *WILC_strchr(const WILC_Char *str, WILC_Char c);

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
WILC_Char *WILC_strcat(WILC_Char *str1, const WILC_Char *str2);


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
WILC_Char *WILC_strcpy(WILC_Char *pcTarget, const WILC_Char *pcSource);



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
WILC_Uint32 WILC_strcspn(const WILC_Char *str1, const WILC_Char *str2);


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
WILC_Char *WILC_strerror(WILC_Sint32 errnum);

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
WILC_Char *WILC_strstr(const WILC_Char *str1, const WILC_Char *str2);

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
WILC_Char *WILC_strchr(const WILC_Char *str, WILC_Char c);


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
WILC_Double WILC_StringToDouble(const WILC_Char *str,
				WILC_Char **endptr);


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
WILC_Uint32 WILC_StringToUint32(const WILC_Char *str,
				WILC_Char **endptr,
				WILC_Sint32 base);



#endif

#endif
