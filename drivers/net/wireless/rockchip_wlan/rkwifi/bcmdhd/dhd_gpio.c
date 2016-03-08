
#include <osl.h>
#include <dhd_linux.h>
#include <linux/rfkill-wlan.h>

#ifdef CONFIG_MACH_ODROID_4210
#include <mach/gpio.h>
#include <mach/regs-gpio.h>
#include <plat/gpio-cfg.h>
#include <plat/sdhci.h>
#include <plat/devs.h>
#define	sdmmc_channel	s3c_device_hsmmc0
#endif

struct wifi_platform_data dhd_wlan_control = {0};

#ifdef CUSTOMER_OOB
uint bcm_wlan_get_oob_irq(void)
{
	uint host_oob_irq = 0;

	host_oob_irq = rockchip_wifi_get_oob_irq();

	printk("host_oob_irq: %d\n", host_oob_irq);

	return host_oob_irq;
}

uint bcm_wlan_get_oob_irq_flags(void)
{
	uint host_oob_irq_flags = 0;

	host_oob_irq_flags = (IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHLEVEL | IORESOURCE_IRQ_SHAREABLE) & IRQF_TRIGGER_MASK;
	printk("host_oob_irq_flags=0x%X\n", host_oob_irq_flags);

	return host_oob_irq_flags;
}
#endif

int bcm_wlan_set_power(bool on)
{
	int err = 0;

	if (on) {
		printk("======== PULL WL_REG_ON HIGH! ========\n");
		rockchip_wifi_power(1);
	} else {
		printk("======== PULL WL_REG_ON LOW! ========\n");
		rockchip_wifi_power(0);
	}

	return err;
}

int bcm_wlan_set_carddetect(bool present)
{
	int err = 0;

	if (present) {
		printk("======== Card detection to detect SDIO card! ========\n");
	        rockchip_wifi_set_carddetect(1);
	} else {
		printk("======== Card detection to remove SDIO card! ========\n");
	        rockchip_wifi_set_carddetect(0);
	}

	return err;
}

int bcm_wlan_get_mac_address(unsigned char *buf)
{
	int err = 0;

	printk("======== %s ========\n", __FUNCTION__);
#ifdef EXAMPLE_GET_MAC
	/* EXAMPLE code */
	{
		struct ether_addr ea_example = {{0x00, 0x11, 0x22, 0x33, 0x44, 0xFF}};
		bcopy((char *)&ea_example, buf, sizeof(struct ether_addr));
	}
#endif /* EXAMPLE_GET_MAC */
	err = rockchip_wifi_mac_addr(buf);

	return err;
}

#ifdef CONFIG_DHD_USE_STATIC_BUF
extern void *bcmdhd_mem_prealloc(int section, unsigned long size);
void* bcm_wlan_prealloc(int section, unsigned long size)
{
	void *alloc_ptr = NULL;
	alloc_ptr = bcmdhd_mem_prealloc(section, size);
	if (alloc_ptr) {
		printf("success alloc section %d, size %ld\n", section, size);
		if (size != 0L)
			bzero(alloc_ptr, size);
		return alloc_ptr;
	}
	printf("can't alloc section %d\n", section);
	return NULL;
}
#endif

#if !defined(WL_WIRELESS_EXT)
struct cntry_locales_custom {
	char iso_abbrev[WLC_CNTRY_BUF_SZ];	/* ISO 3166-1 country abbreviation */
	char custom_locale[WLC_CNTRY_BUF_SZ];	/* Custom firmware locale */
	int32 custom_locale_rev;		/* Custom local revisin default -1 */
};
#endif

static struct cntry_locales_custom brcm_wlan_translate_custom_table[] = {
	/* Table should be filled out based on custom platform regulatory requirement */
	{"",   "XT", 49},  /* Universal if Country code is unknown or empty */
	{"US", "US", 0},
};

#ifdef CUSTOM_FORCE_NODFS_FLAG
struct cntry_locales_custom brcm_wlan_translate_nodfs_table[] = {
	{"",   "XT", 50},  /* Universal if Country code is unknown or empty */
	{"US", "US", 0},
};
#endif

static void *bcm_wlan_get_country_code(char *ccode
#ifdef CUSTOM_FORCE_NODFS_FLAG
	, u32 flags
#endif
)
{
	struct cntry_locales_custom *locales;
	int size;
	int i;

	if (!ccode)
		return NULL;

#ifdef CUSTOM_FORCE_NODFS_FLAG
	if (flags & WLAN_PLAT_NODFS_FLAG) {
		locales = brcm_wlan_translate_nodfs_table;
		size = ARRAY_SIZE(brcm_wlan_translate_nodfs_table);
	} else {
#endif
		locales = brcm_wlan_translate_custom_table;
		size = ARRAY_SIZE(brcm_wlan_translate_custom_table);
#ifdef CUSTOM_FORCE_NODFS_FLAG
	}
#endif

	for (i = 0; i < size; i++)
		if (strcmp(ccode, locales[i].iso_abbrev) == 0)
			return &locales[i];
	return NULL;
}

int bcm_wlan_set_plat_data(void) {
	printf("======== %s ========\n", __FUNCTION__);
	dhd_wlan_control.set_power = bcm_wlan_set_power;
	dhd_wlan_control.set_carddetect = bcm_wlan_set_carddetect;
	dhd_wlan_control.get_mac_addr = bcm_wlan_get_mac_address;
#ifdef CONFIG_DHD_USE_STATIC_BUF
	dhd_wlan_control.mem_prealloc = bcm_wlan_prealloc;
#endif
	dhd_wlan_control.get_country_code = bcm_wlan_get_country_code;
	return 0;
}

