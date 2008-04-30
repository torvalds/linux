/*
 * drivers/ata/pata_mpc52xx.c
 *
 * libata driver for the Freescale MPC52xx on-chip IDE interface
 *
 * Copyright (C) 2006 Sylvain Munaut <tnt@246tNt.com>
 * Copyright (C) 2003 Mipsys - Benjamin Herrenschmidt
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/libata.h>

#include <asm/types.h>
#include <asm/prom.h>
#include <asm/of_platform.h>
#include <asm/mpc52xx.h>


#define DRV_NAME	"mpc52xx_ata"
#define DRV_VERSION	"0.1.2"


/* Private structures used by the driver */
struct mpc52xx_ata_timings {
	u32	pio1;
	u32	pio2;
};

struct mpc52xx_ata_priv {
	unsigned int			ipb_period;
	struct mpc52xx_ata __iomem *	ata_regs;
	int				ata_irq;
	struct mpc52xx_ata_timings	timings[2];
	int				csel;
};


/* ATAPI-4 PIO specs (in ns) */
static const int ataspec_t0[5]    = {600, 383, 240, 180, 120};
static const int ataspec_t1[5]    = { 70,  50,  30,  30,  25};
static const int ataspec_t2_8[5]  = {290, 290, 290,  80,  70};
static const int ataspec_t2_16[5] = {165, 125, 100,  80,  70};
static const int ataspec_t2i[5]   = {  0,   0,   0,  70,  25};
static const int ataspec_t4[5]    = { 30,  20,  15,  10,  10};
static const int ataspec_ta[5]    = { 35,  35,  35,  35,  35};

#define CALC_CLKCYC(c,v) ((((v)+(c)-1)/(c)))


/* Bit definitions inside the registers */
#define MPC52xx_ATA_HOSTCONF_SMR	0x80000000UL /* State machine reset */
#define MPC52xx_ATA_HOSTCONF_FR		0x40000000UL /* FIFO Reset */
#define MPC52xx_ATA_HOSTCONF_IE		0x02000000UL /* Enable interrupt in PIO */
#define MPC52xx_ATA_HOSTCONF_IORDY	0x01000000UL /* Drive supports IORDY protocol */

#define MPC52xx_ATA_HOSTSTAT_TIP	0x80000000UL /* Transaction in progress */
#define MPC52xx_ATA_HOSTSTAT_UREP	0x40000000UL /* UDMA Read Extended Pause */
#define MPC52xx_ATA_HOSTSTAT_RERR	0x02000000UL /* Read Error */
#define MPC52xx_ATA_HOSTSTAT_WERR	0x01000000UL /* Write Error */

#define MPC52xx_ATA_FIFOSTAT_EMPTY	0x01 /* FIFO Empty */

#define MPC52xx_ATA_DMAMODE_WRITE	0x01 /* Write DMA */
#define MPC52xx_ATA_DMAMODE_READ	0x02 /* Read DMA */
#define MPC52xx_ATA_DMAMODE_UDMA	0x04 /* UDMA enabled */
#define MPC52xx_ATA_DMAMODE_IE		0x08 /* Enable drive interrupt to CPU in DMA mode */
#define MPC52xx_ATA_DMAMODE_FE		0x10 /* FIFO Flush enable in Rx mode */
#define MPC52xx_ATA_DMAMODE_FR		0x20 /* FIFO Reset */
#define MPC52xx_ATA_DMAMODE_HUT		0x40 /* Host UDMA burst terminate */


/* Structure of the hardware registers */
struct mpc52xx_ata {

	/* Host interface registers */
	u32 config;		/* ATA + 0x00 Host configuration */
	u32 host_status;	/* ATA + 0x04 Host controller status */
	u32 pio1;		/* ATA + 0x08 PIO Timing 1 */
	u32 pio2;		/* ATA + 0x0c PIO Timing 2 */
	u32 mdma1;		/* ATA + 0x10 MDMA Timing 1 */
	u32 mdma2;		/* ATA + 0x14 MDMA Timing 2 */
	u32 udma1;		/* ATA + 0x18 UDMA Timing 1 */
	u32 udma2;		/* ATA + 0x1c UDMA Timing 2 */
	u32 udma3;		/* ATA + 0x20 UDMA Timing 3 */
	u32 udma4;		/* ATA + 0x24 UDMA Timing 4 */
	u32 udma5;		/* ATA + 0x28 UDMA Timing 5 */
	u32 share_cnt;		/* ATA + 0x2c ATA share counter */
	u32 reserved0[3];

	/* FIFO registers */
	u32 fifo_data;		/* ATA + 0x3c */
	u8  fifo_status_frame;	/* ATA + 0x40 */
	u8  fifo_status;	/* ATA + 0x41 */
	u16 reserved7[1];
	u8  fifo_control;	/* ATA + 0x44 */
	u8  reserved8[5];
	u16 fifo_alarm;		/* ATA + 0x4a */
	u16 reserved9;
	u16 fifo_rdp;		/* ATA + 0x4e */
	u16 reserved10;
	u16 fifo_wrp;		/* ATA + 0x52 */
	u16 reserved11;
	u16 fifo_lfrdp;		/* ATA + 0x56 */
	u16 reserved12;
	u16 fifo_lfwrp;		/* ATA + 0x5a */

	/* Drive TaskFile registers */
	u8  tf_control;		/* ATA + 0x5c TASKFILE Control/Alt Status */
	u8  reserved13[3];
	u16 tf_data;		/* ATA + 0x60 TASKFILE Data */
	u16 reserved14;
	u8  tf_features;	/* ATA + 0x64 TASKFILE Features/Error */
	u8  reserved15[3];
	u8  tf_sec_count;	/* ATA + 0x68 TASKFILE Sector Count */
	u8  reserved16[3];
	u8  tf_sec_num;		/* ATA + 0x6c TASKFILE Sector Number */
	u8  reserved17[3];
	u8  tf_cyl_low;		/* ATA + 0x70 TASKFILE Cylinder Low */
	u8  reserved18[3];
	u8  tf_cyl_high;	/* ATA + 0x74 TASKFILE Cylinder High */
	u8  reserved19[3];
	u8  tf_dev_head;	/* ATA + 0x78 TASKFILE Device/Head */
	u8  reserved20[3];
	u8  tf_command;		/* ATA + 0x7c TASKFILE Command/Status */
	u8  dma_mode;		/* ATA + 0x7d ATA Host DMA Mode configuration */
	u8  reserved21[2];
};


/* ======================================================================== */
/* Aux fns                                                                  */
/* ======================================================================== */


/* MPC52xx low level hw control */

static int
mpc52xx_ata_compute_pio_timings(struct mpc52xx_ata_priv *priv, int dev, int pio)
{
	struct mpc52xx_ata_timings *timing = &priv->timings[dev];
	unsigned int ipb_period = priv->ipb_period;
	unsigned int t0, t1, t2_8, t2_16, t2i, t4, ta;

	if ((pio<0) || (pio>4))
		return -EINVAL;

	t0	= CALC_CLKCYC(ipb_period, 1000 * ataspec_t0[pio]);
	t1	= CALC_CLKCYC(ipb_period, 1000 * ataspec_t1[pio]);
	t2_8	= CALC_CLKCYC(ipb_period, 1000 * ataspec_t2_8[pio]);
	t2_16	= CALC_CLKCYC(ipb_period, 1000 * ataspec_t2_16[pio]);
	t2i	= CALC_CLKCYC(ipb_period, 1000 * ataspec_t2i[pio]);
	t4	= CALC_CLKCYC(ipb_period, 1000 * ataspec_t4[pio]);
	ta	= CALC_CLKCYC(ipb_period, 1000 * ataspec_ta[pio]);

	timing->pio1 = (t0 << 24) | (t2_8 << 16) | (t2_16 << 8) | (t2i);
	timing->pio2 = (t4 << 24) | (t1 << 16) | (ta << 8);

	return 0;
}

static void
mpc52xx_ata_apply_timings(struct mpc52xx_ata_priv *priv, int device)
{
	struct mpc52xx_ata __iomem *regs = priv->ata_regs;
	struct mpc52xx_ata_timings *timing = &priv->timings[device];

	out_be32(&regs->pio1,  timing->pio1);
	out_be32(&regs->pio2,  timing->pio2);
	out_be32(&regs->mdma1, 0);
	out_be32(&regs->mdma2, 0);
	out_be32(&regs->udma1, 0);
	out_be32(&regs->udma2, 0);
	out_be32(&regs->udma3, 0);
	out_be32(&regs->udma4, 0);
	out_be32(&regs->udma5, 0);

	priv->csel = device;
}

static int
mpc52xx_ata_hw_init(struct mpc52xx_ata_priv *priv)
{
	struct mpc52xx_ata __iomem *regs = priv->ata_regs;
	int tslot;

	/* Clear share_cnt (all sample code do this ...) */
	out_be32(&regs->share_cnt, 0);

	/* Configure and reset host */
	out_be32(&regs->config,
			MPC52xx_ATA_HOSTCONF_IE |
			MPC52xx_ATA_HOSTCONF_IORDY |
			MPC52xx_ATA_HOSTCONF_SMR |
			MPC52xx_ATA_HOSTCONF_FR);

	udelay(10);

	out_be32(&regs->config,
			MPC52xx_ATA_HOSTCONF_IE |
			MPC52xx_ATA_HOSTCONF_IORDY);

	/* Set the time slot to 1us */
	tslot = CALC_CLKCYC(priv->ipb_period, 1000000);
	out_be32(&regs->share_cnt, tslot << 16 );

	/* Init timings to PIO0 */
	memset(priv->timings, 0x00, 2*sizeof(struct mpc52xx_ata_timings));

	mpc52xx_ata_compute_pio_timings(priv, 0, 0);
	mpc52xx_ata_compute_pio_timings(priv, 1, 0);

	mpc52xx_ata_apply_timings(priv, 0);

	return 0;
}


/* ======================================================================== */
/* libata driver                                                            */
/* ======================================================================== */

static void
mpc52xx_ata_set_piomode(struct ata_port *ap, struct ata_device *adev)
{
	struct mpc52xx_ata_priv *priv = ap->host->private_data;
	int pio, rv;

	pio = adev->pio_mode - XFER_PIO_0;

	rv = mpc52xx_ata_compute_pio_timings(priv, adev->devno, pio);

	if (rv) {
		printk(KERN_ERR DRV_NAME
			": Trying to select invalid PIO mode %d\n", pio);
		return;
	}

	mpc52xx_ata_apply_timings(priv, adev->devno);
}
static void
mpc52xx_ata_dev_select(struct ata_port *ap, unsigned int device)
{
	struct mpc52xx_ata_priv *priv = ap->host->private_data;

	if (device != priv->csel)
		mpc52xx_ata_apply_timings(priv, device);

	ata_sff_dev_select(ap,device);
}

static struct scsi_host_template mpc52xx_ata_sht = {
	ATA_PIO_SHT(DRV_NAME),
};

static struct ata_port_operations mpc52xx_ata_port_ops = {
	.inherits		= &ata_sff_port_ops,
	.sff_dev_select		= mpc52xx_ata_dev_select,
	.cable_detect		= ata_cable_40wire,
	.set_piomode		= mpc52xx_ata_set_piomode,
	.post_internal_cmd	= ATA_OP_NULL,
};

static int __devinit
mpc52xx_ata_init_one(struct device *dev, struct mpc52xx_ata_priv *priv,
		     unsigned long raw_ata_regs)
{
	struct ata_host *host;
	struct ata_port *ap;
	struct ata_ioports *aio;

	host = ata_host_alloc(dev, 1);
	if (!host)
		return -ENOMEM;

	ap = host->ports[0];
	ap->flags		|= ATA_FLAG_SLAVE_POSS;
	ap->pio_mask		= 0x1f;	/* Up to PIO4 */
	ap->mwdma_mask		= 0x00;	/* No MWDMA   */
	ap->udma_mask		= 0x00;	/* No UDMA    */
	ap->ops			= &mpc52xx_ata_port_ops;
	host->private_data	= priv;

	aio = &ap->ioaddr;
	aio->cmd_addr		= NULL;	/* Don't have a classic reg block */
	aio->altstatus_addr	= &priv->ata_regs->tf_control;
	aio->ctl_addr		= &priv->ata_regs->tf_control;
	aio->data_addr		= &priv->ata_regs->tf_data;
	aio->error_addr		= &priv->ata_regs->tf_features;
	aio->feature_addr	= &priv->ata_regs->tf_features;
	aio->nsect_addr		= &priv->ata_regs->tf_sec_count;
	aio->lbal_addr		= &priv->ata_regs->tf_sec_num;
	aio->lbam_addr		= &priv->ata_regs->tf_cyl_low;
	aio->lbah_addr		= &priv->ata_regs->tf_cyl_high;
	aio->device_addr	= &priv->ata_regs->tf_dev_head;
	aio->status_addr	= &priv->ata_regs->tf_command;
	aio->command_addr	= &priv->ata_regs->tf_command;

	ata_port_desc(ap, "ata_regs 0x%lx", raw_ata_regs);

	/* activate host */
	return ata_host_activate(host, priv->ata_irq, ata_sff_interrupt, 0,
				 &mpc52xx_ata_sht);
}

static struct mpc52xx_ata_priv *
mpc52xx_ata_remove_one(struct device *dev)
{
	struct ata_host *host = dev_get_drvdata(dev);
	struct mpc52xx_ata_priv *priv = host->private_data;

	ata_host_detach(host);

	return priv;
}


/* ======================================================================== */
/* OF Platform driver                                                       */
/* ======================================================================== */

static int __devinit
mpc52xx_ata_probe(struct of_device *op, const struct of_device_id *match)
{
	unsigned int ipb_freq;
	struct resource res_mem;
	int ata_irq;
	struct mpc52xx_ata __iomem *ata_regs;
	struct mpc52xx_ata_priv *priv;
	int rv;

	/* Get ipb frequency */
	ipb_freq = mpc52xx_find_ipb_freq(op->node);
	if (!ipb_freq) {
		printk(KERN_ERR DRV_NAME ": "
			"Unable to find IPB Bus frequency\n" );
		return -ENODEV;
	}

	/* Get IRQ and register */
	rv = of_address_to_resource(op->node, 0, &res_mem);
	if (rv) {
		printk(KERN_ERR DRV_NAME ": "
			"Error while parsing device node resource\n" );
		return rv;
	}

	ata_irq = irq_of_parse_and_map(op->node, 0);
	if (ata_irq == NO_IRQ) {
		printk(KERN_ERR DRV_NAME ": "
			"Error while mapping the irq\n");
		return -EINVAL;
	}

	/* Request mem region */
	if (!devm_request_mem_region(&op->dev, res_mem.start,
				     sizeof(struct mpc52xx_ata), DRV_NAME)) {
		printk(KERN_ERR DRV_NAME ": "
			"Error while requesting mem region\n");
		rv = -EBUSY;
		goto err;
	}

	/* Remap registers */
	ata_regs = devm_ioremap(&op->dev, res_mem.start,
				sizeof(struct mpc52xx_ata));
	if (!ata_regs) {
		printk(KERN_ERR DRV_NAME ": "
			"Error while mapping register set\n");
		rv = -ENOMEM;
		goto err;
	}

	/* Prepare our private structure */
	priv = devm_kzalloc(&op->dev, sizeof(struct mpc52xx_ata_priv),
			    GFP_ATOMIC);
	if (!priv) {
		printk(KERN_ERR DRV_NAME ": "
			"Error while allocating private structure\n");
		rv = -ENOMEM;
		goto err;
	}

	priv->ipb_period = 1000000000 / (ipb_freq / 1000);
	priv->ata_regs = ata_regs;
	priv->ata_irq = ata_irq;
	priv->csel = -1;

	/* Init the hw */
	rv = mpc52xx_ata_hw_init(priv);
	if (rv) {
		printk(KERN_ERR DRV_NAME ": Error during HW init\n");
		goto err;
	}

	/* Register ourselves to libata */
	rv = mpc52xx_ata_init_one(&op->dev, priv, res_mem.start);
	if (rv) {
		printk(KERN_ERR DRV_NAME ": "
			"Error while registering to ATA layer\n");
		return rv;
	}

	/* Done */
	return 0;

	/* Error path */
err:
	irq_dispose_mapping(ata_irq);
	return rv;
}

static int
mpc52xx_ata_remove(struct of_device *op)
{
	struct mpc52xx_ata_priv *priv;

	priv = mpc52xx_ata_remove_one(&op->dev);
	irq_dispose_mapping(priv->ata_irq);

	return 0;
}


#ifdef CONFIG_PM

static int
mpc52xx_ata_suspend(struct of_device *op, pm_message_t state)
{
	struct ata_host *host = dev_get_drvdata(&op->dev);

	return ata_host_suspend(host, state);
}

static int
mpc52xx_ata_resume(struct of_device *op)
{
	struct ata_host *host = dev_get_drvdata(&op->dev);
	struct mpc52xx_ata_priv *priv = host->private_data;
	int rv;

	rv = mpc52xx_ata_hw_init(priv);
	if (rv) {
		printk(KERN_ERR DRV_NAME ": Error during HW init\n");
		return rv;
	}

	ata_host_resume(host);

	return 0;
}

#endif


static struct of_device_id mpc52xx_ata_of_match[] = {
	{ .compatible = "fsl,mpc5200-ata", },
	{ .compatible = "mpc5200-ata", },
	{},
};


static struct of_platform_driver mpc52xx_ata_of_platform_driver = {
	.owner		= THIS_MODULE,
	.name		= DRV_NAME,
	.match_table	= mpc52xx_ata_of_match,
	.probe		= mpc52xx_ata_probe,
	.remove		= mpc52xx_ata_remove,
#ifdef CONFIG_PM
	.suspend	= mpc52xx_ata_suspend,
	.resume		= mpc52xx_ata_resume,
#endif
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};


/* ======================================================================== */
/* Module                                                                   */
/* ======================================================================== */

static int __init
mpc52xx_ata_init(void)
{
	printk(KERN_INFO "ata: MPC52xx IDE/ATA libata driver\n");
	return of_register_platform_driver(&mpc52xx_ata_of_platform_driver);
}

static void __exit
mpc52xx_ata_exit(void)
{
	of_unregister_platform_driver(&mpc52xx_ata_of_platform_driver);
}

module_init(mpc52xx_ata_init);
module_exit(mpc52xx_ata_exit);

MODULE_AUTHOR("Sylvain Munaut <tnt@246tNt.com>");
MODULE_DESCRIPTION("Freescale MPC52xx IDE/ATA libata driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, mpc52xx_ata_of_match);
MODULE_VERSION(DRV_VERSION);

