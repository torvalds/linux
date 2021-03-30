/* SPDX-License-Identifier: GPL-2.0 */
/*
 *  ATI Frame Buffer Device Driver Core Definitions
 */

#include <linux/spinlock.h>
#include <linux/wait.h>
    /*
     *  Elements of the hardware specific atyfb_par structure
     */

struct crtc {
	u32 vxres;
	u32 vyres;
	u32 xoffset;
	u32 yoffset;
	u32 bpp;
	u32 h_tot_disp;
	u32 h_sync_strt_wid;
	u32 v_tot_disp;
	u32 v_sync_strt_wid;
	u32 vline_crnt_vline;
	u32 off_pitch;
	u32 gen_cntl;
	u32 dp_pix_width;	/* acceleration */
	u32 dp_chain_mask;	/* acceleration */
#ifdef CONFIG_FB_ATY_GENERIC_LCD
	u32 horz_stretching;
	u32 vert_stretching;
	u32 ext_vert_stretch;
	u32 shadow_h_tot_disp;
	u32 shadow_h_sync_strt_wid;
	u32 shadow_v_tot_disp;
	u32 shadow_v_sync_strt_wid;
	u32 lcd_gen_cntl;
	u32 lcd_config_panel;
	u32 lcd_index;
#endif
};

struct aty_interrupt {
	wait_queue_head_t wait;
	unsigned int count;
	int pan_display;
};

struct pll_info {
	int pll_max;
	int pll_min;
	int sclk, mclk, mclk_pm, xclk;
	int ref_div;
	int ref_clk;
	int ecp_max;
};

typedef struct {
	u16 unknown1;
	u16 PCLK_min_freq;
	u16 PCLK_max_freq;
	u16 unknown2;
	u16 ref_freq;
	u16 ref_divider;
	u16 unknown3;
	u16 MCLK_pwd;
	u16 MCLK_max_freq;
	u16 XCLK_max_freq;
	u16 SCLK_freq;
} __attribute__ ((packed)) PLL_BLOCK_MACH64;

struct pll_514 {
	u8 m;
	u8 n;
};

struct pll_18818 {
	u32 program_bits;
	u32 locationAddr;
	u32 period_in_ps;
	u32 post_divider;
};

struct pll_ct {
	u8 pll_ref_div;
	u8 pll_gen_cntl;
	u8 mclk_fb_div;
	u8 mclk_fb_mult; /* 2 ro 4 */
	u8 sclk_fb_div;
	u8 pll_vclk_cntl;
	u8 vclk_post_div;
	u8 vclk_fb_div;
	u8 pll_ext_cntl;
	u8 ext_vpll_cntl;
	u8 spll_cntl2;
	u32 dsp_config; /* Mach64 GTB DSP */
	u32 dsp_on_off; /* Mach64 GTB DSP */
	u32 dsp_loop_latency;
	u32 fifo_size;
	u32 xclkpagefaultdelay;
	u32 xclkmaxrasdelay;
	u8 xclk_ref_div;
	u8 xclk_post_div;
	u8 mclk_post_div_real;
	u8 xclk_post_div_real;
	u8 vclk_post_div_real;
	u8 features;
#ifdef CONFIG_FB_ATY_GENERIC_LCD
	u32 xres; /* use for LCD stretching/scaling */
#endif
};

/*
	for pll_ct.features
*/
#define DONT_USE_SPLL 0x1
#define DONT_USE_XDLL 0x2
#define USE_CPUCLK    0x4
#define POWERDOWN_PLL 0x8

union aty_pll {
	struct pll_ct ct;
	struct pll_514 ibm514;
	struct pll_18818 ics2595;
};

    /*
     *  The hardware parameters for each card
     */

struct atyfb_par {
	u32 pseudo_palette[16];
	struct { u8 red, green, blue; } palette[256];
	const struct aty_dac_ops *dac_ops;
	const struct aty_pll_ops *pll_ops;
	void __iomem *ati_regbase;
	unsigned long clk_wr_offset; /* meaning overloaded, clock id by CT */
	struct crtc crtc;
	union aty_pll pll;
	struct pll_info pll_limits;
	u32 features;
	u32 ref_clk_per;
	u32 pll_per;
	u32 mclk_per;
	u32 xclk_per;
	u8 bus_type;
	u8 ram_type;
	u8 mem_refresh_rate;
	u16 pci_id;
	u32 accel_flags;
	int blitter_may_be_busy;
	unsigned fifo_space;
	int asleep;
	int lock_blank;
	unsigned long res_start;
	unsigned long res_size;
	struct pci_dev *pdev;
#ifdef __sparc__
	struct pci_mmap_map *mmap_map;
	u8 mmaped;
#endif
	int open;
#ifdef CONFIG_FB_ATY_GENERIC_LCD
	unsigned long bios_base_phys;
	unsigned long bios_base;
	unsigned long lcd_table;
	u16 lcd_width;
	u16 lcd_height;
	u32 lcd_pixclock;
	u16 lcd_refreshrate;
	u16 lcd_htotal;
	u16 lcd_hdisp;
	u16 lcd_hsync_dly;
	u16 lcd_hsync_len;
	u16 lcd_vtotal;
	u16 lcd_vdisp;
	u16 lcd_vsync_len;
	u16 lcd_right_margin;
	u16 lcd_lower_margin;
	u16 lcd_hblank_len;
	u16 lcd_vblank_len;
#endif
	unsigned long aux_start; /* auxiliary aperture */
	unsigned long aux_size;
	struct aty_interrupt vblank;
	unsigned long irq_flags;
	unsigned int irq;
	spinlock_t int_lock;
	int wc_cookie;
	u32 mem_cntl;
	struct crtc saved_crtc;
	union aty_pll saved_pll;
};

    /*
     *  ATI Mach64 features
     */

#define M64_HAS(feature)	((par)->features & (M64F_##feature))

#define M64F_RESET_3D		0x00000001
#define M64F_MAGIC_FIFO		0x00000002
#define M64F_GTB_DSP		0x00000004
#define M64F_FIFO_32		0x00000008
#define M64F_SDRAM_MAGIC_PLL	0x00000010
#define M64F_MAGIC_POSTDIV	0x00000020
#define M64F_INTEGRATED		0x00000040
#define M64F_CT_BUS		0x00000080
#define M64F_VT_BUS		0x00000100
#define M64F_MOBIL_BUS		0x00000200
#define M64F_GX			0x00000400
#define M64F_CT			0x00000800
#define M64F_VT			0x00001000
#define M64F_GT			0x00002000
#define M64F_MAGIC_VRAM_SIZE	0x00004000
#define M64F_G3_PB_1_1		0x00008000
#define M64F_G3_PB_1024x768	0x00010000
#define M64F_EXTRA_BRIGHT	0x00020000
#define M64F_LT_LCD_REGS	0x00040000
#define M64F_XL_DLL		0x00080000
#define M64F_MFB_FORCE_4	0x00100000
#define M64F_HW_TRIPLE		0x00200000
#define M64F_XL_MEM		0x00400000
    /*
     *  Register access
     */

static inline u32 aty_ld_le32(int regindex, const struct atyfb_par *par)
{
	/* Hack for bloc 1, should be cleanly optimized by compiler */
	if (regindex >= 0x400)
		regindex -= 0x800;

#ifdef CONFIG_ATARI
	return in_le32(par->ati_regbase + regindex);
#else
	return readl(par->ati_regbase + regindex);
#endif
}

static inline void aty_st_le32(int regindex, u32 val, const struct atyfb_par *par)
{
	/* Hack for bloc 1, should be cleanly optimized by compiler */
	if (regindex >= 0x400)
		regindex -= 0x800;

#ifdef CONFIG_ATARI
	out_le32(par->ati_regbase + regindex, val);
#else
	writel(val, par->ati_regbase + regindex);
#endif
}

static inline void aty_st_le16(int regindex, u16 val,
			       const struct atyfb_par *par)
{
	/* Hack for bloc 1, should be cleanly optimized by compiler */
	if (regindex >= 0x400)
		regindex -= 0x800;
#ifdef CONFIG_ATARI
	out_le16(par->ati_regbase + regindex, val);
#else
	writel(val, par->ati_regbase + regindex);
#endif
}

static inline u8 aty_ld_8(int regindex, const struct atyfb_par *par)
{
	/* Hack for bloc 1, should be cleanly optimized by compiler */
	if (regindex >= 0x400)
		regindex -= 0x800;
#ifdef CONFIG_ATARI
	return in_8(par->ati_regbase + regindex);
#else
	return readb(par->ati_regbase + regindex);
#endif
}

static inline void aty_st_8(int regindex, u8 val, const struct atyfb_par *par)
{
	/* Hack for bloc 1, should be cleanly optimized by compiler */
	if (regindex >= 0x400)
		regindex -= 0x800;

#ifdef CONFIG_ATARI
	out_8(par->ati_regbase + regindex, val);
#else
	writeb(val, par->ati_regbase + regindex);
#endif
}

extern void aty_st_lcd(int index, u32 val, const struct atyfb_par *par);
extern u32 aty_ld_lcd(int index, const struct atyfb_par *par);

    /*
     *  DAC operations
     */

struct aty_dac_ops {
	int (*set_dac) (const struct fb_info * info,
		const union aty_pll * pll, u32 bpp, u32 accel);
};

extern const struct aty_dac_ops aty_dac_ibm514; /* IBM RGB514 */
extern const struct aty_dac_ops aty_dac_ati68860b; /* ATI 68860-B */
extern const struct aty_dac_ops aty_dac_att21c498; /* AT&T 21C498 */
extern const struct aty_dac_ops aty_dac_unsupported; /* unsupported */
extern const struct aty_dac_ops aty_dac_ct; /* Integrated */


    /*
     *  Clock operations
     */

struct aty_pll_ops {
	int (*var_to_pll) (const struct fb_info * info, u32 vclk_per, u32 bpp, union aty_pll * pll);
	u32 (*pll_to_var) (const struct fb_info * info, const union aty_pll * pll);
	void (*set_pll)   (const struct fb_info * info, const union aty_pll * pll);
	void (*get_pll)   (const struct fb_info *info, union aty_pll * pll);
	int (*init_pll)   (const struct fb_info * info, union aty_pll * pll);
	void (*resume_pll)(const struct fb_info *info, union aty_pll *pll);
};

extern const struct aty_pll_ops aty_pll_ati18818_1; /* ATI 18818 */
extern const struct aty_pll_ops aty_pll_stg1703; /* STG 1703 */
extern const struct aty_pll_ops aty_pll_ch8398; /* Chrontel 8398 */
extern const struct aty_pll_ops aty_pll_att20c408; /* AT&T 20C408 */
extern const struct aty_pll_ops aty_pll_ibm514; /* IBM RGB514 */
extern const struct aty_pll_ops aty_pll_unsupported; /* unsupported */
extern const struct aty_pll_ops aty_pll_ct; /* Integrated */


extern void aty_set_pll_ct(const struct fb_info *info, const union aty_pll *pll);
extern u8 aty_ld_pll_ct(int offset, const struct atyfb_par *par);

extern const u8 aty_postdividers[8];


    /*
     *  Hardware cursor support
     */

extern int aty_init_cursor(struct fb_info *info, struct fb_ops *atyfb_ops);

    /*
     *  Hardware acceleration
     */

static inline void wait_for_fifo(u16 entries, struct atyfb_par *par)
{
	unsigned fifo_space = par->fifo_space;
	while (entries > fifo_space) {
		fifo_space = 16 - fls(aty_ld_le32(FIFO_STAT, par) & 0xffff);
	}
	par->fifo_space = fifo_space - entries;
}

static inline void wait_for_idle(struct atyfb_par *par)
{
	wait_for_fifo(16, par);
	while ((aty_ld_le32(GUI_STAT, par) & 1) != 0);
	par->blitter_may_be_busy = 0;
}

extern void aty_reset_engine(struct atyfb_par *par);
extern void aty_init_engine(struct atyfb_par *par, struct fb_info *info);

void atyfb_copyarea(struct fb_info *info, const struct fb_copyarea *area);
void atyfb_fillrect(struct fb_info *info, const struct fb_fillrect *rect);
void atyfb_imageblit(struct fb_info *info, const struct fb_image *image);

