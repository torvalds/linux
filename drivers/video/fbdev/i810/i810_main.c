 /*-*- linux-c -*-
 *  linux/drivers/video/i810_main.c -- Intel 810 frame buffer device
 *
 *      Copyright (C) 2001 Antonino Daplas<adaplas@pol.net>
 *      All Rights Reserved      
 *
 *      Contributors:
 *         Michael Vogt <mvogt@acm.org> - added support for Intel 815 chipsets
 *                                        and enabling the power-on state of 
 *                                        external VGA connectors for 
 *                                        secondary displays
 *
 *         Fredrik Andersson <krueger@shell.linux.se> - alpha testing of
 *                                        the VESA GTF
 *
 *         Brad Corrion <bcorrion@web-co.com> - alpha testing of customized
 *                                        timings support
 *
 *	The code framework is a modification of vfb.c by Geert Uytterhoeven.
 *      DotClock and PLL calculations are partly based on i810_driver.c 
 *              in xfree86 v4.0.3 by Precision Insight.
 *      Watermark calculation and tables are based on i810_wmark.c 
 *              in xfre86 v4.0.3 by Precision Insight.  Slight modifications 
 *              only to allow for integer operations instead of floating point.
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
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/resource.h>
#include <linux/unistd.h>
#include <linux/console.h>
#include <linux/io.h>

#include <asm/io.h>
#include <asm/div64.h>
#include <asm/page.h>

#include "i810_regs.h"
#include "i810.h"
#include "i810_main.h"

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
static u32 v_offset_default; /* For 32 MiB Aper size, 8 should be the default */
static u32 voffset;

static int i810fb_cursor(struct fb_info *info, struct fb_cursor *cursor);
static int i810fb_init_pci(struct pci_dev *dev,
			   const struct pci_device_id *entry);
static void i810fb_remove_pci(struct pci_dev *dev);
static int i810fb_resume(struct pci_dev *dev);
static int i810fb_suspend(struct pci_dev *dev, pm_message_t state);

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

/* PCI */
static const char * const i810_pci_list[] = {
	"Intel(R) 810 Framebuffer Device"                                 ,
	"Intel(R) 810-DC100 Framebuffer Device"                           ,
	"Intel(R) 810E Framebuffer Device"                                ,
	"Intel(R) 815 (Internal Graphics 100Mhz FSB) Framebuffer Device"  ,
	"Intel(R) 815 (Internal Graphics only) Framebuffer Device"        ,
	"Intel(R) 815 (Internal Graphics with AGP) Framebuffer Device"
};

static const struct pci_device_id i810fb_pci_tbl[] = {
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82810_IG1,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82810_IG3,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 1  },
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82810E_IG,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 2 },
	/* mvo: added i815 PCI-ID */
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82815_100,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 3 },
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82815_NOAGP,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 4 },
	{ PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82815_CGC,
	  PCI_ANY_ID, PCI_ANY_ID, 0, 0, 5 },
	{ 0 },
};

static struct pci_driver i810fb_driver = {
	.name     =	"i810fb",
	.id_table =	i810fb_pci_tbl,
	.probe    =	i810fb_init_pci,
	.remove   =	i810fb_remove_pci,
	.suspend  =     i810fb_suspend,
	.resume   =     i810fb_resume,
};

static char *mode_option = NULL;
static int vram = 4;
static int bpp = 8;
static bool mtrr;
static bool accel;
static int hsync1;
static int hsync2;
static int vsync1;
static int vsync2;
static int xres;
static int yres;
static int vyres;
static bool sync;
static bool extvga;
static bool dcolor;
static bool ddc3;

/*------------------------------------------------------------*/

/**************************************************************
 *                Hardware Low Level Routines                 *
 **************************************************************/

/**
 * i810_screen_off - turns off/on display
 * @mmio: address of register space
 * @mode: on or off
 *
 * DESCRIPTION:
 * Blanks/unblanks the display
 */
static void i810_screen_off(u8 __iomem *mmio, u8 mode)
{
	u32 count = WAIT_COUNT;
	u8 val;

	i810_writeb(SR_INDEX, mmio, SR01);
	val = i810_readb(SR_DATA, mmio);
	val = (mode == OFF) ? val | SCR_OFF :
		val & ~SCR_OFF;

	while((i810_readw(DISP_SL, mmio) & 0xFFF) && count--);
	i810_writeb(SR_INDEX, mmio, SR01);
	i810_writeb(SR_DATA, mmio, val);
}

/**
 * i810_dram_off - turns off/on dram refresh
 * @mmio: address of register space
 * @mode: on or off
 *
 * DESCRIPTION:
 * Turns off DRAM refresh.  Must be off for only 2 vsyncs
 * before data becomes corrupt
 */
static void i810_dram_off(u8 __iomem *mmio, u8 mode)
{
	u8 val;

	val = i810_readb(DRAMCH, mmio);
	val &= DRAM_OFF;
	val = (mode == OFF) ? val : val | DRAM_ON;
	i810_writeb(DRAMCH, mmio, val);
}

/**
 * i810_protect_regs - allows rw/ro mode of certain VGA registers
 * @mmio: address of register space
 * @mode: protect/unprotect
 *
 * DESCRIPTION:
 * The IBM VGA standard allows protection of certain VGA registers.  
 * This will  protect or unprotect them. 
 */
static void i810_protect_regs(u8 __iomem *mmio, int mode)
{
	u8 reg;

	i810_writeb(CR_INDEX_CGA, mmio, CR11);
	reg = i810_readb(CR_DATA_CGA, mmio);
	reg = (mode == OFF) ? reg & ~0x80 :
		reg | 0x80;
 		
	i810_writeb(CR_INDEX_CGA, mmio, CR11);
	i810_writeb(CR_DATA_CGA, mmio, reg);
}

/**
 * i810_load_pll - loads values for the hardware PLL clock
 * @par: pointer to i810fb_par structure
 *
 * DESCRIPTION:
 * Loads the P, M, and N registers.  
 */
static void i810_load_pll(struct i810fb_par *par)
{
	u32 tmp1, tmp2;
	u8 __iomem *mmio = par->mmio_start_virtual;
	
	tmp1 = par->regs.M | par->regs.N << 16;
	tmp2 = i810_readl(DCLK_2D, mmio);
	tmp2 &= ~MN_MASK;
	i810_writel(DCLK_2D, mmio, tmp1 | tmp2);
	
	tmp1 = par->regs.P;
	tmp2 = i810_readl(DCLK_0DS, mmio);
	tmp2 &= ~(P_OR << 16);
	i810_writel(DCLK_0DS, mmio, (tmp1 << 16) | tmp2);

	i810_writeb(MSR_WRITE, mmio, par->regs.msr | 0xC8 | 1);

}

/**
 * i810_load_vga - load standard VGA registers
 * @par: pointer to i810fb_par structure
 *
 * DESCRIPTION:
 * Load values to VGA registers
 */
static void i810_load_vga(struct i810fb_par *par)
{	
	u8 __iomem *mmio = par->mmio_start_virtual;

	/* interlace */
	i810_writeb(CR_INDEX_CGA, mmio, CR70);
	i810_writeb(CR_DATA_CGA, mmio, par->interlace);

	i810_writeb(CR_INDEX_CGA, mmio, CR00);
	i810_writeb(CR_DATA_CGA, mmio, par->regs.cr00);
	i810_writeb(CR_INDEX_CGA, mmio, CR01);
	i810_writeb(CR_DATA_CGA, mmio, par->regs.cr01);
	i810_writeb(CR_INDEX_CGA, mmio, CR02);
	i810_writeb(CR_DATA_CGA, mmio, par->regs.cr02);
	i810_writeb(CR_INDEX_CGA, mmio, CR03);
	i810_writeb(CR_DATA_CGA, mmio, par->regs.cr03);
	i810_writeb(CR_INDEX_CGA, mmio, CR04);
	i810_writeb(CR_DATA_CGA, mmio, par->regs.cr04);
	i810_writeb(CR_INDEX_CGA, mmio, CR05);
	i810_writeb(CR_DATA_CGA, mmio, par->regs.cr05);
	i810_writeb(CR_INDEX_CGA, mmio, CR06);
	i810_writeb(CR_DATA_CGA, mmio, par->regs.cr06);
	i810_writeb(CR_INDEX_CGA, mmio, CR09);
	i810_writeb(CR_DATA_CGA, mmio, par->regs.cr09);
	i810_writeb(CR_INDEX_CGA, mmio, CR10);
	i810_writeb(CR_DATA_CGA, mmio, par->regs.cr10);
	i810_writeb(CR_INDEX_CGA, mmio, CR11);
	i810_writeb(CR_DATA_CGA, mmio, par->regs.cr11);
	i810_writeb(CR_INDEX_CGA, mmio, CR12);
	i810_writeb(CR_DATA_CGA, mmio, par->regs.cr12);
	i810_writeb(CR_INDEX_CGA, mmio, CR15);
	i810_writeb(CR_DATA_CGA, mmio, par->regs.cr15);
	i810_writeb(CR_INDEX_CGA, mmio, CR16);
	i810_writeb(CR_DATA_CGA, mmio, par->regs.cr16);
}

/**
 * i810_load_vgax - load extended VGA registers
 * @par: pointer to i810fb_par structure
 *
 * DESCRIPTION:
 * Load values to extended VGA registers
 */
static void i810_load_vgax(struct i810fb_par *par)
{
	u8 __iomem *mmio = par->mmio_start_virtual;

	i810_writeb(CR_INDEX_CGA, mmio, CR30);
	i810_writeb(CR_DATA_CGA, mmio, par->regs.cr30);
	i810_writeb(CR_INDEX_CGA, mmio, CR31);
	i810_writeb(CR_DATA_CGA, mmio, par->regs.cr31);
	i810_writeb(CR_INDEX_CGA, mmio, CR32);
	i810_writeb(CR_DATA_CGA, mmio, par->regs.cr32);
	i810_writeb(CR_INDEX_CGA, mmio, CR33);
	i810_writeb(CR_DATA_CGA, mmio, par->regs.cr33);
	i810_writeb(CR_INDEX_CGA, mmio, CR35);
	i810_writeb(CR_DATA_CGA, mmio, par->regs.cr35);
	i810_writeb(CR_INDEX_CGA, mmio, CR39);
	i810_writeb(CR_DATA_CGA, mmio, par->regs.cr39);
}

/**
 * i810_load_2d - load grahics registers
 * @par: pointer to i810fb_par structure
 *
 * DESCRIPTION:
 * Load values to graphics registers
 */
static void i810_load_2d(struct i810fb_par *par)
{
	u32 tmp;
	u8 tmp8;
	u8 __iomem *mmio = par->mmio_start_virtual;

  	i810_writel(FW_BLC, mmio, par->watermark); 
	tmp = i810_readl(PIXCONF, mmio);
	tmp |= 1 | 1 << 20;
	i810_writel(PIXCONF, mmio, tmp);

	i810_writel(OVRACT, mmio, par->ovract);

	i810_writeb(GR_INDEX, mmio, GR10);
	tmp8 = i810_readb(GR_DATA, mmio);
	tmp8 |= 2;
	i810_writeb(GR_INDEX, mmio, GR10);
	i810_writeb(GR_DATA, mmio, tmp8);
}	

/**
 * i810_hires - enables high resolution mode
 * @mmio: address of register space
 */
static void i810_hires(u8 __iomem *mmio)
{
	u8 val;
	
	i810_writeb(CR_INDEX_CGA, mmio, CR80);
	val = i810_readb(CR_DATA_CGA, mmio);
	i810_writeb(CR_INDEX_CGA, mmio, CR80);
	i810_writeb(CR_DATA_CGA, mmio, val | 1);
	/* Stop LCD displays from flickering */
	i810_writel(MEM_MODE, mmio, i810_readl(MEM_MODE, mmio) | 4);
}

/**
 * i810_load_pitch - loads the characters per line of the display
 * @par: pointer to i810fb_par structure
 *
 * DESCRIPTION:
 * Loads the characters per line
 */	
static void i810_load_pitch(struct i810fb_par *par)
{
	u32 tmp, pitch;
	u8 val;
	u8 __iomem *mmio = par->mmio_start_virtual;
			
	pitch = par->pitch >> 3;
	i810_writeb(SR_INDEX, mmio, SR01);
	val = i810_readb(SR_DATA, mmio);
	val &= 0xE0;
	val |= 1 | 1 << 2;
	i810_writeb(SR_INDEX, mmio, SR01);
	i810_writeb(SR_DATA, mmio, val);

	tmp = pitch & 0xFF;
	i810_writeb(CR_INDEX_CGA, mmio, CR13);
	i810_writeb(CR_DATA_CGA, mmio, (u8) tmp);
	
	tmp = pitch >> 8;
	i810_writeb(CR_INDEX_CGA, mmio, CR41);
	val = i810_readb(CR_DATA_CGA, mmio) & ~0x0F;
	i810_writeb(CR_INDEX_CGA, mmio, CR41);
	i810_writeb(CR_DATA_CGA, mmio, (u8) tmp | val);
}

/**
 * i810_load_color - loads the color depth of the display
 * @par: pointer to i810fb_par structure
 *
 * DESCRIPTION:
 * Loads the color depth of the display and the graphics engine
 */
static void i810_load_color(struct i810fb_par *par)
{
	u8 __iomem *mmio = par->mmio_start_virtual;
	u32 reg1;
	u16 reg2;

	reg1 = i810_readl(PIXCONF, mmio) & ~(0xF0000 | 1 << 27);
	reg2 = i810_readw(BLTCNTL, mmio) & ~0x30;

	reg1 |= 0x8000 | par->pixconf;
	reg2 |= par->bltcntl;
	i810_writel(PIXCONF, mmio, reg1);
	i810_writew(BLTCNTL, mmio, reg2);
}

/**
 * i810_load_regs - loads all registers for the mode
 * @par: pointer to i810fb_par structure
 * 
 * DESCRIPTION:
 * Loads registers
 */
static void i810_load_regs(struct i810fb_par *par)
{
	u8 __iomem *mmio = par->mmio_start_virtual;

	i810_screen_off(mmio, OFF);
	i810_protect_regs(mmio, OFF);
	i810_dram_off(mmio, OFF);
	i810_load_pll(par);
	i810_load_vga(par);
	i810_load_vgax(par);
	i810_dram_off(mmio, ON);	
	i810_load_2d(par);
	i810_hires(mmio);
	i810_screen_off(mmio, ON);
	i810_protect_regs(mmio, ON);
	i810_load_color(par);
	i810_load_pitch(par);
}

static void i810_write_dac(u8 regno, u8 red, u8 green, u8 blue,
			  u8 __iomem *mmio)
{
	i810_writeb(CLUT_INDEX_WRITE, mmio, regno);
	i810_writeb(CLUT_DATA, mmio, red);
	i810_writeb(CLUT_DATA, mmio, green);
	i810_writeb(CLUT_DATA, mmio, blue); 	
}

static void i810_read_dac(u8 regno, u8 *red, u8 *green, u8 *blue,
			  u8 __iomem *mmio)
{
	i810_writeb(CLUT_INDEX_READ, mmio, regno);
	*red = i810_readb(CLUT_DATA, mmio);
	*green = i810_readb(CLUT_DATA, mmio);
	*blue = i810_readb(CLUT_DATA, mmio);
}

/************************************************************
 *                   VGA State Restore                      * 
 ************************************************************/
static void i810_restore_pll(struct i810fb_par *par)
{
	u32 tmp1, tmp2;
	u8 __iomem *mmio = par->mmio_start_virtual;
	
	tmp1 = par->hw_state.dclk_2d;
	tmp2 = i810_readl(DCLK_2D, mmio);
	tmp1 &= ~MN_MASK;
	tmp2 &= MN_MASK;
	i810_writel(DCLK_2D, mmio, tmp1 | tmp2);

	tmp1 = par->hw_state.dclk_1d;
	tmp2 = i810_readl(DCLK_1D, mmio);
	tmp1 &= ~MN_MASK;
	tmp2 &= MN_MASK;
	i810_writel(DCLK_1D, mmio, tmp1 | tmp2);

	i810_writel(DCLK_0DS, mmio, par->hw_state.dclk_0ds);
}

static void i810_restore_dac(struct i810fb_par *par)
{
	u32 tmp1, tmp2;
	u8 __iomem *mmio = par->mmio_start_virtual;

	tmp1 = par->hw_state.pixconf;
	tmp2 = i810_readl(PIXCONF, mmio);
	tmp1 &= DAC_BIT;
	tmp2 &= ~DAC_BIT;
	i810_writel(PIXCONF, mmio, tmp1 | tmp2);
}

static void i810_restore_vgax(struct i810fb_par *par)
{
	u8 i, j;
	u8 __iomem *mmio = par->mmio_start_virtual;
	
	for (i = 0; i < 4; i++) {
		i810_writeb(CR_INDEX_CGA, mmio, CR30+i);
		i810_writeb(CR_DATA_CGA, mmio, *(&(par->hw_state.cr30) + i));
	}
	i810_writeb(CR_INDEX_CGA, mmio, CR35);
	i810_writeb(CR_DATA_CGA, mmio, par->hw_state.cr35);
	i810_writeb(CR_INDEX_CGA, mmio, CR39);
	i810_writeb(CR_DATA_CGA, mmio, par->hw_state.cr39);
	i810_writeb(CR_INDEX_CGA, mmio, CR41);
	i810_writeb(CR_DATA_CGA, mmio, par->hw_state.cr39);

	/*restore interlace*/
	i810_writeb(CR_INDEX_CGA, mmio, CR70);
	i = par->hw_state.cr70;
	i &= INTERLACE_BIT;
	j = i810_readb(CR_DATA_CGA, mmio);
	i810_writeb(CR_INDEX_CGA, mmio, CR70);
	i810_writeb(CR_DATA_CGA, mmio, j | i);

	i810_writeb(CR_INDEX_CGA, mmio, CR80);
	i810_writeb(CR_DATA_CGA, mmio, par->hw_state.cr80);
	i810_writeb(MSR_WRITE, mmio, par->hw_state.msr);
	i810_writeb(SR_INDEX, mmio, SR01);
	i = (par->hw_state.sr01) & ~0xE0 ;
	j = i810_readb(SR_DATA, mmio) & 0xE0;
	i810_writeb(SR_INDEX, mmio, SR01);
	i810_writeb(SR_DATA, mmio, i | j);
}

static void i810_restore_vga(struct i810fb_par *par)
{
	u8 i;
	u8 __iomem *mmio = par->mmio_start_virtual;
	
	for (i = 0; i < 10; i++) {
		i810_writeb(CR_INDEX_CGA, mmio, CR00 + i);
		i810_writeb(CR_DATA_CGA, mmio, *((&par->hw_state.cr00) + i));
	}
	for (i = 0; i < 8; i++) {
		i810_writeb(CR_INDEX_CGA, mmio, CR10 + i);
		i810_writeb(CR_DATA_CGA, mmio, *((&par->hw_state.cr10) + i));
	}
}

static void i810_restore_addr_map(struct i810fb_par *par)
{
	u8 tmp;
	u8 __iomem *mmio = par->mmio_start_virtual;

	i810_writeb(GR_INDEX, mmio, GR10);
	tmp = i810_readb(GR_DATA, mmio);
	tmp &= ADDR_MAP_MASK;
	tmp |= par->hw_state.gr10;
	i810_writeb(GR_INDEX, mmio, GR10);
	i810_writeb(GR_DATA, mmio, tmp);
}

static void i810_restore_2d(struct i810fb_par *par)
{
	u32 tmp_long;
	u16 tmp_word;
	u8 __iomem *mmio = par->mmio_start_virtual;

	tmp_word = i810_readw(BLTCNTL, mmio);
	tmp_word &= ~(3 << 4); 
	tmp_word |= par->hw_state.bltcntl;
	i810_writew(BLTCNTL, mmio, tmp_word);
       
	i810_dram_off(mmio, OFF);
	i810_writel(PIXCONF, mmio, par->hw_state.pixconf);
	i810_dram_off(mmio, ON);

	tmp_word = i810_readw(HWSTAM, mmio);
	tmp_word &= 3 << 13;
	tmp_word |= par->hw_state.hwstam;
	i810_writew(HWSTAM, mmio, tmp_word);

	tmp_long = i810_readl(FW_BLC, mmio);
	tmp_long &= FW_BLC_MASK;
	tmp_long |= par->hw_state.fw_blc;
	i810_writel(FW_BLC, mmio, tmp_long);

	i810_writel(HWS_PGA, mmio, par->hw_state.hws_pga); 
	i810_writew(IER, mmio, par->hw_state.ier);
	i810_writew(IMR, mmio, par->hw_state.imr);
	i810_writel(DPLYSTAS, mmio, par->hw_state.dplystas);
}

static void i810_restore_vga_state(struct i810fb_par *par)
{
	u8 __iomem *mmio = par->mmio_start_virtual;

	i810_screen_off(mmio, OFF);
	i810_protect_regs(mmio, OFF);
	i810_dram_off(mmio, OFF);
	i810_restore_pll(par);
	i810_restore_dac(par);
	i810_restore_vga(par);
	i810_restore_vgax(par);
	i810_restore_addr_map(par);
	i810_dram_off(mmio, ON);
	i810_restore_2d(par);
	i810_screen_off(mmio, ON);
	i810_protect_regs(mmio, ON);
}

/***********************************************************************
 *                         VGA State Save                              *
 ***********************************************************************/

static void i810_save_vgax(struct i810fb_par *par)
{
	u8 i;
	u8 __iomem *mmio = par->mmio_start_virtual;

	for (i = 0; i < 4; i++) {
		i810_writeb(CR_INDEX_CGA, mmio, CR30 + i);
		*(&(par->hw_state.cr30) + i) = i810_readb(CR_DATA_CGA, mmio);
	}
	i810_writeb(CR_INDEX_CGA, mmio, CR35);
	par->hw_state.cr35 = i810_readb(CR_DATA_CGA, mmio);
	i810_writeb(CR_INDEX_CGA, mmio, CR39);
	par->hw_state.cr39 = i810_readb(CR_DATA_CGA, mmio);
	i810_writeb(CR_INDEX_CGA, mmio, CR41);
	par->hw_state.cr41 = i810_readb(CR_DATA_CGA, mmio);
	i810_writeb(CR_INDEX_CGA, mmio, CR70);
	par->hw_state.cr70 = i810_readb(CR_DATA_CGA, mmio);	
	par->hw_state.msr = i810_readb(MSR_READ, mmio);
	i810_writeb(CR_INDEX_CGA, mmio, CR80);
	par->hw_state.cr80 = i810_readb(CR_DATA_CGA, mmio);
	i810_writeb(SR_INDEX, mmio, SR01);
	par->hw_state.sr01 = i810_readb(SR_DATA, mmio);
}

static void i810_save_vga(struct i810fb_par *par)
{
	u8 i;
	u8 __iomem *mmio = par->mmio_start_virtual;

	for (i = 0; i < 10; i++) {
		i810_writeb(CR_INDEX_CGA, mmio, CR00 + i);
		*((&par->hw_state.cr00) + i) = i810_readb(CR_DATA_CGA, mmio);
	}
	for (i = 0; i < 8; i++) {
		i810_writeb(CR_INDEX_CGA, mmio, CR10 + i);
		*((&par->hw_state.cr10) + i) = i810_readb(CR_DATA_CGA, mmio);
	}
}

static void i810_save_2d(struct i810fb_par *par)
{
	u8 __iomem *mmio = par->mmio_start_virtual;

	par->hw_state.dclk_2d = i810_readl(DCLK_2D, mmio);
	par->hw_state.dclk_1d = i810_readl(DCLK_1D, mmio);
	par->hw_state.dclk_0ds = i810_readl(DCLK_0DS, mmio);
	par->hw_state.pixconf = i810_readl(PIXCONF, mmio);
	par->hw_state.fw_blc = i810_readl(FW_BLC, mmio);
	par->hw_state.bltcntl = i810_readw(BLTCNTL, mmio);
	par->hw_state.hwstam = i810_readw(HWSTAM, mmio); 
	par->hw_state.hws_pga = i810_readl(HWS_PGA, mmio); 
	par->hw_state.ier = i810_readw(IER, mmio);
	par->hw_state.imr = i810_readw(IMR, mmio);
	par->hw_state.dplystas = i810_readl(DPLYSTAS, mmio);
}

static void i810_save_vga_state(struct i810fb_par *par)
{
	i810_save_vga(par);
	i810_save_vgax(par);
	i810_save_2d(par);
}

/************************************************************
 *                    Helpers                               * 
 ************************************************************/
/**
 * get_line_length - calculates buffer pitch in bytes
 * @par: pointer to i810fb_par structure
 * @xres_virtual: virtual resolution of the frame
 * @bpp: bits per pixel
 *
 * DESCRIPTION:
 * Calculates buffer pitch in bytes.  
 */
static u32 get_line_length(struct i810fb_par *par, int xres_virtual, int bpp)
{
   	u32 length;
	
	length = xres_virtual*bpp;
	length = (length+31)&-32;
	length >>= 3;
	return length;
}

/**
 * i810_calc_dclk - calculates the P, M, and N values of a pixelclock value
 * @freq: target pixelclock in picoseconds
 * @m: where to write M register
 * @n: where to write N register
 * @p: where to write P register
 *
 * DESCRIPTION:
 * Based on the formula Freq_actual = (4*M*Freq_ref)/(N^P)
 * Repeatedly computes the Freq until the actual Freq is equal to
 * the target Freq or until the loop count is zero.  In the latter
 * case, the actual frequency nearest the target will be used.
 */
static void i810_calc_dclk(u32 freq, u32 *m, u32 *n, u32 *p)
{
	u32 m_reg, n_reg, p_divisor, n_target_max;
	u32 m_target, n_target, p_target, n_best, m_best, mod;
	u32 f_out, target_freq, diff = 0, mod_min, diff_min;

	diff_min = mod_min = 0xFFFFFFFF;
	n_best = m_best = m_target = f_out = 0;

	target_freq =  freq;
	n_target_max = 30;

	/*
	 * find P such that target freq is 16x reference freq (Hz). 
	 */
	p_divisor = 1;
	p_target = 0;
	while(!((1000000 * p_divisor)/(16 * 24 * target_freq)) && 
	      p_divisor <= 32) {
		p_divisor <<= 1;
		p_target++;
	}

	n_reg = m_reg = n_target = 3;	
	while (diff_min && mod_min && (n_target < n_target_max)) {
		f_out = (p_divisor * n_reg * 1000000)/(4 * 24 * m_reg);
		mod = (p_divisor * n_reg * 1000000) % (4 * 24 * m_reg);
		m_target = m_reg;
		n_target = n_reg;
		if (f_out <= target_freq) {
			n_reg++;
			diff = target_freq - f_out;
		} else {
			m_reg++;
			diff = f_out - target_freq;
		}

		if (diff_min > diff) {
			diff_min = diff;
			n_best = n_target;
			m_best = m_target;
		}		 

		if (!diff && mod_min > mod) {
			mod_min = mod;
			n_best = n_target;
			m_best = m_target;
		}
	} 
	if (m) *m = (m_best - 2) & 0x3FF;
	if (n) *n = (n_best - 2) & 0x3FF;
	if (p) *p = (p_target << 4);
}

/*************************************************************
 *                Hardware Cursor Routines                   *
 *************************************************************/

/**
 * i810_enable_cursor - show or hide the hardware cursor
 * @mmio: address of register space
 * @mode: show (1) or hide (0)
 *
 * Description:
 * Shows or hides the hardware cursor
 */
static void i810_enable_cursor(u8 __iomem *mmio, int mode)
{
	u32 temp;
	
	temp = i810_readl(PIXCONF, mmio);
	temp = (mode == ON) ? temp | CURSOR_ENABLE_MASK :
		temp & ~CURSOR_ENABLE_MASK;

	i810_writel(PIXCONF, mmio, temp);
}

static void i810_reset_cursor_image(struct i810fb_par *par)
{
	u8 __iomem *addr = par->cursor_heap.virtual;
	int i, j;

	for (i = 64; i--; ) {
		for (j = 0; j < 8; j++) {             
			i810_writeb(j, addr, 0xff);   
			i810_writeb(j+8, addr, 0x00); 
		}	
		addr +=16;
	}
}

static void i810_load_cursor_image(int width, int height, u8 *data,
				   struct i810fb_par *par)
{
	u8 __iomem *addr = par->cursor_heap.virtual;
	int i, j, w = width/8;
	int mod = width % 8, t_mask, d_mask;
	
	t_mask = 0xff >> mod;
	d_mask = ~(0xff >> mod); 
	for (i = height; i--; ) {
		for (j = 0; j < w; j++) {
			i810_writeb(j+0, addr, 0x00);
			i810_writeb(j+8, addr, *data++);
		}
		if (mod) {
			i810_writeb(j+0, addr, t_mask);
			i810_writeb(j+8, addr, *data++ & d_mask);
		}
		addr += 16;
	}
}

static void i810_load_cursor_colors(int fg, int bg, struct fb_info *info)
{
	struct i810fb_par *par = info->par;
	u8 __iomem *mmio = par->mmio_start_virtual;
	u8 red, green, blue, trans, temp;

	i810fb_getcolreg(bg, &red, &green, &blue, &trans, info);

	temp = i810_readb(PIXCONF1, mmio);
	i810_writeb(PIXCONF1, mmio, temp | EXTENDED_PALETTE);

	i810_write_dac(4, red, green, blue, mmio);

	i810_writeb(PIXCONF1, mmio, temp);

	i810fb_getcolreg(fg, &red, &green, &blue, &trans, info);
	temp = i810_readb(PIXCONF1, mmio);
	i810_writeb(PIXCONF1, mmio, temp | EXTENDED_PALETTE);

	i810_write_dac(5, red, green, blue, mmio);

	i810_writeb(PIXCONF1, mmio, temp);
}

/**
 * i810_init_cursor - initializes the cursor
 * @par: pointer to i810fb_par structure
 *
 * DESCRIPTION:
 * Initializes the cursor registers
 */
static void i810_init_cursor(struct i810fb_par *par)
{
	u8 __iomem *mmio = par->mmio_start_virtual;

	i810_enable_cursor(mmio, OFF);
	i810_writel(CURBASE, mmio, par->cursor_heap.physical);
	i810_writew(CURCNTR, mmio, COORD_ACTIVE | CURSOR_MODE_64_XOR);
}	

/*********************************************************************
 *                    Framebuffer hook helpers                       *
 *********************************************************************/
/**
 * i810_round_off -  Round off values to capability of hardware
 * @var: pointer to fb_var_screeninfo structure
 *
 * DESCRIPTION:
 * @var contains user-defined information for the mode to be set.
 * This will try modify those values to ones nearest the
 * capability of the hardware
 */
static void i810_round_off(struct fb_var_screeninfo *var)
{
	u32 xres, yres, vxres, vyres;

	/*
	 *  Presently supports only these configurations 
	 */

	xres = var->xres;
	yres = var->yres;
	vxres = var->xres_virtual;
	vyres = var->yres_virtual;

	var->bits_per_pixel += 7;
	var->bits_per_pixel &= ~7;
	
	if (var->bits_per_pixel < 8)
		var->bits_per_pixel = 8;
	if (var->bits_per_pixel > 32) 
		var->bits_per_pixel = 32;

	round_off_xres(&xres);
	if (xres < 40)
		xres = 40;
	if (xres > 2048) 
		xres = 2048;
	xres = (xres + 7) & ~7;

	if (vxres < xres) 
		vxres = xres;

	round_off_yres(&xres, &yres);
	if (yres < 1)
		yres = 1;
	if (yres >= 2048)
		yres = 2048;

	if (vyres < yres) 
		vyres = yres;

	if (var->bits_per_pixel == 32)
		var->accel_flags = 0;

	/* round of horizontal timings to nearest 8 pixels */
	var->left_margin = (var->left_margin + 4) & ~7;
	var->right_margin = (var->right_margin + 4) & ~7;
	var->hsync_len = (var->hsync_len + 4) & ~7;

	if (var->vmode & FB_VMODE_INTERLACED) {
		if (!((yres + var->upper_margin + var->vsync_len + 
		       var->lower_margin) & 1))
			var->upper_margin++;
	}
	
	var->xres = xres;
	var->yres = yres;
	var->xres_virtual = vxres;
	var->yres_virtual = vyres;
}	

/**
 * set_color_bitfields - sets rgba fields
 * @var: pointer to fb_var_screeninfo
 *
 * DESCRIPTION:
 * The length, offset and ordering  for each color field 
 * (red, green, blue)  will be set as specified 
 * by the hardware
 */  
static void set_color_bitfields(struct fb_var_screeninfo *var)
{
	switch (var->bits_per_pixel) {
	case 8:       
		var->red.offset = 0;
		var->red.length = 8;
		var->green.offset = 0;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 0;
		var->transp.length = 0;
		break;
	case 16:
		var->green.length = (var->green.length == 5) ? 5 : 6;
		var->red.length = 5;
		var->blue.length = 5;
		var->transp.length = 6 - var->green.length;
		var->blue.offset = 0;
		var->green.offset = 5;
		var->red.offset = 5 + var->green.length;
		var->transp.offset =  (5 + var->red.offset) & 15;
		break;
	case 24:	/* RGB 888   */
	case 32:	/* RGBA 8888 */
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.length = var->bits_per_pixel - 24;
		var->transp.offset = (var->transp.length) ? 24 : 0;
		break;
	}
	var->red.msb_right = 0;
	var->green.msb_right = 0;
	var->blue.msb_right = 0;
	var->transp.msb_right = 0;
}

/**
 * i810_check_params - check if contents in var are valid
 * @var: pointer to fb_var_screeninfo
 * @info: pointer to fb_info
 *
 * DESCRIPTION:
 * This will check if the framebuffer size is sufficient 
 * for the current mode and if the user's monitor has the 
 * required specifications to display the current mode.
 */
static int i810_check_params(struct fb_var_screeninfo *var, 
			     struct fb_info *info)
{
	struct i810fb_par *par = info->par;
	int line_length, vidmem, mode_valid = 0, retval = 0;
	u32 vyres = var->yres_virtual, vxres = var->xres_virtual;

	/*
	 *  Memory limit
	 */
	line_length = get_line_length(par, vxres, var->bits_per_pixel);
	vidmem = line_length*vyres;

	if (vidmem > par->fb.size) {
		vyres = par->fb.size/line_length;
		if (vyres < var->yres) {
			vyres = info->var.yres;
			vxres = par->fb.size/vyres;
			vxres /= var->bits_per_pixel >> 3;
			line_length = get_line_length(par, vxres, 
						      var->bits_per_pixel);
			vidmem = line_length * info->var.yres;
			if (vxres < var->xres) {
				printk("i810fb: required video memory, "
				       "%d bytes, for %dx%d-%d (virtual) "
				       "is out of range\n", 
				       vidmem, vxres, vyres, 
				       var->bits_per_pixel);
				return -ENOMEM;
			}
		}
	}

	var->xres_virtual = vxres;
	var->yres_virtual = vyres;

	/*
	 * Monitor limit
	 */
	switch (var->bits_per_pixel) {
	case 8:
		info->monspecs.dclkmax = 234000000;
		break;
	case 16:
		info->monspecs.dclkmax = 229000000;
		break;
	case 24:
	case 32:
		info->monspecs.dclkmax = 204000000;
		break;
	}

	info->monspecs.dclkmin = 15000000;

	if (!fb_validate_mode(var, info))
		mode_valid = 1;

#ifdef CONFIG_FB_I810_I2C
	if (!mode_valid && info->monspecs.gtf &&
	    !fb_get_mode(FB_MAXTIMINGS, 0, var, info))
		mode_valid = 1;

	if (!mode_valid && info->monspecs.modedb_len) {
		const struct fb_videomode *mode;

		mode = fb_find_best_mode(var, &info->modelist);
		if (mode) {
			fb_videomode_to_var(var, mode);
			mode_valid = 1;
		}
	}
#endif
	if (!mode_valid && info->monspecs.modedb_len == 0) {
		if (fb_get_mode(FB_MAXTIMINGS, 0, var, info)) {
			int default_sync = (info->monspecs.hfmin-HFMIN)
				|(info->monspecs.hfmax-HFMAX)
				|(info->monspecs.vfmin-VFMIN)
				|(info->monspecs.vfmax-VFMAX);
			printk("i810fb: invalid video mode%s\n",
			       default_sync ? "" : ". Specifying "
			       "vsyncN/hsyncN parameters may help");
			retval = -EINVAL;
		}
	}

	return retval;
}	

/**
 * encode_fix - fill up fb_fix_screeninfo structure
 * @fix: pointer to fb_fix_screeninfo
 * @info: pointer to fb_info
 *
 * DESCRIPTION:
 * This will set up parameters that are unmodifiable by the user.
 */
static int encode_fix(struct fb_fix_screeninfo *fix, struct fb_info *info)
{
	struct i810fb_par *par = info->par;

    	memset(fix, 0, sizeof(struct fb_fix_screeninfo));

    	strcpy(fix->id, "I810");
	mutex_lock(&info->mm_lock);
    	fix->smem_start = par->fb.physical;
    	fix->smem_len = par->fb.size;
	mutex_unlock(&info->mm_lock);
    	fix->type = FB_TYPE_PACKED_PIXELS;
    	fix->type_aux = 0;
	fix->xpanstep = 8;
	fix->ypanstep = 1;

    	switch (info->var.bits_per_pixel) {
	case 8:
	    	fix->visual = FB_VISUAL_PSEUDOCOLOR;
	    	break;
	case 16:
	case 24:
	case 32:
		if (info->var.nonstd)
			fix->visual = FB_VISUAL_DIRECTCOLOR;
		else
			fix->visual = FB_VISUAL_TRUECOLOR;
	    	break;
	default:
		return -EINVAL;
	}
    	fix->ywrapstep = 0;
	fix->line_length = par->pitch;
	fix->mmio_start = par->mmio_start_phys;
	fix->mmio_len = MMIO_SIZE;
	fix->accel = FB_ACCEL_I810;

	return 0;
}

/**
 * decode_var - modify par according to contents of var
 * @var: pointer to fb_var_screeninfo
 * @par: pointer to i810fb_par
 *
 * DESCRIPTION:
 * Based on the contents of @var, @par will be dynamically filled up.
 * @par contains all information necessary to modify the hardware. 
*/
static void decode_var(const struct fb_var_screeninfo *var, 
		       struct i810fb_par *par)
{
	u32 xres, yres, vxres, vyres;

	xres = var->xres;
	yres = var->yres;
	vxres = var->xres_virtual;
	vyres = var->yres_virtual;

	switch (var->bits_per_pixel) {
	case 8:
		par->pixconf = PIXCONF8;
		par->bltcntl = 0;
		par->depth = 1;
		par->blit_bpp = BPP8;
		break;
	case 16:
		if (var->green.length == 5)
			par->pixconf = PIXCONF15;
		else
			par->pixconf = PIXCONF16;
		par->bltcntl = 16;
		par->depth = 2;
		par->blit_bpp = BPP16;
		break;
	case 24:
		par->pixconf = PIXCONF24;
		par->bltcntl = 32;
		par->depth = 3;
		par->blit_bpp = BPP24;
		break;
	case 32:
		par->pixconf = PIXCONF32;
		par->bltcntl = 0;
		par->depth = 4;
		par->blit_bpp = 3 << 24;
		break;
	}
	if (var->nonstd && var->bits_per_pixel != 8)
		par->pixconf |= 1 << 27;

	i810_calc_dclk(var->pixclock, &par->regs.M, 
		       &par->regs.N, &par->regs.P);
	i810fb_encode_registers(var, par, xres, yres);

	par->watermark = i810_get_watermark(var, par);
	par->pitch = get_line_length(par, vxres, var->bits_per_pixel);
}	

/**
 * i810fb_getcolreg - gets red, green and blue values of the hardware DAC
 * @regno: DAC index
 * @red: red
 * @green: green
 * @blue: blue
 * @transp: transparency (alpha)
 * @info: pointer to fb_info
 *
 * DESCRIPTION:
 * Gets the red, green and blue values of the hardware DAC as pointed by @regno
 * and writes them to @red, @green and @blue respectively
 */
static int i810fb_getcolreg(u8 regno, u8 *red, u8 *green, u8 *blue, 
			    u8 *transp, struct fb_info *info)
{
	struct i810fb_par *par = info->par;
	u8 __iomem *mmio = par->mmio_start_virtual;
	u8 temp;

	if (info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
		if ((info->var.green.length == 5 && regno > 31) ||
		    (info->var.green.length == 6 && regno > 63))
			return 1;
	}

	temp = i810_readb(PIXCONF1, mmio);
	i810_writeb(PIXCONF1, mmio, temp & ~EXTENDED_PALETTE);

	if (info->fix.visual == FB_VISUAL_DIRECTCOLOR && 
	    info->var.green.length == 5) 
		i810_read_dac(regno * 8, red, green, blue, mmio);

	else if (info->fix.visual == FB_VISUAL_DIRECTCOLOR && 
		 info->var.green.length == 6) {
		u8 tmp;

		i810_read_dac(regno * 8, red, &tmp, blue, mmio);
		i810_read_dac(regno * 4, &tmp, green, &tmp, mmio);
	}
	else 
		i810_read_dac(regno, red, green, blue, mmio);

    	*transp = 0;
	i810_writeb(PIXCONF1, mmio, temp);

    	return 0;
}

/****************************************************************** 
 *           Framebuffer device-specific hooks                    *
 ******************************************************************/

static int i810fb_open(struct fb_info *info, int user)
{
	struct i810fb_par *par = info->par;

	mutex_lock(&par->open_lock);
	if (par->use_count == 0) {
		memset(&par->state, 0, sizeof(struct vgastate));
		par->state.flags = VGA_SAVE_CMAP;
		par->state.vgabase = par->mmio_start_virtual;
		save_vga(&par->state);

		i810_save_vga_state(par);
	}

	par->use_count++;
	mutex_unlock(&par->open_lock);
	
	return 0;
}

static int i810fb_release(struct fb_info *info, int user)
{
	struct i810fb_par *par = info->par;

	mutex_lock(&par->open_lock);
	if (par->use_count == 0) {
		mutex_unlock(&par->open_lock);
		return -EINVAL;
	}

	if (par->use_count == 1) {
		i810_restore_vga_state(par);
		restore_vga(&par->state);
	}

	par->use_count--;
	mutex_unlock(&par->open_lock);
	
	return 0;
}


static int i810fb_setcolreg(unsigned regno, unsigned red, unsigned green, 
			    unsigned blue, unsigned transp, 
			    struct fb_info *info)
{
	struct i810fb_par *par = info->par;
	u8 __iomem *mmio = par->mmio_start_virtual;
	u8 temp;
	int i;

 	if (regno > 255) return 1;

	if (info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
		if ((info->var.green.length == 5 && regno > 31) ||
		    (info->var.green.length == 6 && regno > 63))
			return 1;
	}

	if (info->var.grayscale)
		red = green = blue = (19595 * red + 38470 * green +
				      7471 * blue) >> 16;

	temp = i810_readb(PIXCONF1, mmio);
	i810_writeb(PIXCONF1, mmio, temp & ~EXTENDED_PALETTE);

	if (info->fix.visual == FB_VISUAL_DIRECTCOLOR && 
	    info->var.green.length == 5) {
		for (i = 0; i < 8; i++) 
			i810_write_dac((u8) (regno * 8) + i, (u8) red, 
				       (u8) green, (u8) blue, mmio);
	} else if (info->fix.visual == FB_VISUAL_DIRECTCOLOR && 
		 info->var.green.length == 6) {
		u8 r, g, b;

		if (regno < 32) {
			for (i = 0; i < 8; i++) 
				i810_write_dac((u8) (regno * 8) + i,
					       (u8) red, (u8) green, 
					       (u8) blue, mmio);
		}
		i810_read_dac((u8) (regno*4), &r, &g, &b, mmio);
		for (i = 0; i < 4; i++) 
			i810_write_dac((u8) (regno*4) + i, r, (u8) green, 
				       b, mmio);
	} else if (info->fix.visual == FB_VISUAL_PSEUDOCOLOR) {
		i810_write_dac((u8) regno, (u8) red, (u8) green,
			       (u8) blue, mmio);
	}

	i810_writeb(PIXCONF1, mmio, temp);

	if (regno < 16) {
		switch (info->var.bits_per_pixel) {
		case 16:	
			if (info->fix.visual == FB_VISUAL_DIRECTCOLOR) {
				if (info->var.green.length == 5) 
					((u32 *)info->pseudo_palette)[regno] = 
						(regno << 10) | (regno << 5) |
						regno;
				else
					((u32 *)info->pseudo_palette)[regno] = 
						(regno << 11) | (regno << 5) |
						regno;
			} else {
				if (info->var.green.length == 5) {
					/* RGB 555 */
					((u32 *)info->pseudo_palette)[regno] = 
						((red & 0xf800) >> 1) |
						((green & 0xf800) >> 6) |
						((blue & 0xf800) >> 11);
				} else {
					/* RGB 565 */
					((u32 *)info->pseudo_palette)[regno] =
						(red & 0xf800) |
						((green & 0xf800) >> 5) |
						((blue & 0xf800) >> 11);
				}
			}
			break;
		case 24:	/* RGB 888 */
		case 32:	/* RGBA 8888 */
			if (info->fix.visual == FB_VISUAL_DIRECTCOLOR) 
				((u32 *)info->pseudo_palette)[regno] = 
					(regno << 16) | (regno << 8) |
					regno;
			else 
				((u32 *)info->pseudo_palette)[regno] = 
					((red & 0xff00) << 8) |
					(green & 0xff00) |
					((blue & 0xff00) >> 8);
			break;
		}
	}
	return 0;
}

static int i810fb_pan_display(struct fb_var_screeninfo *var, 
			      struct fb_info *info)
{
	struct i810fb_par *par = info->par;
	u32 total;
	
	total = var->xoffset * par->depth + 
		var->yoffset * info->fix.line_length;
	i810fb_load_front(total, info);

	return 0;
}

static int i810fb_blank (int blank_mode, struct fb_info *info)
{
	struct i810fb_par *par = info->par;
	u8 __iomem *mmio = par->mmio_start_virtual;
	int mode = 0, pwr, scr_off = 0;
	
	pwr = i810_readl(PWR_CLKC, mmio);

	switch (blank_mode) {
	case FB_BLANK_UNBLANK:
		mode = POWERON;
		pwr |= 1;
		scr_off = ON;
		break;
	case FB_BLANK_NORMAL:
		mode = POWERON;
		pwr |= 1;
		scr_off = OFF;
		break;
	case FB_BLANK_VSYNC_SUSPEND:
		mode = STANDBY;
		pwr |= 1;
		scr_off = OFF;
		break;
	case FB_BLANK_HSYNC_SUSPEND:
		mode = SUSPEND;
		pwr |= 1;
		scr_off = OFF;
		break;
	case FB_BLANK_POWERDOWN:
		mode = POWERDOWN;
		pwr &= ~1;
		scr_off = OFF;
		break;
	default:
		return -EINVAL; 
	}

	i810_screen_off(mmio, scr_off);
	i810_writel(HVSYNC, mmio, mode);
	i810_writel(PWR_CLKC, mmio, pwr);

	return 0;
}

static int i810fb_set_par(struct fb_info *info)
{
	struct i810fb_par *par = info->par;

	decode_var(&info->var, par);
	i810_load_regs(par);
	i810_init_cursor(par);
	encode_fix(&info->fix, info);

	if (info->var.accel_flags && !(par->dev_flags & LOCKUP)) {
		info->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_YPAN |
		FBINFO_HWACCEL_COPYAREA | FBINFO_HWACCEL_FILLRECT |
		FBINFO_HWACCEL_IMAGEBLIT;
		info->pixmap.scan_align = 2;
	} else {
		info->pixmap.scan_align = 1;
		info->flags = FBINFO_DEFAULT | FBINFO_HWACCEL_YPAN;
	}
	return 0;
}

static int i810fb_check_var(struct fb_var_screeninfo *var, 
			    struct fb_info *info)
{
	int err;

	if (IS_DVT) {
		var->vmode &= ~FB_VMODE_MASK;
		var->vmode |= FB_VMODE_NONINTERLACED;
	}
	if (var->vmode & FB_VMODE_DOUBLE) {
		var->vmode &= ~FB_VMODE_MASK;
		var->vmode |= FB_VMODE_NONINTERLACED;
	}

	i810_round_off(var);
	if ((err = i810_check_params(var, info)))
		return err;

	i810fb_fill_var_timings(var);
	set_color_bitfields(var);
	return 0;
}

static int i810fb_cursor(struct fb_info *info, struct fb_cursor *cursor)
{
	struct i810fb_par *par = info->par;
	u8 __iomem *mmio = par->mmio_start_virtual;

	if (par->dev_flags & LOCKUP)
		return -ENXIO;

	if (cursor->image.width > 64 || cursor->image.height > 64)
		return -ENXIO;

	if ((i810_readl(CURBASE, mmio) & 0xf) != par->cursor_heap.physical) {
		i810_init_cursor(par);
		cursor->set |= FB_CUR_SETALL;
	}

	i810_enable_cursor(mmio, OFF);

	if (cursor->set & FB_CUR_SETPOS) {
		u32 tmp;

		tmp = (cursor->image.dx - info->var.xoffset) & 0xffff;
		tmp |= (cursor->image.dy - info->var.yoffset) << 16;
		i810_writel(CURPOS, mmio, tmp);
	}

	if (cursor->set & FB_CUR_SETSIZE)
		i810_reset_cursor_image(par);

	if (cursor->set & FB_CUR_SETCMAP)
		i810_load_cursor_colors(cursor->image.fg_color,
					cursor->image.bg_color,
					info);

	if (cursor->set & (FB_CUR_SETSHAPE | FB_CUR_SETIMAGE)) {
		int size = ((cursor->image.width + 7) >> 3) *
			cursor->image.height;
		int i;
		u8 *data = kmalloc(64 * 8, GFP_ATOMIC);

		if (data == NULL)
			return -ENOMEM;

		switch (cursor->rop) {
		case ROP_XOR:
			for (i = 0; i < size; i++)
				data[i] = cursor->image.data[i] ^ cursor->mask[i];
			break;
		case ROP_COPY:
		default:
			for (i = 0; i < size; i++)
				data[i] = cursor->image.data[i] & cursor->mask[i];
			break;
		}

		i810_load_cursor_image(cursor->image.width,
				       cursor->image.height, data,
				       par);
		kfree(data);
	}

	if (cursor->enable)
		i810_enable_cursor(mmio, ON);

	return 0;
}

static const struct fb_ops i810fb_ops = {
	.owner =             THIS_MODULE,
	.fb_open =           i810fb_open,
	.fb_release =        i810fb_release,
	.fb_check_var =      i810fb_check_var,
	.fb_set_par =        i810fb_set_par,
	.fb_setcolreg =      i810fb_setcolreg,
	.fb_blank =          i810fb_blank,
	.fb_pan_display =    i810fb_pan_display, 
	.fb_fillrect =       i810fb_fillrect,
	.fb_copyarea =       i810fb_copyarea,
	.fb_imageblit =      i810fb_imageblit,
	.fb_cursor =         i810fb_cursor,
	.fb_sync =           i810fb_sync,
};

/***********************************************************************
 *                         Power Management                            *
 ***********************************************************************/
static int i810fb_suspend(struct pci_dev *dev, pm_message_t mesg)
{
	struct fb_info *info = pci_get_drvdata(dev);
	struct i810fb_par *par = info->par;

	par->cur_state = mesg.event;

	switch (mesg.event) {
	case PM_EVENT_FREEZE:
	case PM_EVENT_PRETHAW:
		dev->dev.power.power_state = mesg;
		return 0;
	}

	console_lock();
	fb_set_suspend(info, 1);

	if (info->fbops->fb_sync)
		info->fbops->fb_sync(info);

	i810fb_blank(FB_BLANK_POWERDOWN, info);
	agp_unbind_memory(par->i810_gtt.i810_fb_memory);
	agp_unbind_memory(par->i810_gtt.i810_cursor_memory);

	pci_save_state(dev);
	pci_disable_device(dev);
	pci_set_power_state(dev, pci_choose_state(dev, mesg));
	console_unlock();

	return 0;
}

static int i810fb_resume(struct pci_dev *dev) 
{
	struct fb_info *info = pci_get_drvdata(dev);
	struct i810fb_par *par = info->par;
	int cur_state = par->cur_state;

	par->cur_state = PM_EVENT_ON;

	if (cur_state == PM_EVENT_FREEZE) {
		pci_set_power_state(dev, PCI_D0);
		return 0;
	}

	console_lock();
	pci_set_power_state(dev, PCI_D0);
	pci_restore_state(dev);

	if (pci_enable_device(dev))
		goto fail;

	pci_set_master(dev);
	agp_bind_memory(par->i810_gtt.i810_fb_memory,
			par->fb.offset);
	agp_bind_memory(par->i810_gtt.i810_cursor_memory,
			par->cursor_heap.offset);
	i810fb_set_par(info);
	fb_set_suspend (info, 0);
	info->fbops->fb_blank(VESA_NO_BLANKING, info);
fail:
	console_unlock();
	return 0;
}
/***********************************************************************
 *                  AGP resource allocation                            *
 ***********************************************************************/
  
static void i810_fix_pointers(struct i810fb_par *par)
{
      	par->fb.physical = par->aperture.physical+(par->fb.offset << 12);
	par->fb.virtual = par->aperture.virtual+(par->fb.offset << 12);
	par->iring.physical = par->aperture.physical + 
		(par->iring.offset << 12);
	par->iring.virtual = par->aperture.virtual + 
		(par->iring.offset << 12);
	par->cursor_heap.virtual = par->aperture.virtual+
		(par->cursor_heap.offset << 12);
}

static void i810_fix_offsets(struct i810fb_par *par)
{
	if (vram + 1 > par->aperture.size >> 20)
		vram = (par->aperture.size >> 20) - 1;
	if (v_offset_default > (par->aperture.size >> 20))
		v_offset_default = (par->aperture.size >> 20);
	if (vram + v_offset_default + 1 > par->aperture.size >> 20)
		v_offset_default = (par->aperture.size >> 20) - (vram + 1);

	par->fb.size = vram << 20;
	par->fb.offset = v_offset_default << 20;
	par->fb.offset >>= 12;

	par->iring.offset = par->fb.offset + (par->fb.size >> 12);
	par->iring.size = RINGBUFFER_SIZE;

	par->cursor_heap.offset = par->iring.offset + (RINGBUFFER_SIZE >> 12);
	par->cursor_heap.size = 4096;
}

static int i810_alloc_agp_mem(struct fb_info *info)
{
	struct i810fb_par *par = info->par;
	int size;
	struct agp_bridge_data *bridge;
	
	i810_fix_offsets(par);
	size = par->fb.size + par->iring.size;

	if (!(bridge = agp_backend_acquire(par->dev))) {
		printk("i810fb_alloc_fbmem: cannot acquire agpgart\n");
		return -ENODEV;
	}
	if (!(par->i810_gtt.i810_fb_memory = 
	      agp_allocate_memory(bridge, size >> 12, AGP_NORMAL_MEMORY))) {
		printk("i810fb_alloc_fbmem: can't allocate framebuffer "
		       "memory\n");
		agp_backend_release(bridge);
		return -ENOMEM;
	}
	if (agp_bind_memory(par->i810_gtt.i810_fb_memory,
			    par->fb.offset)) {
		printk("i810fb_alloc_fbmem: can't bind framebuffer memory\n");
		agp_backend_release(bridge);
		return -EBUSY;
	}	
	
	if (!(par->i810_gtt.i810_cursor_memory = 
	      agp_allocate_memory(bridge, par->cursor_heap.size >> 12,
				  AGP_PHYSICAL_MEMORY))) {
		printk("i810fb_alloc_cursormem:  can't allocate "
		       "cursor memory\n");
		agp_backend_release(bridge);
		return -ENOMEM;
	}
	if (agp_bind_memory(par->i810_gtt.i810_cursor_memory,
			    par->cursor_heap.offset)) {
		printk("i810fb_alloc_cursormem: cannot bind cursor memory\n");
		agp_backend_release(bridge);
		return -EBUSY;
	}	

	par->cursor_heap.physical = par->i810_gtt.i810_cursor_memory->physical;

	i810_fix_pointers(par);

	agp_backend_release(bridge);

	return 0;
}

/*************************************************************** 
 *                    Initialization                           * 
 ***************************************************************/

/**
 * i810_init_monspecs
 * @info: pointer to device specific info structure
 *
 * DESCRIPTION:
 * Sets the user monitor's horizontal and vertical
 * frequency limits
 */
static void i810_init_monspecs(struct fb_info *info)
{
	if (!hsync1)
		hsync1 = HFMIN;
	if (!hsync2) 
		hsync2 = HFMAX;
	if (!info->monspecs.hfmax)
		info->monspecs.hfmax = hsync2;
	if (!info->monspecs.hfmin)
		info->monspecs.hfmin = hsync1;
	if (hsync2 < hsync1)
		info->monspecs.hfmin = hsync2;

	if (!vsync1)
		vsync1 = VFMIN;
	if (!vsync2) 
		vsync2 = VFMAX;
	if (IS_DVT && vsync1 < 60)
		vsync1 = 60;
	if (!info->monspecs.vfmax)
		info->monspecs.vfmax = vsync2;
	if (!info->monspecs.vfmin)
		info->monspecs.vfmin = vsync1;
	if (vsync2 < vsync1) 
		info->monspecs.vfmin = vsync2;
}

/**
 * i810_init_defaults - initializes default values to use
 * @par: pointer to i810fb_par structure
 * @info: pointer to current fb_info structure
 */
static void i810_init_defaults(struct i810fb_par *par, struct fb_info *info)
{
	mutex_init(&par->open_lock);

	if (voffset) 
		v_offset_default = voffset;
	else if (par->aperture.size > 32 * 1024 * 1024)
		v_offset_default = 16;
	else
		v_offset_default = 8;

	if (!vram) 
		vram = 1;

	if (accel) 
		par->dev_flags |= HAS_ACCELERATION;

	if (sync) 
		par->dev_flags |= ALWAYS_SYNC;

	par->ddc_num = (ddc3 ? 3 : 2);

	if (bpp < 8)
		bpp = 8;
	
	par->i810fb_ops = i810fb_ops;

	if (xres)
		info->var.xres = xres;
	else
		info->var.xres = 640;

	if (yres)
		info->var.yres = yres;
	else
		info->var.yres = 480;

	if (!vyres) 
		vyres = (vram << 20)/(info->var.xres*bpp >> 3);

	info->var.yres_virtual = vyres;
	info->var.bits_per_pixel = bpp;

	if (dcolor)
		info->var.nonstd = 1;

	if (par->dev_flags & HAS_ACCELERATION) 
		info->var.accel_flags = 1;

	i810_init_monspecs(info);
}
	
/**
 * i810_init_device - initialize device
 * @par: pointer to i810fb_par structure
 */
static void i810_init_device(struct i810fb_par *par)
{
	u8 reg;
	u8 __iomem *mmio = par->mmio_start_virtual;

	if (mtrr)
		par->wc_cookie= arch_phys_wc_add((u32) par->aperture.physical,
						 par->aperture.size);

	i810_init_cursor(par);

	/* mvo: enable external vga-connector (for laptops) */
	if (extvga) {
		i810_writel(HVSYNC, mmio, 0);
		i810_writel(PWR_CLKC, mmio, 3);
	}

	pci_read_config_byte(par->dev, 0x50, &reg);
	reg &= FREQ_MASK;
	par->mem_freq = (reg) ? 133 : 100;

}

static int i810_allocate_pci_resource(struct i810fb_par *par,
				      const struct pci_device_id *entry)
{
	int err;

	if ((err = pci_enable_device(par->dev))) { 
		printk("i810fb_init: cannot enable device\n");
		return err;		
	}
	par->res_flags |= PCI_DEVICE_ENABLED;

	if (pci_resource_len(par->dev, 0) > 512 * 1024) {
		par->aperture.physical = pci_resource_start(par->dev, 0);
		par->aperture.size = pci_resource_len(par->dev, 0);
		par->mmio_start_phys = pci_resource_start(par->dev, 1);
	} else {
		par->aperture.physical = pci_resource_start(par->dev, 1);
		par->aperture.size = pci_resource_len(par->dev, 1);
		par->mmio_start_phys = pci_resource_start(par->dev, 0);
	}
	if (!par->aperture.size) {
		printk("i810fb_init: device is disabled\n");
		return -ENOMEM;
	}

	if (!request_mem_region(par->aperture.physical, 
				par->aperture.size, 
				i810_pci_list[entry->driver_data])) {
		printk("i810fb_init: cannot request framebuffer region\n");
		return -ENODEV;
	}
	par->res_flags |= FRAMEBUFFER_REQ;

	par->aperture.virtual = ioremap_wc(par->aperture.physical,
					   par->aperture.size);
	if (!par->aperture.virtual) {
		printk("i810fb_init: cannot remap framebuffer region\n");
		return -ENODEV;
	}
  
	if (!request_mem_region(par->mmio_start_phys, 
				MMIO_SIZE, 
				i810_pci_list[entry->driver_data])) {
		printk("i810fb_init: cannot request mmio region\n");
		return -ENODEV;
	}
	par->res_flags |= MMIO_REQ;

	par->mmio_start_virtual = ioremap_nocache(par->mmio_start_phys, 
						  MMIO_SIZE);
	if (!par->mmio_start_virtual) {
		printk("i810fb_init: cannot remap mmio region\n");
		return -ENODEV;
	}

	return 0;
}

static void i810fb_find_init_mode(struct fb_info *info)
{
	struct fb_videomode mode;
	struct fb_var_screeninfo var;
	struct fb_monspecs *specs = &info->monspecs;
	int found = 0;
#ifdef CONFIG_FB_I810_I2C
	int i;
	int err = 1;
	struct i810fb_par *par = info->par;
#endif

	INIT_LIST_HEAD(&info->modelist);
	memset(&mode, 0, sizeof(struct fb_videomode));
	var = info->var;
#ifdef CONFIG_FB_I810_I2C
	i810_create_i2c_busses(par);

	for (i = 0; i < par->ddc_num + 1; i++) {
		err = i810_probe_i2c_connector(info, &par->edid, i);
		if (!err)
			break;
	}

	if (!err)
		printk("i810fb_init_pci: DDC probe successful\n");

	fb_edid_to_monspecs(par->edid, specs);

	if (specs->modedb == NULL)
		printk("i810fb_init_pci: Unable to get Mode Database\n");

	fb_videomode_to_modelist(specs->modedb, specs->modedb_len,
				 &info->modelist);
	if (specs->modedb != NULL) {
		const struct fb_videomode *m;

		if (xres && yres) {
			if ((m = fb_find_best_mode(&var, &info->modelist))) {
				mode = *m;
				found  = 1;
			}
		}

		if (!found) {
			m = fb_find_best_display(&info->monspecs, &info->modelist);
			mode = *m;
			found = 1;
		}

		fb_videomode_to_var(&var, &mode);
	}
#endif
	if (mode_option)
		fb_find_mode(&var, info, mode_option, specs->modedb,
			     specs->modedb_len, (found) ? &mode : NULL,
			     info->var.bits_per_pixel);

	info->var = var;
	fb_destroy_modedb(specs->modedb);
	specs->modedb = NULL;
}

#ifndef MODULE
static int i810fb_setup(char *options)
{
	char *this_opt, *suffix = NULL;

	if (!options || !*options)
		return 0;
	
	while ((this_opt = strsep(&options, ",")) != NULL) {
		if (!strncmp(this_opt, "mtrr", 4))
			mtrr = 1;
		else if (!strncmp(this_opt, "accel", 5))
			accel = 1;
		else if (!strncmp(this_opt, "extvga", 6))
			extvga = 1;
		else if (!strncmp(this_opt, "sync", 4))
			sync = 1;
		else if (!strncmp(this_opt, "vram:", 5))
			vram = (simple_strtoul(this_opt+5, NULL, 0));
		else if (!strncmp(this_opt, "voffset:", 8))
			voffset = (simple_strtoul(this_opt+8, NULL, 0));
		else if (!strncmp(this_opt, "xres:", 5))
			xres = simple_strtoul(this_opt+5, NULL, 0);
		else if (!strncmp(this_opt, "yres:", 5))
			yres = simple_strtoul(this_opt+5, NULL, 0);
		else if (!strncmp(this_opt, "vyres:", 6))
			vyres = simple_strtoul(this_opt+6, NULL, 0);
		else if (!strncmp(this_opt, "bpp:", 4))
			bpp = simple_strtoul(this_opt+4, NULL, 0);
		else if (!strncmp(this_opt, "hsync1:", 7)) {
			hsync1 = simple_strtoul(this_opt+7, &suffix, 0);
			if (strncmp(suffix, "H", 1)) 
				hsync1 *= 1000;
		} else if (!strncmp(this_opt, "hsync2:", 7)) {
			hsync2 = simple_strtoul(this_opt+7, &suffix, 0);
			if (strncmp(suffix, "H", 1)) 
				hsync2 *= 1000;
		} else if (!strncmp(this_opt, "vsync1:", 7)) 
			vsync1 = simple_strtoul(this_opt+7, NULL, 0);
		else if (!strncmp(this_opt, "vsync2:", 7))
			vsync2 = simple_strtoul(this_opt+7, NULL, 0);
		else if (!strncmp(this_opt, "dcolor", 6))
			dcolor = 1;
		else if (!strncmp(this_opt, "ddc3", 4))
			ddc3 = true;
		else
			mode_option = this_opt;
	}
	return 0;
}
#endif

static int i810fb_init_pci(struct pci_dev *dev,
			   const struct pci_device_id *entry)
{
	struct fb_info    *info;
	struct i810fb_par *par = NULL;
	struct fb_videomode mode;
	int err = -1, vfreq, hfreq, pixclock;

	info = framebuffer_alloc(sizeof(struct i810fb_par), &dev->dev);
	if (!info)
		return -ENOMEM;

	par = info->par;
	par->dev = dev;

	if (!(info->pixmap.addr = kzalloc(8*1024, GFP_KERNEL))) {
		i810fb_release_resource(info, par);
		return -ENOMEM;
	}
	info->pixmap.size = 8*1024;
	info->pixmap.buf_align = 8;
	info->pixmap.access_align = 32;
	info->pixmap.flags = FB_PIXMAP_SYSTEM;

	if ((err = i810_allocate_pci_resource(par, entry))) {
		i810fb_release_resource(info, par);
		return err;
	}

	i810_init_defaults(par, info);

	if ((err = i810_alloc_agp_mem(info))) {
		i810fb_release_resource(info, par);
		return err;
	}

	i810_init_device(par);        

	info->screen_base = par->fb.virtual;
	info->fbops = &par->i810fb_ops;
	info->pseudo_palette = par->pseudo_palette;
	fb_alloc_cmap(&info->cmap, 256, 0);
	i810fb_find_init_mode(info);

	if ((err = info->fbops->fb_check_var(&info->var, info))) {
		i810fb_release_resource(info, par);
		return err;
	}

	fb_var_to_videomode(&mode, &info->var);
	fb_add_videomode(&mode, &info->modelist);

	i810fb_init_ringbuffer(info);
	err = register_framebuffer(info);

	if (err < 0) {
    		i810fb_release_resource(info, par); 
		printk("i810fb_init: cannot register framebuffer device\n");
    		return err;  
    	}   

	pci_set_drvdata(dev, info);
	pixclock = 1000000000/(info->var.pixclock);
	pixclock *= 1000;
	hfreq = pixclock/(info->var.xres + info->var.left_margin + 
			  info->var.hsync_len + info->var.right_margin);
	vfreq = hfreq/(info->var.yres + info->var.upper_margin +
		       info->var.vsync_len + info->var.lower_margin);

      	printk("I810FB: fb%d         : %s v%d.%d.%d%s\n"
      	       "I810FB: Video RAM   : %dK\n" 
	       "I810FB: Monitor     : H: %d-%d KHz V: %d-%d Hz\n"
	       "I810FB: Mode        : %dx%d-%dbpp@%dHz\n",
	       info->node,
	       i810_pci_list[entry->driver_data],
	       VERSION_MAJOR, VERSION_MINOR, VERSION_TEENIE, BRANCH_VERSION,
	       (int) par->fb.size>>10, info->monspecs.hfmin/1000,
	       info->monspecs.hfmax/1000, info->monspecs.vfmin,
	       info->monspecs.vfmax, info->var.xres, 
	       info->var.yres, info->var.bits_per_pixel, vfreq);
	return 0;
}

/***************************************************************
 *                     De-initialization                        *
 ***************************************************************/

static void i810fb_release_resource(struct fb_info *info, 
				    struct i810fb_par *par)
{
	struct gtt_data *gtt = &par->i810_gtt;
	arch_phys_wc_del(par->wc_cookie);

	i810_delete_i2c_busses(par);

	if (par->i810_gtt.i810_cursor_memory)
		agp_free_memory(gtt->i810_cursor_memory);
	if (par->i810_gtt.i810_fb_memory)
		agp_free_memory(gtt->i810_fb_memory);

	if (par->mmio_start_virtual)
		iounmap(par->mmio_start_virtual);
	if (par->aperture.virtual)
		iounmap(par->aperture.virtual);
	kfree(par->edid);
	if (par->res_flags & FRAMEBUFFER_REQ)
		release_mem_region(par->aperture.physical,
				   par->aperture.size);
	if (par->res_flags & MMIO_REQ)
		release_mem_region(par->mmio_start_phys, MMIO_SIZE);

	framebuffer_release(info);

}

static void i810fb_remove_pci(struct pci_dev *dev)
{
	struct fb_info *info = pci_get_drvdata(dev);
	struct i810fb_par *par = info->par;

	unregister_framebuffer(info);  
	i810fb_release_resource(info, par);
	printk("cleanup_module:  unloaded i810 framebuffer device\n");
}                                                	

#ifndef MODULE
static int i810fb_init(void)
{
	char *option = NULL;

	if (fb_get_options("i810fb", &option))
		return -ENODEV;
	i810fb_setup(option);

	return pci_register_driver(&i810fb_driver);
}
#endif 

/*********************************************************************
 *                          Modularization                           *
 *********************************************************************/

#ifdef MODULE

static int i810fb_init(void)
{
	hsync1 *= 1000;
	hsync2 *= 1000;

	return pci_register_driver(&i810fb_driver);
}

module_param(vram, int, 0);
MODULE_PARM_DESC(vram, "System RAM to allocate to framebuffer in MiB" 
		 " (default=4)");
module_param(voffset, int, 0);
MODULE_PARM_DESC(voffset, "at what offset to place start of framebuffer "
                 "memory (0 to maximum aperture size), in MiB (default = 48)");
module_param(bpp, int, 0);
MODULE_PARM_DESC(bpp, "Color depth for display in bits per pixel"
		 " (default = 8)");
module_param(xres, int, 0);
MODULE_PARM_DESC(xres, "Horizontal resolution in pixels (default = 640)");
module_param(yres, int, 0);
MODULE_PARM_DESC(yres, "Vertical resolution in scanlines (default = 480)");
module_param(vyres,int, 0);
MODULE_PARM_DESC(vyres, "Virtual vertical resolution in scanlines"
		 " (default = 480)");
module_param(hsync1, int, 0);
MODULE_PARM_DESC(hsync1, "Minimum horizontal frequency of monitor in KHz"
		 " (default = 29)");
module_param(hsync2, int, 0);
MODULE_PARM_DESC(hsync2, "Maximum horizontal frequency of monitor in KHz"
		 " (default = 30)");
module_param(vsync1, int, 0);
MODULE_PARM_DESC(vsync1, "Minimum vertical frequency of monitor in Hz"
		 " (default = 50)");
module_param(vsync2, int, 0);
MODULE_PARM_DESC(vsync2, "Maximum vertical frequency of monitor in Hz" 
		 " (default = 60)");
module_param(accel, bool, 0);
MODULE_PARM_DESC(accel, "Use Acceleration (BLIT) engine (default = 0)");
module_param(mtrr, bool, 0);
MODULE_PARM_DESC(mtrr, "Use MTRR (default = 0)");
module_param(extvga, bool, 0);
MODULE_PARM_DESC(extvga, "Enable external VGA connector (default = 0)");
module_param(sync, bool, 0);
MODULE_PARM_DESC(sync, "wait for accel engine to finish drawing"
		 " (default = 0)");
module_param(dcolor, bool, 0);
MODULE_PARM_DESC(dcolor, "use DirectColor visuals"
		 " (default = 0 = TrueColor)");
module_param(ddc3, bool, 0);
MODULE_PARM_DESC(ddc3, "Probe DDC bus 3 (default = 0 = no)");
module_param(mode_option, charp, 0);
MODULE_PARM_DESC(mode_option, "Specify initial video mode");

MODULE_AUTHOR("Tony A. Daplas");
MODULE_DESCRIPTION("Framebuffer device for the Intel 810/815 and"
		   " compatible cards");
MODULE_LICENSE("GPL"); 

static void __exit i810fb_exit(void)
{
	pci_unregister_driver(&i810fb_driver);
}
module_exit(i810fb_exit);

#endif /* MODULE */

module_init(i810fb_init);
