// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/console.h>
#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif
#include <linux/platform_device.h>
#include <linux/screen_info.h>
#include <linux/sizes.h>

#include "sm750.h"
#include "ddk750.h"
#include "sm750_accel.h"

void __iomem *mmio750;

int hw_sm750_map(struct sm750_dev *sm750_dev, struct pci_dev *pdev)
{
	int ret;

	ret = 0;

	sm750_dev->vidreg_start  = pci_resource_start(pdev, 1);
	sm750_dev->vidreg_size = SZ_2M;

	pr_info("mmio phyAddr = %lx\n", sm750_dev->vidreg_start);

	/*
	 * reserve the vidreg space of smi adaptor
	 * if you do this, you need to add release region code
	 * in lynxfb_remove, or memory will not be mapped again
	 * successfully
	 */
	ret = pci_request_region(pdev, 1, "sm750fb");
	if (ret) {
		pr_err("Can not request PCI regions.\n");
		goto exit;
	}

	/* now map mmio and vidmem */
	sm750_dev->pvReg = ioremap_nocache(sm750_dev->vidreg_start,
					   sm750_dev->vidreg_size);
	if (!sm750_dev->pvReg) {
		pr_err("mmio failed\n");
		ret = -EFAULT;
		goto exit;
	} else {
		pr_info("mmio virtual addr = %p\n", sm750_dev->pvReg);
	}

	sm750_dev->accel.dprBase = sm750_dev->pvReg + DE_BASE_ADDR_TYPE1;
	sm750_dev->accel.dpPortBase = sm750_dev->pvReg + DE_PORT_ADDR_TYPE1;

	mmio750 = sm750_dev->pvReg;
	sm750_set_chip_type(sm750_dev->devid, sm750_dev->revid);

	sm750_dev->vidmem_start = pci_resource_start(pdev, 0);
	/*
	 * don't use pdev_resource[x].end - resource[x].start to
	 * calculate the resource size, it's only the maximum available
	 * size but not the actual size, using
	 * @ddk750_get_vm_size function can be safe.
	 */
	sm750_dev->vidmem_size = ddk750_get_vm_size();
	pr_info("video memory phyAddr = %lx, size = %u bytes\n",
		sm750_dev->vidmem_start, sm750_dev->vidmem_size);

	/* reserve the vidmem space of smi adaptor */
	sm750_dev->pvMem = ioremap_wc(sm750_dev->vidmem_start,
				      sm750_dev->vidmem_size);
	if (!sm750_dev->pvMem) {
		pr_err("Map video memory failed\n");
		ret = -EFAULT;
		goto exit;
	} else {
		pr_info("video memory vaddr = %p\n", sm750_dev->pvMem);
	}
exit:
	return ret;
}

int hw_sm750_inithw(struct sm750_dev *sm750_dev, struct pci_dev *pdev)
{
	struct init_status *parm;

	parm = &sm750_dev->initParm;
	if (parm->chip_clk == 0)
		parm->chip_clk = (sm750_get_chip_type() == SM750LE) ?
						DEFAULT_SM750LE_CHIP_CLOCK :
						DEFAULT_SM750_CHIP_CLOCK;

	if (parm->mem_clk == 0)
		parm->mem_clk = parm->chip_clk;
	if (parm->master_clk == 0)
		parm->master_clk = parm->chip_clk / 3;

	ddk750_init_hw((struct initchip_param *)&sm750_dev->initParm);
	/* for sm718, open pci burst */
	if (sm750_dev->devid == 0x718) {
		poke32(SYSTEM_CTRL,
		       peek32(SYSTEM_CTRL) | SYSTEM_CTRL_PCI_BURST);
	}

	if (sm750_get_chip_type() != SM750LE) {
		unsigned int val;
		/* does user need CRT? */
		if (sm750_dev->nocrt) {
			poke32(MISC_CTRL,
			       peek32(MISC_CTRL) | MISC_CTRL_DAC_POWER_OFF);
			/* shut off dpms */
			val = peek32(SYSTEM_CTRL) & ~SYSTEM_CTRL_DPMS_MASK;
			val |= SYSTEM_CTRL_DPMS_VPHN;
			poke32(SYSTEM_CTRL, val);
		} else {
			poke32(MISC_CTRL,
			       peek32(MISC_CTRL) & ~MISC_CTRL_DAC_POWER_OFF);
			/* turn on dpms */
			val = peek32(SYSTEM_CTRL) & ~SYSTEM_CTRL_DPMS_MASK;
			val |= SYSTEM_CTRL_DPMS_VPHP;
			poke32(SYSTEM_CTRL, val);
		}

		val = peek32(PANEL_DISPLAY_CTRL) &
			~(PANEL_DISPLAY_CTRL_DUAL_DISPLAY |
			  PANEL_DISPLAY_CTRL_DOUBLE_PIXEL);
		switch (sm750_dev->pnltype) {
		case sm750_24TFT:
			break;
		case sm750_doubleTFT:
			val |= PANEL_DISPLAY_CTRL_DOUBLE_PIXEL;
			break;
		case sm750_dualTFT:
			val |= PANEL_DISPLAY_CTRL_DUAL_DISPLAY;
			break;
		}
		poke32(PANEL_DISPLAY_CTRL, val);
	} else {
		/*
		 * for 750LE, no DVI chip initialization
		 * makes Monitor no signal
		 *
		 * Set up GPIO for software I2C to program DVI chip in the
		 * Xilinx SP605 board, in order to have video signal.
		 */
		sm750_sw_i2c_init(0, 1);

		/*
		 * Customer may NOT use CH7301 DVI chip, which has to be
		 * initialized differently.
		 */
		if (sm750_sw_i2c_read_reg(0xec, 0x4a) == 0x95) {
			/*
			 * The following register values for CH7301 are from
			 * Chrontel app note and our experiment.
			 */
			pr_info("yes,CH7301 DVI chip found\n");
			sm750_sw_i2c_write_reg(0xec, 0x1d, 0x16);
			sm750_sw_i2c_write_reg(0xec, 0x21, 0x9);
			sm750_sw_i2c_write_reg(0xec, 0x49, 0xC0);
			pr_info("okay,CH7301 DVI chip setup done\n");
		}
	}

	/* init 2d engine */
	if (!sm750_dev->accel_off)
		hw_sm750_initAccel(sm750_dev);

	return 0;
}

int hw_sm750_output_setMode(struct lynxfb_output *output,
			    struct fb_var_screeninfo *var,
			    struct fb_fix_screeninfo *fix)
{
	int ret;
	disp_output_t dispSet;
	int channel;

	ret = 0;
	dispSet = 0;
	channel = *output->channel;

	if (sm750_get_chip_type() != SM750LE) {
		if (channel == sm750_primary) {
			pr_info("primary channel\n");
			if (output->paths & sm750_panel)
				dispSet |= do_LCD1_PRI;
			if (output->paths & sm750_crt)
				dispSet |= do_CRT_PRI;

		} else {
			pr_info("secondary channel\n");
			if (output->paths & sm750_panel)
				dispSet |= do_LCD1_SEC;
			if (output->paths & sm750_crt)
				dispSet |= do_CRT_SEC;
		}
		ddk750_setLogicalDispOut(dispSet);
	} else {
		/* just open DISPLAY_CONTROL_750LE register bit 3:0 */
		u32 reg;

		reg = peek32(DISPLAY_CONTROL_750LE);
		reg |= 0xf;
		poke32(DISPLAY_CONTROL_750LE, reg);
	}

	pr_info("ddk setlogicdispout done\n");
	return ret;
}

int hw_sm750_crtc_checkMode(struct lynxfb_crtc *crtc,
			    struct fb_var_screeninfo *var)
{
	struct sm750_dev *sm750_dev;
	struct lynxfb_par *par = container_of(crtc, struct lynxfb_par, crtc);

	sm750_dev = par->dev;

	switch (var->bits_per_pixel) {
	case 8:
	case 16:
		break;
	case 32:
		if (sm750_dev->revid == SM750LE_REVISION_ID) {
			pr_debug("750le do not support 32bpp\n");
			return -EINVAL;
		}
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

/* set the controller's mode for @crtc charged with @var and @fix parameters */
int hw_sm750_crtc_setMode(struct lynxfb_crtc *crtc,
			  struct fb_var_screeninfo *var,
			  struct fb_fix_screeninfo *fix)
{
	int ret, fmt;
	u32 reg;
	struct mode_parameter modparm;
	clock_type_t clock;
	struct sm750_dev *sm750_dev;
	struct lynxfb_par *par;

	ret = 0;
	par = container_of(crtc, struct lynxfb_par, crtc);
	sm750_dev = par->dev;

	if (!sm750_dev->accel_off) {
		/* set 2d engine pixel format according to mode bpp */
		switch (var->bits_per_pixel) {
		case 8:
			fmt = 0;
			break;
		case 16:
			fmt = 1;
			break;
		case 32:
		default:
			fmt = 2;
			break;
		}
		sm750_hw_set2dformat(&sm750_dev->accel, fmt);
	}

	/* set timing */
	modparm.pixel_clock = ps_to_hz(var->pixclock);
	modparm.vertical_sync_polarity = (var->sync & FB_SYNC_HOR_HIGH_ACT)
					 ? POS : NEG;
	modparm.horizontal_sync_polarity = (var->sync & FB_SYNC_VERT_HIGH_ACT)
					   ? POS : NEG;
	modparm.clock_phase_polarity = (var->sync & FB_SYNC_COMP_HIGH_ACT)
				       ? POS : NEG;
	modparm.horizontal_display_end = var->xres;
	modparm.horizontal_sync_width = var->hsync_len;
	modparm.horizontal_sync_start = var->xres + var->right_margin;
	modparm.horizontal_total = var->xres + var->left_margin +
				   var->right_margin + var->hsync_len;
	modparm.vertical_display_end = var->yres;
	modparm.vertical_sync_height = var->vsync_len;
	modparm.vertical_sync_start = var->yres + var->lower_margin;
	modparm.vertical_total = var->yres + var->upper_margin +
				 var->lower_margin + var->vsync_len;

	/* choose pll */
	if (crtc->channel != sm750_secondary)
		clock = PRIMARY_PLL;
	else
		clock = SECONDARY_PLL;

	pr_debug("Request pixel clock = %lu\n", modparm.pixel_clock);
	ret = ddk750_setModeTiming(&modparm, clock);
	if (ret) {
		pr_err("Set mode timing failed\n");
		goto exit;
	}

	if (crtc->channel != sm750_secondary) {
		/* set pitch, offset, width, start address, etc... */
		poke32(PANEL_FB_ADDRESS,
		       crtc->oScreen & PANEL_FB_ADDRESS_ADDRESS_MASK);

		reg = var->xres * (var->bits_per_pixel >> 3);
		/*
		 * crtc->channel is not equal to par->index on numeric,
		 * be aware of that
		 */
		reg = ALIGN(reg, crtc->line_pad);
		reg = (reg << PANEL_FB_WIDTH_WIDTH_SHIFT) &
		       PANEL_FB_WIDTH_WIDTH_MASK;
		reg |= (fix->line_length & PANEL_FB_WIDTH_OFFSET_MASK);
		poke32(PANEL_FB_WIDTH, reg);

		reg = ((var->xres - 1) << PANEL_WINDOW_WIDTH_WIDTH_SHIFT) &
		       PANEL_WINDOW_WIDTH_WIDTH_MASK;
		reg |= (var->xoffset & PANEL_WINDOW_WIDTH_X_MASK);
		poke32(PANEL_WINDOW_WIDTH, reg);

		reg = (var->yres_virtual - 1) <<
		      PANEL_WINDOW_HEIGHT_HEIGHT_SHIFT;
		reg &= PANEL_WINDOW_HEIGHT_HEIGHT_MASK;
		reg |= (var->yoffset & PANEL_WINDOW_HEIGHT_Y_MASK);
		poke32(PANEL_WINDOW_HEIGHT, reg);

		poke32(PANEL_PLANE_TL, 0);

		reg = ((var->yres - 1) << PANEL_PLANE_BR_BOTTOM_SHIFT) &
		       PANEL_PLANE_BR_BOTTOM_MASK;
		reg |= ((var->xres - 1) & PANEL_PLANE_BR_RIGHT_MASK);
		poke32(PANEL_PLANE_BR, reg);

		/* set pixel format */
		reg = peek32(PANEL_DISPLAY_CTRL);
		poke32(PANEL_DISPLAY_CTRL, reg | (var->bits_per_pixel >> 4));
	} else {
		/* not implemented now */
		poke32(CRT_FB_ADDRESS, crtc->oScreen);
		reg = var->xres * (var->bits_per_pixel >> 3);
		/*
		 * crtc->channel is not equal to par->index on numeric,
		 * be aware of that
		 */
		reg = ALIGN(reg, crtc->line_pad) << CRT_FB_WIDTH_WIDTH_SHIFT;
		reg &= CRT_FB_WIDTH_WIDTH_MASK;
		reg |= (fix->line_length & CRT_FB_WIDTH_OFFSET_MASK);
		poke32(CRT_FB_WIDTH, reg);

		/* SET PIXEL FORMAT */
		reg = peek32(CRT_DISPLAY_CTRL);
		reg |= ((var->bits_per_pixel >> 4) &
			CRT_DISPLAY_CTRL_FORMAT_MASK);
		poke32(CRT_DISPLAY_CTRL, reg);
	}

exit:
	return ret;
}

int hw_sm750_setColReg(struct lynxfb_crtc *crtc, ushort index,
		       ushort red, ushort green, ushort blue)
{
	static unsigned int add[] = {PANEL_PALETTE_RAM, CRT_PALETTE_RAM};

	poke32(add[crtc->channel] + index * 4,
	       (red << 16) | (green << 8) | blue);
	return 0;
}

int hw_sm750le_setBLANK(struct lynxfb_output *output, int blank)
{
	int dpms, crtdb;

	switch (blank) {
	case FB_BLANK_UNBLANK:
		dpms = CRT_DISPLAY_CTRL_DPMS_0;
		crtdb = 0;
		break;
	case FB_BLANK_NORMAL:
		dpms = CRT_DISPLAY_CTRL_DPMS_0;
		crtdb = CRT_DISPLAY_CTRL_BLANK;
		break;
	case FB_BLANK_VSYNC_SUSPEND:
		dpms = CRT_DISPLAY_CTRL_DPMS_2;
		crtdb = CRT_DISPLAY_CTRL_BLANK;
		break;
	case FB_BLANK_HSYNC_SUSPEND:
		dpms = CRT_DISPLAY_CTRL_DPMS_1;
		crtdb = CRT_DISPLAY_CTRL_BLANK;
		break;
	case FB_BLANK_POWERDOWN:
		dpms = CRT_DISPLAY_CTRL_DPMS_3;
		crtdb = CRT_DISPLAY_CTRL_BLANK;
		break;
	default:
		return -EINVAL;
	}

	if (output->paths & sm750_crt) {
		unsigned int val;

		val = peek32(CRT_DISPLAY_CTRL) & ~CRT_DISPLAY_CTRL_DPMS_MASK;
		poke32(CRT_DISPLAY_CTRL, val | dpms);

		val = peek32(CRT_DISPLAY_CTRL) & ~CRT_DISPLAY_CTRL_BLANK;
		poke32(CRT_DISPLAY_CTRL, val | crtdb);
	}
	return 0;
}

int hw_sm750_setBLANK(struct lynxfb_output *output, int blank)
{
	unsigned int dpms, pps, crtdb;

	dpms = 0;
	pps = 0;
	crtdb = 0;

	switch (blank) {
	case FB_BLANK_UNBLANK:
		pr_debug("flag = FB_BLANK_UNBLANK\n");
		dpms = SYSTEM_CTRL_DPMS_VPHP;
		pps = PANEL_DISPLAY_CTRL_DATA;
		break;
	case FB_BLANK_NORMAL:
		pr_debug("flag = FB_BLANK_NORMAL\n");
		dpms = SYSTEM_CTRL_DPMS_VPHP;
		crtdb = CRT_DISPLAY_CTRL_BLANK;
		break;
	case FB_BLANK_VSYNC_SUSPEND:
		dpms = SYSTEM_CTRL_DPMS_VNHP;
		crtdb = CRT_DISPLAY_CTRL_BLANK;
		break;
	case FB_BLANK_HSYNC_SUSPEND:
		dpms = SYSTEM_CTRL_DPMS_VPHN;
		crtdb = CRT_DISPLAY_CTRL_BLANK;
		break;
	case FB_BLANK_POWERDOWN:
		dpms = SYSTEM_CTRL_DPMS_VNHN;
		crtdb = CRT_DISPLAY_CTRL_BLANK;
		break;
	}

	if (output->paths & sm750_crt) {
		unsigned int val = peek32(SYSTEM_CTRL) & ~SYSTEM_CTRL_DPMS_MASK;

		poke32(SYSTEM_CTRL, val | dpms);

		val = peek32(CRT_DISPLAY_CTRL) & ~CRT_DISPLAY_CTRL_BLANK;
		poke32(CRT_DISPLAY_CTRL, val | crtdb);
	}

	if (output->paths & sm750_panel) {
		unsigned int val = peek32(PANEL_DISPLAY_CTRL);

		val &= ~PANEL_DISPLAY_CTRL_DATA;
		val |= pps;
		poke32(PANEL_DISPLAY_CTRL, val);
	}

	return 0;
}

void hw_sm750_initAccel(struct sm750_dev *sm750_dev)
{
	u32 reg;

	sm750_enable_2d_engine(1);

	if (sm750_get_chip_type() == SM750LE) {
		reg = peek32(DE_STATE1);
		reg |= DE_STATE1_DE_ABORT;
		poke32(DE_STATE1, reg);

		reg = peek32(DE_STATE1);
		reg &= ~DE_STATE1_DE_ABORT;
		poke32(DE_STATE1, reg);

	} else {
		/* engine reset */
		reg = peek32(SYSTEM_CTRL);
		reg |= SYSTEM_CTRL_DE_ABORT;
		poke32(SYSTEM_CTRL, reg);

		reg = peek32(SYSTEM_CTRL);
		reg &= ~SYSTEM_CTRL_DE_ABORT;
		poke32(SYSTEM_CTRL, reg);
	}

	/* call 2d init */
	sm750_dev->accel.de_init(&sm750_dev->accel);
}

int hw_sm750le_deWait(void)
{
	int i = 0x10000000;
	unsigned int mask = DE_STATE2_DE_STATUS_BUSY | DE_STATE2_DE_FIFO_EMPTY |
		DE_STATE2_DE_MEM_FIFO_EMPTY;

	while (i--) {
		unsigned int val = peek32(DE_STATE2);

		if ((val & mask) ==
		    (DE_STATE2_DE_FIFO_EMPTY | DE_STATE2_DE_MEM_FIFO_EMPTY))
			return 0;
	}
	/* timeout error */
	return -1;
}

int hw_sm750_deWait(void)
{
	int i = 0x10000000;
	unsigned int mask = SYSTEM_CTRL_DE_STATUS_BUSY |
		SYSTEM_CTRL_DE_FIFO_EMPTY |
		SYSTEM_CTRL_DE_MEM_FIFO_EMPTY;

	while (i--) {
		unsigned int val = peek32(SYSTEM_CTRL);

		if ((val & mask) ==
		    (SYSTEM_CTRL_DE_FIFO_EMPTY | SYSTEM_CTRL_DE_MEM_FIFO_EMPTY))
			return 0;
	}
	/* timeout error */
	return -1;
}

int hw_sm750_pan_display(struct lynxfb_crtc *crtc,
			 const struct fb_var_screeninfo *var,
			 const struct fb_info *info)
{
	u32 total;
	/* check params */
	if ((var->xoffset + var->xres > var->xres_virtual) ||
	    (var->yoffset + var->yres > var->yres_virtual)) {
		return -EINVAL;
	}

	total = var->yoffset * info->fix.line_length +
		((var->xoffset * var->bits_per_pixel) >> 3);
	total += crtc->oScreen;
	if (crtc->channel == sm750_primary) {
		poke32(PANEL_FB_ADDRESS,
		       peek32(PANEL_FB_ADDRESS) |
		       (total & PANEL_FB_ADDRESS_ADDRESS_MASK));
	} else {
		poke32(CRT_FB_ADDRESS,
		       peek32(CRT_FB_ADDRESS) |
		       (total & CRT_FB_ADDRESS_ADDRESS_MASK));
	}
	return 0;
}
