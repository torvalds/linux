/* SPDX-License-Identifier: GPL-2.0 */
#include "rockchip-hdmi.h"
#include "../../fbdev/edid.h"

#ifdef EDIDDEBUG
#define EDBG	DBG
#else
#define EDBG(format, ...)
#endif

enum {
	E_HDMI_EDID_SUCCESS = 0,
	E_HDMI_EDID_PARAM,
	E_HDMI_EDID_HEAD,
	E_HDMI_EDID_CHECKSUM,
	E_HDMI_EDID_VERSION,
	E_HDMI_EDID_UNKOWNDATA,
	E_HDMI_EDID_NOMEMORY
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
 *	@Des	Parse Detail Timing Descriptor.
 *	@Param	buf	:	pointer to DTD data.
 *	@Param	pvic:	VIC of DTD descripted.
 */
static int hdmi_edid_parse_dtd(unsigned char *block, struct fb_videomode *mode)
{
	mode->xres = H_ACTIVE;
	mode->yres = V_ACTIVE;
	mode->pixclock = PIXEL_CLOCK;
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
	mode->refresh = PIXEL_CLOCK / ((H_ACTIVE + H_BLANKING) *
				       (V_ACTIVE + V_BLANKING));
	if (INTERLACED) {
		mode->yres *= 2;
		mode->upper_margin *= 2;
		mode->lower_margin *= 2;
		mode->vsync_len *= 2;
		mode->vmode |= FB_VMODE_INTERLACED;
	}
	mode->flag = FB_MODE_IS_DETAILED;

	EDBG("<<<<<<<<Detailed Time>>>>>>>>>\n");
	EDBG("%d KHz Refresh %d Hz",
	     PIXEL_CLOCK / 1000, mode->refresh);
	EDBG("%d %d %d %d ", H_ACTIVE, H_ACTIVE + H_SYNC_OFFSET,
	     H_ACTIVE + H_SYNC_OFFSET + H_SYNC_WIDTH, H_ACTIVE + H_BLANKING);
	EDBG("%d %d %d %d ", V_ACTIVE, V_ACTIVE + V_SYNC_OFFSET,
	     V_ACTIVE + V_SYNC_OFFSET + V_SYNC_WIDTH, V_ACTIVE + V_BLANKING);
	EDBG("%sHSync %sVSync\n\n", (HSYNC_POSITIVE) ? "+" : "-",
	     (VSYNC_POSITIVE) ? "+" : "-");
	return E_HDMI_EDID_SUCCESS;
}

static int edid_parse_prop_value(unsigned char *buf,
				 struct hdmi_edid *pedid)
{
	unsigned char *block = &buf[0x36];

	pedid->value.vid = ((buf[ID_MANUFACTURER_NAME_END] << 8) |
				(buf[ID_MANUFACTURER_NAME]));
	pedid->value.pid = ((buf[ID_MODEL + 1] << 8) |
				(buf[ID_MODEL]));
	pedid->value.sn = ((buf[ID_SERIAL_NUMBER + 3] << 24) |
				(buf[ID_SERIAL_NUMBER + 2] << 16) |
				(buf[ID_SERIAL_NUMBER + 1] << 8) |
				buf[ID_SERIAL_NUMBER]);
	pedid->value.xres = H_ACTIVE;
	pedid->value.yres = V_ACTIVE;

	pr_info("%s:read:vid=0x%x,pid=0x%x,sn=0x%x,xres=%d,yres=%d\n",
		__func__, pedid->value.vid, pedid->value.pid,
		pedid->value.sn, pedid->value.xres, pedid->value.yres);

	return 0;
}

int hdmi_edid_parse_base(struct hdmi *hdmi, unsigned char *buf,
			 int *extend_num, struct hdmi_edid *pedid)
{
	int rc = E_HDMI_EDID_SUCCESS;

	if (!buf || !extend_num)
		return E_HDMI_EDID_PARAM;

	*extend_num = buf[0x7e];
	#ifdef DEBUG
	EDBG("[EDID] extend block num is %d\n", buf[0x7e]);
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
		pr_err("[EDID] check header error\n");
		rc = E_HDMI_EDID_HEAD;
		goto out;
	}

	/* Checksum */
	rc = hdmi_edid_checksum(buf);
	if (rc != E_HDMI_EDID_SUCCESS) {
		pr_err("[EDID] base block checksum error\n");
		rc = E_HDMI_EDID_CHECKSUM;
		goto out;
	}

	pedid->specs = kzalloc(sizeof(*pedid->specs), GFP_KERNEL);
	if (!pedid->specs)
		return E_HDMI_EDID_NOMEMORY;

	fb_edid_to_monspecs(buf, pedid->specs);

	if (hdmi->edid_auto_support)
		edid_parse_prop_value(buf, pedid);

out:
	/* For some sink, edid checksum is failed because several
	 * byte is wrong. To fix this case, we think it is a good
	 * edid if 1 <= *extend_num <= 4.
	 */
	if ((rc != E_HDMI_EDID_SUCCESS) &&
	    (*extend_num < 1 || *extend_num > 4))
		return rc;
	else
		return E_HDMI_EDID_SUCCESS;
}

/* Parse CEA Short Video Descriptor */
static int hdmi_edid_get_cea_svd(unsigned char *buf, struct hdmi_edid *pedid)
{
	int count, i, vic;

	count = buf[0] & 0x1F;
	for (i = 0; i < count; i++) {
		EDBG("[CEA] %02x VID %d native %d\n",
		     buf[1 + i], buf[1 + i] & 0x7f, buf[1 + i] >> 7);
		vic = buf[1 + i] & 0x7f;
		hdmi_add_vic(vic, &pedid->modelist);
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
	if (!pedid->audio)
		return E_HDMI_EDID_NOMEMORY;

	pedid->audio_num = count / 3;
	for (i = 0; i < pedid->audio_num; i++) {
		pedid->audio[i].type = (buf[1 + i * 3] >> 3) & 0x0F;
		pedid->audio[i].channel = (buf[1 + i * 3] & 0x07) + 1;
		pedid->audio[i].rate = buf[1 + i * 3 + 1];
		if (pedid->audio[i].type == HDMI_AUDIO_LPCM)
			pedid->audio[i].word_length = buf[1 + i * 3 + 2];
	}
	return E_HDMI_EDID_SUCCESS;
}

static int hdmi_edid_parse_3dinfo(unsigned char *buf, struct list_head *head)
{
	int i, j, len = 0, format_3d, vic_mask;
	unsigned char offset = 2, vic_2d, structure_3d;
	struct list_head *pos;
	struct display_modelist *modelist;

	if (buf[1] & 0xe0) {
		len = (buf[1] & 0xe0) >> 5;
		for (i = 0; i < len; i++) {
			if (buf[offset]) {
				vic_2d = (buf[offset] == 4) ?
					 98 : (96 - buf[offset]);
				hdmi_add_vic(vic_2d, head);
			}
			offset++;
		}
	}

	if (buf[0] & 0x80) {
		/* 3d supported */
		len += (buf[1] & 0x1F) + 2;
		if (((buf[0] & 0x60) == 0x40) || ((buf[0] & 0x60) == 0x20)) {
			format_3d = buf[offset++] << 8;
			format_3d |= buf[offset++];
			if ((buf[0] & 0x60) == 0x20) {
				vic_mask = 0xFFFF;
			} else {
				vic_mask  = buf[offset++] << 8;
				vic_mask |= buf[offset++];
			}
		} else {
			format_3d = 0;
			vic_mask = 0;
		}

		for (i = 0; i < 16; i++) {
			if (vic_mask & (1 << i)) {
				j = 0;
				for (pos = (head)->next; pos != (head);
					pos = pos->next) {
					if (j++ == i) {
						modelist =
			list_entry(pos, struct display_modelist, list);
						modelist->format_3d = format_3d;
						break;
					}
				}
			}
		}
		while (offset < len) {
			vic_2d = (buf[offset] & 0xF0) >> 4;
			structure_3d = (buf[offset++] & 0x0F);
			j = 0;
			for (pos = (head)->next; pos != (head);
				pos = pos->next) {
				j++;
				if (j == vic_2d) {
					modelist =
				list_entry(pos, struct display_modelist, list);
					modelist->format_3d |=
						(1 << structure_3d);
					if (structure_3d & 0x08)
						modelist->detail_3d =
						(buf[offset++] & 0xF0) >> 4;
					break;
				}
			}
		}
		/* mandatory formats */
		for (pos = (head)->next; pos != (head); pos = pos->next) {
			modelist = list_entry(pos,
					      struct display_modelist,
					      list);
			if (modelist->vic == HDMI_1920X1080P_24HZ ||
			    modelist->vic == HDMI_1280X720P_60HZ ||
			    modelist->vic == HDMI_1280X720P_50HZ) {
				modelist->format_3d |=
					(1 << HDMI_3D_FRAME_PACKING) |
					(1 << HDMI_3D_TOP_BOOTOM);
			} else if (modelist->vic == HDMI_1920X1080I_60HZ ||
				   modelist->vic == HDMI_1920X1080I_50HZ) {
				modelist->format_3d |=
					(1 << HDMI_3D_SIDE_BY_SIDE_HALF);
			}
		}
	}

	return 0;
}

static int hdmi_edmi_parse_vsdb(unsigned char *buf, struct hdmi_edid *pedid,
				int cur_offset, int IEEEOUI)
{
	int count, buf_offset;

	count = buf[cur_offset] & 0x1F;
	switch (IEEEOUI) {
	case 0x0c03:
		pedid->sink_hdmi = 1;
		pedid->cecaddress = buf[cur_offset + 5];
		pedid->cecaddress |= buf[cur_offset + 4] << 8;
		EDBG("[CEA] CEC Physical address is 0x%08x.\n",
		     pedid->cecaddress);
		if (count > 6)
			pedid->deepcolor = (buf[cur_offset + 6] >> 3) & 0x0F;
		if (count > 7) {
			pedid->maxtmdsclock = buf[cur_offset + 7] * 5000000;
			EDBG("[CEA] maxtmdsclock is %d.\n",
			     pedid->maxtmdsclock);
		}
		if (count > 8) {
			pedid->fields_present = buf[cur_offset + 8];
			EDBG("[CEA] fields_present is 0x%02x.\n",
			     pedid->fields_present);
		}
		buf_offset = cur_offset + 9;
		if (pedid->fields_present & 0x80) {
			pedid->video_latency = buf[buf_offset++];
			pedid->audio_latency = buf[buf_offset++];
		}
		if (pedid->fields_present & 0x40) {
			pedid->interlaced_video_latency = buf[buf_offset++];
			pedid->interlaced_audio_latency = buf[buf_offset++];
		}
		if (pedid->fields_present & 0x20) {
			hdmi_edid_parse_3dinfo(buf + buf_offset,
					       &pedid->modelist);
		}
		break;
	case 0xc45dd8:
		pedid->sink_hdmi = 1;
		pedid->hf_vsdb_version = buf[cur_offset + 4];
		switch (pedid->hf_vsdb_version) {
		case 1:/*compliant with HDMI Specification 2.0*/
			pedid->maxtmdsclock =
				buf[cur_offset + 5] * 5000000;
			EDBG("[CEA] maxtmdsclock is %d.\n",
			     pedid->maxtmdsclock);
			pedid->scdc_present = buf[cur_offset + 6] >> 7;
			pedid->rr_capable =
				(buf[cur_offset + 6] & 0x40) >> 6;
			pedid->lte_340mcsc_scramble =
				(buf[cur_offset + 6] & 0x08) >> 3;
			pedid->independent_view =
				(buf[cur_offset + 6] & 0x04) >> 2;
			pedid->dual_view =
				(buf[cur_offset + 6] & 0x02) >> 1;
			pedid->osd_disparity_3d =
				buf[cur_offset + 6] & 0x01;
			pedid->deepcolor_420 =
				(buf[cur_offset + 7] & 0x7) << 1;
			break;
		default:
			pr_info("hf_vsdb_version = %d\n",
				pedid->hf_vsdb_version);
			break;
		}
		break;
	default:
		pr_info("IEEOUT = 0x%x\n", IEEEOUI);
		break;
	}
	return 0;
}

static void hdmi_edid_parse_yuv420cmdb(unsigned char *buf, int count,
				       struct list_head *head)
{
	struct list_head *pos;
	struct display_modelist *modelist;
	int i, j, yuv420_mask = 0, vic;

	if (count == 1) {
		list_for_each(pos, head) {
			modelist =
				list_entry(pos, struct display_modelist, list);
			vic = modelist->vic | HDMI_VIDEO_YUV420;
			hdmi_add_vic(vic, head);
		}
	} else {
		for (i = 0; i < count - 1; i++) {
			EDBG("vic which support yuv420 mode is %x\n", buf[i]);
			yuv420_mask |= buf[i] << (8 * i);
		}
		for (i = 0; i < 32; i++) {
			if (!(yuv420_mask & (1 << i)))
				continue;
			j = 0;
			list_for_each(pos, head) {
				if (j++ == i) {
					modelist =
				list_entry(pos, struct display_modelist, list);
					vic = modelist->vic |
					      HDMI_VIDEO_YUV420;
					hdmi_add_vic(vic, head);
					break;
				}
			}
		}
	}
}

/* Parse CEA 861 Serial Extension. */
static int hdmi_edid_parse_extensions_cea(unsigned char *buf,
					  struct hdmi_edid *pedid)
{
	unsigned int ddc_offset, native_dtd_num, cur_offset = 4;
	unsigned int tag, IEEEOUI = 0, count, i;
	struct fb_videomode *vmode;

	if (!buf)
		return E_HDMI_EDID_PARAM;

	/* Check ces extension version */
	if (buf[1] != 3) {
		pr_err("[CEA] error version.\n");
		return E_HDMI_EDID_VERSION;
	}

	ddc_offset = buf[2];
	pedid->baseaudio_support = (buf[3] >> 6) & 0x01;
	pedid->ycbcr444 = (buf[3] >> 5) & 0x01;
	pedid->ycbcr422 = (buf[3] >> 4) & 0x01;
	native_dtd_num = buf[3] & 0x0F;
	/* Parse data block */
	while (cur_offset < ddc_offset) {
		tag = buf[cur_offset] >> 5;
		count = buf[cur_offset] & 0x1F;
		switch (tag) {
		case 0x02:	/* Video Data Block */
			EDBG("[CEA] Video Data Block.\n");
			hdmi_edid_get_cea_svd(buf + cur_offset, pedid);
			break;
		case 0x01:	/* Audio Data Block */
			EDBG("[CEA] Audio Data Block.\n");
			hdmi_edid_parse_cea_sad(buf + cur_offset, pedid);
			break;
		case 0x04:	/* Speaker Allocation Data Block */
			EDBG("[CEA] Speaker Allocatio Data Block.\n");
			break;
		case 0x03:	/* Vendor Specific Data Block */
			EDBG("[CEA] Vendor Specific Data Block.\n");

			IEEEOUI = buf[cur_offset + 3];
			IEEEOUI <<= 8;
			IEEEOUI += buf[cur_offset + 2];
			IEEEOUI <<= 8;
			IEEEOUI += buf[cur_offset + 1];
			EDBG("[CEA] IEEEOUI is 0x%08x.\n", IEEEOUI);

			hdmi_edmi_parse_vsdb(buf, pedid,
					     cur_offset, IEEEOUI);
			break;
		case 0x05:	/* VESA DTC Data Block */
			EDBG("[CEA] VESA DTC Data Block.\n");
			break;
		case 0x07:	/* Use Extended Tag */
			EDBG("[CEA] Use Extended Tag Data Block %02x.\n",
			     buf[cur_offset + 1]);
			switch (buf[cur_offset + 1]) {
			case 0x00:
				EDBG("[CEA] Video Capability Data Block\n");
				EDBG("value is %02x\n", buf[cur_offset + 2]);
				break;
			case 0x05:
				EDBG("[CEA] Colorimetry Data Block\n");
				EDBG("value is %02x\n", buf[cur_offset + 2]);
				pedid->colorimetry = buf[cur_offset + 2];
				break;
			case 0x06:
				EDBG("[CEA] HDR Static Metedata data Block\n");
				for (i = 0; i < count - 1; i++)
					pedid->hdr.data[i] =
						buf[cur_offset + 2 + i];
				break;
			case 0x0e:
				EDBG("[CEA] YCBCR 4:2:0 Video Data Block\n");
				for (i = 0; i < count - 1; i++) {
					EDBG("mode is %d\n",
					     buf[cur_offset + 2 + i]);
					pedid->ycbcr420 = 1;
					IEEEOUI = buf[cur_offset + 2 + i] |
						  HDMI_VIDEO_YUV420;
					hdmi_add_vic(IEEEOUI,
						     &pedid->modelist);
				}
				break;
			case 0x0f:
				EDBG("[CEA] YCBCR 4:2:0 Capability Map Data\n");
				hdmi_edid_parse_yuv420cmdb(&buf[cur_offset + 2],
							   count,
							   &pedid->modelist);
				pedid->ycbcr420 = 1;
				break;
			}
			break;
		default:
			pr_err("[CEA] unkowned data block tag.\n");
			break;
		}
		cur_offset += (buf[cur_offset] & 0x1F) + 1;
	}

	/* Parse DTD */
	vmode = kmalloc(sizeof(*vmode), GFP_KERNEL);

	if (!vmode)
		return E_HDMI_EDID_SUCCESS;
	while (ddc_offset < HDMI_EDID_BLOCK_SIZE - 2) {
		if (!buf[ddc_offset] && !buf[ddc_offset + 1])
			break;
		memset(vmode, 0, sizeof(struct fb_videomode));
		hdmi_edid_parse_dtd(buf + ddc_offset, vmode);
		hdmi_add_vic(hdmi_videomode_to_vic(vmode), &pedid->modelist);
		ddc_offset += 18;
	}
	kfree(vmode);

	return E_HDMI_EDID_SUCCESS;
}

int hdmi_edid_parse_extensions(unsigned char *buf, struct hdmi_edid *pedid)
{
	int rc;

	if (!buf || !pedid)
		return E_HDMI_EDID_PARAM;

	/* Checksum */
	rc = hdmi_edid_checksum(buf);
	if (rc != E_HDMI_EDID_SUCCESS) {
		pr_err("[EDID] extensions block checksum error\n");
		return E_HDMI_EDID_CHECKSUM;
	}

	switch (buf[0]) {
	case 0xF0:
		EDBG("[EDID-EXTEND] Iextensions block map.\n");
		break;
	case 0x02:
		EDBG("[EDID-EXTEND] CEA 861 Series Extension.\n");
		hdmi_edid_parse_extensions_cea(buf, pedid);
		break;
	case 0x10:
		EDBG("[EDID-EXTEND] Video Timing Block Extension.\n");
		break;
	case 0x40:
		EDBG("[EDID-EXTEND] Display Information Extension.\n");
		break;
	case 0x50:
		EDBG("[EDID-EXTEND] Localized String Extension.\n");
		break;
	case 0x60:
		EDBG("[EDID-EXTEND] Digital Packet Video Link Extension.\n");
		break;
	default:
		pr_err("[EDID-EXTEND] Unkowned Extension.\n");
		return E_HDMI_EDID_UNKOWNDATA;
	}

	return E_HDMI_EDID_SUCCESS;
}
