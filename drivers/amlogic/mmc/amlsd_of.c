/*amlsd_of.c*/
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/mmc/host.h>
#include <linux/mmc/core.h>
#include <linux/mmc/mmc.h>
#include <linux/mtd/partitions.h>
#include <linux/slab.h>
#include <mach/sd.h>
#include <mach/pinmux.h>
#include <linux/of_address.h>
#include <mach/am_regs.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include "amlsd.h"

const static struct sd_caps host_caps[] = {
	SD_CAPS(MMC_CAP_4_BIT_DATA, "MMC_CAP_4_BIT_DATA"),
	SD_CAPS(MMC_CAP_MMC_HIGHSPEED, "MMC_CAP_MMC_HIGHSPEED"),
	SD_CAPS(MMC_CAP_SD_HIGHSPEED, "MMC_CAP_SD_HIGHSPEED"),
	SD_CAPS(MMC_CAP_SDIO_IRQ, "MMC_CAP_SDIO_IRQ"),
	SD_CAPS(MMC_CAP_SPI, "MMC_CAP_SPI"),
	SD_CAPS(MMC_CAP_NEEDS_POLL, "MMC_CAP_NEEDS_POLL"),
	SD_CAPS(MMC_CAP_8_BIT_DATA, "MMC_CAP_8_BIT_DATA"),
	SD_CAPS(MMC_CAP_NONREMOVABLE, "MMC_CAP_NONREMOVABLE"),
	SD_CAPS(MMC_CAP_WAIT_WHILE_BUSY, "MMC_CAP_WAIT_WHILE_BUSY"),
	SD_CAPS(MMC_CAP_ERASE, "MMC_CAP_ERASE"),
	SD_CAPS(MMC_CAP_1_8V_DDR, "MMC_CAP_1_8V_DDR"),
	SD_CAPS(MMC_CAP_1_2V_DDR, "MMC_CAP_1_2V_DDR"),
	SD_CAPS(MMC_CAP_POWER_OFF_CARD, "MMC_CAP_POWER_OFF_CARD"),
	SD_CAPS(MMC_CAP_BUS_WIDTH_TEST, "MMC_CAP_BUS_WIDTH_TEST"),
	SD_CAPS(MMC_CAP_UHS_SDR12, "MMC_CAP_UHS_SDR12"),
	SD_CAPS(MMC_CAP_UHS_SDR25, "MMC_CAP_UHS_SDR25"),
	SD_CAPS(MMC_CAP_UHS_SDR50, "MMC_CAP_UHS_SDR50"),
	SD_CAPS(MMC_CAP_UHS_SDR104, "MMC_CAP_UHS_SDR104"),
	SD_CAPS(MMC_CAP_UHS_DDR50, "MMC_CAP_UHS_DDR50"),
	SD_CAPS(MMC_CAP_DRIVER_TYPE_A, "MMC_CAP_DRIVER_TYPE_A"),
	SD_CAPS(MMC_CAP_DRIVER_TYPE_C, "MMC_CAP_DRIVER_TYPE_C"),
	SD_CAPS(MMC_CAP_DRIVER_TYPE_D, "MMC_CAP_DRIVER_TYPE_D"),
	SD_CAPS(MMC_CAP_CMD23, "MMC_CAP_CMD23"),
	SD_CAPS(MMC_CAP_HW_RESET, "MMC_CAP_HW_RESET"),
	SD_CAPS(MMC_PM_KEEP_POWER, "MMC_PM_KEEP_POWER"),
};

#if defined(CONFIG_MACH_MESON8B_ODROIDC)
static int disable_uhs = 0;

static int __init setup_disableuhs(char *line)
{
        disable_uhs = 1;
        return 0;
}
early_param("disableuhs", setup_disableuhs);
#endif

static int amlsd_get_host_caps(struct device_node* of_node,
                struct amlsd_platform* pdata)
{
    const char* str_caps;
    struct property* prop;
    u32 i, caps = 0;

    of_property_for_each_string(of_node, "caps", prop, str_caps){
        for(i = 0; i < ARRAY_SIZE(host_caps);i++){
            if(!strcasecmp(host_caps[i].name,str_caps))
                caps |= host_caps[i].caps;
        }
    };

#if defined(CONFIG_MACH_MESON8B_ODROIDC)
    if (disable_uhs) {
            caps &= ~(MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 |
                            MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_SDR104 |
                            MMC_CAP_UHS_DDR50);
    }
#endif

    pdata->caps = caps;
    printk("pdata->caps %x\n", pdata->caps);
	return 0;
}

const static struct sd_caps host_caps2[] = {
	SD_CAPS(MMC_CAP2_BOOTPART_NOACC, "MMC_CAP2_BOOTPART_NOACC"),
	SD_CAPS(MMC_CAP2_CACHE_CTRL, "MMC_CAP2_CACHE_CTRL"),
	SD_CAPS(MMC_CAP2_POWEROFF_NOTIFY, "MMC_CAP2_POWEROFF_NOTIFY"),
	SD_CAPS(MMC_CAP2_NO_MULTI_READ, "MMC_CAP2_NO_MULTI_READ"),
	SD_CAPS(MMC_CAP2_NO_SLEEP_CMD, "MMC_CAP2_NO_SLEEP_CMD"),
	SD_CAPS(MMC_CAP2_HS200_1_8V_SDR, "MMC_CAP2_HS200_1_8V_SDR"),
	SD_CAPS(MMC_CAP2_HS200_1_2V_SDR, "MMC_CAP2_HS200_1_2V_SDR"),
	SD_CAPS(MMC_CAP2_HS200, "MMC_CAP2_HS200"),
	SD_CAPS(MMC_CAP2_BROKEN_VOLTAGE, "MMC_CAP2_BROKEN_VOLTAGE"),
	SD_CAPS(MMC_CAP2_DETECT_ON_ERR, "MMC_CAP2_DETECT_ON_ERR"),
	SD_CAPS(MMC_CAP2_HC_ERASE_SZ, "MMC_CAP2_HC_ERASE_SZ"),
	SD_CAPS(MMC_CAP2_CD_ACTIVE_HIGH, "MMC_CAP2_CD_ACTIVE_HIGH"),
	SD_CAPS(MMC_CAP2_RO_ACTIVE_HIGH, "MMC_CAP2_RO_ACTIVE_HIGH"),
};

static int amlsd_get_host_caps2(struct device_node* of_node,
                struct amlsd_platform* pdata)
{
    const char* str_caps;
    struct property* prop;
    u32 i, caps = 0;

    of_property_for_each_string(of_node, "caps2", prop, str_caps){
        for(i = 0; i < ARRAY_SIZE(host_caps2);i++){
            if(!strcasecmp(host_caps2[i].name,str_caps))
                caps |= host_caps2[i].caps;
        }
    };
    pdata->caps2 = caps;
    printk("pdata->caps2 %x\n", pdata->caps2);
	return 0;
}

int amlsd_get_reg_base(struct platform_device* pdev,
				struct amlsd_host* host)
{
	struct device_node* of_node = pdev->dev.of_node;

    host->base = of_iomap(of_node, 0);
	if (!host->base) {
		dev_err(&pdev->dev, "of_iomap fail\n");
		return -EINVAL;
	}
    printk("host->base %x\n", (u32)host->base);
	return 0;
}

static void aml_set_gpio_input (const char *pin_name)
{
    int ret = 0, gpio_pin;

    gpio_pin = amlogic_gpio_name_map_num(pin_name);
    ret = amlogic_gpio_request_one(gpio_pin, GPIOF_IN, MODULE_NAME);
    CHECK_RET(ret);
    ret = amlogic_gpio_direction_input(gpio_pin, MODULE_NAME); // output high
    CHECK_RET(ret);
    // print_tmp("\033[0;40;32m %s set input \033[0m\n", pin_name);
    ret = amlogic_gpio_free(gpio_pin, MODULE_NAME);
    CHECK_RET(ret);
}

static int amlsd_init_pins_input (struct device_node* of_node,
                struct amlsd_platform* pdata)
{
    const char* pin_name;
    struct property* prop;

    // print_tmp("\033[0;40;32m Enter \033[0m\n", pin_name);
    of_property_for_each_string(of_node, "all_pins_name", prop, pin_name){
        aml_set_gpio_input(pin_name);
    };
	return 0;
}

int amlsd_get_platform_data(struct platform_device* pdev,
                struct amlsd_platform* pdata,
                struct mmc_host* mmc, u32 index)
{
    struct device_node* of_node = pdev->dev.of_node;
    struct device_node* child;
    u32 i, prop;
	const char *str;

    if(of_node){
		child = of_node->child;
		BUG_ON(!child);
		BUG_ON(index >= MMC_MAX_DEVICE);
		for(i=0;i<index;i++)
			child = child->sibling;
		if(!child)
			return -EINVAL;

        amlsd_get_host_caps(child, pdata);
        amlsd_get_host_caps2(child, pdata);

        amlsd_init_pins_input(child, pdata);

		SD_PARSE_U32_PROP(child, "port", prop, pdata->port);
		SD_PARSE_U32_PROP(child, "ocr_avail", prop, pdata->ocr_avail);
		BUG_ON(!pdata->ocr_avail);
		SD_PARSE_U32_PROP(child, "f_min", prop, pdata->f_min);
		SD_PARSE_U32_PROP(child, "f_max", prop, pdata->f_max);
		SD_PARSE_U32_PROP(child, "f_max_w", prop, pdata->f_max_w);
		SD_PARSE_U32_PROP(child, "max_req_size", prop, pdata->max_req_size);
		SD_PARSE_U32_PROP(child, "irq_in", prop, pdata->irq_in);
		SD_PARSE_U32_PROP(child, "irq_in_edge", prop, pdata->irq_in_edge);
		SD_PARSE_U32_PROP(child, "irq_out", prop, pdata->irq_out);
		SD_PARSE_U32_PROP(child, "irq_out_edge", prop, pdata->irq_out_edge);
		SD_PARSE_U32_PROP(child, "power_level", prop, pdata->power_level);

		SD_PARSE_GPIO_NUM_PROP(child, "gpio_cd", str, pdata->gpio_cd);
		SD_PARSE_U32_PROP(child, "gpio_cd_level", prop, pdata->gpio_cd_level);
		SD_PARSE_GPIO_NUM_PROP(child, "gpio_ro", str, pdata->gpio_ro);
		SD_PARSE_GPIO_NUM_PROP(child, "gpio_power", str, pdata->gpio_power);

		SD_PARSE_STRING_PROP(child, "pinname", str, pdata->pinname);
        SD_PARSE_GPIO_NUM_PROP(child, "jtag_pin", str, pdata->jtag_pin);
		SD_PARSE_U32_PROP(child, "card_type", prop, pdata->card_type);
        SD_PARSE_GPIO_NUM_PROP(child, "gpio_dat3", str, pdata->gpio_dat3);
        SD_PARSE_GPIO_NUM_PROP(child, "gpio_volsw", str, pdata->gpio_volsw);

		pdata->port_init = of_amlsd_init;
		pdata->pwr_pre = of_amlsd_pwr_prepare;
		pdata->pwr_on = of_amlsd_pwr_on;
		pdata->pwr_off = of_amlsd_pwr_off;
		pdata->xfer_pre = of_amlsd_xfer_pre;
		pdata->xfer_post = of_amlsd_xfer_post;
		// pdata->cd = of_amlsd_detect;
		pdata->irq_init = of_amlsd_irq_init;
        pdata->ro = of_amlsd_ro;
    }
    return 0;
}

