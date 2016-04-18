/*
 * arch/arm/mach-omap2/serial.c
 *
 * OMAP2 serial support.
 *
 * Copyright (C) 2005-2008 Nokia Corporation
 * Author: Paul Mundt <paul.mundt@nokia.com>
 *
 * Major rework for PM support by Kevin Hilman
 *
 * Based off of arch/arm/mach-omap/omap1/serial.c
 *
 * Copyright (C) 2009 Texas Instruments
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/console.h>
#include <linux/omap-dma.h>
#include <linux/platform_data/serial-omap.h>

#include "common.h"
#include "omap_hwmod.h"
#include "omap_device.h"
#include "omap-pm.h"
#include "soc.h"
#include "prm2xxx_3xxx.h"
#include "pm.h"
#include "cm2xxx_3xxx.h"
#include "prm-regbits-34xx.h"
#include "control.h"
#include "mux.h"
#include "serial.h"

/*
 * NOTE: By default the serial auto_suspend timeout is disabled as it causes
 * lost characters over the serial ports. This means that the UART clocks will
 * stay on until power/autosuspend_delay is set for the uart from sysfs.
 * This also causes that any deeper omap sleep states are blocked.
 */
#define DEFAULT_AUTOSUSPEND_DELAY	-1

#define MAX_UART_HWMOD_NAME_LEN		16

struct omap_uart_state {
	int num;

	struct list_head node;
	struct omap_hwmod *oh;
	struct omap_device_pad default_omap_uart_pads[2];
};

static LIST_HEAD(uart_list);
static u8 num_uarts;
static u8 console_uart_id = -1;
static u8 uart_debug;

#define DEFAULT_RXDMA_POLLRATE		1	/* RX DMA polling rate (us) */
#define DEFAULT_RXDMA_BUFSIZE		4096	/* RX DMA buffer size */
#define DEFAULT_RXDMA_TIMEOUT		(3 * HZ)/* RX DMA timeout (jiffies) */

static struct omap_uart_port_info omap_serial_default_info[] __initdata = {
	{
		.dma_enabled	= false,
		.dma_rx_buf_size = DEFAULT_RXDMA_BUFSIZE,
		.dma_rx_poll_rate = DEFAULT_RXDMA_POLLRATE,
		.dma_rx_timeout = DEFAULT_RXDMA_TIMEOUT,
		.autosuspend_timeout = DEFAULT_AUTOSUSPEND_DELAY,
	},
};

#ifdef CONFIG_PM
static void omap_uart_enable_wakeup(struct device *dev, bool enable)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct omap_device *od = to_omap_device(pdev);

	if (!od)
		return;

	if (enable)
		omap_hwmod_enable_wakeup(od->hwmods[0]);
	else
		omap_hwmod_disable_wakeup(od->hwmods[0]);
}

#else
static void omap_uart_enable_wakeup(struct device *dev, bool enable)
{}
#endif /* CONFIG_PM */

#ifdef CONFIG_OMAP_MUX

#define OMAP_UART_DEFAULT_PAD_NAME_LEN	28
static char rx_pad_name[OMAP_UART_DEFAULT_PAD_NAME_LEN],
		tx_pad_name[OMAP_UART_DEFAULT_PAD_NAME_LEN] __initdata;

static void  __init
omap_serial_fill_uart_tx_rx_pads(struct omap_board_data *bdata,
				struct omap_uart_state *uart)
{
	uart->default_omap_uart_pads[0].name = rx_pad_name;
	uart->default_omap_uart_pads[0].flags = OMAP_DEVICE_PAD_REMUX |
							OMAP_DEVICE_PAD_WAKEUP;
	uart->default_omap_uart_pads[0].enable = OMAP_PIN_INPUT |
							OMAP_MUX_MODE0;
	uart->default_omap_uart_pads[0].idle = OMAP_PIN_INPUT | OMAP_MUX_MODE0;
	uart->default_omap_uart_pads[1].name = tx_pad_name;
	uart->default_omap_uart_pads[1].enable = OMAP_PIN_OUTPUT |
							OMAP_MUX_MODE0;
	bdata->pads = uart->default_omap_uart_pads;
	bdata->pads_cnt = ARRAY_SIZE(uart->default_omap_uart_pads);
}

static void  __init omap_serial_check_wakeup(struct omap_board_data *bdata,
						struct omap_uart_state *uart)
{
	struct omap_mux_partition *tx_partition = NULL, *rx_partition = NULL;
	struct omap_mux *rx_mux = NULL, *tx_mux = NULL;
	char *rx_fmt, *tx_fmt;
	int uart_nr = bdata->id + 1;

	if (bdata->id != 2) {
		rx_fmt = "uart%d_rx.uart%d_rx";
		tx_fmt = "uart%d_tx.uart%d_tx";
	} else {
		rx_fmt = "uart%d_rx_irrx.uart%d_rx_irrx";
		tx_fmt = "uart%d_tx_irtx.uart%d_tx_irtx";
	}

	snprintf(rx_pad_name, OMAP_UART_DEFAULT_PAD_NAME_LEN, rx_fmt,
			uart_nr, uart_nr);
	snprintf(tx_pad_name, OMAP_UART_DEFAULT_PAD_NAME_LEN, tx_fmt,
			uart_nr, uart_nr);

	if (omap_mux_get_by_name(rx_pad_name, &rx_partition, &rx_mux) >= 0 &&
			omap_mux_get_by_name
				(tx_pad_name, &tx_partition, &tx_mux) >= 0) {
		u16 tx_mode, rx_mode;

		tx_mode = omap_mux_read(tx_partition, tx_mux->reg_offset);
		rx_mode = omap_mux_read(rx_partition, rx_mux->reg_offset);

		/*
		 * Check if uart is used in default tx/rx mode i.e. in mux mode0
		 * if yes then configure rx pin for wake up capability
		 */
		if (OMAP_MODE_UART(rx_mode) && OMAP_MODE_UART(tx_mode))
			omap_serial_fill_uart_tx_rx_pads(bdata, uart);
	}
}
#else
static void __init omap_serial_check_wakeup(struct omap_board_data *bdata,
		struct omap_uart_state *uart)
{
}
#endif

static char *cmdline_find_option(char *str)
{
	extern char *saved_command_line;

	return strstr(saved_command_line, str);
}

static int __init omap_serial_early_init(void)
{
	if (of_have_populated_dt())
		return -ENODEV;

	do {
		char oh_name[MAX_UART_HWMOD_NAME_LEN];
		struct omap_hwmod *oh;
		struct omap_uart_state *uart;
		char uart_name[MAX_UART_HWMOD_NAME_LEN];

		snprintf(oh_name, MAX_UART_HWMOD_NAME_LEN,
			 "uart%d", num_uarts + 1);
		oh = omap_hwmod_lookup(oh_name);
		if (!oh)
			break;

		uart = kzalloc(sizeof(struct omap_uart_state), GFP_KERNEL);
		if (WARN_ON(!uart))
			return -ENODEV;

		uart->oh = oh;
		uart->num = num_uarts++;
		list_add_tail(&uart->node, &uart_list);
		snprintf(uart_name, MAX_UART_HWMOD_NAME_LEN,
				"%s%d", OMAP_SERIAL_NAME, uart->num);

		if (cmdline_find_option(uart_name)) {
			console_uart_id = uart->num;

			if (console_loglevel >= CONSOLE_LOGLEVEL_DEBUG) {
				uart_debug = true;
				pr_info("%s used as console in debug mode: uart%d clocks will not be gated",
					uart_name, uart->num);
			}
		}
	} while (1);

	return 0;
}
omap_postcore_initcall(omap_serial_early_init);

/**
 * omap_serial_init_port() - initialize single serial port
 * @bdata: port specific board data pointer
 * @info: platform specific data pointer
 *
 * This function initialies serial driver for given port only.
 * Platforms can call this function instead of omap_serial_init()
 * if they don't plan to use all available UARTs as serial ports.
 *
 * Don't mix calls to omap_serial_init_port() and omap_serial_init(),
 * use only one of the two.
 */
void __init omap_serial_init_port(struct omap_board_data *bdata,
			struct omap_uart_port_info *info)
{
	struct omap_uart_state *uart;
	struct omap_hwmod *oh;
	struct platform_device *pdev;
	void *pdata = NULL;
	u32 pdata_size = 0;
	char *name;
	struct omap_uart_port_info omap_up;

	if (WARN_ON(!bdata))
		return;
	if (WARN_ON(bdata->id < 0))
		return;
	if (WARN_ON(bdata->id >= num_uarts))
		return;

	list_for_each_entry(uart, &uart_list, node)
		if (bdata->id == uart->num)
			break;
	if (!info)
		info = omap_serial_default_info;

	oh = uart->oh;
	name = OMAP_SERIAL_DRIVER_NAME;

	omap_up.dma_enabled = info->dma_enabled;
	omap_up.uartclk = OMAP24XX_BASE_BAUD * 16;
	omap_up.flags = UPF_BOOT_AUTOCONF;
	omap_up.get_context_loss_count = omap_pm_get_dev_context_loss_count;
	omap_up.enable_wakeup = omap_uart_enable_wakeup;
	omap_up.dma_rx_buf_size = info->dma_rx_buf_size;
	omap_up.dma_rx_timeout = info->dma_rx_timeout;
	omap_up.dma_rx_poll_rate = info->dma_rx_poll_rate;
	omap_up.autosuspend_timeout = info->autosuspend_timeout;

	pdata = &omap_up;
	pdata_size = sizeof(struct omap_uart_port_info);

	if (WARN_ON(!oh))
		return;

	pdev = omap_device_build(name, uart->num, oh, pdata, pdata_size);
	if (IS_ERR(pdev)) {
		WARN(1, "Could not build omap_device for %s: %s.\n", name,
		     oh->name);
		return;
	}

	oh->mux = omap_hwmod_mux_init(bdata->pads, bdata->pads_cnt);

	if (console_uart_id == bdata->id) {
		omap_device_enable(pdev);
		pm_runtime_set_active(&pdev->dev);
	}

	oh->dev_attr = uart;

	if (((cpu_is_omap34xx() || cpu_is_omap44xx()) && bdata->pads)
			&& !uart_debug)
		device_init_wakeup(&pdev->dev, true);
}

/**
 * omap_serial_board_init() - initialize all supported serial ports
 * @info: platform specific data pointer
 *
 * Initializes all available UARTs as serial ports. Platforms
 * can call this function when they want to have default behaviour
 * for serial ports (e.g initialize them all as serial ports).
 */
void __init omap_serial_board_init(struct omap_uart_port_info *info)
{
	struct omap_uart_state *uart;
	struct omap_board_data bdata;

	list_for_each_entry(uart, &uart_list, node) {
		bdata.id = uart->num;
		bdata.flags = 0;
		bdata.pads = NULL;
		bdata.pads_cnt = 0;

		omap_serial_check_wakeup(&bdata, uart);

		if (!info)
			omap_serial_init_port(&bdata, NULL);
		else
			omap_serial_init_port(&bdata, &info[uart->num]);
	}
}

/**
 * omap_serial_init() - initialize all supported serial ports
 *
 * Initializes all available UARTs.
 * Platforms can call this function when they want to have default behaviour
 * for serial ports (e.g initialize them all as serial ports).
 */
void __init omap_serial_init(void)
{
	omap_serial_board_init(NULL);
}
