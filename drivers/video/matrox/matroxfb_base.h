/*
 *
 * Hardware accelerated Matrox Millennium I, II, Mystique, G100, G200, G400 and G450
 *
 * (c) 1998-2002 Petr Vandrovec <vandrove@vc.cvut.cz>
 *
 */
#ifndef __MATROXFB_H__
#define __MATROXFB_H__

/* general, but fairly heavy, debugging */
#undef MATROXFB_DEBUG

/* heavy debugging: */
/* -- logs putc[s], so everytime a char is displayed, it's logged */
#undef MATROXFB_DEBUG_HEAVY

/* This one _could_ cause infinite loops */
/* It _does_ cause lots and lots of messages during idle loops */
#undef MATROXFB_DEBUG_LOOP

/* Debug register calls, too? */
#undef MATROXFB_DEBUG_REG

/* Guard accelerator accesses with spin_lock_irqsave... */
#undef MATROXFB_USE_SPINLOCKS

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/console.h>
#include <linux/selection.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/kd.h>

#include <asm/io.h>
#include <asm/unaligned.h>
#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif

#if defined(CONFIG_PPC_PMAC)
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include "../macmodes.h"
#endif

#ifdef MATROXFB_DEBUG

#define DEBUG
#define DBG(x)		printk(KERN_DEBUG "matroxfb: %s\n", (x));

#ifdef MATROXFB_DEBUG_HEAVY
#define DBG_HEAVY(x)	DBG(x)
#else /* MATROXFB_DEBUG_HEAVY */
#define DBG_HEAVY(x)	/* DBG_HEAVY */
#endif /* MATROXFB_DEBUG_HEAVY */

#ifdef MATROXFB_DEBUG_LOOP
#define DBG_LOOP(x)	DBG(x)
#else /* MATROXFB_DEBUG_LOOP */
#define DBG_LOOP(x)	/* DBG_LOOP */
#endif /* MATROXFB_DEBUG_LOOP */

#ifdef MATROXFB_DEBUG_REG
#define DBG_REG(x)	DBG(x)
#else /* MATROXFB_DEBUG_REG */
#define DBG_REG(x)	/* DBG_REG */
#endif /* MATROXFB_DEBUG_REG */

#else /* MATROXFB_DEBUG */

#define DBG(x)		/* DBG */
#define DBG_HEAVY(x)	/* DBG_HEAVY */
#define DBG_REG(x)	/* DBG_REG */
#define DBG_LOOP(x)	/* DBG_LOOP */

#endif /* MATROXFB_DEBUG */

#ifdef DEBUG
#define dprintk(X...)	printk(X)
#else
#define dprintk(X...)
#endif

#ifndef PCI_SS_VENDOR_ID_SIEMENS_NIXDORF
#define PCI_SS_VENDOR_ID_SIEMENS_NIXDORF	0x110A
#endif
#ifndef PCI_SS_VENDOR_ID_MATROX
#define PCI_SS_VENDOR_ID_MATROX		PCI_VENDOR_ID_MATROX
#endif

#ifndef PCI_SS_ID_MATROX_PRODUCTIVA_G100_AGP
#define PCI_SS_ID_MATROX_GENERIC		0xFF00
#define PCI_SS_ID_MATROX_PRODUCTIVA_G100_AGP	0xFF01
#define PCI_SS_ID_MATROX_MYSTIQUE_G200_AGP	0xFF02
#define PCI_SS_ID_MATROX_MILLENIUM_G200_AGP	0xFF03
#define PCI_SS_ID_MATROX_MARVEL_G200_AGP	0xFF04
#define PCI_SS_ID_MATROX_MGA_G100_PCI		0xFF05
#define PCI_SS_ID_MATROX_MGA_G100_AGP		0x1001
#define PCI_SS_ID_MATROX_MILLENNIUM_G400_MAX_AGP	0x2179
#define PCI_SS_ID_SIEMENS_MGA_G100_AGP		0x001E /* 30 */
#define PCI_SS_ID_SIEMENS_MGA_G200_AGP		0x0032 /* 50 */
#endif

#define MX_VISUAL_TRUECOLOR	FB_VISUAL_DIRECTCOLOR
#define MX_VISUAL_DIRECTCOLOR	FB_VISUAL_TRUECOLOR
#define MX_VISUAL_PSEUDOCOLOR	FB_VISUAL_PSEUDOCOLOR

#define CNVT_TOHW(val,width) ((((val)<<(width))+0x7FFF-(val))>>16)

/* G-series and Mystique have (almost) same DAC */
#undef NEED_DAC1064
#if defined(CONFIG_FB_MATROX_MYSTIQUE) || defined(CONFIG_FB_MATROX_G)
#define NEED_DAC1064 1
#endif

typedef struct {
	void __iomem*	vaddr;
} vaddr_t;

static inline unsigned int mga_readb(vaddr_t va, unsigned int offs) {
	return readb(va.vaddr + offs);
}

static inline void mga_writeb(vaddr_t va, unsigned int offs, u_int8_t value) {
	writeb(value, va.vaddr + offs);
}

static inline void mga_writew(vaddr_t va, unsigned int offs, u_int16_t value) {
	writew(value, va.vaddr + offs);
}

static inline u_int32_t mga_readl(vaddr_t va, unsigned int offs) {
	return readl(va.vaddr + offs);
}

static inline void mga_writel(vaddr_t va, unsigned int offs, u_int32_t value) {
	writel(value, va.vaddr + offs);
}

static inline void mga_memcpy_toio(vaddr_t va, const void* src, int len) {
#if defined(__alpha__) || defined(__i386__) || defined(__x86_64__)
	/*
	 * iowrite32_rep works for us if:
	 *  (1) Copies data as 32bit quantities, not byte after byte,
	 *  (2) Performs LE ordered stores, and
	 *  (3) It copes with unaligned source (destination is guaranteed to be page
	 *      aligned and length is guaranteed to be multiple of 4).
	 */
	iowrite32_rep(va.vaddr, src, len >> 2);
#else
        u_int32_t __iomem* addr = va.vaddr;

	if ((unsigned long)src & 3) {
		while (len >= 4) {
			fb_writel(get_unaligned((u32 *)src), addr);
			addr++;
			len -= 4;
			src += 4;
		}
	} else {
		while (len >= 4) {
			fb_writel(*(u32 *)src, addr);
			addr++;
			len -= 4;
			src += 4;
		}
	}
#endif
}

static inline void vaddr_add(vaddr_t* va, unsigned long offs) {
	va->vaddr += offs;
}

static inline void __iomem* vaddr_va(vaddr_t va) {
	return va.vaddr;
}

#define MGA_IOREMAP_NORMAL	0
#define MGA_IOREMAP_NOCACHE	1

#define MGA_IOREMAP_FB		MGA_IOREMAP_NOCACHE
#define MGA_IOREMAP_MMIO	MGA_IOREMAP_NOCACHE
static inline int mga_ioremap(unsigned long phys, unsigned long size, int flags, vaddr_t* virt) {
	if (flags & MGA_IOREMAP_NOCACHE)
		virt->vaddr = ioremap_nocache(phys, size);
	else
		virt->vaddr = ioremap(phys, size);
	return (virt->vaddr == NULL); /* 0, !0... 0, error_code in future */
}

static inline void mga_iounmap(vaddr_t va) {
	iounmap(va.vaddr);
}

struct my_timming {
	unsigned int pixclock;
	int mnp;
	unsigned int crtc;
	unsigned int HDisplay;
	unsigned int HSyncStart;
	unsigned int HSyncEnd;
	unsigned int HTotal;
	unsigned int VDisplay;
	unsigned int VSyncStart;
	unsigned int VSyncEnd;
	unsigned int VTotal;
	unsigned int sync;
	int	     dblscan;
	int	     interlaced;
	unsigned int delay;	/* CRTC delay */
};

enum { M_SYSTEM_PLL, M_PIXEL_PLL_A, M_PIXEL_PLL_B, M_PIXEL_PLL_C, M_VIDEO_PLL };

struct matrox_pll_cache {
	unsigned int	valid;
	struct {
		unsigned int	mnp_key;
		unsigned int	mnp_value;
		      } data[4];
};

struct matrox_pll_limits {
	unsigned int	vcomin;
	unsigned int	vcomax;
};

struct matrox_pll_features {
	unsigned int	vco_freq_min;
	unsigned int	ref_freq;
	unsigned int	feed_div_min;
	unsigned int	feed_div_max;
	unsigned int	in_div_min;
	unsigned int	in_div_max;
	unsigned int	post_shift_max;
};

struct matroxfb_par
{
	unsigned int	final_bppShift;
	unsigned int	cmap_len;
	struct {
		unsigned int bytes;
		unsigned int pixels;
		unsigned int chunks;
		      } ydstorg;
};

struct matrox_fb_info;

struct matrox_DAC1064_features {
	u_int8_t	xvrefctrl;
	u_int8_t	xmiscctrl;
};

/* current hardware status */
struct mavenregs {
	u_int8_t regs[256];
	int	 mode;
	int	 vlines;
	int	 xtal;
	int	 fv;

	u_int16_t htotal;
	u_int16_t hcorr;
};

struct matrox_crtc2 {
	u_int32_t ctl;
};

struct matrox_hw_state {
	u_int32_t	MXoptionReg;
	unsigned char	DACclk[6];
	unsigned char	DACreg[80];
	unsigned char	MiscOutReg;
	unsigned char	DACpal[768];
	unsigned char	CRTC[25];
	unsigned char	CRTCEXT[9];
	unsigned char	SEQ[5];
	/* unused for MGA mode, but who knows... */
	unsigned char	GCTL[9];
	/* unused for MGA mode, but who knows... */
	unsigned char	ATTR[21];

	/* TVOut only */
	struct mavenregs	maven;

	struct matrox_crtc2	crtc2;
};

struct matrox_accel_data {
#ifdef CONFIG_FB_MATROX_MILLENIUM
	unsigned char	ramdac_rev;
#endif
	u_int32_t	m_dwg_rect;
	u_int32_t	m_opmode;
};

struct v4l2_queryctrl;
struct v4l2_control;

struct matrox_altout {
	const char	*name;
	int		(*compute)(void* altout_dev, struct my_timming* input);
	int		(*program)(void* altout_dev);
	int		(*start)(void* altout_dev);
	int		(*verifymode)(void* altout_dev, u_int32_t mode);
	int		(*getqueryctrl)(void* altout_dev,
					struct v4l2_queryctrl* ctrl);
	int		(*getctrl)(void* altout_dev, 
				   struct v4l2_control* ctrl);
	int		(*setctrl)(void* altout_dev, 
				   struct v4l2_control* ctrl);
};

#define MATROXFB_SRC_NONE	0
#define MATROXFB_SRC_CRTC1	1
#define MATROXFB_SRC_CRTC2	2

enum mga_chip { MGA_2064, MGA_2164, MGA_1064, MGA_1164, MGA_G100, MGA_G200, MGA_G400, MGA_G450, MGA_G550 };

struct matrox_bios {
	unsigned int	bios_valid : 1;
	unsigned int	pins_len;
	unsigned char	pins[128];
	struct {
		unsigned char vMaj, vMin, vRev;
		      } version;
	struct {
		unsigned char state, tvout;
		      } output;
};

struct matrox_switch;
struct matroxfb_driver;
struct matroxfb_dh_fb_info;

struct matrox_vsync {
	wait_queue_head_t	wait;
	unsigned int		cnt;
};

struct matrox_fb_info {
	struct fb_info		fbcon;

	struct list_head	next_fb;

	int			dead;
	int                     initialized;
	unsigned int		usecount;

	unsigned int		userusecount;
	unsigned long		irq_flags;

	struct matroxfb_par	curr;
	struct matrox_hw_state	hw;

	struct matrox_accel_data accel;

	struct pci_dev*		pcidev;

	struct {
		struct matrox_vsync	vsync;
		unsigned int	pixclock;
		int		mnp;
		int		panpos;
			      } crtc1;
	struct {
		struct matrox_vsync	vsync;
		unsigned int 	pixclock;
		int		mnp;
	struct matroxfb_dh_fb_info*	info;
	struct rw_semaphore	lock;
			      } crtc2;
	struct {
	struct rw_semaphore	lock;
	struct {
		int brightness, contrast, saturation, hue, gamma;
		int testout, deflicker;
				} tvo_params;
			      } altout;
#define MATROXFB_MAX_OUTPUTS		3
	struct {
	unsigned int		src;
	struct matrox_altout*	output;
	void*			data;
	unsigned int		mode;
	unsigned int		default_src;
			      } outputs[MATROXFB_MAX_OUTPUTS];

#define MATROXFB_MAX_FB_DRIVERS		5
	struct matroxfb_driver* (drivers[MATROXFB_MAX_FB_DRIVERS]);
	void*			(drivers_data[MATROXFB_MAX_FB_DRIVERS]);
	unsigned int		drivers_count;

	struct {
	unsigned long	base;	/* physical */
	vaddr_t		vbase;	/* CPU view */
	unsigned int	len;
	unsigned int	len_usable;
	unsigned int	len_maximum;
		      } video;

	struct {
	unsigned long	base;	/* physical */
	vaddr_t		vbase;	/* CPU view */
	unsigned int	len;
		      } mmio;

	unsigned int	max_pixel_clock;
	unsigned int	max_pixel_clock_panellink;

	struct matrox_switch*	hw_switch;

	struct {
		struct matrox_pll_features pll;
		struct matrox_DAC1064_features DAC1064;
			      } features;
	struct {
		spinlock_t	DAC;
		spinlock_t	accel;
			      } lock;

	enum mga_chip		chip;

	int			interleave;
	int			millenium;
	int			milleniumII;
	struct {
		int		cfb4;
		const int*	vxres;
		int		cross4MB;
		int		text;
		int		plnwt;
		int		srcorg;
			      } capable;
#ifdef CONFIG_MTRR
	struct {
		int		vram;
		int		vram_valid;
			      } mtrr;
#endif
	struct {
		int		precise_width;
		int		mga_24bpp_fix;
		int		novga;
		int		nobios;
		int		nopciretry;
		int		noinit;
		int		sgram;
		int		support32MB;

		int		accelerator;
		int		text_type_aux;
		int		video64bits;
		int		crtc2;
		int		maven_capable;
		unsigned int	vgastep;
		unsigned int	textmode;
		unsigned int	textstep;
		unsigned int	textvram;	/* character cells */
		unsigned int	ydstorg;	/* offset in bytes from video start to usable memory */
						/* 0 except for 6MB Millenium */
		int		memtype;
		int		g450dac;
		int		dfp_type;
		int		panellink;	/* G400 DFP possible (not G450/G550) */
		int		dualhead;
		unsigned int	fbResource;
			      } devflags;
	struct fb_ops		fbops;
	struct matrox_bios	bios;
	struct {
		struct matrox_pll_limits	pixel;
		struct matrox_pll_limits	system;
		struct matrox_pll_limits	video;
			      } limits;
	struct {
		struct matrox_pll_cache	pixel;
		struct matrox_pll_cache	system;
		struct matrox_pll_cache	video;
				      } cache;
	struct {
		struct {
			unsigned int	video;
			unsigned int	system;
				      } pll;
		struct {
			u_int32_t	opt;
			u_int32_t	opt2;
			u_int32_t	opt3;
			u_int32_t	mctlwtst;
			u_int32_t	mctlwtst_core;
			u_int32_t	memmisc;
			u_int32_t	memrdbk;
			u_int32_t	maccess;
				      } reg;
		struct {
			unsigned int	ddr:1,
			                emrswen:1,
					dll:1;
				      } memory;
			      } values;
	u_int32_t cmap[16];
};

#define info2minfo(info) container_of(info, struct matrox_fb_info, fbcon)

struct matrox_switch {
	int	(*preinit)(struct matrox_fb_info *minfo);
	void	(*reset)(struct matrox_fb_info *minfo);
	int	(*init)(struct matrox_fb_info *minfo, struct my_timming*);
	void	(*restore)(struct matrox_fb_info *minfo);
};

struct matroxfb_driver {
	struct list_head	node;
	char*			name;
	void*			(*probe)(struct matrox_fb_info* info);
	void			(*remove)(struct matrox_fb_info* info, void* data);
};

int matroxfb_register_driver(struct matroxfb_driver* drv);
void matroxfb_unregister_driver(struct matroxfb_driver* drv);

#define PCI_OPTION_REG	0x40
#define   PCI_OPTION_ENABLE_ROM		0x40000000

#define PCI_MGA_INDEX	0x44
#define PCI_MGA_DATA	0x48
#define PCI_OPTION2_REG	0x50
#define PCI_OPTION3_REG	0x54
#define PCI_MEMMISC_REG	0x58

#define M_DWGCTL	0x1C00
#define M_MACCESS	0x1C04
#define M_CTLWTST	0x1C08

#define M_PLNWT		0x1C1C

#define M_BCOL		0x1C20
#define M_FCOL		0x1C24

#define M_SGN		0x1C58
#define M_LEN		0x1C5C
#define M_AR0		0x1C60
#define M_AR1		0x1C64
#define M_AR2		0x1C68
#define M_AR3		0x1C6C
#define M_AR4		0x1C70
#define M_AR5		0x1C74
#define M_AR6		0x1C78

#define M_CXBNDRY	0x1C80
#define M_FXBNDRY	0x1C84
#define M_YDSTLEN	0x1C88
#define M_PITCH		0x1C8C
#define M_YDST		0x1C90
#define M_YDSTORG	0x1C94
#define M_YTOP		0x1C98
#define M_YBOT		0x1C9C

/* mystique only */
#define M_CACHEFLUSH	0x1FFF

#define M_EXEC		0x0100

#define M_DWG_TRAP	0x04
#define M_DWG_BITBLT	0x08
#define M_DWG_ILOAD	0x09

#define M_DWG_LINEAR	0x0080
#define M_DWG_SOLID	0x0800
#define M_DWG_ARZERO	0x1000
#define M_DWG_SGNZERO	0x2000
#define M_DWG_SHIFTZERO	0x4000

#define M_DWG_REPLACE	0x000C0000
#define M_DWG_REPLACE2	(M_DWG_REPLACE | 0x40)
#define M_DWG_XOR	0x00060010

#define M_DWG_BFCOL	0x04000000
#define M_DWG_BMONOWF	0x08000000

#define M_DWG_TRANSC	0x40000000

#define M_FIFOSTATUS	0x1E10
#define M_STATUS	0x1E14
#define M_ICLEAR	0x1E18
#define M_IEN		0x1E1C

#define M_VCOUNT	0x1E20

#define M_RESET		0x1E40
#define M_MEMRDBK	0x1E44

#define M_AGP2PLL	0x1E4C

#define M_OPMODE	0x1E54
#define     M_OPMODE_DMA_GEN_WRITE	0x00
#define     M_OPMODE_DMA_BLIT		0x04
#define     M_OPMODE_DMA_VECTOR_WRITE	0x08
#define     M_OPMODE_DMA_LE		0x0000		/* little endian - no transformation */
#define     M_OPMODE_DMA_BE_8BPP	0x0000
#define     M_OPMODE_DMA_BE_16BPP	0x0100
#define     M_OPMODE_DMA_BE_32BPP	0x0200
#define     M_OPMODE_DIR_LE		0x000000	/* little endian - no transformation */
#define     M_OPMODE_DIR_BE_8BPP	0x000000
#define     M_OPMODE_DIR_BE_16BPP	0x010000
#define     M_OPMODE_DIR_BE_32BPP	0x020000

#define M_ATTR_INDEX	0x1FC0
#define M_ATTR_DATA	0x1FC1

#define M_MISC_REG	0x1FC2
#define M_3C2_RD	0x1FC2

#define M_SEQ_INDEX	0x1FC4
#define M_SEQ_DATA	0x1FC5
#define     M_SEQ1		0x01
#define        M_SEQ1_SCROFF		0x20

#define M_MISC_REG_READ	0x1FCC

#define M_GRAPHICS_INDEX 0x1FCE
#define M_GRAPHICS_DATA	0x1FCF

#define M_CRTC_INDEX	0x1FD4

#define M_ATTR_RESET	0x1FDA
#define M_3DA_WR	0x1FDA
#define M_INSTS1	0x1FDA

#define M_EXTVGA_INDEX	0x1FDE
#define M_EXTVGA_DATA	0x1FDF

/* G200 only */
#define M_SRCORG	0x2CB4
#define M_DSTORG	0x2CB8

#define M_RAMDAC_BASE	0x3C00

/* fortunately, same on TVP3026 and MGA1064 */
#define M_DAC_REG	(M_RAMDAC_BASE+0)
#define M_DAC_VAL	(M_RAMDAC_BASE+1)
#define M_PALETTE_MASK	(M_RAMDAC_BASE+2)

#define M_X_INDEX	0x00
#define M_X_DATAREG	0x0A

#define DAC_XGENIOCTRL		0x2A
#define DAC_XGENIODATA		0x2B

#define M_C2CTL		0x3C10

#define MX_OPTION_BSWAP         0x00000000

#ifdef __LITTLE_ENDIAN
#define M_OPMODE_4BPP	(M_OPMODE_DMA_LE | M_OPMODE_DIR_LE | M_OPMODE_DMA_BLIT)
#define M_OPMODE_8BPP	(M_OPMODE_DMA_LE | M_OPMODE_DIR_LE | M_OPMODE_DMA_BLIT)
#define M_OPMODE_16BPP	(M_OPMODE_DMA_LE | M_OPMODE_DIR_LE | M_OPMODE_DMA_BLIT)
#define M_OPMODE_24BPP	(M_OPMODE_DMA_LE | M_OPMODE_DIR_LE | M_OPMODE_DMA_BLIT)
#define M_OPMODE_32BPP	(M_OPMODE_DMA_LE | M_OPMODE_DIR_LE | M_OPMODE_DMA_BLIT)
#else
#ifdef __BIG_ENDIAN
#define M_OPMODE_4BPP	(M_OPMODE_DMA_LE | M_OPMODE_DIR_LE       | M_OPMODE_DMA_BLIT)	/* TODO */
#define M_OPMODE_8BPP	(M_OPMODE_DMA_LE | M_OPMODE_DIR_BE_8BPP  | M_OPMODE_DMA_BLIT)
#define M_OPMODE_16BPP	(M_OPMODE_DMA_LE | M_OPMODE_DIR_BE_16BPP | M_OPMODE_DMA_BLIT)
#define M_OPMODE_24BPP	(M_OPMODE_DMA_LE | M_OPMODE_DIR_BE_8BPP  | M_OPMODE_DMA_BLIT)	/* TODO, ?32 */
#define M_OPMODE_32BPP	(M_OPMODE_DMA_LE | M_OPMODE_DIR_BE_32BPP | M_OPMODE_DMA_BLIT)
#else
#error "Byte ordering have to be defined. Cannot continue."
#endif
#endif

#define mga_inb(addr)		mga_readb(minfo->mmio.vbase, (addr))
#define mga_inl(addr)		mga_readl(minfo->mmio.vbase, (addr))
#define mga_outb(addr,val)	mga_writeb(minfo->mmio.vbase, (addr), (val))
#define mga_outw(addr,val)	mga_writew(minfo->mmio.vbase, (addr), (val))
#define mga_outl(addr,val)	mga_writel(minfo->mmio.vbase, (addr), (val))
#define mga_readr(port,idx)	(mga_outb((port),(idx)), mga_inb((port)+1))
#define mga_setr(addr,port,val)	mga_outw(addr, ((val)<<8) | (port))

#define mga_fifo(n)	do {} while ((mga_inl(M_FIFOSTATUS) & 0xFF) < (n))

#define WaitTillIdle()	do {} while (mga_inl(M_STATUS) & 0x10000)

/* code speedup */
#ifdef CONFIG_FB_MATROX_MILLENIUM
#define isInterleave(x)	 (x->interleave)
#define isMillenium(x)	 (x->millenium)
#define isMilleniumII(x) (x->milleniumII)
#else
#define isInterleave(x)  (0)
#define isMillenium(x)	 (0)
#define isMilleniumII(x) (0)
#endif

#define matroxfb_DAC_lock()                   spin_lock(&minfo->lock.DAC)
#define matroxfb_DAC_unlock()                 spin_unlock(&minfo->lock.DAC)
#define matroxfb_DAC_lock_irqsave(flags)      spin_lock_irqsave(&minfo->lock.DAC, flags)
#define matroxfb_DAC_unlock_irqrestore(flags) spin_unlock_irqrestore(&minfo->lock.DAC, flags)
extern void matroxfb_DAC_out(const struct matrox_fb_info *minfo, int reg,
			     int val);
extern int matroxfb_DAC_in(const struct matrox_fb_info *minfo, int reg);
extern void matroxfb_var2my(struct fb_var_screeninfo* fvsi, struct my_timming* mt);
extern int matroxfb_wait_for_sync(struct matrox_fb_info *minfo, u_int32_t crtc);
extern int matroxfb_enable_irq(struct matrox_fb_info *minfo, int reenable);

#ifdef MATROXFB_USE_SPINLOCKS
#define CRITBEGIN  spin_lock_irqsave(&minfo->lock.accel, critflags);
#define CRITEND	   spin_unlock_irqrestore(&minfo->lock.accel, critflags);
#define CRITFLAGS  unsigned long critflags;
#else
#define CRITBEGIN
#define CRITEND
#define CRITFLAGS
#endif

#endif	/* __MATROXFB_H__ */
