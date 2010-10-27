/*
 * intelfb
 *
 * Linux framebuffer driver for Intel(R) 865G integrated graphics chips.
 *
 * Copyright © 2002, 2003 David Dawes <dawes@xfree86.org>
 *                   2004 Sylvain Meyer
 *
 * This driver consists of two parts.  The first part (intelfbdrv.c) provides
 * the basic fbdev interfaces, is derived in part from the radeonfb and
 * vesafb drivers, and is covered by the GPL.  The second part (intelfbhw.c)
 * provides the code to program the hardware.  Most of it is derived from
 * the i810/i830 XFree86 driver.  The HW-specific code is covered here
 * under a dual license (GPL and MIT/XFree86 license).
 *
 * Author: David Dawes
 *
 */

/* $DHD: intelfb/intelfbhw.c,v 1.9 2003/06/27 15:06:25 dawes Exp $ */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/interrupt.h>

#include <asm/io.h>

#include "intelfb.h"
#include "intelfbhw.h"

struct pll_min_max {
	int min_m, max_m, min_m1, max_m1;
	int min_m2, max_m2, min_n, max_n;
	int min_p, max_p, min_p1, max_p1;
	int min_vco, max_vco, p_transition_clk, ref_clk;
	int p_inc_lo, p_inc_hi;
};

#define PLLS_I8xx 0
#define PLLS_I9xx 1
#define PLLS_MAX 2

static struct pll_min_max plls[PLLS_MAX] = {
	{ 108, 140, 18, 26,
	  6, 16, 3, 16,
	  4, 128, 0, 31,
	  930000, 1400000, 165000, 48000,
	  4, 2 },		/* I8xx */

	{ 75, 120, 10, 20,
	  5, 9, 4, 7,
	  5, 80, 1, 8,
	  1400000, 2800000, 200000, 96000,
	  10, 5 }		/* I9xx */
};

int intelfbhw_get_chipset(struct pci_dev *pdev, struct intelfb_info *dinfo)
{
	u32 tmp;
	if (!pdev || !dinfo)
		return 1;

	switch (pdev->device) {
	case PCI_DEVICE_ID_INTEL_830M:
		dinfo->name = "Intel(R) 830M";
		dinfo->chipset = INTEL_830M;
		dinfo->mobile = 1;
		dinfo->pll_index = PLLS_I8xx;
		return 0;
	case PCI_DEVICE_ID_INTEL_845G:
		dinfo->name = "Intel(R) 845G";
		dinfo->chipset = INTEL_845G;
		dinfo->mobile = 0;
		dinfo->pll_index = PLLS_I8xx;
		return 0;
	case PCI_DEVICE_ID_INTEL_854:
		dinfo->mobile = 1;
		dinfo->name = "Intel(R) 854";
		dinfo->chipset = INTEL_854;
		return 0;
	case PCI_DEVICE_ID_INTEL_85XGM:
		tmp = 0;
		dinfo->mobile = 1;
		dinfo->pll_index = PLLS_I8xx;
		pci_read_config_dword(pdev, INTEL_85X_CAPID, &tmp);
		switch ((tmp >> INTEL_85X_VARIANT_SHIFT) &
			INTEL_85X_VARIANT_MASK) {
		case INTEL_VAR_855GME:
			dinfo->name = "Intel(R) 855GME";
			dinfo->chipset = INTEL_855GME;
			return 0;
		case INTEL_VAR_855GM:
			dinfo->name = "Intel(R) 855GM";
			dinfo->chipset = INTEL_855GM;
			return 0;
		case INTEL_VAR_852GME:
			dinfo->name = "Intel(R) 852GME";
			dinfo->chipset = INTEL_852GME;
			return 0;
		case INTEL_VAR_852GM:
			dinfo->name = "Intel(R) 852GM";
			dinfo->chipset = INTEL_852GM;
			return 0;
		default:
			dinfo->name = "Intel(R) 852GM/855GM";
			dinfo->chipset = INTEL_85XGM;
			return 0;
		}
		break;
	case PCI_DEVICE_ID_INTEL_865G:
		dinfo->name = "Intel(R) 865G";
		dinfo->chipset = INTEL_865G;
		dinfo->mobile = 0;
		dinfo->pll_index = PLLS_I8xx;
		return 0;
	case PCI_DEVICE_ID_INTEL_915G:
		dinfo->name = "Intel(R) 915G";
		dinfo->chipset = INTEL_915G;
		dinfo->mobile = 0;
		dinfo->pll_index = PLLS_I9xx;
		return 0;
	case PCI_DEVICE_ID_INTEL_915GM:
		dinfo->name = "Intel(R) 915GM";
		dinfo->chipset = INTEL_915GM;
		dinfo->mobile = 1;
		dinfo->pll_index = PLLS_I9xx;
		return 0;
	case PCI_DEVICE_ID_INTEL_945G:
		dinfo->name = "Intel(R) 945G";
		dinfo->chipset = INTEL_945G;
		dinfo->mobile = 0;
		dinfo->pll_index = PLLS_I9xx;
		return 0;
	case PCI_DEVICE_ID_INTEL_945GM:
		dinfo->name = "Intel(R) 945GM";
		dinfo->chipset = INTEL_945GM;
		dinfo->mobile = 1;
		dinfo->pll_index = PLLS_I9xx;
		return 0;
	case PCI_DEVICE_ID_INTEL_945GME:
		dinfo->name = "Intel(R) 945GME";
		dinfo->chipset = INTEL_945GME;
		dinfo->mobile = 1;
		dinfo->pll_index = PLLS_I9xx;
		return 0;
	case PCI_DEVICE_ID_INTEL_965G:
		dinfo->name = "Intel(R) 965G";
		dinfo->chipset = INTEL_965G;
		dinfo->mobile = 0;
		dinfo->pll_index = PLLS_I9xx;
		return 0;
	case PCI_DEVICE_ID_INTEL_965GM:
		dinfo->name = "Intel(R) 965GM";
		dinfo->chipset = INTEL_965GM;
		dinfo->mobile = 1;
		dinfo->pll_index = PLLS_I9xx;
		return 0;
	default:
		return 1;
	}
}

int intelfbhw_get_memory(struct pci_dev *pdev, int *aperture_size,
			 int *stolen_size)
{
	struct pci_dev *bridge_dev;
	u16 tmp;
	int stolen_overhead;

	if (!pdev || !aperture_size || !stolen_size)
		return 1;

	/* Find the bridge device.  It is always 0:0.0 */
	if (!(bridge_dev = pci_get_bus_and_slot(0, PCI_DEVFN(0, 0)))) {
		ERR_MSG("cannot find bridge device\n");
		return 1;
	}

	/* Get the fb aperture size and "stolen" memory amount. */
	tmp = 0;
	pci_read_config_word(bridge_dev, INTEL_GMCH_CTRL, &tmp);
	pci_dev_put(bridge_dev);

	switch (pdev->device) {
	case PCI_DEVICE_ID_INTEL_915G:
	case PCI_DEVICE_ID_INTEL_915GM:
	case PCI_DEVICE_ID_INTEL_945G:
	case PCI_DEVICE_ID_INTEL_945GM:
	case PCI_DEVICE_ID_INTEL_945GME:
	case PCI_DEVICE_ID_INTEL_965G:
	case PCI_DEVICE_ID_INTEL_965GM:
		/* 915, 945 and 965 chipsets support a 256MB aperture.
		   Aperture size is determined by inspected the
		   base address of the aperture. */
		if (pci_resource_start(pdev, 2) & 0x08000000)
			*aperture_size = MB(128);
		else
			*aperture_size = MB(256);
		break;
	default:
		if ((tmp & INTEL_GMCH_MEM_MASK) == INTEL_GMCH_MEM_64M)
			*aperture_size = MB(64);
		else
			*aperture_size = MB(128);
		break;
	}

	/* Stolen memory size is reduced by the GTT and the popup.
	   GTT is 1K per MB of aperture size, and popup is 4K. */
	stolen_overhead = (*aperture_size / MB(1)) + 4;
	switch(pdev->device) {
	case PCI_DEVICE_ID_INTEL_830M:
	case PCI_DEVICE_ID_INTEL_845G:
		switch (tmp & INTEL_830_GMCH_GMS_MASK) {
		case INTEL_830_GMCH_GMS_STOLEN_512:
			*stolen_size = KB(512) - KB(stolen_overhead);
			return 0;
		case INTEL_830_GMCH_GMS_STOLEN_1024:
			*stolen_size = MB(1) - KB(stolen_overhead);
			return 0;
		case INTEL_830_GMCH_GMS_STOLEN_8192:
			*stolen_size = MB(8) - KB(stolen_overhead);
			return 0;
		case INTEL_830_GMCH_GMS_LOCAL:
			ERR_MSG("only local memory found\n");
			return 1;
		case INTEL_830_GMCH_GMS_DISABLED:
			ERR_MSG("video memory is disabled\n");
			return 1;
		default:
			ERR_MSG("unexpected GMCH_GMS value: 0x%02x\n",
				tmp & INTEL_830_GMCH_GMS_MASK);
			return 1;
		}
		break;
	default:
		switch (tmp & INTEL_855_GMCH_GMS_MASK) {
		case INTEL_855_GMCH_GMS_STOLEN_1M:
			*stolen_size = MB(1) - KB(stolen_overhead);
			return 0;
		case INTEL_855_GMCH_GMS_STOLEN_4M:
			*stolen_size = MB(4) - KB(stolen_overhead);
			return 0;
		case INTEL_855_GMCH_GMS_STOLEN_8M:
			*stolen_size = MB(8) - KB(stolen_overhead);
			return 0;
		case INTEL_855_GMCH_GMS_STOLEN_16M:
			*stolen_size = MB(16) - KB(stolen_overhead);
			return 0;
		case INTEL_855_GMCH_GMS_STOLEN_32M:
			*stolen_size = MB(32) - KB(stolen_overhead);
			return 0;
		case INTEL_915G_GMCH_GMS_STOLEN_48M:
			*stolen_size = MB(48) - KB(stolen_overhead);
			return 0;
		case INTEL_915G_GMCH_GMS_STOLEN_64M:
			*stolen_size = MB(64) - KB(stolen_overhead);
			return 0;
		case INTEL_855_GMCH_GMS_DISABLED:
			ERR_MSG("video memory is disabled\n");
			return 0;
		default:
			ERR_MSG("unexpected GMCH_GMS value: 0x%02x\n",
				tmp & INTEL_855_GMCH_GMS_MASK);
			return 1;
		}
	}
}

int intelfbhw_check_non_crt(struct intelfb_info *dinfo)
{
	int dvo = 0;

	if (INREG(LVDS) & PORT_ENABLE)
		dvo |= LVDS_PORT;
	if (INREG(DVOA) & PORT_ENABLE)
		dvo |= DVOA_PORT;
	if (INREG(DVOB) & PORT_ENABLE)
		dvo |= DVOB_PORT;
	if (INREG(DVOC) & PORT_ENABLE)
		dvo |= DVOC_PORT;

	return dvo;
}

const char * intelfbhw_dvo_to_string(int dvo)
{
	if (dvo & DVOA_PORT)
		return "DVO port A";
	else if (dvo & DVOB_PORT)
		return "DVO port B";
	else if (dvo & DVOC_PORT)
		return "DVO port C";
	else if (dvo & LVDS_PORT)
		return "LVDS port";
	else
		return NULL;
}


int intelfbhw_validate_mode(struct intelfb_info *dinfo,
			    struct fb_var_screeninfo *var)
{
	int bytes_per_pixel;
	int tmp;

#if VERBOSE > 0
	DBG_MSG("intelfbhw_validate_mode\n");
#endif

	bytes_per_pixel = var->bits_per_pixel / 8;
	if (bytes_per_pixel == 3)
		bytes_per_pixel = 4;

	/* Check if enough video memory. */
	tmp = var->yres_virtual * var->xres_virtual * bytes_per_pixel;
	if (tmp > dinfo->fb.size) {
		WRN_MSG("Not enough video ram for mode "
			"(%d KByte vs %d KByte).\n",
			BtoKB(tmp), BtoKB(dinfo->fb.size));
		return 1;
	}

	/* Check if x/y limits are OK. */
	if (var->xres - 1 > HACTIVE_MASK) {
		WRN_MSG("X resolution too large (%d vs %d).\n",
			var->xres, HACTIVE_MASK + 1);
		return 1;
	}
	if (var->yres - 1 > VACTIVE_MASK) {
		WRN_MSG("Y resolution too large (%d vs %d).\n",
			var->yres, VACTIVE_MASK + 1);
		return 1;
	}
	if (var->xres < 4) {
		WRN_MSG("X resolution too small (%d vs 4).\n", var->xres);
		return 1;
	}
	if (var->yres < 4) {
		WRN_MSG("Y resolution too small (%d vs 4).\n", var->yres);
		return 1;
	}

	/* Check for doublescan modes. */
	if (var->vmode & FB_VMODE_DOUBLE) {
		WRN_MSG("Mode is double-scan.\n");
		return 1;
	}

	if ((var->vmode & FB_VMODE_INTERLACED) && (var->yres & 1)) {
		WRN_MSG("Odd number of lines in interlaced mode\n");
		return 1;
	}

	/* Check if clock is OK. */
	tmp = 1000000000 / var->pixclock;
	if (tmp < MIN_CLOCK) {
		WRN_MSG("Pixel clock is too low (%d MHz vs %d MHz).\n",
			(tmp + 500) / 1000, MIN_CLOCK / 1000);
		return 1;
	}
	if (tmp > MAX_CLOCK) {
		WRN_MSG("Pixel clock is too high (%d MHz vs %d MHz).\n",
			(tmp + 500) / 1000, MAX_CLOCK / 1000);
		return 1;
	}

	return 0;
}

int intelfbhw_pan_display(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct intelfb_info *dinfo = GET_DINFO(info);
	u32 offset, xoffset, yoffset;

#if VERBOSE > 0
	DBG_MSG("intelfbhw_pan_display\n");
#endif

	xoffset = ROUND_DOWN_TO(var->xoffset, 8);
	yoffset = var->yoffset;

	if ((xoffset + var->xres > var->xres_virtual) ||
	    (yoffset + var->yres > var->yres_virtual))
		return -EINVAL;

	offset = (yoffset * dinfo->pitch) +
		 (xoffset * var->bits_per_pixel) / 8;

	offset += dinfo->fb.offset << 12;

	dinfo->vsync.pan_offset = offset;
	if ((var->activate & FB_ACTIVATE_VBL) &&
	    !intelfbhw_enable_irq(dinfo))
		dinfo->vsync.pan_display = 1;
	else {
		dinfo->vsync.pan_display = 0;
		OUTREG(DSPABASE, offset);
	}

	return 0;
}

/* Blank the screen. */
void intelfbhw_do_blank(int blank, struct fb_info *info)
{
	struct intelfb_info *dinfo = GET_DINFO(info);
	u32 tmp;

#if VERBOSE > 0
	DBG_MSG("intelfbhw_do_blank: blank is %d\n", blank);
#endif

	/* Turn plane A on or off */
	tmp = INREG(DSPACNTR);
	if (blank)
		tmp &= ~DISPPLANE_PLANE_ENABLE;
	else
		tmp |= DISPPLANE_PLANE_ENABLE;
	OUTREG(DSPACNTR, tmp);
	/* Flush */
	tmp = INREG(DSPABASE);
	OUTREG(DSPABASE, tmp);

	/* Turn off/on the HW cursor */
#if VERBOSE > 0
	DBG_MSG("cursor_on is %d\n", dinfo->cursor_on);
#endif
	if (dinfo->cursor_on) {
		if (blank)
			intelfbhw_cursor_hide(dinfo);
		else
			intelfbhw_cursor_show(dinfo);
		dinfo->cursor_on = 1;
	}
	dinfo->cursor_blanked = blank;

	/* Set DPMS level */
	tmp = INREG(ADPA) & ~ADPA_DPMS_CONTROL_MASK;
	switch (blank) {
	case FB_BLANK_UNBLANK:
	case FB_BLANK_NORMAL:
		tmp |= ADPA_DPMS_D0;
		break;
	case FB_BLANK_VSYNC_SUSPEND:
		tmp |= ADPA_DPMS_D1;
		break;
	case FB_BLANK_HSYNC_SUSPEND:
		tmp |= ADPA_DPMS_D2;
		break;
	case FB_BLANK_POWERDOWN:
		tmp |= ADPA_DPMS_D3;
		break;
	}
	OUTREG(ADPA, tmp);

	return;
}


/* Check which pipe is connected to an active display plane. */
int intelfbhw_active_pipe(const struct intelfb_hwstate *hw)
{
	int pipe = -1;

	/* keep old default behaviour - prefer PIPE_A */
	if (hw->disp_b_ctrl & DISPPLANE_PLANE_ENABLE) {
		pipe = (hw->disp_b_ctrl >> DISPPLANE_SEL_PIPE_SHIFT);
		pipe &= PIPE_MASK;
		if (unlikely(pipe == PIPE_A))
			return PIPE_A;
	}
	if (hw->disp_a_ctrl & DISPPLANE_PLANE_ENABLE) {
		pipe = (hw->disp_a_ctrl >> DISPPLANE_SEL_PIPE_SHIFT);
		pipe &= PIPE_MASK;
		if (likely(pipe == PIPE_A))
			return PIPE_A;
	}
	/* Impossible that no pipe is selected - return PIPE_A */
	WARN_ON(pipe == -1);
	if (unlikely(pipe == -1))
		pipe = PIPE_A;

	return pipe;
}

void intelfbhw_setcolreg(struct intelfb_info *dinfo, unsigned regno,
			 unsigned red, unsigned green, unsigned blue,
			 unsigned transp)
{
	u32 palette_reg = (dinfo->pipe == PIPE_A) ?
			  PALETTE_A : PALETTE_B;

#if VERBOSE > 0
	DBG_MSG("intelfbhw_setcolreg: %d: (%d, %d, %d)\n",
		regno, red, green, blue);
#endif

	OUTREG(palette_reg + (regno << 2),
	       (red << PALETTE_8_RED_SHIFT) |
	       (green << PALETTE_8_GREEN_SHIFT) |
	       (blue << PALETTE_8_BLUE_SHIFT));
}


int intelfbhw_read_hw_state(struct intelfb_info *dinfo,
			    struct intelfb_hwstate *hw, int flag)
{
	int i;

#if VERBOSE > 0
	DBG_MSG("intelfbhw_read_hw_state\n");
#endif

	if (!hw || !dinfo)
		return -1;

	/* Read in as much of the HW state as possible. */
	hw->vga0_divisor = INREG(VGA0_DIVISOR);
	hw->vga1_divisor = INREG(VGA1_DIVISOR);
	hw->vga_pd = INREG(VGAPD);
	hw->dpll_a = INREG(DPLL_A);
	hw->dpll_b = INREG(DPLL_B);
	hw->fpa0 = INREG(FPA0);
	hw->fpa1 = INREG(FPA1);
	hw->fpb0 = INREG(FPB0);
	hw->fpb1 = INREG(FPB1);

	if (flag == 1)
		return flag;

#if 0
	/* This seems to be a problem with the 852GM/855GM */
	for (i = 0; i < PALETTE_8_ENTRIES; i++) {
		hw->palette_a[i] = INREG(PALETTE_A + (i << 2));
		hw->palette_b[i] = INREG(PALETTE_B + (i << 2));
	}
#endif

	if (flag == 2)
		return flag;

	hw->htotal_a = INREG(HTOTAL_A);
	hw->hblank_a = INREG(HBLANK_A);
	hw->hsync_a = INREG(HSYNC_A);
	hw->vtotal_a = INREG(VTOTAL_A);
	hw->vblank_a = INREG(VBLANK_A);
	hw->vsync_a = INREG(VSYNC_A);
	hw->src_size_a = INREG(SRC_SIZE_A);
	hw->bclrpat_a = INREG(BCLRPAT_A);
	hw->htotal_b = INREG(HTOTAL_B);
	hw->hblank_b = INREG(HBLANK_B);
	hw->hsync_b = INREG(HSYNC_B);
	hw->vtotal_b = INREG(VTOTAL_B);
	hw->vblank_b = INREG(VBLANK_B);
	hw->vsync_b = INREG(VSYNC_B);
	hw->src_size_b = INREG(SRC_SIZE_B);
	hw->bclrpat_b = INREG(BCLRPAT_B);

	if (flag == 3)
		return flag;

	hw->adpa = INREG(ADPA);
	hw->dvoa = INREG(DVOA);
	hw->dvob = INREG(DVOB);
	hw->dvoc = INREG(DVOC);
	hw->dvoa_srcdim = INREG(DVOA_SRCDIM);
	hw->dvob_srcdim = INREG(DVOB_SRCDIM);
	hw->dvoc_srcdim = INREG(DVOC_SRCDIM);
	hw->lvds = INREG(LVDS);

	if (flag == 4)
		return flag;

	hw->pipe_a_conf = INREG(PIPEACONF);
	hw->pipe_b_conf = INREG(PIPEBCONF);
	hw->disp_arb = INREG(DISPARB);

	if (flag == 5)
		return flag;

	hw->cursor_a_control = INREG(CURSOR_A_CONTROL);
	hw->cursor_b_control = INREG(CURSOR_B_CONTROL);
	hw->cursor_a_base = INREG(CURSOR_A_BASEADDR);
	hw->cursor_b_base = INREG(CURSOR_B_BASEADDR);

	if (flag == 6)
		return flag;

	for (i = 0; i < 4; i++) {
		hw->cursor_a_palette[i] = INREG(CURSOR_A_PALETTE0 + (i << 2));
		hw->cursor_b_palette[i] = INREG(CURSOR_B_PALETTE0 + (i << 2));
	}

	if (flag == 7)
		return flag;

	hw->cursor_size = INREG(CURSOR_SIZE);

	if (flag == 8)
		return flag;

	hw->disp_a_ctrl = INREG(DSPACNTR);
	hw->disp_b_ctrl = INREG(DSPBCNTR);
	hw->disp_a_base = INREG(DSPABASE);
	hw->disp_b_base = INREG(DSPBBASE);
	hw->disp_a_stride = INREG(DSPASTRIDE);
	hw->disp_b_stride = INREG(DSPBSTRIDE);

	if (flag == 9)
		return flag;

	hw->vgacntrl = INREG(VGACNTRL);

	if (flag == 10)
		return flag;

	hw->add_id = INREG(ADD_ID);

	if (flag == 11)
		return flag;

	for (i = 0; i < 7; i++) {
		hw->swf0x[i] = INREG(SWF00 + (i << 2));
		hw->swf1x[i] = INREG(SWF10 + (i << 2));
		if (i < 3)
			hw->swf3x[i] = INREG(SWF30 + (i << 2));
	}

	for (i = 0; i < 8; i++)
		hw->fence[i] = INREG(FENCE + (i << 2));

	hw->instpm = INREG(INSTPM);
	hw->mem_mode = INREG(MEM_MODE);
	hw->fw_blc_0 = INREG(FW_BLC_0);
	hw->fw_blc_1 = INREG(FW_BLC_1);

	hw->hwstam = INREG16(HWSTAM);
	hw->ier = INREG16(IER);
	hw->iir = INREG16(IIR);
	hw->imr = INREG16(IMR);

	return 0;
}


static int calc_vclock3(int index, int m, int n, int p)
{
	if (p == 0 || n == 0)
		return 0;
	return plls[index].ref_clk * m / n / p;
}

static int calc_vclock(int index, int m1, int m2, int n, int p1, int p2,
		       int lvds)
{
	struct pll_min_max *pll = &plls[index];
	u32 m, vco, p;

	m = (5 * (m1 + 2)) + (m2 + 2);
	n += 2;
	vco = pll->ref_clk * m / n;

	if (index == PLLS_I8xx)
		p = ((p1 + 2) * (1 << (p2 + 1)));
	else
		p = ((p1) * (p2 ? 5 : 10));
	return vco / p;
}

#if REGDUMP
static void intelfbhw_get_p1p2(struct intelfb_info *dinfo, int dpll,
			       int *o_p1, int *o_p2)
{
	int p1, p2;

	if (IS_I9XX(dinfo)) {
		if (dpll & DPLL_P1_FORCE_DIV2)
			p1 = 1;
		else
			p1 = (dpll >> DPLL_P1_SHIFT) & 0xff;

		p1 = ffs(p1);

		p2 = (dpll >> DPLL_I9XX_P2_SHIFT) & DPLL_P2_MASK;
	} else {
		if (dpll & DPLL_P1_FORCE_DIV2)
			p1 = 0;
		else
			p1 = (dpll >> DPLL_P1_SHIFT) & DPLL_P1_MASK;
		p2 = (dpll >> DPLL_P2_SHIFT) & DPLL_P2_MASK;
	}

	*o_p1 = p1;
	*o_p2 = p2;
}
#endif


void intelfbhw_print_hw_state(struct intelfb_info *dinfo,
			      struct intelfb_hwstate *hw)
{
#if REGDUMP
	int i, m1, m2, n, p1, p2;
	int index = dinfo->pll_index;
	DBG_MSG("intelfbhw_print_hw_state\n");

	if (!hw)
		return;
	/* Read in as much of the HW state as possible. */
	printk("hw state dump start\n");
	printk("	VGA0_DIVISOR:		0x%08x\n", hw->vga0_divisor);
	printk("	VGA1_DIVISOR:		0x%08x\n", hw->vga1_divisor);
	printk("	VGAPD:			0x%08x\n", hw->vga_pd);
	n = (hw->vga0_divisor >> FP_N_DIVISOR_SHIFT) & FP_DIVISOR_MASK;
	m1 = (hw->vga0_divisor >> FP_M1_DIVISOR_SHIFT) & FP_DIVISOR_MASK;
	m2 = (hw->vga0_divisor >> FP_M2_DIVISOR_SHIFT) & FP_DIVISOR_MASK;

	intelfbhw_get_p1p2(dinfo, hw->vga_pd, &p1, &p2);

	printk("	VGA0: (m1, m2, n, p1, p2) = (%d, %d, %d, %d, %d)\n",
	       m1, m2, n, p1, p2);
	printk("	VGA0: clock is %d\n",
	       calc_vclock(index, m1, m2, n, p1, p2, 0));

	n = (hw->vga1_divisor >> FP_N_DIVISOR_SHIFT) & FP_DIVISOR_MASK;
	m1 = (hw->vga1_divisor >> FP_M1_DIVISOR_SHIFT) & FP_DIVISOR_MASK;
	m2 = (hw->vga1_divisor >> FP_M2_DIVISOR_SHIFT) & FP_DIVISOR_MASK;

	intelfbhw_get_p1p2(dinfo, hw->vga_pd, &p1, &p2);
	printk("	VGA1: (m1, m2, n, p1, p2) = (%d, %d, %d, %d, %d)\n",
	       m1, m2, n, p1, p2);
	printk("	VGA1: clock is %d\n",
	       calc_vclock(index, m1, m2, n, p1, p2, 0));

	printk("	DPLL_A:			0x%08x\n", hw->dpll_a);
	printk("	DPLL_B:			0x%08x\n", hw->dpll_b);
	printk("	FPA0:			0x%08x\n", hw->fpa0);
	printk("	FPA1:			0x%08x\n", hw->fpa1);
	printk("	FPB0:			0x%08x\n", hw->fpb0);
	printk("	FPB1:			0x%08x\n", hw->fpb1);

	n = (hw->fpa0 >> FP_N_DIVISOR_SHIFT) & FP_DIVISOR_MASK;
	m1 = (hw->fpa0 >> FP_M1_DIVISOR_SHIFT) & FP_DIVISOR_MASK;
	m2 = (hw->fpa0 >> FP_M2_DIVISOR_SHIFT) & FP_DIVISOR_MASK;

	intelfbhw_get_p1p2(dinfo, hw->dpll_a, &p1, &p2);

	printk("	PLLA0: (m1, m2, n, p1, p2) = (%d, %d, %d, %d, %d)\n",
	       m1, m2, n, p1, p2);
	printk("	PLLA0: clock is %d\n",
	       calc_vclock(index, m1, m2, n, p1, p2, 0));

	n = (hw->fpa1 >> FP_N_DIVISOR_SHIFT) & FP_DIVISOR_MASK;
	m1 = (hw->fpa1 >> FP_M1_DIVISOR_SHIFT) & FP_DIVISOR_MASK;
	m2 = (hw->fpa1 >> FP_M2_DIVISOR_SHIFT) & FP_DIVISOR_MASK;

	intelfbhw_get_p1p2(dinfo, hw->dpll_a, &p1, &p2);

	printk("	PLLA1: (m1, m2, n, p1, p2) = (%d, %d, %d, %d, %d)\n",
	       m1, m2, n, p1, p2);
	printk("	PLLA1: clock is %d\n",
	       calc_vclock(index, m1, m2, n, p1, p2, 0));

#if 0
	printk("	PALETTE_A:\n");
	for (i = 0; i < PALETTE_8_ENTRIES)
		printk("	%3d:	0x%08x\n", i, hw->palette_a[i]);
	printk("	PALETTE_B:\n");
	for (i = 0; i < PALETTE_8_ENTRIES)
		printk("	%3d:	0x%08x\n", i, hw->palette_b[i]);
#endif

	printk("	HTOTAL_A:		0x%08x\n", hw->htotal_a);
	printk("	HBLANK_A:		0x%08x\n", hw->hblank_a);
	printk("	HSYNC_A:		0x%08x\n", hw->hsync_a);
	printk("	VTOTAL_A:		0x%08x\n", hw->vtotal_a);
	printk("	VBLANK_A:		0x%08x\n", hw->vblank_a);
	printk("	VSYNC_A:		0x%08x\n", hw->vsync_a);
	printk("	SRC_SIZE_A:		0x%08x\n", hw->src_size_a);
	printk("	BCLRPAT_A:		0x%08x\n", hw->bclrpat_a);
	printk("	HTOTAL_B:		0x%08x\n", hw->htotal_b);
	printk("	HBLANK_B:		0x%08x\n", hw->hblank_b);
	printk("	HSYNC_B:		0x%08x\n", hw->hsync_b);
	printk("	VTOTAL_B:		0x%08x\n", hw->vtotal_b);
	printk("	VBLANK_B:		0x%08x\n", hw->vblank_b);
	printk("	VSYNC_B:		0x%08x\n", hw->vsync_b);
	printk("	SRC_SIZE_B:		0x%08x\n", hw->src_size_b);
	printk("	BCLRPAT_B:		0x%08x\n", hw->bclrpat_b);

	printk("	ADPA:			0x%08x\n", hw->adpa);
	printk("	DVOA:			0x%08x\n", hw->dvoa);
	printk("	DVOB:			0x%08x\n", hw->dvob);
	printk("	DVOC:			0x%08x\n", hw->dvoc);
	printk("	DVOA_SRCDIM:		0x%08x\n", hw->dvoa_srcdim);
	printk("	DVOB_SRCDIM:		0x%08x\n", hw->dvob_srcdim);
	printk("	DVOC_SRCDIM:		0x%08x\n", hw->dvoc_srcdim);
	printk("	LVDS:			0x%08x\n", hw->lvds);

	printk("	PIPEACONF:		0x%08x\n", hw->pipe_a_conf);
	printk("	PIPEBCONF:		0x%08x\n", hw->pipe_b_conf);
	printk("	DISPARB:		0x%08x\n", hw->disp_arb);

	printk("	CURSOR_A_CONTROL:	0x%08x\n", hw->cursor_a_control);
	printk("	CURSOR_B_CONTROL:	0x%08x\n", hw->cursor_b_control);
	printk("	CURSOR_A_BASEADDR:	0x%08x\n", hw->cursor_a_base);
	printk("	CURSOR_B_BASEADDR:	0x%08x\n", hw->cursor_b_base);

	printk("	CURSOR_A_PALETTE:	");
	for (i = 0; i < 4; i++) {
		printk("0x%08x", hw->cursor_a_palette[i]);
		if (i < 3)
			printk(", ");
	}
	printk("\n");
	printk("	CURSOR_B_PALETTE:	");
	for (i = 0; i < 4; i++) {
		printk("0x%08x", hw->cursor_b_palette[i]);
		if (i < 3)
			printk(", ");
	}
	printk("\n");

	printk("	CURSOR_SIZE:		0x%08x\n", hw->cursor_size);

	printk("	DSPACNTR:		0x%08x\n", hw->disp_a_ctrl);
	printk("	DSPBCNTR:		0x%08x\n", hw->disp_b_ctrl);
	printk("	DSPABASE:		0x%08x\n", hw->disp_a_base);
	printk("	DSPBBASE:		0x%08x\n", hw->disp_b_base);
	printk("	DSPASTRIDE:		0x%08x\n", hw->disp_a_stride);
	printk("	DSPBSTRIDE:		0x%08x\n", hw->disp_b_stride);

	printk("	VGACNTRL:		0x%08x\n", hw->vgacntrl);
	printk("	ADD_ID:			0x%08x\n", hw->add_id);

	for (i = 0; i < 7; i++) {
		printk("	SWF0%d			0x%08x\n", i,
			hw->swf0x[i]);
	}
	for (i = 0; i < 7; i++) {
		printk("	SWF1%d			0x%08x\n", i,
			hw->swf1x[i]);
	}
	for (i = 0; i < 3; i++) {
		printk("	SWF3%d			0x%08x\n", i,
		       hw->swf3x[i]);
	}
	for (i = 0; i < 8; i++)
		printk("	FENCE%d			0x%08x\n", i,
		       hw->fence[i]);

	printk("	INSTPM			0x%08x\n", hw->instpm);
	printk("	MEM_MODE		0x%08x\n", hw->mem_mode);
	printk("	FW_BLC_0		0x%08x\n", hw->fw_blc_0);
	printk("	FW_BLC_1		0x%08x\n", hw->fw_blc_1);

	printk("	HWSTAM			0x%04x\n", hw->hwstam);
	printk("	IER			0x%04x\n", hw->ier);
	printk("	IIR			0x%04x\n", hw->iir);
	printk("	IMR			0x%04x\n", hw->imr);
	printk("hw state dump end\n");
#endif
}



/* Split the M parameter into M1 and M2. */
static int splitm(int index, unsigned int m, unsigned int *retm1,
		  unsigned int *retm2)
{
	int m1, m2;
	int testm;
	struct pll_min_max *pll = &plls[index];

	/* no point optimising too much - brute force m */
	for (m1 = pll->min_m1; m1 < pll->max_m1 + 1; m1++) {
		for (m2 = pll->min_m2; m2 < pll->max_m2 + 1; m2++) {
			testm = (5 * (m1 + 2)) + (m2 + 2);
			if (testm == m) {
				*retm1 = (unsigned int)m1;
				*retm2 = (unsigned int)m2;
				return 0;
			}
		}
	}
	return 1;
}

/* Split the P parameter into P1 and P2. */
static int splitp(int index, unsigned int p, unsigned int *retp1,
		  unsigned int *retp2)
{
	int p1, p2;
	struct pll_min_max *pll = &plls[index];

	if (index == PLLS_I9xx) {
		p2 = (p % 10) ? 1 : 0;

		p1 = p / (p2 ? 5 : 10);

		*retp1 = (unsigned int)p1;
		*retp2 = (unsigned int)p2;
		return 0;
	}

	if (p % 4 == 0)
		p2 = 1;
	else
		p2 = 0;
	p1 = (p / (1 << (p2 + 1))) - 2;
	if (p % 4 == 0 && p1 < pll->min_p1) {
		p2 = 0;
		p1 = (p / (1 << (p2 + 1))) - 2;
	}
	if (p1 < pll->min_p1 || p1 > pll->max_p1 ||
	    (p1 + 2) * (1 << (p2 + 1)) != p) {
		return 1;
	} else {
		*retp1 = (unsigned int)p1;
		*retp2 = (unsigned int)p2;
		return 0;
	}
}

static int calc_pll_params(int index, int clock, u32 *retm1, u32 *retm2,
			   u32 *retn, u32 *retp1, u32 *retp2, u32 *retclock)
{
	u32 m1, m2, n, p1, p2, n1, testm;
	u32 f_vco, p, p_best = 0, m, f_out = 0;
	u32 err_max, err_target, err_best = 10000000;
	u32 n_best = 0, m_best = 0, f_best, f_err;
	u32 p_min, p_max, p_inc, div_max;
	struct pll_min_max *pll = &plls[index];

	/* Accept 0.5% difference, but aim for 0.1% */
	err_max = 5 * clock / 1000;
	err_target = clock / 1000;

	DBG_MSG("Clock is %d\n", clock);

	div_max = pll->max_vco / clock;

	p_inc = (clock <= pll->p_transition_clk) ? pll->p_inc_lo : pll->p_inc_hi;
	p_min = p_inc;
	p_max = ROUND_DOWN_TO(div_max, p_inc);
	if (p_min < pll->min_p)
		p_min = pll->min_p;
	if (p_max > pll->max_p)
		p_max = pll->max_p;

	DBG_MSG("p range is %d-%d (%d)\n", p_min, p_max, p_inc);

	p = p_min;
	do {
		if (splitp(index, p, &p1, &p2)) {
			WRN_MSG("cannot split p = %d\n", p);
			p += p_inc;
			continue;
		}
		n = pll->min_n;
		f_vco = clock * p;

		do {
			m = ROUND_UP_TO(f_vco * n, pll->ref_clk) / pll->ref_clk;
			if (m < pll->min_m)
				m = pll->min_m + 1;
			if (m > pll->max_m)
				m = pll->max_m - 1;
			for (testm = m - 1; testm <= m; testm++) {
				f_out = calc_vclock3(index, testm, n, p);
				if (splitm(index, testm, &m1, &m2)) {
					WRN_MSG("cannot split m = %d\n",
						testm);
					continue;
				}
				if (clock > f_out)
					f_err = clock - f_out;
				else/* slightly bias the error for bigger clocks */
					f_err = f_out - clock + 1;

				if (f_err < err_best) {
					m_best = testm;
					n_best = n;
					p_best = p;
					f_best = f_out;
					err_best = f_err;
				}
			}
			n++;
		} while ((n <= pll->max_n) && (f_out >= clock));
		p += p_inc;
	} while ((p <= p_max));

	if (!m_best) {
		WRN_MSG("cannot find parameters for clock %d\n", clock);
		return 1;
	}
	m = m_best;
	n = n_best;
	p = p_best;
	splitm(index, m, &m1, &m2);
	splitp(index, p, &p1, &p2);
	n1 = n - 2;

	DBG_MSG("m, n, p: %d (%d,%d), %d (%d), %d (%d,%d), "
		"f: %d (%d), VCO: %d\n",
		m, m1, m2, n, n1, p, p1, p2,
		calc_vclock3(index, m, n, p),
		calc_vclock(index, m1, m2, n1, p1, p2, 0),
		calc_vclock3(index, m, n, p) * p);
	*retm1 = m1;
	*retm2 = m2;
	*retn = n1;
	*retp1 = p1;
	*retp2 = p2;
	*retclock = calc_vclock(index, m1, m2, n1, p1, p2, 0);

	return 0;
}

static __inline__ int check_overflow(u32 value, u32 limit,
				     const char *description)
{
	if (value > limit) {
		WRN_MSG("%s value %d exceeds limit %d\n",
			description, value, limit);
		return 1;
	}
	return 0;
}

/* It is assumed that hw is filled in with the initial state information. */
int intelfbhw_mode_to_hw(struct intelfb_info *dinfo,
			 struct intelfb_hwstate *hw,
			 struct fb_var_screeninfo *var)
{
	int pipe = intelfbhw_active_pipe(hw);
	u32 *dpll, *fp0, *fp1;
	u32 m1, m2, n, p1, p2, clock_target, clock;
	u32 hsync_start, hsync_end, hblank_start, hblank_end, htotal, hactive;
	u32 vsync_start, vsync_end, vblank_start, vblank_end, vtotal, vactive;
	u32 vsync_pol, hsync_pol;
	u32 *vs, *vb, *vt, *hs, *hb, *ht, *ss, *pipe_conf;
	u32 stride_alignment;

	DBG_MSG("intelfbhw_mode_to_hw\n");

	/* Disable VGA */
	hw->vgacntrl |= VGA_DISABLE;

	/* Set which pipe's registers will be set. */
	if (pipe == PIPE_B) {
		dpll = &hw->dpll_b;
		fp0 = &hw->fpb0;
		fp1 = &hw->fpb1;
		hs = &hw->hsync_b;
		hb = &hw->hblank_b;
		ht = &hw->htotal_b;
		vs = &hw->vsync_b;
		vb = &hw->vblank_b;
		vt = &hw->vtotal_b;
		ss = &hw->src_size_b;
		pipe_conf = &hw->pipe_b_conf;
	} else {
		dpll = &hw->dpll_a;
		fp0 = &hw->fpa0;
		fp1 = &hw->fpa1;
		hs = &hw->hsync_a;
		hb = &hw->hblank_a;
		ht = &hw->htotal_a;
		vs = &hw->vsync_a;
		vb = &hw->vblank_a;
		vt = &hw->vtotal_a;
		ss = &hw->src_size_a;
		pipe_conf = &hw->pipe_a_conf;
	}

	/* Use ADPA register for sync control. */
	hw->adpa &= ~ADPA_USE_VGA_HVPOLARITY;

	/* sync polarity */
	hsync_pol = (var->sync & FB_SYNC_HOR_HIGH_ACT) ?
			ADPA_SYNC_ACTIVE_HIGH : ADPA_SYNC_ACTIVE_LOW;
	vsync_pol = (var->sync & FB_SYNC_VERT_HIGH_ACT) ?
			ADPA_SYNC_ACTIVE_HIGH : ADPA_SYNC_ACTIVE_LOW;
	hw->adpa &= ~((ADPA_SYNC_ACTIVE_MASK << ADPA_VSYNC_ACTIVE_SHIFT) |
		      (ADPA_SYNC_ACTIVE_MASK << ADPA_HSYNC_ACTIVE_SHIFT));
	hw->adpa |= (hsync_pol << ADPA_HSYNC_ACTIVE_SHIFT) |
		    (vsync_pol << ADPA_VSYNC_ACTIVE_SHIFT);

	/* Connect correct pipe to the analog port DAC */
	hw->adpa &= ~(PIPE_MASK << ADPA_PIPE_SELECT_SHIFT);
	hw->adpa |= (pipe << ADPA_PIPE_SELECT_SHIFT);

	/* Set DPMS state to D0 (on) */
	hw->adpa &= ~ADPA_DPMS_CONTROL_MASK;
	hw->adpa |= ADPA_DPMS_D0;

	hw->adpa |= ADPA_DAC_ENABLE;

	*dpll |= (DPLL_VCO_ENABLE | DPLL_VGA_MODE_DISABLE);
	*dpll &= ~(DPLL_RATE_SELECT_MASK | DPLL_REFERENCE_SELECT_MASK);
	*dpll |= (DPLL_REFERENCE_DEFAULT | DPLL_RATE_SELECT_FP0);

	/* Desired clock in kHz */
	clock_target = 1000000000 / var->pixclock;

	if (calc_pll_params(dinfo->pll_index, clock_target, &m1, &m2,
			    &n, &p1, &p2, &clock)) {
		WRN_MSG("calc_pll_params failed\n");
		return 1;
	}

	/* Check for overflow. */
	if (check_overflow(p1, DPLL_P1_MASK, "PLL P1 parameter"))
		return 1;
	if (check_overflow(p2, DPLL_P2_MASK, "PLL P2 parameter"))
		return 1;
	if (check_overflow(m1, FP_DIVISOR_MASK, "PLL M1 parameter"))
		return 1;
	if (check_overflow(m2, FP_DIVISOR_MASK, "PLL M2 parameter"))
		return 1;
	if (check_overflow(n, FP_DIVISOR_MASK, "PLL N parameter"))
		return 1;

	*dpll &= ~DPLL_P1_FORCE_DIV2;
	*dpll &= ~((DPLL_P2_MASK << DPLL_P2_SHIFT) |
		   (DPLL_P1_MASK << DPLL_P1_SHIFT));

	if (IS_I9XX(dinfo)) {
		*dpll |= (p2 << DPLL_I9XX_P2_SHIFT);
		*dpll |= (1 << (p1 - 1)) << DPLL_P1_SHIFT;
	} else
		*dpll |= (p2 << DPLL_P2_SHIFT) | (p1 << DPLL_P1_SHIFT);

	*fp0 = (n << FP_N_DIVISOR_SHIFT) |
	       (m1 << FP_M1_DIVISOR_SHIFT) |
	       (m2 << FP_M2_DIVISOR_SHIFT);
	*fp1 = *fp0;

	hw->dvob &= ~PORT_ENABLE;
	hw->dvoc &= ~PORT_ENABLE;

	/* Use display plane A. */
	hw->disp_a_ctrl |= DISPPLANE_PLANE_ENABLE;
	hw->disp_a_ctrl &= ~DISPPLANE_GAMMA_ENABLE;
	hw->disp_a_ctrl &= ~DISPPLANE_PIXFORMAT_MASK;
	switch (intelfb_var_to_depth(var)) {
	case 8:
		hw->disp_a_ctrl |= DISPPLANE_8BPP | DISPPLANE_GAMMA_ENABLE;
		break;
	case 15:
		hw->disp_a_ctrl |= DISPPLANE_15_16BPP;
		break;
	case 16:
		hw->disp_a_ctrl |= DISPPLANE_16BPP;
		break;
	case 24:
		hw->disp_a_ctrl |= DISPPLANE_32BPP_NO_ALPHA;
		break;
	}
	hw->disp_a_ctrl &= ~(PIPE_MASK << DISPPLANE_SEL_PIPE_SHIFT);
	hw->disp_a_ctrl |= (pipe << DISPPLANE_SEL_PIPE_SHIFT);

	/* Set CRTC registers. */
	hactive = var->xres;
	hsync_start = hactive + var->right_margin;
	hsync_end = hsync_start + var->hsync_len;
	htotal = hsync_end + var->left_margin;
	hblank_start = hactive;
	hblank_end = htotal;

	DBG_MSG("H: act %d, ss %d, se %d, tot %d bs %d, be %d\n",
		hactive, hsync_start, hsync_end, htotal, hblank_start,
		hblank_end);

	vactive = var->yres;
	if (var->vmode & FB_VMODE_INTERLACED)
		vactive--; /* the chip adds 2 halflines automatically */
	vsync_start = vactive + var->lower_margin;
	vsync_end = vsync_start + var->vsync_len;
	vtotal = vsync_end + var->upper_margin;
	vblank_start = vactive;
	vblank_end = vtotal;
	vblank_end = vsync_end + 1;

	DBG_MSG("V: act %d, ss %d, se %d, tot %d bs %d, be %d\n",
		vactive, vsync_start, vsync_end, vtotal, vblank_start,
		vblank_end);

	/* Adjust for register values, and check for overflow. */
	hactive--;
	if (check_overflow(hactive, HACTIVE_MASK, "CRTC hactive"))
		return 1;
	hsync_start--;
	if (check_overflow(hsync_start, HSYNCSTART_MASK, "CRTC hsync_start"))
		return 1;
	hsync_end--;
	if (check_overflow(hsync_end, HSYNCEND_MASK, "CRTC hsync_end"))
		return 1;
	htotal--;
	if (check_overflow(htotal, HTOTAL_MASK, "CRTC htotal"))
		return 1;
	hblank_start--;
	if (check_overflow(hblank_start, HBLANKSTART_MASK, "CRTC hblank_start"))
		return 1;
	hblank_end--;
	if (check_overflow(hblank_end, HBLANKEND_MASK, "CRTC hblank_end"))
		return 1;

	vactive--;
	if (check_overflow(vactive, VACTIVE_MASK, "CRTC vactive"))
		return 1;
	vsync_start--;
	if (check_overflow(vsync_start, VSYNCSTART_MASK, "CRTC vsync_start"))
		return 1;
	vsync_end--;
	if (check_overflow(vsync_end, VSYNCEND_MASK, "CRTC vsync_end"))
		return 1;
	vtotal--;
	if (check_overflow(vtotal, VTOTAL_MASK, "CRTC vtotal"))
		return 1;
	vblank_start--;
	if (check_overflow(vblank_start, VBLANKSTART_MASK, "CRTC vblank_start"))
		return 1;
	vblank_end--;
	if (check_overflow(vblank_end, VBLANKEND_MASK, "CRTC vblank_end"))
		return 1;

	*ht = (htotal << HTOTAL_SHIFT) | (hactive << HACTIVE_SHIFT);
	*hb = (hblank_start << HBLANKSTART_SHIFT) |
	      (hblank_end << HSYNCEND_SHIFT);
	*hs = (hsync_start << HSYNCSTART_SHIFT) | (hsync_end << HSYNCEND_SHIFT);

	*vt = (vtotal << VTOTAL_SHIFT) | (vactive << VACTIVE_SHIFT);
	*vb = (vblank_start << VBLANKSTART_SHIFT) |
	      (vblank_end << VSYNCEND_SHIFT);
	*vs = (vsync_start << VSYNCSTART_SHIFT) | (vsync_end << VSYNCEND_SHIFT);
	*ss = (hactive << SRC_SIZE_HORIZ_SHIFT) |
	      (vactive << SRC_SIZE_VERT_SHIFT);

	hw->disp_a_stride = dinfo->pitch;
	DBG_MSG("pitch is %d\n", hw->disp_a_stride);

	hw->disp_a_base = hw->disp_a_stride * var->yoffset +
			  var->xoffset * var->bits_per_pixel / 8;

	hw->disp_a_base += dinfo->fb.offset << 12;

	/* Check stride alignment. */
	stride_alignment = IS_I9XX(dinfo) ? STRIDE_ALIGNMENT_I9XX :
					    STRIDE_ALIGNMENT;
	if (hw->disp_a_stride % stride_alignment != 0) {
		WRN_MSG("display stride %d has bad alignment %d\n",
			hw->disp_a_stride, stride_alignment);
		return 1;
	}

	/* Set the palette to 8-bit mode. */
	*pipe_conf &= ~PIPECONF_GAMMA;

	if (var->vmode & FB_VMODE_INTERLACED)
		*pipe_conf |= PIPECONF_INTERLACE_W_FIELD_INDICATION;
	else
		*pipe_conf &= ~PIPECONF_INTERLACE_MASK;

	return 0;
}

/* Program a (non-VGA) video mode. */
int intelfbhw_program_mode(struct intelfb_info *dinfo,
			   const struct intelfb_hwstate *hw, int blank)
{
	u32 tmp;
	const u32 *dpll, *fp0, *fp1, *pipe_conf;
	const u32 *hs, *ht, *hb, *vs, *vt, *vb, *ss;
	u32 dpll_reg, fp0_reg, fp1_reg, pipe_conf_reg, pipe_stat_reg;
	u32 hsync_reg, htotal_reg, hblank_reg;
	u32 vsync_reg, vtotal_reg, vblank_reg;
	u32 src_size_reg;
	u32 count, tmp_val[3];

	/* Assume single pipe */

#if VERBOSE > 0
	DBG_MSG("intelfbhw_program_mode\n");
#endif

	/* Disable VGA */
	tmp = INREG(VGACNTRL);
	tmp |= VGA_DISABLE;
	OUTREG(VGACNTRL, tmp);

	dinfo->pipe = intelfbhw_active_pipe(hw);

	if (dinfo->pipe == PIPE_B) {
		dpll = &hw->dpll_b;
		fp0 = &hw->fpb0;
		fp1 = &hw->fpb1;
		pipe_conf = &hw->pipe_b_conf;
		hs = &hw->hsync_b;
		hb = &hw->hblank_b;
		ht = &hw->htotal_b;
		vs = &hw->vsync_b;
		vb = &hw->vblank_b;
		vt = &hw->vtotal_b;
		ss = &hw->src_size_b;
		dpll_reg = DPLL_B;
		fp0_reg = FPB0;
		fp1_reg = FPB1;
		pipe_conf_reg = PIPEBCONF;
		pipe_stat_reg = PIPEBSTAT;
		hsync_reg = HSYNC_B;
		htotal_reg = HTOTAL_B;
		hblank_reg = HBLANK_B;
		vsync_reg = VSYNC_B;
		vtotal_reg = VTOTAL_B;
		vblank_reg = VBLANK_B;
		src_size_reg = SRC_SIZE_B;
	} else {
		dpll = &hw->dpll_a;
		fp0 = &hw->fpa0;
		fp1 = &hw->fpa1;
		pipe_conf = &hw->pipe_a_conf;
		hs = &hw->hsync_a;
		hb = &hw->hblank_a;
		ht = &hw->htotal_a;
		vs = &hw->vsync_a;
		vb = &hw->vblank_a;
		vt = &hw->vtotal_a;
		ss = &hw->src_size_a;
		dpll_reg = DPLL_A;
		fp0_reg = FPA0;
		fp1_reg = FPA1;
		pipe_conf_reg = PIPEACONF;
		pipe_stat_reg = PIPEASTAT;
		hsync_reg = HSYNC_A;
		htotal_reg = HTOTAL_A;
		hblank_reg = HBLANK_A;
		vsync_reg = VSYNC_A;
		vtotal_reg = VTOTAL_A;
		vblank_reg = VBLANK_A;
		src_size_reg = SRC_SIZE_A;
	}

	/* turn off pipe */
	tmp = INREG(pipe_conf_reg);
	tmp &= ~PIPECONF_ENABLE;
	OUTREG(pipe_conf_reg, tmp);

	count = 0;
	do {
		tmp_val[count % 3] = INREG(PIPEA_DSL);
		if ((tmp_val[0] == tmp_val[1]) && (tmp_val[1] == tmp_val[2]))
			break;
		count++;
		udelay(1);
		if (count % 200 == 0) {
			tmp = INREG(pipe_conf_reg);
			tmp &= ~PIPECONF_ENABLE;
			OUTREG(pipe_conf_reg, tmp);
		}
	} while (count < 2000);

	OUTREG(ADPA, INREG(ADPA) & ~ADPA_DAC_ENABLE);

	/* Disable planes A and B. */
	tmp = INREG(DSPACNTR);
	tmp &= ~DISPPLANE_PLANE_ENABLE;
	OUTREG(DSPACNTR, tmp);
	tmp = INREG(DSPBCNTR);
	tmp &= ~DISPPLANE_PLANE_ENABLE;
	OUTREG(DSPBCNTR, tmp);

	/* Wait for vblank. For now, just wait for a 50Hz cycle (20ms)) */
	mdelay(20);

	OUTREG(DVOB, INREG(DVOB) & ~PORT_ENABLE);
	OUTREG(DVOC, INREG(DVOC) & ~PORT_ENABLE);
	OUTREG(ADPA, INREG(ADPA) & ~ADPA_DAC_ENABLE);

	/* Disable Sync */
	tmp = INREG(ADPA);
	tmp &= ~ADPA_DPMS_CONTROL_MASK;
	tmp |= ADPA_DPMS_D3;
	OUTREG(ADPA, tmp);

	/* do some funky magic - xyzzy */
	OUTREG(0x61204, 0xabcd0000);

	/* turn off PLL */
	tmp = INREG(dpll_reg);
	tmp &= ~DPLL_VCO_ENABLE;
	OUTREG(dpll_reg, tmp);

	/* Set PLL parameters */
	OUTREG(fp0_reg, *fp0);
	OUTREG(fp1_reg, *fp1);

	/* Enable PLL */
	OUTREG(dpll_reg, *dpll);

	/* Set DVOs B/C */
	OUTREG(DVOB, hw->dvob);
	OUTREG(DVOC, hw->dvoc);

	/* undo funky magic */
	OUTREG(0x61204, 0x00000000);

	/* Set ADPA */
	OUTREG(ADPA, INREG(ADPA) | ADPA_DAC_ENABLE);
	OUTREG(ADPA, (hw->adpa & ~(ADPA_DPMS_CONTROL_MASK)) | ADPA_DPMS_D3);

	/* Set pipe parameters */
	OUTREG(hsync_reg, *hs);
	OUTREG(hblank_reg, *hb);
	OUTREG(htotal_reg, *ht);
	OUTREG(vsync_reg, *vs);
	OUTREG(vblank_reg, *vb);
	OUTREG(vtotal_reg, *vt);
	OUTREG(src_size_reg, *ss);

	switch (dinfo->info->var.vmode & (FB_VMODE_INTERLACED |
					  FB_VMODE_ODD_FLD_FIRST)) {
	case FB_VMODE_INTERLACED | FB_VMODE_ODD_FLD_FIRST:
		OUTREG(pipe_stat_reg, 0xFFFF | PIPESTAT_FLD_EVT_ODD_EN);
		break;
	case FB_VMODE_INTERLACED: /* even lines first */
		OUTREG(pipe_stat_reg, 0xFFFF | PIPESTAT_FLD_EVT_EVEN_EN);
		break;
	default:		/* non-interlaced */
		OUTREG(pipe_stat_reg, 0xFFFF); /* clear all status bits only */
	}
	/* Enable pipe */
	OUTREG(pipe_conf_reg, *pipe_conf | PIPECONF_ENABLE);

	/* Enable sync */
	tmp = INREG(ADPA);
	tmp &= ~ADPA_DPMS_CONTROL_MASK;
	tmp |= ADPA_DPMS_D0;
	OUTREG(ADPA, tmp);

	/* setup display plane */
	if (dinfo->pdev->device == PCI_DEVICE_ID_INTEL_830M) {
		/*
		 *      i830M errata: the display plane must be enabled
		 *      to allow writes to the other bits in the plane
		 *      control register.
		 */
		tmp = INREG(DSPACNTR);
		if ((tmp & DISPPLANE_PLANE_ENABLE) != DISPPLANE_PLANE_ENABLE) {
			tmp |= DISPPLANE_PLANE_ENABLE;
			OUTREG(DSPACNTR, tmp);
			OUTREG(DSPACNTR,
			       hw->disp_a_ctrl|DISPPLANE_PLANE_ENABLE);
			mdelay(1);
		}
	}

	OUTREG(DSPACNTR, hw->disp_a_ctrl & ~DISPPLANE_PLANE_ENABLE);
	OUTREG(DSPASTRIDE, hw->disp_a_stride);
	OUTREG(DSPABASE, hw->disp_a_base);

	/* Enable plane */
	if (!blank) {
		tmp = INREG(DSPACNTR);
		tmp |= DISPPLANE_PLANE_ENABLE;
		OUTREG(DSPACNTR, tmp);
		OUTREG(DSPABASE, hw->disp_a_base);
	}

	return 0;
}

/* forward declarations */
static void refresh_ring(struct intelfb_info *dinfo);
static void reset_state(struct intelfb_info *dinfo);
static void do_flush(struct intelfb_info *dinfo);

static  u32 get_ring_space(struct intelfb_info *dinfo)
{
	u32 ring_space;

	if (dinfo->ring_tail >= dinfo->ring_head)
		ring_space = dinfo->ring.size -
			(dinfo->ring_tail - dinfo->ring_head);
	else
		ring_space = dinfo->ring_head - dinfo->ring_tail;

	if (ring_space > RING_MIN_FREE)
		ring_space -= RING_MIN_FREE;
	else
		ring_space = 0;

	return ring_space;
}

static int wait_ring(struct intelfb_info *dinfo, int n)
{
	int i = 0;
	unsigned long end;
	u32 last_head = INREG(PRI_RING_HEAD) & RING_HEAD_MASK;

#if VERBOSE > 0
	DBG_MSG("wait_ring: %d\n", n);
#endif

	end = jiffies + (HZ * 3);
	while (dinfo->ring_space < n) {
		dinfo->ring_head = INREG(PRI_RING_HEAD) & RING_HEAD_MASK;
		dinfo->ring_space = get_ring_space(dinfo);

		if (dinfo->ring_head != last_head) {
			end = jiffies + (HZ * 3);
			last_head = dinfo->ring_head;
		}
		i++;
		if (time_before(end, jiffies)) {
			if (!i) {
				/* Try again */
				reset_state(dinfo);
				refresh_ring(dinfo);
				do_flush(dinfo);
				end = jiffies + (HZ * 3);
				i = 1;
			} else {
				WRN_MSG("ring buffer : space: %d wanted %d\n",
					dinfo->ring_space, n);
				WRN_MSG("lockup - turning off hardware "
					"acceleration\n");
				dinfo->ring_lockup = 1;
				break;
			}
		}
		udelay(1);
	}
	return i;
}

static void do_flush(struct intelfb_info *dinfo)
{
	START_RING(2);
	OUT_RING(MI_FLUSH | MI_WRITE_DIRTY_STATE | MI_INVALIDATE_MAP_CACHE);
	OUT_RING(MI_NOOP);
	ADVANCE_RING();
}

void intelfbhw_do_sync(struct intelfb_info *dinfo)
{
#if VERBOSE > 0
	DBG_MSG("intelfbhw_do_sync\n");
#endif

	if (!dinfo->accel)
		return;

	/*
	 * Send a flush, then wait until the ring is empty.  This is what
	 * the XFree86 driver does, and actually it doesn't seem a lot worse
	 * than the recommended method (both have problems).
	 */
	do_flush(dinfo);
	wait_ring(dinfo, dinfo->ring.size - RING_MIN_FREE);
	dinfo->ring_space = dinfo->ring.size - RING_MIN_FREE;
}

static void refresh_ring(struct intelfb_info *dinfo)
{
#if VERBOSE > 0
	DBG_MSG("refresh_ring\n");
#endif

	dinfo->ring_head = INREG(PRI_RING_HEAD) & RING_HEAD_MASK;
	dinfo->ring_tail = INREG(PRI_RING_TAIL) & RING_TAIL_MASK;
	dinfo->ring_space = get_ring_space(dinfo);
}

static void reset_state(struct intelfb_info *dinfo)
{
	int i;
	u32 tmp;

#if VERBOSE > 0
	DBG_MSG("reset_state\n");
#endif

	for (i = 0; i < FENCE_NUM; i++)
		OUTREG(FENCE + (i << 2), 0);

	/* Flush the ring buffer if it's enabled. */
	tmp = INREG(PRI_RING_LENGTH);
	if (tmp & RING_ENABLE) {
#if VERBOSE > 0
		DBG_MSG("reset_state: ring was enabled\n");
#endif
		refresh_ring(dinfo);
		intelfbhw_do_sync(dinfo);
		DO_RING_IDLE();
	}

	OUTREG(PRI_RING_LENGTH, 0);
	OUTREG(PRI_RING_HEAD, 0);
	OUTREG(PRI_RING_TAIL, 0);
	OUTREG(PRI_RING_START, 0);
}

/* Stop the 2D engine, and turn off the ring buffer. */
void intelfbhw_2d_stop(struct intelfb_info *dinfo)
{
#if VERBOSE > 0
	DBG_MSG("intelfbhw_2d_stop: accel: %d, ring_active: %d\n",
		dinfo->accel, dinfo->ring_active);
#endif

	if (!dinfo->accel)
		return;

	dinfo->ring_active = 0;
	reset_state(dinfo);
}

/*
 * Enable the ring buffer, and initialise the 2D engine.
 * It is assumed that the graphics engine has been stopped by previously
 * calling intelfb_2d_stop().
 */
void intelfbhw_2d_start(struct intelfb_info *dinfo)
{
#if VERBOSE > 0
	DBG_MSG("intelfbhw_2d_start: accel: %d, ring_active: %d\n",
		dinfo->accel, dinfo->ring_active);
#endif

	if (!dinfo->accel)
		return;

	/* Initialise the primary ring buffer. */
	OUTREG(PRI_RING_LENGTH, 0);
	OUTREG(PRI_RING_TAIL, 0);
	OUTREG(PRI_RING_HEAD, 0);

	OUTREG(PRI_RING_START, dinfo->ring.physical & RING_START_MASK);
	OUTREG(PRI_RING_LENGTH,
		((dinfo->ring.size - GTT_PAGE_SIZE) & RING_LENGTH_MASK) |
		RING_NO_REPORT | RING_ENABLE);
	refresh_ring(dinfo);
	dinfo->ring_active = 1;
}

/* 2D fillrect (solid fill or invert) */
void intelfbhw_do_fillrect(struct intelfb_info *dinfo, u32 x, u32 y, u32 w,
			   u32 h, u32 color, u32 pitch, u32 bpp, u32 rop)
{
	u32 br00, br09, br13, br14, br16;

#if VERBOSE > 0
	DBG_MSG("intelfbhw_do_fillrect: (%d,%d) %dx%d, c 0x%06x, p %d bpp %d, "
		"rop 0x%02x\n", x, y, w, h, color, pitch, bpp, rop);
#endif

	br00 = COLOR_BLT_CMD;
	br09 = dinfo->fb_start + (y * pitch + x * (bpp / 8));
	br13 = (rop << ROP_SHIFT) | pitch;
	br14 = (h << HEIGHT_SHIFT) | ((w * (bpp / 8)) << WIDTH_SHIFT);
	br16 = color;

	switch (bpp) {
	case 8:
		br13 |= COLOR_DEPTH_8;
		break;
	case 16:
		br13 |= COLOR_DEPTH_16;
		break;
	case 32:
		br13 |= COLOR_DEPTH_32;
		br00 |= WRITE_ALPHA | WRITE_RGB;
		break;
	}

	START_RING(6);
	OUT_RING(br00);
	OUT_RING(br13);
	OUT_RING(br14);
	OUT_RING(br09);
	OUT_RING(br16);
	OUT_RING(MI_NOOP);
	ADVANCE_RING();

#if VERBOSE > 0
	DBG_MSG("ring = 0x%08x, 0x%08x (%d)\n", dinfo->ring_head,
		dinfo->ring_tail, dinfo->ring_space);
#endif
}

void
intelfbhw_do_bitblt(struct intelfb_info *dinfo, u32 curx, u32 cury,
		    u32 dstx, u32 dsty, u32 w, u32 h, u32 pitch, u32 bpp)
{
	u32 br00, br09, br11, br12, br13, br22, br23, br26;

#if VERBOSE > 0
	DBG_MSG("intelfbhw_do_bitblt: (%d,%d)->(%d,%d) %dx%d, p %d bpp %d\n",
		curx, cury, dstx, dsty, w, h, pitch, bpp);
#endif

	br00 = XY_SRC_COPY_BLT_CMD;
	br09 = dinfo->fb_start;
	br11 = (pitch << PITCH_SHIFT);
	br12 = dinfo->fb_start;
	br13 = (SRC_ROP_GXCOPY << ROP_SHIFT) | (pitch << PITCH_SHIFT);
	br22 = (dstx << WIDTH_SHIFT) | (dsty << HEIGHT_SHIFT);
	br23 = ((dstx + w) << WIDTH_SHIFT) |
	       ((dsty + h) << HEIGHT_SHIFT);
	br26 = (curx << WIDTH_SHIFT) | (cury << HEIGHT_SHIFT);

	switch (bpp) {
	case 8:
		br13 |= COLOR_DEPTH_8;
		break;
	case 16:
		br13 |= COLOR_DEPTH_16;
		break;
	case 32:
		br13 |= COLOR_DEPTH_32;
		br00 |= WRITE_ALPHA | WRITE_RGB;
		break;
	}

	START_RING(8);
	OUT_RING(br00);
	OUT_RING(br13);
	OUT_RING(br22);
	OUT_RING(br23);
	OUT_RING(br09);
	OUT_RING(br26);
	OUT_RING(br11);
	OUT_RING(br12);
	ADVANCE_RING();
}

int intelfbhw_do_drawglyph(struct intelfb_info *dinfo, u32 fg, u32 bg, u32 w,
			   u32 h, const u8* cdat, u32 x, u32 y, u32 pitch,
			   u32 bpp)
{
	int nbytes, ndwords, pad, tmp;
	u32 br00, br09, br13, br18, br19, br22, br23;
	int dat, ix, iy, iw;
	int i, j;

#if VERBOSE > 0
	DBG_MSG("intelfbhw_do_drawglyph: (%d,%d) %dx%d\n", x, y, w, h);
#endif

	/* size in bytes of a padded scanline */
	nbytes = ROUND_UP_TO(w, 16) / 8;

	/* Total bytes of padded scanline data to write out. */
	nbytes = nbytes * h;

	/*
	 * Check if the glyph data exceeds the immediate mode limit.
	 * It would take a large font (1K pixels) to hit this limit.
	 */
	if (nbytes > MAX_MONO_IMM_SIZE)
		return 0;

	/* Src data is packaged a dword (32-bit) at a time. */
	ndwords = ROUND_UP_TO(nbytes, 4) / 4;

	/*
	 * Ring has to be padded to a quad word. But because the command starts
	   with 7 bytes, pad only if there is an even number of ndwords
	 */
	pad = !(ndwords % 2);

	tmp = (XY_MONO_SRC_IMM_BLT_CMD & DW_LENGTH_MASK) + ndwords;
	br00 = (XY_MONO_SRC_IMM_BLT_CMD & ~DW_LENGTH_MASK) | tmp;
	br09 = dinfo->fb_start;
	br13 = (SRC_ROP_GXCOPY << ROP_SHIFT) | (pitch << PITCH_SHIFT);
	br18 = bg;
	br19 = fg;
	br22 = (x << WIDTH_SHIFT) | (y << HEIGHT_SHIFT);
	br23 = ((x + w) << WIDTH_SHIFT) | ((y + h) << HEIGHT_SHIFT);

	switch (bpp) {
	case 8:
		br13 |= COLOR_DEPTH_8;
		break;
	case 16:
		br13 |= COLOR_DEPTH_16;
		break;
	case 32:
		br13 |= COLOR_DEPTH_32;
		br00 |= WRITE_ALPHA | WRITE_RGB;
		break;
	}

	START_RING(8 + ndwords);
	OUT_RING(br00);
	OUT_RING(br13);
	OUT_RING(br22);
	OUT_RING(br23);
	OUT_RING(br09);
	OUT_RING(br18);
	OUT_RING(br19);
	ix = iy = 0;
	iw = ROUND_UP_TO(w, 8) / 8;
	while (ndwords--) {
		dat = 0;
		for (j = 0; j < 2; ++j) {
			for (i = 0; i < 2; ++i) {
				if (ix != iw || i == 0)
					dat |= cdat[iy*iw + ix++] << (i+j*2)*8;
			}
			if (ix == iw && iy != (h-1)) {
				ix = 0;
				++iy;
			}
		}
		OUT_RING(dat);
	}
	if (pad)
		OUT_RING(MI_NOOP);
	ADVANCE_RING();

	return 1;
}

/* HW cursor functions. */
void intelfbhw_cursor_init(struct intelfb_info *dinfo)
{
	u32 tmp;

#if VERBOSE > 0
	DBG_MSG("intelfbhw_cursor_init\n");
#endif

	if (dinfo->mobile || IS_I9XX(dinfo)) {
		if (!dinfo->cursor.physical)
			return;
		tmp = INREG(CURSOR_A_CONTROL);
		tmp &= ~(CURSOR_MODE_MASK | CURSOR_MOBILE_GAMMA_ENABLE |
			 CURSOR_MEM_TYPE_LOCAL |
			 (1 << CURSOR_PIPE_SELECT_SHIFT));
		tmp |= CURSOR_MODE_DISABLE;
		OUTREG(CURSOR_A_CONTROL, tmp);
		OUTREG(CURSOR_A_BASEADDR, dinfo->cursor.physical);
	} else {
		tmp = INREG(CURSOR_CONTROL);
		tmp &= ~(CURSOR_FORMAT_MASK | CURSOR_GAMMA_ENABLE |
			 CURSOR_ENABLE | CURSOR_STRIDE_MASK);
		tmp = CURSOR_FORMAT_3C;
		OUTREG(CURSOR_CONTROL, tmp);
		OUTREG(CURSOR_A_BASEADDR, dinfo->cursor.offset << 12);
		tmp = (64 << CURSOR_SIZE_H_SHIFT) |
		      (64 << CURSOR_SIZE_V_SHIFT);
		OUTREG(CURSOR_SIZE, tmp);
	}
}

void intelfbhw_cursor_hide(struct intelfb_info *dinfo)
{
	u32 tmp;

#if VERBOSE > 0
	DBG_MSG("intelfbhw_cursor_hide\n");
#endif

	dinfo->cursor_on = 0;
	if (dinfo->mobile || IS_I9XX(dinfo)) {
		if (!dinfo->cursor.physical)
			return;
		tmp = INREG(CURSOR_A_CONTROL);
		tmp &= ~CURSOR_MODE_MASK;
		tmp |= CURSOR_MODE_DISABLE;
		OUTREG(CURSOR_A_CONTROL, tmp);
		/* Flush changes */
		OUTREG(CURSOR_A_BASEADDR, dinfo->cursor.physical);
	} else {
		tmp = INREG(CURSOR_CONTROL);
		tmp &= ~CURSOR_ENABLE;
		OUTREG(CURSOR_CONTROL, tmp);
	}
}

void intelfbhw_cursor_show(struct intelfb_info *dinfo)
{
	u32 tmp;

#if VERBOSE > 0
	DBG_MSG("intelfbhw_cursor_show\n");
#endif

	dinfo->cursor_on = 1;

	if (dinfo->cursor_blanked)
		return;

	if (dinfo->mobile || IS_I9XX(dinfo)) {
		if (!dinfo->cursor.physical)
			return;
		tmp = INREG(CURSOR_A_CONTROL);
		tmp &= ~CURSOR_MODE_MASK;
		tmp |= CURSOR_MODE_64_4C_AX;
		OUTREG(CURSOR_A_CONTROL, tmp);
		/* Flush changes */
		OUTREG(CURSOR_A_BASEADDR, dinfo->cursor.physical);
	} else {
		tmp = INREG(CURSOR_CONTROL);
		tmp |= CURSOR_ENABLE;
		OUTREG(CURSOR_CONTROL, tmp);
	}
}

void intelfbhw_cursor_setpos(struct intelfb_info *dinfo, int x, int y)
{
	u32 tmp;

#if VERBOSE > 0
	DBG_MSG("intelfbhw_cursor_setpos: (%d, %d)\n", x, y);
#endif

	/*
	 * Sets the position. The coordinates are assumed to already
	 * have any offset adjusted. Assume that the cursor is never
	 * completely off-screen, and that x, y are always >= 0.
	 */

	tmp = ((x & CURSOR_POS_MASK) << CURSOR_X_SHIFT) |
	      ((y & CURSOR_POS_MASK) << CURSOR_Y_SHIFT);
	OUTREG(CURSOR_A_POSITION, tmp);

	if (IS_I9XX(dinfo))
		OUTREG(CURSOR_A_BASEADDR, dinfo->cursor.physical);
}

void intelfbhw_cursor_setcolor(struct intelfb_info *dinfo, u32 bg, u32 fg)
{
#if VERBOSE > 0
	DBG_MSG("intelfbhw_cursor_setcolor\n");
#endif

	OUTREG(CURSOR_A_PALETTE0, bg & CURSOR_PALETTE_MASK);
	OUTREG(CURSOR_A_PALETTE1, fg & CURSOR_PALETTE_MASK);
	OUTREG(CURSOR_A_PALETTE2, fg & CURSOR_PALETTE_MASK);
	OUTREG(CURSOR_A_PALETTE3, bg & CURSOR_PALETTE_MASK);
}

void intelfbhw_cursor_load(struct intelfb_info *dinfo, int width, int height,
			   u8 *data)
{
	u8 __iomem *addr = (u8 __iomem *)dinfo->cursor.virtual;
	int i, j, w = width / 8;
	int mod = width % 8, t_mask, d_mask;

#if VERBOSE > 0
	DBG_MSG("intelfbhw_cursor_load\n");
#endif

	if (!dinfo->cursor.virtual)
		return;

	t_mask = 0xff >> mod;
	d_mask = ~(0xff >> mod);
	for (i = height; i--; ) {
		for (j = 0; j < w; j++) {
			writeb(0x00, addr + j);
			writeb(*(data++), addr + j+8);
		}
		if (mod) {
			writeb(t_mask, addr + j);
			writeb(*(data++) & d_mask, addr + j+8);
		}
		addr += 16;
	}
}

void intelfbhw_cursor_reset(struct intelfb_info *dinfo)
{
	u8 __iomem *addr = (u8 __iomem *)dinfo->cursor.virtual;
	int i, j;

#if VERBOSE > 0
	DBG_MSG("intelfbhw_cursor_reset\n");
#endif

	if (!dinfo->cursor.virtual)
		return;

	for (i = 64; i--; ) {
		for (j = 0; j < 8; j++) {
			writeb(0xff, addr + j+0);
			writeb(0x00, addr + j+8);
		}
		addr += 16;
	}
}

static irqreturn_t intelfbhw_irq(int irq, void *dev_id)
{
	u16 tmp;
	struct intelfb_info *dinfo = dev_id;

	spin_lock(&dinfo->int_lock);

	tmp = INREG16(IIR);
	if (dinfo->info->var.vmode & FB_VMODE_INTERLACED)
		tmp &= PIPE_A_EVENT_INTERRUPT;
	else
		tmp &= VSYNC_PIPE_A_INTERRUPT; /* non-interlaced */

	if (tmp == 0) {
		spin_unlock(&dinfo->int_lock);
		return IRQ_RETVAL(0); /* not us */
	}

	/* clear status bits 0-15 ASAP and don't touch bits 16-31 */
	OUTREG(PIPEASTAT, INREG(PIPEASTAT));

	OUTREG16(IIR, tmp);
	if (dinfo->vsync.pan_display) {
		dinfo->vsync.pan_display = 0;
		OUTREG(DSPABASE, dinfo->vsync.pan_offset);
	}

	dinfo->vsync.count++;
	wake_up_interruptible(&dinfo->vsync.wait);

	spin_unlock(&dinfo->int_lock);

	return IRQ_RETVAL(1);
}

int intelfbhw_enable_irq(struct intelfb_info *dinfo)
{
	u16 tmp;
	if (!test_and_set_bit(0, &dinfo->irq_flags)) {
		if (request_irq(dinfo->pdev->irq, intelfbhw_irq, IRQF_SHARED,
				"intelfb", dinfo)) {
			clear_bit(0, &dinfo->irq_flags);
			return -EINVAL;
		}

		spin_lock_irq(&dinfo->int_lock);
		OUTREG16(HWSTAM, 0xfffe); /* i830 DRM uses ffff */
		OUTREG16(IMR, 0);
	} else
		spin_lock_irq(&dinfo->int_lock);

	if (dinfo->info->var.vmode & FB_VMODE_INTERLACED)
		tmp = PIPE_A_EVENT_INTERRUPT;
	else
		tmp = VSYNC_PIPE_A_INTERRUPT; /* non-interlaced */
	if (tmp != INREG16(IER)) {
		DBG_MSG("changing IER to 0x%X\n", tmp);
		OUTREG16(IER, tmp);
	}

	spin_unlock_irq(&dinfo->int_lock);
	return 0;
}

void intelfbhw_disable_irq(struct intelfb_info *dinfo)
{
	if (test_and_clear_bit(0, &dinfo->irq_flags)) {
		if (dinfo->vsync.pan_display) {
			dinfo->vsync.pan_display = 0;
			OUTREG(DSPABASE, dinfo->vsync.pan_offset);
		}
		spin_lock_irq(&dinfo->int_lock);
		OUTREG16(HWSTAM, 0xffff);
		OUTREG16(IMR, 0xffff);
		OUTREG16(IER, 0x0);

		OUTREG16(IIR, INREG16(IIR)); /* clear IRQ requests */
		spin_unlock_irq(&dinfo->int_lock);

		free_irq(dinfo->pdev->irq, dinfo);
	}
}

int intelfbhw_wait_for_vsync(struct intelfb_info *dinfo, u32 pipe)
{
	struct intelfb_vsync *vsync;
	unsigned int count;
	int ret;

	switch (pipe) {
		case 0:
			vsync = &dinfo->vsync;
			break;
		default:
			return -ENODEV;
	}

	ret = intelfbhw_enable_irq(dinfo);
	if (ret)
		return ret;

	count = vsync->count;
	ret = wait_event_interruptible_timeout(vsync->wait,
					       count != vsync->count, HZ / 10);
	if (ret < 0)
		return ret;
	if (ret == 0) {
		DBG_MSG("wait_for_vsync timed out!\n");
		return -ETIMEDOUT;
	}

	return 0;
}
