
#define _CRT_SECURE_NO_DEPRECATE
#include "wilc_oswrapper.h"

#ifdef CONFIG_WILC_TIME_FEATURE


WILC_Uint32 WILC_TimeMsec(void)
{
	WILC_Uint32 u32Time = 0;
	struct timespec current_time;

	current_time = current_kernel_time();
	u32Time = current_time.tv_sec * 1000;
	u32Time += current_time.tv_nsec / 1000000;


	return u32Time;
}


#ifdef CONFIG_WILC_EXTENDED_TIME_OPERATIONS

/**
 *  @brief
 *  @details    function returns the implementation's best approximation to the
 *                              processor time used by the process since the beginning of an
 *                              implementation-dependent time related only to the process invocation.
 *  @return             WILC_Uint32
 *  @note
 *  @author		remil
 *  @date		11 Nov 2010
 *  @version		1.0
 */
WILC_Uint32 WILC_Clock()
{

}


/**
 *  @brief
 *  @details    The difftime() function computes the difference between two calendar
 *                              times (as returned by WILC_GetTime()): time1 - time0.
 *  @param[in]  WILC_Time time1
 *  @param[in]  WILC_Time time0
 *  @return             WILC_Double
 *  @note
 *  @author		remil
 *  @date		11 Nov 2010
 *  @version		1.0
 */
WILC_Double WILC_DiffTime(WILC_Time time1, WILC_Time time0)
{

}



/**
 *  @brief
 *  @details    The gmtime() function converts the time in seconds since
 *                              the Epoch pointed to by timer into a broken-down time,
 *                              expressed as Coordinated Universal Time (UTC).
 *  @param[in]  const WILC_Time* timer
 *  @return             WILC_tm*
 *  @note
 *  @author		remil
 *  @date		11 Nov 2010
 *  @version		1.0
 */
WILC_tm *WILC_GmTime(const WILC_Time *timer)
{

}


/**
 *  @brief
 *  @details    The localtime() function converts the time in seconds since
 *                              the Epoch pointed to by timer into a broken-down time, expressed
 *                              as a local time. The function corrects for the timezone and any
 *                              seasonal time adjustments. Local timezone information is used as
 *                              though localtime() calls tzset().
 *  @param[in]  const WILC_Time* timer
 *  @return             WILC_tm*
 *  @note
 *  @author		remil
 *  @date		11 Nov 2010
 *  @version		1.0
 */
WILC_tm *WILC_LocalTime(const WILC_Time *timer)
{

}


/**
 *  @brief
 *  @details    The mktime() function converts the broken-down time,
 *                              expressed as local time, in the structure pointed to by timeptr,
 *                              into a time since the Epoch value with the same encoding as that
 *                              of the values returned by time(). The original values of the tm_wday
 *                              and tm_yday components of the structure are ignored, and the original
 *                              values of the other components are not restricted to the ranges described
 *                              in the <time.h> entry.
 *  @param[in]  WILC_tm* timer
 *  @return             WILC_Time
 *  @note
 *  @author		remil
 *  @date		11 Nov 2010
 *  @version		1.0
 */
WILC_Time WILC_MkTime(WILC_tm *timer)
{

}


/**
 *  @brief
 *  @details    The strftime() function places bytes into the array
 *                              pointed to by s as controlled by the string pointed to by format.
 *  @param[in]  WILC_Char* s
 *  @param[in]	WILC_Uint32 maxSize
 *  @param[in]	const WILC_Char* format
 *  @param[in]	const WILC_tm* timptr
 *  @return             WILC_Uint32
 *  @note
 *  @author		remil
 *  @date		11 Nov 2010
 *  @version		1.0
 */
WILC_Uint32 WILC_StringFormatTime(WILC_Char *s,
				  WILC_Uint32 maxSize,
				  const WILC_Char *format,
				  const WILC_tm *timptr)
{

}


/**
 *  @brief              The WILC_GetTime() function returns the value of time in seconds since the Epoch.
 *  @details    The tloc argument points to an area where the return value is also stored.
 *                              If tloc is a null pointer, no value is stored.
 *  @param[in]  WILC_Time* tloc
 *  @return             WILC_Time
 *  @note
 *  @author		remil
 *  @date		11 Nov 2010
 *  @version		1.0
 */
WILC_Time WILC_GetTime(WILC_Time *tloc)
{

}


#endif
#endif


