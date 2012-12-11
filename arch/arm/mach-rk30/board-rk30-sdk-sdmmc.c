/* arch/arm/mach-rk30/board-rk30-sdk-sdmmc.c
 *
 * Copyright (C) 2012 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * *
 * History:
 * ver1.0 add combo-wifi operateions. such as commit e049351a09c78db8a08aa5c49ce8eba0a3d6824e, at 2012-09-16
 * ver2.0 Unify all the file versions of board_xxxx_sdmmc.c, at 2012-11-05
 *
 * Content:
 * Part 1: define the gpio for SD-MMC-SDIO-Wifi functions according to your own projects.
         ***********************************************************************************
        * Please set the value according to your own project.
        ***********************************************************************************
 *
 * Part 2: define the gpio for the SDMMC controller. Based on the chip datasheet.
        ***********************************************************************************
        * Please do not change, each platform has a fixed set.  !!!!!!!!!!!!!!!!!!
        *  The system personnel will set the value depending on the specific arch datasheet,
        *  such as RK29XX, RK30XX.
        * If you have any doubt, please consult BangWang Xie.
        ***********************************************************************************
 *
 *.Part 3: The various operations of the SDMMC-SDIO module
        ***********************************************************************************
        * Please do not change, each platform has a fixed set.  !!!!!!!!!!!!!!!!!!
        * define the varaious operations for SDMMC module
        * Generally only the author of SDMMC module will modify this section.
        * If you have any doubt, please consult BangWang Xie.
        ***********************************************************************************
 *
 *.Part 4: The various operations of the Wifi-BT module
        ***********************************************************************************
        * Please do not change, each module has a fixed set.  !!!!!!!!!!!!!!!!!!
        * define the varaious operations for Wifi module
        * Generally only the author of Wifi module will modify this section.
        * If you have any doubt, please consult BangWang Xie, Weiguo Hu, and Weilong Gao.
        ***********************************************************************************
 *
 */

//1.Part 1: define the gpio for SD-MMC-SDIO-Wifi functions  according to your own projects.

/*************************************************************************
* define the gpio for sd-sdio-wifi module
*************************************************************************/
#if defined(CONFIG_SDMMC0_RK29_WRITE_PROTECT)
#define SDMMC0_WRITE_PROTECT_PIN	         RK30_PIN3_PB2	//According to your own project to set the value of write-protect-pin.
#define SDMMC0_WRITE_PROTECT_ENABLE_VALUE    GPIO_HIGH
#endif 

#if defined(CONFIG_SDMMC1_RK29_WRITE_PROTECT)
#define SDMMC1_WRITE_PROTECT_PIN	RK30_PIN3_PB3	//According to your own project to set the value of write-protect-pin.
#define SDMMC1_WRITE_PROTECT_ENABLE_VALUE    GPIO_HIGH
#endif
    
#if defined(CONFIG_RK29_SDIO_IRQ_FROM_GPIO)
#define RK29SDK_WIFI_SDIO_CARD_INT         RK30_PIN3_PD2
#endif

//define the card-detect-pin.
#if defined(CONFIG_ARCH_RK29)
//refer to file /arch/arm/mach-rk29/include/mach/Iomux.h
//define reset-pin
#define RK29SDK_SD_CARD_DETECT_N                RK29_PIN2_PA2  //According to your own project to set the value of card-detect-pin.
#define RK29SDK_SD_CARD_INSERT_LEVEL            GPIO_LOW         // set the voltage of insert-card. Please pay attention to the default setting.
#define RK29SDK_SD_CARD_DETECT_PIN_NAME         GPIO2A2_SDMMC0DETECTN_NAME
#define RK29SDK_SD_CARD_DETECT_IOMUX_FGPIO      GPIO2L_GPIO2A2
#define RK29SDK_SD_CARD_DETECT_IOMUX_FMUX       GPIO2L_SDMMC0_DETECT_N

#elif defined(CONFIG_ARCH_RK3066B)
//refer to file /arch/arm/mach-rk30/include/mach/iomux-rk3066b.h
//define reset-pin
#define RK29SDK_SD_CARD_DETECT_N                RK30_PIN3_PB0  //According to your own project to set the value of card-detect-pin.
#define RK29SDK_SD_CARD_INSERT_LEVEL            GPIO_LOW         // set the voltage of insert-card. Please pay attention to the default setting.
#define RK29SDK_SD_CARD_DETECT_PIN_NAME         GPIO3B0_SDMMC0DETECTN_NAME
#define RK29SDK_SD_CARD_DETECT_IOMUX_FGPIO      GPIO3B_GPIO3B0
#define RK29SDK_SD_CARD_DETECT_IOMUX_FMUX       GPIO3B_SDMMC0DETECTN

#elif defined(CONFIG_ARCH_RK30)&& !defined(CONFIG_ARCH_RK3066B) //for RK30,RK3066 SDK
//refer to file /arch/arm/mach-rk30/include/mach/Iomux.h
//define reset-pin
#define RK29SDK_SD_CARD_DETECT_N                RK30_PIN3_PB6  //According to your own project to set the value of card-detect-pin.
#define RK29SDK_SD_CARD_INSERT_LEVEL            GPIO_LOW         // set the voltage of insert-card. Please pay attention to the default setting.
#define RK29SDK_SD_CARD_DETECT_PIN_NAME         GPIO3B6_SDMMC0DETECTN_NAME
#define RK29SDK_SD_CARD_DETECT_IOMUX_FGPIO      GPIO3B_GPIO3B6
#define RK29SDK_SD_CARD_DETECT_IOMUX_FMUX       GPIO3B_SDMMC0_DETECT_N

#elif defined(CONFIG_ARCH_RK2928)
//refer to file ./arch/arm/mach-rk2928/include/mach/iomux.h
//define reset-pin
    #if defined(CONFIG_SDMMC0_RK29_SDCARD_DET_FROM_GPIO)
    //use gpio-interupt to dectec card in RK2926. Please pay attention to modify the default setting.
    #define RK29SDK_SD_CARD_DETECT_N                RK2928_PIN2_PA7  //According to your own project to set the value of card-detect-pin.
    #define RK29SDK_SD_CARD_INSERT_LEVEL            GPIO_LOW         // set the voltage of insert-card. Please pay attention to the default setting.
    #define RK29SDK_SD_CARD_DETECT_PIN_NAME         GPIO2A7_NAND_DPS_EMMC_CLKOUT_NAME
    #define RK29SDK_SD_CARD_DETECT_IOMUX_FGPIO      GPIO2A_GPIO2A7
    #define RK29SDK_SD_CARD_DETECT_IOMUX_FMUX       GPIO2A_EMMC_CLKOUT
    #else
    #define RK29SDK_SD_CARD_DETECT_N                RK2928_PIN1_PC1  //According to your own project to set the value of card-detect-pin.
    #define RK29SDK_SD_CARD_INSERT_LEVEL            GPIO_LOW         // set the voltage of insert-card. Please pay attention to the default setting.
    #define RK29SDK_SD_CARD_DETECT_PIN_NAME         GPIO1C1_MMC0_DETN_NAME
    #define RK29SDK_SD_CARD_DETECT_IOMUX_FGPIO      GPIO1C_GPIO1C1
    #define RK29SDK_SD_CARD_DETECT_IOMUX_FMUX       GPIO1C_MMC0_DETN
    #endif
#endif


//
// Define wifi module's power and reset gpio, and gpio sensitive level.
// Please set the value according to your own project.
//
#if defined(CONFIG_ARCH_RK30) && !defined(CONFIG_ARCH_RK3066B) //for RK30,RK3066 SDK
    // refer to file /arch/arm/mach-rk30/include/mach/Iomux.h
    #define WIFI_HOST_WAKE RK30_PIN3_PD2

    #if defined(CONFIG_RK903) || defined(CONFIG_RK901) || defined(CONFIG_RTL8192CU) || defined(CONFIG_RTL8188EU)
    //power
    #define RK30SDK_WIFI_GPIO_POWER_N               RK30_PIN3_PD0            
    #define RK30SDK_WIFI_GPIO_POWER_ENABLE_VALUE    GPIO_HIGH        
    //reset
    #define RK30SDK_WIFI_GPIO_POWER_PIN_NAME        GPIO3D0_SDMMC1PWREN_NAME
    #define RK30SDK_WIFI_GPIO_POWER_IOMUX_FGPIO     GPIO3D_GPIO3D0
    
    #elif defined(CONFIG_BCM4329) || defined(CONFIG_BCM4319) 
    //power
    #define RK30SDK_WIFI_GPIO_POWER_N               RK30_PIN3_PD0                 
    #define RK30SDK_WIFI_GPIO_POWER_ENABLE_VALUE    GPIO_HIGH                   
    #define RK30SDK_WIFI_GPIO_POWER_PIN_NAME        GPIO3D0_SDMMC1PWREN_NAME
    #define RK30SDK_WIFI_GPIO_POWER_IOMUX_FGPIO     GPIO3D_GPIO3D0
    //reset
    #define RK30SDK_WIFI_GPIO_RESET_N               RK30_PIN3_PD1
    #define RK30SDK_WIFI_GPIO_RESET_ENABLE_VALUE    GPIO_HIGH 
    #define RK30SDK_WIFI_GPIO_RESET_PIN_NAME        GPIO3D1_SDMMC1BACKENDPWR_NAME
    #define RK30SDK_WIFI_GPIO_RESET_IOMUX_FGPIO     GPIO3D_GPIO3D1

    #elif defined(CONFIG_MT6620)
        #if !defined(CONFIG_USE_SDMMC0_FOR_WIFI_DEVELOP_BOARD)
            #define USE_SDMMC_CONTROLLER_FOR_WIFI 1
        
            #if defined(CONFIG_MACH_RK30_PHONE_PAD)     // define the gpio for MT6620 in RK30_PHONE_PAD project.
                #define COMBO_MODULE_MT6620_CDT    0  //- 1--use Cdtech chip; 0--unuse CDT chip
                //power, PMU_EN
                #define RK30SDK_WIFI_GPIO_POWER_N                   RK30_PIN3_PC7            
                #define RK30SDK_WIFI_GPIO_POWER_ENABLE_VALUE        GPIO_HIGH        
                #define RK30SDK_WIFI_GPIO_POWER_PIN_NAME            GPIO3C7_SDMMC1WRITEPRT_NAME
                #define RK30SDK_WIFI_GPIO_POWER_IOMUX_FGPIO         GPIO3C_GPIO3C7
                #define RK30SDK_WIFI_GPIO_POWER_IOMUX_FMUX          GPIO3C_SDMMC1_WRITE_PRT
                //reset, DAIRST,SYSRST_B
                #define RK30SDK_WIFI_GPIO_RESET_N                   RK30_PIN3_PD1
                #define RK30SDK_WIFI_GPIO_RESET_ENABLE_VALUE        GPIO_HIGH 
                #define RK30SDK_WIFI_GPIO_RESET_PIN_NAME            GPIO3D1_SDMMC1BACKENDPWR_NAME
                #define RK30SDK_WIFI_GPIO_RESET_IOMUX_FGPIO         GPIO3D_GPIO3D1
                #define RK30SDK_WIFI_GPIO_RESET_IOMUX_FMUX          GPIO3D_SDMMC1_BACKEND_PWR
                //VDDIO
                //#define RK30SDK_WIFI_GPIO_VCCIO_WL                 RK30_PIN6_PB4
                //#define RK30SDK_WIFI_GPIO_VCCIO_WL_ENABLE_VALUE    GPIO_HIGH       
                //WIFI_INT_B
                #define RK30SDK_WIFI_GPIO_WIFI_INT_B                RK30_PIN4_PD2
                #define RK30SDK_WIFI_GPIO_WIFI_INT_B_ENABLE_VALUE   GPIO_HIGH 
                #define RK30SDK_WIFI_GPIO_WIFI_INT_B_PIN_NAME       GPIO4D2_SMCDATA10_TRACEDATA10_NAME
                #define RK30SDK_WIFI_GPIO_WIFI_INT_B_IOMUX_FGPIO    GPIO4D_GPIO4D2
                #define RK30SDK_WIFI_GPIO_WIFI_INT_B_IOMUX_FMUX     GPIO4D_SMC_DATA10
                //BGF_INT_B
                #define RK30SDK_WIFI_GPIO_BGF_INT_B                 RK30_PIN6_PA7
                #define RK30SDK_WIFI_GPIO_BGF_INT_B_ENABLE_VALUE    GPIO_HIGH 
                //#define RK30SDK_WIFI_GPIO_BGF_INT_B_PIN_NAME            GPIO3C6_SDMMC1DETECTN_NAME
                //#define RK30SDK_WIFI_GPIO_BGF_INT_B_IOMUX_FGPIO         GPIO3C_GPIO3C6
                //#define RK30SDK_WIFI_GPIO_BGF_INT_B_IOMUX_FMUX          GPIO3C_SDMMC1_DETECT_N
                //GPS_SYNC
                #define RK30SDK_WIFI_GPIO_GPS_SYNC                  RK30_PIN3_PD0
                #define RK30SDK_WIFI_GPIO_GPS_SYNC_ENABLE_VALUE     GPIO_HIGH 
                #define RK30SDK_WIFI_GPIO_GPS_SYNC_PIN_NAME         GPIO3D0_SDMMC1PWREN_NAME
                #define RK30SDK_WIFI_GPIO_GPS_SYNC_IOMUX_FGPIO      GPIO3D_GPIO3D0
                #define RK30SDK_WIFI_GPIO_GPS_SYNC_IOMUX_FMUX       GPIO3D_SDMMC1_PWR_EN
                
            #elif defined(CONFIG_MACH_RK3066_M8000R)     // define the gpio for MT6620 in CONFIG_MACH_RK3066_M8000R project.
                #define COMBO_MODULE_MT6620_CDT    1  //- 1--use Cdtech chip; 0--unuse CDT chip
                //power, PMU_EN
                #define RK30SDK_WIFI_GPIO_POWER_N                   RK30_PIN3_PC7            
                #define RK30SDK_WIFI_GPIO_POWER_ENABLE_VALUE        GPIO_HIGH        
                #define RK30SDK_WIFI_GPIO_POWER_PIN_NAME            GPIO3C7_SDMMC1WRITEPRT_NAME
                #define RK30SDK_WIFI_GPIO_POWER_IOMUX_FGPIO         GPIO3C_GPIO3C7
                #define RK30SDK_WIFI_GPIO_POWER_IOMUX_FMUX          GPIO3C_SDMMC1_WRITE_PRT
                //reset, DAIRST,SYSRST_B
                #define RK30SDK_WIFI_GPIO_RESET_N                   RK30_PIN3_PD1
                #define RK30SDK_WIFI_GPIO_RESET_ENABLE_VALUE        GPIO_HIGH 
                #define RK30SDK_WIFI_GPIO_RESET_PIN_NAME            GPIO3D1_SDMMC1BACKENDPWR_NAME
                #define RK30SDK_WIFI_GPIO_RESET_IOMUX_FGPIO         GPIO3D_GPIO3D1
                #define RK30SDK_WIFI_GPIO_RESET_IOMUX_FMUX          GPIO3D_SDMMC1_BACKEND_PWR
                //VDDIO
                #define RK30SDK_WIFI_GPIO_VCCIO_WL                  RK30_PIN0_PD2
                #define RK30SDK_WIFI_GPIO_VCCIO_WL_ENABLE_VALUE     GPIO_HIGH
                #define RK30SDK_WIFI_GPIO_VCCIO_WL_PIN_NAME         GPIO0D2_I2S22CHLRCKRX_SMCOEN_NAME
                #define RK30SDK_WIFI_GPIO_VCCIO_WL_IOMUX_FGPIO      GPIO0D_GPIO0D2
                #define RK30SDK_WIFI_GPIO_VCCIO_WL_IOMUX_FMUX       GPIO0D_I2S2_2CH_LRCK_RX
                //WIFI_INT_B
                #define RK30SDK_WIFI_GPIO_WIFI_INT_B                RK30_PIN3_PD2
                #define RK30SDK_WIFI_GPIO_WIFI_INT_B_ENABLE_VALUE   GPIO_HIGH 
                #define RK30SDK_WIFI_GPIO_WIFI_INT_B_PIN_NAME       GPIO3D2_SDMMC1INTN_NAME
                #define RK30SDK_WIFI_GPIO_WIFI_INT_B_IOMUX_FGPIO    GPIO3D_GPIO3D2
                #define RK30SDK_WIFI_GPIO_WIFI_INT_B_IOMUX_FMUX     GPIO3D_SDMMC1_INT_N
                //BGF_INT_B
                #define RK30SDK_WIFI_GPIO_BGF_INT_B                 RK30_PIN6_PA7
                #define RK30SDK_WIFI_GPIO_BGF_INT_B_ENABLE_VALUE    GPIO_HIGH 
                //#define RK30SDK_WIFI_GPIO_BGF_INT_B_PIN_NAME        GPIO3C6_SDMMC1DETECTN_NAME
                //#define RK30SDK_WIFI_GPIO_BGF_INT_B_IOMUX_FGPIO     GPIO3C_GPIO3C6
               // #define RK30SDK_WIFI_GPIO_BGF_INT_B_IOMUX_FMUX      GPIO3C_SDMMC1_DETECT_N
                //GPS_SYNC
                #define RK30SDK_WIFI_GPIO_GPS_SYNC                  RK30_PIN3_PD0
                #define RK30SDK_WIFI_GPIO_GPS_SYNC_ENABLE_VALUE     GPIO_HIGH 
                #define RK30SDK_WIFI_GPIO_GPS_SYNC_PIN_NAME         GPIO3D0_SDMMC1PWREN_NAME
                #define RK30SDK_WIFI_GPIO_GPS_SYNC_IOMUX_FGPIO      GPIO3D_GPIO3D0
                #define RK30SDK_WIFI_GPIO_GPS_SYNC_IOMUX_FMUX       GPIO3D_SDMMC1_PWR_EN

                #if COMBO_MODULE_MT6620_CDT
                //ANTSEL2
                //#define RK30SDK_WIFI_GPIO_ANTSEL2                   RK30_PIN4_PD4
                //#define RK30SDK_WIFI_GPIO_ANTSEL2_ENABLE_VALUE      GPIO_LOW    //use 6620 in CDT chip, LOW--work; High--no work.
                //#define RK30SDK_WIFI_GPIO_ANTSEL2_PIN_NAME          GPIO4D4_SMCDATA12_TRACEDATA12_NAME
                //#define RK30SDK_WIFI_GPIO_ANTSEL2_IOMUX_FGPIO       GPIO4D_GPIO4D4
                //#define RK30SDK_WIFI_GPIO_ANTSEL2_IOMUX_FMUX        GPIO4D_TRACE_DATA12
                //ANTSEL3
                //#define RK30SDK_WIFI_GPIO_ANTSEL3                   RK30_PIN4_PD3
                //#define RK30SDK_WIFI_GPIO_ANTSEL3_ENABLE_VALUE      GPIO_HIGH    //use 6620 in CDT chip, High--work; Low--no work..
                //#define RK30SDK_WIFI_GPIO_ANTSEL3_PIN_NAME          GPIO4D3_SMCDATA11_TRACEDATA11_NAME
                //#define RK30SDK_WIFI_GPIO_ANTSEL3_IOMUX_FGPIO       GPIO4D_GPIO4D3
                //#define RK30SDK_WIFI_GPIO_ANTSEL3_IOMUX_FMUX        GPIO4D_TRACE_DATA11
                //GPS_LAN
                //#define RK30SDK_WIFI_GPIO_GPS_LAN                   RK30_PIN4_PD6
                //#define RK30SDK_WIFI_GPIO_GPS_LAN_ENABLE_VALUE      GPIO_HIGH    //use 6620 in CDT chip, High--work; Low--no work..
                //#define RK30SDK_WIFI_GPIO_GPS_LAN_PIN_NAME          GPIO4D6_SMCDATA14_TRACEDATA14_NAME
                //#define RK30SDK_WIFI_GPIO_GPS_LAN_IOMUX_FGPIO       GPIO4D_GPIO4D6
                //#define RK30SDK_WIFI_GPIO_GPS_LAN_IOMUX_FMUX        GPIO4D_TRACE_DATA14
                #endif // #if COMBO_MODULE_MT6620_CDT--#endif
                
            #elif defined(CONFIG_MACH_SKYWORTH_T10_SDK)     // define the gpio for MT6620 in KYWORTH_T10 project.
                #define COMBO_MODULE_MT6620_CDT    0  //- 1--use Cdtech chip; 0--unuse CDT chip
                //power, PMU_EN
                #define RK30SDK_WIFI_GPIO_POWER_N                   RK30_PIN3_PD0            
                #define RK30SDK_WIFI_GPIO_POWER_ENABLE_VALUE        GPIO_HIGH        
                #define RK30SDK_WIFI_GPIO_POWER_PIN_NAME            GPIO3D0_SDMMC1PWREN_NAME
                #define RK30SDK_WIFI_GPIO_POWER_IOMUX_FGPIO         GPIO3D_GPIO3D0
                #define RK30SDK_WIFI_GPIO_POWER_IOMUX_FMUX          GPIO3D_SDMMC1_PWR_EN
                //reset, DAIRST,SYSRST_B
                #define RK30SDK_WIFI_GPIO_RESET_N                   RK30_PIN3_PD1
                #define RK30SDK_WIFI_GPIO_RESET_ENABLE_VALUE        GPIO_HIGH 
                #define RK30SDK_WIFI_GPIO_RESET_PIN_NAME            GPIO3D1_SDMMC1BACKENDPWR_NAME
                #define RK30SDK_WIFI_GPIO_RESET_IOMUX_FGPIO         GPIO3D_GPIO3D1
                #define RK30SDK_WIFI_GPIO_RESET_IOMUX_FMUX          GPIO3D_SDMMC1_BACKEND_PWR
                //VDDIO
                //#define RK30SDK_WIFI_GPIO_VCCIO_WL                 RK30_PIN6_PB4
                //#define RK30SDK_WIFI_GPIO_VCCIO_WL_ENABLE_VALUE    GPIO_HIGH       
                //WIFI_INT_B
                #define RK30SDK_WIFI_GPIO_WIFI_INT_B                RK30_PIN3_PD2
                #define RK30SDK_WIFI_GPIO_WIFI_INT_B_ENABLE_VALUE   GPIO_HIGH 
                #define RK30SDK_WIFI_GPIO_WIFI_INT_B_PIN_NAME       GPIO3D2_SDMMC1INTN_NAME
                #define RK30SDK_WIFI_GPIO_WIFI_INT_B_IOMUX_FGPIO    GPIO3D_GPIO3D2
                #define RK30SDK_WIFI_GPIO_WIFI_INT_B_IOMUX_FMUX     GPIO3D_SDMMC1_INT_N
                //BGF_INT_B
                #define RK30SDK_WIFI_GPIO_BGF_INT_B                 RK30_PIN3_PC6
                #define RK30SDK_WIFI_GPIO_BGF_INT_B_ENABLE_VALUE    GPIO_HIGH 
                #define RK30SDK_WIFI_GPIO_BGF_INT_B_PIN_NAME        GPIO3C6_SDMMC1DETECTN_NAME
                #define RK30SDK_WIFI_GPIO_BGF_INT_B_IOMUX_FGPIO     GPIO3C_GPIO3C6
                #define RK30SDK_WIFI_GPIO_BGF_INT_B_IOMUX_FMUX      GPIO3C_SDMMC1_DETECT_N
                //GPS_SYNC
                #define RK30SDK_WIFI_GPIO_GPS_SYNC                  RK30_PIN3_PC7
                #define RK30SDK_WIFI_GPIO_GPS_SYNC_ENABLE_VALUE     GPIO_HIGH 
                #define RK30SDK_WIFI_GPIO_GPS_SYNC_PIN_NAME         GPIO3C7_SDMMC1WRITEPRT_NAME
                #define RK30SDK_WIFI_GPIO_GPS_SYNC_IOMUX_FGPIO      GPIO3C_GPIO3C7
                #define RK30SDK_WIFI_GPIO_GPS_SYNC_IOMUX_FMUX       GPIO3C_SDMMC1_WRITE_PRT    
            
            #else //For exmpale, to define the gpio for MT6620 in RK30SDK project.
                #define COMBO_MODULE_MT6620_CDT    1  //- 1--use Cdtech chip; 0--unuse CDT chip
                //power
                #define RK30SDK_WIFI_GPIO_POWER_N                   RK30_PIN3_PD0            
                #define RK30SDK_WIFI_GPIO_POWER_ENABLE_VALUE        GPIO_HIGH        
                #define RK30SDK_WIFI_GPIO_POWER_PIN_NAME            GPIO3D0_SDMMC1PWREN_NAME
                #define RK30SDK_WIFI_GPIO_POWER_IOMUX_FGPIO         GPIO3D_GPIO3D0
                #define RK30SDK_WIFI_GPIO_POWER_IOMUX_FMUX          GPIO3D_SDMMC1_PWR_EN
                //reset
                #define RK30SDK_WIFI_GPIO_RESET_N                   RK30_PIN3_PD1
                #define RK30SDK_WIFI_GPIO_RESET_ENABLE_VALUE        GPIO_HIGH 
                #define RK30SDK_WIFI_GPIO_RESET_PIN_NAME            GPIO3D1_SDMMC1BACKENDPWR_NAME
                #define RK30SDK_WIFI_GPIO_RESET_IOMUX_FGPIO         GPIO3D_GPIO3D1
                #define RK30SDK_WIFI_GPIO_RESET_IOMUX_FMUX          GPIO3D_SDMMC1_BACKEND_PWR
                //VDDIO
                //#define RK30SDK_WIFI_GPIO_VCCIO_WL                  RK30_PIN0_PD2
                //#define RK30SDK_WIFI_GPIO_VCCIO_WL_ENABLE_VALUE     GPIO_HIGH
                //#define RK30SDK_WIFI_GPIO_VCCIO_WL_PIN_NAME         GPIO0D2_I2S22CHLRCKRX_SMCOEN_NAME
                //#define RK30SDK_WIFI_GPIO_VCCIO_WL_IOMUX_FGPIO      GPIO0D_GPIO0D2
                //#define RK30SDK_WIFI_GPIO_VCCIO_WL_IOMUX_FMUX       GPIO0D_I2S2_2CH_LRCK_RX       
                //WIFI_INT_B
                #define RK30SDK_WIFI_GPIO_WIFI_INT_B                RK30_PIN3_PD2
                #define RK30SDK_WIFI_GPIO_WIFI_INT_B_ENABLE_VALUE   GPIO_HIGH 
                #define RK30SDK_WIFI_GPIO_WIFI_INT_B_PIN_NAME       GPIO3D2_SDMMC1INTN_NAME
                #define RK30SDK_WIFI_GPIO_WIFI_INT_B_IOMUX_FGPIO    GPIO3D_GPIO3D2
                #define RK30SDK_WIFI_GPIO_WIFI_INT_B_IOMUX_FMUX     GPIO3D_SDMMC1_INT_N
                //BGF_INT_B
                #define RK30SDK_WIFI_GPIO_BGF_INT_B                 RK30_PIN3_PC6
                #define RK30SDK_WIFI_GPIO_BGF_INT_B_ENABLE_VALUE    GPIO_HIGH 
                #define RK30SDK_WIFI_GPIO_BGF_INT_B_PIN_NAME        GPIO3C6_SDMMC1DETECTN_NAME
                #define RK30SDK_WIFI_GPIO_BGF_INT_B_IOMUX_FGPIO     GPIO3C_GPIO3C6
                #define RK30SDK_WIFI_GPIO_BGF_INT_B_IOMUX_FMUX      GPIO3C_SDMMC1_DETECT_N
                //GPS_SYNC
                #define RK30SDK_WIFI_GPIO_GPS_SYNC                  RK30_PIN3_PC7
                #define RK30SDK_WIFI_GPIO_GPS_SYNC_ENABLE_VALUE     GPIO_HIGH 
                #define RK30SDK_WIFI_GPIO_GPS_SYNC_PIN_NAME         GPIO3C7_SDMMC1WRITEPRT_NAME
                #define RK30SDK_WIFI_GPIO_GPS_SYNC_IOMUX_FGPIO      GPIO3C_GPIO3C7
                #define RK30SDK_WIFI_GPIO_GPS_SYNC_IOMUX_FMUX       GPIO3C_SDMMC1_WRITE_PRT

                #if COMBO_MODULE_MT6620_CDT
                //ANTSEL2
                #define RK30SDK_WIFI_GPIO_ANTSEL2                   RK30_PIN4_PD4
                #define RK30SDK_WIFI_GPIO_ANTSEL2_ENABLE_VALUE      GPIO_LOW    //use 6620 in CDT chip, LOW--work; High--no work.
                #define RK30SDK_WIFI_GPIO_ANTSEL2_PIN_NAME          GPIO4D4_SMCDATA12_TRACEDATA12_NAME
                #define RK30SDK_WIFI_GPIO_ANTSEL2_IOMUX_FGPIO       GPIO4D_GPIO4D4
                #define RK30SDK_WIFI_GPIO_ANTSEL2_IOMUX_FMUX        GPIO4D_TRACE_DATA12
                //ANTSEL3
                #define RK30SDK_WIFI_GPIO_ANTSEL3                   RK30_PIN4_PD3
                #define RK30SDK_WIFI_GPIO_ANTSEL3_ENABLE_VALUE      GPIO_HIGH    //use 6620 in CDT chip, High--work; Low--no work..
                #define RK30SDK_WIFI_GPIO_ANTSEL3_PIN_NAME          GPIO4D3_SMCDATA11_TRACEDATA11_NAME
                #define RK30SDK_WIFI_GPIO_ANTSEL3_IOMUX_FGPIO       GPIO4D_GPIO4D3
                #define RK30SDK_WIFI_GPIO_ANTSEL3_IOMUX_FMUX        GPIO4D_TRACE_DATA11
                //GPS_LAN
                #define RK30SDK_WIFI_GPIO_GPS_LAN                   RK30_PIN4_PD6
                #define RK30SDK_WIFI_GPIO_GPS_LAN_ENABLE_VALUE      GPIO_HIGH    //use 6620 in CDT chip, High--work; Low--no work..
                #define RK30SDK_WIFI_GPIO_GPS_LAN_PIN_NAME          GPIO4D6_SMCDATA14_TRACEDATA14_NAME
                #define RK30SDK_WIFI_GPIO_GPS_LAN_IOMUX_FGPIO       GPIO4D_GPIO4D6
                //#define RK30SDK_WIFI_GPIO_GPS_LAN_IOMUX_FMUX        GPIO4D_TRACE_DATA14
                #endif // #if COMBO_MODULE_MT6620_CDT--#endif
            #endif
        #endif// #endif --#if !defined(CONFIG_USE_SDMMC0_FOR_WIFI_DEVELOP_BOARD)
    #endif 
#elif defined(CONFIG_ARCH_RK3066B)//refer to file /arch/arm/mach-rk30/include/mach/iomux-rk3066b.h
    #define WIFI_HOST_WAKE RK30_PIN3_PD2
     
    #if defined(CONFIG_RTL8192CU) || defined(CONFIG_RTL8188EU) 
        #define RK30SDK_WIFI_GPIO_POWER_N               RK30_PIN3_PD0            
        #define RK30SDK_WIFI_GPIO_POWER_ENABLE_VALUE    GPIO_LOW//GPIO_HIGH        
        #define RK30SDK_WIFI_GPIO_POWER_PIN_NAME        GPIO3D0_SDMMC1PWREN_MIIMD_NAME
        #define RK30SDK_WIFI_GPIO_POWER_IOMUX_FGPIO     GPIO3D_GPIO3D0
        
    #elif defined(CONFIG_BCM4329) || defined(CONFIG_BCM4319) || defined(CONFIG_RK903) || defined(CONFIG_RK901)
        #define RK30SDK_WIFI_GPIO_POWER_N               RK30_PIN3_PD0                 
        #define RK30SDK_WIFI_GPIO_POWER_ENABLE_VALUE    GPIO_HIGH                   
        #define RK30SDK_WIFI_GPIO_POWER_PIN_NAME        GPIO3D0_SDMMC1PWREN_MIIMD_NAME
        #define RK30SDK_WIFI_GPIO_POWER_IOMUX_FGPIO     GPIO3D_GPIO3D0

        #define RK30SDK_WIFI_GPIO_RESET_N               RK30_PIN2_PA7
        #define RK30SDK_WIFI_GPIO_RESET_ENABLE_VALUE    GPIO_HIGH 
        #define RK30SDK_WIFI_GPIO_RESET_PIN_NAME        GPIO2A7_LCDC1DATA7_SMCDATA7_TRACEDATA7_NAME
        #define RK30SDK_WIFI_GPIO_RESET_IOMUX_FGPIO     GPIO2A_GPIO2A7
    #endif   
#elif defined(CONFIG_ARCH_RK2928) //refer to file ./arch/arm/mach-rk2928/include/mach/iomux.h
    #define WIFI_HOST_WAKE RK2928_PIN3_PC0 

	#if defined(CONFIG_RK903) || defined(CONFIG_RK901) || defined(CONFIG_BCM4329) || defined(CONFIG_BCM4319)
        #define RK30SDK_WIFI_GPIO_POWER_N               RK2928_PIN0_PD6
        #define RK30SDK_WIFI_GPIO_POWER_ENABLE_VALUE    GPIO_HIGH
        #define RK30SDK_WIFI_GPIO_POWER_PIN_NAME        GPIO0D6_MMC1_PWREN_NAME
        #define RK30SDK_WIFI_GPIO_POWER_IOMUX_FGPIO     GPIO0D_GPIO0D6

        #define RK30SDK_WIFI_GPIO_RESET_N               RK2928_PIN3_PC2
        #define RK30SDK_WIFI_GPIO_RESET_ENABLE_VALUE    GPIO_HIGH
        //You need not define the pin-iomux-name due to the pin only used gpio.
        //#define RK30SDK_WIFI_GPIO_RESET_PIN_NAME        GPIO3C2_SDMMC1DATA1_NAME 
        //#define RK30SDK_WIFI_GPIO_RESET_IOMUX_FGPIO     GPIO3C_GPIO3C2
    	
    #elif  defined(CONFIG_RDA5990)
        #define RK30SDK_WIFI_GPIO_POWER_N               INVALID_GPIO//RK2928_PIN0_PD6
        #define RK30SDK_WIFI_GPIO_POWER_ENABLE_VALUE    GPIO_HIGH
        //#define RK30SDK_WIFI_GPIO_POWER_PIN_NAME        GPIO0B6_MMC1_PWREN_NAME
        //#define RK30SDK_WIFI_GPIO_POWER_IOMUX_FGPIO     GPIO0B_GPIO0B6
        
        #define RK30SDK_WIFI_GPIO_RESET_N               INVALID_GPIO//RK2928_PIN3_PC2
        #define RK30SDK_WIFI_GPIO_RESET_ENABLE_VALUE    GPIO_HIGH
        //#define RK30SDK_WIFI_GPIO_RESET_PIN_NAME        GPIO3C2_SDMMC1DATA1_NAME
        //#define RK30SDK_WIFI_GPIO_RESET_IOMUX_FGPIO     GPIO3C_GPIO3C2


        #elif defined(CONFIG_RTL8192CU) || defined(CONFIG_RTL8188EU) 
        #define RK30SDK_WIFI_GPIO_POWER_N               RK2928_PIN0_PD6
        #define RK30SDK_WIFI_GPIO_POWER_ENABLE_VALUE    GPIO_LOW
        #define RK30SDK_WIFI_GPIO_POWER_PIN_NAME        GPIO0D6_MMC1_PWREN_NAME
        #define RK30SDK_WIFI_GPIO_POWER_IOMUX_FGPIO     GPIO0D_GPIO0D6

    #endif
#endif

    
//1. Part 2: to define the gpio for the SDMMC controller. Based on the chip datasheet.
/*************************************************************************
* define the gpio for SDMMC module on various platforms
* Generally only system personnel will modify this part
*************************************************************************/
#if defined(CONFIG_ARCH_RK29)
//refer to file /arch/arm/mach-rk29/include/mach/Iomux.h
//define PowerEn-pin
#define RK29SDK_SD_CARD_PWR_EN                  RK29_PIN5_PD5  
#define RK29SDK_SD_CARD_PWR_EN_LEVEL            GPIO_LOW   
#define RK29SDK_SD_CARD_PWR_EN_PIN_NAME         GPIO5D5_SDMMC0PWREN_NAME
#define RK29SDK_SD_CARD_PWR_EN_IOMUX_FGPIO      GPIO5H_GPIO5D5
#define RK29SDK_SD_CARD_PWR_EN_IOMUX_FMUX       GPIO5H_SDMMC0_PWR_EN

#elif defined(CONFIG_ARCH_RK3066B)
//refer to file /arch/arm/mach-rk30/include/mach/iomux-rk3066b.h
//define PowerEn-pin
#define RK29SDK_SD_CARD_PWR_EN                  RK30_PIN3_PA1
#define RK29SDK_SD_CARD_PWR_EN_LEVEL            GPIO_LOW 
#define RK29SDK_SD_CARD_PWR_EN_PIN_NAME         GPIO3A1_SDMMC0PWREN_NAME
#define RK29SDK_SD_CARD_PWR_EN_IOMUX_FGPIO      GPIO3A_GPIO3A1
#define RK29SDK_SD_CARD_PWR_EN_IOMUX_FMUX       GPIO3A_SDMMC0PWREN

#elif defined(CONFIG_ARCH_RK30)&& !defined(CONFIG_ARCH_RK3066B) //for RK30,RK3066 SDK
//refer to file /arch/arm/mach-rk30/include/mach/Iomux.h
//define PowerEn-pin
#define RK29SDK_SD_CARD_PWR_EN                  RK30_PIN3_PA7 
#define RK29SDK_SD_CARD_PWR_EN_LEVEL            GPIO_LOW
#define RK29SDK_SD_CARD_PWR_EN_PIN_NAME         GPIO3A7_SDMMC0PWREN_NAME
#define RK29SDK_SD_CARD_PWR_EN_IOMUX_FGPIO      GPIO3A_GPIO3A7
#define RK29SDK_SD_CARD_PWR_EN_IOMUX_FMUX       GPIO3A_SDMMC0_PWR_EN

#elif defined(CONFIG_ARCH_RK2928)
//refer to file ./arch/arm/mach-rk2928/include/mach/iomux.h
//define PowerEn-pin
#define RK29SDK_SD_CARD_PWR_EN                  RK2928_PIN1_PB6
#define RK29SDK_SD_CARD_PWR_EN_LEVEL            GPIO_LOW 
#define RK29SDK_SD_CARD_PWR_EN_PIN_NAME         GPIO1B6_MMC0_PWREN_NAME
#define RK29SDK_SD_CARD_PWR_EN_IOMUX_FGPIO      GPIO1B_GPIO1B6
#define RK29SDK_SD_CARD_PWR_EN_IOMUX_FMUX       GPIO1B_MMC0_PWREN

#endif

#if defined(CONFIG_ARCH_RK30)&& !defined(CONFIG_ARCH_RK3066B)//for RK30,RK3066 SDK
/*
* define the gpio for sdmmc0
*/
struct rksdmmc_gpio_board rksdmmc0_gpio_init = {

     .clk_gpio       = {
        .io             = RK30_PIN3_PB0,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3B0_SDMMC0CLKOUT_NAME,
            .fgpio      = GPIO3B_GPIO3B0,
            .fmux       = GPIO3B_SDMMC0_CLKOUT,
        },
    },   

    .cmd_gpio           = {
        .io             = RK30_PIN3_PB1,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3B1_SDMMC0CMD_NAME,
            .fgpio      = GPIO3B_GPIO3B1,
            .fmux       = GPIO3B_SDMMC0_CMD,
        },
    },      

   .data0_gpio       = {
        .io             = RK30_PIN3_PB2,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3B2_SDMMC0DATA0_NAME,
            .fgpio      = GPIO3B_GPIO3B2,
            .fmux       = GPIO3B_SDMMC0_DATA0,
        },
    },      

    .data1_gpio       = {
        .io             = RK30_PIN3_PB3,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3B3_SDMMC0DATA1_NAME,
            .fgpio      = GPIO3B_GPIO3B3,
            .fmux       = GPIO3B_SDMMC0_DATA1,
        },
    },      

    .data2_gpio       = {
        .io             = RK30_PIN3_PB4,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3B4_SDMMC0DATA2_NAME,
            .fgpio      = GPIO3B_GPIO3B4,
            .fmux       = GPIO3B_SDMMC0_DATA2,
        },
    }, 

    .data3_gpio       = {
        .io             = RK30_PIN3_PB5,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3B5_SDMMC0DATA3_NAME,
            .fgpio      = GPIO3B_GPIO3B5,
            .fmux       = GPIO3B_SDMMC0_DATA3,
        },
    }, 
    
    .power_en_gpio      = {   
#if defined(RK29SDK_SD_CARD_PWR_EN) || (INVALID_GPIO != RK29SDK_SD_CARD_PWR_EN)
        .io             = RK29SDK_SD_CARD_PWR_EN,
        .enable         = RK29SDK_SD_CARD_PWR_EN_LEVEL,
        #ifdef RK29SDK_SD_CARD_PWR_EN_PIN_NAME
        .iomux          = {
            .name       = RK29SDK_SD_CARD_PWR_EN_PIN_NAME,
            #ifdef RK29SDK_SD_CARD_PWR_EN_IOMUX_FGPIO
            .fgpio      = RK29SDK_SD_CARD_PWR_EN_IOMUX_FGPIO,
            #endif
            #ifdef RK29SDK_SD_CARD_PWR_EN_IOMUX_FMUX
            .fmux       = RK29SDK_SD_CARD_PWR_EN_IOMUX_FMUX,
            #endif
        },
        #endif
#else
        .io             = INVALID_GPIO,
        .enable         = GPIO_LOW,
#endif
    }, 

    .detect_irq       = {
#if defined(RK29SDK_SD_CARD_DETECT_N) || (INVALID_GPIO != RK29SDK_SD_CARD_DETECT_N)  
        .io             = RK29SDK_SD_CARD_DETECT_N,
        .enable         = RK29SDK_SD_CARD_INSERT_LEVEL,
        #ifdef RK29SDK_SD_CARD_DETECT_PIN_NAME
        .iomux          = {
            .name       = RK29SDK_SD_CARD_DETECT_PIN_NAME,
            #ifdef RK29SDK_SD_CARD_DETECT_IOMUX_FGPIO
            .fgpio      = RK29SDK_SD_CARD_DETECT_IOMUX_FGPIO,
            #endif
            #ifdef RK29SDK_SD_CARD_DETECT_IOMUX_FMUX
            .fmux       = RK29SDK_SD_CARD_DETECT_IOMUX_FMUX,
            #endif
        },
        #endif
#else
        .io             = INVALID_GPIO,
        .enable         = GPIO_LOW,
#endif            
    },
};


/*
* define the gpio for sdmmc1
*/
static struct rksdmmc_gpio_board rksdmmc1_gpio_init = {

     .clk_gpio       = {
        .io             = RK30_PIN3_PC5,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3C5_SDMMC1CLKOUT_NAME,
            .fgpio      = GPIO3C_GPIO3C5,
            .fmux       = GPIO3B_SDMMC0_CLKOUT,
        },
    },   

    .cmd_gpio           = {
        .io             = RK30_PIN3_PC0,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3C0_SMMC1CMD_NAME,
            .fgpio      = GPIO3C_GPIO3C0,
            .fmux       = GPIO3B_SDMMC0_CMD,
        },
    },      

   .data0_gpio       = {
        .io             = RK30_PIN3_PC1,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3C1_SDMMC1DATA0_NAME,
            .fgpio      = GPIO3C_GPIO3C1,
            .fmux       = GPIO3B_SDMMC0_DATA0,
        },
    },      

    .data1_gpio       = {
        .io             = RK30_PIN3_PC2,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3C2_SDMMC1DATA1_NAME,
            .fgpio      = GPIO3C_GPIO3C2,
            .fmux       = GPIO3B_SDMMC0_DATA1,
        },
    },      

    .data2_gpio       = {
        .io             = RK30_PIN3_PC3,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3C3_SDMMC1DATA2_NAME,
            .fgpio      = GPIO3C_GPIO3C3,
            .fmux       = GPIO3B_SDMMC0_DATA2,
        },
    }, 

    .data3_gpio       = {
        .io             = RK30_PIN3_PC4,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3C4_SDMMC1DATA3_NAME,
            .fgpio      = GPIO3C_GPIO3C4,
            .fmux       = GPIO3B_SDMMC0_DATA3,
        },
    }, 
};
 // ---end -#if defined(CONFIG_ARCH_RK30)&& !defined(CONFIG_ARCH_RK3066B)

#elif defined(CONFIG_ARCH_RK3066B)

/*
* define the gpio for sdmmc0
*/
static struct rksdmmc_gpio_board rksdmmc0_gpio_init = {

     .clk_gpio       = {
        .io             = RK30_PIN3_PA2,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3A2_SDMMC0CLKOUT_NAME,
            .fgpio      = GPIO3A_GPIO3A2,
            .fmux       = GPIO3A_SDMMC0CLKOUT,
        },
    },   

    .cmd_gpio           = {
        .io             = RK30_PIN3_PA3,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3A3_SDMMC0CMD_NAME,
            .fgpio      = GPIO3A_GPIO3A3,
            .fmux       = GPIO3A_SDMMC0CMD,
        },
    },      

   .data0_gpio       = {
        .io             = RK30_PIN3_PA4,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3A4_SDMMC0DATA0_NAME,
            .fgpio      = GPIO3A_GPIO3A4,
            .fmux       = GPIO3A_SDMMC0DATA0,
        },
    },      

    .data1_gpio       = {
        .io             = RK30_PIN3_PA5,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3A5_SDMMC0DATA1_NAME,
            .fgpio      = GPIO3A_GPIO3A5,
            .fmux       = GPIO3A_SDMMC0DATA1,
        },
    },      

    .data2_gpio       = {
        .io             = RK30_PIN3_PA6,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3A6_SDMMC0DATA2_NAME,
            .fgpio      = GPIO3A_GPIO3A6,
            .fmux       = GPIO3A_SDMMC0DATA2,
        },
    }, 

    .data3_gpio       = {
        .io             = RK30_PIN3_PA7,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3A7_SDMMC0DATA3_NAME,
            .fgpio      = GPIO3A_GPIO3A7,
            .fmux       = GPIO3A_SDMMC0DATA3,
        },
    }, 

               
    .power_en_gpio      = {   
#if defined(RK29SDK_SD_CARD_PWR_EN) || (INVALID_GPIO != RK29SDK_SD_CARD_PWR_EN)
                    .io             = RK29SDK_SD_CARD_PWR_EN,
                    .enable         = RK29SDK_SD_CARD_PWR_EN_LEVEL,
        #ifdef RK29SDK_SD_CARD_PWR_EN_PIN_NAME
                    .iomux          = {
                        .name       = RK29SDK_SD_CARD_PWR_EN_PIN_NAME,
            #ifdef RK29SDK_SD_CARD_PWR_EN_IOMUX_FGPIO
                        .fgpio      = RK29SDK_SD_CARD_PWR_EN_IOMUX_FGPIO,
            #endif
            #ifdef RK29SDK_SD_CARD_PWR_EN_IOMUX_FMUX
                        .fmux       = RK29SDK_SD_CARD_PWR_EN_IOMUX_FMUX,
            #endif
                    },
        #endif
#else
                    .io             = INVALID_GPIO,
                    .enable         = GPIO_LOW,
#endif
                }, 
            
        .detect_irq       = {
#if defined(RK29SDK_SD_CARD_DETECT_N) || (INVALID_GPIO != RK29SDK_SD_CARD_DETECT_N)  
                    .io             = RK29SDK_SD_CARD_DETECT_N,
                    .enable         = RK29SDK_SD_CARD_INSERT_LEVEL,
        #ifdef RK29SDK_SD_CARD_DETECT_PIN_NAME
                    .iomux          = {
                        .name       = RK29SDK_SD_CARD_DETECT_PIN_NAME,
            #ifdef RK29SDK_SD_CARD_DETECT_IOMUX_FGPIO
                        .fgpio      = RK29SDK_SD_CARD_DETECT_IOMUX_FGPIO,
            #endif
            #ifdef RK29SDK_SD_CARD_DETECT_IOMUX_FMUX
                        .fmux       = RK29SDK_SD_CARD_DETECT_IOMUX_FMUX,
            #endif
                    },
        #endif
#else
                    .io             = INVALID_GPIO,
                    .enable         = GPIO_LOW,
#endif            
    },

};


/*
* define the gpio for sdmmc1
*/
static struct rksdmmc_gpio_board rksdmmc1_gpio_init = {

     .clk_gpio       = {
        .io             = RK30_PIN3_PC5,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3C5_SDMMC1CLKOUT_RMIICLKOUT_RMIICLKIN_NAME,
            .fgpio      = GPIO3C_GPIO3C5,
            .fmux       = GPIO3C_SDMMC1CLKOUT,
        },
    },   

    .cmd_gpio           = {
        .io             = RK30_PIN3_PC0,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3C0_SDMMC1CMD_RMIITXEN_NAME,
            .fgpio      = GPIO3C_GPIO3C0,
            .fmux       = GPIO3C_SDMMC1CMD,
        },
    },      

   .data0_gpio       = {
        .io             = RK30_PIN3_PC1,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3C1_SDMMC1DATA0_RMIITXD1_NAME,
            .fgpio      = GPIO3C_GPIO3C1,
            .fmux       = GPIO3C_SDMMC1DATA0,
        },
    },      

    .data1_gpio       = {
        .io             = RK30_PIN3_PC2,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3C2_SDMMC1DATA1_RMIITXD0_NAME,
            .fgpio      = GPIO3C_GPIO3C2,
            .fmux       = GPIO3C_SDMMC1DATA1,
        },
    },      

    .data2_gpio       = {
        .io             = RK30_PIN3_PC3,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3C3_SDMMC1DATA2_RMIIRXD0_NAME,
            .fgpio      = GPIO3C_GPIO3C3,
            .fmux       = GPIO3C_SDMMC1DATA2,
        },
    }, 

    .data3_gpio       = {
        .io             = RK30_PIN3_PC4,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO3C4_SDMMC1DATA3_RMIIRXD1_NAME,
            .fgpio      = GPIO3C_GPIO3C4,
            .fmux       = GPIO3C_SDMMC1DATA3,
        },
    }, 
};
// ---end -#if defined(CONFIG_ARCH_RK3066B)

#elif defined(CONFIG_ARCH_RK2928)
/*
* define the gpio for sdmmc0
*/
static struct rksdmmc_gpio_board rksdmmc0_gpio_init = {

     .clk_gpio       = {
        .io             = RK2928_PIN1_PC0,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO1C0_MMC0_CLKOUT_NAME,
            .fgpio      = GPIO1C_GPIO1C0,
            .fmux       = GPIO1C_MMC0_CLKOUT,
        },
    },   

    .cmd_gpio           = {
        .io             = RK2928_PIN1_PC7,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO1B7_MMC0_CMD_NAME,
            .fgpio      = GPIO1B_GPIO1B7,
            .fmux       = GPIO1B_MMC0_CMD,
        },
    },      

   .data0_gpio       = {
        .io             = RK2928_PIN1_PC2,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO1C2_MMC0_D0_NAME,
            .fgpio      = GPIO1C_GPIO1C2,
            .fmux       = GPIO1C_MMC0_D0,
        },
    },      

    .data1_gpio       = {
        .io             = RK2928_PIN1_PC3,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO1C3_MMC0_D1_NAME,
            .fgpio      = GPIO1C_GPIO1C3,
            .fmux       = GPIO1C_MMC0_D1,
        },
    },      

    .data2_gpio       = {
        .io             = RK2928_PIN1_PC4,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO1C4_MMC0_D2_NAME,
            .fgpio      = GPIO1C_GPIO1C4,
            .fmux       = GPIO1C_MMC0_D2,
        },
    }, 

    .data3_gpio       = {
        .io             = RK2928_PIN1_PC5,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO1C5_MMC0_D3_NAME,
            .fgpio      = GPIO1C_GPIO1C5,
            .fmux       = GPIO1C_MMC0_D3,
        },
    }, 

   
    .power_en_gpio      = {   
#if defined(RK29SDK_SD_CARD_PWR_EN) || (INVALID_GPIO != RK29SDK_SD_CARD_PWR_EN)
        .io             = RK29SDK_SD_CARD_PWR_EN,
        .enable         = RK29SDK_SD_CARD_PWR_EN_LEVEL,
        #ifdef RK29SDK_SD_CARD_PWR_EN_PIN_NAME
        .iomux          = {
            .name       = RK29SDK_SD_CARD_PWR_EN_PIN_NAME,
            #ifdef RK29SDK_SD_CARD_PWR_EN_IOMUX_FGPIO
            .fgpio      = RK29SDK_SD_CARD_PWR_EN_IOMUX_FGPIO,
            #endif
            #ifdef RK29SDK_SD_CARD_PWR_EN_IOMUX_FMUX
            .fmux       = RK29SDK_SD_CARD_PWR_EN_IOMUX_FMUX,
            #endif
        },
        #endif
#else
        .io             = INVALID_GPIO,
        .enable         = GPIO_LOW,
#endif
    }, 

    .detect_irq       = {
#if defined(RK29SDK_SD_CARD_DETECT_N) || (INVALID_GPIO != RK29SDK_SD_CARD_DETECT_N)  
        .io             = RK29SDK_SD_CARD_DETECT_N,
        .enable         = RK29SDK_SD_CARD_INSERT_LEVEL,
        #ifdef RK29SDK_SD_CARD_DETECT_PIN_NAME
        .iomux          = {
            .name       = RK29SDK_SD_CARD_DETECT_PIN_NAME,
            #ifdef RK29SDK_SD_CARD_DETECT_IOMUX_FGPIO
            .fgpio      = RK29SDK_SD_CARD_DETECT_IOMUX_FGPIO,
            #endif
            #ifdef RK29SDK_SD_CARD_DETECT_IOMUX_FMUX
            .fmux       = RK29SDK_SD_CARD_DETECT_IOMUX_FMUX,
            #endif
        },
        #endif
#else
        .io             = INVALID_GPIO,
        .enable         = GPIO_LOW,
#endif            
    }, 
};


/*
* define the gpio for sdmmc1
*/
static struct rksdmmc_gpio_board rksdmmc1_gpio_init = {

     .clk_gpio       = {
        .io             = RK2928_PIN0_PB1,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO0B1_MMC1_CLKOUT_NAME,
            .fgpio      = GPIO0B_GPIO0B1,
            .fmux       = GPIO0B_MMC1_CLKOUT,
        },
    },   

    .cmd_gpio           = {
        .io             = RK2928_PIN0_PB0,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO0B0_MMC1_CMD_NAME,
            .fgpio      = GPIO0B_GPIO0B0,
            .fmux       = GPIO0B_MMC1_CMD,
        },
    },      

   .data0_gpio       = {
        .io             = RK2928_PIN0_PB3,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO0B3_MMC1_D0_NAME,
            .fgpio      = GPIO0B_GPIO0B3,
            .fmux       = GPIO0B_MMC1_D0,
        },
    },      

    .data1_gpio       = {
        .io             = RK2928_PIN0_PB4,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO0B4_MMC1_D1_NAME,
            .fgpio      = GPIO0B_GPIO0B4,
            .fmux       = GPIO0B_MMC1_D1,
        },
    },      

    .data2_gpio       = {
        .io             = RK2928_PIN0_PB5,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO0B5_MMC1_D2_NAME,
            .fgpio      = GPIO0B_GPIO0B5,
            .fmux       = GPIO0B_MMC1_D2,
        },
    }, 

    .data3_gpio       = {
        .io             = RK2928_PIN0_PB6,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = GPIO0B6_MMC1_D3_NAME,
            .fgpio      = GPIO0B_GPIO0B6,
            .fmux       = GPIO0B_MMC1_D3,
        },
    }, 


};
// ---end -#if defined(CONFIG_ARCH_RK2928)
#endif



//1.Part 3: The various operations of the SDMMC-SDIO module
/*************************************************************************
* define the varaious operations for SDMMC module
* Generally only the author of SDMMC module will modify this section.
*************************************************************************/

#if !defined(CONFIG_SDMMC_RK29_OLD)	
static void rk29_sdmmc_gpio_open(int device_id, int on)
{
    switch(device_id)
    {
        case 0://mmc0
        {
            #ifdef CONFIG_SDMMC0_RK29
            if(on)
            {
                gpio_direction_output(rksdmmc0_gpio_init.clk_gpio.io, GPIO_HIGH);//set mmc0-clk to high
                gpio_direction_output(rksdmmc0_gpio_init.cmd_gpio.io, GPIO_HIGH);// set mmc0-cmd to high.
                gpio_direction_output(rksdmmc0_gpio_init.data0_gpio.io,GPIO_HIGH);//set mmc0-data0 to high.
                gpio_direction_output(rksdmmc0_gpio_init.data1_gpio.io,GPIO_HIGH);//set mmc0-data1 to high.
                gpio_direction_output(rksdmmc0_gpio_init.data2_gpio.io,GPIO_HIGH);//set mmc0-data2 to high.
                gpio_direction_output(rksdmmc0_gpio_init.data3_gpio.io,GPIO_HIGH);//set mmc0-data3 to high.

                mdelay(30);
            }
            else
            {
                rk30_mux_api_set(rksdmmc0_gpio_init.clk_gpio.iomux.name, rksdmmc0_gpio_init.clk_gpio.iomux.fgpio);
                gpio_request(rksdmmc0_gpio_init.clk_gpio.io, "mmc0-clk");
                gpio_direction_output(rksdmmc0_gpio_init.clk_gpio.io,GPIO_LOW);//set mmc0-clk to low.

                rk30_mux_api_set(rksdmmc0_gpio_init.cmd_gpio.iomux.name, rksdmmc0_gpio_init.cmd_gpio.iomux.fgpio);
                gpio_request(rksdmmc0_gpio_init.cmd_gpio.io, "mmc0-cmd");
                gpio_direction_output(rksdmmc0_gpio_init.cmd_gpio.io,GPIO_LOW);//set mmc0-cmd to low.

                rk30_mux_api_set(rksdmmc0_gpio_init.data0_gpio.iomux.name, rksdmmc0_gpio_init.data0_gpio.iomux.fgpio);
                gpio_request(rksdmmc0_gpio_init.data0_gpio.io, "mmc0-data0");
                gpio_direction_output(rksdmmc0_gpio_init.data0_gpio.io,GPIO_LOW);//set mmc0-data0 to low.

                rk30_mux_api_set(rksdmmc0_gpio_init.data1_gpio.iomux.name, rksdmmc0_gpio_init.data1_gpio.iomux.fgpio);
                gpio_request(rksdmmc0_gpio_init.data1_gpio.io, "mmc0-data1");
                gpio_direction_output(rksdmmc0_gpio_init.data1_gpio.io,GPIO_LOW);//set mmc0-data1 to low.

                rk30_mux_api_set(rksdmmc0_gpio_init.data2_gpio.iomux.name, rksdmmc0_gpio_init.data2_gpio.iomux.fgpio);
                gpio_request(rksdmmc0_gpio_init.data2_gpio.io, "mmc0-data2");
                gpio_direction_output(rksdmmc0_gpio_init.data2_gpio.io,GPIO_LOW);//set mmc0-data2 to low.

                rk30_mux_api_set(rksdmmc0_gpio_init.data3_gpio.iomux.name, rksdmmc0_gpio_init.data3_gpio.iomux.fgpio);
                gpio_request(rksdmmc0_gpio_init.data3_gpio.io, "mmc0-data3");
                gpio_direction_output(rksdmmc0_gpio_init.data3_gpio.io,GPIO_LOW);//set mmc0-data3 to low.

                mdelay(30);
            }
            #endif
        }
        break;
        
        case 1://mmc1
        {
            #ifdef CONFIG_SDMMC1_RK29
            if(on)
            {
                gpio_direction_output(rksdmmc1_gpio_init.clk_gpio.io,GPIO_HIGH);//set mmc1-clk to high
                gpio_direction_output(rksdmmc1_gpio_init.cmd_gpio.io,GPIO_HIGH);//set mmc1-cmd to high.
                gpio_direction_output(rksdmmc1_gpio_init.data0_gpio.io,GPIO_HIGH);//set mmc1-data0 to high.
                gpio_direction_output(rksdmmc1_gpio_init.data1_gpio.io,GPIO_HIGH);//set mmc1-data1 to high.
                gpio_direction_output(rksdmmc1_gpio_init.data2_gpio.io,GPIO_HIGH);//set mmc1-data2 to high.
                gpio_direction_output(rksdmmc1_gpio_init.data3_gpio.io,GPIO_HIGH);//set mmc1-data3 to high.
                mdelay(100);
            }
            else
            {
                rk30_mux_api_set(rksdmmc1_gpio_init.clk_gpio.iomux.name, rksdmmc1_gpio_init.clk_gpio.iomux.fgpio);
                gpio_request(rksdmmc1_gpio_init.clk_gpio.io, "mmc1-clk");
                gpio_direction_output(rksdmmc1_gpio_init.clk_gpio.io,GPIO_LOW);//set mmc1-clk to low.

                rk30_mux_api_set(rksdmmc1_gpio_init.cmd_gpio.iomux.name, rksdmmc1_gpio_init.cmd_gpio.iomux.fgpio);
                gpio_request(rksdmmc1_gpio_init.cmd_gpio.io, "mmc1-cmd");
                gpio_direction_output(rksdmmc1_gpio_init.cmd_gpio.io,GPIO_LOW);//set mmc1-cmd to low.

                rk30_mux_api_set(rksdmmc1_gpio_init.data0_gpio.iomux.name, rksdmmc1_gpio_init.data0_gpio.iomux.fgpio);
                gpio_request(rksdmmc1_gpio_init.data0_gpio.io, "mmc1-data0");
                gpio_direction_output(rksdmmc1_gpio_init.data0_gpio.io,GPIO_LOW);//set mmc1-data0 to low.
                
            #if defined(CONFIG_WIFI_COMBO_MODULE_CONTROL_FUNC)
                rk29_mux_api_set(rksdmmc1_gpio_init.data1_gpio.iomux.name, rksdmmc1_gpio_init.data1_gpio.iomux.fgpio);
                gpio_request(rksdmmc1_gpio_init.data1_gpio.io, "mmc1-data1");
                gpio_direction_output(rksdmmc1_gpio_init.data1_gpio.io,GPIO_LOW);//set mmc1-data1 to low.

                rk29_mux_api_set(rksdmmc1_gpio_init.data2_gpio.iomux.name, rksdmmc1_gpio_init.data2_gpio.iomux.fgpio);
                gpio_request(rksdmmc1_gpio_init.data2_gpio.io, "mmc1-data2");
                gpio_direction_output(rksdmmc1_gpio_init.data2_gpio.io,GPIO_LOW);//set mmc1-data2 to low.

                rk29_mux_api_set(rksdmmc1_gpio_init.data3_gpio.iomux.name, rksdmmc1_gpio_init.data3_gpio.iomux.fgpio);
                gpio_request(rksdmmc1_gpio_init.data3_gpio.io, "mmc1-data3");
                gpio_direction_output(rksdmmc1_gpio_init.data3_gpio.io,GPIO_LOW);//set mmc1-data3 to low.
           #endif
                mdelay(100);
            }
            #endif
        }
        break; 
        
        case 2: //mmc2
        break;
        
        default:
        break;
    }
}

static void rk29_sdmmc_set_iomux_mmc0(unsigned int bus_width)
{
    switch (bus_width)
    {
        
    	case 1://SDMMC_CTYPE_4BIT:
    	{
        	rk30_mux_api_set(rksdmmc0_gpio_init.data1_gpio.iomux.name, rksdmmc0_gpio_init.data1_gpio.iomux.fmux);
        	rk30_mux_api_set(rksdmmc0_gpio_init.data2_gpio.iomux.name, rksdmmc0_gpio_init.data2_gpio.iomux.fmux);
        	rk30_mux_api_set(rksdmmc0_gpio_init.data3_gpio.iomux.name, rksdmmc0_gpio_init.data3_gpio.iomux.fmux);
    	}
    	break;

    	case 0x10000://SDMMC_CTYPE_8BIT:
    	    break;
    	case 0xFFFF: //gpio_reset
    	{
            rk30_mux_api_set(rksdmmc0_gpio_init.power_en_gpio.iomux.name, rksdmmc0_gpio_init.power_en_gpio.iomux.fgpio);
            gpio_request(rksdmmc0_gpio_init.power_en_gpio.io,"sdmmc-power");
            gpio_direction_output(rksdmmc0_gpio_init.power_en_gpio.io, !(rksdmmc0_gpio_init.power_en_gpio.enable)); //power-off

        #if 0 //replace the power control into rk29_sdmmc_set_ios(); modifyed by xbw at 2012-08-12
            rk29_sdmmc_gpio_open(0, 0);

            gpio_direction_output(rksdmmc0_gpio_init.power_en_gpio.io, rksdmmc0_gpio_init.power_en_gpio.enable); //power-on

            rk29_sdmmc_gpio_open(0, 1);
          #endif  
    	}
    	break;

    	default: //case 0://SDMMC_CTYPE_1BIT:
        {
        	rk30_mux_api_set(rksdmmc0_gpio_init.cmd_gpio.iomux.name, rksdmmc0_gpio_init.cmd_gpio.iomux.fmux);
        	rk30_mux_api_set(rksdmmc0_gpio_init.clk_gpio.iomux.name, rksdmmc0_gpio_init.clk_gpio.iomux.fmux);
        	rk30_mux_api_set(rksdmmc0_gpio_init.data0_gpio.iomux.name, rksdmmc0_gpio_init.data0_gpio.iomux.fmux);

            rk30_mux_api_set(rksdmmc0_gpio_init.data1_gpio.iomux.name, rksdmmc0_gpio_init.data1_gpio.iomux.fgpio);
            gpio_request(rksdmmc0_gpio_init.data1_gpio.io, "mmc0-data1");
            gpio_direction_output(rksdmmc0_gpio_init.data1_gpio.io,GPIO_HIGH);//set mmc0-data1 to high.

            rk30_mux_api_set(rksdmmc0_gpio_init.data2_gpio.iomux.name, rksdmmc0_gpio_init.data2_gpio.iomux.fgpio);
            gpio_request(rksdmmc0_gpio_init.data2_gpio.io, "mmc0-data2");
            gpio_direction_output(rksdmmc0_gpio_init.data2_gpio.io,GPIO_HIGH);//set mmc0-data2 to high.

            rk30_mux_api_set(rksdmmc0_gpio_init.data3_gpio.iomux.name, rksdmmc0_gpio_init.data3_gpio.iomux.fgpio);
            gpio_request(rksdmmc0_gpio_init.data3_gpio.io, "mmc0-data3");
            gpio_direction_output(rksdmmc0_gpio_init.data3_gpio.io,GPIO_HIGH);//set mmc0-data3 to high.
    	}
    	break;
	}
}

static void rk29_sdmmc_set_iomux_mmc1(unsigned int bus_width)
{
    rk30_mux_api_set(rksdmmc1_gpio_init.cmd_gpio.iomux.name, rksdmmc1_gpio_init.cmd_gpio.iomux.fmux);
    rk30_mux_api_set(rksdmmc1_gpio_init.clk_gpio.iomux.name, rksdmmc1_gpio_init.clk_gpio.iomux.fmux);
    rk30_mux_api_set(rksdmmc1_gpio_init.data0_gpio.iomux.name, rksdmmc1_gpio_init.data0_gpio.iomux.fmux);
    rk30_mux_api_set(rksdmmc1_gpio_init.data1_gpio.iomux.name, rksdmmc1_gpio_init.data1_gpio.iomux.fmux);
    rk30_mux_api_set(rksdmmc1_gpio_init.data2_gpio.iomux.name, rksdmmc1_gpio_init.data2_gpio.iomux.fmux);
    rk30_mux_api_set(rksdmmc1_gpio_init.data3_gpio.iomux.name, rksdmmc1_gpio_init.data3_gpio.iomux.fmux);
}

static void rk29_sdmmc_set_iomux_mmc2(unsigned int bus_width)
{
    ;//
}

static void rk29_sdmmc_set_iomux(int device_id, unsigned int bus_width)
{
    switch(device_id)
    {
        case 0:
            #ifdef CONFIG_SDMMC0_RK29
            rk29_sdmmc_set_iomux_mmc0(bus_width);
            #endif
            break;
        case 1:
            #ifdef CONFIG_SDMMC1_RK29
            rk29_sdmmc_set_iomux_mmc1(bus_width);
            #endif
            break;
        case 2:
            rk29_sdmmc_set_iomux_mmc2(bus_width);
            break;
        default:
            break;
    }    
}

#endif



//1.Part 4: The various operations of the Wifi-BT module
/*************************************************************************
* define the varaious operations for Wifi module
* Generally only the author of Wifi module will modify this section.
*************************************************************************/

static int rk29sdk_wifi_status(struct device *dev);
static int rk29sdk_wifi_status_register(void (*callback)(int card_presend, void *dev_id), void *dev_id);

#if defined(CONFIG_WIFI_COMBO_MODULE_CONTROL_FUNC)
static int rk29sdk_wifi_mmc0_status(struct device *dev);
static int rk29sdk_wifi_mmc0_status_register(void (*callback)(int card_presend, void *dev_id), void *dev_id);
static int rk29sdk_wifi_mmc0_cd = 0;   /* wifi virtual 'card detect' status */
static void (*wifi_mmc0_status_cb)(int card_present, void *dev_id);
static void *wifi_mmc0_status_cb_devid;

int rk29sdk_wifi_power_state = 0;
int rk29sdk_bt_power_state = 0;

    #if defined(CONFIG_USE_SDMMC0_FOR_WIFI_DEVELOP_BOARD)
    	/////////////////////////////////////////////////////////////////////////////////////
	    // set the gpio to develop wifi EVB if you select the macro of CONFIG_USE_SDMMC0_FOR_WIFI_DEVELOP_BOARD
	    #define USE_SDMMC_CONTROLLER_FOR_WIFI   0
   		#define COMBO_MODULE_MT6620_CDT         0  //- 1--use Cdtech chip; 0--unuse CDT chip
        //power
        #define RK30SDK_WIFI_GPIO_POWER_N                   RK30_PIN3_PD0            
        #define RK30SDK_WIFI_GPIO_POWER_ENABLE_VALUE        GPIO_HIGH        
        #define RK30SDK_WIFI_GPIO_POWER_PIN_NAME            GPIO3D0_SDMMC1PWREN_NAME
        #define RK30SDK_WIFI_GPIO_POWER_IOMUX_FGPIO         GPIO3D_GPIO3D0
        #define RK30SDK_WIFI_GPIO_POWER_IOMUX_FMUX          GPIO3D_SDMMC1_PWR_EN
        //reset
        #define RK30SDK_WIFI_GPIO_RESET_N                   RK30_PIN3_PD1
        #define RK30SDK_WIFI_GPIO_RESET_ENABLE_VALUE        GPIO_HIGH 
        #define RK30SDK_WIFI_GPIO_RESET_PIN_NAME            GPIO3D1_SDMMC1BACKENDPWR_NAME
        #define RK30SDK_WIFI_GPIO_RESET_IOMUX_FGPIO         GPIO3D_GPIO3D1
        #define RK30SDK_WIFI_GPIO_RESET_IOMUX_FMUX          GPIO3D_SDMMC1_BACKEND_PWR
        //VDDIO
        //#define RK30SDK_WIFI_GPIO_VCCIO_WL                 RK30_PIN2_PC5
        //#define RK30SDK_WIFI_GPIO_VCCIO_WL_ENABLE_VALUE    GPIO_HIGH       
        //WIFI_INT_B
        #define RK30SDK_WIFI_GPIO_WIFI_INT_B                RK30_PIN3_PD2
        #define RK30SDK_WIFI_GPIO_WIFI_INT_B_ENABLE_VALUE   GPIO_HIGH 
        #define RK30SDK_WIFI_GPIO_WIFI_INT_B_PIN_NAME       GPIO3D2_SDMMC1INTN_NAME
        #define RK30SDK_WIFI_GPIO_WIFI_INT_B_IOMUX_FGPIO    GPIO3D_GPIO3D2
        #define RK30SDK_WIFI_GPIO_WIFI_INT_B_IOMUX_FMUX     GPIO3D_SDMMC1_INT_N
        //BGF_INT_B
        #define RK30SDK_WIFI_GPIO_BGF_INT_B                 RK30_PIN3_PC6
        #define RK30SDK_WIFI_GPIO_BGF_INT_B_ENABLE_VALUE    GPIO_HIGH 
        #define RK30SDK_WIFI_GPIO_BGF_INT_B_PIN_NAME        GPIO3C6_SDMMC1DETECTN_NAME
        #define RK30SDK_WIFI_GPIO_BGF_INT_B_IOMUX_FGPIO     GPIO3C_GPIO3C6
        #define RK30SDK_WIFI_GPIO_BGF_INT_B_IOMUX_FMUX      GPIO3C_SDMMC1_DETECT_N
        //GPS_SYNC
        #define RK30SDK_WIFI_GPIO_GPS_SYNC                  RK30_PIN3_PC7
        #define RK30SDK_WIFI_GPIO_GPS_SYNC_ENABLE_VALUE     GPIO_HIGH 
        #define RK30SDK_WIFI_GPIO_GPS_SYNC_PIN_NAME         GPIO3C7_SDMMC1WRITEPRT_NAME
        #define RK30SDK_WIFI_GPIO_GPS_SYNC_IOMUX_FGPIO      GPIO3C_GPIO3C7
        #define RK30SDK_WIFI_GPIO_GPS_SYNC_IOMUX_FMUX       GPIO3C_SDMMC1_WRITE_PRT

        #if COMBO_MODULE_MT6620_CDT
        //ANTSEL2
        #define RK30SDK_WIFI_GPIO_ANTSEL2                   RK30_PIN4_PD4
        #define RK30SDK_WIFI_GPIO_ANTSEL2_ENABLE_VALUE      GPIO_LOW    //use 6620 in CDT chip, LOW--work; High--no work.
        #define RK30SDK_WIFI_GPIO_ANTSEL2_PIN_NAME          GPIO4D4_SMCDATA12_TRACEDATA12_NAME
        #define RK30SDK_WIFI_GPIO_ANTSEL2_IOMUX_FGPIO       GPIO4D_GPIO4D4
        #define RK30SDK_WIFI_GPIO_ANTSEL2_IOMUX_FMUX        GPIO4D_TRACE_DATA12
        //ANTSEL3
        #define RK30SDK_WIFI_GPIO_ANTSEL3                   RK30_PIN4_PD3
        #define RK30SDK_WIFI_GPIO_ANTSEL3_ENABLE_VALUE      GPIO_HIGH    //use 6620 in CDT chip, High--work; Low--no work..
        #define RK30SDK_WIFI_GPIO_ANTSEL3_PIN_NAME          GPIO4D3_SMCDATA11_TRACEDATA11_NAME
        #define RK30SDK_WIFI_GPIO_ANTSEL3_IOMUX_FGPIO       GPIO4D_GPIO4D3
        #define RK30SDK_WIFI_GPIO_ANTSEL3_IOMUX_FMUX        GPIO4D_TRACE_DATA11
        //GPS_LAN
        #define RK30SDK_WIFI_GPIO_GPS_LAN                   RK30_PIN4_PD6
        #define RK30SDK_WIFI_GPIO_GPS_LAN_ENABLE_VALUE      GPIO_HIGH    //use 6620 in CDT chip, High--work; Low--no work..
        #define RK30SDK_WIFI_GPIO_GPS_LAN_PIN_NAME          GPIO4D6_SMCDATA14_TRACEDATA14_NAME
        #define RK30SDK_WIFI_GPIO_GPS_LAN_IOMUX_FGPIO       GPIO4D_GPIO4D6
        #define RK30SDK_WIFI_GPIO_GPS_LAN_IOMUX_FMUX        GPIO4D_TRACE_DATA14
        #endif // #if COMBO_MODULE_MT6620_CDT--#endif
        
    #endif // #if defined(CONFIG_USE_SDMMC0_FOR_WIFI_DEVELOP_BOARD)---#endif
#endif // #if defined(CONFIG_WIFI_COMBO_MODULE_CONTROL_FUNC) ---#endif

static int rk29sdk_wifi_cd = 0;   /* wifi virtual 'card detect' status */
static void (*wifi_status_cb)(int card_present, void *dev_id);
static void *wifi_status_cb_devid;

#if defined(CONFIG_RK29_SDIO_IRQ_FROM_GPIO)
#define RK29SDK_WIFI_SDIO_CARD_INT         RK30SDK_WIFI_GPIO_WIFI_INT_B
#endif

struct rksdmmc_gpio_wifi_moudle  rk_platform_wifi_gpio = {
    .power_n = {
            .io             = RK30SDK_WIFI_GPIO_POWER_N, 
            .enable         = RK30SDK_WIFI_GPIO_POWER_ENABLE_VALUE,
            #ifdef RK30SDK_WIFI_GPIO_POWER_PIN_NAME
            .iomux          = {
                .name       = RK30SDK_WIFI_GPIO_POWER_PIN_NAME,
                .fgpio      = RK30SDK_WIFI_GPIO_POWER_IOMUX_FGPIO,
                #ifdef RK30SDK_WIFI_GPIO_POWER_IOMUX_FMUX
                .fmux       = RK30SDK_WIFI_GPIO_POWER_IOMUX_FMUX,
                #endif
            },
            #endif
     },
     
    #ifdef RK30SDK_WIFI_GPIO_RESET_N
    .reset_n = {
            .io             = RK30SDK_WIFI_GPIO_RESET_N,
            .enable         = RK30SDK_WIFI_GPIO_RESET_ENABLE_VALUE,
            #ifdef RK30SDK_WIFI_GPIO_RESET_PIN_NAME
            .iomux          = {
                .name       = RK30SDK_WIFI_GPIO_RESET_PIN_NAME,
                .fgpio      = RK30SDK_WIFI_GPIO_RESET_IOMUX_FGPIO,
                #ifdef RK30SDK_WIFI_GPIO_RESET_IOMUX_FMUX
                .fmux       = RK30SDK_WIFI_GPIO_RESET_IOMUX_FMUX,
                #endif
            },
            #endif
    },
    #endif
    
    #ifdef RK30SDK_WIFI_GPIO_WIFI_INT_B
    .wifi_int_b = {
            .io             = RK30SDK_WIFI_GPIO_WIFI_INT_B,
            .enable         = RK30SDK_WIFI_GPIO_WIFI_INT_B_ENABLE_VALUE,
            #ifdef RK30SDK_WIFI_GPIO_WIFI_INT_B_PIN_NAME
            .iomux          = {
                .name       = RK30SDK_WIFI_GPIO_WIFI_INT_B_PIN_NAME,
                .fgpio      = RK30SDK_WIFI_GPIO_WIFI_INT_B_IOMUX_FGPIO,
                #ifdef RK30SDK_WIFI_GPIO_WIFI_INT_B_IOMUX_FMUX
                .fmux       = RK30SDK_WIFI_GPIO_WIFI_INT_B_IOMUX_FMUX,
                #endif
            },
            #endif
     },
     #endif

    #ifdef RK30SDK_WIFI_GPIO_VCCIO_WL 
    .vddio = {
            .io             = RK30SDK_WIFI_GPIO_VCCIO_WL,
            .enable         = RK30SDK_WIFI_GPIO_VCCIO_WL_ENABLE_VALUE,
            #ifdef RK30SDK_WIFI_GPIO_VCCIO_WL_PIN_NAME
            .iomux          = {
                .name       = RK30SDK_WIFI_GPIO_VCCIO_WL_PIN_NAME,
                .fgpio      = RK30SDK_WIFI_GPIO_VCCIO_WL_IOMUX_FGPIO,
                #ifdef RK30SDK_WIFI_GPIO_VCCIO_WL_IOMUX_FMUX
                .fmux       = RK30SDK_WIFI_GPIO_VCCIO_WL_IOMUX_FMUX,
                #endif
            },
            #endif
     },
     #endif
     
     #ifdef RK30SDK_WIFI_GPIO_BGF_INT_B
    .bgf_int_b = {
            .io             = RK30SDK_WIFI_GPIO_BGF_INT_B,
            .enable         = RK30SDK_WIFI_GPIO_BGF_INT_B_ENABLE_VALUE,
            #ifdef RK30SDK_WIFI_GPIO_BGF_INT_B_PIN_NAME
            .iomux          = {
                .name       = RK30SDK_WIFI_GPIO_BGF_INT_B_PIN_NAME,
                .fgpio      = RK30SDK_WIFI_GPIO_BGF_INT_B_IOMUX_FGPIO,
                #ifdef RK30SDK_WIFI_GPIO_BGF_INT_B_IOMUX_FMUX
                .fmux       = RK30SDK_WIFI_GPIO_BGF_INT_B_IOMUX_FMUX,
                #endif
            },
            #endif
        },       
    #endif
    
    #ifdef RK30SDK_WIFI_GPIO_GPS_SYNC
    .gps_sync = {
            .io             = RK30SDK_WIFI_GPIO_GPS_SYNC,
            .enable         = RK30SDK_WIFI_GPIO_GPS_SYNC_ENABLE_VALUE,
            #ifdef RK30SDK_WIFI_GPIO_GPS_SYNC_PIN_NAME
            .iomux          = {
                .name       = RK30SDK_WIFI_GPIO_GPS_SYNC_PIN_NAME,
                .fgpio      = RK30SDK_WIFI_GPIO_GPS_SYNC_IOMUX_FGPIO,
                #ifdef RK30SDK_WIFI_GPIO_GPS_SYNC_IOMUX_FMUX
                .fmux       = RK30SDK_WIFI_GPIO_GPS_SYNC_IOMUX_FMUX,
                #endif
            },
            #endif
    },
    #endif
    
#if COMBO_MODULE_MT6620_CDT
    #ifdef RK30SDK_WIFI_GPIO_ANTSEL2
    .ANTSEL2 = {
            .io             = RK30SDK_WIFI_GPIO_ANTSEL2,
            .enable         = RK30SDK_WIFI_GPIO_ANTSEL2_ENABLE_VALUE,
            #ifdef RK30SDK_WIFI_GPIO_ANTSEL2_PIN_NAME
            .iomux          = {
                .name       = RK30SDK_WIFI_GPIO_ANTSEL2_PIN_NAME,
                .fgpio      = RK30SDK_WIFI_GPIO_ANTSEL2_IOMUX_FGPIO,
                #ifdef RK30SDK_WIFI_GPIO_ANTSEL2_IOMUX_FMUX
                .fmux       = RK30SDK_WIFI_GPIO_ANTSEL2_IOMUX_FMUX,
                #endif
            },
            #endif
    },
    #endif

    #ifdef RK30SDK_WIFI_GPIO_ANTSEL3
    .ANTSEL3 = {
            .io             = RK30SDK_WIFI_GPIO_ANTSEL3,
            .enable         = RK30SDK_WIFI_GPIO_ANTSEL3_ENABLE_VALUE,
            #ifdef RK30SDK_WIFI_GPIO_ANTSEL3_PIN_NAME
            .iomux          = {
                .name       = RK30SDK_WIFI_GPIO_ANTSEL3_PIN_NAME,
                .fgpio      = RK30SDK_WIFI_GPIO_ANTSEL3_IOMUX_FGPIO,
                #ifdef RK30SDK_WIFI_GPIO_ANTSEL3_IOMUX_FMUX
                .fmux       = RK30SDK_WIFI_GPIO_ANTSEL3_IOMUX_FMUX,
                #endif
            },
            #endif
    },
    #endif

    #ifdef RK30SDK_WIFI_GPIO_GPS_LAN
    .GPS_LAN = {
            .io             = RK30SDK_WIFI_GPIO_GPS_LAN,
            .enable         = RK30SDK_WIFI_GPIO_GPS_LAN_ENABLE_VALUE,
            #ifdef RK30SDK_WIFI_GPIO_GPS_LAN_PIN_NAME
            .iomux          = {
                .name       = RK30SDK_WIFI_GPIO_GPS_LAN_PIN_NAME,
                .fgpio      = RK30SDK_WIFI_GPIO_GPS_LAN_IOMUX_FGPIO,
                #ifdef RK30SDK_WIFI_GPIO_GPS_LAN_IOMUX_FMUX
                .fmux       = RK30SDK_WIFI_GPIO_GPS_LAN_IOMUX_FMUX,
                #endif
            },
            #endif
    },
    #endif
#endif // #if COMBO_MODULE_MT6620_CDT--#endif   
};



#ifdef CONFIG_WIFI_CONTROL_FUNC
#define PREALLOC_WLAN_SEC_NUM           4
#define PREALLOC_WLAN_BUF_NUM           160
#define PREALLOC_WLAN_SECTION_HEADER    24

#define WLAN_SECTION_SIZE_0     (PREALLOC_WLAN_BUF_NUM * 128)
#define WLAN_SECTION_SIZE_1     (PREALLOC_WLAN_BUF_NUM * 128)
#define WLAN_SECTION_SIZE_2     (PREALLOC_WLAN_BUF_NUM * 512)
#define WLAN_SECTION_SIZE_3     (PREALLOC_WLAN_BUF_NUM * 1024)

#define WLAN_SKB_BUF_NUM        16

static struct sk_buff *wlan_static_skb[WLAN_SKB_BUF_NUM];

struct wifi_mem_prealloc {
        void *mem_ptr;
        unsigned long size;
};

static struct wifi_mem_prealloc wifi_mem_array[PREALLOC_WLAN_SEC_NUM] = {
        {NULL, (WLAN_SECTION_SIZE_0 + PREALLOC_WLAN_SECTION_HEADER)},
        {NULL, (WLAN_SECTION_SIZE_1 + PREALLOC_WLAN_SECTION_HEADER)},
        {NULL, (WLAN_SECTION_SIZE_2 + PREALLOC_WLAN_SECTION_HEADER)},
        {NULL, (WLAN_SECTION_SIZE_3 + PREALLOC_WLAN_SECTION_HEADER)}
};

static void *rk29sdk_mem_prealloc(int section, unsigned long size)
{
        if (section == PREALLOC_WLAN_SEC_NUM)
                return wlan_static_skb;

        if ((section < 0) || (section > PREALLOC_WLAN_SEC_NUM))
                return NULL;

        if (wifi_mem_array[section].size < size)
                return NULL;

        return wifi_mem_array[section].mem_ptr;
}

static int __init rk29sdk_init_wifi_mem(void)
{
        int i;
        int j;

        for (i = 0 ; i < WLAN_SKB_BUF_NUM ; i++) {
                wlan_static_skb[i] = dev_alloc_skb(
                                ((i < (WLAN_SKB_BUF_NUM / 2)) ? 4096 : 8192));

                if (!wlan_static_skb[i])
                        goto err_skb_alloc;
        }

        for (i = 0 ; i < PREALLOC_WLAN_SEC_NUM ; i++) {
                wifi_mem_array[i].mem_ptr =
                                kmalloc(wifi_mem_array[i].size, GFP_KERNEL);

                if (!wifi_mem_array[i].mem_ptr)
                        goto err_mem_alloc;
        }
        return 0;

err_mem_alloc:
        pr_err("Failed to mem_alloc for WLAN\n");
        for (j = 0 ; j < i ; j++)
               kfree(wifi_mem_array[j].mem_ptr);

        i = WLAN_SKB_BUF_NUM;

err_skb_alloc:
        pr_err("Failed to skb_alloc for WLAN\n");
        for (j = 0 ; j < i ; j++)
                dev_kfree_skb(wlan_static_skb[j]);

        return -ENOMEM;
}

static int rk29sdk_wifi_status(struct device *dev)
{
        return rk29sdk_wifi_cd;
}

static int rk29sdk_wifi_status_register(void (*callback)(int card_present, void *dev_id), void *dev_id)
{
        if(wifi_status_cb)
                return -EAGAIN;
        wifi_status_cb = callback;
        wifi_status_cb_devid = dev_id;
        return 0;
}

static int __init rk29sdk_wifi_bt_gpio_control_init(void)
{
    rk29sdk_init_wifi_mem();    
    rk29_mux_api_set(rk_platform_wifi_gpio.power_n.iomux.name, rk_platform_wifi_gpio.power_n.iomux.fgpio);
    
    if (gpio_request(rk_platform_wifi_gpio.power_n.io, "wifi_power")) {
           pr_info("%s: request wifi power gpio failed\n", __func__);
           return -1;
    }

#ifdef RK30SDK_WIFI_GPIO_RESET_N
    if (gpio_request(rk_platform_wifi_gpio.reset_n.io, "wifi reset")) {
           pr_info("%s: request wifi reset gpio failed\n", __func__);
           gpio_free(rk_platform_wifi_gpio.reset_n.io);
           return -1;
    }
#endif    

    gpio_direction_output(rk_platform_wifi_gpio.power_n.io, !(rk_platform_wifi_gpio.power_n.enable) );

#ifdef RK30SDK_WIFI_GPIO_RESET_N 
    gpio_direction_output(rk_platform_wifi_gpio.reset_n.io, !(rk_platform_wifi_gpio.reset_n.enable) );
#endif    

    #if defined(CONFIG_SDMMC1_RK29) && !defined(CONFIG_SDMMC_RK29_OLD)
    
    rk29_mux_api_set(rksdmmc1_gpio_init.data1_gpio.iomux.name, rksdmmc1_gpio_init.data1_gpio.iomux.fgpio);
    gpio_request(rksdmmc1_gpio_init.data1_gpio.io, "mmc1-data1");
    gpio_direction_output(rksdmmc1_gpio_init.data1_gpio.io,GPIO_LOW);//set mmc1-data1 to low.

    rk29_mux_api_set(rksdmmc1_gpio_init.data2_gpio.iomux.name, rksdmmc1_gpio_init.data2_gpio.iomux.fgpio);
    gpio_request(rksdmmc1_gpio_init.data2_gpio.io, "mmc1-data2");
    gpio_direction_output(rksdmmc1_gpio_init.data2_gpio.io,GPIO_LOW);//set mmc1-data2 to low.

    rk29_mux_api_set(rksdmmc1_gpio_init.data3_gpio.iomux.name,  rksdmmc1_gpio_init.data3_gpio.iomux.fgpio);
    gpio_request(rksdmmc1_gpio_init.data3_gpio.io, "mmc1-data3");
    gpio_direction_output(rksdmmc1_gpio_init.data3_gpio.io,GPIO_LOW);//set mmc1-data3 to low.
    
    rk29_sdmmc_gpio_open(1, 0); //added by xbw at 2011-10-13
    #endif    
    pr_info("%s: init finished\n",__func__);

    return 0;
}

#if (defined(CONFIG_RTL8192CU) || defined(CONFIG_RTL8188EU) )&& defined(CONFIG_ARCH_RK2928)
static int usbwifi_power_status = 1;
int rk29sdk_wifi_power(int on)
{
        pr_info("%s: %d\n", __func__, on);
         if (on){
                if(usbwifi_power_status == 1) {
                    rkusb_wifi_power(0);
                    mdelay(50);
                }
                rkusb_wifi_power(1);
                usbwifi_power_status = 1;
                 pr_info("wifi turn on power\n");  	
        }else{
                rkusb_wifi_power(0);
                usbwifi_power_status = 0;    	
                 pr_info("wifi shut off power\n");
        }
        return 0;
}
#else
int rk29sdk_wifi_power(int on)
{
        pr_info("%s: %d\n", __func__, on);
        if (on){
                gpio_set_value(rk_platform_wifi_gpio.power_n.io, rk_platform_wifi_gpio.power_n.enable);
                mdelay(50);

                #if defined(CONFIG_SDMMC1_RK29) && !defined(CONFIG_SDMMC_RK29_OLD)	
                rk29_sdmmc_gpio_open(1, 1); //added by xbw at 2011-10-13
                #endif

            #ifdef RK30SDK_WIFI_GPIO_RESET_N
                gpio_set_value(rk_platform_wifi_gpio.reset_n.io, rk_platform_wifi_gpio.reset_n.enable);
            #endif                
                mdelay(100);
                pr_info("wifi turn on power\n");
        }else{
//                if (!rk29sdk_bt_power_state){
                        gpio_set_value(rk_platform_wifi_gpio.power_n.io, !(rk_platform_wifi_gpio.power_n.enable));

                        #if defined(CONFIG_SDMMC1_RK29) && !defined(CONFIG_SDMMC_RK29_OLD)	
                        rk29_sdmmc_gpio_open(1, 0); //added by xbw at 2011-10-13
                        #endif
                        
                        mdelay(100);
                        pr_info("wifi shut off power\n");
//                }else
//                {
//                        pr_info("wifi shouldn't shut off power, bt is using it!\n");
//                }
#ifdef RK30SDK_WIFI_GPIO_RESET_N
                gpio_set_value(rk_platform_wifi_gpio.reset_n.io, !(rk_platform_wifi_gpio.reset_n.enable));
#endif 
        }

//        rk29sdk_wifi_power_state = on;
        return 0;
}
#endif
EXPORT_SYMBOL(rk29sdk_wifi_power);

static int rk29sdk_wifi_reset_state;
static int rk29sdk_wifi_reset(int on)
{
        pr_info("%s: %d\n", __func__, on);
        //mdelay(100);
        rk29sdk_wifi_reset_state = on;
        return 0;
}

int rk29sdk_wifi_set_carddetect(int val)
{
        pr_info("%s:%d\n", __func__, val);
        rk29sdk_wifi_cd = val;
        if (wifi_status_cb){
                wifi_status_cb(val, wifi_status_cb_devid);
        }else {
                pr_warning("%s, nobody to notify\n", __func__);
        }
        return 0;
}
EXPORT_SYMBOL(rk29sdk_wifi_set_carddetect);


static struct resource resources[] = {
	{
		.start = WIFI_HOST_WAKE,
		.flags = IORESOURCE_IRQ,
		.name = "bcmdhd_wlan_irq",
	},
};
 //#if defined(CONFIG_WIFI_CONTROL_FUNC)----#elif

///////////////////////////////////////////////////////////////////////////////////
#elif defined(CONFIG_WIFI_COMBO_MODULE_CONTROL_FUNC)

#define debug_combo_system 0

int rk29sdk_wifi_combo_get_BGFgpio(void)
{
    return rk_platform_wifi_gpio.bgf_int_b.io;
}
EXPORT_SYMBOL(rk29sdk_wifi_combo_get_BGFgpio);


int rk29sdk_wifi_combo_get_GPS_SYNC_gpio(void)
{
    return rk_platform_wifi_gpio.gps_sync.io;
}
EXPORT_SYMBOL(rk29sdk_wifi_combo_get_GPS_SYNC_gpio);


static int rk29sdk_wifi_combo_module_gpio_init(void)
{
    //VDDIO
    #ifdef RK30SDK_WIFI_GPIO_VCCIO_WL
        #ifdef RK30SDK_WIFI_GPIO_VCCIO_WL_PIN_NAME
        rk30_mux_api_set(rk_platform_wifi_gpio.vddio.iomux.name, rk_platform_wifi_gpio.vddio.iomux.fgpio);
        #endif
        gpio_request(rk_platform_wifi_gpio.vddio.io, "combo-VDDIO");	
	    gpio_direction_output(rk_platform_wifi_gpio.vddio.io, !(rk_platform_wifi_gpio.power_n.enable));
    #endif
    
    //BGF_INT_B
    #ifdef RK30SDK_WIFI_GPIO_BGF_INT_B_PIN_NAME
    rk30_mux_api_set(rk_platform_wifi_gpio.bgf_int_b.iomux.name, rk_platform_wifi_gpio.bgf_int_b.iomux.fgpio);
    #endif
    gpio_request(rk_platform_wifi_gpio.bgf_int_b.io, "combo-BGFINT");
    gpio_pull_updown(rk_platform_wifi_gpio.bgf_int_b.io, GPIOPullUp);
    gpio_direction_input(rk_platform_wifi_gpio.bgf_int_b.io);
    
    //WIFI_INT_B
    #ifdef RK30SDK_WIFI_GPIO_WIFI_INT_B_PIN_NAME
    rk30_mux_api_set(rk_platform_wifi_gpio.bgf_int_b.iomux.name, rk_platform_wifi_gpio.bgf_int_b.iomux.fgpio);
    #endif
    gpio_request(rk_platform_wifi_gpio.wifi_int_b.io, "combo-WIFIINT");
    gpio_pull_updown(rk_platform_wifi_gpio.wifi_int_b.io, GPIOPullUp);
    gpio_direction_input(rk_platform_wifi_gpio.wifi_int_b.io); 
    
    //reset
    #ifdef RK30SDK_WIFI_GPIO_RESET_PIN_NAME
    rk30_mux_api_set(rk_platform_wifi_gpio.reset_n.iomux.name, rk_platform_wifi_gpio.reset_n.iomux.fgpio);
    #endif
    gpio_request(rk_platform_wifi_gpio.reset_n.io, "combo-RST");
    gpio_direction_output(rk_platform_wifi_gpio.reset_n.io, !(rk_platform_wifi_gpio.reset_n.enable));

    //power
    #ifdef RK30SDK_WIFI_GPIO_POWER_PIN_NAME
    rk30_mux_api_set(rk_platform_wifi_gpio.power_n.iomux.name, rk_platform_wifi_gpio.power_n.iomux.fgpio);
    #endif
    gpio_request(rk_platform_wifi_gpio.power_n.io, "combo-PMUEN");	
	gpio_direction_output(rk_platform_wifi_gpio.power_n.io, !(rk_platform_wifi_gpio.power_n.enable));

	#if COMBO_MODULE_MT6620_CDT
	//ANTSEL2
	#ifdef RK30SDK_WIFI_GPIO_ANTSEL2
	    #ifdef RK30SDK_WIFI_GPIO_ANTSEL2_PIN_NAME
        rk30_mux_api_set(rk_platform_wifi_gpio.ANTSEL2.iomux.name, rk_platform_wifi_gpio.ANTSEL2.iomux.fgpio);
        #endif
    gpio_request(rk_platform_wifi_gpio.ANTSEL2.io, "combo-ANTSEL2");
    gpio_direction_output(rk_platform_wifi_gpio.reset_n.io, rk_platform_wifi_gpio.ANTSEL2.enable);
    #endif

    //ANTSEL3
    #ifdef RK30SDK_WIFI_GPIO_ANTSEL3
        #ifdef RK30SDK_WIFI_GPIO_ANTSEL3_PIN_NAME
        rk30_mux_api_set(rk_platform_wifi_gpio.ANTSEL3.iomux.name, rk_platform_wifi_gpio.ANTSEL3.iomux.fgpio);
        #endif
    gpio_request(rk_platform_wifi_gpio.ANTSEL3.io, "combo-ANTSEL3");
    gpio_direction_output(rk_platform_wifi_gpio.ANTSEL3.io, rk_platform_wifi_gpio.ANTSEL3.enable);
    #endif

    //GPS_LAN
    #ifdef RK30SDK_WIFI_GPIO_GPS_LAN
        #ifdef RK30SDK_WIFI_GPIO_GPS_LAN_PIN_NAME
        rk30_mux_api_set(rk_platform_wifi_gpio.GPS_LAN.iomux.name, rk_platform_wifi_gpio.GPS_LAN.iomux.fgpio);
        #endif
    gpio_request(rk_platform_wifi_gpio.GPS_LAN.io, "combo-GPSLAN");
    gpio_direction_output(rk_platform_wifi_gpio.GPS_LAN.io, rk_platform_wifi_gpio.GPS_LAN.enable);
	#endif

	#endif//#if COMBO_MODULE_MT6620_CDT ---#endif 

    return 0;
}


int rk29sdk_wifi_combo_module_power(int on)
{
     if(on)
    {
    #ifdef RK30SDK_WIFI_GPIO_VCCIO_WL
        gpio_set_value(rk_platform_wifi_gpio.vddio.io, rk_platform_wifi_gpio.vddio.enable);
        mdelay(10);
    #endif
	
        gpio_set_value(rk_platform_wifi_gpio.power_n.io, rk_platform_wifi_gpio.power_n.enable);     
        mdelay(10);
        pr_info("combo-module turn on power\n");
    }
    else
    {
        gpio_set_value(rk_platform_wifi_gpio.power_n.io, !(rk_platform_wifi_gpio.power_n.enable) );        
        mdelay(10);

	 #ifdef RK30SDK_WIFI_GPIO_VCCIO_WL	
        gpio_set_value(rk_platform_wifi_gpio.vddio.io, !(rk_platform_wifi_gpio.vddio.enable));
	 #endif
	 
        pr_info("combo-module turn off power\n");
    }
     return 0;
    
}
EXPORT_SYMBOL(rk29sdk_wifi_combo_module_power);


int rk29sdk_wifi_combo_module_reset(int on)
{
    if(on)
    {
        gpio_set_value(rk_platform_wifi_gpio.reset_n.io, rk_platform_wifi_gpio.reset_n.enable);     
        pr_info("combo-module reset out 1\n");
    }
    else
    {
        gpio_set_value(rk_platform_wifi_gpio.reset_n.io, !(rk_platform_wifi_gpio.reset_n.enable) );        
        pr_info("combo-module  reset out 0\n");
    }

    return 0;   
}
EXPORT_SYMBOL(rk29sdk_wifi_combo_module_reset);


static int rk29sdk_wifi_mmc0_status(struct device *dev)
{
        return rk29sdk_wifi_mmc0_cd;
}

static int rk29sdk_wifi_mmc0_status_register(void (*callback)(int card_present, void *dev_id), void *dev_id)
{
        if(wifi_mmc0_status_cb)
                return -EAGAIN;
        wifi_mmc0_status_cb = callback;
        wifi_mmc0_status_cb_devid = dev_id;
        return 0;
}


static int rk29sdk_wifi_status(struct device *dev)
{
        return rk29sdk_wifi_cd;
}

static int rk29sdk_wifi_status_register(void (*callback)(int card_present, void *dev_id), void *dev_id)
{
        if(wifi_status_cb)
                return -EAGAIN;
        wifi_status_cb = callback;
        wifi_status_cb_devid = dev_id;
        return 0;
}

int rk29sdk_wifi_power(int on)
{
    pr_info("%s: %d\n", __func__, on);
    if (on){
    
        #if defined(CONFIG_SDMMC1_RK29) && !defined(CONFIG_SDMMC_RK29_OLD)  
            
          #if defined(CONFIG_USE_SDMMC0_FOR_WIFI_DEVELOP_BOARD)
             rk29_sdmmc_gpio_open(0, 1); 
          #else
            rk29_sdmmc_gpio_open(1, 0);                
            mdelay(10);
            rk29_sdmmc_gpio_open(1, 1); 
          #endif 
        #endif
    
            mdelay(100);
            pr_info("wifi turn on power\n");
    }
    else
    {    
#if defined(CONFIG_SDMMC1_RK29) && !defined(CONFIG_SDMMC_RK29_OLD)
        #if defined(CONFIG_USE_SDMMC0_FOR_WIFI_DEVELOP_BOARD)
        rk29_sdmmc_gpio_open(0, 0);
        #else
        rk29_sdmmc_gpio_open(1, 0);
        #endif
#endif      
        mdelay(100);
        pr_info("wifi shut off power\n");
         
    }
    
    rk29sdk_wifi_power_state = on;
    return 0;

}
EXPORT_SYMBOL(rk29sdk_wifi_power);


int rk29sdk_wifi_reset(int on)
{    
    return 0;
}
EXPORT_SYMBOL(rk29sdk_wifi_reset);


#if defined(CONFIG_USE_SDMMC0_FOR_WIFI_DEVELOP_BOARD)
int rk29sdk_wifi_set_carddetect(int val)
{
    pr_info("%s:%d\n", __func__, val);
    rk29sdk_wifi_mmc0_cd = val;
    if (wifi_mmc0_status_cb){
            wifi_mmc0_status_cb(val, wifi_mmc0_status_cb_devid);
    }else {
            pr_warning("%s,in mmc0 nobody to notify\n", __func__);
    }
    return 0; 
}

#else
int rk29sdk_wifi_set_carddetect(int val)
{
    pr_info("%s:%d\n", __func__, val);
    rk29sdk_wifi_cd = val;
    if (wifi_status_cb){
            wifi_status_cb(val, wifi_status_cb_devid);
    }else {
            pr_warning("%s,in mmc1 nobody to notify\n", __func__);
    }
    return 0; 
}
#endif

EXPORT_SYMBOL(rk29sdk_wifi_set_carddetect);

///////////////////////////////////////////////////////////////////////////////////
#endif  //#if defined(CONFIG_WIFI_CONTROL_FUNC)---#elif defined(CONFIG_WIFI_COMBO_MODULE_CONTROL_FUNC) --#endif



#if defined(CONFIG_WIFI_CONTROL_FUNC)
static struct wifi_platform_data rk29sdk_wifi_control = {
        .set_power = rk29sdk_wifi_power,
        .set_reset = rk29sdk_wifi_reset,
        .set_carddetect = rk29sdk_wifi_set_carddetect,
        .mem_prealloc   = rk29sdk_mem_prealloc,
};

static struct platform_device rk29sdk_wifi_device = {
        .name = "bcmdhd_wlan",
        .id = 1,
        .num_resources = ARRAY_SIZE(resources),
        .resource = resources,
        .dev = {
                .platform_data = &rk29sdk_wifi_control,
         },
};

#elif defined(CONFIG_WIFI_COMBO_MODULE_CONTROL_FUNC)

    #if debug_combo_system
        static struct combo_module_platform_data rk29sdk_combo_module_control = {
            .set_power = rk29sdk_wifi_combo_module_power,
            .set_reset = rk29sdk_wifi_combo_module_reset,  
        };

        static struct platform_device  rk29sdk_combo_module_device = {
                .name = "combo-system",
                .id = 1,
                .dev = {
                        .platform_data = &rk29sdk_combo_module_control,
                 },
        };
    #endif

static struct wifi_platform_data rk29sdk_wifi_control = {
        .set_power = rk29sdk_wifi_power,
        .set_reset = rk29sdk_wifi_reset,
        .set_carddetect = rk29sdk_wifi_set_carddetect,
};

static struct platform_device rk29sdk_wifi_device = {
        .name = "combo-wifi",
        .id = 1,
        .dev = {
                .platform_data = &rk29sdk_wifi_control,
         },
};

#endif


