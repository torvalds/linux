/**
 * SiIxxxx <Firmware or Driver>
 *
 * Copyright (C) 2011 Silicon Image Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed .as is. WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
 * PURPOSE.  See the GNU General Public License for more details.
*/


/**
 * @file sii_hal_linux_timer.c
 *
 * @brief Linux implementation of timer support used by Silicon Image
 *        MHL devices.
 *
 * $Author: Dave Canfield
 * $Rev: $
 * $Date: Feb. 4, 2011
 */

/***** #include statements ***************************************************/
#include "si_common.h"
#include "sii_hal_priv.h"
#include <linux/jiffies.h>

uint32_t g_timerCounters[ TIMER_COUNT ];

/*****************************************************************************/
/**
 * @brief Wait for the specified number of milliseconds to elapse.
 *
 *****************************************************************************/
void HalTimerWait(uint16_t m_sec)
{
	unsigned long	time_usec = m_sec * 1000;

	usleep_range(time_usec, time_usec);
}

void    HalTimerInit( void )
{
    uint8_t i;

    //initializer timer counters in array

    for ( i = 0; i < TIMER_COUNT; i++ )
    {
        g_timerCounters[i] = 0;
    }
}
void    HalTimerSet( uint8_t index, uint16_t m_sec )
{
    switch ( index )
    {
        case TIMER_0:
        case TIMER_1:
        case TIMER_2:
        case TIMER_3:
            g_timerCounters[index] = jiffies + m_sec * HZ / 1000;
            break;
    }
}

#if 0
uint8_t HalTimerExpired( uint8_t timer )
{
    if ( timer < TIMER_COUNT )
    {
        return time_after(jiffies, g_timerCounters[timer]);
    }
    return false;
}
uint16_t HalTimerElapsed( uint8_t timer )
{
    //Not implemented since no user use this function
    return 0;
}
uint32_t HalTimerSysTicks (void)
{
    return (get_jiffies_64()*1000/ HZ);
}
#endif
