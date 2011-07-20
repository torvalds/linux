/*
	2011.01.2  lw@rock-chips.com
*/

#ifndef __RK29_GPS_H__
#define __RK29_GPS_H__

struct rk29_gps_data {
	int (*power_up)(void);
	int (*power_down)(void);
	int (*reset)(int);
	int uart_id;
	int power_flag;
	struct semaphore power_sem;
	struct workqueue_struct *wq;
	struct work_struct work;
};

#endif
