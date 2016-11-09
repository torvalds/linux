/*
 * TimeSync API driver.
 *
 * Copyright 2016 Google Inc.
 * Copyright 2016 Linaro Ltd.
 *
 * Released under the GPLv2 only.
 */

#ifndef __TIMESYNC_H
#define __TIMESYNC_H

struct gb_svc;
struct gb_interface;
struct gb_timesync_svc;

/* Platform */
u64 gb_timesync_platform_get_counter(void);
u32 gb_timesync_platform_get_clock_rate(void);
int gb_timesync_platform_lock_bus(struct gb_timesync_svc *pdata);
void gb_timesync_platform_unlock_bus(void);

int gb_timesync_platform_init(void);
void gb_timesync_platform_exit(void);

/* Core API */
int gb_timesync_interface_add(struct gb_interface *interface);
void gb_timesync_interface_remove(struct gb_interface *interface);
int gb_timesync_svc_add(struct gb_svc *svc);
void gb_timesync_svc_remove(struct gb_svc *svc);

u64 gb_timesync_get_frame_time_by_interface(struct gb_interface *interface);
u64 gb_timesync_get_frame_time_by_svc(struct gb_svc *svc);
int gb_timesync_to_timespec_by_svc(struct gb_svc *svc, u64 frame_time,
				   struct timespec *ts);
int gb_timesync_to_timespec_by_interface(struct gb_interface *interface,
					 u64 frame_time, struct timespec *ts);

int gb_timesync_schedule_synchronous(struct gb_interface *intf);
void gb_timesync_schedule_asynchronous(struct gb_interface *intf);
void gb_timesync_irq(struct gb_timesync_svc *timesync_svc);
int gb_timesync_init(void);
void gb_timesync_exit(void);

#endif /* __TIMESYNC_H */
