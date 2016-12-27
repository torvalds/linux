#include <linux/ctype.h>
#include <linux/string.h>
#include <linux/display-sys.h>
#include <linux/interrupt.h>
#include <linux/moduleparam.h>
#include "rockchip-hdmi.h"

int hdmi_dbg_level;
module_param(hdmi_dbg_level, int, S_IRUGO | S_IWUSR);

static int hdmi_get_enable(struct rk_display_device *device)
{
	struct hdmi *hdmi = device->priv_data;
	int enable;

	enable = hdmi->enable;
	return enable;
}

static int hdmi_set_enable(struct rk_display_device *device, int enable)
{
	struct hdmi *hdmi = device->priv_data;

	if (enable == 0)
		hdmi_submit_work(hdmi, HDMI_DISABLE_CTL, 0, 0);
	else
		hdmi_submit_work(hdmi, HDMI_ENABLE_CTL, 0, 0);
	return 0;
}

static int hdmi_get_status(struct rk_display_device *device)
{
	struct hdmi *hdmi = device->priv_data;

	if (hdmi->hotplug == HDMI_HPD_ACTIVATED)
		return 1;
	else
		return 0;
}

static int hdmi_get_modelist(struct rk_display_device *device,
			     struct list_head **modelist)
{
	struct hdmi *hdmi = device->priv_data;

	*modelist = &hdmi->edid.modelist;
	return 0;
}

static int hdmi_set_mode(struct rk_display_device *device,
			 struct fb_videomode *mode)
{
	struct hdmi *hdmi = device->priv_data;
	struct display_modelist *display_modelist =
			container_of(mode, struct display_modelist, mode);
	int vic = 0;

	if (!mode) {
		hdmi->autoset = 1;
		vic = hdmi_find_best_mode(hdmi, 0);
	} else {
		hdmi->autoset = 0;
		vic = display_modelist->vic;
	}

	if (vic && hdmi->vic != vic) {
		hdmi->vic = vic;
		if (hdmi->hotplug == HDMI_HPD_ACTIVATED)
			hdmi_submit_work(hdmi, HDMI_SET_VIDEO, 0, 0);
	}
	return 0;
}

static int hdmi_get_mode(struct rk_display_device *device,
			 struct fb_videomode *mode)
{
	struct hdmi *hdmi = device->priv_data;
	struct fb_videomode *vmode;

	if (!mode)
		return -1;

	if (hdmi->vic) {
		vmode = (struct fb_videomode *)
			hdmi_vic_to_videomode(hdmi->vic);
		if (unlikely(!vmode))
			return -1;
		*mode = *vmode;
		if (hdmi->vic & HDMI_VIDEO_YUV420)
			mode->flag = 1;
	} else {
		memset(mode, 0, sizeof(struct fb_videomode));
	}
	return 0;
}

static int hdmi_set_3dmode(struct rk_display_device *device, int mode)
{
	struct hdmi *hdmi = device->priv_data;
	struct list_head *modelist, *pos;
	struct display_modelist *display_modelist = NULL;

	if (!hdmi)
		return -1;

	modelist = &hdmi->edid.modelist;
	list_for_each(pos, modelist) {
		display_modelist =
			list_entry(pos, struct display_modelist, list);
		if (hdmi->vic != display_modelist->vic)
			display_modelist = NULL;
		else
			break;
	}
	if (!display_modelist)
		return -1;

	if ((mode != HDMI_3D_NONE) &&
	    ((display_modelist->format_3d & (1 << mode)) == 0))
		pr_warn("warning: sink not support input 3d mode %d", mode);

	if (hdmi->mode_3d != mode) {
		hdmi->mode_3d = mode;
		if (hdmi->hotplug == HDMI_HPD_ACTIVATED)
			hdmi_submit_work(hdmi, HDMI_SET_3D, 0, 0);
	}
	return 0;
}

static int hdmi_get_3dmode(struct rk_display_device *device)
{
	struct hdmi *hdmi = device->priv_data;

	if (!hdmi)
		return -1;
	else
		return hdmi->mode_3d;
}

/* CEA 861-E: Audio Coding Type
 * sync width enum hdmi_audio_type
 */
static const char * const audioformatstr[] = {
	"",
	"LPCM",		/*HDMI_AUDIO_LPCM = 1,*/
	"AC3",		/*HDMI_AUDIO_AC3,*/
	"MPEG1",	/*HDMI_AUDIO_MPEG1,*/
	"MP3",		/*HDMI_AUDIO_MP3,*/
	"MPEG2",	/*HDMI_AUDIO_MPEG2,*/
	"AAC-LC",	/*HDMI_AUDIO_AAC_LC, AAC*/
	"DTS",		/*HDMI_AUDIO_DTS,*/
	"ATARC",	/*HDMI_AUDIO_ATARC,*/
	"DSD",		/*HDMI_AUDIO_DSD, One bit Audio */
	"E-AC3",	/*HDMI_AUDIO_E_AC3,*/
	"DTS-HD",	/*HDMI_AUDIO_DTS_HD,*/
	"MLP",		/*HDMI_AUDIO_MLP,*/
	"DST",		/*HDMI_AUDIO_DST,*/
	"WMA-PRO",	/*HDMI_AUDIO_WMA_PRO*/
};

static int hdmi_get_edidaudioinfo(struct rk_display_device *device,
				  char *audioinfo, int len)
{
	struct hdmi *hdmi = device->priv_data;
	int i = 0, size = 0;
	struct hdmi_audio *audio;

	if (!hdmi)
		return -1;

	memset(audioinfo, 0x00, len);
	/*printk("hdmi:edid: audio_num: %d\n", hdmi->edid.audio_num);*/
	for (i = 0; i < hdmi->edid.audio_num; i++) {
		audio = &hdmi->edid.audio[i];
		if (audio->type < 1 || audio->type > HDMI_AUDIO_WMA_PRO) {
			pr_info("audio type: unsupported.");
			continue;
		}
		size = strlen(audioformatstr[audio->type]);
		memcpy(audioinfo, audioformatstr[audio->type], size);
		audioinfo[size] = ',';
		audioinfo += (size + 1);
	}
	return 0;
}

static int hdmi_get_color(struct rk_display_device *device, char *buf)
{
	struct hdmi *hdmi = device->priv_data;
	int i, mode;

	mode = (1 << HDMI_COLOR_RGB_0_255);
	if (hdmi->edid.sink_hdmi) {
		mode |= (1 << HDMI_COLOR_RGB_16_235);
		if (hdmi->edid.ycbcr422)
			mode |= (1 << HDMI_COLOR_YCBCR422);
		if (hdmi->edid.ycbcr444)
			mode |= (1 << HDMI_COLOR_YCBCR444);
	}
	i = snprintf(buf, PAGE_SIZE,
		     "Supported Color Mode: %d\n", mode);
	i += snprintf(buf + i, PAGE_SIZE - i,
		      "Current Color Mode: %d\n", hdmi->video.color_output);

	mode = (1 << 1); /* 24 bit*/
	if (hdmi->edid.deepcolor & HDMI_DEEP_COLOR_30BITS &&
	    hdmi->property->feature & SUPPORT_DEEP_10BIT)
		mode |= (1 << HDMI_DEEP_COLOR_30BITS);
	if (hdmi->edid.deepcolor & HDMI_DEEP_COLOR_36BITS &&
	    hdmi->property->feature & SUPPORT_DEEP_12BIT)
		mode |= (1 << HDMI_DEEP_COLOR_36BITS);
	if (hdmi->edid.deepcolor & HDMI_DEEP_COLOR_48BITS &&
	    hdmi->property->feature & SUPPORT_DEEP_16BIT)
		mode |= (1 << HDMI_DEEP_COLOR_48BITS);
	i += snprintf(buf + i, PAGE_SIZE - i,
		      "Supported Color Depth: %d\n", mode);
	i += snprintf(buf + i, PAGE_SIZE - i,
		      "Current Color Depth: %d\n",
		      hdmi->video.color_output_depth);
	i += snprintf(buf + i, PAGE_SIZE - i,
		      "Supported Colorimetry: %d\n", hdmi->edid.colorimetry);
	i += snprintf(buf + i, PAGE_SIZE - i,
		      "Current Colorimetry: %d\n", hdmi->colorimetry);
	i += snprintf(buf + i, PAGE_SIZE - i,
		      "Supported EOTF: 0x%x\n", hdmi->edid.hdr.hdrinfo.eotf);
	i += snprintf(buf + i, PAGE_SIZE - i,
		      "Current EOTF: 0x%x\n", hdmi->eotf);
	i += snprintf(buf + i, PAGE_SIZE - i,
		      "HDR MeteData: %d %d %d %d %d %d %d %d %d %d %d %d\n",
		      hdmi->hdr.prim_x0, hdmi->hdr.prim_y0,
		      hdmi->hdr.prim_x1, hdmi->hdr.prim_y1,
		      hdmi->hdr.prim_x2, hdmi->hdr.prim_y2,
		      hdmi->hdr.white_px, hdmi->hdr.white_py,
		      hdmi->hdr.max_dml, hdmi->hdr.min_dml,
		      hdmi->hdr.max_cll, hdmi->hdr.max_fall);
	return i;
}

static int hdmi_set_color(struct rk_display_device *device,
			  const char *buf, int len)
{
	struct hdmi *hdmi = device->priv_data;
	int value;

	if (!strncmp(buf, "mode", 4)) {
		if (sscanf(buf, "mode=%d", &value) == -1)
			return -1;
		pr_debug("current mode is %d input mode is %x\n",
			 hdmi->colormode, value);
		if (hdmi->colormode != (value & 0xff))
			hdmi->colormode = value & 0xff;
		if (hdmi->colordepth != ((value >> 8) & 0xff)) {
			pr_debug("current depth is %d input mode is %d\n",
				 hdmi->colordepth, ((value >> 8) & 0xff));
			hdmi->colordepth = ((value >> 8) & 0xff);
		}
	} else if (!strncmp(buf, "depth", 5)) {
		if (sscanf(buf, "depth=%d", &value) == -1)
			return -1;
		pr_debug("current depth is %d input mode is %d\n",
			 hdmi->colordepth, value);
		if (hdmi->colordepth != value)
			hdmi->colordepth = value;
		else
			return 0;
	} else if (!strncmp(buf, "colorimetry", 11)) {
		if (sscanf(buf, "colorimetry=%d", &value) == -1)
			return -1;
		pr_debug("current colorimetry is %d input colorimetry is %d\n",
			 hdmi->colorimetry, value);
		if (hdmi->colorimetry != value)
			hdmi->colorimetry = value;
		else
			return 0;
	} else if (!strncmp(buf, "hdr", 3)) {
		if (sscanf(buf, "hdr=%d", &value) == -1)
			return -1;
		pr_info("current hdr eotf is %d input hdr eotf is %d\n",
			hdmi->eotf, value);
		if (hdmi->eotf != value &&
		    (value & hdmi->edid.hdr.hdrinfo.eotf ||
		     value == 0)) {
			hdmi->eotf = value;
			if (hdmi->hotplug == HDMI_HPD_ACTIVATED)
				hdmi_submit_work(hdmi, HDMI_SET_HDR, 0, 0);
		}
		return 0;
	} else if (!strncmp(buf, "hdrmdata", 8)) {
		value = sscanf(buf,
			       "hdrmdata=%u %u %u %u %u %u %u %u %u %u %u %u",
			       &hdmi->hdr.prim_x0, &hdmi->hdr.prim_y0,
			       &hdmi->hdr.prim_x1, &hdmi->hdr.prim_y1,
			       &hdmi->hdr.prim_x2, &hdmi->hdr.prim_y2,
			       &hdmi->hdr.white_px, &hdmi->hdr.white_py,
			       &hdmi->hdr.max_dml, &hdmi->hdr.min_dml,
			       &hdmi->hdr.max_cll, &hdmi->hdr.max_fall);
		if (value == -1)
			return -1;
		else
			return 0;
	} else {
		pr_err("%s unknown event\n", __func__);
		return -1;
	}
	if (hdmi->hotplug == HDMI_HPD_ACTIVATED)
		hdmi_submit_work(hdmi, HDMI_SET_COLOR, 0, 0);
	return 0;
}

static int hdmi_set_scale(struct rk_display_device *device, int direction,
			  int value)
{
	struct hdmi *hdmi = device->priv_data;

	if (!hdmi || value < 0 || value > 100)
		return -1;

	if (!hdmi->hotplug)
		return 0;

	if (direction == DISPLAY_SCALE_X)
		hdmi->xscale = value;
	else if (direction == DISPLAY_SCALE_Y)
		hdmi->yscale = value;
	else
		return -1;
	rk_fb_disp_scale(hdmi->xscale, hdmi->yscale, hdmi->lcdc->id);
	return 0;
}

static int hdmi_get_scale(struct rk_display_device *device, int direction)
{
	struct hdmi *hdmi = device->priv_data;

	if (!hdmi)
		return -1;

	if (direction == DISPLAY_SCALE_X)
		return hdmi->xscale;
	else if (direction == DISPLAY_SCALE_Y)
		return hdmi->yscale;
	else
		return -1;
}

static int hdmi_get_monspecs(struct rk_display_device *device,
			     struct fb_monspecs *monspecs)
{
	struct hdmi *hdmi = device->priv_data;

	if (!hdmi)
		return -1;

	if (hdmi->edid.specs)
		*monspecs = *hdmi->edid.specs;
	return 0;
}

/**
 * hdmi_show_sink_info: show hdmi sink device information
 * @hdmi: handle of hdmi
 */
static int hdmi_show_sink_info(struct hdmi *hdmi, char *buf, int len)
{
	struct list_head *pos, *head = &hdmi->edid.modelist;
	struct display_modelist *modelist;
	struct fb_videomode *m;
	struct hdmi_audio *audio;
	int i, lens = len;

	lens += snprintf(buf + lens, PAGE_SIZE - lens,
			"******** Show Sink Info ********\n");
	lens += snprintf(buf + lens, PAGE_SIZE - lens,
			 "Max tmds clk is %u\n",
			 hdmi->edid.maxtmdsclock);
	if (hdmi->edid.hf_vsdb_version)
		lens += snprintf(buf + lens, PAGE_SIZE - lens,
				 "Support HFVSDB\n");
	if (hdmi->edid.scdc_present)
		lens += snprintf(buf + lens, PAGE_SIZE - lens,
				 "Support SCDC\n");
	lens += snprintf(buf + lens, PAGE_SIZE - lens,
			 "Support video mode:\n");
	list_for_each(pos, head) {
		modelist = list_entry(pos, struct display_modelist, list);
		m = &modelist->mode;
		if (m->flag)
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 "\t%s(YCbCr420)\n", m->name);
		else
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 "\t%s\n", m->name);
	}
	lens += snprintf(buf + lens, PAGE_SIZE - lens,
			 "Support video color mode:");
	lens += snprintf(buf + lens, PAGE_SIZE - lens, " RGB");
	if (hdmi->edid.ycbcr420)
		lens += snprintf(buf + lens, PAGE_SIZE - lens,
				 " YCbCr420");
	if (hdmi->edid.ycbcr422)
		lens += snprintf(buf + lens, PAGE_SIZE - lens,
				 " YCbCr422");
	if (hdmi->edid.ycbcr444)
		lens += snprintf(buf + lens, PAGE_SIZE - lens,
				 " YCbCr444");
	lens += snprintf(buf + lens, PAGE_SIZE - lens,
			 "\nSupport video color depth:");
	lens += snprintf(buf + lens, PAGE_SIZE - lens, " 24bit");
	if (hdmi->edid.deepcolor & HDMI_DEEP_COLOR_30BITS)
		lens += snprintf(buf + lens, PAGE_SIZE - lens, " 30bit");
	if (hdmi->edid.deepcolor & HDMI_DEEP_COLOR_36BITS)
		lens += snprintf(buf + lens, PAGE_SIZE - lens, " 36bit");
	if (hdmi->edid.deepcolor & HDMI_DEEP_COLOR_48BITS)
		lens += snprintf(buf + lens, PAGE_SIZE - lens, " 48bit");
	if (hdmi->edid.ycbcr420)
		lens += snprintf(buf + lens, PAGE_SIZE - lens, " 420_24bit");
	if (hdmi->edid.deepcolor_420 & HDMI_DEEP_COLOR_30BITS)
		lens += snprintf(buf + lens, PAGE_SIZE - lens, " 420_30bit");
	if (hdmi->edid.deepcolor_420 & HDMI_DEEP_COLOR_36BITS)
		lens += snprintf(buf + lens, PAGE_SIZE - lens, " 420_36bit");
	if (hdmi->edid.deepcolor_420 & HDMI_DEEP_COLOR_48BITS)
		lens += snprintf(buf + lens, PAGE_SIZE - lens, " 420_48bit");
	if (hdmi->edid.colorimetry) {
		lens += snprintf(buf + lens, PAGE_SIZE - lens,
				 "\nExtended Colorimetry:");
		if (hdmi->edid.colorimetry &
		    (1 << (HDMI_COLORIMETRY_EXTEND_XVYCC_601 - 3)))
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " xvYCC601");
		if (hdmi->edid.colorimetry &
		    (1 << (HDMI_COLORIMETRY_EXTEND_XVYCC_709 - 3)))
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " xvYCC709");
		if (hdmi->edid.colorimetry &
		    (1 << (HDMI_COLORIMETRY_EXTEND_SYCC_601 - 3)))
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " sYCC601");
		if (hdmi->edid.colorimetry &
		    (1 << (HDMI_COLORIMETRY_EXTEND_ADOBE_YCC601 - 3)))
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " AdobeYCC601");
		if (hdmi->edid.colorimetry &
		    (1 << (HDMI_COLORIMETRY_EXTEND_ADOBE_RGB - 3)))
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " AdobeRGB");
		if (hdmi->edid.colorimetry &
		    (1 << (HDMI_COLORIMETRY_EXTEND_BT_2020_YCC_C - 3)))
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " BT2020cYCC");
		if (hdmi->edid.colorimetry &
		    (1 << (HDMI_COLORIMETRY_EXTEND_BT_2020_YCC - 3)))
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " BT2020YCC");
		if (hdmi->edid.colorimetry &
		    (1 << (HDMI_COLORIMETRY_EXTEND_BT_2020_RGB - 3)))
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " BT2020RGB");
	}
	lens += snprintf(buf + lens, PAGE_SIZE - lens,
			 "\nSupport audio type:");
	for (i = 0; i < hdmi->edid.audio_num; i++) {
		audio = &hdmi->edid.audio[i];
		switch (audio->type) {
		case HDMI_AUDIO_LPCM:
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
				" LPCM\n");
			break;
		case HDMI_AUDIO_AC3:
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " AC3");
			break;
		case HDMI_AUDIO_MPEG1:
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " MPEG1");
			break;
		case HDMI_AUDIO_MP3:
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " MP3");
			break;
		case HDMI_AUDIO_MPEG2:
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " MPEG2");
			break;
		case HDMI_AUDIO_AAC_LC:
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " AAC");
			break;
		case HDMI_AUDIO_DTS:
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " DTS");
			break;
		case HDMI_AUDIO_ATARC:
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " ATARC");
			break;
		case HDMI_AUDIO_DSD:
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " DSD");
			break;
		case HDMI_AUDIO_E_AC3:
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " E-AC3");
			break;
		case HDMI_AUDIO_DTS_HD:
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " DTS-HD");
			break;
		case HDMI_AUDIO_MLP:
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " MLP");
			break;
		case HDMI_AUDIO_DST:
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " DST");
			break;
		case HDMI_AUDIO_WMA_PRO:
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " WMP-PRO");
			break;
		default:
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " Unknown");
			break;
		}
		lens += snprintf(buf + lens, PAGE_SIZE - lens,
				 "Support max audio channel is %d\n",
				 audio->channel);
		lens += snprintf(buf + lens, PAGE_SIZE - lens,
				 "Support audio sample rate:");
		if (audio->rate & HDMI_AUDIO_FS_32000)
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " 32000");
		if (audio->rate & HDMI_AUDIO_FS_44100)
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " 44100");
		if (audio->rate & HDMI_AUDIO_FS_48000)
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " 48000");
		if (audio->rate & HDMI_AUDIO_FS_88200)
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " 88200");
		if (audio->rate & HDMI_AUDIO_FS_96000)
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " 96000");
		if (audio->rate & HDMI_AUDIO_FS_176400)
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " 176400");
		if (audio->rate & HDMI_AUDIO_FS_192000)
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " 192000");
		lens += snprintf(buf + lens, PAGE_SIZE - lens,
				 "\nSupport audio word length:");
		if (audio->rate & HDMI_AUDIO_WORD_LENGTH_16bit)
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " 16bit");
		if (audio->rate & HDMI_AUDIO_WORD_LENGTH_20bit)
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " 20bit");
		if (audio->rate & HDMI_AUDIO_WORD_LENGTH_24bit)
			lens += snprintf(buf + lens, PAGE_SIZE - lens,
					 " 24bit");
		lens += snprintf(buf + lens, PAGE_SIZE - lens, "\n");
	}
	return lens;
}

static int hdmi_get_debug(struct rk_display_device *device, char *buf)
{
	struct hdmi *hdmi = device->priv_data;
	u8 *buff;
	int i, j, len = 0;

	if (!hdmi)
		return 0;
	len += snprintf(buf + len, PAGE_SIZE - len, "EDID status:%s\n",
			hdmi->edid.status ? "False" : "Okay");
	len += snprintf(buf + len, PAGE_SIZE - len, "Raw Data:");
	for (i = 0; i < HDMI_MAX_EDID_BLOCK; i++) {
		if (!hdmi->edid.raw[i])
			break;
		buff = hdmi->edid.raw[i];
		for (j = 0; j < HDMI_EDID_BLOCK_SIZE; j++) {
			if (j % 16 == 0)
				len += snprintf(buf + len,
						PAGE_SIZE - len, "\n");
			len += snprintf(buf + len, PAGE_SIZE - len, "0x%02x, ",
					buff[j]);
		}
	}
	len += snprintf(buf + len, PAGE_SIZE, "\n");
	if (!hdmi->edid.status)
		len += hdmi_show_sink_info(hdmi, buf, len);
	return len;
}

static int vr_get_info(struct rk_display_device *device, char *buf)
{
	struct hdmi *hdmi = device->priv_data;
	int valid, width, height, x_w, x_h, hwr, einit, vsync, panel, scan;
	int len = 0;

	valid = hdmi->prop.valid;
	width = hdmi->prop.value.width;
	height = hdmi->prop.value.height;
	x_w = hdmi->prop.value.x_w;
	x_h = hdmi->prop.value.x_h;
	hwr = hdmi->prop.value.hwrotation;
	einit = hdmi->prop.value.einit;
	vsync = hdmi->prop.value.vsync;
	panel = hdmi->prop.value.panel;
	scan = hdmi->prop.value.scan;

	len = snprintf(buf, PAGE_SIZE,
		"valid=%d,width=%d,height=%d,xres=%d,yres=%d,hwrotation=%d,orientation=%d,vsync=%d,panel=%d,scan=%d\n",
		valid, width, height, x_w, x_h, hwr, einit, vsync, panel, scan);

	return len;
}

static struct rk_display_ops hdmi_display_ops = {
	.setenable = hdmi_set_enable,
	.getenable = hdmi_get_enable,
	.getstatus = hdmi_get_status,
	.getmodelist = hdmi_get_modelist,
	.setmode = hdmi_set_mode,
	.getmode = hdmi_get_mode,
	.set3dmode = hdmi_set_3dmode,
	.get3dmode = hdmi_get_3dmode,
	.getedidaudioinfo = hdmi_get_edidaudioinfo,
	.setcolor = hdmi_set_color,
	.getcolor = hdmi_get_color,
	.getmonspecs = hdmi_get_monspecs,
	.setscale = hdmi_set_scale,
	.getscale = hdmi_get_scale,
	.getdebug = hdmi_get_debug,
	.getvrinfo = vr_get_info,
};

static int hdmi_display_probe(struct rk_display_device *device, void *devdata)
{
	struct hdmi *hdmi = devdata;

	device->owner = THIS_MODULE;
	strcpy(device->type, "HDMI");
	if (strstr(hdmi->property->name, "dp"))
		strcpy(device->type, "DP");
	else
		strcpy(device->type, "HDMI");

	device->priority = DISPLAY_PRIORITY_HDMI;
	device->name = hdmi->property->name;
	device->property = hdmi->property->display;
	device->priv_data = devdata;
	device->ops = &hdmi_display_ops;
	return 1;
}

static struct rk_display_driver display_hdmi = {
	.probe = hdmi_display_probe,
};

struct rk_display_device *hdmi_register_display_sysfs(struct hdmi *hdmi,
						      struct device *parent)
{
	return rk_display_device_register(&display_hdmi, parent, hdmi);
}

void hdmi_unregister_display_sysfs(struct hdmi *hdmi)
{
	if (hdmi->ddev)
		rk_display_device_unregister(hdmi->ddev);
}
