/*
 *
 * Hardware accelerated Matrox Millennium I, II, Mystique, G100, G200, G400 and G450.
 *
 * (c) 1998-2002 Petr Vandrovec <vandrove@vc.cvut.cz>
 *
 * Portions Copyright (c) 2001 Matrox Graphics Inc.
 *
 * Version: 1.65 2002/08/14
 *
 * See matroxfb_base.c for contributors.
 *
 */

#include <linux/config.h>

#include "matroxfb_DAC1064.h"
#include "matroxfb_misc.h"
#include "matroxfb_accel.h"
#include "g450_pll.h"
#include <linux/matroxfb.h>

#ifdef NEED_DAC1064
#define outDAC1064 matroxfb_DAC_out
#define inDAC1064 matroxfb_DAC_in

#define DAC1064_OPT_SCLK_PCI	0x00
#define DAC1064_OPT_SCLK_PLL	0x01
#define DAC1064_OPT_SCLK_EXT	0x02
#define DAC1064_OPT_SCLK_MASK	0x03
#define DAC1064_OPT_GDIV1	0x04	/* maybe it is GDIV2 on G100 ?! */
#define DAC1064_OPT_GDIV3	0x00
#define DAC1064_OPT_MDIV1	0x08
#define DAC1064_OPT_MDIV2	0x00
#define DAC1064_OPT_RESERVED	0x10

static void DAC1064_calcclock(CPMINFO unsigned int freq, unsigned int fmax, unsigned int* in, unsigned int* feed, unsigned int* post) {
	unsigned int fvco;
	unsigned int p;

	DBG(__FUNCTION__)
	
	/* only for devices older than G450 */

	fvco = PLL_calcclock(PMINFO freq, fmax, in, feed, &p);
	
	p = (1 << p) - 1;
	if (fvco <= 100000)
		;
	else if (fvco <= 140000)
		p |= 0x08;
	else if (fvco <= 180000)
		p |= 0x10;
	else
		p |= 0x18;
	*post = p;
}

/* they must be in POS order */
static const unsigned char MGA1064_DAC_regs[] = {
		M1064_XCURADDL, M1064_XCURADDH, M1064_XCURCTRL,
		M1064_XCURCOL0RED, M1064_XCURCOL0GREEN, M1064_XCURCOL0BLUE,
		M1064_XCURCOL1RED, M1064_XCURCOL1GREEN, M1064_XCURCOL1BLUE,
		M1064_XCURCOL2RED, M1064_XCURCOL2GREEN, M1064_XCURCOL2BLUE,
		DAC1064_XVREFCTRL, M1064_XMULCTRL, M1064_XPIXCLKCTRL, M1064_XGENCTRL,
		M1064_XMISCCTRL,
		M1064_XGENIOCTRL, M1064_XGENIODATA, M1064_XZOOMCTRL, M1064_XSENSETEST,
		M1064_XCRCBITSEL,
		M1064_XCOLKEYMASKL, M1064_XCOLKEYMASKH, M1064_XCOLKEYL, M1064_XCOLKEYH };

static const unsigned char MGA1064_DAC[] = {
		0x00, 0x00, M1064_XCURCTRL_DIS,
		0x00, 0x00, 0x00, 	/* black */
		0xFF, 0xFF, 0xFF,	/* white */
		0xFF, 0x00, 0x00,	/* red */
		0x00, 0,
		M1064_XPIXCLKCTRL_PLL_UP | M1064_XPIXCLKCTRL_EN | M1064_XPIXCLKCTRL_SRC_PLL,
		M1064_XGENCTRL_VS_0 | M1064_XGENCTRL_ALPHA_DIS | M1064_XGENCTRL_BLACK_0IRE | M1064_XGENCTRL_NO_SYNC_ON_GREEN,
		M1064_XMISCCTRL_DAC_8BIT,
		0x00, 0x00, M1064_XZOOMCTRL_1, M1064_XSENSETEST_BCOMP | M1064_XSENSETEST_GCOMP | M1064_XSENSETEST_RCOMP | M1064_XSENSETEST_PDOWN,
		0x00,
		0x00, 0x00, 0xFF, 0xFF};

static void DAC1064_setpclk(WPMINFO unsigned long fout) {
	unsigned int m, n, p;

	DBG(__FUNCTION__)

	DAC1064_calcclock(PMINFO fout, ACCESS_FBINFO(max_pixel_clock), &m, &n, &p);
	ACCESS_FBINFO(hw).DACclk[0] = m;
	ACCESS_FBINFO(hw).DACclk[1] = n;
	ACCESS_FBINFO(hw).DACclk[2] = p;
}

static void DAC1064_setmclk(WPMINFO int oscinfo, unsigned long fmem) {
	u_int32_t mx;
	struct matrox_hw_state* hw = &ACCESS_FBINFO(hw);

	DBG(__FUNCTION__)

	if (ACCESS_FBINFO(devflags.noinit)) {
		/* read MCLK and give up... */
		hw->DACclk[3] = inDAC1064(PMINFO DAC1064_XSYSPLLM);
		hw->DACclk[4] = inDAC1064(PMINFO DAC1064_XSYSPLLN);
		hw->DACclk[5] = inDAC1064(PMINFO DAC1064_XSYSPLLP);
		return;
	}
	mx = hw->MXoptionReg | 0x00000004;
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, mx);
	mx &= ~0x000000BB;
	if (oscinfo & DAC1064_OPT_GDIV1)
		mx |= 0x00000008;
	if (oscinfo & DAC1064_OPT_MDIV1)
		mx |= 0x00000010;
	if (oscinfo & DAC1064_OPT_RESERVED)
		mx |= 0x00000080;
	if ((oscinfo & DAC1064_OPT_SCLK_MASK) == DAC1064_OPT_SCLK_PLL) {
		/* select PCI clock until we have setup oscilator... */
		int clk;
		unsigned int m, n, p;

		/* powerup system PLL, select PCI clock */
		mx |= 0x00000020;
		pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, mx);
		mx &= ~0x00000004;
		pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, mx);

		/* !!! you must not access device if MCLK is not running !!!
		   Doing so cause immediate PCI lockup :-( Maybe they should
		   generate ABORT or I/O (parity...) error and Linux should
		   recover from this... (kill driver/process). But world is not
		   perfect... */
		/* (bit 2 of PCI_OPTION_REG must be 0... and bits 0,1 must not
		   select PLL... because of PLL can be stopped at this time) */
		DAC1064_calcclock(PMINFO fmem, ACCESS_FBINFO(max_pixel_clock), &m, &n, &p);
		outDAC1064(PMINFO DAC1064_XSYSPLLM, hw->DACclk[3] = m);
		outDAC1064(PMINFO DAC1064_XSYSPLLN, hw->DACclk[4] = n);
		outDAC1064(PMINFO DAC1064_XSYSPLLP, hw->DACclk[5] = p);
		for (clk = 65536; clk; --clk) {
			if (inDAC1064(PMINFO DAC1064_XSYSPLLSTAT) & 0x40)
				break;
		}
		if (!clk)
			printk(KERN_ERR "matroxfb: aiee, SYSPLL not locked\n");
		/* select PLL */
		mx |= 0x00000005;
	} else {
		/* select specified system clock source */
		mx |= oscinfo & DAC1064_OPT_SCLK_MASK;
	}
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, mx);
	mx &= ~0x00000004;
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, mx);
	hw->MXoptionReg = mx;
}

#ifdef CONFIG_FB_MATROX_G
static void g450_set_plls(WPMINFO2) {
	u_int32_t c2_ctl;
	unsigned int pxc;
	struct matrox_hw_state* hw = &ACCESS_FBINFO(hw);
	int pixelmnp;
	int videomnp;
	
	c2_ctl = hw->crtc2.ctl & ~0x4007;	/* Clear PLL + enable for CRTC2 */
	c2_ctl |= 0x0001;			/* Enable CRTC2 */
	hw->DACreg[POS1064_XPWRCTRL] &= ~0x02;	/* Stop VIDEO PLL */
	pixelmnp = ACCESS_FBINFO(crtc1).mnp;
	videomnp = ACCESS_FBINFO(crtc2).mnp;
	if (videomnp < 0) {
		c2_ctl &= ~0x0001;			/* Disable CRTC2 */
		hw->DACreg[POS1064_XPWRCTRL] &= ~0x10;	/* Powerdown CRTC2 */
	} else if (ACCESS_FBINFO(crtc2).pixclock == ACCESS_FBINFO(features).pll.ref_freq) {
		c2_ctl |=  0x4002;	/* Use reference directly */
	} else if (videomnp == pixelmnp) {
		c2_ctl |=  0x0004;	/* Use pixel PLL */
	} else {
		if (0 == ((videomnp ^ pixelmnp) & 0xFFFFFF00)) {
			/* PIXEL and VIDEO PLL must not use same frequency. We modify N
			   of PIXEL PLL in such case because of VIDEO PLL may be source
			   of TVO clocks, and chroma subcarrier is derived from its
			   pixel clocks */
			pixelmnp += 0x000100;
		}
		c2_ctl |=  0x0006;	/* Use video PLL */
		hw->DACreg[POS1064_XPWRCTRL] |= 0x02;
		
		outDAC1064(PMINFO M1064_XPWRCTRL, hw->DACreg[POS1064_XPWRCTRL]);
		matroxfb_g450_setpll_cond(PMINFO videomnp, M_VIDEO_PLL);
	}

	hw->DACreg[POS1064_XPIXCLKCTRL] &= ~M1064_XPIXCLKCTRL_PLL_UP;
	if (pixelmnp >= 0) {
		hw->DACreg[POS1064_XPIXCLKCTRL] |= M1064_XPIXCLKCTRL_PLL_UP;
		
		outDAC1064(PMINFO M1064_XPIXCLKCTRL, hw->DACreg[POS1064_XPIXCLKCTRL]);
		matroxfb_g450_setpll_cond(PMINFO pixelmnp, M_PIXEL_PLL_C);
	}
	if (c2_ctl != hw->crtc2.ctl) {
		hw->crtc2.ctl = c2_ctl;
		mga_outl(0x3C10, c2_ctl);
	}

	pxc = ACCESS_FBINFO(crtc1).pixclock;
	if (pxc == 0 || ACCESS_FBINFO(outputs[2]).src == MATROXFB_SRC_CRTC2) {
		pxc = ACCESS_FBINFO(crtc2).pixclock;
	}
	if (ACCESS_FBINFO(chip) == MGA_G550) {
		if (pxc < 45000) {
			hw->DACreg[POS1064_XPANMODE] = 0x00;	/* 0-50 */
		} else if (pxc < 55000) {
			hw->DACreg[POS1064_XPANMODE] = 0x08;	/* 34-62 */
		} else if (pxc < 70000) {
			hw->DACreg[POS1064_XPANMODE] = 0x10;	/* 42-78 */
		} else if (pxc < 85000) {
			hw->DACreg[POS1064_XPANMODE] = 0x18;	/* 62-92 */
		} else if (pxc < 100000) {
			hw->DACreg[POS1064_XPANMODE] = 0x20;	/* 74-108 */
		} else if (pxc < 115000) {
			hw->DACreg[POS1064_XPANMODE] = 0x28;	/* 94-122 */
		} else if (pxc < 125000) {
			hw->DACreg[POS1064_XPANMODE] = 0x30;	/* 108-132 */
		} else {
			hw->DACreg[POS1064_XPANMODE] = 0x38;	/* 120-168 */
		}
	} else {
		/* G450 */
		if (pxc < 45000) {
			hw->DACreg[POS1064_XPANMODE] = 0x00;	/* 0-54 */
		} else if (pxc < 65000) {
			hw->DACreg[POS1064_XPANMODE] = 0x08;	/* 38-70 */
		} else if (pxc < 85000) {
			hw->DACreg[POS1064_XPANMODE] = 0x10;	/* 56-96 */
		} else if (pxc < 105000) {
			hw->DACreg[POS1064_XPANMODE] = 0x18;	/* 80-114 */
		} else if (pxc < 135000) {
			hw->DACreg[POS1064_XPANMODE] = 0x20;	/* 102-144 */
		} else if (pxc < 160000) {
			hw->DACreg[POS1064_XPANMODE] = 0x28;	/* 132-166 */
		} else if (pxc < 175000) {
			hw->DACreg[POS1064_XPANMODE] = 0x30;	/* 154-182 */
		} else {
			hw->DACreg[POS1064_XPANMODE] = 0x38;	/* 170-204 */
		}
	}
}
#endif

void DAC1064_global_init(WPMINFO2) {
	struct matrox_hw_state* hw = &ACCESS_FBINFO(hw);

	hw->DACreg[POS1064_XMISCCTRL] &= M1064_XMISCCTRL_DAC_WIDTHMASK;
	hw->DACreg[POS1064_XMISCCTRL] |= M1064_XMISCCTRL_LUT_EN;
	hw->DACreg[POS1064_XPIXCLKCTRL] = M1064_XPIXCLKCTRL_PLL_UP | M1064_XPIXCLKCTRL_EN | M1064_XPIXCLKCTRL_SRC_PLL;
#ifdef CONFIG_FB_MATROX_G
	if (ACCESS_FBINFO(devflags.g450dac)) {
		hw->DACreg[POS1064_XPWRCTRL] = 0x1F;	/* powerup everything */
		hw->DACreg[POS1064_XOUTPUTCONN] = 0x00;	/* disable outputs */
		hw->DACreg[POS1064_XMISCCTRL] |= M1064_XMISCCTRL_DAC_EN;
		switch (ACCESS_FBINFO(outputs[0]).src) {
			case MATROXFB_SRC_CRTC1:
			case MATROXFB_SRC_CRTC2:
				hw->DACreg[POS1064_XOUTPUTCONN] |= 0x01;	/* enable output; CRTC1/2 selection is in CRTC2 ctl */
				break;
			case MATROXFB_SRC_NONE:
				hw->DACreg[POS1064_XMISCCTRL] &= ~M1064_XMISCCTRL_DAC_EN;
				break;
		}
		switch (ACCESS_FBINFO(outputs[1]).src) {
			case MATROXFB_SRC_CRTC1:
				hw->DACreg[POS1064_XOUTPUTCONN] |= 0x04;
				break;
			case MATROXFB_SRC_CRTC2:
				if (ACCESS_FBINFO(outputs[1]).mode == MATROXFB_OUTPUT_MODE_MONITOR) {
					hw->DACreg[POS1064_XOUTPUTCONN] |= 0x08;
				} else {
					hw->DACreg[POS1064_XOUTPUTCONN] |= 0x0C;
				}
				break;
			case MATROXFB_SRC_NONE:
				hw->DACreg[POS1064_XPWRCTRL] &= ~0x01;		/* Poweroff DAC2 */
				break;
		}
		switch (ACCESS_FBINFO(outputs[2]).src) {
			case MATROXFB_SRC_CRTC1:
				hw->DACreg[POS1064_XOUTPUTCONN] |= 0x20;
				break;
			case MATROXFB_SRC_CRTC2:
				hw->DACreg[POS1064_XOUTPUTCONN] |= 0x40;
				break;
			case MATROXFB_SRC_NONE:
#if 0
				/* HELP! If we boot without DFP connected to DVI, we can
				   poweroff TMDS. But if we boot with DFP connected,
				   TMDS generated clocks are used instead of ALL pixclocks
				   available... If someone knows which register
				   handles it, please reveal this secret to me... */			
				hw->DACreg[POS1064_XPWRCTRL] &= ~0x04;		/* Poweroff TMDS */
#endif				
				break;
		}
		/* Now set timming related variables... */
		g450_set_plls(PMINFO2);
	} else
#endif
	{
		if (ACCESS_FBINFO(outputs[1]).src == MATROXFB_SRC_CRTC1) {
			hw->DACreg[POS1064_XPIXCLKCTRL] = M1064_XPIXCLKCTRL_PLL_UP | M1064_XPIXCLKCTRL_EN | M1064_XPIXCLKCTRL_SRC_EXT;
			hw->DACreg[POS1064_XMISCCTRL] |= GX00_XMISCCTRL_MFC_MAFC | G400_XMISCCTRL_VDO_MAFC12;
		} else if (ACCESS_FBINFO(outputs[1]).src == MATROXFB_SRC_CRTC2) {
			hw->DACreg[POS1064_XMISCCTRL] |= GX00_XMISCCTRL_MFC_MAFC | G400_XMISCCTRL_VDO_C2_MAFC12;
		} else if (ACCESS_FBINFO(outputs[2]).src == MATROXFB_SRC_CRTC1)
			hw->DACreg[POS1064_XMISCCTRL] |= GX00_XMISCCTRL_MFC_PANELLINK | G400_XMISCCTRL_VDO_MAFC12;
		else
			hw->DACreg[POS1064_XMISCCTRL] |= GX00_XMISCCTRL_MFC_DIS;

		if (ACCESS_FBINFO(outputs[0]).src != MATROXFB_SRC_NONE)
			hw->DACreg[POS1064_XMISCCTRL] |= M1064_XMISCCTRL_DAC_EN;
	}
}

void DAC1064_global_restore(WPMINFO2) {
	struct matrox_hw_state* hw = &ACCESS_FBINFO(hw);

	outDAC1064(PMINFO M1064_XPIXCLKCTRL, hw->DACreg[POS1064_XPIXCLKCTRL]);
	outDAC1064(PMINFO M1064_XMISCCTRL, hw->DACreg[POS1064_XMISCCTRL]);
	if (ACCESS_FBINFO(devflags.accelerator) == FB_ACCEL_MATROX_MGAG400) {
		outDAC1064(PMINFO 0x20, 0x04);
		outDAC1064(PMINFO 0x1F, ACCESS_FBINFO(devflags.dfp_type));
		if (ACCESS_FBINFO(devflags.g450dac)) {
			outDAC1064(PMINFO M1064_XSYNCCTRL, 0xCC);
			outDAC1064(PMINFO M1064_XPWRCTRL, hw->DACreg[POS1064_XPWRCTRL]);
			outDAC1064(PMINFO M1064_XPANMODE, hw->DACreg[POS1064_XPANMODE]);
			outDAC1064(PMINFO M1064_XOUTPUTCONN, hw->DACreg[POS1064_XOUTPUTCONN]);
		}
	}
}

static int DAC1064_init_1(WPMINFO struct my_timming* m) {
	struct matrox_hw_state* hw = &ACCESS_FBINFO(hw);

	DBG(__FUNCTION__)

	memcpy(hw->DACreg, MGA1064_DAC, sizeof(MGA1064_DAC_regs));
	switch (ACCESS_FBINFO(fbcon).var.bits_per_pixel) {
		/* case 4: not supported by MGA1064 DAC */
		case 8:
			hw->DACreg[POS1064_XMULCTRL] = M1064_XMULCTRL_DEPTH_8BPP | M1064_XMULCTRL_GRAPHICS_PALETIZED;
			break;
		case 16:
			if (ACCESS_FBINFO(fbcon).var.green.length == 5)
				hw->DACreg[POS1064_XMULCTRL] = M1064_XMULCTRL_DEPTH_15BPP_1BPP | M1064_XMULCTRL_GRAPHICS_PALETIZED;
			else
				hw->DACreg[POS1064_XMULCTRL] = M1064_XMULCTRL_DEPTH_16BPP | M1064_XMULCTRL_GRAPHICS_PALETIZED;
			break;
		case 24:
			hw->DACreg[POS1064_XMULCTRL] = M1064_XMULCTRL_DEPTH_24BPP | M1064_XMULCTRL_GRAPHICS_PALETIZED;
			break;
		case 32:
			hw->DACreg[POS1064_XMULCTRL] = M1064_XMULCTRL_DEPTH_32BPP | M1064_XMULCTRL_GRAPHICS_PALETIZED;
			break;
		default:
			return 1;	/* unsupported depth */
	}
	hw->DACreg[POS1064_XVREFCTRL] = ACCESS_FBINFO(features.DAC1064.xvrefctrl);
	hw->DACreg[POS1064_XGENCTRL] &= ~M1064_XGENCTRL_SYNC_ON_GREEN_MASK;
	hw->DACreg[POS1064_XGENCTRL] |= (m->sync & FB_SYNC_ON_GREEN)?M1064_XGENCTRL_SYNC_ON_GREEN:M1064_XGENCTRL_NO_SYNC_ON_GREEN;
	hw->DACreg[POS1064_XCURADDL] = 0;
	hw->DACreg[POS1064_XCURADDH] = 0;

	DAC1064_global_init(PMINFO2);
	return 0;
}

static int DAC1064_init_2(WPMINFO struct my_timming* m) {
	struct matrox_hw_state* hw = &ACCESS_FBINFO(hw);

	DBG(__FUNCTION__)

	if (ACCESS_FBINFO(fbcon).var.bits_per_pixel > 16) {	/* 256 entries */
		int i;

		for (i = 0; i < 256; i++) {
			hw->DACpal[i * 3 + 0] = i;
			hw->DACpal[i * 3 + 1] = i;
			hw->DACpal[i * 3 + 2] = i;
		}
	} else if (ACCESS_FBINFO(fbcon).var.bits_per_pixel > 8) {
		if (ACCESS_FBINFO(fbcon).var.green.length == 5) {	/* 0..31, 128..159 */
			int i;

			for (i = 0; i < 32; i++) {
				/* with p15 == 0 */
				hw->DACpal[i * 3 + 0] = i << 3;
				hw->DACpal[i * 3 + 1] = i << 3;
				hw->DACpal[i * 3 + 2] = i << 3;
				/* with p15 == 1 */
				hw->DACpal[(i + 128) * 3 + 0] = i << 3;
				hw->DACpal[(i + 128) * 3 + 1] = i << 3;
				hw->DACpal[(i + 128) * 3 + 2] = i << 3;
			}
		} else {
			int i;

			for (i = 0; i < 64; i++) {		/* 0..63 */
				hw->DACpal[i * 3 + 0] = i << 3;
				hw->DACpal[i * 3 + 1] = i << 2;
				hw->DACpal[i * 3 + 2] = i << 3;
			}
		}
	} else {
		memset(hw->DACpal, 0, 768);
	}
	return 0;
}

static void DAC1064_restore_1(WPMINFO2) {
	struct matrox_hw_state* hw = &ACCESS_FBINFO(hw);

	CRITFLAGS

	DBG(__FUNCTION__)

	CRITBEGIN

	if ((inDAC1064(PMINFO DAC1064_XSYSPLLM) != hw->DACclk[3]) ||
	    (inDAC1064(PMINFO DAC1064_XSYSPLLN) != hw->DACclk[4]) ||
	    (inDAC1064(PMINFO DAC1064_XSYSPLLP) != hw->DACclk[5])) {
		outDAC1064(PMINFO DAC1064_XSYSPLLM, hw->DACclk[3]);
		outDAC1064(PMINFO DAC1064_XSYSPLLN, hw->DACclk[4]);
		outDAC1064(PMINFO DAC1064_XSYSPLLP, hw->DACclk[5]);
	}
	{
		unsigned int i;

		for (i = 0; i < sizeof(MGA1064_DAC_regs); i++) {
			if ((i != POS1064_XPIXCLKCTRL) && (i != POS1064_XMISCCTRL))
				outDAC1064(PMINFO MGA1064_DAC_regs[i], hw->DACreg[i]);
		}
	}

	DAC1064_global_restore(PMINFO2);

	CRITEND
};

static void DAC1064_restore_2(WPMINFO2) {
#ifdef DEBUG
	unsigned int i;
#endif

	DBG(__FUNCTION__)

#ifdef DEBUG
	dprintk(KERN_DEBUG "DAC1064regs ");
	for (i = 0; i < sizeof(MGA1064_DAC_regs); i++) {
		dprintk("R%02X=%02X ", MGA1064_DAC_regs[i], ACCESS_FBINFO(hw).DACreg[i]);
		if ((i & 0x7) == 0x7) dprintk("\n" KERN_DEBUG "continuing... ");
	}
	dprintk("\n" KERN_DEBUG "DAC1064clk ");
	for (i = 0; i < 6; i++)
		dprintk("C%02X=%02X ", i, ACCESS_FBINFO(hw).DACclk[i]);
	dprintk("\n");
#endif
}

static int m1064_compute(void* out, struct my_timming* m) {
#define minfo ((struct matrox_fb_info*)out)
	{
		int i;
		int tmout;
		CRITFLAGS

		DAC1064_setpclk(PMINFO m->pixclock);

		CRITBEGIN

		for (i = 0; i < 3; i++)
			outDAC1064(PMINFO M1064_XPIXPLLCM + i, ACCESS_FBINFO(hw).DACclk[i]);
		for (tmout = 500000; tmout; tmout--) {
			if (inDAC1064(PMINFO M1064_XPIXPLLSTAT) & 0x40)
				break;
			udelay(10);
		};

		CRITEND

		if (!tmout)
			printk(KERN_ERR "matroxfb: Pixel PLL not locked after 5 secs\n");
	}
#undef minfo
	return 0;
}

static struct matrox_altout m1064 = {
	.name	 = "Primary output",
	.compute = m1064_compute,
};

#ifdef CONFIG_FB_MATROX_G
static int g450_compute(void* out, struct my_timming* m) {
#define minfo ((struct matrox_fb_info*)out)
	if (m->mnp < 0) {
		m->mnp = matroxfb_g450_setclk(PMINFO m->pixclock, (m->crtc == MATROXFB_SRC_CRTC1) ? M_PIXEL_PLL_C : M_VIDEO_PLL);
		if (m->mnp >= 0) {
			m->pixclock = g450_mnp2f(PMINFO m->mnp);
		}
	}
#undef minfo
	return 0;
}

static struct matrox_altout g450out = {
	.name	 = "Primary output",
	.compute = g450_compute,
};
#endif

#endif /* NEED_DAC1064 */

#ifdef CONFIG_FB_MATROX_MYSTIQUE
static int MGA1064_init(WPMINFO struct my_timming* m) {
	struct matrox_hw_state* hw = &ACCESS_FBINFO(hw);

	DBG(__FUNCTION__)

	if (DAC1064_init_1(PMINFO m)) return 1;
	if (matroxfb_vgaHWinit(PMINFO m)) return 1;

	hw->MiscOutReg = 0xCB;
	if (m->sync & FB_SYNC_HOR_HIGH_ACT)
		hw->MiscOutReg &= ~0x40;
	if (m->sync & FB_SYNC_VERT_HIGH_ACT)
		hw->MiscOutReg &= ~0x80;
	if (m->sync & FB_SYNC_COMP_HIGH_ACT) /* should be only FB_SYNC_COMP */
		hw->CRTCEXT[3] |= 0x40;

	if (DAC1064_init_2(PMINFO m)) return 1;
	return 0;
}
#endif

#ifdef CONFIG_FB_MATROX_G
static int MGAG100_init(WPMINFO struct my_timming* m) {
	struct matrox_hw_state* hw = &ACCESS_FBINFO(hw);

	DBG(__FUNCTION__)

	if (DAC1064_init_1(PMINFO m)) return 1;
	hw->MXoptionReg &= ~0x2000;
	if (matroxfb_vgaHWinit(PMINFO m)) return 1;

	hw->MiscOutReg = 0xEF;
	if (m->sync & FB_SYNC_HOR_HIGH_ACT)
		hw->MiscOutReg &= ~0x40;
	if (m->sync & FB_SYNC_VERT_HIGH_ACT)
		hw->MiscOutReg &= ~0x80;
	if (m->sync & FB_SYNC_COMP_HIGH_ACT) /* should be only FB_SYNC_COMP */
		hw->CRTCEXT[3] |= 0x40;

	if (DAC1064_init_2(PMINFO m)) return 1;
	return 0;
}
#endif	/* G */

#ifdef CONFIG_FB_MATROX_MYSTIQUE
static void MGA1064_ramdac_init(WPMINFO2) {

	DBG(__FUNCTION__)

	/* ACCESS_FBINFO(features.DAC1064.vco_freq_min) = 120000; */
	ACCESS_FBINFO(features.pll.vco_freq_min) = 62000;
	ACCESS_FBINFO(features.pll.ref_freq)	 = 14318;
	ACCESS_FBINFO(features.pll.feed_div_min) = 100;
	ACCESS_FBINFO(features.pll.feed_div_max) = 127;
	ACCESS_FBINFO(features.pll.in_div_min)	 = 1;
	ACCESS_FBINFO(features.pll.in_div_max)	 = 31;
	ACCESS_FBINFO(features.pll.post_shift_max) = 3;
	ACCESS_FBINFO(features.DAC1064.xvrefctrl) = DAC1064_XVREFCTRL_EXTERNAL;
	/* maybe cmdline MCLK= ?, doc says gclk=44MHz, mclk=66MHz... it was 55/83 with old values */
	DAC1064_setmclk(PMINFO DAC1064_OPT_MDIV2 | DAC1064_OPT_GDIV3 | DAC1064_OPT_SCLK_PLL, 133333);
}
#endif

#ifdef CONFIG_FB_MATROX_G
/* BIOS environ */
static int x7AF4 = 0x10;	/* flags, maybe 0x10 = SDRAM, 0x00 = SGRAM??? */
				/* G100 wants 0x10, G200 SGRAM does not care... */
#if 0
static int def50 = 0;	/* reg50, & 0x0F, & 0x3000 (only 0x0000, 0x1000, 0x2000 (0x3000 disallowed and treated as 0) */
#endif

static void MGAG100_progPixClock(CPMINFO int flags, int m, int n, int p) {
	int reg;
	int selClk;
	int clk;

	DBG(__FUNCTION__)

	outDAC1064(PMINFO M1064_XPIXCLKCTRL, inDAC1064(PMINFO M1064_XPIXCLKCTRL) | M1064_XPIXCLKCTRL_DIS |
		   M1064_XPIXCLKCTRL_PLL_UP);
	switch (flags & 3) {
		case 0:		reg = M1064_XPIXPLLAM; break;
		case 1:		reg = M1064_XPIXPLLBM; break;
		default:	reg = M1064_XPIXPLLCM; break;
	}
	outDAC1064(PMINFO reg++, m);
	outDAC1064(PMINFO reg++, n);
	outDAC1064(PMINFO reg, p);
	selClk = mga_inb(M_MISC_REG_READ) & ~0xC;
	/* there should be flags & 0x03 & case 0/1/else */
	/* and we should first select source and after that we should wait for PLL */
	/* and we are waiting for PLL with oscilator disabled... Is it right? */
	switch (flags & 0x03) {
		case 0x00:	break;
		case 0x01:	selClk |= 4; break;
		default:	selClk |= 0x0C; break;
	}
	mga_outb(M_MISC_REG, selClk);
	for (clk = 500000; clk; clk--) {
		if (inDAC1064(PMINFO M1064_XPIXPLLSTAT) & 0x40)
			break;
		udelay(10);
	};
	if (!clk)
		printk(KERN_ERR "matroxfb: Pixel PLL%c not locked after usual time\n", (reg-M1064_XPIXPLLAM-2)/4 + 'A');
	selClk = inDAC1064(PMINFO M1064_XPIXCLKCTRL) & ~M1064_XPIXCLKCTRL_SRC_MASK;
	switch (flags & 0x0C) {
		case 0x00:	selClk |= M1064_XPIXCLKCTRL_SRC_PCI; break;
		case 0x04:	selClk |= M1064_XPIXCLKCTRL_SRC_PLL; break;
		default:	selClk |= M1064_XPIXCLKCTRL_SRC_EXT; break;
	}
	outDAC1064(PMINFO M1064_XPIXCLKCTRL, selClk);
	outDAC1064(PMINFO M1064_XPIXCLKCTRL, inDAC1064(PMINFO M1064_XPIXCLKCTRL) & ~M1064_XPIXCLKCTRL_DIS);
}

static void MGAG100_setPixClock(CPMINFO int flags, int freq) {
	unsigned int m, n, p;

	DBG(__FUNCTION__)

	DAC1064_calcclock(PMINFO freq, ACCESS_FBINFO(max_pixel_clock), &m, &n, &p);
	MGAG100_progPixClock(PMINFO flags, m, n, p);
}
#endif

#ifdef CONFIG_FB_MATROX_MYSTIQUE
static int MGA1064_preinit(WPMINFO2) {
	static const int vxres_mystique[] = { 512,        640, 768,  800,  832,  960,
					     1024, 1152, 1280,      1600, 1664, 1920,
					     2048,    0};
	struct matrox_hw_state* hw = &ACCESS_FBINFO(hw);

	DBG(__FUNCTION__)

	/* ACCESS_FBINFO(capable.cfb4) = 0; ... preinitialized by 0 */
	ACCESS_FBINFO(capable.text) = 1;
	ACCESS_FBINFO(capable.vxres) = vxres_mystique;

	ACCESS_FBINFO(outputs[0]).output = &m1064;
	ACCESS_FBINFO(outputs[0]).src = ACCESS_FBINFO(outputs[0]).default_src;
	ACCESS_FBINFO(outputs[0]).data = MINFO;
	ACCESS_FBINFO(outputs[0]).mode = MATROXFB_OUTPUT_MODE_MONITOR;

	if (ACCESS_FBINFO(devflags.noinit))
		return 0;	/* do not modify settings */
	hw->MXoptionReg &= 0xC0000100;
	hw->MXoptionReg |= 0x00094E20;
	if (ACCESS_FBINFO(devflags.novga))
		hw->MXoptionReg &= ~0x00000100;
	if (ACCESS_FBINFO(devflags.nobios))
		hw->MXoptionReg &= ~0x40000000;
	if (ACCESS_FBINFO(devflags.nopciretry))
		hw->MXoptionReg |=  0x20000000;
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, hw->MXoptionReg);
	mga_setr(M_SEQ_INDEX, 0x01, 0x20);
	mga_outl(M_CTLWTST, 0x00000000);
	udelay(200);
	mga_outl(M_MACCESS, 0x00008000);
	udelay(100);
	mga_outl(M_MACCESS, 0x0000C000);
	return 0;
}

static void MGA1064_reset(WPMINFO2) {

	DBG(__FUNCTION__);

	MGA1064_ramdac_init(PMINFO2);
}
#endif

#ifdef CONFIG_FB_MATROX_G
static void g450_mclk_init(WPMINFO2) {
	/* switch all clocks to PCI source */
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, ACCESS_FBINFO(hw).MXoptionReg | 4);
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION3_REG, ACCESS_FBINFO(values).reg.opt3 & ~0x00300C03);
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, ACCESS_FBINFO(hw).MXoptionReg);
	
	if (((ACCESS_FBINFO(values).reg.opt3 & 0x000003) == 0x000003) ||
	    ((ACCESS_FBINFO(values).reg.opt3 & 0x000C00) == 0x000C00) ||
	    ((ACCESS_FBINFO(values).reg.opt3 & 0x300000) == 0x300000)) {
		matroxfb_g450_setclk(PMINFO ACCESS_FBINFO(values.pll.video), M_VIDEO_PLL);
	} else {
		unsigned long flags;
		unsigned int pwr;
		
		matroxfb_DAC_lock_irqsave(flags);
		pwr = inDAC1064(PMINFO M1064_XPWRCTRL) & ~0x02;
		outDAC1064(PMINFO M1064_XPWRCTRL, pwr);
		matroxfb_DAC_unlock_irqrestore(flags);
	}
	matroxfb_g450_setclk(PMINFO ACCESS_FBINFO(values.pll.system), M_SYSTEM_PLL);
	
	/* switch clocks to their real PLL source(s) */
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, ACCESS_FBINFO(hw).MXoptionReg | 4);
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION3_REG, ACCESS_FBINFO(values).reg.opt3);
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, ACCESS_FBINFO(hw).MXoptionReg);

}

static void g450_memory_init(WPMINFO2) {
	/* disable memory refresh */
	ACCESS_FBINFO(hw).MXoptionReg &= ~0x001F8000;
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, ACCESS_FBINFO(hw).MXoptionReg);
	
	/* set memory interface parameters */
	ACCESS_FBINFO(hw).MXoptionReg &= ~0x00207E00;
	ACCESS_FBINFO(hw).MXoptionReg |= 0x00207E00 & ACCESS_FBINFO(values).reg.opt;
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, ACCESS_FBINFO(hw).MXoptionReg);
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION2_REG, ACCESS_FBINFO(values).reg.opt2);
	
	mga_outl(M_CTLWTST, ACCESS_FBINFO(values).reg.mctlwtst);
	
	/* first set up memory interface with disabled memory interface clocks */
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_MEMMISC_REG, ACCESS_FBINFO(values).reg.memmisc & ~0x80000000U);
	mga_outl(M_MEMRDBK, ACCESS_FBINFO(values).reg.memrdbk);
	mga_outl(M_MACCESS, ACCESS_FBINFO(values).reg.maccess);
	/* start memory clocks */
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_MEMMISC_REG, ACCESS_FBINFO(values).reg.memmisc | 0x80000000U);

	udelay(200);
	
	if (ACCESS_FBINFO(values).memory.ddr && (!ACCESS_FBINFO(values).memory.emrswen || !ACCESS_FBINFO(values).memory.dll)) {
		mga_outl(M_MEMRDBK, ACCESS_FBINFO(values).reg.memrdbk & ~0x1000);
	}
	mga_outl(M_MACCESS, ACCESS_FBINFO(values).reg.maccess | 0x8000);
	
	udelay(200);
	
	ACCESS_FBINFO(hw).MXoptionReg |= 0x001F8000 & ACCESS_FBINFO(values).reg.opt;
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, ACCESS_FBINFO(hw).MXoptionReg);
	
	/* value is written to memory chips only if old != new */
	mga_outl(M_PLNWT, 0);
	mga_outl(M_PLNWT, ~0);
	
	if (ACCESS_FBINFO(values).reg.mctlwtst != ACCESS_FBINFO(values).reg.mctlwtst_core) {
		mga_outl(M_CTLWTST, ACCESS_FBINFO(values).reg.mctlwtst_core);
	}
	
}

static void g450_preinit(WPMINFO2) {
	u_int32_t c2ctl;
	u_int8_t curctl;
	u_int8_t c1ctl;
	
	/* ACCESS_FBINFO(hw).MXoptionReg = minfo->values.reg.opt; */
	ACCESS_FBINFO(hw).MXoptionReg &= 0xC0000100;
	ACCESS_FBINFO(hw).MXoptionReg |= 0x00000020;
	if (ACCESS_FBINFO(devflags.novga))
		ACCESS_FBINFO(hw).MXoptionReg &= ~0x00000100;
	if (ACCESS_FBINFO(devflags.nobios))
		ACCESS_FBINFO(hw).MXoptionReg &= ~0x40000000;
	if (ACCESS_FBINFO(devflags.nopciretry))
		ACCESS_FBINFO(hw).MXoptionReg |=  0x20000000;
	ACCESS_FBINFO(hw).MXoptionReg |= ACCESS_FBINFO(values).reg.opt & 0x03400040;
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, ACCESS_FBINFO(hw).MXoptionReg);

	/* Init system clocks */
		
	/* stop crtc2 */
	c2ctl = mga_inl(M_C2CTL);
	mga_outl(M_C2CTL, c2ctl & ~1);
	/* stop cursor */
	curctl = inDAC1064(PMINFO M1064_XCURCTRL);
	outDAC1064(PMINFO M1064_XCURCTRL, 0);
	/* stop crtc1 */
	c1ctl = mga_readr(M_SEQ_INDEX, 1);
	mga_setr(M_SEQ_INDEX, 1, c1ctl | 0x20);

	g450_mclk_init(PMINFO2);
	g450_memory_init(PMINFO2);
	
	/* set legacy VGA clock sources for DOSEmu or VMware... */
	matroxfb_g450_setclk(PMINFO 25175, M_PIXEL_PLL_A);
	matroxfb_g450_setclk(PMINFO 28322, M_PIXEL_PLL_B);

	/* restore crtc1 */
	mga_setr(M_SEQ_INDEX, 1, c1ctl);
	
	/* restore cursor */
	outDAC1064(PMINFO M1064_XCURCTRL, curctl);

	/* restore crtc2 */
	mga_outl(M_C2CTL, c2ctl);
	
	return;
}

static int MGAG100_preinit(WPMINFO2) {
	static const int vxres_g100[] = {  512,        640, 768,  800,  832,  960,
                                          1024, 1152, 1280,      1600, 1664, 1920,
                                          2048, 0};
	struct matrox_hw_state* hw = &ACCESS_FBINFO(hw);

        u_int32_t reg50;
#if 0
	u_int32_t q;
#endif

	DBG(__FUNCTION__)

	/* there are some instabilities if in_div > 19 && vco < 61000 */
	if (ACCESS_FBINFO(devflags.g450dac)) {
		ACCESS_FBINFO(features.pll.vco_freq_min) = 130000;	/* my sample: >118 */
	} else {
		ACCESS_FBINFO(features.pll.vco_freq_min) = 62000;
	}
	if (!ACCESS_FBINFO(features.pll.ref_freq)) {
		ACCESS_FBINFO(features.pll.ref_freq)	 = 27000;
	}
	ACCESS_FBINFO(features.pll.feed_div_min) = 7;
	ACCESS_FBINFO(features.pll.feed_div_max) = 127;
	ACCESS_FBINFO(features.pll.in_div_min)	 = 1;
	ACCESS_FBINFO(features.pll.in_div_max)	 = 31;
	ACCESS_FBINFO(features.pll.post_shift_max) = 3;
	ACCESS_FBINFO(features.DAC1064.xvrefctrl) = DAC1064_XVREFCTRL_G100_DEFAULT;
	/* ACCESS_FBINFO(capable.cfb4) = 0; ... preinitialized by 0 */
	ACCESS_FBINFO(capable.text) = 1;
	ACCESS_FBINFO(capable.vxres) = vxres_g100;
	ACCESS_FBINFO(capable.plnwt) = ACCESS_FBINFO(devflags.accelerator) == FB_ACCEL_MATROX_MGAG100
			? ACCESS_FBINFO(devflags.sgram) : 1;

#ifdef CONFIG_FB_MATROX_G
	if (ACCESS_FBINFO(devflags.g450dac)) {
		ACCESS_FBINFO(outputs[0]).output = &g450out;
	} else
#endif
	{
		ACCESS_FBINFO(outputs[0]).output = &m1064;
	}
	ACCESS_FBINFO(outputs[0]).src = ACCESS_FBINFO(outputs[0]).default_src;
	ACCESS_FBINFO(outputs[0]).data = MINFO;
	ACCESS_FBINFO(outputs[0]).mode = MATROXFB_OUTPUT_MODE_MONITOR;

	if (ACCESS_FBINFO(devflags.g450dac)) {
		/* we must do this always, BIOS does not do it for us
		   and accelerator dies without it */
		mga_outl(0x1C0C, 0);
	}
	if (ACCESS_FBINFO(devflags.noinit))
		return 0;
	if (ACCESS_FBINFO(devflags.g450dac)) {
		g450_preinit(PMINFO2);
		return 0;
	}
	hw->MXoptionReg &= 0xC0000100;
	hw->MXoptionReg |= 0x00000020;
	if (ACCESS_FBINFO(devflags.novga))
		hw->MXoptionReg &= ~0x00000100;
	if (ACCESS_FBINFO(devflags.nobios))
		hw->MXoptionReg &= ~0x40000000;
	if (ACCESS_FBINFO(devflags.nopciretry))
		hw->MXoptionReg |=  0x20000000;
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, hw->MXoptionReg);
	DAC1064_setmclk(PMINFO DAC1064_OPT_MDIV2 | DAC1064_OPT_GDIV3 | DAC1064_OPT_SCLK_PCI, 133333);

	if (ACCESS_FBINFO(devflags.accelerator) == FB_ACCEL_MATROX_MGAG100) {
		pci_read_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION2_REG, &reg50);
		reg50 &= ~0x3000;
		pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION2_REG, reg50);

		hw->MXoptionReg |= 0x1080;
		pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, hw->MXoptionReg);
		mga_outl(M_CTLWTST, ACCESS_FBINFO(values).reg.mctlwtst);
		udelay(100);
		mga_outb(0x1C05, 0x00);
		mga_outb(0x1C05, 0x80);
		udelay(100);
		mga_outb(0x1C05, 0x40);
		mga_outb(0x1C05, 0xC0);
		udelay(100);
		reg50 &= ~0xFF;
		reg50 |=  0x07;
		pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION2_REG, reg50);
		/* it should help with G100 */
		mga_outb(M_GRAPHICS_INDEX, 6);
		mga_outb(M_GRAPHICS_DATA, (mga_inb(M_GRAPHICS_DATA) & 3) | 4);
		mga_setr(M_EXTVGA_INDEX, 0x03, 0x81);
		mga_setr(M_EXTVGA_INDEX, 0x04, 0x00);
		mga_writeb(ACCESS_FBINFO(video.vbase), 0x0000, 0xAA);
		mga_writeb(ACCESS_FBINFO(video.vbase), 0x0800, 0x55);
		mga_writeb(ACCESS_FBINFO(video.vbase), 0x4000, 0x55);
#if 0
		if (mga_readb(ACCESS_FBINFO(video.vbase), 0x0000) != 0xAA) {
			hw->MXoptionReg &= ~0x1000;
		}
#endif
		hw->MXoptionReg |= 0x00078020;
	} else if (ACCESS_FBINFO(devflags.accelerator) == FB_ACCEL_MATROX_MGAG200) {
		pci_read_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION2_REG, &reg50);
		reg50 &= ~0x3000;
		pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION2_REG, reg50);

		if (ACCESS_FBINFO(devflags.memtype) == -1)
			hw->MXoptionReg |= ACCESS_FBINFO(values).reg.opt & 0x1C00;
		else
			hw->MXoptionReg |= (ACCESS_FBINFO(devflags.memtype) & 7) << 10;
		if (ACCESS_FBINFO(devflags.sgram))
			hw->MXoptionReg |= 0x4000;
		mga_outl(M_CTLWTST, ACCESS_FBINFO(values).reg.mctlwtst);
		mga_outl(M_MEMRDBK, ACCESS_FBINFO(values).reg.memrdbk);
		udelay(200);
		mga_outl(M_MACCESS, 0x00000000);
		mga_outl(M_MACCESS, 0x00008000);
		udelay(100);
		mga_outw(M_MEMRDBK, ACCESS_FBINFO(values).reg.memrdbk);
		hw->MXoptionReg |= 0x00078020;
	} else {
		pci_read_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION2_REG, &reg50);
		reg50 &= ~0x00000100;
		reg50 |=  0x00000000;
		pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION2_REG, reg50);

		if (ACCESS_FBINFO(devflags.memtype) == -1)
			hw->MXoptionReg |= ACCESS_FBINFO(values).reg.opt & 0x1C00;
		else
			hw->MXoptionReg |= (ACCESS_FBINFO(devflags.memtype) & 7) << 10;
		if (ACCESS_FBINFO(devflags.sgram))
			hw->MXoptionReg |= 0x4000;
		mga_outl(M_CTLWTST, ACCESS_FBINFO(values).reg.mctlwtst);
		mga_outl(M_MEMRDBK, ACCESS_FBINFO(values).reg.memrdbk);
		udelay(200);
		mga_outl(M_MACCESS, 0x00000000);
		mga_outl(M_MACCESS, 0x00008000);
		udelay(100);
		mga_outl(M_MEMRDBK, ACCESS_FBINFO(values).reg.memrdbk);
		hw->MXoptionReg |= 0x00040020;
	}
	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, hw->MXoptionReg);
	return 0;
}

static void MGAG100_reset(WPMINFO2) {
	u_int8_t b;
	struct matrox_hw_state* hw = &ACCESS_FBINFO(hw);

	DBG(__FUNCTION__)

	{
#ifdef G100_BROKEN_IBM_82351
		u_int32_t d;

		find 1014/22 (IBM/82351); /* if found and bridging Matrox, do some strange stuff */
		pci_read_config_byte(ibm, PCI_SECONDARY_BUS, &b);
		if (b == ACCESS_FBINFO(pcidev)->bus->number) {
			pci_write_config_byte(ibm, PCI_COMMAND+1, 0);	/* disable back-to-back & SERR */
			pci_write_config_byte(ibm, 0x41, 0xF4);		/* ??? */
			pci_write_config_byte(ibm, PCI_IO_BASE, 0xF0);	/* ??? */
			pci_write_config_byte(ibm, PCI_IO_LIMIT, 0x00);	/* ??? */
		}
#endif
		if (!ACCESS_FBINFO(devflags.noinit)) {
			if (x7AF4 & 8) {
				hw->MXoptionReg |= 0x40;	/* FIXME... */
				pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, hw->MXoptionReg);
			}
			mga_setr(M_EXTVGA_INDEX, 0x06, 0x00);
		}
	}
	if (ACCESS_FBINFO(devflags.g450dac)) {
		/* either leave MCLK as is... or they were set in preinit */
		hw->DACclk[3] = inDAC1064(PMINFO DAC1064_XSYSPLLM);
		hw->DACclk[4] = inDAC1064(PMINFO DAC1064_XSYSPLLN);
		hw->DACclk[5] = inDAC1064(PMINFO DAC1064_XSYSPLLP);
	} else {
		DAC1064_setmclk(PMINFO DAC1064_OPT_RESERVED | DAC1064_OPT_MDIV2 | DAC1064_OPT_GDIV1 | DAC1064_OPT_SCLK_PLL, 133333);
	}
	if (ACCESS_FBINFO(devflags.accelerator) == FB_ACCEL_MATROX_MGAG400) {
		if (ACCESS_FBINFO(devflags.dfp_type) == -1) {
			ACCESS_FBINFO(devflags.dfp_type) = inDAC1064(PMINFO 0x1F);
		}
	}
	if (ACCESS_FBINFO(devflags.noinit))
		return;
	if (ACCESS_FBINFO(devflags.g450dac)) {
	} else {
		MGAG100_setPixClock(PMINFO 4, 25175);
		MGAG100_setPixClock(PMINFO 5, 28322);
		if (x7AF4 & 0x10) {
			b = inDAC1064(PMINFO M1064_XGENIODATA) & ~1;
			outDAC1064(PMINFO M1064_XGENIODATA, b);
			b = inDAC1064(PMINFO M1064_XGENIOCTRL) | 1;
			outDAC1064(PMINFO M1064_XGENIOCTRL, b);
		}
	}
}
#endif

#ifdef CONFIG_FB_MATROX_MYSTIQUE
static void MGA1064_restore(WPMINFO2) {
	int i;
	struct matrox_hw_state* hw = &ACCESS_FBINFO(hw);

	CRITFLAGS

	DBG(__FUNCTION__)

	CRITBEGIN

	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, hw->MXoptionReg);
	mga_outb(M_IEN, 0x00);
	mga_outb(M_CACHEFLUSH, 0x00);

	CRITEND

	DAC1064_restore_1(PMINFO2);
	matroxfb_vgaHWrestore(PMINFO2);
	ACCESS_FBINFO(crtc1.panpos) = -1;
	for (i = 0; i < 6; i++)
		mga_setr(M_EXTVGA_INDEX, i, hw->CRTCEXT[i]);
	DAC1064_restore_2(PMINFO2);
}
#endif

#ifdef CONFIG_FB_MATROX_G
static void MGAG100_restore(WPMINFO2) {
	int i;
	struct matrox_hw_state* hw = &ACCESS_FBINFO(hw);

	CRITFLAGS

	DBG(__FUNCTION__)

	CRITBEGIN

	pci_write_config_dword(ACCESS_FBINFO(pcidev), PCI_OPTION_REG, hw->MXoptionReg);
	CRITEND

	DAC1064_restore_1(PMINFO2);
	matroxfb_vgaHWrestore(PMINFO2);
#ifdef CONFIG_FB_MATROX_32MB
	if (ACCESS_FBINFO(devflags.support32MB))
		mga_setr(M_EXTVGA_INDEX, 8, hw->CRTCEXT[8]);
#endif
	ACCESS_FBINFO(crtc1.panpos) = -1;
	for (i = 0; i < 6; i++)
		mga_setr(M_EXTVGA_INDEX, i, hw->CRTCEXT[i]);
	DAC1064_restore_2(PMINFO2);
}
#endif

#ifdef CONFIG_FB_MATROX_MYSTIQUE
struct matrox_switch matrox_mystique = {
	MGA1064_preinit, MGA1064_reset, MGA1064_init, MGA1064_restore,
};
EXPORT_SYMBOL(matrox_mystique);
#endif

#ifdef CONFIG_FB_MATROX_G
struct matrox_switch matrox_G100 = {
	MGAG100_preinit, MGAG100_reset, MGAG100_init, MGAG100_restore,
};
EXPORT_SYMBOL(matrox_G100);
#endif

#ifdef NEED_DAC1064
EXPORT_SYMBOL(DAC1064_global_init);
EXPORT_SYMBOL(DAC1064_global_restore);
#endif
MODULE_LICENSE("GPL");
