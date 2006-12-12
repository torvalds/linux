/*
 *  linux/drivers/video/S3Triofb.c -- Open Firmware based frame buffer device
 *
 *	Copyright (C) 1997 Peter De Schrijver
 *
 *  This driver is partly based on the PowerMac console driver:
 *
 *	Copyright (C) 1996 Paul Mackerras
 *
 *  and on the Open Firmware based frame buffer device:
 *
 *	Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

/*
	Bugs : + OF dependencies should be removed.
               + This driver should be merged with the CyberVision driver. The
                 CyberVision is a Zorro III implementation of the S3Trio64 chip.

*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/selection.h>
#include <asm/io.h>
#include <asm/prom.h>
#include <asm/pci-bridge.h>
#include <linux/pci.h>

#include <video/fbcon.h>
#include <video/fbcon-cfb8.h>
#include <video/s3blit.h>


#define mem_in8(addr)           in_8((void *)(addr))
#define mem_in16(addr)          in_le16((void *)(addr))
#define mem_in32(addr)          in_le32((void *)(addr))

#define mem_out8(val, addr)     out_8((void *)(addr), val)
#define mem_out16(val, addr)    out_le16((void *)(addr), val)
#define mem_out32(val, addr)    out_le32((void *)(addr), val)

#define IO_OUT16VAL(v, r)       (((v) << 8) | (r))

static struct display disp;
static struct fb_info fb_info;
static struct { u_char red, green, blue, pad; } palette[256];
static char s3trio_name[16] = "S3Trio ";
static char *s3trio_base;

static struct fb_fix_screeninfo fb_fix;
static struct fb_var_screeninfo fb_var = { 0, };


    /*
     *  Interface used by the world
     */

static void __init s3triofb_of_init(struct device_node *dp);
static int s3trio_get_fix(struct fb_fix_screeninfo *fix, int con,
			  struct fb_info *info);
static int s3trio_get_var(struct fb_var_screeninfo *var, int con,
			  struct fb_info *info);
static int s3trio_set_var(struct fb_var_screeninfo *var, int con,
			  struct fb_info *info);
static int s3trio_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			   struct fb_info *info);
static int s3trio_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                         u_int transp, struct fb_info *info);
static int s3trio_pan_display(struct fb_var_screeninfo *var, int con,
			      struct fb_info *info);
static void s3triofb_blank(int blank, struct fb_info *info);

    /*
     *  Interface to the low level console driver
     */

int s3triofb_init(void);
static int s3triofbcon_switch(int con, struct fb_info *info);
static int s3triofbcon_updatevar(int con, struct fb_info *info);

    /*
     *  Text console acceleration
     */

#ifdef FBCON_HAS_CFB8
static struct display_switch fbcon_trio8;
#endif

    /*
     *    Accelerated Functions used by the low level console driver
     */

static void Trio_WaitQueue(u_short fifo);
static void Trio_WaitBlit(void);
static void Trio_BitBLT(u_short curx, u_short cury, u_short destx,
			u_short desty, u_short width, u_short height,
			u_short mode);
static void Trio_RectFill(u_short x, u_short y, u_short width, u_short height,
			  u_short mode, u_short color);
static void Trio_MoveCursor(u_short x, u_short y);


    /*
     *  Internal routines
     */

static int s3trio_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
                         u_int *transp, struct fb_info *info);

static struct fb_ops s3trio_ops = {
	.owner =	THIS_MODULE,
	.fb_get_fix =	s3trio_get_fix,
	.fb_get_var =	s3trio_get_var,
	.fb_set_var =	s3trio_set_var,
	.fb_get_cmap =	s3trio_get_cmap,
	.fb_set_cmap =	gen_set_cmap,
	.fb_setcolreg =	s3trio_setcolreg,
	.fb_pan_display =s3trio_pan_display,
	.fb_blank =	s3triofb_blank,
};

    /*
     *  Get the Fixed Part of the Display
     */

static int s3trio_get_fix(struct fb_fix_screeninfo *fix, int con,
			  struct fb_info *info)
{
    memcpy(fix, &fb_fix, sizeof(fb_fix));
    return 0;
}


    /*
     *  Get the User Defined Part of the Display
     */

static int s3trio_get_var(struct fb_var_screeninfo *var, int con,
			  struct fb_info *info)
{
    memcpy(var, &fb_var, sizeof(fb_var));
    return 0;
}


    /*
     *  Set the User Defined Part of the Display
     */

static int s3trio_set_var(struct fb_var_screeninfo *var, int con,
			  struct fb_info *info)
{
    if (var->xres > fb_var.xres || var->yres > fb_var.yres ||
	var->bits_per_pixel > fb_var.bits_per_pixel )
	/* || var->nonstd || var->vmode != FB_VMODE_NONINTERLACED) */
	return -EINVAL;
    if (var->xres_virtual > fb_var.xres_virtual) {
	outw(IO_OUT16VAL((var->xres_virtual /8) & 0xff, 0x13), 0x3d4);
	outw(IO_OUT16VAL(((var->xres_virtual /8 ) & 0x300) >> 3, 0x51), 0x3d4);
	fb_var.xres_virtual = var->xres_virtual;
	fb_fix.line_length = var->xres_virtual;
    }
    fb_var.yres_virtual = var->yres_virtual;
    memcpy(var, &fb_var, sizeof(fb_var));
    return 0;
}


    /*
     *  Pan or Wrap the Display
     *
     *  This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
     */

static int s3trio_pan_display(struct fb_var_screeninfo *var, int con,
			      struct fb_info *info)
{
    unsigned int base;

    if (var->xoffset > (var->xres_virtual - var->xres))
	return -EINVAL;
    if (var->yoffset > (var->yres_virtual - var->yres))
	return -EINVAL;

    fb_var.xoffset = var->xoffset;
    fb_var.yoffset = var->yoffset;

    base = var->yoffset * fb_fix.line_length + var->xoffset;

    outw(IO_OUT16VAL((base >> 8) & 0xff, 0x0c),0x03D4);
    outw(IO_OUT16VAL(base  & 0xff, 0x0d),0x03D4);
    outw(IO_OUT16VAL((base >> 16) & 0xf, 0x69),0x03D4);
    return 0;
}


    /*
     *  Get the Colormap
     */

static int s3trio_get_cmap(struct fb_cmap *cmap, int kspc, int con,
			   struct fb_info *info)
{
    if (con == info->currcon) /* current console? */
	return fb_get_cmap(cmap, kspc, s3trio_getcolreg, info);
    else if (fb_display[con].cmap.len) /* non default colormap? */
	fb_copy_cmap(&fb_display[con].cmap, cmap, kspc ? 0 : 2);
    else
	fb_copy_cmap(fb_default_cmap(1 << fb_display[con].var.bits_per_pixel),
		     cmap, kspc ? 0 : 2);
    return 0;
}

int __init s3triofb_init(void)
{
	struct device_node *dp;

	dp = find_devices("S3Trio");
	if (dp != 0)
	    s3triofb_of_init(dp);
	return 0;
}

void __init s3trio_resetaccel(void){


#define EC01_ENH_ENB    0x0005
#define EC01_LAW_ENB    0x0010
#define EC01_MMIO_ENB   0x0020

#define EC00_RESET      0x8000
#define EC00_ENABLE     0x4000
#define MF_MULT_MISC    0xE000
#define SRC_FOREGROUND  0x0020
#define SRC_BACKGROUND  0x0000
#define MIX_SRC                 0x0007
#define MF_T_CLIP       0x1000
#define MF_L_CLIP       0x2000
#define MF_B_CLIP       0x3000
#define MF_R_CLIP       0x4000
#define MF_PIX_CONTROL  0xA000
#define MFA_SRC_FOREGR_MIX      0x0000
#define MF_PIX_CONTROL  0xA000

	outw(EC00_RESET,  0x42e8);
	inw(  0x42e8);
	outw(EC00_ENABLE,  0x42e8);
	inw(  0x42e8);
	outw(EC01_ENH_ENB | EC01_LAW_ENB,
		   0x4ae8);
	outw(MF_MULT_MISC,  0xbee8); /* 16 bit I/O registers */

	/* Now set some basic accelerator registers */
	Trio_WaitQueue(0x0400);
	outw(SRC_FOREGROUND | MIX_SRC, 0xbae8);
	outw(SRC_BACKGROUND | MIX_SRC,  0xb6e8);/* direct color*/
	outw(MF_T_CLIP | 0, 0xbee8 );     /* clip virtual area  */
	outw(MF_L_CLIP | 0, 0xbee8 );
	outw(MF_R_CLIP | (640 - 1), 0xbee8);
	outw(MF_B_CLIP | (480 - 1),  0xbee8);
	Trio_WaitQueue(0x0400);
	outw(0xffff,  0xaae8);       /* Enable all planes */
	outw(0xffff, 0xaae8);       /* Enable all planes */
	outw( MF_PIX_CONTROL | MFA_SRC_FOREGR_MIX,  0xbee8);
}

int __init s3trio_init(struct device_node *dp){

    u_char bus, dev;
    unsigned int t32;
    unsigned short cmd;

	pci_device_loc(dp,&bus,&dev);
                pcibios_read_config_dword(bus, dev, PCI_VENDOR_ID, &t32);
                if(t32 == (PCI_DEVICE_ID_S3_TRIO << 16) + PCI_VENDOR_ID_S3) {
                        pcibios_read_config_dword(bus, dev, PCI_BASE_ADDRESS_0, &t32);
                        pcibios_read_config_dword(bus, dev, PCI_BASE_ADDRESS_1, &t32);
			pcibios_read_config_word(bus, dev, PCI_COMMAND,&cmd);

			pcibios_write_config_word(bus, dev, PCI_COMMAND, PCI_COMMAND_IO | PCI_COMMAND_MEMORY);

			pcibios_write_config_dword(bus, dev, PCI_BASE_ADDRESS_0,0xffffffff);
                        pcibios_read_config_dword(bus, dev, PCI_BASE_ADDRESS_0, &t32);

/* This is a gross hack as OF only maps enough memory for the framebuffer and
   we want to use MMIO too. We should find out which chunk of address space
   we can use here */
			pcibios_write_config_dword(bus,dev,PCI_BASE_ADDRESS_0,0xc6000000);

			/* unlock s3 */

			outb(0x01, 0x3C3);

			outb(inb(0x03CC) | 1, 0x3c2);

			outw(IO_OUT16VAL(0x48, 0x38),0x03D4);
			outw(IO_OUT16VAL(0xA0, 0x39),0x03D4);
			outb(0x33,0x3d4);
			outw(IO_OUT16VAL((inb(0x3d5) & ~(0x2 | 0x10 |  0x40)) |
					  0x20, 0x33), 0x3d4);

			outw(IO_OUT16VAL(0x6, 0x8), 0x3c4);

			/* switch to MMIO only mode */

			outb(0x58, 0x3d4);
			outw(IO_OUT16VAL(inb(0x3d5) | 3 | 0x10, 0x58), 0x3d4);
			outw(IO_OUT16VAL(8, 0x53), 0x3d4);

			/* switch off I/O accesses */

#if 0
			pcibios_write_config_word(bus, dev, PCI_COMMAND,
				        PCI_COMMAND_IO | PCI_COMMAND_MEMORY);
#endif
			return 1;
                }

	return 0;
}


    /*
     *  Initialisation
     *  We heavily rely on OF for the moment. This needs fixing.
     */

static void __init s3triofb_of_init(struct device_node *dp)
{
    int i, *pp, len;
    unsigned long address, size;
    u_long *CursorBase;

    strncat(s3trio_name, dp->name, sizeof(s3trio_name));
    s3trio_name[sizeof(s3trio_name)-1] = '\0';
    strcpy(fb_fix.id, s3trio_name);

    if((pp = get_property(dp, "vendor-id", &len)) != NULL
	&& *pp!=PCI_VENDOR_ID_S3) {
	printk("%s: can't find S3 Trio board\n", dp->full_name);
	return;
    }

    if((pp = get_property(dp, "device-id", &len)) != NULL
	&& *pp!=PCI_DEVICE_ID_S3_TRIO) {
	printk("%s: can't find S3 Trio board\n", dp->full_name);
	return;
    }

    if ((pp = get_property(dp, "depth", &len)) != NULL
	&& len == sizeof(int) && *pp != 8) {
	printk("%s: can't use depth = %d\n", dp->full_name, *pp);
	return;
    }
    if ((pp = get_property(dp, "width", &len)) != NULL
	&& len == sizeof(int))
	fb_var.xres = fb_var.xres_virtual = *pp;
    if ((pp = get_property(dp, "height", &len)) != NULL
	&& len == sizeof(int))
	fb_var.yres = fb_var.yres_virtual = *pp;
    if ((pp = get_property(dp, "linebytes", &len)) != NULL
	&& len == sizeof(int))
	fb_fix.line_length = *pp;
    else
	fb_fix.line_length = fb_var.xres_virtual;
    fb_fix.smem_len = fb_fix.line_length*fb_var.yres;

    address = 0xc6000000;
    size = 64*1024*1024;
    if (!request_mem_region(address, size, "S3triofb"))
	return;

    s3trio_init(dp);
    s3trio_base = ioremap(address, size);
    fb_fix.smem_start = address;
    fb_fix.type = FB_TYPE_PACKED_PIXELS;
    fb_fix.type_aux = 0;
    fb_fix.accel = FB_ACCEL_S3_TRIO64;
    fb_fix.mmio_start = address+0x1000000;
    fb_fix.mmio_len = 0x1000000;

    fb_fix.xpanstep = 1;
    fb_fix.ypanstep = 1;

    s3trio_resetaccel();

    mem_out8(0x30, s3trio_base+0x1008000 + 0x03D4);
    mem_out8(0x2d, s3trio_base+0x1008000 + 0x03D4);
    mem_out8(0x2e, s3trio_base+0x1008000 + 0x03D4);

    mem_out8(0x50, s3trio_base+0x1008000 + 0x03D4);

    /* disable HW cursor */

    mem_out8(0x39, s3trio_base+0x1008000 + 0x03D4);
    mem_out8(0xa0, s3trio_base+0x1008000 + 0x03D5);

    mem_out8(0x45, s3trio_base+0x1008000 + 0x03D4);
    mem_out8(0, s3trio_base+0x1008000 + 0x03D5);

    mem_out8(0x4e, s3trio_base+0x1008000 + 0x03D4);
    mem_out8(0, s3trio_base+0x1008000 + 0x03D5);

    mem_out8(0x4f, s3trio_base+0x1008000 + 0x03D4);
    mem_out8(0, s3trio_base+0x1008000 + 0x03D5);

    /* init HW cursor */

    CursorBase = (u_long *)(s3trio_base + 2*1024*1024 - 0x400);
	for (i = 0; i < 8; i++) {
		*(CursorBase  +(i*4)) = 0xffffff00;
		*(CursorBase+1+(i*4)) = 0xffff0000;
		*(CursorBase+2+(i*4)) = 0xffff0000;
		*(CursorBase+3+(i*4)) = 0xffff0000;
	}
	for (i = 8; i < 64; i++) {
		*(CursorBase  +(i*4)) = 0xffff0000;
		*(CursorBase+1+(i*4)) = 0xffff0000;
		*(CursorBase+2+(i*4)) = 0xffff0000;
		*(CursorBase+3+(i*4)) = 0xffff0000;
	}


    mem_out8(0x4c, s3trio_base+0x1008000 + 0x03D4);
    mem_out8(((2*1024 - 1)&0xf00)>>8, s3trio_base+0x1008000 + 0x03D5);

    mem_out8(0x4d, s3trio_base+0x1008000 + 0x03D4);
    mem_out8((2*1024 - 1) & 0xff, s3trio_base+0x1008000 + 0x03D5);

    mem_out8(0x45, s3trio_base+0x1008000 + 0x03D4);
    mem_in8(s3trio_base+0x1008000 + 0x03D4);

    mem_out8(0x4a, s3trio_base+0x1008000 + 0x03D4);
    mem_out8(0x80, s3trio_base+0x1008000 + 0x03D5);
    mem_out8(0x80, s3trio_base+0x1008000 + 0x03D5);
    mem_out8(0x80, s3trio_base+0x1008000 + 0x03D5);

    mem_out8(0x4b, s3trio_base+0x1008000 + 0x03D4);
    mem_out8(0x00, s3trio_base+0x1008000 + 0x03D5);
    mem_out8(0x00, s3trio_base+0x1008000 + 0x03D5);
    mem_out8(0x00, s3trio_base+0x1008000 + 0x03D5);

    mem_out8(0x45, s3trio_base+0x1008000 + 0x03D4);
    mem_out8(0, s3trio_base+0x1008000 + 0x03D5);

    /* setup default color table */

	for(i = 0; i < 16; i++) {
		int j = color_table[i];
		palette[i].red=default_red[j];
		palette[i].green=default_grn[j];
		palette[i].blue=default_blu[j];
	}

    s3trio_setcolreg(255, 56, 100, 160, 0, NULL /* not used */);
    s3trio_setcolreg(254, 0, 0, 0, 0, NULL /* not used */);
    memset((char *)s3trio_base, 0, 640*480);

#if 0
    Trio_RectFill(0, 0, 90, 90, 7, 1);
#endif

    fb_fix.visual = FB_VISUAL_PSEUDOCOLOR ;
    fb_var.xoffset = fb_var.yoffset = 0;
    fb_var.bits_per_pixel = 8;
    fb_var.grayscale = 0;
    fb_var.red.offset = fb_var.green.offset = fb_var.blue.offset = 0;
    fb_var.red.length = fb_var.green.length = fb_var.blue.length = 8;
    fb_var.red.msb_right = fb_var.green.msb_right = fb_var.blue.msb_right = 0;
    fb_var.transp.offset = fb_var.transp.length = fb_var.transp.msb_right = 0;
    fb_var.nonstd = 0;
    fb_var.activate = 0;
    fb_var.height = fb_var.width = -1;
    fb_var.accel_flags = FB_ACCELF_TEXT;
#warning FIXME: always obey fb_var.accel_flags
    fb_var.pixclock = 1;
    fb_var.left_margin = fb_var.right_margin = 0;
    fb_var.upper_margin = fb_var.lower_margin = 0;
    fb_var.hsync_len = fb_var.vsync_len = 0;
    fb_var.sync = 0;
    fb_var.vmode = FB_VMODE_NONINTERLACED;

    disp.var = fb_var;
    disp.cmap.start = 0;
    disp.cmap.len = 0;
    disp.cmap.red = disp.cmap.green = disp.cmap.blue = disp.cmap.transp = NULL;
    disp.visual = fb_fix.visual;
    disp.type = fb_fix.type;
    disp.type_aux = fb_fix.type_aux;
    disp.ypanstep = 0;
    disp.ywrapstep = 0;
    disp.line_length = fb_fix.line_length;
    disp.can_soft_blank = 1;
    disp.inverse = 0;
#ifdef FBCON_HAS_CFB8
    if (fb_var.accel_flags & FB_ACCELF_TEXT)
	disp.dispsw = &fbcon_trio8;
    else
	disp.dispsw = &fbcon_cfb8;
#else
    disp.dispsw = &fbcon_dummy;
#endif
    disp.scrollmode = fb_var.accel_flags & FB_ACCELF_TEXT ? 0 : SCROLL_YREDRAW;

    strcpy(fb_info.modename, "Trio64 ");
    strncat(fb_info.modename, dp->full_name, sizeof(fb_info.modename));
    fb_info.currcon = -1;
    fb_info.fbops = &s3trio_ops;
    fb_info.screen_base = s3trio_base;	
#if 0
    fb_info.fbvar_num = 1;
    fb_info.fbvar = &fb_var;
#endif
    fb_info.disp = &disp;
    fb_info.fontname[0] = '\0';
    fb_info.changevar = NULL;
    fb_info.switch_con = &s3triofbcon_switch;
    fb_info.updatevar = &s3triofbcon_updatevar;
#if 0
    fb_info.setcmap = &s3triofbcon_setcmap;
#endif

    fb_info.flags = FBINFO_FLAG_DEFAULT;
    if (register_framebuffer(&fb_info) < 0) {
		iounmap(fb_info.screen_base);
		fb_info.screen_base = NULL;
		return;
    }

    printk("fb%d: S3 Trio frame buffer device on %s\n",
	   fb_info.node, dp->full_name);
}


static int s3triofbcon_switch(int con, struct fb_info *info)
{
    /* Do we have to save the colormap? */
    if (fb_display[info->currcon].cmap.len)
	fb_get_cmap(&fb_display[info->currcon].cmap, 1, s3trio_getcolreg, info);

    info->currcon = con;
    /* Install new colormap */
    do_install_cmap(con,info);
    return 0;
}

    /*
     *  Update the `var' structure (called by fbcon.c)
     */

static int s3triofbcon_updatevar(int con, struct fb_info *info)
{
    /* Nothing */
    return 0;
}

    /*
     *  Blank the display.
     */

static int s3triofb_blank(int blank, struct fb_info *info)
{
    unsigned char x;

    mem_out8(0x1, s3trio_base+0x1008000 + 0x03c4);
    x = mem_in8(s3trio_base+0x1008000 + 0x03c5);
    mem_out8((x & (~0x20)) | (blank << 5), s3trio_base+0x1008000 + 0x03c5);
    return 0;	
}

    /*
     *  Read a single color register and split it into
     *  colors/transparent. Return != 0 for invalid regno.
     */

static int s3trio_getcolreg(u_int regno, u_int *red, u_int *green, u_int *blue,
                         u_int *transp, struct fb_info *info)
{
    if (regno > 255)
	return 1;
    *red = (palette[regno].red << 8) | palette[regno].red;
    *green = (palette[regno].green << 8) | palette[regno].green;
    *blue = (palette[regno].blue << 8) | palette[regno].blue;
    *transp = 0;
    return 0;
}


    /*
     *  Set a single color register. Return != 0 for invalid regno.
     */

static int s3trio_setcolreg(u_int regno, u_int red, u_int green, u_int blue,
                            u_int transp, struct fb_info *info)
{
    if (regno > 255)
	return 1;

    red >>= 8;
    green >>= 8;
    blue >>= 8;
    palette[regno].red = red;
    palette[regno].green = green;
    palette[regno].blue = blue;

    mem_out8(regno,s3trio_base+0x1008000 + 0x3c8);
    mem_out8((red & 0xff) >> 2,s3trio_base+0x1008000 + 0x3c9);
    mem_out8((green & 0xff) >> 2,s3trio_base+0x1008000 + 0x3c9);
    mem_out8((blue & 0xff) >> 2,s3trio_base+0x1008000 + 0x3c9);

    return 0;
}

static void Trio_WaitQueue(u_short fifo) {

	u_short status;

        do
        {
		status = mem_in16(s3trio_base + 0x1000000 + 0x9AE8);
	}  while (!(status & fifo));

}

static void Trio_WaitBlit(void) {

	u_short status;

        do
        {
		status = mem_in16(s3trio_base + 0x1000000 + 0x9AE8);
	}  while (status & 0x200);

}

static void Trio_BitBLT(u_short curx, u_short cury, u_short destx,
			u_short desty, u_short width, u_short height,
			u_short mode) {

	u_short blitcmd = 0xc011;

	/* Set drawing direction */
        /* -Y, X maj, -X (default) */

	if (curx > destx)
		blitcmd |= 0x0020;  /* Drawing direction +X */
	else {
		curx  += (width - 1);
		destx += (width - 1);
	}

	if (cury > desty)
		blitcmd |= 0x0080;  /* Drawing direction +Y */
	else {
		cury  += (height - 1);
		desty += (height - 1);
	}

	Trio_WaitQueue(0x0400);

	outw(0xa000,  0xBEE8);
	outw(0x60 | mode,  0xBAE8);

	outw(curx,  0x86E8);
	outw(cury,  0x82E8);

	outw(destx,  0x8EE8);
	outw(desty,  0x8AE8);

	outw(height - 1,  0xBEE8);
	outw(width - 1,  0x96E8);

	outw(blitcmd,  0x9AE8);

}

static void Trio_RectFill(u_short x, u_short y, u_short width, u_short height,
			  u_short mode, u_short color) {

	u_short blitcmd = 0x40b1;

	Trio_WaitQueue(0x0400);

	outw(0xa000,  0xBEE8);
	outw((0x20 | mode),  0xBAE8);
	outw(0xe000,  0xBEE8);
	outw(color,  0xA6E8);
	outw(x,  0x86E8);
	outw(y,  0x82E8);
	outw((height - 1), 0xBEE8);
	outw((width - 1), 0x96E8);
	outw(blitcmd,  0x9AE8);

}


static void Trio_MoveCursor(u_short x, u_short y) {

	mem_out8(0x39, s3trio_base + 0x1008000 + 0x3d4);
	mem_out8(0xa0, s3trio_base + 0x1008000 + 0x3d5);

	mem_out8(0x46, s3trio_base + 0x1008000 + 0x3d4);
	mem_out8((x & 0x0700) >> 8, s3trio_base + 0x1008000 + 0x3d5);
	mem_out8(0x47, s3trio_base + 0x1008000 + 0x3d4);
	mem_out8(x & 0x00ff, s3trio_base + 0x1008000 + 0x3d5);

	mem_out8(0x48, s3trio_base + 0x1008000 + 0x3d4);
	mem_out8((y & 0x0700) >> 8, s3trio_base + 0x1008000 + 0x3d5);
	mem_out8(0x49, s3trio_base + 0x1008000 + 0x3d4);
	mem_out8(y & 0x00ff, s3trio_base + 0x1008000 + 0x3d5);

}


    /*
     *  Text console acceleration
     */

#ifdef FBCON_HAS_CFB8
static void fbcon_trio8_bmove(struct display *p, int sy, int sx, int dy,
			      int dx, int height, int width)
{
    sx *= 8; dx *= 8; width *= 8;
    Trio_BitBLT((u_short)sx, (u_short)(sy*fontheight(p)), (u_short)dx,
		 (u_short)(dy*fontheight(p)), (u_short)width,
		 (u_short)(height*fontheight(p)), (u_short)S3_NEW);
}

static void fbcon_trio8_clear(struct vc_data *conp, struct display *p, int sy,
			      int sx, int height, int width)
{
    unsigned char bg;

    sx *= 8; width *= 8;
    bg = attr_bgcol_ec(p,conp);
    Trio_RectFill((u_short)sx,
		   (u_short)(sy*fontheight(p)),
		   (u_short)width,
		   (u_short)(height*fontheight(p)),
		   (u_short)S3_NEW,
		   (u_short)bg);
}

static void fbcon_trio8_putc(struct vc_data *conp, struct display *p, int c,
			     int yy, int xx)
{
    Trio_WaitBlit();
    fbcon_cfb8_putc(conp, p, c, yy, xx);
}

static void fbcon_trio8_putcs(struct vc_data *conp, struct display *p,
			      const unsigned short *s, int count, int yy, int xx)
{
    Trio_WaitBlit();
    fbcon_cfb8_putcs(conp, p, s, count, yy, xx);
}

static void fbcon_trio8_revc(struct display *p, int xx, int yy)
{
    Trio_WaitBlit();
    fbcon_cfb8_revc(p, xx, yy);
}

static struct display_switch fbcon_trio8 = {
   .setup =		fbcon_cfb8_setup,
   .bmove =		fbcon_trio8_bmove,
   .clear =		fbcon_trio8_clear,
   .putc =		fbcon_trio8_putc,
   .putcs =		fbcon_trio8_putcs,
   .revc =		fbcon_trio8_revc,
   .clear_margins =	fbcon_cfb8_clear_margins,
   .fontwidthmask =	FONTWIDTH(8)
};
#endif

MODULE_LICENSE("GPL");
