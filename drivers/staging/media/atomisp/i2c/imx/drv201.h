/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DRV201_H__
#define __DRV201_H__

#include "../../include/linux/atomisp_platform.h"
#include <linux/types.h>
#include <linux/time.h>

#define DRV201_VCM_ADDR	0x0e

/* drv201 device structure */
struct drv201_device {
	const struct camera_af_platform_data *platform_data;
	struct timespec timestamp_t_focus_abs;
	struct timespec focus_time;	/* Time when focus was last time set */
	s32 focus;			/* Current focus value */
	s16 number_of_steps;
	bool initialized;		/* true if drv201 is detected */
};

#define DRV201_INVALID_CONFIG	0xffffffff
#define DRV201_MAX_FOCUS_POS	1023
#define DELAY_PER_STEP_NS	1000000
#define DELAY_MAX_PER_STEP_NS	(1000000 * 1023)

#define DRV201_CONTROL				2
#define DRV201_VCM_CURRENT		3
#define DRV201_STATUS				5
#define DRV201_MODE				6
#define DRV201_VCM_FREQ			7

#define DEFAULT_CONTROL_VAL		2
#define DRV201_RESET				1
#define WAKEUP_DELAY_US			100
#define VCM_CODE_MASK	0x03ff

#endif


