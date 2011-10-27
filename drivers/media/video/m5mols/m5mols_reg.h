/*
 * Register map for M-5MOLS 8M Pixel camera sensor with ISP
 *
 * Copyright (C) 2011 Samsung Electronics Co., Ltd.
 * Author: HeungJun Kim <riverful.kim@samsung.com>
 *
 * Copyright (C) 2009 Samsung Electronics Co., Ltd.
 * Author: Dongsoo Nathaniel Kim <dongsoo45.kim@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef M5MOLS_REG_H
#define M5MOLS_REG_H

#define M5MOLS_I2C_MAX_SIZE	4
#define M5MOLS_BYTE_READ	0x01
#define M5MOLS_BYTE_WRITE	0x02

#define I2C_CATEGORY(__cat)		((__cat >> 16) & 0xff)
#define I2C_COMMAND(__comm)		((__comm >> 8) & 0xff)
#define I2C_SIZE(__reg_s)		((__reg_s) & 0xff)
#define I2C_REG(__cat, __cmd, __reg_s)	((__cat << 16) | (__cmd << 8) | __reg_s)

/*
 * Category section register
 *
 * The category means set including relevant command of M-5MOLS.
 */
#define CAT_SYSTEM		0x00
#define CAT_PARAM		0x01
#define CAT_MONITOR		0x02
#define CAT_AE			0x03
#define CAT_WB			0x06
#define CAT_EXIF		0x07
#define CAT_FD			0x09
#define CAT_LENS		0x0a
#define CAT_CAPT_PARM		0x0b
#define CAT_CAPT_CTRL		0x0c
#define CAT_FLASH		0x0f	/* related to FW, revisions, booting */

/*
 * Category 0 - SYSTEM mode
 *
 * The SYSTEM mode in the M-5MOLS means area available to handle with the whole
 * & all-round system of sensor. It deals with version/interrupt/setting mode &
 * even sensor's status. Especially, the M-5MOLS sensor with ISP varies by
 * packaging & manufacturer, even the customer and project code. And the
 * function details may vary among them. The version information helps to
 * determine what methods shall be used in the driver.
 *
 * There is many registers between customer version address and awb one. For
 * more specific contents, see definition if file m5mols.h.
 */
#define CAT0_VER_CUSTOMER	0x00	/* customer version */
#define CAT0_VER_PROJECT	0x01	/* project version */
#define CAT0_VER_FIRMWARE	0x02	/* Firmware version */
#define CAT0_VER_HARDWARE	0x04	/* Hardware version */
#define CAT0_VER_PARAMETER	0x06	/* Parameter version */
#define CAT0_VER_AWB		0x08	/* Auto WB version */
#define CAT0_VER_STRING		0x0a	/* string including M-5MOLS */
#define CAT0_SYSMODE		0x0b	/* SYSTEM mode register */
#define CAT0_STATUS		0x0c	/* SYSTEM mode status register */
#define CAT0_INT_FACTOR		0x10	/* interrupt pending register */
#define CAT0_INT_ENABLE		0x11	/* interrupt enable register */

#define SYSTEM_VER_CUSTOMER	I2C_REG(CAT_SYSTEM, CAT0_VER_CUSTOMER, 1)
#define SYSTEM_VER_PROJECT	I2C_REG(CAT_SYSTEM, CAT0_VER_PROJECT, 1)
#define SYSTEM_VER_FIRMWARE	I2C_REG(CAT_SYSTEM, CAT0_VER_FIRMWARE, 2)
#define SYSTEM_VER_HARDWARE	I2C_REG(CAT_SYSTEM, CAT0_VER_HARDWARE, 2)
#define SYSTEM_VER_PARAMETER	I2C_REG(CAT_SYSTEM, CAT0_VER_PARAMETER, 2)
#define SYSTEM_VER_AWB		I2C_REG(CAT_SYSTEM, CAT0_VER_AWB, 2)

#define SYSTEM_SYSMODE		I2C_REG(CAT_SYSTEM, CAT0_SYSMODE, 1)
#define REG_SYSINIT		0x00	/* SYSTEM mode */
#define REG_PARAMETER		0x01	/* PARAMETER mode */
#define REG_MONITOR		0x02	/* MONITOR mode */
#define REG_CAPTURE		0x03	/* CAPTURE mode */

#define SYSTEM_CMD(__cmd)	I2C_REG(CAT_SYSTEM, cmd, 1)
#define SYSTEM_VER_STRING	I2C_REG(CAT_SYSTEM, CAT0_VER_STRING, 1)
#define REG_SAMSUNG_ELECTRO	"SE"	/* Samsung Electro-Mechanics */
#define REG_SAMSUNG_OPTICS	"OP"	/* Samsung Fiber-Optics */
#define REG_SAMSUNG_TECHWIN	"TB"	/* Samsung Techwin */

#define SYSTEM_INT_FACTOR	I2C_REG(CAT_SYSTEM, CAT0_INT_FACTOR, 1)
#define SYSTEM_INT_ENABLE	I2C_REG(CAT_SYSTEM, CAT0_INT_ENABLE, 1)
#define REG_INT_MODE		(1 << 0)
#define REG_INT_AF		(1 << 1)
#define REG_INT_ZOOM		(1 << 2)
#define REG_INT_CAPTURE		(1 << 3)
#define REG_INT_FRAMESYNC	(1 << 4)
#define REG_INT_FD		(1 << 5)
#define REG_INT_LENS_INIT	(1 << 6)
#define REG_INT_SOUND		(1 << 7)
#define REG_INT_MASK		0x0f

/*
 * category 1 - PARAMETER mode
 *
 * This category supports function of camera features of M-5MOLS. It means we
 * can handle with preview(MONITOR) resolution size/frame per second/interface
 * between the sensor and the Application Processor/even the image effect.
 */
#define CAT1_DATA_INTERFACE	0x00	/* interface between sensor and AP */
#define CAT1_MONITOR_SIZE	0x01	/* resolution at the MONITOR mode */
#define CAT1_MONITOR_FPS	0x02	/* frame per second at this mode */
#define CAT1_EFFECT		0x0b	/* image effects */

#define PARM_MON_SIZE		I2C_REG(CAT_PARAM, CAT1_MONITOR_SIZE, 1)

#define PARM_MON_FPS		I2C_REG(CAT_PARAM, CAT1_MONITOR_FPS, 1)
#define REG_FPS_30		0x02

#define PARM_INTERFACE		I2C_REG(CAT_PARAM, CAT1_DATA_INTERFACE, 1)
#define REG_INTERFACE_MIPI	0x02

#define PARM_EFFECT		I2C_REG(CAT_PARAM, CAT1_EFFECT, 1)
#define REG_EFFECT_OFF		0x00
#define REG_EFFECT_NEGA		0x01
#define REG_EFFECT_EMBOSS	0x06
#define REG_EFFECT_OUTLINE	0x07
#define REG_EFFECT_WATERCOLOR	0x08

/*
 * Category 2 - MONITOR mode
 *
 * The MONITOR mode is same as preview mode as we said. The M-5MOLS has another
 * mode named "Preview", but this preview mode is used at the case specific
 * vider-recording mode. This mmode supports only YUYV format. On the other
 * hand, the JPEG & RAW formats is supports by CAPTURE mode. And, there are
 * another options like zoom/color effect(different with effect in PARAMETER
 * mode)/anti hand shaking algorithm.
 */
#define CAT2_ZOOM		0x01	/* set the zoom position & execute */
#define CAT2_ZOOM_STEP		0x03	/* set the zoom step */
#define CAT2_CFIXB		0x09	/* CB value for color effect */
#define CAT2_CFIXR		0x0a	/* CR value for color effect */
#define CAT2_COLOR_EFFECT	0x0b	/* set on/off of color effect */
#define CAT2_CHROMA_LVL		0x0f	/* set chroma level */
#define CAT2_CHROMA_EN		0x10	/* set on/off of choroma */
#define CAT2_EDGE_LVL		0x11	/* set sharpness level */
#define CAT2_EDGE_EN		0x12	/* set on/off sharpness */
#define CAT2_TONE_CTL		0x25	/* set tone color(contrast) */

#define MON_ZOOM		I2C_REG(CAT_MONITOR, CAT2_ZOOM, 1)

#define MON_CFIXR		I2C_REG(CAT_MONITOR, CAT2_CFIXR, 1)
#define MON_CFIXB		I2C_REG(CAT_MONITOR, CAT2_CFIXB, 1)
#define REG_CFIXB_SEPIA		0xd8
#define REG_CFIXR_SEPIA		0x18

#define MON_EFFECT		I2C_REG(CAT_MONITOR, CAT2_COLOR_EFFECT, 1)
#define REG_COLOR_EFFECT_OFF	0x00
#define REG_COLOR_EFFECT_ON	0x01

#define MON_CHROMA_EN		I2C_REG(CAT_MONITOR, CAT2_CHROMA_EN, 1)
#define MON_CHROMA_LVL		I2C_REG(CAT_MONITOR, CAT2_CHROMA_LVL, 1)
#define REG_CHROMA_OFF		0x00
#define REG_CHROMA_ON		0x01

#define MON_EDGE_EN		I2C_REG(CAT_MONITOR, CAT2_EDGE_EN, 1)
#define MON_EDGE_LVL		I2C_REG(CAT_MONITOR, CAT2_EDGE_LVL, 1)
#define REG_EDGE_OFF		0x00
#define REG_EDGE_ON		0x01

#define MON_TONE_CTL		I2C_REG(CAT_MONITOR, CAT2_TONE_CTL, 1)

/*
 * Category 3 - Auto Exposure
 *
 * The M-5MOLS exposure capbility is detailed as which is similar to digital
 * camera. This category supports AE locking/various AE mode(range of exposure)
 * /ISO/flickering/EV bias/shutter/meteoring, and anything else. And the
 * maximum/minimum exposure gain value depending on M-5MOLS firmware, may be
 * different. So, this category also provide getting the max/min values. And,
 * each MONITOR and CAPTURE mode has each gain/shutter/max exposure values.
 */
#define CAT3_AE_LOCK		0x00	/* locking Auto exposure */
#define CAT3_AE_MODE		0x01	/* set AE mode, mode means range */
#define CAT3_ISO		0x05	/* set ISO */
#define CAT3_EV_PRESET_MONITOR	0x0a	/* EV(scenemode) preset for MONITOR */
#define CAT3_EV_PRESET_CAPTURE	0x0b	/* EV(scenemode) preset for CAPTURE */
#define CAT3_MANUAL_GAIN_MON	0x12	/* meteoring value for the MONITOR */
#define CAT3_MAX_GAIN_MON	0x1a	/* max gain value for the MONITOR */
#define CAT3_MANUAL_GAIN_CAP	0x26	/* meteoring value for the CAPTURE */
#define CAT3_AE_INDEX		0x38	/* AE index */

#define AE_LOCK			I2C_REG(CAT_AE, CAT3_AE_LOCK, 1)
#define REG_AE_UNLOCK		0x00
#define REG_AE_LOCK		0x01

#define AE_MODE			I2C_REG(CAT_AE, CAT3_AE_MODE, 1)
#define REG_AE_OFF		0x00	/* AE off */
#define REG_AE_ALL		0x01	/* calc AE in all block integral */
#define REG_AE_CENTER		0x03	/* calc AE in center weighted */
#define REG_AE_SPOT		0x06	/* calc AE in specific spot */

#define AE_ISO			I2C_REG(CAT_AE, CAT3_ISO, 1)
#define REG_ISO_AUTO		0x00
#define REG_ISO_50		0x01
#define REG_ISO_100		0x02
#define REG_ISO_200		0x03
#define REG_ISO_400		0x04
#define REG_ISO_800		0x05

#define AE_EV_PRESET_MONITOR	I2C_REG(CAT_AE, CAT3_EV_PRESET_MONITOR, 1)
#define AE_EV_PRESET_CAPTURE	I2C_REG(CAT_AE, CAT3_EV_PRESET_CAPTURE, 1)
#define REG_SCENE_NORMAL	0x00
#define REG_SCENE_PORTRAIT	0x01
#define REG_SCENE_LANDSCAPE	0x02
#define REG_SCENE_SPORTS	0x03
#define REG_SCENE_PARTY_INDOOR	0x04
#define REG_SCENE_BEACH_SNOW	0x05
#define REG_SCENE_SUNSET	0x06
#define REG_SCENE_DAWN_DUSK	0x07
#define REG_SCENE_FALL		0x08
#define REG_SCENE_NIGHT		0x09
#define REG_SCENE_AGAINST_LIGHT	0x0a
#define REG_SCENE_FIRE		0x0b
#define REG_SCENE_TEXT		0x0c
#define REG_SCENE_CANDLE	0x0d

#define AE_MAN_GAIN_MON		I2C_REG(CAT_AE, CAT3_MANUAL_GAIN_MON, 2)
#define AE_MAX_GAIN_MON		I2C_REG(CAT_AE, CAT3_MAX_GAIN_MON, 2)
#define AE_MAN_GAIN_CAP		I2C_REG(CAT_AE, CAT3_MANUAL_GAIN_CAP, 2)

#define AE_INDEX		I2C_REG(CAT_AE, CAT3_AE_INDEX, 1)
#define REG_AE_INDEX_20_NEG	0x00
#define REG_AE_INDEX_15_NEG	0x01
#define REG_AE_INDEX_10_NEG	0x02
#define REG_AE_INDEX_05_NEG	0x03
#define REG_AE_INDEX_00		0x04
#define REG_AE_INDEX_05_POS	0x05
#define REG_AE_INDEX_10_POS	0x06
#define REG_AE_INDEX_15_POS	0x07
#define REG_AE_INDEX_20_POS	0x08

/*
 * Category 6 - White Balance
 *
 * This category provide AWB locking/mode/preset/speed/gain bias, etc.
 */
#define CAT6_AWB_LOCK		0x00	/* locking Auto Whitebalance */
#define CAT6_AWB_MODE		0x02	/* set Auto or Manual */
#define CAT6_AWB_MANUAL		0x03	/* set Manual(preset) value */

#define AWB_LOCK		I2C_REG(CAT_WB, CAT6_AWB_LOCK, 1)
#define REG_AWB_UNLOCK		0x00
#define REG_AWB_LOCK		0x01

#define AWB_MODE		I2C_REG(CAT_WB, CAT6_AWB_MODE, 1)
#define REG_AWB_AUTO		0x01	/* AWB off */
#define REG_AWB_PRESET		0x02	/* AWB preset */

#define AWB_MANUAL		I2C_REG(CAT_WB, CAT6_AWB_MANUAL, 1)
#define REG_AWB_INCANDESCENT	0x01
#define REG_AWB_FLUORESCENT_1	0x02
#define REG_AWB_FLUORESCENT_2	0x03
#define REG_AWB_DAYLIGHT	0x04
#define REG_AWB_CLOUDY		0x05
#define REG_AWB_SHADE		0x06
#define REG_AWB_HORIZON		0x07
#define REG_AWB_LEDLIGHT	0x09

/*
 * Category 7 - EXIF information
 */
#define CAT7_INFO_EXPTIME_NU	0x00
#define CAT7_INFO_EXPTIME_DE	0x04
#define CAT7_INFO_TV_NU		0x08
#define CAT7_INFO_TV_DE		0x0c
#define CAT7_INFO_AV_NU		0x10
#define CAT7_INFO_AV_DE		0x14
#define CAT7_INFO_BV_NU		0x18
#define CAT7_INFO_BV_DE		0x1c
#define CAT7_INFO_EBV_NU	0x20
#define CAT7_INFO_EBV_DE	0x24
#define CAT7_INFO_ISO		0x28
#define CAT7_INFO_FLASH		0x2a
#define CAT7_INFO_SDR		0x2c
#define CAT7_INFO_QVAL		0x2e

#define EXIF_INFO_EXPTIME_NU	I2C_REG(CAT_EXIF, CAT7_INFO_EXPTIME_NU, 4)
#define EXIF_INFO_EXPTIME_DE	I2C_REG(CAT_EXIF, CAT7_INFO_EXPTIME_DE, 4)
#define EXIF_INFO_TV_NU		I2C_REG(CAT_EXIF, CAT7_INFO_TV_NU, 4)
#define EXIF_INFO_TV_DE		I2C_REG(CAT_EXIF, CAT7_INFO_TV_DE, 4)
#define EXIF_INFO_AV_NU		I2C_REG(CAT_EXIF, CAT7_INFO_AV_NU, 4)
#define EXIF_INFO_AV_DE		I2C_REG(CAT_EXIF, CAT7_INFO_AV_DE, 4)
#define EXIF_INFO_BV_NU		I2C_REG(CAT_EXIF, CAT7_INFO_BV_NU, 4)
#define EXIF_INFO_BV_DE		I2C_REG(CAT_EXIF, CAT7_INFO_BV_DE, 4)
#define EXIF_INFO_EBV_NU	I2C_REG(CAT_EXIF, CAT7_INFO_EBV_NU, 4)
#define EXIF_INFO_EBV_DE	I2C_REG(CAT_EXIF, CAT7_INFO_EBV_DE, 4)
#define EXIF_INFO_ISO		I2C_REG(CAT_EXIF, CAT7_INFO_ISO, 2)
#define EXIF_INFO_FLASH		I2C_REG(CAT_EXIF, CAT7_INFO_FLASH, 2)
#define EXIF_INFO_SDR		I2C_REG(CAT_EXIF, CAT7_INFO_SDR, 2)
#define EXIF_INFO_QVAL		I2C_REG(CAT_EXIF, CAT7_INFO_QVAL, 2)

/*
 * Category 9 - Face Detection
 */
#define CAT9_FD_CTL		0x00

#define FD_CTL			I2C_REG(CAT_FD, CAT9_FD_CTL, 1)
#define BIT_FD_EN		0
#define BIT_FD_DRAW_FACE_FRAME	4
#define BIT_FD_DRAW_SMILE_LVL	6
#define REG_FD(shift)		(1 << shift)
#define REG_FD_OFF		0x0

/*
 * Category A - Lens Parameter
 */
#define CATA_AF_MODE		0x01
#define CATA_AF_EXECUTE		0x02
#define CATA_AF_STATUS		0x03
#define CATA_AF_VERSION		0x0a

#define AF_MODE			I2C_REG(CAT_LENS, CATA_AF_MODE, 1)
#define REG_AF_NORMAL		0x00	/* Normal AF, one time */
#define REG_AF_MACRO		0x01	/* Macro AF, one time */
#define REG_AF_POWEROFF		0x07

#define AF_EXECUTE		I2C_REG(CAT_LENS, CATA_AF_EXECUTE, 1)
#define REG_AF_STOP		0x00
#define REG_AF_EXE_AUTO		0x01
#define REG_AF_EXE_CAF		0x02

#define AF_STATUS		I2C_REG(CAT_LENS, CATA_AF_STATUS, 1)
#define REG_AF_FAIL		0x00
#define REG_AF_SUCCESS		0x02
#define REG_AF_IDLE		0x04
#define REG_AF_BUSY		0x05

#define AF_VERSION		I2C_REG(CAT_LENS, CATA_AF_VERSION, 1)

/*
 * Category B - CAPTURE Parameter
 */
#define CATB_YUVOUT_MAIN	0x00
#define CATB_MAIN_IMAGE_SIZE	0x01
#define CATB_MCC_MODE		0x1d
#define CATB_WDR_EN		0x2c
#define CATB_LIGHT_CTRL		0x40
#define CATB_FLASH_CTRL		0x41

#define CAPP_YUVOUT_MAIN	I2C_REG(CAT_CAPT_PARM, CATB_YUVOUT_MAIN, 1)
#define REG_YUV422		0x00
#define REG_BAYER10		0x05
#define REG_BAYER8		0x06
#define REG_JPEG		0x10

#define CAPP_MAIN_IMAGE_SIZE	I2C_REG(CAT_CAPT_PARM, CATB_MAIN_IMAGE_SIZE, 1)

#define CAPP_MCC_MODE		I2C_REG(CAT_CAPT_PARM, CATB_MCC_MODE, 1)
#define REG_MCC_OFF		0x00
#define REG_MCC_NORMAL		0x01

#define CAPP_WDR_EN		I2C_REG(CAT_CAPT_PARM, CATB_WDR_EN, 1)
#define REG_WDR_OFF		0x00
#define REG_WDR_ON		0x01
#define REG_WDR_AUTO		0x02

#define CAPP_LIGHT_CTRL		I2C_REG(CAT_CAPT_PARM, CATB_LIGHT_CTRL, 1)
#define REG_LIGHT_OFF		0x00
#define REG_LIGHT_ON		0x01
#define REG_LIGHT_AUTO		0x02

#define CAPP_FLASH_CTRL		I2C_REG(CAT_CAPT_PARM, CATB_FLASH_CTRL, 1)
#define REG_FLASH_OFF		0x00
#define REG_FLASH_ON		0x01
#define REG_FLASH_AUTO		0x02

/*
 * Category C - CAPTURE Control
 */
#define CATC_CAP_MODE		0x00
#define CATC_CAP_SEL_FRAME	0x06	/* It determines Single or Multi */
#define CATC_CAP_START		0x09
#define CATC_CAP_IMAGE_SIZE	0x0d
#define CATC_CAP_THUMB_SIZE	0x11

#define CAPC_MODE		I2C_REG(CAT_CAPT_CTRL, CATC_CAP_MODE, 1)
#define REG_CAP_NONE		0x00
#define REG_CAP_ANTI_SHAKE	0x02

#define CAPC_SEL_FRAME		I2C_REG(CAT_CAPT_CTRL, CATC_CAP_SEL_FRAME, 1)

#define CAPC_START		I2C_REG(CAT_CAPT_CTRL, CATC_CAP_START, 1)
#define REG_CAP_START_MAIN	0x01
#define REG_CAP_START_THUMB	0x03

#define CAPC_IMAGE_SIZE		I2C_REG(CAT_CAPT_CTRL, CATC_CAP_IMAGE_SIZE, 4)
#define CAPC_THUMB_SIZE		I2C_REG(CAT_CAPT_CTRL, CATC_CAP_THUMB_SIZE, 4)

/*
 * Category F - Flash
 *
 * This mode provides functions about internal flash stuff and system startup.
 */
#define CATF_CAM_START		0x12	/* It starts internal ARM core booting
					 * after power-up */

#define FLASH_CAM_START		I2C_REG(CAT_FLASH, CATF_CAM_START, 1)
#define REG_START_ARM_BOOT	0x01

#endif	/* M5MOLS_REG_H */
