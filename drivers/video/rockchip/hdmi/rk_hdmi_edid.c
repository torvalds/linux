#include "rk_hdmi.h"
#include "../../edid.h"

#define hdmi_edid_error(fmt, ...) \
	printk(pr_fmt(fmt), ##__VA_ARGS__)

#if 0
#define hdmi_edid_debug(fmt, ...) \
	printk(pr_fmt(fmt), ##__VA_ARGS__)
#else
#define hdmi_edid_debug(fmt, ...)
#endif

enum HDMI_EDID_ERRORCODE {
	E_HDMI_EDID_SUCCESS = 0,
	E_HDMI_EDID_PARAM,
	E_HDMI_EDID_HEAD,
	E_HDMI_EDID_CHECKSUM,
	E_HDMI_EDID_VERSION,
	E_HDMI_EDID_UNKOWNDATA,
	E_HDMI_EDID_NOMEMORY
};

static const unsigned int double_aspect_vic[] = {
	3, 7, 9, 11, 13, 15, 18, 22, 24, 26, 28, 30,
	36, 38, 43, 45, 49, 51, 53, 55, 57, 59
};

static int hdmi_edid_checksum(unsigned char *buf)
{
	int i;
	int checksum = 0;

	for (i = 0; i < HDMI_EDID_BLOCK_SIZE; i++)
		checksum += buf[i];

	checksum &= 0xff;

	if (checksum == 0)
		return E_HDMI_EDID_SUCCESS;
	else
		return E_HDMI_EDID_CHECKSUM;
}

/*
 * @Des	Parse Detail Timing Descriptor.
 * @Param	buf	:	pointer to DTD data.
 * @Param	pvic:	VIC of DTD descripted.
 */
static int hdmi_edid_parse_dtd(unsigned char *block, struct fb_videomode *mode)
{
	mode->xres = H_ACTIVE;
	mode->yres = V_ACTIVE;
	mode->pixclock = PIXEL_CLOCK;
/*
	mode->pixclock /= 1000;
	mode->pixclock = KHZ2PICOS(mode->pixclock);
*/
	mode->right_margin = H_SYNC_OFFSET;
	mode->left_margin = (H_ACTIVE + H_BLANKING) -
		(H_ACTIVE + H_SYNC_OFFSET + H_SYNC_WIDTH);
	mode->upper_margin = V_BLANKING - V_SYNC_OFFSET -
		V_SYNC_WIDTH;
	mode->lower_margin = V_SYNC_OFFSET;
	mode->hsync_len = H_SYNC_WIDTH;
	mode->vsync_len = V_SYNC_WIDTH;
	if (HSYNC_POSITIVE)
		mode->sync |= FB_SYNC_HOR_HIGH_ACT;
	if (VSYNC_POSITIVE)
		mode->sync |= FB_SYNC_VERT_HIGH_ACT;
	mode->refresh = PIXEL_CLOCK/((H_ACTIVE + H_BLANKING) *
				     (V_ACTIVE + V_BLANKING));
	if (INTERLACED) {
		mode->yres *= 2;
		mode->upper_margin *= 2;
		mode->lower_margin *= 2;
		mode->vsync_len *= 2;
		mode->vmode |= FB_VMODE_INTERLACED;
	}
	mode->flag = FB_MODE_IS_DETAILED;

	hdmi_edid_debug("<<<<<<<<Detailed Time>>>>>>>>>\n");
	hdmi_edid_debug("%d KHz Refresh %d Hz",  PIXEL_CLOCK/1000,
			mode->refresh);
	hdmi_edid_debug("%d %d %d %d ", H_ACTIVE, H_ACTIVE + H_SYNC_OFFSET,
	       H_ACTIVE + H_SYNC_OFFSET + H_SYNC_WIDTH, H_ACTIVE + H_BLANKING);
	hdmi_edid_debug("%d %d %d %d ", V_ACTIVE, V_ACTIVE + V_SYNC_OFFSET,
	       V_ACTIVE + V_SYNC_OFFSET + V_SYNC_WIDTH, V_ACTIVE + V_BLANKING);
	hdmi_edid_debug("%sHSync %sVSync\n\n", (HSYNC_POSITIVE) ? "+" : "-",
	       (VSYNC_POSITIVE) ? "+" : "-");
	return E_HDMI_EDID_SUCCESS;
}

static int hdmi_edid_parse_base(unsigned char *buf, int *extend_num,
				struct hdmi_edid *pedid)
{
	int rc;
#ifdef DEBUG
	int i = 0;
#endif

	if (buf == NULL || extend_num == NULL)
		return E_HDMI_EDID_PARAM;

#ifdef DEBUG
	for (i = 0; i < HDMI_EDID_BLOCK_SIZE; i++) {
		hdmi_edid_debug("%02x ", buf[i]&0xff);
		if ((i+1) % 16 == 0)
			hdmi_edid_debug("\n");
	}
#endif

	/* Check first 8 byte to ensure it is an edid base block. */
	if (buf[0] != 0x00 ||
	    buf[1] != 0xFF ||
	    buf[2] != 0xFF ||
	    buf[3] != 0xFF ||
	    buf[4] != 0xFF ||
	    buf[5] != 0xFF ||
	    buf[6] != 0xFF ||
	    buf[7] != 0x00) {
		hdmi_edid_error("[EDID] check header error\n");
		return E_HDMI_EDID_HEAD;
	}

	*extend_num = buf[0x7e];
#ifdef DEBUG
	hdmi_edid_debug("[EDID] extend block num is %d\n", buf[0x7e]);
#endif

	/* Checksum */
	rc = hdmi_edid_checksum(buf);
	if (rc != E_HDMI_EDID_SUCCESS) {
		hdmi_edid_error("[EDID] base block checksum error\n");
		return E_HDMI_EDID_CHECKSUM;
	}

	pedid->specs = kzalloc(sizeof(struct fb_monspecs), GFP_KERNEL);
	if (pedid->specs == NULL)
		return E_HDMI_EDID_NOMEMORY;

	fb_edid_to_monspecs(buf, pedid->specs);

	return E_HDMI_EDID_SUCCESS;
}

/* Parse CEA Short Video Descriptor */
static int hdmi_edid_get_cea_svd(unsigned char *buf, struct hdmi_edid *pedid)
{
	const struct fb_videomode *mode;
	int count, i, j, vic;

	count = buf[0] & 0x1F;
	for (i = 0; i < count; i++) {
		hdmi_edid_debug("[EDID-CEA] %02x VID %d native %d\n",
				buf[1 + i],
				buf[1 + i] & 0x7f,
				buf[1 + i] >> 7);
	#ifndef HDMI_VERSION_2
		vic = buf[1 + i] & 0x7f;
	#else
		vic = buf[1 + i] & 0xff;
	#endif
		for (j = 0; j < ARRAY_SIZE(double_aspect_vic); j++) {
			if (vic == double_aspect_vic[j]) {
				vic--;
				break;
			}
		}
		if (vic) {
			mode = hdmi_vic_to_videomode(vic);
			if (mode)
				hdmi_add_videomode(mode, &pedid->modelist);
		}
	}
	return 0;
}

/* Parse CEA Short Audio Descriptor */
static int hdmi_edid_parse_cea_sad(unsigned char *buf, struct hdmi_edid *pedid)
{
	int i, count;

	count = buf[0] & 0x1F;
	pedid->audio = kmalloc((count / 3) * sizeof(struct hdmi_audio),
				GFP_KERNEL);
	if (pedid->audio == NULL)
		return E_HDMI_EDID_NOMEMORY;

	pedid->audio_num = count / 3;
	for (i = 0; i < pedid->audio_num; i++) {
		pedid->audio[i].type = (buf[1 + i * 3] >> 3) & 0x0F;
		pedid->audio[i].channel = (buf[1 + i * 3] & 0x07) + 1;
		pedid->audio[i].rate = buf[1 + i * 3 + 1];
		if (pedid->audio[i].type == HDMI_AUDIO_LPCM)	/* LPCM */
			pedid->audio[i].word_length = buf[1 + i * 3 + 2];
/*
		printk("[EDID-CEA] type %d channel %d rate %d word length %d\n",
			pedid->audio[i].type, pedid->audio[i].channel,
			pedid->audio[i].rate, pedid->audio[i].word_length);
*/
	}
	return E_HDMI_EDID_SUCCESS;
}

/* Parse CEA Vendor Specific Data Block */
static int hdmi_edid_parse_cea_sdb(unsigned char *buf, struct hdmi_edid *pedid)
{
	unsigned int count = 0, cur_offset = 0, i = 0;
	unsigned int IEEEOUI = 0;
	unsigned int supports_ai, dc_48bit, dc_36bit, dc_30bit, dc_y444;
	unsigned int len_3d, len_4k;
	unsigned char vic = 0;
	const struct fb_videomode *mode;

	count = buf[0] & 0x1F;
	IEEEOUI = buf[3];
	IEEEOUI <<= 8;
	IEEEOUI += buf[2];
	IEEEOUI <<= 8;
	IEEEOUI += buf[1];
	hdmi_edid_debug("[EDID-CEA] IEEEOUI is 0x%08x.\n", IEEEOUI);
	if (IEEEOUI == 0x0c03)
		pedid->sink_hdmi = 1;

	pedid->cecaddress = buf[cur_offset + 5];
	pedid->cecaddress |= buf[cur_offset + 4] << 8;
	hdmi_edid_debug("[EDID-CEA] CEC Physical addres is 0x%08x.\n", pedid->cecaddress);

	if (count > 5) {
		pedid->deepcolor = (buf[6] >> 3) & 0x0F;
		supports_ai = buf[6] >> 7;
		dc_48bit = (buf[6] >> 6) & 0x1;
		dc_36bit = (buf[6] >> 5) & 0x1;
		dc_30bit = (buf[6] >> 4) & 0x1;
		dc_y444 = (buf[6] >> 3) & 0x1;
		hdmi_edid_debug("[EDID-CEA] supports_ai %d\n"
			"dc_48bit %d dc_36bit %d dc_30bit %d dc_y444 %d\n",
			supports_ai,
			dc_48bit, dc_36bit, dc_30bit, dc_y444);
	}
	if (count > 6)
		pedid->maxtmdsclock = buf[7] * 5000000;

	if (count > 7) {
		pedid->latency_fields_present = (buf[8] & 0x80) ? 1 : 0;
		pedid->i_latency_fields_present = (buf[8] & 0x40) ? 1 : 0;
		pedid->video_present = (buf[8] & 0x20) ? 1 : 0;
	}

	cur_offset = 9;
	if (count >= cur_offset) {
		if (pedid->latency_fields_present == 1) {
			pedid->video_latency = buf[cur_offset++];
			pedid->audio_latency = buf[cur_offset++];
		}
		if (count >= cur_offset && pedid->i_latency_fields_present) {
			pedid->interlaced_video_latency = buf[cur_offset++];
			pedid->interlaced_audio_latency = buf[cur_offset++];
		}
	}

	if (pedid->video_present == 0)
		return E_HDMI_EDID_SUCCESS;

	if (count >= cur_offset) {
		pedid->support_3d = (buf[cur_offset++] & 0x80) ? 1 : 0;

		len_4k = (buf[cur_offset] >> 5) & 0x07;
		len_3d = buf[cur_offset] & 0x1F;
		cur_offset++;
	}
	if (count >= cur_offset && len_4k > 0) {
		for (i = 0; i < len_4k; i++) {
		#ifndef HDMI_VERSION_2
			vic = buf[cur_offset + i] & 0x7f;
			if (vic > 0 && vic < 5)
				vic = (vic == 4) ? 98 : (96 - vic);
			hdmi_edid_debug("[EDID-CEA] %02x VID %d native %d\n",
					buf[cur_offset + i],
					vic,
					buf[cur_offset + i] >> 7);
		#else
			vic = buf[cur_offset + i] & 0xff;
			hdmi_edid_debug("[EDID-CEA] %02x VID %d native %d\n",
					buf[cur_offset + i], vic);
		#endif
			if (vic) {
				mode = hdmi_vic_to_videomode(vic);
				if (mode)
					hdmi_add_videomode(mode,
							   &pedid->modelist);
			}
		}
		cur_offset += i;
	}

/* TODO Daisen wait to add
	if (count >= cur_offset && pedid->support_3d && len_3d > 0) {

	}
*/
	return E_HDMI_EDID_SUCCESS;
}

/* Parse CEA 861 Serial Extension. */
static int hdmi_edid_parse_extensions_cea(unsigned char *buf,
					  struct hdmi_edid *pedid)
{
	unsigned int ddc_offset, native_dtd_num, cur_offset = 4;
	unsigned int underscan_support, baseaudio_support;
	unsigned int tag;

	if (buf == NULL)
		return E_HDMI_EDID_PARAM;

	/* Check ces extension version */
	if (buf[1] != 3) {
		hdmi_edid_error("[EDID-CEA] error version.\n");
		return E_HDMI_EDID_VERSION;
	}

	ddc_offset = buf[2];
	underscan_support = (buf[3] >> 7) & 0x01;
	baseaudio_support = (buf[3] >> 6) & 0x01;
	pedid->ycbcr444 = (buf[3] >> 5) & 0x01;
	pedid->ycbcr422 = (buf[3] >> 4) & 0x01;
	native_dtd_num = buf[3] & 0x0F;
	pedid->base_audio_support = baseaudio_support;

	/* Parse data block */
	while (cur_offset < ddc_offset) {
		tag = buf[cur_offset] >> 5;
		switch (tag) {
		case 0x02:	/* Video Data Block */
			hdmi_edid_debug("[EDID-CEA] It is a Video Data Block.\n");
			hdmi_edid_get_cea_svd(buf + cur_offset, pedid);
			break;
		case 0x01:	/* Audio Data Block */
			hdmi_edid_debug("[EDID-CEA] It is a Audio Data Block.\n");
			hdmi_edid_parse_cea_sad(buf + cur_offset, pedid);
			break;
		case 0x04:	/* Speaker Allocation Data Block */
			hdmi_edid_debug("[EDID-CEA] It is a Speaker Allocation Data Block.\n");
			break;
		case 0x03:	/* Vendor Specific Data Block */
			hdmi_edid_debug("[EDID-CEA] It is a Vendor Specific Data Block.\n");
			hdmi_edid_parse_cea_sdb(buf + cur_offset, pedid);
			break;
		case 0x05:	/* VESA DTC Data Block */
			hdmi_edid_debug("[EDID-CEA] It is a VESA DTC Data Block.\n");
			break;
		case 0x07:	/* Use Extended Tag */
			hdmi_edid_debug("[EDID-CEA] It is a Use Extended Tag Data Block.\n");
			break;
		default:
			hdmi_edid_error("[EDID-CEA] unkowned data block tag.\n");
			break;
		}
		cur_offset += (buf[cur_offset] & 0x1F) + 1;
	}
#if 1
{
	/* Parse DTD */
	struct fb_videomode *vmode = kmalloc(sizeof(struct fb_videomode),
					     GFP_KERNEL);
	if (vmode == NULL)
		return E_HDMI_EDID_SUCCESS;
	/* buf[126] = 0 and buf[127] = checksum */
	while (ddc_offset < HDMI_EDID_BLOCK_SIZE - 2) {
		if (!buf[ddc_offset] && !buf[ddc_offset + 1])
			break;
		memset(vmode, 0, sizeof(struct fb_videomode));
		hdmi_edid_parse_dtd(buf + ddc_offset, vmode);
		hdmi_add_videomode(vmode, &pedid->modelist);
		ddc_offset += 18;
	}
	kfree(vmode);
}
#endif
	return E_HDMI_EDID_SUCCESS;
}

static int hdmi_edid_parse_extensions(unsigned char *buf,
				      struct hdmi_edid *pedid)
{
	int rc;

	if (buf == NULL || pedid == NULL)
		return E_HDMI_EDID_PARAM;

	/* Checksum */
	rc = hdmi_edid_checksum(buf);
	if (rc != E_HDMI_EDID_SUCCESS) {
		hdmi_edid_error("[EDID] extensions block checksum error\n");
		return E_HDMI_EDID_CHECKSUM;
	}

	switch (buf[0]) {
	case 0xF0:
		hdmi_edid_debug("[EDID-EXTEND] It is a extensions block map.\n");
		break;
	case 0x02:
		hdmi_edid_debug("[EDID-EXTEND] It is a  CEA 861 Series Extension.\n");
		hdmi_edid_parse_extensions_cea(buf, pedid);
		break;
	case 0x10:
		hdmi_edid_debug("[EDID-EXTEND] It is a Video Timing Block Extension.\n");
		break;
	case 0x40:
		hdmi_edid_debug("[EDID-EXTEND] It is a Display Information Extension.\n");
		break;
	case 0x50:
		hdmi_edid_debug("[EDID-EXTEND] It is a Localized String Extension.\n");
		break;
	case 0x60:
		hdmi_edid_debug("[EDID-EXTEND] It is a Digital Packet Video Link Extension.\n");
		break;
	default:
		hdmi_edid_error("[EDID-EXTEND] Unkowned extension.\n");
		return E_HDMI_EDID_UNKOWNDATA;
	}

	return E_HDMI_EDID_SUCCESS;
}


int hdmi_sys_parse_edid(struct hdmi *hdmi)
{
	struct hdmi_edid *pedid;
	unsigned char *buff = NULL;
	int rc = HDMI_ERROR_SUCESS, extendblock = 0, i;

	if (hdmi == NULL)
		return HDMI_ERROR_FALSE;

	pedid = &(hdmi->edid);
	memset(pedid, 0, sizeof(struct hdmi_edid));
	INIT_LIST_HEAD(&pedid->modelist);

	buff = kmalloc(HDMI_EDID_BLOCK_SIZE, GFP_KERNEL);
	if (buff == NULL) {
		hdmi_dbg(hdmi->dev,
			 "[%s] can not allocate memory for edid buff.\n",
			 __func__);
		return -1;
	}

	/* Read base block edid. */
	memset(buff, 0 , HDMI_EDID_BLOCK_SIZE);
	rc = hdmi->read_edid(hdmi, 0, buff);
	if (rc) {
		dev_err(hdmi->dev, "[HDMI] read edid base block error\n");
		goto out;
	}
	rc = hdmi_edid_parse_base(buff, &extendblock, pedid);
	if (rc) {
		dev_err(hdmi->dev, "[HDMI] parse edid base block error\n");
		goto out;
	}
	for (i = 1; i < extendblock + 1; i++) {
		memset(buff, 0 , HDMI_EDID_BLOCK_SIZE);
		rc = hdmi->read_edid(hdmi, i, buff);
		if (rc) {
			printk("[HDMI] read edid block %d error\n", i);
			goto out;
		}
		rc = hdmi_edid_parse_extensions(buff, pedid);
		if (rc) {
			dev_err(hdmi->dev, "[HDMI] parse edid block %d error\n",
				i);
			continue;
		}
	}
out:
	kfree(buff);
	rc = hdmi_ouputmode_select(hdmi, rc);
	return rc;
}
