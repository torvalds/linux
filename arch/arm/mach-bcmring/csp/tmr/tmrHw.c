/*****************************************************************************
* Copyright 2003 - 2008 Broadcom Corporation.  All rights reserved.
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

/****************************************************************************/
/**
*  @file    tmrHw.c
*
*  @brief   Low level Timer driver routines
*
*  @note
*
*   These routines provide basic timer functionality only.
*/
/****************************************************************************/

/* ---- Include Files ---------------------------------------------------- */

#include <csp/errno.h>
#include <csp/stdint.h>

#include <csp/tmrHw.h>
#include <mach/csp/tmrHw_reg.h>

#define tmrHw_ASSERT(a)                     if (!(a)) *(char *)0 = 0
#define tmrHw_MILLISEC_PER_SEC              (1000)

#define tmrHw_LOW_1_RESOLUTION_COUNT        (tmrHw_LOW_RESOLUTION_CLOCK / tmrHw_MILLISEC_PER_SEC)
#define tmrHw_LOW_1_MAX_MILLISEC            (0xFFFFFFFF / tmrHw_LOW_1_RESOLUTION_COUNT)
#define tmrHw_LOW_16_RESOLUTION_COUNT       (tmrHw_LOW_1_RESOLUTION_COUNT / 16)
#define tmrHw_LOW_16_MAX_MILLISEC           (0xFFFFFFFF / tmrHw_LOW_16_RESOLUTION_COUNT)
#define tmrHw_LOW_256_RESOLUTION_COUNT      (tmrHw_LOW_1_RESOLUTION_COUNT / 256)
#define tmrHw_LOW_256_MAX_MILLISEC          (0xFFFFFFFF / tmrHw_LOW_256_RESOLUTION_COUNT)

#define tmrHw_HIGH_1_RESOLUTION_COUNT       (tmrHw_HIGH_RESOLUTION_CLOCK / tmrHw_MILLISEC_PER_SEC)
#define tmrHw_HIGH_1_MAX_MILLISEC           (0xFFFFFFFF / tmrHw_HIGH_1_RESOLUTION_COUNT)
#define tmrHw_HIGH_16_RESOLUTION_COUNT      (tmrHw_HIGH_1_RESOLUTION_COUNT / 16)
#define tmrHw_HIGH_16_MAX_MILLISEC          (0xFFFFFFFF / tmrHw_HIGH_16_RESOLUTION_COUNT)
#define tmrHw_HIGH_256_RESOLUTION_COUNT     (tmrHw_HIGH_1_RESOLUTION_COUNT / 256)
#define tmrHw_HIGH_256_MAX_MILLISEC         (0xFFFFFFFF / tmrHw_HIGH_256_RESOLUTION_COUNT)

static void ResetTimer(tmrHw_ID_t timerId)
    __attribute__ ((section(".aramtext")));
static int tmrHw_divide(int num, int denom)
    __attribute__ ((section(".aramtext")));

/****************************************************************************/
/**
*  @brief   Get timer capability
*
*  This function returns various capabilities/attributes of a timer
*
*  @return  Capability
*
*/
/****************************************************************************/
uint32_t tmrHw_getTimerCapability(tmrHw_ID_t timerId,	/*  [ IN ] Timer Id */
				  tmrHw_CAPABILITY_e capability	/*  [ IN ] Timer capability */
) {
	switch (capability) {
	case tmrHw_CAPABILITY_CLOCK:
		return (timerId <=
			1) ? tmrHw_LOW_RESOLUTION_CLOCK :
		    tmrHw_HIGH_RESOLUTION_CLOCK;
	case tmrHw_CAPABILITY_RESOLUTION:
		return 32;
	default:
		return 0;
	}
	return 0;
}

/****************************************************************************/
/**
*  @brief   Resets a timer
*
*  This function initializes  timer
*
*  @return  void
*
*/
/****************************************************************************/
static void ResetTimer(tmrHw_ID_t timerId	/*  [ IN ] Timer Id */
) {
	/* Reset timer */
	pTmrHw[timerId].LoadValue = 0;
	pTmrHw[timerId].CurrentValue = 0xFFFFFFFF;
	pTmrHw[timerId].Control = 0;
	pTmrHw[timerId].BackgroundLoad = 0;
	/* Always configure as a 32 bit timer */
	pTmrHw[timerId].Control |= tmrHw_CONTROL_32BIT;
	/* Clear interrupt only if raw status interrupt is set */
	if (pTmrHw[timerId].RawInterruptStatus) {
		pTmrHw[timerId].InterruptClear = 0xFFFFFFFF;
	}
}

/****************************************************************************/
/**
*  @brief   Sets counter value for an interval in ms
*
*  @return   On success: Effective counter value set
*            On failure: 0
*
*/
/****************************************************************************/
static tmrHw_INTERVAL_t SetTimerPeriod(tmrHw_ID_t timerId,	/*  [ IN ] Timer Id */
				       tmrHw_INTERVAL_t msec	/*  [ IN ] Interval in milli-second */
) {
	uint32_t scale = 0;
	uint32_t count = 0;

	if (timerId == 0 || timerId == 1) {
		if (msec <= tmrHw_LOW_1_MAX_MILLISEC) {
			pTmrHw[timerId].Control |= tmrHw_CONTROL_PRESCALE_1;
			scale = tmrHw_LOW_1_RESOLUTION_COUNT;
		} else if (msec <= tmrHw_LOW_16_MAX_MILLISEC) {
			pTmrHw[timerId].Control |= tmrHw_CONTROL_PRESCALE_16;
			scale = tmrHw_LOW_16_RESOLUTION_COUNT;
		} else if (msec <= tmrHw_LOW_256_MAX_MILLISEC) {
			pTmrHw[timerId].Control |= tmrHw_CONTROL_PRESCALE_256;
			scale = tmrHw_LOW_256_RESOLUTION_COUNT;
		} else {
			return 0;
		}

		count = msec * scale;
		/* Set counter value */
		pTmrHw[timerId].LoadValue = count;
		pTmrHw[timerId].BackgroundLoad = count;

	} else if (timerId == 2 || timerId == 3) {
		if (msec <= tmrHw_HIGH_1_MAX_MILLISEC) {
			pTmrHw[timerId].Control |= tmrHw_CONTROL_PRESCALE_1;
			scale = tmrHw_HIGH_1_RESOLUTION_COUNT;
		} else if (msec <= tmrHw_HIGH_16_MAX_MILLISEC) {
			pTmrHw[timerId].Control |= tmrHw_CONTROL_PRESCALE_16;
			scale = tmrHw_HIGH_16_RESOLUTION_COUNT;
		} else if (msec <= tmrHw_HIGH_256_MAX_MILLISEC) {
			pTmrHw[timerId].Control |= tmrHw_CONTROL_PRESCALE_256;
			scale = tmrHw_HIGH_256_RESOLUTION_COUNT;
		} else {
			return 0;
		}

		count = msec * scale;
		/* Set counter value */
		pTmrHw[timerId].LoadValue = count;
		pTmrHw[timerId].BackgroundLoad = count;
	}
	return count / scale;
}

/****************************************************************************/
/**
*  @brief   Configures a periodic timer in terms of timer interrupt rate
*
*  This function initializes a periodic timer to generate specific number of
*  timer interrupt per second
*
*  @return   On success: Effective timer frequency
*            On failure: 0
*
*/
/****************************************************************************/
tmrHw_RATE_t tmrHw_setPeriodicTimerRate(tmrHw_ID_t timerId,	/*  [ IN ] Timer Id */
					tmrHw_RATE_t rate	/*  [ IN ] Number of timer interrupt per second */
) {
	uint32_t resolution = 0;
	uint32_t count = 0;
	ResetTimer(timerId);

	/* Set timer mode periodic */
	pTmrHw[timerId].Control |= tmrHw_CONTROL_PERIODIC;
	pTmrHw[timerId].Control &= ~tmrHw_CONTROL_ONESHOT;
	/* Set timer in highest resolution */
	pTmrHw[timerId].Control |= tmrHw_CONTROL_PRESCALE_1;

	if (rate && (timerId == 0 || timerId == 1)) {
		if (rate > tmrHw_LOW_RESOLUTION_CLOCK) {
			return 0;
		}
		resolution = tmrHw_LOW_RESOLUTION_CLOCK;
	} else if (rate && (timerId == 2 || timerId == 3)) {
		if (rate > tmrHw_HIGH_RESOLUTION_CLOCK) {
			return 0;
		} else {
			resolution = tmrHw_HIGH_RESOLUTION_CLOCK;
		}
	} else {
		return 0;
	}
	/* Find the counter value */
	count = resolution / rate;
	/* Set counter value */
	pTmrHw[timerId].LoadValue = count;
	pTmrHw[timerId].BackgroundLoad = count;

	return resolution / count;
}

/****************************************************************************/
/**
*  @brief   Configures a periodic timer to generate timer interrupt after
*           certain time interval
*
*  This function initializes a periodic timer to generate timer interrupt
*  after every time interval in millisecond
*
*  @return   On success: Effective interval set in milli-second
*            On failure: 0
*
*/
/****************************************************************************/
tmrHw_INTERVAL_t tmrHw_setPeriodicTimerInterval(tmrHw_ID_t timerId,	/*  [ IN ] Timer Id */
						tmrHw_INTERVAL_t msec	/*  [ IN ] Interval in milli-second */
) {
	ResetTimer(timerId);

	/* Set timer mode periodic */
	pTmrHw[timerId].Control |= tmrHw_CONTROL_PERIODIC;
	pTmrHw[timerId].Control &= ~tmrHw_CONTROL_ONESHOT;

	return SetTimerPeriod(timerId, msec);
}

/****************************************************************************/
/**
*  @brief   Configures a periodic timer to generate timer interrupt just once
*           after certain time interval
*
*  This function initializes a periodic timer to generate a single ticks after
*  certain time interval in millisecond
*
*  @return   On success: Effective interval set in milli-second
*            On failure: 0
*
*/
/****************************************************************************/
tmrHw_INTERVAL_t tmrHw_setOneshotTimerInterval(tmrHw_ID_t timerId,	/*  [ IN ] Timer Id */
					       tmrHw_INTERVAL_t msec	/*  [ IN ] Interval in milli-second */
) {
	ResetTimer(timerId);

	/* Set timer mode oneshot */
	pTmrHw[timerId].Control |= tmrHw_CONTROL_PERIODIC;
	pTmrHw[timerId].Control |= tmrHw_CONTROL_ONESHOT;

	return SetTimerPeriod(timerId, msec);
}

/****************************************************************************/
/**
*  @brief   Configures a timer to run as a free running timer
*
*  This function initializes a timer to run as a free running timer
*
*  @return   Timer resolution (count / sec)
*
*/
/****************************************************************************/
tmrHw_RATE_t tmrHw_setFreeRunningTimer(tmrHw_ID_t timerId,	/*  [ IN ] Timer Id */
				       uint32_t divider	/*  [ IN ] Dividing the clock frequency */
) {
	uint32_t scale = 0;

	ResetTimer(timerId);
	/* Set timer as free running mode */
	pTmrHw[timerId].Control &= ~tmrHw_CONTROL_PERIODIC;
	pTmrHw[timerId].Control &= ~tmrHw_CONTROL_ONESHOT;

	if (divider >= 64) {
		pTmrHw[timerId].Control |= tmrHw_CONTROL_PRESCALE_256;
		scale = 256;
	} else if (divider >= 8) {
		pTmrHw[timerId].Control |= tmrHw_CONTROL_PRESCALE_16;
		scale = 16;
	} else {
		pTmrHw[timerId].Control |= tmrHw_CONTROL_PRESCALE_1;
		scale = 1;
	}

	if (timerId == 0 || timerId == 1) {
		return tmrHw_divide(tmrHw_LOW_RESOLUTION_CLOCK, scale);
	} else if (timerId == 2 || timerId == 3) {
		return tmrHw_divide(tmrHw_HIGH_RESOLUTION_CLOCK, scale);
	}

	return 0;
}

/****************************************************************************/
/**
*  @brief   Starts a timer
*
*  This function starts a preconfigured timer
*
*  @return  -1     - On Failure
*            0     - On Success
*
*/
/****************************************************************************/
int tmrHw_startTimer(tmrHw_ID_t timerId	/*  [ IN ] Timer id */
) {
	pTmrHw[timerId].Control |= tmrHw_CONTROL_TIMER_ENABLE;
	return 0;
}

/****************************************************************************/
/**
*  @brief   Stops a timer
*
*  This function stops a running timer
*
*  @return  -1     - On Failure
*            0     - On Success
*
*/
/****************************************************************************/
int tmrHw_stopTimer(tmrHw_ID_t timerId	/*  [ IN ] Timer id */
) {
	pTmrHw[timerId].Control &= ~tmrHw_CONTROL_TIMER_ENABLE;
	return 0;
}

/****************************************************************************/
/**
*  @brief   Gets current timer count
*
*  This function returns the current timer value
*
*  @return  Current downcounting timer value
*
*/
/****************************************************************************/
uint32_t tmrHw_GetCurrentCount(tmrHw_ID_t timerId	/*  [ IN ] Timer id */
) {
	/* return 32 bit timer value */
	switch (pTmrHw[timerId].Control & tmrHw_CONTROL_MODE_MASK) {
	case tmrHw_CONTROL_FREE_RUNNING:
		if (pTmrHw[timerId].CurrentValue) {
			return tmrHw_MAX_COUNT - pTmrHw[timerId].CurrentValue;
		}
		break;
	case tmrHw_CONTROL_PERIODIC:
	case tmrHw_CONTROL_ONESHOT:
		return pTmrHw[timerId].BackgroundLoad -
		    pTmrHw[timerId].CurrentValue;
	}
	return 0;
}

/****************************************************************************/
/**
*  @brief   Gets timer count rate
*
*  This function returns the number of counts per second
*
*  @return  Count rate
*
*/
/****************************************************************************/
tmrHw_RATE_t tmrHw_getCountRate(tmrHw_ID_t timerId	/*  [ IN ] Timer id */
) {
	uint32_t divider = 0;

	switch (pTmrHw[timerId].Control & tmrHw_CONTROL_PRESCALE_MASK) {
	case tmrHw_CONTROL_PRESCALE_1:
		divider = 1;
		break;
	case tmrHw_CONTROL_PRESCALE_16:
		divider = 16;
		break;
	case tmrHw_CONTROL_PRESCALE_256:
		divider = 256;
		break;
	default:
		tmrHw_ASSERT(0);
	}

	if (timerId == 0 || timerId == 1) {
		return tmrHw_divide(tmrHw_LOW_RESOLUTION_CLOCK, divider);
	} else {
		return tmrHw_divide(tmrHw_HIGH_RESOLUTION_CLOCK, divider);
	}
	return 0;
}

/****************************************************************************/
/**
*  @brief   Enables timer interrupt
*
*  This function enables the timer interrupt
*
*  @return   N/A
*
*/
/****************************************************************************/
void tmrHw_enableInterrupt(tmrHw_ID_t timerId	/*  [ IN ] Timer id */
) {
	pTmrHw[timerId].Control |= tmrHw_CONTROL_INTERRUPT_ENABLE;
}

/****************************************************************************/
/**
*  @brief   Disables timer interrupt
*
*  This function disable the timer interrupt
*
*  @return   N/A
*
*/
/****************************************************************************/
void tmrHw_disableInterrupt(tmrHw_ID_t timerId	/*  [ IN ] Timer id */
) {
	pTmrHw[timerId].Control &= ~tmrHw_CONTROL_INTERRUPT_ENABLE;
}

/****************************************************************************/
/**
*  @brief   Clears the interrupt
*
*  This function clears the timer interrupt
*
*  @return   N/A
*
*  @note
*     Must be called under the context of ISR
*/
/****************************************************************************/
void tmrHw_clearInterrupt(tmrHw_ID_t timerId	/*  [ IN ] Timer id */
) {
	pTmrHw[timerId].InterruptClear = 0x1;
}

/****************************************************************************/
/**
*  @brief   Gets the interrupt status
*
*  This function returns timer interrupt status
*
*  @return   Interrupt status
*/
/****************************************************************************/
tmrHw_INTERRUPT_STATUS_e tmrHw_getInterruptStatus(tmrHw_ID_t timerId	/*  [ IN ] Timer id */
) {
	if (pTmrHw[timerId].InterruptStatus) {
		return tmrHw_INTERRUPT_STATUS_SET;
	} else {
		return tmrHw_INTERRUPT_STATUS_UNSET;
	}
}

/****************************************************************************/
/**
*  @brief   Indentifies a timer causing interrupt
*
*  This functions returns a timer causing interrupt
*
*  @return  0xFFFFFFFF   : No timer causing an interrupt
*           ! 0xFFFFFFFF : timer causing an interrupt
*  @note
*     tmrHw_clearIntrrupt() must be called with a valid timer id after calling this function
*/
/****************************************************************************/
tmrHw_ID_t tmrHw_getInterruptSource(void	/*  void */
) {
	int i;

	for (i = 0; i < tmrHw_TIMER_NUM_COUNT; i++) {
		if (pTmrHw[i].InterruptStatus) {
			return i;
		}
	}

	return 0xFFFFFFFF;
}

/****************************************************************************/
/**
*  @brief   Displays specific timer registers
*
*
*  @return  void
*
*/
/****************************************************************************/
void tmrHw_printDebugInfo(tmrHw_ID_t timerId,	/*  [ IN ] Timer id */
			  int (*fpPrint) (const char *, ...)	/*  [ IN ] Print callback function */
) {
	(*fpPrint) ("Displaying register contents \n\n");
	(*fpPrint) ("Timer %d: Load value              0x%X\n", timerId,
		    pTmrHw[timerId].LoadValue);
	(*fpPrint) ("Timer %d: Background load value   0x%X\n", timerId,
		    pTmrHw[timerId].BackgroundLoad);
	(*fpPrint) ("Timer %d: Control                 0x%X\n", timerId,
		    pTmrHw[timerId].Control);
	(*fpPrint) ("Timer %d: Interrupt clear         0x%X\n", timerId,
		    pTmrHw[timerId].InterruptClear);
	(*fpPrint) ("Timer %d: Interrupt raw interrupt 0x%X\n", timerId,
		    pTmrHw[timerId].RawInterruptStatus);
	(*fpPrint) ("Timer %d: Interrupt status        0x%X\n", timerId,
		    pTmrHw[timerId].InterruptStatus);
}

/****************************************************************************/
/**
*  @brief   Use a timer to perform a busy wait delay for a number of usecs.
*
*  @return   N/A
*/
/****************************************************************************/
void tmrHw_udelay(tmrHw_ID_t timerId,	/*  [ IN ] Timer id */
		  unsigned long usecs /*  [ IN ] usec to delay */
) {
	tmrHw_RATE_t usec_tick_rate;
	tmrHw_COUNT_t start_time;
	tmrHw_COUNT_t delta_time;

	start_time = tmrHw_GetCurrentCount(timerId);
	usec_tick_rate = tmrHw_divide(tmrHw_getCountRate(timerId), 1000000);
	delta_time = usecs * usec_tick_rate;

	/* Busy wait */
	while (delta_time > (tmrHw_GetCurrentCount(timerId) - start_time))
		;
}

/****************************************************************************/
/**
*  @brief   Local Divide function
*
*  This function does the divide
*
*  @return divide value
*
*/
/****************************************************************************/
static int tmrHw_divide(int num, int denom)
{
	int r;
	int t = 1;

	/* Shift denom and t up to the largest value to optimize algorithm */
	/* t contains the units of each divide */
	while ((denom & 0x40000000) == 0) {	/* fails if denom=0 */
		denom = denom << 1;
		t = t << 1;
	}

	/* Initialize the result */
	r = 0;

	do {
		/* Determine if there exists a positive remainder */
		if ((num - denom) >= 0) {
			/* Accumlate t to the result and calculate a new remainder */
			num = num - denom;
			r = r + t;
		}
		/* Continue to shift denom and shift t down to 0 */
		denom = denom >> 1;
		t = t >> 1;
	} while (t != 0);
	return r;
}
