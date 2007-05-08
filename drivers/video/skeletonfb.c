/*
 * linux/drivers/video/skeletonfb.c -- Skeleton for a frame buffer device
 *
 *  Modified to new api Jan 2001 by James Simmons (jsimmons@transvirtual.com)
 *
 *  Created 28 Dec 1997 by Geert Uytterhoeven
 *
 *
 *  I have started rewriting this driver as a example of the upcoming new API
 *  The primary goal is to remove the console code from fbdev and place it
 *  into fbcon.c. This reduces the code and makes writing a new fbdev driver
 *  easy since the author doesn't need to worry about console internals. It
 *  also allows the ability to run fbdev without a console/tty system on top 
 *  of it. 
 *
 *  First the roles of struct fb_info and struct display have changed. Struct
 *  display will go away. The way the the new framebuffer console code will
 *  work is that it will act to translate data about the tty/console in 
 *  struct vc_data to data in a device independent way in struct fb_info. Then
 *  various functions in struct fb_ops will be called to store the device 
 *  dependent state in the par field in struct fb_info and to change the 
 *  hardware to that state. This allows a very clean separation of the fbdev
 *  layer from the console layer. It also allows one to use fbdev on its own
 *  which is a bounus for embedded devices. The reason this approach works is  
 *  for each framebuffer device when used as a tty/console device is allocated
 *  a set of virtual terminals to it. Only one virtual terminal can be active 
 *  per framebuffer device. We already have all the data we need in struct 
 *  vc_data so why store a bunch of colormaps and other fbdev specific data
 *  per virtual terminal. 
 *
 *  As you can see doing this makes the con parameter pretty much useless
 *  for struct fb_ops functions, as it should be. Also having struct  
 *  fb_var_screeninfo and other data in fb_info pretty much eliminates the 
 *  need for get_fix and get_var. Once all drivers use the fix, var, and cmap
 *  fbcon can be written around these fields. This will also eliminate the
 *  need to regenerate struct fb_var_screeninfo, struct fb_fix_screeninfo
 *  struct fb_cmap every time get_var, get_fix, get_cmap functions are called
 *  as many drivers do now. 
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pci.h>

    /*
     *  This is just simple sample code.
     *
     *  No warranty that it actually compiles.
     *  Even less warranty that it actually works :-)
     */

/*
 * Driver data
 */
static char *mode_option __devinitdata;

/*
 *  If your driver supports multiple boards, you should make the  
 *  below data types arrays, or allocate them dynamically (using kmalloc()). 
 */ 

/* 
 * This structure defines the hardware state of the graphics card. Normally
 * you place this in a header file in linux/include/video. This file usually
 * also includes register information. That allows other driver subsystems
 * and userland applications the ability to use the same header file to 
 * avoid duplicate work and easy porting of software. 
 */
struct xxx_par;

/*
 * Here we define the default structs fb_fix_screeninfo and fb_var_screeninfo
 * if we don't use modedb. If we do use modedb see xxxfb_init how to use it
 * to get a fb_var_screeninfo. Otherwise define a default var as well. 
 */
static struct fb_fix_screeninfo xxxfb_fix __initdata = {
	.id =		"FB's name", 
	.type =		FB_TYPE_PACKED_PIXELS,
	.visual =	FB_VISUAL_PSEUDOCOLOR,
	.xpanstep =	1,
	.ypanstep =	1,
	.ywrapstep =	1, 
	.accel =	FB_ACCEL_NONE,
};

    /*
     * 	Modern graphical hardware not only supports pipelines but some 
     *  also support multiple monitors where each display can have its  
     *  its own unique data. In this case each display could be  
     *  represented by a separate framebuffer device thus a separate 
     *  struct fb_info. Now the struct xxx_par represents the graphics
     *  hardware state thus only one exist per card. In this case the 
     *  struct xxx_par for each graphics card would be shared between 
     *  every struct fb_info that represents a framebuffer on that card. 
     *  This allows when one display changes it video resolution (info->var) 
     *  the other displays know instantly. Each display can always be
     *  aware of the entire hardware state that affects it because they share
     *  the same xxx_par struct. The other side of the coin is multiple
     *  graphics cards that pass data around until it is finally displayed
     *  on one monitor. Such examples are the voodoo 1 cards and high end
     *  NUMA graphics servers. For this case we have a bunch of pars, each
     *  one that represents a graphics state, that belong to one struct 
     *  fb_info. Their you would want to have *par point to a array of device
     *  states and have each struct fb_ops function deal with all those 
     *  states. I hope this covers every possible hardware design. If not
     *  feel free to send your ideas at jsimmons@users.sf.net 
     */

    /*
     *  If your driver supports multiple boards or it supports multiple 
     *  framebuffers, you should make these arrays, or allocate them 
     *  dynamically using framebuffer_alloc() and free them with
     *  framebuffer_release().
     */ 
static struct fb_info info;

    /* 
     * Each one represents the state of the hardware. Most hardware have
     * just one hardware state. These here represent the default state(s). 
     */
static struct xxx_par __initdata current_par;

int xxxfb_init(void);
int xxxfb_setup(char*);

/**
 *	xxxfb_open - Optional function. Called when the framebuffer is
 *		     first accessed.
 *	@info: frame buffer structure that represents a single frame buffer
 *	@user: tell us if the userland (value=1) or the console is accessing
 *	       the framebuffer. 
 *
 *	This function is the first function called in the framebuffer api.
 *	Usually you don't need to provide this function. The case where it 
 *	is used is to change from a text mode hardware state to a graphics
 * 	mode state. 
 *
 *	Returns negative errno on error, or zero on success.
 */
static int xxxfb_open(const struct fb_info *info, int user)
{
    return 0;
}

/**
 *	xxxfb_release - Optional function. Called when the framebuffer 
 *			device is closed. 
 *	@info: frame buffer structure that represents a single frame buffer
 *	@user: tell us if the userland (value=1) or the console is accessing
 *	       the framebuffer. 
 *	
 *	Thus function is called when we close /dev/fb or the framebuffer 
 *	console system is released. Usually you don't need this function.
 *	The case where it is usually used is to go from a graphics state
 *	to a text mode state.
 *
 *	Returns negative errno on error, or zero on success.
 */
static int xxxfb_release(const struct fb_info *info, int user)
{
    return 0;
}

/**
 *      xxxfb_check_var - Optional function. Validates a var passed in. 
 *      @var: frame buffer variable screen structure
 *      @info: frame buffer structure that represents a single frame buffer 
 *
 *	Checks to see if the hardware supports the state requested by
 *	var passed in. This function does not alter the hardware state!!! 
 *	This means the data stored in struct fb_info and struct xxx_par do 
 *      not change. This includes the var inside of struct fb_info. 
 *	Do NOT change these. This function can be called on its own if we
 *	intent to only test a mode and not actually set it. The stuff in 
 *	modedb.c is a example of this. If the var passed in is slightly 
 *	off by what the hardware can support then we alter the var PASSED in
 *	to what we can do.
 *
 *      For values that are off, this function must round them _up_ to the
 *      next value that is supported by the hardware.  If the value is
 *      greater than the highest value supported by the hardware, then this
 *      function must return -EINVAL.
 *
 *      Exception to the above rule:  Some drivers have a fixed mode, ie,
 *      the hardware is already set at boot up, and cannot be changed.  In
 *      this case, it is more acceptable that this function just return
 *      a copy of the currently working var (info->var). Better is to not
 *      implement this function, as the upper layer will do the copying
 *      of the current var for you.
 *
 *      Note:  This is the only function where the contents of var can be
 *      freely adjusted after the driver has been registered. If you find
 *      that you have code outside of this function that alters the content
 *      of var, then you are doing something wrong.  Note also that the
 *      contents of info->var must be left untouched at all times after
 *      driver registration.
 *
 *	Returns negative errno on error, or zero on success.
 */
static int xxxfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
    /* ... */
    return 0;	   	
}

/**
 *      xxxfb_set_par - Optional function. Alters the hardware state.
 *      @info: frame buffer structure that represents a single frame buffer
 *
 *	Using the fb_var_screeninfo in fb_info we set the resolution of the
 *	this particular framebuffer. This function alters the par AND the
 *	fb_fix_screeninfo stored in fb_info. It doesn't not alter var in 
 *	fb_info since we are using that data. This means we depend on the
 *	data in var inside fb_info to be supported by the hardware. 
 *
 *      This function is also used to recover/restore the hardware to a
 *      known working state.
 *
 *	xxxfb_check_var is always called before xxxfb_set_par to ensure that
 *      the contents of var is always valid.
 *
 *	Again if you can't change the resolution you don't need this function.
 *
 *      However, even if your hardware does not support mode changing,
 *      a set_par might be needed to at least initialize the hardware to
 *      a known working state, especially if it came back from another
 *      process that also modifies the same hardware, such as X.
 *
 *      If this is the case, a combination such as the following should work:
 *
 *      static int xxxfb_check_var(struct fb_var_screeninfo *var,
 *                                struct fb_info *info)
 *      {
 *              *var = info->var;
 *              return 0;
 *      }
 *
 *      static int xxxfb_set_par(struct fb_info *info)
 *      {
 *              init your hardware here
 *      }
 *
 *	Returns negative errno on error, or zero on success.
 */
static int xxxfb_set_par(struct fb_info *info)
{
    struct xxx_par *par = info->par;
    /* ... */
    return 0;	
}

/**
 *  	xxxfb_setcolreg - Optional function. Sets a color register.
 *      @regno: Which register in the CLUT we are programming 
 *      @red: The red value which can be up to 16 bits wide 
 *	@green: The green value which can be up to 16 bits wide 
 *	@blue:  The blue value which can be up to 16 bits wide.
 *	@transp: If supported, the alpha value which can be up to 16 bits wide.
 *      @info: frame buffer info structure
 * 
 *  	Set a single color register. The values supplied have a 16 bit
 *  	magnitude which needs to be scaled in this function for the hardware. 
 *	Things to take into consideration are how many color registers, if
 *	any, are supported with the current color visual. With truecolor mode
 *	no color palettes are supported. Here a pseudo palette is created
 *	which we store the value in pseudo_palette in struct fb_info. For
 *	pseudocolor mode we have a limited color palette. To deal with this
 *	we can program what color is displayed for a particular pixel value.
 *	DirectColor is similar in that we can program each color field. If
 *	we have a static colormap we don't need to implement this function. 
 * 
 *	Returns negative errno on error, or zero on success.
 */
static int xxxfb_setcolreg(unsigned regno, unsigned red, unsigned green,
			   unsigned blue, unsigned transp,
			   struct fb_info *info)
{
    if (regno >= 256)  /* no. of hw registers */
       return -EINVAL;
    /*
     * Program hardware... do anything you want with transp
     */

    /* grayscale works only partially under directcolor */
    if (info->var.grayscale) {
       /* grayscale = 0.30*R + 0.59*G + 0.11*B */
       red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;
    }

    /* Directcolor:
     *   var->{color}.offset contains start of bitfield
     *   var->{color}.length contains length of bitfield
     *   {hardwarespecific} contains width of DAC
     *   pseudo_palette[X] is programmed to (X << red.offset) |
     *                                      (X << green.offset) |
     *                                      (X << blue.offset)
     *   RAMDAC[X] is programmed to (red, green, blue)
     *   color depth = SUM(var->{color}.length)
     *
     * Pseudocolor:
     *    var->{color}.offset is 0
     *    var->{color}.length contains width of DAC or the number of unique
     *                        colors available (color depth)
     *    pseudo_palette is not used
     *    RAMDAC[X] is programmed to (red, green, blue)
     *    color depth = var->{color}.length
     *
     * Static pseudocolor:
     *    same as Pseudocolor, but the RAMDAC is not programmed (read-only)
     *
     * Mono01/Mono10:
     *    Has only 2 values, black on white or white on black (fg on bg),
     *    var->{color}.offset is 0
     *    white = (1 << var->{color}.length) - 1, black = 0
     *    pseudo_palette is not used
     *    RAMDAC does not exist
     *    color depth is always 2
     *
     * Truecolor:
     *    does not use RAMDAC (usually has 3 of them).
     *    var->{color}.offset contains start of bitfield
     *    var->{color}.length contains length of bitfield
     *    pseudo_palette is programmed to (red << red.offset) |
     *                                    (green << green.offset) |
     *                                    (blue << blue.offset) |
     *                                    (transp << transp.offset)
     *    RAMDAC does not exist
     *    color depth = SUM(var->{color}.length})
     *
     *  The color depth is used by fbcon for choosing the logo and also
     *  for color palette transformation if color depth < 4
     *
     *  As can be seen from the above, the field bits_per_pixel is _NOT_
     *  a criteria for describing the color visual.
     *
     *  A common mistake is assuming that bits_per_pixel <= 8 is pseudocolor,
     *  and higher than that, true/directcolor.  This is incorrect, one needs
     *  to look at the fix->visual.
     *
     *  Another common mistake is using bits_per_pixel to calculate the color
     *  depth.  The bits_per_pixel field does not directly translate to color
     *  depth. You have to compute for the color depth (using the color
     *  bitfields) and fix->visual as seen above.
     */

    /*
     * This is the point where the color is converted to something that
     * is acceptable by the hardware.
     */
#define CNVT_TOHW(val,width) ((((val)<<(width))+0x7FFF-(val))>>16)
    red = CNVT_TOHW(red, info->var.red.length);
    green = CNVT_TOHW(green, info->var.green.length);
    blue = CNVT_TOHW(blue, info->var.blue.length);
    transp = CNVT_TOHW(transp, info->var.transp.length);
#undef CNVT_TOHW
    /*
     * This is the point where the function feeds the color to the hardware
     * palette after converting the colors to something acceptable by
     * the hardware. Note, only FB_VISUAL_DIRECTCOLOR and
     * FB_VISUAL_PSEUDOCOLOR visuals need to write to the hardware palette.
     * If you have code that writes to the hardware CLUT, and it's not
     * any of the above visuals, then you are doing something wrong.
     */
    if (info->fix.visual == FB_VISUAL_DIRECTCOLOR ||
	info->fix.visual == FB_VISUAL_TRUECOLOR)
	    write_{red|green|blue|transp}_to_clut();

    /* This is the point were you need to fill up the contents of
     * info->pseudo_palette. This structure is used _only_ by fbcon, thus
     * it only contains 16 entries to match the number of colors supported
     * by the console. The pseudo_palette is used only if the visual is
     * in directcolor or truecolor mode.  With other visuals, the
     * pseudo_palette is not used. (This might change in the future.)
     *
     * The contents of the pseudo_palette is in raw pixel format.  Ie, each
     * entry can be written directly to the framebuffer without any conversion.
     * The pseudo_palette is (void *).  However, if using the generic
     * drawing functions (cfb_imageblit, cfb_fillrect), the pseudo_palette
     * must be casted to (u32 *) _regardless_ of the bits per pixel. If the
     * driver is using its own drawing functions, then it can use whatever
     * size it wants.
     */
    if (info->fix.visual == FB_VISUAL_TRUECOLOR ||
	info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
	    u32 v;

	    if (regno >= 16)
		    return -EINVAL;

	    v = (red << info->var.red.offset) |
		    (green << info->var.green.offset) |
		    (blue << info->var.blue.offset) |
		    (transp << info->var.transp.offset);

	    ((u32*)(info->pseudo_palette))[regno] = v;
    }

    /* ... */
    return 0;
}

/**
 *      xxxfb_pan_display - NOT a required function. Pans the display.
 *      @var: frame buffer variable screen structure
 *      @info: frame buffer structure that represents a single frame buffer
 *
 *	Pan (or wrap, depending on the `vmode' field) the display using the
 *  	`xoffset' and `yoffset' fields of the `var' structure.
 *  	If the values don't fit, return -EINVAL.
 *
 *      Returns negative errno on error, or zero on success.
 */
static int xxxfb_pan_display(struct fb_var_screeninfo *var,
			     struct fb_info *info)
{
    /*
     * If your hardware does not support panning, _do_ _not_ implement this
     * function. Creating a dummy function will just confuse user apps.
     */

    /*
     * Note that even if this function is fully functional, a setting of
     * 0 in both xpanstep and ypanstep means that this function will never
     * get called.
     */

    /* ... */
    return 0;
}

/**
 *      xxxfb_blank - NOT a required function. Blanks the display.
 *      @blank_mode: the blank mode we want. 
 *      @info: frame buffer structure that represents a single frame buffer
 *
 *      Blank the screen if blank_mode != FB_BLANK_UNBLANK, else unblank.
 *      Return 0 if blanking succeeded, != 0 if un-/blanking failed due to
 *      e.g. a video mode which doesn't support it.
 *
 *      Implements VESA suspend and powerdown modes on hardware that supports
 *      disabling hsync/vsync:
 *
 *      FB_BLANK_NORMAL = display is blanked, syncs are on.
 *      FB_BLANK_HSYNC_SUSPEND = hsync off
 *      FB_BLANK_VSYNC_SUSPEND = vsync off
 *      FB_BLANK_POWERDOWN =  hsync and vsync off
 *
 *      If implementing this function, at least support FB_BLANK_UNBLANK.
 *      Return !0 for any modes that are unimplemented.
 *
 */
static int xxxfb_blank(int blank_mode, struct fb_info *info)
{
    /* ... */
    return 0;
}

/* ------------ Accelerated Functions --------------------- */

/*
 * We provide our own functions if we have hardware acceleration
 * or non packed pixel format layouts. If we have no hardware 
 * acceleration, we can use a generic unaccelerated function. If using
 * a pack pixel format just use the functions in cfb_*.c. Each file 
 * has one of the three different accel functions we support.
 */

/**
 *      xxxfb_fillrect - REQUIRED function. Can use generic routines if 
 *		 	 non acclerated hardware and packed pixel based.
 *			 Draws a rectangle on the screen.		
 *
 *      @info: frame buffer structure that represents a single frame buffer
 *	@region: The structure representing the rectangular region we 
 *		 wish to draw to.
 *
 *	This drawing operation places/removes a retangle on the screen 
 *	depending on the rastering operation with the value of color which
 *	is in the current color depth format.
 */
void xxxfb_fillrect(struct fb_info *p, const struct fb_fillrect *region)
{
/*	Meaning of struct fb_fillrect
 *
 *	@dx: The x and y corrdinates of the upper left hand corner of the 
 *	@dy: area we want to draw to. 
 *	@width: How wide the rectangle is we want to draw.
 *	@height: How tall the rectangle is we want to draw.
 *	@color:	The color to fill in the rectangle with. 
 *	@rop: The raster operation. We can draw the rectangle with a COPY
 *	      of XOR which provides erasing effect. 
 */
}

/**
 *      xxxfb_copyarea - REQUIRED function. Can use generic routines if
 *                       non acclerated hardware and packed pixel based.
 *                       Copies one area of the screen to another area.
 *
 *      @info: frame buffer structure that represents a single frame buffer
 *      @area: Structure providing the data to copy the framebuffer contents
 *	       from one region to another.
 *
 *      This drawing operation copies a rectangular area from one area of the
 *	screen to another area.
 */
void xxxfb_copyarea(struct fb_info *p, const struct fb_copyarea *area) 
{
/*
 *      @dx: The x and y coordinates of the upper left hand corner of the
 *	@dy: destination area on the screen.
 *      @width: How wide the rectangle is we want to copy.
 *      @height: How tall the rectangle is we want to copy.
 *      @sx: The x and y coordinates of the upper left hand corner of the
 *      @sy: source area on the screen.
 */
}


/**
 *      xxxfb_imageblit - REQUIRED function. Can use generic routines if
 *                        non acclerated hardware and packed pixel based.
 *                        Copies a image from system memory to the screen. 
 *
 *      @info: frame buffer structure that represents a single frame buffer
 *	@image:	structure defining the image.
 *
 *      This drawing operation draws a image on the screen. It can be a 
 *	mono image (needed for font handling) or a color image (needed for
 *	tux). 
 */
void xxxfb_imageblit(struct fb_info *p, const struct fb_image *image) 
{
/*
 *      @dx: The x and y coordinates of the upper left hand corner of the
 *	@dy: destination area to place the image on the screen.
 *      @width: How wide the image is we want to copy.
 *      @height: How tall the image is we want to copy.
 *      @fg_color: For mono bitmap images this is color data for     
 *      @bg_color: the foreground and background of the image to
 *		   write directly to the frmaebuffer.
 *	@depth:	How many bits represent a single pixel for this image.
 *	@data: The actual data used to construct the image on the display.
 *	@cmap: The colormap used for color images.   
 */

/*
 * The generic function, cfb_imageblit, expects that the bitmap scanlines are
 * padded to the next byte.  Most hardware accelerators may require padding to
 * the next u16 or the next u32.  If that is the case, the driver can specify
 * this by setting info->pixmap.scan_align = 2 or 4.  See a more
 * comprehensive description of the pixmap below.
 */
}

/**
 *	xxxfb_cursor - 	OPTIONAL. If your hardware lacks support
 *			for a cursor, leave this field NULL.
 *
 *      @info: frame buffer structure that represents a single frame buffer
 *	@cursor: structure defining the cursor to draw.
 *
 *      This operation is used to set or alter the properities of the
 *	cursor.
 *
 *	Returns negative errno on error, or zero on success.
 */
int xxxfb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
/*
 *      @set: 	Which fields we are altering in struct fb_cursor 
 *	@enable: Disable or enable the cursor 
 *      @rop: 	The bit operation we want to do. 
 *      @mask:  This is the cursor mask bitmap. 
 *      @dest:  A image of the area we are going to display the cursor.
 *		Used internally by the driver.	 
 *      @hot:	The hot spot. 
 *	@image:	The actual data for the cursor image.
 *
 *      NOTES ON FLAGS (cursor->set):
 *
 *      FB_CUR_SETIMAGE - the cursor image has changed (cursor->image.data)
 *      FB_CUR_SETPOS   - the cursor position has changed (cursor->image.dx|dy)
 *      FB_CUR_SETHOT   - the cursor hot spot has changed (cursor->hot.dx|dy)
 *      FB_CUR_SETCMAP  - the cursor colors has changed (cursor->fg_color|bg_color)
 *      FB_CUR_SETSHAPE - the cursor bitmask has changed (cursor->mask)
 *      FB_CUR_SETSIZE  - the cursor size has changed (cursor->width|height)
 *      FB_CUR_SETALL   - everything has changed
 *
 *      NOTES ON ROPs (cursor->rop, Raster Operation)
 *
 *      ROP_XOR         - cursor->image.data XOR cursor->mask
 *      ROP_COPY        - curosr->image.data AND cursor->mask
 *
 *      OTHER NOTES:
 *
 *      - fbcon only supports a 2-color cursor (cursor->image.depth = 1)
 *      - The fb_cursor structure, @cursor, _will_ always contain valid
 *        fields, whether any particular bitfields in cursor->set is set
 *        or not.
 */
}

/**
 *	xxxfb_rotate -  NOT a required function. If your hardware
 *			supports rotation the whole screen then 
 *			you would provide a hook for this. 
 *
 *      @info: frame buffer structure that represents a single frame buffer
 *	@angle: The angle we rotate the screen.   
 *
 *      This operation is used to set or alter the properities of the
 *	cursor.
 */
void xxxfb_rotate(struct fb_info *info, int angle)
{
/* Will be deprecated */
}

/**
 *	xxxfb_poll - NOT a required function. The purpose of this
 *		     function is to provide a way for some process
 *		     to wait until a specific hardware event occurs
 *		     for the framebuffer device.
 * 				 
 *      @info: frame buffer structure that represents a single frame buffer
 *	@wait: poll table where we store process that await a event.     
 */
void xxxfb_poll(struct fb_info *info, poll_table *wait)
{
}

/**
 *	xxxfb_sync - NOT a required function. Normally the accel engine 
 *		     for a graphics card take a specific amount of time.
 *		     Often we have to wait for the accelerator to finish
 *		     its operation before we can write to the framebuffer
 *		     so we can have consistent display output. 
 *
 *      @info: frame buffer structure that represents a single frame buffer
 *
 *      If the driver has implemented its own hardware-based drawing function,
 *      implementing this function is highly recommended.
 */
int xxxfb_sync(struct fb_info *info)
{
	return 0;
}

    /*
     *  Frame buffer operations
     */

static struct fb_ops xxxfb_ops = {
	.owner		= THIS_MODULE,
	.fb_open	= xxxfb_open,
	.fb_read	= xxxfb_read,
	.fb_write	= xxxfb_write,
	.fb_release	= xxxfb_release,
	.fb_check_var	= xxxfb_check_var,
	.fb_set_par	= xxxfb_set_par,
	.fb_setcolreg	= xxxfb_setcolreg,
	.fb_blank	= xxxfb_blank,
	.fb_pan_display	= xxxfb_pan_display,
	.fb_fillrect	= xxxfb_fillrect, 	/* Needed !!! */
	.fb_copyarea	= xxxfb_copyarea,	/* Needed !!! */
	.fb_imageblit	= xxxfb_imageblit,	/* Needed !!! */
	.fb_cursor	= xxxfb_cursor,		/* Optional !!! */
	.fb_rotate	= xxxfb_rotate,
	.fb_poll	= xxxfb_poll,
	.fb_sync	= xxxfb_sync,
	.fb_ioctl	= xxxfb_ioctl,
	.fb_mmap	= xxxfb_mmap,
};

/* ------------------------------------------------------------------------- */

    /*
     *  Initialization
     */

/* static int __init xxfb_probe (struct device *device) -- for platform devs */
static int __devinit xxxfb_probe(struct pci_dev *dev,
			      const_struct pci_device_id *ent)
{
    struct fb_info *info;
    struct xxx_par *par;
    struct device* device = &dev->dev; /* for pci drivers */
    int cmap_len, retval;	
   
    /*
     * Dynamically allocate info and par
     */
    info = framebuffer_alloc(sizeof(struct xxx_par), device);

    if (!info) {
	    /* goto error path */
    }

    par = info->par;

    /* 
     * Here we set the screen_base to the virtual memory address
     * for the framebuffer. Usually we obtain the resource address
     * from the bus layer and then translate it to virtual memory
     * space via ioremap. Consult ioport.h. 
     */
    info->screen_base = framebuffer_virtual_memory;
    info->fbops = &xxxfb_ops;
    info->fix = xxxfb_fix; /* this will be the only time xxxfb_fix will be
			    * used, so mark it as __initdata
			    */
    info->pseudo_palette = pseudo_palette; /* The pseudopalette is an
					    * 16-member array
					    */
    /*
     * Set up flags to indicate what sort of acceleration your
     * driver can provide (pan/wrap/copyarea/etc.) and whether it
     * is a module -- see FBINFO_* in include/linux/fb.h
     *
     * If your hardware can support any of the hardware accelerated functions
     * fbcon performance will improve if info->flags is set properly.
     *
     * FBINFO_HWACCEL_COPYAREA - hardware moves
     * FBINFO_HWACCEL_FILLRECT - hardware fills
     * FBINFO_HWACCEL_IMAGEBLIT - hardware mono->color expansion
     * FBINFO_HWACCEL_YPAN - hardware can pan display in y-axis
     * FBINFO_HWACCEL_YWRAP - hardware can wrap display in y-axis
     * FBINFO_HWACCEL_DISABLED - supports hardware accels, but disabled
     * FBINFO_READS_FAST - if set, prefer moves over mono->color expansion
     * FBINFO_MISC_TILEBLITTING - hardware can do tile blits
     *
     * NOTE: These are for fbcon use only.
     */
    info->flags = FBINFO_DEFAULT;

/********************* This stage is optional ******************************/
     /*
     * The struct pixmap is a scratch pad for the drawing functions. This
     * is where the monochrome bitmap is constructed by the higher layers
     * and then passed to the accelerator.  For drivers that uses
     * cfb_imageblit, you can skip this part.  For those that have a more
     * rigorous requirement, this stage is needed
     */

    /* PIXMAP_SIZE should be small enough to optimize drawing, but not
     * large enough that memory is wasted.  A safe size is
     * (max_xres * max_font_height/8). max_xres is driver dependent,
     * max_font_height is 32.
     */
    info->pixmap.addr = kmalloc(PIXMAP_SIZE, GFP_KERNEL);
    if (!info->pixmap.addr) {
	    /* goto error */
    }

    info->pixmap.size = PIXMAP_SIZE;

    /*
     * FB_PIXMAP_SYSTEM - memory is in system ram
     * FB_PIXMAP_IO     - memory is iomapped
     * FB_PIXMAP_SYNC   - if set, will call fb_sync() per access to pixmap,
     *                    usually if FB_PIXMAP_IO is set.
     *
     * Currently, FB_PIXMAP_IO is unimplemented.
     */
    info->pixmap.flags = FB_PIXMAP_SYSTEM;

    /*
     * scan_align is the number of padding for each scanline.  It is in bytes.
     * Thus for accelerators that need padding to the next u32, put 4 here.
     */
    info->pixmap.scan_align = 4;

    /*
     * buf_align is the amount to be padded for the buffer. For example,
     * the i810fb needs a scan_align of 2 but expects it to be fed with
     * dwords, so a buf_align = 4 is required.
     */
    info->pixmap.buf_align = 4;

    /* access_align is how many bits can be accessed from the framebuffer
     * ie. some epson cards allow 16-bit access only.  Most drivers will
     * be safe with u32 here.
     *
     * NOTE: This field is currently unused.
     */
    info->pixmap.scan_align = 32;
/***************************** End optional stage ***************************/

    /*
     * This should give a reasonable default video mode. The following is
     * done when we can set a video mode. 
     */
    if (!mode_option)
	mode_option = "640x480@60";	 	

    retval = fb_find_mode(&info->var, info, mode_option, NULL, 0, NULL, 8);
  
    if (!retval || retval == 4)
	return -EINVAL;			

    /* This has to been done !!! */	
    fb_alloc_cmap(&info->cmap, cmap_len, 0);
	
    /* 
     * The following is done in the case of having hardware with a static 
     * mode. If we are setting the mode ourselves we don't call this. 
     */	
    info->var = xxxfb_var;

    /*
     * For drivers that can...
     */
    xxxfb_check_var(&info->var, info);

    /*
     * Does a call to fb_set_par() before register_framebuffer needed?  This
     * will depend on you and the hardware.  If you are sure that your driver
     * is the only device in the system, a call to fb_set_par() is safe.
     *
     * Hardware in x86 systems has a VGA core.  Calling set_par() at this
     * point will corrupt the VGA console, so it might be safer to skip a
     * call to set_par here and just allow fbcon to do it for you.
     */
    /* xxxfb_set_par(info); */

    if (register_framebuffer(info) < 0)
	return -EINVAL;
    printk(KERN_INFO "fb%d: %s frame buffer device\n", info->node,
	   info->fix.id);
    pci_set_drvdata(dev, info); /* or dev_set_drvdata(device, info) */
    return 0;
}

    /*
     *  Cleanup
     */
/* static void __devexit xxxfb_remove(struct device *device) */
static void __devexit xxxfb_remove(struct pci_dev *dev)
{
	struct fb_info *info = pci_get_drvdata(dev);
	/* or dev_get_drvdata(device); */

	if (info) {
		unregister_framebuffer(info);
		fb_dealloc_cmap(&info->cmap);
		/* ... */
		framebuffer_release(info);
	}

	return 0;
}

#ifdef CONFIG_PCI
#ifdef CONFIG_PM
/**
 *	xxxfb_suspend - Optional but recommended function. Suspend the device.
 *	@dev: PCI device
 *	@msg: the suspend event code.
 *
 *      See Documentation/power/devices.txt for more information
 */
static int xxxfb_suspend(struct pci_dev *dev, pm_message_t msg)
{
	struct fb_info *info = pci_get_drvdata(dev);
	struct xxxfb_par *par = info->par;

	/* suspend here */
	return 0;
}

/**
 *	xxxfb_resume - Optional but recommended function. Resume the device.
 *	@dev: PCI device
 *
 *      See Documentation/power/devices.txt for more information
 */
static int xxxfb_resume(struct pci_dev *dev)
{
	struct fb_info *info = pci_get_drvdata(dev);
	struct xxxfb_par *par = info->par;

	/* resume here */
	return 0;
}
#else
#define xxxfb_suspend NULL
#define xxxfb_resume NULL
#endif /* CONFIG_PM */

static struct pci_device_id xxxfb_id_table[] = {
	{ PCI_VENDOR_ID_XXX, PCI_DEVICE_ID_XXX,
	  PCI_ANY_ID, PCI_ANY_ID, PCI_BASE_CLASS_DISPLAY << 16,
	  ADDR, 0 },
	{ 0, }
};

/* For PCI drivers */
static struct pci_driver xxxfb_driver = {
	.name =		"xxxfb",
	.id_table =	xxxfb_id_table,
	.probe =	xxxfb_probe,
	.remove =	__devexit_p(xxxfb_remove),
	.suspend =      xxxfb_suspend, /* optional but recommended */
	.resume =       xxxfb_resume,  /* optional but recommended */
};

int __init xxxfb_init(void)
{
	/*
	 *  For kernel boot options (in 'video=xxxfb:<options>' format)
	 */
#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("xxxfb", &option))
		return -ENODEV;
	xxxfb_setup(option);
#endif

	return pci_register_driver(&xxxfb_driver);
}

static void __exit xxxfb_exit(void)
{
	pci_unregister_driver(&xxxfb_driver);
}
#else /* non PCI, platform drivers */
#include <linux/platform_device.h>
/* for platform devices */

#ifdef CONFIG_PM
/**
 *	xxxfb_suspend - Optional but recommended function. Suspend the device.
 *	@dev: platform device
 *	@msg: the suspend event code.
 *
 *      See Documentation/power/devices.txt for more information
 */
static int xxxfb_suspend(struct platform_device *dev, pm_message_t msg)
{
	struct fb_info *info = platform_get_drvdata(dev);
	struct xxxfb_par *par = info->par;

	/* suspend here */
	return 0;
}

/**
 *	xxxfb_resume - Optional but recommended function. Resume the device.
 *	@dev: PCI device
 *
 *      See Documentation/power/devices.txt for more information
 */
static int xxxfb_suspend(struct platform_dev *dev)
{
	struct fb_info *info = platform_get_drvdata(dev);
	struct xxxfb_par *par = info->par;

	/* resume here */
	return 0;
}
#else
#define xxxfb_suspend NULL
#define xxxfb_resume NULL
#endif /* CONFIG_PM */

static struct device_driver xxxfb_driver = {
	.name = "xxxfb",
	.bus  = &platform_bus_type,
	.probe = xxxfb_probe,
	.remove = xxxfb_remove,
	.suspend = xxxfb_suspend, /* optional but recommended */
	.resume = xxxfb_resume,   /* optional but recommended */
};

static struct platform_device xxxfb_device = {
	.name = "xxxfb",
};

static int __init xxxfb_init(void)
{
	int ret;
	/*
	 *  For kernel boot options (in 'video=xxxfb:<options>' format)
	 */
#ifndef MODULE
	char *option = NULL;

	if (fb_get_options("xxxfb", &option))
		return -ENODEV;
	xxxfb_setup(option);
#endif
	ret = driver_register(&xxxfb_driver);

	if (!ret) {
		ret = platform_device_register(&xxxfb_device);
		if (ret)
			driver_unregister(&xxxfb_driver);
	}

	return ret;
}

static void __exit xxxfb_exit(void)
{
	platform_device_unregister(&xxxfb_device);
	driver_unregister(&xxxfb_driver);
}
#endif /* CONFIG_PCI */

#ifdef MODULE
    /*
     *  Setup
     */

/* 
 * Only necessary if your driver takes special options,
 * otherwise we fall back on the generic fb_setup().
 */
int __init xxxfb_setup(char *options)
{
    /* Parse user speficied options (`video=xxxfb:') */
}
#endif /* MODULE *?

/* ------------------------------------------------------------------------- */


    /*
     *  Modularization
     */

module_init(xxxfb_init);
module_exit(xxxfb_remove);

MODULE_LICENSE("GPL");
