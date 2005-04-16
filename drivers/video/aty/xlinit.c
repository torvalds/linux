/*
 *  ATI Rage XL Initialization. Support for Xpert98 and Victoria
 *  PCI cards.
 *
 *  Copyright (C) 2002 MontaVista Software Inc.
 *  Author: MontaVista Software, Inc.
 *         	stevel@mvista.com or source@mvista.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h> 
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/io.h>
#include <video/mach64.h>
#include "atyfb.h"

#define MPLL_GAIN       0xad
#define VPLL_GAIN       0xd5

enum {
	VICTORIA = 0,
	XPERT98,
	NUM_XL_CARDS
};

extern const struct aty_pll_ops aty_pll_ct;

#define DEFAULT_CARD XPERT98
static int xl_card = DEFAULT_CARD;

static const struct xl_card_cfg_t {
	int ref_crystal; // 10^4 Hz
	int mem_type;
	int mem_size;
	u32 mem_cntl;
	u32 ext_mem_cntl;
	u32 mem_addr_config;
	u32 bus_cntl;
	u32 dac_cntl;
	u32 hw_debug;
	u32 custom_macro_cntl;
	u8  dll2_cntl;
	u8  pll_yclk_cntl;
} card_cfg[NUM_XL_CARDS] = {
	// VICTORIA
	{	2700, SDRAM, 0x800000,
		0x10757A3B, 0x64000C81, 0x00110202, 0x7b33A040,
		0x82010102, 0x48803800, 0x005E0179,
		0x50, 0x25
	},
	// XPERT98
	{	1432,  WRAM, 0x800000,
		0x00165A2B, 0xE0000CF1, 0x00200213, 0x7333A001,
		0x8000000A, 0x48833800, 0x007F0779,
		0x10, 0x19
	}
};
	  
typedef struct {
	u8 lcd_reg;
	u32 val;
} lcd_tbl_t;

static const lcd_tbl_t lcd_tbl[] = {
	{ 0x01,	0x000520C0 },
	{ 0x08,	0x02000408 },
	{ 0x03,	0x00000F00 },
	{ 0x00,	0x00000000 },
	{ 0x02,	0x00000000 },
	{ 0x04,	0x00000000 },
	{ 0x05,	0x00000000 },
	{ 0x06,	0x00000000 },
	{ 0x33,	0x00000000 },
	{ 0x34,	0x00000000 },
	{ 0x35,	0x00000000 },
	{ 0x36,	0x00000000 },
	{ 0x37,	0x00000000 }
};

static void reset_gui(struct atyfb_par *par)
{
	aty_st_8(GEN_TEST_CNTL+1, 0x01, par);
	aty_st_8(GEN_TEST_CNTL+1, 0x00, par);
	aty_st_8(GEN_TEST_CNTL+1, 0x02, par);
	mdelay(5);
}

static void reset_sdram(struct atyfb_par *par)
{
	u8 temp;

	temp = aty_ld_8(EXT_MEM_CNTL, par);
	temp |= 0x02;
	aty_st_8(EXT_MEM_CNTL, temp, par); // MEM_SDRAM_RESET = 1b
	temp |= 0x08;
	aty_st_8(EXT_MEM_CNTL, temp, par); // MEM_CYC_TEST    = 10b
	temp |= 0x0c;
	aty_st_8(EXT_MEM_CNTL, temp, par); // MEM_CYC_TEST    = 11b
	mdelay(5);
	temp &= 0xf3;
	aty_st_8(EXT_MEM_CNTL, temp, par); // MEM_CYC_TEST    = 00b
	temp &= 0xfd;
	aty_st_8(EXT_MEM_CNTL, temp, par); // MEM_SDRAM_REST  = 0b
	mdelay(5);
}

static void init_dll(struct atyfb_par *par)
{
	// enable DLL
	aty_st_pll_ct(PLL_GEN_CNTL,
		   aty_ld_pll_ct(PLL_GEN_CNTL, par) & 0x7f,
		   par);

	// reset DLL
	aty_st_pll_ct(DLL_CNTL, 0x82, par);
	aty_st_pll_ct(DLL_CNTL, 0xE2, par);
	mdelay(5);
	aty_st_pll_ct(DLL_CNTL, 0x82, par);
	mdelay(6);
}

static void reset_clocks(struct atyfb_par *par, struct pll_ct *pll,
			 int hsync_enb)
{
	reset_gui(par);
	aty_st_pll_ct(MCLK_FB_DIV, pll->mclk_fb_div, par);
	aty_st_pll_ct(SCLK_FB_DIV, pll->sclk_fb_div, par);

	mdelay(15);
	init_dll(par);
	aty_st_8(GEN_TEST_CNTL+1, 0x00, par);
	mdelay(5);
	aty_st_8(CRTC_GEN_CNTL+3, 0x04, par);
	mdelay(6);
	reset_sdram(par);
	aty_st_8(CRTC_GEN_CNTL+3,
		 hsync_enb ? 0x00 : 0x04, par);

	aty_st_pll_ct(SPLL_CNTL2, pll->spll_cntl2, par);
	aty_st_pll_ct(PLL_GEN_CNTL, pll->pll_gen_cntl, par);
	aty_st_pll_ct(PLL_VCLK_CNTL, pll->pll_vclk_cntl, par);
}

int atyfb_xl_init(struct fb_info *info)
{
	const struct xl_card_cfg_t * card = &card_cfg[xl_card];
	struct atyfb_par *par = (struct atyfb_par *) info->par;
	union aty_pll pll;
	int i, err;
	u32 temp;
	
	aty_st_8(CONFIG_STAT0, 0x85, par);
	mdelay(10);

	/*
	 * The following needs to be set before the call
	 * to var_to_pll() below. They'll be re-set again
	 * to the same values in aty_init().
	 */
	par->ref_clk_per = 100000000UL/card->ref_crystal;
	par->ram_type = card->mem_type;
	info->fix.smem_len = card->mem_size;
	if (xl_card == VICTORIA) {
		// the MCLK, XCLK are 120MHz on victoria card
		par->mclk_per = 1000000/120;
		par->xclk_per = 1000000/120;
		par->features &= ~M64F_MFB_FORCE_4;
	}
	
	/*
	 * Calculate mclk and xclk dividers, etc. The passed
	 * pixclock and bpp values don't matter yet, the vclk
	 * isn't programmed until later.
	 */
	if ((err = aty_pll_ct.var_to_pll(info, 39726, 8, &pll)))
		return err;

	aty_st_pll_ct(LVDS_CNTL0, 0x00, par);
	aty_st_pll_ct(DLL2_CNTL, card->dll2_cntl, par);
	aty_st_pll_ct(V2PLL_CNTL, 0x10, par);
	aty_st_pll_ct(MPLL_CNTL, MPLL_GAIN, par);
	aty_st_pll_ct(VPLL_CNTL, VPLL_GAIN, par);
	aty_st_pll_ct(PLL_VCLK_CNTL, 0x00, par);
	aty_st_pll_ct(VFC_CNTL, 0x1B, par);
	aty_st_pll_ct(PLL_REF_DIV, pll.ct.pll_ref_div, par);
	aty_st_pll_ct(PLL_EXT_CNTL, pll.ct.pll_ext_cntl, par);
	aty_st_pll_ct(SPLL_CNTL2, 0x03, par);
	aty_st_pll_ct(PLL_GEN_CNTL, 0x44, par);

	reset_clocks(par, &pll.ct, 0);
	mdelay(10);

	aty_st_pll_ct(VCLK_POST_DIV, 0x03, par);
	aty_st_pll_ct(VCLK0_FB_DIV, 0xDA, par);
	aty_st_pll_ct(VCLK_POST_DIV, 0x0F, par);
	aty_st_pll_ct(VCLK1_FB_DIV, 0xF5, par);
	aty_st_pll_ct(VCLK_POST_DIV, 0x3F, par);
	aty_st_pll_ct(PLL_EXT_CNTL, 0x40 | pll.ct.pll_ext_cntl, par);
	aty_st_pll_ct(VCLK2_FB_DIV, 0x00, par);
	aty_st_pll_ct(VCLK_POST_DIV, 0xFF, par);
	aty_st_pll_ct(PLL_EXT_CNTL, 0xC0 | pll.ct.pll_ext_cntl, par);
	aty_st_pll_ct(VCLK3_FB_DIV, 0x00, par);

	aty_st_8(BUS_CNTL, 0x01, par);
	aty_st_le32(BUS_CNTL, card->bus_cntl | 0x08000000, par);

	aty_st_le32(CRTC_GEN_CNTL, 0x04000200, par);
	aty_st_le16(CONFIG_STAT0, 0x0020, par);
	aty_st_le32(MEM_CNTL, 0x10151A33, par);
	aty_st_le32(EXT_MEM_CNTL, 0xE0000C01, par);
	aty_st_le16(CRTC_GEN_CNTL+2, 0x0000, par);
	aty_st_le32(DAC_CNTL, card->dac_cntl, par);
	aty_st_le16(GEN_TEST_CNTL, 0x0100, par);
	aty_st_le32(CUSTOM_MACRO_CNTL, 0x003C0171, par);
	aty_st_le32(MEM_BUF_CNTL, 0x00382848, par);

	aty_st_le32(HW_DEBUG, card->hw_debug, par);
	aty_st_le16(MEM_ADDR_CONFIG, 0x0000, par);
	aty_st_le16(GP_IO+2, 0x0000, par);
	aty_st_le16(GEN_TEST_CNTL, 0x0000, par);
	aty_st_le16(EXT_DAC_REGS+2, 0x0000, par);
	aty_st_le32(CRTC_INT_CNTL, 0x00000000, par);
	aty_st_le32(TIMER_CONFIG, 0x00000000, par);
	aty_st_le32(0xEC, 0x00000000, par);
	aty_st_le32(0xFC, 0x00000000, par);

	for (i=0; i<sizeof(lcd_tbl)/sizeof(lcd_tbl_t); i++) {
		aty_st_lcd(lcd_tbl[i].lcd_reg, lcd_tbl[i].val, par);
	}

	aty_st_le16(CONFIG_STAT0, 0x00A4, par);
	mdelay(10);

	aty_st_8(BUS_CNTL+1, 0xA0, par);
	mdelay(10);
	
	reset_clocks(par, &pll.ct, 1);
	mdelay(10);

	// something about power management
	aty_st_8(LCD_INDEX, 0x08, par);
	aty_st_8(LCD_DATA, 0x0A, par);
	aty_st_8(LCD_INDEX, 0x08, par);
	aty_st_8(LCD_DATA+3, 0x02, par);
	aty_st_8(LCD_INDEX, 0x08, par);
	aty_st_8(LCD_DATA, 0x0B, par);
	mdelay(2);
	
	// enable display requests, enable CRTC
	aty_st_8(CRTC_GEN_CNTL+3, 0x02, par);
	// disable display
	aty_st_8(CRTC_GEN_CNTL, 0x40, par);
	// disable display requests, disable CRTC
	aty_st_8(CRTC_GEN_CNTL+3, 0x04, par);
	mdelay(10);

	aty_st_pll_ct(PLL_YCLK_CNTL, 0x25, par);

	aty_st_le16(CUSTOM_MACRO_CNTL, 0x0179, par);
	aty_st_le16(CUSTOM_MACRO_CNTL+2, 0x005E, par);
	aty_st_le16(CUSTOM_MACRO_CNTL+2, card->custom_macro_cntl>>16, par);
	aty_st_8(CUSTOM_MACRO_CNTL+1,
		 (card->custom_macro_cntl>>8) & 0xff, par);

	aty_st_le32(MEM_ADDR_CONFIG, card->mem_addr_config, par);
	aty_st_le32(MEM_CNTL, card->mem_cntl, par);
	aty_st_le32(EXT_MEM_CNTL, card->ext_mem_cntl, par);

	aty_st_8(CONFIG_STAT0, 0xA0 | card->mem_type, par);

	aty_st_pll_ct(PLL_YCLK_CNTL, 0x01, par);
	mdelay(15);
	aty_st_pll_ct(PLL_YCLK_CNTL, card->pll_yclk_cntl, par);
	mdelay(1);
	
	reset_clocks(par, &pll.ct, 0);
	mdelay(50);
	reset_clocks(par, &pll.ct, 0);
	mdelay(50);

	// enable extended register block
	aty_st_8(BUS_CNTL+3, 0x7B, par);
	mdelay(1);
	// disable extended register block
	aty_st_8(BUS_CNTL+3, 0x73, par);

	aty_st_8(CONFIG_STAT0, 0x80 | card->mem_type, par);

	// disable display requests, disable CRTC
	aty_st_8(CRTC_GEN_CNTL+3, 0x04, par);
	// disable mapping registers in VGA aperture
	aty_st_8(CONFIG_CNTL, aty_ld_8(CONFIG_CNTL, par) & ~0x04, par);
	mdelay(50);
	// enable display requests, enable CRTC
	aty_st_8(CRTC_GEN_CNTL+3, 0x02, par);

	// make GPIO's 14,15,16 all inputs
	aty_st_8(LCD_INDEX, 0x07, par);
	aty_st_8(LCD_DATA+3, 0x00, par);

	// enable the display
	aty_st_8(CRTC_GEN_CNTL, 0x00, par);
	mdelay(17);
	// reset the memory controller
	aty_st_8(GEN_TEST_CNTL+1, 0x02, par);
	mdelay(15);
	aty_st_8(GEN_TEST_CNTL+1, 0x00, par);
	mdelay(30);

	// enable extended register block
	aty_st_8(BUS_CNTL+3,
		 (u8)(aty_ld_8(BUS_CNTL+3, par) | 0x08),
		 par);
	// set FIFO size to 512 (PIO)
	aty_st_le32(GUI_CNTL,
		    aty_ld_le32(GUI_CNTL, par) & ~0x3,
		    par);

	// enable CRT and disable lcd
	aty_st_8(LCD_INDEX, 0x01, par);
	temp = aty_ld_le32(LCD_DATA, par);
	temp = (temp | 0x01) & ~0x02;
	aty_st_le32(LCD_DATA, temp, par);
	return 0;
}

