/*
 * Xilinx XPS PS/2 device driver
 *
 * (c) 2005 MontaVista Software, Inc.
 * (c) 2008 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */


#include <linux/module.h>
#include <linux/serio.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/io.h>

#include <linux/of_device.h>
#include <linux/of_platform.h>

#define DRIVER_NAME		"xilinx_ps2"

/* Register offsets for the xps2 device */
#define XPS2_SRST_OFFSET	0x00000000 /* Software Reset register */
#define XPS2_STATUS_OFFSET	0x00000004 /* Status register */
#define XPS2_RX_DATA_OFFSET	0x00000008 /* Receive Data register */
#define XPS2_TX_DATA_OFFSET	0x0000000C /* Transmit Data register */
#define XPS2_GIER_OFFSET	0x0000002C /* Global Interrupt Enable reg */
#define XPS2_IPISR_OFFSET	0x00000030 /* Interrupt Status register */
#define XPS2_IPIER_OFFSET	0x00000038 /* Interrupt Enable register */

/* Reset Register Bit Definitions */
#define XPS2_SRST_RESET		0x0000000A /* Software Reset  */

/* Status Register Bit Positions */
#define XPS2_STATUS_RX_FULL	0x00000001 /* Receive Full  */
#define XPS2_STATUS_TX_FULL	0x00000002 /* Transmit Full  */

/* Bit definitions for ISR/IER registers. Both the registers have the same bit
 * definitions and are only defined once. */
#define XPS2_IPIXR_WDT_TOUT	0x00000001 /* Watchdog Timeout Interrupt */
#define XPS2_IPIXR_TX_NOACK	0x00000002 /* Transmit No ACK Interrupt */
#define XPS2_IPIXR_TX_ACK	0x00000004 /* Transmit ACK (Data) Interrupt */
#define XPS2_IPIXR_RX_OVF	0x00000008 /* Receive Overflow Interrupt */
#define XPS2_IPIXR_RX_ERR	0x00000010 /* Receive Error Interrupt */
#define XPS2_IPIXR_RX_FULL	0x00000020 /* Receive Data Interrupt */

/* Mask for all the Transmit Interrupts */
#define XPS2_IPIXR_TX_ALL	(XPS2_IPIXR_TX_NOACK | XPS2_IPIXR_TX_ACK)

/* Mask for all the Receive Interrupts */
#define XPS2_IPIXR_RX_ALL	(XPS2_IPIXR_RX_OVF | XPS2_IPIXR_RX_ERR |  \
					XPS2_IPIXR_RX_FULL)

/* Mask for all the Interrupts */
#define XPS2_IPIXR_ALL		(XPS2_IPIXR_TX_ALL | XPS2_IPIXR_RX_ALL |  \
					XPS2_IPIXR_WDT_TOUT)

/* Global Interrupt Enable mask */
#define XPS2_GIER_GIE_MASK	0x80000000

struct xps2data {
	int irq;
	u32 phys_addr;
	u32 remap_size;
	spinlock_t lock;
	u8 rxb;				/* Rx buffer */
	void __iomem *base_address;	/* virt. address of control registers */
	unsigned int dfl;
	struct serio serio;		/* serio */
};

/************************************/
/* XPS PS/2 data transmission calls */
/************************************/

/*
 * xps2_recv() will attempt to receive a byte of data from the PS/2 port.
 */
static int xps2_recv(struct xps2data *drvdata, u8 *byte)
{
	u32 sr;
	int status = -1;

	/* If there is data available in the PS/2 receiver, read it */
	sr = in_be32(drvdata->base_address + XPS2_STATUS_OFFSET);
	if (sr & XPS2_STATUS_RX_FULL) {
		*byte = in_be32(drvdata->base_address + XPS2_RX_DATA_OFFSET);
		status = 0;
	}

	return status;
}

/*********************/
/* Interrupt handler */
/*********************/
static irqreturn_t xps2_interrupt(int irq, void *dev_id)
{
	struct xps2data *drvdata = dev_id;
	u32 intr_sr;
	u8 c;
	int status;

	/* Get the PS/2 interrupts and clear them */
	intr_sr = in_be32(drvdata->base_address + XPS2_IPISR_OFFSET);
	out_be32(drvdata->base_address + XPS2_IPISR_OFFSET, intr_sr);

	/* Check which interrupt is active */
	if (intr_sr & XPS2_IPIXR_RX_OVF)
		printk(KERN_WARNING "%s: receive overrun error\n",
			drvdata->serio.name);

	if (intr_sr & XPS2_IPIXR_RX_ERR)
		drvdata->dfl |= SERIO_PARITY;

	if (intr_sr & (XPS2_IPIXR_TX_NOACK | XPS2_IPIXR_WDT_TOUT))
		drvdata->dfl |= SERIO_TIMEOUT;

	if (intr_sr & XPS2_IPIXR_RX_FULL) {
		status = xps2_recv(drvdata, &drvdata->rxb);

		/* Error, if a byte is not received */
		if (status) {
			printk(KERN_ERR
				"%s: wrong rcvd byte count (%d)\n",
				drvdata->serio.name, status);
		} else {
			c = drvdata->rxb;
			serio_interrupt(&drvdata->serio, c, drvdata->dfl);
			drvdata->dfl = 0;
		}
	}

	if (intr_sr & XPS2_IPIXR_TX_ACK)
		drvdata->dfl = 0;

	return IRQ_HANDLED;
}

/*******************/
/* serio callbacks */
/*******************/

/*
 * sxps2_write() sends a byte out through the PS/2 interface.
 */
static int sxps2_write(struct serio *pserio, unsigned char c)
{
	struct xps2data *drvdata = pserio->port_data;
	unsigned long flags;
	u32 sr;
	int status = -1;

	spin_lock_irqsave(&drvdata->lock, flags);

	/* If the PS/2 transmitter is empty send a byte of data */
	sr = in_be32(drvdata->base_address + XPS2_STATUS_OFFSET);
	if (!(sr & XPS2_STATUS_TX_FULL)) {
		out_be32(drvdata->base_address + XPS2_TX_DATA_OFFSET, c);
		status = 0;
	}

	spin_unlock_irqrestore(&drvdata->lock, flags);

	return status;
}

/*
 * sxps2_open() is called when a port is open by the higher layer.
 */
static int sxps2_open(struct serio *pserio)
{
	struct xps2data *drvdata = pserio->port_data;
	int retval;

	retval = request_irq(drvdata->irq, &xps2_interrupt, 0,
				DRIVER_NAME, drvdata);
	if (retval) {
		printk(KERN_ERR
			"%s: Couldn't allocate interrupt %d\n",
			drvdata->serio.name, drvdata->irq);
		return retval;
	}

	/* start reception by enabling the interrupts */
	out_be32(drvdata->base_address + XPS2_GIER_OFFSET, XPS2_GIER_GIE_MASK);
	out_be32(drvdata->base_address + XPS2_IPIER_OFFSET, XPS2_IPIXR_RX_ALL);
	(void)xps2_recv(drvdata, &drvdata->rxb);

	return 0;		/* success */
}

/*
 * sxps2_close() frees the interrupt.
 */
static void sxps2_close(struct serio *pserio)
{
	struct xps2data *drvdata = pserio->port_data;

	/* Disable the PS2 interrupts */
	out_be32(drvdata->base_address + XPS2_GIER_OFFSET, 0x00);
	out_be32(drvdata->base_address + XPS2_IPIER_OFFSET, 0x00);
	free_irq(drvdata->irq, drvdata);
}

/*********************/
/* Device setup code */
/*********************/

static int xps2_setup(struct device *dev, struct resource *regs_res,
		      struct resource *irq_res)
{
	struct xps2data *drvdata;
	struct serio *serio;
	unsigned long remap_size;
	int retval;

	if (!dev)
		return -EINVAL;

	if (!regs_res || !irq_res) {
		dev_err(dev, "IO resource(s) not found\n");
		return -EINVAL;
	}

	drvdata = kzalloc(sizeof(struct xps2data), GFP_KERNEL);
	if (!drvdata) {
		dev_err(dev, "Couldn't allocate device private record\n");
		return -ENOMEM;
	}

	dev_set_drvdata(dev, drvdata);

	spin_lock_init(&drvdata->lock);
	drvdata->irq = irq_res->start;

	remap_size = regs_res->end - regs_res->start + 1;
	if (!request_mem_region(regs_res->start, remap_size, DRIVER_NAME)) {
		dev_err(dev, "Couldn't lock memory region at 0x%08X\n",
			(unsigned int)regs_res->start);
		retval = -EBUSY;
		goto failed1;
	}

	/* Fill in configuration data and add them to the list */
	drvdata->phys_addr = regs_res->start;
	drvdata->remap_size = remap_size;
	drvdata->base_address = ioremap(regs_res->start, remap_size);
	if (drvdata->base_address == NULL) {
		dev_err(dev, "Couldn't ioremap memory at 0x%08X\n",
			(unsigned int)regs_res->start);
		retval = -EFAULT;
		goto failed2;
	}

	/* Disable all the interrupts, just in case */
	out_be32(drvdata->base_address + XPS2_IPIER_OFFSET, 0);

	/* Reset the PS2 device and abort any current transaction, to make sure
	 * we have the PS2 in a good state */
	out_be32(drvdata->base_address + XPS2_SRST_OFFSET, XPS2_SRST_RESET);

	dev_info(dev, "Xilinx PS2 at 0x%08X mapped to 0x%p, irq=%d\n",
		drvdata->phys_addr, drvdata->base_address, drvdata->irq);

	serio = &drvdata->serio;
	serio->id.type = SERIO_8042;
	serio->write = sxps2_write;
	serio->open = sxps2_open;
	serio->close = sxps2_close;
	serio->port_data = drvdata;
	serio->dev.parent = dev;
	snprintf(serio->name, sizeof(serio->name),
		 "Xilinx XPS PS/2 at %08X", drvdata->phys_addr);
	snprintf(serio->phys, sizeof(serio->phys),
		 "xilinxps2/serio at %08X", drvdata->phys_addr);
	serio_register_port(serio);

	return 0;		/* success */

failed2:
	release_mem_region(regs_res->start, remap_size);
failed1:
	kfree(drvdata);
	dev_set_drvdata(dev, NULL);

	return retval;
}

/***************************/
/* OF Platform Bus Support */
/***************************/

static int __devinit xps2_of_probe(struct of_device *ofdev, const struct
				   of_device_id * match)
{
	struct resource r_irq; /* Interrupt resources */
	struct resource r_mem; /* IO mem resources */
	int rc = 0;

	printk(KERN_INFO "Device Tree Probing \'%s\'\n",
			ofdev->node->name);

	/* Get iospace for the device */
	rc = of_address_to_resource(ofdev->node, 0, &r_mem);
	if (rc) {
		dev_err(&ofdev->dev, "invalid address\n");
		return rc;
	}

	/* Get IRQ for the device */
	rc = of_irq_to_resource(ofdev->node, 0, &r_irq);
	if (rc == NO_IRQ) {
		dev_err(&ofdev->dev, "no IRQ found\n");
		return rc;
	}

	return xps2_setup(&ofdev->dev, &r_mem, &r_irq);
}

static int __devexit xps2_of_remove(struct of_device *of_dev)
{
	struct device *dev = &of_dev->dev;
	struct xps2data *drvdata;

	if (!dev)
		return -EINVAL;

	drvdata = dev_get_drvdata(dev);

	serio_unregister_port(&drvdata->serio);
	iounmap(drvdata->base_address);
	release_mem_region(drvdata->phys_addr, drvdata->remap_size);
	kfree(drvdata);

	dev_set_drvdata(dev, NULL);

	return 0;		/* success */
}

/* Match table for of_platform binding */
static struct of_device_id xps2_of_match[] __devinitdata = {
	{ .compatible = "xlnx,xps-ps2-1.00.a", },
	{ /* end of list */ },
};
MODULE_DEVICE_TABLE(of, xps2_of_match);

static struct of_platform_driver xps2_of_driver = {
	.name		= DRIVER_NAME,
	.match_table	= xps2_of_match,
	.probe		= xps2_of_probe,
	.remove		= __devexit_p(xps2_of_remove),
};

static int __init xps2_init(void)
{
	return of_register_platform_driver(&xps2_of_driver);
}

static void __exit xps2_cleanup(void)
{
	of_unregister_platform_driver(&xps2_of_driver);
}

module_init(xps2_init);
module_exit(xps2_cleanup);

MODULE_AUTHOR("Xilinx, Inc.");
MODULE_DESCRIPTION("Xilinx XPS PS/2 driver");
MODULE_LICENSE("GPL");

