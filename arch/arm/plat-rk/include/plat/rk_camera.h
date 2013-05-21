/*
    camera.h - PXA camera driver header file

    Copyright (C) 2003, Intel Corporation
    Copyright (C) 2008, Guennadi Liakhovetski <kernel@pengutronix.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef __ASM_ARCH_CAMERA_RK_H_
#define __ASM_ARCH_CAMERA_RK_H_

#include <linux/videodev2.h>
#include <media/soc_camera.h>
#include <linux/i2c.h>

#define RK29_CAM_PLATFORM_DEV_ID 33
#define RK_CAM_PLATFORM_DEV_ID_0 RK29_CAM_PLATFORM_DEV_ID
#define RK_CAM_PLATFORM_DEV_ID_1 (RK_CAM_PLATFORM_DEV_ID_0+1)
#define INVALID_VALUE -1
#ifndef INVALID_GPIO
#define INVALID_GPIO INVALID_VALUE
#endif
#define RK29_CAM_IO_SUCCESS 0
#define RK29_CAM_EIO_INVALID -1
#define RK29_CAM_EIO_REQUESTFAIL -2

#define RK29_CAM_POWERACTIVE_BITPOS	0x00
#define RK29_CAM_RESETACTIVE_BITPOS	0x01
#define RK29_CAM_POWERDNACTIVE_BITPOS 0x02
#define RK29_CAM_FLASHACTIVE_BITPOS	0x03

#define RK_CAM_NUM 6
#define RK29_CAM_SUPPORT_NUMS  RK_CAM_NUM
#define RK_CAM_SUPPORT_RESOLUTION 0x800000

#define _CONS(a,b) a##b
#define CONS(a,b) _CONS(a,b)

#define _CONS4(a,b,c,d) a##b##c##d
#define CONS4(a,b,c,d) _CONS4(a,b,c,d)

#define __STR(x) #x
#define _STR(x) __STR(x)
#define STR(x) _STR(x)

#define new_camera_device_ex(sensor_name,\
                             face,\
                             ori,\
                             pwr_io,\
                             pwr_active,\
                             rst_io,\
                             rst_active,\
                             pwdn_io,\
                             pwdn_active,\
                             flash_attach,\
                             res,\
                             mir,\
                             i2c_chl,\
                             i2c_spd,\
                             i2c_addr,\
                             cif_chl,\
                             mclk)\
        {\
            .dev = {\
                .i2c_cam_info = {\
                    I2C_BOARD_INFO(STR(sensor_name), i2c_addr>>1),\
                },\
                .link_info = {\
                	.bus_id= RK29_CAM_PLATFORM_DEV_ID+cif_chl,\
                	.i2c_adapter_id = i2c_chl,\
                	.module_name	= STR(sensor_name),\
                },\
                .device_info = {\
                	.name = "soc-camera-pdrv",\
                	.dev	= {\
                		.init_name = STR(CONS(_CONS(sensor_name,_),face)),\
                	},\
                },\
            },\
            .io = {\
                .gpio_power = pwr_io,\
                .gpio_reset = rst_io,\
                .gpio_powerdown = pwdn_io,\
                .gpio_flash = INVALID_GPIO,\
                .gpio_flag = ((pwr_active&0x01)<<RK29_CAM_POWERACTIVE_BITPOS)|((rst_active&0x01)<<RK29_CAM_RESETACTIVE_BITPOS)|((pwdn_active&0x01)<<RK29_CAM_POWERDNACTIVE_BITPOS),\
            },\
            .orientation = ori,\
            .resolution = res,\
            .mirror = mir,\
            .i2c_rate = i2c_spd,\
            .flash = flash_attach,\
            .pwdn_info = ((pwdn_active&0x10)|0x01),\
            .powerup_sequence = CONS(sensor_name,_PWRSEQ),\
            .mclk_rate = mclk,\
        }

#define new_camera_device(sensor_name,\
                          face,\
                          pwdn_io,\
                          flash_attach,\
                          mir,\
                          i2c_chl,\
                          cif_chl)    \
    new_camera_device_ex(sensor_name,\
                        face,\
                        INVALID_VALUE,\
                        INVALID_VALUE,\
                        INVALID_VALUE,\
                        INVALID_VALUE,\
                        INVALID_VALUE,\
                        pwdn_io,\
                        CONS(sensor_name,_PWRDN_ACTIVE),\
                        flash_attach,\
                        CONS(sensor_name,_FULL_RESOLUTION),\
                        mir,i2c_chl,\
                        100000,\
                        CONS(sensor_name,_I2C_ADDR),\
                        cif_chl,\
                        24)

#define new_camera_device_end new_camera_device_ex(end,end,\
                                    INVALID_VALUE,INVALID_VALUE,INVALID_VALUE,INVALID_VALUE,\
                                    INVALID_VALUE,INVALID_VALUE,INVALID_VALUE,INVALID_VALUE,\
                                    INVALID_VALUE,INVALID_VALUE,INVALID_VALUE,INVALID_VALUE,INVALID_VALUE,INVALID_VALUE,INVALID_VALUE)                        


/*---------------- Camera Sensor Must Define Macro Begin  ------------------------*/
#define RK29_CAM_SENSOR_OV7675 ov7675
#define RK29_CAM_SENSOR_OV9650 ov9650
#define RK29_CAM_SENSOR_OV2640 ov2640
#define RK29_CAM_SENSOR_OV2655 ov2655
#define RK29_CAM_SENSOR_OV2659 ov2659
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
#define RK29_CAM_SENSOR_NT99160 nt99160  //oyyf@rock-chips.com 
#define RK29_CAM_SENSOR_NT99240 nt99240  //oyyf@rock-chips.com 
#define RK29_CAM_SENSOR_NT99252 nt99252  //oyyf@rock-chips.com 
#define RK29_CAM_SENSOR_NT99340 nt99340  //oyyf@rock-chips.com 


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
#define RK29_CAM_SENSOR_NAME_GC2015  "gc2015"
#define RK29_CAM_SENSOR_NAME_GC0328  "gc0328"
#define RK29_CAM_SENSOR_NAME_GC2035  "gc2035"
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

//Sensor full resolution define
#define ov7675_FULL_RESOLUTION     0x30000            // 0.3 megapixel
#define ov9650_FULL_RESOLUTION     0x130000           // 1.3 megapixel   
#define ov2640_FULL_RESOLUTION     0x200000           // 2 megapixel
#define ov2655_FULL_RESOLUTION     0x200000           // 2 megapixel
#define ov2659_FULL_RESOLUTION     0x200000           // 2 megapixel
#define ov7690_FULL_RESOLUTION     0x300000           // 2 megapixel
#define ov3640_FULL_RESOLUTION     0x300000           // 3 megapixel
#define ov3660_FULL_RESOLUTION     0x300000           // 3 megapixel
#define ov5640_FULL_RESOLUTION     0x500000           // 5 megapixel
#if defined(CONFIG_SOC_CAMERA_OV5642_INTERPOLATION_8M)
	#define ov5642_FULL_RESOLUTION     0x800000            // 8 megapixel
#else	
    #define ov5642_FULL_RESOLUTION     0x500000           // 5 megapixel
#endif
#define s5k6aa_FULL_RESOLUTION     0x130000           // 1.3 megapixel
#define mt9d112_FULL_RESOLUTION    0x200000           // 2 megapixel
#define mt9d113_FULL_RESOLUTION    0x200000           // 2 megapixel
#define mt9t111_FULL_RESOLUTION    0x300000           // 3 megapixel
#define mt9p111_FULL_RESOLUTION    0x500000           // 5 megapixel
#define gt2005_FULL_RESOLUTION     0x200000           // 2 megapixel
#if defined(CONFIG_SOC_CAMERA_GC0308_INTERPOLATION_5M)
	#define gc0308_FULL_RESOLUTION     0x500000            // 5 megapixel
#elif defined(CONFIG_SOC_CAMERA_GC0308_INTERPOLATION_3M)
	#define gc0308_FULL_RESOLUTION     0x300000            // 3 megapixel
#elif defined(CONFIG_SOC_CAMERA_GC0308_INTERPOLATION_2M)
	#define gc0308_FULL_RESOLUTION     0x200000            // 2 megapixel
#else
	#define gc0308_FULL_RESOLUTION     0x30000            // 0.3 megapixel#endif
#endif
#define gc0328_FULL_RESOLUTION     0x30000            // 0.3 megapixel
#define gc0307_FULL_RESOLUTION     0x30000            // 0.3 megapixel
#define gc0309_FULL_RESOLUTION     0x30000            // 0.3 megapixel
#define gc2015_FULL_RESOLUTION     0x200000           // 2 megapixel
#define siv120b_FULL_RESOLUTION     0x30000            // 0.3 megapixel
#define siv121d_FULL_RESOLUTION     0x30000            // 0.3 megapixel
#define sid130B_FULL_RESOLUTION     0x200000           // 2 megapixel    

#if defined(CONFIG_SOC_CAMERA_HI253_INTERPOLATION_5M) 
	#define hi253_FULL_RESOLUTION       0x500000			// 5 megapixel
#elif defined(CONFIG_SOC_CAMERA_HI253_INTERPOLATION_3M)
	#define hi253_FULL_RESOLUTION       0x300000           // 3 megapixel
#else
	#define hi253_FULL_RESOLUTION       0x200000           // 2 megapixel
#endif

#define hi704_FULL_RESOLUTION       0x30000            // 0.3 megapixel
#define nt99250_FULL_RESOLUTION     0x200000           // 2 megapixel
#define sp0718_FULL_RESOLUTION      0x30000            // 0.3 megapixel
#define sp0838_FULL_RESOLUTION      0x30000            // 0.3 megapixel
#define sp2518_FULL_RESOLUTION      0x200000            // 2 megapixel
#define gc0329_FULL_RESOLUTION      0x30000            // 0.3 megapixel
#define s5k5ca_FULL_RESOLUTION      0x300000            // 3 megapixel
#define mtk9335isp_FULL_RESOLUTION  0x500000   		//5 megapixel
#define gc2035_FULL_RESOLUTION      0x200000            // 2 megapixel
#define hm2057_FULL_RESOLUTION      0x200000            // 2 megapixel
#define hm5065_FULL_RESOLUTION      0x500000            // 5 megapixel
#define nt99160_FULL_RESOLUTION     0x100000           // oyyf@rock-chips.com:  1 megapixel 1280*720    
#define nt99240_FULL_RESOLUTION     0x200000           // oyyf@rock-chips.com:  2 megapixel 1600*1200
#define nt99252_FULL_RESOLUTION     0x200000           // oyyf@rock-chips.com:  2 megapixel 1600*1200
#define nt99340_FULL_RESOLUTION     0x300000           // oyyf@rock-chips.com:  3 megapixel 2048*1536

#define end_FULL_RESOLUTION         0x00

//Sensor i2c addr define
#define ov7675_I2C_ADDR             0x78            
#define ov9650_I2C_ADDR             0x60           
#define ov2640_I2C_ADDR             0x60
#define ov2655_I2C_ADDR             0x60
#define ov2659_I2C_ADDR             0x60
#define ov7690_I2C_ADDR             0x42
#define ov3640_I2C_ADDR             0x78
#define ov3660_I2C_ADDR             0x78
#define ov5640_I2C_ADDR             0x78
#define ov5642_I2C_ADDR             0x78

#define s5k6aa_I2C_ADDR             0x78           //0x5a
#define s5k5ca_I2C_ADDR             0x78           //0x5a

#define mt9d112_I2C_ADDR             0x78
#define mt9d113_I2C_ADDR             0x78
#define mt9t111_I2C_ADDR             0x78           // 0x7a 

#define mt9p111_I2C_ADDR            0x78            //0x7a
#define gt2005_I2C_ADDR             0x78           
#define gc0307_I2C_ADDR             0x42
#define gc0328_I2C_ADDR             0x42
#define gc0308_I2C_ADDR             0x42
#define gc0309_I2C_ADDR             0x42
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
#define sp0838_I2C_ADDR             INVALID_VALUE  
#define sp0a19_I2C_ADDR             0x7a
#define sp1628_I2C_ADDR             0x78
#define sp2518_I2C_ADDR             0x60 
#define mtk9335isp_I2C_ADDR         0x50 
#define hm2057_I2C_ADDR             0x48
#define hm5065_I2C_ADDR             0x3e
#define end_I2C_ADDR                INVALID_VALUE


//Sensor power down active level define
#define ov7675_PWRDN_ACTIVE             0x01            
#define ov9650_PWRDN_ACTIVE             0x01           
#define ov2640_PWRDN_ACTIVE             0x01
#define ov2655_PWRDN_ACTIVE             0x01
#define ov2659_PWRDN_ACTIVE             0x01
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
#define end_PWRDN_ACTIVE                INVALID_VALUE


//Sensor power up sequence  define
//type: bit0-bit4
#define SENSOR_PWRSEQ_BEGIN         0x00
#define SENSOR_PWRSEQ_AVDD          0x01
#define SENSOR_PWRSEQ_DOVDD         0x02
#define SENSOR_PWRSEQ_DVDD          0x03
#define SENSOR_PWRSEQ_PWR           0x04
#define SENSOR_PWRSEQ_HWRST         0x05
#define SENSOR_PWRSEQ_PWRDN         0x06
#define SENSOR_PWRSEQ_CLKIN         0x07
#define SENSOR_PWRSEQ_END           0x0F

#define SENSOR_PWRSEQ_SET(type,idx)    (type<<(idx*4))
#define SENSOR_PWRSEQ_GET(seq,idx)     ((seq>>(idx*4))&0x0f)

#define sensor_PWRSEQ_DEFAULT      (SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_PWR,0)|\
                                    SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_HWRST,1)|\
                                    SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_PWRDN,2)|\
                                    SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_CLKIN,3))

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
#define hm5065_PWRSEQ                   (SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_PWR,1)|\
                                        SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_HWRST,2)|\
                                        SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_PWRDN,0)|\
                                        SENSOR_PWRSEQ_SET(SENSOR_PWRSEQ_CLKIN,3))
#define mtk9335isp_PWRSEQ               sensor_PWRSEQ_DEFAULT
#define end_PWRSEQ                      0xffffffff
                                          


/*---------------- Camera Sensor Must Define Macro End  ------------------------*/


//#define RK29_CAM_POWERACTIVE_BITPOS	0x00
#define RK29_CAM_POWERACTIVE_MASK	(1<<RK29_CAM_POWERACTIVE_BITPOS)
#define RK29_CAM_POWERACTIVE_H	(0x01<<RK29_CAM_POWERACTIVE_BITPOS)
#define RK29_CAM_POWERACTIVE_L	(0x00<<RK29_CAM_POWERACTIVE_BITPOS)

//#define RK29_CAM_RESETACTIVE_BITPOS	0x01
#define RK29_CAM_RESETACTIVE_MASK	(1<<RK29_CAM_RESETACTIVE_BITPOS)
#define RK29_CAM_RESETACTIVE_H	(0x01<<RK29_CAM_RESETACTIVE_BITPOS)
#define RK29_CAM_RESETACTIVE_L  (0x00<<RK29_CAM_RESETACTIVE_BITPOS)

//#define RK29_CAM_POWERDNACTIVE_BITPOS	0x02
#define RK29_CAM_POWERDNACTIVE_MASK	(1<<RK29_CAM_POWERDNACTIVE_BITPOS)
#define RK29_CAM_POWERDNACTIVE_H	(0x01<<RK29_CAM_POWERDNACTIVE_BITPOS)
#define RK29_CAM_POWERDNACTIVE_L	(0x00<<RK29_CAM_POWERDNACTIVE_BITPOS)

//#define RK29_CAM_FLASHACTIVE_BITPOS	0x03
#define RK29_CAM_FLASHACTIVE_MASK	(1<<RK29_CAM_FLASHACTIVE_BITPOS)
#define RK29_CAM_FLASHACTIVE_H	(0x01<<RK29_CAM_FLASHACTIVE_BITPOS)
#define RK29_CAM_FLASHACTIVE_L  (0x00<<RK29_CAM_FLASHACTIVE_BITPOS)


#define RK_CAM_SCALE_CROP_ARM      0
#define RK_CAM_SCALE_CROP_IPP      1
#define RK_CAM_SCALE_CROP_RGA      2
#define RK_CAM_SCALE_CROP_PP       3

#define RK_CAM_INPUT_FMT_YUV422    (1<<0)
#define RK_CAM_INPUT_FMT_RAW10     (1<<1)
#define RK_CAM_INPUT_FMT_RAW12     (1<<2)

/* v4l2_subdev_core_ops.ioctl  ioctl_cmd macro */
#define RK29_CAM_SUBDEV_ACTIVATE            0x00
#define RK29_CAM_SUBDEV_DEACTIVATE          0x01
#define RK29_CAM_SUBDEV_IOREQUEST			0x02
#define RK29_CAM_SUBDEV_CB_REGISTER         0x03

#define Sensor_HasBeen_PwrOff(a)            (a&0x01)
#define Sensor_Support_DirectResume(a)      ((a&0x10)==0x10)

enum rk29camera_ioctrl_cmd
{
	Cam_Power,
	Cam_Reset,
	Cam_PowerDown,
	Cam_Flash,
	Cam_Mclk
};

enum rk29sensor_power_cmd
{
    Sensor_Power,
	Sensor_Reset,
	Sensor_PowerDown,
	Sensor_Flash
};

enum rk29camera_flash_cmd
{
    Flash_Off,
    Flash_On,
    Flash_Torch
};

struct rk29camera_gpio_res {
    unsigned int gpio_reset;
    unsigned int gpio_power;
	unsigned int gpio_powerdown;
	unsigned int gpio_flash;
	unsigned int gpio_flag;
	unsigned int gpio_init;
	const char *dev_name;
};

struct rk29camera_mem_res {
	const char *name;
	unsigned int start;
	unsigned int size;
    void __iomem *vbase;
};
struct rk29camera_info {
    const char *dev_name;
    unsigned int orientation;
    struct v4l2_frmivalenum fival[10];
};

struct reginfo_t
{
	u16 reg;
	u16 val;
	u16 reg_len;
	u16 rev;
};
typedef struct rk_sensor_user_init_data{
	int rk_sensor_init_width;
	int rk_sensor_init_height;
	unsigned long rk_sensor_init_bus_param;
	enum v4l2_mbus_pixelcode rk_sensor_init_pixelcode;
	struct reginfo_t * rk_sensor_init_data;
	int rk_sensor_winseq_size;
	struct reginfo_t * rk_sensor_init_winseq;
	int rk_sensor_init_data_size;
}rk_sensor_user_init_data_s;

typedef struct rk_camera_device_register_info {
    struct i2c_board_info i2c_cam_info;
    struct soc_camera_link link_info;
    struct platform_device device_info;
}rk_camera_device_register_info_t;

struct rkcamera_platform_data {
    rk_camera_device_register_info_t dev;
    char dev_name[32];
    struct rk29camera_gpio_res io;
    int orientation;
    int resolution;   
    int mirror;       /* bit0:  0: mirror off
                                1: mirror on
                         bit1:  0: flip off
                                1: flip on
                      */
    int i2c_rate;     /* 100KHz = 100000  */                    
    bool flash;       /* true:  the sensor attached flash;
                         false: the sensor haven't attach flash;

                      */
    int pwdn_info;    /* bit4: 1: sensor isn't need to be init after exit stanby, it can streaming directly 
                               0: sensor must be init after exit standby;

                         bit0: 1: sensor power have been turn off;
                               0: sensor power is always on;
                      */

    long powerup_sequence;       /*  
                                    bit0-bit3 --- power up sequence first step;
                                    bit4-bit7 --- power up sequence second step;
                                     .....
                                  */
    int mclk_rate;       /* MHz : 24/48 */                                  
                      
};

struct rk29camera_platform_data {
    int (*io_init)(void);
    int (*io_deinit)(int sensor);
    int (*iomux)(int pin);
	int (*sensor_ioctrl)(struct device *dev,enum rk29camera_ioctrl_cmd cmd,int on);

    int (*sensor_register)(void);
    int (*sensor_mclk)(int cif_idx, int on, int clk_rate);
    
	rk_sensor_user_init_data_s* sensor_init_data[RK_CAM_NUM];
	struct rk29camera_gpio_res gpio_res[RK_CAM_NUM];
	struct rk29camera_mem_res meminfo;
	struct rk29camera_mem_res meminfo_cif1;
	struct rk29camera_info info[RK_CAM_NUM];
    rk_camera_device_register_info_t register_dev[RK_CAM_NUM];
    struct rkcamera_platform_data *register_dev_new;
};

struct rk29camera_platform_ioctl_cb {
    int (*sensor_power_cb)(struct rk29camera_gpio_res *res, int on);
    int (*sensor_reset_cb)(struct rk29camera_gpio_res *res, int on);
    int (*sensor_powerdown_cb)(struct rk29camera_gpio_res *res, int on);
    int (*sensor_flash_cb)(struct rk29camera_gpio_res *res, int on);
};

typedef struct rk29_camera_sensor_cb {
    int (*sensor_cb)(void *arg); 
    int (*scale_crop_cb)(struct work_struct *work);
}rk29_camera_sensor_cb_s;
#endif /* __ASM_ARCH_CAMERA_H_ */

