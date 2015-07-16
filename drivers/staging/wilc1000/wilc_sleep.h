#ifndef __WILC_SLEEP_H__
#define __WILC_SLEEP_H__

#include <linux/types.h>
#include <linux/delay.h>

/*!
 *  @brief	forces the current thread to sleep until the given time has elapsed
 *  @param[in]	u32TimeMilliSec Time to sleep in Milli seconds
 *  @sa		WILC_SleepMicrosec
 *  @author	syounan
 *  @date	10 Aug 2010
 *  @version	1.0
 *  @note	This function offers a relatively innacurate and low resolution
 *              sleep, for accurate high resolution sleep use u32TimeMicoSec
 */
/* TODO: remove and open-code in callers */
void WILC_Sleep(u32 u32TimeMilliSec);

#endif
