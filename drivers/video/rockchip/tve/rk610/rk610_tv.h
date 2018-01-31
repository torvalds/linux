/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _RK610_TV_H
#define _RK610_TV_H
#include <linux/display-sys.h>
#include <linux/rk_fb.h>
#include <mach/board.h>
#include <mach/gpio.h>
#ifdef CONFIG_ARCH_RK29
#include <mach/rk29_iomap.h>
#endif
#include "../screen/screen.h"
#include "../../rk29_fb.h"
#include <linux/mfd/rk610_core.h>

#define TVE_VFCR		0x00
	#define TVE_VFCR_ENABLE_SUBCARRIER_RESET	0 << 6
	#define TVE_VFCR_DISABLE_SUBCARRIER_RESET	1 << 6
	#define TVE_VFCR_VIN_RANGE_16_235			0 << 3
	#define TVE_VFCR_VIN_RANGE_1_254			1 << 3
	#define TVE_VFCR_BLACK_7_5_IRE				0 << 2
	#define TVE_VFCR_BLACK_0_IRE				1 << 2
	#define TVE_VFCR_NTSC						0
	#define TVE_VFCR_PAL_M						1
	#define TVE_VFCR_PAL_B_N					2
	#define TVE_VFCR_PAL_NC						3

#define TVE_VINCR		0x01
	#define TVE_VINCR_PIX_DATA_DELAY(n)			(n << 5)
	#define TVE_VINCR_H_SYNC_POLARITY_NEGTIVE	0 << 4
	#define TVE_VINCR_H_SYNC_POLARITY_POSITIVE	1 << 4
	#define TVE_VINCR_V_SYNC_POLARITY_NEGTIVE	0 << 3
	#define TVE_VINCR_V_SYNC_POLARITY_POSITIVE	1 << 3
enum {
	INPUT_FORMAT_BT601_SLAVE = 0,
	INPUT_FORMAT_BT656,
	INPUT_FORMAT_BT601_MASTER,
	INPUT_FORMAT_INTERNAL_COLLOR_BAR
};
	#define TVE_VINCR_INPUT_FORMAT(n)			(n << 1)
	#define TVE_VINCR_VSYNC_FUNCTION_VSYNC		0
	#define TVE_VINCR_VSYNC_FUNCTION_FIELD		1

#define TVE_VOUTCR		0x02
	#define TVE_VOUTCR_OUTPUT_CVBS				0 << 6
	#define TVE_VOUTCR_OUTPUT_YPBPR				1 << 6
	#define TVE_VOUTCR_OUTPUT_ENABLE_BLUE		1 << 5
	#define TVE_VOUTCR_OUTPUT_ENABLE_BLACK		1 << 4
	#define TVE_VOUTCR_DISABLE_CVBS_COLOR		1 << 3
	#define TVE_VOUTCR_CVBS_Y2C_DELAY(n)				(n << 0)

#define TVE_POWERCR		0x03
	#define TVE_PIX_CLK_INVERSE_ENABLE			1 << 4
	#define TVE_DAC_CLK_INVERSE_DISABLE			1 << 3
	#define TVE_DAC_Y_ENABLE					1 << 2
	#define TVE_DAC_U_ENABLE					1 << 1
	#define TVE_DAC_V_ENABLE					1 << 0

#define TVE_HDTVCR		0x05
	#define TVE_RESET							1 << 7
	#define TVE_FILTER(n)						(n << 5)
	#define TVE_COLOR_CONVERT_REC601			0 << 4
	#define TVE_COLOR_CONVERT_REC709			1 << 4
	#define TVE_INPUT_DATA_RGB					0 << 3
	#define TVE_INPUT_DATA_YUV					1 << 3
	#define TVE_OUTPUT_50HZ						0 << 2
	#define TVE_OUTPUT_60HZ						1 << 2
	#define TVE_OUTPUT_MODE_PAL_NTSC			0
	#define TVE_OUTPUT_MODE_576P				1
	#define TVE_OUTPUT_MODE_480P				2
	#define TVE_OUTPUT_MODE_720P				3

#define TVE_YADJCR		0x06
	#define TVE_OUTPUT_MODE_1080P				1 << 6
	#define TVE_OUTPUT_MODE_1080I				1 << 5
	#define TVE_Y_ADJ_VALUE(n)					n
#define TVE_YCBADJCR	0x07
#define TVE_YCRADJCR	0x08

/******************* TVOUT OUTPUT TYPE **********************/
struct rk610_monspecs {
	struct rk_display_device	*ddev;
	unsigned int				enable;
	struct fb_videomode			*mode;
	struct list_head			modelist;
	unsigned int 				mode_set;
};

enum {
	TVOUT_CVBS_NTSC = 1,
	TVOUT_CVBS_PAL,
	TVOUT_YPbPr_720x480p_60,
	TVOUT_YPbPr_720x576p_50,
	TVOUT_YPbPr_1280x720p_50,
	TVOUT_YPbPr_1280x720p_60,
	//TVOUT_YPbPr_1920x1080i_50,
	TVOUT_YPbPr_1920x1080i_60,
	TVOUT_YPbPr_1920x1080p_50,
	TVOUT_YPbPr_1920x1080p_60
};

#define RK610_TVOUT_DEAULT TVOUT_CVBS_NTSC

enum {
	RK610_TVOUT_CVBS = 0,
	RK610_TVOUT_YC,
	RK610_TVOUT_YPBPR,
};

extern volatile int rk610_tv_output_status;
extern struct rk_display_ops rk610_display_ops;

extern int FB_Switch_Screen( struct rk_screen *screen, u32 enable );

extern int rk610_tv_wirte_reg(u8 reg, u8 data);
extern int rk610_tv_standby(int type);
extern int rk610_switch_fb(const struct fb_videomode *modedb, int tv_mode);
extern int rk610_register_display(struct device *parent);

#ifdef CONFIG_RK610_TVOUT_YPbPr
extern int rk610_tv_ypbpr_init(void);
extern int rk610_register_display_ypbpr(struct device *parent);
extern struct rk610_monspecs rk610_ypbpr_monspecs;
#endif

#ifdef CONFIG_RK610_TVOUT_CVBS
extern int rk610_tv_cvbs_init(void);
extern int rk610_register_display_cvbs(struct device *parent);
extern struct rk610_monspecs rk610_cvbs_monspecs;
#endif

#endif

