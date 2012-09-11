/******************************************************************************/
/*                                                                            */
/* Silicom Bypass Control Utility, Copyright (c) 2005-2007 Silicom            */
/* All rights reserved.                                                       */
/*                                                                            */
/* This program is free software; you can redistribute it and/or modify       */
/* it under the terms of the GNU General Public License as published by       */
/* the Free Software Foundation, located in the file LICENSE.                 */
/*                                                                            */
/*                                                                            */
/******************************************************************************/

#ifndef BP_IOCTL_H
#define BP_IOCTL_H

#define BP_CAP                   0x01	/* BIT_0 */
#define BP_STATUS_CAP            0x02
#define BP_STATUS_CHANGE_CAP     0x04
#define SW_CTL_CAP               0x08
#define BP_DIS_CAP               0x10
#define BP_DIS_STATUS_CAP        0x20
#define STD_NIC_CAP              0x40
#define BP_PWOFF_ON_CAP          0x80
#define BP_PWOFF_OFF_CAP         0x0100
#define BP_PWOFF_CTL_CAP         0x0200
#define BP_PWUP_ON_CAP           0x0400
#define BP_PWUP_OFF_CAP          0x0800
#define BP_PWUP_CTL_CAP          0x1000
#define WD_CTL_CAP               0x2000
#define WD_STATUS_CAP            0x4000
#define WD_TIMEOUT_CAP           0x8000
#define TX_CTL_CAP               0x10000
#define TX_STATUS_CAP            0x20000
#define TAP_CAP                  0x40000
#define TAP_STATUS_CAP           0x80000
#define TAP_STATUS_CHANGE_CAP    0x100000
#define TAP_DIS_CAP              0x200000
#define TAP_DIS_STATUS_CAP       0x400000
#define TAP_PWUP_ON_CAP          0x800000
#define TAP_PWUP_OFF_CAP         0x1000000
#define TAP_PWUP_CTL_CAP         0x2000000
#define NIC_CAP_NEG              0x4000000
#define TPL_CAP                  0x8000000
#define DISC_CAP                 0x10000000
#define DISC_DIS_CAP             0x20000000
#define DISC_PWUP_CTL_CAP        0x40000000

#define TPL2_CAP_EX              0x01
#define DISC_PORT_CAP_EX         0x02

#define WD_MIN_TIME_MASK(val)      (val & 0xf)
#define WD_STEP_COUNT_MASK(val)    ((val & 0xf) << 5)
#define WDT_STEP_TIME              0x10	/* BIT_4 */

#define WD_MIN_TIME_GET(desc)   (desc & 0xf)
#define WD_STEP_COUNT_GET(desc) ((desc>>5) & 0xf)

typedef enum {
	IF_SCAN,
	GET_DEV_NUM,
	IS_BYPASS,
	GET_BYPASS_SLAVE,
	GET_BYPASS_CAPS,
	GET_WD_SET_CAPS,
	SET_BYPASS,
	GET_BYPASS,
	GET_BYPASS_CHANGE,
	SET_BYPASS_WD,
	GET_BYPASS_WD,
	GET_WD_EXPIRE_TIME,
	RESET_BYPASS_WD_TIMER,
	SET_DIS_BYPASS,
	GET_DIS_BYPASS,
	SET_BYPASS_PWOFF,
	GET_BYPASS_PWOFF,
	SET_BYPASS_PWUP,
	GET_BYPASS_PWUP,
	SET_STD_NIC,
	GET_STD_NIC,
	SET_TX,
	GET_TX,
	SET_TAP,
	GET_TAP,
	GET_TAP_CHANGE,
	SET_DIS_TAP,
	GET_DIS_TAP,
	SET_TAP_PWUP,
	GET_TAP_PWUP,
	SET_WD_EXP_MODE,
	GET_WD_EXP_MODE,
	SET_WD_AUTORESET,
	GET_WD_AUTORESET,
	SET_TPL,
	GET_TPL,
	SET_DISC,
	GET_DISC,
	GET_DISC_CHANGE,
	SET_DIS_DISC,
	GET_DIS_DISC,
	SET_DISC_PWUP,
	GET_DISC_PWUP,
	GET_BYPASS_INFO = 100,
	GET_BP_WAIT_AT_PWUP,
	SET_BP_WAIT_AT_PWUP,
	GET_BP_HW_RESET,
	SET_BP_HW_RESET,
	SET_DISC_PORT,
	GET_DISC_PORT,
	SET_DISC_PORT_PWUP,
	GET_DISC_PORT_PWUP,
	SET_BP_FORCE_LINK,
	GET_BP_FORCE_LINK,
#ifdef BP_SELF_TEST
	SET_BP_SELF_TEST = 200,
	GET_BP_SELF_TEST,
#endif

} CMND_TYPE_SD;

/*
* The major device number. We can't rely on dynamic
* registration any more, because ioctls need to know
* it.
*/

#define MAGIC_NUM 'J'

/* for passing single values */
struct bpctl_cmd {
	int status;
	int data[8];
	int in_param[8];
	int out_param[8];
};

#define IOCTL_TX_MSG(cmd) _IOWR(MAGIC_NUM, cmd, struct bpctl_cmd)

#define DEVICE_NODE "/dev/bpctl0"
#define DEVICE_NAME "bpctl"

#endif
