#ifndef __WILC_SEMAPHORE_H__
#define __WILC_SEMAPHORE_H__

/*!
 *  @file		wilc_semaphore.h
 *  @brief		Semaphore OS Wrapper functionality
 *  @author		syounan
 *  @sa			wilc_oswrapper.h top level OS wrapper file
 *  @date		10 Aug 2010
 *  @version		1.0
 */


#ifndef CONFIG_WILC_SEMAPHORE_FEATURE
#error the feature WILC_OS_FEATURE_SEMAPHORE must be supported to include this file
#endif

/*!
 *  @struct             WILC_SemaphoreAttrs
 *  @brief		Semaphore API options
 *  @author		syounan
 *  @date		10 Aug 2010
 *  @version		1.0
 */
typedef struct {
	/*!<
	 * Initial count when the semaphore is created. default is 1
	 */
	WILC_Uint32 u32InitCount;

	#ifdef CONFIG_WILC_SEMAPHORE_TIMEOUT
	/*!<
	 * Timeout for use with WILC_SemaphoreAcquire, 0 to return immediately and
	 * WILC_OS_INFINITY to wait forever. default is WILC_OS_INFINITY
	 */
	WILC_Uint32 u32TimeOut;
	#endif

} tstrWILC_SemaphoreAttrs;


/*!
 *  @brief	Fills the WILC_SemaphoreAttrs with default parameters
 *  @param[out]	pstrAttrs structure to be filled
 *  @sa		WILC_SemaphoreAttrs
 *  @author	syounan
 *  @date	10 Aug 2010
 *  @version	1.0
 */
static void WILC_SemaphoreFillDefault(tstrWILC_SemaphoreAttrs *pstrAttrs)
{
	pstrAttrs->u32InitCount = 1;
	#ifdef CONFIG_WILC_SEMAPHORE_TIMEOUT
	pstrAttrs->u32TimeOut = WILC_OS_INFINITY;
	#endif
}
/*!
 *  @brief	Creates a new Semaphore object
 *  @param[out]	pHandle handle to the newly created semaphore
 *  @param[in]	pstrAttrs Optional attributes, NULL for defaults
 *                              pstrAttrs->u32InitCount controls the initial count
 *  @return	Error code indicating success/failure
 *  @sa		WILC_SemaphoreAttrs
 *  @author	syounan
 *  @date	10 Aug 2010
 *  @version	1.0
 */
WILC_ErrNo WILC_SemaphoreCreate(WILC_SemaphoreHandle *pHandle,
				tstrWILC_SemaphoreAttrs *pstrAttrs);

/*!
 *  @brief	Destroyes an existing Semaphore, releasing any resources
 *  @param[in]	pHandle handle to the semaphore object
 *  @param[in]	pstrAttrs Optional attributes, NULL for defaults
 *  @return	Error code indicating success/failure
 *  @sa		WILC_SemaphoreAttrs
 *  @todo	need to define behaviour if the semaphore delayed while it is pending
 *  @author	syounan
 *  @date	10 Aug 2010
 *  @version	1.0
 */
WILC_ErrNo WILC_SemaphoreDestroy(WILC_SemaphoreHandle *pHandle,
				 tstrWILC_SemaphoreAttrs *pstrAttrs);

/*!
 *  @brief	Acquire the Semaphore object
 *  @details	This function will block until it can Acquire the given
 *		semaphore, if the feature WILC_OS_FEATURE_SEMAPHORE_TIMEOUT is
 *		eanbled a timeout value can be passed in pstrAttrs
 *  @param[in]	pHandle handle to the semaphore object
 *  @param[in]	pstrAttrs Optional attributes, NULL for default
 *  @return	Error code indicating success/failure
 *  @sa		WILC_SemaphoreAttrs
 *  @author	syounan
 *  @date	10 Aug 2010
 *  @version	1.0
 */
WILC_ErrNo WILC_SemaphoreAcquire(WILC_SemaphoreHandle *pHandle,
				 tstrWILC_SemaphoreAttrs *pstrAttrs);

/*!
 *  @brief	Release the Semaphore object
 *  @param[in]	pHandle handle to the semaphore object
 *  @param[in]	pstrAttrs Optional attributes, NULL for default
 *  @return	Error code indicating sucess/failure
 *  @sa		WILC_SemaphoreAttrs
 *  @author	syounan
 *  @date	10 Aug 2010
 *  @version	1.0
 */
WILC_ErrNo WILC_SemaphoreRelease(WILC_SemaphoreHandle *pHandle,
				 tstrWILC_SemaphoreAttrs *pstrAttrs);


#endif
