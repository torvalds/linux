/*
 * XG20, XG21, XG40, XG42 frame buffer device
 * for Linux kernels  2.5.x, 2.6.x
 * Base on TW's sis fbdev code.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/sizes.h>
#include <linux/module.h>

#ifdef CONFIG_MTRR
#include <asm/mtrr.h>
#endif

#include "XGI_main.h"
#include "vb_init.h"
#include "vb_util.h"
#include "vb_setmode.h"

#define Index_CR_GPIO_Reg1 0x48
#define Index_CR_GPIO_Reg3 0x4a

#define GPIOG_EN    (1<<6)
#define GPIOG_READ  (1<<1)

static char *forcecrt2type;
static char *mode;
static int vesa = -1;
static unsigned int refresh_rate;

/* -------------------- Macro definitions ---------------------------- */

#ifdef DEBUG
static void dumpVGAReg(void)
{
	u8 i, reg;

	xgifb_reg_set(XGISR, 0x05, 0x86);

	for (i = 0; i < 0x4f; i++) {
		reg = xgifb_reg_get(XGISR, i);
		pr_debug("o 3c4 %x\n", i);
		pr_debug("i 3c5 => %x\n", reg);
	}

	for (i = 0; i < 0xF0; i++) {
		reg = xgifb_reg_get(XGICR, i);
		pr_debug("o 3d4 %x\n", i);
		pr_debug("i 3d5 => %x\n", reg);
	}
}
#else
static inline void dumpVGAReg(void)
{
}
#endif

/* --------------- Hardware Access Routines -------------------------- */

static int XGIfb_mode_rate_to_dclock(struct vb_device_info *XGI_Pr,
		struct xgi_hw_device_info *HwDeviceExtension,
		unsigned char modeno)
{
	unsigned short ModeNo = modeno;
	unsigned short ModeIdIndex = 0, ClockIndex = 0;
	unsigned short RefreshRateTableIndex = 0;
	int Clock;

	InitTo330Pointer(HwDeviceExtension->jChipType, XGI_Pr);

	XGI_SearchModeID(ModeNo, &ModeIdIndex);

	RefreshRateTableIndex = XGI_GetRatePtrCRT2(HwDeviceExtension, ModeNo,
			ModeIdIndex, XGI_Pr);

	ClockIndex = XGI330_RefIndex[RefreshRateTableIndex].Ext_CRTVCLK;

	Clock = XGI_VCLKData[ClockIndex].CLOCK * 1000;

	return Clock;
}

static int XGIfb_mode_rate_to_ddata(struct vb_device_info *XGI_Pr,
		struct xgi_hw_device_info *HwDeviceExtension,
		unsigned char modeno,
		u32 *left_margin, u32 *right_margin, u32 *upper_margin,
		u32 *lower_margin, u32 *hsync_len, u32 *vsync_len, u32 *sync,
		u32 *vmode)
{
	unsigned short ModeNo = modeno;
	unsigned short ModeIdIndex, index = 0;
	unsigned short RefreshRateTableIndex = 0;

	unsigned short VRE, VBE, VRS, VDE;
	unsigned short HRE, HBE, HRS, HDE;
	unsigned char sr_data, cr_data, cr_data2;
	int B, C, D, F, temp, j;

	InitTo330Pointer(HwDeviceExtension->jChipType, XGI_Pr);
	if (!XGI_SearchModeID(ModeNo, &ModeIdIndex))
		return 0;
	RefreshRateTableIndex = XGI_GetRatePtrCRT2(HwDeviceExtension, ModeNo,
			ModeIdIndex, XGI_Pr);
	index = XGI330_RefIndex[RefreshRateTableIndex].Ext_CRT1CRTC;

	sr_data = XGI_CRT1Table[index].CR[5];

	HDE = (XGI330_RefIndex[RefreshRateTableIndex].XRes >> 3);

	cr_data = XGI_CRT1Table[index].CR[3];

	/* Horizontal retrace (=sync) start */
	HRS = (cr_data & 0xff) | ((unsigned short) (sr_data & 0xC0) << 2);
	F = HRS - HDE - 3;

	sr_data = XGI_CRT1Table[index].CR[6];

	cr_data = XGI_CRT1Table[index].CR[2];

	cr_data2 = XGI_CRT1Table[index].CR[4];

	/* Horizontal blank end */
	HBE = (cr_data & 0x1f) | ((unsigned short) (cr_data2 & 0x80) >> 2)
			| ((unsigned short) (sr_data & 0x03) << 6);

	/* Horizontal retrace (=sync) end */
	HRE = (cr_data2 & 0x1f) | ((sr_data & 0x04) << 3);

	temp = HBE - ((HDE - 1) & 255);
	B = (temp > 0) ? temp : (temp + 256);

	temp = HRE - ((HDE + F + 3) & 63);
	C = (temp > 0) ? temp : (temp + 64);

	D = B - F - C;

	*left_margin = D * 8;
	*right_margin = F * 8;
	*hsync_len = C * 8;

	sr_data = XGI_CRT1Table[index].CR[14];

	cr_data2 = XGI_CRT1Table[index].CR[9];

	VDE = XGI330_RefIndex[RefreshRateTableIndex].YRes;

	cr_data = XGI_CRT1Table[index].CR[10];

	/* Vertical retrace (=sync) start */
	VRS = (cr_data & 0xff) | ((unsigned short) (cr_data2 & 0x04) << 6)
			| ((unsigned short) (cr_data2 & 0x80) << 2)
			| ((unsigned short) (sr_data & 0x08) << 7);
	F = VRS + 1 - VDE;

	cr_data = XGI_CRT1Table[index].CR[13];

	/* Vertical blank end */
	VBE = (cr_data & 0xff) | ((unsigned short) (sr_data & 0x10) << 4);
	temp = VBE - ((VDE - 1) & 511);
	B = (temp > 0) ? temp : (temp + 512);

	cr_data = XGI_CRT1Table[index].CR[11];

	/* Vertical retrace (=sync) end */
	VRE = (cr_data & 0x0f) | ((sr_data & 0x20) >> 1);
	temp = VRE - ((VDE + F - 1) & 31);
	C = (temp > 0) ? temp : (temp + 32);

	D = B - F - C;

	*upper_margin = D;
	*lower_margin = F;
	*vsync_len = C;

	if (XGI330_RefIndex[RefreshRateTableIndex].Ext_InfoFlag & 0x8000)
		*sync &= ~FB_SYNC_VERT_HIGH_ACT;
	else
		*sync |= FB_SYNC_VERT_HIGH_ACT;

	if (XGI330_RefIndex[RefreshRateTableIndex].Ext_InfoFlag & 0x4000)
		*sync &= ~FB_SYNC_HOR_HIGH_ACT;
	else
		*sync |= FB_SYNC_HOR_HIGH_ACT;

	*vmode = FB_VMODE_NONINTERLACED;
	if (XGI330_RefIndex[RefreshRateTableIndex].Ext_InfoFlag & 0x0080)
		*vmode = FB_VMODE_INTERLACED;
	else {
		j = 0;
		while (XGI330_EModeIDTable[j].Ext_ModeID != 0xff) {
			if (XGI330_EModeIDTable[j].Ext_ModeID ==
			    XGI330_RefIndex[RefreshRateTableIndex].ModeID) {
				if (XGI330_EModeIDTable[j].Ext_ModeFlag &
				    DoubleScanMode) {
					*vmode = FB_VMODE_DOUBLE;
				}
				break;
			}
			j++;
		}
	}

	return 1;
}

void XGIRegInit(struct vb_device_info *XGI_Pr, unsigned long BaseAddr)
{
	XGI_Pr->P3c4 = BaseAddr + 0x14;
	XGI_Pr->P3d4 = BaseAddr + 0x24;
	XGI_Pr->P3c0 = BaseAddr + 0x10;
	XGI_Pr->P3ce = BaseAddr + 0x1e;
	XGI_Pr->P3c2 = BaseAddr + 0x12;
	XGI_Pr->P3cc = BaseAddr + 0x1c;
	XGI_Pr->P3ca = BaseAddr + 0x1a;
	XGI_Pr->P3c6 = BaseAddr + 0x16;
	XGI_Pr->P3c7 = BaseAddr + 0x17;
	XGI_Pr->P3c8 = BaseAddr + 0x18;
	XGI_Pr->P3c9 = BaseAddr + 0x19;
	XGI_Pr->P3da = BaseAddr + 0x2A;
	XGI_Pr->Part0Port = BaseAddr + XGI_CRT2_PORT_00;
	/* Digital video interface registers (LCD) */
	XGI_Pr->Part1Port = BaseAddr + SIS_CRT2_PORT_04;
	/* 301 TV Encoder registers */
	XGI_Pr->Part2Port = BaseAddr + SIS_CRT2_PORT_10;
	/* 301 Macrovision registers */
	XGI_Pr->Part3Port = BaseAddr + SIS_CRT2_PORT_12;
	/* 301 VGA2 (and LCD) registers */
	XGI_Pr->Part4Port = BaseAddr + SIS_CRT2_PORT_14;
	/* 301 palette address port registers */
	XGI_Pr->Part5Port = BaseAddr + SIS_CRT2_PORT_14 + 2;

}

/* ------------------ Internal helper routines ----------------- */

static int XGIfb_GetXG21DefaultLVDSModeIdx(struct xgifb_video_info *xgifb_info)
{
	int i = 0;

	while ((XGIbios_mode[i].mode_no != 0)
	       && (XGIbios_mode[i].xres <= xgifb_info->lvds_data.LVDSHDE)) {
		if ((XGIbios_mode[i].xres == xgifb_info->lvds_data.LVDSHDE)
		    && (XGIbios_mode[i].yres == xgifb_info->lvds_data.LVDSVDE)
		    && (XGIbios_mode[i].bpp == 8)) {
			return i;
		}
		i++;
	}

	return -1;
}

static void XGIfb_search_mode(struct xgifb_video_info *xgifb_info,
			      const char *name)
{
	unsigned int xres;
	unsigned int yres;
	unsigned int bpp;
	int i;

	if (sscanf(name, "%ux%ux%u", &xres, &yres, &bpp) != 3)
		goto invalid_mode;

	if (bpp == 24)
		bpp = 32; /* That's for people who mix up color and fb depth. */

	for (i = 0; XGIbios_mode[i].mode_no != 0; i++)
		if (XGIbios_mode[i].xres == xres &&
		    XGIbios_mode[i].yres == yres &&
		    XGIbios_mode[i].bpp == bpp) {
			xgifb_info->mode_idx = i;
			return;
		}
invalid_mode:
	pr_info("Invalid mode '%s'\n", name);
}

static void XGIfb_search_vesamode(struct xgifb_video_info *xgifb_info,
				  unsigned int vesamode)
{
	int i = 0;

	if (vesamode == 0)
		goto invalid;

	vesamode &= 0x1dff; /* Clean VESA mode number from other flags */

	while (XGIbios_mode[i].mode_no != 0) {
		if ((XGIbios_mode[i].vesa_mode_no_1 == vesamode) ||
		    (XGIbios_mode[i].vesa_mode_no_2 == vesamode)) {
			xgifb_info->mode_idx = i;
			return;
		}
		i++;
	}

invalid:
	pr_info("Invalid VESA mode 0x%x'\n", vesamode);
}

static int XGIfb_validate_mode(struct xgifb_video_info *xgifb_info, int myindex)
{
	u16 xres, yres;
	struct xgi_hw_device_info *hw_info = &xgifb_info->hw_info;
	unsigned long required_mem;

	if (xgifb_info->chip == XG21) {
		if (xgifb_info->display2 == XGIFB_DISP_LCD) {
			xres = xgifb_info->lvds_data.LVDSHDE;
			yres = xgifb_info->lvds_data.LVDSVDE;
			if (XGIbios_mode[myindex].xres > xres)
				return -1;
			if (XGIbios_mode[myindex].yres > yres)
				return -1;
			if ((XGIbios_mode[myindex].xres < xres) &&
			    (XGIbios_mode[myindex].yres < yres)) {
				if (XGIbios_mode[myindex].bpp > 8)
					return -1;
			}

		}
		goto check_memory;

	}

	/* FIXME: for now, all is valid on XG27 */
	if (xgifb_info->chip == XG27)
		goto check_memory;

	if (!(XGIbios_mode[myindex].chipset & MD_XGI315))
		return -1;

	switch (xgifb_info->display2) {
	case XGIFB_DISP_LCD:
		switch (hw_info->ulCRT2LCDType) {
		case LCD_640x480:
			xres = 640;
			yres = 480;
			break;
		case LCD_800x600:
			xres = 800;
			yres = 600;
			break;
		case LCD_1024x600:
			xres = 1024;
			yres = 600;
			break;
		case LCD_1024x768:
			xres = 1024;
			yres = 768;
			break;
		case LCD_1152x768:
			xres = 1152;
			yres = 768;
			break;
		case LCD_1280x960:
			xres = 1280;
			yres = 960;
			break;
		case LCD_1280x768:
			xres = 1280;
			yres = 768;
			break;
		case LCD_1280x1024:
			xres = 1280;
			yres = 1024;
			break;
		case LCD_1400x1050:
			xres = 1400;
			yres = 1050;
			break;
		case LCD_1600x1200:
			xres = 1600;
			yres = 1200;
			break;
		default:
			xres = 0;
			yres = 0;
			break;
		}
		if (XGIbios_mode[myindex].xres > xres)
			return -1;
		if (XGIbios_mode[myindex].yres > yres)
			return -1;
		if ((hw_info->ulExternalChip == 0x01) || /* LVDS */
		    (hw_info->ulExternalChip == 0x05)) { /* LVDS+Chrontel */
			switch (XGIbios_mode[myindex].xres) {
			case 512:
				if (XGIbios_mode[myindex].yres != 512)
					return -1;
				if (hw_info->ulCRT2LCDType == LCD_1024x600)
					return -1;
				break;
			case 640:
				if ((XGIbios_mode[myindex].yres != 400)
						&& (XGIbios_mode[myindex].yres
								!= 480))
					return -1;
				break;
			case 800:
				if (XGIbios_mode[myindex].yres != 600)
					return -1;
				break;
			case 1024:
				if ((XGIbios_mode[myindex].yres != 600) &&
				    (XGIbios_mode[myindex].yres != 768))
					return -1;
				if ((XGIbios_mode[myindex].yres == 600) &&
				    (hw_info->ulCRT2LCDType != LCD_1024x600))
					return -1;
				break;
			case 1152:
				if ((XGIbios_mode[myindex].yres) != 768)
					return -1;
				if (hw_info->ulCRT2LCDType != LCD_1152x768)
					return -1;
				break;
			case 1280:
				if ((XGIbios_mode[myindex].yres != 768) &&
				    (XGIbios_mode[myindex].yres != 1024))
					return -1;
				if ((XGIbios_mode[myindex].yres == 768) &&
				    (hw_info->ulCRT2LCDType != LCD_1280x768))
					return -1;
				break;
			case 1400:
				if (XGIbios_mode[myindex].yres != 1050)
					return -1;
				break;
			case 1600:
				if (XGIbios_mode[myindex].yres != 1200)
					return -1;
				break;
			default:
				return -1;
			}
		} else {
			switch (XGIbios_mode[myindex].xres) {
			case 512:
				if (XGIbios_mode[myindex].yres != 512)
					return -1;
				break;
			case 640:
				if ((XGIbios_mode[myindex].yres != 400) &&
				    (XGIbios_mode[myindex].yres != 480))
					return -1;
				break;
			case 800:
				if (XGIbios_mode[myindex].yres != 600)
					return -1;
				break;
			case 1024:
				if (XGIbios_mode[myindex].yres != 768)
					return -1;
				break;
			case 1280:
				if ((XGIbios_mode[myindex].yres != 960) &&
				    (XGIbios_mode[myindex].yres != 1024))
					return -1;
				if (XGIbios_mode[myindex].yres == 960) {
					if (hw_info->ulCRT2LCDType ==
					    LCD_1400x1050)
						return -1;
				}
				break;
			case 1400:
				if (XGIbios_mode[myindex].yres != 1050)
					return -1;
				break;
			case 1600:
				if (XGIbios_mode[myindex].yres != 1200)
					return -1;
				break;
			default:
				return -1;
			}
		}
		break;
	case XGIFB_DISP_TV:
		switch (XGIbios_mode[myindex].xres) {
		case 512:
		case 640:
		case 800:
			break;
		case 720:
			if (xgifb_info->TV_type == TVMODE_NTSC) {
				if (XGIbios_mode[myindex].yres != 480)
					return -1;
			} else if (xgifb_info->TV_type == TVMODE_PAL) {
				if (XGIbios_mode[myindex].yres != 576)
					return -1;
			}
			/* LVDS/CHRONTEL does not support 720 */
			if (xgifb_info->hasVB == HASVB_LVDS_CHRONTEL ||
			    xgifb_info->hasVB == HASVB_CHRONTEL) {
				return -1;
			}
			break;
		case 1024:
			if (xgifb_info->TV_type == TVMODE_NTSC) {
				if (XGIbios_mode[myindex].bpp == 32)
					return -1;
			}
			break;
		default:
			return -1;
		}
		break;
	case XGIFB_DISP_CRT:
		if (XGIbios_mode[myindex].xres > 1280)
			return -1;
		break;
	case XGIFB_DISP_NONE:
		break;
	}

check_memory:
	required_mem = XGIbios_mode[myindex].xres * XGIbios_mode[myindex].yres *
		       XGIbios_mode[myindex].bpp / 8;
	if (required_mem > xgifb_info->video_size)
		return -1;
	return myindex;

}

static void XGIfb_search_crt2type(const char *name)
{
	int i = 0;

	if (name == NULL)
		return;

	while (XGI_crt2type[i].type_no != -1) {
		if (!strcmp(name, XGI_crt2type[i].name)) {
			XGIfb_crt2type = XGI_crt2type[i].type_no;
			XGIfb_tvplug = XGI_crt2type[i].tvplug_no;
			break;
		}
		i++;
	}
	if (XGIfb_crt2type < 0)
		pr_info("Invalid CRT2 type: %s\n", name);
}

static u8 XGIfb_search_refresh_rate(struct xgifb_video_info *xgifb_info,
				    unsigned int rate)
{
	u16 xres, yres;
	int i = 0;

	xres = XGIbios_mode[xgifb_info->mode_idx].xres;
	yres = XGIbios_mode[xgifb_info->mode_idx].yres;

	xgifb_info->rate_idx = 0;
	while ((XGIfb_vrate[i].idx != 0) && (XGIfb_vrate[i].xres <= xres)) {
		if ((XGIfb_vrate[i].xres == xres) &&
		    (XGIfb_vrate[i].yres == yres)) {
			if (XGIfb_vrate[i].refresh == rate) {
				xgifb_info->rate_idx = XGIfb_vrate[i].idx;
				break;
			} else if (XGIfb_vrate[i].refresh > rate) {
				if ((XGIfb_vrate[i].refresh - rate) <= 3) {
					pr_debug("Adjusting rate from %d up to %d\n",
						 rate, XGIfb_vrate[i].refresh);
					xgifb_info->rate_idx =
						XGIfb_vrate[i].idx;
					xgifb_info->refresh_rate =
						XGIfb_vrate[i].refresh;
				} else if (((rate - XGIfb_vrate[i - 1].refresh)
						<= 2) && (XGIfb_vrate[i].idx
						!= 1)) {
					pr_debug("Adjusting rate from %d down to %d\n",
						 rate,
						 XGIfb_vrate[i-1].refresh);
					xgifb_info->rate_idx =
						XGIfb_vrate[i - 1].idx;
					xgifb_info->refresh_rate =
						XGIfb_vrate[i - 1].refresh;
				}
				break;
			} else if ((rate - XGIfb_vrate[i].refresh) <= 2) {
				pr_debug("Adjusting rate from %d down to %d\n",
					 rate, XGIfb_vrate[i].refresh);
				xgifb_info->rate_idx = XGIfb_vrate[i].idx;
				break;
			}
		}
		i++;
	}
	if (xgifb_info->rate_idx > 0)
		return xgifb_info->rate_idx;
	pr_info("Unsupported rate %d for %dx%d\n",
		rate, xres, yres);
	return 0;
}

static void XGIfb_search_tvstd(const char *name)
{
	int i = 0;

	if (name == NULL)
		return;

	while (XGI_tvtype[i].type_no != -1) {
		if (!strcmp(name, XGI_tvtype[i].name)) {
			XGIfb_tvmode = XGI_tvtype[i].type_no;
			break;
		}
		i++;
	}
}

/* ----------- FBDev related routines for all series ----------- */

static void XGIfb_bpp_to_var(struct xgifb_video_info *xgifb_info,
			     struct fb_var_screeninfo *var)
{
	switch (var->bits_per_pixel) {
	case 8:
		var->red.offset = var->green.offset = var->blue.offset = 0;
		var->red.length = var->green.length = var->blue.length = 6;
		xgifb_info->video_cmap_len = 256;
		break;
	case 16:
		var->red.offset = 11;
		var->red.length = 5;
		var->green.offset = 5;
		var->green.length = 6;
		var->blue.offset = 0;
		var->blue.length = 5;
		var->transp.offset = 0;
		var->transp.length = 0;
		xgifb_info->video_cmap_len = 16;
		break;
	case 32:
		var->red.offset = 16;
		var->red.length = 8;
		var->green.offset = 8;
		var->green.length = 8;
		var->blue.offset = 0;
		var->blue.length = 8;
		var->transp.offset = 24;
		var->transp.length = 8;
		xgifb_info->video_cmap_len = 16;
		break;
	}
}

/* --------------------- SetMode routines ------------------------- */

static void XGIfb_pre_setmode(struct xgifb_video_info *xgifb_info)
{
	u8 cr30 = 0, cr31 = 0;

	cr31 = xgifb_reg_get(XGICR, 0x31);
	cr31 &= ~0x60;

	switch (xgifb_info->display2) {
	case XGIFB_DISP_CRT:
		cr30 = (SIS_VB_OUTPUT_CRT2 | SIS_SIMULTANEOUS_VIEW_ENABLE);
		cr31 |= SIS_DRIVER_MODE;
		break;
	case XGIFB_DISP_LCD:
		cr30 = (SIS_VB_OUTPUT_LCD | SIS_SIMULTANEOUS_VIEW_ENABLE);
		cr31 |= SIS_DRIVER_MODE;
		break;
	case XGIFB_DISP_TV:
		if (xgifb_info->TV_type == TVMODE_HIVISION)
			cr30 = (SIS_VB_OUTPUT_HIVISION
					| SIS_SIMULTANEOUS_VIEW_ENABLE);
		else if (xgifb_info->TV_plug == TVPLUG_SVIDEO)
			cr30 = (SIS_VB_OUTPUT_SVIDEO
					| SIS_SIMULTANEOUS_VIEW_ENABLE);
		else if (xgifb_info->TV_plug == TVPLUG_COMPOSITE)
			cr30 = (SIS_VB_OUTPUT_COMPOSITE
					| SIS_SIMULTANEOUS_VIEW_ENABLE);
		else if (xgifb_info->TV_plug == TVPLUG_SCART)
			cr30 = (SIS_VB_OUTPUT_SCART
					| SIS_SIMULTANEOUS_VIEW_ENABLE);
		cr31 |= SIS_DRIVER_MODE;

		if (XGIfb_tvmode == 1 || xgifb_info->TV_type == TVMODE_PAL)
			cr31 |= 0x01;
		else
			cr31 &= ~0x01;
		break;
	default: /* disable CRT2 */
		cr30 = 0x00;
		cr31 |= (SIS_DRIVER_MODE | SIS_VB_OUTPUT_DISABLE);
	}

	xgifb_reg_set(XGICR, IND_XGI_SCRATCH_REG_CR30, cr30);
	xgifb_reg_set(XGICR, IND_XGI_SCRATCH_REG_CR31, cr31);
	xgifb_reg_set(XGICR, IND_XGI_SCRATCH_REG_CR33,
						(xgifb_info->rate_idx & 0x0F));
}

static void XGIfb_post_setmode(struct xgifb_video_info *xgifb_info)
{
	u8 reg;
	unsigned char doit = 1;

	if (xgifb_info->video_bpp == 8) {
		/*
		 * We can't switch off CRT1 on LVDS/Chrontel
		 * in 8bpp Modes
		 */
		if ((xgifb_info->hasVB == HASVB_LVDS) ||
		    (xgifb_info->hasVB == HASVB_LVDS_CHRONTEL)) {
			doit = 0;
		}
		/*
		 * We can't switch off CRT1 on 301B-DH
		 * in 8bpp Modes if using LCD
		 */
		if (xgifb_info->display2 == XGIFB_DISP_LCD)
			doit = 0;
	}

	/* We can't switch off CRT1 if bridge is in slave mode */
	if (xgifb_info->hasVB != HASVB_NONE) {
		reg = xgifb_reg_get(XGIPART1, 0x00);

		if ((reg & 0x50) == 0x10)
			doit = 0;

	} else {
		XGIfb_crt1off = 0;
	}

	reg = xgifb_reg_get(XGICR, 0x17);
	if ((XGIfb_crt1off) && (doit))
		reg &= ~0x80;
	else
		reg |= 0x80;
	xgifb_reg_set(XGICR, 0x17, reg);

	xgifb_reg_and(XGISR, IND_SIS_RAMDAC_CONTROL, ~0x04);

	if (xgifb_info->display2 == XGIFB_DISP_TV &&
	    xgifb_info->hasVB == HASVB_301) {

		reg = xgifb_reg_get(XGIPART4, 0x01);

		if (reg < 0xB0) { /* Set filter for XGI301 */
			int filter_tb;

			switch (xgifb_info->video_width) {
			case 320:
				filter_tb = (xgifb_info->TV_type ==
					     TVMODE_NTSC) ? 4 : 12;
				break;
			case 640:
				filter_tb = (xgifb_info->TV_type ==
					     TVMODE_NTSC) ? 5 : 13;
				break;
			case 720:
				filter_tb = (xgifb_info->TV_type ==
					     TVMODE_NTSC) ? 6 : 14;
				break;
			case 800:
				filter_tb = (xgifb_info->TV_type ==
					     TVMODE_NTSC) ? 7 : 15;
				break;
			default:
				filter_tb = 0;
				filter = -1;
				break;
			}
			xgifb_reg_or(XGIPART1,
				     SIS_CRT2_WENABLE_315,
				     0x01);

			if (xgifb_info->TV_type == TVMODE_NTSC) {

				xgifb_reg_and(XGIPART2, 0x3a, 0x1f);

				if (xgifb_info->TV_plug == TVPLUG_SVIDEO) {

					xgifb_reg_and(XGIPART2, 0x30, 0xdf);

				} else if (xgifb_info->TV_plug
						== TVPLUG_COMPOSITE) {

					xgifb_reg_or(XGIPART2, 0x30, 0x20);

					switch (xgifb_info->video_width) {
					case 640:
						xgifb_reg_set(XGIPART2,
							      0x35,
							      0xEB);
						xgifb_reg_set(XGIPART2,
							      0x36,
							      0x04);
						xgifb_reg_set(XGIPART2,
							      0x37,
							      0x25);
						xgifb_reg_set(XGIPART2,
							      0x38,
							      0x18);
						break;
					case 720:
						xgifb_reg_set(XGIPART2,
							      0x35,
							      0xEE);
						xgifb_reg_set(XGIPART2,
							      0x36,
							      0x0C);
						xgifb_reg_set(XGIPART2,
							      0x37,
							      0x22);
						xgifb_reg_set(XGIPART2,
							      0x38,
							      0x08);
						break;
					case 800:
						xgifb_reg_set(XGIPART2,
							      0x35,
							      0xEB);
						xgifb_reg_set(XGIPART2,
							      0x36,
							      0x15);
						xgifb_reg_set(XGIPART2,
							      0x37,
							      0x25);
						xgifb_reg_set(XGIPART2,
							      0x38,
							      0xF6);
						break;
					}
				}

			} else if (xgifb_info->TV_type == TVMODE_PAL) {

				xgifb_reg_and(XGIPART2, 0x3A, 0x1F);

				if (xgifb_info->TV_plug == TVPLUG_SVIDEO) {

					xgifb_reg_and(XGIPART2, 0x30, 0xDF);

				} else if (xgifb_info->TV_plug
						== TVPLUG_COMPOSITE) {

					xgifb_reg_or(XGIPART2, 0x30, 0x20);

					switch (xgifb_info->video_width) {
					case 640:
						xgifb_reg_set(XGIPART2,
							      0x35,
							      0xF1);
						xgifb_reg_set(XGIPART2,
							      0x36,
							      0xF7);
						xgifb_reg_set(XGIPART2,
							      0x37,
							      0x1F);
						xgifb_reg_set(XGIPART2,
							      0x38,
							      0x32);
						break;
					case 720:
						xgifb_reg_set(XGIPART2,
							      0x35,
							      0xF3);
						xgifb_reg_set(XGIPART2,
							      0x36,
							      0x00);
						xgifb_reg_set(XGIPART2,
							      0x37,
							      0x1D);
						xgifb_reg_set(XGIPART2,
							      0x38,
							      0x20);
						break;
					case 800:
						xgifb_reg_set(XGIPART2,
							      0x35,
							      0xFC);
						xgifb_reg_set(XGIPART2,
							      0x36,
							      0xFB);
						xgifb_reg_set(XGIPART2,
							      0x37,
							      0x14);
						xgifb_reg_set(XGIPART2,
							      0x38,
							      0x2A);
						break;
					}
				}
			}

			if ((filter >= 0) && (filter <= 7)) {
				pr_debug("FilterTable[%d]-%d: %*ph\n",
					 filter_tb, filter,
					 4, XGI_TV_filter[filter_tb].
						   filter[filter]);
				xgifb_reg_set(
					XGIPART2,
					0x35,
					(XGI_TV_filter[filter_tb].
						filter[filter][0]));
				xgifb_reg_set(
					XGIPART2,
					0x36,
					(XGI_TV_filter[filter_tb].
						filter[filter][1]));
				xgifb_reg_set(
					XGIPART2,
					0x37,
					(XGI_TV_filter[filter_tb].
						filter[filter][2]));
				xgifb_reg_set(
					XGIPART2,
					0x38,
					(XGI_TV_filter[filter_tb].
						filter[filter][3]));
			}
		}
	}
}

static int XGIfb_do_set_var(struct fb_var_screeninfo *var, int isactive,
		struct fb_info *info)
{
	struct xgifb_video_info *xgifb_info = info->par;
	struct xgi_hw_device_info *hw_info = &xgifb_info->hw_info;
	unsigned int htotal = var->left_margin + var->xres + var->right_margin
			+ var->hsync_len;
	unsigned int vtotal = var->upper_margin + var->yres + var->lower_margin
			+ var->vsync_len;
#if defined(__powerpc__)
	u8 cr_data;
#endif
	unsigned int drate = 0, hrate = 0;
	int found_mode = 0;
	int old_mode;

	info->var.xres_virtual = var->xres_virtual;
	info->var.yres_virtual = var->yres_virtual;
	info->var.bits_per_pixel = var->bits_per_pixel;

	if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_NONINTERLACED)
		vtotal <<= 1;
	else if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE)
		vtotal <<= 2;

	if (!htotal || !vtotal) {
		pr_debug("Invalid 'var' information\n");
		return -EINVAL;
	} pr_debug("var->pixclock=%d, htotal=%d, vtotal=%d\n",
			var->pixclock, htotal, vtotal);

	if (var->pixclock && htotal && vtotal) {
		drate = 1000000000 / var->pixclock;
		hrate = (drate * 1000) / htotal;
		xgifb_info->refresh_rate = (unsigned int) (hrate * 2
				/ vtotal);
	} else {
		xgifb_info->refresh_rate = 60;
	}

	pr_debug("Change mode to %dx%dx%d-%dHz\n",
	       var->xres,
	       var->yres,
	       var->bits_per_pixel,
	       xgifb_info->refresh_rate);

	old_mode = xgifb_info->mode_idx;
	xgifb_info->mode_idx = 0;

	while ((XGIbios_mode[xgifb_info->mode_idx].mode_no != 0) &&
	       (XGIbios_mode[xgifb_info->mode_idx].xres <= var->xres)) {
		if ((XGIbios_mode[xgifb_info->mode_idx].xres == var->xres) &&
		    (XGIbios_mode[xgifb_info->mode_idx].yres == var->yres) &&
		    (XGIbios_mode[xgifb_info->mode_idx].bpp
						== var->bits_per_pixel)) {
			found_mode = 1;
			break;
		}
		xgifb_info->mode_idx++;
	}

	if (found_mode)
		xgifb_info->mode_idx = XGIfb_validate_mode(xgifb_info,
							xgifb_info->mode_idx);
	else
		xgifb_info->mode_idx = -1;

	if (xgifb_info->mode_idx < 0) {
		pr_err("Mode %dx%dx%d not supported\n",
		       var->xres, var->yres, var->bits_per_pixel);
		xgifb_info->mode_idx = old_mode;
		return -EINVAL;
	}

	if (XGIfb_search_refresh_rate(xgifb_info,
				      xgifb_info->refresh_rate) == 0) {
		xgifb_info->rate_idx = 1;
		xgifb_info->refresh_rate = 60;
	}

	if (isactive) {

		XGIfb_pre_setmode(xgifb_info);
		if (XGISetModeNew(xgifb_info, hw_info,
				  XGIbios_mode[xgifb_info->mode_idx].mode_no)
					== 0) {
			pr_err("Setting mode[0x%x] failed\n",
			       XGIbios_mode[xgifb_info->mode_idx].mode_no);
			return -EINVAL;
		}
		info->fix.line_length = ((info->var.xres_virtual
				* info->var.bits_per_pixel) >> 6);

		xgifb_reg_set(XGISR, IND_SIS_PASSWORD, SIS_PASSWORD);

		xgifb_reg_set(XGICR, 0x13, (info->fix.line_length & 0x00ff));
		xgifb_reg_set(XGISR,
			      0x0E,
			      (info->fix.line_length & 0xff00) >> 8);

		XGIfb_post_setmode(xgifb_info);

		pr_debug("Set new mode: %dx%dx%d-%d\n",
			 XGIbios_mode[xgifb_info->mode_idx].xres,
			 XGIbios_mode[xgifb_info->mode_idx].yres,
			 XGIbios_mode[xgifb_info->mode_idx].bpp,
			 xgifb_info->refresh_rate);

		xgifb_info->video_bpp = XGIbios_mode[xgifb_info->mode_idx].bpp;
		xgifb_info->video_vwidth = info->var.xres_virtual;
		xgifb_info->video_width =
			XGIbios_mode[xgifb_info->mode_idx].xres;
		xgifb_info->video_vheight = info->var.yres_virtual;
		xgifb_info->video_height =
			XGIbios_mode[xgifb_info->mode_idx].yres;
		xgifb_info->org_x = xgifb_info->org_y = 0;
		xgifb_info->video_linelength = info->var.xres_virtual
				* (xgifb_info->video_bpp >> 3);
		switch (xgifb_info->video_bpp) {
		case 8:
			xgifb_info->DstColor = 0x0000;
			xgifb_info->XGI310_AccelDepth = 0x00000000;
			xgifb_info->video_cmap_len = 256;
#if defined(__powerpc__)
			cr_data = xgifb_reg_get(XGICR, 0x4D);
			xgifb_reg_set(XGICR, 0x4D, (cr_data & 0xE0));
#endif
			break;
		case 16:
			xgifb_info->DstColor = 0x8000;
			xgifb_info->XGI310_AccelDepth = 0x00010000;
#if defined(__powerpc__)
			cr_data = xgifb_reg_get(XGICR, 0x4D);
			xgifb_reg_set(XGICR, 0x4D, ((cr_data & 0xE0) | 0x0B));
#endif
			xgifb_info->video_cmap_len = 16;
			break;
		case 32:
			xgifb_info->DstColor = 0xC000;
			xgifb_info->XGI310_AccelDepth = 0x00020000;
			xgifb_info->video_cmap_len = 16;
#if defined(__powerpc__)
			cr_data = xgifb_reg_get(XGICR, 0x4D);
			xgifb_reg_set(XGICR, 0x4D, ((cr_data & 0xE0) | 0x15));
#endif
			break;
		default:
			xgifb_info->video_cmap_len = 16;
			pr_err("Unsupported depth %d\n",
			       xgifb_info->video_bpp);
			break;
		}
	}
	XGIfb_bpp_to_var(xgifb_info, var); /*update ARGB info*/

	dumpVGAReg();
	return 0;
}

static int XGIfb_pan_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct xgifb_video_info *xgifb_info = info->par;
	unsigned int base;

	base = var->yoffset * info->var.xres_virtual + var->xoffset;

	/* calculate base bpp dep. */
	switch (info->var.bits_per_pixel) {
	case 16:
		base >>= 1;
		break;
	case 32:
		break;
	case 8:
	default:
		base >>= 2;
		break;
	}

	xgifb_reg_set(XGISR, IND_SIS_PASSWORD, SIS_PASSWORD);

	xgifb_reg_set(XGICR, 0x0D, base & 0xFF);
	xgifb_reg_set(XGICR, 0x0C, (base >> 8) & 0xFF);
	xgifb_reg_set(XGISR, 0x0D, (base >> 16) & 0xFF);
	xgifb_reg_set(XGISR, 0x37, (base >> 24) & 0x03);
	xgifb_reg_and_or(XGISR, 0x37, 0xDF, (base >> 21) & 0x04);

	if (xgifb_info->display2 != XGIFB_DISP_NONE) {
		xgifb_reg_or(XGIPART1, SIS_CRT2_WENABLE_315, 0x01);
		xgifb_reg_set(XGIPART1, 0x06, (base & 0xFF));
		xgifb_reg_set(XGIPART1, 0x05, ((base >> 8) & 0xFF));
		xgifb_reg_set(XGIPART1, 0x04, ((base >> 16) & 0xFF));
		xgifb_reg_and_or(XGIPART1,
				 0x02,
				 0x7F,
				 ((base >> 24) & 0x01) << 7);
	}
	return 0;
}

static int XGIfb_open(struct fb_info *info, int user)
{
	return 0;
}

static int XGIfb_release(struct fb_info *info, int user)
{
	return 0;
}

/* similar to sisfb_get_cmap_len */
static int XGIfb_get_cmap_len(const struct fb_var_screeninfo *var)
{
	return (var->bits_per_pixel == 8) ? 256 : 16;
}

static int XGIfb_setcolreg(unsigned regno, unsigned red, unsigned green,
		unsigned blue, unsigned transp, struct fb_info *info)
{
	struct xgifb_video_info *xgifb_info = info->par;

	if (regno >= XGIfb_get_cmap_len(&info->var))
		return 1;

	switch (info->var.bits_per_pixel) {
	case 8:
		outb(regno, XGIDACA);
		outb((red >> 10), XGIDACD);
		outb((green >> 10), XGIDACD);
		outb((blue >> 10), XGIDACD);
		if (xgifb_info->display2 != XGIFB_DISP_NONE) {
			outb(regno, XGIDAC2A);
			outb((red >> 8), XGIDAC2D);
			outb((green >> 8), XGIDAC2D);
			outb((blue >> 8), XGIDAC2D);
		}
		break;
	case 16:
		((u32 *) (info->pseudo_palette))[regno] = ((red & 0xf800))
				| ((green & 0xfc00) >> 5) | ((blue & 0xf800)
				>> 11);
		break;
	case 32:
		red >>= 8;
		green >>= 8;
		blue >>= 8;
		((u32 *) (info->pseudo_palette))[regno] = (red << 16) | (green
				<< 8) | (blue);
		break;
	}
	return 0;
}

/* ----------- FBDev related routines for all series ---------- */

static int XGIfb_get_fix(struct fb_fix_screeninfo *fix, int con,
		struct fb_info *info)
{
	struct xgifb_video_info *xgifb_info = info->par;

	memset(fix, 0, sizeof(struct fb_fix_screeninfo));

	strncpy(fix->id, "XGI", sizeof(fix->id) - 1);

	/* if register_framebuffer has been called, we must lock */
	if (atomic_read(&info->count))
		mutex_lock(&info->mm_lock);

	fix->smem_start = xgifb_info->video_base;
	fix->smem_len = xgifb_info->video_size;

	/* if register_framebuffer has been called, we can unlock */
	if (atomic_read(&info->count))
		mutex_unlock(&info->mm_lock);

	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->type_aux = 0;
	if (xgifb_info->video_bpp == 8)
		fix->visual = FB_VISUAL_PSEUDOCOLOR;
	else
		fix->visual = FB_VISUAL_DIRECTCOLOR;
	fix->xpanstep = 0;
	if (XGIfb_ypan)
		fix->ypanstep = 1;
	fix->ywrapstep = 0;
	fix->line_length = xgifb_info->video_linelength;
	fix->mmio_start = xgifb_info->mmio_base;
	fix->mmio_len = xgifb_info->mmio_size;
	fix->accel = FB_ACCEL_SIS_XABRE;

	return 0;
}

static int XGIfb_set_par(struct fb_info *info)
{
	int err;

	err = XGIfb_do_set_var(&info->var, 1, info);
	if (err)
		return err;
	XGIfb_get_fix(&info->fix, -1, info);
	return 0;
}

static int XGIfb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{
	struct xgifb_video_info *xgifb_info = info->par;
	unsigned int htotal = var->left_margin + var->xres + var->right_margin
			+ var->hsync_len;
	unsigned int vtotal = 0;
	unsigned int drate = 0, hrate = 0;
	int found_mode = 0;
	int refresh_rate, search_idx;

	if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_NONINTERLACED) {
		vtotal = var->upper_margin + var->yres + var->lower_margin
				+ var->vsync_len;
		vtotal <<= 1;
	} else if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_DOUBLE) {
		vtotal = var->upper_margin + var->yres + var->lower_margin
				+ var->vsync_len;
		vtotal <<= 2;
	} else if ((var->vmode & FB_VMODE_MASK) == FB_VMODE_INTERLACED) {
		vtotal = var->upper_margin + (var->yres / 2)
				+ var->lower_margin + var->vsync_len;
	} else
		vtotal = var->upper_margin + var->yres + var->lower_margin
				+ var->vsync_len;

	if (!(htotal) || !(vtotal)) {
		pr_debug("No valid timing data\n");
		return -EINVAL;
	}

	if (var->pixclock && htotal && vtotal) {
		drate = 1000000000 / var->pixclock;
		hrate = (drate * 1000) / htotal;
		xgifb_info->refresh_rate =
			(unsigned int) (hrate * 2 / vtotal);
		pr_debug(
			"%s: pixclock = %d ,htotal=%d, vtotal=%d\n"
			"%s: drate=%d, hrate=%d, refresh_rate=%d\n",
			__func__, var->pixclock, htotal, vtotal,
			__func__, drate, hrate, xgifb_info->refresh_rate);
	} else {
		xgifb_info->refresh_rate = 60;
	}

	/* Calculation wrong for 1024x600 - force it to 60Hz */
	if ((var->xres == 1024) && (var->yres == 600))
		refresh_rate = 60;

	search_idx = 0;
	while ((XGIbios_mode[search_idx].mode_no != 0) &&
		(XGIbios_mode[search_idx].xres <= var->xres)) {
		if ((XGIbios_mode[search_idx].xres == var->xres) &&
			(XGIbios_mode[search_idx].yres == var->yres) &&
			(XGIbios_mode[search_idx].bpp == var->bits_per_pixel)) {
			if (XGIfb_validate_mode(xgifb_info, search_idx) > 0) {
				found_mode = 1;
				break;
			}
		}
		search_idx++;
	}

	if (!found_mode) {

		pr_err("%dx%dx%d is no valid mode\n",
			var->xres, var->yres, var->bits_per_pixel);
		search_idx = 0;
		while (XGIbios_mode[search_idx].mode_no != 0) {
			if ((var->xres <= XGIbios_mode[search_idx].xres) &&
			    (var->yres <= XGIbios_mode[search_idx].yres) &&
			    (var->bits_per_pixel ==
			     XGIbios_mode[search_idx].bpp)) {
				if (XGIfb_validate_mode(xgifb_info,
							search_idx) > 0) {
					found_mode = 1;
					break;
				}
			}
			search_idx++;
		}
		if (found_mode) {
			var->xres = XGIbios_mode[search_idx].xres;
			var->yres = XGIbios_mode[search_idx].yres;
			pr_debug("Adapted to mode %dx%dx%d\n",
				var->xres, var->yres, var->bits_per_pixel);

		} else {
			pr_err("Failed to find similar mode to %dx%dx%d\n",
				var->xres, var->yres, var->bits_per_pixel);
			return -EINVAL;
		}
	}

	/* Adapt RGB settings */
	XGIfb_bpp_to_var(xgifb_info, var);

	if (!XGIfb_ypan) {
		if (var->xres != var->xres_virtual)
			var->xres_virtual = var->xres;
		if (var->yres != var->yres_virtual)
			var->yres_virtual = var->yres;
	}

	/* Truncate offsets to maximum if too high */
	if (var->xoffset > var->xres_virtual - var->xres)
		var->xoffset = var->xres_virtual - var->xres - 1;

	if (var->yoffset > var->yres_virtual - var->yres)
		var->yoffset = var->yres_virtual - var->yres - 1;

	/* Set everything else to 0 */
	var->red.msb_right =
	var->green.msb_right =
	var->blue.msb_right =
	var->transp.offset = var->transp.length = var->transp.msb_right = 0;

	return 0;
}

static int XGIfb_pan_display(struct fb_var_screeninfo *var,
		struct fb_info *info)
{
	int err;

	if (var->xoffset > (info->var.xres_virtual - info->var.xres))
		return -EINVAL;
	if (var->yoffset > (info->var.yres_virtual - info->var.yres))
		return -EINVAL;

	if (var->vmode & FB_VMODE_YWRAP) {
		if (var->yoffset >= info->var.yres_virtual || var->xoffset)
			return -EINVAL;
	} else if (var->xoffset + info->var.xres > info->var.xres_virtual
				|| var->yoffset + info->var.yres
						> info->var.yres_virtual) {
		return -EINVAL;
	}
	err = XGIfb_pan_var(var, info);
	if (err < 0)
		return err;

	info->var.xoffset = var->xoffset;
	info->var.yoffset = var->yoffset;
	if (var->vmode & FB_VMODE_YWRAP)
		info->var.vmode |= FB_VMODE_YWRAP;
	else
		info->var.vmode &= ~FB_VMODE_YWRAP;

	return 0;
}

static int XGIfb_blank(int blank, struct fb_info *info)
{
	struct xgifb_video_info *xgifb_info = info->par;
	u8 reg;

	reg = xgifb_reg_get(XGICR, 0x17);

	if (blank > 0)
		reg &= 0x7f;
	else
		reg |= 0x80;

	xgifb_reg_set(XGICR, 0x17, reg);
	xgifb_reg_set(XGISR, 0x00, 0x01); /* Synchronous Reset */
	xgifb_reg_set(XGISR, 0x00, 0x03); /* End Reset */
	return 0;
}

static struct fb_ops XGIfb_ops = {
	.owner = THIS_MODULE,
	.fb_open = XGIfb_open,
	.fb_release = XGIfb_release,
	.fb_check_var = XGIfb_check_var,
	.fb_set_par = XGIfb_set_par,
	.fb_setcolreg = XGIfb_setcolreg,
	.fb_pan_display = XGIfb_pan_display,
	.fb_blank = XGIfb_blank,
	.fb_fillrect = cfb_fillrect,
	.fb_copyarea = cfb_copyarea,
	.fb_imageblit = cfb_imageblit,
};

/* ---------------- Chip generation dependent routines ---------------- */

/* for XGI 315/550/650/740/330 */

static int XGIfb_get_dram_size(struct xgifb_video_info *xgifb_info)
{

	u8 ChannelNum, tmp;
	u8 reg = 0;

	/* xorg driver sets 32MB * 1 channel */
	if (xgifb_info->chip == XG27)
		xgifb_reg_set(XGISR, IND_SIS_DRAM_SIZE, 0x51);

	reg = xgifb_reg_get(XGISR, IND_SIS_DRAM_SIZE);
	if (!reg)
		return -1;

	switch ((reg & XGI_DRAM_SIZE_MASK) >> 4) {
	case XGI_DRAM_SIZE_1MB:
		xgifb_info->video_size = 0x100000;
		break;
	case XGI_DRAM_SIZE_2MB:
		xgifb_info->video_size = 0x200000;
		break;
	case XGI_DRAM_SIZE_4MB:
		xgifb_info->video_size = 0x400000;
		break;
	case XGI_DRAM_SIZE_8MB:
		xgifb_info->video_size = 0x800000;
		break;
	case XGI_DRAM_SIZE_16MB:
		xgifb_info->video_size = 0x1000000;
		break;
	case XGI_DRAM_SIZE_32MB:
		xgifb_info->video_size = 0x2000000;
		break;
	case XGI_DRAM_SIZE_64MB:
		xgifb_info->video_size = 0x4000000;
		break;
	case XGI_DRAM_SIZE_128MB:
		xgifb_info->video_size = 0x8000000;
		break;
	case XGI_DRAM_SIZE_256MB:
		xgifb_info->video_size = 0x10000000;
		break;
	default:
		return -1;
	}

	tmp = (reg & 0x0c) >> 2;
	switch (xgifb_info->chip) {
	case XG20:
	case XG21:
	case XG27:
		ChannelNum = 1;
		break;

	case XG42:
		if (reg & 0x04)
			ChannelNum = 2;
		else
			ChannelNum = 1;
		break;

	case XG40:
	default:
		if (tmp == 2)
			ChannelNum = 2;
		else if (tmp == 3)
			ChannelNum = 3;
		else
			ChannelNum = 1;
		break;
	}

	xgifb_info->video_size = xgifb_info->video_size * ChannelNum;

	pr_info("SR14=%x DramSzie %x ChannelNum %x\n",
	       reg,
	       xgifb_info->video_size, ChannelNum);
	return 0;

}

static void XGIfb_detect_VB(struct xgifb_video_info *xgifb_info)
{
	u8 cr32, temp = 0;

	xgifb_info->TV_plug = xgifb_info->TV_type = 0;

	cr32 = xgifb_reg_get(XGICR, IND_XGI_SCRATCH_REG_CR32);

	if ((cr32 & SIS_CRT1) && !XGIfb_crt1off)
		XGIfb_crt1off = 0;
	else {
		if (cr32 & 0x5F)
			XGIfb_crt1off = 1;
		else
			XGIfb_crt1off = 0;
	}

	if (!xgifb_info->display2_force) {
		if (cr32 & SIS_VB_TV)
			xgifb_info->display2 = XGIFB_DISP_TV;
		else if (cr32 & SIS_VB_LCD)
			xgifb_info->display2 = XGIFB_DISP_LCD;
		else if (cr32 & SIS_VB_CRT2)
			xgifb_info->display2 = XGIFB_DISP_CRT;
		else
			xgifb_info->display2 = XGIFB_DISP_NONE;
	}

	if (XGIfb_tvplug != -1)
		/* Override with option */
		xgifb_info->TV_plug = XGIfb_tvplug;
	else if (cr32 & SIS_VB_HIVISION) {
		xgifb_info->TV_type = TVMODE_HIVISION;
		xgifb_info->TV_plug = TVPLUG_SVIDEO;
	} else if (cr32 & SIS_VB_SVIDEO)
		xgifb_info->TV_plug = TVPLUG_SVIDEO;
	else if (cr32 & SIS_VB_COMPOSITE)
		xgifb_info->TV_plug = TVPLUG_COMPOSITE;
	else if (cr32 & SIS_VB_SCART)
		xgifb_info->TV_plug = TVPLUG_SCART;

	if (xgifb_info->TV_type == 0) {
		temp = xgifb_reg_get(XGICR, 0x38);
		if (temp & 0x10)
			xgifb_info->TV_type = TVMODE_PAL;
		else
			xgifb_info->TV_type = TVMODE_NTSC;
	}

	/* Copy forceCRT1 option to CRT1off if option is given */
	if (XGIfb_forcecrt1 != -1) {
		if (XGIfb_forcecrt1)
			XGIfb_crt1off = 0;
		else
			XGIfb_crt1off = 1;
	}
}

static int XGIfb_has_VB(struct xgifb_video_info *xgifb_info)
{
	u8 vb_chipid;

	vb_chipid = xgifb_reg_get(XGIPART4, 0x00);
	switch (vb_chipid) {
	case 0x01:
		xgifb_info->hasVB = HASVB_301;
		break;
	case 0x02:
		xgifb_info->hasVB = HASVB_302;
		break;
	default:
		xgifb_info->hasVB = HASVB_NONE;
		return 0;
	}
	return 1;
}

static void XGIfb_get_VB_type(struct xgifb_video_info *xgifb_info)
{
	u8 reg;

	if (!XGIfb_has_VB(xgifb_info)) {
		reg = xgifb_reg_get(XGICR, IND_XGI_SCRATCH_REG_CR37);
		switch ((reg & SIS_EXTERNAL_CHIP_MASK) >> 1) {
		case SIS_EXTERNAL_CHIP_LVDS:
			xgifb_info->hasVB = HASVB_LVDS;
			break;
		case SIS_EXTERNAL_CHIP_LVDS_CHRONTEL:
			xgifb_info->hasVB = HASVB_LVDS_CHRONTEL;
			break;
		default:
			break;
		}
	}
}

static int __init xgifb_optval(char *fullopt, int validx)
{
	unsigned long lres;

	if (kstrtoul(fullopt + validx, 0, &lres) < 0 || lres > INT_MAX) {
		pr_err("Invalid value for option: %s\n", fullopt);
		return 0;
	}
	return lres;
}

static int __init XGIfb_setup(char *options)
{
	char *this_opt;

	if (!options || !*options)
		return 0;

	pr_info("Options: %s\n", options);

	while ((this_opt = strsep(&options, ",")) != NULL) {

		if (!*this_opt)
			continue;

		if (!strncmp(this_opt, "mode:", 5)) {
			mode = this_opt + 5;
		} else if (!strncmp(this_opt, "vesa:", 5)) {
			vesa = xgifb_optval(this_opt, 5);
		} else if (!strncmp(this_opt, "vrate:", 6)) {
			refresh_rate = xgifb_optval(this_opt, 6);
		} else if (!strncmp(this_opt, "rate:", 5)) {
			refresh_rate = xgifb_optval(this_opt, 5);
		} else if (!strncmp(this_opt, "crt1off", 7)) {
			XGIfb_crt1off = 1;
		} else if (!strncmp(this_opt, "filter:", 7)) {
			filter = xgifb_optval(this_opt, 7);
		} else if (!strncmp(this_opt, "forcecrt2type:", 14)) {
			XGIfb_search_crt2type(this_opt + 14);
		} else if (!strncmp(this_opt, "forcecrt1:", 10)) {
			XGIfb_forcecrt1 = xgifb_optval(this_opt, 10);
		} else if (!strncmp(this_opt, "tvmode:", 7)) {
			XGIfb_search_tvstd(this_opt + 7);
		} else if (!strncmp(this_opt, "tvstandard:", 11)) {
			XGIfb_search_tvstd(this_opt + 7);
		} else if (!strncmp(this_opt, "dstn", 4)) {
			enable_dstn = 1;
			/* DSTN overrules forcecrt2type */
			XGIfb_crt2type = XGIFB_DISP_LCD;
		} else if (!strncmp(this_opt, "noypan", 6)) {
			XGIfb_ypan = 0;
		} else {
			mode = this_opt;
		}
	}
	return 0;
}

static int xgifb_probe(struct pci_dev *pdev,
		const struct pci_device_id *ent)
{
	u8 reg, reg1;
	u8 CR48, CR38;
	int ret;
	struct fb_info *fb_info;
	struct xgifb_video_info *xgifb_info;
	struct xgi_hw_device_info *hw_info;
	unsigned long video_size_max;

	fb_info = framebuffer_alloc(sizeof(*xgifb_info), &pdev->dev);
	if (!fb_info)
		return -ENOMEM;

	xgifb_info = fb_info->par;
	hw_info = &xgifb_info->hw_info;
	xgifb_info->fb_info = fb_info;
	xgifb_info->chip_id = pdev->device;
	pci_read_config_byte(pdev,
			     PCI_REVISION_ID,
			     &xgifb_info->revision_id);
	hw_info->jChipRevision = xgifb_info->revision_id;

	xgifb_info->pcibus = pdev->bus->number;
	xgifb_info->pcislot = PCI_SLOT(pdev->devfn);
	xgifb_info->pcifunc = PCI_FUNC(pdev->devfn);
	xgifb_info->subsysvendor = pdev->subsystem_vendor;
	xgifb_info->subsysdevice = pdev->subsystem_device;

	video_size_max = pci_resource_len(pdev, 0);
	xgifb_info->video_base = pci_resource_start(pdev, 0);
	xgifb_info->mmio_base = pci_resource_start(pdev, 1);
	xgifb_info->mmio_size = pci_resource_len(pdev, 1);
	xgifb_info->vga_base = pci_resource_start(pdev, 2) + 0x30;
	dev_info(&pdev->dev, "Relocate IO address: %Lx [%08lx]\n",
		 (u64) pci_resource_start(pdev, 2),
		 xgifb_info->vga_base);

	if (pci_enable_device(pdev)) {
		ret = -EIO;
		goto error;
	}

	if (XGIfb_crt2type != -1) {
		xgifb_info->display2 = XGIfb_crt2type;
		xgifb_info->display2_force = true;
	}

	XGIRegInit(&xgifb_info->dev_info, xgifb_info->vga_base);

	xgifb_reg_set(XGISR, IND_SIS_PASSWORD, SIS_PASSWORD);
	reg1 = xgifb_reg_get(XGISR, IND_SIS_PASSWORD);

	if (reg1 != 0xa1) { /*I/O error */
		dev_err(&pdev->dev, "I/O error\n");
		ret = -EIO;
		goto error_disable;
	}

	switch (xgifb_info->chip_id) {
	case PCI_DEVICE_ID_XGI_20:
		xgifb_reg_or(XGICR, Index_CR_GPIO_Reg3, GPIOG_EN);
		CR48 = xgifb_reg_get(XGICR, Index_CR_GPIO_Reg1);
		if (CR48&GPIOG_READ)
			xgifb_info->chip = XG21;
		else
			xgifb_info->chip = XG20;
		break;
	case PCI_DEVICE_ID_XGI_40:
		xgifb_info->chip = XG40;
		break;
	case PCI_DEVICE_ID_XGI_42:
		xgifb_info->chip = XG42;
		break;
	case PCI_DEVICE_ID_XGI_27:
		xgifb_info->chip = XG27;
		break;
	default:
		ret = -ENODEV;
		goto error_disable;
	}

	dev_info(&pdev->dev, "chipid = %x\n", xgifb_info->chip);
	hw_info->jChipType = xgifb_info->chip;

	if (XGIfb_get_dram_size(xgifb_info)) {
		xgifb_info->video_size = min_t(unsigned long, video_size_max,
						SZ_16M);
	} else if (xgifb_info->video_size > video_size_max) {
		xgifb_info->video_size = video_size_max;
	}

	/* Enable PCI_LINEAR_ADDRESSING and MMIO_ENABLE  */
	xgifb_reg_or(XGISR,
		     IND_SIS_PCI_ADDRESS_SET,
		     (SIS_PCI_ADDR_ENABLE | SIS_MEM_MAP_IO_ENABLE));
	/* Enable 2D accelerator engine */
	xgifb_reg_or(XGISR, IND_SIS_MODULE_ENABLE, SIS_ENABLE_2D);

	hw_info->ulVideoMemorySize = xgifb_info->video_size;

	if (!request_mem_region(xgifb_info->video_base,
				xgifb_info->video_size,
				"XGIfb FB")) {
		dev_err(&pdev->dev, "Unable request memory size %x\n",
		       xgifb_info->video_size);
		dev_err(&pdev->dev,
			"Fatal error: Unable to reserve frame buffer memory. Is there another framebuffer driver active?\n");
		ret = -ENODEV;
		goto error_disable;
	}

	if (!request_mem_region(xgifb_info->mmio_base,
				xgifb_info->mmio_size,
				"XGIfb MMIO")) {
		dev_err(&pdev->dev,
			"Fatal error: Unable to reserve MMIO region\n");
		ret = -ENODEV;
		goto error_0;
	}

	xgifb_info->video_vbase = hw_info->pjVideoMemoryAddress =
	ioremap(xgifb_info->video_base, xgifb_info->video_size);
	xgifb_info->mmio_vbase = ioremap(xgifb_info->mmio_base,
					    xgifb_info->mmio_size);

	dev_info(&pdev->dev,
		 "Framebuffer at 0x%Lx, mapped to 0x%p, size %dk\n",
		 (u64) xgifb_info->video_base,
		 xgifb_info->video_vbase,
		 xgifb_info->video_size / 1024);

	dev_info(&pdev->dev,
		 "MMIO at 0x%Lx, mapped to 0x%p, size %ldk\n",
		 (u64) xgifb_info->mmio_base, xgifb_info->mmio_vbase,
		 xgifb_info->mmio_size / 1024);

	pci_set_drvdata(pdev, xgifb_info);
	if (!XGIInitNew(pdev))
		dev_err(&pdev->dev, "XGIInitNew() failed!\n");

	xgifb_info->mtrr = -1;

	xgifb_info->hasVB = HASVB_NONE;
	if ((xgifb_info->chip == XG20) ||
	    (xgifb_info->chip == XG27)) {
		xgifb_info->hasVB = HASVB_NONE;
	} else if (xgifb_info->chip == XG21) {
		CR38 = xgifb_reg_get(XGICR, 0x38);
		if ((CR38&0xE0) == 0xC0)
			xgifb_info->display2 = XGIFB_DISP_LCD;
		else if ((CR38&0xE0) == 0x60)
			xgifb_info->hasVB = HASVB_CHRONTEL;
		else
			xgifb_info->hasVB = HASVB_NONE;
	} else {
		XGIfb_get_VB_type(xgifb_info);
	}

	hw_info->ujVBChipID = VB_CHIP_UNKNOWN;

	hw_info->ulExternalChip = 0;

	switch (xgifb_info->hasVB) {
	case HASVB_301:
		reg = xgifb_reg_get(XGIPART4, 0x01);
		if (reg >= 0xE0) {
			hw_info->ujVBChipID = VB_CHIP_302LV;
			dev_info(&pdev->dev,
				 "XGI302LV bridge detected (revision 0x%02x)\n",
				 reg);
		} else if (reg >= 0xD0) {
			hw_info->ujVBChipID = VB_CHIP_301LV;
			dev_info(&pdev->dev,
				 "XGI301LV bridge detected (revision 0x%02x)\n",
				 reg);
		} else {
			hw_info->ujVBChipID = VB_CHIP_301;
			dev_info(&pdev->dev, "XGI301 bridge detected\n");
		}
		break;
	case HASVB_302:
		reg = xgifb_reg_get(XGIPART4, 0x01);
		if (reg >= 0xE0) {
			hw_info->ujVBChipID = VB_CHIP_302LV;
			dev_info(&pdev->dev,
				 "XGI302LV bridge detected (revision 0x%02x)\n",
				 reg);
		} else if (reg >= 0xD0) {
			hw_info->ujVBChipID = VB_CHIP_301LV;
			dev_info(&pdev->dev,
				 "XGI302LV bridge detected (revision 0x%02x)\n",
				 reg);
		} else if (reg >= 0xB0) {
			reg1 = xgifb_reg_get(XGIPART4, 0x23);

			hw_info->ujVBChipID = VB_CHIP_302B;

		} else {
			hw_info->ujVBChipID = VB_CHIP_302;
			dev_info(&pdev->dev, "XGI302 bridge detected\n");
		}
		break;
	case HASVB_LVDS:
		hw_info->ulExternalChip = 0x1;
		dev_info(&pdev->dev, "LVDS transmitter detected\n");
		break;
	case HASVB_TRUMPION:
		hw_info->ulExternalChip = 0x2;
		dev_info(&pdev->dev, "Trumpion Zurac LVDS scaler detected\n");
		break;
	case HASVB_CHRONTEL:
		hw_info->ulExternalChip = 0x4;
		dev_info(&pdev->dev, "Chrontel TV encoder detected\n");
		break;
	case HASVB_LVDS_CHRONTEL:
		hw_info->ulExternalChip = 0x5;
		dev_info(&pdev->dev,
			 "LVDS transmitter and Chrontel TV encoder detected\n");
		break;
	default:
		dev_info(&pdev->dev, "No or unknown bridge type detected\n");
		break;
	}

	if (xgifb_info->hasVB != HASVB_NONE)
		XGIfb_detect_VB(xgifb_info);
	else if (xgifb_info->chip != XG21)
		xgifb_info->display2 = XGIFB_DISP_NONE;

	if (xgifb_info->display2 == XGIFB_DISP_LCD) {
		if (!enable_dstn) {
			reg = xgifb_reg_get(XGICR, IND_XGI_LCD_PANEL);
			reg &= 0x0f;
			hw_info->ulCRT2LCDType = XGI310paneltype[reg];
		}
	}

	xgifb_info->mode_idx = -1;

	if (mode)
		XGIfb_search_mode(xgifb_info, mode);
	else if (vesa != -1)
		XGIfb_search_vesamode(xgifb_info, vesa);

	if (xgifb_info->mode_idx >= 0)
		xgifb_info->mode_idx =
			XGIfb_validate_mode(xgifb_info, xgifb_info->mode_idx);

	if (xgifb_info->mode_idx < 0) {
		if (xgifb_info->display2 == XGIFB_DISP_LCD &&
		    xgifb_info->chip == XG21)
			xgifb_info->mode_idx =
				XGIfb_GetXG21DefaultLVDSModeIdx(xgifb_info);
		else
			xgifb_info->mode_idx = DEFAULT_MODE;
	}

	if (xgifb_info->mode_idx < 0) {
		dev_err(&pdev->dev, "No supported video mode found\n");
		ret = -EINVAL;
		goto error_1;
	}

	/* set default refresh rate */
	xgifb_info->refresh_rate = refresh_rate;
	if (xgifb_info->refresh_rate == 0)
		xgifb_info->refresh_rate = 60;
	if (XGIfb_search_refresh_rate(xgifb_info,
			xgifb_info->refresh_rate) == 0) {
		xgifb_info->rate_idx = 1;
		xgifb_info->refresh_rate = 60;
	}

	xgifb_info->video_bpp = XGIbios_mode[xgifb_info->mode_idx].bpp;
	xgifb_info->video_vwidth =
		xgifb_info->video_width =
			XGIbios_mode[xgifb_info->mode_idx].xres;
	xgifb_info->video_vheight =
		xgifb_info->video_height =
			XGIbios_mode[xgifb_info->mode_idx].yres;
	xgifb_info->org_x = xgifb_info->org_y = 0;
	xgifb_info->video_linelength =
		xgifb_info->video_width *
		(xgifb_info->video_bpp >> 3);
	switch (xgifb_info->video_bpp) {
	case 8:
		xgifb_info->DstColor = 0x0000;
		xgifb_info->XGI310_AccelDepth = 0x00000000;
		xgifb_info->video_cmap_len = 256;
		break;
	case 16:
		xgifb_info->DstColor = 0x8000;
		xgifb_info->XGI310_AccelDepth = 0x00010000;
		xgifb_info->video_cmap_len = 16;
		break;
	case 32:
		xgifb_info->DstColor = 0xC000;
		xgifb_info->XGI310_AccelDepth = 0x00020000;
		xgifb_info->video_cmap_len = 16;
		break;
	default:
		xgifb_info->video_cmap_len = 16;
		pr_info("Unsupported depth %d\n",
		       xgifb_info->video_bpp);
		break;
	}

	pr_info("Default mode is %dx%dx%d (%dHz)\n",
	       xgifb_info->video_width,
	       xgifb_info->video_height,
	       xgifb_info->video_bpp,
	       xgifb_info->refresh_rate);

	fb_info->var.red.length		= 8;
	fb_info->var.green.length	= 8;
	fb_info->var.blue.length	= 8;
	fb_info->var.activate		= FB_ACTIVATE_NOW;
	fb_info->var.height		= -1;
	fb_info->var.width		= -1;
	fb_info->var.vmode		= FB_VMODE_NONINTERLACED;
	fb_info->var.xres		= xgifb_info->video_width;
	fb_info->var.xres_virtual	= xgifb_info->video_width;
	fb_info->var.yres		= xgifb_info->video_height;
	fb_info->var.yres_virtual	= xgifb_info->video_height;
	fb_info->var.bits_per_pixel	= xgifb_info->video_bpp;

	XGIfb_bpp_to_var(xgifb_info, &fb_info->var);

	fb_info->var.pixclock = (u32) (1000000000 /
			XGIfb_mode_rate_to_dclock(&xgifb_info->dev_info,
				hw_info,
				XGIbios_mode[xgifb_info->mode_idx].mode_no));

	if (XGIfb_mode_rate_to_ddata(&xgifb_info->dev_info, hw_info,
		XGIbios_mode[xgifb_info->mode_idx].mode_no,
		&fb_info->var.left_margin,
		&fb_info->var.right_margin,
		&fb_info->var.upper_margin,
		&fb_info->var.lower_margin,
		&fb_info->var.hsync_len,
		&fb_info->var.vsync_len,
		&fb_info->var.sync,
		&fb_info->var.vmode)) {

		if ((fb_info->var.vmode & FB_VMODE_MASK) ==
		    FB_VMODE_INTERLACED) {
			fb_info->var.yres <<= 1;
			fb_info->var.yres_virtual <<= 1;
		} else if ((fb_info->var.vmode & FB_VMODE_MASK) ==
			   FB_VMODE_DOUBLE) {
			fb_info->var.pixclock >>= 1;
			fb_info->var.yres >>= 1;
			fb_info->var.yres_virtual >>= 1;
		}

	}

	fb_info->flags = FBINFO_FLAG_DEFAULT;
	fb_info->screen_base = xgifb_info->video_vbase;
	fb_info->fbops = &XGIfb_ops;
	XGIfb_get_fix(&fb_info->fix, -1, fb_info);
	fb_info->pseudo_palette = xgifb_info->pseudo_palette;

	fb_alloc_cmap(&fb_info->cmap, 256 , 0);

#ifdef CONFIG_MTRR
	xgifb_info->mtrr = mtrr_add(xgifb_info->video_base,
		xgifb_info->video_size, MTRR_TYPE_WRCOMB, 1);
	if (xgifb_info->mtrr >= 0)
		dev_info(&pdev->dev, "Added MTRR\n");
#endif

	if (register_framebuffer(fb_info) < 0) {
		ret = -EINVAL;
		goto error_mtrr;
	}

	dumpVGAReg();

	return 0;

error_mtrr:
#ifdef CONFIG_MTRR
	if (xgifb_info->mtrr >= 0)
		mtrr_del(xgifb_info->mtrr, xgifb_info->video_base,
			xgifb_info->video_size);
#endif /* CONFIG_MTRR */
error_1:
	iounmap(xgifb_info->mmio_vbase);
	iounmap(xgifb_info->video_vbase);
	release_mem_region(xgifb_info->mmio_base, xgifb_info->mmio_size);
error_0:
	release_mem_region(xgifb_info->video_base, xgifb_info->video_size);
error_disable:
	pci_disable_device(pdev);
error:
	framebuffer_release(fb_info);
	return ret;
}

/*****************************************************/
/*                PCI DEVICE HANDLING                */
/*****************************************************/

static void xgifb_remove(struct pci_dev *pdev)
{
	struct xgifb_video_info *xgifb_info = pci_get_drvdata(pdev);
	struct fb_info *fb_info = xgifb_info->fb_info;

	unregister_framebuffer(fb_info);
#ifdef CONFIG_MTRR
	if (xgifb_info->mtrr >= 0)
		mtrr_del(xgifb_info->mtrr, xgifb_info->video_base,
			xgifb_info->video_size);
#endif /* CONFIG_MTRR */
	iounmap(xgifb_info->mmio_vbase);
	iounmap(xgifb_info->video_vbase);
	release_mem_region(xgifb_info->mmio_base, xgifb_info->mmio_size);
	release_mem_region(xgifb_info->video_base, xgifb_info->video_size);
	pci_disable_device(pdev);
	framebuffer_release(fb_info);
}

static struct pci_driver xgifb_driver = {
	.name = "xgifb",
	.id_table = xgifb_pci_table,
	.probe = xgifb_probe,
	.remove = xgifb_remove
};



/*****************************************************/
/*                      MODULE                       */
/*****************************************************/

module_param(mode, charp, 0);
MODULE_PARM_DESC(mode,
	"Selects the desired default display mode in the format XxYxDepth (eg. 1024x768x16).");

module_param(forcecrt2type, charp, 0);
MODULE_PARM_DESC(forcecrt2type,
	"Force the second display output type. Possible values are NONE, LCD, TV, VGA, SVIDEO or COMPOSITE.");

module_param(vesa, int, 0);
MODULE_PARM_DESC(vesa,
	"Selects the desired default display mode by VESA mode number (eg. 0x117).");

module_param(filter, int, 0);
MODULE_PARM_DESC(filter,
	"Selects TV flicker filter type (only for systems with a SiS301 video bridge). Possible values 0-7. Default: [no filter]).");

static int __init xgifb_init(void)
{
	char *option = NULL;

	if (forcecrt2type != NULL)
		XGIfb_search_crt2type(forcecrt2type);
	if (fb_get_options("xgifb", &option))
		return -ENODEV;
	XGIfb_setup(option);

	return pci_register_driver(&xgifb_driver);
}

static void __exit xgifb_remove_module(void)
{
	pci_unregister_driver(&xgifb_driver);
	pr_debug("Module unloaded\n");
}

MODULE_DESCRIPTION("Z7 Z9 Z9S Z11 framebuffer device driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("XGITECH , Others");
module_init(xgifb_init);
module_exit(xgifb_remove_module);
