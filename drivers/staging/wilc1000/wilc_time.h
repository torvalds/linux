#ifndef __WILC_TIME_H__
#define __WILC_TIME_H__

/*!
*  @file		wilc_time.h
*  @brief		Time retrival functionality
*  @author		syounan
*  @sa			wilc_oswrapper.h top level OS wrapper file
*  @date		2 Sep 2010
*  @version		1.0
*/

#ifndef CONFIG_WILC_TIME_FEATURE
#error the feature CONFIG_WILC_TIME_FEATURE must be supported to include this file
#endif

/*!
*  @struct 		WILC_ThreadAttrs
*  @brief		Thread API options
*  @author		syounan
*  @date		2 Sep 2010
*  @version		1.0
*/
typedef struct {
	/* a dummy type to prevent compile errors on empty structure*/
	WILC_Uint8 dummy;
} tstrWILC_TimeAttrs;

typedef struct {
	/*!< current year */
	WILC_Uint16	u16Year;
	/*!< current month */
	WILC_Uint8	u8Month;
	/*!< current day */
	WILC_Uint8	u8Day;

	/*!< current hour (in 24H format) */
	WILC_Uint8	u8Hour;
	/*!< current minute */
	WILC_Uint8	u8Miute;
	/*!< current second */
	WILC_Uint8	u8Second;

} tstrWILC_TimeCalender;

/*!
*  @brief		returns the number of msec elapsed since system start up
*  @return		number of msec elapsed singe system start up
*  @note		since this returned value is 32 bit, the caller must handle
				wraparounds in values every about 49 of continous operations
*  @author		syounan
*  @date		2 Sep 2010
*  @version		1.0
*/
WILC_Uint32 WILC_TimeMsec(void);



#ifdef CONFIG_WILC_EXTENDED_TIME_OPERATIONS
/**
*  @brief
*  @details 	function returns the implementation's best approximation to the
				processor time used by the process since the beginning of an
				implementation-dependent time related only to the process invocation.
*  @return 		WILC_Uint32
*  @note
*  @author		remil
*  @date		11 Nov 2010
*  @version		1.0
*/
WILC_Uint32 WILC_Clock();

/**
*  @brief
*  @details 	The difftime() function computes the difference between two calendar
				times (as returned by WILC_GetTime()): time1 - time0.
*  @param[in] 	WILC_Time time1
*  @param[in] 	WILC_Time time0
*  @return 		WILC_Double
*  @note
*  @author		remil
*  @date		11 Nov 2010
*  @version		1.0
*/
WILC_Double WILC_DiffTime(WILC_Time time1, WILC_Time time0);

/**
*  @brief
*  @details 	The gmtime() function converts the time in seconds since
				the Epoch pointed to by timer into a broken-down time,
				expressed as Coordinated Universal Time (UTC).
*  @param[in] 	const WILC_Time* timer
*  @return 		WILC_tm*
*  @note
*  @author		remil
*  @date		11 Nov 2010
*  @version		1.0
*/
WILC_tm *WILC_GmTime(const WILC_Time *timer);


/**
*  @brief
*  @details 	The localtime() function converts the time in seconds since
				the Epoch pointed to by timer into a broken-down time, expressed
				as a local time. The function corrects for the timezone and any
				seasonal time adjustments. Local timezone information is used as
				though localtime() calls tzset().
*  @param[in] 	const WILC_Time* timer
*  @return 		WILC_tm*
*  @note
*  @author		remil
*  @date		11 Nov 2010
*  @version		1.0
*/
WILC_tm *WILC_LocalTime(const WILC_Time *timer);


/**
*  @brief
*  @details 	The mktime() function converts the broken-down time,
				expressed as local time, in the structure pointed to by timeptr,
				into a time since the Epoch value with the same encoding as that
				of the values returned by time(). The original values of the tm_wday
				and tm_yday components of the structure are ignored, and the original
				values of the other components are not restricted to the ranges described
				in the <time.h> entry.
*  @param[in] 	WILC_tm* timer
*  @return 		WILC_Time
*  @note
*  @author		remil
*  @date		11 Nov 2010
*  @version		1.0
*/
WILC_Time WILC_MkTime(WILC_tm *timer);


/**
*  @brief
*  @details 	The strftime() function places bytes into the array
				pointed to by s as controlled by the string pointed to by format.
*  @param[in] 	WILC_Char* s
*  @param[in]	WILC_Uint32 maxSize
*  @param[in]	const WILC_Char* format
*  @param[in]	const WILC_tm* timptr
*  @return 		WILC_Uint32
*  @note
*  @author		remil
*  @date		11 Nov 2010
*  @version		1.0
*/
WILC_Uint32 WILC_StringFormatTime(WILC_Char *s,
								WILC_Uint32 maxSize,
								const WILC_Char *format,
								const WILC_tm *timptr);


/**
*  @brief 		The WILC_GetTime() function returns the value of time in seconds since the Epoch.
*  @details 	The tloc argument points to an area where the return value is also stored.
				If tloc is a null pointer, no value is stored.
*  @param[in] 	WILC_Time* tloc
*  @return 		WILC_Time
*  @note
*  @author		remil
*  @date		11 Nov 2010
*  @version		1.0
*/
WILC_Time WILC_GetTime(WILC_Time *tloc);

#endif

#ifdef CONFIG_WILC_TIME_UTC_SINCE_1970

/*!
*  @brief		returns the number of seconds elapsed since 1970 (in UTC)
*  @param[in]	pstrAttrs Optional attributes, NULL for default
*  @return		number of seconds elapsed since 1970 (in UTC)
*  @sa			tstrWILC_TimeAttrs
*  @author		syounan
*  @date		2 Sep 2010
*  @version		1.0
*/
WILC_Uint32 WILC_TimeUtcSince1970(tstrWILC_TimeAttrs *pstrAttrs);

#endif

#ifdef CONFIG_WILC_TIME_CALENDER

/*!
*  @brief		gets the current calender time
*  @return		number of seconds elapsed since 1970 (in UTC)
*  @param[out]	ptstrCalender calender structure to be filled with time
*  @param[in]	pstrAttrs Optional attributes, NULL for default
*  @sa			WILC_ThreadAttrs
*  @author		syounan
*  @date		2 Sep 2010
*  @version		1.0
*/
WILC_ErrNo WILC_TimeCalender(tstrWILC_TimeCalender *ptstrCalender,
	tstrWILC_TimeAttrs *pstrAttrs);

#endif

#endif
