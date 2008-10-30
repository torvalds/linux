#include "os_common.h"
#include "adapter.h"

static void RxTimerHandler(unsigned long data)
{
	WARN_ON(1);
}

void vRxTimerInit(struct wb35_adapter *adapter)
{
	init_timer(&adapter->Mds.timer);
	adapter->Mds.timer.function = RxTimerHandler;
	adapter->Mds.timer.data = (unsigned long) adapter;
}

void vRxTimerStart(struct wb35_adapter *adapter, int timeout_value)
{
	if (timeout_value < MIN_TIMEOUT_VAL)
		timeout_value = MIN_TIMEOUT_VAL;

	adapter->Mds.timer.expires = jiffies + msecs_to_jiffies(timeout_value);
	add_timer(&adapter->Mds.timer);
}

void vRxTimerStop(struct wb35_adapter *adapter)
{
	del_timer_sync(&adapter->Mds.timer);
}
