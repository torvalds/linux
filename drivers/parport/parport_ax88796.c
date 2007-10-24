/* linux/drivers/parport/parport_ax88796.c
 *
 * (c) 2005,2006 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/parport.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/platform_device.h>

#include <asm/io.h>
#include <asm/irq.h>

#define AX_SPR_BUSY		(1<<7)
#define AX_SPR_ACK		(1<<6)
#define AX_SPR_PE		(1<<5)
#define AX_SPR_SLCT		(1<<4)
#define AX_SPR_ERR		(1<<3)

#define AX_CPR_nDOE		(1<<5)
#define AX_CPR_SLCTIN		(1<<3)
#define AX_CPR_nINIT		(1<<2)
#define AX_CPR_ATFD		(1<<1)
#define AX_CPR_STRB		(1<<0)

struct ax_drvdata {
	struct parport		*parport;
	struct parport_state	 suspend;

	struct device		*dev;
	struct resource		*io;

	unsigned char		 irq_enabled;

	void __iomem		*base;
	void __iomem		*spp_data;
	void __iomem		*spp_spr;
	void __iomem		*spp_cpr;
};

static inline struct ax_drvdata *pp_to_drv(struct parport *p)
{
	return p->private_data;
}

static unsigned char
parport_ax88796_read_data(struct parport *p)
{
	struct ax_drvdata *dd = pp_to_drv(p);

	return readb(dd->spp_data);
}

static void
parport_ax88796_write_data(struct parport *p, unsigned char data)
{
	struct ax_drvdata *dd = pp_to_drv(p);

	writeb(data, dd->spp_data);
}

static unsigned char
parport_ax88796_read_control(struct parport *p)
{
	struct ax_drvdata *dd = pp_to_drv(p);
	unsigned int cpr = readb(dd->spp_cpr);
	unsigned int ret = 0;

	if (!(cpr & AX_CPR_STRB))
		ret |= PARPORT_CONTROL_STROBE;

	if (!(cpr & AX_CPR_ATFD))
		ret |= PARPORT_CONTROL_AUTOFD;

	if (cpr & AX_CPR_nINIT)
		ret |= PARPORT_CONTROL_INIT;

	if (!(cpr & AX_CPR_SLCTIN))
		ret |= PARPORT_CONTROL_SELECT;

	return ret;
}

static void
parport_ax88796_write_control(struct parport *p, unsigned char control)
{
	struct ax_drvdata *dd = pp_to_drv(p);
	unsigned int cpr = readb(dd->spp_cpr);

	cpr &= AX_CPR_nDOE;

	if (!(control & PARPORT_CONTROL_STROBE))
		cpr |= AX_CPR_STRB;

	if (!(control & PARPORT_CONTROL_AUTOFD))
		cpr |= AX_CPR_ATFD;

	if (control & PARPORT_CONTROL_INIT)
		cpr |= AX_CPR_nINIT;

	if (!(control & PARPORT_CONTROL_SELECT))
		cpr |= AX_CPR_SLCTIN;

	dev_dbg(dd->dev, "write_control: ctrl=%02x, cpr=%02x\n", control, cpr);
	writeb(cpr, dd->spp_cpr);

	if (parport_ax88796_read_control(p) != control) {
		dev_err(dd->dev, "write_control: read != set (%02x, %02x)\n",
			parport_ax88796_read_control(p), control);
	}
}

static unsigned char
parport_ax88796_read_status(struct parport *p)
{
	struct ax_drvdata *dd = pp_to_drv(p);
	unsigned int status = readb(dd->spp_spr);
	unsigned int ret = 0;

	if (status & AX_SPR_BUSY)
		ret |= PARPORT_STATUS_BUSY;

	if (status & AX_SPR_ACK)
		ret |= PARPORT_STATUS_ACK;

	if (status & AX_SPR_ERR)
		ret |= PARPORT_STATUS_ERROR;

	if (status & AX_SPR_SLCT)
		ret |= PARPORT_STATUS_SELECT;

	if (status & AX_SPR_PE)
		ret |= PARPORT_STATUS_PAPEROUT;

	return ret;
}

static unsigned char
parport_ax88796_frob_control(struct parport *p, unsigned char mask,
			     unsigned char val)
{
	struct ax_drvdata *dd = pp_to_drv(p);
	unsigned char old = parport_ax88796_read_control(p);

	dev_dbg(dd->dev, "frob: mask=%02x, val=%02x, old=%02x\n",
		mask, val, old);

	parport_ax88796_write_control(p, (old & ~mask) | val);
	return old;
}

static void
parport_ax88796_enable_irq(struct parport *p)
{
	struct ax_drvdata *dd = pp_to_drv(p);
	unsigned long flags;

	local_irq_save(flags);
	if (!dd->irq_enabled) {
		enable_irq(p->irq);
		dd->irq_enabled = 1;
	}
	local_irq_restore(flags);
}

static void
parport_ax88796_disable_irq(struct parport *p)
{
	struct ax_drvdata *dd = pp_to_drv(p);
	unsigned long flags;

	local_irq_save(flags);
	if (dd->irq_enabled) {
		disable_irq(p->irq);
		dd->irq_enabled = 0;
	}
	local_irq_restore(flags);
}

static void
parport_ax88796_data_forward(struct parport *p)
{
	struct ax_drvdata *dd = pp_to_drv(p);
	void __iomem *cpr = dd->spp_cpr;

	writeb((readb(cpr) & ~AX_CPR_nDOE), cpr);
}

static void
parport_ax88796_data_reverse(struct parport *p)
{
	struct ax_drvdata *dd = pp_to_drv(p);
	void __iomem *cpr = dd->spp_cpr;

	writeb(readb(cpr) | AX_CPR_nDOE, cpr);
}

static void
parport_ax88796_init_state(struct pardevice *d, struct parport_state *s)
{
	struct ax_drvdata *dd = pp_to_drv(d->port);

	memset(s, 0, sizeof(struct parport_state));

	dev_dbg(dd->dev, "init_state: %p: state=%p\n", d, s);
	s->u.ax88796.cpr = readb(dd->spp_cpr);
}

static void
parport_ax88796_save_state(struct parport *p, struct parport_state *s)
{
	struct ax_drvdata *dd = pp_to_drv(p);

	dev_dbg(dd->dev, "save_state: %p: state=%p\n", p, s);
	s->u.ax88796.cpr = readb(dd->spp_cpr);
}

static void
parport_ax88796_restore_state(struct parport *p, struct parport_state *s)
{
	struct ax_drvdata *dd = pp_to_drv(p);

	dev_dbg(dd->dev, "restore_state: %p: state=%p\n", p, s);
	writeb(s->u.ax88796.cpr, dd->spp_cpr);
}

static struct parport_operations parport_ax88796_ops = {
	.write_data	= parport_ax88796_write_data,
	.read_data	= parport_ax88796_read_data,

	.write_control	= parport_ax88796_write_control,
	.read_control	= parport_ax88796_read_control,
	.frob_control	= parport_ax88796_frob_control,

	.read_status	= parport_ax88796_read_status,

	.enable_irq	= parport_ax88796_enable_irq,
	.disable_irq	= parport_ax88796_disable_irq,

	.data_forward	= parport_ax88796_data_forward,
	.data_reverse	= parport_ax88796_data_reverse,

	.init_state	= parport_ax88796_init_state,
	.save_state	= parport_ax88796_save_state,
	.restore_state	= parport_ax88796_restore_state,

	.epp_write_data	= parport_ieee1284_epp_write_data,
	.epp_read_data	= parport_ieee1284_epp_read_data,
	.epp_write_addr	= parport_ieee1284_epp_write_addr,
	.epp_read_addr	= parport_ieee1284_epp_read_addr,

	.ecp_write_data	= parport_ieee1284_ecp_write_data,
	.ecp_read_data	= parport_ieee1284_ecp_read_data,
	.ecp_write_addr	= parport_ieee1284_ecp_write_addr,

	.compat_write_data	= parport_ieee1284_write_compat,
	.nibble_read_data	= parport_ieee1284_read_nibble,
	.byte_read_data		= parport_ieee1284_read_byte,

	.owner		= THIS_MODULE,
};

static int parport_ax88796_probe(struct platform_device *pdev)
{
	struct device *_dev = &pdev->dev;
	struct ax_drvdata *dd;
	struct parport *pp = NULL;
	struct resource *res;
	unsigned long size;
	int spacing;
	int irq;
	int ret;

	dd = kzalloc(sizeof(struct ax_drvdata), GFP_KERNEL);
	if (dd == NULL) {
		dev_err(_dev, "no memory for private data\n");
		return -ENOMEM;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(_dev, "no MEM specified\n");
		ret = -ENXIO;
		goto exit_mem;
	}

	size = (res->end - res->start) + 1;
	spacing = size / 3;

	dd->io = request_mem_region(res->start, size, pdev->name);
	if (dd->io == NULL) {
		dev_err(_dev, "cannot reserve memory\n");
		ret = -ENXIO;
		goto exit_mem;
	}

	dd->base = ioremap(res->start, size);
	if (dd->base == NULL) {
		dev_err(_dev, "cannot ioremap region\n");
		ret = -ENXIO;
		goto exit_res;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq <= 0)
		irq = PARPORT_IRQ_NONE;

	pp = parport_register_port((unsigned long)dd->base, irq,
				   PARPORT_DMA_NONE,
				   &parport_ax88796_ops);

	if (pp == NULL) {
		dev_err(_dev, "failed to register parallel port\n");
		ret = -ENOMEM;
		goto exit_unmap;
	}

	pp->private_data = dd;
	dd->parport = pp;
	dd->dev = _dev;

	dd->spp_data = dd->base;
	dd->spp_spr  = dd->base + (spacing * 1);
	dd->spp_cpr  = dd->base + (spacing * 2);

	/* initialise the port controls */
	writeb(AX_CPR_STRB, dd->spp_cpr);

	if (irq >= 0) {
		/* request irq */
		ret = request_irq(irq, parport_irq_handler,
				  IRQF_TRIGGER_FALLING, pdev->name, pp);

		if (ret < 0)
			goto exit_port;

		dd->irq_enabled = 1;
	}

	platform_set_drvdata(pdev, pp);

	dev_info(_dev, "attached parallel port driver\n");
	parport_announce_port(pp);

	return 0;

 exit_port:
	parport_remove_port(pp);
 exit_unmap:
	iounmap(dd->base);
 exit_res:
	release_resource(dd->io);
	kfree(dd->io);
 exit_mem:
	kfree(dd);
	return ret;
}

static int parport_ax88796_remove(struct platform_device *pdev)
{
	struct parport *p = platform_get_drvdata(pdev);
	struct ax_drvdata *dd = pp_to_drv(p);

	free_irq(p->irq, p);
	parport_remove_port(p);
	iounmap(dd->base);
	release_resource(dd->io);
	kfree(dd->io);
	kfree(dd);

	return 0;
}

#ifdef CONFIG_PM

static int parport_ax88796_suspend(struct platform_device *dev,
				   pm_message_t state)
{
	struct parport *p = platform_get_drvdata(dev);
	struct ax_drvdata *dd = pp_to_drv(p);

	parport_ax88796_save_state(p, &dd->suspend);
	writeb(AX_CPR_nDOE | AX_CPR_STRB, dd->spp_cpr);
	return 0;
}

static int parport_ax88796_resume(struct platform_device *dev)
{
	struct parport *p = platform_get_drvdata(dev);
	struct ax_drvdata *dd = pp_to_drv(p);

	parport_ax88796_restore_state(p, &dd->suspend);
	return 0;
}

#else
#define parport_ax88796_suspend NULL
#define parport_ax88796_resume  NULL
#endif

static struct platform_driver axdrv = {
	.driver		= {
		.name	= "ax88796-pp",
		.owner	= THIS_MODULE,
	},
	.probe		= parport_ax88796_probe,
	.remove		= parport_ax88796_remove,
	.suspend	= parport_ax88796_suspend,
	.resume		= parport_ax88796_resume,
};

static int __init parport_ax88796_init(void)
{
	return platform_driver_register(&axdrv);
}

static void __exit parport_ax88796_exit(void)
{
	platform_driver_unregister(&axdrv);
}

module_init(parport_ax88796_init)
module_exit(parport_ax88796_exit)

MODULE_AUTHOR("Ben Dooks <ben@simtec.co.uk>");
MODULE_DESCRIPTION("AX88796 Parport parallel port driver");
MODULE_LICENSE("GPL");
