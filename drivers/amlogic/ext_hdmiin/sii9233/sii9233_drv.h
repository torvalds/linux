#ifndef __HDMIRX_H__
#define __HDMIRX_H__

#include <linux/i2c.h>
#include <mach/gpio.h>
#include <linux/amlogic/tvin/tvin_v4l2.h>
#include "../../../../hardware/tvin/tvin_frontend.h"

#define SII9233A_DRV_VER "2013Nov12"

#define SII9233A_DRV_NAME "sii9233a"

typedef struct
{
	unsigned int 		cur_height;
	unsigned int 		cur_width;
	unsigned int 		cur_frame_rate;
	/* data */
}vdin_info_t;

typedef struct
{
	unsigned int		i2c_bus_index;
	struct i2c_client	*i2c_client;
	gpio_t				gpio_intr; // interrupt pin
	gpio_t				gpio_reset; // hardware reset pin
	tvin_frontend_t		tvin_frontend;
	vdin_parm_t			vdin_parm;
	unsigned int 		vdin_started;
	unsigned int 		user_cmd;	// 0 to disable from user
									// 1 to enable, driver will trigger to vdin-stop
									// 2 to enable, driver will trigger to vdin-start
									// 3 to enable, driver will trigger to vdin-start/vdin-stop
									// 4 to enable, driver will not trigger to vdin-start/vdin-stop
									// 0xff to enable, and driver will NOT trigger no signal-lost/vdin-stop, signal-get/vdin-start
	unsigned int 		cable_status; // 1 for cable plug in, 0 for cable plgu out
	unsigned int 		signal_status; // external hdmi cable is insert or not
	vdin_info_t			vdin_info;
}sii9233a_info_t;

// according to the 《CEA-861-D》
typedef enum
{
	CEA_480P60	= 2,
	CEA_720P60	= 4,
	CEA_1080I60	= 5,
	CEA_480I60	= 6,

	CEA_1080P60	= 16,
	CEA_576P50	= 17,
	CEA_720P50	= 19,
	CEA_1080I50	= 20,
	CEA_576I50	= 21,

	CEA_1080P50	= 31,

	CEA_MAX = 60
}SII9233_VIDEO_MODE;

int aml_sii9233a_i2c_read(unsigned char dev_addr, char *buf, int addr_len, int data_len);
int aml_sii9233a_i2c_write(unsigned char dev_addr, char *buf, int len);

#endif
