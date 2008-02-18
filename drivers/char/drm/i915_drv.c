/* i915_drv.c -- i830,i845,i855,i865,i915 driver -*- linux-c -*-
 */
/*
 *
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL TUNGSTEN GRAPHICS AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"

#include "drm_pciids.h"

static struct pci_device_id pciidlist[] = {
	i915_PCI_IDS
};

enum pipe {
    PIPE_A = 0,
    PIPE_B,
};

static bool i915_pipe_enabled(struct drm_device *dev, enum pipe pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;

	if (pipe == PIPE_A)
		return (I915_READ(DPLL_A) & DPLL_VCO_ENABLE);
	else
		return (I915_READ(DPLL_B) & DPLL_VCO_ENABLE);
}

static void i915_save_palette(struct drm_device *dev, enum pipe pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long reg = (pipe == PIPE_A ? PALETTE_A : PALETTE_B);
	u32 *array;
	int i;

	if (!i915_pipe_enabled(dev, pipe))
		return;

	if (pipe == PIPE_A)
		array = dev_priv->save_palette_a;
	else
		array = dev_priv->save_palette_b;

	for(i = 0; i < 256; i++)
		array[i] = I915_READ(reg + (i << 2));
}

static void i915_restore_palette(struct drm_device *dev, enum pipe pipe)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	unsigned long reg = (pipe == PIPE_A ? PALETTE_A : PALETTE_B);
	u32 *array;
	int i;

	if (!i915_pipe_enabled(dev, pipe))
		return;

	if (pipe == PIPE_A)
		array = dev_priv->save_palette_a;
	else
		array = dev_priv->save_palette_b;

	for(i = 0; i < 256; i++)
		I915_WRITE(reg + (i << 2), array[i]);
}

static u8 i915_read_indexed(u16 index_port, u16 data_port, u8 reg)
{
	outb(reg, index_port);
	return inb(data_port);
}

static u8 i915_read_ar(u16 st01, u8 reg, u16 palette_enable)
{
	inb(st01);
	outb(palette_enable | reg, VGA_AR_INDEX);
	return inb(VGA_AR_DATA_READ);
}

static void i915_write_ar(u8 st01, u8 reg, u8 val, u16 palette_enable)
{
	inb(st01);
	outb(palette_enable | reg, VGA_AR_INDEX);
	outb(val, VGA_AR_DATA_WRITE);
}

static void i915_write_indexed(u16 index_port, u16 data_port, u8 reg, u8 val)
{
	outb(reg, index_port);
	outb(val, data_port);
}

static void i915_save_vga(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;
	u16 cr_index, cr_data, st01;

	/* VGA color palette registers */
	dev_priv->saveDACMASK = inb(VGA_DACMASK);
	/* DACCRX automatically increments during read */
	outb(0, VGA_DACRX);
	/* Read 3 bytes of color data from each index */
	for (i = 0; i < 256 * 3; i++)
		dev_priv->saveDACDATA[i] = inb(VGA_DACDATA);

	/* MSR bits */
	dev_priv->saveMSR = inb(VGA_MSR_READ);
	if (dev_priv->saveMSR & VGA_MSR_CGA_MODE) {
		cr_index = VGA_CR_INDEX_CGA;
		cr_data = VGA_CR_DATA_CGA;
		st01 = VGA_ST01_CGA;
	} else {
		cr_index = VGA_CR_INDEX_MDA;
		cr_data = VGA_CR_DATA_MDA;
		st01 = VGA_ST01_MDA;
	}

	/* CRT controller regs */
	i915_write_indexed(cr_index, cr_data, 0x11,
			   i915_read_indexed(cr_index, cr_data, 0x11) &
			   (~0x80));
	for (i = 0; i < 0x24; i++)
		dev_priv->saveCR[i] =
			i915_read_indexed(cr_index, cr_data, i);
	/* Make sure we don't turn off CR group 0 writes */
	dev_priv->saveCR[0x11] &= ~0x80;

	/* Attribute controller registers */
	inb(st01);
	dev_priv->saveAR_INDEX = inb(VGA_AR_INDEX);
	for (i = 0; i < 20; i++)
		dev_priv->saveAR[i] = i915_read_ar(st01, i, 0);
	inb(st01);
	outb(dev_priv->saveAR_INDEX, VGA_AR_INDEX);

	/* Graphics controller registers */
	for (i = 0; i < 9; i++)
		dev_priv->saveGR[i] =
			i915_read_indexed(VGA_GR_INDEX, VGA_GR_DATA, i);

	dev_priv->saveGR[0x10] =
		i915_read_indexed(VGA_GR_INDEX, VGA_GR_DATA, 0x10);
	dev_priv->saveGR[0x11] =
		i915_read_indexed(VGA_GR_INDEX, VGA_GR_DATA, 0x11);
	dev_priv->saveGR[0x18] =
		i915_read_indexed(VGA_GR_INDEX, VGA_GR_DATA, 0x18);

	/* Sequencer registers */
	for (i = 0; i < 8; i++)
		dev_priv->saveSR[i] =
			i915_read_indexed(VGA_SR_INDEX, VGA_SR_DATA, i);
}

static void i915_restore_vga(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;
	u16 cr_index, cr_data, st01;

	/* MSR bits */
	outb(dev_priv->saveMSR, VGA_MSR_WRITE);
	if (dev_priv->saveMSR & VGA_MSR_CGA_MODE) {
		cr_index = VGA_CR_INDEX_CGA;
		cr_data = VGA_CR_DATA_CGA;
		st01 = VGA_ST01_CGA;
	} else {
		cr_index = VGA_CR_INDEX_MDA;
		cr_data = VGA_CR_DATA_MDA;
		st01 = VGA_ST01_MDA;
	}

	/* Sequencer registers, don't write SR07 */
	for (i = 0; i < 7; i++)
		i915_write_indexed(VGA_SR_INDEX, VGA_SR_DATA, i,
				   dev_priv->saveSR[i]);

	/* CRT controller regs */
	/* Enable CR group 0 writes */
	i915_write_indexed(cr_index, cr_data, 0x11, dev_priv->saveCR[0x11]);
	for (i = 0; i < 0x24; i++)
		i915_write_indexed(cr_index, cr_data, i, dev_priv->saveCR[i]);

	/* Graphics controller regs */
	for (i = 0; i < 9; i++)
		i915_write_indexed(VGA_GR_INDEX, VGA_GR_DATA, i,
				   dev_priv->saveGR[i]);

	i915_write_indexed(VGA_GR_INDEX, VGA_GR_DATA, 0x10,
			   dev_priv->saveGR[0x10]);
	i915_write_indexed(VGA_GR_INDEX, VGA_GR_DATA, 0x11,
			   dev_priv->saveGR[0x11]);
	i915_write_indexed(VGA_GR_INDEX, VGA_GR_DATA, 0x18,
			   dev_priv->saveGR[0x18]);

	/* Attribute controller registers */
	for (i = 0; i < 20; i++)
		i915_write_ar(st01, i, dev_priv->saveAR[i], 0);
	inb(st01); /* switch back to index mode */
	outb(dev_priv->saveAR_INDEX | 0x20, VGA_AR_INDEX);

	/* VGA color palette registers */
	outb(dev_priv->saveDACMASK, VGA_DACMASK);
	/* DACCRX automatically increments during read */
	outb(0, VGA_DACWX);
	/* Read 3 bytes of color data from each index */
	for (i = 0; i < 256 * 3; i++)
		outb(dev_priv->saveDACDATA[i], VGA_DACDATA);

}

static int i915_suspend(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;

	if (!dev || !dev_priv) {
		printk(KERN_ERR "dev: %p, dev_priv: %p\n", dev, dev_priv);
		printk(KERN_ERR "DRM not initialized, aborting suspend.\n");
		return -ENODEV;
	}

	pci_save_state(dev->pdev);
	pci_read_config_byte(dev->pdev, LBB, &dev_priv->saveLBB);

	/* Pipe & plane A info */
	dev_priv->savePIPEACONF = I915_READ(PIPEACONF);
	dev_priv->savePIPEASRC = I915_READ(PIPEASRC);
	dev_priv->saveFPA0 = I915_READ(FPA0);
	dev_priv->saveFPA1 = I915_READ(FPA1);
	dev_priv->saveDPLL_A = I915_READ(DPLL_A);
	if (IS_I965G(dev))
		dev_priv->saveDPLL_A_MD = I915_READ(DPLL_A_MD);
	dev_priv->saveHTOTAL_A = I915_READ(HTOTAL_A);
	dev_priv->saveHBLANK_A = I915_READ(HBLANK_A);
	dev_priv->saveHSYNC_A = I915_READ(HSYNC_A);
	dev_priv->saveVTOTAL_A = I915_READ(VTOTAL_A);
	dev_priv->saveVBLANK_A = I915_READ(VBLANK_A);
	dev_priv->saveVSYNC_A = I915_READ(VSYNC_A);
	dev_priv->saveBCLRPAT_A = I915_READ(BCLRPAT_A);

	dev_priv->saveDSPACNTR = I915_READ(DSPACNTR);
	dev_priv->saveDSPASTRIDE = I915_READ(DSPASTRIDE);
	dev_priv->saveDSPASIZE = I915_READ(DSPASIZE);
	dev_priv->saveDSPAPOS = I915_READ(DSPAPOS);
	dev_priv->saveDSPABASE = I915_READ(DSPABASE);
	if (IS_I965G(dev)) {
		dev_priv->saveDSPASURF = I915_READ(DSPASURF);
		dev_priv->saveDSPATILEOFF = I915_READ(DSPATILEOFF);
	}
	i915_save_palette(dev, PIPE_A);

	/* Pipe & plane B info */
	dev_priv->savePIPEBCONF = I915_READ(PIPEBCONF);
	dev_priv->savePIPEBSRC = I915_READ(PIPEBSRC);
	dev_priv->saveFPB0 = I915_READ(FPB0);
	dev_priv->saveFPB1 = I915_READ(FPB1);
	dev_priv->saveDPLL_B = I915_READ(DPLL_B);
	if (IS_I965G(dev))
		dev_priv->saveDPLL_B_MD = I915_READ(DPLL_B_MD);
	dev_priv->saveHTOTAL_B = I915_READ(HTOTAL_B);
	dev_priv->saveHBLANK_B = I915_READ(HBLANK_B);
	dev_priv->saveHSYNC_B = I915_READ(HSYNC_B);
	dev_priv->saveVTOTAL_B = I915_READ(VTOTAL_B);
	dev_priv->saveVBLANK_B = I915_READ(VBLANK_B);
	dev_priv->saveVSYNC_B = I915_READ(VSYNC_B);
	dev_priv->saveBCLRPAT_A = I915_READ(BCLRPAT_A);

	dev_priv->saveDSPBCNTR = I915_READ(DSPBCNTR);
	dev_priv->saveDSPBSTRIDE = I915_READ(DSPBSTRIDE);
	dev_priv->saveDSPBSIZE = I915_READ(DSPBSIZE);
	dev_priv->saveDSPBPOS = I915_READ(DSPBPOS);
	dev_priv->saveDSPBBASE = I915_READ(DSPBBASE);
	if (IS_I965GM(dev) || IS_IGD_GM(dev)) {
		dev_priv->saveDSPBSURF = I915_READ(DSPBSURF);
		dev_priv->saveDSPBTILEOFF = I915_READ(DSPBTILEOFF);
	}
	i915_save_palette(dev, PIPE_B);

	/* CRT state */
	dev_priv->saveADPA = I915_READ(ADPA);

	/* LVDS state */
	dev_priv->savePP_CONTROL = I915_READ(PP_CONTROL);
	dev_priv->savePFIT_PGM_RATIOS = I915_READ(PFIT_PGM_RATIOS);
	dev_priv->saveBLC_PWM_CTL = I915_READ(BLC_PWM_CTL);
	if (IS_I965G(dev))
		dev_priv->saveBLC_PWM_CTL2 = I915_READ(BLC_PWM_CTL2);
	if (IS_MOBILE(dev) && !IS_I830(dev))
		dev_priv->saveLVDS = I915_READ(LVDS);
	if (!IS_I830(dev) && !IS_845G(dev))
		dev_priv->savePFIT_CONTROL = I915_READ(PFIT_CONTROL);
	dev_priv->saveLVDSPP_ON = I915_READ(LVDSPP_ON);
	dev_priv->saveLVDSPP_OFF = I915_READ(LVDSPP_OFF);
	dev_priv->savePP_CYCLE = I915_READ(PP_CYCLE);

	/* FIXME: save TV & SDVO state */

	/* FBC state */
	dev_priv->saveFBC_CFB_BASE = I915_READ(FBC_CFB_BASE);
	dev_priv->saveFBC_LL_BASE = I915_READ(FBC_LL_BASE);
	dev_priv->saveFBC_CONTROL2 = I915_READ(FBC_CONTROL2);
	dev_priv->saveFBC_CONTROL = I915_READ(FBC_CONTROL);

	/* VGA state */
	dev_priv->saveVCLK_DIVISOR_VGA0 = I915_READ(VCLK_DIVISOR_VGA0);
	dev_priv->saveVCLK_DIVISOR_VGA1 = I915_READ(VCLK_DIVISOR_VGA1);
	dev_priv->saveVCLK_POST_DIV = I915_READ(VCLK_POST_DIV);
	dev_priv->saveVGACNTRL = I915_READ(VGACNTRL);

	/* Scratch space */
	for (i = 0; i < 16; i++) {
		dev_priv->saveSWF0[i] = I915_READ(SWF0 + (i << 2));
		dev_priv->saveSWF1[i] = I915_READ(SWF10 + (i << 2));
	}
	for (i = 0; i < 3; i++)
		dev_priv->saveSWF2[i] = I915_READ(SWF30 + (i << 2));

	i915_save_vga(dev);

	/* Shut down the device */
	pci_disable_device(dev->pdev);
	pci_set_power_state(dev->pdev, PCI_D3hot);

	return 0;
}

static int i915_resume(struct drm_device *dev)
{
	struct drm_i915_private *dev_priv = dev->dev_private;
	int i;

	pci_set_power_state(dev->pdev, PCI_D0);
	pci_restore_state(dev->pdev);
	if (pci_enable_device(dev->pdev))
		return -1;

	pci_write_config_byte(dev->pdev, LBB, dev_priv->saveLBB);

	/* Pipe & plane A info */
	/* Prime the clock */
	if (dev_priv->saveDPLL_A & DPLL_VCO_ENABLE) {
		I915_WRITE(DPLL_A, dev_priv->saveDPLL_A &
			   ~DPLL_VCO_ENABLE);
		udelay(150);
	}
	I915_WRITE(FPA0, dev_priv->saveFPA0);
	I915_WRITE(FPA1, dev_priv->saveFPA1);
	/* Actually enable it */
	I915_WRITE(DPLL_A, dev_priv->saveDPLL_A);
	udelay(150);
	if (IS_I965G(dev))
		I915_WRITE(DPLL_A_MD, dev_priv->saveDPLL_A_MD);
	udelay(150);

	/* Restore mode */
	I915_WRITE(HTOTAL_A, dev_priv->saveHTOTAL_A);
	I915_WRITE(HBLANK_A, dev_priv->saveHBLANK_A);
	I915_WRITE(HSYNC_A, dev_priv->saveHSYNC_A);
	I915_WRITE(VTOTAL_A, dev_priv->saveVTOTAL_A);
	I915_WRITE(VBLANK_A, dev_priv->saveVBLANK_A);
	I915_WRITE(VSYNC_A, dev_priv->saveVSYNC_A);
	I915_WRITE(BCLRPAT_A, dev_priv->saveBCLRPAT_A);

	/* Restore plane info */
	I915_WRITE(DSPASIZE, dev_priv->saveDSPASIZE);
	I915_WRITE(DSPAPOS, dev_priv->saveDSPAPOS);
	I915_WRITE(PIPEASRC, dev_priv->savePIPEASRC);
	I915_WRITE(DSPABASE, dev_priv->saveDSPABASE);
	I915_WRITE(DSPASTRIDE, dev_priv->saveDSPASTRIDE);
	if (IS_I965G(dev)) {
		I915_WRITE(DSPASURF, dev_priv->saveDSPASURF);
		I915_WRITE(DSPATILEOFF, dev_priv->saveDSPATILEOFF);
	}

	if ((dev_priv->saveDPLL_A & DPLL_VCO_ENABLE) &&
	    (dev_priv->saveDPLL_A & DPLL_VGA_MODE_DIS))
		I915_WRITE(PIPEACONF, dev_priv->savePIPEACONF);

	i915_restore_palette(dev, PIPE_A);
	/* Enable the plane */
	I915_WRITE(DSPACNTR, dev_priv->saveDSPACNTR);
	I915_WRITE(DSPABASE, I915_READ(DSPABASE));

	/* Pipe & plane B info */
	if (dev_priv->saveDPLL_B & DPLL_VCO_ENABLE) {
		I915_WRITE(DPLL_B, dev_priv->saveDPLL_B &
			   ~DPLL_VCO_ENABLE);
		udelay(150);
	}
	I915_WRITE(FPB0, dev_priv->saveFPB0);
	I915_WRITE(FPB1, dev_priv->saveFPB1);
	/* Actually enable it */
	I915_WRITE(DPLL_B, dev_priv->saveDPLL_B);
	udelay(150);
	if (IS_I965G(dev))
		I915_WRITE(DPLL_B_MD, dev_priv->saveDPLL_B_MD);
	udelay(150);

	/* Restore mode */
	I915_WRITE(HTOTAL_B, dev_priv->saveHTOTAL_B);
	I915_WRITE(HBLANK_B, dev_priv->saveHBLANK_B);
	I915_WRITE(HSYNC_B, dev_priv->saveHSYNC_B);
	I915_WRITE(VTOTAL_B, dev_priv->saveVTOTAL_B);
	I915_WRITE(VBLANK_B, dev_priv->saveVBLANK_B);
	I915_WRITE(VSYNC_B, dev_priv->saveVSYNC_B);
	I915_WRITE(BCLRPAT_B, dev_priv->saveBCLRPAT_B);

	/* Restore plane info */
	I915_WRITE(DSPBSIZE, dev_priv->saveDSPBSIZE);
	I915_WRITE(DSPBPOS, dev_priv->saveDSPBPOS);
	I915_WRITE(PIPEBSRC, dev_priv->savePIPEBSRC);
	I915_WRITE(DSPBBASE, dev_priv->saveDSPBBASE);
	I915_WRITE(DSPBSTRIDE, dev_priv->saveDSPBSTRIDE);
	if (IS_I965G(dev)) {
		I915_WRITE(DSPBSURF, dev_priv->saveDSPBSURF);
		I915_WRITE(DSPBTILEOFF, dev_priv->saveDSPBTILEOFF);
	}

	if ((dev_priv->saveDPLL_B & DPLL_VCO_ENABLE) &&
	    (dev_priv->saveDPLL_B & DPLL_VGA_MODE_DIS))
		I915_WRITE(PIPEBCONF, dev_priv->savePIPEBCONF);
	i915_restore_palette(dev, PIPE_A);
	/* Enable the plane */
	I915_WRITE(DSPBCNTR, dev_priv->saveDSPBCNTR);
	I915_WRITE(DSPBBASE, I915_READ(DSPBBASE));

	/* CRT state */
	I915_WRITE(ADPA, dev_priv->saveADPA);

	/* LVDS state */
	if (IS_I965G(dev))
		I915_WRITE(BLC_PWM_CTL2, dev_priv->saveBLC_PWM_CTL2);
	if (IS_MOBILE(dev) && !IS_I830(dev))
		I915_WRITE(LVDS, dev_priv->saveLVDS);
	if (!IS_I830(dev) && !IS_845G(dev))
		I915_WRITE(PFIT_CONTROL, dev_priv->savePFIT_CONTROL);

	I915_WRITE(PFIT_PGM_RATIOS, dev_priv->savePFIT_PGM_RATIOS);
	I915_WRITE(BLC_PWM_CTL, dev_priv->saveBLC_PWM_CTL);
	I915_WRITE(LVDSPP_ON, dev_priv->saveLVDSPP_ON);
	I915_WRITE(LVDSPP_OFF, dev_priv->saveLVDSPP_OFF);
	I915_WRITE(PP_CYCLE, dev_priv->savePP_CYCLE);
	I915_WRITE(PP_CONTROL, dev_priv->savePP_CONTROL);

	/* FIXME: restore TV & SDVO state */

	/* FBC info */
	I915_WRITE(FBC_CFB_BASE, dev_priv->saveFBC_CFB_BASE);
	I915_WRITE(FBC_LL_BASE, dev_priv->saveFBC_LL_BASE);
	I915_WRITE(FBC_CONTROL2, dev_priv->saveFBC_CONTROL2);
	I915_WRITE(FBC_CONTROL, dev_priv->saveFBC_CONTROL);

	/* VGA state */
	I915_WRITE(VGACNTRL, dev_priv->saveVGACNTRL);
	I915_WRITE(VCLK_DIVISOR_VGA0, dev_priv->saveVCLK_DIVISOR_VGA0);
	I915_WRITE(VCLK_DIVISOR_VGA1, dev_priv->saveVCLK_DIVISOR_VGA1);
	I915_WRITE(VCLK_POST_DIV, dev_priv->saveVCLK_POST_DIV);
	udelay(150);

	for (i = 0; i < 16; i++) {
		I915_WRITE(SWF0 + (i << 2), dev_priv->saveSWF0[i]);
		I915_WRITE(SWF10 + (i << 2), dev_priv->saveSWF1[i+7]);
	}
	for (i = 0; i < 3; i++)
		I915_WRITE(SWF30 + (i << 2), dev_priv->saveSWF2[i]);

	i915_restore_vga(dev);

	return 0;
}

static struct drm_driver driver = {
	/* don't use mtrr's here, the Xserver or user space app should
	 * deal with them for intel hardware.
	 */
	.driver_features =
	    DRIVER_USE_AGP | DRIVER_REQUIRE_AGP | /* DRIVER_USE_MTRR |*/
	    DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED | DRIVER_IRQ_VBL |
	    DRIVER_IRQ_VBL2,
	.load = i915_driver_load,
	.unload = i915_driver_unload,
	.lastclose = i915_driver_lastclose,
	.preclose = i915_driver_preclose,
	.suspend = i915_suspend,
	.resume = i915_resume,
	.device_is_agp = i915_driver_device_is_agp,
	.vblank_wait = i915_driver_vblank_wait,
	.vblank_wait2 = i915_driver_vblank_wait2,
	.irq_preinstall = i915_driver_irq_preinstall,
	.irq_postinstall = i915_driver_irq_postinstall,
	.irq_uninstall = i915_driver_irq_uninstall,
	.irq_handler = i915_driver_irq_handler,
	.reclaim_buffers = drm_core_reclaim_buffers,
	.get_map_ofs = drm_core_get_map_ofs,
	.get_reg_ofs = drm_core_get_reg_ofs,
	.ioctls = i915_ioctls,
	.fops = {
		 .owner = THIS_MODULE,
		 .open = drm_open,
		 .release = drm_release,
		 .ioctl = drm_ioctl,
		 .mmap = drm_mmap,
		 .poll = drm_poll,
		 .fasync = drm_fasync,
#ifdef CONFIG_COMPAT
		 .compat_ioctl = i915_compat_ioctl,
#endif
	},

	.pci_driver = {
		 .name = DRIVER_NAME,
		 .id_table = pciidlist,
	},

	.name = DRIVER_NAME,
	.desc = DRIVER_DESC,
	.date = DRIVER_DATE,
	.major = DRIVER_MAJOR,
	.minor = DRIVER_MINOR,
	.patchlevel = DRIVER_PATCHLEVEL,
};

static int __init i915_init(void)
{
	driver.num_ioctls = i915_max_ioctl;
	return drm_init(&driver);
}

static void __exit i915_exit(void)
{
	drm_exit(&driver);
}

module_init(i915_init);
module_exit(i915_exit);

MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL and additional rights");
