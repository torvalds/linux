/*
 * rk_camera_sensor_info.h - PXA camera driver header file
 *
 * Copyright (C) 2003, Intel Corporation
 * Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __RK_CAMERA_SENSOR_INFO_H_
#define __RK_CAMERA_SENSOR_INFO_H_

/* Camera Sensor Must Define Macro Begin */
#define RK29_CAM_SENSOR_OV7675 ov7675
#define RK29_CAM_SENSOR_OV9650 ov9650
#define RK29_CAM_SENSOR_OV2640 ov2640
#define RK29_CAM_SENSOR_OV2655 ov2655
#define RK29_CAM_SENSOR_OV2659 ov2659
#define RK29_CAM_SENSOR_GC2145 gc2145
#define RK29_CAM_SENSOR_GC2155 gc2155
#define RK29_CAM_SENSOR_OV7690 ov7690
#define RK29_CAM_SENSOR_OV3640 ov3640
#define RK29_CAM_SENSOR_OV3660 ov3660
#define RK29_CAM_SENSOR_OV5640 ov5640
#define RK29_CAM_SENSOR_OV5642 ov5642
#define RK29_CAM_SENSOR_S5K6AA s5k6aa
#define RK29_CAM_SENSOR_MT9D112 mt9d112
#define RK29_CAM_SENSOR_MT9D113 mt9d113
#define RK29_CAM_SENSOR_MT9P111 mt9p111
#define RK29_CAM_SENSOR_MT9T111 mt9t111
#define RK29_CAM_SENSOR_GT2005  gt2005
#define RK29_CAM_SENSOR_GC0307  gc0307
#define RK29_CAM_SENSOR_GC0308  gc0308
#define RK29_CAM_SENSOR_GC0309  gc0309
#define RK29_CAM_SENSOR_GC0312  gc0312
#define RK29_CAM_SENSOR_GC2015  gc2015
#define RK29_CAM_SENSOR_GC0328  gc0328
#define RK29_CAM_SENSOR_GC0329  gc0329
#define RK29_CAM_SENSOR_GC2035	gc2035
#define RK29_CAM_SENSOR_SIV120B  siv120b
#define RK29_CAM_SENSOR_SIV121D  siv121d
#define RK29_CAM_SENSOR_SID130B  sid130B
#define RK29_CAM_SENSOR_HI253  hi253
#define RK29_CAM_SENSOR_HI704  hi704
#define RK29_CAM_SENSOR_NT99250 nt99250
#define RK29_CAM_SENSOR_SP0718  sp0718
#define RK29_CAM_SENSOR_SP0838  sp0838
#define RK29_CAM_SENSOR_SP2518  sp2518
#define RK29_CAM_SENSOR_S5K5CA  s5k5ca
#define RK29_CAM_ISP_MTK9335	mtk9335isp
#define RK29_CAM_SENSOR_HM2057  hm2057
#define RK29_CAM_SENSOR_HM5065  hm5065
#define RK29_CAM_SENSOR_NT99160 nt99160
#define RK29_CAM_SENSOR_NT99240 nt99240
#define RK29_CAM_SENSOR_NT99252 nt99252
#define RK29_CAM_SENSOR_NT99340 nt99340
#define RK29_CAM_ISP_ICATCH7002_MI1040  icatchmi1040
#define RK29_CAM_ISP_ICATCH7002_OV5693  icatchov5693
#define RK29_CAM_ISP_ICATCH7002_OV8825  icatchov8825
#define RK29_CAM_ISP_ICATCH7002_OV2720  icatchov2720
#define RK29_CAM_SENSOR_TP2825  tp2825	/* benjo.zhou#rock-chips.com */
#define RK29_CAM_SENSOR_ADV7181 adv7181

#define RK29_CAM_SENSOR_NAME_OV7675 "ov7675"
#define RK29_CAM_SENSOR_NAME_OV9650 "ov9650"
#define RK29_CAM_SENSOR_NAME_OV2640 "ov2640"
#define RK29_CAM_SENSOR_NAME_OV2655 "ov2655"
#define RK29_CAM_SENSOR_NAME_OV2659 "ov2659"
#define RK29_CAM_SENSOR_NAME_OV7690 "ov7690"
#define RK29_CAM_SENSOR_NAME_OV3640 "ov3640"
#define RK29_CAM_SENSOR_NAME_OV3660 "ov3660"
#define RK29_CAM_SENSOR_NAME_OV5640 "ov5640"
#define RK29_CAM_SENSOR_NAME_OV5642 "ov5642"
#define RK29_CAM_SENSOR_NAME_S5K6AA "s5k6aa"
#define RK29_CAM_SENSOR_NAME_MT9D112 "mt9d112"
#define RK29_CAM_SENSOR_NAME_MT9D113 "mt9d113"
#define RK29_CAM_SENSOR_NAME_MT9P111 "mt9p111"
#define RK29_CAM_SENSOR_NAME_MT9T111 "mt9t111"
#define RK29_CAM_SENSOR_NAME_GT2005  "gt2005"
#define RK29_CAM_SENSOR_NAME_GC0307  "gc0307"
#define RK29_CAM_SENSOR_NAME_GC0308  "gc0308"
#define RK29_CAM_SENSOR_NAME_GC0309  "gc0309"
#define RK29_CAM_SENSOR_NAME_GC0312  "gc0312"
#define RK29_CAM_SENSOR_NAME_GC2015  "gc2015"
#define RK29_CAM_SENSOR_NAME_GC0328  "gc0328"
#define RK29_CAM_SENSOR_NAME_GC2035  "gc2035"
#define RK29_CAM_SENSOR_NAME_GC2145  "gc2145"
#define RK29_CAM_SENSOR_NAME_GC2155  "gc2155"
#define RK29_CAM_SENSOR_NAME_GC0329  "gc0329"
#define RK29_CAM_SENSOR_NAME_SIV120B "siv120b"
#define RK29_CAM_SENSOR_NAME_SIV121D "siv121d"
#define RK29_CAM_SENSOR_NAME_SID130B "sid130B"
#define RK29_CAM_SENSOR_NAME_HI253  "hi253"
#define RK29_CAM_SENSOR_NAME_HI704  "hi704"
#define RK29_CAM_SENSOR_NAME_NT99250 "nt99250"
#define RK29_CAM_SENSOR_NAME_SP0718  "sp0718"
#define RK29_CAM_SENSOR_NAME_SP0838  "sp0838"
#define RK29_CAM_SENSOR_NAME_SP2518  "sp2518"
#define RK29_CAM_SENSOR_NAME_S5K5CA  "s5k5ca"
#define RK29_CAM_ISP_NAME_MTK9335ISP "mtk9335isp"
#define RK29_CAM_SENSOR_NAME_HM2057  "hm2057"
#define RK29_CAM_SENSOR_NAME_HM5065  "hm5065"
#define RK29_CAM_ISP_NAME_ICATCH7002_MI1040 "icatchmi1040"
#define RK29_CAM_ISP_NAME_ICATCH7002_OV5693 "icatchov5693"
#define RK29_CAM_ISP_NAME_ICATCH7002_OV8825 "icatchov8825"
#define RK29_CAM_ISP_NAME_ICATCH7002_OV2720 "icatchov2720"
#define RK29_CAM_SENSOR_NAME_TP2825  "tp2825"
#define RK29_CAM_SENSOR_NAME_ADV7181 "adv7181"

/* Sensor full resolution define */
#define ov7675_FULL_RESOLUTION     0x30000 /* 0.3 megapixel */
#define ov9650_FULL_RESOLUTION     0x130000 /* 1.3 megapixel */
#define ov2640_FULL_RESOLUTION     0x200000 /* 2 megapixel */
#define ov2655_FULL_RESOLUTION     0x200000
#define ov2659_FULL_RESOLUTION     0x200000
#define gc2145_FULL_RESOLUTION     0x200000
#define gc2155_FULL_RESOLUTION     0x200000

#define ov2660_FULL_RESOLUTION     0x200000

#define ov7690_FULL_RESOLUTION     0x300000
#define ov3640_FULL_RESOLUTION     0x300000
#define ov3660_FULL_RESOLUTION     0x300000
#define ov5640_FULL_RESOLUTION     0x500000
#if defined(CONFIG_SOC_CAMERA_OV5642_INTERPOLATION_8M)
	#define ov5642_FULL_RESOLUTION     0x800000
#else
    #define ov5642_FULL_RESOLUTION     0x500000
#endif
#define s5k6aa_FULL_RESOLUTION     0x130000
#define mt9d112_FULL_RESOLUTION    0x200000
#define mt9d113_FULL_RESOLUTION    0x200000
#define mt9t111_FULL_RESOLUTION    0x300000
#define mt9p111_FULL_RESOLUTION    0x500000
#define gt2005_FULL_RESOLUTION     0x200000
#if defined(CONFIG_SOC_CAMERA_GC0308_INTERPOLATION_5M)
	#define gc0308_FULL_RESOLUTION     0x500000
#elif defined(CONFIG_SOC_CAMERA_GC0308_INTERPOLATION_3M)
	#define gc0308_FULL_RESOLUTION     0x300000
#elif defined(CONFIG_SOC_CAMERA_GC0308_INTERPOLATION_2M)
	#define gc0308_FULL_RESOLUTION     0x200000
#else
	#define gc0308_FULL_RESOLUTION     0x30000
#endif
#define gc0328_FULL_RESOLUTION     0x30000
#define gc0307_FULL_RESOLUTION     0x30000
#define gc0309_FULL_RESOLUTION     0x30000
#define gc0312_FULL_RESOLUTION     0x30000
#define gc2015_FULL_RESOLUTION     0x200000
#define siv120b_FULL_RESOLUTION     0x30000
#define siv121d_FULL_RESOLUTION     0x30000
#define sid130B_FULL_RESOLUTION     0x200000

#if defined(CONFIG_SOC_CAMERA_HI253_INTERPOLATION_5M)
	#define hi253_FULL_RESOLUTION       0x500000
#elif defined(CONFIG_SOC_CAMERA_HI253_INTERPOLATION_3M)
	#define hi253_FULL_RESOLUTION       0x300000
#else
	#define hi253_FULL_RESOLUTION       0x200000
#endif

#define hi704_FULL_RESOLUTION       0x30000
#define nt99250_FULL_RESOLUTION     0x200000
#define sp0718_FULL_RESOLUTION      0x30000
#define sp0838_FULL_RESOLUTION      0x30000
#define sp2518_FULL_RESOLUTION      0x200000
#define gc0329_FULL_RESOLUTION      0x30000
#define s5k5ca_FULL_RESOLUTION      0x300000
#define mtk9335isp_FULL_RESOLUTION  0x500000
#define gc2035_FULL_RESOLUTION      0x200000
#define hm2057_FULL_RESOLUTION      0x200000
#define hm5065_FULL_RESOLUTION      0x500000
#define nt99160_FULL_RESOLUTION     0x100000
#define nt99240_FULL_RESOLUTION     0x200000
#define nt99252_FULL_RESOLUTION     0x200000
#define nt99340_FULL_RESOLUTION     0x300000
#define icatchmi1040_FULL_RESOLUTION 0x200000
#define icatchov5693_FULL_RESOLUTION 0x500000
#define icatchov8825_FULL_RESOLUTION 0x800000
#define icatchov2720_FULL_RESOLUTION 0x210000
#define tp2825_FULL_RESOLUTION		0x100000
#define adv7181_FULL_RESOLUTION		0x100000
#define end_FULL_RESOLUTION         0x00

/* Sensor i2c addr define */
#define ov7675_I2C_ADDR             0x78
#define ov9650_I2C_ADDR             0x60
#define ov2640_I2C_ADDR             0x60
#define ov2655_I2C_ADDR             0x60
#define ov2659_I2C_ADDR             0x60
#define gc2145_I2C_ADDR             0x78
#define gc2155_I2C_ADDR             0x78

#define ov7690_I2C_ADDR             0x42
#define ov3640_I2C_ADDR             0x78
#define ov3660_I2C_ADDR             0x78
#define ov5640_I2C_ADDR             0x78
#define ov5642_I2C_ADDR             0x78

#define s5k6aa_I2C_ADDR             0x78
#define s5k5ca_I2C_ADDR             0x78

#define mt9d112_I2C_ADDR             0x78
#define mt9d113_I2C_ADDR             0x78
#define mt9t111_I2C_ADDR             0x78

#define mt9p111_I2C_ADDR            0x78
#define gt2005_I2C_ADDR             0x78
#define gc0307_I2C_ADDR             0x42
#define gc0328_I2C_ADDR             0x42
#define gc0308_I2C_ADDR             0x42
#define gc0309_I2C_ADDR             0x42
#define gc0312_I2C_ADDR             0x42
#define gc0329_I2C_ADDR             0x62
#define gc2015_I2C_ADDR             0x60
#define gc2035_I2C_ADDR             0x78

#define siv120b_I2C_ADDR             INVALID_VALUE
#define siv121d_I2C_ADDR             INVALID_VALUE
#define sid130B_I2C_ADDR             0x37

#define hi253_I2C_ADDR             0x40
#define hi704_I2C_ADDR             0x60

#define nt99160_I2C_ADDR             0x54
#define nt99240_I2C_ADDR             0x6c
#define nt99250_I2C_ADDR             0x6c
#define nt99252_I2C_ADDR             0x6c
#define nt99340_I2C_ADDR             0x76

#define sp0718_I2C_ADDR             0x42
#define sp0838_I2C_ADDR             0x30
#define sp0a19_I2C_ADDR             0x7a
#define sp1628_I2C_ADDR             0x78
#define sp2518_I2C_ADDR             0x60
#define mtk9335isp_I2C_ADDR         0x50
#define hm2057_I2C_ADDR             0x48
#define hm5065_I2C_ADDR             0x3e
#define icatchmi1040_I2C_ADDR		0x78
#define icatchov5693_I2C_ADDR       0x78
#define icatchov8825_I2C_ADDR       0x78
#define icatchov2720_I2C_ADDR       0x78
#define tp2825_I2C_ADDR				0x88
#define adv7181_I2C_ADDR		0x42
#define end_I2C_ADDR                INVALID_VALUE

/* Sensor power  active level define */
#define PWR_ACTIVE_HIGH                  0x01
#define PWR_ACTIVE_LOW					 0x0

/* Sensor power down active level define */
#define ov7675_PWRDN_ACTIVE             0x01
#define ov9650_PWRDN_ACTIVE             0x01
#define ov2640_PWRDN_ACTIVE             0x01
#define ov2655_PWRDN_ACTIVE             0x01
#define ov2659_PWRDN_ACTIVE             0x01
#define gc2145_PWRDN_ACTIVE             0x01
#define gc2155_PWRDN_ACTIVE             0x01

#define ov7690_PWRDN_ACTIVE             0x01
#define ov3640_PWRDN_ACTIVE             0x01
#define ov3660_PWRDN_ACTIVE             0x01
#define ov5640_PWRDN_ACTIVE             0x01
#define ov5642_PWRDN_ACTIVE             0x01

#define s5k6aa_PWRDN_ACTIVE             0x00
#define s5k5ca_PWRDN_ACTIVE             0x00

#define mt9d112_PWRDN_ACTIVE             0x01
#define mt9d113_PWRDN_ACTIVE             0x01
#define mt9t111_PWRDN_ACTIVE             0x01
#define mt9p111_PWRDN_ACTIVE             0x01

#define gt2005_PWRDN_ACTIVE             0x00
#define gc0307_PWRDN_ACTIVE             0x01
#define gc0308_PWRDN_ACTIVE             0x01
#define gc0328_PWRDN_ACTIVE             0x01
#define gc0309_PWRDN_ACTIVE             0x01
#define gc0329_PWRDN_ACTIVE             0x01
#define gc0312_PWRDN_ACTIVE             0x01
#define gc2015_PWRDN_ACTIVE             0x01
#define gc2035_PWRDN_ACTIVE             0x01

#define siv120b_PWRDN_ACTIVE             INVALID_VALUE
#define siv121d_PWRDN_ACTIVE             INVALID_VALUE
#define sid130B_PWRDN_ACTIVE             0x37

#define hi253_PWRDN_ACTIVE             0x01
#define hi704_PWRDN_ACTIVE             0x01

#define nt99160_PWRDN_ACTIVE             0x01
#define nt99240_PWRDN_ACTIVE             0x01
#define nt99250_PWRDN_ACTIVE             0x01
#define nt99252_PWRDN_ACTIVE             0x01
#define nt99340_PWRDN_ACTIVE             0x01

#define sp0718_PWRDN_ACTIVE             0x01
#define sp0838_PWRDN_ACTIVE             0x01
#define sp0a19_PWRDN_ACTIVE             0x01
#define sp1628_PWRDN_ACTIVE             0x01
#define sp2518_PWRDN_ACTIVE             0x01
#define hm2057_PWRDN_ACTIVE             0x01
#define hm5065_PWRDN_ACTIVE             0x00
#define mtk9335isp_PWRDN_ACTIVE         0x01
#define tp2825_PWRDN_ACTIVE				0x00
#define adv7181_PWRDN_ACTIVE		0x00
#define end_PWRDN_ACTIVE                INVALID_VALUE


/* Sensor power up sequence  define */
/* type: bit0-bit4 */
#define SENSOR_PWRSEQ_BEGIN         0x00
#define SENSOR_PWRSEQ_AVDD          0x01
#define SENSOR_PWRSEQ_DOVDD         0x02
#define SENSOR_PWRSEQ_DVDD          0x03
#define SENSOR_PWRSEQ_PWR           0x04
#define SENSOR_PWRSEQ_HWRST         0x05
#define SENSOR_PWRSEQ_PWRDN         0x06
#define SENSOR_PWRSEQ_CLKIN         0x07
#define SENSOR_PWRSEQ_END           0x0F
#define SENSOR_PWRSEQ_CNT           0x07

#define SENSOR_PWRSEQ_SET(type, idx)    (type << ((idx) * 4))
#define SENSOR_PWRSEQ_GET(seq, idx)     ((seq >> ((idx) * 4)) & 0x0f)

#define sensor_PWRSEQ_DEFAULT		\
	(SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_PWR, 0) |\
	SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_HWRST, 1) |\
	SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_PWRDN, 2) |\
	SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_CLKIN, 3))

#define ov7675_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define ov9650_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define ov2640_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define ov2655_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define ov2659_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define ov7690_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define ov3640_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define ov3660_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define ov5640_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define ov5642_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define gc2145_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define gc2155_PWRSEQ                   sensor_PWRSEQ_DEFAULT

#define s5k6aa_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define s5k5ca_PWRSEQ                   sensor_PWRSEQ_DEFAULT

#define mt9d112_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define mt9d113_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define mt9t111_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define mt9p111_PWRSEQ                   sensor_PWRSEQ_DEFAULT

#define gt2005_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define gc0307_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define gc0308_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define gc0328_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define gc0309_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define gc0329_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define gc0312_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define gc2015_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define gc2035_PWRSEQ                   sensor_PWRSEQ_DEFAULT

#define siv120b_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define siv121d_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define sid130B_PWRSEQ                   sensor_PWRSEQ_DEFAULT

#define hi253_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define hi704_PWRSEQ                   sensor_PWRSEQ_DEFAULT

#define nt99160_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define nt99240_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define nt99250_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define nt99252_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define nt99340_PWRSEQ                   sensor_PWRSEQ_DEFAULT

#define sp0718_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define sp0838_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define sp0a19_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define sp1628_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define sp2518_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define hm2057_PWRSEQ                   sensor_PWRSEQ_DEFAULT
#define hm5065_PWRSEQ		\
	(SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_PWR, 1) |\
	SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_HWRST, 2) |\
	SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_PWRDN, 0) |\
	SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_CLKIN, 3))
#define mtk9335isp_PWRSEQ			sensor_PWRSEQ_DEFAULT
#define icatchov5693_PWRSEQ		\
	(SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_PWR, 0) |\
	SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_HWRST, 2) |\
	SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_CLKIN, 1))

#define icatchov8825_PWRSEQ		\
	(SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_PWR, 0) |\
	SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_HWRST, 2) |\
	SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_CLKIN, 1))

#define icatchov2720_PWRSEQ		\
	(SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_PWR, 0) |\
	SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_HWRST, 2) |\
	SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_CLKIN, 1))

#define icatchmi1040_PWRSEQ		\
	(SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_PWR, 0) |\
	SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_HWRST, 2) |\
	SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_CLKIN, 1))

#define tp2825_PWRSEQ					sensor_PWRSEQ_DEFAULT
#define adv7181_PWRSEQ				sensor_PWRSEQ_DEFAULT

#define end_PWRSEQ         0xffffffff
/* Camera Sensor Must Define Macro End */
#endif
