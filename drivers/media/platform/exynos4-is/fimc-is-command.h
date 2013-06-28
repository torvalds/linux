/*
 * Samsung Exynos4x12 FIMC-IS (Imaging Subsystem) driver
 *
 * FIMC-IS command set definitions
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *
 * Authors: Younghwan Joo <yhwan.joo@samsung.com>
 *          Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef FIMC_IS_CMD_H_
#define FIMC_IS_CMD_H_

#define FIMC_IS_COMMAND_VER	110 /* FIMC-IS command set version 1.10 */

/* Enumeration of commands beetween the FIMC-IS and the host processor. */

/* HOST to FIMC-IS */
#define HIC_PREVIEW_STILL	0x0001
#define HIC_PREVIEW_VIDEO	0x0002
#define HIC_CAPTURE_STILL	0x0003
#define HIC_CAPTURE_VIDEO	0x0004
#define HIC_STREAM_ON		0x0005
#define HIC_STREAM_OFF		0x0006
#define HIC_SET_PARAMETER	0x0007
#define HIC_GET_PARAMETER	0x0008
#define HIC_SET_TUNE		0x0009
#define HIC_GET_STATUS		0x000b
/* Sensor part */
#define HIC_OPEN_SENSOR		0x000c
#define HIC_CLOSE_SENSOR	0x000d
#define HIC_SIMMIAN_INIT	0x000e
#define HIC_SIMMIAN_WRITE	0x000f
#define HIC_SIMMIAN_READ	0x0010
#define HIC_POWER_DOWN		0x0011
#define HIC_GET_SET_FILE_ADDR	0x0012
#define HIC_LOAD_SET_FILE	0x0013
#define HIC_MSG_CONFIG		0x0014
#define HIC_MSG_TEST		0x0015
/* FIMC-IS to HOST */
#define IHC_GET_SENSOR_NUM	0x1000
#define IHC_SET_SHOT_MARK	0x1001
/* parameter1: frame number */
/* parameter2: confidence level (smile 0~100) */
/* parameter3: confidence level (blink 0~100) */
#define IHC_SET_FACE_MARK	0x1002
/* parameter1: coordinate count */
/* parameter2: coordinate buffer address */
#define IHC_FRAME_DONE		0x1003
/* parameter1: frame start number */
/* parameter2: frame count */
#define IHC_AA_DONE		0x1004
#define IHC_NOT_READY		0x1005

#define IH_REPLY_DONE		0x2000
#define IH_REPLY_NOT_DONE	0x2001

enum fimc_is_scenario {
	IS_SC_PREVIEW_STILL,
	IS_SC_PREVIEW_VIDEO,
	IS_SC_CAPTURE_STILL,
	IS_SC_CAPTURE_VIDEO,
	IS_SC_MAX
};

enum fimc_is_sub_scenario {
	IS_SC_SUB_DEFAULT,
	IS_SC_SUB_PS_VTCALL,
	IS_SC_SUB_CS_VTCALL,
	IS_SC_SUB_PV_VTCALL,
	IS_SC_SUB_CV_VTCALL,
};

struct is_common_regs {
	u32 hicmd;
	u32 hic_sensorid;
	u32 hic_param[4];
	u32 reserved1[4];

	u32 ihcmd;
	u32 ihc_sensorid;
	u32 ihc_param[4];
	u32 reserved2[4];

	u32 isp_sensor_id;
	u32 isp_param[2];
	u32 reserved3[1];

	u32 scc_sensor_id;
	u32 scc_param[2];
	u32 reserved4[1];

	u32 dnr_sensor_id;
	u32 dnr_param[2];
	u32 reserved5[1];

	u32 scp_sensor_id;
	u32 scp_param[2];
	u32 reserved6[29];
} __packed;

struct is_mcuctl_reg {
	u32 mcuctl;
	u32 bboar;

	u32 intgr0;
	u32 intcr0;
	u32 intmr0;
	u32 intsr0;
	u32 intmsr0;

	u32 intgr1;
	u32 intcr1;
	u32 intmr1;
	u32 intsr1;
	u32 intmsr1;

	u32 intcr2;
	u32 intmr2;
	u32 intsr2;
	u32 intmsr2;

	u32 gpoctrl;
	u32 cpoenctlr;
	u32 gpictlr;

	u32 reserved[0xd];

	struct is_common_regs common;
} __packed;

#endif /* FIMC_IS_CMD_H_ */
