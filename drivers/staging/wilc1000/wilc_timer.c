
#include "wilc_timer.h"

WILC_ErrNo WILC_TimerCreate(struct timer_list *pHandle,
	tpfWILC_TimerFunction pfCallback)
{
	WILC_ErrNo s32RetStatus = WILC_SUCCESS;
	setup_timer(pHandle, (void(*)(unsigned long))pfCallback, 0);

	return s32RetStatus;
}

WILC_ErrNo WILC_TimerStart(struct timer_list *pHandle, u32 u32Timeout,
	void *pvArg)
{
	WILC_ErrNo s32RetStatus = WILC_FAIL;
	if (pHandle != NULL) {
		pHandle->data = (unsigned long)pvArg;
		s32RetStatus = mod_timer(pHandle, (jiffies + msecs_to_jiffies(u32Timeout)));
	}
	return s32RetStatus;
}
