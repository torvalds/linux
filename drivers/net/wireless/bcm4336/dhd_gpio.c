
#include <osl.h>

#ifdef CUSTOMER_HW
#include "ap6210.h"

struct wifi_platform_data {
	int (*set_power)(bool val);
	int (*set_carddetect)(bool val);
	void *(*mem_prealloc)(int section, unsigned long size);
	int (*get_mac_addr)(unsigned char *buf);
	void *(*get_country_code)(char *ccode);
};

struct resource dhd_wlan_resources = {0};
struct wifi_platform_data dhd_wlan_control = {0};

#ifdef CUSTOMER_OOB
uint bcm_wlan_get_oob_irq(void)
{
	uint host_oob_irq = 0;

#ifdef GPIO_WLAN_HOST_WAKE
	printk("GPIO(GPIO_WLAN_HOST_WAKE) = %d\n", brcm_gpio_host_wake());
	host_oob_irq = gpio_to_irq(brcm_gpio_host_wake());
	gpio_direction_input(brcm_gpio_host_wake());
#endif

	printk("host_oob_irq: %d\n", host_oob_irq);
	return host_oob_irq;
}

uint bcm_wlan_get_oob_irq_flags(void)
{
	uint host_oob_irq_flags = 0;

#ifdef GPIO_WLAN_HOST_WAKE
	host_oob_irq_flags = (IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL | IORESOURCE_IRQ_SHAREABLE) & IRQF_TRIGGER_MASK;
#endif
	printk("host_oob_irq_flags = %x\n", host_oob_irq_flags);

	return host_oob_irq_flags;
}
#endif

int bcm_wlan_set_power(bool on)
{
	int err = 0;

	if (on) {
		printk("======== PULL WL_REG_ON HIGH! ========\n");
#ifdef GPIO_WLAN_EN
		gpio_set_value(GPIO_WLAN_EN, 1);
#endif
		/* Lets customer power to get stable */
		mdelay(50);
	} else {
		printk("======== PULL WL_REG_ON LOW! ========\n");
#ifdef GPIO_WLAN_EN
		gpio_set_value(GPIO_WLAN_EN, 0);
		mdelay(50);
#endif
	}

	return err;
}

int bcm_wlan_set_carddetect(bool present)
{
	int err = 0;

#if 0
	if (present) {
		printk("======== Card detection to detect SDIO card! ========\n");
		err = sdhci_s3c_force_presence_change(&sdmmc_channel, 1);
	} else {
		printk("======== Card detection to remove SDIO card! ========\n");
		err = sdhci_s3c_force_presence_change(&sdmmc_channel, 0);
	}
#endif

	mmc_force_presence_change_onoff(&sdmmc_channel, present);
	return err;
}

#ifdef CONFIG_DHD_USE_STATIC_BUF
extern void *bcmdhd_mem_prealloc(int section, unsigned long size);
void* bcm_wlan_prealloc(int section, unsigned long size)
{
	void *alloc_ptr = NULL;
	alloc_ptr = bcmdhd_mem_prealloc(section, size);
	if (alloc_ptr) {
		printk("success alloc section %d, size %ld\n", section, size);
		if (size != 0L)
			bzero(alloc_ptr, size);
		return alloc_ptr;
	}
	printk("can't alloc section %d\n", section);
	return NULL;
}
#endif

int bcm_wlan_set_plat_data(void) {
	printk("======== %s ========\n", __FUNCTION__);
	dhd_wlan_control.set_power = bcm_wlan_set_power;
	dhd_wlan_control.set_carddetect = bcm_wlan_set_carddetect;
#ifdef CONFIG_DHD_USE_STATIC_BUF
	dhd_wlan_control.mem_prealloc = bcm_wlan_prealloc;
#endif
	return 0;
}

#endif /* CUSTOMER_HW */
