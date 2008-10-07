/*
 * VIPER PCMCIA support
 *   Copyright 2004 Arcom Control Systems
 *
 * Maintained by Marc Zyngier <maz@misterjones.org>
 * 			      <marc.zyngier@altran.com>
 *
 * Based on:
 *   iPAQ h2200 PCMCIA support
 *   Copyright 2004 Koen Kooi <koen@vestingbar.nl>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>

#include <pcmcia/ss.h>

#include <asm/irq.h>

#include <mach/pxa-regs.h>
#include <mach/viper.h>
#include <asm/mach-types.h>

#include "soc_common.h"
#include "pxa2xx_base.h"

static struct pcmcia_irqs irqs[] = {
	{ 0, gpio_to_irq(VIPER_CF_CD_GPIO),  "PCMCIA_CD" }
};

static int viper_pcmcia_hw_init(struct soc_pcmcia_socket *skt)
{
	unsigned long flags;

	skt->irq = gpio_to_irq(VIPER_CF_RDY_GPIO);

	if (gpio_request(VIPER_CF_CD_GPIO, "CF detect"))
		goto err_request_cd;

	if (gpio_request(VIPER_CF_RDY_GPIO, "CF ready"))
		goto err_request_rdy;

	if (gpio_request(VIPER_CF_POWER_GPIO, "CF power"))
		goto err_request_pwr;

	local_irq_save(flags);

	/* GPIO 82 is the CF power enable line. initially off */
	if (gpio_direction_output(VIPER_CF_POWER_GPIO, 0) ||
	    gpio_direction_input(VIPER_CF_CD_GPIO) ||
	    gpio_direction_input(VIPER_CF_RDY_GPIO)) {
		local_irq_restore(flags);
		goto err_dir;
	}

	local_irq_restore(flags);

	return soc_pcmcia_request_irqs(skt, irqs, ARRAY_SIZE(irqs));

err_dir:
	gpio_free(VIPER_CF_POWER_GPIO);
err_request_pwr:
	gpio_free(VIPER_CF_RDY_GPIO);
err_request_rdy:
	gpio_free(VIPER_CF_CD_GPIO);
err_request_cd:
	printk(KERN_ERR "viper: Failed to setup PCMCIA GPIOs\n");
	return -1;
}

/*
 * Release all resources.
 */
static void viper_pcmcia_hw_shutdown(struct soc_pcmcia_socket *skt)
{
	soc_pcmcia_free_irqs(skt, irqs, ARRAY_SIZE(irqs));
	gpio_free(VIPER_CF_POWER_GPIO);
	gpio_free(VIPER_CF_RDY_GPIO);
	gpio_free(VIPER_CF_CD_GPIO);
}

static void viper_pcmcia_socket_state(struct soc_pcmcia_socket *skt,
				      struct pcmcia_state *state)
{
	state->detect = gpio_get_value(VIPER_CF_CD_GPIO) ? 0 : 1;
	state->ready  = gpio_get_value(VIPER_CF_RDY_GPIO) ? 1 : 0;
	state->bvd1   = 1;
	state->bvd2   = 1;
	state->wrprot = 0;
	state->vs_3v  = 1; /* Can only apply 3.3V */
	state->vs_Xv  = 0;
}

static int viper_pcmcia_configure_socket(struct soc_pcmcia_socket *skt,
					 const socket_state_t *state)
{
	/* Silently ignore Vpp, output enable, speaker enable. */
	viper_cf_rst(state->flags & SS_RESET);

	/* Apply socket voltage */
	switch (state->Vcc) {
	case 0:
		gpio_set_value(VIPER_CF_POWER_GPIO, 0);
		break;
	case 33:
		gpio_set_value(VIPER_CF_POWER_GPIO, 1);
		break;
	default:
		printk(KERN_ERR "%s: Unsupported Vcc:%d\n",
		       __func__, state->Vcc);
		return -1;
	}

	return 0;
}

static void viper_pcmcia_socket_init(struct soc_pcmcia_socket *skt)
{
}

static void viper_pcmcia_socket_suspend(struct soc_pcmcia_socket *skt)
{
}

static struct pcmcia_low_level viper_pcmcia_ops __initdata = {
	.owner          	= THIS_MODULE,
	.hw_init        	= viper_pcmcia_hw_init,
	.hw_shutdown		= viper_pcmcia_hw_shutdown,
	.socket_state		= viper_pcmcia_socket_state,
	.configure_socket	= viper_pcmcia_configure_socket,
	.socket_init		= viper_pcmcia_socket_init,
	.socket_suspend		= viper_pcmcia_socket_suspend,
	.nr         		= 1,
};

static struct platform_device *viper_pcmcia_device;

static int __init viper_pcmcia_init(void)
{
	int ret;

	if (!machine_is_viper())
		return -ENODEV;

	viper_pcmcia_device = platform_device_alloc("pxa2xx-pcmcia", -1);
	if (!viper_pcmcia_device)
		return -ENOMEM;

	ret = platform_device_add_data(viper_pcmcia_device,
				       &viper_pcmcia_ops,
				       sizeof(viper_pcmcia_ops));

	if (!ret)
		ret = platform_device_add(viper_pcmcia_device);

	if (ret)
		platform_device_put(viper_pcmcia_device);

	return ret;
}

static void __exit viper_pcmcia_exit(void)
{
	platform_device_unregister(viper_pcmcia_device);
}

module_init(viper_pcmcia_init);
module_exit(viper_pcmcia_exit);

MODULE_LICENSE("GPL");
