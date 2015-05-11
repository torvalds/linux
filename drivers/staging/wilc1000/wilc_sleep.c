
#include "wilc_oswrapper.h"

#ifdef CONFIG_WILC_SLEEP_FEATURE

/*
 *  @author	mdaftedar
 *  @date	10 Aug 2010
 *  @version	1.0
 */
void WILC_Sleep(WILC_Uint32 u32TimeMilliSec)
{
	if (u32TimeMilliSec <= 4000000)	{
		WILC_Uint32 u32Temp = u32TimeMilliSec * 1000;
#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35)
		usleep_range(u32Temp, u32Temp);
#else
		udelay(u32Temp);
#endif
	} else {
		msleep(u32TimeMilliSec);
	}

}
#endif

/* #ifdef CONFIG_WILC_SLEEP_HI_RES */
void WILC_SleepMicrosec(WILC_Uint32 u32TimeMicoSec)
{
	#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 35)
	usleep_range(u32TimeMicoSec, u32TimeMicoSec);
	#else
	udelay(u32TimeMicoSec);
	#endif
}
/* #endif */
