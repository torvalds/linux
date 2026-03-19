/* SPDX-License-Identifier: GPL-2.0 */

/* Common definitions for cpuidle governors. */

#ifndef __CPUIDLE_GOVERNOR_H
#define __CPUIDLE_GOVERNOR_H

/*
 * Idle state target residency threshold used for deciding whether or not to
 * check the time till the closest expected timer event.
 */
#define RESIDENCY_THRESHOLD_NS	(15 * NSEC_PER_USEC)
/*
 * If the closest timer is in this range, the governor idle state selection need
 * not be adjusted after the scheduler tick has been stopped.
 */
#define SAFE_TIMER_RANGE_NS	(2 * TICK_NSEC)

#endif /* __CPUIDLE_GOVERNOR_H */
