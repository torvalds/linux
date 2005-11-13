/*********************************************************************
 * $Id: smsc-ircc2.c,v 1.19.2.5 2002/10/27 11:34:26 dip Exp $
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
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/rtnetlink.h>
#include <linux/serial_reg.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include <asm/dma.h>
#include <asm/byteorder.h>

#include <linux/spinlock.h>
#include <linux/pm.h>

#include <net/irda/wrapper.h>
#include <net/irda/irda.h>
#include <net/irda/irda_device.h>

#include "smsc-ircc2.h"
#include "smsc-sio.h"


MODULE_AUTHOR("Daniele Peri <peri@csai.unipa.it>");
MODULE_DESCRIPTION("SMC IrCC SIR/FIR controller driver");
MODULE_LICENSE("GPL");

static int ircc_dma = 255;
module_param(ircc_dma, int, 0);
MODULE_PARM_DESC(ircc_dma, "DMA channel");

static int ircc_irq = 255;
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
	struct net_device_stats stats;
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
static int  smsc_ircc_hard_xmit_sir(struct sk_buff *skb, struct net_device *dev);
static int  smsc_ircc_hard_xmit_fir(struct sk_buff *skb, struct net_device *dev);
static void smsc_ircc_dma_xmit(struct smsc_ircc_cb *self, int bofs);
static void smsc_ircc_dma_xmit_complete(struct smsc_ircc_cb *self);
static void smsc_ircc_change_speed(struct smsc_ircc_cb *self, u32 speed);
static void smsc_ircc_set_sir_speed(struct smsc_ircc_cb *self, u32 speed);
static irqreturn_t smsc_ircc_interrupt(int irq, void *dev_id, struct pt_regs *regs);
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
static struct net_device_stats *smsc_ircc_net_get_stats(struct net_device *dev);
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


/*******************************************************************************
 *
 *
 * SMSC-ircc stuff
 *
 *
 *******************************************************************************/

/*
 * Function smsc_ircc_init ()
 *
 *    Initialize chip. Just try to find out how many chips we are dealing with
 *    and where they are
 */
static int __init smsc_ircc_init(void)
{
	int ret;

	IRDA_DEBUG(1, "%s\n", __FUNCTION__);

	ret = platform_driver_register(&smsc_ircc_driver);
	if (ret) {
		IRDA_ERROR("%s, Can't register driver!\n", driver_name);
		return ret;
	}

	dev_count = 0;

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

	if (ret)
		platform_driver_unregister(&smsc_ircc_driver);

	return ret;
}

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

	IRDA_DEBUG(1, "%s\n", __FUNCTION__);

	err = smsc_ircc_present(fir_base, sir_base);
	if (err)
		goto err_out;

	err = -ENOMEM;
	if (dev_count >= ARRAY_SIZE(dev_self)) {
	        IRDA_WARNING("%s(), too many devices!\n", __FUNCTION__);
		goto err_out1;
	}

	/*
	 *  Allocate new instance of the driver
	 */
	dev = alloc_irdadev(sizeof(struct smsc_ircc_cb));
	if (!dev) {
		IRDA_WARNING("%s() can't allocate net device\n", __FUNCTION__);
		goto err_out1;
	}

	SET_MODULE_OWNER(dev);

	dev->hard_start_xmit = smsc_ircc_hard_xmit_sir;
#if SMSC_IRCC2_C_NET_TIMEOUT
	dev->tx_timeout	     = smsc_ircc_timeout;
	dev->watchdog_timeo  = HZ * 2;  /* Allow enough time for speed change */
#endif
	dev->open            = smsc_ircc_net_open;
	dev->stop            = smsc_ircc_net_close;
	dev->do_ioctl        = smsc_ircc_net_ioctl;
	dev->get_stats	     = smsc_ircc_net_get_stats;

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
			     __FUNCTION__, fir_base);
		goto out1;
	}

	if (!request_region(sir_base, SMSC_IRCC2_SIR_CHIP_IO_EXTENT,
			    driver_name)) {
		IRDA_WARNING("%s: can't get sir_base of 0x%03x\n",
			     __FUNCTION__, sir_base);
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
			     __FUNCTION__, fir_base);
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

	if (irq < 255) {
		if (irq != chip_irq)
			IRDA_MESSAGE("%s, Overriding IRQ - chip says %d, using %d\n",
				     driver_name, chip_irq, irq);
		self->io.irq = irq;
	} else
		self->io.irq = chip_irq;

	if (dma < 255) {
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

	IRDA_DEBUG(2, "%s(), %s, (cmd=0x%X)\n", __FUNCTION__, dev->name, cmd);

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

static struct net_device_stats *smsc_ircc_net_get_stats(struct net_device *dev)
{
	struct smsc_ircc_cb *self = netdev_priv(dev);

	return &self->stats;
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
	dev->trans_start = jiffies;
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
int smsc_ircc_hard_xmit_sir(struct sk_buff *skb, struct net_device *dev)
{
	struct smsc_ircc_cb *self;
	unsigned long flags;
	s32 speed;

	IRDA_DEBUG(1, "%s\n", __FUNCTION__);

	IRDA_ASSERT(dev != NULL, return 0;);

	self = netdev_priv(dev);
	IRDA_ASSERT(self != NULL, return 0;);

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
			return 0;
		}
		self->new_speed = speed;
	}

	/* Init tx buffer */
	self->tx_buff.data = self->tx_buff.head;

	/* Copy skb to tx_buff while wrapping, stuffing and making CRC */
	self->tx_buff.len = async_wrap_skb(skb, self->tx_buff.data,
					   self->tx_buff.truesize);

	self->stats.tx_bytes += self->tx_buff.len;

	/* Turn on transmit finished interrupt. Will fire immediately!  */
	outb(UART_IER_THRI, self->io.sir_base + UART_IER);

	spin_unlock_irqrestore(&self->lock, flags);

	dev_kfree_skb(skb);

	return 0;
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
		IRDA_DEBUG(0, "%s(), handling baud of 576000\n", __FUNCTION__);
		break;
	case 1152000:
		ir_mode = IRCC_CFGA_IRDA_HDLC;
		ctrl = IRCC_1152 | IRCC_CRC;
		fast = IRCC_LCR_A_FAST | IRCC_LCR_A_GP_DATA;
		IRDA_DEBUG(0, "%s(), handling baud of 1152000\n",
			   __FUNCTION__);
		break;
	case 4000000:
		ir_mode = IRCC_CFGA_IRDA_4PPM;
		ctrl = IRCC_CRC;
		fast = IRCC_LCR_A_FAST;
		IRDA_DEBUG(0, "%s(), handling baud of 4000000\n",
			   __FUNCTION__);
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

	IRDA_DEBUG(1, "%s\n", __FUNCTION__);

	IRDA_ASSERT(self != NULL, return;);
	dev = self->netdev;
	IRDA_ASSERT(dev != NULL, return;);

	fir_base = self->io.fir_base;

	/* Reset everything */

	/* Install FIR transmit handler */
	dev->hard_start_xmit = smsc_ircc_hard_xmit_fir;

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

	IRDA_DEBUG(1, "%s\n", __FUNCTION__);

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

	IRDA_DEBUG(0, "%s() changing speed to: %d\n", __FUNCTION__, speed);

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
void smsc_ircc_set_sir_speed(struct smsc_ircc_cb *self, __u32 speed)
{
	int iobase;
	int fcr;    /* FIFO control reg */
	int lcr;    /* Line control reg */
	int divisor;

	IRDA_DEBUG(0, "%s(), Setting speed to: %d\n", __FUNCTION__, speed);

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

	IRDA_DEBUG(2, "%s() speed changed to: %d\n", __FUNCTION__, speed);
}


/*
 * Function smsc_ircc_hard_xmit_fir (skb, dev)
 *
 *    Transmit the frame!
 *
 */
static int smsc_ircc_hard_xmit_fir(struct sk_buff *skb, struct net_device *dev)
{
	struct smsc_ircc_cb *self;
	unsigned long flags;
	s32 speed;
	int mtt;

	IRDA_ASSERT(dev != NULL, return 0;);
	self = netdev_priv(dev);
	IRDA_ASSERT(self != NULL, return 0;);

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
			return 0;
		}

		self->new_speed = speed;
	}

	memcpy(self->tx_buff.head, skb->data, skb->len);

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

	return 0;
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

	IRDA_DEBUG(3, "%s\n", __FUNCTION__);
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

	IRDA_DEBUG(3, "%s\n", __FUNCTION__);
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
		self->stats.tx_errors++;
		self->stats.tx_fifo_errors++;

		/* Reset error condition */
		register_bank(iobase, 0);
		outb(IRCC_MASTER_ERROR_RESET, iobase + IRCC_MASTER);
		outb(0x00, iobase + IRCC_MASTER);
	} else {
		self->stats.tx_packets++;
		self->stats.tx_bytes += self->tx_buff.len;
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

	IRDA_DEBUG(3, "%s\n", __FUNCTION__);
#if 0
	/* Disable Rx */
	register_bank(iobase, 0);
	outb(0x00, iobase + IRCC_LCR_B);
#endif
	register_bank(iobase, 0);
	outb(inb(iobase + IRCC_LSAR) & ~IRCC_LSAR_ADDRESS_MASK, iobase + IRCC_LSAR);
	lsr= inb(iobase + IRCC_LSR);
	msgcnt = inb(iobase + IRCC_LCR_B) & 0x08;

	IRDA_DEBUG(2, "%s: dma count = %d\n", __FUNCTION__,
		   get_dma_residue(self->io.dma));

	len = self->rx_buff.truesize - get_dma_residue(self->io.dma);

	/* Look for errors */
	if (lsr & (IRCC_LSR_FRAME_ERROR | IRCC_LSR_CRC_ERROR | IRCC_LSR_SIZE_ERROR)) {
		self->stats.rx_errors++;
		if (lsr & IRCC_LSR_FRAME_ERROR)
			self->stats.rx_frame_errors++;
		if (lsr & IRCC_LSR_CRC_ERROR)
			self->stats.rx_crc_errors++;
		if (lsr & IRCC_LSR_SIZE_ERROR)
			self->stats.rx_length_errors++;
		if (lsr & (IRCC_LSR_UNDERRUN | IRCC_LSR_OVERRUN))
			self->stats.rx_length_errors++;
		return;
	}

	/* Remove CRC */
	len -= self->io.speed < 4000000 ? 2 : 4;

	if (len < 2 || len > 2050) {
		IRDA_WARNING("%s(), bogus len=%d\n", __FUNCTION__, len);
		return;
	}
	IRDA_DEBUG(2, "%s: msgcnt = %d, len=%d\n", __FUNCTION__, msgcnt, len);

	skb = dev_alloc_skb(len + 1);
	if (!skb) {
		IRDA_WARNING("%s(), memory squeeze, dropping frame.\n",
			     __FUNCTION__);
		return;
	}
	/* Make sure IP header gets aligned */
	skb_reserve(skb, 1);

	memcpy(skb_put(skb, len), self->rx_buff.data, len);
	self->stats.rx_packets++;
	self->stats.rx_bytes += len;

	skb->dev = self->netdev;
	skb->mac.raw  = skb->data;
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
		async_unwrap_char(self->netdev, &self->stats, &self->rx_buff,
				  inb(iobase + UART_RX));

		/* Make sure we don't stay here to long */
		if (boguscount++ > 32) {
			IRDA_DEBUG(2, "%s(), breaking!\n", __FUNCTION__);
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
static irqreturn_t smsc_ircc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = (struct net_device *) dev_id;
	struct smsc_ircc_cb *self;
	int iobase, iir, lcra, lsr;
	irqreturn_t ret = IRQ_NONE;

	if (dev == NULL) {
		printk(KERN_WARNING "%s: irq %d for unknown device.\n",
		       driver_name, irq);
		goto irq_ret;
	}

	self = netdev_priv(dev);
	IRDA_ASSERT(self != NULL, return IRQ_NONE;);

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

	IRDA_DEBUG(2, "%s(), iir = 0x%02x\n", __FUNCTION__, iir);

	if (iir & IRCC_IIR_EOM) {
		if (self->io.direction == IO_RECV)
			smsc_ircc_dma_receive_complete(self);
		else
			smsc_ircc_dma_xmit_complete(self);

		smsc_ircc_dma_receive(self);
	}

	if (iir & IRCC_IIR_ACTIVE_FRAME) {
		/*printk(KERN_WARNING "%s(): Active Frame\n", __FUNCTION__);*/
	}

	/* Enable interrupts again */

	register_bank(iobase, 0);
	outb(IRCC_IER_ACTIVE_FRAME | IRCC_IER_EOM, iobase + IRCC_IER);

 irq_ret_unlock:
	spin_unlock(&self->lock);
 irq_ret:
	return ret;
}

/*
 * Function irport_interrupt_sir (irq, dev_id, regs)
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
			    __FUNCTION__, iir, lsr, iobase);

		switch (iir) {
		case UART_IIR_RLSI:
			IRDA_DEBUG(2, "%s(), RLSI\n", __FUNCTION__);
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
				   __FUNCTION__, iir);
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

	IRDA_DEBUG(1, "%s\n", __FUNCTION__);

	IRDA_ASSERT(self != NULL, return FALSE;);

	IRDA_DEBUG(0, "%s: dma count = %d\n", __FUNCTION__,
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
			   __FUNCTION__, self->io.irq, error);

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

	IRDA_DEBUG(1, "%s\n", __FUNCTION__);

	IRDA_ASSERT(dev != NULL, return -1;);
	self = netdev_priv(dev);
	IRDA_ASSERT(self != NULL, return 0;);

	if (self->io.suspended) {
		IRDA_DEBUG(0, "%s(), device is suspended\n", __FUNCTION__);
		return -EAGAIN;
	}

	if (request_irq(self->io.irq, smsc_ircc_interrupt, 0, dev->name,
			(void *) dev)) {
		IRDA_DEBUG(0, "%s(), unable to allocate irq=%d\n",
			   __FUNCTION__, self->io.irq);
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
			     __FUNCTION__, self->io.dma);
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

	IRDA_DEBUG(1, "%s\n", __FUNCTION__);

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
	IRDA_DEBUG(1, "%s\n", __FUNCTION__);

	IRDA_ASSERT(self != NULL, return -1;);

	platform_device_unregister(self->pldev);

	/* Remove netdevice */
	unregister_netdev(self->netdev);

	smsc_ircc_stop_interrupts(self);

	/* Release the PORTS that this driver is using */
	IRDA_DEBUG(0, "%s(), releasing 0x%03x\n",  __FUNCTION__,
		   self->io.fir_base);

	release_region(self->io.fir_base, self->io.fir_ext);

	IRDA_DEBUG(0, "%s(), releasing 0x%03x\n", __FUNCTION__,
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

	IRDA_DEBUG(1, "%s\n", __FUNCTION__);

	for (i = 0; i < 2; i++) {
		if (dev_self[i])
			smsc_ircc_close(dev_self[i]);
	}

	platform_driver_unregister(&smsc_ircc_driver);
}

/*
 *	Start SIR operations
 *
 * This function *must* be called with spinlock held, because it may
 * be called from the irq handler (via smsc_ircc_change_speed()). - Jean II
 */
void smsc_ircc_sir_start(struct smsc_ircc_cb *self)
{
	struct net_device *dev;
	int fir_base, sir_base;

	IRDA_DEBUG(3, "%s\n", __FUNCTION__);

	IRDA_ASSERT(self != NULL, return;);
	dev = self->netdev;
	IRDA_ASSERT(dev != NULL, return;);
	dev->hard_start_xmit = &smsc_ircc_hard_xmit_sir;

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

	IRDA_DEBUG(3, "%s() - exit\n", __FUNCTION__);

	outb(0x00, fir_base + IRCC_MASTER);
}

#if SMSC_IRCC2_C_SIR_STOP
void smsc_ircc_sir_stop(struct smsc_ircc_cb *self)
{
	int iobase;

	IRDA_DEBUG(3, "%s\n", __FUNCTION__);
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

	IRDA_DEBUG(4, "%s\n", __FUNCTION__);

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
				   __FUNCTION__, self->new_speed);
			smsc_ircc_sir_wait_hw_transmitter_finish(self);
			smsc_ircc_change_speed(self, self->new_speed);
			self->new_speed = 0;
		} else {
			/* Tell network layer that we want more frames */
			netif_wake_queue(self->netdev);
		}
		self->stats.tx_packets++;

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
		IRDA_WARNING("%s(), failed, fifo not empty!\n", __FUNCTION__);
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
	return (self->rx_buff.state != OUTSIDE_FRAME);
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

	if (count == 0)
		IRDA_DEBUG(0, "%s(): stuck transmitter\n", __FUNCTION__);
}


/* PROBING
 *
 *
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

		/*printk(KERN_WARNING "%s(): probing: 0x%02x for: 0x%02x\n", __FUNCTION__, cfg_base, address->type);*/

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

	IRDA_DEBUG(1, "%s\n", __FUNCTION__);

	if (smsc_ircc_probe(cfgbase, SMSCSIOFLAT_DEVICEID_REG, chips, type) == NULL)
		return ret;

	outb(SMSCSIOFLAT_UARTMODE0C_REG, cfgbase);
	mode = inb(cfgbase + 1);

	/*printk(KERN_WARNING "%s(): mode: 0x%02x\n", __FUNCTION__, mode);*/

	if (!(mode & SMSCSIOFLAT_UART2MODE_VAL_IRDA))
		IRDA_WARNING("%s(): IrDA not enabled\n", __FUNCTION__);

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

	IRDA_MESSAGE("%s(): fir: 0x%02x, sir: 0x%02x, dma: %02d, irq: %d, mode: 0x%02x\n", __FUNCTION__, firbase, sirbase, dma, irq, mode);

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

	IRDA_DEBUG(1, "%s\n", __FUNCTION__);

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
	IRDA_DEBUG(1, "%s\n", __FUNCTION__);

	outb(reg, cfg_base);
	return inb(cfg_base) != reg ? -1 : 0;
}

static const struct smsc_chip * __init smsc_ircc_probe(unsigned short cfg_base, u8 reg, const struct smsc_chip *chip, char *type)
{
	u8 devid, xdevid, rev;

	IRDA_DEBUG(1, "%s\n", __FUNCTION__);

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
			     __FUNCTION__, cfg_base);
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
			     __FUNCTION__, cfg_base);
	} else {
		if (!smsc_superio_flat(lpc_chips_flat, cfg_base, "LPC") ||
		    !smsc_superio_paged(lpc_chips_paged, cfg_base, "LPC"))
			ret = 0;

		release_region(cfg_base, 2);
	}
	return ret;
}

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
		IRDA_WARNING("%s(): ATC: 0x%02x\n", __FUNCTION__,
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
