#ifndef __RADEONFB_H__
#define __RADEONFB_H__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/fb.h>


#ifdef CONFIG_FB_RADEON_I2C
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#endif

#include <asm/io.h>

#ifdef CONFIG_PPC_OF
#include <asm/prom.h>
#endif

#include <video/radeon.h>

/***************************************************************
 * Most of the definitions here are adapted right from XFree86 *
 ***************************************************************/


/*
 * Chip families. Must fit in the low 16 bits of a long word
 */
enum radeon_family {
	CHIP_FAMILY_UNKNOW,
	CHIP_FAMILY_LEGACY,
	CHIP_FAMILY_RADEON,
	CHIP_FAMILY_RV100,
	CHIP_FAMILY_RS100,    /* U1 (IGP320M) or A3 (IGP320)*/
	CHIP_FAMILY_RV200,
	CHIP_FAMILY_RS200,    /* U2 (IGP330M/340M/350M) or A4 (IGP330/340/345/350),
				 RS250 (IGP 7000) */
	CHIP_FAMILY_R200,
	CHIP_FAMILY_RV250,
	CHIP_FAMILY_RS300,    /* Radeon 9000 IGP */
	CHIP_FAMILY_RV280,
	CHIP_FAMILY_R300,
	CHIP_FAMILY_R350,
	CHIP_FAMILY_RV350,
	CHIP_FAMILY_RV380,    /* RV370/RV380/M22/M24 */
	CHIP_FAMILY_R420,     /* R420/R423/M18 */
	CHIP_FAMILY_LAST,
};

#define IS_RV100_VARIANT(rinfo) (((rinfo)->family == CHIP_FAMILY_RV100)  || \
				 ((rinfo)->family == CHIP_FAMILY_RV200)  || \
				 ((rinfo)->family == CHIP_FAMILY_RS100)  || \
				 ((rinfo)->family == CHIP_FAMILY_RS200)  || \
				 ((rinfo)->family == CHIP_FAMILY_RV250)  || \
				 ((rinfo)->family == CHIP_FAMILY_RV280)  || \
				 ((rinfo)->family == CHIP_FAMILY_RS300))


#define IS_R300_VARIANT(rinfo) (((rinfo)->family == CHIP_FAMILY_R300)  || \
				((rinfo)->family == CHIP_FAMILY_RV350) || \
				((rinfo)->family == CHIP_FAMILY_R350)  || \
				((rinfo)->family == CHIP_FAMILY_RV380) || \
				((rinfo)->family == CHIP_FAMILY_R420))

/*
 * Chip flags
 */
enum radeon_chip_flags {
	CHIP_FAMILY_MASK	= 0x0000ffffUL,
	CHIP_FLAGS_MASK		= 0xffff0000UL,
	CHIP_IS_MOBILITY	= 0x00010000UL,
	CHIP_IS_IGP		= 0x00020000UL,
	CHIP_HAS_CRTC2		= 0x00040000UL,	
};

/*
 * Errata workarounds
 */
enum radeon_errata {
	CHIP_ERRATA_R300_CG		= 0x00000001,
	CHIP_ERRATA_PLL_DUMMYREADS	= 0x00000002,
	CHIP_ERRATA_PLL_DELAY		= 0x00000004,
};


/*
 * Monitor types
 */
enum radeon_montype {
	MT_NONE = 0,
	MT_CRT,		/* CRT */
	MT_LCD,		/* LCD */
	MT_DFP,		/* DVI */
	MT_CTV,		/* composite TV */
	MT_STV		/* S-Video out */
};

/*
 * DDC i2c ports
 */
enum ddc_type {
	ddc_none,
	ddc_monid,
	ddc_dvi,
	ddc_vga,
	ddc_crt2,
};

/*
 * Connector types
 */
enum conn_type {
	conn_none,
	conn_proprietary,
	conn_crt,
	conn_DVI_I,
	conn_DVI_D,
};


/*
 * PLL infos
 */
struct pll_info {
	int ppll_max;
	int ppll_min;
	int sclk, mclk;
	int ref_div;
	int ref_clk;
};


/*
 * This structure contains the various registers manipulated by this
 * driver for setting or restoring a mode. It's mostly copied from
 * XFree's RADEONSaveRec structure. A few chip settings might still be
 * tweaked without beeing reflected or saved in these registers though
 */
struct radeon_regs {
	/* Common registers */
	u32		ovr_clr;
	u32		ovr_wid_left_right;
	u32		ovr_wid_top_bottom;
	u32		ov0_scale_cntl;
	u32		mpp_tb_config;
	u32		mpp_gp_config;
	u32		subpic_cntl;
	u32		viph_control;
	u32		i2c_cntl_1;
	u32		gen_int_cntl;
	u32		cap0_trig_cntl;
	u32		cap1_trig_cntl;
	u32		bus_cntl;
	u32		surface_cntl;
	u32		bios_5_scratch;

	/* Other registers to save for VT switches or driver load/unload */
	u32		dp_datatype;
	u32		rbbm_soft_reset;
	u32		clock_cntl_index;
	u32		amcgpio_en_reg;
	u32		amcgpio_mask;

	/* Surface/tiling registers */
	u32		surf_lower_bound[8];
	u32		surf_upper_bound[8];
	u32		surf_info[8];

	/* CRTC registers */
	u32		crtc_gen_cntl;
	u32		crtc_ext_cntl;
	u32		dac_cntl;
	u32		crtc_h_total_disp;
	u32		crtc_h_sync_strt_wid;
	u32		crtc_v_total_disp;
	u32		crtc_v_sync_strt_wid;
	u32		crtc_offset;
	u32		crtc_offset_cntl;
	u32		crtc_pitch;
	u32		disp_merge_cntl;
	u32		grph_buffer_cntl;
	u32		crtc_more_cntl;

	/* CRTC2 registers */
	u32		crtc2_gen_cntl;
	u32		dac2_cntl;
	u32		disp_output_cntl;
	u32		disp_hw_debug;
	u32		disp2_merge_cntl;
	u32		grph2_buffer_cntl;
	u32		crtc2_h_total_disp;
	u32		crtc2_h_sync_strt_wid;
	u32		crtc2_v_total_disp;
	u32		crtc2_v_sync_strt_wid;
	u32		crtc2_offset;
	u32		crtc2_offset_cntl;
	u32		crtc2_pitch;

	/* Flat panel regs */
	u32 		fp_crtc_h_total_disp;
	u32		fp_crtc_v_total_disp;
	u32		fp_gen_cntl;
	u32		fp2_gen_cntl;
	u32		fp_h_sync_strt_wid;
	u32		fp2_h_sync_strt_wid;
	u32		fp_horz_stretch;
	u32		fp_panel_cntl;
	u32		fp_v_sync_strt_wid;
	u32		fp2_v_sync_strt_wid;
	u32		fp_vert_stretch;
	u32		lvds_gen_cntl;
	u32		lvds_pll_cntl;
	u32		tmds_crc;
	u32		tmds_transmitter_cntl;

	/* Computed values for PLL */
	u32		dot_clock_freq;
	int		feedback_div;
	int		post_div;	

	/* PLL registers */
	u32		ppll_div_3;
	u32		ppll_ref_div;
	u32		vclk_ecp_cntl;
	u32		clk_cntl_index;

	/* Computed values for PLL2 */
	u32		dot_clock_freq_2;
	int		feedback_div_2;
	int		post_div_2;

	/* PLL2 registers */
	u32		p2pll_ref_div;
	u32		p2pll_div_0;
	u32		htotal_cntl2;

       	/* Palette */
	int		palette_valid;
};

struct panel_info {
	int xres, yres;
	int valid;
	int clock;
	int hOver_plus, hSync_width, hblank;
	int vOver_plus, vSync_width, vblank;
	int hAct_high, vAct_high, interlaced;
	int pwr_delay;
	int use_bios_dividers;
	int ref_divider;
	int post_divider;
	int fbk_divider;
};

struct radeonfb_info;

#ifdef CONFIG_FB_RADEON_I2C
struct radeon_i2c_chan {
	struct radeonfb_info		*rinfo;
	u32		 		ddc_reg;
	struct i2c_adapter		adapter;
	struct i2c_algo_bit_data	algo;
};
#endif

enum radeon_pm_mode {
	radeon_pm_none	= 0,		/* Nothing supported */
	radeon_pm_d2	= 0x00000001,	/* Can do D2 state */
	radeon_pm_off	= 0x00000002,	/* Can resume from D3 cold */
};

typedef void (*reinit_function_ptr)(struct radeonfb_info *rinfo);

struct radeonfb_info {
	struct fb_info		*info;

	struct radeon_regs 	state;
	struct radeon_regs	init_state;

	char			name[DEVICE_NAME_SIZE];

	unsigned long		mmio_base_phys;
	unsigned long		fb_base_phys;

	void __iomem		*mmio_base;
	void __iomem		*fb_base;

	unsigned long		fb_local_base;

	struct pci_dev		*pdev;
#ifdef CONFIG_PPC_OF
	struct device_node	*of_node;
#endif

	void __iomem		*bios_seg;
	int			fp_bios_start;

	u32			pseudo_palette[17];
	struct { u8 red, green, blue, pad; }
				palette[256];

	int			chipset;
	u8			family;
	u8			rev;
	unsigned int		errata;
	unsigned long		video_ram;
	unsigned long		mapped_vram;
	int			vram_width;
	int			vram_ddr;

	int			pitch, bpp, depth;

	int			has_CRTC2;
	int			is_mobility;
	int			is_IGP;
	int			reversed_DAC;
	int			reversed_TMDS;
	struct panel_info	panel_info;
	int			mon1_type;
	u8			*mon1_EDID;
	struct fb_videomode	*mon1_modedb;
	int			mon1_dbsize;
	int			mon2_type;
	u8		        *mon2_EDID;

	u32			dp_gui_master_cntl;

	struct pll_info		pll;

	int			mtrr_hdl;

	int			pm_reg;
	u32			save_regs[100];
	int			asleep;
	int			lock_blank;
	int			dynclk;
	int			no_schedule;
	enum radeon_pm_mode	pm_mode;
	reinit_function_ptr     reinit_func;

	/* Lock on register access */
	spinlock_t		reg_lock;

	/* Timer used for delayed LVDS operations */
	struct timer_list	lvds_timer;
	u32			pending_lvds_gen_cntl;

#ifdef CONFIG_FB_RADEON_I2C
	struct radeon_i2c_chan 	i2c[4];
#endif

	u32			cfg_save[64];
};


#define PRIMARY_MONITOR(rinfo)	(rinfo->mon1_type)


/*
 * Debugging stuffs
 */
#ifdef CONFIG_FB_RADEON_DEBUG
#define DEBUG		1
#else
#define DEBUG		0
#endif

#if DEBUG
#define RTRACE		printk
#else
#define RTRACE		if(0) printk
#endif


/*
 * IO macros
 */

/* Note about this function: we have some rare cases where we must not schedule,
 * this typically happen with our special "wake up early" hook which allows us to
 * wake up the graphic chip (and thus get the console back) before everything else
 * on some machines that support that mechanism. At this point, interrupts are off
 * and scheduling is not permitted
 */
static inline void _radeon_msleep(struct radeonfb_info *rinfo, unsigned long ms)
{
	if (rinfo->no_schedule || oops_in_progress)
		mdelay(ms);
	else
		msleep(ms);
}


#define INREG8(addr)		readb((rinfo->mmio_base)+addr)
#define OUTREG8(addr,val)	writeb(val, (rinfo->mmio_base)+addr)
#define INREG16(addr)		readw((rinfo->mmio_base)+addr)
#define OUTREG16(addr,val)	writew(val, (rinfo->mmio_base)+addr)
#define INREG(addr)		readl((rinfo->mmio_base)+addr)
#define OUTREG(addr,val)	writel(val, (rinfo->mmio_base)+addr)

static inline void _OUTREGP(struct radeonfb_info *rinfo, u32 addr,
		       u32 val, u32 mask)
{
	unsigned long flags;
	unsigned int tmp;

	spin_lock_irqsave(&rinfo->reg_lock, flags);
	tmp = INREG(addr);
	tmp &= (mask);
	tmp |= (val);
	OUTREG(addr, tmp);
	spin_unlock_irqrestore(&rinfo->reg_lock, flags);
}

#define OUTREGP(addr,val,mask)	_OUTREGP(rinfo, addr, val,mask)

/*
 * Note about PLL register accesses:
 *
 * I have removed the spinlock on them on purpose. The driver now
 * expects that it will only manipulate the PLL registers in normal
 * task environment, where radeon_msleep() will be called, protected
 * by a semaphore (currently the console semaphore) so that no conflict
 * will happen on the PLL register index.
 *
 * With the latest changes to the VT layer, this is guaranteed for all
 * calls except the actual drawing/blits which aren't supposed to use
 * the PLL registers anyway
 *
 * This is very important for the workarounds to work properly. The only
 * possible exception to this rule is the call to unblank(), which may
 * be done at irq time if an oops is in progress.
 */
static inline void radeon_pll_errata_after_index(struct radeonfb_info *rinfo)
{
	if (!(rinfo->errata & CHIP_ERRATA_PLL_DUMMYREADS))
		return;

	(void)INREG(CLOCK_CNTL_DATA);
	(void)INREG(CRTC_GEN_CNTL);
}

static inline void radeon_pll_errata_after_data(struct radeonfb_info *rinfo)
{
	if (rinfo->errata & CHIP_ERRATA_PLL_DELAY) {
		/* we can't deal with posted writes here ... */
		_radeon_msleep(rinfo, 5);
	}
	if (rinfo->errata & CHIP_ERRATA_R300_CG) {
		u32 save, tmp;
		save = INREG(CLOCK_CNTL_INDEX);
		tmp = save & ~(0x3f | PLL_WR_EN);
		OUTREG(CLOCK_CNTL_INDEX, tmp);
		tmp = INREG(CLOCK_CNTL_DATA);
		OUTREG(CLOCK_CNTL_INDEX, save);
	}
}

static inline u32 __INPLL(struct radeonfb_info *rinfo, u32 addr)
{
	u32 data;

	OUTREG8(CLOCK_CNTL_INDEX, addr & 0x0000003f);
	radeon_pll_errata_after_index(rinfo);
	data = INREG(CLOCK_CNTL_DATA);
	radeon_pll_errata_after_data(rinfo);
	return data;
}

static inline void __OUTPLL(struct radeonfb_info *rinfo, unsigned int index,
			    u32 val)
{

	OUTREG8(CLOCK_CNTL_INDEX, (index & 0x0000003f) | 0x00000080);
	radeon_pll_errata_after_index(rinfo);
	OUTREG(CLOCK_CNTL_DATA, val);
	radeon_pll_errata_after_data(rinfo);
}


static inline void __OUTPLLP(struct radeonfb_info *rinfo, unsigned int index,
			     u32 val, u32 mask)
{
	unsigned int tmp;

	tmp  = __INPLL(rinfo, index);
	tmp &= (mask);
	tmp |= (val);
	__OUTPLL(rinfo, index, tmp);
}


#define INPLL(addr)			__INPLL(rinfo, addr)
#define OUTPLL(index, val)		__OUTPLL(rinfo, index, val)
#define OUTPLLP(index, val, mask)	__OUTPLLP(rinfo, index, val, mask)


#define BIOS_IN8(v)  	(readb(rinfo->bios_seg + (v)))
#define BIOS_IN16(v) 	(readb(rinfo->bios_seg + (v)) | \
			  (readb(rinfo->bios_seg + (v) + 1) << 8))
#define BIOS_IN32(v) 	(readb(rinfo->bios_seg + (v)) | \
			  (readb(rinfo->bios_seg + (v) + 1) << 8) | \
			  (readb(rinfo->bios_seg + (v) + 2) << 16) | \
			  (readb(rinfo->bios_seg + (v) + 3) << 24))

/*
 * Inline utilities
 */
static inline int round_div(int num, int den)
{
        return (num + (den / 2)) / den;
}

static inline int var_to_depth(const struct fb_var_screeninfo *var)
{
	if (var->bits_per_pixel != 16)
		return var->bits_per_pixel;
	return (var->green.length == 5) ? 15 : 16;
}

static inline u32 radeon_get_dstbpp(u16 depth)
{
	switch (depth) {
       	case 8:
       		return DST_8BPP;
       	case 15:
       		return DST_15BPP;
       	case 16:
       		return DST_16BPP;
       	case 32:
       		return DST_32BPP;
       	default:
       		return 0;
	}
}

/*
 * 2D Engine helper routines
 */
static inline void radeon_engine_flush (struct radeonfb_info *rinfo)
{
	int i;

	/* initiate flush */
	OUTREGP(RB2D_DSTCACHE_CTLSTAT, RB2D_DC_FLUSH_ALL,
	        ~RB2D_DC_FLUSH_ALL);

	for (i=0; i < 2000000; i++) {
		if (!(INREG(RB2D_DSTCACHE_CTLSTAT) & RB2D_DC_BUSY))
			return;
		udelay(1);
	}
	printk(KERN_ERR "radeonfb: Flush Timeout !\n");
}


static inline void _radeon_fifo_wait(struct radeonfb_info *rinfo, int entries)
{
	int i;

	for (i=0; i<2000000; i++) {
		if ((INREG(RBBM_STATUS) & 0x7f) >= entries)
			return;
		udelay(1);
	}
	printk(KERN_ERR "radeonfb: FIFO Timeout !\n");
}


static inline void _radeon_engine_idle(struct radeonfb_info *rinfo)
{
	int i;

	/* ensure FIFO is empty before waiting for idle */
	_radeon_fifo_wait (rinfo, 64);

	for (i=0; i<2000000; i++) {
		if (((INREG(RBBM_STATUS) & GUI_ACTIVE)) == 0) {
			radeon_engine_flush (rinfo);
			return;
		}
		udelay(1);
	}
	printk(KERN_ERR "radeonfb: Idle Timeout !\n");
}


#define radeon_engine_idle()		_radeon_engine_idle(rinfo)
#define radeon_fifo_wait(entries)	_radeon_fifo_wait(rinfo,entries)
#define radeon_msleep(ms)		_radeon_msleep(rinfo,ms)


/* I2C Functions */
extern void radeon_create_i2c_busses(struct radeonfb_info *rinfo);
extern void radeon_delete_i2c_busses(struct radeonfb_info *rinfo);
extern int radeon_probe_i2c_connector(struct radeonfb_info *rinfo, int conn, u8 **out_edid);

/* PM Functions */
extern int radeonfb_pci_suspend(struct pci_dev *pdev, pm_message_t state);
extern int radeonfb_pci_resume(struct pci_dev *pdev);
extern void radeonfb_pm_init(struct radeonfb_info *rinfo, int dynclk, int ignore_devlist, int force_sleep);
extern void radeonfb_pm_exit(struct radeonfb_info *rinfo);

/* Monitor probe functions */
extern void radeon_probe_screens(struct radeonfb_info *rinfo,
				 const char *monitor_layout, int ignore_edid);
extern void radeon_check_modes(struct radeonfb_info *rinfo, const char *mode_option);
extern int radeon_match_mode(struct radeonfb_info *rinfo,
			     struct fb_var_screeninfo *dest,
			     const struct fb_var_screeninfo *src);

/* Accel functions */
extern void radeonfb_fillrect(struct fb_info *info, const struct fb_fillrect *region);
extern void radeonfb_copyarea(struct fb_info *info, const struct fb_copyarea *area);
extern void radeonfb_imageblit(struct fb_info *p, const struct fb_image *image);
extern int radeonfb_sync(struct fb_info *info);
extern void radeonfb_engine_init (struct radeonfb_info *rinfo);
extern void radeonfb_engine_reset(struct radeonfb_info *rinfo);

/* Other functions */
extern int radeon_screen_blank(struct radeonfb_info *rinfo, int blank, int mode_switch);
extern void radeon_write_mode (struct radeonfb_info *rinfo, struct radeon_regs *mode,
			       int reg_only);

/* Backlight functions */
#ifdef CONFIG_FB_RADEON_BACKLIGHT
extern void radeonfb_bl_init(struct radeonfb_info *rinfo);
extern void radeonfb_bl_exit(struct radeonfb_info *rinfo);
#else
static inline void radeonfb_bl_init(struct radeonfb_info *rinfo) {}
static inline void radeonfb_bl_exit(struct radeonfb_info *rinfo) {}
#endif

#endif /* __RADEONFB_H__ */
