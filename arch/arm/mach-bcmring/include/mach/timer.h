/*****************************************************************************
* Copyright 2004 - 2008 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

/*
*
*****************************************************************************
*
*  timer.h
*
*  PURPOSE:
*
*
*
*  NOTES:
*
*****************************************************************************/

#if !defined(BCM_LINUX_TIMER_H)
#define BCM_LINUX_TIMER_H

#if defined(__KERNEL__)

/* ---- Include Files ---------------------------------------------------- */
/* ---- Constants and Types ---------------------------------------------- */

typedef unsigned int timer_tick_count_t;
typedef unsigned int timer_tick_rate_t;
typedef unsigned int timer_msec_t;

/* ---- Variable Externs ------------------------------------------------- */
/* ---- Function Prototypes ---------------------------------------------- */

/****************************************************************************
*
*  timer_get_tick_count
*
*
***************************************************************************/
timer_tick_count_t timer_get_tick_count(void);

/****************************************************************************
*
*  timer_get_tick_rate
*
*
***************************************************************************/
timer_tick_rate_t timer_get_tick_rate(void);

/****************************************************************************
*
*  timer_get_msec
*
*
***************************************************************************/
timer_msec_t timer_get_msec(void);

/****************************************************************************
*
*  timer_ticks_to_msec
*
*
***************************************************************************/
timer_msec_t timer_ticks_to_msec(timer_tick_count_t ticks);

#endif /* __KERNEL__ */
#endif /* BCM_LINUX_TIMER_H */
