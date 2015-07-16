
#include "wilc_timer.h"

WILC_ErrNo WILC_TimerCreate(WILC_TimerHandle *pHandle,
	tpfWILC_TimerFunction pfCallback, tstrWILC_TimerAttrs *pstrAttrs)
{
	WILC_ErrNo s32RetStatus = WILC_SUCCESS;
	setup_timer(pHandle, (void(*)(unsigned long))pfCallback, 0);

	return s32RetStatus;
}

WILC_ErrNo WILC_TimerDestroy(WILC_TimerHandle *pHandle,
	tstrWILC_TimerAttrs *pstrAttrs)
{
	WILC_ErrNo s32RetStatus = WILC_FAIL;
	if (pHandle != NULL) {
		s32RetStatus = del_timer_sync(pHandle);
		pHandle = NULL;
	}

	return s32RetStatus;
}


WILC_ErrNo WILC_TimerStart(WILC_TimerHandle *pHandle, u32 u32Timeout,
	void *pvArg, tstrWILC_TimerAttrs *pstrAttrs)
{
	WILC_ErrNo s32RetStatus = WILC_FAIL;
	if (pHandle != NULL) {
		pHandle->data = (unsigned long)pvArg;
		s32RetStatus = mod_timer(pHandle, (jiffies + msecs_to_jiffies(u32Timeout)));
	}
	return s32RetStatus;
}

WILC_ErrNo WILC_TimerStop(WILC_TimerHandle *pHandle,
	tstrWILC_TimerAttrs *pstrAttrs)
{
	WILC_ErrNo s32RetStatus = WILC_FAIL;
	if (pHandle != NULL)
		s32RetStatus = del_timer(pHandle);

	return s32RetStatus;
}
