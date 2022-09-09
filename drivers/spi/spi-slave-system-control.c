/*
 * SPI slave handler controlling system state
 *
 * This SPI slave handler allows remote control of system reboot, power off,
 * halt, and suspend.
 *
 * Copyright (C) 2016-2017 Glider bvba
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Usage (assuming /dev/spidev2.0 corresponds to the SPI master on the remote
 * system):
 *
 *   # reboot='\x7c\x50'
 *   # poweroff='\x71\x3f'
 *   # halt='\x38\x76'
 *   # suspend='\x1b\x1b'
 *   # spidev_test -D /dev/spidev2.0 -p $suspend # or $reboot, $poweroff, $halt
 */

#include <linux/completion.h>
#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/suspend.h>
#include <linux/spi/spi.h>

/*
 * The numbers are chosen to display something human-readable on two 7-segment
 * displays connected to two 74HC595 shift registers
 */
#define CMD_REBOOT	0x7c50	/* rb */
#define CMD_POWEROFF	0x713f	/* OF */
#define CMD_HALT	0x3876	/* HL */
#define CMD_SUSPEND	0x1b1b	/* ZZ */

struct spi_slave_system_control_priv {
	struct spi_device *spi;
	struct completion finished;
	struct spi_transfer xfer;
	struct spi_message msg;
	__be16 cmd;
};

static
int spi_slave_system_control_submit(struct spi_slave_system_control_priv *priv);

static void spi_slave_system_control_complete(void *arg)
{
	struct spi_slave_system_control_priv *priv = arg;
	u16 cmd;
	int ret;

	if (priv->msg.status)
		goto terminate;

	cmd = be16_to_cpu(priv->cmd);
	switch (cmd) {
	case CMD_REBOOT:
		dev_info(&priv->spi->dev, "Rebooting system...\n");
		kernel_restart(NULL);
		break;

	case CMD_POWEROFF:
		dev_info(&priv->spi->dev, "Powering off system...\n");
		kernel_power_off();
		break;

	case CMD_HALT:
		dev_info(&priv->spi->dev, "Halting system...\n");
		kernel_halt();
		break;

	case CMD_SUSPEND:
		dev_info(&priv->spi->dev, "Suspending system...\n");
		pm_suspend(PM_SUSPEND_MEM);
		break;

	default:
		dev_warn(&priv->spi->dev, "Unknown command 0x%x\n", cmd);
		break;
	}

	ret = spi_slave_system_control_submit(priv);
	if (ret)
		goto terminate;

	return;

terminate:
	dev_info(&priv->spi->dev, "Terminating\n");
	complete(&priv->finished);
}

static
int spi_slave_system_control_submit(struct spi_slave_system_control_priv *priv)
{
	int ret;

	spi_message_init_with_transfers(&priv->msg, &priv->xfer, 1);

	priv->msg.complete = spi_slave_system_control_complete;
	priv->msg.context = priv;

	ret = spi_async(priv->spi, &priv->msg);
	if (ret)
		dev_err(&priv->spi->dev, "spi_async() failed %d\n", ret);

	return ret;
}

static int spi_slave_system_control_probe(struct spi_device *spi)
{
	struct spi_slave_system_control_priv *priv;
	int ret;

	priv = devm_kzalloc(&spi->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->spi = spi;
	init_completion(&priv->finished);
	priv->xfer.rx_buf = &priv->cmd;
	priv->xfer.len = sizeof(priv->cmd);

	ret = spi_slave_system_control_submit(priv);
	if (ret)
		return ret;

	spi_set_drvdata(spi, priv);
	return 0;
}

static void spi_slave_system_control_remove(struct spi_device *spi)
{
	struct spi_slave_system_control_priv *priv = spi_get_drvdata(spi);

	spi_slave_abort(spi);
	wait_for_completion(&priv->finished);
}

static struct spi_driver spi_slave_system_control_driver = {
	.driver = {
		.name	= "spi-slave-system-control",
	},
	.probe		= spi_slave_system_control_probe,
	.remove		= spi_slave_system_control_remove,
};
module_spi_driver(spi_slave_system_control_driver);

MODULE_AUTHOR("Geert Uytterhoeven <geert+renesas@glider.be>");
MODULE_DESCRIPTION("SPI slave handler controlling system state");
MODULE_LICENSE("GPL v2");
