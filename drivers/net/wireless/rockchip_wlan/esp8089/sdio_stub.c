/*
 * Copyright (c) 2013 Espressif System.
 *
 *  sdio stub code for RK
 */

//#include <mach/gpio.h> libing
//#include <mach/iomux.h> libing

#define ESP8089_DRV_VERSION "2.20"
//extern int rk29sdk_wifi_power(int on); libing
extern int rockchip_wifi_power(int on);
//extern int rk29sdk_wifi_set_carddetect(int val); libing
extern int rockchip_wifi_set_carddetect(int val);
int rockchip_wifi_init_module(void)
{
		
return esp_sdio_init();		
}

void rockchip_wifi_exit_module(void)
{
	esp_sdio_exit(); 
		 
}
void sif_platform_rescan_card(unsigned insert)
{
		//rk29sdk_wifi_set_carddetect(insert); libing
        rockchip_wifi_set_carddetect(insert);
}

void sif_platform_reset_target(void)
{
	if(sif_get_bt_config() == 1 && sif_get_retry_config() == 0){
		if(sif_get_rst_config() == 1){
			//TODO, use gpio to reset target
		}// else {
		//	iomux_set(GPIO3_D2);
		//	gpio_request(RK30_PIN3_PD2, "esp8089-sdio-wifi");

		//	mdelay(100);
		//	gpio_direction_output(RK30_PIN3_PD2, 0);
		//	mdelay(50);
		//	gpio_direction_output(RK30_PIN3_PD2, 1);
	//	}
	} else {
		//rk29sdk_wifi_set_carddetect(0);
        	//rk29sdk_wifi_power(0); //libing
			rockchip_wifi_power(0);
		//rk29sdk_wifi_power(1); //libing
		rockchip_wifi_power(1);
        	//rk29sdk_wifi_set_carddetect(1);
	}
}

void sif_platform_target_poweroff(void)
{
	printk("=======================================================\n");
	printk("==== Dislaunching Wi-Fi driver! (Powered by Rockchip) ====\n");
	printk("=======================================================\n");
	printk("Espressif ESP8089 SDIO WiFi driver (Powered by Rockchip,Ver %s) init.\n", ESP8089_DRV_VERSION);

	//rk29sdk_wifi_set_carddetect(0);
	if(sif_get_bt_config() != 1)
        	//rk29sdk_wifi_power(0); libing
			rockchip_wifi_power(0);
}

void sif_platform_target_poweron(void)
{
	printk("=======================================================\n");
	printk("==== Launching Wi-Fi driver! (Powered by Rockchip) ====\n");
	printk("=======================================================\n");
	printk("Espressif ESP8089 SDIO WiFi driver (Powered by Rockchip,Ver %s) init.\n", ESP8089_DRV_VERSION);

	if(sif_get_bt_config() == 1){
		sif_platform_reset_target();
	}
	
	//rk29sdk_wifi_power(1); libing
	rockchip_wifi_power(1);
        //rk29sdk_wifi_set_carddetect(1);
}

void sif_platform_target_speed(int high_speed)
{
}

void sif_platform_check_r1_ready(struct esp_pub *epub)
{
}


#ifdef ESP_ACK_INTERRUPT
//extern void sdmmc_ack_interrupt(struct mmc_host *mmc); libing

void sif_platform_ack_interrupt(struct esp_pub *epub)
{
        struct esp_sdio_ctrl *sctrl = NULL;
        struct sdio_func *func = NULL;

        ASSERT(epub != NULL);
        sctrl = (struct esp_sdio_ctrl *)epub->sif;
        func = sctrl->func;
        ASSERT(func != NULL);

        //sdmmc_ack_interrupt(func->card->host);libing

}
#endif //ESP_ACK_INTERRUPT
 EXPORT_SYMBOL(rockchip_wifi_init_module);
 EXPORT_SYMBOL(rockchip_wifi_exit_module);

//late_initcall(esp_sdio_init);
//module_exit(esp_sdio_exit);
