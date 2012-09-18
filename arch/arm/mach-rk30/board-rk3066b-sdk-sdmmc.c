/*
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
 */

#ifdef CONFIG_SDMMC_RK29

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
		gpio_direction_output(RK30_PIN3_PA2,GPIO_HIGH);//set mmc0-clk to high
                gpio_direction_output(RK30_PIN3_PA3,GPIO_HIGH);// set mmc0-cmd to high.
                gpio_direction_output(RK30_PIN3_PA4,GPIO_HIGH);//set mmc0-data0 to high.
                gpio_direction_output(RK30_PIN3_PA5,GPIO_HIGH);//set mmc0-data1 to high.
                gpio_direction_output(RK30_PIN3_PA6,GPIO_HIGH);//set mmc0-data2 to high.
                gpio_direction_output(RK30_PIN3_PA7,GPIO_HIGH);//set mmc0-data3 to high.
                mdelay(30);
            }
            else
            {
		rk30_mux_api_set(GPIO3A2_SDMMC0CLKOUT_NAME, GPIO3A_GPIO3A2);
                gpio_request(RK30_PIN3_PA2, "mmc0-clk");
                gpio_direction_output(RK30_PIN3_PA2,GPIO_LOW);//set mmc0-clk to low.

                rk30_mux_api_set(GPIO3A3_SDMMC0CMD_NAME, GPIO3A_GPIO3A3);
                gpio_request(RK30_PIN3_PA3, "mmc0-cmd");
                gpio_direction_output(RK30_PIN3_PA3,GPIO_LOW);//set mmc0-cmd to low.

                rk30_mux_api_set(GPIO3A4_SDMMC0DATA0_NAME, GPIO3A_GPIO3A4);
                gpio_request(RK30_PIN3_PA4, "mmc0-data0");
                gpio_direction_output(RK30_PIN3_PA4,GPIO_LOW);//set mmc0-data0 to low.

                rk30_mux_api_set(GPIO3A5_SDMMC0DATA1_NAME, GPIO3A_GPIO3A5);
                gpio_request(RK30_PIN3_PA5, "mmc0-data1");
                gpio_direction_output(RK30_PIN3_PA5,GPIO_LOW);//set mmc0-data1 to low.

                rk30_mux_api_set(GPIO3A6_SDMMC0DATA2_NAME, GPIO3A_GPIO3A6);
                gpio_request(RK30_PIN3_PA6, "mmc0-data2");
                gpio_direction_output(RK30_PIN3_PA6,GPIO_LOW);//set mmc0-data2 to low.

                rk30_mux_api_set(GPIO3A7_SDMMC0DATA3_NAME, GPIO3A_GPIO3A7);
                gpio_request(RK30_PIN3_PA7, "mmc0-data3");
                gpio_direction_output(RK30_PIN3_PA7,GPIO_LOW);//set mmc0-data3 to low.
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
                gpio_direction_output(RK30_PIN3_PC5,GPIO_HIGH);//set mmc1-clk to high
                gpio_direction_output(RK30_PIN3_PC0,GPIO_HIGH);//set mmc1-cmd to high.
                gpio_direction_output(RK30_PIN3_PC1,GPIO_HIGH);//set mmc1-data0 to high.
                gpio_direction_output(RK30_PIN3_PC2,GPIO_HIGH);//set mmc1-data1 to high.
                gpio_direction_output(RK30_PIN3_PC3,GPIO_HIGH);//set mmc1-data2 to high.
                gpio_direction_output(RK30_PIN3_PC4,GPIO_HIGH);//set mmc1-data3 to high.
                mdelay(100);
            }
            else
            {
		rk30_mux_api_set(GPIO3C5_SDMMC1CLKOUT_RMIICLKOUT_RMIICLKIN_NAME, GPIO3C_GPIO3C5);
                gpio_request(RK30_PIN3_PC5, "mmc1-clk");
                gpio_direction_output(RK30_PIN3_PC5,GPIO_LOW);//set mmc1-clk to low.

                rk30_mux_api_set(GPIO3C0_SDMMC1CMD_RMIITXEN_NAME, GPIO3C_GPIO3C0);
                gpio_request(RK30_PIN3_PC0, "mmc1-cmd");
                gpio_direction_output(RK30_PIN3_PC0,GPIO_LOW);//set mmc1-cmd to low.

                rk30_mux_api_set(GPIO3C1_SDMMC1DATA0_RMIITXD1_NAME, GPIO3C_GPIO3C1);
                gpio_request(RK30_PIN3_PC1, "mmc1-data0");
                gpio_direction_output(RK30_PIN3_PC1,GPIO_LOW);//set mmc1-data0 to low.
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
		rk30_mux_api_set(GPIO3A5_SDMMC0DATA1_NAME, GPIO3A_SDMMC0DATA1);
                rk30_mux_api_set(GPIO3A6_SDMMC0DATA2_NAME, GPIO3A_SDMMC0DATA2);
                rk30_mux_api_set(GPIO3A7_SDMMC0DATA3_NAME, GPIO3A_SDMMC0DATA3);
    	}
    	break;

    	case 0x10000://SDMMC_CTYPE_8BIT:
    	    break;
    	case 0xFFFF: //gpio_reset
    	{
	    rk30_mux_api_set(GPIO3A1_SDMMC0PWREN_NAME, GPIO3A_GPIO3A1);
            gpio_request(RK30_PIN3_PA1,"sdmmc-power");
            gpio_direction_output(RK30_PIN3_PA1,GPIO_HIGH); //power-off

            rk29_sdmmc_gpio_open(0, 0);

            gpio_direction_output(RK30_PIN3_PA1,GPIO_LOW); //power-on

            rk29_sdmmc_gpio_open(0, 1);
    	}
    	break;

    	default: //case 0://SDMMC_CTYPE_1BIT:
        {
	    rk30_mux_api_set(GPIO3A3_SDMMC0CMD_NAME, GPIO3A_SDMMC0CMD);
            rk30_mux_api_set(GPIO3A2_SDMMC0CLKOUT_NAME, GPIO3A_SDMMC0CLKOUT);
            rk30_mux_api_set(GPIO3A4_SDMMC0DATA0_NAME, GPIO3A_SDMMC0DATA0);

            rk30_mux_api_set(GPIO3A5_SDMMC0DATA1_NAME, GPIO3A_GPIO3A5);
            gpio_request(RK30_PIN3_PA5, "mmc0-data1");
            gpio_direction_output(RK30_PIN3_PA5,GPIO_HIGH);//set mmc0-data1 to high.

            rk30_mux_api_set(GPIO3A6_SDMMC0DATA2_NAME, GPIO3A_GPIO3A6);
            gpio_request(RK30_PIN3_PA5, "mmc0-data2");
            gpio_direction_output(RK30_PIN3_PA5,GPIO_HIGH);//set mmc0-data2 to high.

            rk30_mux_api_set(GPIO3A7_SDMMC0DATA3_NAME, GPIO3A_GPIO3A7);
            gpio_request(RK30_PIN3_PA7, "mmc0-data3");
            gpio_direction_output(RK30_PIN3_PA7,GPIO_HIGH);//set mmc0-data3 to high.
    	}
    	break;
	}
}

static void rk29_sdmmc_set_iomux_mmc1(unsigned int bus_width)
{
    rk30_mux_api_set(GPIO3C0_SDMMC1CMD_RMIITXEN_NAME, GPIO3C_SDMMC1CMD);
    rk30_mux_api_set(GPIO3C5_SDMMC1CLKOUT_RMIICLKOUT_RMIICLKIN_NAME, GPIO3C_SDMMC1CLKOUT);
    rk30_mux_api_set(GPIO3C1_SDMMC1DATA0_RMIITXD1_NAME, GPIO3C_SDMMC1DATA0);
    rk30_mux_api_set(GPIO3C2_SDMMC1DATA1_RMIITXD0_NAME, GPIO3C_SDMMC1DATA1);
    rk30_mux_api_set(GPIO3C3_SDMMC1DATA2_RMIIRXD0_NAME, GPIO3C_SDMMC1DATA2);
    rk30_mux_api_set(GPIO3C4_SDMMC1DATA3_RMIIRXD1_NAME, GPIO3C_SDMMC1DATA3);
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



#ifdef CONFIG_WIFI_CONTROL_FUNC

//
// Define wifi module's power and reset gpio, and gpio sensitive level 
//

#if defined(CONFIG_RK903) || defined(CONFIG_RK901)
#define RK30SDK_WIFI_GPIO_POWER_N       RK30_PIN3_PD0
#define RK30SDK_WIFI_GPIO_POWER_ENABLE_VALUE GPIO_HIGH 
#define RK30SDK_WIFI_GPIO_RESET_N       RK30_PIN2_PA7
#define RK30SDK_WIFI_GPIO_RESET_ENABLE_VALUE GPIO_HIGH 
#endif

#if defined(CONFIG_RTL8192CU) || defined(CONFIG_RTL8188EU) 
#define RK30SDK_WIFI_GPIO_POWER_N       RK30_PIN3_PD0
#define RK30SDK_WIFI_GPIO_POWER_ENABLE_VALUE GPIO_LOW 
#endif

#if defined(CONFIG_BCM4329) || defined(CONFIG_BCM4319) 
#define RK30SDK_WIFI_GPIO_POWER_N       RK30_PIN3_PD0
#define RK30SDK_WIFI_GPIO_POWER_ENABLE_VALUE GPIO_HIGH 
#define RK30SDK_WIFI_GPIO_RESET_N       RK30_PIN2_PA7
#define RK30SDK_WIFI_GPIO_RESET_ENABLE_VALUE GPIO_HIGH 
#endif

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

static int rk29sdk_wifi_cd = 0;   /* wifi virtual 'card detect' status */
static void (*wifi_status_cb)(int card_present, void *dev_id);
static void *wifi_status_cb_devid;

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
    
    rk29_mux_api_set(GPIO2A7_LCDC1DATA7_SMCDATA7_TRACEDATA7_NAME, GPIO2A_GPIO2A7);
    rk29_mux_api_set(GPIO3D0_SDMMC1PWREN_MIIMD_NAME, GPIO3D_GPIO3D0);
    
#ifdef RK30SDK_WIFI_GPIO_POWER_N
    if (gpio_request(RK30SDK_WIFI_GPIO_POWER_N, "wifi_power")) {
           pr_info("%s: request wifi power gpio failed\n", __func__);
           return -1;
    }
#endif

#ifdef RK30SDK_WIFI_GPIO_RESET_N
    if (gpio_request(RK30SDK_WIFI_GPIO_RESET_N, "wifi reset")) {
           pr_info("%s: request wifi reset gpio failed\n", __func__);
           gpio_free(RK30SDK_WIFI_GPIO_POWER_N);
           return -1;
    }
#endif    

#ifdef RK30SDK_WIFI_GPIO_POWER_N
    gpio_direction_output(RK30SDK_WIFI_GPIO_POWER_N, !RK30SDK_WIFI_GPIO_POWER_ENABLE_VALUE);
#endif
#ifdef RK30SDK_WIFI_GPIO_RESET_N    
    gpio_direction_output(RK30SDK_WIFI_GPIO_RESET_N, !RK30SDK_WIFI_GPIO_RESET_ENABLE_VALUE);
#endif    

    #if defined(CONFIG_SDMMC1_RK29) && !defined(CONFIG_SDMMC_RK29_OLD)
    
    rk29_mux_api_set(GPIO3C2_SDMMC1DATA1_RMIITXD0_NAME, GPIO3C_GPIO3C2);
    gpio_request(RK30_PIN3_PC2, "mmc1-data1");
    gpio_direction_output(RK30_PIN3_PC2,GPIO_LOW);//set mmc1-data1 to low.

    rk29_mux_api_set(GPIO3C3_SDMMC1DATA2_RMIIRXD0_NAME, GPIO3C_GPIO3C3);
    gpio_request(RK30_PIN3_PC3, "mmc1-data2");
    gpio_direction_output(RK30_PIN3_PC3,GPIO_LOW);//set mmc1-data2 to low.

    rk29_mux_api_set(GPIO3C4_SDMMC1DATA3_RMIIRXD1_NAME, GPIO3C_GPIO3C4);
    gpio_request(RK30_PIN3_PC4, "mmc1-data3");
    gpio_direction_output(RK30_PIN3_PC4,GPIO_LOW);//set mmc1-data3 to low.
    
    rk29_sdmmc_gpio_open(1, 0); //added by xbw at 2011-10-13
#endif
    pr_info("%s: init finished\n",__func__);

    return 0;
}

int rk29sdk_wifi_power(int on)
{
        pr_info("%s: %d\n", __func__, on);
        if (on){
#ifdef RK30SDK_WIFI_GPIO_POWER_N
                gpio_set_value(RK30SDK_WIFI_GPIO_POWER_N, RK30SDK_WIFI_GPIO_POWER_ENABLE_VALUE);
                mdelay(50);
#endif

                #if defined(CONFIG_SDMMC1_RK29) && !defined(CONFIG_SDMMC_RK29_OLD)	
                rk29_sdmmc_gpio_open(1, 1); //added by xbw at 2011-10-13
                #endif

#ifdef RK30SDK_WIFI_GPIO_RESET_N
                gpio_set_value(RK30SDK_WIFI_GPIO_RESET_N, RK30SDK_WIFI_GPIO_RESET_ENABLE_VALUE);
#endif                
                mdelay(100);
                pr_info("wifi turn on power\n");
        }else{
//                if (!rk29sdk_bt_power_state){
#ifdef RK30SDK_WIFI_GPIO_POWER_N
                        gpio_set_value(RK30SDK_WIFI_GPIO_POWER_N, !RK30SDK_WIFI_GPIO_POWER_ENABLE_VALUE);
#endif

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
                gpio_set_value(RK30SDK_WIFI_GPIO_RESET_N, !RK30SDK_WIFI_GPIO_RESET_ENABLE_VALUE);
#endif 

        }

        //rk29sdk_wifi_power_state = on;
        return 0;
}
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
#endif


#endif // endif --#ifdef CONFIG_SDMMC_RK29

