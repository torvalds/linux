#include "rockchip-hdmi.h"

static const struct hdmi_video_timing hdmi_mode[] = {
/*		name			refresh	xres	yres	pixclock	h_bp	h_fp	v_bp	v_fp	h_pw	v_pw			polariry			PorI	flag		vic		2ndvic		pixelrepeat	interface */

	{ {	"720x480i@60Hz",	60,	720,    480,    27000000,	57,     19,	15,     4,	62,     3,			0,				1,      0	},	6,	HDMI_720X480I_60HZ_16_9,	2,	OUT_P888},
	{ {	"720x576i@50Hz",	50,	720,	576,	27000000,	69,	12,	19,	2,	63,	3,			0,				1,	0	},	21,	HDMI_720X576I_50HZ_16_9,	2,	OUT_P888},
	{ {	"720x480p@60Hz",	60,	720,	480,	27000000,	60,	16,	30,	9,	62,	6,			0,				0,	0	},	2,	HDMI_720X480P_60HZ_16_9,	1,	OUT_P888},
	{ {	"720x576p@50Hz",	50,	720,	576,	27000000,	68,	12,	39,	5,	64,	5,			0,				0,	0	},	17,	HDMI_720X576P_50HZ_16_9,	1,	OUT_P888},
	{ {	"1280x720p@24Hz",	24,	1280,	720,	59400000,	220,	1760,	20,	5,	40,	5,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	0,	0	},	60,	HDMI_1280X720P_24HZ_4_3,	1,	OUT_P888},
	{ {	"1280x720p@25Hz",	25,	1280,	720,	74250000,	220,	2420,	20,	5,	40,	5,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	0,	0	},	61,	HDMI_1280X720P_25HZ_4_3,	1,	OUT_P888},
	{ {	"1280x720p@30Hz",	30,	1280,	720,	74250000,	220,	1760,	20,	5,	40,	5,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	0,	0	},	62,	HDMI_1280X720P_30HZ_4_3,	1,	OUT_P888},
	{ {	"1280x720p@50Hz",	50,	1280,	720,	74250000,	220,	440,	20,	5,	40,	5,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	0,	0	},	19,	HDMI_1280X720P_50HZ_4_3,	1,	OUT_P888},
	{ {	"1280x720p@60Hz",	60,	1280,	720,	74250000,	220,	110,	20,	5,	40,	5,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	0,	0	},	4,	HDMI_1280X720P_60HZ_4_3,	1,	OUT_P888},
	{ {	"1920x1080i@50Hz",	50,	1920,	1080,	74250000,	148,	528,	15,	2,	44,	5,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	1,	0	},	20,	0,				1,	OUT_P888},
	{ {	"1920x1080i@60Hz",	60,	1920,	1080,	74250000,	148,	88,	15,	2,	44,	5,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	1,	0	},	5,	0,				1,	OUT_P888},
	{ {	"1920x1080p@24Hz",	24,	1920,	1080,	74250000,	148,	638,	36,	4,	44,	5,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	0,	0	},	32,	HDMI_1920X1080P_24HZ_4_3,	1,	OUT_P888},
	{ {	"1920x1080p@25Hz",	25,	1920,	1080,	74250000,	148,	528,	36,	4,	44,	5,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	0,	0	},	33,	HDMI_1920X1080P_25HZ_4_3,	1,	OUT_P888},
	{ {	"1920x1080p@30Hz",	30,	1920,	1080,	74250000,	148,	88,	36,	4,	44,	5,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	0,	0	},	34,	HDMI_1920X1080P_30HZ_4_3,	1,	OUT_P888},
	{ {	"1920x1080p@50Hz",	50,	1920,	1080,	148500000,	148,	528,	36,	4,	44,	5,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	0,	0	},	31,	HDMI_1920X1080P_50HZ_4_3,	1,	OUT_P888},
	{ {	"1920x1080p@60Hz",	60,	1920,	1080,	148500000,	148,	88,	36,	4,	44,	5,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	0,	0	},	16,	HDMI_1920X1080P_60HZ_4_3,	1,	OUT_P888},
	{ {	"3840x2160p@24Hz",	24,	3840,	2160,	297000000,	296,	1276,	72,	8,	88,	10,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	0,	0	},	93,	HDMI_3840X2160P_24HZ_4_3,	1,	OUT_P888},
	{ {	"3840x2160p@25Hz",	25,	3840,	2160,	297000000,	296,	1056,	72,	8,	88,	10,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	0,	0	},	94,	HDMI_3840X2160P_25HZ_4_3,	1,	OUT_P888},
	{ {	"3840x2160p@30Hz",	30,	3840,	2160,	297000000,	296,	176,	72,	8,	88,	10,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	0,	0	},	95,	HDMI_3840X2160P_30HZ_4_3,	1,	OUT_P888},
	{ {	"4096x2160p@24Hz",	24,	4096,	2160,	297000000,	296,	1020,	72,	8,	88,	10,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	0,	0	},	98,	0,				1,	OUT_P888},
	{ {	"4096x2160p@25Hz",	25,	4096,	2160,	297000000,	128,	968,	72,	8,	88,	10,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	0,	0	},	99,	0,				1,	OUT_P888},
	{ {	"4096x2160p@30Hz",	30,	4096,	2160,	297000000,	128,	88,	72,	8,	88,	10,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	0,	0	},	100,	0,				1,	OUT_P888},
	{ {	"3840x2160p@50Hz",	50,	3840,	2160,	594000000,	296,	1056,	72,	8,	88,	10,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	0,	0	},	96,	HDMI_3840X2160P_50HZ_4_3,	1,	OUT_P888},
	{ {	"3840x2160p@60Hz",	60,	3840,	2160,	594000000,	296,	176,	72,	8,	88,	10,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	0,	0	},	97,	HDMI_3840X2160P_60HZ_4_3,	1,	OUT_P888},
	{ {	"4096x2160p@50Hz",	50,	4096,	2160,	594000000,	128,	968,	72,	8,	88,	10,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	0,	0	},	101,	0,				1,	OUT_P888},
	{ {	"4096x2160p@60Hz",	60,	4096,	2160,	594000000,	128,	88,	72,	8,	88,	10,	FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,	0,	0	},	102,	0,				1,	OUT_P888},
};

static int hdmi_set_info(struct rk_screen *screen, struct hdmi *hdmi)
{
	int i;
	struct fb_videomode *mode;

	if (screen == NULL || hdmi == NULL)
		return HDMI_ERROR_FALSE;

	if (hdmi->vic == 0)
		hdmi->vic = hdmi->property->defaultmode;

	for (i = 0; i < ARRAY_SIZE(hdmi_mode); i++) {
		if (hdmi_mode[i].vic == (hdmi->vic & HDMI_VIC_MASK) ||
		    hdmi_mode[i].vic_2nd == (hdmi->vic & HDMI_VIC_MASK))
			break;
	}
	if (i == ARRAY_SIZE(hdmi_mode))
		return HDMI_ERROR_FALSE;

	memset(screen, 0, sizeof(struct rk_screen));

	/* screen type & face */
	screen->type = SCREEN_HDMI;
	if (hdmi->colormode_input == HDMI_COLOR_RGB_0_255)
		screen->color_mode = COLOR_RGB;
	else
		screen->color_mode = COLOR_YCBCR;
	if (hdmi->vic & HDMI_VIDEO_YUV420)
		screen->face = OUT_YUV_420;
	else
		screen->face = hdmi_mode[i].interface;
	screen->pixelrepeat = hdmi_mode[i].pixelrepeat - 1;
	mode = (struct fb_videomode *)&(hdmi_mode[i].mode);

	screen->mode = *mode;

	/* Pin polarity */
	#ifdef CONFIG_HDMI_RK616
	screen->pin_hsync = 0;
	screen->pin_vsync = 0;
	#else
	if (FB_SYNC_HOR_HIGH_ACT & mode->sync)
		screen->pin_hsync = 1;
	else
		screen->pin_hsync = 0;
	if (FB_SYNC_VERT_HIGH_ACT & mode->sync)
		screen->pin_vsync = 1;
	else
		screen->pin_vsync = 0;
	#endif
	screen->pin_den = 0;
	screen->pin_dclk = 1;

	/* Swap rule */
	if (hdmi->soctype == HDMI_SOC_RK3368 &&
	    screen->color_mode == COLOR_YCBCR &&
	    screen->face == OUT_P888)
		screen->swap_rb = 1;
	else
		screen->swap_rb = 0;
	screen->swap_rg = 0;
	screen->swap_gb = 0;
	screen->swap_delta = 0;
	screen->swap_dumy = 0;

	/* Operation function*/
	screen->init = NULL;
	screen->standby = NULL;

	screen->overscan.left = hdmi->xscale;
	screen->overscan.top = hdmi->yscale;
	screen->overscan.right = hdmi->xscale;
	screen->overscan.bottom = hdmi->yscale;
	return 0;
}

/**
 * hdmi_find_best_mode: find the video mode nearest to input vic
 * @hdmi:
 * @vic: input vic
 *
 * NOTES:
 * If vic is zero, return the high resolution video mode vic.
 */
int hdmi_find_best_mode(struct hdmi *hdmi, int vic)
{
	struct list_head *pos, *head = &hdmi->edid.modelist;
	struct display_modelist *modelist;
	int found = 0;
/*	pr_info("%s vic %d\n", __FUNCTION__, vic); */
	if (vic) {
		list_for_each(pos, head) {
			modelist =
				list_entry(pos,
					   struct display_modelist, list);
			if (modelist->vic == vic) {
				found = 1;
				break;
			}
		}
	}
	if ((vic == 0 || found == 0) && head->next != head) {
		/* If parse edid error, we select default mode; */
		if (hdmi->edid.specs == NULL ||
		    hdmi->edid.specs->modedb_len == 0)
			return hdmi->property->defaultmode;
			/*modelist = list_entry(head->prev,
					struct display_modelist, list);*/
		else
			modelist = list_entry(head->next,
					      struct display_modelist, list);
	}

	if (modelist != NULL)
		return modelist->vic;
	else
		return 0;
}
/**
 * hdmi_set_lcdc: switch lcdc mode to required video mode
 * @hdmi:
 *
 * NOTES:
 *
 */
int hdmi_set_lcdc(struct hdmi *hdmi)
{
	int rc = 0;
	struct rk_screen screen;

	if (hdmi->autoset)
		hdmi->vic = hdmi_find_best_mode(hdmi, 0);
	else
		hdmi->vic = hdmi_find_best_mode(hdmi, hdmi->vic);

	if (hdmi->vic == 0)
		hdmi->vic = hdmi->property->defaultmode;

	rc = hdmi_set_info(&screen, hdmi);

	if (rc == 0) {
		rk_fb_switch_screen(&screen, 1, hdmi->lcdc->id);
/*		if (rk_fb_get_display_policy() != DISPLAY_POLICY_BOX)
			rk_fb_disp_scale(hdmi->xscale,
					 hdmi->yscale,
					 hdmi->lcdc->id);
*/	}
	return rc;
}

/**
 * hdmi_videomode_compare - compare 2 videomodes
 * @mode1: first videomode
 * @mode2: second videomode
 *
 * RETURNS:
 * 1 if mode1 > mode2, 0 if mode1 = mode2, -1 mode1 < mode2
 */
static int hdmi_videomode_compare(const struct fb_videomode *mode1,
				  const struct fb_videomode *mode2)
{
	if (mode1->xres > mode2->xres)
		return 1;

	if (mode1->xres == mode2->xres) {
		if (mode1->yres > mode2->yres)
			return 1;
		if (mode1->yres == mode2->yres) {
			if (mode1->vmode < mode2->vmode)
				return 1;
			if (mode1->pixclock > mode2->pixclock)
				return 1;
			if (mode1->pixclock == mode2->pixclock) {
				if (mode1->refresh > mode2->refresh)
					return 1;
				if (mode1->refresh == mode2->refresh) {
					if (mode2->flag > mode1->flag)
						return 1;
					if (mode2->flag < mode1->flag)
						return -1;
					if (mode2->vmode > mode1->vmode)
						return 1;
					if (mode2->vmode == mode1->vmode)
						return 0;
				}
			}
		}
	}
	return -1;
}

/**
 * hdmi_add_vic - add entry to modelist according vic
 * @vic: vic to be added
 * @head: struct list_head of modelist
 *
 * NOTES:
 * Will only add unmatched mode entries
 */
int hdmi_add_vic(int vic, struct list_head *head)
{
	struct list_head *pos;
	struct display_modelist *modelist;
	int found = 0, v;

/*	DBG("%s vic %d", __FUNCTION__, vic); */
	if (vic == 0)
		return -1;

	list_for_each(pos, head) {
		modelist = list_entry(pos, struct display_modelist, list);
		v = modelist->vic;
		if (v == vic) {
			found = 1;
			break;
		}
	}
	if (!found) {
		modelist = kmalloc(sizeof(*modelist),
				   GFP_KERNEL);

		if (!modelist)
			return -ENOMEM;
		memset(modelist, 0, sizeof(struct display_modelist));
		modelist->vic = vic;
		list_add_tail(&modelist->list, head);
	}
	return 0;
}

/**
 * hdmi_add_videomode: adds videomode entry to modelist
 * @mode: videomode to be added
 * @head: struct list_head of modelist
 *
 * NOTES:
 * Will only add unmatched mode entries
 */
static int hdmi_add_videomode(const struct fb_videomode *mode,
			      struct list_head *head)
{
	struct list_head *pos;
	struct display_modelist *modelist, *modelist_new;
	struct fb_videomode *m;
	int i, found = 0;

	for (i = 0; i < ARRAY_SIZE(hdmi_mode); i++) {
		m = (struct fb_videomode *)&(hdmi_mode[i].mode);
		if (fb_mode_is_equal(m, mode)) {
			found = 1;
			break;
		}
	}

	if (found) {
		list_for_each(pos, head) {
			modelist = list_entry(pos,
					      struct display_modelist, list);
			m = &modelist->mode;
			if (fb_mode_is_equal(m, mode))
				return 0;
			else if (hdmi_videomode_compare(m, mode) == -1)
				break;
		}

		modelist_new = kmalloc(sizeof(*modelist_new), GFP_KERNEL);
		if (!modelist_new)
			return -ENOMEM;
		memset(modelist_new, 0, sizeof(struct display_modelist));
		modelist_new->mode = hdmi_mode[i].mode;
		modelist_new->vic = hdmi_mode[i].vic;
		list_add_tail(&modelist_new->list, pos);
	}

	return 0;
}

/**
 * hdmi_sort_modelist: sort modelist of edid
 * @edid: edid to be sort
 */
static void hdmi_sort_modelist(struct hdmi_edid *edid, int feature)
{
	struct list_head *pos, *pos_new;
	struct list_head head_new, *head = &edid->modelist;
	struct display_modelist *modelist, *modelist_new, *modelist_n;
	struct fb_videomode *m, *m_new;
	int i, compare, vic;

	INIT_LIST_HEAD(&head_new);
	list_for_each(pos, head) {
		modelist = list_entry(pos, struct display_modelist, list);
		/*pr_info("%s vic %d\n", __function__, modelist->vic);*/
		for (i = 0; i < ARRAY_SIZE(hdmi_mode); i++) {
			vic = modelist->vic & HDMI_VIC_MASK;
			if (vic == hdmi_mode[i].vic ||
			    vic == hdmi_mode[i].vic_2nd) {
				if ((feature & SUPPORT_4K) == 0 &&
				    hdmi_mode[i].mode.xres >= 3840)
					continue;
				if ((feature & SUPPORT_4K_4096) == 0 &&
				    hdmi_mode[i].mode.xres == 4096)
					continue;
				if ((feature & SUPPORT_TMDS_600M) == 0 &&
				    !(modelist->vic & HDMI_VIDEO_YUV420) &&
				    hdmi_mode[i].mode.pixclock > 340000000)
					continue;
				if ((modelist->vic & HDMI_VIDEO_YUV420) &&
				    (feature & SUPPORT_YUV420) == 0)
					continue;
				if ((feature & SUPPORT_1080I) == 0 &&
				    hdmi_mode[i].mode.xres == 1920 &&
				    hdmi_mode[i].mode.vmode ==
				    FB_VMODE_INTERLACED)
					continue;
				if ((feature & SUPPORT_480I_576I) == 0 &&
				    hdmi_mode[i].mode.xres == 720 &&
				    hdmi_mode[i].mode.vmode ==
				    FB_VMODE_INTERLACED)
					continue;
				vic = modelist->vic;
				modelist->vic = hdmi_mode[i].vic;
				modelist->mode = hdmi_mode[i].mode;
				if (vic & HDMI_VIDEO_YUV420) {
					modelist->vic |= HDMI_VIDEO_YUV420;
					modelist->mode.flag = 1;
				}
				compare = 1;
				m = (struct fb_videomode *)&(modelist->mode);
				list_for_each(pos_new, &head_new) {
					modelist_new =
					list_entry(pos_new,
						   struct display_modelist,
						   list);
					m_new = &modelist_new->mode;
					compare =
					hdmi_videomode_compare(m, m_new);
					if (compare != -1)
						break;
				}
				if (compare != 0) {
					modelist_n =
						kmalloc(sizeof(*modelist_n),
							GFP_KERNEL);
					if (!modelist_n)
						return;
					*modelist_n = *modelist;
					list_add_tail(&modelist_n->list,
						      pos_new);
				}
				break;
			}
		}
	}
	fb_destroy_modelist(head);
	if (head_new.next == &head_new) {
		pr_info("There is no available video mode in EDID.\n");
		INIT_LIST_HEAD(&edid->modelist);
	} else {
		edid->modelist = head_new;
		edid->modelist.prev->next = &edid->modelist;
		edid->modelist.next->prev = &edid->modelist;
	}
}

/**
 * hdmi_ouputmode_select - select hdmi transmitter output mode: hdmi or dvi?
 * @hdmi: handle of hdmi
 * @edid_ok: get EDID data success or not, HDMI_ERROR_SUCESS means success.
 */
int hdmi_ouputmode_select(struct hdmi *hdmi, int edid_ok)
{
	struct list_head *head = &hdmi->edid.modelist;
	struct fb_monspecs *specs = hdmi->edid.specs;
	struct fb_videomode *modedb = NULL, *mode = NULL;
	int i, pixclock, feature = hdmi->property->feature;

	if (edid_ok != HDMI_ERROR_SUCESS) {
		dev_err(hdmi->dev, "warning: EDID error, assume sink as HDMI !!!!");
		hdmi->edid.status = -1;
		hdmi->edid.sink_hdmi = 1;
		hdmi->edid.baseaudio_support = 1;
		hdmi->edid.ycbcr444 = 0;
		hdmi->edid.ycbcr422 = 0;
	}

	if (head->next == head) {
		dev_info(hdmi->dev,
			 "warning: no CEA video mode parsed from EDID !!!!\n");
		/* If EDID get error, list all system supported mode.
		   If output mode is set to DVI and EDID is ok, check
		   the output timing.
		*/
		if (hdmi->edid.sink_hdmi == 0 && specs && specs->modedb_len) {
			/* Get max resolution timing */
			modedb = &specs->modedb[0];
			for (i = 0; i < specs->modedb_len; i++) {
				if (specs->modedb[i].xres > modedb->xres)
					modedb = &specs->modedb[i];
				else if (specs->modedb[i].xres ==
					 modedb->xres &&
					 specs->modedb[i].yres > modedb->yres)
					modedb = &specs->modedb[i];
			}
			/* For some monitor, the max pixclock read from EDID
			   is smaller than the clock of max resolution mode
			   supported. We fix it. */
			pixclock = PICOS2KHZ(modedb->pixclock);
			pixclock /= 250;
			pixclock *= 250;
			pixclock *= 1000;
			if (pixclock == 148250000)
				pixclock = 148500000;
			if (pixclock > specs->dclkmax)
				specs->dclkmax = pixclock;
		}

		for (i = 0; i < ARRAY_SIZE(hdmi_mode); i++) {
			mode = (struct fb_videomode *)&(hdmi_mode[i].mode);
			if (modedb) {
				if ((mode->pixclock < specs->dclkmin) ||
				    (mode->pixclock > specs->dclkmax) ||
				    (mode->refresh < specs->vfmin) ||
				    (mode->refresh > specs->vfmax) ||
				    (mode->xres > modedb->xres) ||
				    (mode->yres > modedb->yres))
					continue;
			} else {
				/* If there is no valid information in EDID,
				   just list common hdmi foramt. */
				if (mode->xres > 3840 ||
				    mode->refresh < 50 ||
				    mode->vmode == FB_VMODE_INTERLACED)
					continue;
			}
			if ((feature & SUPPORT_TMDS_600M) == 0 &&
			    mode->pixclock > 340000000)
				continue;
			if ((feature & SUPPORT_4K) == 0 &&
			    mode->xres >= 3840)
				continue;
			if ((feature & SUPPORT_4K_4096) == 0 &&
			    mode->xres == 4096)
				continue;
			if ((feature & SUPPORT_1080I) == 0 &&
			    mode->xres == 1920 &&
			    mode->vmode == FB_VMODE_INTERLACED)
				continue;
			if ((feature & SUPPORT_480I_576I) == 0 &&
			    mode->xres == 720 &&
			    mode->vmode == FB_VMODE_INTERLACED)
				continue;
			hdmi_add_videomode(mode, head);
		}
	} else {
		/* There are some video mode is not defined in EDID extend
		   block, so we need to check first block data.*/
		if (specs && specs->modedb_len) {
			for (i = 0; i < specs->modedb_len; i++) {
				modedb = &specs->modedb[0];
				pixclock = hdmi_videomode_to_vic(modedb);
				if (pixclock)
					hdmi_add_vic(pixclock, head);
			}
		}
		hdmi_sort_modelist(&hdmi->edid, hdmi->property->feature);
	}

	return HDMI_ERROR_SUCESS;
}

/**
 * hdmi_videomode_to_vic: transverse video mode to vic
 * @vmode: videomode to transverse
 *
 */
int hdmi_videomode_to_vic(struct fb_videomode *vmode)
{
	struct fb_videomode *mode;
	unsigned char vic = 0;
	int i = 0;

	for (i = 0; i < ARRAY_SIZE(hdmi_mode); i++) {
		mode = (struct fb_videomode *)&(hdmi_mode[i].mode);
		if (vmode->vmode == mode->vmode &&
		    vmode->refresh == mode->refresh &&
		    vmode->xres == mode->xres &&
		    vmode->yres == mode->yres &&
		    vmode->left_margin == mode->left_margin &&
		    vmode->right_margin == mode->right_margin &&
		    vmode->upper_margin == mode->upper_margin &&
		    vmode->lower_margin == mode->lower_margin &&
		    vmode->hsync_len == mode->hsync_len &&
		    vmode->vsync_len == mode->vsync_len) {
			vic = hdmi_mode[i].vic;
			break;
		}
	}
	return vic;
}

/**
 * hdmi_vic2timing: transverse vic mode to video timing
 * @vmode: vic to transverse
 *
 */
const struct hdmi_video_timing *hdmi_vic2timing(int vic)
{
	int i;

	if (vic == 0)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(hdmi_mode); i++) {
		if (hdmi_mode[i].vic == vic || hdmi_mode[i].vic_2nd == vic)
			return &(hdmi_mode[i]);
	}
	return NULL;
}

/**
 * hdmi_vic_to_videomode: transverse vic mode to video mode
 * @vmode: vic to transverse
 *
 */
const struct fb_videomode *hdmi_vic_to_videomode(int vic)
{
	int i;

	if (vic == 0)
		return NULL;

	for (i = 0; i < ARRAY_SIZE(hdmi_mode); i++) {
		if (hdmi_mode[i].vic == (vic & HDMI_VIC_MASK) ||
		    hdmi_mode[i].vic_2nd == (vic & HDMI_VIC_MASK))
			return &hdmi_mode[i].mode;
	}
	return NULL;
}

/**
 * hdmi_init_modelist: initial hdmi mode list
 * @hdmi:
 *
 * NOTES:
 *
 */
void hdmi_init_modelist(struct hdmi *hdmi)
{
	int i, feature;
	struct list_head *head = &hdmi->edid.modelist;

	feature = hdmi->property->feature;
	INIT_LIST_HEAD(&hdmi->edid.modelist);
	for (i = 0; i < ARRAY_SIZE(hdmi_mode); i++) {
		if ((feature & SUPPORT_TMDS_600M) == 0 &&
		    hdmi_mode[i].mode.pixclock > 340000000)
			continue;
		if ((feature & SUPPORT_4K) == 0 &&
		    hdmi_mode[i].mode.xres >= 3840)
			continue;
		if ((feature & SUPPORT_4K_4096) == 0 &&
		    hdmi_mode[i].mode.xres == 4096)
			continue;
		if ((feature & SUPPORT_1080I) == 0 &&
		    hdmi_mode[i].mode.xres == 1920 &&
		    hdmi_mode[i].mode.vmode == FB_VMODE_INTERLACED)
			continue;
		if ((feature & SUPPORT_480I_576I) == 0 &&
		    hdmi_mode[i].mode.xres == 720 &&
		    hdmi_mode[i].mode.vmode == FB_VMODE_INTERLACED)
			continue;
		hdmi_add_videomode(&(hdmi_mode[i].mode), head);
	}
}
