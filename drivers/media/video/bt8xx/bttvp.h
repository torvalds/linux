/*

    bttv - Bt848 frame grabber driver

    bttv's *private* header file  --  nobody other than bttv itself
    should ever include this file.

    (c) 2000-2002 Gerd Knorr <kraxel@bytesex.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef _BTTVP_H_
#define _BTTVP_H_

#include <linux/version.h>
#define BTTV_VERSION_CODE KERNEL_VERSION(0,9,16)

#include <linux/types.h>
#include <linux/wait.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/videodev.h>
#include <linux/pci.h>
#include <linux/input.h>
#include <linux/mutex.h>
#include <asm/scatterlist.h>
#include <asm/io.h>

#include <linux/device.h>
#include <media/video-buf.h>
#include <media/tuner.h>
#include <media/tveeprom.h>
#include <media/ir-common.h>


#include "bt848.h"
#include "bttv.h"
#include "btcx-risc.h"

#ifdef __KERNEL__

#define FORMAT_FLAGS_DITHER       0x01
#define FORMAT_FLAGS_PACKED       0x02
#define FORMAT_FLAGS_PLANAR       0x04
#define FORMAT_FLAGS_RAW          0x08
#define FORMAT_FLAGS_CrCb         0x10

#define RISC_SLOT_O_VBI        4
#define RISC_SLOT_O_FIELD      6
#define RISC_SLOT_E_VBI       10
#define RISC_SLOT_E_FIELD     12
#define RISC_SLOT_LOOP        14

#define RESOURCE_OVERLAY       1
#define RESOURCE_VIDEO         2
#define RESOURCE_VBI           4

#define RAW_LINES            640
#define RAW_BPL             1024

#define UNSET (-1U)

#define clamp(x, low, high) min (max (low, x), high)

/* ---------------------------------------------------------- */

struct bttv_tvnorm {
	int   v4l2_id;
	char  *name;
	u32   Fsc;
	u16   swidth, sheight; /* scaled standard width, height */
	u16   totalwidth;
	u8    adelay, bdelay, iform;
	u32   scaledtwidth;
	u16   hdelayx1, hactivex1;
	u16   vdelay;
	u8    vbipack;
	u16   vtotal;
	int   sram;
	/* ITU-R frame line number of the first VBI line we can
	   capture, of the first and second field. */
	u16   vbistart[2];
};
extern const struct bttv_tvnorm bttv_tvnorms[];

struct bttv_format {
	char *name;
	int  palette;         /* video4linux 1      */
	int  fourcc;          /* video4linux 2      */
	int  btformat;        /* BT848_COLOR_FMT_*  */
	int  btswap;          /* BT848_COLOR_CTL_*  */
	int  depth;           /* bit/pixel          */
	int  flags;
	int  hshift,vshift;   /* for planar modes   */
};

/* ---------------------------------------------------------- */

struct bttv_geometry {
	u8  vtc,crop,comb;
	u16 width,hscale,hdelay;
	u16 sheight,vscale,vdelay,vtotal;
};

struct bttv_buffer {
	/* common v4l buffer stuff -- must be first */
	struct videobuf_buffer     vb;

	/* bttv specific */
	const struct bttv_format   *fmt;
	int                        tvnorm;
	int                        btformat;
	int                        btswap;
	struct bttv_geometry       geo;
	struct btcx_riscmem        top;
	struct btcx_riscmem        bottom;
};

struct bttv_buffer_set {
	struct bttv_buffer     *top;       /* top field buffer    */
	struct bttv_buffer     *bottom;    /* bottom field buffer */
	unsigned int           top_irq;
	unsigned int           frame_irq;
};

struct bttv_overlay {
	int                    tvnorm;
	struct v4l2_rect       w;
	enum v4l2_field        field;
	struct v4l2_clip       *clips;
	int                    nclips;
	int                    setup_ok;
};

struct bttv_fh {
	struct bttv              *btv;
	int resources;
#ifdef VIDIOC_G_PRIORITY
	enum v4l2_priority       prio;
#endif
	enum v4l2_buf_type       type;

	/* video capture */
	struct videobuf_queue    cap;
	const struct bttv_format *fmt;
	int                      width;
	int                      height;

	/* current settings */
	const struct bttv_format *ovfmt;
	struct bttv_overlay      ov;

	/* video overlay */
	struct videobuf_queue    vbi;
	int                      lines;
};

/* ---------------------------------------------------------- */
/* bttv-risc.c                                                */

/* risc code generators - capture */
int bttv_risc_packed(struct bttv *btv, struct btcx_riscmem *risc,
		     struct scatterlist *sglist,
		     unsigned int offset, unsigned int bpl,
		     unsigned int pitch, unsigned int lines);

/* control dma register + risc main loop */
void bttv_set_dma(struct bttv *btv, int override);
int bttv_risc_init_main(struct bttv *btv);
int bttv_risc_hook(struct bttv *btv, int slot, struct btcx_riscmem *risc,
		   int irqflags);

/* capture buffer handling */
int bttv_buffer_risc(struct bttv *btv, struct bttv_buffer *buf);
int bttv_buffer_activate_video(struct bttv *btv,
			       struct bttv_buffer_set *set);
int bttv_buffer_activate_vbi(struct bttv *btv,
			     struct bttv_buffer *vbi);
void bttv_dma_free(struct videobuf_queue *q, struct bttv *btv,
		   struct bttv_buffer *buf);

/* overlay handling */
int bttv_overlay_risc(struct bttv *btv, struct bttv_overlay *ov,
		      const struct bttv_format *fmt,
		      struct bttv_buffer *buf);


/* ---------------------------------------------------------- */
/* bttv-vbi.c                                                 */

void bttv_vbi_try_fmt(struct bttv_fh *fh, struct v4l2_format *f);
void bttv_vbi_get_fmt(struct bttv_fh *fh, struct v4l2_format *f);
void bttv_vbi_setlines(struct bttv_fh *fh, struct bttv *btv, int lines);

extern struct videobuf_queue_ops bttv_vbi_qops;

/* ---------------------------------------------------------- */
/* bttv-gpio.c */


extern struct bus_type bttv_sub_bus_type;
int bttv_sub_add_device(struct bttv_core *core, char *name);
int bttv_sub_del_devices(struct bttv_core *core);


/* ---------------------------------------------------------- */
/* bttv-driver.c                                              */

/* insmod options */
extern unsigned int bttv_verbose;
extern unsigned int bttv_debug;
extern unsigned int bttv_gpio;
extern void bttv_gpio_tracking(struct bttv *btv, char *comment);
extern int init_bttv_i2c(struct bttv *btv);
extern int fini_bttv_i2c(struct bttv *btv);

#define bttv_printk if (bttv_verbose) printk
#define dprintk  if (bttv_debug >= 1) printk
#define d2printk if (bttv_debug >= 2) printk

#define BTTV_MAX_FBUF   0x208000
#define VBIBUF_SIZE     (2048*VBI_MAXLINES*2)
#define BTTV_TIMEOUT    (HZ/2) /* 0.5 seconds */
#define BTTV_FREE_IDLE  (HZ)   /* one second */


struct bttv_pll_info {
	unsigned int pll_ifreq;    /* PLL input frequency        */
	unsigned int pll_ofreq;    /* PLL output frequency       */
	unsigned int pll_crystal;  /* Crystal used for input     */
	unsigned int pll_current;  /* Currently programmed ofreq */
};

/* for gpio-connected remote control */
struct bttv_input {
	struct input_dev      *dev;
	struct ir_input_state ir;
	char                  name[32];
	char                  phys[32];
	u32                   mask_keycode;
	u32                   mask_keydown;
};

struct bttv_suspend_state {
	u32  gpio_enable;
	u32  gpio_data;
	int  disabled;
	int  loop_irq;
	struct bttv_buffer_set video;
	struct bttv_buffer     *vbi;
};

struct bttv {
	struct bttv_core c;

	/* pci device config */
	unsigned short id;
	unsigned char revision;
	unsigned char __iomem *bt848_mmio;   /* pointer to mmio */

	/* card configuration info */
	unsigned int cardid;   /* pci subsystem id (bt878 based ones) */
	unsigned int tuner_type;  /* tuner chip type */
	unsigned int tda9887_conf;
	unsigned int svhs;
	struct bttv_pll_info pll;
	int triton1;
	int gpioirq;
	int (*custom_irq)(struct bttv *btv);

	int use_i2c_hw;

	/* old gpio interface */
	wait_queue_head_t gpioq;
	int shutdown;
	void (*audio_hook)(struct bttv *btv, struct video_audio *v, int set);

	/* new gpio interface */
	spinlock_t gpio_lock;

	/* i2c layer */
	struct i2c_algo_bit_data   i2c_algo;
	struct i2c_client          i2c_client;
	int                        i2c_state, i2c_rc;
	int                        i2c_done;
	wait_queue_head_t          i2c_queue;
	struct i2c_client 	  *i2c_msp34xx_client;
	struct i2c_client 	  *i2c_tvaudio_client;

	/* video4linux (1) */
	struct video_device *video_dev;
	struct video_device *radio_dev;
	struct video_device *vbi_dev;

	/* infrared remote */
	int has_remote;
	struct bttv_ir *remote;

	/* locking */
	spinlock_t s_lock;
	struct mutex lock;
	int resources;
	struct mutex reslock;
#ifdef VIDIOC_G_PRIORITY
	struct v4l2_prio_state prio;
#endif

	/* video state */
	unsigned int input;
	unsigned int audio;
	unsigned int mute;
	unsigned long freq;
	int tvnorm,hue,contrast,bright,saturation;
	struct v4l2_framebuffer fbuf;
	unsigned int field_count;

	/* various options */
	int opt_combfilter;
	int opt_lumafilter;
	int opt_automute;
	int opt_chroma_agc;
	int opt_adc_crush;
	int opt_vcr_hack;
	int opt_whitecrush_upper;
	int opt_whitecrush_lower;
	int opt_uv_ratio;
	int opt_full_luma_range;
	int opt_coring;

	/* radio data/state */
	int has_radio;
	int radio_user;

	/* miro/pinnacle + Aimslab VHX
	   philips matchbox (tea5757 radio tuner) support */
	int has_matchbox;
	int mbox_we;
	int mbox_data;
	int mbox_clk;
	int mbox_most;
	int mbox_mask;

	/* ISA stuff (Terratec Active Radio Upgrade) */
	int mbox_ior;
	int mbox_iow;
	int mbox_csel;

	/* risc memory management data
	   - must aquire s_lock before changing these
	   - only the irq handler is supported to touch top + bottom + vcurr */
	struct btcx_riscmem     main;
	struct bttv_buffer      *screen;    /* overlay             */
	struct list_head        capture;    /* video capture queue */
	struct list_head        vcapture;   /* vbi capture queue   */
	struct bttv_buffer_set  curr;       /* active buffers      */
	struct bttv_buffer      *cvbi;      /* active vbi buffer   */
	int                     loop_irq;
	int                     new_input;

	unsigned long cap_ctl;
	unsigned long dma_on;
	struct timer_list timeout;
	struct bttv_suspend_state state;

	/* stats */
	unsigned int errors;
	unsigned int framedrop;
	unsigned int irq_total;
	unsigned int irq_me;

	unsigned int users;
	struct bttv_fh init;
};

/* our devices */
#define BTTV_MAX 16
extern unsigned int bttv_num;
extern struct bttv bttvs[BTTV_MAX];

/* private ioctls */
#define BTTV_VERSION            _IOR('v' , BASE_VIDIOCPRIVATE+6, int)
#define BTTV_VBISIZE            _IOR('v' , BASE_VIDIOCPRIVATE+8, int)

#endif

#define btwrite(dat,adr)    writel((dat), btv->bt848_mmio+(adr))
#define btread(adr)         readl(btv->bt848_mmio+(adr))

#define btand(dat,adr)      btwrite((dat) & btread(adr), adr)
#define btor(dat,adr)       btwrite((dat) | btread(adr), adr)
#define btaor(dat,mask,adr) btwrite((dat) | ((mask) & btread(adr)), adr)

#endif /* _BTTVP_H_ */

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
