
#include "wilc_oswrapper.h"

#ifdef CONFIG_WILC_THREAD_FEATURE



WILC_ErrNo WILC_ThreadCreate(WILC_ThreadHandle *pHandle, tpfWILC_ThreadFunction pfEntry,
			     void *pvArg, tstrWILC_ThreadAttrs *pstrAttrs)
{


	*pHandle = kthread_run((int (*)(void *))pfEntry, pvArg, "WILC_kthread");


	if (IS_ERR(*pHandle)) {
		return WILC_FAIL;
	} else {
		return WILC_SUCCESS;
	}

}

WILC_ErrNo WILC_ThreadDestroy(WILC_ThreadHandle *pHandle,
			      tstrWILC_ThreadAttrs *pstrAttrs)
{
	WILC_ErrNo s32RetStatus = WILC_SUCCESS;

	kthread_stop(*pHandle);
	return s32RetStatus;
}



#endif
