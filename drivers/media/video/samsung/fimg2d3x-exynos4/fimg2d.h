/* drivers/media/video/samsung/fimg2d3x/fimg2d_3x.h
 *
 * Copyright  2010 Samsung Electronics Co, Ltd. All Rights Reserved.
 *		      http://www.samsungsemi.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
 
#ifndef __SEC_FIMG2D_H_
#define __SEC_FIMG2D_H_

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/sched.h>

#define G2D_SFR_SIZE    0x1000

#define TRUE            (1)
#define FALSE           (0)

#define G2D_MINOR       240

#define G2D_IOCTL_MAGIC 'G'

#define G2D_BLIT                        _IO(G2D_IOCTL_MAGIC,0)
#define G2D_GET_VERSION                 _IO(G2D_IOCTL_MAGIC,1)
#define G2D_GET_MEMORY                  _IOR(G2D_IOCTL_MAGIC,2, unsigned int)
#define G2D_GET_MEMORY_SIZE             _IOR(G2D_IOCTL_MAGIC,3, unsigned int)
#define G2D_DMA_CACHE_CLEAN	        _IOWR(G2D_IOCTL_MAGIC,4, struct g2d_dma_info)
#define G2D_DMA_CACHE_FLUSH	        _IOWR(G2D_IOCTL_MAGIC,5, struct g2d_dma_info)
#define G2D_SYNC                    	_IO(G2D_IOCTL_MAGIC,6)
#define G2D_RESET                    	_IO(G2D_IOCTL_MAGIC, 7)

#define G2D_TIMEOUT             (1000)

#define G2D_MAX_WIDTH   	(2048)
#define G2D_MAX_HEIGHT  	(2048)

#define G2D_ALPHA_VALUE_MAX 	(255)

#define G2D_POLLING         	(1<<0)
#define G2D_INTERRUPT       	(0<<0)
#define G2D_CACHE_OP      	(1<<1)
#define G2D_NONE_INVALIDATE 	(0<<1)
#define G2D_HYBRID_MODE 	(1<<2)

#define G2D_PT_NOTVALID		(0)
#define G2D_PT_CACHED		(1)
#define G2D_PT_UNCACHED		(2)

#define GET_FRAME_SIZE(rect)    ((rect.full_w) * (rect.full_h) * (rect.bytes_per_pixel))
#define GET_RECT_SIZE(rect)     ((rect.full_w) * (rect.h) * (rect.bytes_per_pixel))
#define GET_REAL_SIZE(rect)     ((rect.full_w) * (rect.h) * (rect.bytes_per_pixel))
#define GET_STRIDE(rect)		((rect.full_w) * (rect.bytes_per_pixel))
#define GET_SPARE_BYTES(rect)	((rect.full_w - rect.w) * rect.bytes_per_pixel)
#define GET_START_ADDR(rect)    (rect.addr + ((rect.y * rect.full_w) * rect.bytes_per_pixel))
#define GET_REAL_START_ADDR(rect)    GET_START_ADDR(rect) + (rect.x * rect.bytes_per_pixel)
#define GET_REAL_END_ADDR(rect)    GET_START_ADDR(rect) + GET_RECT_SIZE(rect) - ((rect.full_w - (rect.x + rect.w)) * rect.bytes_per_pixel)

#define GET_RECT_SIZE_C(rect, clip)     ((rect.full_w) * (clip.b - clip.t) * (rect.bytes_per_pixel))
#define GET_START_ADDR_C(rect, clip)    (rect.addr + ((clip.t * rect.full_w) * rect.bytes_per_pixel))
#define GET_REAL_START_ADDR_C(rect, clip)    GET_START_ADDR_C(rect, clip) + (clip.l * rect.bytes_per_pixel)
#define GET_REAL_END_ADDR_C(rect, clip)    GET_START_ADDR_C(rect, clip) + GET_RECT_SIZE_C(rect, clip) - ((rect.full_w - clip.r) * rect.bytes_per_pixel)

#define GET_USEC(before, after) ((after.tv_sec - before.tv_sec) * 1000000 + (after.tv_usec - before.tv_usec))

typedef enum {
	G2D_ROT_0 = 0,
	G2D_ROT_90,
	G2D_ROT_180,
	G2D_ROT_270,
	G2D_ROT_X_FLIP,
	G2D_ROT_Y_FLIP
} G2D_ROT_DEG;

typedef enum {
	G2D_ALPHA_BLENDING_MIN    = 0,   // wholly transparent
	G2D_ALPHA_BLENDING_MAX    = 255, // 255
	G2D_ALPHA_BLENDING_OPAQUE = 256, // opaque
} G2D_ALPHA_BLENDING_MODE;
    
typedef enum {
	G2D_COLORKEY_NONE = 0,
	G2D_COLORKEY_SRC_ON,
	G2D_COLORKEY_DST_ON,
	G2D_COLORKEY_SRC_DST_ON,
}G2D_COLORKEY_MODE;

typedef enum {
	G2D_BLUE_SCREEN_NONE = 0,
	G2D_BLUE_SCREEN_TRANSPARENT,
	G2D_BLUE_SCREEN_WITH_COLOR,
}G2D_BLUE_SCREEN_MODE;

typedef enum {
	G2D_ROP_SRC = 0,
	G2D_ROP_DST,
	G2D_ROP_SRC_AND_DST,
	G2D_ROP_SRC_OR_DST,
	G2D_ROP_3RD_OPRND,
	G2D_ROP_SRC_AND_3RD_OPRND,
	G2D_ROP_SRC_OR_3RD_OPRND,
	G2D_ROP_SRC_XOR_3RD_OPRND,
	G2D_ROP_DST_OR_3RD,
}G2D_ROP_TYPE;

typedef enum {
	G2D_THIRD_OP_NONE = 0,
	G2D_THIRD_OP_PATTERN,
	G2D_THIRD_OP_FG,
	G2D_THIRD_OP_BG
}G2D_THIRD_OP_MODE;

typedef enum {
	G2D_BLACK = 0,
	G2D_RED,
	G2D_GREEN,
	G2D_BLUE,
	G2D_WHITE, 
	G2D_YELLOW,
	G2D_CYAN,
	G2D_MAGENTA
}G2D_COLOR;

typedef enum {
	G2D_RGB_565 = ((0<<4)|2),

	G2D_ABGR_8888 = ((2<<4)|1),
	G2D_BGRA_8888 = ((3<<4)|1),
	G2D_ARGB_8888 = ((0<<4)|1),
	G2D_RGBA_8888 = ((1<<4)|1),

	G2D_XBGR_8888 = ((2<<4)|0),
	G2D_BGRX_8888 = ((3<<4)|0),
	G2D_XRGB_8888 = ((0<<4)|0),
	G2D_RGBX_8888 = ((1<<4)|0),

	G2D_ABGR_1555 = ((2<<4)|4),
	G2D_BGRA_5551 = ((3<<4)|4),
	G2D_ARGB_1555 = ((0<<4)|4),
	G2D_RGBA_5551 = ((1<<4)|4),

	G2D_XBGR_1555 = ((2<<4)|3),
	G2D_BGRX_5551 = ((3<<4)|3),
	G2D_XRGB_1555 = ((0<<4)|3),
	G2D_RGBX_5551 = ((1<<4)|3),

	G2D_ABGR_4444 = ((2<<4)|6),
	G2D_BGRA_4444 = ((3<<4)|6),
	G2D_ARGB_4444 = ((0<<4)|6),
	G2D_RGBA_4444 = ((1<<4)|6),

	G2D_XBGR_4444 = ((2<<4)|5),
	G2D_BGRX_4444 = ((3<<4)|5),
	G2D_XRGB_4444 = ((0<<4)|5),
	G2D_RGBX_4444 = ((1<<4)|5),
	
	G2D_PACKED_BGR_888 = ((2<<4)|7),
	G2D_PACKED_RGB_888 = ((0<<4)|7),

	G2D_MAX_COLOR_SPACE
}G2D_COLOR_SPACE;

typedef enum {
        G2D_Clear_Mode,    //!< [0, 0]
        G2D_Src_Mode,      //!< [Sa, Sc]
        G2D_Dst_Mode,      //!< [Da, Dc]
        G2D_SrcOver_Mode,  //!< [Sa + Da - Sa*Da, Rc = Sc + (1 - Sa)*Dc]
        G2D_DstOver_Mode,  //!< [Sa + Da - Sa*Da, Rc = Dc + (1 - Da)*Sc]
        G2D_SrcIn_Mode,    //!< [Sa * Da, Sc * Da]
        G2D_DstIn_Mode,    //!< [Sa * Da, Sa * Dc]
        G2D_SrcOut_Mode,   //!< [Sa * (1 - Da), Sc * (1 - Da)]
        G2D_DstOut_Mode,   //!< [Da * (1 - Sa), Dc * (1 - Sa)]
        G2D_SrcATop_Mode,  //!< [Da, Sc * Da + (1 - Sa) * Dc]
        G2D_DstATop_Mode,  //!< [Sa, Sa * Dc + Sc * (1 - Da)]
        G2D_Xor_Mode,      //!< [Sa + Da - 2 * Sa * Da, Sc * (1 - Da) + (1 - Sa) * Dc]

        // these modes are defined in the SVG Compositing standard
        // http://www.w3.org/TR/2009/WD-SVGCompositing-20090430/
        G2D_Plus_Mode,
        G2D_Multiply_Mode,
        G2D_Screen_Mode,
        G2D_Overlay_Mode,
        G2D_Darken_Mode,
        G2D_Lighten_Mode,
        G2D_ColorDodge_Mode,
        G2D_ColorBurn_Mode,
        G2D_HardLight_Mode,
        G2D_SoftLight_Mode,
        G2D_Difference_Mode,
        G2D_Exclusion_Mode,

        kLastMode = G2D_Exclusion_Mode
}G2D_PORTTERDUFF_MODE;

typedef enum {
       G2D_MEMORY_KERNEL,
       G2D_MEMORY_USER
}G2D_MEMORY_TYPE;

typedef struct {
        int    x;
        int    y;
        unsigned int    w;
        unsigned int    h;
        unsigned int    full_w;
        unsigned int    full_h;
        int             color_format;
        unsigned int    bytes_per_pixel;
        unsigned char 	* addr;
} g2d_rect;

typedef struct {
        unsigned int    t;
        unsigned int    b;
        unsigned int    l;
        unsigned int    r;
} g2d_clip;

typedef struct {
        unsigned int    rotate_val;
        unsigned int    alpha_val;

        unsigned int    blue_screen_mode;     //true : enable, false : disable
        unsigned int    color_key_val;        //screen color value
        unsigned int    color_switch_val;     //one color

        unsigned int    src_color;            // when set one color on SRC
        		
        unsigned int    third_op_mode;
        unsigned int    rop_mode;
        unsigned int    mask_mode;
        unsigned int    render_mode;    
        unsigned int    potterduff_mode;
        unsigned int    memory_type;
} g2d_flag;

typedef struct {
        g2d_rect src_rect;
        g2d_rect dst_rect;
        g2d_clip clip;   
        g2d_flag flag;
} g2d_params;

/* for reserved memory */
struct g2d_reserved_mem {
	/* buffer base */
	unsigned int	base;
	/* buffer size */
	unsigned int	size;
};


struct g2d_dma_info {
	unsigned long addr;
	unsigned int  size;
};

struct g2d_platdata {
	int hw_ver;
	const char *parent_clkname;
	const char *clkname;
	const char *gate_clkname;
	unsigned long clkrate;
};

struct g2d_timer {
	int			cnt;
	struct timeval		start_marker;
	struct timeval		cur_marker;
};

struct g2d_global {
	int             	irq_num;
	struct resource 	* mem;
	void   __iomem  	* base;
	struct clk 		* clock;
	atomic_t		clk_enable_flag;
	wait_queue_head_t 	waitq;
	atomic_t        	in_use;
	atomic_t		num_of_object;
	struct mutex		lock;
	struct device		* dev;
	atomic_t 		ready_to_run;
	int 			src_attribute;
	int 			dst_attribute;

	struct g2d_reserved_mem	reserved_mem;		/* for reserved memory */
	atomic_t		is_mmu_faulted;
	unsigned int		faulted_addr;
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend	early_suspend;
#endif	
	int                     irq_handled;
};


/****** debug message API *****/
enum fimg2d_log {
	FIMG2D_LOG_DEBUG	= 0x1000,
	FIMG2D_LOG_INFO		= 0x0100,
	FIMG2D_LOG_WARN		= 0x0010,
	FIMG2D_LOG_ERR		= 0x0001,
};

/* debug macro */
#define FIMG2D_LOG_DEFAULT	(FIMG2D_LOG_WARN | FIMG2D_LOG_ERR)

#define FIMG2D_DEBUG(fmt, ...)						\
	do {								\
		if (FIMG2D_LOG_DEFAULT & FIMG2D_LOG_DEBUG)			\
			printk(KERN_DEBUG "%s: "			\
				fmt, __func__, ##__VA_ARGS__);		\
	} while (0)

#define FIMG2D_INFO(fmt, ...)						\
	do {								\
		if (FIMG2D_LOG_DEFAULT & FIMG2D_LOG_INFO)			\
			printk(KERN_INFO "%s: "				\
				fmt, __func__, ##__VA_ARGS__);		\
	} while (0)

#define FIMG2D_WARN(fmt, ...)						\
	do {								\
		if (FIMG2D_LOG_DEFAULT & FIMG2D_LOG_WARN)			\
			printk(KERN_WARNING "%s: "			\
				fmt, __func__, ##__VA_ARGS__);		\
	} while (0)


#define FIMG2D_ERROR(fmt, ...)						\
	do {								\
		if (FIMG2D_LOG_DEFAULT & FIMG2D_LOG_ERR)			\
			printk(KERN_ERR "%s: "				\
				fmt, __func__, ##__VA_ARGS__);		\
	} while (0)


#define fimg2d_dbg(fmt, ...)		FIMG2D_DEBUG(fmt, ##__VA_ARGS__)
#define fimg2d_info(fmt, ...)		FIMG2D_INFO(fmt, ##__VA_ARGS__)
#define fimg2d_warn(fmt, ...)		FIMG2D_WARN(fmt, ##__VA_ARGS__)
#define fimg2d_err(fmt, ...)		FIMG2D_ERROR(fmt, ##__VA_ARGS__)


/**** function declearation***************************/
int g2d_check_params(g2d_params *params);
void g2d_start_bitblt(struct g2d_global *g2d_dev, g2d_params *params);
void g2d_check_fifo_state_wait(struct g2d_global *g2d_dev);
u32  g2d_set_src_img(struct g2d_global *g2d_dev, g2d_rect * rect, g2d_flag * flag);
u32  g2d_set_dst_img(struct g2d_global *g2d_dev, g2d_rect * rect);
u32  g2d_set_pattern(struct g2d_global *g2d_dev, g2d_rect * rect, g2d_flag * flag);
u32  g2d_set_clip_win(struct g2d_global *g2d_dev, g2d_clip * rect);
u32  g2d_set_rotation(struct g2d_global *g2d_dev, g2d_flag * flag);
u32  g2d_set_color_key(struct g2d_global *g2d_dev, g2d_flag * flag);
u32  g2d_set_alpha(struct g2d_global *g2d_dev, g2d_flag * flag);
void g2d_set_bitblt_cmd(struct g2d_global *g2d_dev, g2d_rect * src_rect, g2d_rect * dst_rect, g2d_clip * clip, u32 blt_cmd);
void g2d_reset(struct g2d_global *g2d_dev);
void g2d_disable_int(struct g2d_global *g2d_dev);
void g2d_set_int_finish(struct g2d_global *g2d_dev);

/* fimg2d_cache */
void g2d_clip_for_src(g2d_rect *src_rect, g2d_rect *dst_rect, g2d_clip *clip, g2d_clip *src_clip);
void g2d_mem_inner_cache(g2d_params *params);
void g2d_mem_outer_cache(struct g2d_global *g2d_dev, g2d_params *params, int *need_dst_clean);
void g2d_mem_cache_oneshot(void *src_addr,  void *dst_addr, unsigned long src_size, unsigned long dst_size);
u32 g2d_mem_cache_op(unsigned int cmd, void * addr, unsigned int size);
void g2d_mem_outer_cache_flush(void *start_addr, unsigned long size);                                      
void g2d_mem_outer_cache_clean(const void *start_addr, unsigned long size);
void g2d_mem_outer_cache_inv(g2d_params *params);
u32 g2d_check_pagetable(void * vaddr, unsigned int size, unsigned long pgd);
void g2d_pagetable_clean(const void *start_addr, unsigned long size, unsigned long pgd);
int g2d_check_need_dst_cache_clean(g2d_params * params);

#ifdef CONFIG_HAS_EARLYSUSPEND
void g2d_early_suspend(struct early_suspend *h);
void g2d_late_resume(struct early_suspend *h);
#endif

/* fimg2d_core */
int g2d_clk_enable(struct g2d_global *g2d_dev);
int g2d_clk_disable(struct g2d_global *g2d_dev);
void g2d_sysmmu_on(struct g2d_global *g2d_dev);
void g2d_sysmmu_off(struct g2d_global *g2d_dev);
void g2d_sysmmu_set_pgd(u32 pgd);
void g2d_fail_debug(g2d_params *params);
int g2d_init_regs(struct g2d_global *g2d_dev, g2d_params *params);
int g2d_do_blit(struct g2d_global *g2d_dev, g2d_params *params);
int g2d_wait_for_finish(struct g2d_global *g2d_dev, g2d_params *params);
int g2d_init_mem(struct device *dev, unsigned int *base, unsigned int *size);

#endif /*__SEC_FIMG2D_H_*/
