#ifndef __RK29_LIGHTSENSOR_H__
#define __RK29_LIGHTSENSOR_H__

#include <linux/ioctl.h>


#define STARTUP_LEV_LOW			0
#define SHUTDOWN_LEV_HIGH		1

#define LSR_GPIO				RK29_PIN5_PA2
#define LSR_IOCTL_NR_ENABLE		1
#define LSR_IOCTL_NR_SETRATE	2
#define LSR_IOCTL_NR_DEVNAME	3
#define LSR_IOCTL_NR_SWICTH		4


#define LSR_IOCTL_ENABLE		_IOR('l', LSR_IOCTL_NR_ENABLE, unsigned long )
#define LSR_IOCTL_SETRATE		_IOR('l', LSR_IOCTL_NR_SETRATE, unsigned long )
#define LSR_IOCTL_DEVNAME		_IOR('l', LSR_IOCTL_NR_DEVNAME, unsigned long )
#define LSR_IOCTL_SWICTH		_IOR('l', LSR_IOCTL_NR_SWICTH, unsigned long )

#define LSR_ON					0
#define LSR_OFF					1
#define LSR_NAME				"rk29-lsr"

#define	RATE(x)					(1000/(x))

struct rk29_lsr_platform_data {
	int 				gpio;
	char 				*desc;
	int 				adc_chn;
	struct adc_client 	*client; 
	struct timer_list	timer;
	struct input_dev 	*input_dev;
	unsigned int		delay_time;
	unsigned int		rate;
	int					oldresult;
	struct mutex 		lsr_mutex;
	int 				active_low;
	unsigned int		lsr_state;
	unsigned int		timer_on;
};



#endif
