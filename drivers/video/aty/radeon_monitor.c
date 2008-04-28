#include "radeonfb.h"
#include "../edid.h"

static struct fb_var_screeninfo radeonfb_default_var = {
	.xres		= 640,
	.yres		= 480,
	.xres_virtual	= 640,
	.yres_virtual	= 480,
	.bits_per_pixel = 8,
	.red		= { .length = 8 },
	.green		= { .length = 8 },
	.blue		= { .length = 8 },
	.activate	= FB_ACTIVATE_NOW,
	.height		= -1,
	.width		= -1,
	.pixclock	= 39721,
	.left_margin	= 40,
	.right_margin	= 24,
	.upper_margin	= 32,
	.lower_margin	= 11,
	.hsync_len	= 96,
	.vsync_len	= 2,
	.vmode		= FB_VMODE_NONINTERLACED
};

static char *radeon_get_mon_name(int type)
{
	char *pret = NULL;

	switch (type) {
		case MT_NONE:
			pret = "no";
			break;
		case MT_CRT:
			pret = "CRT";
			break;
		case MT_DFP:
			pret = "DFP";
			break;
		case MT_LCD:
			pret = "LCD";
			break;
		case MT_CTV:
			pret = "CTV";
			break;
		case MT_STV:
			pret = "STV";
			break;
	}

	return pret;
}


#if defined(CONFIG_PPC_OF) || defined(CONFIG_SPARC)
/*
 * Try to find monitor informations & EDID data out of the Open Firmware
 * device-tree. This also contains some "hacks" to work around a few machine
 * models with broken OF probing by hard-coding known EDIDs for some Mac
 * laptops internal LVDS panel. (XXX: not done yet)
 */
static int __devinit radeon_parse_montype_prop(struct device_node *dp, u8 **out_EDID,
					       int hdno)
{
        static char *propnames[] = { "DFP,EDID", "LCD,EDID", "EDID",
				     "EDID1", "EDID2",  NULL };
	const u8 *pedid = NULL;
	const u8 *pmt = NULL;
	u8 *tmp;
        int i, mt = MT_NONE;  
	
	pr_debug("analyzing OF properties...\n");
	pmt = of_get_property(dp, "display-type", NULL);
	if (!pmt)
		return MT_NONE;
	pr_debug("display-type: %s\n", pmt);
	/* OF says "LCD" for DFP as well, we discriminate from the caller of this
	 * function
	 */
	if (!strcmp(pmt, "LCD") || !strcmp(pmt, "DFP"))
		mt = MT_DFP;
	else if (!strcmp(pmt, "CRT"))
		mt = MT_CRT;
	else {
		if (strcmp(pmt, "NONE") != 0)
			printk(KERN_WARNING "radeonfb: Unknown OF display-type: %s\n",
			       pmt);
		return MT_NONE;
	}

	for (i = 0; propnames[i] != NULL; ++i) {
		pedid = of_get_property(dp, propnames[i], NULL);
		if (pedid != NULL)
			break;
	}
	/* We didn't find the EDID in the leaf node, some cards will actually
	 * put EDID1/EDID2 in the parent, look for these (typically M6 tipb).
	 * single-head cards have hdno == -1 and skip this step
	 */
	if (pedid == NULL && dp->parent && (hdno != -1))
		pedid = of_get_property(dp->parent,
				(hdno == 0) ? "EDID1" : "EDID2", NULL);
	if (pedid == NULL && dp->parent && (hdno == 0))
		pedid = of_get_property(dp->parent, "EDID", NULL);
	if (pedid == NULL)
		return mt;

	tmp = kmemdup(pedid, EDID_LENGTH, GFP_KERNEL);
	if (!tmp)
		return mt;
	*out_EDID = tmp;
	return mt;
}

static int __devinit radeon_probe_OF_head(struct radeonfb_info *rinfo, int head_no,
					  u8 **out_EDID)
{
        struct device_node *dp;

	pr_debug("radeon_probe_OF_head\n");

        dp = rinfo->of_node;
        while (dp == NULL)
		return MT_NONE;

	if (rinfo->has_CRTC2) {
		const char *pname;
		int len, second = 0;

		dp = dp->child;
		do {
			if (!dp)
				return MT_NONE;
			pname = of_get_property(dp, "name", NULL);
			if (!pname)
				return MT_NONE;
			len = strlen(pname);
			pr_debug("head: %s (letter: %c, head_no: %d)\n",
			       pname, pname[len-1], head_no);
			if (pname[len-1] == 'A' && head_no == 0) {
				int mt = radeon_parse_montype_prop(dp, out_EDID, 0);
				/* Maybe check for LVDS_GEN_CNTL here ? I need to check out
				 * what OF does when booting with lid closed
				 */
				if (mt == MT_DFP && rinfo->is_mobility)
					mt = MT_LCD;
				return mt;
			} else if (pname[len-1] == 'B' && head_no == 1)
				return radeon_parse_montype_prop(dp, out_EDID, 1);
			second = 1;
			dp = dp->sibling;
		} while(!second);
	} else {
		if (head_no > 0)
			return MT_NONE;
		return radeon_parse_montype_prop(dp, out_EDID, -1);
	}
        return MT_NONE;
}
#endif /* CONFIG_PPC_OF || CONFIG_SPARC */


static int __devinit radeon_get_panel_info_BIOS(struct radeonfb_info *rinfo)
{
	unsigned long tmp, tmp0;
	char stmp[30];
	int i;

	if (!rinfo->bios_seg)
		return 0;

	if (!(tmp = BIOS_IN16(rinfo->fp_bios_start + 0x40))) {
		printk(KERN_ERR "radeonfb: Failed to detect DFP panel info using BIOS\n");
		rinfo->panel_info.pwr_delay = 200;
		return 0;
	}

	for(i=0; i<24; i++)
		stmp[i] = BIOS_IN8(tmp+i+1);
	stmp[24] = 0;
	printk("radeonfb: panel ID string: %s\n", stmp);
	rinfo->panel_info.xres = BIOS_IN16(tmp + 25);
	rinfo->panel_info.yres = BIOS_IN16(tmp + 27);
	printk("radeonfb: detected LVDS panel size from BIOS: %dx%d\n",
		rinfo->panel_info.xres, rinfo->panel_info.yres);

	rinfo->panel_info.pwr_delay = BIOS_IN16(tmp + 44);
	pr_debug("BIOS provided panel power delay: %d\n", rinfo->panel_info.pwr_delay);
	if (rinfo->panel_info.pwr_delay > 2000 || rinfo->panel_info.pwr_delay <= 0)
		rinfo->panel_info.pwr_delay = 2000;

	/*
	 * Some panels only work properly with some divider combinations
	 */
	rinfo->panel_info.ref_divider = BIOS_IN16(tmp + 46);
	rinfo->panel_info.post_divider = BIOS_IN8(tmp + 48);
	rinfo->panel_info.fbk_divider = BIOS_IN16(tmp + 49);
	if (rinfo->panel_info.ref_divider != 0 &&
	    rinfo->panel_info.fbk_divider > 3) {
		rinfo->panel_info.use_bios_dividers = 1;
		printk(KERN_INFO "radeondb: BIOS provided dividers will be used\n");
		pr_debug("ref_divider = %x\n", rinfo->panel_info.ref_divider);
		pr_debug("post_divider = %x\n", rinfo->panel_info.post_divider);
		pr_debug("fbk_divider = %x\n", rinfo->panel_info.fbk_divider);
	}
	pr_debug("Scanning BIOS table ...\n");
	for(i=0; i<32; i++) {
		tmp0 = BIOS_IN16(tmp+64+i*2);
		if (tmp0 == 0)
			break;
		pr_debug(" %d x %d\n", BIOS_IN16(tmp0), BIOS_IN16(tmp0+2));
		if ((BIOS_IN16(tmp0) == rinfo->panel_info.xres) &&
		    (BIOS_IN16(tmp0+2) == rinfo->panel_info.yres)) {
			rinfo->panel_info.hblank = (BIOS_IN16(tmp0+17) - BIOS_IN16(tmp0+19)) * 8;
			rinfo->panel_info.hOver_plus = ((BIOS_IN16(tmp0+21) -
							 BIOS_IN16(tmp0+19) -1) * 8) & 0x7fff;
			rinfo->panel_info.hSync_width = BIOS_IN8(tmp0+23) * 8;
			rinfo->panel_info.vblank = BIOS_IN16(tmp0+24) - BIOS_IN16(tmp0+26);
			rinfo->panel_info.vOver_plus = (BIOS_IN16(tmp0+28) & 0x7ff) - BIOS_IN16(tmp0+26);
			rinfo->panel_info.vSync_width = (BIOS_IN16(tmp0+28) & 0xf800) >> 11;
			rinfo->panel_info.clock = BIOS_IN16(tmp0+9);
			/* Assume high active syncs for now until ATI tells me more... maybe we
			 * can probe register values here ?
			 */
			rinfo->panel_info.hAct_high = 1;
			rinfo->panel_info.vAct_high = 1;
			/* Mark panel infos valid */
			rinfo->panel_info.valid = 1;

			pr_debug("Found panel in BIOS table:\n");
			pr_debug("  hblank: %d\n", rinfo->panel_info.hblank);
			pr_debug("  hOver_plus: %d\n", rinfo->panel_info.hOver_plus);
			pr_debug("  hSync_width: %d\n", rinfo->panel_info.hSync_width);
			pr_debug("  vblank: %d\n", rinfo->panel_info.vblank);
			pr_debug("  vOver_plus: %d\n", rinfo->panel_info.vOver_plus);
			pr_debug("  vSync_width: %d\n", rinfo->panel_info.vSync_width);
			pr_debug("  clock: %d\n", rinfo->panel_info.clock);
				
			return 1;
		}
	}
	pr_debug("Didn't find panel in BIOS table !\n");

	return 0;
}

/* Try to extract the connector informations from the BIOS. This
 * doesn't quite work yet, but it's output is still useful for
 * debugging
 */
static void __devinit radeon_parse_connector_info(struct radeonfb_info *rinfo)
{
	int offset, chips, connectors, tmp, i, conn, type;

	static char* __conn_type_table[16] = {
		"NONE", "Proprietary", "CRT", "DVI-I", "DVI-D", "Unknown", "Unknown",
		"Unknown", "Unknown", "Unknown", "Unknown", "Unknown", "Unknown",
		"Unknown", "Unknown", "Unknown"
	};

	if (!rinfo->bios_seg)
		return;

	offset = BIOS_IN16(rinfo->fp_bios_start + 0x50);
	if (offset == 0) {
		printk(KERN_WARNING "radeonfb: No connector info table detected\n");
		return;
	}

	/* Don't do much more at this point but displaying the data if
	 * DEBUG is enabled
	 */
	chips = BIOS_IN8(offset++) >> 4;
	pr_debug("%d chips in connector info\n", chips);
	for (i = 0; i < chips; i++) {
		tmp = BIOS_IN8(offset++);
		connectors = tmp & 0x0f;
		pr_debug(" - chip %d has %d connectors\n", tmp >> 4, connectors);
		for (conn = 0; ; conn++) {
			tmp = BIOS_IN16(offset);
			if (tmp == 0)
				break;
			offset += 2;
			type = (tmp >> 12) & 0x0f;
			pr_debug("  * connector %d of type %d (%s) : %04x\n",
			       conn, type, __conn_type_table[type], tmp);
		}
	}
}


/*
 * Probe physical connection of a CRT. This code comes from XFree
 * as well and currently is only implemented for the CRT DAC, the
 * code for the TVDAC is commented out in XFree as "non working"
 */
static int __devinit radeon_crt_is_connected(struct radeonfb_info *rinfo, int is_crt_dac)
{
    int	          connected = 0;

    /* the monitor either wasn't connected or it is a non-DDC CRT.
     * try to probe it
     */
    if (is_crt_dac) {
	unsigned long ulOrigVCLK_ECP_CNTL;
	unsigned long ulOrigDAC_CNTL;
	unsigned long ulOrigDAC_EXT_CNTL;
	unsigned long ulOrigCRTC_EXT_CNTL;
	unsigned long ulData;
	unsigned long ulMask;

	ulOrigVCLK_ECP_CNTL = INPLL(VCLK_ECP_CNTL);

	ulData              = ulOrigVCLK_ECP_CNTL;
	ulData             &= ~(PIXCLK_ALWAYS_ONb
				| PIXCLK_DAC_ALWAYS_ONb);
	ulMask              = ~(PIXCLK_ALWAYS_ONb
				| PIXCLK_DAC_ALWAYS_ONb);
	OUTPLLP(VCLK_ECP_CNTL, ulData, ulMask);

	ulOrigCRTC_EXT_CNTL = INREG(CRTC_EXT_CNTL);
	ulData              = ulOrigCRTC_EXT_CNTL;
	ulData             |= CRTC_CRT_ON;
	OUTREG(CRTC_EXT_CNTL, ulData);
   
	ulOrigDAC_EXT_CNTL = INREG(DAC_EXT_CNTL);
	ulData             = ulOrigDAC_EXT_CNTL;
	ulData            &= ~DAC_FORCE_DATA_MASK;
	ulData            |=  (DAC_FORCE_BLANK_OFF_EN
			       |DAC_FORCE_DATA_EN
			       |DAC_FORCE_DATA_SEL_MASK);
	if ((rinfo->family == CHIP_FAMILY_RV250) ||
	    (rinfo->family == CHIP_FAMILY_RV280))
	    ulData |= (0x01b6 << DAC_FORCE_DATA_SHIFT);
	else
	    ulData |= (0x01ac << DAC_FORCE_DATA_SHIFT);

	OUTREG(DAC_EXT_CNTL, ulData);

	ulOrigDAC_CNTL     = INREG(DAC_CNTL);
	ulData             = ulOrigDAC_CNTL;
	ulData            |= DAC_CMP_EN;
	ulData            &= ~(DAC_RANGE_CNTL_MASK
			       | DAC_PDWN);
	ulData            |= 0x2;
	OUTREG(DAC_CNTL, ulData);

	mdelay(1);

	ulData     = INREG(DAC_CNTL);
	connected =  (DAC_CMP_OUTPUT & ulData) ? 1 : 0;
  
	ulData    = ulOrigVCLK_ECP_CNTL;
	ulMask    = 0xFFFFFFFFL;
	OUTPLLP(VCLK_ECP_CNTL, ulData, ulMask);

	OUTREG(DAC_CNTL,      ulOrigDAC_CNTL     );
	OUTREG(DAC_EXT_CNTL,  ulOrigDAC_EXT_CNTL );
	OUTREG(CRTC_EXT_CNTL, ulOrigCRTC_EXT_CNTL);
    }

    return connected ? MT_CRT : MT_NONE;
}

/*
 * Parse the "monitor_layout" string if any. This code is mostly
 * copied from XFree's radeon driver
 */
static int __devinit radeon_parse_monitor_layout(struct radeonfb_info *rinfo,
						 const char *monitor_layout)
{
	char s1[5], s2[5];
	int i = 0, second = 0;
	const char *s;

	if (!monitor_layout)
		return 0;

	s = monitor_layout;
	do {
		switch(*s) {
		case ',':
			s1[i] = '\0';
			i = 0;
			second = 1;
			break;
		case ' ':
		case '\0':
			break;
		default:
			if (i > 4)
				break;
			if (second)
				s2[i] = *s;
			else
				s1[i] = *s;
			i++;
		}

		if (i > 4)
			i = 4;

	} while (*s++);
	if (second)
		s2[i] = 0;
	else {
		s1[i] = 0;
		s2[0] = 0;
	}
	if (strcmp(s1, "CRT") == 0)
		rinfo->mon1_type = MT_CRT;
	else if (strcmp(s1, "TMDS") == 0)
		rinfo->mon1_type = MT_DFP;
	else if (strcmp(s1, "LVDS") == 0)
		rinfo->mon1_type = MT_LCD;

	if (strcmp(s2, "CRT") == 0)
		rinfo->mon2_type = MT_CRT;
	else if (strcmp(s2, "TMDS") == 0)
		rinfo->mon2_type = MT_DFP;
	else if (strcmp(s2, "LVDS") == 0)
		rinfo->mon2_type = MT_LCD;

	return 1;
}

/*
 * Probe display on both primary and secondary card's connector (if any)
 * by various available techniques (i2c, OF device tree, BIOS, ...) and
 * try to retrieve EDID. The algorithm here comes from XFree's radeon
 * driver
 */
void __devinit radeon_probe_screens(struct radeonfb_info *rinfo,
				    const char *monitor_layout, int ignore_edid)
{
#ifdef CONFIG_FB_RADEON_I2C
	int ddc_crt2_used = 0;	
#endif
	int tmp, i;

	radeon_parse_connector_info(rinfo);

	if (radeon_parse_monitor_layout(rinfo, monitor_layout)) {

		/*
		 * If user specified a monitor_layout option, use it instead
		 * of auto-detecting. Maybe we should only use this argument
		 * on the first radeon card probed or provide a way to specify
		 * a layout for each card ?
		 */

		pr_debug("Using specified monitor layout: %s", monitor_layout);
#ifdef CONFIG_FB_RADEON_I2C
		if (!ignore_edid) {
			if (rinfo->mon1_type != MT_NONE)
				if (!radeon_probe_i2c_connector(rinfo, ddc_dvi, &rinfo->mon1_EDID)) {
					radeon_probe_i2c_connector(rinfo, ddc_crt2, &rinfo->mon1_EDID);
					ddc_crt2_used = 1;
				}
			if (rinfo->mon2_type != MT_NONE)
				if (!radeon_probe_i2c_connector(rinfo, ddc_vga, &rinfo->mon2_EDID) &&
				    !ddc_crt2_used)
					radeon_probe_i2c_connector(rinfo, ddc_crt2, &rinfo->mon2_EDID);
		}
#endif /* CONFIG_FB_RADEON_I2C */
		if (rinfo->mon1_type == MT_NONE) {
			if (rinfo->mon2_type != MT_NONE) {
				rinfo->mon1_type = rinfo->mon2_type;
				rinfo->mon1_EDID = rinfo->mon2_EDID;
			} else {
				rinfo->mon1_type = MT_CRT;
				printk(KERN_INFO "radeonfb: No valid monitor, assuming CRT on first port\n");
			}
			rinfo->mon2_type = MT_NONE;
			rinfo->mon2_EDID = NULL;
		}
	} else {
		/*
		 * Auto-detecting display type (well... trying to ...)
		 */
		
		pr_debug("Starting monitor auto detection...\n");

#if defined(DEBUG) && defined(CONFIG_FB_RADEON_I2C)
		{
			u8 *EDIDs[4] = { NULL, NULL, NULL, NULL };
			int mon_types[4] = {MT_NONE, MT_NONE, MT_NONE, MT_NONE};
			int i;

			for (i = 0; i < 4; i++)
				mon_types[i] = radeon_probe_i2c_connector(rinfo,
									  i+1, &EDIDs[i]);
		}
#endif /* DEBUG */
		/*
		 * Old single head cards
		 */
		if (!rinfo->has_CRTC2) {
#if defined(CONFIG_PPC_OF) || defined(CONFIG_SPARC)
			if (rinfo->mon1_type == MT_NONE)
				rinfo->mon1_type = radeon_probe_OF_head(rinfo, 0,
									&rinfo->mon1_EDID);
#endif /* CONFIG_PPC_OF || CONFIG_SPARC */
#ifdef CONFIG_FB_RADEON_I2C
			if (rinfo->mon1_type == MT_NONE)
				rinfo->mon1_type =
					radeon_probe_i2c_connector(rinfo, ddc_dvi,
								   &rinfo->mon1_EDID);
			if (rinfo->mon1_type == MT_NONE)
				rinfo->mon1_type =
					radeon_probe_i2c_connector(rinfo, ddc_vga,
								   &rinfo->mon1_EDID);
			if (rinfo->mon1_type == MT_NONE)
				rinfo->mon1_type =
					radeon_probe_i2c_connector(rinfo, ddc_crt2,
								   &rinfo->mon1_EDID);	
#endif /* CONFIG_FB_RADEON_I2C */
			if (rinfo->mon1_type == MT_NONE)
				rinfo->mon1_type = MT_CRT;
			goto bail;
		}

		/*
		 * Check for cards with reversed DACs or TMDS controllers using BIOS
		 */
		if (rinfo->bios_seg &&
		    (tmp = BIOS_IN16(rinfo->fp_bios_start + 0x50))) {
			for (i = 1; i < 4; i++) {
				unsigned int tmp0;

				if (!BIOS_IN8(tmp + i*2) && i > 1)
					break;
				tmp0 = BIOS_IN16(tmp + i*2);
				if ((!(tmp0 & 0x01)) && (((tmp0 >> 8) & 0x0f) == ddc_dvi)) {
					rinfo->reversed_DAC = 1;
					printk(KERN_INFO "radeonfb: Reversed DACs detected\n");
				}
				if ((((tmp0 >> 8) & 0x0f) == ddc_dvi) && ((tmp0 >> 4) & 0x01)) {
					rinfo->reversed_TMDS = 1;
					printk(KERN_INFO "radeonfb: Reversed TMDS detected\n");
				}
			}
		}

		/*
		 * Probe primary head (DVI or laptop internal panel)
		 */
#if defined(CONFIG_PPC_OF) || defined(CONFIG_SPARC)
		if (rinfo->mon1_type == MT_NONE)
			rinfo->mon1_type = radeon_probe_OF_head(rinfo, 0,
								&rinfo->mon1_EDID);
#endif /* CONFIG_PPC_OF || CONFIG_SPARC */
#ifdef CONFIG_FB_RADEON_I2C
		if (rinfo->mon1_type == MT_NONE)
			rinfo->mon1_type = radeon_probe_i2c_connector(rinfo, ddc_dvi,
								      &rinfo->mon1_EDID);
		if (rinfo->mon1_type == MT_NONE) {
			rinfo->mon1_type = radeon_probe_i2c_connector(rinfo, ddc_crt2,
								      &rinfo->mon1_EDID);
			if (rinfo->mon1_type != MT_NONE)
				ddc_crt2_used = 1;
		}
#endif /* CONFIG_FB_RADEON_I2C */
		if (rinfo->mon1_type == MT_NONE && rinfo->is_mobility &&
		    ((rinfo->bios_seg && (INREG(BIOS_4_SCRATCH) & 4))
		     || (INREG(LVDS_GEN_CNTL) & LVDS_ON))) {
			rinfo->mon1_type = MT_LCD;
			printk("Non-DDC laptop panel detected\n");
		}
		if (rinfo->mon1_type == MT_NONE)
			rinfo->mon1_type = radeon_crt_is_connected(rinfo, rinfo->reversed_DAC);

		/*
		 * Probe secondary head (mostly VGA, can be DVI)
		 */
#if defined(CONFIG_PPC_OF) || defined(CONFIG_SPARC)
		if (rinfo->mon2_type == MT_NONE)
			rinfo->mon2_type = radeon_probe_OF_head(rinfo, 1,
								&rinfo->mon2_EDID);
#endif /* CONFIG_PPC_OF || defined(CONFIG_SPARC) */
#ifdef CONFIG_FB_RADEON_I2C
		if (rinfo->mon2_type == MT_NONE)
			rinfo->mon2_type = radeon_probe_i2c_connector(rinfo, ddc_vga,
								      &rinfo->mon2_EDID);
		if (rinfo->mon2_type == MT_NONE && !ddc_crt2_used)
			rinfo->mon2_type = radeon_probe_i2c_connector(rinfo, ddc_crt2,
								      &rinfo->mon2_EDID);
#endif /* CONFIG_FB_RADEON_I2C */
		if (rinfo->mon2_type == MT_NONE)
			rinfo->mon2_type = radeon_crt_is_connected(rinfo, !rinfo->reversed_DAC);

		/*
		 * If we only detected port 2, we swap them, if none detected,
		 * assume CRT (maybe fallback to old BIOS_SCRATCH stuff ? or look
		 * at FP registers ?)
		 */
		if (rinfo->mon1_type == MT_NONE) {
			if (rinfo->mon2_type != MT_NONE) {
				rinfo->mon1_type = rinfo->mon2_type;
				rinfo->mon1_EDID = rinfo->mon2_EDID;
			} else
				rinfo->mon1_type = MT_CRT;
			rinfo->mon2_type = MT_NONE;
			rinfo->mon2_EDID = NULL;
		}

		/*
		 * Deal with reversed TMDS
		 */
		if (rinfo->reversed_TMDS) {
			/* Always keep internal TMDS as primary head */
			if (rinfo->mon1_type == MT_DFP || rinfo->mon2_type == MT_DFP) {
				int tmp_type = rinfo->mon1_type;
				u8 *tmp_EDID = rinfo->mon1_EDID;
				rinfo->mon1_type = rinfo->mon2_type;
				rinfo->mon1_EDID = rinfo->mon2_EDID;
				rinfo->mon2_type = tmp_type;
				rinfo->mon2_EDID = tmp_EDID;
				if (rinfo->mon1_type == MT_CRT || rinfo->mon2_type == MT_CRT)
					rinfo->reversed_DAC ^= 1;
			}
		}
	}
	if (ignore_edid) {
		kfree(rinfo->mon1_EDID);
		rinfo->mon1_EDID = NULL;
		kfree(rinfo->mon2_EDID);
		rinfo->mon2_EDID = NULL;
	}

 bail:
	printk(KERN_INFO "radeonfb: Monitor 1 type %s found\n",
	       radeon_get_mon_name(rinfo->mon1_type));
	if (rinfo->mon1_EDID)
		printk(KERN_INFO "radeonfb: EDID probed\n");
	if (!rinfo->has_CRTC2)
		return;
	printk(KERN_INFO "radeonfb: Monitor 2 type %s found\n",
	       radeon_get_mon_name(rinfo->mon2_type));
	if (rinfo->mon2_EDID)
		printk(KERN_INFO "radeonfb: EDID probed\n");
}


/*
 * This functions applyes any arch/model/machine specific fixups
 * to the panel info. It may eventually alter EDID block as
 * well or whatever is specific to a given model and not probed
 * properly by the default code
 */
static void radeon_fixup_panel_info(struct radeonfb_info *rinfo)
{
#ifdef CONFIG_PPC_OF
	/*
	 * LCD Flat panels should use fixed dividers, we enfore that on
	 * PPC only for now...
	 */
	if (!rinfo->panel_info.use_bios_dividers && rinfo->mon1_type == MT_LCD
	    && rinfo->is_mobility) {
		int ppll_div_sel;
		u32 ppll_divn;
		ppll_div_sel = INREG8(CLOCK_CNTL_INDEX + 1) & 0x3;
		radeon_pll_errata_after_index(rinfo);
		ppll_divn = INPLL(PPLL_DIV_0 + ppll_div_sel);
		rinfo->panel_info.ref_divider = rinfo->pll.ref_div;
		rinfo->panel_info.fbk_divider = ppll_divn & 0x7ff;
		rinfo->panel_info.post_divider = (ppll_divn >> 16) & 0x7;
		rinfo->panel_info.use_bios_dividers = 1;

		printk(KERN_DEBUG "radeonfb: Using Firmware dividers 0x%08x "
		       "from PPLL %d\n",
		       rinfo->panel_info.fbk_divider |
		       (rinfo->panel_info.post_divider << 16),
		       ppll_div_sel);
	}
#endif /* CONFIG_PPC_OF */
}


/*
 * Fill up panel infos from a mode definition, either returned by the EDID
 * or from the default mode when we can't do any better
 */
static void radeon_var_to_panel_info(struct radeonfb_info *rinfo, struct fb_var_screeninfo *var)
{
	rinfo->panel_info.xres = var->xres;
	rinfo->panel_info.yres = var->yres;
	rinfo->panel_info.clock = 100000000 / var->pixclock;
	rinfo->panel_info.hOver_plus = var->right_margin;
	rinfo->panel_info.hSync_width = var->hsync_len;
       	rinfo->panel_info.hblank = var->left_margin +
		(var->right_margin + var->hsync_len);
	rinfo->panel_info.vOver_plus = var->lower_margin;
	rinfo->panel_info.vSync_width = var->vsync_len;
       	rinfo->panel_info.vblank = var->upper_margin +
		(var->lower_margin + var->vsync_len);
	rinfo->panel_info.hAct_high =
		(var->sync & FB_SYNC_HOR_HIGH_ACT) != 0;
	rinfo->panel_info.vAct_high =
		(var->sync & FB_SYNC_VERT_HIGH_ACT) != 0;
	rinfo->panel_info.valid = 1;
	/* We use a default of 200ms for the panel power delay, 
	 * I need to have a real schedule() instead of mdelay's in the panel code.
	 * we might be possible to figure out a better power delay either from
	 * MacOS OF tree or from the EDID block (proprietary extensions ?)
	 */
	rinfo->panel_info.pwr_delay = 200;
}

static void radeon_videomode_to_var(struct fb_var_screeninfo *var,
				    const struct fb_videomode *mode)
{
	var->xres = mode->xres;
	var->yres = mode->yres;
	var->xres_virtual = mode->xres;
	var->yres_virtual = mode->yres;
	var->xoffset = 0;
	var->yoffset = 0;
	var->pixclock = mode->pixclock;
	var->left_margin = mode->left_margin;
	var->right_margin = mode->right_margin;
	var->upper_margin = mode->upper_margin;
	var->lower_margin = mode->lower_margin;
	var->hsync_len = mode->hsync_len;
	var->vsync_len = mode->vsync_len;
	var->sync = mode->sync;
	var->vmode = mode->vmode;
}

/*
 * Build the modedb for head 1 (head 2 will come later), check panel infos
 * from either BIOS or EDID, and pick up the default mode
 */
void __devinit radeon_check_modes(struct radeonfb_info *rinfo, const char *mode_option)
{
	struct fb_info * info = rinfo->info;
	int has_default_mode = 0;

	/*
	 * Fill default var first
	 */
	info->var = radeonfb_default_var;
	INIT_LIST_HEAD(&info->modelist);

	/*
	 * First check out what BIOS has to say
	 */
	if (rinfo->mon1_type == MT_LCD)
		radeon_get_panel_info_BIOS(rinfo);

	/*
	 * Parse EDID detailed timings and deduce panel infos if any. Right now
	 * we only deal with first entry returned by parse_EDID, we may do better
	 * some day...
	 */
	if (!rinfo->panel_info.use_bios_dividers && rinfo->mon1_type != MT_CRT
	    && rinfo->mon1_EDID) {
		struct fb_var_screeninfo var;
		pr_debug("Parsing EDID data for panel info\n");
		if (fb_parse_edid(rinfo->mon1_EDID, &var) == 0) {
			if (var.xres >= rinfo->panel_info.xres &&
			    var.yres >= rinfo->panel_info.yres)
				radeon_var_to_panel_info(rinfo, &var);
		}
	}

	/*
	 * Do any additional platform/arch fixups to the panel infos
	 */
	radeon_fixup_panel_info(rinfo);

	/*
	 * If we have some valid panel infos, we setup the default mode based on
	 * those
	 */
	if (rinfo->mon1_type != MT_CRT && rinfo->panel_info.valid) {
		struct fb_var_screeninfo *var = &info->var;

		pr_debug("Setting up default mode based on panel info\n");
		var->xres = rinfo->panel_info.xres;
		var->yres = rinfo->panel_info.yres;
		var->xres_virtual = rinfo->panel_info.xres;
		var->yres_virtual = rinfo->panel_info.yres;
		var->xoffset = var->yoffset = 0;
		var->bits_per_pixel = 8;
		var->pixclock = 100000000 / rinfo->panel_info.clock;
		var->left_margin = (rinfo->panel_info.hblank - rinfo->panel_info.hOver_plus
				    - rinfo->panel_info.hSync_width);
		var->right_margin = rinfo->panel_info.hOver_plus;
		var->upper_margin = (rinfo->panel_info.vblank - rinfo->panel_info.vOver_plus
				     - rinfo->panel_info.vSync_width);
		var->lower_margin = rinfo->panel_info.vOver_plus;
		var->hsync_len = rinfo->panel_info.hSync_width;
		var->vsync_len = rinfo->panel_info.vSync_width;
		var->sync = 0;
		if (rinfo->panel_info.hAct_high)
			var->sync |= FB_SYNC_HOR_HIGH_ACT;
		if (rinfo->panel_info.vAct_high)
			var->sync |= FB_SYNC_VERT_HIGH_ACT;
		var->vmode = 0;
		has_default_mode = 1;
	}

	/*
	 * Now build modedb from EDID
	 */
	if (rinfo->mon1_EDID) {
		fb_edid_to_monspecs(rinfo->mon1_EDID, &info->monspecs);
		fb_videomode_to_modelist(info->monspecs.modedb,
					 info->monspecs.modedb_len,
					 &info->modelist);
		rinfo->mon1_modedb = info->monspecs.modedb;
		rinfo->mon1_dbsize = info->monspecs.modedb_len;
	}

	
	/*
	 * Finally, if we don't have panel infos we need to figure some (or
	 * we try to read it from card), we try to pick a default mode
	 * and create some panel infos. Whatever...
	 */
	if (rinfo->mon1_type != MT_CRT && !rinfo->panel_info.valid) {
		struct fb_videomode	*modedb;
		int			dbsize;
		char			modename[32];

		pr_debug("Guessing panel info...\n");
		if (rinfo->panel_info.xres == 0 || rinfo->panel_info.yres == 0) {
			u32 tmp = INREG(FP_HORZ_STRETCH) & HORZ_PANEL_SIZE;
			rinfo->panel_info.xres = ((tmp >> HORZ_PANEL_SHIFT) + 1) * 8;
			tmp = INREG(FP_VERT_STRETCH) & VERT_PANEL_SIZE;
			rinfo->panel_info.yres = (tmp >> VERT_PANEL_SHIFT) + 1;
		}
		if (rinfo->panel_info.xres == 0 || rinfo->panel_info.yres == 0) {
			printk(KERN_WARNING "radeonfb: Can't find panel size, going back to CRT\n");
			rinfo->mon1_type = MT_CRT;
			goto pickup_default;
		}
		printk(KERN_WARNING "radeonfb: Assuming panel size %dx%d\n",
		       rinfo->panel_info.xres, rinfo->panel_info.yres);
		modedb = rinfo->mon1_modedb;
		dbsize = rinfo->mon1_dbsize;
		snprintf(modename, 31, "%dx%d", rinfo->panel_info.xres, rinfo->panel_info.yres);
		if (fb_find_mode(&info->var, info, modename,
				 modedb, dbsize, NULL, 8) == 0) {
			printk(KERN_WARNING "radeonfb: Can't find mode for panel size, going back to CRT\n");
			rinfo->mon1_type = MT_CRT;
			goto pickup_default;
		}
		has_default_mode = 1;
		radeon_var_to_panel_info(rinfo, &info->var);
	}

 pickup_default:
	/*
	 * Apply passed-in mode option if any
	 */
	if (mode_option) {
		if (fb_find_mode(&info->var, info, mode_option,
				 info->monspecs.modedb,
				 info->monspecs.modedb_len, NULL, 8) != 0)
			has_default_mode = 1;
 	}

	/*
	 * Still no mode, let's pick up a default from the db
	 */
	if (!has_default_mode && info->monspecs.modedb != NULL) {
		struct fb_monspecs *specs = &info->monspecs;
		struct fb_videomode *modedb = NULL;

		/* get preferred timing */
		if (specs->misc & FB_MISC_1ST_DETAIL) {
			int i;

			for (i = 0; i < specs->modedb_len; i++) {
				if (specs->modedb[i].flag & FB_MODE_IS_FIRST) {
					modedb = &specs->modedb[i];
					break;
				}
			}
		} else {
			/* otherwise, get first mode in database */
			modedb = &specs->modedb[0];
		}
		if (modedb != NULL) {
			info->var.bits_per_pixel = 8;
			radeon_videomode_to_var(&info->var, modedb);
			has_default_mode = 1;
		}
	}
	if (1) {
		struct fb_videomode mode;
		/* Make sure that whatever mode got selected is actually in the
		 * modelist or the kernel may die
		 */
		fb_var_to_videomode(&mode, &info->var);
		fb_add_videomode(&mode, &info->modelist);
	}
}

/*
 * The code below is used to pick up a mode in check_var and
 * set_var. It should be made generic
 */

/*
 * This is used when looking for modes. We assign a "distance" value
 * to a mode in the modedb depending how "close" it is from what we
 * are looking for.
 * Currently, we don't compare that much, we could do better but
 * the current fbcon doesn't quite mind ;)
 */
static int radeon_compare_modes(const struct fb_var_screeninfo *var,
				const struct fb_videomode *mode)
{
	int distance = 0;

	distance = mode->yres - var->yres;
	distance += (mode->xres - var->xres)/2;
	return distance;
}

/*
 * This function is called by check_var, it gets the passed in mode parameter, and
 * outputs a valid mode matching the passed-in one as closely as possible.
 * We need something better ultimately. Things like fbcon basically pass us out
 * current mode with xres/yres hacked, while things like XFree will actually
 * produce a full timing that we should respect as much as possible.
 *
 * This is why I added the FB_ACTIVATE_FIND that is used by fbcon. Without this,
 * we do a simple spec match, that's all. With it, we actually look for a mode in
 * either our monitor modedb or the vesa one if none
 *
 */
int  radeon_match_mode(struct radeonfb_info *rinfo,
		       struct fb_var_screeninfo *dest,
		       const struct fb_var_screeninfo *src)
{
	const struct fb_videomode	*db = vesa_modes;
	int				i, dbsize = 34;
	int				has_rmx, native_db = 0;
	int				distance = INT_MAX;
	const struct fb_videomode	*candidate = NULL;

	/* Start with a copy of the requested mode */
	memcpy(dest, src, sizeof(struct fb_var_screeninfo));

	/* Check if we have a modedb built from EDID */
	if (rinfo->mon1_modedb) {
		db = rinfo->mon1_modedb;
		dbsize = rinfo->mon1_dbsize;
		native_db = 1;
	}

	/* Check if we have a scaler allowing any fancy mode */
	has_rmx = rinfo->mon1_type == MT_LCD || rinfo->mon1_type == MT_DFP;

	/* If we have a scaler and are passed FB_ACTIVATE_TEST or
	 * FB_ACTIVATE_NOW, just do basic checking and return if the
	 * mode match
	 */
	if ((src->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_TEST ||
	    (src->activate & FB_ACTIVATE_MASK) == FB_ACTIVATE_NOW) {
		/* We don't have an RMX, validate timings. If we don't have
	 	 * monspecs, we should be paranoid and not let use go above
		 * 640x480-60, but I assume userland knows what it's doing here
		 * (though I may be proven wrong...)
		 */
		if (has_rmx == 0 && rinfo->mon1_modedb)
			if (fb_validate_mode((struct fb_var_screeninfo *)src, rinfo->info))
				return -EINVAL;
		return 0;
	}

	/* Now look for a mode in the database */
	while (db) {
		for (i = 0; i < dbsize; i++) {
			int d;

			if (db[i].yres < src->yres)
				continue;	
			if (db[i].xres < src->xres)
				continue;
			d = radeon_compare_modes(src, &db[i]);
			/* If the new mode is at least as good as the previous one,
			 * then it's our new candidate
			 */
			if (d < distance) {
				candidate = &db[i];
				distance = d;
			}
		}
		db = NULL;
		/* If we have a scaler, we allow any mode from the database */
		if (native_db && has_rmx) {
			db = vesa_modes;
			dbsize = 34;
			native_db = 0;
		}
	}

	/* If we have found a match, return it */
	if (candidate != NULL) {
		radeon_videomode_to_var(dest, candidate);
		return 0;
	}

	/* If we haven't and don't have a scaler, fail */
	if (!has_rmx)
		return -EINVAL;

	return 0;
}
