
#include "wilc_oswrapper.h"
#ifdef CONFIG_WILC_SEMAPHORE_FEATURE


WILC_ErrNo WILC_SemaphoreCreate(WILC_SemaphoreHandle *pHandle,
				tstrWILC_SemaphoreAttrs *pstrAttrs)
{
	tstrWILC_SemaphoreAttrs strDefaultAttrs;
	if (pstrAttrs == WILC_NULL) {
		WILC_SemaphoreFillDefault(&strDefaultAttrs);
		pstrAttrs = &strDefaultAttrs;
	}

	sema_init(pHandle, pstrAttrs->u32InitCount);
	return WILC_SUCCESS;

}


WILC_ErrNo WILC_SemaphoreDestroy(WILC_SemaphoreHandle *pHandle,
				 tstrWILC_SemaphoreAttrs *pstrAttrs)
{
	/* nothing to be done ! */

	return WILC_SUCCESS;

}


WILC_ErrNo WILC_SemaphoreAcquire(WILC_SemaphoreHandle *pHandle,
				 tstrWILC_SemaphoreAttrs *pstrAttrs)
{
	WILC_ErrNo s32RetStatus = WILC_SUCCESS;

	#ifndef CONFIG_WILC_SEMAPHORE_TIMEOUT
	while (down_interruptible(pHandle))
		;

	#else
	if (pstrAttrs == WILC_NULL) {
		down(pHandle);
	} else {

		s32RetStatus = down_timeout(pHandle, msecs_to_jiffies(pstrAttrs->u32TimeOut));
	}
	#endif

	if (s32RetStatus == 0) {
		return WILC_SUCCESS;
	} else if (s32RetStatus == -ETIME)   {
		return WILC_TIMEOUT;
	} else {
		return WILC_FAIL;
	}

	return WILC_SUCCESS;

}

WILC_ErrNo WILC_SemaphoreRelease(WILC_SemaphoreHandle *pHandle,
				 tstrWILC_SemaphoreAttrs *pstrAttrs)
{

	up(pHandle);
	return WILC_SUCCESS;

}

#endif
