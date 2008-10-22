#include "os_common.h"

void vRxTimerInit(struct wb35_adapter * adapter)
{
	OS_TIMER_INITIAL(&(adapter->Mds.nTimer), (void*) RxTimerHandler, (void*) adapter);
}

void vRxTimerStart(struct wb35_adapter * adapter, int timeout_value)
{
	if (timeout_value<MIN_TIMEOUT_VAL)
		timeout_value=MIN_TIMEOUT_VAL;

	OS_TIMER_SET( &(adapter->Mds.nTimer), timeout_value );
}

void vRxTimerStop(struct wb35_adapter * adapter)
{
	OS_TIMER_CANCEL( &(adapter->Mds.nTimer), 0 );
}

void RxTimerHandler_1a( struct wb35_adapter * adapter)
{
	RxTimerHandler(NULL, adapter, NULL, NULL);
}

void RxTimerHandler(void* SystemSpecific1, struct wb35_adapter * adapter,
		    void* SystemSpecific2, void* SystemSpecific3)
{
	WARN_ON(1);
}
