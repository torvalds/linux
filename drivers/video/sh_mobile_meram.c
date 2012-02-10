/*
 * SuperH Mobile MERAM Driver for SuperH Mobile LCDC Driver
 *
 * Copyright (c) 2011	Damian Hobson-Garcia <dhobsong@igel.co.jp>
 *                      Takanari Hayama <taki@igel.co.jp>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/pm_runtime.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <video/sh_mobile_meram.h>

/* meram registers */
#define MEVCR1			0x4
#define MEVCR1_RST		(1 << 31)
#define MEVCR1_WD		(1 << 30)
#define MEVCR1_AMD1		(1 << 29)
#define MEVCR1_AMD0		(1 << 28)
#define MEQSEL1			0x40
#define MEQSEL2			0x44

#define MExxCTL			0x400
#define MExxCTL_BV		(1 << 31)
#define MExxCTL_BSZ_SHIFT	28
#define MExxCTL_MSAR_MASK	(0x7ff << MExxCTL_MSAR_SHIFT)
#define MExxCTL_MSAR_SHIFT	16
#define MExxCTL_NXT_MASK	(0x1f << MExxCTL_NXT_SHIFT)
#define MExxCTL_NXT_SHIFT	11
#define MExxCTL_WD1		(1 << 10)
#define MExxCTL_WD0		(1 << 9)
#define MExxCTL_WS		(1 << 8)
#define MExxCTL_CB		(1 << 7)
#define MExxCTL_WBF		(1 << 6)
#define MExxCTL_WF		(1 << 5)
#define MExxCTL_RF		(1 << 4)
#define MExxCTL_CM		(1 << 3)
#define MExxCTL_MD_READ		(1 << 0)
#define MExxCTL_MD_WRITE	(2 << 0)
#define MExxCTL_MD_ICB_WB	(3 << 0)
#define MExxCTL_MD_ICB		(4 << 0)
#define MExxCTL_MD_FB		(7 << 0)
#define MExxCTL_MD_MASK		(7 << 0)
#define MExxBSIZE		0x404
#define MExxBSIZE_RCNT_SHIFT	28
#define MExxBSIZE_YSZM1_SHIFT	16
#define MExxBSIZE_XSZM1_SHIFT	0
#define MExxMNCF		0x408
#define MExxMNCF_KWBNM_SHIFT	28
#define MExxMNCF_KRBNM_SHIFT	24
#define MExxMNCF_BNM_SHIFT	16
#define MExxMNCF_XBV		(1 << 15)
#define MExxMNCF_CPL_YCBCR444	(1 << 12)
#define MExxMNCF_CPL_YCBCR420	(2 << 12)
#define MExxMNCF_CPL_YCBCR422	(3 << 12)
#define MExxMNCF_CPL_MSK	(3 << 12)
#define MExxMNCF_BL		(1 << 2)
#define MExxMNCF_LNM_SHIFT	0
#define MExxSARA		0x410
#define MExxSARB		0x414
#define MExxSBSIZE		0x418
#define MExxSBSIZE_HDV		(1 << 31)
#define MExxSBSIZE_HSZ16	(0 << 28)
#define MExxSBSIZE_HSZ32	(1 << 28)
#define MExxSBSIZE_HSZ64	(2 << 28)
#define MExxSBSIZE_HSZ128	(3 << 28)
#define MExxSBSIZE_SBSIZZ_SHIFT	0

#define MERAM_MExxCTL_VAL(next, addr)	\
	((((next) << MExxCTL_NXT_SHIFT) & MExxCTL_NXT_MASK) | \
	 (((addr) << MExxCTL_MSAR_SHIFT) & MExxCTL_MSAR_MASK))
#define	MERAM_MExxBSIZE_VAL(rcnt, yszm1, xszm1) \
	(((rcnt) << MExxBSIZE_RCNT_SHIFT) | \
	 ((yszm1) << MExxBSIZE_YSZM1_SHIFT) | \
	 ((xszm1) << MExxBSIZE_XSZM1_SHIFT))

#define SH_MOBILE_MERAM_ICB_NUM		32

static unsigned long common_regs[] = {
	MEVCR1,
	MEQSEL1,
	MEQSEL2,
};
#define CMN_REGS_SIZE ARRAY_SIZE(common_regs)

static unsigned long icb_regs[] = {
	MExxCTL,
	MExxBSIZE,
	MExxMNCF,
	MExxSARA,
	MExxSARB,
	MExxSBSIZE,
};
#define ICB_REGS_SIZE ARRAY_SIZE(icb_regs)

struct sh_mobile_meram_priv {
	void __iomem	*base;
	struct mutex	lock;
	unsigned long	used_icb;
	int		used_meram_cache_regions;
	unsigned long	used_meram_cache[SH_MOBILE_MERAM_ICB_NUM];
	unsigned long	cmn_saved_regs[CMN_REGS_SIZE];
	unsigned long	icb_saved_regs[ICB_REGS_SIZE * SH_MOBILE_MERAM_ICB_NUM];
};

/* settings */
#define MERAM_SEC_LINE 15
#define MERAM_LINE_WIDTH 2048

/*
 * MERAM/ICB access functions
 */

#define MERAM_ICB_OFFSET(base, idx, off)	((base) + (off) + (idx) * 0x20)

static inline void meram_write_icb(void __iomem *base, int idx, int off,
	unsigned long val)
{
	iowrite32(val, MERAM_ICB_OFFSET(base, idx, off));
}

static inline unsigned long meram_read_icb(void __iomem *base, int idx, int off)
{
	return ioread32(MERAM_ICB_OFFSET(base, idx, off));
}

static inline void meram_write_reg(void __iomem *base, int off,
		unsigned long val)
{
	iowrite32(val, base + off);
}

static inline unsigned long meram_read_reg(void __iomem *base, int off)
{
	return ioread32(base + off);
}

/*
 * register ICB
 */

#define MERAM_CACHE_START(p)	 ((p) >> 16)
#define MERAM_CACHE_END(p)	 ((p) & 0xffff)
#define MERAM_CACHE_SET(o, s)	 ((((o) & 0xffff) << 16) | \
				  (((o) + (s) - 1) & 0xffff))

/*
 * check if there's no overlaps in MERAM allocation.
 */

static inline int meram_check_overlap(struct sh_mobile_meram_priv *priv,
				      struct sh_mobile_meram_icb *new)
{
	int i;
	int used_start, used_end, meram_start, meram_end;

	/* valid ICB? */
	if (new->marker_icb & ~0x1f || new->cache_icb & ~0x1f)
		return 1;

	if (test_bit(new->marker_icb, &priv->used_icb) ||
			test_bit(new->cache_icb,  &priv->used_icb))
		return  1;

	for (i = 0; i < priv->used_meram_cache_regions; i++) {
		used_start = MERAM_CACHE_START(priv->used_meram_cache[i]);
		used_end   = MERAM_CACHE_END(priv->used_meram_cache[i]);
		meram_start = new->meram_offset;
		meram_end   = new->meram_offset + new->meram_size;

		if ((meram_start >= used_start && meram_start < used_end) ||
			(meram_end > used_start && meram_end < used_end))
			return 1;
	}

	return 0;
}

/*
 * mark the specified ICB as used
 */

static inline void meram_mark(struct sh_mobile_meram_priv *priv,
			      struct sh_mobile_meram_icb *new)
{
	int n;

	if (new->marker_icb < 0 || new->cache_icb < 0)
		return;

	__set_bit(new->marker_icb, &priv->used_icb);
	__set_bit(new->cache_icb, &priv->used_icb);

	n = priv->used_meram_cache_regions;

	priv->used_meram_cache[n] = MERAM_CACHE_SET(new->meram_offset,
						    new->meram_size);

	priv->used_meram_cache_regions++;
}

/*
 * unmark the specified ICB as used
 */

static inline void meram_unmark(struct sh_mobile_meram_priv *priv,
				struct sh_mobile_meram_icb *icb)
{
	int i;
	unsigned long pattern;

	if (icb->marker_icb < 0 || icb->cache_icb < 0)
		return;

	__clear_bit(icb->marker_icb, &priv->used_icb);
	__clear_bit(icb->cache_icb, &priv->used_icb);

	pattern = MERAM_CACHE_SET(icb->meram_offset, icb->meram_size);
	for (i = 0; i < priv->used_meram_cache_regions; i++) {
		if (priv->used_meram_cache[i] == pattern) {
			while (i < priv->used_meram_cache_regions - 1) {
				priv->used_meram_cache[i] =
					priv->used_meram_cache[i + 1] ;
				i++;
			}
			priv->used_meram_cache[i] = 0;
			priv->used_meram_cache_regions--;
			break;
		}
	}
}

/*
 * is this a YCbCr(NV12, NV16 or NV24) colorspace
 */
static inline int is_nvcolor(int cspace)
{
	if (cspace == SH_MOBILE_MERAM_PF_NV ||
			cspace == SH_MOBILE_MERAM_PF_NV24)
		return 1;
	return 0;
}

/*
 * set the next address to fetch
 */
static inline void meram_set_next_addr(struct sh_mobile_meram_priv *priv,
				       struct sh_mobile_meram_cfg *cfg,
				       unsigned long base_addr_y,
				       unsigned long base_addr_c)
{
	unsigned long target;

	target = (cfg->current_reg) ? MExxSARA : MExxSARB;
	cfg->current_reg ^= 1;

	/* set the next address to fetch */
	meram_write_icb(priv->base, cfg->icb[0].cache_icb,  target,
			base_addr_y);
	meram_write_icb(priv->base, cfg->icb[0].marker_icb, target,
			base_addr_y + cfg->icb[0].cache_unit);

	if (is_nvcolor(cfg->pixelformat)) {
		meram_write_icb(priv->base, cfg->icb[1].cache_icb,  target,
				base_addr_c);
		meram_write_icb(priv->base, cfg->icb[1].marker_icb, target,
				base_addr_c + cfg->icb[1].cache_unit);
	}
}

/*
 * get the next ICB address
 */
static inline void meram_get_next_icb_addr(struct sh_mobile_meram_info *pdata,
					   struct sh_mobile_meram_cfg *cfg,
					   unsigned long *icb_addr_y,
					   unsigned long *icb_addr_c)
{
	unsigned long icb_offset;

	if (pdata->addr_mode == SH_MOBILE_MERAM_MODE0)
		icb_offset = 0x80000000 | (cfg->current_reg << 29);
	else
		icb_offset = 0xc0000000 | (cfg->current_reg << 23);

	*icb_addr_y = icb_offset | (cfg->icb[0].marker_icb << 24);
	if (is_nvcolor(cfg->pixelformat))
		*icb_addr_c = icb_offset | (cfg->icb[1].marker_icb << 24);
}

#define MERAM_CALC_BYTECOUNT(x, y) \
	(((x) * (y) + (MERAM_LINE_WIDTH - 1)) & ~(MERAM_LINE_WIDTH - 1))

/*
 * initialize MERAM
 */

static int meram_init(struct sh_mobile_meram_priv *priv,
		      struct sh_mobile_meram_icb *icb,
		      int xres, int yres, int *out_pitch)
{
	unsigned long total_byte_count = MERAM_CALC_BYTECOUNT(xres, yres);
	unsigned long bnm;
	int lcdc_pitch, xpitch, line_cnt;
	int save_lines;

	/* adjust pitch to 1024, 2048, 4096 or 8192 */
	lcdc_pitch = (xres - 1) | 1023;
	lcdc_pitch = lcdc_pitch | (lcdc_pitch >> 1);
	lcdc_pitch = lcdc_pitch | (lcdc_pitch >> 2);
	lcdc_pitch += 1;

	/* derive settings */
	if (lcdc_pitch == 8192 && yres >= 1024) {
		lcdc_pitch = xpitch = MERAM_LINE_WIDTH;
		line_cnt = total_byte_count >> 11;
		*out_pitch = xres;
		save_lines = (icb->meram_size / 16 / MERAM_SEC_LINE);
		save_lines *= MERAM_SEC_LINE;
	} else {
		xpitch = xres;
		line_cnt = yres;
		*out_pitch = lcdc_pitch;
		save_lines = icb->meram_size / (lcdc_pitch >> 10) / 2;
		save_lines &= 0xff;
	}
	bnm = (save_lines - 1) << 16;

	/* TODO: we better to check if we have enough MERAM buffer size */

	/* set up ICB */
	meram_write_icb(priv->base, icb->cache_icb,  MExxBSIZE,
			MERAM_MExxBSIZE_VAL(0x0, line_cnt - 1, xpitch - 1));
	meram_write_icb(priv->base, icb->marker_icb, MExxBSIZE,
			MERAM_MExxBSIZE_VAL(0xf, line_cnt - 1, xpitch - 1));

	meram_write_icb(priv->base, icb->cache_icb,  MExxMNCF, bnm);
	meram_write_icb(priv->base, icb->marker_icb, MExxMNCF, bnm);

	meram_write_icb(priv->base, icb->cache_icb,  MExxSBSIZE, xpitch);
	meram_write_icb(priv->base, icb->marker_icb, MExxSBSIZE, xpitch);

	/* save a cache unit size */
	icb->cache_unit = xres * save_lines;

	/*
	 * Set MERAM for framebuffer
	 *
	 * we also chain the cache_icb and the marker_icb.
	 * we also split the allocated MERAM buffer between two ICBs.
	 */
	meram_write_icb(priv->base, icb->cache_icb, MExxCTL,
			MERAM_MExxCTL_VAL(icb->marker_icb, icb->meram_offset) |
			MExxCTL_WD1 | MExxCTL_WD0 | MExxCTL_WS | MExxCTL_CM |
			MExxCTL_MD_FB);
	meram_write_icb(priv->base, icb->marker_icb, MExxCTL,
			MERAM_MExxCTL_VAL(icb->cache_icb, icb->meram_offset +
					  icb->meram_size / 2) |
			MExxCTL_WD1 | MExxCTL_WD0 | MExxCTL_WS | MExxCTL_CM |
			MExxCTL_MD_FB);

	return 0;
}

static void meram_deinit(struct sh_mobile_meram_priv *priv,
			struct sh_mobile_meram_icb *icb)
{
	/* disable ICB */
	meram_write_icb(priv->base, icb->cache_icb,  MExxCTL,
			MExxCTL_WBF | MExxCTL_WF | MExxCTL_RF);
	meram_write_icb(priv->base, icb->marker_icb, MExxCTL,
			MExxCTL_WBF | MExxCTL_WF | MExxCTL_RF);
	icb->cache_unit = 0;
}

/*
 * register the ICB
 */

static int sh_mobile_meram_register(struct sh_mobile_meram_info *pdata,
				    struct sh_mobile_meram_cfg *cfg,
				    int xres, int yres, int pixelformat,
				    unsigned long base_addr_y,
				    unsigned long base_addr_c,
				    unsigned long *icb_addr_y,
				    unsigned long *icb_addr_c,
				    int *pitch)
{
	struct platform_device *pdev;
	struct sh_mobile_meram_priv *priv;
	int n, out_pitch;
	int error = 0;

	if (!pdata || !pdata->priv || !pdata->pdev || !cfg)
		return -EINVAL;

	if (pixelformat != SH_MOBILE_MERAM_PF_NV &&
	    pixelformat != SH_MOBILE_MERAM_PF_NV24 &&
	    pixelformat != SH_MOBILE_MERAM_PF_RGB)
		return -EINVAL;

	priv = pdata->priv;
	pdev = pdata->pdev;

	dev_dbg(&pdev->dev, "registering %dx%d (%s) (y=%08lx, c=%08lx)",
		xres, yres, (!pixelformat) ? "yuv" : "rgb",
		base_addr_y, base_addr_c);

	/* we can't handle wider than 8192px */
	if (xres > 8192) {
		dev_err(&pdev->dev, "width exceeding the limit (> 8192).");
		return -EINVAL;
	}

	/* do we have at least one ICB config? */
	if (cfg->icb[0].marker_icb < 0 || cfg->icb[0].cache_icb < 0) {
		dev_err(&pdev->dev, "at least one ICB is required.");
		return -EINVAL;
	}

	mutex_lock(&priv->lock);

	if (priv->used_meram_cache_regions + 2 > SH_MOBILE_MERAM_ICB_NUM) {
		dev_err(&pdev->dev, "no more ICB available.");
		error = -EINVAL;
		goto err;
	}

	/* make sure that there's no overlaps */
	if (meram_check_overlap(priv, &cfg->icb[0])) {
		dev_err(&pdev->dev, "conflicting config detected.");
		error = -EINVAL;
		goto err;
	}
	n = 1;

	/* do the same if we have the second ICB set */
	if (cfg->icb[1].marker_icb >= 0 && cfg->icb[1].cache_icb >= 0) {
		if (meram_check_overlap(priv, &cfg->icb[1])) {
			dev_err(&pdev->dev, "conflicting config detected.");
			error = -EINVAL;
			goto err;
		}
		n = 2;
	}

	if (is_nvcolor(pixelformat) && n != 2) {
		dev_err(&pdev->dev, "requires two ICB sets for planar Y/C.");
		error =  -EINVAL;
		goto err;
	}

	/* we now register the ICB */
	cfg->pixelformat = pixelformat;
	meram_mark(priv, &cfg->icb[0]);
	if (is_nvcolor(pixelformat))
		meram_mark(priv, &cfg->icb[1]);

	/* initialize MERAM */
	meram_init(priv, &cfg->icb[0], xres, yres, &out_pitch);
	*pitch = out_pitch;
	if (pixelformat == SH_MOBILE_MERAM_PF_NV)
		meram_init(priv, &cfg->icb[1], xres, (yres + 1) / 2,
			&out_pitch);
	else if (pixelformat == SH_MOBILE_MERAM_PF_NV24)
		meram_init(priv, &cfg->icb[1], 2 * xres, (yres + 1) / 2,
			&out_pitch);

	cfg->current_reg = 1;
	meram_set_next_addr(priv, cfg, base_addr_y, base_addr_c);
	meram_get_next_icb_addr(pdata, cfg, icb_addr_y, icb_addr_c);

	dev_dbg(&pdev->dev, "registered - can access via y=%08lx, c=%08lx",
		*icb_addr_y, *icb_addr_c);

err:
	mutex_unlock(&priv->lock);
	return error;
}

static int sh_mobile_meram_unregister(struct sh_mobile_meram_info *pdata,
				      struct sh_mobile_meram_cfg *cfg)
{
	struct sh_mobile_meram_priv *priv;

	if (!pdata || !pdata->priv || !cfg)
		return -EINVAL;

	priv = pdata->priv;

	mutex_lock(&priv->lock);

	/* deinit & unmark */
	if (is_nvcolor(cfg->pixelformat)) {
		meram_deinit(priv, &cfg->icb[1]);
		meram_unmark(priv, &cfg->icb[1]);
	}
	meram_deinit(priv, &cfg->icb[0]);
	meram_unmark(priv, &cfg->icb[0]);

	mutex_unlock(&priv->lock);

	return 0;
}

static int sh_mobile_meram_update(struct sh_mobile_meram_info *pdata,
				  struct sh_mobile_meram_cfg *cfg,
				  unsigned long base_addr_y,
				  unsigned long base_addr_c,
				  unsigned long *icb_addr_y,
				  unsigned long *icb_addr_c)
{
	struct sh_mobile_meram_priv *priv;

	if (!pdata || !pdata->priv || !cfg)
		return -EINVAL;

	priv = pdata->priv;

	mutex_lock(&priv->lock);

	meram_set_next_addr(priv, cfg, base_addr_y, base_addr_c);
	meram_get_next_icb_addr(pdata, cfg, icb_addr_y, icb_addr_c);

	mutex_unlock(&priv->lock);

	return 0;
}

static int sh_mobile_meram_runtime_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sh_mobile_meram_priv *priv = platform_get_drvdata(pdev);
	int k, j;

	for (k = 0; k < CMN_REGS_SIZE; k++)
		priv->cmn_saved_regs[k] = meram_read_reg(priv->base,
			common_regs[k]);

	for (j = 0; j < 32; j++) {
		if (!test_bit(j, &priv->used_icb))
			continue;
		for (k = 0; k < ICB_REGS_SIZE; k++) {
			priv->icb_saved_regs[j * ICB_REGS_SIZE + k] =
				meram_read_icb(priv->base, j, icb_regs[k]);
			/* Reset ICB on resume */
			if (icb_regs[k] == MExxCTL)
				priv->icb_saved_regs[j * ICB_REGS_SIZE + k] |=
					MExxCTL_WBF | MExxCTL_WF | MExxCTL_RF;
		}
	}
	return 0;
}

static int sh_mobile_meram_runtime_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct sh_mobile_meram_priv *priv = platform_get_drvdata(pdev);
	int k, j;

	for (j = 0; j < 32; j++) {
		if (!test_bit(j, &priv->used_icb))
			continue;
		for (k = 0; k < ICB_REGS_SIZE; k++) {
			meram_write_icb(priv->base, j, icb_regs[k],
			priv->icb_saved_regs[j * ICB_REGS_SIZE + k]);
		}
	}

	for (k = 0; k < CMN_REGS_SIZE; k++)
		meram_write_reg(priv->base, common_regs[k],
			priv->cmn_saved_regs[k]);
	return 0;
}

static const struct dev_pm_ops sh_mobile_meram_dev_pm_ops = {
	.runtime_suspend = sh_mobile_meram_runtime_suspend,
	.runtime_resume = sh_mobile_meram_runtime_resume,
};

static struct sh_mobile_meram_ops sh_mobile_meram_ops = {
	.module			= THIS_MODULE,
	.meram_register		= sh_mobile_meram_register,
	.meram_unregister	= sh_mobile_meram_unregister,
	.meram_update		= sh_mobile_meram_update,
};

/*
 * initialize MERAM
 */

static int sh_mobile_meram_remove(struct platform_device *pdev);

static int __devinit sh_mobile_meram_probe(struct platform_device *pdev)
{
	struct sh_mobile_meram_priv *priv;
	struct sh_mobile_meram_info *pdata = pdev->dev.platform_data;
	struct resource *res;
	int error;

	if (!pdata) {
		dev_err(&pdev->dev, "no platform data defined\n");
		return -EINVAL;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "cannot get platform resources\n");
		return -ENOENT;
	}

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "cannot allocate device data\n");
		return -ENOMEM;
	}

	platform_set_drvdata(pdev, priv);

	/* initialize private data */
	mutex_init(&priv->lock);
	priv->base = ioremap_nocache(res->start, resource_size(res));
	if (!priv->base) {
		dev_err(&pdev->dev, "ioremap failed\n");
		error = -EFAULT;
		goto err;
	}
	pdata->ops = &sh_mobile_meram_ops;
	pdata->priv = priv;
	pdata->pdev = pdev;

	/* initialize ICB addressing mode */
	if (pdata->addr_mode == SH_MOBILE_MERAM_MODE1)
		meram_write_reg(priv->base, MEVCR1, MEVCR1_AMD1);

	pm_runtime_enable(&pdev->dev);

	dev_info(&pdev->dev, "sh_mobile_meram initialized.");

	return 0;

err:
	sh_mobile_meram_remove(pdev);

	return error;
}


static int sh_mobile_meram_remove(struct platform_device *pdev)
{
	struct sh_mobile_meram_priv *priv = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);

	if (priv->base)
		iounmap(priv->base);

	mutex_destroy(&priv->lock);

	kfree(priv);

	return 0;
}

static struct platform_driver sh_mobile_meram_driver = {
	.driver	= {
		.name		= "sh_mobile_meram",
		.owner		= THIS_MODULE,
		.pm		= &sh_mobile_meram_dev_pm_ops,
	},
	.probe		= sh_mobile_meram_probe,
	.remove		= sh_mobile_meram_remove,
};

module_platform_driver(sh_mobile_meram_driver);

MODULE_DESCRIPTION("SuperH Mobile MERAM driver");
MODULE_AUTHOR("Damian Hobson-Garcia / Takanari Hayama");
MODULE_LICENSE("GPL v2");
