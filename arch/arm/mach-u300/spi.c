/*
 * arch/arm/mach-u300/spi.c
 *
 * Copyright (C) 2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 *
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 */
#include <linux/device.h>
#include <linux/amba/bus.h>
#include <linux/spi/spi.h>
#include <linux/amba/pl022.h>
#include <linux/err.h>
#include "padmux.h"

/*
 * The following is for the actual devices on the SSP/SPI bus
 */
#ifdef CONFIG_MACH_U300_SPIDUMMY
static void select_dummy_chip(u32 chipselect)
{
	pr_debug("CORE: %s called with CS=0x%x (%s)\n",
		 __func__,
		 chipselect,
		 chipselect ? "unselect chip" : "select chip");
	/*
	 * Here you would write the chip select value to the GPIO pins if
	 * this was a real chip (but this is a loopback dummy).
	 */
}

struct pl022_config_chip dummy_chip_info = {
	/*
	 * available POLLING_TRANSFER and INTERRUPT_TRANSFER,
	 * DMA_TRANSFER does not work
	 */
	.com_mode = INTERRUPT_TRANSFER,
	.iface = SSP_INTERFACE_MOTOROLA_SPI,
	/* We can only act as master but SSP_SLAVE is possible in theory */
	.hierarchy = SSP_MASTER,
	/* 0 = drive TX even as slave, 1 = do not drive TX as slave */
	.slave_tx_disable = 0,
	.rx_lev_trig = SSP_RX_1_OR_MORE_ELEM,
	.tx_lev_trig = SSP_TX_1_OR_MORE_EMPTY_LOC,
	.ctrl_len = SSP_BITS_12,
	.wait_state = SSP_MWIRE_WAIT_ZERO,
	.duplex = SSP_MICROWIRE_CHANNEL_FULL_DUPLEX,
	/*
	 * This is where you insert a call to a function to enable CS
	 * (usually GPIO) for a certain chip.
	 */
	.cs_control = select_dummy_chip,
};
#endif

static struct spi_board_info u300_spi_devices[] = {
#ifdef CONFIG_MACH_U300_SPIDUMMY
	{
		/* A dummy chip used for loopback tests */
		.modalias       = "spi-dummy",
		/* Really dummy, pass in additional chip config here */
		.platform_data  = NULL,
		/* This defines how the controller shall handle the device */
		.controller_data = &dummy_chip_info,
		/* .irq - no external IRQ routed from this device */
		.max_speed_hz   = 1000000,
		.bus_num        = 0, /* Only one bus on this chip */
		.chip_select    = 0,
		/* Means SPI_CS_HIGH, change if e.g low CS */
		.mode           = SPI_MODE_1 | SPI_LOOP,
	},
#endif
};

static struct pl022_ssp_controller ssp_platform_data = {
	/* If you have several SPI buses this varies, we have only bus 0 */
	.bus_id = 0,
	/* Set this to 1 when we think we got DMA working */
	.enable_dma = 0,
	/*
	 * On the APP CPU GPIO 4, 5 and 6 are connected as generic
	 * chip selects for SPI. (Same on U330, U335 and U365.)
	 * TODO: make sure the GPIO driver can select these properly
	 * and do padmuxing accordingly too.
	 */
	.num_chipselect = 3,
};


void __init u300_spi_init(struct amba_device *adev)
{
	struct pmx *pmx;

	adev->dev.platform_data = &ssp_platform_data;
	/*
	 * Setup padmuxing for SPI. Since this must always be
	 * compiled into the kernel, pmx is never released.
	 */
	pmx = pmx_get(&adev->dev, U300_APP_PMX_SPI_SETTING);

	if (IS_ERR(pmx))
		dev_warn(&adev->dev, "Could not get padmux handle\n");
	else {
		int ret;

		ret = pmx_activate(&adev->dev, pmx);
		if (IS_ERR_VALUE(ret))
			dev_warn(&adev->dev, "Could not activate padmuxing\n");
	}

}
void __init u300_spi_register_board_devices(void)
{
	/* Register any SPI devices */
	spi_register_board_info(u300_spi_devices, ARRAY_SIZE(u300_spi_devices));
}
