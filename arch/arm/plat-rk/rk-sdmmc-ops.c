/***************************************************************************************************
 * arch/arm/palt-rk/rk-sdmmc-ops.c
 *
 * Copyright (C) 2013 ROCKCHIP, Inc.
 *
 * Description: define the gpio for SDMMC module on various platforms
 *
 * Author: Michael Xie
 * E-mail: xbw@rock-chips.com
 *
 * History:
 *     ver1.0 Unified function interface for new imoux-API, created at 2013-01-15
 *     ver1.1 add drive strength control and the setting of IO-voltage, created at 2013-01-29
 *
 **************************************************************************************************/

//use the new iomux-API
#if 1//defined(CONFIG_ARCH_RK3066B)||defined(CONFIG_ARCH_RK3168)||defined(CONFIG_ARCH_RK3188)
#define SDMMC_USE_NEW_IOMUX_API 1
#else
#define SDMMC_USE_NEW_IOMUX_API 0
#endif

//define IO volatga
#if defined(CONFIG_ARCH_RK3066B)||defined(CONFIG_ARCH_RK3168)||defined(CONFIG_ARCH_RK3188)
#define SDMMC_SET_IO_VOLTAGE    1
#else
#define SDMMC_SET_IO_VOLTAGE    0
#endif

#if SDMMC_SET_IO_VOLTAGE
//GRF_IO_CON2                        0x0FC
#define SDMMC0_DRIVER_STRENGTH_2MA            (0x00 << 6)
#define SDMMC0_DRIVER_STRENGTH_4MA            (0x01 << 6)
#define SDMMC0_DRIVER_STRENGTH_8MA            (0x02 << 6)
#define SDMMC0_DRIVER_STRENGTH_12MA           (0x03 << 6)
#define SDMMC0_DRIVER_STRENGTH_MASK           (0x03 << 22)

//GRF_IO_CON3                        0x100
#define SDMMC1_DRIVER_STRENGTH_2MA            (0x00 << 2)
#define SDMMC1_DRIVER_STRENGTH_4MA            (0x01 << 2)
#define SDMMC1_DRIVER_STRENGTH_8MA            (0x02 << 2)
#define SDMMC1_DRIVER_STRENGTH_12MA           (0x03 << 2)
#define SDMMC1_DRIVER_STRENGTH_MASK           (0x03 << 18)

//GRF_IO_CON4       0x104
#define SDMMC0_IO_VOLTAGE_33            (0x00 << 12)
#define SDMMC0_IO_VOLTAGE_18            (0x01 << 12)
#define SDMMC0_IO_VOLTAGE_MASK          (0x01 << 28)

#define SDMMC1_IO_VOLTAGE_33            (0x00 << 8)
#define SDMMC1_IO_VOLTAGE_18            (0x01 << 8)
#define SDMMC1_IO_VOLTAGE_MASK          (0x01 << 24)

#define SDMMC_write_grf_reg(addr, val)  __raw_writel(val, addr+RK30_GRF_BASE)
#define SDMMC_read_grf_reg(addr) __raw_readl(addr+RK30_GRF_BASE)
#define SDMMC_mask_grf_reg(addr, msk, val)	write_grf_reg(addr,(val)|((~(msk))&read_grf_reg(addr)))
#else
#define SDMMC_write_grf_reg(addr, val)  
#define SDMMC_read_grf_reg(addr)
#define SDMMC_mask_grf_reg(addr, msk, val)	
#endif

int rk31sdk_wifi_voltage_select(void)
{
    int voltage;
    int voltage_flag = 0;

    voltage = rk31sdk_get_sdio_wifi_voltage();
   
     if(voltage >= 2700)
        voltage_flag = 0;
     else if(voltage <= 2000)
        voltage_flag = 1;
     else
        voltage_flag = 1;

    return voltage_flag;
}


#if defined(CONFIG_ARCH_RK3066B) || defined(CONFIG_ARCH_RK3188)
/*
* define the gpio for sdmmc0
*/
static struct rksdmmc_gpio_board rksdmmc0_gpio_init = {

     .clk_gpio       = {
        .io             = RK30_PIN3_PA2,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = "mmc0_clkout",
            .fgpio      = GPIO3_A2,
            .fmux       = MMC0_CLKOUT,
        },
    },   

    .cmd_gpio           = {
        .io             = RK30_PIN3_PA3,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = "mmc0_cmd",
            .fgpio      = GPIO3_A3,
            .fmux       = MMC0_CMD,
        },
    },      

   .data0_gpio       = {
        .io             = RK30_PIN3_PA4,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = "mmc0_d0",
            .fgpio      = GPIO3_A4,
            .fmux       = MMC0_D0,
        },
    },      

    .data1_gpio       = {
        .io             = RK30_PIN3_PA5,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = "mmc0_d1",
            .fgpio      = GPIO3_A5,
            .fmux       = MMC0_D1,
        },
    },      

    .data2_gpio       = {
        .io             = RK30_PIN3_PA6,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = "mmc0_d2",
            .fgpio      = GPIO3_A6,
            .fmux       = MMC0_D2,
        },
    }, 

    .data3_gpio       = {
        .io             = RK30_PIN3_PA7,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = "mmc0_d3",
            .fgpio      = GPIO3_A7,
            .fmux       = MMC0_D3,
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
            .name       = "mmc1_clkout",
            .fgpio      = GPIO3_C5,
            .fmux       = MMC1_CLKOUT,
        },
    },   

    .cmd_gpio           = {
        .io             = RK30_PIN3_PC0,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = "mmc1_cmd",
            .fgpio      = GPIO3_C0,
            .fmux       = MMC1_CMD,
        },
    },      

   .data0_gpio       = {
        .io             = RK30_PIN3_PC1,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = "mmc1_d0",
            .fgpio      = GPIO3_C1,
            .fmux       = MMC1_D0,
        },
    },      

    .data1_gpio       = {
        .io             = RK30_PIN3_PC2,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = "mmc1_d1",
            .fgpio      = GPIO3_C2,
            .fmux       = MMC1_D1,
        },
    },      

    .data2_gpio       = {
        .io             = RK30_PIN3_PC3,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = "mmc1_d2",
            .fgpio      = GPIO3_C3,
            .fmux       = MMC1_D2,
        },
    }, 

    .data3_gpio       = {
        .io             = RK30_PIN3_PC4,
        .enable         = GPIO_HIGH,
        .iomux          = {
            .name       = "mmc1_d3",
            .fgpio      = GPIO3_C4,
            .fmux       = MMC1_D3,
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

#else //default for RK30,RK3066 SDK
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
 // ---end -defualt rk30sdk,rk3066sdk

#endif



//1.Part 3: The various operations of the SDMMC-SDIO module
/*************************************************************************
* define the varaious operations for SDMMC module
* Generally only the author of SDMMC module will modify this section.
*************************************************************************/
#if !defined(CONFIG_SDMMC_RK29_OLD)	
//static void rk29_sdmmc_gpio_open(int device_id, int on)
void rk29_sdmmc_gpio_open(int device_id, int on)
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
                #if !(!!SDMMC_USE_NEW_IOMUX_API)
                rk30_mux_api_set(rksdmmc0_gpio_init.clk_gpio.iomux.name, rksdmmc0_gpio_init.clk_gpio.iomux.fgpio);
                #endif
                gpio_request(rksdmmc0_gpio_init.clk_gpio.io, "mmc0-clk");
                gpio_direction_output(rksdmmc0_gpio_init.clk_gpio.io,GPIO_LOW);//set mmc0-clk to low.

                #if !(!!SDMMC_USE_NEW_IOMUX_API)
                rk30_mux_api_set(rksdmmc0_gpio_init.cmd_gpio.iomux.name, rksdmmc0_gpio_init.cmd_gpio.iomux.fgpio);
                #endif
                gpio_request(rksdmmc0_gpio_init.cmd_gpio.io, "mmc0-cmd");
                gpio_direction_output(rksdmmc0_gpio_init.cmd_gpio.io,GPIO_LOW);//set mmc0-cmd to low.

                #if !(!!SDMMC_USE_NEW_IOMUX_API)
                rk30_mux_api_set(rksdmmc0_gpio_init.data0_gpio.iomux.name, rksdmmc0_gpio_init.data0_gpio.iomux.fgpio);
                #endif
                gpio_request(rksdmmc0_gpio_init.data0_gpio.io, "mmc0-data0");
                gpio_direction_output(rksdmmc0_gpio_init.data0_gpio.io,GPIO_LOW);//set mmc0-data0 to low.

                #if !(!!SDMMC_USE_NEW_IOMUX_API)
                rk30_mux_api_set(rksdmmc0_gpio_init.data1_gpio.iomux.name, rksdmmc0_gpio_init.data1_gpio.iomux.fgpio);
                #endif
                gpio_request(rksdmmc0_gpio_init.data1_gpio.io, "mmc0-data1");
                gpio_direction_output(rksdmmc0_gpio_init.data1_gpio.io,GPIO_LOW);//set mmc0-data1 to low.

                #if !(!!SDMMC_USE_NEW_IOMUX_API)
                rk30_mux_api_set(rksdmmc0_gpio_init.data2_gpio.iomux.name, rksdmmc0_gpio_init.data2_gpio.iomux.fgpio);
                #endif
                gpio_request(rksdmmc0_gpio_init.data2_gpio.io, "mmc0-data2");
                gpio_direction_output(rksdmmc0_gpio_init.data2_gpio.io,GPIO_LOW);//set mmc0-data2 to low.

                #if !(!!SDMMC_USE_NEW_IOMUX_API)
                rk30_mux_api_set(rksdmmc0_gpio_init.data3_gpio.iomux.name, rksdmmc0_gpio_init.data3_gpio.iomux.fgpio);
                #endif
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
                #if !(!!SDMMC_USE_NEW_IOMUX_API)
                rk30_mux_api_set(rksdmmc1_gpio_init.clk_gpio.iomux.name, rksdmmc1_gpio_init.clk_gpio.iomux.fgpio);
                #endif
                gpio_request(rksdmmc1_gpio_init.clk_gpio.io, "mmc1-clk");
                gpio_direction_output(rksdmmc1_gpio_init.clk_gpio.io,GPIO_LOW);//set mmc1-clk to low.

                #if !(!!SDMMC_USE_NEW_IOMUX_API)
                rk30_mux_api_set(rksdmmc1_gpio_init.cmd_gpio.iomux.name, rksdmmc1_gpio_init.cmd_gpio.iomux.fgpio);
                #endif
                gpio_request(rksdmmc1_gpio_init.cmd_gpio.io, "mmc1-cmd");
                gpio_direction_output(rksdmmc1_gpio_init.cmd_gpio.io,GPIO_LOW);//set mmc1-cmd to low.

                #if !(!!SDMMC_USE_NEW_IOMUX_API)
                rk30_mux_api_set(rksdmmc1_gpio_init.data0_gpio.iomux.name, rksdmmc1_gpio_init.data0_gpio.iomux.fgpio);
                #endif
                gpio_request(rksdmmc1_gpio_init.data0_gpio.io, "mmc1-data0");
                gpio_direction_output(rksdmmc1_gpio_init.data0_gpio.io,GPIO_LOW);//set mmc1-data0 to low.
                
            #if defined(CONFIG_WIFI_COMBO_MODULE_CONTROL_FUNC) || defined(CONFIG_MT5931) || defined(CONFIG_MT5931_MT6622)
                #if !(!!SDMMC_USE_NEW_IOMUX_API)
                rk29_mux_api_set(rksdmmc1_gpio_init.data1_gpio.iomux.name, rksdmmc1_gpio_init.data1_gpio.iomux.fgpio);
                #endif
                gpio_request(rksdmmc1_gpio_init.data1_gpio.io, "mmc1-data1");
                gpio_direction_output(rksdmmc1_gpio_init.data1_gpio.io,GPIO_LOW);//set mmc1-data1 to low.

                #if !(!!SDMMC_USE_NEW_IOMUX_API)
                rk29_mux_api_set(rksdmmc1_gpio_init.data2_gpio.iomux.name, rksdmmc1_gpio_init.data2_gpio.iomux.fgpio);
                #endif
                gpio_request(rksdmmc1_gpio_init.data2_gpio.io, "mmc1-data2");
                gpio_direction_output(rksdmmc1_gpio_init.data2_gpio.io,GPIO_LOW);//set mmc1-data2 to low.

                #if !(!!SDMMC_USE_NEW_IOMUX_API)
                rk29_mux_api_set(rksdmmc1_gpio_init.data3_gpio.iomux.name, rksdmmc1_gpio_init.data3_gpio.iomux.fgpio);
                #endif
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
    	    #if SDMMC_USE_NEW_IOMUX_API
            iomux_set(rksdmmc0_gpio_init.data1_gpio.iomux.fmux);
            iomux_set(rksdmmc0_gpio_init.data2_gpio.iomux.fmux);
            iomux_set(rksdmmc0_gpio_init.data3_gpio.iomux.fmux);
    	    #else
        	rk30_mux_api_set(rksdmmc0_gpio_init.data1_gpio.iomux.name, rksdmmc0_gpio_init.data1_gpio.iomux.fmux);
        	rk30_mux_api_set(rksdmmc0_gpio_init.data2_gpio.iomux.name, rksdmmc0_gpio_init.data2_gpio.iomux.fmux);
        	rk30_mux_api_set(rksdmmc0_gpio_init.data3_gpio.iomux.name, rksdmmc0_gpio_init.data3_gpio.iomux.fmux);
        	#endif
    	}
    	break;

    	case 0x10000://SDMMC_CTYPE_8BIT:
    	    break;
    	case 0xFFFF: //gpio_reset
    	{
    	    #if (!!SDMMC_USE_NEW_IOMUX_API) && !defined(CONFIG_SDMMC0_RK29_SDCARD_DET_FROM_GPIO)
    	    iomux_set(MMC0_DETN);
    	    #endif
    	    
    	    #if !(!!SDMMC_USE_NEW_IOMUX_API)
            rk30_mux_api_set(rksdmmc0_gpio_init.power_en_gpio.iomux.name, rksdmmc0_gpio_init.power_en_gpio.iomux.fgpio);
            #endif
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
            #if SDMMC_USE_NEW_IOMUX_API
        	iomux_set(rksdmmc0_gpio_init.cmd_gpio.iomux.fmux);
        	iomux_set(rksdmmc0_gpio_init.clk_gpio.iomux.fmux);
        	iomux_set(rksdmmc0_gpio_init.data0_gpio.iomux.fmux);
            #else
        	rk30_mux_api_set(rksdmmc0_gpio_init.cmd_gpio.iomux.name, rksdmmc0_gpio_init.cmd_gpio.iomux.fmux);
        	rk30_mux_api_set(rksdmmc0_gpio_init.clk_gpio.iomux.name, rksdmmc0_gpio_init.clk_gpio.iomux.fmux);
        	rk30_mux_api_set(rksdmmc0_gpio_init.data0_gpio.iomux.name, rksdmmc0_gpio_init.data0_gpio.iomux.fmux);
        	#endif

            //IO voltage(vccio);
            #ifdef RK31SDK_SET_SDMMC0_PIN_VOLTAGE
                if(rk31sdk_get_sdmmc0_pin_io_voltage() > 2700)
                    SDMMC_write_grf_reg(GRF_IO_CON4, (SDMMC0_IO_VOLTAGE_MASK |SDMMC0_IO_VOLTAGE_33)); //set SDMMC0 pin to 3.3v
                else
                    SDMMC_write_grf_reg(GRF_IO_CON4, (SDMMC0_IO_VOLTAGE_MASK |SDMMC0_IO_VOLTAGE_18));//set SDMMC0 pin to 1.8v
            #else
            //default set the voltage of SDMMC0 to 3.3V
            SDMMC_write_grf_reg(GRF_IO_CON4, (SDMMC0_IO_VOLTAGE_MASK |SDMMC0_IO_VOLTAGE_33));
            #endif
    
            //sdmmc drive strength control
            SDMMC_write_grf_reg(GRF_IO_CON2, (SDMMC0_DRIVER_STRENGTH_MASK |SDMMC0_DRIVER_STRENGTH_8MA));
            
            #if !(!!SDMMC_USE_NEW_IOMUX_API)
            rk30_mux_api_set(rksdmmc0_gpio_init.data1_gpio.iomux.name, rksdmmc0_gpio_init.data1_gpio.iomux.fgpio);
            #endif
            gpio_request(rksdmmc0_gpio_init.data1_gpio.io, "mmc0-data1");
            gpio_direction_output(rksdmmc0_gpio_init.data1_gpio.io,GPIO_HIGH);//set mmc0-data1 to high.

            #if !(!!SDMMC_USE_NEW_IOMUX_API)
            rk30_mux_api_set(rksdmmc0_gpio_init.data2_gpio.iomux.name, rksdmmc0_gpio_init.data2_gpio.iomux.fgpio);
            #endif
            gpio_request(rksdmmc0_gpio_init.data2_gpio.io, "mmc0-data2");
            gpio_direction_output(rksdmmc0_gpio_init.data2_gpio.io,GPIO_HIGH);//set mmc0-data2 to high.

            #if !(!!SDMMC_USE_NEW_IOMUX_API)
            rk30_mux_api_set(rksdmmc0_gpio_init.data3_gpio.iomux.name, rksdmmc0_gpio_init.data3_gpio.iomux.fgpio);
            #endif
            gpio_request(rksdmmc0_gpio_init.data3_gpio.io, "mmc0-data3");
            gpio_direction_output(rksdmmc0_gpio_init.data3_gpio.io,GPIO_HIGH);//set mmc0-data3 to high.
    	}
    	break;
	}
}

static void rk29_sdmmc_set_iomux_mmc1(unsigned int bus_width)
{
    #if SDMMC_USE_NEW_IOMUX_API
    iomux_set(rksdmmc1_gpio_init.cmd_gpio.iomux.fmux);
    iomux_set(rksdmmc1_gpio_init.clk_gpio.iomux.fmux);
    iomux_set(rksdmmc1_gpio_init.data0_gpio.iomux.fmux);
    iomux_set(rksdmmc1_gpio_init.data1_gpio.iomux.fmux);
    iomux_set(rksdmmc1_gpio_init.data2_gpio.iomux.fmux);
    iomux_set(rksdmmc1_gpio_init.data3_gpio.iomux.fmux);
    #else
    rk30_mux_api_set(rksdmmc1_gpio_init.cmd_gpio.iomux.name, rksdmmc1_gpio_init.cmd_gpio.iomux.fmux);
    rk30_mux_api_set(rksdmmc1_gpio_init.clk_gpio.iomux.name, rksdmmc1_gpio_init.clk_gpio.iomux.fmux);
    rk30_mux_api_set(rksdmmc1_gpio_init.data0_gpio.iomux.name, rksdmmc1_gpio_init.data0_gpio.iomux.fmux);
    rk30_mux_api_set(rksdmmc1_gpio_init.data1_gpio.iomux.name, rksdmmc1_gpio_init.data1_gpio.iomux.fmux);
    rk30_mux_api_set(rksdmmc1_gpio_init.data2_gpio.iomux.name, rksdmmc1_gpio_init.data2_gpio.iomux.fmux);
    rk30_mux_api_set(rksdmmc1_gpio_init.data3_gpio.iomux.name, rksdmmc1_gpio_init.data3_gpio.iomux.fmux);
    #endif

     //IO voltage(vcc-ap0)
    if(rk31sdk_wifi_voltage_select())
        SDMMC_write_grf_reg(GRF_IO_CON4, (SDMMC1_IO_VOLTAGE_MASK|SDMMC1_IO_VOLTAGE_18));       
    else
        SDMMC_write_grf_reg(GRF_IO_CON4, (SDMMC1_IO_VOLTAGE_MASK|SDMMC1_IO_VOLTAGE_33));

    //sdmmc1 drive strength control
    SDMMC_write_grf_reg(GRF_IO_CON3, (SDMMC1_DRIVER_STRENGTH_MASK|SDMMC1_DRIVER_STRENGTH_12MA));
    
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


