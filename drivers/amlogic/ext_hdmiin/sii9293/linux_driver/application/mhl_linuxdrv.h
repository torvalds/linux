/*
 * SiIxxxx <Firmware or Driver>
 *
 * Copyright (C) 2011 Silicon Image Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed .as is. WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
 * PURPOSE.  See the GNU General Public License for more details.
*/

/**
 * @file mhl_driver.h
 *
 * @brief Main header file of the MHL Tx driver.
 *
 * $Author: Dave Canfield
 * $Rev: $
 * $Date: Jan 20, 2011
 *
 *****************************************************************************/


#if !defined(MHL_DRIVER_H)
#define MHL_DRIVER_H

#include <linux/amlogic/tvin/tvin_v4l2.h>
#include "../../../../../../../../../hardware/tvin/tvin_frontend.h"

#include "sii_hal.h"
#include <linux/device.h>
#include <mach/gpio.h>




#ifdef __cplusplus 
extern "C" { 
#endif  /* _defined (__cplusplus) */

/***** macro definitions *****************************************************/
#if defined(MAKE_5293_DRIVER)

#define MHL_DRIVER_NAME "sii5293drv"
#define MHL_DEVICE_NAME "sii-5293"
//#define CLASS_NAME	"video"
#define CLASS_NAME  "sii9293"
#define DEVNAME		"sii5293"
#define MHL_DEVNAME	"mhl"

#define NUMBER_OF_DEVS	2

#define DEVICE_EVENT	"DEVICE_EVENT"
#define MHL_EVENT		"MHL_EVENT"

/* Device events */
#define DEV_CONNECTION_CHANGE_EVENT "connection_change"
#define DEV_INPUT_VIDEO_MODE_EVENT "input_video_stable"

/* MHL events */
#define MHL_CONNECTED_EVENT	"connected"
#define MHL_DISCONNECTED_EVENT	"disconnected"
#define MHL_RAP_RECEIVED_EVENT	"received_rap"
#define MHL_RAP_ACKED_EVENT	"received_rapk"
#define MHL_RCP_RECEIVED_EVENT	"received_rcp"
#define MHL_RCP_ACKED_EVENT	"received_rcpk"
#define MHL_RCP_ERROR_EVENT	"received_rcpe"
#define MHL_UCP_RECEIVED_EVENT	"received_ucp"
#define MHL_UCP_ACKED_EVENT	"received_ucpk"
#define MHL_UCP_ERROR_EVENT	"received_ucpe"

#else   

#error "Need to add name and description strings for new drivers here!"

#endif


#define MHL_DRIVER_MINOR_MAX   1


/***** public type definitions ***********************************************/

typedef enum
{
    MHL_CONN = 1,   //default value is 0, so that when comes the notify first time will always effective.
    HDMI_CONN,
    NO_CONN,
} SourceConnection_t;

typedef struct {
    uint8_t chip_revision;      // chip revision
    bool_t  pwr5v_state;        // power 5v state
    bool_t  mhl_cable_state;    // mhl cable state
    SourceConnection_t connection_state;
    uint8_t input_video_mode;       // last determined video mode
    uint8_t debug_i2c_address;
    uint8_t debug_i2c_offset;
    uint8_t debug_i2c_xfer_length;
    uint8_t devcap_remote_offset;   // last Device Capability register 
    uint8_t devcap_local_offset;    // last Device Capability register
    uint8_t rap_in_keycode;         // last RAP key code received.
    uint8_t rap_out_keycode;        // last RAP key code transmitted.
    uint8_t rap_out_statecode;      // last RAP state code transmitted
    uint8_t rcp_in_keycode;         // last RCP key code received.
    uint8_t rcp_out_keycode;        // last RCP key code transmitted.
    uint8_t rcp_out_statecode;      // last RCP state code transmitted
    uint8_t ucp_in_keycode;         // last UCP key code received.
    uint8_t ucp_out_keycode;        // last UCP key code transmitted.
    uint8_t ucp_out_statecode;      // last UCP state code transmitted
} MHL_DRIVER_CONTEXT_T, *PMHL_DRIVER_CONTEXT_T;

struct mhl_device_info {
	dev_t devnum;
	struct cdev *cdev;
	struct device *device;

};

typedef struct
{
    /* data */
    unsigned int    i2c_bus_index;
    gpio_t          gpio_reset;
    gpio_t          gpio_intr;
}sii5293_config;

struct device_info {
	dev_t devnum;
	struct cdev *cdev;
	struct device *device;
	struct class *dev_class;

	struct mhl_device_info *mhl;
    sii5293_config config;

	uint8_t my_rap_input_device;
	uint8_t my_rcp_input_device;
	uint8_t my_ucp_input_device;
};

typedef struct
{
	unsigned int 		cur_height;
	unsigned int 		cur_width;
	unsigned int 		cur_frame_rate;
	/* data */
}vdin_info_t;

typedef struct
{
	tvin_frontend_t tvin_frontend;
	vdin_parm_t	vdin_parm;
	vdin_info_t		vdin_info;
	unsigned int	vdin_started;
}sii5293_vdin;

typedef struct
{
    unsigned int        user_cmd; // 0 to disable from user
                                  // 1 to enable, driver will trigger to vdin-stop
                                  // 2 to enable, driver will trigger to vdin-start
                                  // 3 to enable, driver will trigger to vdin-start/vdin-stop
                                  // 4 to enable, driver will not trigger to vdin-start/vdin-stop
                                  // 0xff to enable, and driver will NOT trigger on signal-lost/vdin-stop, singal-get/vdin-start
    unsigned int        cable_status; // 1 for cable plug in, 0 for cable plug out
    unsigned int        signal_status; // external hdmi cable is insert or not
}sii9293_info_t;

#define HDMIIN_FRAME_SKIP_MECHANISM 1

#ifdef HDMIIN_FRAME_SKIP_MECHANISM
// frame skip configuration is needed as:
//     for following status: standby/powerup, cable plug out/in, etc
//     we need drop some frame for HDMIIN device will still keep old frames
// the skip num maybe different in each status.

#define FRAME_SKIP_NUM_NORMAL	1
#define FRAME_SKIP_NUM_STANDBY	1
#define FRAME_SKIP_NUM_CABLE	1

typedef enum
{
	SKIP_STATUS_NORMAL 	= 0,
	SKIP_STATUS_STANDBY = 1,
	SKIP_STATUS_CABLE 	= 2,
	SKIP_STATUS_MAX
}skip_status_e;

typedef struct
{
	unsigned char skip_num_normal;
	unsigned char skip_num_standby;
	unsigned char skip_num_cable;
}sii9293_frame_skip_t;

#endif

/***** global variables ********************************************/

extern MHL_DRIVER_CONTEXT_T gDriverContext;
extern struct device_info *devinfo;

/***** public function prototypes ********************************************/
/**
 * \defgroup driver_public_api Driver Public API
 * @{
 */
int send_sii5293_uevent(struct device *device, const char *event_cat,
			const char *event_type, const char *event_data);

void SiiConnectionStateNotify(bool_t connect);

#ifdef __cplusplus
}
#endif  /* _defined (__cplusplus) */

#endif /* _defined (MHL_DRIVER_H) */
