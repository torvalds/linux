#ifndef __WILC_THREAD_H__
#define __WILC_THREAD_H__

/*!
 *  @file		wilc_thread.h
 *  @brief		Thread OS Wrapper functionality
 *  @author		syounan
 *  @sa			wilc_oswrapper.h top level OS wrapper file
 *  @date		10 Aug 2010
 *  @version		1.0
 */

#ifndef CONFIG_WILC_THREAD_FEATURE
#error the feature WILC_OS_FEATURE_THREAD must be supported to include this file
#endif

typedef void (*tpfWILC_ThreadFunction)(void *);

typedef enum {
	#ifdef CONFIG_WILC_THREAD_STRICT_PRIORITY
	WILC_OS_THREAD_PIORITY_0 = 0,
	WILC_OS_THREAD_PIORITY_1 = 1,
	WILC_OS_THREAD_PIORITY_2 = 2,
	WILC_OS_THREAD_PIORITY_3 = 3,
	WILC_OS_THREAD_PIORITY_4 = 4,
	#endif

	WILC_OS_THREAD_PIORITY_HIGH = 0,
	WILC_OS_THREAD_PIORITY_NORMAL = 2,
	WILC_OS_THREAD_PIORITY_LOW = 4
} tenuWILC_ThreadPiority;

/*!
 *  @struct             WILC_ThreadAttrs
 *  @brief		Thread API options
 *  @author		syounan
 *  @date		10 Aug 2010
 *  @version		1.0
 */
typedef struct {
	/*!<
	 * stack size for use with WILC_ThreadCreate, default is WILC_OS_THREAD_DEFAULT_STACK
	 */
	WILC_Uint32 u32StackSize;

	/*!<
	 * piority for the thread, if WILC_OS_FEATURE_THREAD_STRICT_PIORITY is defined
	 * this value is strictly observed and can take a larger resolution
	 */
	tenuWILC_ThreadPiority enuPiority;

	#ifdef CONFIG_WILC_THREAD_SUSPEND_CONTROL
	/*!
	 * if true the thread will be created suspended
	 */
	WILC_Bool bStartSuspended;
	#endif

} tstrWILC_ThreadAttrs;

#define WILC_OS_THREAD_DEFAULT_STACK (10 * 1024)

/*!
 *  @brief	Fills the WILC_ThreadAttrs with default parameters
 *  @param[out]	pstrAttrs structure to be filled
 *  @sa		WILC_ThreadAttrs
 *  @author	syounan
 *  @date	10 Aug 2010
 *  @version	1.0
 */

static void WILC_ThreadFillDefault(tstrWILC_ThreadAttrs *pstrAttrs)
{
	pstrAttrs->u32StackSize = WILC_OS_THREAD_DEFAULT_STACK;
	pstrAttrs->enuPiority = WILC_OS_THREAD_PIORITY_NORMAL;

	#ifdef CONFIG_WILC_THREAD_SUSPEND_CONTROL
	pstrAttrs->bStartSuspended = WILC_FALSE;
	#endif
}

/*!
 *  @brief	Creates a new thread
 *  @details	if the feature WILC_OS_FEATURE_THREAD_SUSPEND_CONTROL is
 *              defined and tstrWILC_ThreadAttrs.bStartSuspended is set to true
 *              the new thread will be created in suspended state, otherwise
 *              it will start executing immeadiately
 *              if the feature WILC_OS_FEATURE_THREAD_STRICT_PIORITY is defined
 *              piorities are strictly observed, otherwise the underlaying OS
 *              may not observe piorities
 *  @param[out]	pHandle handle to the newly created thread object
 *  @param[in]	pfEntry pointer to the entry point of the new thread
 *  @param[in]	pstrAttrs Optional attributes, NULL for default
 *  @return	Error code indicating sucess/failure
 *  @sa		WILC_ThreadAttrs
 *  @author	syounan
 *  @date	10 Aug 2010
 *  @version	1.0
 */
WILC_ErrNo WILC_ThreadCreate(WILC_ThreadHandle *pHandle, tpfWILC_ThreadFunction pfEntry,
			     void *pvArg, tstrWILC_ThreadAttrs *pstrAttrs);

/*!
 *  @brief	Destroys the Thread object
 *  @details	This function is used for clean up and freeing any used resources
 *		This function will block until the destroyed thread exits cleanely,
 *		so, the thread code thould handle an exit case before this calling
 *		this function
 *  @param[in]	pHandle handle to the thread object
 *  @param[in]	pstrAttrs Optional attributes, NULL for default
 *  @return	Error code indicating sucess/failure
 *  @sa		WILC_ThreadAttrs
 *  @author	syounan
 *  @date	10 Aug 2010
 *  @version	1.0
 */
WILC_ErrNo WILC_ThreadDestroy(WILC_ThreadHandle *pHandle,
			      tstrWILC_ThreadAttrs *pstrAttrs);

#ifdef CONFIG_WILC_THREAD_SUSPEND_CONTROL

/*!
 *  @brief	Suspends an executing Thread object
 *  @param[in]	pHandle handle to the thread object
 *  @param[in]	pstrAttrs Optional attributes, NULL for default
 *  @return	Error code indicating sucess/failure
 *  @sa		WILC_ThreadAttrs
 *  @note	Optional part, WILC_OS_FEATURE_THREAD_SUSPEND_CONTROL must be enabled
 *  @author	syounan
 *  @date	10 Aug 2010
 *  @version	1.0
 */
WILC_ErrNo WILC_ThreadSuspend(WILC_ThreadHandle *pHandle,
			      tstrWILC_ThreadAttrs *pstrAttrs);

/*!
 *  @brief	Resumes a suspened Thread object
 *  @param[in]	pHandle handle to the thread object
 *  @param[in]	pstrAttrs Optional attributes, NULL for default
 *  @return	Error code indicating sucess/failure
 *  @sa		WILC_ThreadAttrs
 *  @note	Optional part, WILC_OS_FEATURE_THREAD_SUSPEND_CONTROL must be enabled
 *  @author	syounan
 *  @date	10 Aug 2010
 *  @version	1.0
 */
WILC_ErrNo WILC_ThreadResume(WILC_ThreadHandle *pHandle,
			     tstrWILC_ThreadAttrs *pstrAttrs);

#endif


#endif
