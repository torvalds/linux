/*
    ivtv firmware functions.
    Copyright (C) 2003-2004  Kevin Thayer <nufan_wfk at yahoo.com>
    Copyright (C) 2004  Chris Kennedy <c@groovy.org>
    Copyright (C) 2005-2007  Hans Verkuil <hverkuil@xs4all.nl>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "ivtv-driver.h"
#include "ivtv-mailbox.h"
#include "ivtv-firmware.h"
#include "ivtv-yuv.h"
#include "ivtv-ioctl.h"
#include "ivtv-cards.h"
#include <linux/firmware.h>
#include <media/i2c/saa7127.h>

#define IVTV_MASK_SPU_ENABLE		0xFFFFFFFE
#define IVTV_MASK_VPU_ENABLE15		0xFFFFFFF6
#define IVTV_MASK_VPU_ENABLE16		0xFFFFFFFB
#define IVTV_CMD_VDM_STOP		0x00000000
#define IVTV_CMD_AO_STOP		0x00000005
#define IVTV_CMD_APU_PING		0x00000000
#define IVTV_CMD_VPU_STOP15		0xFFFFFFFE
#define IVTV_CMD_VPU_STOP16		0xFFFFFFEE
#define IVTV_CMD_HW_BLOCKS_RST		0xFFFFFFFF
#define IVTV_CMD_SPU_STOP		0x00000001
#define IVTV_CMD_SDRAM_PRECHARGE_INIT	0x0000001A
#define IVTV_CMD_SDRAM_REFRESH_INIT	0x80000640
#define IVTV_SDRAM_SLEEPTIME		600

#define IVTV_DECODE_INIT_MPEG_FILENAME	"v4l-cx2341x-init.mpg"
#define IVTV_DECODE_INIT_MPEG_SIZE	(152*1024)

/* Encoder/decoder firmware sizes */
#define IVTV_FW_ENC_SIZE		(376836)
#define IVTV_FW_DEC_SIZE		(256*1024)

static int load_fw_direct(const char *fn, volatile u8 __iomem *mem, struct ivtv *itv, long size)
{
	const struct firmware *fw = NULL;
	int retries = 3;

retry:
	if (retries && request_firmware(&fw, fn, &itv->pdev->dev) == 0) {
		int i;
		volatile u32 __iomem *dst = (volatile u32 __iomem *)mem;
		const u32 *src = (const u32 *)fw->data;

		if (fw->size != size) {
			/* Due to race conditions in firmware loading (esp. with udev <0.95)
			   the wrong file was sometimes loaded. So we check filesizes to
			   see if at least the right-sized file was loaded. If not, then we
			   retry. */
			IVTV_INFO("Retry: file loaded was not %s (expected size %ld, got %zu)\n", fn, size, fw->size);
			release_firmware(fw);
			retries--;
			goto retry;
		}
		for (i = 0; i < fw->size; i += 4) {
			/* no need for endianness conversion on the ppc */
			__raw_writel(*src, dst);
			dst++;
			src++;
		}
		IVTV_INFO("Loaded %s firmware (%zu bytes)\n", fn, fw->size);
		release_firmware(fw);
		return size;
	}
	IVTV_ERR("Unable to open firmware %s (must be %ld bytes)\n", fn, size);
	IVTV_ERR("Did you put the firmware in the hotplug firmware directory?\n");
	return -ENOMEM;
}

void ivtv_halt_firmware(struct ivtv *itv)
{
	IVTV_DEBUG_INFO("Preparing for firmware halt.\n");
	if (itv->has_cx23415 && itv->dec_mbox.mbox)
		ivtv_vapi(itv, CX2341X_DEC_HALT_FW, 0);
	if (itv->enc_mbox.mbox)
		ivtv_vapi(itv, CX2341X_ENC_HALT_FW, 0);

	ivtv_msleep_timeout(10, 0);
	itv->enc_mbox.mbox = itv->dec_mbox.mbox = NULL;

	IVTV_DEBUG_INFO("Stopping VDM\n");
	write_reg(IVTV_CMD_VDM_STOP, IVTV_REG_VDM);

	IVTV_DEBUG_INFO("Stopping AO\n");
	write_reg(IVTV_CMD_AO_STOP, IVTV_REG_AO);

	IVTV_DEBUG_INFO("pinging (?) APU\n");
	write_reg(IVTV_CMD_APU_PING, IVTV_REG_APU);

	IVTV_DEBUG_INFO("Stopping VPU\n");
	if (!itv->has_cx23415)
		write_reg(IVTV_CMD_VPU_STOP16, IVTV_REG_VPU);
	else
		write_reg(IVTV_CMD_VPU_STOP15, IVTV_REG_VPU);

	IVTV_DEBUG_INFO("Resetting Hw Blocks\n");
	write_reg(IVTV_CMD_HW_BLOCKS_RST, IVTV_REG_HW_BLOCKS);

	IVTV_DEBUG_INFO("Stopping SPU\n");
	write_reg(IVTV_CMD_SPU_STOP, IVTV_REG_SPU);

	ivtv_msleep_timeout(10, 0);

	IVTV_DEBUG_INFO("init Encoder SDRAM pre-charge\n");
	write_reg(IVTV_CMD_SDRAM_PRECHARGE_INIT, IVTV_REG_ENC_SDRAM_PRECHARGE);

	IVTV_DEBUG_INFO("init Encoder SDRAM refresh to 1us\n");
	write_reg(IVTV_CMD_SDRAM_REFRESH_INIT, IVTV_REG_ENC_SDRAM_REFRESH);

	if (itv->has_cx23415) {
		IVTV_DEBUG_INFO("init Decoder SDRAM pre-charge\n");
		write_reg(IVTV_CMD_SDRAM_PRECHARGE_INIT, IVTV_REG_DEC_SDRAM_PRECHARGE);

		IVTV_DEBUG_INFO("init Decoder SDRAM refresh to 1us\n");
		write_reg(IVTV_CMD_SDRAM_REFRESH_INIT, IVTV_REG_DEC_SDRAM_REFRESH);
	}

	IVTV_DEBUG_INFO("Sleeping for %dms\n", IVTV_SDRAM_SLEEPTIME);
	ivtv_msleep_timeout(IVTV_SDRAM_SLEEPTIME, 0);
}

void ivtv_firmware_versions(struct ivtv *itv)
{
	u32 data[CX2341X_MBOX_MAX_DATA];

	/* Encoder */
	ivtv_vapi_result(itv, data, CX2341X_ENC_GET_VERSION, 0);
	IVTV_INFO("Encoder revision: 0x%08x\n", data[0]);

	if (data[0] != 0x02060039)
		IVTV_WARN("Recommended firmware version is 0x02060039.\n");

	if (itv->has_cx23415) {
		/* Decoder */
		ivtv_vapi_result(itv, data, CX2341X_DEC_GET_VERSION, 0);
		IVTV_INFO("Decoder revision: 0x%08x\n", data[0]);
	}
}

static int ivtv_firmware_copy(struct ivtv *itv)
{
	IVTV_DEBUG_INFO("Loading encoder image\n");
	if (load_fw_direct(CX2341X_FIRM_ENC_FILENAME,
		   itv->enc_mem, itv, IVTV_FW_ENC_SIZE) != IVTV_FW_ENC_SIZE) {
		IVTV_DEBUG_WARN("failed loading encoder firmware\n");
		return -3;
	}
	if (!itv->has_cx23415)
		return 0;

	IVTV_DEBUG_INFO("Loading decoder image\n");
	if (load_fw_direct(CX2341X_FIRM_DEC_FILENAME,
		   itv->dec_mem, itv, IVTV_FW_DEC_SIZE) != IVTV_FW_DEC_SIZE) {
		IVTV_DEBUG_WARN("failed loading decoder firmware\n");
		return -1;
	}
	return 0;
}

static volatile struct ivtv_mailbox __iomem *ivtv_search_mailbox(const volatile u8 __iomem *mem, u32 size)
{
	int i;

	/* mailbox is preceded by a 16 byte 'magic cookie' starting at a 256-byte
	   address boundary */
	for (i = 0; i < size; i += 0x100) {
		if (readl(mem + i)      == 0x12345678 &&
		    readl(mem + i + 4)  == 0x34567812 &&
		    readl(mem + i + 8)  == 0x56781234 &&
		    readl(mem + i + 12) == 0x78123456) {
			return (volatile struct ivtv_mailbox __iomem *)(mem + i + 16);
		}
	}
	return NULL;
}

int ivtv_firmware_init(struct ivtv *itv)
{
	int err;

	ivtv_halt_firmware(itv);

	/* load firmware */
	err = ivtv_firmware_copy(itv);
	if (err) {
		IVTV_DEBUG_WARN("Error %d loading firmware\n", err);
		return err;
	}

	/* start firmware */
	write_reg(read_reg(IVTV_REG_SPU) & IVTV_MASK_SPU_ENABLE, IVTV_REG_SPU);
	ivtv_msleep_timeout(100, 0);
	if (itv->has_cx23415)
		write_reg(read_reg(IVTV_REG_VPU) & IVTV_MASK_VPU_ENABLE15, IVTV_REG_VPU);
	else
		write_reg(read_reg(IVTV_REG_VPU) & IVTV_MASK_VPU_ENABLE16, IVTV_REG_VPU);
	ivtv_msleep_timeout(100, 0);

	/* find mailboxes and ping firmware */
	itv->enc_mbox.mbox = ivtv_search_mailbox(itv->enc_mem, IVTV_ENCODER_SIZE);
	if (itv->enc_mbox.mbox == NULL)
		IVTV_ERR("Encoder mailbox not found\n");
	else if (ivtv_vapi(itv, CX2341X_ENC_PING_FW, 0)) {
		IVTV_ERR("Encoder firmware dead!\n");
		itv->enc_mbox.mbox = NULL;
	}
	if (itv->enc_mbox.mbox == NULL)
		return -ENODEV;

	if (!itv->has_cx23415)
		return 0;

	itv->dec_mbox.mbox = ivtv_search_mailbox(itv->dec_mem, IVTV_DECODER_SIZE);
	if (itv->dec_mbox.mbox == NULL) {
		IVTV_ERR("Decoder mailbox not found\n");
	} else if (itv->has_cx23415 && ivtv_vapi(itv, CX2341X_DEC_PING_FW, 0)) {
		IVTV_ERR("Decoder firmware dead!\n");
		itv->dec_mbox.mbox = NULL;
	} else {
		/* Firmware okay, so check yuv output filter table */
		ivtv_yuv_filter_check(itv);
	}
	return itv->dec_mbox.mbox ? 0 : -ENODEV;
}

void ivtv_init_mpeg_decoder(struct ivtv *itv)
{
	u32 data[CX2341X_MBOX_MAX_DATA];
	long readbytes;
	volatile u8 __iomem *mem_offset;

	data[0] = 0;
	data[1] = itv->cxhdl.width;	/* YUV source width */
	data[2] = itv->cxhdl.height;
	data[3] = itv->cxhdl.audio_properties;	/* Audio settings to use,
							   bitmap. see docs. */
	if (ivtv_api(itv, CX2341X_DEC_SET_DECODER_SOURCE, 4, data)) {
		IVTV_ERR("ivtv_init_mpeg_decoder failed to set decoder source\n");
		return;
	}

	if (ivtv_vapi(itv, CX2341X_DEC_START_PLAYBACK, 2, 0, 1) != 0) {
		IVTV_ERR("ivtv_init_mpeg_decoder failed to start playback\n");
		return;
	}
	ivtv_api_get_data(&itv->dec_mbox, IVTV_MBOX_DMA, 2, data);
	mem_offset = itv->dec_mem + data[1];

	if ((readbytes = load_fw_direct(IVTV_DECODE_INIT_MPEG_FILENAME,
		mem_offset, itv, IVTV_DECODE_INIT_MPEG_SIZE)) <= 0) {
		IVTV_DEBUG_WARN("failed to read mpeg decoder initialisation file %s\n",
				IVTV_DECODE_INIT_MPEG_FILENAME);
	} else {
		ivtv_vapi(itv, CX2341X_DEC_SCHED_DMA_FROM_HOST, 3, 0, readbytes, 0);
		ivtv_msleep_timeout(100, 0);
	}
	ivtv_vapi(itv, CX2341X_DEC_STOP_PLAYBACK, 4, 0, 0, 0, 1);
}

/* Try to restart the card & restore previous settings */
static int ivtv_firmware_restart(struct ivtv *itv)
{
	int rc = 0;
	v4l2_std_id std;

	if (itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT)
		/* Display test image during restart */
		ivtv_call_hw(itv, IVTV_HW_SAA7127, video, s_routing,
		    SAA7127_INPUT_TYPE_TEST_IMAGE,
		    itv->card->video_outputs[itv->active_output].video_output,
		    0);

	mutex_lock(&itv->udma.lock);

	rc = ivtv_firmware_init(itv);
	if (rc) {
		mutex_unlock(&itv->udma.lock);
		return rc;
	}

	/* Allow settings to reload */
	ivtv_mailbox_cache_invalidate(itv);

	/* Restore encoder video standard */
	std = itv->std;
	itv->std = 0;
	ivtv_s_std_enc(itv, std);

	if (itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT) {
		ivtv_init_mpeg_decoder(itv);

		/* Restore decoder video standard */
		std = itv->std_out;
		itv->std_out = 0;
		ivtv_s_std_dec(itv, std);

		/* Restore framebuffer if active */
		if (itv->ivtvfb_restore)
			itv->ivtvfb_restore(itv);

		/* Restore alpha settings */
		ivtv_set_osd_alpha(itv);

		/* Restore normal output */
		ivtv_call_hw(itv, IVTV_HW_SAA7127, video, s_routing,
		    SAA7127_INPUT_TYPE_NORMAL,
		    itv->card->video_outputs[itv->active_output].video_output,
		    0);
	}

	mutex_unlock(&itv->udma.lock);
	return rc;
}

/* Check firmware running state. The checks fall through
   allowing multiple failures to be logged. */
int ivtv_firmware_check(struct ivtv *itv, char *where)
{
	int res = 0;

	/* Check encoder is still running */
	if (ivtv_vapi(itv, CX2341X_ENC_PING_FW, 0) < 0) {
		IVTV_WARN("Encoder has died : %s\n", where);
		res = -1;
	}

	/* Also check audio. Only check if not in use & encoder is okay */
	if (!res && !atomic_read(&itv->capturing) &&
	    (!atomic_read(&itv->decoding) ||
	     (atomic_read(&itv->decoding) < 2 && test_bit(IVTV_F_I_DEC_YUV,
							     &itv->i_flags)))) {

		if (ivtv_vapi(itv, CX2341X_ENC_MISC, 1, 12) < 0) {
			IVTV_WARN("Audio has died (Encoder OK) : %s\n", where);
			res = -2;
		}
	}

	if (itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT) {
		/* Second audio check. Skip if audio already failed */
		if (res != -2 && read_dec(0x100) != read_dec(0x104)) {
			/* Wait & try again to be certain. */
			ivtv_msleep_timeout(14, 0);
			if (read_dec(0x100) != read_dec(0x104)) {
				IVTV_WARN("Audio has died (Decoder) : %s\n",
					  where);
				res = -1;
			}
		}

		/* Check decoder is still running */
		if (ivtv_vapi(itv, CX2341X_DEC_PING_FW, 0) < 0) {
			IVTV_WARN("Decoder has died : %s\n", where);
			res = -1;
		}
	}

	/* If something failed & currently idle, try to reload */
	if (res && !atomic_read(&itv->capturing) &&
						!atomic_read(&itv->decoding)) {
		IVTV_INFO("Detected in %s that firmware had failed - Reloading\n",
			  where);
		res = ivtv_firmware_restart(itv);
		/*
		 * Even if restarted ok, still signal a problem had occurred.
		 * The caller can come through this function again to check
		 * if things are really ok after the restart.
		 */
		if (!res) {
			IVTV_INFO("Firmware restart okay\n");
			res = -EAGAIN;
		} else {
			IVTV_INFO("Firmware restart failed\n");
		}
	} else if (res) {
		res = -EIO;
	}

	return res;
}

MODULE_FIRMWARE(CX2341X_FIRM_ENC_FILENAME);
MODULE_FIRMWARE(CX2341X_FIRM_DEC_FILENAME);
MODULE_FIRMWARE(IVTV_DECODE_INIT_MPEG_FILENAME);
