/*
 * The setup file for USB related hardware on PMC-Sierra MSP processors.
 *
 * Copyright 2006 PMC-Sierra, Inc.
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
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
#if defined(CONFIG_USB_EHCI_HCD) || defined(CONFIG_USB_GADGET)

#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>

#include <asm/mipsregs.h>

#include <msp_regs.h>
#include <msp_int.h>
#include <msp_prom.h>
#include <msp_usb.h>


#if defined(CONFIG_USB_EHCI_HCD)
static struct resource msp_usbhost0_resources[] = {
	[0] = { /* EHCI-HS operational and capabilities registers */
		.start  = MSP_USB0_HS_START,
		.end    = MSP_USB0_HS_END,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = MSP_INT_USB,
		.end    = MSP_INT_USB,
		.flags  = IORESOURCE_IRQ,
	},
	[2] = { /* MSBus-to-AMBA bridge register space */
		.start	= MSP_USB0_MAB_START,
		.end	= MSP_USB0_MAB_END,
		.flags	= IORESOURCE_MEM,
	},
	[3] = { /* Identification and general hardware parameters */
		.start	= MSP_USB0_ID_START,
		.end	= MSP_USB0_ID_END,
		.flags	= IORESOURCE_MEM,
	},
};

static u64 msp_usbhost0_dma_mask = 0xffffffffUL;

static struct mspusb_device msp_usbhost0_device = {
	.dev	= {
		.name	= "pmcmsp-ehci",
		.id	= 0,
		.dev	= {
			.dma_mask = &msp_usbhost0_dma_mask,
			.coherent_dma_mask = 0xffffffffUL,
		},
		.num_resources  = ARRAY_SIZE(msp_usbhost0_resources),
		.resource       = msp_usbhost0_resources,
	},
};

/* MSP7140/MSP82XX has two USB2 hosts. */
#ifdef CONFIG_MSP_HAS_DUAL_USB
static u64 msp_usbhost1_dma_mask = 0xffffffffUL;

static struct resource msp_usbhost1_resources[] = {
	[0] = { /* EHCI-HS operational and capabilities registers */
		.start	= MSP_USB1_HS_START,
		.end	= MSP_USB1_HS_END,
		.flags	= IORESOURCE_MEM,
	},
	[1] = {
		.start	= MSP_INT_USB,
		.end	= MSP_INT_USB,
		.flags	= IORESOURCE_IRQ,
	},
	[2] = { /* MSBus-to-AMBA bridge register space */
		.start	= MSP_USB1_MAB_START,
		.end	= MSP_USB1_MAB_END,
		.flags	= IORESOURCE_MEM,
	},
	[3] = { /* Identification and general hardware parameters */
		.start	= MSP_USB1_ID_START,
		.end	= MSP_USB1_ID_END,
		.flags	= IORESOURCE_MEM,
	},
};

static struct mspusb_device msp_usbhost1_device = {
	.dev	= {
		.name	= "pmcmsp-ehci",
		.id	= 1,
		.dev	= {
			.dma_mask = &msp_usbhost1_dma_mask,
			.coherent_dma_mask = 0xffffffffUL,
		},
		.num_resources	= ARRAY_SIZE(msp_usbhost1_resources),
		.resource	= msp_usbhost1_resources,
	},
};
#endif /* CONFIG_MSP_HAS_DUAL_USB */
#endif /* CONFIG_USB_EHCI_HCD */

#if defined(CONFIG_USB_GADGET)
static struct resource msp_usbdev0_resources[] = {
	[0] = { /* EHCI-HS operational and capabilities registers */
		.start  = MSP_USB0_HS_START,
		.end    = MSP_USB0_HS_END,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = MSP_INT_USB,
		.end    = MSP_INT_USB,
		.flags  = IORESOURCE_IRQ,
	},
	[2] = { /* MSBus-to-AMBA bridge register space */
		.start	= MSP_USB0_MAB_START,
		.end	= MSP_USB0_MAB_END,
		.flags	= IORESOURCE_MEM,
	},
	[3] = { /* Identification and general hardware parameters */
		.start	= MSP_USB0_ID_START,
		.end	= MSP_USB0_ID_END,
		.flags	= IORESOURCE_MEM,
	},
};

static u64 msp_usbdev_dma_mask = 0xffffffffUL;

/* This may need to be converted to a mspusb_device, too. */
static struct mspusb_device msp_usbdev0_device = {
	.dev	= {
		.name	= "msp71xx_udc",
		.id	= 0,
		.dev	= {
			.dma_mask = &msp_usbdev_dma_mask,
			.coherent_dma_mask = 0xffffffffUL,
		},
		.num_resources  = ARRAY_SIZE(msp_usbdev0_resources),
		.resource       = msp_usbdev0_resources,
	},
};

#ifdef CONFIG_MSP_HAS_DUAL_USB
static struct resource msp_usbdev1_resources[] = {
	[0] = { /* EHCI-HS operational and capabilities registers */
		.start  = MSP_USB1_HS_START,
		.end    = MSP_USB1_HS_END,
		.flags  = IORESOURCE_MEM,
	},
	[1] = {
		.start  = MSP_INT_USB,
		.end    = MSP_INT_USB,
		.flags  = IORESOURCE_IRQ,
	},
	[2] = { /* MSBus-to-AMBA bridge register space */
		.start	= MSP_USB1_MAB_START,
		.end	= MSP_USB1_MAB_END,
		.flags	= IORESOURCE_MEM,
	},
	[3] = { /* Identification and general hardware parameters */
		.start	= MSP_USB1_ID_START,
		.end	= MSP_USB1_ID_END,
		.flags	= IORESOURCE_MEM,
	},
};

/* This may need to be converted to a mspusb_device, too. */
static struct mspusb_device msp_usbdev1_device = {
	.dev	= {
		.name	= "msp71xx_udc",
		.id	= 0,
		.dev	= {
			.dma_mask = &msp_usbdev_dma_mask,
			.coherent_dma_mask = 0xffffffffUL,
		},
		.num_resources  = ARRAY_SIZE(msp_usbdev1_resources),
		.resource       = msp_usbdev1_resources,
	},
};

#endif /* CONFIG_MSP_HAS_DUAL_USB */
#endif /* CONFIG_USB_GADGET */

static int __init msp_usb_setup(void)
{
	char		*strp;
	char		envstr[32];
	struct platform_device *msp_devs[NUM_USB_DEVS];
	unsigned int val;

	/* construct environment name usbmode */
	/* set usbmode <host/device> as pmon environment var */
	/*
	 * Could this perhaps be integrated into the "features" env var?
	 * Use the features key "U", and follow with "H" for host-mode,
	 * "D" for device-mode.  If it works for Ethernet, why not USB...
	 *  -- hammtrev, 2007/03/22
	 */
	snprintf((char *)&envstr[0], sizeof(envstr), "usbmode");

	/* set default host mode */
	val = 1;

	/* get environment string */
	strp = prom_getenv((char *)&envstr[0]);
	if (strp) {
		/* compare string */
		if (!strcmp(strp, "device"))
			val = 0;
	}

	if (val) {
#if defined(CONFIG_USB_EHCI_HCD)
		msp_devs[0] = &msp_usbhost0_device.dev;
		ppfinit("platform add USB HOST done %s.\n", msp_devs[0]->name);
#ifdef CONFIG_MSP_HAS_DUAL_USB
		msp_devs[1] = &msp_usbhost1_device.dev;
		ppfinit("platform add USB HOST done %s.\n", msp_devs[1]->name);
#endif
#else
		ppfinit("%s: echi_hcd not supported\n", __FILE__);
#endif  /* CONFIG_USB_EHCI_HCD */
	} else {
#if defined(CONFIG_USB_GADGET)
		/* get device mode structure */
		msp_devs[0] = &msp_usbdev0_device.dev;
		ppfinit("platform add USB DEVICE done %s.\n"
					, msp_devs[0]->name);
#ifdef CONFIG_MSP_HAS_DUAL_USB
		msp_devs[1] = &msp_usbdev1_device.dev;
		ppfinit("platform add USB DEVICE done %s.\n"
					, msp_devs[1]->name);
#endif
#else
		ppfinit("%s: usb_gadget not supported\n", __FILE__);
#endif  /* CONFIG_USB_GADGET */
	}
	/* add device */
	platform_add_devices(msp_devs, ARRAY_SIZE(msp_devs));

	return 0;
}

subsys_initcall(msp_usb_setup);
#endif /* CONFIG_USB_EHCI_HCD || CONFIG_USB_GADGET */
