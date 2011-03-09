/*********************************************************************
 *
 * Description:   Driver for the SMC Infrared Communications Controller
 * Status:        Experimental.
 * Author:        Daniele Peri (peri@csai.unipa.it)
 * Created at:
 * Modified at:
 * Modified by:
 *
 *     Copyright (c) 2002      Daniele Peri
 *     All Rights Reserved.
 *     Copyright (c) 2002      Jean Tourrilhes
 *     Copyright (c) 2006      Linus Walleij
 *
 *
 * Based on smc-ircc.c:
 *
 *     Copyright (c) 2001      Stefani Seibold
 *     Copyright (c) 1999-2001 Dag Brattli
 *     Copyright (c) 1998-1999 Thomas Davis,
 *
 *	and irport.c:
 *
 *     Copyright (c) 1997, 1998, 1999-2000 Dag Brattli, All Rights Reserved.
 *
 *
 *     This program is free software; you can redistribute it and/or
 *     modify it under the terms of the GNU General Public License as
 *     published by the Free Software Foundation; either version 2 of
 *     the License, or (at your option) any later version.
 *
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 *
 *     You should have received a copy of the GNU General Public License
 *     along with this program; if not, write to the Free Software
 *     Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 *     MA 02111-1307 USA
 *
 ********************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/rtnetlink.h>
#include <linux/serial_reg.h>
#include <linux/dma-mapping.h>
#include <linux/pnp.h>
#include <linux/platform_device.h>
#include <linux/gfp.h>

#include <asm/io.h>
#include <asm/dma.h>
#include <asm/byteorder.h>

#include <linux/spinlock.h>
#include <linux/pm.h>
#ifdef CONFIG_PCI
#include <linux/pci.h>
#endif

#include <net/irda/wrapper.h>
#include <net/irda/irda.h>
#include <net/irda/irda_device.h>

#include "smsc-ircc2.h"
#include "smsc-sio.h"


MODULE_AUTHOR("Daniele Peri <peri@csai.unipa.it>");
MODULE_DESCRIPTION("SMC IrCC SIR/FIR controller driver");
MODULE_LICENSE("GPL");

static int smsc_nopnp = 1;
module_param_named(nopnp, smsc_nopnp, bool, 0);
MODULE_PARM_DESC(nopnp, "Do not use PNP to detect controller settings, defaults to true");

#define DMA_INVAL 255
static int ircc_dma = DMA_INVAL;
module_param(ircc_dma, int, 0);
MODULE_PARM_DESC(ircc_dma, "DMA channel");

#define IRQ_INVAL 255
static int ircc_irq = IRQ_INVAL;
module_param(ircc_irq, int, 0);
MODULE_PARM_DESC(ircc_irq, "IRQ line");

static int ircc_fir;
module_param(ircc_fir, int, 0);
MODULE_PARM_DESC(ircc_fir, "FIR Base Address");

static int ircc_sir;
module_param(ircc_sir, int, 0);
MODULE_PARM_DESC(ircc_sir, "SIR Base Address");

static int ircc_cfg;
module_param(ircc_cfg, int, 0);
MODULE_PARM_DESC(ircc_cfg, "Configuration register base address");

static int ircc_transceiver;
module_param(ircc_transceiver, int, 0);
MODULE_PARM_DESC(ircc_transceiver, "Transceiver type");

/* Types */

#ifdef CONFIG_PCI
struct smsc_ircc_subsystem_configuration {
	unsigned short vendor; /* PCI vendor ID */
	unsigned short device; /* PCI vendor ID */
	unsigned short subvendor; /* PCI subsystem vendor ID */
	unsigned short subdevice; /* PCI subsystem device ID */
	unsigned short sir_io; /* I/O port for SIR */
	unsigned short fir_io; /* I/O port for FIR */
	unsigned char  fir_irq; /* FIR IRQ */
	unsigned char  fir_dma; /* FIR DMA */
	unsigned short cfg_base; /* I/O port for chip configuration */
	int (*preconfigure)(struct pci_dev *dev, struct smsc_ircc_subsystem_configuration *conf); /* Preconfig function */
	const char *name;	/* name shown as info */
};
#endif

struct smsc_transceiver {
	char *name;
	void (*set_for_speed)(int fir_base, u32 speed);
	int  (*probe)(int fir_base);
};

struct smsc_chip {
	char *name;
	#if 0
	u8	type;
	#endif
	u16 flags;
	u8 devid;
	u8 rev;
};

struct smsc_chip_address {
	unsigned int cfg_base;
	unsigned int type;
};

/* Private data for each instance */
struct smsc_ircc_cb {
	struct net_device *netdev;     /* Yes! we are some kind of netdevice */
	struct irlap_cb    *irlap; /* The link layer we are binded to */

	chipio_t io;               /* IrDA controller information */
	iobuff_t tx_buff;          /* Transmit buffer */
	iobuff_t rx_buff;          /* Receive buffer */
	dma_addr_t tx_buff_dma;
	dma_addr_t rx_buff_dma;

	struct qos_info qos;       /* QoS capabilities for this device */

	spinlock_t lock;           /* For serializing operations */

	__u32 new_speed;
	__u32 flags;               /* Interface flags */

	int tx_buff_offsets[10];   /* Offsets between frames in tx_buff */
	int tx_len;                /* Number of frames in tx_buff */

	int transceiver;
	struct platform_device *pldev;
};

/* Constants */

#define SMSC_IRCC2_DRIVER_NAME			"smsc-ircc2"

#define SMSC_IRCC2_C_IRDA_FALLBACK_SPEED	9600
#define SMSC_IRCC2_C_DEFAULT_TRANSCEIVER	1
#define SMSC_IRCC2_C_NET_TIMEOUT		0
#define SMSC_IRCC2_C_SIR_STOP			0

static const char *driver_name = SMSC_IRCC2_DRIVER_NAME;

/* Prototypes */

static int smsc_ircc_open(unsigned int firbase, unsigned int sirbase, u8 dma, u8 irq);
static int smsc_ircc_present(unsigned int fir_base, unsigned int sir_base);
static void smsc_ircc_setup_io(struct smsc_ircc_cb *self, unsigned int fir_base, unsigned int sir_base, u8 dma, u8 irq);
static void smsc_ircc_setup_qos(struct smsc_ircc_cb *self);
static void smsc_ircc_init_chip(struct smsc_ircc_cb *self);
static int __exit smsc_ircc_close(struct smsc_ircc_cb *self);
static int  smsc_ircc_dma_receive(struct smsc_ircc_cb *self);
static void smsc_ircc_dma_receive_complete(struct smsc_ircc_cb *self);
static void smsc_ircc_sir_receive(struct smsc_ircc_cb *self);
static netdev_tx_t  smsc_ircc_hard_xmit_sir(struct sk_buff *skb,
						  struct net_device *dev);
static netdev_tx_t  smsc_ircc_hard_xmit_fir(struct sk_buff *skb,
						  struct net_device *dev);
static void smsc_ircc_dma_xmit(struct smsc_ircc_cb *self, int bofs);
static void smsc_ircc_dma_xmit_complete(struct smsc_ircc_cb *self);
static void smsc_ircc_change_speed(struct smsc_ircc_cb *self, u32 speed);
static void smsc_ircc_set_sir_speed(struct smsc_ircc_cb *self, u32 speed);
static irqreturn_t smsc_ircc_interrupt(int irq, void *dev_id);
static irqreturn_t smsc_ircc_interrupt_sir(struct net_device *dev);
static void smsc_ircc_sir_start(struct smsc_ircc_cb *self);
#if SMSC_IRCC2_C_SIR_STOP
static void smsc_ircc_sir_stop(struct smsc_ircc_cb *self);
#endif
static void smsc_ircc_sir_write_wakeup(struct smsc_ircc_cb *self);
static int  smsc_ircc_sir_write(int iobase, int fifo_size, __u8 *buf, int len);
static int  smsc_ircc_net_open(struct net_device *dev);
static int  smsc_ircc_net_close(struct net_device *dev);
static int  smsc_ircc_net_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
#if SMSC_IRCC2_C_NET_TIMEOUT
static void smsc_ircc_timeout(struct net_device *dev);
#endif
static int smsc_ircc_is_receiving(struct smsc_ircc_cb *self);
static void smsc_ircc_probe_transceiver(struct smsc_ircc_cb *self);
static void smsc_ircc_set_transceiver_for_speed(struct smsc_ircc_cb *self, u32 speed);
static void smsc_ircc_sir_wait_hw_transmitter_finish(struct smsc_ircc_cb *self);

/* Probing */
static int __init smsc_ircc_look_for_chips(void);
static const struct smsc_chip * __init smsc_ircc_probe(unsigned short cfg_base, u8 reg, const struct smsc_chip *chip, char *type);
static int __init smsc_superio_flat(const struct smsc_chip *chips, unsigned short cfg_base, char *type);
static int __init smsc_superio_paged(const struct smsc_chip *chips, unsigned short cfg_base, char *type);
static int __init smsc_superio_fdc(unsigned short cfg_base);
static int __init smsc_superio_lpc(unsigned short cfg_base);
#ifdef CONFIG_PCI
static int __init preconfigure_smsc_chip(struct smsc_ircc_subsystem_configuration *conf);
static int __init preconfigure_through_82801(struct pci_dev *dev, struct smsc_ircc_subsystem_configuration *conf);
static void __init preconfigure_ali_port(struct pci_dev *dev,
					 unsigned short port);
static int __init preconfigure_through_ali(struct pci_dev *dev, struct smsc_ircc_subsystem_configuration *conf);
static int __init smsc_ircc_preconfigure_subsystems(unsigned short ircc_cfg,
						    unsigned short ircc_fir,
						    unsigned short ircc_sir,
						    unsigned char ircc_dma,
						    unsigned char ircc_irq);
#endif

/* Transceivers specific functions */

static void smsc_ircc_set_transceiver_toshiba_sat1800(int fir_base, u32 speed);
static int  smsc_ircc_probe_transceiver_toshiba_sat1800(int fir_base);
static void smsc_ircc_set_transceiver_smsc_ircc_fast_pin_select(int fir_base, u32 speed);
static int  smsc_ircc_probe_transceiver_smsc_ircc_fast_pin_select(int fir_base);
static void smsc_ircc_set_transceiver_smsc_ircc_atc(int fir_base, u32 speed);
static int  smsc_ircc_probe_transceiver_smsc_ircc_atc(int fir_base);

/* Power Management */

static int smsc_ircc_suspend(struct platform_device *dev, pm_message_t state);
static int smsc_ircc_resume(struct platform_device *dev);

static struct platform_driver smsc_ircc_driver = {
	.suspend	= smsc_ircc_suspend,
	.resume		= smsc_ircc_resume,
	.driver		= {
		.name	= SMSC_IRCC2_DRIVER_NAME,
	},
};

/* Transceivers for SMSC-ircc */

static struct smsc_transceiver smsc_transceivers[] =
{
	{ "Toshiba Satellite 1800 (GP data pin select)", smsc_ircc_set_transceiver_toshiba_sat1800, smsc_ircc_probe_transceiver_toshiba_sat1800 },
	{ "Fast pin select", smsc_ircc_set_transceiver_smsc_ircc_fast_pin_select, smsc_ircc_probe_transceiver_smsc_ircc_fast_pin_select },
	{ "ATC IRMode", smsc_ircc_set_transceiver_smsc_ircc_atc, smsc_ircc_probe_transceiver_smsc_ircc_atc },
	{ NULL, NULL }
};
#define SMSC_IRCC2_C_NUMBER_OF_TRANSCEIVERS (ARRAY_SIZE(smsc_transceivers) - 1)

/*  SMC SuperIO chipsets definitions */

#define	KEY55_1	0	/* SuperIO Configuration mode with Key <0x55> */
#define	KEY55_2	1	/* SuperIO Configuration mode with Key <0x55,0x55> */
#define	NoIRDA	2	/* SuperIO Chip has no IRDA Port */
#define	SIR	0	/* SuperIO Chip has only slow IRDA */
#define	FIR	4	/* SuperIO Chip has fast IRDA */
#define	SERx4	8	/* SuperIO Chip supports 115,2 KBaud * 4=460,8 KBaud */

static struct smsc_chip __initdata fdc_chips_flat[] =
{
	/* Base address 0x3f0 or 0x370 */
	{ "37C44",	KEY55_1|NoIRDA,		0x00, 0x00 }, /* This chip cannot be detected */
	{ "37C665GT",	KEY55_2|NoIRDA,		0x65, 0x01 },
	{ "37C665GT",	KEY55_2|NoIRDA,		0x66, 0x01 },
	{ "37C669",	KEY55_2|SIR|SERx4,	0x03, 0x02 },
	{ "37C669",	KEY55_2|SIR|SERx4,	0x04, 0x02 }, /* ID? */
	{ "37C78",	KEY55_2|NoIRDA,		0x78, 0x00 },
	{ "37N769",	KEY55_1|FIR|SERx4,	0x28, 0x00 },
	{ "37N869",	KEY55_1|FIR|SERx4,	0x29, 0x00 },
	{ NULL }
};

static struct smsc_chip __initdata fdc_chips_paged[] =
{
	/* Base address 0x3f0 or 0x370 */
	{ "37B72X",	KEY55_1|SIR|SERx4,	0x4c, 0x00 },
	{ "37B77X",	KEY55_1|SIR|SERx4,	0x43, 0x00 },
	{ "37B78X",	KEY55_1|SIR|SERx4,	0x44, 0x00 },
	{ "37B80X",	KEY55_1|SIR|SERx4,	0x42, 0x00 },
	{ "37C67X",	KEY55_1|FIR|SERx4,	0x40, 0x00 },
	{ "37C93X",	KEY55_2|SIR|SERx4,	0x02, 0x01 },
	{ "37C93XAPM",	KEY55_1|SIR|SERx4,	0x30, 0x01 },
	{ "37C93XFR",	KEY55_2|FIR|SERx4,	0x03, 0x01 },
	{ "37M707",	KEY55_1|SIR|SERx4,	0x42, 0x00 },
	{ "37M81X",	KEY55_1|SIR|SERx4,	0x4d, 0x00 },
	{ "37N958FR",	KEY55_1|FIR|SERx4,	0x09, 0x04 },
	{ "37N971",	KEY55_1|FIR|SERx4,	0x0a, 0x00 },
	{ "37N972",	KEY55_1|FIR|SERx4,	0x0b, 0x00 },
	{ NULL }
};

static struct smsc_chip __initdata lpc_chips_flat[] =
{
	/* Base address 0x2E or 0x4E */
	{ "47N227",	KEY55_1|FIR|SERx4,	0x5a, 0x00 },
	{ "47N227",	KEY55_1|FIR|SERx4,	0x7a, 0x00 },
	{ "47N267",	KEY55_1|FIR|SERx4,	0x5e, 0x00 },
	{ NULL }
};

static struct smsc_chip __initdata lpc_chips_paged[] =
{
	/* Base address 0x2E or 0x4E */
	{ "47B27X",	KEY55_1|SIR|SERx4,	0x51, 0x00 },
	{ "47B37X",	KEY55_1|SIR|SERx4,	0x52, 0x00 },
	{ "47M10X",	KEY55_1|SIR|SERx4,	0x59, 0x00 },
	{ "47M120",	KEY55_1|NoIRDA|SERx4,	0x5c, 0x00 },
	{ "47M13X",	KEY55_1|SIR|SERx4,	0x59, 0x00 },
	{ "47M14X",	KEY55_1|SIR|SERx4,	0x5f, 0x00 },
	{ "47N252",	KEY55_1|FIR|SERx4,	0x0e, 0x00 },
	{ "47S42X",	KEY55_1|SIR|SERx4,	0x57, 0x00 },
	{ NULL }
};

#define SMSCSIO_TYPE_FDC	1
#define SMSCSIO_TYPE_LPC	2
#define SMSCSIO_TYPE_FLAT	4
#define SMSCSIO_TYPE_PAGED	8

static struct smsc_chip_address __initdata possible_addresses[] =
{
	{ 0x3f0, SMSCSIO_TYPE_FDC|SMSCSIO_TYPE_FLAT|SMSCSIO_TYPE_PAGED },
	{ 0x370, SMSCSIO_TYPE_FDC|SMSCSIO_TYPE_FLAT|SMSCSIO_TYPE_PAGED },
	{ 0xe0,  SMSCSIO_TYPE_FDC|SMSCSIO_TYPE_FLAT|SMSCSIO_TYPE_PAGED },
	{ 0x2e,  SMSCSIO_TYPE_LPC|SMSCSIO_TYPE_FLAT|SMSCSIO_TYPE_PAGED },
	{ 0x4e,  SMSCSIO_TYPE_LPC|SMSCSIO_TYPE_FLAT|SMSCSIO_TYPE_PAGED },
	{ 0, 0 }
};

/* Globals */

static struct smsc_ircc_cb *dev_self[] = { NULL, NULL };
static unsigned short dev_count;

static inline void register_bank(int iobase, int bank)
{
        outb(((inb(iobase + IRCC_MASTER) & 0xf0) | (bank & 0x07)),
               iobase + IRCC_MASTER);
}

/* PNP hotplug support */
static const struct pnp_device_id smsc_ircc_pnp_table[] = {
	{ .id = "SMCf010", .driver_data = 0 },
	/* and presumably others */
	{ }
};
MODULE_DEVICE_TABLE(pnp, smsc_ircc_pnp_table);

static int pnp_driver_registered;

#ifdef CONFIG_PNP
static int __devinit smsc_ircc_pnp_probe(struct pnp_dev *dev,
				      const struct pnp_device_id *dev_id)
{
	unsigned int firbase, sirbase;
	u8 dma, irq;

	if (!(pnp_port_valid(dev, 0) && pnp_port_valid(dev, 1) &&
	      pnp_dma_valid(dev, 0) && pnp_irq_valid(dev, 0)))
		return -EINVAL;

	sirbase = pnp_port_start(dev, 0);
	firbase = pnp_port_start(dev, 1);
	dma = pnp_dma(dev, 0);
	irq = pnp_irq(dev, 0);

	if (smsc_ircc_open(firbase, sirbase, dma, irq))
		return -ENODEV;

	return 0;
}

static struct pnp_driver smsc_ircc_pnp_driver = {
	.name		= "smsc-ircc2",
	.id_table	= smsc_ircc_pnp_table,
	.probe		= smsc_ircc_pnp_probe,
};
#else /* CONFIG_PNP */
static struct pnp_driver smsc_ircc_pnp_driver;
#endif

/*******************************************************************************
 *
 *
 * SMSC-ircc stuff
 *
 *
 *******************************************************************************/

static int __init smsc_ircc_legacy_probe(void)
{
	int ret = 0;

#ifdef CONFIG_PCI
	if (smsc_ircc_preconfigure_subsystems(ircc_cfg, ircc_fir, ircc_sir, ircc_dma, ircc_irq) < 0) {
		/* Ignore errors from preconfiguration */
		IRDA_ERROR("%s, Preconfiguration failed !\n", driver_name);
	}
#endif

	if (ircc_fir > 0 && ircc_sir > 0) {
		IRDA_MESSAGE(" Overriding FIR address 0x%04x\n", ircc_fir);
		IRDA_MESSAGE(" Overriding SIR address 0x%04x\n", ircc_sir);

		if (smsc_ircc_open(ircc_fir, ircc_sir, ircc_dma, ircc_irq))
			ret = -ENODEV;
	} else {
		ret = -ENODEV;

		/* try user provided configuration register base address */
		if (ircc_cfg > 0) {
			IRDA_MESSAGE(" Overriding configuration address "
				     "0x%04x\n", ircc_cfg);
			if (!smsc_superio_fdc(ircc_cfg))
				ret = 0;
			if (!smsc_superio_lpc(ircc_cfg))
				ret = 0;
		}

		if (smsc_ircc_look_for_chips() > 0)
			ret = 0;
	}
	return ret;
}

/*
 * Function smsc_ircc_init ()
 *
 *    Initialize chip. Just try to find out how many chips we are dealing with
 *    and where they are
 */
static int __init smsc_ircc_init(void)
{
	int ret;

	IRDA_DEBUG(1, "%s\n", __func__);

	ret = platform_driver_register(&smsc_ircc_driver);
	if (ret) {
		IRDA_ERROR("%s, Can't register driver!\n", driver_name);
		return ret;
	}

	dev_count = 0;

	if (smsc_nopnp || !pnp_platform_devices ||
	    ircc_cfg || ircc_fir || ircc_sir ||
	    ircc_dma != DMA_INVAL || ircc_irq != IRQ_INVAL) {
		ret = smsc_ircc_legacy_probe();
	} else {
		if (pnp_register_driver(&smsc_ircc_pnp_driver) == 0)
			pnp_driver_registered = 1;
	}

	if (ret) {
		if (pnp_driver_registered)
			pnp_unregister_driver(&smsc_ircc_pnp_driver);
		platform_driver_unregister(&smsc_ircc_driver);
	}

	return ret;
}

static netdev_tx_t smsc_ircc_net_xmit(struct sk_buff *skb,
					    struct net_device *dev)
{
	struct smsc_ircc_cb *self = netdev_priv(dev);

	if (self->io.speed > 115200)
		return 	smsc_ircc_hard_xmit_fir(skb, dev);
	else
		return 	smsc_ircc_hard_xmit_sir(skb, dev);
}

static const struct net_device_ops smsc_ircc_netdev_ops = {
	.ndo_open       = smsc_ircc_net_open,
	.ndo_stop       = smsc_ircc_net_close,
	.ndo_do_ioctl   = smsc_ircc_net_ioctl,
	.ndo_start_xmit = smsc_ircc_net_xmit,
#if SMSC_IRCC2_C_NET_TIMEOUT
	.ndo_tx_timeout	= smsc_ircc_timeout,
#endif
};

/*
 * Function smsc_ircc_open (firbase, sirbase, dma, irq)
 *
 *    Try to open driver instance
 *
 */
static int __init smsc_ircc_open(unsigned int fir_base, unsigned int sir_base, u8 dma, u8 irq)
{
	struct smsc_ircc_cb *self;
	struct net_device *dev;
	int err;

	IRDA_DEBUG(1, "%s\n", __func__);

	err = smsc_ircc_present(fir_base, sir_base);
	if (err)
		goto err_out;

	err = -ENOMEM;
	if (dev_count >= ARRAY_SIZE(dev_self)) {
	        IRDA_WARNING("%s(), too many devices!\n", __func__);
		goto err_out1;
	}

	/*
	 *  Allocate new instance of the driver
	 */
	dev = alloc_irdadev(sizeof(struct smsc_ircc_cb));
	if (!dev) {
		IRDA_WARNING("%s() can't allocate net device\n", __func__);
		goto err_out1;
	}

#if SMSC_IRCC2_C_NET_TIMEOUT
	dev->watchdog_timeo  = HZ * 2;  /* Allow enough time for speed change */
#endif
	dev->netdev_ops = &smsc_ircc_netdev_ops;

	self = netdev_priv(dev);
	self->netdev = dev;

	/* Make ifconfig display some details */
	dev->base_addr = self->io.fir_base = fir_base;
	dev->irq = self->io.irq = irq;

	/* Need to store self somewhere */
	dev_self[dev_count] = self;
	spin_lock_init(&self->lock);

	self->rx_buff.truesize = SMSC_IRCC2_RX_BUFF_TRUESIZE;
	self->tx_buff.truesize = SMSC_IRCC2_TX_BUFF_TRUESIZE;

	self->rx_buff.head =
		dma_alloc_coherent(NULL, self->rx_buff.truesize,
				   &self->rx_buff_dma, GFP_KERNEL);
	if (self->rx_buff.head == NULL) {
		IRDA_ERROR("%s, Can't allocate memory for receive buffer!\n",
			   driver_name);
		goto err_out2;
	}

	self->tx_buff.head =
		dma_alloc_coherent(NULL, self->tx_buff.truesize,
				   &self->tx_buff_dma, GFP_KERNEL);
	if (self->tx_buff.head == NULL) {
		IRDA_ERROR("%s, Can't allocate memory for transmit buffer!\n",
			   driver_name);
		goto err_out3;
	}

	memset(self->rx_buff.head, 0, self->rx_buff.truesize);
	memset(self->tx_buff.head, 0, self->tx_buff.truesize);

	self->rx_buff.in_frame = FALSE;
	self->rx_buff.state = OUTSIDE_FRAME;
	self->tx_buff.data = self->tx_buff.head;
	self->rx_buff.data = self->rx_buff.head;

	smsc_ircc_setup_io(self, fir_base, sir_base, dma, irq);
	smsc_ircc_setup_qos(self);
	smsc_ircc_init_chip(self);

	if (ircc_transceiver > 0  &&
	    ircc_transceiver < SMSC_IRCC2_C_NUMBER_OF_TRANSCEIVERS)
		self->transceiver = ircc_transceiver;
	else
		smsc_ircc_probe_transceiver(self);

	err = register_netdev(self->netdev);
	if (err) {
		IRDA_ERROR("%s, Network device registration failed!\n",
			   driver_name);
		goto err_out4;
	}

	self->pldev = platform_device_register_simple(SMSC_IRCC2_DRIVER_NAME,
						      dev_count, NULL, 0);
	if (IS_ERR(self->pldev)) {
		err = PTR_ERR(self->pldev);
		goto err_out5;
	}
	platform_set_drvdata(self->pldev, self);

	IRDA_MESSAGE("IrDA: Registered device %s\n", dev->name);
	dev_count++;

	return 0;

 err_out5:
	unregister_netdev(self->netdev);

 err_out4:
	dma_free_coherent(NULL, self->tx_buff.truesize,
			  self->tx_buff.head, self->tx_buff_dma);
 err_out3:
	dma_free_coherent(NULL, self->rx_buff.truesize,
			  self->rx_buff.head, self->rx_buff_dma);
 err_out2:
	free_netdev(self->netdev);
	dev_self[dev_count] = NULL;
 err_out1:
	release_region(fir_base, SMSC_IRCC2_FIR_CHIP_IO_EXTENT);
	release_region(sir_base, SMSC_IRCC2_SIR_CHIP_IO_EXTENT);
 err_out:
	return err;
}

/*
 * Function smsc_ircc_present(fir_base, sir_base)
 *
 *    Check the smsc-ircc chip presence
 *
 */
static int smsc_ircc_present(unsigned int fir_base, unsigned int sir_base)
{
	unsigned char low, high, chip, config, dma, irq, version;

	if (!request_region(fir_base, SMSC_IRCC2_FIR_CHIP_IO_EXTENT,
			    driver_name)) {
		IRDA_WARNING("%s: can't get fir_base of 0x%03x\n",
			     __func__, fir_base);
		goto out1;
	}

	if (!request_region(sir_base, SMSC_IRCC2_SIR_CHIP_IO_EXTENT,
			    driver_name)) {
		IRDA_WARNING("%s: can't get sir_base of 0x%03x\n",
			     __func__, sir_base);
		goto out2;
	}

	register_bank(fir_base, 3);

	high    = inb(fir_base + IRCC_ID_HIGH);
	low     = inb(fir_base + IRCC_ID_LOW);
	chip    = inb(fir_base + IRCC_CHIP_ID);
	version = inb(fir_base + IRCC_VERSION);
	config  = inb(fir_base + IRCC_INTERFACE);
	dma     = config & IRCC_INTERFACE_DMA_MASK;
	irq     = (config & IRCC_INTERFACE_IRQ_MASK) >> 4;

	if (high != 0x10 || low != 0xb8 || (chip != 0xf1 && chip != 0xf2)) {
		IRDA_WARNING("%s(), addr 0x%04x - no device found!\n",
			     __func__, fir_base);
		goto out3;
	}
	IRDA_MESSAGE("SMsC IrDA Controller found\n IrCC version %d.%d, "
		     "firport 0x%03x, sirport 0x%03x dma=%d, irq=%d\n",
		     chip & 0x0f, version, fir_base, sir_base, dma, irq);

	return 0;

 out3:
	release_region(sir_base, SMSC_IRCC2_SIR_CHIP_IO_EXTENT);
 out2:
	release_region(fir_base, SMSC_IRCC2_FIR_CHIP_IO_EXTENT);
 out1:
	return -ENODEV;
}

/*
 * Function smsc_ircc_setup_io(self, fir_base, sir_base, dma, irq)
 *
 *    Setup I/O
 *
 */
static void smsc_ircc_setup_io(struct smsc_ircc_cb *self,
			       unsigned int fir_base, unsigned int sir_base,
			       u8 dma, u8 irq)
{
	unsigned char config, chip_dma, chip_irq;

	register_bank(fir_base, 3);
	config = inb(fir_base + IRCC_INTERFACE);
	chip_dma = config & IRCC_INTERFACE_DMA_MASK;
	chip_irq = (config & IRCC_INTERFACE_IRQ_MASK) >> 4;

	self->io.fir_base  = fir_base;
	self->io.sir_base  = sir_base;
	self->io.fir_ext   = SMSC_IRCC2_FIR_CHIP_IO_EXTENT;
	self->io.sir_ext   = SMSC_IRCC2_SIR_CHIP_IO_EXTENT;
	self->io.fifo_size = SMSC_IRCC2_FIFO_SIZE;
	self->io.speed = SMSC_IRCC2_C_IRDA_FALLBACK_SPEED;

	if (irq != IRQ_INVAL) {
		if (irq != chip_irq)
			IRDA_MESSAGE("%s, Overriding IRQ - chip says %d, using %d\n",
				     driver_name, chip_irq, irq);
		self->io.irq = irq;
	} else
		self->io.irq = chip_irq;

	if (dma != DMA_INVAL) {
		if (dma != chip_dma)
			IRDA_MESSAGE("%s, Overriding DMA - chip says %d, using %d\n",
				     driver_name, chip_dma, dma);
		self->io.dma = dma;
	} else
		self->io.dma = chip_dma;

}

/*
 * Function smsc_ircc_setup_qos(self)
 *
 *    Setup qos
 *
 */
static void smsc_ircc_setup_qos(struct smsc_ircc_cb *self)
{
	/* Initialize QoS for this device */
	irda_init_max_qos_capabilies(&self->qos);

	self->qos.baud_rate.bits = IR_9600|IR_19200|IR_38400|IR_57600|
		IR_115200|IR_576000|IR_1152000|(IR_4000000 << 8);

	self->qos.min_turn_time.bits = SMSC_IRCC2_MIN_TURN_TIME;
	self->qos.window_size.bits = SMSC_IRCC2_WINDOW_SIZE;
	irda_qos_bits_to_value(&self->qos);
}

/*
 * Function smsc_ircc_init_chip(self)
 *
 *    Init chip
 *
 */
static void smsc_ircc_init_chip(struct smsc_ircc_cb *self)
{
	int iobase = self->io.fir_base;

	register_bank(iobase, 0);
	outb(IRCC_MASTER_RESET, iobase + IRCC_MASTER);
	outb(0x00, iobase + IRCC_MASTER);

	register_bank(iobase, 1);
	outb(((inb(iobase + IRCC_SCE_CFGA) & 0x87) | IRCC_CFGA_IRDA_SIR_A),
	     iobase + IRCC_SCE_CFGA);

#ifdef smsc_669 /* Uses pin 88/89 for Rx/Tx */
	outb(((inb(iobase + IRCC_SCE_CFGB) & 0x3f) | IRCC_CFGB_MUX_COM),
	     iobase + IRCC_SCE_CFGB);
#else
	outb(((inb(iobase + IRCC_SCE_CFGB) & 0x3f) | IRCC_CFGB_MUX_IR),
	     iobase + IRCC_SCE_CFGB);
#endif
	(void) inb(iobase + IRCC_FIFO_THRESHOLD);
	outb(SMSC_IRCC2_FIFO_THRESHOLD, iobase + IRCC_FIFO_THRESHOLD);

	register_bank(iobase, 4);
	outb((inb(iobase + IRCC_CONTROL) & 0x30), iobase + IRCC_CONTROL);

	register_bank(iobase, 0);
	outb(0, iobase + IRCC_LCR_A);

	smsc_ircc_set_sir_speed(self, SMSC_IRCC2_C_IRDA_FALLBACK_SPEED);

	/* Power on device */
	outb(0x00, iobase + IRCC_MASTER);
}

/*
 * Function smsc_ircc_net_ioctl (dev, rq, cmd)
 *
 *    Process IOCTL commands for this device
 *
 */
static int smsc_ircc_net_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	struct if_irda_req *irq = (struct if_irda_req *) rq;
	struct smsc_ircc_cb *self;
	unsigned long flags;
	int ret = 0;

	IRDA_ASSERT(dev != NULL, return -1;);

	self = netdev_priv(dev);

	IRDA_ASSERT(self != NULL, return -1;);

	IRDA_DEBUG(2, "%s(), %s, (cmd=0x%X)\n", __func__, dev->name, cmd);

	switch (cmd) {
	case SIOCSBANDWIDTH: /* Set bandwidth */
		if (!capable(CAP_NET_ADMIN))
			ret = -EPERM;
                else {
			/* Make sure we are the only one touching
			 * self->io.speed and the hardware - Jean II */
			spin_lock_irqsave(&self->lock, flags);
			smsc_ircc_change_speed(self, irq->ifr_baudrate);
			spin_unlock_irqrestore(&self->lock, flags);
		}
		break;
	case SIOCSMEDIABUSY: /* Set media busy */
		if (!capable(CAP_NET_ADMIN)) {
			ret = -EPERM;
			break;
		}

		irda_device_set_media_busy(self->netdev, TRUE);
		break;
	case SIOCGRECEIVING: /* Check if we are receiving right now */
		irq->ifr_receiving = smsc_ircc_is_receiving(self);
		break;
	#if 0
	case SIOCSDTRRTS:
		if (!capable(CAP_NET_ADMIN)) {
			ret = -EPERM;
			break;
		}
		smsc_ircc_sir_set_dtr_rts(dev, irq->ifr_dtr, irq->ifr_rts);
		break;
	#endif
	default:
		ret = -EOPNOTSUPP;
	}

	return ret;
}

#if SMSC_IRCC2_C_NET_TIMEOUT
/*
 * Function smsc_ircc_timeout (struct net_device *dev)
 *
 *    The networking timeout management.
 *
 */

static void smsc_ircc_timeout(struct net_device *dev)
{
	struct smsc_ircc_cb *self = netdev_priv(dev);
	unsigned long flags;

	IRDA_WARNING("%s: transmit timed out, changing speed to: %d\n",
		     dev->name, self->io.speed);
	spin_lock_irqsave(&self->lock, flags);
	smsc_ircc_sir_start(self);
	smsc_ircc_change_speed(self, self->io.speed);
	dev->trans_start = jiffies; /* prevent tx timeout */
	netif_wake_queue(dev);
	spin_unlock_irqrestore(&self->lock, flags);
}
#endif

/*
 * Function smsc_ircc_hard_xmit_sir (struct sk_buff *skb, struct net_device *dev)
 *
 *    Transmits the current frame until FIFO is full, then
 *    waits until the next transmit interrupt, and continues until the
 *    frame is transmitted.
 */
static netdev_tx_t smsc_ircc_hard_xmit_sir(struct sk_buff *skb,
						 struct net_device *dev)
{
	struct smsc_ircc_cb *self;
	unsigned long flags;
	s32 speed;

	IRDA_DEBUG(1, "%s\n", __func__);

	IRDA_ASSERT(dev != NULL, return NETDEV_TX_OK;);

	self = netdev_priv(dev);
	IRDA_ASSERT(self != NULL, return NETDEV_TX_OK;);

	netif_stop_queue(dev);

	/* Make sure test of self->io.speed & speed change are atomic */
	spin_lock_irqsave(&self->lock, flags);

	/* Check if we need to change the speed */
	speed = irda_get_next_speed(skb);
	if (speed != self->io.speed && speed != -1) {
		/* Check for empty frame */
		if (!skb->len) {
			/*
			 * We send frames one by one in SIR mode (no
			 * pipelining), so at this point, if we were sending
			 * a previous frame, we just received the interrupt
			 * telling us it is finished (UART_IIR_THRI).
			 * Therefore, waiting for the transmitter to really
			 * finish draining the fifo won't take too long.
			 * And the interrupt handler is not expected to run.
			 * - Jean II */
			smsc_ircc_sir_wait_hw_transmitter_finish(self);
			smsc_ircc_change_speed(self, speed);
			spin_unlock_irqrestore(&self->lock, flags);
			dev_kfree_skb(skb);
			return NETDEV_TX_OK;
		}
		self->new_speed = speed;
	}

	/* Init tx buffer */
	self->tx_buff.data = self->tx_buff.head;

	/* Copy skb to tx_buff while wrapping, stuffing and making CRC */
	self->tx_buff.len = async_wrap_skb(skb, self->tx_buff.data,
					   self->tx_buff.truesize);

	dev->stats.tx_bytes += self->tx_buff.len;

	/* Turn on transmit finished interrupt. Will fire immediately!  */
	outb(UART_IER_THRI, self->io.sir_base + UART_IER);

	spin_unlock_irqrestore(&self->lock, flags);

	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

/*
 * Function smsc_ircc_set_fir_speed (self, baud)
 *
 *    Change the speed of the device
 *
 */
static void smsc_ircc_set_fir_speed(struct smsc_ircc_cb *self, u32 speed)
{
	int fir_base, ir_mode, ctrl, fast;

	IRDA_ASSERT(self != NULL, return;);
	fir_base = self->io.fir_base;

	self->io.speed = speed;

	switch (speed) {
	default:
	case 576000:
		ir_mode = IRCC_CFGA_IRDA_HDLC;
		ctrl = IRCC_CRC;
		fast = 0;
		IRDA_DEBUG(0, "%s(), handling baud of 576000\n", __func__);
		break;
	case 1152000:
		ir_mode = IRCC_CFGA_IRDA_HDLC;
		ctrl = IRCC_1152 | IRCC_CRC;
		fast = IRCC_LCR_A_FAST | IRCC_LCR_A_GP_DATA;
		IRDA_DEBUG(0, "%s(), handling baud of 1152000\n",
			   __func__);
		break;
	case 4000000:
		ir_mode = IRCC_CFGA_IRDA_4PPM;
		ctrl = IRCC_CRC;
		fast = IRCC_LCR_A_FAST;
		IRDA_DEBUG(0, "%s(), handling baud of 4000000\n",
			   __func__);
		break;
	}
	#if 0
	Now in tranceiver!
	/* This causes an interrupt */
	register_bank(fir_base, 0);
	outb((inb(fir_base + IRCC_LCR_A) &  0xbf) | fast, fir_base + IRCC_LCR_A);
	#endif

	register_bank(fir_base, 1);
	outb(((inb(fir_base + IRCC_SCE_CFGA) & IRCC_SCE_CFGA_BLOCK_CTRL_BITS_MASK) | ir_mode), fir_base + IRCC_SCE_CFGA);

	register_bank(fir_base, 4);
	outb((inb(fir_base + IRCC_CONTROL) & 0x30) | ctrl, fir_base + IRCC_CONTROL);
}

/*
 * Function smsc_ircc_fir_start(self)
 *
 *    Change the speed of the device
 *
 */
static void smsc_ircc_fir_start(struct smsc_ircc_cb *self)
{
	struct net_device *dev;
	int fir_base;

	IRDA_DEBUG(1, "%s\n", __func__);

	IRDA_ASSERT(self != NULL, return;);
	dev = self->netdev;
	IRDA_ASSERT(dev != NULL, return;);

	fir_base = self->io.fir_base;

	/* Reset everything */

	/* Clear FIFO */
	outb(inb(fir_base + IRCC_LCR_A) | IRCC_LCR_A_FIFO_RESET, fir_base + IRCC_LCR_A);

	/* Enable interrupt */
	/*outb(IRCC_IER_ACTIVE_FRAME|IRCC_IER_EOM, fir_base + IRCC_IER);*/

	register_bank(fir_base, 1);

	/* Select the TX/RX interface */
#ifdef SMSC_669 /* Uses pin 88/89 for Rx/Tx */
	outb(((inb(fir_base + IRCC_SCE_CFGB) & 0x3f) | IRCC_CFGB_MUX_COM),
	     fir_base + IRCC_SCE_CFGB);
#else
	outb(((inb(fir_base + IRCC_SCE_CFGB) & 0x3f) | IRCC_CFGB_MUX_IR),
	     fir_base + IRCC_SCE_CFGB);
#endif
	(void) inb(fir_base + IRCC_FIFO_THRESHOLD);

	/* Enable SCE interrupts */
	outb(0, fir_base + IRCC_MASTER);
	register_bank(fir_base, 0);
	outb(IRCC_IER_ACTIVE_FRAME | IRCC_IER_EOM, fir_base + IRCC_IER);
	outb(IRCC_MASTER_INT_EN, fir_base + IRCC_MASTER);
}

/*
 * Function smsc_ircc_fir_stop(self, baud)
 *
 *    Change the speed of the device
 *
 */
static void smsc_ircc_fir_stop(struct smsc_ircc_cb *self)
{
	int fir_base;

	IRDA_DEBUG(1, "%s\n", __func__);

	IRDA_ASSERT(self != NULL, return;);

	fir_base = self->io.fir_base;
	register_bank(fir_base, 0);
	/*outb(IRCC_MASTER_RESET, fir_base + IRCC_MASTER);*/
	outb(inb(fir_base + IRCC_LCR_B) & IRCC_LCR_B_SIP_ENABLE, fir_base + IRCC_LCR_B);
}


/*
 * Function smsc_ircc_change_speed(self, baud)
 *
 *    Change the speed of the device
 *
 * This function *must* be called with spinlock held, because it may
 * be called from the irq handler. - Jean II
 */
static void smsc_ircc_change_speed(struct smsc_ircc_cb *self, u32 speed)
{
	struct net_device *dev;
	int last_speed_was_sir;

	IRDA_DEBUG(0, "%s() changing speed to: %d\n", __func__, speed);

	IRDA_ASSERT(self != NULL, return;);
	dev = self->netdev;

	last_speed_was_sir = self->io.speed <= SMSC_IRCC2_MAX_SIR_SPEED;

	#if 0
	/* Temp Hack */
	speed= 1152000;
	self->io.speed = speed;
	last_speed_was_sir = 0;
	smsc_ircc_fir_start(self);
	#endif

	if (self->io.speed == 0)
		smsc_ircc_sir_start(self);

	#if 0
	if (!last_speed_was_sir) speed = self->io.speed;
	#endif

	if (self->io.speed != speed)
		smsc_ircc_set_transceiver_for_speed(self, speed);

	self->io.speed = speed;

	if (speed <= SMSC_IRCC2_MAX_SIR_SPEED) {
		if (!last_speed_was_sir) {
			smsc_ircc_fir_stop(self);
			smsc_ircc_sir_start(self);
		}
		smsc_ircc_set_sir_speed(self, speed);
	} else {
		if (last_speed_was_sir) {
			#if SMSC_IRCC2_C_SIR_STOP
			smsc_ircc_sir_stop(self);
			#endif
			smsc_ircc_fir_start(self);
		}
		smsc_ircc_set_fir_speed(self, speed);

		#if 0
		self->tx_buff.len = 10;
		self->tx_buff.data = self->tx_buff.head;

		smsc_ircc_dma_xmit(self, 4000);
		#endif
		/* Be ready for incoming frames */
		smsc_ircc_dma_receive(self);
	}

	netif_wake_queue(dev);
}

/*
 * Function smsc_ircc_set_sir_speed (self, speed)
 *
 *    Set speed of IrDA port to specified baudrate
 *
 */
static void smsc_ircc_set_sir_speed(struct smsc_ircc_cb *self, __u32 speed)
{
	int iobase;
	int fcr;    /* FIFO control reg */
	int lcr;    /* Line control reg */
	int divisor;

	IRDA_DEBUG(0, "%s(), Setting speed to: %d\n", __func__, speed);

	IRDA_ASSERT(self != NULL, return;);
	iobase = self->io.sir_base;

	/* Update accounting for new speed */
	self->io.speed = speed;

	/* Turn off interrupts */
	outb(0, iobase + UART_IER);

	divisor = SMSC_IRCC2_MAX_SIR_SPEED / speed;

	fcr = UART_FCR_ENABLE_FIFO;

	/*
	 * Use trigger level 1 to avoid 3 ms. timeout delay at 9600 bps, and
	 * almost 1,7 ms at 19200 bps. At speeds above that we can just forget
	 * about this timeout since it will always be fast enough.
	 */
	fcr |= self->io.speed < 38400 ?
		UART_FCR_TRIGGER_1 : UART_FCR_TRIGGER_14;

	/* IrDA ports use 8N1 */
	lcr = UART_LCR_WLEN8;

	outb(UART_LCR_DLAB | lcr, iobase + UART_LCR); /* Set DLAB */
	outb(divisor & 0xff,      iobase + UART_DLL); /* Set speed */
	outb(divisor >> 8,	  iobase + UART_DLM);
	outb(lcr,		  iobase + UART_LCR); /* Set 8N1 */
	outb(fcr,		  iobase + UART_FCR); /* Enable FIFO's */

	/* Turn on interrups */
	outb(UART_IER_RLSI | UART_IER_RDI | UART_IER_THRI, iobase + UART_IER);

	IRDA_DEBUG(2, "%s() speed changed to: %d\n", __func__, speed);
}


/*
 * Function smsc_ircc_hard_xmit_fir (skb, dev)
 *
 *    Transmit the frame!
 *
 */
static netdev_tx_t smsc_ircc_hard_xmit_fir(struct sk_buff *skb,
						 struct net_device *dev)
{
	struct smsc_ircc_cb *self;
	unsigned long flags;
	s32 speed;
	int mtt;

	IRDA_ASSERT(dev != NULL, return NETDEV_TX_OK;);
	self = netdev_priv(dev);
	IRDA_ASSERT(self != NULL, return NETDEV_TX_OK;);

	netif_stop_queue(dev);

	/* Make sure test of self->io.speed & speed change are atomic */
	spin_lock_irqsave(&self->lock, flags);

	/* Check if we need to change the speed after this frame */
	speed = irda_get_next_speed(skb);
	if (speed != self->io.speed && speed != -1) {
		/* Check for empty frame */
		if (!skb->len) {
			/* Note : you should make sure that speed changes
			 * are not going to corrupt any outgoing frame.
			 * Look at nsc-ircc for the gory details - Jean II */
			smsc_ircc_change_speed(self, speed);
			spin_unlock_irqrestore(&self->lock, flags);
			dev_kfree_skb(skb);
			return NETDEV_TX_OK;
		}

		self->new_speed = speed;
	}

	skb_copy_from_linear_data(skb, self->tx_buff.head, skb->len);

	self->tx_buff.len = skb->len;
	self->tx_buff.data = self->tx_buff.head;

	mtt = irda_get_mtt(skb);
	if (mtt) {
		int bofs;

		/*
		 * Compute how many BOFs (STA or PA's) we need to waste the
		 * min turn time given the speed of the link.
		 */
		bofs = mtt * (self->io.speed / 1000) / 8000;
		if (bofs > 4095)
			bofs = 4095;

		smsc_ircc_dma_xmit(self, bofs);
	} else {
		/* Transmit frame */
		smsc_ircc_dma_xmit(self, 0);
	}

	spin_unlock_irqrestore(&self->lock, flags);
	dev_kfree_skb(skb);

	return NETDEV_TX_OK;
}

/*
 * Function smsc_ircc_dma_xmit (self, bofs)
 *
 *    Transmit data using DMA
 *
 */
static void smsc_ircc_dma_xmit(struct smsc_ircc_cb *self, int bofs)
{
	int iobase = self->io.fir_base;
	u8 ctrl;

	IRDA_DEBUG(3, "%s\n", __func__);
#if 1
	/* Disable Rx */
	register_bank(iobase, 0);
	outb(0x00, iobase + IRCC_LCR_B);
#endif
	register_bank(iobase, 1);
	outb(inb(iobase + IRCC_SCE_CFGB) & ~IRCC_CFGB_DMA_ENABLE,
	     iobase + IRCC_SCE_CFGB);

	self->io.direction = IO_XMIT;

	/* Set BOF additional count for generating the min turn time */
	register_bank(iobase, 4);
	outb(bofs & 0xff, iobase + IRCC_BOF_COUNT_LO);
	ctrl = inb(iobase + IRCC_CONTROL) & 0xf0;
	outb(ctrl | ((bofs >> 8) & 0x0f), iobase + IRCC_BOF_COUNT_HI);

	/* Set max Tx frame size */
	outb(self->tx_buff.len >> 8, iobase + IRCC_TX_SIZE_HI);
	outb(self->tx_buff.len & 0xff, iobase + IRCC_TX_SIZE_LO);

	/*outb(UART_MCR_OUT2, self->io.sir_base + UART_MCR);*/

	/* Enable burst mode chip Tx DMA */
	register_bank(iobase, 1);
	outb(inb(iobase + IRCC_SCE_CFGB) | IRCC_CFGB_DMA_ENABLE |
	     IRCC_CFGB_DMA_BURST, iobase + IRCC_SCE_CFGB);

	/* Setup DMA controller (must be done after enabling chip DMA) */
	irda_setup_dma(self->io.dma, self->tx_buff_dma, self->tx_buff.len,
		       DMA_TX_MODE);

	/* Enable interrupt */

	register_bank(iobase, 0);
	outb(IRCC_IER_ACTIVE_FRAME | IRCC_IER_EOM, iobase + IRCC_IER);
	outb(IRCC_MASTER_INT_EN, iobase + IRCC_MASTER);

	/* Enable transmit */
	outb(IRCC_LCR_B_SCE_TRANSMIT | IRCC_LCR_B_SIP_ENABLE, iobase + IRCC_LCR_B);
}

/*
 * Function smsc_ircc_dma_xmit_complete (self)
 *
 *    The transfer of a frame in finished. This function will only be called
 *    by the interrupt handler
 *
 */
static void smsc_ircc_dma_xmit_complete(struct smsc_ircc_cb *self)
{
	int iobase = self->io.fir_base;

	IRDA_DEBUG(3, "%s\n", __func__);
#if 0
	/* Disable Tx */
	register_bank(iobase, 0);
	outb(0x00, iobase + IRCC_LCR_B);
#endif
	register_bank(iobase, 1);
	outb(inb(iobase + IRCC_SCE_CFGB) & ~IRCC_CFGB_DMA_ENABLE,
	     iobase + IRCC_SCE_CFGB);

	/* Check for underrun! */
	register_bank(iobase, 0);
	if (inb(iobase + IRCC_LSR) & IRCC_LSR_UNDERRUN) {
		self->netdev->stats.tx_errors++;
		self->netdev->stats.tx_fifo_errors++;

		/* Reset error condition */
		register_bank(iobase, 0);
		outb(IRCC_MASTER_ERROR_RESET, iobase + IRCC_MASTER);
		outb(0x00, iobase + IRCC_MASTER);
	} else {
		self->netdev->stats.tx_packets++;
		self->netdev->stats.tx_bytes += self->tx_buff.len;
	}

	/* Check if it's time to change the speed */
	if (self->new_speed) {
		smsc_ircc_change_speed(self, self->new_speed);
		self->new_speed = 0;
	}

	netif_wake_queue(self->netdev);
}

/*
 * Function smsc_ircc_dma_receive(self)
 *
 *    Get ready for receiving a frame. The device will initiate a DMA
 *    if it starts to receive a frame.
 *
 */
static int smsc_ircc_dma_receive(struct smsc_ircc_cb *self)
{
	int iobase = self->io.fir_base;
#if 0
	/* Turn off chip DMA */
	register_bank(iobase, 1);
	outb(inb(iobase + IRCC_SCE_CFGB) & ~IRCC_CFGB_DMA_ENABLE,
	     iobase + IRCC_SCE_CFGB);
#endif

	/* Disable Tx */
	register_bank(iobase, 0);
	outb(0x00, iobase + IRCC_LCR_B);

	/* Turn off chip DMA */
	register_bank(iobase, 1);
	outb(inb(iobase + IRCC_SCE_CFGB) & ~IRCC_CFGB_DMA_ENABLE,
	     iobase + IRCC_SCE_CFGB);

	self->io.direction = IO_RECV;
	self->rx_buff.data = self->rx_buff.head;

	/* Set max Rx frame size */
	register_bank(iobase, 4);
	outb((2050 >> 8) & 0x0f, iobase + IRCC_RX_SIZE_HI);
	outb(2050 & 0xff, iobase + IRCC_RX_SIZE_LO);

	/* Setup DMA controller */
	irda_setup_dma(self->io.dma, self->rx_buff_dma, self->rx_buff.truesize,
		       DMA_RX_MODE);

	/* Enable burst mode chip Rx DMA */
	register_bank(iobase, 1);
	outb(inb(iobase + IRCC_SCE_CFGB) | IRCC_CFGB_DMA_ENABLE |
	     IRCC_CFGB_DMA_BURST, iobase + IRCC_SCE_CFGB);

	/* Enable interrupt */
	register_bank(iobase, 0);
	outb(IRCC_IER_ACTIVE_FRAME | IRCC_IER_EOM, iobase + IRCC_IER);
	outb(IRCC_MASTER_INT_EN, iobase + IRCC_MASTER);

	/* Enable receiver */
	register_bank(iobase, 0);
	outb(IRCC_LCR_B_SCE_RECEIVE | IRCC_LCR_B_SIP_ENABLE,
	     iobase + IRCC_LCR_B);

	return 0;
}

/*
 * Function smsc_ircc_dma_receive_complete(self)
 *
 *    Finished with receiving frames
 *
 */
static void smsc_ircc_dma_receive_complete(struct smsc_ircc_cb *self)
{
	struct sk_buff *skb;
	int len, msgcnt, lsr;
	int iobase = self->io.fir_base;

	register_bank(iobase, 0);

	IRDA_DEBUG(3, "%s\n", __func__);
#if 0
	/* Disable Rx */
	register_bank(iobase, 0);
	outb(0x00, iobase + IRCC_LCR_B);
#endif
	register_bank(iobase, 0);
	outb(inb(iobase + IRCC_LSAR) & ~IRCC_LSAR_ADDRESS_MASK, iobase + IRCC_LSAR);
	lsr= inb(iobase + IRCC_LSR);
	msgcnt = inb(iobase + IRCC_LCR_B) & 0x08;

	IRDA_DEBUG(2, "%s: dma count = %d\n", __func__,
		   get_dma_residue(self->io.dma));

	len = self->rx_buff.truesize - get_dma_residue(self->io.dma);

	/* Look for errors */
	if (lsr & (IRCC_LSR_FRAME_ERROR | IRCC_LSR_CRC_ERROR | IRCC_LSR_SIZE_ERROR)) {
		self->netdev->stats.rx_errors++;
		if (lsr & IRCC_LSR_FRAME_ERROR)
			self->netdev->stats.rx_frame_errors++;
		if (lsr & IRCC_LSR_CRC_ERROR)
			self->netdev->stats.rx_crc_errors++;
		if (lsr & IRCC_LSR_SIZE_ERROR)
			self->netdev->stats.rx_length_errors++;
		if (lsr & (IRCC_LSR_UNDERRUN | IRCC_LSR_OVERRUN))
			self->netdev->stats.rx_length_errors++;
		return;
	}

	/* Remove CRC */
	len -= self->io.speed < 4000000 ? 2 : 4;

	if (len < 2 || len > 2050) {
		IRDA_WARNING("%s(), bogus len=%d\n", __func__, len);
		return;
	}
	IRDA_DEBUG(2, "%s: msgcnt = %d, len=%d\n", __func__, msgcnt, len);

	skb = dev_alloc_skb(len + 1);
	if (!skb) {
		IRDA_WARNING("%s(), memory squeeze, dropping frame.\n",
			     __func__);
		return;
	}
	/* Make sure IP header gets aligned */
	skb_reserve(skb, 1);

	memcpy(skb_put(skb, len), self->rx_buff.data, len);
	self->netdev->stats.rx_packets++;
	self->netdev->stats.rx_bytes += len;

	skb->dev = self->netdev;
	skb_reset_mac_header(skb);
	skb->protocol = htons(ETH_P_IRDA);
	netif_rx(skb);
}

/*
 * Function smsc_ircc_sir_receive (self)
 *
 *    Receive one frame from the infrared port
 *
 */
static void smsc_ircc_sir_receive(struct smsc_ircc_cb *self)
{
	int boguscount = 0;
	int iobase;

	IRDA_ASSERT(self != NULL, return;);

	iobase = self->io.sir_base;

	/*
	 * Receive all characters in Rx FIFO, unwrap and unstuff them.
         * async_unwrap_char will deliver all found frames
	 */
	do {
		async_unwrap_char(self->netdev, &self->netdev->stats, &self->rx_buff,
				  inb(iobase + UART_RX));

		/* Make sure we don't stay here to long */
		if (boguscount++ > 32) {
			IRDA_DEBUG(2, "%s(), breaking!\n", __func__);
			break;
		}
	} while (inb(iobase + UART_LSR) & UART_LSR_DR);
}


/*
 * Function smsc_ircc_interrupt (irq, dev_id, regs)
 *
 *    An interrupt from the chip has arrived. Time to do some work
 *
 */
static irqreturn_t smsc_ircc_interrupt(int dummy, void *dev_id)
{
	struct net_device *dev = dev_id;
	struct smsc_ircc_cb *self = netdev_priv(dev);
	int iobase, iir, lcra, lsr;
	irqreturn_t ret = IRQ_NONE;

	/* Serialise the interrupt handler in various CPUs, stop Tx path */
	spin_lock(&self->lock);

	/* Check if we should use the SIR interrupt handler */
	if (self->io.speed <= SMSC_IRCC2_MAX_SIR_SPEED) {
		ret = smsc_ircc_interrupt_sir(dev);
		goto irq_ret_unlock;
	}

	iobase = self->io.fir_base;

	register_bank(iobase, 0);
	iir = inb(iobase + IRCC_IIR);
	if (iir == 0)
		goto irq_ret_unlock;
	ret = IRQ_HANDLED;

	/* Disable interrupts */
	outb(0, iobase + IRCC_IER);
	lcra = inb(iobase + IRCC_LCR_A);
	lsr = inb(iobase + IRCC_LSR);

	IRDA_DEBUG(2, "%s(), iir = 0x%02x\n", __func__, iir);

	if (iir & IRCC_IIR_EOM) {
		if (self->io.direction == IO_RECV)
			smsc_ircc_dma_receive_complete(self);
		else
			smsc_ircc_dma_xmit_complete(self);

		smsc_ircc_dma_receive(self);
	}

	if (iir & IRCC_IIR_ACTIVE_FRAME) {
		/*printk(KERN_WARNING "%s(): Active Frame\n", __func__);*/
	}

	/* Enable interrupts again */

	register_bank(iobase, 0);
	outb(IRCC_IER_ACTIVE_FRAME | IRCC_IER_EOM, iobase + IRCC_IER);

 irq_ret_unlock:
	spin_unlock(&self->lock);

	return ret;
}

/*
 * Function irport_interrupt_sir (irq, dev_id)
 *
 *    Interrupt handler for SIR modes
 */
static irqreturn_t smsc_ircc_interrupt_sir(struct net_device *dev)
{
	struct smsc_ircc_cb *self = netdev_priv(dev);
	int boguscount = 0;
	int iobase;
	int iir, lsr;

	/* Already locked comming here in smsc_ircc_interrupt() */
	/*spin_lock(&self->lock);*/

	iobase = self->io.sir_base;

	iir = inb(iobase + UART_IIR) & UART_IIR_ID;
	if (iir == 0)
		return IRQ_NONE;
	while (iir) {
		/* Clear interrupt */
		lsr = inb(iobase + UART_LSR);

		IRDA_DEBUG(4, "%s(), iir=%02x, lsr=%02x, iobase=%#x\n",
			    __func__, iir, lsr, iobase);

		switch (iir) {
		case UART_IIR_RLSI:
			IRDA_DEBUG(2, "%s(), RLSI\n", __func__);
			break;
		case UART_IIR_RDI:
			/* Receive interrupt */
			smsc_ircc_sir_receive(self);
			break;
		case UART_IIR_THRI:
			if (lsr & UART_LSR_THRE)
				/* Transmitter ready for data */
				smsc_ircc_sir_write_wakeup(self);
			break;
		default:
			IRDA_DEBUG(0, "%s(), unhandled IIR=%#x\n",
				   __func__, iir);
			break;
		}

		/* Make sure we don't stay here to long */
		if (boguscount++ > 100)
			break;

	        iir = inb(iobase + UART_IIR) & UART_IIR_ID;
	}
	/*spin_unlock(&self->lock);*/
	return IRQ_HANDLED;
}


#if 0 /* unused */
/*
 * Function ircc_is_receiving (self)
 *
 *    Return TRUE is we are currently receiving a frame
 *
 */
static int ircc_is_receiving(struct smsc_ircc_cb *self)
{
	int status = FALSE;
	/* int iobase; */

	IRDA_DEBUG(1, "%s\n", __func__);

	IRDA_ASSERT(self != NULL, return FALSE;);

	IRDA_DEBUG(0, "%s: dma count = %d\n", __func__,
		   get_dma_residue(self->io.dma));

	status = (self->rx_buff.state != OUTSIDE_FRAME);

	return status;
}
#endif /* unused */

static int smsc_ircc_request_irq(struct smsc_ircc_cb *self)
{
	int error;

	error = request_irq(self->io.irq, smsc_ircc_interrupt, 0,
			    self->netdev->name, self->netdev);
	if (error)
		IRDA_DEBUG(0, "%s(), unable to allocate irq=%d, err=%d\n",
			   __func__, self->io.irq, error);

	return error;
}

static void smsc_ircc_start_interrupts(struct smsc_ircc_cb *self)
{
	unsigned long flags;

	spin_lock_irqsave(&self->lock, flags);

	self->io.speed = 0;
	smsc_ircc_change_speed(self, SMSC_IRCC2_C_IRDA_FALLBACK_SPEED);

	spin_unlock_irqrestore(&self->lock, flags);
}

static void smsc_ircc_stop_interrupts(struct smsc_ircc_cb *self)
{
	int iobase = self->io.fir_base;
	unsigned long flags;

	spin_lock_irqsave(&self->lock, flags);

	register_bank(iobase, 0);
	outb(0, iobase + IRCC_IER);
	outb(IRCC_MASTER_RESET, iobase + IRCC_MASTER);
	outb(0x00, iobase + IRCC_MASTER);

	spin_unlock_irqrestore(&self->lock, flags);
}


/*
 * Function smsc_ircc_net_open (dev)
 *
 *    Start the device
 *
 */
static int smsc_ircc_net_open(struct net_device *dev)
{
	struct smsc_ircc_cb *self;
	char hwname[16];

	IRDA_DEBUG(1, "%s\n", __func__);

	IRDA_ASSERT(dev != NULL, return -1;);
	self = netdev_priv(dev);
	IRDA_ASSERT(self != NULL, return 0;);

	if (self->io.suspended) {
		IRDA_DEBUG(0, "%s(), device is suspended\n", __func__);
		return -EAGAIN;
	}

	if (request_irq(self->io.irq, smsc_ircc_interrupt, 0, dev->name,
			(void *) dev)) {
		IRDA_DEBUG(0, "%s(), unable to allocate irq=%d\n",
			   __func__, self->io.irq);
		return -EAGAIN;
	}

	smsc_ircc_start_interrupts(self);

	/* Give self a hardware name */
	/* It would be cool to offer the chip revision here - Jean II */
	sprintf(hwname, "SMSC @ 0x%03x", self->io.fir_base);

	/*
	 * Open new IrLAP layer instance, now that everything should be
	 * initialized properly
	 */
	self->irlap = irlap_open(dev, &self->qos, hwname);

	/*
	 * Always allocate the DMA channel after the IRQ,
	 * and clean up on failure.
	 */
	if (request_dma(self->io.dma, dev->name)) {
		smsc_ircc_net_close(dev);

		IRDA_WARNING("%s(), unable to allocate DMA=%d\n",
			     __func__, self->io.dma);
		return -EAGAIN;
	}

	netif_start_queue(dev);

	return 0;
}

/*
 * Function smsc_ircc_net_close (dev)
 *
 *    Stop the device
 *
 */
static int smsc_ircc_net_close(struct net_device *dev)
{
	struct smsc_ircc_cb *self;

	IRDA_DEBUG(1, "%s\n", __func__);

	IRDA_ASSERT(dev != NULL, return -1;);
	self = netdev_priv(dev);
	IRDA_ASSERT(self != NULL, return 0;);

	/* Stop device */
	netif_stop_queue(dev);

	/* Stop and remove instance of IrLAP */
	if (self->irlap)
		irlap_close(self->irlap);
	self->irlap = NULL;

	smsc_ircc_stop_interrupts(self);

	/* if we are called from smsc_ircc_resume we don't have IRQ reserved */
	if (!self->io.suspended)
		free_irq(self->io.irq, dev);

	disable_dma(self->io.dma);
	free_dma(self->io.dma);

	return 0;
}

static int smsc_ircc_suspend(struct platform_device *dev, pm_message_t state)
{
	struct smsc_ircc_cb *self = platform_get_drvdata(dev);

	if (!self->io.suspended) {
		IRDA_DEBUG(1, "%s, Suspending\n", driver_name);

		rtnl_lock();
		if (netif_running(self->netdev)) {
			netif_device_detach(self->netdev);
			smsc_ircc_stop_interrupts(self);
			free_irq(self->io.irq, self->netdev);
			disable_dma(self->io.dma);
		}
		self->io.suspended = 1;
		rtnl_unlock();
	}

	return 0;
}

static int smsc_ircc_resume(struct platform_device *dev)
{
	struct smsc_ircc_cb *self = platform_get_drvdata(dev);

	if (self->io.suspended) {
		IRDA_DEBUG(1, "%s, Waking up\n", driver_name);

		rtnl_lock();
		smsc_ircc_init_chip(self);
		if (netif_running(self->netdev)) {
			if (smsc_ircc_request_irq(self)) {
				/*
				 * Don't fail resume process, just kill this
				 * network interface
				 */
				unregister_netdevice(self->netdev);
			} else {
				enable_dma(self->io.dma);
				smsc_ircc_start_interrupts(self);
				netif_device_attach(self->netdev);
			}
		}
		self->io.suspended = 0;
		rtnl_unlock();
	}
	return 0;
}

/*
 * Function smsc_ircc_close (self)
 *
 *    Close driver instance
 *
 */
static int __exit smsc_ircc_close(struct smsc_ircc_cb *self)
{
	IRDA_DEBUG(1, "%s\n", __func__);

	IRDA_ASSERT(self != NULL, return -1;);

	platform_device_unregister(self->pldev);

	/* Remove netdevice */
	unregister_netdev(self->netdev);

	smsc_ircc_stop_interrupts(self);

	/* Release the PORTS that this driver is using */
	IRDA_DEBUG(0, "%s(), releasing 0x%03x\n",  __func__,
		   self->io.fir_base);

	release_region(self->io.fir_base, self->io.fir_ext);

	IRDA_DEBUG(0, "%s(), releasing 0x%03x\n", __func__,
		   self->io.sir_base);

	release_region(self->io.sir_base, self->io.sir_ext);

	if (self->tx_buff.head)
		dma_free_coherent(NULL, self->tx_buff.truesize,
				  self->tx_buff.head, self->tx_buff_dma);

	if (self->rx_buff.head)
		dma_free_coherent(NULL, self->rx_buff.truesize,
				  self->rx_buff.head, self->rx_buff_dma);

	free_netdev(self->netdev);

	return 0;
}

static void __exit smsc_ircc_cleanup(void)
{
	int i;

	IRDA_DEBUG(1, "%s\n", __func__);

	for (i = 0; i < 2; i++) {
		if (dev_self[i])
			smsc_ircc_close(dev_self[i]);
	}

	if (pnp_driver_registered)
		pnp_unregister_driver(&smsc_ircc_pnp_driver);

	platform_driver_unregister(&smsc_ircc_driver);
}

/*
 *	Start SIR operations
 *
 * This function *must* be called with spinlock held, because it may
 * be called from the irq handler (via smsc_ircc_change_speed()). - Jean II
 */
static void smsc_ircc_sir_start(struct smsc_ircc_cb *self)
{
	struct net_device *dev;
	int fir_base, sir_base;

	IRDA_DEBUG(3, "%s\n", __func__);

	IRDA_ASSERT(self != NULL, return;);
	dev = self->netdev;
	IRDA_ASSERT(dev != NULL, return;);

	fir_base = self->io.fir_base;
	sir_base = self->io.sir_base;

	/* Reset everything */
	outb(IRCC_MASTER_RESET, fir_base + IRCC_MASTER);

	#if SMSC_IRCC2_C_SIR_STOP
	/*smsc_ircc_sir_stop(self);*/
	#endif

	register_bank(fir_base, 1);
	outb(((inb(fir_base + IRCC_SCE_CFGA) & IRCC_SCE_CFGA_BLOCK_CTRL_BITS_MASK) | IRCC_CFGA_IRDA_SIR_A), fir_base + IRCC_SCE_CFGA);

	/* Initialize UART */
	outb(UART_LCR_WLEN8, sir_base + UART_LCR);  /* Reset DLAB */
	outb((UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2), sir_base + UART_MCR);

	/* Turn on interrups */
	outb(UART_IER_RLSI | UART_IER_RDI |UART_IER_THRI, sir_base + UART_IER);

	IRDA_DEBUG(3, "%s() - exit\n", __func__);

	outb(0x00, fir_base + IRCC_MASTER);
}

#if SMSC_IRCC2_C_SIR_STOP
void smsc_ircc_sir_stop(struct smsc_ircc_cb *self)
{
	int iobase;

	IRDA_DEBUG(3, "%s\n", __func__);
	iobase = self->io.sir_base;

	/* Reset UART */
	outb(0, iobase + UART_MCR);

	/* Turn off interrupts */
	outb(0, iobase + UART_IER);
}
#endif

/*
 * Function smsc_sir_write_wakeup (self)
 *
 *    Called by the SIR interrupt handler when there's room for more data.
 *    If we have more packets to send, we send them here.
 *
 */
static void smsc_ircc_sir_write_wakeup(struct smsc_ircc_cb *self)
{
	int actual = 0;
	int iobase;
	int fcr;

	IRDA_ASSERT(self != NULL, return;);

	IRDA_DEBUG(4, "%s\n", __func__);

	iobase = self->io.sir_base;

	/* Finished with frame?  */
	if (self->tx_buff.len > 0)  {
		/* Write data left in transmit buffer */
		actual = smsc_ircc_sir_write(iobase, self->io.fifo_size,
				      self->tx_buff.data, self->tx_buff.len);
		self->tx_buff.data += actual;
		self->tx_buff.len  -= actual;
	} else {

	/*if (self->tx_buff.len ==0)  {*/

		/*
		 *  Now serial buffer is almost free & we can start
		 *  transmission of another packet. But first we must check
		 *  if we need to change the speed of the hardware
		 */
		if (self->new_speed) {
			IRDA_DEBUG(5, "%s(), Changing speed to %d.\n",
				   __func__, self->new_speed);
			smsc_ircc_sir_wait_hw_transmitter_finish(self);
			smsc_ircc_change_speed(self, self->new_speed);
			self->new_speed = 0;
		} else {
			/* Tell network layer that we want more frames */
			netif_wake_queue(self->netdev);
		}
		self->netdev->stats.tx_packets++;

		if (self->io.speed <= 115200) {
			/*
			 * Reset Rx FIFO to make sure that all reflected transmit data
			 * is discarded. This is needed for half duplex operation
			 */
			fcr = UART_FCR_ENABLE_FIFO | UART_FCR_CLEAR_RCVR;
			fcr |= self->io.speed < 38400 ?
					UART_FCR_TRIGGER_1 : UART_FCR_TRIGGER_14;

			outb(fcr, iobase + UART_FCR);

			/* Turn on receive interrupts */
			outb(UART_IER_RDI, iobase + UART_IER);
		}
	}
}

/*
 * Function smsc_ircc_sir_write (iobase, fifo_size, buf, len)
 *
 *    Fill Tx FIFO with transmit data
 *
 */
static int smsc_ircc_sir_write(int iobase, int fifo_size, __u8 *buf, int len)
{
	int actual = 0;

	/* Tx FIFO should be empty! */
	if (!(inb(iobase + UART_LSR) & UART_LSR_THRE)) {
		IRDA_WARNING("%s(), failed, fifo not empty!\n", __func__);
		return 0;
	}

	/* Fill FIFO with current frame */
	while (fifo_size-- > 0 && actual < len) {
		/* Transmit next byte */
		outb(buf[actual], iobase + UART_TX);
		actual++;
	}
	return actual;
}

/*
 * Function smsc_ircc_is_receiving (self)
 *
 *    Returns true is we are currently receiving data
 *
 */
static int smsc_ircc_is_receiving(struct smsc_ircc_cb *self)
{
	return self->rx_buff.state != OUTSIDE_FRAME;
}


/*
 * Function smsc_ircc_probe_transceiver(self)
 *
 *    Tries to find the used Transceiver
 *
 */
static void smsc_ircc_probe_transceiver(struct smsc_ircc_cb *self)
{
	unsigned int	i;

	IRDA_ASSERT(self != NULL, return;);

	for (i = 0; smsc_transceivers[i].name != NULL; i++)
		if (smsc_transceivers[i].probe(self->io.fir_base)) {
			IRDA_MESSAGE(" %s transceiver found\n",
				     smsc_transceivers[i].name);
			self->transceiver= i + 1;
			return;
		}

	IRDA_MESSAGE("No transceiver found. Defaulting to %s\n",
		     smsc_transceivers[SMSC_IRCC2_C_DEFAULT_TRANSCEIVER].name);

	self->transceiver = SMSC_IRCC2_C_DEFAULT_TRANSCEIVER;
}


/*
 * Function smsc_ircc_set_transceiver_for_speed(self, speed)
 *
 *    Set the transceiver according to the speed
 *
 */
static void smsc_ircc_set_transceiver_for_speed(struct smsc_ircc_cb *self, u32 speed)
{
	unsigned int trx;

	trx = self->transceiver;
	if (trx > 0)
		smsc_transceivers[trx - 1].set_for_speed(self->io.fir_base, speed);
}

/*
 * Function smsc_ircc_wait_hw_transmitter_finish ()
 *
 *    Wait for the real end of HW transmission
 *
 * The UART is a strict FIFO, and we get called only when we have finished
 * pushing data to the FIFO, so the maximum amount of time we must wait
 * is only for the FIFO to drain out.
 *
 * We use a simple calibrated loop. We may need to adjust the loop
 * delay (udelay) to balance I/O traffic and latency. And we also need to
 * adjust the maximum timeout.
 * It would probably be better to wait for the proper interrupt,
 * but it doesn't seem to be available.
 *
 * We can't use jiffies or kernel timers because :
 * 1) We are called from the interrupt handler, which disable softirqs,
 * so jiffies won't be increased
 * 2) Jiffies granularity is usually very coarse (10ms), and we don't
 * want to wait that long to detect stuck hardware.
 * Jean II
 */

static void smsc_ircc_sir_wait_hw_transmitter_finish(struct smsc_ircc_cb *self)
{
	int iobase = self->io.sir_base;
	int count = SMSC_IRCC2_HW_TRANSMITTER_TIMEOUT_US;

	/* Calibrated busy loop */
	while (count-- > 0 && !(inb(iobase + UART_LSR) & UART_LSR_TEMT))
		udelay(1);

	if (count < 0)
		IRDA_DEBUG(0, "%s(): stuck transmitter\n", __func__);
}


/* PROBING
 *
 * REVISIT we can be told about the device by PNP, and should use that info
 * instead of probing hardware and creating a platform_device ...
 */

static int __init smsc_ircc_look_for_chips(void)
{
	struct smsc_chip_address *address;
	char *type;
	unsigned int cfg_base, found;

	found = 0;
	address = possible_addresses;

	while (address->cfg_base) {
		cfg_base = address->cfg_base;

		/*printk(KERN_WARNING "%s(): probing: 0x%02x for: 0x%02x\n", __func__, cfg_base, address->type);*/

		if (address->type & SMSCSIO_TYPE_FDC) {
			type = "FDC";
			if (address->type & SMSCSIO_TYPE_FLAT)
				if (!smsc_superio_flat(fdc_chips_flat, cfg_base, type))
					found++;

			if (address->type & SMSCSIO_TYPE_PAGED)
				if (!smsc_superio_paged(fdc_chips_paged, cfg_base, type))
					found++;
		}
		if (address->type & SMSCSIO_TYPE_LPC) {
			type = "LPC";
			if (address->type & SMSCSIO_TYPE_FLAT)
				if (!smsc_superio_flat(lpc_chips_flat, cfg_base, type))
					found++;

			if (address->type & SMSCSIO_TYPE_PAGED)
				if (!smsc_superio_paged(lpc_chips_paged, cfg_base, type))
					found++;
		}
		address++;
	}
	return found;
}

/*
 * Function smsc_superio_flat (chip, base, type)
 *
 *    Try to get configuration of a smc SuperIO chip with flat register model
 *
 */
static int __init smsc_superio_flat(const struct smsc_chip *chips, unsigned short cfgbase, char *type)
{
	unsigned short firbase, sirbase;
	u8 mode, dma, irq;
	int ret = -ENODEV;

	IRDA_DEBUG(1, "%s\n", __func__);

	if (smsc_ircc_probe(cfgbase, SMSCSIOFLAT_DEVICEID_REG, chips, type) == NULL)
		return ret;

	outb(SMSCSIOFLAT_UARTMODE0C_REG, cfgbase);
	mode = inb(cfgbase + 1);

	/*printk(KERN_WARNING "%s(): mode: 0x%02x\n", __func__, mode);*/

	if (!(mode & SMSCSIOFLAT_UART2MODE_VAL_IRDA))
		IRDA_WARNING("%s(): IrDA not enabled\n", __func__);

	outb(SMSCSIOFLAT_UART2BASEADDR_REG, cfgbase);
	sirbase = inb(cfgbase + 1) << 2;

	/* FIR iobase */
	outb(SMSCSIOFLAT_FIRBASEADDR_REG, cfgbase);
	firbase = inb(cfgbase + 1) << 3;

	/* DMA */
	outb(SMSCSIOFLAT_FIRDMASELECT_REG, cfgbase);
	dma = inb(cfgbase + 1) & SMSCSIOFLAT_FIRDMASELECT_MASK;

	/* IRQ */
	outb(SMSCSIOFLAT_UARTIRQSELECT_REG, cfgbase);
	irq = inb(cfgbase + 1) & SMSCSIOFLAT_UART2IRQSELECT_MASK;

	IRDA_MESSAGE("%s(): fir: 0x%02x, sir: 0x%02x, dma: %02d, irq: %d, mode: 0x%02x\n", __func__, firbase, sirbase, dma, irq, mode);

	if (firbase && smsc_ircc_open(firbase, sirbase, dma, irq) == 0)
		ret = 0;

	/* Exit configuration */
	outb(SMSCSIO_CFGEXITKEY, cfgbase);

	return ret;
}

/*
 * Function smsc_superio_paged (chip, base, type)
 *
 *    Try  to get configuration of a smc SuperIO chip with paged register model
 *
 */
static int __init smsc_superio_paged(const struct smsc_chip *chips, unsigned short cfg_base, char *type)
{
	unsigned short fir_io, sir_io;
	int ret = -ENODEV;

	IRDA_DEBUG(1, "%s\n", __func__);

	if (smsc_ircc_probe(cfg_base, 0x20, chips, type) == NULL)
		return ret;

	/* Select logical device (UART2) */
	outb(0x07, cfg_base);
	outb(0x05, cfg_base + 1);

	/* SIR iobase */
	outb(0x60, cfg_base);
	sir_io = inb(cfg_base + 1) << 8;
	outb(0x61, cfg_base);
	sir_io |= inb(cfg_base + 1);

	/* Read FIR base */
	outb(0x62, cfg_base);
	fir_io = inb(cfg_base + 1) << 8;
	outb(0x63, cfg_base);
	fir_io |= inb(cfg_base + 1);
	outb(0x2b, cfg_base); /* ??? */

	if (fir_io && smsc_ircc_open(fir_io, sir_io, ircc_dma, ircc_irq) == 0)
		ret = 0;

	/* Exit configuration */
	outb(SMSCSIO_CFGEXITKEY, cfg_base);

	return ret;
}


static int __init smsc_access(unsigned short cfg_base, unsigned char reg)
{
	IRDA_DEBUG(1, "%s\n", __func__);

	outb(reg, cfg_base);
	return inb(cfg_base) != reg ? -1 : 0;
}

static const struct smsc_chip * __init smsc_ircc_probe(unsigned short cfg_base, u8 reg, const struct smsc_chip *chip, char *type)
{
	u8 devid, xdevid, rev;

	IRDA_DEBUG(1, "%s\n", __func__);

	/* Leave configuration */

	outb(SMSCSIO_CFGEXITKEY, cfg_base);

	if (inb(cfg_base) == SMSCSIO_CFGEXITKEY)	/* not a smc superio chip */
		return NULL;

	outb(reg, cfg_base);

	xdevid = inb(cfg_base + 1);

	/* Enter configuration */

	outb(SMSCSIO_CFGACCESSKEY, cfg_base);

	#if 0
	if (smsc_access(cfg_base,0x55))	/* send second key and check */
		return NULL;
	#endif

	/* probe device ID */

	if (smsc_access(cfg_base, reg))
		return NULL;

	devid = inb(cfg_base + 1);

	if (devid == 0 || devid == 0xff)	/* typical values for unused port */
		return NULL;

	/* probe revision ID */

	if (smsc_access(cfg_base, reg + 1))
		return NULL;

	rev = inb(cfg_base + 1);

	if (rev >= 128)			/* i think this will make no sense */
		return NULL;

	if (devid == xdevid)		/* protection against false positives */
		return NULL;

	/* Check for expected device ID; are there others? */

	while (chip->devid != devid) {

		chip++;

		if (chip->name == NULL)
			return NULL;
	}

	IRDA_MESSAGE("found SMC SuperIO Chip (devid=0x%02x rev=%02X base=0x%04x): %s%s\n",
		     devid, rev, cfg_base, type, chip->name);

	if (chip->rev > rev) {
		IRDA_MESSAGE("Revision higher than expected\n");
		return NULL;
	}

	if (chip->flags & NoIRDA)
		IRDA_MESSAGE("chipset does not support IRDA\n");

	return chip;
}

static int __init smsc_superio_fdc(unsigned short cfg_base)
{
	int ret = -1;

	if (!request_region(cfg_base, 2, driver_name)) {
		IRDA_WARNING("%s: can't get cfg_base of 0x%03x\n",
			     __func__, cfg_base);
	} else {
		if (!smsc_superio_flat(fdc_chips_flat, cfg_base, "FDC") ||
		    !smsc_superio_paged(fdc_chips_paged, cfg_base, "FDC"))
			ret =  0;

		release_region(cfg_base, 2);
	}

	return ret;
}

static int __init smsc_superio_lpc(unsigned short cfg_base)
{
	int ret = -1;

	if (!request_region(cfg_base, 2, driver_name)) {
		IRDA_WARNING("%s: can't get cfg_base of 0x%03x\n",
			     __func__, cfg_base);
	} else {
		if (!smsc_superio_flat(lpc_chips_flat, cfg_base, "LPC") ||
		    !smsc_superio_paged(lpc_chips_paged, cfg_base, "LPC"))
			ret = 0;

		release_region(cfg_base, 2);
	}
	return ret;
}

/*
 * Look for some specific subsystem setups that need
 * pre-configuration not properly done by the BIOS (especially laptops)
 * This code is based in part on smcinit.c, tosh1800-smcinit.c
 * and tosh2450-smcinit.c. The table lists the device entries
 * for ISA bridges with an LPC (Low Pin Count) controller which
 * handles the communication with the SMSC device. After the LPC
 * controller is initialized through PCI, the SMSC device is initialized
 * through a dedicated port in the ISA port-mapped I/O area, this latter
 * area is used to configure the SMSC device with default
 * SIR and FIR I/O ports, DMA and IRQ. Different vendors have
 * used different sets of parameters and different control port
 * addresses making a subsystem device table necessary.
 */
#ifdef CONFIG_PCI
#define PCIID_VENDOR_INTEL 0x8086
#define PCIID_VENDOR_ALI 0x10b9
static struct smsc_ircc_subsystem_configuration subsystem_configurations[] __initdata = {
	/*
	 * Subsystems needing entries:
	 * 0x10b9:0x1533 0x103c:0x0850 HP nx9010 family
	 * 0x10b9:0x1533 0x0e11:0x005a Compaq nc4000 family
	 * 0x8086:0x24cc 0x0e11:0x002a HP nx9000 family
	 */
	{
		/* Guessed entry */
		.vendor = PCIID_VENDOR_INTEL, /* Intel 82801DBM LPC bridge */
		.device = 0x24cc,
		.subvendor = 0x103c,
		.subdevice = 0x08bc,
		.sir_io = 0x02f8,
		.fir_io = 0x0130,
		.fir_irq = 0x05,
		.fir_dma = 0x03,
		.cfg_base = 0x004e,
		.preconfigure = preconfigure_through_82801,
		.name = "HP nx5000 family",
	},
	{
		.vendor = PCIID_VENDOR_INTEL, /* Intel 82801DBM LPC bridge */
		.device = 0x24cc,
		.subvendor = 0x103c,
		.subdevice = 0x088c,
		/* Quite certain these are the same for nc8000 as for nc6000 */
		.sir_io = 0x02f8,
		.fir_io = 0x0130,
		.fir_irq = 0x05,
		.fir_dma = 0x03,
		.cfg_base = 0x004e,
		.preconfigure = preconfigure_through_82801,
		.name = "HP nc8000 family",
	},
	{
		.vendor = PCIID_VENDOR_INTEL, /* Intel 82801DBM LPC bridge */
		.device = 0x24cc,
		.subvendor = 0x103c,
		.subdevice = 0x0890,
		.sir_io = 0x02f8,
		.fir_io = 0x0130,
		.fir_irq = 0x05,
		.fir_dma = 0x03,
		.cfg_base = 0x004e,
		.preconfigure = preconfigure_through_82801,
		.name = "HP nc6000 family",
	},
	{
		.vendor = PCIID_VENDOR_INTEL, /* Intel 82801DBM LPC bridge */
		.device = 0x24cc,
		.subvendor = 0x0e11,
		.subdevice = 0x0860,
		/* I assume these are the same for x1000 as for the others */
		.sir_io = 0x02e8,
		.fir_io = 0x02f8,
		.fir_irq = 0x07,
		.fir_dma = 0x03,
		.cfg_base = 0x002e,
		.preconfigure = preconfigure_through_82801,
		.name = "Compaq x1000 family",
	},
	{
		/* Intel 82801DB/DBL (ICH4/ICH4-L) LPC Interface Bridge */
		.vendor = PCIID_VENDOR_INTEL,
		.device = 0x24c0,
		.subvendor = 0x1179,
		.subdevice = 0xffff, /* 0xffff is "any" */
		.sir_io = 0x03f8,
		.fir_io = 0x0130,
		.fir_irq = 0x07,
		.fir_dma = 0x01,
		.cfg_base = 0x002e,
		.preconfigure = preconfigure_through_82801,
		.name = "Toshiba laptop with Intel 82801DB/DBL LPC bridge",
	},
	{
		.vendor = PCIID_VENDOR_INTEL, /* Intel 82801CAM ISA bridge */
		.device = 0x248c,
		.subvendor = 0x1179,
		.subdevice = 0xffff, /* 0xffff is "any" */
		.sir_io = 0x03f8,
		.fir_io = 0x0130,
		.fir_irq = 0x03,
		.fir_dma = 0x03,
		.cfg_base = 0x002e,
		.preconfigure = preconfigure_through_82801,
		.name = "Toshiba laptop with Intel 82801CAM ISA bridge",
	},
	{
		/* 82801DBM (ICH4-M) LPC Interface Bridge */
		.vendor = PCIID_VENDOR_INTEL,
		.device = 0x24cc,
		.subvendor = 0x1179,
		.subdevice = 0xffff, /* 0xffff is "any" */
		.sir_io = 0x03f8,
		.fir_io = 0x0130,
		.fir_irq = 0x03,
		.fir_dma = 0x03,
		.cfg_base = 0x002e,
		.preconfigure = preconfigure_through_82801,
		.name = "Toshiba laptop with Intel 8281DBM LPC bridge",
	},
	{
		/* ALi M1533/M1535 PCI to ISA Bridge [Aladdin IV/V/V+] */
		.vendor = PCIID_VENDOR_ALI,
		.device = 0x1533,
		.subvendor = 0x1179,
		.subdevice = 0xffff, /* 0xffff is "any" */
		.sir_io = 0x02e8,
		.fir_io = 0x02f8,
		.fir_irq = 0x07,
		.fir_dma = 0x03,
		.cfg_base = 0x002e,
		.preconfigure = preconfigure_through_ali,
		.name = "Toshiba laptop with ALi ISA bridge",
	},
	{ } // Terminator
};


/*
 * This sets up the basic SMSC parameters
 * (FIR port, SIR port, FIR DMA, FIR IRQ)
 * through the chip configuration port.
 */
static int __init preconfigure_smsc_chip(struct
					 smsc_ircc_subsystem_configuration
					 *conf)
{
	unsigned short iobase = conf->cfg_base;
	unsigned char tmpbyte;

	outb(LPC47N227_CFGACCESSKEY, iobase); // enter configuration state
	outb(SMSCSIOFLAT_DEVICEID_REG, iobase); // set for device ID
	tmpbyte = inb(iobase +1); // Read device ID
	IRDA_DEBUG(0,
		   "Detected Chip id: 0x%02x, setting up registers...\n",
		   tmpbyte);

	/* Disable UART1 and set up SIR I/O port */
	outb(0x24, iobase);  // select CR24 - UART1 base addr
	outb(0x00, iobase + 1); // disable UART1
	outb(SMSCSIOFLAT_UART2BASEADDR_REG, iobase);  // select CR25 - UART2 base addr
	outb( (conf->sir_io >> 2), iobase + 1); // bits 2-9 of 0x3f8
	tmpbyte = inb(iobase + 1);
	if (tmpbyte != (conf->sir_io >> 2) ) {
		IRDA_WARNING("ERROR: could not configure SIR ioport.\n");
		IRDA_WARNING("Try to supply ircc_cfg argument.\n");
		return -ENXIO;
	}

	/* Set up FIR IRQ channel for UART2 */
	outb(SMSCSIOFLAT_UARTIRQSELECT_REG, iobase); // select CR28 - UART1,2 IRQ select
	tmpbyte = inb(iobase + 1);
	tmpbyte &= SMSCSIOFLAT_UART1IRQSELECT_MASK; // Do not touch the UART1 portion
	tmpbyte |= (conf->fir_irq & SMSCSIOFLAT_UART2IRQSELECT_MASK);
	outb(tmpbyte, iobase + 1);
	tmpbyte = inb(iobase + 1) & SMSCSIOFLAT_UART2IRQSELECT_MASK;
	if (tmpbyte != conf->fir_irq) {
		IRDA_WARNING("ERROR: could not configure FIR IRQ channel.\n");
		return -ENXIO;
	}

	/* Set up FIR I/O port */
	outb(SMSCSIOFLAT_FIRBASEADDR_REG, iobase);  // CR2B - SCE (FIR) base addr
	outb((conf->fir_io >> 3), iobase + 1);
	tmpbyte = inb(iobase + 1);
	if (tmpbyte != (conf->fir_io >> 3) ) {
		IRDA_WARNING("ERROR: could not configure FIR I/O port.\n");
		return -ENXIO;
	}

	/* Set up FIR DMA channel */
	outb(SMSCSIOFLAT_FIRDMASELECT_REG, iobase);  // CR2C - SCE (FIR) DMA select
	outb((conf->fir_dma & LPC47N227_FIRDMASELECT_MASK), iobase + 1); // DMA
	tmpbyte = inb(iobase + 1) & LPC47N227_FIRDMASELECT_MASK;
	if (tmpbyte != (conf->fir_dma & LPC47N227_FIRDMASELECT_MASK)) {
		IRDA_WARNING("ERROR: could not configure FIR DMA channel.\n");
		return -ENXIO;
	}

	outb(SMSCSIOFLAT_UARTMODE0C_REG, iobase);  // CR0C - UART mode
	tmpbyte = inb(iobase + 1);
	tmpbyte &= ~SMSCSIOFLAT_UART2MODE_MASK |
		SMSCSIOFLAT_UART2MODE_VAL_IRDA;
	outb(tmpbyte, iobase + 1); // enable IrDA (HPSIR) mode, high speed

	outb(LPC47N227_APMBOOTDRIVE_REG, iobase);  // CR07 - Auto Pwr Mgt/boot drive sel
	tmpbyte = inb(iobase + 1);
	outb(tmpbyte | LPC47N227_UART2AUTOPWRDOWN_MASK, iobase + 1); // enable UART2 autopower down

	/* This one was not part of tosh1800 */
	outb(0x0a, iobase);  // CR0a - ecp fifo / ir mux
	tmpbyte = inb(iobase + 1);
	outb(tmpbyte | 0x40, iobase + 1); // send active device to ir port

	outb(LPC47N227_UART12POWER_REG, iobase);  // CR02 - UART 1,2 power
	tmpbyte = inb(iobase + 1);
	outb(tmpbyte | LPC47N227_UART2POWERDOWN_MASK, iobase + 1); // UART2 power up mode, UART1 power down

	outb(LPC47N227_FDCPOWERVALIDCONF_REG, iobase);  // CR00 - FDC Power/valid config cycle
	tmpbyte = inb(iobase + 1);
	outb(tmpbyte | LPC47N227_VALID_MASK, iobase + 1); // valid config cycle done

	outb(LPC47N227_CFGEXITKEY, iobase);  // Exit configuration

	return 0;
}

/* 82801CAM generic registers */
#define VID 0x00
#define DID 0x02
#define PIRQ_A_D_ROUT 0x60
#define SIRQ_CNTL 0x64
#define PIRQ_E_H_ROUT 0x68
#define PCI_DMA_C 0x90
/* LPC-specific registers */
#define COM_DEC 0xe0
#define GEN1_DEC 0xe4
#define LPC_EN 0xe6
#define GEN2_DEC 0xec
/*
 * Sets up the I/O range using the 82801CAM ISA bridge, 82801DBM LPC bridge
 * or Intel 82801DB/DBL (ICH4/ICH4-L) LPC Interface Bridge.
 * They all work the same way!
 */
static int __init preconfigure_through_82801(struct pci_dev *dev,
					     struct
					     smsc_ircc_subsystem_configuration
					     *conf)
{
	unsigned short tmpword;
	unsigned char tmpbyte;

	IRDA_MESSAGE("Setting up Intel 82801 controller and SMSC device\n");
	/*
	 * Select the range for the COMA COM port (SIR)
	 * Register COM_DEC:
	 * Bit 7: reserved
	 * Bit 6-4, COMB decode range
	 * Bit 3: reserved
	 * Bit 2-0, COMA decode range
	 *
	 * Decode ranges:
	 *   000 = 0x3f8-0x3ff (COM1)
	 *   001 = 0x2f8-0x2ff (COM2)
	 *   010 = 0x220-0x227
	 *   011 = 0x228-0x22f
	 *   100 = 0x238-0x23f
	 *   101 = 0x2e8-0x2ef (COM4)
	 *   110 = 0x338-0x33f
	 *   111 = 0x3e8-0x3ef (COM3)
	 */
	pci_read_config_byte(dev, COM_DEC, &tmpbyte);
	tmpbyte &= 0xf8; /* mask COMA bits */
	switch(conf->sir_io) {
	case 0x3f8:
		tmpbyte |= 0x00;
		break;
	case 0x2f8:
		tmpbyte |= 0x01;
		break;
	case 0x220:
		tmpbyte |= 0x02;
		break;
	case 0x228:
		tmpbyte |= 0x03;
		break;
	case 0x238:
		tmpbyte |= 0x04;
		break;
	case 0x2e8:
		tmpbyte |= 0x05;
		break;
	case 0x338:
		tmpbyte |= 0x06;
		break;
	case 0x3e8:
		tmpbyte |= 0x07;
		break;
	default:
		tmpbyte |= 0x01; /* COM2 default */
	}
	IRDA_DEBUG(1, "COM_DEC (write): 0x%02x\n", tmpbyte);
	pci_write_config_byte(dev, COM_DEC, tmpbyte);

	/* Enable Low Pin Count interface */
	pci_read_config_word(dev, LPC_EN, &tmpword);
	/* These seem to be set up at all times,
	 * just make sure it is properly set.
	 */
	switch(conf->cfg_base) {
	case 0x04e:
		tmpword |= 0x2000;
		break;
	case 0x02e:
		tmpword |= 0x1000;
		break;
	case 0x062:
		tmpword |= 0x0800;
		break;
	case 0x060:
		tmpword |= 0x0400;
		break;
	default:
		IRDA_WARNING("Uncommon I/O base address: 0x%04x\n",
			     conf->cfg_base);
		break;
	}
	tmpword &= 0xfffd; /* disable LPC COMB */
	tmpword |= 0x0001; /* set bit 0 : enable LPC COMA addr range (GEN2) */
	IRDA_DEBUG(1, "LPC_EN (write): 0x%04x\n", tmpword);
	pci_write_config_word(dev, LPC_EN, tmpword);

	/*
	 * Configure LPC DMA channel
	 * PCI_DMA_C bits:
	 * Bit 15-14: DMA channel 7 select
	 * Bit 13-12: DMA channel 6 select
	 * Bit 11-10: DMA channel 5 select
	 * Bit 9-8:   Reserved
	 * Bit 7-6:   DMA channel 3 select
	 * Bit 5-4:   DMA channel 2 select
	 * Bit 3-2:   DMA channel 1 select
	 * Bit 1-0:   DMA channel 0 select
	 *  00 = Reserved value
	 *  01 = PC/PCI DMA
	 *  10 = Reserved value
	 *  11 = LPC I/F DMA
	 */
	pci_read_config_word(dev, PCI_DMA_C, &tmpword);
	switch(conf->fir_dma) {
	case 0x07:
		tmpword |= 0xc000;
		break;
	case 0x06:
		tmpword |= 0x3000;
		break;
	case 0x05:
		tmpword |= 0x0c00;
		break;
	case 0x03:
		tmpword |= 0x00c0;
		break;
	case 0x02:
		tmpword |= 0x0030;
		break;
	case 0x01:
		tmpword |= 0x000c;
		break;
	case 0x00:
		tmpword |= 0x0003;
		break;
	default:
		break; /* do not change settings */
	}
	IRDA_DEBUG(1, "PCI_DMA_C (write): 0x%04x\n", tmpword);
	pci_write_config_word(dev, PCI_DMA_C, tmpword);

	/*
	 * GEN2_DEC bits:
	 * Bit 15-4: Generic I/O range
	 * Bit 3-1: reserved (read as 0)
	 * Bit 0: enable GEN2 range on LPC I/F
	 */
	tmpword = conf->fir_io & 0xfff8;
	tmpword |= 0x0001;
	IRDA_DEBUG(1, "GEN2_DEC (write): 0x%04x\n", tmpword);
	pci_write_config_word(dev, GEN2_DEC, tmpword);

	/* Pre-configure chip */
	return preconfigure_smsc_chip(conf);
}

/*
 * Pre-configure a certain port on the ALi 1533 bridge.
 * This is based on reverse-engineering since ALi does not
 * provide any data sheet for the 1533 chip.
 */
static void __init preconfigure_ali_port(struct pci_dev *dev,
					 unsigned short port)
{
	unsigned char reg;
	/* These bits obviously control the different ports */
	unsigned char mask;
	unsigned char tmpbyte;

	switch(port) {
	case 0x0130:
	case 0x0178:
		reg = 0xb0;
		mask = 0x80;
		break;
	case 0x03f8:
		reg = 0xb4;
		mask = 0x80;
		break;
	case 0x02f8:
		reg = 0xb4;
		mask = 0x30;
		break;
	case 0x02e8:
		reg = 0xb4;
		mask = 0x08;
		break;
	default:
		IRDA_ERROR("Failed to configure unsupported port on ALi 1533 bridge: 0x%04x\n", port);
		return;
	}

	pci_read_config_byte(dev, reg, &tmpbyte);
	/* Turn on the right bits */
	tmpbyte |= mask;
	pci_write_config_byte(dev, reg, tmpbyte);
	IRDA_MESSAGE("Activated ALi 1533 ISA bridge port 0x%04x.\n", port);
}

static int __init preconfigure_through_ali(struct pci_dev *dev,
					   struct
					   smsc_ircc_subsystem_configuration
					   *conf)
{
	/* Configure the two ports on the ALi 1533 */
	preconfigure_ali_port(dev, conf->sir_io);
	preconfigure_ali_port(dev, conf->fir_io);

	/* Pre-configure chip */
	return preconfigure_smsc_chip(conf);
}

static int __init smsc_ircc_preconfigure_subsystems(unsigned short ircc_cfg,
						    unsigned short ircc_fir,
						    unsigned short ircc_sir,
						    unsigned char ircc_dma,
						    unsigned char ircc_irq)
{
	struct pci_dev *dev = NULL;
	unsigned short ss_vendor = 0x0000;
	unsigned short ss_device = 0x0000;
	int ret = 0;

	for_each_pci_dev(dev) {
		struct smsc_ircc_subsystem_configuration *conf;

		/*
		 * Cache the subsystem vendor/device:
		 * some manufacturers fail to set this for all components,
		 * so we save it in case there is just 0x0000 0x0000 on the
		 * device we want to check.
		 */
		if (dev->subsystem_vendor != 0x0000U) {
			ss_vendor = dev->subsystem_vendor;
			ss_device = dev->subsystem_device;
		}
		conf = subsystem_configurations;
		for( ; conf->subvendor; conf++) {
			if(conf->vendor == dev->vendor &&
			   conf->device == dev->device &&
			   conf->subvendor == ss_vendor &&
			   /* Sometimes these are cached values */
			   (conf->subdevice == ss_device ||
			    conf->subdevice == 0xffff)) {
				struct smsc_ircc_subsystem_configuration
					tmpconf;

				memcpy(&tmpconf, conf,
				       sizeof(struct smsc_ircc_subsystem_configuration));

				/*
				 * Override the default values with anything
				 * passed in as parameter
				 */
				if (ircc_cfg != 0)
					tmpconf.cfg_base = ircc_cfg;
				if (ircc_fir != 0)
					tmpconf.fir_io = ircc_fir;
				if (ircc_sir != 0)
					tmpconf.sir_io = ircc_sir;
				if (ircc_dma != DMA_INVAL)
					tmpconf.fir_dma = ircc_dma;
				if (ircc_irq != IRQ_INVAL)
					tmpconf.fir_irq = ircc_irq;

				IRDA_MESSAGE("Detected unconfigured %s SMSC IrDA chip, pre-configuring device.\n", conf->name);
				if (conf->preconfigure)
					ret = conf->preconfigure(dev, &tmpconf);
				else
					ret = -ENODEV;
			}
		}
	}

	return ret;
}
#endif // CONFIG_PCI

/************************************************
 *
 * Transceivers specific functions
 *
 ************************************************/


/*
 * Function smsc_ircc_set_transceiver_smsc_ircc_atc(fir_base, speed)
 *
 *    Program transceiver through smsc-ircc ATC circuitry
 *
 */

static void smsc_ircc_set_transceiver_smsc_ircc_atc(int fir_base, u32 speed)
{
	unsigned long jiffies_now, jiffies_timeout;
	u8 val;

	jiffies_now = jiffies;
	jiffies_timeout = jiffies + SMSC_IRCC2_ATC_PROGRAMMING_TIMEOUT_JIFFIES;

	/* ATC */
	register_bank(fir_base, 4);
	outb((inb(fir_base + IRCC_ATC) & IRCC_ATC_MASK) | IRCC_ATC_nPROGREADY|IRCC_ATC_ENABLE,
	     fir_base + IRCC_ATC);

	while ((val = (inb(fir_base + IRCC_ATC) & IRCC_ATC_nPROGREADY)) &&
		!time_after(jiffies, jiffies_timeout))
		/* empty */;

	if (val)
		IRDA_WARNING("%s(): ATC: 0x%02x\n", __func__,
			     inb(fir_base + IRCC_ATC));
}

/*
 * Function smsc_ircc_probe_transceiver_smsc_ircc_atc(fir_base)
 *
 *    Probe transceiver smsc-ircc ATC circuitry
 *
 */

static int smsc_ircc_probe_transceiver_smsc_ircc_atc(int fir_base)
{
	return 0;
}

/*
 * Function smsc_ircc_set_transceiver_smsc_ircc_fast_pin_select(self, speed)
 *
 *    Set transceiver
 *
 */

static void smsc_ircc_set_transceiver_smsc_ircc_fast_pin_select(int fir_base, u32 speed)
{
	u8 fast_mode;

	switch (speed) {
	default:
	case 576000 :
		fast_mode = 0;
		break;
	case 1152000 :
	case 4000000 :
		fast_mode = IRCC_LCR_A_FAST;
		break;
	}
	register_bank(fir_base, 0);
	outb((inb(fir_base + IRCC_LCR_A) & 0xbf) | fast_mode, fir_base + IRCC_LCR_A);
}

/*
 * Function smsc_ircc_probe_transceiver_smsc_ircc_fast_pin_select(fir_base)
 *
 *    Probe transceiver
 *
 */

static int smsc_ircc_probe_transceiver_smsc_ircc_fast_pin_select(int fir_base)
{
	return 0;
}

/*
 * Function smsc_ircc_set_transceiver_toshiba_sat1800(fir_base, speed)
 *
 *    Set transceiver
 *
 */

static void smsc_ircc_set_transceiver_toshiba_sat1800(int fir_base, u32 speed)
{
	u8 fast_mode;

	switch (speed) {
	default:
	case 576000 :
		fast_mode = 0;
		break;
	case 1152000 :
	case 4000000 :
		fast_mode = /*IRCC_LCR_A_FAST |*/ IRCC_LCR_A_GP_DATA;
		break;

	}
	/* This causes an interrupt */
	register_bank(fir_base, 0);
	outb((inb(fir_base + IRCC_LCR_A) &  0xbf) | fast_mode, fir_base + IRCC_LCR_A);
}

/*
 * Function smsc_ircc_probe_transceiver_toshiba_sat1800(fir_base)
 *
 *    Probe transceiver
 *
 */

static int smsc_ircc_probe_transceiver_toshiba_sat1800(int fir_base)
{
	return 0;
}


module_init(smsc_ircc_init);
module_exit(smsc_ircc_cleanup);
