/*
 * drivers/ata/pata_mpc52xx.c
 *
 * libata driver for the Freescale MPC52xx on-chip IDE interface
 *
 * Copyright (C) 2006 Sylvain Munaut <tnt@246tNt.com>
 * Copyright (C) 2003 Mipsys - Benjamin Herrenschmidt
 *
 * UDMA support based on patches by Freescale (Bernard Kuhn, John Rigby),
 * Domen Puncer and Tim Yamin.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gfp.h>
#include <linux/delay.h>
#include <linux/libata.h>
#include <linux/of_platform.h>
#include <linux/types.h>

#include <asm/cacheflush.h>
#include <asm/prom.h>
#include <asm/mpc52xx.h>

#include <linux/fsl/bestcomm/bestcomm.h>
#include <linux/fsl/bestcomm/bestcomm_priv.h>
#include <linux/fsl/bestcomm/ata.h>

#define DRV_NAME	"mpc52xx_ata"

/* Private structures used by the driver */
struct mpc52xx_ata_timings {
	u32	pio1;
	u32	pio2;
	u32	mdma1;
	u32	mdma2;
	u32	udma1;
	u32	udma2;
	u32	udma3;
	u32	udma4;
	u32	udma5;
	int	using_udma;
};

struct mpc52xx_ata_priv {
	unsigned int			ipb_period;
	struct mpc52xx_ata __iomem	*ata_regs;
	phys_addr_t			ata_regs_pa;
	int				ata_irq;
	struct mpc52xx_ata_timings	timings[2];
	int				csel;

	/* DMA */
	struct bcom_task		*dmatsk;
	const struct udmaspec		*udmaspec;
	const struct mdmaspec		*mdmaspec;
	int 				mpc52xx_ata_dma_last_write;
	int				waiting_for_dma;
};


/* ATAPI-4 PIO specs (in ns) */
static const u16 ataspec_t0[5]		= {600, 383, 240, 180, 120};
static const u16 ataspec_t1[5]		= { 70,  50,  30,  30,  25};
static const u16 ataspec_t2_8[5]	= {290, 290, 290,  80,  70};
static const u16 ataspec_t2_16[5]	= {165, 125, 100,  80,  70};
static const u16 ataspec_t2i[5]		= {  0,   0,   0,  70,  25};
static const u16 ataspec_t4[5]		= { 30,  20,  15,  10,  10};
static const u16 ataspec_ta[5]		= { 35,  35,  35,  35,  35};

#define CALC_CLKCYC(c,v) ((((v)+(c)-1)/(c)))

/* ======================================================================== */

/* ATAPI-4 MDMA specs (in clocks) */
struct mdmaspec {
	u8 t0M;
	u8 td;
	u8 th;
	u8 tj;
	u8 tkw;
	u8 tm;
	u8 tn;
};

static const struct mdmaspec mdmaspec66[3] = {
	{ .t0M = 32, .td = 15, .th = 2, .tj = 2, .tkw = 15, .tm = 4, .tn = 1 },
	{ .t0M = 10, .td = 6,  .th = 1, .tj = 1, .tkw = 4,  .tm = 2, .tn = 1 },
	{ .t0M = 8,  .td = 5,  .th = 1, .tj = 1, .tkw = 2,  .tm = 2, .tn = 1 },
};

static const struct mdmaspec mdmaspec132[3] = {
	{ .t0M = 64, .td = 29, .th = 3, .tj = 3, .tkw = 29, .tm = 7, .tn = 2 },
	{ .t0M = 20, .td = 11, .th = 2, .tj = 1, .tkw = 7,  .tm = 4, .tn = 1 },
	{ .t0M = 16, .td = 10, .th = 2, .tj = 1, .tkw = 4,  .tm = 4, .tn = 1 },
};

/* ATAPI-4 UDMA specs (in clocks) */
struct udmaspec {
	u8 tcyc;
	u8 t2cyc;
	u8 tds;
	u8 tdh;
	u8 tdvs;
	u8 tdvh;
	u8 tfs;
	u8 tli;
	u8 tmli;
	u8 taz;
	u8 tzah;
	u8 tenv;
	u8 tsr;
	u8 trfs;
	u8 trp;
	u8 tack;
	u8 tss;
};

static const struct udmaspec udmaspec66[6] = {
	{ .tcyc = 8,  .t2cyc = 16, .tds  = 1,  .tdh  = 1, .tdvs = 5,  .tdvh = 1,
	  .tfs  = 16, .tli   = 10, .tmli = 2,  .taz  = 1, .tzah = 2,  .tenv = 2,
	  .tsr  = 3,  .trfs  = 5,  .trp  = 11, .tack = 2, .tss  = 4,
	},
	{ .tcyc = 5,  .t2cyc = 11, .tds  = 1,  .tdh  = 1, .tdvs = 4,  .tdvh = 1,
	  .tfs  = 14, .tli   = 10, .tmli = 2,  .taz  = 1, .tzah = 2,  .tenv = 2,
	  .tsr  = 2,  .trfs  = 5,  .trp  = 9,  .tack = 2, .tss  = 4,
	},
	{ .tcyc = 4,  .t2cyc = 8,  .tds  = 1,  .tdh  = 1, .tdvs = 3,  .tdvh = 1,
	  .tfs  = 12, .tli   = 10, .tmli = 2,  .taz  = 1, .tzah = 2,  .tenv = 2,
	  .tsr  = 2,  .trfs  = 4,  .trp  = 7,  .tack = 2, .tss  = 4,
	},
	{ .tcyc = 3,  .t2cyc = 6,  .tds  = 1,  .tdh  = 1, .tdvs = 2,  .tdvh = 1,
	  .tfs  = 9,  .tli   = 7,  .tmli = 2,  .taz  = 1, .tzah = 2,  .tenv = 2,
	  .tsr  = 2,  .trfs  = 4,  .trp  = 7,  .tack = 2, .tss  = 4,
	},
	{ .tcyc = 2,  .t2cyc = 4,  .tds  = 1,  .tdh  = 1, .tdvs = 1,  .tdvh = 1,
	  .tfs  = 8,  .tli   = 8,  .tmli = 2,  .taz  = 1, .tzah = 2,  .tenv = 2,
	  .tsr  = 2,  .trfs  = 4,  .trp  = 7,  .tack = 2, .tss  = 4,
	},
	{ .tcyc = 2,  .t2cyc = 2,  .tds  = 1,  .tdh  = 1, .tdvs = 1,  .tdvh = 1,
	  .tfs  = 6,  .tli   = 5,  .tmli = 2,  .taz  = 1, .tzah = 2,  .tenv = 2,
	  .tsr  = 2,  .trfs  = 4,  .trp  = 6,  .tack = 2, .tss  = 4,
	},
};

static const struct udmaspec udmaspec132[6] = {
	{ .tcyc = 15, .t2cyc = 31, .tds  = 2,  .tdh  = 1, .tdvs = 10, .tdvh = 1,
	  .tfs  = 30, .tli   = 20, .tmli = 3,  .taz  = 2, .tzah = 3,  .tenv = 3,
	  .tsr  = 7,  .trfs  = 10, .trp  = 22, .tack = 3, .tss  = 7,
	},
	{ .tcyc = 10, .t2cyc = 21, .tds  = 2,  .tdh  = 1, .tdvs = 7,  .tdvh = 1,
	  .tfs  = 27, .tli   = 20, .tmli = 3,  .taz  = 2, .tzah = 3,  .tenv = 3,
	  .tsr  = 4,  .trfs  = 10, .trp  = 17, .tack = 3, .tss  = 7,
	},
	{ .tcyc = 6,  .t2cyc = 12, .tds  = 1,  .tdh  = 1, .tdvs = 5,  .tdvh = 1,
	  .tfs  = 23, .tli   = 20, .tmli = 3,  .taz  = 2, .tzah = 3,  .tenv = 3,
	  .tsr  = 3,  .trfs  = 8,  .trp  = 14, .tack = 3, .tss  = 7,
	},
	{ .tcyc = 7,  .t2cyc = 12, .tds  = 1,  .tdh  = 1, .tdvs = 3,  .tdvh = 1,
	  .tfs  = 15, .tli   = 13, .tmli = 3,  .taz  = 2, .tzah = 3,  .tenv = 3,
	  .tsr  = 3,  .trfs  = 8,  .trp  = 14, .tack = 3, .tss  = 7,
	},
	{ .tcyc = 2,  .t2cyc = 5,  .tds  = 0,  .tdh  = 0, .tdvs = 1,  .tdvh = 1,
	  .tfs  = 16, .tli   = 14, .tmli = 2,  .taz  = 1, .tzah = 2,  .tenv = 2,
	  .tsr  = 2,  .trfs  = 7,  .trp  = 13, .tack = 2, .tss  = 6,
	},
	{ .tcyc = 3,  .t2cyc = 6,  .tds  = 1,  .tdh  = 1, .tdvs = 1,  .tdvh = 1,
	  .tfs  = 12, .tli   = 10, .tmli = 3,  .taz  = 2, .tzah = 3,  .tenv = 3,
	  .tsr  = 3,  .trfs  = 7,  .trp  = 12, .tack = 3, .tss  = 7,
	},
};

/* ======================================================================== */

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
#define MPC52xx_ATA_FIFOSTAT_ERROR	0x40 /* FIFO Error */

#define MPC52xx_ATA_DMAMODE_WRITE	0x01 /* Write DMA */
#define MPC52xx_ATA_DMAMODE_READ	0x02 /* Read DMA */
#define MPC52xx_ATA_DMAMODE_UDMA	0x04 /* UDMA enabled */
#define MPC52xx_ATA_DMAMODE_IE		0x08 /* Enable drive interrupt to CPU in DMA mode */
#define MPC52xx_ATA_DMAMODE_FE		0x10 /* FIFO Flush enable in Rx mode */
#define MPC52xx_ATA_DMAMODE_FR		0x20 /* FIFO Reset */
#define MPC52xx_ATA_DMAMODE_HUT		0x40 /* Host UDMA burst terminate */

#define MAX_DMA_BUFFERS 128
#define MAX_DMA_BUFFER_SIZE 0x20000u

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
	u32 t0, t1, t2_8, t2_16, t2i, t4, ta;

	if ((pio < 0) || (pio > 4))
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

static int
mpc52xx_ata_compute_mdma_timings(struct mpc52xx_ata_priv *priv, int dev,
				 int speed)
{
	struct mpc52xx_ata_timings *t = &priv->timings[dev];
	const struct mdmaspec *s = &priv->mdmaspec[speed];

	if (speed < 0 || speed > 2)
		return -EINVAL;

	t->mdma1 = ((u32)s->t0M << 24) | ((u32)s->td << 16) | ((u32)s->tkw << 8) | s->tm;
	t->mdma2 = ((u32)s->th << 24) | ((u32)s->tj << 16) | ((u32)s->tn << 8);
	t->using_udma = 0;

	return 0;
}

static int
mpc52xx_ata_compute_udma_timings(struct mpc52xx_ata_priv *priv, int dev,
				 int speed)
{
	struct mpc52xx_ata_timings *t = &priv->timings[dev];
	const struct udmaspec *s = &priv->udmaspec[speed];

	if (speed < 0 || speed > 2)
		return -EINVAL;

	t->udma1 = ((u32)s->t2cyc << 24) | ((u32)s->tcyc << 16) | ((u32)s->tds << 8) | s->tdh;
	t->udma2 = ((u32)s->tdvs << 24) | ((u32)s->tdvh << 16) | ((u32)s->tfs << 8) | s->tli;
	t->udma3 = ((u32)s->tmli << 24) | ((u32)s->taz << 16) | ((u32)s->tenv << 8) | s->tsr;
	t->udma4 = ((u32)s->tss << 24) | ((u32)s->trfs << 16) | ((u32)s->trp << 8) | s->tack;
	t->udma5 = (u32)s->tzah << 24;
	t->using_udma = 1;

	return 0;
}

static void
mpc52xx_ata_apply_timings(struct mpc52xx_ata_priv *priv, int device)
{
	struct mpc52xx_ata __iomem *regs = priv->ata_regs;
	struct mpc52xx_ata_timings *timing = &priv->timings[device];

	out_be32(&regs->pio1,  timing->pio1);
	out_be32(&regs->pio2,  timing->pio2);
	out_be32(&regs->mdma1, timing->mdma1);
	out_be32(&regs->mdma2, timing->mdma2);
	out_be32(&regs->udma1, timing->udma1);
	out_be32(&regs->udma2, timing->udma2);
	out_be32(&regs->udma3, timing->udma3);
	out_be32(&regs->udma4, timing->udma4);
	out_be32(&regs->udma5, timing->udma5);
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
	out_be32(&regs->share_cnt, tslot << 16);

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
		dev_err(ap->dev, "error: invalid PIO mode: %d\n", pio);
		return;
	}

	mpc52xx_ata_apply_timings(priv, adev->devno);
}

static void
mpc52xx_ata_set_dmamode(struct ata_port *ap, struct ata_device *adev)
{
	struct mpc52xx_ata_priv *priv = ap->host->private_data;
	int rv;

	if (adev->dma_mode >= XFER_UDMA_0) {
		int dma = adev->dma_mode - XFER_UDMA_0;
		rv = mpc52xx_ata_compute_udma_timings(priv, adev->devno, dma);
	} else {
		int dma = adev->dma_mode - XFER_MW_DMA_0;
		rv = mpc52xx_ata_compute_mdma_timings(priv, adev->devno, dma);
	}

	if (rv) {
		dev_alert(ap->dev,
			"Trying to select invalid DMA mode %d\n",
			adev->dma_mode);
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

	ata_sff_dev_select(ap, device);
}

static int
mpc52xx_ata_build_dmatable(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct mpc52xx_ata_priv *priv = ap->host->private_data;
	struct bcom_ata_bd *bd;
	unsigned int read = !(qc->tf.flags & ATA_TFLAG_WRITE), si;
	struct scatterlist *sg;
	int count = 0;

	if (read)
		bcom_ata_rx_prepare(priv->dmatsk);
	else
		bcom_ata_tx_prepare(priv->dmatsk);

	for_each_sg(qc->sg, sg, qc->n_elem, si) {
		dma_addr_t cur_addr = sg_dma_address(sg);
		u32 cur_len = sg_dma_len(sg);

		while (cur_len) {
			unsigned int tc = min(cur_len, MAX_DMA_BUFFER_SIZE);
			bd = (struct bcom_ata_bd *)
				bcom_prepare_next_buffer(priv->dmatsk);

			if (read) {
				bd->status = tc;
				bd->src_pa = (__force u32) priv->ata_regs_pa +
					offsetof(struct mpc52xx_ata, fifo_data);
				bd->dst_pa = (__force u32) cur_addr;
			} else {
				bd->status = tc;
				bd->src_pa = (__force u32) cur_addr;
				bd->dst_pa = (__force u32) priv->ata_regs_pa +
					offsetof(struct mpc52xx_ata, fifo_data);
			}

			bcom_submit_next_buffer(priv->dmatsk, NULL);

			cur_addr += tc;
			cur_len -= tc;
			count++;

			if (count > MAX_DMA_BUFFERS) {
				dev_alert(ap->dev, "dma table"
					"too small\n");
				goto use_pio_instead;
			}
		}
	}
	return 1;

 use_pio_instead:
	bcom_ata_reset_bd(priv->dmatsk);
	return 0;
}

static void
mpc52xx_bmdma_setup(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct mpc52xx_ata_priv *priv = ap->host->private_data;
	struct mpc52xx_ata __iomem *regs = priv->ata_regs;

	unsigned int read = !(qc->tf.flags & ATA_TFLAG_WRITE);
	u8 dma_mode;

	if (!mpc52xx_ata_build_dmatable(qc))
		dev_alert(ap->dev, "%s: %i, return 1?\n",
			__func__, __LINE__);

	/* Check FIFO is OK... */
	if (in_8(&priv->ata_regs->fifo_status) & MPC52xx_ATA_FIFOSTAT_ERROR)
		dev_alert(ap->dev, "%s: FIFO error detected: 0x%02x!\n",
			__func__, in_8(&priv->ata_regs->fifo_status));

	if (read) {
		dma_mode = MPC52xx_ATA_DMAMODE_IE | MPC52xx_ATA_DMAMODE_READ |
				MPC52xx_ATA_DMAMODE_FE;

		/* Setup FIFO if direction changed */
		if (priv->mpc52xx_ata_dma_last_write != 0) {
			priv->mpc52xx_ata_dma_last_write = 0;

			/* Configure FIFO with granularity to 7 */
			out_8(&regs->fifo_control, 7);
			out_be16(&regs->fifo_alarm, 128);

			/* Set FIFO Reset bit (FR) */
			out_8(&regs->dma_mode, MPC52xx_ATA_DMAMODE_FR);
		}
	} else {
		dma_mode = MPC52xx_ATA_DMAMODE_IE | MPC52xx_ATA_DMAMODE_WRITE;

		/* Setup FIFO if direction changed */
		if (priv->mpc52xx_ata_dma_last_write != 1) {
			priv->mpc52xx_ata_dma_last_write = 1;

			/* Configure FIFO with granularity to 4 */
			out_8(&regs->fifo_control, 4);
			out_be16(&regs->fifo_alarm, 128);
		}
	}

	if (priv->timings[qc->dev->devno].using_udma)
		dma_mode |= MPC52xx_ATA_DMAMODE_UDMA;

	out_8(&regs->dma_mode, dma_mode);
	priv->waiting_for_dma = ATA_DMA_ACTIVE;

	ata_wait_idle(ap);
	ap->ops->sff_exec_command(ap, &qc->tf);
}

static void
mpc52xx_bmdma_start(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct mpc52xx_ata_priv *priv = ap->host->private_data;

	bcom_set_task_auto_start(priv->dmatsk->tasknum, priv->dmatsk->tasknum);
	bcom_enable(priv->dmatsk);
}

static void
mpc52xx_bmdma_stop(struct ata_queued_cmd *qc)
{
	struct ata_port *ap = qc->ap;
	struct mpc52xx_ata_priv *priv = ap->host->private_data;

	bcom_disable(priv->dmatsk);
	bcom_ata_reset_bd(priv->dmatsk);
	priv->waiting_for_dma = 0;

	/* Check FIFO is OK... */
	if (in_8(&priv->ata_regs->fifo_status) & MPC52xx_ATA_FIFOSTAT_ERROR)
		dev_alert(ap->dev, "%s: FIFO error detected: 0x%02x!\n",
			__func__, in_8(&priv->ata_regs->fifo_status));
}

static u8
mpc52xx_bmdma_status(struct ata_port *ap)
{
	struct mpc52xx_ata_priv *priv = ap->host->private_data;

	/* Check FIFO is OK... */
	if (in_8(&priv->ata_regs->fifo_status) & MPC52xx_ATA_FIFOSTAT_ERROR) {
		dev_alert(ap->dev, "%s: FIFO error detected: 0x%02x!\n",
			__func__, in_8(&priv->ata_regs->fifo_status));
		return priv->waiting_for_dma | ATA_DMA_ERR;
	}

	return priv->waiting_for_dma;
}

static irqreturn_t
mpc52xx_ata_task_irq(int irq, void *vpriv)
{
	struct mpc52xx_ata_priv *priv = vpriv;
	while (bcom_buffer_done(priv->dmatsk))
		bcom_retrieve_buffer(priv->dmatsk, NULL, NULL);

	priv->waiting_for_dma |= ATA_DMA_INTR;

	return IRQ_HANDLED;
}

static struct scsi_host_template mpc52xx_ata_sht = {
	ATA_PIO_SHT(DRV_NAME),
};

static struct ata_port_operations mpc52xx_ata_port_ops = {
	.inherits		= &ata_bmdma_port_ops,
	.sff_dev_select		= mpc52xx_ata_dev_select,
	.set_piomode		= mpc52xx_ata_set_piomode,
	.set_dmamode		= mpc52xx_ata_set_dmamode,
	.bmdma_setup		= mpc52xx_bmdma_setup,
	.bmdma_start		= mpc52xx_bmdma_start,
	.bmdma_stop		= mpc52xx_bmdma_stop,
	.bmdma_status		= mpc52xx_bmdma_status,
	.qc_prep		= ata_noop_qc_prep,
};

static int mpc52xx_ata_init_one(struct device *dev,
				struct mpc52xx_ata_priv *priv,
				unsigned long raw_ata_regs,
				int mwdma_mask, int udma_mask)
{
	struct ata_host *host;
	struct ata_port *ap;
	struct ata_ioports *aio;

	host = ata_host_alloc(dev, 1);
	if (!host)
		return -ENOMEM;

	ap = host->ports[0];
	ap->flags		|= ATA_FLAG_SLAVE_POSS;
	ap->pio_mask		= ATA_PIO4;
	ap->mwdma_mask		= mwdma_mask;
	ap->udma_mask		= udma_mask;
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
	return ata_host_activate(host, priv->ata_irq, ata_bmdma_interrupt, 0,
				 &mpc52xx_ata_sht);
}

/* ======================================================================== */
/* OF Platform driver                                                       */
/* ======================================================================== */

static int mpc52xx_ata_probe(struct platform_device *op)
{
	unsigned int ipb_freq;
	struct resource res_mem;
	int ata_irq = 0;
	struct mpc52xx_ata __iomem *ata_regs;
	struct mpc52xx_ata_priv *priv = NULL;
	int rv, task_irq;
	int mwdma_mask = 0, udma_mask = 0;
	const __be32 *prop;
	int proplen;
	struct bcom_task *dmatsk;

	/* Get ipb frequency */
	ipb_freq = mpc5xxx_get_bus_frequency(op->dev.of_node);
	if (!ipb_freq) {
		dev_err(&op->dev, "could not determine IPB bus frequency\n");
		return -ENODEV;
	}

	/* Get device base address from device tree, request the region
	 * and ioremap it. */
	rv = of_address_to_resource(op->dev.of_node, 0, &res_mem);
	if (rv) {
		dev_err(&op->dev, "could not determine device base address\n");
		return rv;
	}

	if (!devm_request_mem_region(&op->dev, res_mem.start,
				     sizeof(*ata_regs), DRV_NAME)) {
		dev_err(&op->dev, "error requesting register region\n");
		return -EBUSY;
	}

	ata_regs = devm_ioremap(&op->dev, res_mem.start, sizeof(*ata_regs));
	if (!ata_regs) {
		dev_err(&op->dev, "error mapping device registers\n");
		return -ENOMEM;
	}

	/*
	 * By default, all DMA modes are disabled for the MPC5200.  Some
	 * boards don't have the required signals routed to make DMA work.
	 * Also, the MPC5200B has a silicon bug that causes data corruption
	 * with UDMA if it is used at the same time as the LocalPlus bus.
	 *
	 * Instead of trying to guess what modes are usable, check the
	 * ATA device tree node to find out what DMA modes work on the board.
	 * UDMA/MWDMA modes can also be forced by adding "libata.force=<mode>"
	 * to the kernel boot parameters.
	 *
	 * The MPC5200 ATA controller supports MWDMA modes 0, 1 and 2 and
	 * UDMA modes 0, 1 and 2.
	 */
	prop = of_get_property(op->dev.of_node, "mwdma-mode", &proplen);
	if ((prop) && (proplen >= 4))
		mwdma_mask = ATA_MWDMA2 & ((1 << (*prop + 1)) - 1);
	prop = of_get_property(op->dev.of_node, "udma-mode", &proplen);
	if ((prop) && (proplen >= 4))
		udma_mask = ATA_UDMA2 & ((1 << (*prop + 1)) - 1);

	ata_irq = irq_of_parse_and_map(op->dev.of_node, 0);
	if (ata_irq == NO_IRQ) {
		dev_err(&op->dev, "error mapping irq\n");
		return -EINVAL;
	}

	/* Prepare our private structure */
	priv = devm_kzalloc(&op->dev, sizeof(*priv), GFP_ATOMIC);
	if (!priv) {
		dev_err(&op->dev, "error allocating private structure\n");
		rv = -ENOMEM;
		goto err1;
	}

	priv->ipb_period = 1000000000 / (ipb_freq / 1000);
	priv->ata_regs = ata_regs;
	priv->ata_regs_pa = res_mem.start;
	priv->ata_irq = ata_irq;
	priv->csel = -1;
	priv->mpc52xx_ata_dma_last_write = -1;

	if (ipb_freq/1000000 == 66) {
		priv->mdmaspec = mdmaspec66;
		priv->udmaspec = udmaspec66;
	} else {
		priv->mdmaspec = mdmaspec132;
		priv->udmaspec = udmaspec132;
	}

	/* Allocate a BestComm task for DMA */
	dmatsk = bcom_ata_init(MAX_DMA_BUFFERS, MAX_DMA_BUFFER_SIZE);
	if (!dmatsk) {
		dev_err(&op->dev, "bestcomm initialization failed\n");
		rv = -ENOMEM;
		goto err1;
	}

	task_irq = bcom_get_task_irq(dmatsk);
	rv = devm_request_irq(&op->dev, task_irq, &mpc52xx_ata_task_irq, 0,
				"ATA task", priv);
	if (rv) {
		dev_err(&op->dev, "error requesting DMA IRQ\n");
		goto err2;
	}
	priv->dmatsk = dmatsk;

	/* Init the hw */
	rv = mpc52xx_ata_hw_init(priv);
	if (rv) {
		dev_err(&op->dev, "error initializing hardware\n");
		goto err2;
	}

	/* Register ourselves to libata */
	rv = mpc52xx_ata_init_one(&op->dev, priv, res_mem.start,
				  mwdma_mask, udma_mask);
	if (rv) {
		dev_err(&op->dev, "error registering with ATA layer\n");
		goto err2;
	}

	return 0;

 err2:
	irq_dispose_mapping(task_irq);
	bcom_ata_release(dmatsk);
 err1:
	irq_dispose_mapping(ata_irq);
	return rv;
}

static int
mpc52xx_ata_remove(struct platform_device *op)
{
	struct ata_host *host = platform_get_drvdata(op);
	struct mpc52xx_ata_priv *priv = host->private_data;
	int task_irq;

	/* Deregister the ATA interface */
	ata_platform_remove_one(op);

	/* Clean up DMA */
	task_irq = bcom_get_task_irq(priv->dmatsk);
	irq_dispose_mapping(task_irq);
	bcom_ata_release(priv->dmatsk);
	irq_dispose_mapping(priv->ata_irq);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int
mpc52xx_ata_suspend(struct platform_device *op, pm_message_t state)
{
	struct ata_host *host = platform_get_drvdata(op);

	return ata_host_suspend(host, state);
}

static int
mpc52xx_ata_resume(struct platform_device *op)
{
	struct ata_host *host = platform_get_drvdata(op);
	struct mpc52xx_ata_priv *priv = host->private_data;
	int rv;

	rv = mpc52xx_ata_hw_init(priv);
	if (rv) {
		dev_err(host->dev, "error initializing hardware\n");
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


static struct platform_driver mpc52xx_ata_of_platform_driver = {
	.probe		= mpc52xx_ata_probe,
	.remove		= mpc52xx_ata_remove,
#ifdef CONFIG_PM_SLEEP
	.suspend	= mpc52xx_ata_suspend,
	.resume		= mpc52xx_ata_resume,
#endif
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
		.of_match_table = mpc52xx_ata_of_match,
	},
};

module_platform_driver(mpc52xx_ata_of_platform_driver);

MODULE_AUTHOR("Sylvain Munaut <tnt@246tNt.com>");
MODULE_DESCRIPTION("Freescale MPC52xx IDE/ATA libata driver");
MODULE_LICENSE("GPL");
MODULE_DEVICE_TABLE(of, mpc52xx_ata_of_match);

