/* linux/drivers/media/video/samsung/fimc/fimc-ipc.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * Header file for Samsung IPC driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __FIMC_IPC_H
#define __FIMC_IPC_H __FILE__

#define IPC_NAME		"s3c-ipc"
#define IPC_CLK_NAME		"ipc"

#define OFF			0
#define ON			1

#define IN_SC_MAX_WIDTH		1024
#define IN_SC_MAX_HEIGHT	768

#define ipc_err(args...) do { printk(KERN_ERR  IPC_NAME ": " args); } while (0)

enum ipc_enoff {
	DISABLED,
	ENABLED
};

enum ipc_field_id {
	IPC_TOP_FIELD,
	IPC_BOTTOM_FIELD
};

enum ipc_field_id_sel {
	INTERNAL,
	CAM_FIELD_SIG
};

enum ipc_field_id_togl {
	BYUSER,
	AUTO
};

enum ipc_2d {
	IPC_HDS, /* Horizontal Double scaling */
	IPC_2D	/* 2D IPC */
};

enum scan_mode {
	PROGRESSIVE
};

enum ipc_sharpness {
	NO_EFFECT,
	MIN_EDGE,
	MODERATE_EDGE,
	MAX_EDGE
};

enum ipc_pp_lineeq_val {
	IPC_PP_LINEEQ_0 = 0,
	IPC_PP_LINEEQ_1,
	IPC_PP_LINEEQ_2,
	IPC_PP_LINEEQ_3,
	IPC_PP_LINEEQ_4,
	IPC_PP_LINEEQ_5,
	IPC_PP_LINEEQ_6,
	IPC_PP_LINEEQ_7,
	IPC_PP_LINEEQ_ALL
};

enum ipc_filter_h_pp {
	/* Don't change the order and the value */
	IPC_PP_H_NORMAL = 0,
	IPC_PP_H_8_9,		/* 720 to 640 */
	IPC_PP_H_1_2,
	IPC_PP_H_1_3,
	IPC_PP_H_1_4
};

enum ipc_filter_v_pp{
	/* Don't change the order and the value */
	IPC_PP_V_NORMAL = 0,
	IPC_PP_V_5_6,		/* PAL to NTSC */
	IPC_PP_V_3_4,
	IPC_PP_V_1_2,
	IPC_PP_V_1_3,
	IPC_PP_V_1_4
};

struct ipc_source{
	u32 srcstaddr;
	u32 imghsz;
	u32 imgvsz;
	u32 srcxpos;
	u32 srcypos;
	u32 srchsz;
	u32 srcvsz;
	u32 srcnumoffrm;
	u32 lastfrmbufidx;
};

struct ipc_destination {
	enum scan_mode scanmode;
	u32 orgdsthsz;
	u32 orgdstvsz;
	u32 dstxpos;
	u32 dstypos;
	u32 dsthsz;
	u32 dstvsz;
};

struct ipc_controlvariable {
	u32 modeval;
	u32 lineeqval;
	u32 scanconversionidx;
};

struct ipc_enhancingvariable {
	u32 contrast[8];
	u32 brightness[8];
	u32 saturation;
	enum ipc_sharpness sharpness;
	u32 thhnoise;
	u32 brightoffset;
};

struct ipc_control {
	char				name[16];
	void __iomem			*regs;
	struct clk			*clk;
	struct device			*dev;
	struct ipc_source 		src;
	struct ipc_destination 		dst;
	struct ipc_controlvariable 	control_var;
	struct ipc_enhancingvariable	enhance_var;
};

extern int ipc_init(u32 input_width, u32 input_height, enum ipc_2d ipc2d);
extern void ipc_start(void);
extern void ipc_stop(void);

#endif /* __FIMC_IPC_H */
