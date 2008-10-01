#include "os_common.h"

void vRxTimerInit(PWB32_ADAPTER Adapter)
{
	OS_TIMER_INITIAL(&(Adapter->Mds.nTimer), (void*) RxTimerHandler, (void*) Adapter);
}

void vRxTimerStart(PWB32_ADAPTER Adapter, int timeout_value)
{
	if (timeout_value<MIN_TIMEOUT_VAL)
		timeout_value=MIN_TIMEOUT_VAL;

	OS_TIMER_SET( &(Adapter->Mds.nTimer), timeout_value );
}

void vRxTimerStop(PWB32_ADAPTER Adapter)
{
	OS_TIMER_CANCEL( &(Adapter->Mds.nTimer), 0 );
}

void RxTimerHandler_1a( PADAPTER Adapter)
{
	RxTimerHandler(NULL, Adapter, NULL, NULL);
}

void RxTimerHandler(void* SystemSpecific1, PWB32_ADAPTER Adapter,
		    void* SystemSpecific2, void* SystemSpecific3)
{
	WARN_ON(1);
}
