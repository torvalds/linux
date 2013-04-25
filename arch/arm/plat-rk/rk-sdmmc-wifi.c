/******************************************************************************************
 * arch/arm/palt-rk/rk-sdmmc-wifi.c
 *
 * Copyright (C) 2013 ROCKCHIP, Inc.
 *
 * Description: define the varaious operations for Wifi module
 *
 * Author: Michael Xie
 * E-mail: xbw@rock-chips.com
 *
 * History:
 *      ver1.0 Unified function interface for new imoux-API, created at 2013-01-15
 *
 *******************************************************************************************/

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
    
#if defined(COMBO_MODULE_MT6620_CDT) && COMBO_MODULE_MT6620_CDT
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

    if (rk_platform_wifi_gpio.power_n.io != INVALID_GPIO) {
        if (gpio_request(rk_platform_wifi_gpio.power_n.io, "wifi_power")) {
               pr_info("%s: request wifi power gpio failed\n", __func__);
               return -1;
        }
    }

#ifdef RK30SDK_WIFI_GPIO_RESET_N
    if (rk_platform_wifi_gpio.reset_n.io != INVALID_GPIO) {
        if (gpio_request(rk_platform_wifi_gpio.reset_n.io, "wifi reset")) {
               pr_info("%s: request wifi reset gpio failed\n", __func__);
               gpio_free(rk_platform_wifi_gpio.power_n.io);
               return -1;
        }
    }
#endif    

    if (rk_platform_wifi_gpio.power_n.io != INVALID_GPIO)
        gpio_direction_output(rk_platform_wifi_gpio.power_n.io, !(rk_platform_wifi_gpio.power_n.enable) );

#ifdef RK30SDK_WIFI_GPIO_RESET_N 
    if (rk_platform_wifi_gpio.reset_n.io != INVALID_GPIO)
        gpio_direction_output(rk_platform_wifi_gpio.reset_n.io, !(rk_platform_wifi_gpio.reset_n.enable) );
#endif    

    #if defined(CONFIG_SDMMC1_RK29) && !defined(CONFIG_SDMMC_RK29_OLD)
    
    #if !defined(CONFIG_MT5931) && !defined(CONFIG_MT5931_MT6622)
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
    rk29_mux_api_set(rksdmmc1_gpio_init.data3_gpio.iomux.name,  rksdmmc1_gpio_init.data3_gpio.iomux.fgpio);
    #endif
    gpio_request(rksdmmc1_gpio_init.data3_gpio.io, "mmc1-data3");
    gpio_direction_output(rksdmmc1_gpio_init.data3_gpio.io,GPIO_LOW);//set mmc1-data3 to low.
    #endif
    
    rk29_sdmmc_gpio_open(1, 0); //added by xbw at 2011-10-13
    #endif    
    pr_info("%s: init finished\n",__func__);

    return 0;
}

#if (defined(CONFIG_RTL8192CU) || defined(CONFIG_RTL8188EU) || defined(CONFIG_RTL8723AU)) \
	&& defined(CONFIG_ARCH_RK2928)
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
                if (rk_platform_wifi_gpio.reset_n.io != INVALID_GPIO)
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
                if (rk_platform_wifi_gpio.reset_n.io != INVALID_GPIO)
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

#define WIFI_HOST_WAKE RK30_PIN3_PD2

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

	#if defined(COMBO_MODULE_MT6620_CDT) && COMBO_MODULE_MT6620_CDT
	//ANTSEL2
	#ifdef RK30SDK_WIFI_GPIO_ANTSEL2
	    #ifdef RK30SDK_WIFI_GPIO_ANTSEL2_PIN_NAME
        rk30_mux_api_set(rk_platform_wifi_gpio.ANTSEL2.iomux.name, rk_platform_wifi_gpio.ANTSEL2.iomux.fgpio);
        #endif
    gpio_request(rk_platform_wifi_gpio.ANTSEL2.io, "combo-ANTSEL2");
    gpio_direction_output(rk_platform_wifi_gpio.ANTSEL2.io, rk_platform_wifi_gpio.ANTSEL2.enable);
    #endif

    //ANTSEL3
    #ifdef RK30SDK_WIFI_GPIO_ANTSEL3
        #ifdef RK30SDK_WIFI_GPIO_ANTSEL3_PIN_NAME
        rk30_mux_api_set(rk_platform_wifi_gpio.ANTSEL3.iomux.name, rk_platform_wifi_gpio.ANTSEL3.iomux.fgpio);
        #endif
    gpio_request(rk_platform_wifi_gpio.ANTSEL3.io, "combo-ANTSEL3");
    gpio_direction_output(rk_platform_wifi_gpio.ANTSEL3.io, !(rk_platform_wifi_gpio.ANTSEL3.enable));
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
        #ifdef RK30SDK_WIFI_GPIO_ANTSEL2
        gpio_direction_output(rk_platform_wifi_gpio.ANTSEL2.io, rk_platform_wifi_gpio.ANTSEL2.enable);
        #endif

        #ifdef RK30SDK_WIFI_GPIO_ANTSEL3
        gpio_direction_output(rk_platform_wifi_gpio.ANTSEL3.io, rk_platform_wifi_gpio.ANTSEL3.enable);
        #endif

        #ifdef RK30SDK_WIFI_GPIO_GPS_LAN
        gpio_direction_output(rk_platform_wifi_gpio.GPS_LAN.io, rk_platform_wifi_gpio.GPS_LAN.enable);
        #endif
    
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

        #ifdef RK30SDK_WIFI_GPIO_ANTSEL2
        //Because the foot is pulled low, therefore, continue to remain low
        gpio_direction_output(rk_platform_wifi_gpio.ANTSEL2.io, rk_platform_wifi_gpio.ANTSEL2.enable);
        #endif

        #ifdef RK30SDK_WIFI_GPIO_ANTSEL3
        gpio_direction_output(rk_platform_wifi_gpio.ANTSEL3.io, !(rk_platform_wifi_gpio.ANTSEL3.enable));
        #endif
        
        #ifdef RK30SDK_WIFI_GPIO_GPS_LAN
        gpio_direction_output(rk_platform_wifi_gpio.GPS_LAN.io, !(rk_platform_wifi_gpio.GPS_LAN.enable));
        #endif

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


