/*
 * Copyright (C) 2012 - 2014 Cisco Systems
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#ifndef __TIMER_INTERNAL_H__
#define __TIMER_INTERNAL_H__

#define TIMER_MULTIPLIER 256
#define TIMER_MIN_DELTA  500

enum time_travel_mode {
	TT_MODE_OFF,
	TT_MODE_BASIC,
	TT_MODE_INFCPU,
};

enum time_travel_timer_mode {
	TT_TMR_DISABLED,
	TT_TMR_ONESHOT,
	TT_TMR_PERIODIC,
};

#ifdef CONFIG_UML_TIME_TRAVEL_SUPPORT
extern enum time_travel_mode time_travel_mode;
extern unsigned long long time_travel_time;
extern enum time_travel_timer_mode time_travel_timer_mode;
extern unsigned long long time_travel_timer_expiry;
extern unsigned long long time_travel_timer_interval;

static inline void time_travel_set_time(unsigned long long ns)
{
	time_travel_time = ns;
}

static inline void time_travel_set_timer(enum time_travel_timer_mode mode,
					 unsigned long long expiry)
{
	time_travel_timer_mode = mode;
	time_travel_timer_expiry = expiry;
}
#else
#define time_travel_mode TT_MODE_OFF
#define time_travel_time 0
#define time_travel_timer_expiry 0
#define time_travel_timer_interval 0

static inline void time_travel_set_time(unsigned long long ns)
{
}

static inline void time_travel_set_timer(enum time_travel_timer_mode mode,
					 unsigned long long expiry)
{
}

#define time_travel_timer_mode TT_TMR_DISABLED
#endif

#endif
