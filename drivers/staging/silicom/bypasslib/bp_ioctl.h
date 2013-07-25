/******************************************************************************/
/*                                                                            */
/* bypass library, Copyright (c) 2004-2006 Silicom, Ltd                       */
/* Corporation.                                                               */
/*                                                                            */
/* This program is free software; you can redistribute it and/or modify       */
/* it under the terms of the GNU General Public License as published by       */
/* the Free Software Foundation, located in the file LICENSE.                 */
/*                                                                            */
/*                                                                            */
/*                                                                            */
/******************************************************************************/

#ifndef BP_IOCTL_H
#define BP_IOCTL_H

#define BP_CAP                   0x01	/* BIT_0 */
#define BP_STATUS_CAP            0x02	/* BIT_1 */
#define BP_STATUS_CHANGE_CAP     0x04	/* BIT_2 */
#define SW_CTL_CAP               0x08	/* BIT_3 */
#define BP_DIS_CAP               0x10	/* BIT_4 */
#define BP_DIS_STATUS_CAP        0x20	/* BIT_5 */
#define STD_NIC_CAP              0x40	/* BIT_6 */
#define BP_PWOFF_ON_CAP          0x80	/* BIT_7 */
#define BP_PWOFF_OFF_CAP         0x0100	/* BIT_8 */
#define BP_PWOFF_CTL_CAP         0x0200	/* BIT_9 */
#define BP_PWUP_ON_CAP           0x0400	/* BIT_10 */
#define BP_PWUP_OFF_CAP          0x0800	/* BIT_11 */
#define BP_PWUP_CTL_CAP          0x1000	/* BIT_12 */
#define WD_CTL_CAP               0x2000	/* BIT_13 */
#define WD_STATUS_CAP            0x4000	/* BIT_14 */
#define WD_TIMEOUT_CAP           0x8000	/* BIT_15 */
#define TX_CTL_CAP               0x10000	/* BIT_16 */
#define TX_STATUS_CAP            0x20000	/* BIT_17 */
#define TAP_CAP                  0x40000	/* BIT_18 */
#define TAP_STATUS_CAP           0x80000	/* BIT_19 */
#define TAP_STATUS_CHANGE_CAP    0x100000	/* BIT_20 */
#define TAP_DIS_CAP              0x200000	/* BIT_21 */
#define TAP_DIS_STATUS_CAP       0x400000	/* BIT_22 */
#define TAP_PWUP_ON_CAP          0x800000	/* BIT_23 */
#define TAP_PWUP_OFF_CAP         0x1000000	/* BIT 24 */
#define TAP_PWUP_CTL_CAP         0x2000000	/* BIT 25 */
#define NIC_CAP_NEG              0x4000000	/* BIT 26 */
#define TPL_CAP                  0x8000000	/* BIT 27 */
#define DISC_CAP                 0x10000000	/* BIT 28 */
#define DISC_DIS_CAP             0x20000000	/* BIT 29 */
#define DISC_PWUP_CTL_CAP        0x40000000	/* BIT 30 */

#define WD_MIN_TIME_MASK(val)      (val & 0xf)
#define WD_STEP_COUNT_MASK(val)    ((val & 0xf) << 5)
#define WDT_STEP_TIME              0x10	/* BIT_4 */

#define WD_MIN_TIME_GET(desc)   (desc & 0xf)
#define WD_STEP_COUNT_GET(desc) (desc>>5) & 0xf

typedef enum {
	IS_BYPASS = 1,
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
} CMND_TYPE;

typedef enum {
	IF_SCAN_SD,
	GET_DEV_NUM_SD,
	IS_BYPASS_SD,
	GET_BYPASS_SLAVE_SD,
	GET_BYPASS_CAPS_SD,
	GET_WD_SET_CAPS_SD,
	SET_BYPASS_SD,
	GET_BYPASS_SD,
	GET_BYPASS_CHANGE_SD,
	SET_BYPASS_WD_SD,
	GET_BYPASS_WD_SD,
	GET_WD_EXPIRE_TIME_SD,
	RESET_BYPASS_WD_TIMER_SD,
	SET_DIS_BYPASS_SD,
	GET_DIS_BYPASS_SD,
	SET_BYPASS_PWOFF_SD,
	GET_BYPASS_PWOFF_SD,
	SET_BYPASS_PWUP_SD,
	GET_BYPASS_PWUP_SD,
	SET_STD_NIC_SD,
	GET_STD_NIC_SD,
	SET_TX_SD,
	GET_TX_SD,
	SET_TAP_SD,
	GET_TAP_SD,
	GET_TAP_CHANGE_SD,
	SET_DIS_TAP_SD,
	GET_DIS_TAP_SD,
	SET_TAP_PWUP_SD,
	GET_TAP_PWUP_SD,
	SET_WD_EXP_MODE_SD,
	GET_WD_EXP_MODE_SD,
	SET_WD_AUTORESET_SD,
	GET_WD_AUTORESET_SD,
	SET_TPL_SD,
	GET_TPL_SD,
	SET_DISC_SD,
	GET_DISC_SD,
	GET_DISC_CHANGE_SD,
	SET_DIS_DISC_SD,
	GET_DIS_DISC_SD,
	SET_DISC_PWUP_SD,
	GET_DISC_PWUP_SD,

	GET_BYPASS_INFO_SD = 100,
	GET_BP_WAIT_AT_PWUP_SD,
	SET_BP_WAIT_AT_PWUP_SD,
	GET_BP_HW_RESET_SD,
	SET_BP_HW_RESET_SD,

} CMND_TYPE_SD;

#define SIOCGIFBYPASS SIOCDEVPRIVATE+10

struct bp_info {
	char prod_name[14];
	unsigned char fw_ver;
};

/* for passing single values */
struct if_bypass {
	char if_name[IFNAMSIZ];
	int cmd;
	int data;
};
struct if_bypass_info {
	char if_name[IFNAMSIZ];
	char cmd;
	struct bp_info bp_info;
};

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

#define DEVICE_NAME "bpctl"

#endif
