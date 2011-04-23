#ifndef SH_MOBILE_LCDCFB_H
#define SH_MOBILE_LCDCFB_H

#include <linux/completion.h>
#include <linux/fb.h>
#include <linux/mutex.h>
#include <linux/wait.h>

/* per-channel registers */
enum { LDDCKPAT1R, LDDCKPAT2R, LDMT1R, LDMT2R, LDMT3R, LDDFR, LDSM1R,
       LDSM2R, LDSA1R, LDSA2R, LDMLSR, LDHCNR, LDHSYNR, LDVLNR, LDVSYNR, LDPMR,
       LDHAJR,
       NR_CH_REGS };

#define PALETTE_NR 16

struct sh_mobile_lcdc_priv;
struct fb_info;
struct backlight_device;

struct sh_mobile_lcdc_chan {
	struct sh_mobile_lcdc_priv *lcdc;
	unsigned long *reg_offs;
	unsigned long ldmt1r_value;
	unsigned long enabled; /* ME and SE in LDCNT2R */
	struct sh_mobile_lcdc_chan_cfg cfg;
	u32 pseudo_palette[PALETTE_NR];
	unsigned long saved_ch_regs[NR_CH_REGS];
	struct fb_info *info;
	struct backlight_device *bl;
	dma_addr_t dma_handle;
	struct fb_deferred_io defio;
	struct scatterlist *sglist;
	unsigned long frame_end;
	unsigned long pan_offset;
	wait_queue_head_t frame_end_wait;
	struct completion vsync_completion;
	struct fb_var_screeninfo display_var;
	int use_count;
	int blank_status;
	struct mutex open_lock;		/* protects the use counter */
};

#endif
