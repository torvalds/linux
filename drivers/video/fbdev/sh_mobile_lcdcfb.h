/* SPDX-License-Identifier: GPL-2.0 */
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

struct backlight_device;
struct fb_info;
struct module;
struct sh_mobile_lcdc_chan;
struct sh_mobile_lcdc_entity;
struct sh_mobile_lcdc_format_info;
struct sh_mobile_lcdc_priv;

#define SH_MOBILE_LCDC_DISPLAY_DISCONNECTED	0
#define SH_MOBILE_LCDC_DISPLAY_CONNECTED	1

struct sh_mobile_lcdc_entity_ops {
	/* Display */
	int (*display_on)(struct sh_mobile_lcdc_entity *entity);
	void (*display_off)(struct sh_mobile_lcdc_entity *entity);
};

enum sh_mobile_lcdc_entity_event {
	SH_MOBILE_LCDC_EVENT_DISPLAY_CONNECT,
	SH_MOBILE_LCDC_EVENT_DISPLAY_DISCONNECT,
	SH_MOBILE_LCDC_EVENT_DISPLAY_MODE,
};

struct sh_mobile_lcdc_entity {
	struct module *owner;
	const struct sh_mobile_lcdc_entity_ops *ops;
	struct sh_mobile_lcdc_chan *lcdc;
	struct fb_videomode def_mode;
};

/*
 * struct sh_mobile_lcdc_chan - LCDC display channel
 *
 * @pan_y_offset: Panning linear offset in bytes (luma component)
 * @base_addr_y: Frame buffer viewport base address (luma component)
 * @base_addr_c: Frame buffer viewport base address (chroma component)
 * @pitch: Frame buffer line pitch
 */
struct sh_mobile_lcdc_chan {
	struct sh_mobile_lcdc_priv *lcdc;
	struct sh_mobile_lcdc_entity *tx_dev;
	const struct sh_mobile_lcdc_chan_cfg *cfg;

	unsigned long *reg_offs;
	unsigned long ldmt1r_value;
	unsigned long enabled; /* ME and SE in LDCNT2R */

	struct mutex open_lock;		/* protects the use counter */
	int use_count;

	void *fb_mem;
	unsigned long fb_size;

	dma_addr_t dma_handle;
	unsigned long pan_y_offset;

	unsigned long frame_end;
	wait_queue_head_t frame_end_wait;
	struct completion vsync_completion;

	const struct sh_mobile_lcdc_format_info *format;
	u32 colorspace;
	unsigned int xres;
	unsigned int xres_virtual;
	unsigned int yres;
	unsigned int yres_virtual;
	unsigned int pitch;

	unsigned long base_addr_y;
	unsigned long base_addr_c;
	unsigned int line_size;

	int (*notify)(struct sh_mobile_lcdc_chan *ch,
		      enum sh_mobile_lcdc_entity_event event,
		      const struct fb_videomode *mode,
		      const struct fb_monspecs *monspec);

	/* Backlight */
	struct backlight_device *bl;
	unsigned int bl_brightness;

	/* FB */
	struct fb_info *info;
	u32 pseudo_palette[PALETTE_NR];
	struct {
		unsigned int width;
		unsigned int height;
		struct fb_videomode mode;
	} display;
	struct fb_deferred_io defio;
	struct scatterlist *sglist;
	int blank_status;
};

#endif
