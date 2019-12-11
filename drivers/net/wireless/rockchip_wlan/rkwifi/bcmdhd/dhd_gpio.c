/* SPDX-License-Identifier: GPL-2.0 */
#include <osl.h>
#include <dhd_linux.h>
#include <linux/gpio.h>
#include <linux/rfkill-wlan.h>

#ifdef CUSTOMER_HW_PLATFORM
#include <plat/sdhci.h>
#define	sdmmc_channel	sdmmc_device_mmc0
#endif /* CUSTOMER_HW_PLATFORM */

#if defined(BUS_POWER_RESTORE) && defined(BCMSDIO)
#include <linux/mmc/core.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#endif /* defined(BUS_POWER_RESTORE) && defined(BCMSDIO) */

#ifdef CONFIG_DHD_USE_STATIC_BUF
extern void *dhd_wlan_mem_prealloc(int section, unsigned long size);
#endif /* CONFIG_DHD_USE_STATIC_BUF */

static int gpio_wl_reg_on = -1; // WL_REG_ON is input pin of WLAN module
#ifdef CUSTOMER_OOB
static int gpio_wl_host_wake = -1; // WL_HOST_WAKE is output pin of WLAN module
#endif

static int
dhd_wlan_set_power(int on
#ifdef BUS_POWER_RESTORE
, wifi_adapter_info_t *adapter
#endif /* BUS_POWER_RESTORE */
)
{
	int err = 0;

	if (on) {
		printf("======== PULL WL_REG_ON(%d) HIGH! ========\n", gpio_wl_reg_on);
		if (gpio_wl_reg_on >= 0) {
			err = gpio_direction_output(gpio_wl_reg_on, 1);
			if (err) {
				printf("%s: WL_REG_ON didn't output high\n", __FUNCTION__);
				return -EIO;
			}
		}
		rockchip_wifi_power(1);
#if defined(BUS_POWER_RESTORE)
#if defined(BCMSDIO) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
		if (adapter->sdio_func && adapter->sdio_func->card && adapter->sdio_func->card->host) {
			mdelay(100);
			printf("======== mmc_power_restore_host! ========\n");
			mmc_power_restore_host(adapter->sdio_func->card->host);
		}
#elif defined(BCMPCIE)
		if (adapter->pci_dev) {
			mdelay(100);
			printf("======== pci_set_power_state PCI_D0! ========\n");
			pci_set_power_state(adapter->pci_dev, PCI_D0);
			if (adapter->pci_saved_state)
				pci_load_and_free_saved_state(adapter->pci_dev, &adapter->pci_saved_state);
			pci_restore_state(adapter->pci_dev);
			err = pci_enable_device(adapter->pci_dev);
			if (err < 0)
				printf("%s: PCI enable device failed", __FUNCTION__);
			pci_set_master(adapter->pci_dev);
		}
#endif /* BCMPCIE */
#endif /* BUS_POWER_RESTORE */
		/* Lets customer power to get stable */
		mdelay(100);
	} else {
#if defined(BUS_POWER_RESTORE)
#if defined(BCMSDIO) && (LINUX_VERSION_CODE < KERNEL_VERSION(4, 19, 0))
		if (adapter->sdio_func && adapter->sdio_func->card && adapter->sdio_func->card->host) {
			printf("======== mmc_power_save_host! ========\n");
			mmc_power_save_host(adapter->sdio_func->card->host);
		}
#elif defined(BCMPCIE)
		if (adapter->pci_dev) {
			printf("======== pci_set_power_state PCI_D3hot! ========\n");
			pci_save_state(adapter->pci_dev);
			adapter->pci_saved_state = pci_store_saved_state(adapter->pci_dev);
			if (pci_is_enabled(adapter->pci_dev))
				pci_disable_device(adapter->pci_dev);
			pci_set_power_state(adapter->pci_dev, PCI_D3hot);
		}
#endif /* BCMPCIE */
#endif /* BUS_POWER_RESTORE */
		printf("======== PULL WL_REG_ON(%d) LOW! ========\n", gpio_wl_reg_on);
		if (gpio_wl_reg_on >= 0) {
			err = gpio_direction_output(gpio_wl_reg_on, 0);
			if (err) {
				printf("%s: WL_REG_ON didn't output low\n", __FUNCTION__);
				return -EIO;
			}
		}
		rockchip_wifi_power(0);
	}

	return err;
}

static int dhd_wlan_set_reset(int onoff)
{
	return 0;
}

static int dhd_wlan_set_carddetect(int present)
{
	int err = 0;

#if !defined(BUS_POWER_RESTORE)
	if (present) {
#if defined(BCMSDIO)
		printf("======== Card detection to detect SDIO card! ========\n");
#ifdef CUSTOMER_HW_PLATFORM
		err = sdhci_force_presence_change(&sdmmc_channel, 1);
#endif /* CUSTOMER_HW_PLATFORM */
		rockchip_wifi_set_carddetect(1);
#elif defined(BCMPCIE)
		printf("======== Card detection to detect PCIE card! ========\n");
#endif
	} else {
#if defined(BCMSDIO)
		printf("======== Card detection to remove SDIO card! ========\n");
#ifdef CUSTOMER_HW_PLATFORM
		err = sdhci_force_presence_change(&sdmmc_channel, 0);
#endif /* CUSTOMER_HW_PLATFORM */
	rockchip_wifi_set_carddetect(0);
#elif defined(BCMPCIE)
		printf("======== Card detection to remove PCIE card! ========\n");
#endif
	}
#endif /* BUS_POWER_RESTORE */

	return err;
}

static int dhd_wlan_get_mac_addr(unsigned char *buf)
{
	int err = 0;

	printf("======== %s ========\n", __FUNCTION__);
#ifdef EXAMPLE_GET_MAC
	/* EXAMPLE code */
	{
		struct ether_addr ea_example = {{0x00, 0x11, 0x22, 0x33, 0x44, 0xFF}};
		bcopy((char *)&ea_example, buf, sizeof(struct ether_addr));
	}
#endif /* EXAMPLE_GET_MAC */
	err = rockchip_wifi_mac_addr(buf);
#ifdef EXAMPLE_GET_MAC_VER2
	/* EXAMPLE code */
	{
		char macpad[56]= {
		0x00,0xaa,0x9c,0x84,0xc7,0xbc,0x9b,0xf6,
		0x02,0x33,0xa9,0x4d,0x5c,0xb4,0x0a,0x5d,
		0xa8,0xef,0xb0,0xcf,0x8e,0xbf,0x24,0x8a,
		0x87,0x0f,0x6f,0x0d,0xeb,0x83,0x6a,0x70,
		0x4a,0xeb,0xf6,0xe6,0x3c,0xe7,0x5f,0xfc,
		0x0e,0xa7,0xb3,0x0f,0x00,0xe4,0x4a,0xaf,
		0x87,0x08,0x16,0x6d,0x3a,0xe3,0xc7,0x80};
		bcopy(macpad, buf+6, sizeof(macpad));
	}
#endif /* EXAMPLE_GET_MAC_VER2 */

	return err;
}

static struct cntry_locales_custom brcm_wlan_translate_custom_table[] = {
	/* Table should be filled out based on custom platform regulatory requirement */
#ifdef EXAMPLE_TABLE
	{"",   "XT", 49},  /* Universal if Country code is unknown or empty */
	{"US", "US", 0},
#endif /* EXMAPLE_TABLE */
};

#ifdef CUSTOM_FORCE_NODFS_FLAG
struct cntry_locales_custom brcm_wlan_translate_nodfs_table[] = {
#ifdef EXAMPLE_TABLE
	{"",   "XT", 50},  /* Universal if Country code is unknown or empty */
	{"US", "US", 0},
#endif /* EXMAPLE_TABLE */
};
#endif

static void *dhd_wlan_get_country_code(char *ccode
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

struct resource dhd_wlan_resources[] = {
	[0] = {
		.name	= "bcmdhd_wlan_irq",
		.start	= 0, /* Dummy */
		.end	= 0, /* Dummy */
		.flags	= IORESOURCE_IRQ | IORESOURCE_IRQ_SHAREABLE
			| IORESOURCE_IRQ_HIGHLEVEL, /* Dummy */
	},
};

struct wifi_platform_data dhd_wlan_control = {
	.set_power	= dhd_wlan_set_power,
	.set_reset	= dhd_wlan_set_reset,
	.set_carddetect	= dhd_wlan_set_carddetect,
	.get_mac_addr	= dhd_wlan_get_mac_addr,
#ifdef CONFIG_DHD_USE_STATIC_BUF
	.mem_prealloc	= dhd_wlan_mem_prealloc,
#endif /* CONFIG_DHD_USE_STATIC_BUF */
	.get_country_code = dhd_wlan_get_country_code,
};

int dhd_wlan_init_gpio(void)
{
	int err = 0;
#ifdef CUSTOMER_OOB
	int host_oob_irq = -1;
	uint host_oob_irq_flags = 0;
	int irq_flags = -1;
#endif

	/* Please check your schematic and fill right GPIO number which connected to
	* WL_REG_ON and WL_HOST_WAKE.
	*/
	gpio_wl_reg_on = -1;
#ifdef CUSTOMER_OOB
	gpio_wl_host_wake = -1;
#endif

	if (gpio_wl_reg_on >= 0) {
		err = gpio_request(gpio_wl_reg_on, "WL_REG_ON");
		if (err < 0) {
			printf("%s: gpio_request(%d) for WL_REG_ON failed\n",
				__FUNCTION__, gpio_wl_reg_on);
			gpio_wl_reg_on = -1;
		}
	}

#ifdef CUSTOMER_OOB
	if (gpio_wl_host_wake >= 0) {
		err = gpio_request(gpio_wl_host_wake, "bcmdhd");
		if (err < 0) {
			printf("%s: gpio_request(%d) for WL_HOST_WAKE failed\n",
				__FUNCTION__, gpio_wl_host_wake);
			return -1;
		}
		err = gpio_direction_input(gpio_wl_host_wake);
		if (err < 0) {
			printf("%s: gpio_direction_input(%d) for WL_HOST_WAKE failed\n",
				__FUNCTION__, gpio_wl_host_wake);
			gpio_free(gpio_wl_host_wake);
			return -1;
		}
		host_oob_irq = gpio_to_irq(gpio_wl_host_wake);
		if (host_oob_irq < 0) {
			printf("%s: gpio_to_irq(%d) for WL_HOST_WAKE failed\n",
				__FUNCTION__, gpio_wl_host_wake);
			gpio_free(gpio_wl_host_wake);
			return -1;
		}
	}
	host_oob_irq = rockchip_wifi_get_oob_irq();

#ifdef HW_OOB
	host_oob_irq_flags = IORESOURCE_IRQ | IORESOURCE_IRQ_SHAREABLE;
	irq_flags = rockchip_wifi_get_oob_irq_flag();
	if (irq_flags == 1)
		host_oob_irq_flags |= IORESOURCE_IRQ_HIGHLEVEL;
	else if (irq_flags == 0)
		host_oob_irq_flags |= IORESOURCE_IRQ_LOWLEVEL;
	else
		pr_warn("%s: unknown oob irqflags !\n", __func__);
#else
	host_oob_irq_flags = IORESOURCE_IRQ | IORESOURCE_IRQ_HIGHEDGE | IORESOURCE_IRQ_SHAREABLE;
#endif

	dhd_wlan_resources[0].start = dhd_wlan_resources[0].end = host_oob_irq;
	dhd_wlan_resources[0].flags = host_oob_irq_flags;
	printf("%s: WL_HOST_WAKE=%d, oob_irq=%d, oob_irq_flags=0x%x\n", __FUNCTION__,
		gpio_wl_host_wake, host_oob_irq, host_oob_irq_flags);
#endif /* CUSTOMER_OOB */
	printf("%s: WL_REG_ON=%d\n", __FUNCTION__, gpio_wl_reg_on);

	return 0;
}

static void dhd_wlan_deinit_gpio(void)
{
	if (gpio_wl_reg_on >= 0) {
		printf("%s: gpio_free(WL_REG_ON %d)\n", __FUNCTION__, gpio_wl_reg_on);
		gpio_free(gpio_wl_reg_on);
		gpio_wl_reg_on = -1;
	}
#ifdef CUSTOMER_OOB
	if (gpio_wl_host_wake >= 0) {
		printf("%s: gpio_free(WL_HOST_WAKE %d)\n", __FUNCTION__, gpio_wl_host_wake);
		gpio_free(gpio_wl_host_wake);
		gpio_wl_host_wake = -1;
	}
#endif /* CUSTOMER_OOB */
}

int dhd_wlan_init_plat_data(void)
{
	int err = 0;

	printf("======== %s ========\n", __FUNCTION__);
	err = dhd_wlan_init_gpio();
	return err;
}

void dhd_wlan_deinit_plat_data(wifi_adapter_info_t *adapter)
{
	printf("======== %s ========\n", __FUNCTION__);
	dhd_wlan_deinit_gpio();
}

