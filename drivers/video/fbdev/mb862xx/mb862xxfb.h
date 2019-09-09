/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __MB862XX_H__
#define __MB862XX_H__

struct mb862xx_l1_cfg {
	unsigned short sx;
	unsigned short sy;
	unsigned short sw;
	unsigned short sh;
	unsigned short dx;
	unsigned short dy;
	unsigned short dw;
	unsigned short dh;
	int mirror;
};

#define MB862XX_BASE		'M'
#define MB862XX_L1_GET_CFG	_IOR(MB862XX_BASE, 0, struct mb862xx_l1_cfg*)
#define MB862XX_L1_SET_CFG	_IOW(MB862XX_BASE, 1, struct mb862xx_l1_cfg*)
#define MB862XX_L1_ENABLE	_IOW(MB862XX_BASE, 2, int)
#define MB862XX_L1_CAP_CTL	_IOW(MB862XX_BASE, 3, int)

#ifdef __KERNEL__

#define PCI_VENDOR_ID_FUJITSU_LIMITED	0x10cf
#define PCI_DEVICE_ID_FUJITSU_CORALP	0x2019
#define PCI_DEVICE_ID_FUJITSU_CORALPA	0x201e
#define PCI_DEVICE_ID_FUJITSU_CARMINE	0x202b

#define GC_MMR_CORALP_EVB_VAL		0x11d7fa13

enum gdctype {
	BT_NONE,
	BT_LIME,
	BT_MINT,
	BT_CORAL,
	BT_CORALP,
	BT_CARMINE,
};

struct mb862xx_gc_mode {
	struct fb_videomode	def_mode;	/* mode of connected display */
	unsigned int		def_bpp;	/* default depth */
	unsigned long		max_vram;	/* connected SDRAM size */
	unsigned long		ccf;		/* gdc clk */
	unsigned long		mmr;		/* memory mode for SDRAM */
};

/* private data */
struct mb862xxfb_par {
	struct fb_info		*info;		/* fb info head */
	struct device		*dev;
	struct pci_dev		*pdev;
	struct resource		*res;		/* framebuffer/mmio resource */

	resource_size_t		fb_base_phys;	/* fb base, 36-bit PPC440EPx */
	resource_size_t		mmio_base_phys;	/* io base addr */
	void __iomem		*fb_base;	/* remapped framebuffer */
	void __iomem		*mmio_base;	/* remapped registers */
	size_t			mapped_vram;	/* length of remapped vram */
	size_t			mmio_len;	/* length of register region */
	unsigned long		cap_buf;	/* capture buffers offset */
	size_t			cap_len;	/* length of capture buffers */

	void __iomem		*host;		/* relocatable reg. bases */
	void __iomem		*i2c;
	void __iomem		*disp;
	void __iomem		*disp1;
	void __iomem		*cap;
	void __iomem		*cap1;
	void __iomem		*draw;
	void __iomem		*geo;
	void __iomem		*pio;
	void __iomem		*ctrl;
	void __iomem		*dram_ctrl;
	void __iomem		*wrback;

	unsigned int		irq;
	unsigned int		type;		/* GDC type */
	unsigned int		refclk;		/* disp. reference clock */
	struct mb862xx_gc_mode	*gc_mode;	/* GDC mode init data */
	int			pre_init;	/* don't init display if 1 */
	struct i2c_adapter	*adap;		/* GDC I2C bus adapter */
	int			i2c_rs;

	struct mb862xx_l1_cfg	l1_cfg;
	int			l1_stride;

	u32			pseudo_palette[16];
};

extern void mb862xxfb_init_accel(struct fb_info *info, int xres);
#ifdef CONFIG_FB_MB862XX_I2C
extern int mb862xx_i2c_init(struct mb862xxfb_par *par);
extern void mb862xx_i2c_exit(struct mb862xxfb_par *par);
#else
static inline int mb862xx_i2c_init(struct mb862xxfb_par *par) { return 0; }
static inline void mb862xx_i2c_exit(struct mb862xxfb_par *par) { }
#endif

#if defined(CONFIG_FB_MB862XX_LIME) && defined(CONFIG_FB_MB862XX_PCI_GDC)
#error	"Select Lime GDC or CoralP/Carmine support, but not both together"
#endif
#if defined(CONFIG_FB_MB862XX_LIME)
#define gdc_read	__raw_readl
#define gdc_write	__raw_writel
#else
#define gdc_read	readl
#define gdc_write	writel
#endif

#define inreg(type, off)	\
	gdc_read((par->type + (off)))

#define outreg(type, off, val)	\
	gdc_write((val), (par->type + (off)))

#define pack(a, b)	(((a) << 16) | (b))

#endif /* __KERNEL__ */

#endif
