#ifndef __WILC_EVENT_H__
#define __WILC_EVENT_H__

/*!
 *  @file	wilc_event.h
 *  @brief	Event OS wrapper functionality
 *  @author	syounan
 *  @sa		wilc_oswrapper.h top level OS wrapper file
 *  @date	10 Oct 2010
 *  @version	1.0
 */

#ifndef CONFIG_WILC_EVENT_FEATURE
#error the feature CONFIG_WILC_EVENT_FEATURE must be supported to include this file
#endif


/*!
 *  @struct             tstrWILC_TimerAttrs
 *  @brief		Timer API options
 *  @author		syounan
 *  @date		10 Oct 2010
 *  @version		1.0
 */
typedef struct {
	/* a dummy member to avoid compiler errors*/
	WILC_Uint8 dummy;

	#ifdef CONFIG_WILC_EVENT_TIMEOUT
	/*!<
	 * Timeout for use with WILC_EventWait, 0 to return immediately and
	 * WILC_OS_INFINITY to wait forever. default is WILC_OS_INFINITY
	 */
	WILC_Uint32 u32TimeOut;
	#endif

} tstrWILC_EventAttrs;

/*!
 *  @brief	Fills the WILC_TimerAttrs with default parameters
 *  @param[out]	pstrAttrs structure to be filled
 *  @sa		WILC_TimerAttrs
 *  @author	syounan
 *  @date	10 Oct 2010
 *  @version	1.0
 */
static void WILC_EventFillDefault(tstrWILC_EventAttrs *pstrAttrs)
{
	#ifdef CONFIG_WILC_EVENT_TIMEOUT
	pstrAttrs->u32TimeOut = WILC_OS_INFINITY;
	#endif
}

/*!
 *  @brief	Creates a new Event
 *  @details	the Event is an object that allows a thread to wait until an external
 *                      event occuers, Event objects have 2 states, either TRIGGERED or
 *                      UNTRIGGERED
 *  @param[out]	pHandle handle to the newly created event object
 *  @param[in]	pstrAttrs Optional attributes, NULL for default
 *  @return	Error code indicating sucess/failure
 *  @sa		tstrWILC_EventAttrs
 *  @author	syounan
 *  @date	10 Oct 2010
 *  @version	1.0
 */
WILC_ErrNo WILC_EventCreate(WILC_EventHandle *pHandle, tstrWILC_EventAttrs *pstrAttrs);


/*!
 *  @brief	Destroys a given event
 *  @details	This will destroy a given event freeing any resources used by it
 *              if there are any thread blocked by the WILC_EventWait call the the
 *              behaviour is undefined
 *  @param[in]	pHandle handle to the event object
 *  @param[in]	pstrAttrs Optional attributes, NULL for default
 *  @return	Error code indicating sucess/failure
 *  @sa		tstrWILC_EventAttrs
 *  @author	syounan
 *  @date	10 Oct 2010
 *  @version	1.0
 */
WILC_ErrNo WILC_EventDestroy(WILC_EventHandle *pHandle,
			     tstrWILC_EventAttrs *pstrAttrs);

/*!
 *  @brief	Triggers a given event
 *  @details	This function will set the given event into the TRIGGERED state,
 *                      if the event is already in TRIGGERED, this function will have no
 *                      effect
 *  @param[in]	pHandle handle to the event object
 *  @param[in]	pstrAttrs Optional attributes, NULL for default
 *  @return	Error code indicating sucess/failure
 *  @sa		tstrWILC_EventAttrs
 *  @author	syounan
 *  @date	10 Oct 2010
 *  @version	1.0
 */
WILC_ErrNo WILC_EventTrigger(WILC_EventHandle *pHandle,
			     tstrWILC_EventAttrs *pstrAttrs);


/*!
 *  @brief	waits until a given event is triggered
 *  @details	This function will block the calling thread until the event becomes
 *                      in the TRIGGERED state. the call will retun the event into the
 *                      UNTRIGGERED	state upon completion
 *                      if multible threads are waiting on the same event at the same time,
 *                      behaviour is undefined
 *  @param[in]	pHandle handle to the event object
 *  @param[in]	pstrAttrs Optional attributes, NULL for default
 *  @return	Error code indicating sucess/failure
 *  @sa		tstrWILC_EventAttrs
 *  @author	syounan
 *  @date	10 Oct 2010
 *  @version	1.0
 */
WILC_ErrNo WILC_EventWait(WILC_EventHandle *pHandle,
			  tstrWILC_EventAttrs *pstrAttrs);



#endif