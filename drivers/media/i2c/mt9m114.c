// SPDX-License-Identifier: GPL-2.0-only
/*
 * mt9m114.c onsemi MT9M114 sensor driver
 *
 * Copyright (c) 2020-2023 Laurent Pinchart <laurent.pinchart@ideasonboard.com>
 * Copyright (c) 2012 Analog Devices Inc.
 *
 * Almost complete rewrite of work by Scott Jiang <Scott.Jiang.Linux@gmail.com>
 * itself based on work from Andrew Chew <achew@nvidia.com>.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>
#include <linux/videodev2.h>

#include <media/v4l2-async.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-mediabus.h>
#include <media/v4l2-subdev.h>

/* Sysctl registers */
#define MT9M114_CHIP_ID					CCI_REG16(0x0000)
#define MT9M114_COMMAND_REGISTER			CCI_REG16(0x0080)
#define MT9M114_COMMAND_REGISTER_APPLY_PATCH			BIT(0)
#define MT9M114_COMMAND_REGISTER_SET_STATE			BIT(1)
#define MT9M114_COMMAND_REGISTER_REFRESH			BIT(2)
#define MT9M114_COMMAND_REGISTER_WAIT_FOR_EVENT			BIT(3)
#define MT9M114_COMMAND_REGISTER_OK				BIT(15)
#define MT9M114_RESET_AND_MISC_CONTROL			CCI_REG16(0x001a)
#define MT9M114_RESET_SOC					BIT(0)
#define MT9M114_PAD_SLEW				CCI_REG16(0x001e)
#define MT9M114_PAD_CONTROL				CCI_REG16(0x0032)

/* XDMA registers */
#define MT9M114_ACCESS_CTL_STAT				CCI_REG16(0x0982)
#define MT9M114_PHYSICAL_ADDRESS_ACCESS			CCI_REG16(0x098a)
#define MT9M114_LOGICAL_ADDRESS_ACCESS			CCI_REG16(0x098e)

/* Sensor Core registers */
#define MT9M114_COARSE_INTEGRATION_TIME			CCI_REG16(0x3012)
#define MT9M114_FINE_INTEGRATION_TIME			CCI_REG16(0x3014)
#define MT9M114_RESET_REGISTER				CCI_REG16(0x301a)
#define MT9M114_RESET_REGISTER_LOCK_REG				BIT(3)
#define MT9M114_RESET_REGISTER_MASK_BAD				BIT(9)
#define MT9M114_FLASH					CCI_REG16(0x3046)
#define MT9M114_GREEN1_GAIN				CCI_REG16(0x3056)
#define MT9M114_BLUE_GAIN				CCI_REG16(0x3058)
#define MT9M114_RED_GAIN				CCI_REG16(0x305a)
#define MT9M114_GREEN2_GAIN				CCI_REG16(0x305c)
#define MT9M114_GLOBAL_GAIN				CCI_REG16(0x305e)
#define MT9M114_GAIN_DIGITAL_GAIN(n)				((n) << 12)
#define MT9M114_GAIN_DIGITAL_GAIN_MASK				(0xf << 12)
#define MT9M114_GAIN_ANALOG_GAIN(n)				((n) << 0)
#define MT9M114_GAIN_ANALOG_GAIN_MASK				(0xff << 0)
#define MT9M114_CUSTOMER_REV				CCI_REG16(0x31fe)

/* Monitor registers */
#define MT9M114_MON_MAJOR_VERSION			CCI_REG16(0x8000)
#define MT9M114_MON_MINOR_VERSION			CCI_REG16(0x8002)
#define MT9M114_MON_RELEASE_VERSION			CCI_REG16(0x8004)

/* Auto-Exposure Track registers */
#define MT9M114_AE_TRACK_ALGO				CCI_REG16(0xa804)
#define MT9M114_AE_TRACK_EXEC_AUTOMATIC_EXPOSURE		BIT(0)
#define MT9M114_AE_TRACK_AE_TRACKING_DAMPENING_SPEED	CCI_REG8(0xa80a)

/* Color Correction Matrix registers */
#define MT9M114_CCM_ALGO				CCI_REG16(0xb404)
#define MT9M114_CCM_EXEC_CALC_CCM_MATRIX			BIT(4)
#define MT9M114_CCM_DELTA_GAIN				CCI_REG8(0xb42a)

/* Camera Control registers */
#define MT9M114_CAM_SENSOR_CFG_Y_ADDR_START		CCI_REG16(0xc800)
#define MT9M114_CAM_SENSOR_CFG_X_ADDR_START		CCI_REG16(0xc802)
#define MT9M114_CAM_SENSOR_CFG_Y_ADDR_END		CCI_REG16(0xc804)
#define MT9M114_CAM_SENSOR_CFG_X_ADDR_END		CCI_REG16(0xc806)
#define MT9M114_CAM_SENSOR_CFG_PIXCLK			CCI_REG32(0xc808)
#define MT9M114_CAM_SENSOR_CFG_ROW_SPEED		CCI_REG16(0xc80c)
#define MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN	CCI_REG16(0xc80e)
#define MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX	CCI_REG16(0xc810)
#define MT9M114_CAM_SENSOR_CFG_FRAME_LENGTH_LINES	CCI_REG16(0xc812)
#define MT9M114_CAM_SENSOR_CFG_FRAME_LENGTH_LINES_MAX		65535
#define MT9M114_CAM_SENSOR_CFG_LINE_LENGTH_PCK		CCI_REG16(0xc814)
#define MT9M114_CAM_SENSOR_CFG_LINE_LENGTH_PCK_MAX		8191
#define MT9M114_CAM_SENSOR_CFG_FINE_CORRECTION		CCI_REG16(0xc816)
#define MT9M114_CAM_SENSOR_CFG_CPIPE_LAST_ROW		CCI_REG16(0xc818)
#define MT9M114_CAM_SENSOR_CFG_REG_0_DATA		CCI_REG16(0xc826)
#define MT9M114_CAM_SENSOR_CONTROL_READ_MODE		CCI_REG16(0xc834)
#define MT9M114_CAM_SENSOR_CONTROL_HORZ_MIRROR_EN		BIT(0)
#define MT9M114_CAM_SENSOR_CONTROL_VERT_FLIP_EN			BIT(1)
#define MT9M114_CAM_SENSOR_CONTROL_X_READ_OUT_NORMAL		(0 << 4)
#define MT9M114_CAM_SENSOR_CONTROL_X_READ_OUT_SKIPPING		(1 << 4)
#define MT9M114_CAM_SENSOR_CONTROL_X_READ_OUT_AVERAGE		(2 << 4)
#define MT9M114_CAM_SENSOR_CONTROL_X_READ_OUT_SUMMING		(3 << 4)
#define MT9M114_CAM_SENSOR_CONTROL_X_READ_OUT_MASK		(3 << 4)
#define MT9M114_CAM_SENSOR_CONTROL_Y_READ_OUT_NORMAL		(0 << 8)
#define MT9M114_CAM_SENSOR_CONTROL_Y_READ_OUT_SKIPPING		(1 << 8)
#define MT9M114_CAM_SENSOR_CONTROL_Y_READ_OUT_SUMMING		(3 << 8)
#define MT9M114_CAM_SENSOR_CONTROL_Y_READ_OUT_MASK		(3 << 8)
#define MT9M114_CAM_SENSOR_CONTROL_ANALOG_GAIN		CCI_REG16(0xc836)
#define MT9M114_CAM_SENSOR_CONTROL_COARSE_INTEGRATION_TIME	CCI_REG16(0xc83c)
#define MT9M114_CAM_SENSOR_CONTROL_FINE_INTEGRATION_TIME	CCI_REG16(0xc83e)
#define MT9M114_CAM_MODE_SELECT				CCI_REG8(0xc84c)
#define MT9M114_CAM_MODE_SELECT_NORMAL				(0 << 0)
#define MT9M114_CAM_MODE_SELECT_LENS_CALIBRATION		(1 << 0)
#define MT9M114_CAM_MODE_SELECT_TEST_PATTERN			(2 << 0)
#define MT9M114_CAM_MODE_TEST_PATTERN_SELECT		CCI_REG8(0xc84d)
#define MT9M114_CAM_MODE_TEST_PATTERN_SELECT_SOLID		(1 << 0)
#define MT9M114_CAM_MODE_TEST_PATTERN_SELECT_SOLID_BARS		(4 << 0)
#define MT9M114_CAM_MODE_TEST_PATTERN_SELECT_RANDOM		(5 << 0)
#define MT9M114_CAM_MODE_TEST_PATTERN_SELECT_FADING_BARS	(8 << 0)
#define MT9M114_CAM_MODE_TEST_PATTERN_SELECT_WALKING_1S_10B	(10 << 0)
#define MT9M114_CAM_MODE_TEST_PATTERN_SELECT_WALKING_1S_8B	(11 << 0)
#define MT9M114_CAM_MODE_TEST_PATTERN_RED		CCI_REG16(0xc84e)
#define MT9M114_CAM_MODE_TEST_PATTERN_GREEN		CCI_REG16(0xc850)
#define MT9M114_CAM_MODE_TEST_PATTERN_BLUE		CCI_REG16(0xc852)
#define MT9M114_CAM_CROP_WINDOW_XOFFSET			CCI_REG16(0xc854)
#define MT9M114_CAM_CROP_WINDOW_YOFFSET			CCI_REG16(0xc856)
#define MT9M114_CAM_CROP_WINDOW_WIDTH			CCI_REG16(0xc858)
#define MT9M114_CAM_CROP_WINDOW_HEIGHT			CCI_REG16(0xc85a)
#define MT9M114_CAM_CROP_CROPMODE			CCI_REG8(0xc85c)
#define MT9M114_CAM_CROP_MODE_AE_AUTO_CROP_EN			BIT(0)
#define MT9M114_CAM_CROP_MODE_AWB_AUTO_CROP_EN			BIT(1)
#define MT9M114_CAM_OUTPUT_WIDTH			CCI_REG16(0xc868)
#define MT9M114_CAM_OUTPUT_HEIGHT			CCI_REG16(0xc86a)
#define MT9M114_CAM_OUTPUT_FORMAT			CCI_REG16(0xc86c)
#define MT9M114_CAM_OUTPUT_FORMAT_SWAP_RED_BLUE			BIT(0)
#define MT9M114_CAM_OUTPUT_FORMAT_SWAP_BYTES			BIT(1)
#define MT9M114_CAM_OUTPUT_FORMAT_MONO_ENABLE			BIT(2)
#define MT9M114_CAM_OUTPUT_FORMAT_BT656_ENABLE			BIT(3)
#define MT9M114_CAM_OUTPUT_FORMAT_BT656_CROP_SCALE_DISABLE	BIT(4)
#define MT9M114_CAM_OUTPUT_FORMAT_FVLV_DISABLE			BIT(5)
#define MT9M114_CAM_OUTPUT_FORMAT_FORMAT_YUV			(0 << 8)
#define MT9M114_CAM_OUTPUT_FORMAT_FORMAT_RGB			(1 << 8)
#define MT9M114_CAM_OUTPUT_FORMAT_FORMAT_BAYER			(2 << 8)
#define MT9M114_CAM_OUTPUT_FORMAT_FORMAT_NONE			(3 << 8)
#define MT9M114_CAM_OUTPUT_FORMAT_FORMAT_MASK			(3 << 8)
#define MT9M114_CAM_OUTPUT_FORMAT_BAYER_FORMAT_RAWR10		(0 << 10)
#define MT9M114_CAM_OUTPUT_FORMAT_BAYER_FORMAT_PRELSC_8_2	(1 << 10)
#define MT9M114_CAM_OUTPUT_FORMAT_BAYER_FORMAT_POSTLSC_8_2	(2 << 10)
#define MT9M114_CAM_OUTPUT_FORMAT_BAYER_FORMAT_PROCESSED8	(3 << 10)
#define MT9M114_CAM_OUTPUT_FORMAT_BAYER_FORMAT_MASK		(3 << 10)
#define MT9M114_CAM_OUTPUT_FORMAT_RGB_FORMAT_565RGB		(0 << 12)
#define MT9M114_CAM_OUTPUT_FORMAT_RGB_FORMAT_555RGB		(1 << 12)
#define MT9M114_CAM_OUTPUT_FORMAT_RGB_FORMAT_444xRGB		(2 << 12)
#define MT9M114_CAM_OUTPUT_FORMAT_RGB_FORMAT_444RGBx		(3 << 12)
#define MT9M114_CAM_OUTPUT_FORMAT_RGB_FORMAT_MASK		(3 << 12)
#define MT9M114_CAM_OUTPUT_FORMAT_YUV			CCI_REG16(0xc86e)
#define MT9M114_CAM_OUTPUT_FORMAT_YUV_CLIP			BIT(5)
#define MT9M114_CAM_OUTPUT_FORMAT_YUV_AUV_OFFSET		BIT(4)
#define MT9M114_CAM_OUTPUT_FORMAT_YUV_SELECT_601		BIT(3)
#define MT9M114_CAM_OUTPUT_FORMAT_YUV_NORMALISE			BIT(2)
#define MT9M114_CAM_OUTPUT_FORMAT_YUV_SAMPLING_EVEN_UV		(0 << 0)
#define MT9M114_CAM_OUTPUT_FORMAT_YUV_SAMPLING_ODD_UV		(1 << 0)
#define MT9M114_CAM_OUTPUT_FORMAT_YUV_SAMPLING_EVENU_ODDV	(2 << 0)
#define MT9M114_CAM_OUTPUT_Y_OFFSET			CCI_REG8(0xc870)
#define MT9M114_CAM_AET_AEMODE				CCI_REG8(0xc878)
#define MT9M114_CAM_AET_EXEC_SET_INDOOR				BIT(0)
#define MT9M114_CAM_AET_DISCRETE_FRAMERATE			BIT(1)
#define MT9M114_CAM_AET_ADAPTATIVE_TARGET_LUMA			BIT(2)
#define MT9M114_CAM_AET_ADAPTATIVE_SKIP_FRAMES			BIT(3)
#define MT9M114_CAM_AET_SKIP_FRAMES			CCI_REG8(0xc879)
#define MT9M114_CAM_AET_TARGET_AVERAGE_LUMA		CCI_REG8(0xc87a)
#define MT9M114_CAM_AET_TARGET_AVERAGE_LUMA_DARK	CCI_REG8(0xc87b)
#define MT9M114_CAM_AET_BLACK_CLIPPING_TARGET		CCI_REG16(0xc87c)
#define MT9M114_CAM_AET_AE_MIN_VIRT_INT_TIME_PCLK	CCI_REG16(0xc87e)
#define MT9M114_CAM_AET_AE_MIN_VIRT_DGAIN		CCI_REG16(0xc880)
#define MT9M114_CAM_AET_AE_MAX_VIRT_DGAIN		CCI_REG16(0xc882)
#define MT9M114_CAM_AET_AE_MIN_VIRT_AGAIN		CCI_REG16(0xc884)
#define MT9M114_CAM_AET_AE_MAX_VIRT_AGAIN		CCI_REG16(0xc886)
#define MT9M114_CAM_AET_AE_VIRT_GAIN_TH_EG		CCI_REG16(0xc888)
#define MT9M114_CAM_AET_AE_EG_GATE_PERCENTAGE		CCI_REG8(0xc88a)
#define MT9M114_CAM_AET_FLICKER_FREQ_HZ			CCI_REG8(0xc88b)
#define MT9M114_CAM_AET_MAX_FRAME_RATE			CCI_REG16(0xc88c)
#define MT9M114_CAM_AET_MIN_FRAME_RATE			CCI_REG16(0xc88e)
#define MT9M114_CAM_AET_TARGET_GAIN			CCI_REG16(0xc890)
#define MT9M114_CAM_AWB_CCM_L(n)			CCI_REG16(0xc892 + (n) * 2)
#define MT9M114_CAM_AWB_CCM_M(n)			CCI_REG16(0xc8a4 + (n) * 2)
#define MT9M114_CAM_AWB_CCM_R(n)			CCI_REG16(0xc8b6 + (n) * 2)
#define MT9M114_CAM_AWB_CCM_L_RG_GAIN			CCI_REG16(0xc8c8)
#define MT9M114_CAM_AWB_CCM_L_BG_GAIN			CCI_REG16(0xc8ca)
#define MT9M114_CAM_AWB_CCM_M_RG_GAIN			CCI_REG16(0xc8cc)
#define MT9M114_CAM_AWB_CCM_M_BG_GAIN			CCI_REG16(0xc8ce)
#define MT9M114_CAM_AWB_CCM_R_RG_GAIN			CCI_REG16(0xc8d0)
#define MT9M114_CAM_AWB_CCM_R_BG_GAIN			CCI_REG16(0xc8d2)
#define MT9M114_CAM_AWB_CCM_L_CTEMP			CCI_REG16(0xc8d4)
#define MT9M114_CAM_AWB_CCM_M_CTEMP			CCI_REG16(0xc8d6)
#define MT9M114_CAM_AWB_CCM_R_CTEMP			CCI_REG16(0xc8d8)
#define MT9M114_CAM_AWB_AWB_XSCALE			CCI_REG8(0xc8f2)
#define MT9M114_CAM_AWB_AWB_YSCALE			CCI_REG8(0xc8f3)
#define MT9M114_CAM_AWB_AWB_WEIGHTS(n)			CCI_REG16(0xc8f4 + (n) * 2)
#define MT9M114_CAM_AWB_AWB_XSHIFT_PRE_ADJ		CCI_REG16(0xc904)
#define MT9M114_CAM_AWB_AWB_YSHIFT_PRE_ADJ		CCI_REG16(0xc906)
#define MT9M114_CAM_AWB_AWBMODE				CCI_REG8(0xc909)
#define MT9M114_CAM_AWB_MODE_AUTO				BIT(1)
#define MT9M114_CAM_AWB_MODE_EXCLUSIVE_AE			BIT(0)
#define MT9M114_CAM_AWB_K_R_L				CCI_REG8(0xc90c)
#define MT9M114_CAM_AWB_K_G_L				CCI_REG8(0xc90d)
#define MT9M114_CAM_AWB_K_B_L				CCI_REG8(0xc90e)
#define MT9M114_CAM_AWB_K_R_R				CCI_REG8(0xc90f)
#define MT9M114_CAM_AWB_K_G_R				CCI_REG8(0xc910)
#define MT9M114_CAM_AWB_K_B_R				CCI_REG8(0xc911)
#define MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XSTART		CCI_REG16(0xc914)
#define MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YSTART		CCI_REG16(0xc916)
#define MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XEND		CCI_REG16(0xc918)
#define MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YEND		CCI_REG16(0xc91a)
#define MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XSTART	CCI_REG16(0xc91c)
#define MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YSTART	CCI_REG16(0xc91e)
#define MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XEND		CCI_REG16(0xc920)
#define MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YEND		CCI_REG16(0xc922)
#define MT9M114_CAM_LL_LLMODE				CCI_REG16(0xc924)
#define MT9M114_CAM_LL_START_BRIGHTNESS			CCI_REG16(0xc926)
#define MT9M114_CAM_LL_STOP_BRIGHTNESS			CCI_REG16(0xc928)
#define MT9M114_CAM_LL_START_SATURATION			CCI_REG8(0xc92a)
#define MT9M114_CAM_LL_END_SATURATION			CCI_REG8(0xc92b)
#define MT9M114_CAM_LL_START_DESATURATION		CCI_REG8(0xc92c)
#define MT9M114_CAM_LL_END_DESATURATION			CCI_REG8(0xc92d)
#define MT9M114_CAM_LL_START_DEMOSAICING		CCI_REG8(0xc92e)
#define MT9M114_CAM_LL_START_AP_GAIN			CCI_REG8(0xc92f)
#define MT9M114_CAM_LL_START_AP_THRESH			CCI_REG8(0xc930)
#define MT9M114_CAM_LL_STOP_DEMOSAICING			CCI_REG8(0xc931)
#define MT9M114_CAM_LL_STOP_AP_GAIN			CCI_REG8(0xc932)
#define MT9M114_CAM_LL_STOP_AP_THRESH			CCI_REG8(0xc933)
#define MT9M114_CAM_LL_START_NR_RED			CCI_REG8(0xc934)
#define MT9M114_CAM_LL_START_NR_GREEN			CCI_REG8(0xc935)
#define MT9M114_CAM_LL_START_NR_BLUE			CCI_REG8(0xc936)
#define MT9M114_CAM_LL_START_NR_THRESH			CCI_REG8(0xc937)
#define MT9M114_CAM_LL_STOP_NR_RED			CCI_REG8(0xc938)
#define MT9M114_CAM_LL_STOP_NR_GREEN			CCI_REG8(0xc939)
#define MT9M114_CAM_LL_STOP_NR_BLUE			CCI_REG8(0xc93a)
#define MT9M114_CAM_LL_STOP_NR_THRESH			CCI_REG8(0xc93b)
#define MT9M114_CAM_LL_START_CONTRAST_BM		CCI_REG16(0xc93c)
#define MT9M114_CAM_LL_STOP_CONTRAST_BM			CCI_REG16(0xc93e)
#define MT9M114_CAM_LL_GAMMA				CCI_REG16(0xc940)
#define MT9M114_CAM_LL_START_CONTRAST_GRADIENT		CCI_REG8(0xc942)
#define MT9M114_CAM_LL_STOP_CONTRAST_GRADIENT		CCI_REG8(0xc943)
#define MT9M114_CAM_LL_START_CONTRAST_LUMA_PERCENTAGE	CCI_REG8(0xc944)
#define MT9M114_CAM_LL_STOP_CONTRAST_LUMA_PERCENTAGE	CCI_REG8(0xc945)
#define MT9M114_CAM_LL_START_GAIN_METRIC		CCI_REG16(0xc946)
#define MT9M114_CAM_LL_STOP_GAIN_METRIC			CCI_REG16(0xc948)
#define MT9M114_CAM_LL_START_FADE_TO_BLACK_LUMA		CCI_REG16(0xc94a)
#define MT9M114_CAM_LL_STOP_FADE_TO_BLACK_LUMA		CCI_REG16(0xc94c)
#define MT9M114_CAM_LL_CLUSTER_DC_TH_BM			CCI_REG16(0xc94e)
#define MT9M114_CAM_LL_CLUSTER_DC_GATE_PERCENTAGE	CCI_REG8(0xc950)
#define MT9M114_CAM_LL_SUMMING_SENSITIVITY_FACTOR	CCI_REG8(0xc951)
#define MT9M114_CAM_LL_START_TARGET_LUMA_BM		CCI_REG16(0xc952)
#define MT9M114_CAM_LL_STOP_TARGET_LUMA_BM		CCI_REG16(0xc954)
#define MT9M114_CAM_PGA_PGA_CONTROL			CCI_REG16(0xc95e)
#define MT9M114_CAM_SYSCTL_PLL_ENABLE			CCI_REG8(0xc97e)
#define MT9M114_CAM_SYSCTL_PLL_ENABLE_VALUE			BIT(0)
#define MT9M114_CAM_SYSCTL_PLL_DIVIDER_M_N		CCI_REG16(0xc980)
#define MT9M114_CAM_SYSCTL_PLL_DIVIDER_VALUE(m, n)		(((n) << 8) | (m))
#define MT9M114_CAM_SYSCTL_PLL_DIVIDER_P		CCI_REG16(0xc982)
#define MT9M114_CAM_SYSCTL_PLL_DIVIDER_P_VALUE(p)		((p) << 8)
#define MT9M114_CAM_PORT_OUTPUT_CONTROL			CCI_REG16(0xc984)
#define MT9M114_CAM_PORT_PORT_SELECT_PARALLEL			(0 << 0)
#define MT9M114_CAM_PORT_PORT_SELECT_MIPI			(1 << 0)
#define MT9M114_CAM_PORT_CLOCK_SLOWDOWN				BIT(3)
#define MT9M114_CAM_PORT_TRUNCATE_RAW_BAYER			BIT(4)
#define MT9M114_CAM_PORT_PIXCLK_GATE				BIT(5)
#define MT9M114_CAM_PORT_CONT_MIPI_CLK				BIT(6)
#define MT9M114_CAM_PORT_CHAN_NUM(vc)				((vc) << 8)
#define MT9M114_CAM_PORT_MIPI_TIMING_T_HS_ZERO		CCI_REG16(0xc988)
#define MT9M114_CAM_PORT_MIPI_TIMING_T_HS_ZERO_VALUE(n)		((n) << 8)
#define MT9M114_CAM_PORT_MIPI_TIMING_T_HS_EXIT_TRAIL	CCI_REG16(0xc98a)
#define MT9M114_CAM_PORT_MIPI_TIMING_T_HS_EXIT_VALUE(n)		((n) << 8)
#define MT9M114_CAM_PORT_MIPI_TIMING_T_HS_TRAIL_VALUE(n)	((n) << 0)
#define MT9M114_CAM_PORT_MIPI_TIMING_T_CLK_POST_PRE	CCI_REG16(0xc98c)
#define MT9M114_CAM_PORT_MIPI_TIMING_T_CLK_POST_VALUE(n)	((n) << 8)
#define MT9M114_CAM_PORT_MIPI_TIMING_T_CLK_PRE_VALUE(n)		((n) << 0)
#define MT9M114_CAM_PORT_MIPI_TIMING_T_CLK_TRAIL_ZERO	CCI_REG16(0xc98e)
#define MT9M114_CAM_PORT_MIPI_TIMING_T_CLK_TRAIL_VALUE(n)	((n) << 8)
#define MT9M114_CAM_PORT_MIPI_TIMING_T_CLK_ZERO_VALUE(n)	((n) << 0)

/* System Manager registers */
#define MT9M114_SYSMGR_NEXT_STATE			CCI_REG8(0xdc00)
#define MT9M114_SYSMGR_CURRENT_STATE			CCI_REG8(0xdc01)
#define MT9M114_SYSMGR_CMD_STATUS			CCI_REG8(0xdc02)

/* Patch Loader registers */
#define MT9M114_PATCHLDR_LOADER_ADDRESS			CCI_REG16(0xe000)
#define MT9M114_PATCHLDR_PATCH_ID			CCI_REG16(0xe002)
#define MT9M114_PATCHLDR_FIRMWARE_ID			CCI_REG32(0xe004)
#define MT9M114_PATCHLDR_APPLY_STATUS			CCI_REG8(0xe008)
#define MT9M114_PATCHLDR_NUM_PATCHES			CCI_REG8(0xe009)
#define MT9M114_PATCHLDR_PATCH_ID_0			CCI_REG16(0xe00a)
#define MT9M114_PATCHLDR_PATCH_ID_1			CCI_REG16(0xe00c)
#define MT9M114_PATCHLDR_PATCH_ID_2			CCI_REG16(0xe00e)
#define MT9M114_PATCHLDR_PATCH_ID_3			CCI_REG16(0xe010)
#define MT9M114_PATCHLDR_PATCH_ID_4			CCI_REG16(0xe012)
#define MT9M114_PATCHLDR_PATCH_ID_5			CCI_REG16(0xe014)
#define MT9M114_PATCHLDR_PATCH_ID_6			CCI_REG16(0xe016)
#define MT9M114_PATCHLDR_PATCH_ID_7			CCI_REG16(0xe018)

/* SYS_STATE values (for SYSMGR_NEXT_STATE and SYSMGR_CURRENT_STATE) */
#define MT9M114_SYS_STATE_ENTER_CONFIG_CHANGE		0x28
#define MT9M114_SYS_STATE_STREAMING			0x31
#define MT9M114_SYS_STATE_START_STREAMING		0x34
#define MT9M114_SYS_STATE_ENTER_SUSPEND			0x40
#define MT9M114_SYS_STATE_SUSPENDED			0x41
#define MT9M114_SYS_STATE_ENTER_STANDBY			0x50
#define MT9M114_SYS_STATE_STANDBY			0x52
#define MT9M114_SYS_STATE_LEAVE_STANDBY			0x54

/* Result status of last SET_STATE comamnd */
#define MT9M114_SET_STATE_RESULT_ENOERR			0x00
#define MT9M114_SET_STATE_RESULT_EINVAL			0x0c
#define MT9M114_SET_STATE_RESULT_ENOSPC			0x0d

/*
 * The minimum amount of horizontal and vertical blanking is undocumented. The
 * minimum values that have been seen in register lists are 303 and 38, use
 * them.
 *
 * Set the default to achieve 1280x960 at 30fps.
 */
#define MT9M114_MIN_HBLANK				303
#define MT9M114_MIN_VBLANK				38
#define MT9M114_DEF_HBLANK				323
#define MT9M114_DEF_VBLANK				39

#define MT9M114_DEF_FRAME_RATE				30
#define MT9M114_MAX_FRAME_RATE				120

#define MT9M114_PIXEL_ARRAY_WIDTH			1296U
#define MT9M114_PIXEL_ARRAY_HEIGHT			976U

/*
 * These values are not well documented and are semi-arbitrary. The pixel array
 * minimum output size is 8 pixels larger than the minimum scaler cropped input
 * width to account for the demosaicing.
 */
#define MT9M114_PIXEL_ARRAY_MIN_OUTPUT_WIDTH		(32U + 8U)
#define MT9M114_PIXEL_ARRAY_MIN_OUTPUT_HEIGHT		(32U + 8U)
#define MT9M114_SCALER_CROPPED_INPUT_WIDTH		32U
#define MT9M114_SCALER_CROPPED_INPUT_HEIGHT		32U

/* Indices into the mt9m114.ifp.tpg array. */
#define MT9M114_TPG_PATTERN				0
#define MT9M114_TPG_RED					1
#define MT9M114_TPG_GREEN				2
#define MT9M114_TPG_BLUE				3

/* -----------------------------------------------------------------------------
 * Data Structures
 */

enum mt9m114_format_flag {
	MT9M114_FMT_FLAG_PARALLEL = BIT(0),
	MT9M114_FMT_FLAG_CSI2 = BIT(1),
};

struct mt9m114_format_info {
	u32 code;
	u32 output_format;
	u32 flags;
};

struct mt9m114 {
	struct i2c_client *client;
	struct regmap *regmap;

	struct clk *clk;
	struct gpio_desc *reset;
	struct regulator_bulk_data supplies[3];
	struct v4l2_fwnode_endpoint bus_cfg;

	struct {
		unsigned int m;
		unsigned int n;
		unsigned int p;
	} pll;

	unsigned int pixrate;
	bool streaming;

	/* Pixel Array */
	struct {
		struct v4l2_subdev sd;
		struct media_pad pad;

		struct v4l2_ctrl_handler hdl;
		struct v4l2_ctrl *exposure;
		struct v4l2_ctrl *gain;
		struct v4l2_ctrl *hblank;
		struct v4l2_ctrl *vblank;
	} pa;

	/* Image Flow Processor */
	struct {
		struct v4l2_subdev sd;
		struct media_pad pads[2];

		struct v4l2_ctrl_handler hdl;
		unsigned int frame_rate;

		struct v4l2_ctrl *tpg[4];
	} ifp;
};

/* -----------------------------------------------------------------------------
 * Formats
 */

static const struct mt9m114_format_info mt9m114_format_infos[] = {
	{
		/*
		 * The first two entries are used as defaults, for parallel and
		 * CSI-2 buses respectively. Keep them in that order.
		 */
		.code = MEDIA_BUS_FMT_UYVY8_2X8,
		.flags = MT9M114_FMT_FLAG_PARALLEL,
		.output_format = MT9M114_CAM_OUTPUT_FORMAT_FORMAT_YUV,
	}, {
		.code = MEDIA_BUS_FMT_UYVY8_1X16,
		.flags = MT9M114_FMT_FLAG_CSI2,
		.output_format = MT9M114_CAM_OUTPUT_FORMAT_FORMAT_YUV,
	}, {
		.code = MEDIA_BUS_FMT_YUYV8_2X8,
		.flags = MT9M114_FMT_FLAG_PARALLEL,
		.output_format = MT9M114_CAM_OUTPUT_FORMAT_FORMAT_YUV
			       | MT9M114_CAM_OUTPUT_FORMAT_SWAP_BYTES,
	}, {
		.code = MEDIA_BUS_FMT_YUYV8_1X16,
		.flags = MT9M114_FMT_FLAG_CSI2,
		.output_format = MT9M114_CAM_OUTPUT_FORMAT_FORMAT_YUV
			       | MT9M114_CAM_OUTPUT_FORMAT_SWAP_BYTES,
	}, {
		.code = MEDIA_BUS_FMT_RGB565_2X8_LE,
		.flags = MT9M114_FMT_FLAG_PARALLEL,
		.output_format = MT9M114_CAM_OUTPUT_FORMAT_RGB_FORMAT_565RGB
			       | MT9M114_CAM_OUTPUT_FORMAT_FORMAT_RGB
			       | MT9M114_CAM_OUTPUT_FORMAT_SWAP_BYTES,
	}, {
		.code = MEDIA_BUS_FMT_RGB565_2X8_BE,
		.flags = MT9M114_FMT_FLAG_PARALLEL,
		.output_format = MT9M114_CAM_OUTPUT_FORMAT_RGB_FORMAT_565RGB
			       | MT9M114_CAM_OUTPUT_FORMAT_FORMAT_RGB,
	}, {
		.code = MEDIA_BUS_FMT_RGB565_1X16,
		.flags = MT9M114_FMT_FLAG_CSI2,
		.output_format = MT9M114_CAM_OUTPUT_FORMAT_RGB_FORMAT_565RGB
			       | MT9M114_CAM_OUTPUT_FORMAT_FORMAT_RGB,
	}, {
		.code = MEDIA_BUS_FMT_SGRBG8_1X8,
		.output_format = MT9M114_CAM_OUTPUT_FORMAT_BAYER_FORMAT_PROCESSED8
			       | MT9M114_CAM_OUTPUT_FORMAT_FORMAT_BAYER,
		.flags = MT9M114_FMT_FLAG_PARALLEL | MT9M114_FMT_FLAG_CSI2,
	}, {
		/* Keep the format compatible with the IFP sink pad last. */
		.code = MEDIA_BUS_FMT_SGRBG10_1X10,
		.output_format = MT9M114_CAM_OUTPUT_FORMAT_BAYER_FORMAT_RAWR10
			| MT9M114_CAM_OUTPUT_FORMAT_FORMAT_BAYER,
		.flags = MT9M114_FMT_FLAG_PARALLEL | MT9M114_FMT_FLAG_CSI2,
	}
};

static const struct mt9m114_format_info *
mt9m114_default_format_info(struct mt9m114 *sensor)
{
	if (sensor->bus_cfg.bus_type == V4L2_MBUS_CSI2_DPHY)
		return &mt9m114_format_infos[1];
	else
		return &mt9m114_format_infos[0];
}

static const struct mt9m114_format_info *
mt9m114_format_info(struct mt9m114 *sensor, unsigned int pad, u32 code)
{
	const unsigned int num_formats = ARRAY_SIZE(mt9m114_format_infos);
	unsigned int flag;
	unsigned int i;

	switch (pad) {
	case 0:
		return &mt9m114_format_infos[num_formats - 1];

	case 1:
		if (sensor->bus_cfg.bus_type == V4L2_MBUS_CSI2_DPHY)
			flag = MT9M114_FMT_FLAG_CSI2;
		else
			flag = MT9M114_FMT_FLAG_PARALLEL;

		for (i = 0; i < num_formats; ++i) {
			const struct mt9m114_format_info *info =
				&mt9m114_format_infos[i];

			if (info->code == code && info->flags & flag)
				return info;
		}

		return mt9m114_default_format_info(sensor);

	default:
		return NULL;
	}
}

/* -----------------------------------------------------------------------------
 * Initialization
 */

static const struct cci_reg_sequence mt9m114_init[] = {
	{ MT9M114_RESET_REGISTER, MT9M114_RESET_REGISTER_MASK_BAD |
				  MT9M114_RESET_REGISTER_LOCK_REG |
				  0x0010 },

	/* Sensor optimization */
	{ CCI_REG16(0x316a), 0x8270 },
	{ CCI_REG16(0x316c), 0x8270 },
	{ CCI_REG16(0x3ed0), 0x2305 },
	{ CCI_REG16(0x3ed2), 0x77cf },
	{ CCI_REG16(0x316e), 0x8202 },
	{ CCI_REG16(0x3180), 0x87ff },
	{ CCI_REG16(0x30d4), 0x6080 },
	{ CCI_REG16(0xa802), 0x0008 },

	{ CCI_REG16(0x3e14), 0xff39 },

	/* APGA */
	{ MT9M114_CAM_PGA_PGA_CONTROL,			0x0000 },

	/* Automatic White balance */
	{ MT9M114_CAM_AWB_CCM_L(0),			0x0267 },
	{ MT9M114_CAM_AWB_CCM_L(1),			0xff1a },
	{ MT9M114_CAM_AWB_CCM_L(2),			0xffb3 },
	{ MT9M114_CAM_AWB_CCM_L(3),			0xff80 },
	{ MT9M114_CAM_AWB_CCM_L(4),			0x0166 },
	{ MT9M114_CAM_AWB_CCM_L(5),			0x0003 },
	{ MT9M114_CAM_AWB_CCM_L(6),			0xff9a },
	{ MT9M114_CAM_AWB_CCM_L(7),			0xfeb4 },
	{ MT9M114_CAM_AWB_CCM_L(8),			0x024d },
	{ MT9M114_CAM_AWB_CCM_M(0),			0x01bf },
	{ MT9M114_CAM_AWB_CCM_M(1),			0xff01 },
	{ MT9M114_CAM_AWB_CCM_M(2),			0xfff3 },
	{ MT9M114_CAM_AWB_CCM_M(3),			0xff75 },
	{ MT9M114_CAM_AWB_CCM_M(4),			0x0198 },
	{ MT9M114_CAM_AWB_CCM_M(5),			0xfffd },
	{ MT9M114_CAM_AWB_CCM_M(6),			0xff9a },
	{ MT9M114_CAM_AWB_CCM_M(7),			0xfee7 },
	{ MT9M114_CAM_AWB_CCM_M(8),			0x02a8 },
	{ MT9M114_CAM_AWB_CCM_R(0),			0x01d9 },
	{ MT9M114_CAM_AWB_CCM_R(1),			0xff26 },
	{ MT9M114_CAM_AWB_CCM_R(2),			0xfff3 },
	{ MT9M114_CAM_AWB_CCM_R(3),			0xffb3 },
	{ MT9M114_CAM_AWB_CCM_R(4),			0x0132 },
	{ MT9M114_CAM_AWB_CCM_R(5),			0xffe8 },
	{ MT9M114_CAM_AWB_CCM_R(6),			0xffda },
	{ MT9M114_CAM_AWB_CCM_R(7),			0xfecd },
	{ MT9M114_CAM_AWB_CCM_R(8),			0x02c2 },
	{ MT9M114_CAM_AWB_CCM_L_RG_GAIN,		0x0075 },
	{ MT9M114_CAM_AWB_CCM_L_BG_GAIN,		0x011c },
	{ MT9M114_CAM_AWB_CCM_M_RG_GAIN,		0x009a },
	{ MT9M114_CAM_AWB_CCM_M_BG_GAIN,		0x0105 },
	{ MT9M114_CAM_AWB_CCM_R_RG_GAIN,		0x00a4 },
	{ MT9M114_CAM_AWB_CCM_R_BG_GAIN,		0x00ac },
	{ MT9M114_CAM_AWB_CCM_L_CTEMP,			0x0a8c },
	{ MT9M114_CAM_AWB_CCM_M_CTEMP,			0x0f0a },
	{ MT9M114_CAM_AWB_CCM_R_CTEMP,			0x1964 },
	{ MT9M114_CAM_AWB_AWB_XSHIFT_PRE_ADJ,		51 },
	{ MT9M114_CAM_AWB_AWB_YSHIFT_PRE_ADJ,		60 },
	{ MT9M114_CAM_AWB_AWB_XSCALE,			3 },
	{ MT9M114_CAM_AWB_AWB_YSCALE,			2 },
	{ MT9M114_CAM_AWB_AWB_WEIGHTS(0),		0x0000 },
	{ MT9M114_CAM_AWB_AWB_WEIGHTS(1),		0x0000 },
	{ MT9M114_CAM_AWB_AWB_WEIGHTS(2),		0x0000 },
	{ MT9M114_CAM_AWB_AWB_WEIGHTS(3),		0xe724 },
	{ MT9M114_CAM_AWB_AWB_WEIGHTS(4),		0x1583 },
	{ MT9M114_CAM_AWB_AWB_WEIGHTS(5),		0x2045 },
	{ MT9M114_CAM_AWB_AWB_WEIGHTS(6),		0x03ff },
	{ MT9M114_CAM_AWB_AWB_WEIGHTS(7),		0x007c },
	{ MT9M114_CAM_AWB_K_R_L,			0x80 },
	{ MT9M114_CAM_AWB_K_G_L,			0x80 },
	{ MT9M114_CAM_AWB_K_B_L,			0x80 },
	{ MT9M114_CAM_AWB_K_R_R,			0x88 },
	{ MT9M114_CAM_AWB_K_G_R,			0x80 },
	{ MT9M114_CAM_AWB_K_B_R,			0x80 },

	/* Low-Light Image Enhancements */
	{ MT9M114_CAM_LL_START_BRIGHTNESS,		0x0020 },
	{ MT9M114_CAM_LL_STOP_BRIGHTNESS,		0x009a },
	{ MT9M114_CAM_LL_START_GAIN_METRIC,		0x0070 },
	{ MT9M114_CAM_LL_STOP_GAIN_METRIC,		0x00f3 },
	{ MT9M114_CAM_LL_START_CONTRAST_LUMA_PERCENTAGE, 0x20 },
	{ MT9M114_CAM_LL_STOP_CONTRAST_LUMA_PERCENTAGE,	0x9a },
	{ MT9M114_CAM_LL_START_SATURATION,		0x80 },
	{ MT9M114_CAM_LL_END_SATURATION,		0x4b },
	{ MT9M114_CAM_LL_START_DESATURATION,		0x00 },
	{ MT9M114_CAM_LL_END_DESATURATION,		0xff },
	{ MT9M114_CAM_LL_START_DEMOSAICING,		0x3c },
	{ MT9M114_CAM_LL_START_AP_GAIN,			0x02 },
	{ MT9M114_CAM_LL_START_AP_THRESH,		0x06 },
	{ MT9M114_CAM_LL_STOP_DEMOSAICING,		0x64 },
	{ MT9M114_CAM_LL_STOP_AP_GAIN,			0x01 },
	{ MT9M114_CAM_LL_STOP_AP_THRESH,		0x0c },
	{ MT9M114_CAM_LL_START_NR_RED,			0x3c },
	{ MT9M114_CAM_LL_START_NR_GREEN,		0x3c },
	{ MT9M114_CAM_LL_START_NR_BLUE,			0x3c },
	{ MT9M114_CAM_LL_START_NR_THRESH,		0x0f },
	{ MT9M114_CAM_LL_STOP_NR_RED,			0x64 },
	{ MT9M114_CAM_LL_STOP_NR_GREEN,			0x64 },
	{ MT9M114_CAM_LL_STOP_NR_BLUE,			0x64 },
	{ MT9M114_CAM_LL_STOP_NR_THRESH,		0x32 },
	{ MT9M114_CAM_LL_START_CONTRAST_BM,		0x0020 },
	{ MT9M114_CAM_LL_STOP_CONTRAST_BM,		0x009a },
	{ MT9M114_CAM_LL_GAMMA,				0x00dc },
	{ MT9M114_CAM_LL_START_CONTRAST_GRADIENT,	0x38 },
	{ MT9M114_CAM_LL_STOP_CONTRAST_GRADIENT,	0x30 },
	{ MT9M114_CAM_LL_START_CONTRAST_LUMA_PERCENTAGE, 0x50 },
	{ MT9M114_CAM_LL_STOP_CONTRAST_LUMA_PERCENTAGE,	0x19 },
	{ MT9M114_CAM_LL_START_FADE_TO_BLACK_LUMA,	0x0230 },
	{ MT9M114_CAM_LL_STOP_FADE_TO_BLACK_LUMA,	0x0010 },
	{ MT9M114_CAM_LL_CLUSTER_DC_TH_BM,		0x01cd },
	{ MT9M114_CAM_LL_CLUSTER_DC_GATE_PERCENTAGE,	0x05 },
	{ MT9M114_CAM_LL_SUMMING_SENSITIVITY_FACTOR,	0x40 },

	/* Auto-Exposure */
	{ MT9M114_CAM_AET_TARGET_AVERAGE_LUMA_DARK,	0x1b },
	{ MT9M114_CAM_AET_AEMODE,			0x00 },
	{ MT9M114_CAM_AET_TARGET_GAIN,			0x0080 },
	{ MT9M114_CAM_AET_AE_MAX_VIRT_AGAIN,		0x0100 },
	{ MT9M114_CAM_AET_BLACK_CLIPPING_TARGET,	0x005a },

	{ MT9M114_CCM_DELTA_GAIN,			0x05 },
	{ MT9M114_AE_TRACK_AE_TRACKING_DAMPENING_SPEED,	0x20 },

	/* Pixel array timings and integration time */
	{ MT9M114_CAM_SENSOR_CFG_ROW_SPEED,		1 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MIN,	219 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_INTEG_TIME_MAX,	1459 },
	{ MT9M114_CAM_SENSOR_CFG_FINE_CORRECTION,	96 },
	{ MT9M114_CAM_SENSOR_CFG_REG_0_DATA,		32 },

	/* Miscellaneous settings */
	{ MT9M114_PAD_SLEW,				0x0777 },
};

/* -----------------------------------------------------------------------------
 * Hardware Configuration
 */

/* Wait for a command to complete. */
static int mt9m114_poll_command(struct mt9m114 *sensor, u32 command)
{
	unsigned int i;
	u64 value;
	int ret;

	for (i = 0; i < 100; ++i) {
		ret = cci_read(sensor->regmap, MT9M114_COMMAND_REGISTER, &value,
			       NULL);
		if (ret < 0)
			return ret;

		if (!(value & command))
			break;

		usleep_range(5000, 6000);
	}

	if (value & command) {
		dev_err(&sensor->client->dev, "Command %u completion timeout\n",
			command);
		return -ETIMEDOUT;
	}

	if (!(value & MT9M114_COMMAND_REGISTER_OK)) {
		dev_err(&sensor->client->dev, "Command %u failed\n", command);
		return -EIO;
	}

	return 0;
}

/* Wait for a state to be entered. */
static int mt9m114_poll_state(struct mt9m114 *sensor, u32 state)
{
	unsigned int i;
	u64 value;
	int ret;

	for (i = 0; i < 100; ++i) {
		ret = cci_read(sensor->regmap, MT9M114_SYSMGR_CURRENT_STATE,
			       &value, NULL);
		if (ret < 0)
			return ret;

		if (value == state)
			return 0;

		usleep_range(1000, 1500);
	}

	dev_err(&sensor->client->dev, "Timeout waiting for state 0x%02x\n",
		state);
	return -ETIMEDOUT;
}

static int mt9m114_set_state(struct mt9m114 *sensor, u8 next_state)
{
	int ret = 0;

	/* Set the next desired state and start the state transition. */
	cci_write(sensor->regmap, MT9M114_SYSMGR_NEXT_STATE, next_state, &ret);
	cci_write(sensor->regmap, MT9M114_COMMAND_REGISTER,
		  MT9M114_COMMAND_REGISTER_OK |
		  MT9M114_COMMAND_REGISTER_SET_STATE, &ret);
	if (ret < 0)
		return ret;

	/* Wait for the state transition to complete. */
	ret = mt9m114_poll_command(sensor, MT9M114_COMMAND_REGISTER_SET_STATE);
	if (ret < 0)
		return ret;

	return 0;
}

static int mt9m114_initialize(struct mt9m114 *sensor)
{
	u32 value;
	int ret;

	ret = cci_multi_reg_write(sensor->regmap, mt9m114_init,
				  ARRAY_SIZE(mt9m114_init), NULL);
	if (ret < 0) {
		dev_err(&sensor->client->dev,
			"Failed to initialize the sensor\n");
		return ret;
	}

	/* Configure the PLL. */
	cci_write(sensor->regmap, MT9M114_CAM_SYSCTL_PLL_ENABLE,
		  MT9M114_CAM_SYSCTL_PLL_ENABLE_VALUE, &ret);
	cci_write(sensor->regmap, MT9M114_CAM_SYSCTL_PLL_DIVIDER_M_N,
		  MT9M114_CAM_SYSCTL_PLL_DIVIDER_VALUE(sensor->pll.m,
						       sensor->pll.n),
		  &ret);
	cci_write(sensor->regmap, MT9M114_CAM_SYSCTL_PLL_DIVIDER_P,
		  MT9M114_CAM_SYSCTL_PLL_DIVIDER_P_VALUE(sensor->pll.p), &ret);
	cci_write(sensor->regmap, MT9M114_CAM_SENSOR_CFG_PIXCLK,
		  sensor->pixrate, &ret);

	/* Configure the output mode. */
	if (sensor->bus_cfg.bus_type == V4L2_MBUS_CSI2_DPHY) {
		value = MT9M114_CAM_PORT_PORT_SELECT_MIPI
		      | MT9M114_CAM_PORT_CHAN_NUM(0)
		      | 0x8000;
		if (!(sensor->bus_cfg.bus.mipi_csi2.flags &
		      V4L2_MBUS_CSI2_NONCONTINUOUS_CLOCK))
			value |= MT9M114_CAM_PORT_CONT_MIPI_CLK;
	} else {
		value = MT9M114_CAM_PORT_PORT_SELECT_PARALLEL
		      | 0x8000;
	}
	cci_write(sensor->regmap, MT9M114_CAM_PORT_OUTPUT_CONTROL, value, &ret);
	if (ret < 0)
		return ret;

	ret = mt9m114_set_state(sensor, MT9M114_SYS_STATE_ENTER_CONFIG_CHANGE);
	if (ret < 0)
		return ret;

	ret = mt9m114_set_state(sensor, MT9M114_SYS_STATE_ENTER_SUSPEND);
	if (ret < 0)
		return ret;

	return 0;
}

static int mt9m114_configure(struct mt9m114 *sensor,
			     struct v4l2_subdev_state *pa_state,
			     struct v4l2_subdev_state *ifp_state)
{
	const struct v4l2_mbus_framefmt *pa_format;
	const struct v4l2_rect *pa_crop;
	const struct mt9m114_format_info *ifp_info;
	const struct v4l2_mbus_framefmt *ifp_format;
	const struct v4l2_rect *ifp_crop;
	const struct v4l2_rect *ifp_compose;
	unsigned int hratio, vratio;
	u64 output_format;
	u64 read_mode;
	int ret = 0;

	pa_format = v4l2_subdev_get_pad_format(&sensor->pa.sd, pa_state, 0);
	pa_crop = v4l2_subdev_get_pad_crop(&sensor->pa.sd, pa_state, 0);

	ifp_format = v4l2_subdev_get_pad_format(&sensor->ifp.sd, ifp_state, 1);
	ifp_info = mt9m114_format_info(sensor, 1, ifp_format->code);
	ifp_crop = v4l2_subdev_get_pad_crop(&sensor->ifp.sd, ifp_state, 0);
	ifp_compose = v4l2_subdev_get_pad_compose(&sensor->ifp.sd, ifp_state, 0);

	ret = cci_read(sensor->regmap, MT9M114_CAM_SENSOR_CONTROL_READ_MODE,
		       &read_mode, NULL);
	if (ret < 0)
		return ret;

	ret = cci_read(sensor->regmap, MT9M114_CAM_OUTPUT_FORMAT,
		       &output_format, NULL);
	if (ret < 0)
		return ret;

	hratio = pa_crop->width / pa_format->width;
	vratio = pa_crop->height / pa_format->height;

	/*
	 * Pixel array crop and binning. The CAM_SENSOR_CFG_CPIPE_LAST_ROW
	 * register isn't clearly documented, but is always set to the number
	 * of active rows minus 4 divided by the vertical binning factor in all
	 * example sensor modes.
	 */
	cci_write(sensor->regmap, MT9M114_CAM_SENSOR_CFG_X_ADDR_START,
		  pa_crop->left, &ret);
	cci_write(sensor->regmap, MT9M114_CAM_SENSOR_CFG_Y_ADDR_START,
		  pa_crop->top, &ret);
	cci_write(sensor->regmap, MT9M114_CAM_SENSOR_CFG_X_ADDR_END,
		  pa_crop->width + pa_crop->left - 1, &ret);
	cci_write(sensor->regmap, MT9M114_CAM_SENSOR_CFG_Y_ADDR_END,
		  pa_crop->height + pa_crop->top - 1, &ret);
	cci_write(sensor->regmap, MT9M114_CAM_SENSOR_CFG_CPIPE_LAST_ROW,
		  (pa_crop->height - 4) / vratio - 1, &ret);

	read_mode &= ~(MT9M114_CAM_SENSOR_CONTROL_X_READ_OUT_MASK |
		       MT9M114_CAM_SENSOR_CONTROL_Y_READ_OUT_MASK);

	if (hratio > 1)
		read_mode |= MT9M114_CAM_SENSOR_CONTROL_X_READ_OUT_SUMMING;
	if (vratio > 1)
		read_mode |= MT9M114_CAM_SENSOR_CONTROL_Y_READ_OUT_SUMMING;

	cci_write(sensor->regmap, MT9M114_CAM_SENSOR_CONTROL_READ_MODE,
		  read_mode, &ret);

	/*
	 * Color pipeline (IFP) cropping and scaling. Subtract 4 from the left
	 * and top coordinates to compensate for the lines and columns removed
	 * by demosaicing that are taken into account in the crop rectangle but
	 * not in the hardware.
	 */
	cci_write(sensor->regmap, MT9M114_CAM_CROP_WINDOW_XOFFSET,
		  ifp_crop->left - 4, &ret);
	cci_write(sensor->regmap, MT9M114_CAM_CROP_WINDOW_YOFFSET,
		  ifp_crop->top - 4, &ret);
	cci_write(sensor->regmap, MT9M114_CAM_CROP_WINDOW_WIDTH,
		  ifp_crop->width, &ret);
	cci_write(sensor->regmap, MT9M114_CAM_CROP_WINDOW_HEIGHT,
		  ifp_crop->height, &ret);

	cci_write(sensor->regmap, MT9M114_CAM_OUTPUT_WIDTH,
		  ifp_compose->width, &ret);
	cci_write(sensor->regmap, MT9M114_CAM_OUTPUT_HEIGHT,
		  ifp_compose->height, &ret);

	/* AWB and AE windows, use the full frame. */
	cci_write(sensor->regmap, MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XSTART,
		  0, &ret);
	cci_write(sensor->regmap, MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YSTART,
		  0, &ret);
	cci_write(sensor->regmap, MT9M114_CAM_STAT_AWB_CLIP_WINDOW_XEND,
		  ifp_compose->width - 1, &ret);
	cci_write(sensor->regmap, MT9M114_CAM_STAT_AWB_CLIP_WINDOW_YEND,
		  ifp_compose->height - 1, &ret);

	cci_write(sensor->regmap, MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XSTART,
		  0, &ret);
	cci_write(sensor->regmap, MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YSTART,
		  0, &ret);
	cci_write(sensor->regmap, MT9M114_CAM_STAT_AE_INITIAL_WINDOW_XEND,
		  ifp_compose->width / 5 - 1, &ret);
	cci_write(sensor->regmap, MT9M114_CAM_STAT_AE_INITIAL_WINDOW_YEND,
		  ifp_compose->height / 5 - 1, &ret);

	cci_write(sensor->regmap, MT9M114_CAM_CROP_CROPMODE,
		  MT9M114_CAM_CROP_MODE_AWB_AUTO_CROP_EN |
		  MT9M114_CAM_CROP_MODE_AE_AUTO_CROP_EN, &ret);

	/* Set the media bus code. */
	output_format &= ~(MT9M114_CAM_OUTPUT_FORMAT_RGB_FORMAT_MASK |
			   MT9M114_CAM_OUTPUT_FORMAT_BAYER_FORMAT_MASK |
			   MT9M114_CAM_OUTPUT_FORMAT_FORMAT_MASK |
			   MT9M114_CAM_OUTPUT_FORMAT_SWAP_BYTES |
			   MT9M114_CAM_OUTPUT_FORMAT_SWAP_RED_BLUE);
	output_format |= ifp_info->output_format;

	cci_write(sensor->regmap, MT9M114_CAM_OUTPUT_FORMAT,
		  output_format, &ret);

	return ret;
}

static int mt9m114_set_frame_rate(struct mt9m114 *sensor)
{
	u16 frame_rate = sensor->ifp.frame_rate << 8;
	int ret = 0;

	cci_write(sensor->regmap, MT9M114_CAM_AET_MIN_FRAME_RATE,
		  frame_rate, &ret);
	cci_write(sensor->regmap, MT9M114_CAM_AET_MAX_FRAME_RATE,
		  frame_rate, &ret);

	return ret;
}

static int mt9m114_start_streaming(struct mt9m114 *sensor,
				   struct v4l2_subdev_state *pa_state,
				   struct v4l2_subdev_state *ifp_state)
{
	int ret;

	ret = pm_runtime_resume_and_get(&sensor->client->dev);
	if (ret)
		return ret;

	ret = mt9m114_configure(sensor, pa_state, ifp_state);
	if (ret)
		goto error;

	ret = mt9m114_set_frame_rate(sensor);
	if (ret)
		goto error;

	ret = __v4l2_ctrl_handler_setup(&sensor->pa.hdl);
	if (ret)
		goto error;

	ret = __v4l2_ctrl_handler_setup(&sensor->ifp.hdl);
	if (ret)
		goto error;

	/*
	 * The Change-Config state is transient and moves to the streaming
	 * state automatically.
	 */
	ret = mt9m114_set_state(sensor, MT9M114_SYS_STATE_ENTER_CONFIG_CHANGE);
	if (ret)
		goto error;

	sensor->streaming = true;

	return 0;

error:
	pm_runtime_mark_last_busy(&sensor->client->dev);
	pm_runtime_put_autosuspend(&sensor->client->dev);

	return ret;
}

static int mt9m114_stop_streaming(struct mt9m114 *sensor)
{
	int ret;

	sensor->streaming = false;

	ret = mt9m114_set_state(sensor, MT9M114_SYS_STATE_ENTER_SUSPEND);

	pm_runtime_mark_last_busy(&sensor->client->dev);
	pm_runtime_put_autosuspend(&sensor->client->dev);

	return ret;
}

/* -----------------------------------------------------------------------------
 * Common Subdev Operations
 */

static const struct media_entity_operations mt9m114_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

/* -----------------------------------------------------------------------------
 * Pixel Array Control Operations
 */

static inline struct mt9m114 *pa_ctrl_to_mt9m114(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct mt9m114, pa.hdl);
}

static int mt9m114_pa_g_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mt9m114 *sensor = pa_ctrl_to_mt9m114(ctrl);
	u64 value;
	int ret;

	if (!pm_runtime_get_if_in_use(&sensor->client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		ret = cci_read(sensor->regmap,
			       MT9M114_CAM_SENSOR_CONTROL_COARSE_INTEGRATION_TIME,
			       &value, NULL);
		if (ret)
			break;

		ctrl->val = value;
		break;

	case V4L2_CID_ANALOGUE_GAIN:
		ret = cci_read(sensor->regmap,
			       MT9M114_CAM_SENSOR_CONTROL_ANALOG_GAIN,
			       &value, NULL);
		if (ret)
			break;

		ctrl->val = value;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_mark_last_busy(&sensor->client->dev);
	pm_runtime_put_autosuspend(&sensor->client->dev);

	return ret;
}

static int mt9m114_pa_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mt9m114 *sensor = pa_ctrl_to_mt9m114(ctrl);
	const struct v4l2_mbus_framefmt *format;
	struct v4l2_subdev_state *state;
	int ret = 0;
	u64 mask;

	/* V4L2 controls values are applied only when power is up. */
	if (!pm_runtime_get_if_in_use(&sensor->client->dev))
		return 0;

	state = v4l2_subdev_get_locked_active_state(&sensor->pa.sd);
	format = v4l2_subdev_get_pad_format(&sensor->pa.sd, state, 0);

	switch (ctrl->id) {
	case V4L2_CID_HBLANK:
		cci_write(sensor->regmap, MT9M114_CAM_SENSOR_CFG_LINE_LENGTH_PCK,
			  ctrl->val + format->width, &ret);
		break;

	case V4L2_CID_VBLANK:
		cci_write(sensor->regmap, MT9M114_CAM_SENSOR_CFG_FRAME_LENGTH_LINES,
			  ctrl->val + format->height, &ret);
		break;

	case V4L2_CID_EXPOSURE:
		cci_write(sensor->regmap,
			  MT9M114_CAM_SENSOR_CONTROL_COARSE_INTEGRATION_TIME,
			  ctrl->val, &ret);
		break;

	case V4L2_CID_ANALOGUE_GAIN:
		/*
		 * The CAM_SENSOR_CONTROL_ANALOG_GAIN contains linear analog
		 * gain values that are mapped to the GLOBAL_GAIN register
		 * values by the sensor firmware.
		 */
		cci_write(sensor->regmap, MT9M114_CAM_SENSOR_CONTROL_ANALOG_GAIN,
			  ctrl->val, &ret);
		break;

	case V4L2_CID_HFLIP:
		mask = MT9M114_CAM_SENSOR_CONTROL_HORZ_MIRROR_EN;
		ret = cci_update_bits(sensor->regmap,
				      MT9M114_CAM_SENSOR_CONTROL_READ_MODE,
				      mask, ctrl->val ? mask : 0, NULL);
		break;

	case V4L2_CID_VFLIP:
		mask = MT9M114_CAM_SENSOR_CONTROL_VERT_FLIP_EN;
		ret = cci_update_bits(sensor->regmap,
				      MT9M114_CAM_SENSOR_CONTROL_READ_MODE,
				      mask, ctrl->val ? mask : 0, NULL);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_mark_last_busy(&sensor->client->dev);
	pm_runtime_put_autosuspend(&sensor->client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops mt9m114_pa_ctrl_ops = {
	.g_volatile_ctrl = mt9m114_pa_g_ctrl,
	.s_ctrl = mt9m114_pa_s_ctrl,
};

static void mt9m114_pa_ctrl_update_exposure(struct mt9m114 *sensor, bool manual)
{
	/*
	 * Update the volatile flag on the manual exposure and gain controls.
	 * If the controls have switched to manual, read their current value
	 * from the hardware to ensure that control read and write operations
	 * will behave correctly
	 */
	if (manual) {
		mt9m114_pa_g_ctrl(sensor->pa.exposure);
		sensor->pa.exposure->cur.val = sensor->pa.exposure->val;
		sensor->pa.exposure->flags &= ~V4L2_CTRL_FLAG_VOLATILE;

		mt9m114_pa_g_ctrl(sensor->pa.gain);
		sensor->pa.gain->cur.val = sensor->pa.gain->val;
		sensor->pa.gain->flags &= ~V4L2_CTRL_FLAG_VOLATILE;
	} else {
		sensor->pa.exposure->flags |= V4L2_CTRL_FLAG_VOLATILE;
		sensor->pa.gain->flags |= V4L2_CTRL_FLAG_VOLATILE;
	}
}

static void mt9m114_pa_ctrl_update_blanking(struct mt9m114 *sensor,
					    const struct v4l2_mbus_framefmt *format)
{
	unsigned int max_blank;

	/* Update the blanking controls ranges based on the output size. */
	max_blank = MT9M114_CAM_SENSOR_CFG_LINE_LENGTH_PCK_MAX
		  - format->width;
	__v4l2_ctrl_modify_range(sensor->pa.hblank, MT9M114_MIN_HBLANK,
				 max_blank, 1, MT9M114_DEF_HBLANK);

	max_blank = MT9M114_CAM_SENSOR_CFG_FRAME_LENGTH_LINES_MAX
		  - format->height;
	__v4l2_ctrl_modify_range(sensor->pa.vblank, MT9M114_MIN_VBLANK,
				 max_blank, 1, MT9M114_DEF_VBLANK);
}

/* -----------------------------------------------------------------------------
 * Pixel Array Subdev Operations
 */

static inline struct mt9m114 *pa_to_mt9m114(struct v4l2_subdev *sd)
{
	return container_of(sd, struct mt9m114, pa.sd);
}

static int mt9m114_pa_init_cfg(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state)
{
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;

	crop = v4l2_subdev_get_pad_crop(sd, state, 0);

	crop->left = 0;
	crop->top = 0;
	crop->width = MT9M114_PIXEL_ARRAY_WIDTH;
	crop->height = MT9M114_PIXEL_ARRAY_HEIGHT;

	format = v4l2_subdev_get_pad_format(sd, state, 0);

	format->width = MT9M114_PIXEL_ARRAY_WIDTH;
	format->height = MT9M114_PIXEL_ARRAY_HEIGHT;
	format->code = MEDIA_BUS_FMT_SGRBG10_1X10;
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_RAW;
	format->ycbcr_enc = V4L2_YCBCR_ENC_601;
	format->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	format->xfer_func = V4L2_XFER_FUNC_NONE;

	return 0;
}

static int mt9m114_pa_enum_mbus_code(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *state,
				     struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGRBG10_1X10;

	return 0;
}

static int mt9m114_pa_enum_framesizes(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state,
				      struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index > 1)
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SGRBG10_1X10)
		return -EINVAL;

	/* Report binning capability through frame size enumeration. */
	fse->min_width = MT9M114_PIXEL_ARRAY_WIDTH / (fse->index + 1);
	fse->max_width = MT9M114_PIXEL_ARRAY_WIDTH / (fse->index + 1);
	fse->min_height = MT9M114_PIXEL_ARRAY_HEIGHT / (fse->index + 1);
	fse->max_height = MT9M114_PIXEL_ARRAY_HEIGHT / (fse->index + 1);

	return 0;
}

static int mt9m114_pa_set_fmt(struct v4l2_subdev *sd,
			      struct v4l2_subdev_state *state,
			      struct v4l2_subdev_format *fmt)
{
	struct mt9m114 *sensor = pa_to_mt9m114(sd);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;
	unsigned int hscale;
	unsigned int vscale;

	crop = v4l2_subdev_get_pad_crop(sd, state, fmt->pad);
	format = v4l2_subdev_get_pad_format(sd, state, fmt->pad);

	/* The sensor can bin horizontally and vertically. */
	hscale = DIV_ROUND_CLOSEST(crop->width, fmt->format.width ? : 1);
	vscale = DIV_ROUND_CLOSEST(crop->height, fmt->format.height ? : 1);
	format->width = crop->width / clamp(hscale, 1U, 2U);
	format->height = crop->height / clamp(vscale, 1U, 2U);

	fmt->format = *format;

	if (fmt->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		mt9m114_pa_ctrl_update_blanking(sensor, format);

	return 0;
}

static int mt9m114_pa_get_selection(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_selection *sel)
{
	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = *v4l2_subdev_get_pad_crop(sd, state, sel->pad);
		return 0;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = MT9M114_PIXEL_ARRAY_WIDTH;
		sel->r.height = MT9M114_PIXEL_ARRAY_HEIGHT;
		return 0;

	default:
		return -EINVAL;
	}
}

static int mt9m114_pa_set_selection(struct v4l2_subdev *sd,
				    struct v4l2_subdev_state *state,
				    struct v4l2_subdev_selection *sel)
{
	struct mt9m114 *sensor = pa_to_mt9m114(sd);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;

	if (sel->target != V4L2_SEL_TGT_CROP)
		return -EINVAL;

	crop = v4l2_subdev_get_pad_crop(sd, state, sel->pad);
	format = v4l2_subdev_get_pad_format(sd, state, sel->pad);

	/*
	 * Clamp the crop rectangle. The vertical coordinates must be even, and
	 * the horizontal coordinates must be a multiple of 4.
	 *
	 * FIXME: The horizontal coordinates must be a multiple of 8 when
	 * binning, but binning is configured after setting the selection, so
	 * we can't know tell here if it will be used.
	 */
	crop->left = ALIGN(sel->r.left, 4);
	crop->top = ALIGN(sel->r.top, 2);
	crop->width = clamp_t(unsigned int, ALIGN(sel->r.width, 4),
			      MT9M114_PIXEL_ARRAY_MIN_OUTPUT_WIDTH,
			      MT9M114_PIXEL_ARRAY_WIDTH - crop->left);
	crop->height = clamp_t(unsigned int, ALIGN(sel->r.height, 2),
			       MT9M114_PIXEL_ARRAY_MIN_OUTPUT_HEIGHT,
			       MT9M114_PIXEL_ARRAY_HEIGHT - crop->top);

	sel->r = *crop;

	/* Reset the format. */
	format->width = crop->width;
	format->height = crop->height;

	if (sel->which == V4L2_SUBDEV_FORMAT_ACTIVE)
		mt9m114_pa_ctrl_update_blanking(sensor, format);

	return 0;
}

static const struct v4l2_subdev_pad_ops mt9m114_pa_pad_ops = {
	.init_cfg = mt9m114_pa_init_cfg,
	.enum_mbus_code = mt9m114_pa_enum_mbus_code,
	.enum_frame_size = mt9m114_pa_enum_framesizes,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = mt9m114_pa_set_fmt,
	.get_selection = mt9m114_pa_get_selection,
	.set_selection = mt9m114_pa_set_selection,
};

static const struct v4l2_subdev_ops mt9m114_pa_ops = {
	.pad = &mt9m114_pa_pad_ops,
};

static int mt9m114_pa_init(struct mt9m114 *sensor)
{
	struct v4l2_ctrl_handler *hdl = &sensor->pa.hdl;
	struct v4l2_subdev *sd = &sensor->pa.sd;
	struct media_pad *pads = &sensor->pa.pad;
	const struct v4l2_mbus_framefmt *format;
	struct v4l2_subdev_state *state;
	unsigned int max_exposure;
	int ret;

	/* Initialize the subdev. */
	v4l2_subdev_init(sd, &mt9m114_pa_ops);
	v4l2_i2c_subdev_set_name(sd, sensor->client, NULL, " pixel array");

	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->owner = THIS_MODULE;
	sd->dev = &sensor->client->dev;
	v4l2_set_subdevdata(sd, sensor->client);

	/* Initialize the media entity. */
	sd->entity.function = MEDIA_ENT_F_CAM_SENSOR;
	sd->entity.ops = &mt9m114_entity_ops;
	pads[0].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, 1, pads);
	if (ret < 0)
		return ret;

	/* Initialize the control handler. */
	v4l2_ctrl_handler_init(hdl, 7);

	/* The range of the HBLANK and VBLANK controls will be updated below. */
	sensor->pa.hblank = v4l2_ctrl_new_std(hdl, &mt9m114_pa_ctrl_ops,
					      V4L2_CID_HBLANK,
					      MT9M114_DEF_HBLANK,
					      MT9M114_DEF_HBLANK, 1,
					      MT9M114_DEF_HBLANK);
	sensor->pa.vblank = v4l2_ctrl_new_std(hdl, &mt9m114_pa_ctrl_ops,
					      V4L2_CID_VBLANK,
					      MT9M114_DEF_VBLANK,
					      MT9M114_DEF_VBLANK, 1,
					      MT9M114_DEF_VBLANK);

	/*
	 * The maximum coarse integration time is the frame length in lines
	 * minus two. The default is taken directly from the datasheet, but
	 * makes little sense as auto-exposure is enabled by default.
	 */
	max_exposure = MT9M114_PIXEL_ARRAY_HEIGHT + MT9M114_MIN_VBLANK - 2;
	sensor->pa.exposure = v4l2_ctrl_new_std(hdl, &mt9m114_pa_ctrl_ops,
						V4L2_CID_EXPOSURE, 1,
						max_exposure, 1, 16);
	if (sensor->pa.exposure)
		sensor->pa.exposure->flags |= V4L2_CTRL_FLAG_VOLATILE;

	sensor->pa.gain = v4l2_ctrl_new_std(hdl, &mt9m114_pa_ctrl_ops,
					    V4L2_CID_ANALOGUE_GAIN, 1,
					    511, 1, 32);
	if (sensor->pa.gain)
		sensor->pa.gain->flags |= V4L2_CTRL_FLAG_VOLATILE;

	v4l2_ctrl_new_std(hdl, &mt9m114_pa_ctrl_ops,
			  V4L2_CID_PIXEL_RATE,
			  sensor->pixrate, sensor->pixrate, 1,
			  sensor->pixrate);

	v4l2_ctrl_new_std(hdl, &mt9m114_pa_ctrl_ops,
			  V4L2_CID_HFLIP,
			  0, 1, 1, 0);
	v4l2_ctrl_new_std(hdl, &mt9m114_pa_ctrl_ops,
			  V4L2_CID_VFLIP,
			  0, 1, 1, 0);

	if (hdl->error) {
		ret = hdl->error;
		goto error;
	}

	sd->state_lock = hdl->lock;

	ret = v4l2_subdev_init_finalize(sd);
	if (ret)
		goto error;

	/* Update the range of the blanking controls based on the format. */
	state = v4l2_subdev_lock_and_get_active_state(sd);
	format = v4l2_subdev_get_pad_format(sd, state, 0);
	mt9m114_pa_ctrl_update_blanking(sensor, format);
	v4l2_subdev_unlock_state(state);

	sd->ctrl_handler = hdl;

	return 0;

error:
	v4l2_ctrl_handler_free(&sensor->pa.hdl);
	media_entity_cleanup(&sensor->pa.sd.entity);
	return ret;
}

static void mt9m114_pa_cleanup(struct mt9m114 *sensor)
{
	v4l2_ctrl_handler_free(&sensor->pa.hdl);
	media_entity_cleanup(&sensor->pa.sd.entity);
}

/* -----------------------------------------------------------------------------
 * Image Flow Processor Control Operations
 */

static const char * const mt9m114_test_pattern_menu[] = {
	"Disabled",
	"Solid Color",
	"100% Color Bars",
	"Pseudo-Random",
	"Fade-to-Gray Color Bars",
	"Walking Ones 10-bit",
	"Walking Ones 8-bit",
};

/* Keep in sync with mt9m114_test_pattern_menu */
static const unsigned int mt9m114_test_pattern_value[] = {
	MT9M114_CAM_MODE_TEST_PATTERN_SELECT_SOLID,
	MT9M114_CAM_MODE_TEST_PATTERN_SELECT_SOLID_BARS,
	MT9M114_CAM_MODE_TEST_PATTERN_SELECT_RANDOM,
	MT9M114_CAM_MODE_TEST_PATTERN_SELECT_FADING_BARS,
	MT9M114_CAM_MODE_TEST_PATTERN_SELECT_WALKING_1S_10B,
	MT9M114_CAM_MODE_TEST_PATTERN_SELECT_WALKING_1S_8B,
};

static inline struct mt9m114 *ifp_ctrl_to_mt9m114(struct v4l2_ctrl *ctrl)
{
	return container_of(ctrl->handler, struct mt9m114, ifp.hdl);
}

static int mt9m114_ifp_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct mt9m114 *sensor = ifp_ctrl_to_mt9m114(ctrl);
	u32 value;
	int ret = 0;

	if (ctrl->id == V4L2_CID_EXPOSURE_AUTO)
		mt9m114_pa_ctrl_update_exposure(sensor,
						ctrl->val != V4L2_EXPOSURE_AUTO);

	/* V4L2 controls values are applied only when power is up. */
	if (!pm_runtime_get_if_in_use(&sensor->client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_AUTO_WHITE_BALANCE:
		/* Control both the AWB mode and the CCM algorithm. */
		if (ctrl->val)
			value = MT9M114_CAM_AWB_MODE_AUTO
			      | MT9M114_CAM_AWB_MODE_EXCLUSIVE_AE;
		else
			value = 0;

		cci_write(sensor->regmap, MT9M114_CAM_AWB_AWBMODE, value, &ret);

		if (ctrl->val)
			value = MT9M114_CCM_EXEC_CALC_CCM_MATRIX | 0x22;
		else
			value = 0;

		cci_write(sensor->regmap, MT9M114_CCM_ALGO, value, &ret);
		break;

	case V4L2_CID_EXPOSURE_AUTO:
		if (ctrl->val == V4L2_EXPOSURE_AUTO)
			value = MT9M114_AE_TRACK_EXEC_AUTOMATIC_EXPOSURE
			      | 0x00fe;
		else
			value = 0;

		cci_write(sensor->regmap, MT9M114_AE_TRACK_ALGO, value, &ret);
		if (ret)
			break;

		break;

	case V4L2_CID_TEST_PATTERN:
	case V4L2_CID_TEST_PATTERN_RED:
	case V4L2_CID_TEST_PATTERN_GREENR:
	case V4L2_CID_TEST_PATTERN_BLUE: {
		unsigned int pattern = sensor->ifp.tpg[MT9M114_TPG_PATTERN]->val;

		if (pattern) {
			cci_write(sensor->regmap, MT9M114_CAM_MODE_SELECT,
				  MT9M114_CAM_MODE_SELECT_TEST_PATTERN, &ret);
			cci_write(sensor->regmap,
				  MT9M114_CAM_MODE_TEST_PATTERN_SELECT,
				  mt9m114_test_pattern_value[pattern - 1], &ret);
			cci_write(sensor->regmap,
				  MT9M114_CAM_MODE_TEST_PATTERN_RED,
				  sensor->ifp.tpg[MT9M114_TPG_RED]->val, &ret);
			cci_write(sensor->regmap,
				  MT9M114_CAM_MODE_TEST_PATTERN_GREEN,
				  sensor->ifp.tpg[MT9M114_TPG_GREEN]->val, &ret);
			cci_write(sensor->regmap,
				  MT9M114_CAM_MODE_TEST_PATTERN_BLUE,
				  sensor->ifp.tpg[MT9M114_TPG_BLUE]->val, &ret);
		} else {
			cci_write(sensor->regmap, MT9M114_CAM_MODE_SELECT,
				  MT9M114_CAM_MODE_SELECT_NORMAL, &ret);
		}

		/*
		 * A Config-Change needs to be issued for the change to take
		 * effect. If we're not streaming ignore this, the change will
		 * be applied when the stream is started.
		 */
		if (ret || !sensor->streaming)
			break;

		ret = mt9m114_set_state(sensor,
					MT9M114_SYS_STATE_ENTER_CONFIG_CHANGE);
		break;
	}

	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_mark_last_busy(&sensor->client->dev);
	pm_runtime_put_autosuspend(&sensor->client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops mt9m114_ifp_ctrl_ops = {
	.s_ctrl = mt9m114_ifp_s_ctrl,
};

/* -----------------------------------------------------------------------------
 * Image Flow Processor Subdev Operations
 */

static inline struct mt9m114 *ifp_to_mt9m114(struct v4l2_subdev *sd)
{
	return container_of(sd, struct mt9m114, ifp.sd);
}

static int mt9m114_ifp_s_stream(struct v4l2_subdev *sd, int enable)
{
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);
	struct v4l2_subdev_state *pa_state;
	struct v4l2_subdev_state *ifp_state;
	int ret;

	if (!enable)
		return mt9m114_stop_streaming(sensor);

	ifp_state = v4l2_subdev_lock_and_get_active_state(&sensor->ifp.sd);
	pa_state = v4l2_subdev_lock_and_get_active_state(&sensor->pa.sd);

	ret = mt9m114_start_streaming(sensor, pa_state, ifp_state);

	v4l2_subdev_unlock_state(pa_state);
	v4l2_subdev_unlock_state(ifp_state);

	return ret;
}

static int mt9m114_ifp_g_frame_interval(struct v4l2_subdev *sd,
					struct v4l2_subdev_frame_interval *interval)
{
	struct v4l2_fract *ival = &interval->interval;
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);

	mutex_lock(sensor->ifp.hdl.lock);

	ival->numerator = 1;
	ival->denominator = sensor->ifp.frame_rate;

	mutex_unlock(sensor->ifp.hdl.lock);

	return 0;
}

static int mt9m114_ifp_s_frame_interval(struct v4l2_subdev *sd,
					struct v4l2_subdev_frame_interval *interval)
{
	struct v4l2_fract *ival = &interval->interval;
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);
	int ret = 0;

	mutex_lock(sensor->ifp.hdl.lock);

	if (ival->numerator != 0 && ival->denominator != 0)
		sensor->ifp.frame_rate = min_t(unsigned int,
					       ival->denominator / ival->numerator,
					       MT9M114_MAX_FRAME_RATE);
	else
		sensor->ifp.frame_rate = MT9M114_MAX_FRAME_RATE;

	ival->numerator = 1;
	ival->denominator = sensor->ifp.frame_rate;

	if (sensor->streaming)
		ret = mt9m114_set_frame_rate(sensor);

	mutex_unlock(sensor->ifp.hdl.lock);

	return ret;
}

static int mt9m114_ifp_init_cfg(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state)
{
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;
	struct v4l2_rect *compose;

	format = v4l2_subdev_get_pad_format(sd, state, 0);

	format->width = MT9M114_PIXEL_ARRAY_WIDTH;
	format->height = MT9M114_PIXEL_ARRAY_HEIGHT;
	format->code = MEDIA_BUS_FMT_SGRBG10_1X10;
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_RAW;
	format->ycbcr_enc = V4L2_YCBCR_ENC_601;
	format->quantization = V4L2_QUANTIZATION_FULL_RANGE;
	format->xfer_func = V4L2_XFER_FUNC_NONE;

	crop = v4l2_subdev_get_pad_crop(sd, state, 0);

	crop->left = 4;
	crop->top = 4;
	crop->width = format->width - 8;
	crop->height = format->height - 8;

	compose = v4l2_subdev_get_pad_compose(sd, state, 0);

	compose->left = 0;
	compose->top = 0;
	compose->width = crop->width;
	compose->height = crop->height;

	format = v4l2_subdev_get_pad_format(sd, state, 1);

	format->width = compose->width;
	format->height = compose->height;
	format->code = mt9m114_default_format_info(sensor)->code;
	format->field = V4L2_FIELD_NONE;
	format->colorspace = V4L2_COLORSPACE_SRGB;
	format->ycbcr_enc = V4L2_YCBCR_ENC_DEFAULT;
	format->quantization = V4L2_QUANTIZATION_DEFAULT;
	format->xfer_func = V4L2_XFER_FUNC_DEFAULT;

	return 0;
}

static int mt9m114_ifp_enum_mbus_code(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *state,
				      struct v4l2_subdev_mbus_code_enum *code)
{
	const unsigned int num_formats = ARRAY_SIZE(mt9m114_format_infos);
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);
	unsigned int index = 0;
	unsigned int flag;
	unsigned int i;

	switch (code->pad) {
	case 0:
		if (code->index != 0)
			return -EINVAL;

		code->code = mt9m114_format_infos[num_formats - 1].code;
		return 0;

	case 1:
		if (sensor->bus_cfg.bus_type == V4L2_MBUS_CSI2_DPHY)
			flag = MT9M114_FMT_FLAG_CSI2;
		else
			flag = MT9M114_FMT_FLAG_PARALLEL;

		for (i = 0; i < num_formats; ++i) {
			const struct mt9m114_format_info *info =
				&mt9m114_format_infos[i];

			if (info->flags & flag) {
				if (index == code->index) {
					code->code = info->code;
					return 0;
				}

				index++;
			}
		}

		return -EINVAL;

	default:
		return -EINVAL;
	}
}

static int mt9m114_ifp_enum_framesizes(struct v4l2_subdev *sd,
				       struct v4l2_subdev_state *state,
				       struct v4l2_subdev_frame_size_enum *fse)
{
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);
	const struct mt9m114_format_info *info;

	if (fse->index > 0)
		return -EINVAL;

	info = mt9m114_format_info(sensor, fse->pad, fse->code);
	if (!info || info->code != fse->code)
		return -EINVAL;

	if (fse->pad == 0) {
		fse->min_width = MT9M114_PIXEL_ARRAY_MIN_OUTPUT_WIDTH;
		fse->max_width = MT9M114_PIXEL_ARRAY_WIDTH;
		fse->min_height = MT9M114_PIXEL_ARRAY_MIN_OUTPUT_HEIGHT;
		fse->max_height = MT9M114_PIXEL_ARRAY_HEIGHT;
	} else {
		const struct v4l2_rect *crop;

		crop = v4l2_subdev_get_pad_crop(sd, state, 0);

		fse->max_width = crop->width;
		fse->max_height = crop->height;

		fse->min_width = fse->max_width / 4;
		fse->min_height = fse->max_height / 4;
	}

	return 0;
}

static int mt9m114_ifp_enum_frameintervals(struct v4l2_subdev *sd,
					   struct v4l2_subdev_state *state,
					   struct v4l2_subdev_frame_interval_enum *fie)
{
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);
	const struct mt9m114_format_info *info;

	if (fie->index > 0)
		return -EINVAL;

	info = mt9m114_format_info(sensor, fie->pad, fie->code);
	if (!info || info->code != fie->code)
		return -EINVAL;

	fie->interval.numerator = 1;
	fie->interval.denominator = MT9M114_MAX_FRAME_RATE;

	return 0;
}

static int mt9m114_ifp_set_fmt(struct v4l2_subdev *sd,
			       struct v4l2_subdev_state *state,
			       struct v4l2_subdev_format *fmt)
{
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);
	struct v4l2_mbus_framefmt *format;

	format = v4l2_subdev_get_pad_format(sd, state, fmt->pad);

	if (fmt->pad == 0) {
		/* Only the size can be changed on the sink pad. */
		format->width = clamp(ALIGN(fmt->format.width, 8),
				      MT9M114_PIXEL_ARRAY_MIN_OUTPUT_WIDTH,
				      MT9M114_PIXEL_ARRAY_WIDTH);
		format->height = clamp(ALIGN(fmt->format.height, 8),
				       MT9M114_PIXEL_ARRAY_MIN_OUTPUT_HEIGHT,
				       MT9M114_PIXEL_ARRAY_HEIGHT);
	} else {
		const struct mt9m114_format_info *info;

		/* Only the media bus code can be changed on the source pad. */
		info = mt9m114_format_info(sensor, 1, fmt->format.code);

		format->code = info->code;

		/* If the output format is RAW10, bypass the scaler. */
		if (format->code == MEDIA_BUS_FMT_SGRBG10_1X10)
			*format = *v4l2_subdev_get_pad_format(sd, state, 0);
	}

	fmt->format = *format;

	return 0;
}

static int mt9m114_ifp_get_selection(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *state,
				     struct v4l2_subdev_selection *sel)
{
	const struct v4l2_mbus_framefmt *format;
	const struct v4l2_rect *crop;
	int ret = 0;

	/* Crop and compose are only supported on the sink pad. */
	if (sel->pad != 0)
		return -EINVAL;

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP:
		sel->r = *v4l2_subdev_get_pad_crop(sd, state, 0);
		break;

	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		/*
		 * The crop default and bounds are equal to the sink
		 * format size minus 4 pixels on each side for demosaicing.
		 */
		format = v4l2_subdev_get_pad_format(sd, state, 0);

		sel->r.left = 4;
		sel->r.top = 4;
		sel->r.width = format->width - 8;
		sel->r.height = format->height - 8;
		break;

	case V4L2_SEL_TGT_COMPOSE:
		sel->r = *v4l2_subdev_get_pad_compose(sd, state, 0);
		break;

	case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_COMPOSE_BOUNDS:
		/*
		 * The compose default and bounds sizes are equal to the sink
		 * crop rectangle size.
		 */
		crop = v4l2_subdev_get_pad_crop(sd, state, 0);
		sel->r.left = 0;
		sel->r.top = 0;
		sel->r.width = crop->width;
		sel->r.height = crop->height;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int mt9m114_ifp_set_selection(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *state,
				     struct v4l2_subdev_selection *sel)
{
	struct v4l2_mbus_framefmt *format;
	struct v4l2_rect *crop;
	struct v4l2_rect *compose;

	if (sel->target != V4L2_SEL_TGT_CROP &&
	    sel->target != V4L2_SEL_TGT_COMPOSE)
		return -EINVAL;

	/* Crop and compose are only supported on the sink pad. */
	if (sel->pad != 0)
		return -EINVAL;

	format = v4l2_subdev_get_pad_format(sd, state, 0);
	crop = v4l2_subdev_get_pad_crop(sd, state, 0);
	compose = v4l2_subdev_get_pad_compose(sd, state, 0);

	if (sel->target == V4L2_SEL_TGT_CROP) {
		/*
		 * Clamp the crop rectangle. Demosaicing removes 4 pixels on
		 * each side of the image.
		 */
		crop->left = clamp_t(unsigned int, ALIGN(sel->r.left, 2), 4,
				     format->width - 4 -
				     MT9M114_SCALER_CROPPED_INPUT_WIDTH);
		crop->top = clamp_t(unsigned int, ALIGN(sel->r.top, 2), 4,
				    format->height - 4 -
				    MT9M114_SCALER_CROPPED_INPUT_HEIGHT);
		crop->width = clamp_t(unsigned int, ALIGN(sel->r.width, 2),
				      MT9M114_SCALER_CROPPED_INPUT_WIDTH,
				      format->width - 4 - crop->left);
		crop->height = clamp_t(unsigned int, ALIGN(sel->r.height, 2),
				       MT9M114_SCALER_CROPPED_INPUT_HEIGHT,
				       format->height - 4 - crop->top);

		sel->r = *crop;

		/* Propagate to the compose rectangle. */
		compose->width = crop->width;
		compose->height = crop->height;
	} else {
		/*
		 * Clamp the compose rectangle. The scaler can only downscale.
		 */
		compose->left = 0;
		compose->top = 0;
		compose->width = clamp_t(unsigned int, ALIGN(sel->r.width, 2),
					 MT9M114_SCALER_CROPPED_INPUT_WIDTH,
					 crop->width);
		compose->height = clamp_t(unsigned int, ALIGN(sel->r.height, 2),
					  MT9M114_SCALER_CROPPED_INPUT_HEIGHT,
					  crop->height);

		sel->r = *compose;
	}

	/* Propagate the compose rectangle to the source format. */
	format = v4l2_subdev_get_pad_format(sd, state, 1);
	format->width = compose->width;
	format->height = compose->height;

	return 0;
}

static void mt9m114_ifp_unregistered(struct v4l2_subdev *sd)
{
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);

	v4l2_device_unregister_subdev(&sensor->pa.sd);
}

static int mt9m114_ifp_registered(struct v4l2_subdev *sd)
{
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);
	int ret;

	ret = v4l2_device_register_subdev(sd->v4l2_dev, &sensor->pa.sd);
	if (ret < 0) {
		dev_err(&sensor->client->dev,
			"Failed to register pixel array subdev\n");
		return ret;
	}

	ret = media_create_pad_link(&sensor->pa.sd.entity, 0,
				    &sensor->ifp.sd.entity, 0,
				    MEDIA_LNK_FL_ENABLED |
				    MEDIA_LNK_FL_IMMUTABLE);
	if (ret < 0) {
		dev_err(&sensor->client->dev,
			"Failed to link pixel array to ifp\n");
		v4l2_device_unregister_subdev(&sensor->pa.sd);
		return ret;
	}

	return 0;
}

static const struct v4l2_subdev_video_ops mt9m114_ifp_video_ops = {
	.s_stream = mt9m114_ifp_s_stream,
	.g_frame_interval = mt9m114_ifp_g_frame_interval,
	.s_frame_interval = mt9m114_ifp_s_frame_interval,
};

static const struct v4l2_subdev_pad_ops mt9m114_ifp_pad_ops = {
	.init_cfg = mt9m114_ifp_init_cfg,
	.enum_mbus_code = mt9m114_ifp_enum_mbus_code,
	.enum_frame_size = mt9m114_ifp_enum_framesizes,
	.enum_frame_interval = mt9m114_ifp_enum_frameintervals,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = mt9m114_ifp_set_fmt,
	.get_selection = mt9m114_ifp_get_selection,
	.set_selection = mt9m114_ifp_set_selection,
};

static const struct v4l2_subdev_ops mt9m114_ifp_ops = {
	.video = &mt9m114_ifp_video_ops,
	.pad = &mt9m114_ifp_pad_ops,
};

static const struct v4l2_subdev_internal_ops mt9m114_ifp_internal_ops = {
	.registered = mt9m114_ifp_registered,
	.unregistered = mt9m114_ifp_unregistered,
};

static int mt9m114_ifp_init(struct mt9m114 *sensor)
{
	struct v4l2_subdev *sd = &sensor->ifp.sd;
	struct media_pad *pads = sensor->ifp.pads;
	struct v4l2_ctrl_handler *hdl = &sensor->ifp.hdl;
	struct v4l2_ctrl *link_freq;
	int ret;

	/* Initialize the subdev. */
	v4l2_i2c_subdev_init(sd, sensor->client, &mt9m114_ifp_ops);
	v4l2_i2c_subdev_set_name(sd, sensor->client, NULL, " ifp");

	sd->flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;
	sd->internal_ops = &mt9m114_ifp_internal_ops;

	/* Initialize the media entity. */
	sd->entity.function = MEDIA_ENT_F_PROC_VIDEO_ISP;
	sd->entity.ops = &mt9m114_entity_ops;
	pads[0].flags = MEDIA_PAD_FL_SINK;
	pads[1].flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sd->entity, 2, pads);
	if (ret < 0)
		return ret;

	sensor->ifp.frame_rate = MT9M114_DEF_FRAME_RATE;

	/* Initialize the control handler. */
	v4l2_ctrl_handler_init(hdl, 8);
	v4l2_ctrl_new_std(hdl, &mt9m114_ifp_ctrl_ops,
			  V4L2_CID_AUTO_WHITE_BALANCE,
			  0, 1, 1, 1);
	v4l2_ctrl_new_std_menu(hdl, &mt9m114_ifp_ctrl_ops,
			       V4L2_CID_EXPOSURE_AUTO,
			       V4L2_EXPOSURE_MANUAL, 0,
			       V4L2_EXPOSURE_AUTO);

	link_freq = v4l2_ctrl_new_int_menu(hdl, &mt9m114_ifp_ctrl_ops,
					   V4L2_CID_LINK_FREQ,
					   sensor->bus_cfg.nr_of_link_frequencies - 1,
					   0, sensor->bus_cfg.link_frequencies);
	if (link_freq)
		link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(hdl, &mt9m114_ifp_ctrl_ops,
			  V4L2_CID_PIXEL_RATE,
			  sensor->pixrate, sensor->pixrate, 1,
			  sensor->pixrate);

	sensor->ifp.tpg[MT9M114_TPG_PATTERN] =
		v4l2_ctrl_new_std_menu_items(hdl, &mt9m114_ifp_ctrl_ops,
					     V4L2_CID_TEST_PATTERN,
					     ARRAY_SIZE(mt9m114_test_pattern_menu) - 1,
					     0, 0, mt9m114_test_pattern_menu);
	sensor->ifp.tpg[MT9M114_TPG_RED] =
		v4l2_ctrl_new_std(hdl, &mt9m114_ifp_ctrl_ops,
				  V4L2_CID_TEST_PATTERN_RED,
				  0, 1023, 1, 1023);
	sensor->ifp.tpg[MT9M114_TPG_GREEN] =
		v4l2_ctrl_new_std(hdl, &mt9m114_ifp_ctrl_ops,
				  V4L2_CID_TEST_PATTERN_GREENR,
				  0, 1023, 1, 1023);
	sensor->ifp.tpg[MT9M114_TPG_BLUE] =
		v4l2_ctrl_new_std(hdl, &mt9m114_ifp_ctrl_ops,
				  V4L2_CID_TEST_PATTERN_BLUE,
				  0, 1023, 1, 1023);

	v4l2_ctrl_cluster(ARRAY_SIZE(sensor->ifp.tpg), sensor->ifp.tpg);

	if (hdl->error) {
		ret = hdl->error;
		goto error;
	}

	sd->ctrl_handler = hdl;
	sd->state_lock = hdl->lock;

	ret = v4l2_subdev_init_finalize(sd);
	if (ret)
		goto error;

	return 0;

error:
	v4l2_ctrl_handler_free(&sensor->ifp.hdl);
	media_entity_cleanup(&sensor->ifp.sd.entity);
	return ret;
}

static void mt9m114_ifp_cleanup(struct mt9m114 *sensor)
{
	v4l2_ctrl_handler_free(&sensor->ifp.hdl);
	media_entity_cleanup(&sensor->ifp.sd.entity);
}

/* -----------------------------------------------------------------------------
 * Power Management
 */

static int mt9m114_power_on(struct mt9m114 *sensor)
{
	int ret;

	/* Enable power and clocks. */
	ret = regulator_bulk_enable(ARRAY_SIZE(sensor->supplies),
				    sensor->supplies);
	if (ret < 0)
		return ret;

	ret = clk_prepare_enable(sensor->clk);
	if (ret < 0)
		goto error_regulator;

	/* Perform a hard reset if available, or a soft reset otherwise. */
	if (sensor->reset) {
		long freq = clk_get_rate(sensor->clk);
		unsigned int duration;

		/*
		 * The minimum duration is 50 clock cycles, thus typically
		 * around 2µs. Double it to be safe.
		 */
		duration = DIV_ROUND_UP(2 * 50 * 1000000, freq);

		gpiod_set_value(sensor->reset, 1);
		udelay(duration);
		gpiod_set_value(sensor->reset, 0);
	} else {
		/*
		 * The power may have just been turned on, we need to wait for
		 * the sensor to be ready to accept I2C commands.
		 */
		usleep_range(44500, 50000);

		cci_write(sensor->regmap, MT9M114_RESET_AND_MISC_CONTROL,
			  MT9M114_RESET_SOC, &ret);
		cci_write(sensor->regmap, MT9M114_RESET_AND_MISC_CONTROL, 0,
			  &ret);

		if (ret < 0) {
			dev_err(&sensor->client->dev, "Soft reset failed\n");
			goto error_clock;
		}
	}

	/*
	 * Wait for the sensor to be ready to accept I2C commands by polling the
	 * command register to wait for initialization to complete.
	 */
	usleep_range(44500, 50000);

	ret = mt9m114_poll_command(sensor, MT9M114_COMMAND_REGISTER_SET_STATE);
	if (ret < 0)
		goto error_clock;

	if (sensor->bus_cfg.bus_type == V4L2_MBUS_PARALLEL) {
		/*
		 * In parallel mode (OE set to low), the sensor will enter the
		 * streaming state after initialization. Enter the standby
		 * manually to stop streaming.
		 */
		ret = mt9m114_set_state(sensor,
					MT9M114_SYS_STATE_ENTER_STANDBY);
		if (ret < 0)
			goto error_clock;
	}

	/*
	 * Before issuing any Set-State command, we must ensure that the sensor
	 * reaches the standby mode (either initiated manually above in
	 * parallel mode, or automatically after reset in MIPI mode).
	 */
	ret = mt9m114_poll_state(sensor, MT9M114_SYS_STATE_STANDBY);
	if (ret < 0)
		goto error_clock;

	return 0;

error_clock:
	clk_disable_unprepare(sensor->clk);
error_regulator:
	regulator_bulk_disable(ARRAY_SIZE(sensor->supplies), sensor->supplies);
	return ret;
}

static void mt9m114_power_off(struct mt9m114 *sensor)
{
	clk_disable_unprepare(sensor->clk);
	regulator_bulk_disable(ARRAY_SIZE(sensor->supplies), sensor->supplies);
}

static int __maybe_unused mt9m114_runtime_resume(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);
	int ret;

	ret = mt9m114_power_on(sensor);
	if (ret)
		return ret;

	ret = mt9m114_initialize(sensor);
	if (ret) {
		mt9m114_power_off(sensor);
		return ret;
	}

	return 0;
}

static int __maybe_unused mt9m114_runtime_suspend(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);

	mt9m114_power_off(sensor);

	return 0;
}

static const struct dev_pm_ops mt9m114_pm_ops = {
	SET_RUNTIME_PM_OPS(mt9m114_runtime_suspend, mt9m114_runtime_resume, NULL)
};

/* -----------------------------------------------------------------------------
 * Probe & Remove
 */

static int mt9m114_clk_init(struct mt9m114 *sensor)
{
	unsigned int link_freq;

	/* Hardcode the PLL multiplier and dividers to default settings. */
	sensor->pll.m = 32;
	sensor->pll.n = 1;
	sensor->pll.p = 7;

	/*
	 * Calculate the pixel rate and link frequency. The CSI-2 bus is clocked
	 * for 16-bit per pixel, transmitted in DDR over a single lane. For
	 * parallel mode, the sensor ouputs one pixel in two PIXCLK cycles.
	 */
	sensor->pixrate = clk_get_rate(sensor->clk) * sensor->pll.m
			/ ((sensor->pll.n + 1) * (sensor->pll.p + 1));

	link_freq = sensor->bus_cfg.bus_type == V4L2_MBUS_CSI2_DPHY
		  ? sensor->pixrate * 8 : sensor->pixrate * 2;

	if (sensor->bus_cfg.nr_of_link_frequencies != 1 ||
	    sensor->bus_cfg.link_frequencies[0] != link_freq) {
		dev_err(&sensor->client->dev, "Unsupported DT link-frequencies\n");
		return -EINVAL;
	}

	return 0;
}

static int mt9m114_identify(struct mt9m114 *sensor)
{
	u64 major, minor, release, customer;
	u64 value;
	int ret;

	ret = cci_read(sensor->regmap, MT9M114_CHIP_ID, &value, NULL);
	if (ret) {
		dev_err(&sensor->client->dev, "Failed to read chip ID\n");
		return -ENXIO;
	}

	if (value != 0x2481) {
		dev_err(&sensor->client->dev, "Invalid chip ID 0x%04llx\n",
			value);
		return -ENXIO;
	}

	cci_read(sensor->regmap, MT9M114_MON_MAJOR_VERSION, &major, &ret);
	cci_read(sensor->regmap, MT9M114_MON_MINOR_VERSION, &minor, &ret);
	cci_read(sensor->regmap, MT9M114_MON_RELEASE_VERSION, &release, &ret);
	cci_read(sensor->regmap, MT9M114_CUSTOMER_REV, &customer, &ret);
	if (ret) {
		dev_err(&sensor->client->dev, "Failed to read version\n");
		return -ENXIO;
	}

	dev_dbg(&sensor->client->dev,
		"monitor v%llu.%llu.%04llx customer rev 0x%04llx\n",
		major, minor, release, customer);

	return 0;
}

static int mt9m114_parse_dt(struct mt9m114 *sensor)
{
	struct fwnode_handle *fwnode = dev_fwnode(&sensor->client->dev);
	struct fwnode_handle *ep;
	int ret;

	ep = fwnode_graph_get_next_endpoint(fwnode, NULL);
	if (!ep) {
		dev_err(&sensor->client->dev, "No endpoint found\n");
		return -EINVAL;
	}

	sensor->bus_cfg.bus_type = V4L2_MBUS_UNKNOWN;
	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &sensor->bus_cfg);
	fwnode_handle_put(ep);
	if (ret < 0) {
		dev_err(&sensor->client->dev, "Failed to parse endpoint\n");
		goto error;
	}

	switch (sensor->bus_cfg.bus_type) {
	case V4L2_MBUS_CSI2_DPHY:
	case V4L2_MBUS_PARALLEL:
		break;

	default:
		dev_err(&sensor->client->dev, "unsupported bus type %u\n",
			sensor->bus_cfg.bus_type);
		ret = -EINVAL;
		goto error;
	}

	return 0;

error:
	v4l2_fwnode_endpoint_free(&sensor->bus_cfg);
	return ret;
}

static int mt9m114_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct mt9m114 *sensor;
	int ret;

	sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->client = client;

	sensor->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(sensor->regmap)) {
		dev_err(dev, "Unable to initialize I2C\n");
		return -ENODEV;
	}

	ret = mt9m114_parse_dt(sensor);
	if (ret < 0)
		return ret;

	/* Acquire clocks, GPIOs and regulators. */
	sensor->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(sensor->clk)) {
		ret = PTR_ERR(sensor->clk);
		dev_err_probe(dev, ret, "Failed to get clock\n");
		goto error_ep_free;
	}

	sensor->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(sensor->reset)) {
		ret = PTR_ERR(sensor->reset);
		dev_err_probe(dev, ret, "Failed to get reset GPIO\n");
		goto error_ep_free;
	}

	sensor->supplies[0].supply = "vddio";
	sensor->supplies[1].supply = "vdd";
	sensor->supplies[2].supply = "vaa";

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(sensor->supplies),
				      sensor->supplies);
	if (ret < 0) {
		dev_err_probe(dev, ret, "Failed to get regulators\n");
		goto error_ep_free;
	}

	ret = mt9m114_clk_init(sensor);
	if (ret)
		goto error_ep_free;

	/*
	 * Identify the sensor. The driver supports runtime PM, but needs to
	 * work when runtime PM is disabled in the kernel. To that end, power
	 * the sensor on manually here, and initialize it after identification
	 * to reach the same state as if resumed through runtime PM.
	 */
	ret = mt9m114_power_on(sensor);
	if (ret < 0) {
		dev_err_probe(dev, ret, "Could not power on the device\n");
		goto error_ep_free;
	}

	ret = mt9m114_identify(sensor);
	if (ret < 0)
		goto error_power_off;

	ret = mt9m114_initialize(sensor);
	if (ret < 0)
		goto error_power_off;

	/*
	 * Enable runtime PM with autosuspend. As the device has been powered
	 * manually, mark it as active, and increase the usage count without
	 * resuming the device.
	 */
	pm_runtime_set_active(dev);
	pm_runtime_get_noresume(dev);
	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, 1000);
	pm_runtime_use_autosuspend(dev);

	/* Initialize the subdevices. */
	ret = mt9m114_pa_init(sensor);
	if (ret < 0)
		goto error_pm_cleanup;

	ret = mt9m114_ifp_init(sensor);
	if (ret < 0)
		goto error_pa_cleanup;

	ret = v4l2_async_register_subdev(&sensor->ifp.sd);
	if (ret < 0)
		goto error_ifp_cleanup;

	/*
	 * Decrease the PM usage count. The device will get suspended after the
	 * autosuspend delay, turning the power off.
	 */
	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);

	return 0;

error_ifp_cleanup:
	mt9m114_ifp_cleanup(sensor);
error_pa_cleanup:
	mt9m114_pa_cleanup(sensor);
error_pm_cleanup:
	pm_runtime_disable(dev);
	pm_runtime_put_noidle(dev);
error_power_off:
	mt9m114_power_off(sensor);
error_ep_free:
	v4l2_fwnode_endpoint_free(&sensor->bus_cfg);
	return ret;
}

static void mt9m114_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct mt9m114 *sensor = ifp_to_mt9m114(sd);
	struct device *dev = &client->dev;

	v4l2_async_unregister_subdev(&sensor->ifp.sd);

	mt9m114_ifp_cleanup(sensor);
	mt9m114_pa_cleanup(sensor);
	v4l2_fwnode_endpoint_free(&sensor->bus_cfg);

	/*
	 * Disable runtime PM. In case runtime PM is disabled in the kernel,
	 * make sure to turn power off manually.
	 */
	pm_runtime_disable(dev);
	if (!pm_runtime_status_suspended(dev))
		mt9m114_power_off(sensor);
	pm_runtime_set_suspended(dev);
}

static const struct of_device_id mt9m114_of_ids[] = {
	{ .compatible = "onnn,mt9m114" },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, mt9m114_of_ids);

static struct i2c_driver mt9m114_driver = {
	.driver = {
		.name	= "mt9m114",
		.pm	= &mt9m114_pm_ops,
		.of_match_table = mt9m114_of_ids,
	},
	.probe		= mt9m114_probe,
	.remove		= mt9m114_remove,
};

module_i2c_driver(mt9m114_driver);

MODULE_DESCRIPTION("onsemi MT9M114 Sensor Driver");
MODULE_AUTHOR("Laurent Pinchart <laurent.pinchart@ideasonboard.com>");
MODULE_LICENSE("GPL");
