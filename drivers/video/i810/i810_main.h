/*-*- linux-c -*-
 *  linux/drivers/video/i810fb_main.h -- Intel 810 frame buffer device 
 *                                       main header file
 *
 *      Copyright (C) 2001 Antonino Daplas<adaplas@pol.net>
 *      All Rights Reserved      
 *
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#ifndef __I810_MAIN_H__
#define __I810_MAIN_H__

/* Video Timings */
extern void round_off_xres         (u32 *xres);
extern void round_off_yres         (u32 *xres, u32 *yres);
extern u32 i810_get_watermark      (const struct fb_var_screeninfo *var,
			            struct i810fb_par *par);
extern void i810fb_encode_registers(const struct fb_var_screeninfo *var,
				    struct i810fb_par *par, u32 xres, u32 yres);
extern void i810fb_fill_var_timings(struct fb_var_screeninfo *var);
				    
/* Accelerated Functions */
extern void i810fb_fillrect (struct fb_info *p, 
			     const struct fb_fillrect *rect);
extern void i810fb_copyarea (struct fb_info *p, 
			     const struct fb_copyarea *region);
extern void i810fb_imageblit(struct fb_info *p, const struct fb_image *image);
extern int  i810fb_sync     (struct fb_info *p);

extern void i810fb_init_ringbuffer(struct fb_info *info);
extern void i810fb_load_front     (u32 offset, struct fb_info *info);

#ifdef CONFIG_FB_I810_I2C
/* I2C */
extern int i810_probe_i2c_connector(struct fb_info *info, u8 **out_edid,
				    int conn);
extern void i810_create_i2c_busses(struct i810fb_par *par);
extern void i810_delete_i2c_busses(struct i810fb_par *par);
#else
static inline int i810_probe_i2c_connector(struct fb_info *info, u8 **out_edid,
				    int conn)
{
	return 1;
}
static inline void i810_create_i2c_busses(struct i810fb_par *par) { }
static inline void i810_delete_i2c_busses(struct i810fb_par *par) { }
#endif

/* Conditionals */
#ifdef CONFIG_X86
static inline void flush_cache(void)
{
	asm volatile ("wbinvd":::"memory");
}
#else
#define flush_cache() do { } while(0)
#endif 

#ifdef CONFIG_MTRR

#include <asm/mtrr.h>

static inline void __devinit set_mtrr(struct i810fb_par *par)
{
	par->mtrr_reg = mtrr_add((u32) par->aperture.physical, 
		 par->aperture.size, MTRR_TYPE_WRCOMB, 1);
	if (par->mtrr_reg < 0) {
		printk(KERN_ERR "set_mtrr: unable to set MTRR\n");
		return;
	}
	par->dev_flags |= HAS_MTRR;
}
static inline void unset_mtrr(struct i810fb_par *par)
{
  	if (par->dev_flags & HAS_MTRR) 
  		mtrr_del(par->mtrr_reg, (u32) par->aperture.physical, 
			 par->aperture.size); 
}
#else
#define set_mtrr(x) printk("set_mtrr: MTRR is disabled in the kernel\n")

#define unset_mtrr(x) do { } while (0)
#endif /* CONFIG_MTRR */

#ifdef CONFIG_FB_I810_GTF
#define IS_DVT (0)
#else
#define IS_DVT (1)
#endif

#endif /* __I810_MAIN_H__ */
