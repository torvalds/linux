/*******************************************************************
 *  Copyright C 2010 by Amlogic, Inc. All Rights Reserved.
 *  File name: tvafe.h
 *  Description: IO function, structure, enum, used in TVIN AFE sub-module processing
 *******************************************************************/

#ifndef _TVAFE_H
#define _TVAFE_H

/* Standard Linux Headers */
#include <linux/cdev.h>
#include <linux/timer.h>
#include <linux/mutex.h>

#include <linux/amlogic/tvin/tvin.h>
#include "../tvin_global.h"
#include "../tvin_frontend.h"
#include "tvafe_general.h"   /* For Kernel used only */
#include "tvafe_adc.h"       /* For Kernel used only */
#include "tvafe_cvd.h"       /* For Kernel used only */

// ***************************************************************************
// *** macro definitions *********************************************
// ***************************************************************************
#define TVAFE_VER "Ref.2013/10/23"

/* used to set the flag of tvafe_dev_s */
#define TVAFE_FLAG_DEV_OPENED       0x00000010
#define TVAFE_FLAG_DEV_STARTED      0x00000020
#define TVAFE_POWERDOWN_IN_IDLE

// ***************************************************************************
// *** enum definitions *********************************************
// ***************************************************************************

// ***************************************************************************
// *** structure definitions *********************************************
// ***************************************************************************

/* tvafe module structure */
typedef struct tvafe_info_s {
	struct tvin_parm_s          parm;

	//adc calibration data
	struct tvafe_cal_s          cal;

	/* WSS data */
	struct tvafe_comp_wss_s     comp_wss;    //WSS data;

	struct tvafe_adc_s          adc;
	struct tvafe_cvd2_s         cvd2;
} tvafe_info_t;

/* tvafe device structure */
typedef struct tvafe_dev_s {
	int                         index;

	dev_t                       devt;
	struct cdev                 cdev;
	struct device               *dev;


	struct mutex                afe_mutex;
	struct timer_list           timer;

	struct tvin_frontend_s      frontend;
	unsigned int                flags;        // bit4: TVAFE_FLAG_DEV_OPENED
	// bit5: TVAFE_FLAG_DEV_STARTED
	struct tvafe_pin_mux_s      *pinmux;      //pin mux setting from board config
	/* cvd2 memory */
	struct tvafe_cvd2_mem_s     mem;

	struct tvafe_info_s         tvafe;

} tvafe_dev_t;


#endif  // _TVAFE_H

