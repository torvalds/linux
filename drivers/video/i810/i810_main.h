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

static int  __devinit i810fb_init_pci (struct pci_dev *dev, 
				       const struct pci_device_id *entry);
static void __exit i810fb_remove_pci(struct pci_dev *dev);
static int i810fb_resume(struct pci_dev *dev);
static int i810fb_suspend(struct pci_dev *dev, pm_message_t state);

/*
 * voffset - framebuffer offset in MiB from aperture start address.  In order for
 * the driver to work with X, we must try to use memory holes left untouched by X. The 
 * following table lists where X's different surfaces start at.  
 * 
 * ---------------------------------------------
 * :                :  64 MiB     : 32 MiB      :
 * ----------------------------------------------
 * : FrontBuffer    :   0         :  0          :
 * : DepthBuffer    :   48        :  16         :
 * : BackBuffer     :   56        :  24         :
 * ----------------------------------------------
 *
 * So for chipsets with 64 MiB Aperture sizes, 32 MiB for v_offset is okay, allowing up to
 * 15 + 1 MiB of Framebuffer memory.  For 32 MiB Aperture sizes, a v_offset of 8 MiB should
 * work, allowing 7 + 1 MiB of Framebuffer memory.
 * Note, the size of the hole may change depending on how much memory you allocate to X,
 * and how the memory is split up between these surfaces.  
 *
 * Note: Anytime the DepthBuffer or FrontBuffer is overlapped, X would still run but with
 * DRI disabled.  But if the Frontbuffer is overlapped, X will fail to load.
 * 
 * Experiment with v_offset to find out which works best for you.
 */
static u32 v_offset_default __initdata; /* For 32 MiB Aper size, 8 should be the default */
static u32 voffset          __initdata = 0;

static int i810fb_cursor(struct fb_info *info, struct fb_cursor *cursor);

/* Chipset Specific Functions */
static int i810fb_set_par    (struct fb_info *info);
static int i810fb_getcolreg  (u8 regno, u8 *red, u8 *green, u8 *blue,
			      u8 *transp, struct fb_info *info);
static int i810fb_setcolreg  (unsigned regno, unsigned red, unsigned green, unsigned blue,
			      unsigned transp, struct fb_info *info);
static int i810fb_pan_display(struct fb_var_screeninfo *var, struct fb_info *info);
static int i810fb_blank      (int blank_mode, struct fb_info *info);

/* Initialization */
static void i810fb_release_resource       (struct fb_info *info, struct i810fb_par *par);
extern int __init agp_intel_init(void);


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
inline void flush_cache(void)
{
	asm volatile ("wbinvd":::"memory");
}
#else
#define flush_cache() do { } while(0)
#endif 

#ifdef CONFIG_MTRR
#define KERNEL_HAS_MTRR 1
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
#define KERNEL_HAS_MTRR 0
#define set_mtrr(x) printk("set_mtrr: MTRR is disabled in the kernel\n")

#define unset_mtrr(x) do { } while (0)
#endif /* CONFIG_MTRR */

#ifdef CONFIG_FB_I810_GTF
#define IS_DVT (0)
#else
#define IS_DVT (1)
#endif

#endif /* __I810_MAIN_H__ */
