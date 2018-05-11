/*
 * TI DaVinci DM646X EVM board
 *
 * Derived from: arch/arm/mach-davinci/board-evm.c
 * Copyright (C) 2006 Texas Instruments.
 *
 * (C) 2007-2008, MontaVista Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 *
 */

/**************************************************************************
 * Included Files
 **************************************************************************/

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/leds.h>
#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/platform_data/at24.h>
#include <linux/platform_data/pcf857x.h>

#include <media/i2c/tvp514x.h>
#include <media/i2c/adv7343.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/rawnand.h>
#include <linux/mtd/partitions.h>
#include <linux/clk.h>
#include <linux/export.h>
#include <linux/platform_data/gpio-davinci.h>
#include <linux/platform_data/i2c-davinci.h>
#include <linux/platform_data/mtd-davinci.h>
#include <linux/platform_data/mtd-davinci-aemif.h>

#include <asm/mach-types.h>
#include <asm/mach/arch.h>

#include <mach/common.h>
#include <mach/irqs.h>
#include <mach/serial.h>
#include <mach/clock.h>

#include "davinci.h"
#include "clock.h"

#define NAND_BLOCK_SIZE		SZ_128K

/* Note: We are setting first partition as 'bootloader' constituting UBL, U-Boot
 * and U-Boot environment this avoids dependency on any particular combination
 * of UBL, U-Boot or flashing tools etc.
 */
static struct mtd_partition davinci_nand_partitions[] = {
	{
		/* UBL, U-Boot with environment */
		.name		= "bootloader",
		.offset		= MTDPART_OFS_APPEND,
		.size		= 16 * NAND_BLOCK_SIZE,
		.mask_flags	= MTD_WRITEABLE,	/* force read-only */
	}, {
		.name		= "kernel",
		.offset		= MTDPART_OFS_APPEND,
		.size		= SZ_4M,
		.mask_flags	= 0,
	}, {
		.name		= "filesystem",
		.offset		= MTDPART_OFS_APPEND,
		.size		= MTDPART_SIZ_FULL,
		.mask_flags	= 0,
	}
};

static struct davinci_aemif_timing dm6467tevm_nandflash_timing = {
	.wsetup		= 29,
	.wstrobe	= 24,
	.whold		= 14,
	.rsetup		= 19,
	.rstrobe	= 33,
	.rhold		= 0,
	.ta		= 29,
};

static struct davinci_nand_pdata davinci_nand_data = {
	.mask_cle 		= 0x80000,
	.mask_ale 		= 0x40000,
	.parts			= davinci_nand_partitions,
	.nr_parts		= ARRAY_SIZE(davinci_nand_partitions),
	.ecc_mode		= NAND_ECC_HW,
	.ecc_bits		= 1,
	.options		= 0,
};

static struct resource davinci_nand_resources[] = {
	{
		.start		= DM646X_ASYNC_EMIF_CS2_SPACE_BASE,
		.end		= DM646X_ASYNC_EMIF_CS2_SPACE_BASE + SZ_32M - 1,
		.flags		= IORESOURCE_MEM,
	}, {
		.start		= DM646X_ASYNC_EMIF_CONTROL_BASE,
		.end		= DM646X_ASYNC_EMIF_CONTROL_BASE + SZ_4K - 1,
		.flags		= IORESOURCE_MEM,
	},
};

static struct platform_device davinci_nand_device = {
	.name			= "davinci_nand",
	.id			= 0,

	.num_resources		= ARRAY_SIZE(davinci_nand_resources),
	.resource		= davinci_nand_resources,

	.dev			= {
		.platform_data	= &davinci_nand_data,
	},
};

#define HAS_ATA		(IS_ENABLED(CONFIG_BLK_DEV_PALMCHIP_BK3710) || \
			 IS_ENABLED(CONFIG_PATA_BK3710))

#ifdef CONFIG_I2C
/* CPLD Register 0 bits to control ATA */
#define DM646X_EVM_ATA_RST		BIT(0)
#define DM646X_EVM_ATA_PWD		BIT(1)

/* CPLD Register 0 Client: used for I/O Control */
static int cpld_reg0_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	if (HAS_ATA) {
		u8 data;
		struct i2c_msg msg[2] = {
			{
				.addr = client->addr,
				.flags = I2C_M_RD,
				.len = 1,
				.buf = &data,
			},
			{
				.addr = client->addr,
				.flags = 0,
				.len = 1,
				.buf = &data,
			},
		};

		/* Clear ATA_RSTn and ATA_PWD bits to enable ATA operation. */
		i2c_transfer(client->adapter, msg, 1);
		data &= ~(DM646X_EVM_ATA_RST | DM646X_EVM_ATA_PWD);
		i2c_transfer(client->adapter, msg + 1, 1);
	}

	return 0;
}

static const struct i2c_device_id cpld_reg_ids[] = {
	{ "cpld_reg0", 0, },
	{ },
};

static struct i2c_driver dm6467evm_cpld_driver = {
	.driver.name	= "cpld_reg0",
	.id_table	= cpld_reg_ids,
	.probe		= cpld_reg0_probe,
};

/* LEDS */

static struct gpio_led evm_leds[] = {
	{ .name = "DS1", .active_low = 1, },
	{ .name = "DS2", .active_low = 1, },
	{ .name = "DS3", .active_low = 1, },
	{ .name = "DS4", .active_low = 1, },
};

static const struct gpio_led_platform_data evm_led_data = {
	.num_leds = ARRAY_SIZE(evm_leds),
	.leds     = evm_leds,
};

static struct platform_device *evm_led_dev;

static int evm_led_setup(struct i2c_client *client, int gpio,
			unsigned int ngpio, void *c)
{
	struct gpio_led *leds = evm_leds;
	int status;

	while (ngpio--) {
		leds->gpio = gpio++;
		leds++;
	}

	evm_led_dev = platform_device_alloc("leds-gpio", 0);
	platform_device_add_data(evm_led_dev, &evm_led_data,
				sizeof(evm_led_data));

	evm_led_dev->dev.parent = &client->dev;
	status = platform_device_add(evm_led_dev);
	if (status < 0) {
		platform_device_put(evm_led_dev);
		evm_led_dev = NULL;
	}
	return status;
}

static int evm_led_teardown(struct i2c_client *client, int gpio,
				unsigned ngpio, void *c)
{
	if (evm_led_dev) {
		platform_device_unregister(evm_led_dev);
		evm_led_dev = NULL;
	}
	return 0;
}

static int evm_sw_gpio[4] = { -EINVAL, -EINVAL, -EINVAL, -EINVAL };

static int evm_sw_setup(struct i2c_client *client, int gpio,
			unsigned ngpio, void *c)
{
	int status;
	int i;
	char label[10];

	for (i = 0; i < 4; ++i) {
		snprintf(label, 10, "user_sw%d", i);
		status = gpio_request(gpio, label);
		if (status)
			goto out_free;
		evm_sw_gpio[i] = gpio++;

		status = gpio_direction_input(evm_sw_gpio[i]);
		if (status) {
			gpio_free(evm_sw_gpio[i]);
			evm_sw_gpio[i] = -EINVAL;
			goto out_free;
		}

		status = gpio_export(evm_sw_gpio[i], 0);
		if (status) {
			gpio_free(evm_sw_gpio[i]);
			evm_sw_gpio[i] = -EINVAL;
			goto out_free;
		}
	}
	return status;
out_free:
	for (i = 0; i < 4; ++i) {
		if (evm_sw_gpio[i] != -EINVAL) {
			gpio_free(evm_sw_gpio[i]);
			evm_sw_gpio[i] = -EINVAL;
		}
	}
	return status;
}

static int evm_sw_teardown(struct i2c_client *client, int gpio,
			unsigned ngpio, void *c)
{
	int i;

	for (i = 0; i < 4; ++i) {
		if (evm_sw_gpio[i] != -EINVAL) {
			gpio_unexport(evm_sw_gpio[i]);
			gpio_free(evm_sw_gpio[i]);
			evm_sw_gpio[i] = -EINVAL;
		}
	}
	return 0;
}

static int evm_pcf_setup(struct i2c_client *client, int gpio,
			unsigned int ngpio, void *c)
{
	int status;

	if (ngpio < 8)
		return -EINVAL;

	status = evm_sw_setup(client, gpio, 4, c);
	if (status)
		return status;

	return evm_led_setup(client, gpio+4, 4, c);
}

static int evm_pcf_teardown(struct i2c_client *client, int gpio,
			unsigned int ngpio, void *c)
{
	BUG_ON(ngpio < 8);

	evm_sw_teardown(client, gpio, 4, c);
	evm_led_teardown(client, gpio+4, 4, c);

	return 0;
}

static struct pcf857x_platform_data pcf_data = {
	.gpio_base	= DAVINCI_N_GPIO+1,
	.setup		= evm_pcf_setup,
	.teardown	= evm_pcf_teardown,
};

/* Most of this EEPROM is unused, but U-Boot uses some data:
 *  - 0x7f00, 6 bytes Ethernet Address
 *  - ... newer boards may have more
 */

static struct at24_platform_data eeprom_info = {
	.byte_len       = (256*1024) / 8,
	.page_size      = 64,
	.flags          = AT24_FLAG_ADDR16,
	.setup          = davinci_get_mac_addr,
	.context	= (void *)0x7f00,
};
#endif

static u8 dm646x_iis_serializer_direction[] = {
       TX_MODE, RX_MODE, INACTIVE_MODE, INACTIVE_MODE,
};

static u8 dm646x_dit_serializer_direction[] = {
       TX_MODE,
};

static struct snd_platform_data dm646x_evm_snd_data[] = {
	{
		.tx_dma_offset  = 0x400,
		.rx_dma_offset  = 0x400,
		.op_mode        = DAVINCI_MCASP_IIS_MODE,
		.num_serializer = ARRAY_SIZE(dm646x_iis_serializer_direction),
		.tdm_slots      = 2,
		.serial_dir     = dm646x_iis_serializer_direction,
		.asp_chan_q     = EVENTQ_0,
	},
	{
		.tx_dma_offset  = 0x400,
		.rx_dma_offset  = 0,
		.op_mode        = DAVINCI_MCASP_DIT_MODE,
		.num_serializer = ARRAY_SIZE(dm646x_dit_serializer_direction),
		.tdm_slots      = 32,
		.serial_dir     = dm646x_dit_serializer_direction,
		.asp_chan_q     = EVENTQ_0,
	},
};

#ifdef CONFIG_I2C
static struct i2c_client *cpld_client;

static int cpld_video_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	cpld_client = client;
	return 0;
}

static int cpld_video_remove(struct i2c_client *client)
{
	cpld_client = NULL;
	return 0;
}

static const struct i2c_device_id cpld_video_id[] = {
	{ "cpld_video", 0 },
	{ }
};

static struct i2c_driver cpld_video_driver = {
	.driver = {
		.name	= "cpld_video",
	},
	.probe		= cpld_video_probe,
	.remove		= cpld_video_remove,
	.id_table	= cpld_video_id,
};

static void evm_init_cpld(void)
{
	i2c_add_driver(&cpld_video_driver);
}

static struct i2c_board_info __initdata i2c_info[] =  {
	{
		I2C_BOARD_INFO("24c256", 0x50),
		.platform_data  = &eeprom_info,
	},
	{
		I2C_BOARD_INFO("pcf8574a", 0x38),
		.platform_data	= &pcf_data,
	},
	{
		I2C_BOARD_INFO("cpld_reg0", 0x3a),
	},
	{
		I2C_BOARD_INFO("tlv320aic33", 0x18),
	},
	{
		I2C_BOARD_INFO("cpld_video", 0x3b),
	},
};

static struct davinci_i2c_platform_data i2c_pdata = {
	.bus_freq       = 100 /* kHz */,
	.bus_delay      = 0 /* usec */,
};

#define VCH2CLK_MASK		(BIT_MASK(10) | BIT_MASK(9) | BIT_MASK(8))
#define VCH2CLK_SYSCLK8		(BIT(9))
#define VCH2CLK_AUXCLK		(BIT(9) | BIT(8))
#define VCH3CLK_MASK		(BIT_MASK(14) | BIT_MASK(13) | BIT_MASK(12))
#define VCH3CLK_SYSCLK8		(BIT(13))
#define VCH3CLK_AUXCLK		(BIT(14) | BIT(13))

#define VIDCH2CLK		(BIT(10))
#define VIDCH3CLK		(BIT(11))
#define VIDCH1CLK		(BIT(4))
#define TVP7002_INPUT		(BIT(4))
#define TVP5147_INPUT		(~BIT(4))
#define VPIF_INPUT_ONE_CHANNEL	(BIT(5))
#define VPIF_INPUT_TWO_CHANNEL	(~BIT(5))
#define TVP5147_CH0		"tvp514x-0"
#define TVP5147_CH1		"tvp514x-1"

/* spin lock for updating above registers */
static spinlock_t vpif_reg_lock;

static int set_vpif_clock(int mux_mode, int hd)
{
	unsigned long flags;
	unsigned int value;
	int val = 0;
	int err = 0;

	if (!cpld_client)
		return -ENXIO;

	/* disable the clock */
	spin_lock_irqsave(&vpif_reg_lock, flags);
	value = __raw_readl(DAVINCI_SYSMOD_VIRT(SYSMOD_VSCLKDIS));
	value |= (VIDCH3CLK | VIDCH2CLK);
	__raw_writel(value, DAVINCI_SYSMOD_VIRT(SYSMOD_VSCLKDIS));
	spin_unlock_irqrestore(&vpif_reg_lock, flags);

	val = i2c_smbus_read_byte(cpld_client);
	if (val < 0)
		return val;

	if (mux_mode == 1)
		val &= ~0x40;
	else
		val |= 0x40;

	err = i2c_smbus_write_byte(cpld_client, val);
	if (err)
		return err;

	value = __raw_readl(DAVINCI_SYSMOD_VIRT(SYSMOD_VIDCLKCTL));
	value &= ~(VCH2CLK_MASK);
	value &= ~(VCH3CLK_MASK);

	if (hd >= 1)
		value |= (VCH2CLK_SYSCLK8 | VCH3CLK_SYSCLK8);
	else
		value |= (VCH2CLK_AUXCLK | VCH3CLK_AUXCLK);

	__raw_writel(value, DAVINCI_SYSMOD_VIRT(SYSMOD_VIDCLKCTL));

	spin_lock_irqsave(&vpif_reg_lock, flags);
	value = __raw_readl(DAVINCI_SYSMOD_VIRT(SYSMOD_VSCLKDIS));
	/* enable the clock */
	value &= ~(VIDCH3CLK | VIDCH2CLK);
	__raw_writel(value, DAVINCI_SYSMOD_VIRT(SYSMOD_VSCLKDIS));
	spin_unlock_irqrestore(&vpif_reg_lock, flags);

	return 0;
}

static struct vpif_subdev_info dm646x_vpif_subdev[] = {
	{
		.name	= "adv7343",
		.board_info = {
			I2C_BOARD_INFO("adv7343", 0x2a),
		},
	},
	{
		.name	= "ths7303",
		.board_info = {
			I2C_BOARD_INFO("ths7303", 0x2c),
		},
	},
};

static const struct vpif_output dm6467_ch0_outputs[] = {
	{
		.output = {
			.index = 0,
			.name = "Composite",
			.type = V4L2_OUTPUT_TYPE_ANALOG,
			.capabilities = V4L2_OUT_CAP_STD,
			.std = V4L2_STD_ALL,
		},
		.subdev_name = "adv7343",
		.output_route = ADV7343_COMPOSITE_ID,
	},
	{
		.output = {
			.index = 1,
			.name = "Component",
			.type = V4L2_OUTPUT_TYPE_ANALOG,
			.capabilities = V4L2_OUT_CAP_DV_TIMINGS,
		},
		.subdev_name = "adv7343",
		.output_route = ADV7343_COMPONENT_ID,
	},
	{
		.output = {
			.index = 2,
			.name = "S-Video",
			.type = V4L2_OUTPUT_TYPE_ANALOG,
			.capabilities = V4L2_OUT_CAP_STD,
			.std = V4L2_STD_ALL,
		},
		.subdev_name = "adv7343",
		.output_route = ADV7343_SVIDEO_ID,
	},
};

static struct vpif_display_config dm646x_vpif_display_config = {
	.set_clock	= set_vpif_clock,
	.subdevinfo	= dm646x_vpif_subdev,
	.subdev_count	= ARRAY_SIZE(dm646x_vpif_subdev),
	.i2c_adapter_id = 1,
	.chan_config[0] = {
		.outputs = dm6467_ch0_outputs,
		.output_count = ARRAY_SIZE(dm6467_ch0_outputs),
	},
	.card_name	= "DM646x EVM Video Display",
};

/**
 * setup_vpif_input_path()
 * @channel: channel id (0 - CH0, 1 - CH1)
 * @sub_dev_name: ptr sub device name
 *
 * This will set vpif input to capture data from tvp514x or
 * tvp7002.
 */
static int setup_vpif_input_path(int channel, const char *sub_dev_name)
{
	int err = 0;
	int val;

	/* for channel 1, we don't do anything */
	if (channel != 0)
		return 0;

	if (!cpld_client)
		return -ENXIO;

	val = i2c_smbus_read_byte(cpld_client);
	if (val < 0)
		return val;

	if (!strcmp(sub_dev_name, TVP5147_CH0) ||
	    !strcmp(sub_dev_name, TVP5147_CH1))
		val &= TVP5147_INPUT;
	else
		val |= TVP7002_INPUT;

	err = i2c_smbus_write_byte(cpld_client, val);
	if (err)
		return err;
	return 0;
}

/**
 * setup_vpif_input_channel_mode()
 * @mux_mode:  mux mode. 0 - 1 channel or (1) - 2 channel
 *
 * This will setup input mode to one channel (TVP7002) or 2 channel (TVP5147)
 */
static int setup_vpif_input_channel_mode(int mux_mode)
{
	unsigned long flags;
	int err = 0;
	int val;
	u32 value;

	if (!cpld_client)
		return -ENXIO;

	val = i2c_smbus_read_byte(cpld_client);
	if (val < 0)
		return val;

	spin_lock_irqsave(&vpif_reg_lock, flags);
	value = __raw_readl(DAVINCI_SYSMOD_VIRT(SYSMOD_VIDCLKCTL));
	if (mux_mode) {
		val &= VPIF_INPUT_TWO_CHANNEL;
		value |= VIDCH1CLK;
	} else {
		val |= VPIF_INPUT_ONE_CHANNEL;
		value &= ~VIDCH1CLK;
	}
	__raw_writel(value, DAVINCI_SYSMOD_VIRT(SYSMOD_VIDCLKCTL));
	spin_unlock_irqrestore(&vpif_reg_lock, flags);

	err = i2c_smbus_write_byte(cpld_client, val);
	if (err)
		return err;

	return 0;
}

static struct tvp514x_platform_data tvp5146_pdata = {
	.clk_polarity = 0,
	.hs_polarity = 1,
	.vs_polarity = 1
};

#define TVP514X_STD_ALL (V4L2_STD_NTSC | V4L2_STD_PAL)

static struct vpif_subdev_info vpif_capture_sdev_info[] = {
	{
		.name	= TVP5147_CH0,
		.board_info = {
			I2C_BOARD_INFO("tvp5146", 0x5d),
			.platform_data = &tvp5146_pdata,
		},
	},
	{
		.name	= TVP5147_CH1,
		.board_info = {
			I2C_BOARD_INFO("tvp5146", 0x5c),
			.platform_data = &tvp5146_pdata,
		},
	},
};

static struct vpif_input dm6467_ch0_inputs[] = {
	{
		.input = {
			.index = 0,
			.name = "Composite",
			.type = V4L2_INPUT_TYPE_CAMERA,
			.capabilities = V4L2_IN_CAP_STD,
			.std = TVP514X_STD_ALL,
		},
		.subdev_name = TVP5147_CH0,
		.input_route = INPUT_CVBS_VI2B,
		.output_route = OUTPUT_10BIT_422_EMBEDDED_SYNC,
	},
};

static struct vpif_input dm6467_ch1_inputs[] = {
       {
		.input = {
			.index = 0,
			.name = "S-Video",
			.type = V4L2_INPUT_TYPE_CAMERA,
			.capabilities = V4L2_IN_CAP_STD,
			.std = TVP514X_STD_ALL,
		},
		.subdev_name = TVP5147_CH1,
		.input_route = INPUT_SVIDEO_VI2C_VI1C,
		.output_route = OUTPUT_10BIT_422_EMBEDDED_SYNC,
	},
};

static struct vpif_capture_config dm646x_vpif_capture_cfg = {
	.setup_input_path = setup_vpif_input_path,
	.setup_input_channel_mode = setup_vpif_input_channel_mode,
	.subdev_info = vpif_capture_sdev_info,
	.subdev_count = ARRAY_SIZE(vpif_capture_sdev_info),
	.i2c_adapter_id = 1,
	.chan_config[0] = {
		.inputs = dm6467_ch0_inputs,
		.input_count = ARRAY_SIZE(dm6467_ch0_inputs),
		.vpif_if = {
			.if_type = VPIF_IF_BT656,
			.hd_pol = 1,
			.vd_pol = 1,
			.fid_pol = 0,
		},
	},
	.chan_config[1] = {
		.inputs = dm6467_ch1_inputs,
		.input_count = ARRAY_SIZE(dm6467_ch1_inputs),
		.vpif_if = {
			.if_type = VPIF_IF_BT656,
			.hd_pol = 1,
			.vd_pol = 1,
			.fid_pol = 0,
		},
	},
	.card_name = "DM646x EVM Video Capture",
};

static void __init evm_init_video(void)
{
	spin_lock_init(&vpif_reg_lock);

	dm646x_setup_vpif(&dm646x_vpif_display_config,
			  &dm646x_vpif_capture_cfg);
}

static void __init evm_init_i2c(void)
{
	davinci_init_i2c(&i2c_pdata);
	i2c_add_driver(&dm6467evm_cpld_driver);
	i2c_register_board_info(1, i2c_info, ARRAY_SIZE(i2c_info));
	evm_init_cpld();
	evm_init_video();
}
#endif

#define DM6467T_EVM_REF_FREQ		33000000

static void __init davinci_map_io(void)
{
	dm646x_init();

	if (machine_is_davinci_dm6467tevm())
		davinci_set_refclk_rate(DM6467T_EVM_REF_FREQ);
}

#define DM646X_EVM_PHY_ID		"davinci_mdio-0:01"
/*
 * The following EDMA channels/slots are not being used by drivers (for
 * example: Timer, GPIO, UART events etc) on dm646x, hence they are being
 * reserved for codecs on the DSP side.
 */
static const s16 dm646x_dma_rsv_chans[][2] = {
	/* (offset, number) */
	{ 0,  4},
	{13,  3},
	{24,  4},
	{30,  2},
	{54,  3},
	{-1, -1}
};

static const s16 dm646x_dma_rsv_slots[][2] = {
	/* (offset, number) */
	{ 0,  4},
	{13,  3},
	{24,  4},
	{30,  2},
	{54,  3},
	{128, 384},
	{-1, -1}
};

static struct edma_rsv_info dm646x_edma_rsv[] = {
	{
		.rsv_chans	= dm646x_dma_rsv_chans,
		.rsv_slots	= dm646x_dma_rsv_slots,
	},
};

static __init void evm_init(void)
{
	int ret;
	struct davinci_soc_info *soc_info = &davinci_soc_info;

	ret = dm646x_gpio_register();
	if (ret)
		pr_warn("%s: GPIO init failed: %d\n", __func__, ret);

#ifdef CONFIG_I2C
	evm_init_i2c();
#endif

	davinci_serial_init(dm646x_serial_device);
	dm646x_init_mcasp0(&dm646x_evm_snd_data[0]);
	dm646x_init_mcasp1(&dm646x_evm_snd_data[1]);

	if (machine_is_davinci_dm6467tevm())
		davinci_nand_data.timing = &dm6467tevm_nandflash_timing;

	platform_device_register(&davinci_nand_device);

	if (davinci_aemif_setup(&davinci_nand_device))
		pr_warn("%s: Cannot configure AEMIF.\n", __func__);

	dm646x_init_edma(dm646x_edma_rsv);

	if (HAS_ATA)
		davinci_init_ide();

	soc_info->emac_pdata->phy_id = DM646X_EVM_PHY_ID;
}

MACHINE_START(DAVINCI_DM6467_EVM, "DaVinci DM646x EVM")
	.atag_offset  = 0x100,
	.map_io       = davinci_map_io,
	.init_irq     = davinci_irq_init,
	.init_time	= davinci_timer_init,
	.init_machine = evm_init,
	.init_late	= davinci_init_late,
	.dma_zone_size	= SZ_128M,
	.restart	= davinci_restart,
MACHINE_END

MACHINE_START(DAVINCI_DM6467TEVM, "DaVinci DM6467T EVM")
	.atag_offset  = 0x100,
	.map_io       = davinci_map_io,
	.init_irq     = davinci_irq_init,
	.init_time	= davinci_timer_init,
	.init_machine = evm_init,
	.init_late	= davinci_init_late,
	.dma_zone_size	= SZ_128M,
	.restart	= davinci_restart,
MACHINE_END

