/* linux/arch/arm/mach-msm/board-stingray-wifi.c
*/
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <asm/mach-types.h>
#include <asm/gpio.h>
#include <asm/io.h>
#include <asm/setup.h>
#include <linux/if.h>
#include <linux/skbuff.h>
#include <linux/wlan_plat.h>
#include <mach/sdhci.h>

#include <linux/random.h>
#include <linux/jiffies.h>

#include "board-stingray.h"
#include "gpio-names.h"

#define STINGRAY_WLAN_IRQ	TEGRA_GPIO_PU5
#define STINGRAY_WLAN_PWR	TEGRA_GPIO_PU4
#define STINGRAY_WLAN_RST	TEGRA_GPIO_PU2

#define ATAG_STINGRAY_MAC	0x57464d41
#define ATAG_STINGRAY_MAC_DEBUG

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

static void *stingray_wifi_mem_prealloc(int section, unsigned long size)
{
	if (section == PREALLOC_WLAN_NUMBER_OF_SECTIONS)
		return wlan_static_skb;
	if ((section < 0) || (section > PREALLOC_WLAN_NUMBER_OF_SECTIONS))
		return NULL;
	if (wifi_mem_array[section].size < size)
		return NULL;
	return wifi_mem_array[section].mem_ptr;
}

int __init stingray_init_wifi_mem(void)
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

static struct resource stingray_wifi_resources[] = {
	[0] = {
		.name		= "bcm4329_wlan_irq",
		.start		= TEGRA_GPIO_TO_IRQ(STINGRAY_WLAN_IRQ),
		.end		= TEGRA_GPIO_TO_IRQ(STINGRAY_WLAN_IRQ),
		.flags          = IORESOURCE_IRQ | IORESOURCE_IRQ_LOWEDGE,
	},
};

/* BCM4329 returns wrong sdio_vsn(1) when we read cccr,
 * we use predefined value (sdio_vsn=2) here to initial sdio driver well
  */
static struct embedded_sdio_data stingray_wifi_emb_data = {
	.cccr	= {
		.sdio_vsn       = 2,
		.multi_block    = 1,
		.low_speed      = 0,
		.wide_bus       = 0,
		.high_power     = 1,
		.high_speed     = 1,
	},
};

static int stingray_wifi_cd = 0; /* WIFI virtual 'card detect' status */
static void (*wifi_status_cb)(int card_present, void *dev_id);
static void *wifi_status_cb_devid;

static int stingray_wifi_status_register(
		void (*callback)(int card_present, void *dev_id),
		void *dev_id)
{
	if (wifi_status_cb)
		return -EAGAIN;
	wifi_status_cb = callback;
	wifi_status_cb_devid = dev_id;
	return 0;
}

static unsigned int stingray_wifi_status(struct device *dev)
{
	return stingray_wifi_cd;
}

struct tegra_sdhci_platform_data stingray_wifi_data = {
	.clk_id = NULL,
	.force_hs = 0,
	.mmc_data = {
		.ocr_mask		= MMC_VDD_165_195,
		.built_in		= 1,
		.status			= stingray_wifi_status,
		.register_status_notify	= stingray_wifi_status_register,
		.embedded_sdio		= &stingray_wifi_emb_data,
	}
};

static int stingray_wifi_set_carddetect(int val)
{
	pr_debug("%s: %d\n", __func__, val);
	stingray_wifi_cd = val;
	if (wifi_status_cb) {
		wifi_status_cb(val, wifi_status_cb_devid);
	} else
		pr_warning("%s: Nobody to notify\n", __func__);
	return 0;
}

static int stingray_wifi_power_state;

static int stingray_wifi_power(int on)
{
	pr_debug("%s: %d\n", __func__, on);

	mdelay(100);
	gpio_set_value(STINGRAY_WLAN_PWR, on);
	mdelay(100);
	gpio_set_value(STINGRAY_WLAN_RST, on);
	mdelay(200);

	stingray_wifi_power_state = on;
	return 0;
}

static int stingray_wifi_reset_state;

static int stingray_wifi_reset(int on)
{
	pr_debug("%s: do nothing\n", __func__);
	stingray_wifi_reset_state = on;
	return 0;
}

static unsigned char stingray_mac_addr[IFHWADDRLEN] = { 0,0x90,0x4c,0,0,0 };

static int __init parse_tag_wlan_mac(const struct tag *tag)
{
	unsigned char *dptr = (unsigned char *)(&tag->u);
	unsigned size;
#ifdef ATAG_STINGRAY_MAC_DEBUG
	unsigned i;
#endif

	size = min((tag->hdr.size - 2) * sizeof(__u32), (unsigned)IFHWADDRLEN);
#ifdef ATAG_STINGRAY_MAC_DEBUG
	printk("WiFi MAC Addr [%d] = 0x%x\n", tag->hdr.size, tag->hdr.tag);
	for(i=0;(i < size);i++) {
		printk(" %02x", dptr[i]);
	}
	printk("\n");
#endif
	memcpy(stingray_mac_addr, dptr, size);
	return 0;
}

__tagtable(ATAG_STINGRAY_MAC, parse_tag_wlan_mac);

static int stingray_wifi_get_mac_addr(unsigned char *buf)
{
	uint rand_mac;

	if (!buf)
		return -EINVAL;

	if ((stingray_mac_addr[4] == 0) && (stingray_mac_addr[5] == 0)) {
		srandom32((uint)jiffies);
		rand_mac = random32();
		stingray_mac_addr[3] = (unsigned char)rand_mac;
		stingray_mac_addr[4] = (unsigned char)(rand_mac >> 8);
		stingray_mac_addr[5] = (unsigned char)(rand_mac >> 16);
	}
	memcpy(buf, stingray_mac_addr, IFHWADDRLEN);
	return 0;
}

static struct wifi_platform_data stingray_wifi_control = {
	.set_power      = stingray_wifi_power,
	.set_reset      = stingray_wifi_reset,
	.set_carddetect = stingray_wifi_set_carddetect,
	.mem_prealloc	= stingray_wifi_mem_prealloc,
	.get_mac_addr	= stingray_wifi_get_mac_addr,
};

static struct platform_device stingray_wifi_device = {
        .name           = "bcm4329_wlan",
        .id             = 1,
        .num_resources  = ARRAY_SIZE(stingray_wifi_resources),
        .resource       = stingray_wifi_resources,
        .dev            = {
                .platform_data = &stingray_wifi_control,
        },
};

static void __init stingray_wlan_gpio(void)
{
	tegra_gpio_enable(STINGRAY_WLAN_PWR);
	gpio_request(STINGRAY_WLAN_PWR, "wlan_pwr");
	gpio_direction_output(STINGRAY_WLAN_PWR, 0);

	tegra_gpio_enable(STINGRAY_WLAN_RST);
	gpio_request(STINGRAY_WLAN_RST, "wlan_rst");
	gpio_direction_output(STINGRAY_WLAN_RST, 0);

	tegra_gpio_enable(STINGRAY_WLAN_IRQ);
	gpio_request(STINGRAY_WLAN_IRQ, "wlan_irq");
	gpio_direction_input(STINGRAY_WLAN_IRQ);
}

int __init stingray_wlan_init(void)
{
	pr_debug("%s: start\n", __func__);
	stingray_wlan_gpio();
	stingray_init_wifi_mem();
	return platform_device_register(&stingray_wifi_device);
}
