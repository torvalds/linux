/* linux/arch/arm/mach-msm/board-olympus-wifi.c
*/
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <asm/mach-types.h>
#include <asm/gpio.h>
#include <asm/io.h>
#include <linux/skbuff.h>
#include <linux/wlan_plat.h>
#include <mach/sdhci.h>

#include "board-olympus.h"
#include "gpio-names.h"

#define OLYMPUS_WLAN_IRQ	TEGRA_GPIO_PU5
#define OLYMPUS_WLAN_PWR	TEGRA_GPIO_PU3
#define OLYMPUS_WLAN_RST	TEGRA_GPIO_PU2

#define PREALLOC_WLAN_NUMBER_OF_SECTIONS	4
#define PREALLOC_WLAN_NUMBER_OF_BUFFERS		160
#define PREALLOC_WLAN_SECTION_HEADER		24

#define WLAN_SECTION_SIZE_0	(PREALLOC_WLAN_NUMBER_OF_BUFFERS * 128)
#define WLAN_SECTION_SIZE_1	(PREALLOC_WLAN_NUMBER_OF_BUFFERS * 128)
#define WLAN_SECTION_SIZE_2	(PREALLOC_WLAN_NUMBER_OF_BUFFERS * 512)
#define WLAN_SECTION_SIZE_3	(PREALLOC_WLAN_NUMBER_OF_BUFFERS * 1024)

#define WLAN_SKB_BUF_NUM	16

static struct sk_buff *wlan_static_skb[WLAN_SKB_BUF_NUM];

typedef struct wifi_mem_prealloc_struct {
	void *mem_ptr;
	unsigned long size;
} wifi_mem_prealloc_t;

static wifi_mem_prealloc_t wifi_mem_array[PREALLOC_WLAN_NUMBER_OF_SECTIONS] = {
	{ NULL, (WLAN_SECTION_SIZE_0 + PREALLOC_WLAN_SECTION_HEADER) },
	{ NULL, (WLAN_SECTION_SIZE_1 + PREALLOC_WLAN_SECTION_HEADER) },
	{ NULL, (WLAN_SECTION_SIZE_2 + PREALLOC_WLAN_SECTION_HEADER) },
	{ NULL, (WLAN_SECTION_SIZE_3 + PREALLOC_WLAN_SECTION_HEADER) }
};

static void *olympus_wifi_mem_prealloc(int section, unsigned long size)
{
	if (section == PREALLOC_WLAN_NUMBER_OF_SECTIONS)
		return wlan_static_skb;
	if ((section < 0) || (section > PREALLOC_WLAN_NUMBER_OF_SECTIONS))
		return NULL;
	if (wifi_mem_array[section].size < size)
		return NULL;
	return wifi_mem_array[section].mem_ptr;
}

int __init olympus_init_wifi_mem(void)
{
	int i;

	for(i=0;( i < WLAN_SKB_BUF_NUM );i++) {
		if (i < (WLAN_SKB_BUF_NUM/2))
			wlan_static_skb[i] = dev_alloc_skb(4096);
		else
			wlan_static_skb[i] = dev_alloc_skb(8192);
	}
	for(i=0;( i < PREALLOC_WLAN_NUMBER_OF_SECTIONS );i++) {
		wifi_mem_array[i].mem_ptr = kmalloc(wifi_mem_array[i].size,
							GFP_KERNEL);
		if (wifi_mem_array[i].mem_ptr == NULL)
			return -ENOMEM;
	}
	return 0;
}

static struct resource olympus_wifi_resources[] = {
	[0] = {
		.name		= "bcm4329_wlan_irq",
		.start		= TEGRA_GPIO_TO_IRQ(OLYMPUS_WLAN_IRQ),
		.end		= TEGRA_GPIO_TO_IRQ(OLYMPUS_WLAN_IRQ),
		.flags          = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWEDGE,
	},
};

/* BCM4329 returns wrong sdio_vsn(1) when we read cccr,
 * we use predefined value (sdio_vsn=2) here to initial sdio driver well
  */
static struct embedded_sdio_data olympus_wifi_emb_data = {
	.cccr	= {
		.sdio_vsn       = 2,
		.multi_block    = 1,
		.low_speed      = 0,
		.wide_bus       = 0,
		.high_power     = 1,
		.high_speed     = 1,
	},
};

static int olympus_wifi_cd = 0; /* WIFI virtual 'card detect' status */
static void (*wifi_status_cb)(int card_present, void *dev_id);
static void *wifi_status_cb_devid;

static int olympus_wifi_status_register(
		void (*callback)(int card_present, void *dev_id),
		void *dev_id)
{
	if (wifi_status_cb)
		return -EAGAIN;
	wifi_status_cb = callback;
	wifi_status_cb_devid = dev_id;
	return 0;
}

static unsigned int olympus_wifi_status(struct device *dev)
{
	return olympus_wifi_cd;
}

struct tegra_sdhci_platform_data olympus_wifi_data = {
	.clk_id = NULL,
	.force_hs = 0,
	.mmc_data = {
		.ocr_mask		= MMC_VDD_165_195,
		.status			= olympus_wifi_status,
		.register_status_notify	= olympus_wifi_status_register,
		.embedded_sdio		= &olympus_wifi_emb_data,
	}
};

int olympus_wifi_set_carddetect(int val)
{
	pr_debug("%s: %d\n", __func__, val);
	olympus_wifi_cd = val;
	if (wifi_status_cb) {
		wifi_status_cb(val, wifi_status_cb_devid);
	} else
		pr_warning("%s: Nobody to notify\n", __func__);
	return 0;
}

static int olympus_wifi_power_state;

int olympus_wifi_power(int on)
{
	pr_debug("%s: %d\n", __func__, on);

	mdelay(100);
	gpio_set_value(OLYMPUS_WLAN_PWR, on);
	mdelay(100);
	gpio_set_value(OLYMPUS_WLAN_RST, on);
	mdelay(200);

	olympus_wifi_power_state = on;
	return 0;
}

static int olympus_wifi_reset_state;

int olympus_wifi_reset(int on)
{
	pr_debug("%s: do nothing\n", __func__);
	olympus_wifi_reset_state = on;
	return 0;
}

static struct wifi_platform_data olympus_wifi_control = {
	.set_power      = olympus_wifi_power,
	.set_reset      = olympus_wifi_reset,
	.set_carddetect = olympus_wifi_set_carddetect,
	.mem_prealloc	= olympus_wifi_mem_prealloc,
};

static struct platform_device olympus_wifi_device = {
        .name           = "bcm4329_wlan",
        .id             = 1,
        .num_resources  = ARRAY_SIZE(olympus_wifi_resources),
        .resource       = olympus_wifi_resources,
        .dev            = {
                .platform_data = &olympus_wifi_control,
        },
};

static void __init olympus_wlan_gpio(void)
{
	tegra_gpio_enable(OLYMPUS_WLAN_PWR);
	gpio_request(OLYMPUS_WLAN_PWR, "wlan_pwr");
	gpio_direction_output(OLYMPUS_WLAN_PWR, 0);

	tegra_gpio_enable(OLYMPUS_WLAN_RST);
	gpio_request(OLYMPUS_WLAN_RST, "wlan_rst");
	gpio_direction_output(OLYMPUS_WLAN_RST, 0);

	tegra_gpio_enable(OLYMPUS_WLAN_IRQ);
	gpio_request(OLYMPUS_WLAN_IRQ, "wlan_irq");
	gpio_direction_input(OLYMPUS_WLAN_IRQ);
}

int __init olympus_wlan_init(void)
{
	pr_debug("%s: start\n", __func__);
	olympus_wlan_gpio();
	olympus_init_wifi_mem();
	return platform_device_register(&olympus_wifi_device);
}
