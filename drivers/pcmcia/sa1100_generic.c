/*======================================================================

    Device driver for the PCMCIA control functionality of StrongARM
    SA-1100 microprocessors.

    The contents of this file are subject to the Mozilla Public
    License Version 1.1 (the "License"); you may not use this file
    except in compliance with the License. You may obtain a copy of
    the License at http://www.mozilla.org/MPL/

    Software distributed under the License is distributed on an "AS
    IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
    implied. See the License for the specific language governing
    rights and limitations under the License.

    The initial developer of the original code is John G. Dorsey
    <john+@cs.cmu.edu>.  Portions created by John G. Dorsey are
    Copyright (C) 1999 John G. Dorsey.  All Rights Reserved.

    Alternatively, the contents of this file may be used under the
    terms of the GNU Public License version 2 (the "GPL"), in which
    case the provisions of the GPL are applicable instead of the
    above.  If you wish to allow the use of your version of this file
    only under the terms of the GPL and not to allow others to use
    your version of this file under the MPL, indicate your decision
    by deleting the provisions above and replace them with the notice
    and other provisions required by the GPL.  If you do not delete
    the provisions above, a recipient may use your version of this
    file under either the MPL or the GPL.
    
======================================================================*/

#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/platform_device.h>

#include <pcmcia/ss.h>

#include <asm/hardware/scoop.h>

#include "sa1100_generic.h"

static const char *sa11x0_cf_gpio_names[] = {
	[SOC_STAT_CD] = "detect",
	[SOC_STAT_BVD1] = "bvd1",
	[SOC_STAT_BVD2] = "bvd2",
	[SOC_STAT_RDY] = "ready",
};

static int sa11x0_cf_hw_init(struct soc_pcmcia_socket *skt)
{
	struct device *dev = skt->socket.dev.parent;
	int i;

	skt->gpio_reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(skt->gpio_reset))
		return PTR_ERR(skt->gpio_reset);

	skt->gpio_bus_enable = devm_gpiod_get_optional(dev, "bus-enable",
						       GPIOD_OUT_HIGH);
	if (IS_ERR(skt->gpio_bus_enable))
		return PTR_ERR(skt->gpio_bus_enable);

	skt->vcc.reg = devm_regulator_get_optional(dev, "vcc");
	if (IS_ERR(skt->vcc.reg))
		return PTR_ERR(skt->vcc.reg);

	if (!skt->vcc.reg)
		dev_warn(dev,
			 "no Vcc regulator provided, ignoring Vcc controls\n");

	for (i = 0; i < ARRAY_SIZE(sa11x0_cf_gpio_names); i++) {
		skt->stat[i].name = sa11x0_cf_gpio_names[i];
		skt->stat[i].desc = devm_gpiod_get_optional(dev,
					sa11x0_cf_gpio_names[i], GPIOD_IN);
		if (IS_ERR(skt->stat[i].desc))
			return PTR_ERR(skt->stat[i].desc);
	}
	return 0;
}

static int sa11x0_cf_configure_socket(struct soc_pcmcia_socket *skt,
	const socket_state_t *state)
{
	return soc_pcmcia_regulator_set(skt, &skt->vcc, state->Vcc);
}

static struct pcmcia_low_level sa11x0_cf_ops = {
	.owner = THIS_MODULE,
	.hw_init = sa11x0_cf_hw_init,
	.socket_state = soc_common_cf_socket_state,
	.configure_socket = sa11x0_cf_configure_socket,
};

int __init pcmcia_collie_init(struct device *dev);

static int (*sa11x0_pcmcia_legacy_hw_init[])(struct device *dev) = {
#ifdef CONFIG_SA1100_H3600
	pcmcia_h3600_init,
#endif
#ifdef CONFIG_SA1100_COLLIE
       pcmcia_collie_init,
#endif
};

static int sa11x0_drv_pcmcia_legacy_probe(struct platform_device *dev)
{
	int i, ret = -ENODEV;

	/*
	 * Initialise any "on-board" PCMCIA sockets.
	 */
	for (i = 0; i < ARRAY_SIZE(sa11x0_pcmcia_legacy_hw_init); i++) {
		ret = sa11x0_pcmcia_legacy_hw_init[i](&dev->dev);
		if (ret == 0)
			break;
	}

	return ret;
}

static void sa11x0_drv_pcmcia_legacy_remove(struct platform_device *dev)
{
	struct skt_dev_info *sinfo = platform_get_drvdata(dev);
	int i;

	platform_set_drvdata(dev, NULL);

	for (i = 0; i < sinfo->nskt; i++)
		soc_pcmcia_remove_one(&sinfo->skt[i]);
}

static int sa11x0_drv_pcmcia_probe(struct platform_device *pdev)
{
	struct soc_pcmcia_socket *skt;
	struct device *dev = &pdev->dev;

	if (pdev->id == -1)
		return sa11x0_drv_pcmcia_legacy_probe(pdev);

	skt = devm_kzalloc(dev, sizeof(*skt), GFP_KERNEL);
	if (!skt)
		return -ENOMEM;

	platform_set_drvdata(pdev, skt);

	skt->nr = pdev->id;
	skt->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(skt->clk))
		return PTR_ERR(skt->clk);

	sa11xx_drv_pcmcia_ops(&sa11x0_cf_ops);
	soc_pcmcia_init_one(skt, &sa11x0_cf_ops, dev);

	return sa11xx_drv_pcmcia_add_one(skt);
}

static int sa11x0_drv_pcmcia_remove(struct platform_device *dev)
{
	struct soc_pcmcia_socket *skt;

	if (dev->id == -1) {
		sa11x0_drv_pcmcia_legacy_remove(dev);
		return 0;
	}

	skt = platform_get_drvdata(dev);

	soc_pcmcia_remove_one(skt);

	return 0;
}

static struct platform_driver sa11x0_pcmcia_driver = {
	.driver = {
		.name		= "sa11x0-pcmcia",
	},
	.probe		= sa11x0_drv_pcmcia_probe,
	.remove		= sa11x0_drv_pcmcia_remove,
};

/* sa11x0_pcmcia_init()
 * ^^^^^^^^^^^^^^^^^^^^
 *
 * This routine performs low-level PCMCIA initialization and then
 * registers this socket driver with Card Services.
 *
 * Returns: 0 on success, -ve error code on failure
 */
static int __init sa11x0_pcmcia_init(void)
{
	return platform_driver_register(&sa11x0_pcmcia_driver);
}

/* sa11x0_pcmcia_exit()
 * ^^^^^^^^^^^^^^^^^^^^
 * Invokes the low-level kernel service to free IRQs associated with this
 * socket controller and reset GPIO edge detection.
 */
static void __exit sa11x0_pcmcia_exit(void)
{
	platform_driver_unregister(&sa11x0_pcmcia_driver);
}

MODULE_AUTHOR("John Dorsey <john+@cs.cmu.edu>");
MODULE_DESCRIPTION("Linux PCMCIA Card Services: SA-11x0 Socket Controller");
MODULE_LICENSE("Dual MPL/GPL");

fs_initcall(sa11x0_pcmcia_init);
module_exit(sa11x0_pcmcia_exit);
