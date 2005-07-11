/* 
 * Copyright (C) 2002 Jeff Dike (jdike@karaya.com)
 * Licensed under the GPL
 */

#ifndef __TIME_USER_H__
#define __TIME_USER_H__

extern void timer(void);
extern void switch_timers(int to_real);
extern void idle_sleep(int secs);
extern void enable_timer(void);
extern void prepare_timer(void * ptr);
extern void disable_timer(void);
extern unsigned long time_lock(void);
extern void time_unlock(unsigned long);
extern void user_time_init(void);

#endif
