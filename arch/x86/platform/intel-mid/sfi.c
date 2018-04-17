/*
 * intel_mid_sfi.c: Intel MID SFI initialization code
 *
 * (C) Copyright 2013 Intel Corporation
 * Author: Sathyanarayanan Kuppuswamy <sathyanarayanan.kuppuswamy@intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/scatterlist.h>
#include <linux/sfi.h>
#include <linux/spi/spi.h>
#include <linux/i2c.h>
#include <linux/skbuff.h>
#include <linux/gpio.h>
#include <linux/gpio_keys.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <linux/export.h>
#include <linux/notifier.h>
#include <linux/mmc/core.h>
#include <linux/mmc/card.h>
#include <linux/blkdev.h>

#include <asm/setup.h>
#include <asm/mpspec_def.h>
#include <asm/hw_irq.h>
#include <asm/apic.h>
#include <asm/io_apic.h>
#include <asm/intel-mid.h>
#include <asm/intel_mid_vrtc.h>
#include <asm/io.h>
#include <asm/i8259.h>
#include <asm/intel_scu_ipc.h>
#include <asm/apb_timer.h>
#include <asm/reboot.h>

#define	SFI_SIG_OEM0	"OEM0"
#define MAX_IPCDEVS	24
#define MAX_SCU_SPI	24
#define MAX_SCU_I2C	24

static struct platform_device *ipc_devs[MAX_IPCDEVS];
static struct spi_board_info *spi_devs[MAX_SCU_SPI];
static struct i2c_board_info *i2c_devs[MAX_SCU_I2C];
static struct sfi_gpio_table_entry *gpio_table;
static struct sfi_timer_table_entry sfi_mtimer_array[SFI_MTMR_MAX_NUM];
static int ipc_next_dev;
static int spi_next_dev;
static int i2c_next_dev;
static int i2c_bus[MAX_SCU_I2C];
static int gpio_num_entry;
static u32 sfi_mtimer_usage[SFI_MTMR_MAX_NUM];
int sfi_mrtc_num;
int sfi_mtimer_num;

struct sfi_rtc_table_entry sfi_mrtc_array[SFI_MRTC_MAX];
EXPORT_SYMBOL_GPL(sfi_mrtc_array);

struct blocking_notifier_head intel_scu_notifier =
			BLOCKING_NOTIFIER_INIT(intel_scu_notifier);
EXPORT_SYMBOL_GPL(intel_scu_notifier);

#define intel_mid_sfi_get_pdata(dev, priv)	\
	((dev)->get_platform_data ? (dev)->get_platform_data(priv) : NULL)

/* parse all the mtimer info to a static mtimer array */
int __init sfi_parse_mtmr(struct sfi_table_header *table)
{
	struct sfi_table_simple *sb;
	struct sfi_timer_table_entry *pentry;
	struct mpc_intsrc mp_irq;
	int totallen;

	sb = (struct sfi_table_simple *)table;
	if (!sfi_mtimer_num) {
		sfi_mtimer_num = SFI_GET_NUM_ENTRIES(sb,
					struct sfi_timer_table_entry);
		pentry = (struct sfi_timer_table_entry *) sb->pentry;
		totallen = sfi_mtimer_num * sizeof(*pentry);
		memcpy(sfi_mtimer_array, pentry, totallen);
	}

	pr_debug("SFI MTIMER info (num = %d):\n", sfi_mtimer_num);
	pentry = sfi_mtimer_array;
	for (totallen = 0; totallen < sfi_mtimer_num; totallen++, pentry++) {
		pr_debug("timer[%d]: paddr = 0x%08x, freq = %dHz, irq = %d\n",
			totallen, (u32)pentry->phys_addr,
			pentry->freq_hz, pentry->irq);
		mp_irq.type = MP_INTSRC;
		mp_irq.irqtype = mp_INT;
		mp_irq.irqflag = MP_IRQTRIG_EDGE | MP_IRQPOL_ACTIVE_HIGH;
		mp_irq.srcbus = MP_BUS_ISA;
		mp_irq.srcbusirq = pentry->irq;	/* IRQ */
		mp_irq.dstapic = MP_APIC_ALL;
		mp_irq.dstirq = pentry->irq;
		mp_save_irq(&mp_irq);
		mp_map_gsi_to_irq(pentry->irq, IOAPIC_MAP_ALLOC, NULL);
	}

	return 0;
}

struct sfi_timer_table_entry *sfi_get_mtmr(int hint)
{
	int i;
	if (hint < sfi_mtimer_num) {
		if (!sfi_mtimer_usage[hint]) {
			pr_debug("hint taken for timer %d irq %d\n",
				hint, sfi_mtimer_array[hint].irq);
			sfi_mtimer_usage[hint] = 1;
			return &sfi_mtimer_array[hint];
		}
	}
	/* take the first timer available */
	for (i = 0; i < sfi_mtimer_num;) {
		if (!sfi_mtimer_usage[i]) {
			sfi_mtimer_usage[i] = 1;
			return &sfi_mtimer_array[i];
		}
		i++;
	}
	return NULL;
}

void sfi_free_mtmr(struct sfi_timer_table_entry *mtmr)
{
	int i;
	for (i = 0; i < sfi_mtimer_num;) {
		if (mtmr->irq == sfi_mtimer_array[i].irq) {
			sfi_mtimer_usage[i] = 0;
			return;
		}
		i++;
	}
}

/* parse all the mrtc info to a global mrtc array */
int __init sfi_parse_mrtc(struct sfi_table_header *table)
{
	struct sfi_table_simple *sb;
	struct sfi_rtc_table_entry *pentry;
	struct mpc_intsrc mp_irq;

	int totallen;

	sb = (struct sfi_table_simple *)table;
	if (!sfi_mrtc_num) {
		sfi_mrtc_num = SFI_GET_NUM_ENTRIES(sb,
						struct sfi_rtc_table_entry);
		pentry = (struct sfi_rtc_table_entry *)sb->pentry;
		totallen = sfi_mrtc_num * sizeof(*pentry);
		memcpy(sfi_mrtc_array, pentry, totallen);
	}

	pr_debug("SFI RTC info (num = %d):\n", sfi_mrtc_num);
	pentry = sfi_mrtc_array;
	for (totallen = 0; totallen < sfi_mrtc_num; totallen++, pentry++) {
		pr_debug("RTC[%d]: paddr = 0x%08x, irq = %d\n",
			totallen, (u32)pentry->phys_addr, pentry->irq);
		mp_irq.type = MP_INTSRC;
		mp_irq.irqtype = mp_INT;
		mp_irq.irqflag = MP_IRQTRIG_LEVEL | MP_IRQPOL_ACTIVE_LOW;
		mp_irq.srcbus = MP_BUS_ISA;
		mp_irq.srcbusirq = pentry->irq;	/* IRQ */
		mp_irq.dstapic = MP_APIC_ALL;
		mp_irq.dstirq = pentry->irq;
		mp_save_irq(&mp_irq);
		mp_map_gsi_to_irq(pentry->irq, IOAPIC_MAP_ALLOC, NULL);
	}
	return 0;
}


/*
 * Parsing GPIO table first, since the DEVS table will need this table
 * to map the pin name to the actual pin.
 */
static int __init sfi_parse_gpio(struct sfi_table_header *table)
{
	struct sfi_table_simple *sb;
	struct sfi_gpio_table_entry *pentry;
	int num, i;

	if (gpio_table)
		return 0;
	sb = (struct sfi_table_simple *)table;
	num = SFI_GET_NUM_ENTRIES(sb, struct sfi_gpio_table_entry);
	pentry = (struct sfi_gpio_table_entry *)sb->pentry;

	gpio_table = kmemdup(pentry, num * sizeof(*pentry), GFP_KERNEL);
	if (!gpio_table)
		return -1;
	gpio_num_entry = num;

	pr_debug("GPIO pin info:\n");
	for (i = 0; i < num; i++, pentry++)
		pr_debug("info[%2d]: controller = %16.16s, pin_name = %16.16s,"
		" pin = %d\n", i,
			pentry->controller_name,
			pentry->pin_name,
			pentry->pin_no);
	return 0;
}

int get_gpio_by_name(const char *name)
{
	struct sfi_gpio_table_entry *pentry = gpio_table;
	int i;

	if (!pentry)
		return -1;
	for (i = 0; i < gpio_num_entry; i++, pentry++) {
		if (!strncmp(name, pentry->pin_name, SFI_NAME_LEN))
			return pentry->pin_no;
	}
	return -EINVAL;
}

static void __init intel_scu_ipc_device_register(struct platform_device *pdev)
{
	if (ipc_next_dev == MAX_IPCDEVS)
		pr_err("too many SCU IPC devices");
	else
		ipc_devs[ipc_next_dev++] = pdev;
}

static void __init intel_scu_spi_device_register(struct spi_board_info *sdev)
{
	struct spi_board_info *new_dev;

	if (spi_next_dev == MAX_SCU_SPI) {
		pr_err("too many SCU SPI devices");
		return;
	}

	new_dev = kzalloc(sizeof(*sdev), GFP_KERNEL);
	if (!new_dev) {
		pr_err("failed to alloc mem for delayed spi dev %s\n",
			sdev->modalias);
		return;
	}
	*new_dev = *sdev;

	spi_devs[spi_next_dev++] = new_dev;
}

static void __init intel_scu_i2c_device_register(int bus,
						struct i2c_board_info *idev)
{
	struct i2c_board_info *new_dev;

	if (i2c_next_dev == MAX_SCU_I2C) {
		pr_err("too many SCU I2C devices");
		return;
	}

	new_dev = kzalloc(sizeof(*idev), GFP_KERNEL);
	if (!new_dev) {
		pr_err("failed to alloc mem for delayed i2c dev %s\n",
			idev->type);
		return;
	}
	*new_dev = *idev;

	i2c_bus[i2c_next_dev] = bus;
	i2c_devs[i2c_next_dev++] = new_dev;
}

/* Called by IPC driver */
void intel_scu_devices_create(void)
{
	int i;

	for (i = 0; i < ipc_next_dev; i++)
		platform_device_add(ipc_devs[i]);

	for (i = 0; i < spi_next_dev; i++)
		spi_register_board_info(spi_devs[i], 1);

	for (i = 0; i < i2c_next_dev; i++) {
		struct i2c_adapter *adapter;
		struct i2c_client *client;

		adapter = i2c_get_adapter(i2c_bus[i]);
		if (adapter) {
			client = i2c_new_device(adapter, i2c_devs[i]);
			if (!client)
				pr_err("can't create i2c device %s\n",
					i2c_devs[i]->type);
		} else
			i2c_register_board_info(i2c_bus[i], i2c_devs[i], 1);
	}
	intel_scu_notifier_post(SCU_AVAILABLE, NULL);
}
EXPORT_SYMBOL_GPL(intel_scu_devices_create);

/* Called by IPC driver */
void intel_scu_devices_destroy(void)
{
	int i;

	intel_scu_notifier_post(SCU_DOWN, NULL);

	for (i = 0; i < ipc_next_dev; i++)
		platform_device_del(ipc_devs[i]);
}
EXPORT_SYMBOL_GPL(intel_scu_devices_destroy);

static void __init install_irq_resource(struct platform_device *pdev, int irq)
{
	/* Single threaded */
	static struct resource res __initdata = {
		.name = "IRQ",
		.flags = IORESOURCE_IRQ,
	};
	res.start = irq;
	platform_device_add_resources(pdev, &res, 1);
}

static void __init sfi_handle_ipc_dev(struct sfi_device_table_entry *pentry,
					struct devs_id *dev)
{
	struct platform_device *pdev;
	void *pdata = NULL;

	pr_debug("IPC bus, name = %16.16s, irq = 0x%2x\n",
		pentry->name, pentry->irq);

	/*
	 * We need to call platform init of IPC devices to fill misc_pdata
	 * structure. It will be used in msic_init for initialization.
	 */
	pdata = intel_mid_sfi_get_pdata(dev, pentry);
	if (IS_ERR(pdata))
		return;

	/*
	 * On Medfield the platform device creation is handled by the MSIC
	 * MFD driver so we don't need to do it here.
	 */
	if (dev->msic && intel_mid_has_msic())
		return;

	pdev = platform_device_alloc(pentry->name, 0);
	if (pdev == NULL) {
		pr_err("out of memory for SFI platform device '%s'.\n",
			pentry->name);
		return;
	}
	install_irq_resource(pdev, pentry->irq);

	pdev->dev.platform_data = pdata;
	if (dev->delay)
		intel_scu_ipc_device_register(pdev);
	else
		platform_device_add(pdev);
}

static void __init sfi_handle_spi_dev(struct sfi_device_table_entry *pentry,
					struct devs_id *dev)
{
	struct spi_board_info spi_info;
	void *pdata = NULL;

	memset(&spi_info, 0, sizeof(spi_info));
	strncpy(spi_info.modalias, pentry->name, SFI_NAME_LEN);
	spi_info.irq = ((pentry->irq == (u8)0xff) ? 0 : pentry->irq);
	spi_info.bus_num = pentry->host_num;
	spi_info.chip_select = pentry->addr;
	spi_info.max_speed_hz = pentry->max_freq;
	pr_debug("SPI bus=%d, name=%16.16s, irq=0x%2x, max_freq=%d, cs=%d\n",
		spi_info.bus_num,
		spi_info.modalias,
		spi_info.irq,
		spi_info.max_speed_hz,
		spi_info.chip_select);

	pdata = intel_mid_sfi_get_pdata(dev, &spi_info);
	if (IS_ERR(pdata))
		return;

	spi_info.platform_data = pdata;
	if (dev->delay)
		intel_scu_spi_device_register(&spi_info);
	else
		spi_register_board_info(&spi_info, 1);
}

static void __init sfi_handle_i2c_dev(struct sfi_device_table_entry *pentry,
					struct devs_id *dev)
{
	struct i2c_board_info i2c_info;
	void *pdata = NULL;

	memset(&i2c_info, 0, sizeof(i2c_info));
	strncpy(i2c_info.type, pentry->name, SFI_NAME_LEN);
	i2c_info.irq = ((pentry->irq == (u8)0xff) ? 0 : pentry->irq);
	i2c_info.addr = pentry->addr;
	pr_debug("I2C bus = %d, name = %16.16s, irq = 0x%2x, addr = 0x%x\n",
		pentry->host_num,
		i2c_info.type,
		i2c_info.irq,
		i2c_info.addr);
	pdata = intel_mid_sfi_get_pdata(dev, &i2c_info);
	i2c_info.platform_data = pdata;
	if (IS_ERR(pdata))
		return;

	if (dev->delay)
		intel_scu_i2c_device_register(pentry->host_num, &i2c_info);
	else
		i2c_register_board_info(pentry->host_num, &i2c_info, 1);
}

static void __init sfi_handle_sd_dev(struct sfi_device_table_entry *pentry,
					struct devs_id *dev)
{
	struct mid_sd_board_info sd_info;
	void *pdata;

	memset(&sd_info, 0, sizeof(sd_info));
	strncpy(sd_info.name, pentry->name, SFI_NAME_LEN);
	sd_info.bus_num = pentry->host_num;
	sd_info.max_clk = pentry->max_freq;
	sd_info.addr = pentry->addr;
	pr_debug("SD bus = %d, name = %16.16s, max_clk = %d, addr = 0x%x\n",
		 sd_info.bus_num,
		 sd_info.name,
		 sd_info.max_clk,
		 sd_info.addr);
	pdata = intel_mid_sfi_get_pdata(dev, &sd_info);
	if (IS_ERR(pdata))
		return;

	/* Nothing we can do with this for now */
	sd_info.platform_data = pdata;

	pr_debug("Successfully registered %16.16s", sd_info.name);
}

extern struct devs_id *const __x86_intel_mid_dev_start[],
		      *const __x86_intel_mid_dev_end[];

static struct devs_id __init *get_device_id(u8 type, char *name)
{
	struct devs_id *const *dev_table;

	for (dev_table = __x86_intel_mid_dev_start;
			dev_table < __x86_intel_mid_dev_end; dev_table++) {
		struct devs_id *dev = *dev_table;
		if (dev->type == type &&
			!strncmp(dev->name, name, SFI_NAME_LEN)) {
			return dev;
		}
	}

	return NULL;
}

static int __init sfi_parse_devs(struct sfi_table_header *table)
{
	struct sfi_table_simple *sb;
	struct sfi_device_table_entry *pentry;
	struct devs_id *dev = NULL;
	int num, i, ret;
	int polarity;
	struct irq_alloc_info info;

	sb = (struct sfi_table_simple *)table;
	num = SFI_GET_NUM_ENTRIES(sb, struct sfi_device_table_entry);
	pentry = (struct sfi_device_table_entry *)sb->pentry;

	for (i = 0; i < num; i++, pentry++) {
		int irq = pentry->irq;

		if (irq != (u8)0xff) { /* native RTE case */
			/* these SPI2 devices are not exposed to system as PCI
			 * devices, but they have separate RTE entry in IOAPIC
			 * so we have to enable them one by one here
			 */
			if (intel_mid_identify_cpu() ==
					INTEL_MID_CPU_CHIP_TANGIER) {
				if (!strncmp(pentry->name, "r69001-ts-i2c", 13))
					/* active low */
					polarity = 1;
				else if (!strncmp(pentry->name,
						"synaptics_3202", 14))
					/* active low */
					polarity = 1;
				else if (irq == 41)
					/* fast_int_1 */
					polarity = 1;
				else
					/* active high */
					polarity = 0;
			} else {
				/* PNW and CLV go with active low */
				polarity = 1;
			}

			ioapic_set_alloc_attr(&info, NUMA_NO_NODE, 1, polarity);
			ret = mp_map_gsi_to_irq(irq, IOAPIC_MAP_ALLOC, &info);
			WARN_ON(ret < 0);
		}

		dev = get_device_id(pentry->type, pentry->name);

		if (!dev)
			continue;

		switch (pentry->type) {
		case SFI_DEV_TYPE_IPC:
			sfi_handle_ipc_dev(pentry, dev);
			break;
		case SFI_DEV_TYPE_SPI:
			sfi_handle_spi_dev(pentry, dev);
			break;
		case SFI_DEV_TYPE_I2C:
			sfi_handle_i2c_dev(pentry, dev);
			break;
		case SFI_DEV_TYPE_SD:
			sfi_handle_sd_dev(pentry, dev);
			break;
		case SFI_DEV_TYPE_UART:
		case SFI_DEV_TYPE_HSI:
		default:
			break;
		}
	}
	return 0;
}

static int __init intel_mid_platform_init(void)
{
	sfi_table_parse(SFI_SIG_GPIO, NULL, NULL, sfi_parse_gpio);
	sfi_table_parse(SFI_SIG_DEVS, NULL, NULL, sfi_parse_devs);
	return 0;
}
arch_initcall(intel_mid_platform_init);
