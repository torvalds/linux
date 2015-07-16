/*
 * The setup file for ethernet related hardware on PMC-Sierra MSP processors.
 *
 * Copyright 2010 PMC-Sierra, Inc.
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <msp_regs.h>
#include <msp_int.h>
#include <msp_gpio_macros.h>


#define MSP_ETHERNET_GPIO0	14
#define MSP_ETHERNET_GPIO1	15
#define MSP_ETHERNET_GPIO2	16

#define MSP_ETH_ID	"pmc_mspeth"
#define MSP_ETH_SIZE	0xE0
static struct resource msp_eth0_resources[] = {
	[0] = {
		.start	= MSP_MAC0_BASE,
		.end	= MSP_MAC0_BASE + MSP_ETH_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= MSP_INT_MAC0,
		.end	= MSP_INT_MAC0,
		.flags	= IORESOURCE_IRQ,
	},
};

static struct resource msp_eth1_resources[] = {
	[0] = {
		.start	= MSP_MAC1_BASE,
		.end	= MSP_MAC1_BASE + MSP_ETH_SIZE - 1,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= MSP_INT_MAC1,
		.end	= MSP_INT_MAC1,
		.flags	= IORESOURCE_IRQ,
	},
};



static struct platform_device mspeth_device[] = {
	[0] = {
		.name	= MSP_ETH_ID,
		.id	= 0,
		.num_resources = ARRAY_SIZE(msp_eth0_resources),
		.resource = msp_eth0_resources,
	},
	[1] = {
		.name	= MSP_ETH_ID,
		.id	= 1,
		.num_resources = ARRAY_SIZE(msp_eth1_resources),
		.resource = msp_eth1_resources,
	},

};
#define msp_eth_devs	mspeth_device

int __init msp_eth_setup(void)
{
	int i, ret = 0;

	/* Configure the GPIO and take the ethernet PHY out of reset */
	msp_gpio_pin_mode(MSP_GPIO_OUTPUT, MSP_ETHERNET_GPIO0);
	msp_gpio_pin_hi(MSP_ETHERNET_GPIO0);

	for (i = 0; i < ARRAY_SIZE(msp_eth_devs); i++) {
		ret = platform_device_register(&msp_eth_devs[i]);
		printk(KERN_INFO "device: %d, return value = %d\n", i, ret);
		if (ret) {
			platform_device_unregister(&msp_eth_devs[i]);
			break;
		}
	}

	if (ret)
		printk(KERN_WARNING "Could not initialize "
						"MSPETH device structures.\n");

	return ret;
}
subsys_initcall(msp_eth_setup);
