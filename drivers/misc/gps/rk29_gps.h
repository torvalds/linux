/*
	2011.01.16  lw@rock-chips.com
*/

#ifndef __RK29_GPS_H__
#define __RK29_GPS_H__

struct rk29_gps_data {
	int (*power_up)(void);
	int (*power_down)(void);
	int uart_id;
	int powerpin;
	int powerflag;
};

#endif
