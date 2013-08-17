/*
 * Register map for M5MOLS 8M Pixel camera sensor with ISP
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

/*
 * Category section register
 *
 * The category means a kind of command set. Including category section,
 * all defined categories in this version supports only, as you see below:
 */
#define CAT_SYSTEM		0x00
#define CAT_PARAM		0x01
#define CAT_MON			0x02
#define CAT_AE			0x03
#define CAT_WB			0x06
#define CAT_EXIF		0x07
#define CAT_LENS		0x0a
#define CAT_CAPTURE_PARAMETER	0x0b
#define CAT_CAPTURE_CONTROL	0x0c
#define CAT_FLASH		0x0f	/* related with FW, Verions, booting */

/*
 * Category 0 - System
 *
 * This category supports FW version, managing mode, even interrupt.
 */
#define CAT0_CUSTOMER_CODE	0x00
#define CAT0_PJ_CODE		0x01
#define CAT0_VERSION_FW_H	0x02
#define CAT0_VERSION_FW_L	0x03
#define CAT0_VERSION_HW_H	0x04
#define CAT0_VERSION_HW_L	0x05
#define CAT0_VERSION_PARM_H	0x06
#define CAT0_VERSION_PARM_L	0x07
#define CAT0_VERSION_AWB_H	0x08
#define CAT0_VERSION_AWB_L	0x09
#define CAT0_SYSMODE		0x0b
#define CAT0_STATUS		0x0c
#define CAT0_INT_QUEUE		0x0e
#define CAT0_INT_FACTOR		0x10	/* ver 10bit: 0x10, other 0x1c */
#define CAT0_INT_ENABLE		0x11	/* ver 10bit: 0x11, other 0x10  */
#define CAT0_INT_ROOTEN		0x12
#define CAT0_INT_INFO		0x18

/*
 * category 1 - Parameter mode
 *
 * This category is dealing with almost camera vendor. In spite of that,
 * It's a register to be able to detailed value for whole camera syste.
 * The key parameter like a resolution, FPS, data interface connecting
 * with Mobile AP, even effects.
 */
#define CAT1_DATA_INTERFACE	0x00
#define CAT1_MONITOR_SIZE	0x01
#define CAT1_MONITOR_FPS	0x02
#define CAT1_EFFECT		0x0b

/*
 * Category 2 - Monitor mode
 *
 * This category supports only monitoring mode. The monitoring mode means,
 * similar to preview. It supports like a YUYV format. At the capture mode,
 * it is handled like a JPEG & RAW formats.
 */
#define CAT2_ZOOM		0x01
#define CAT2_ZOOM_POSITION	0x02
#define CAT2_ZOOM_STEP		0x03
#define CAT2_CFIXB		0x09
#define CAT2_CFIXR		0x0a
#define CAT2_COLOR_EFFECT	0x0b
#define CAT2_CHROMA_LVL		0x0f
#define CAT2_CHROMA_EN		0x10

/*
 * Category 3 - Auto Exposure
 *
 * Currently, it supports only gain value with monitor mode. This device
 * is able to support Shutter, Gain(similar with Aperture), Flicker, at
 * monitor mode & capture mode both.
 */
#define CAT3_AE_LOCK		0x00
#define CAT3_AE_MODE		0x01
#define CAT3_MANUAL_GAIN_MON	0x12	/* 2 bytes operations belows */
#define CAT3_MANUAL_SHUT_MON	0x14
#define CAT3_MAX_EXPOSURE_MON	0x16
#define CAT3_MAX_EXPOSURE_CAP	0x18
#define CAT3_MAX_GAIN_MON	0x1a
#define CAT3_MAX_GAIN_CAP	0x1c
#define CAT3_MANUAL_GAIN_CAP	0x26
#define CAT3_MANUAL_SHUT_CAP	0x28

/*
 * Category 6 - White Balance
 *
 * Currently, it supports only auto white balance.
 */
#define CAT6_AWB_LOCK		0x00
#define CAT6_AWB_MODE		0x02
#define CAT6_AWB_MANUAL		0x03

/*
 * Category 7 - EXIF Information
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

/*
 * Category A - Lens Parameter
 */
#define CATA_INIT_AF_FUNC	0x00
#define CATA_AF_MODE		0x01
#define CATA_AF_EXCUTE		0x02
#define CATA_AF_STATUS		0x03
#define CATA_AF_VERSION		0x0a

/*
 * Category B - Capture Parameter
 */
#define CATB_YUVOUT_MAIN	0x00
#define CATB_MAIN_IMAGE_SIZE	0x01

/*
 * Category C - Capture Control
 */
#define CATC_CAP_SEL_FRAME	0x06	/* It determines Single or Multi. */
#define CATC_CAP_START		0x09
#define CATC_CAP_IMAGE_SIZE	0x0d
#define CATC_CAP_THUMB_SIZE	0x11

/*
 * Category F - Flash
 *
 * This mode provides functions about internal Flash works and System startup.
 */
#define CATC_CAM_START		0x12	/* It start internal ARM core booting
					 * after power-up */

#endif	/* M5MOLS_REG_H */
