/*
    mailbox functions
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

#include <stdarg.h>

#include "ivtv-driver.h"
#include "ivtv-mailbox.h"

/* Firmware mailbox flags*/
#define IVTV_MBOX_FIRMWARE_DONE 0x00000004
#define IVTV_MBOX_DRIVER_DONE   0x00000002
#define IVTV_MBOX_DRIVER_BUSY   0x00000001
#define IVTV_MBOX_FREE 		0x00000000

/* Firmware mailbox standard timeout */
#define IVTV_API_STD_TIMEOUT 	0x02000000

#define API_CACHE 	 (1 << 0) 	/* Allow the command to be stored in the cache */
#define API_RESULT	 (1 << 1) 	/* Allow 1 second for this cmd to end */
#define API_FAST_RESULT	 (3 << 1)	/* Allow 0.1 second for this cmd to end */
#define API_DMA 	 (1 << 3)	/* DMA mailbox, has special handling */
#define API_HIGH_VOL 	 (1 << 5)	/* High volume command (i.e. called during encoding or decoding) */
#define API_NO_WAIT_MB 	 (1 << 4)	/* Command may not wait for a free mailbox */
#define API_NO_WAIT_RES	 (1 << 5)	/* Command may not wait for the result */
#define API_NO_POLL	 (1 << 6)	/* Avoid pointless polling */

struct ivtv_api_info {
	int flags;		/* Flags, see above */
	const char *name; 	/* The name of the command */
};

#define API_ENTRY(x, f) [x] = { (f), #x }

static const struct ivtv_api_info api_info[256] = {
	/* MPEG encoder API */
	API_ENTRY(CX2341X_ENC_PING_FW, 			API_FAST_RESULT),
	API_ENTRY(CX2341X_ENC_START_CAPTURE, 		API_RESULT | API_NO_POLL),
	API_ENTRY(CX2341X_ENC_STOP_CAPTURE, 		API_RESULT),
	API_ENTRY(CX2341X_ENC_SET_AUDIO_ID, 		API_CACHE),
	API_ENTRY(CX2341X_ENC_SET_VIDEO_ID, 		API_CACHE),
	API_ENTRY(CX2341X_ENC_SET_PCR_ID, 		API_CACHE),
	API_ENTRY(CX2341X_ENC_SET_FRAME_RATE, 		API_CACHE),
	API_ENTRY(CX2341X_ENC_SET_FRAME_SIZE, 		API_CACHE),
	API_ENTRY(CX2341X_ENC_SET_BIT_RATE, 		API_CACHE),
	API_ENTRY(CX2341X_ENC_SET_GOP_PROPERTIES, 	API_CACHE),
	API_ENTRY(CX2341X_ENC_SET_ASPECT_RATIO, 	API_CACHE),
	API_ENTRY(CX2341X_ENC_SET_DNR_FILTER_MODE, 	API_CACHE),
	API_ENTRY(CX2341X_ENC_SET_DNR_FILTER_PROPS, 	API_CACHE),
	API_ENTRY(CX2341X_ENC_SET_CORING_LEVELS, 	API_CACHE),
	API_ENTRY(CX2341X_ENC_SET_SPATIAL_FILTER_TYPE, 	API_CACHE),
	API_ENTRY(CX2341X_ENC_SET_VBI_LINE, 		API_RESULT),
	API_ENTRY(CX2341X_ENC_SET_STREAM_TYPE, 		API_CACHE),
	API_ENTRY(CX2341X_ENC_SET_OUTPUT_PORT, 		API_CACHE),
	API_ENTRY(CX2341X_ENC_SET_AUDIO_PROPERTIES, 	API_CACHE),
	API_ENTRY(CX2341X_ENC_HALT_FW, 			API_FAST_RESULT),
	API_ENTRY(CX2341X_ENC_GET_VERSION, 		API_FAST_RESULT),
	API_ENTRY(CX2341X_ENC_SET_GOP_CLOSURE, 		API_CACHE),
	API_ENTRY(CX2341X_ENC_GET_SEQ_END, 		API_RESULT),
	API_ENTRY(CX2341X_ENC_SET_PGM_INDEX_INFO, 	API_FAST_RESULT),
	API_ENTRY(CX2341X_ENC_SET_VBI_CONFIG, 		API_RESULT),
	API_ENTRY(CX2341X_ENC_SET_DMA_BLOCK_SIZE, 	API_CACHE),
	API_ENTRY(CX2341X_ENC_GET_PREV_DMA_INFO_MB_10, 	API_FAST_RESULT),
	API_ENTRY(CX2341X_ENC_GET_PREV_DMA_INFO_MB_9, 	API_FAST_RESULT),
	API_ENTRY(CX2341X_ENC_SCHED_DMA_TO_HOST, 	API_DMA | API_HIGH_VOL),
	API_ENTRY(CX2341X_ENC_INITIALIZE_INPUT, 	API_RESULT),
	API_ENTRY(CX2341X_ENC_SET_FRAME_DROP_RATE, 	API_CACHE),
	API_ENTRY(CX2341X_ENC_PAUSE_ENCODER, 		API_RESULT),
	API_ENTRY(CX2341X_ENC_REFRESH_INPUT, 		API_NO_WAIT_MB | API_HIGH_VOL),
	API_ENTRY(CX2341X_ENC_SET_COPYRIGHT, 		API_CACHE),
	API_ENTRY(CX2341X_ENC_SET_EVENT_NOTIFICATION, 	API_RESULT),
	API_ENTRY(CX2341X_ENC_SET_NUM_VSYNC_LINES, 	API_CACHE),
	API_ENTRY(CX2341X_ENC_SET_PLACEHOLDER, 		API_CACHE),
	API_ENTRY(CX2341X_ENC_MUTE_VIDEO, 		API_RESULT),
	API_ENTRY(CX2341X_ENC_MUTE_AUDIO, 		API_RESULT),
	API_ENTRY(CX2341X_ENC_SET_VERT_CROP_LINE,	API_FAST_RESULT),
	API_ENTRY(CX2341X_ENC_MISC, 			API_FAST_RESULT),
	/* Obsolete PULLDOWN API command */
	API_ENTRY(0xb1, 				API_CACHE),

	/* MPEG decoder API */
	API_ENTRY(CX2341X_DEC_PING_FW, 			API_FAST_RESULT),
	API_ENTRY(CX2341X_DEC_START_PLAYBACK, 		API_RESULT | API_NO_POLL),
	API_ENTRY(CX2341X_DEC_STOP_PLAYBACK, 		API_RESULT),
	API_ENTRY(CX2341X_DEC_SET_PLAYBACK_SPEED, 	API_RESULT),
	API_ENTRY(CX2341X_DEC_STEP_VIDEO, 		API_RESULT),
	API_ENTRY(CX2341X_DEC_SET_DMA_BLOCK_SIZE, 	API_CACHE),
	API_ENTRY(CX2341X_DEC_GET_XFER_INFO, 		API_FAST_RESULT),
	API_ENTRY(CX2341X_DEC_GET_DMA_STATUS, 		API_FAST_RESULT),
	API_ENTRY(CX2341X_DEC_SCHED_DMA_FROM_HOST, 	API_DMA | API_HIGH_VOL),
	API_ENTRY(CX2341X_DEC_PAUSE_PLAYBACK, 		API_RESULT),
	API_ENTRY(CX2341X_DEC_HALT_FW, 			API_FAST_RESULT),
	API_ENTRY(CX2341X_DEC_SET_STANDARD, 		API_CACHE),
	API_ENTRY(CX2341X_DEC_GET_VERSION, 		API_FAST_RESULT),
	API_ENTRY(CX2341X_DEC_SET_STREAM_INPUT, 	API_CACHE),
	API_ENTRY(CX2341X_DEC_GET_TIMING_INFO, 		API_RESULT /*| API_NO_WAIT_RES*/),
	API_ENTRY(CX2341X_DEC_SET_AUDIO_MODE, 		API_CACHE),
	API_ENTRY(CX2341X_DEC_SET_EVENT_NOTIFICATION, 	API_RESULT),
	API_ENTRY(CX2341X_DEC_SET_DISPLAY_BUFFERS, 	API_CACHE),
	API_ENTRY(CX2341X_DEC_EXTRACT_VBI, 		API_RESULT),
	API_ENTRY(CX2341X_DEC_SET_DECODER_SOURCE, 	API_FAST_RESULT),
	API_ENTRY(CX2341X_DEC_SET_PREBUFFERING, 	API_CACHE),

	/* OSD API */
	API_ENTRY(CX2341X_OSD_GET_FRAMEBUFFER, 		API_FAST_RESULT),
	API_ENTRY(CX2341X_OSD_GET_PIXEL_FORMAT, 	API_FAST_RESULT),
	API_ENTRY(CX2341X_OSD_SET_PIXEL_FORMAT, 	API_CACHE),
	API_ENTRY(CX2341X_OSD_GET_STATE, 		API_FAST_RESULT),
	API_ENTRY(CX2341X_OSD_SET_STATE, 		API_CACHE),
	API_ENTRY(CX2341X_OSD_GET_OSD_COORDS, 		API_FAST_RESULT),
	API_ENTRY(CX2341X_OSD_SET_OSD_COORDS, 		API_CACHE),
	API_ENTRY(CX2341X_OSD_GET_SCREEN_COORDS, 	API_FAST_RESULT),
	API_ENTRY(CX2341X_OSD_SET_SCREEN_COORDS, 	API_CACHE),
	API_ENTRY(CX2341X_OSD_GET_GLOBAL_ALPHA, 	API_FAST_RESULT),
	API_ENTRY(CX2341X_OSD_SET_GLOBAL_ALPHA, 	API_CACHE),
	API_ENTRY(CX2341X_OSD_SET_BLEND_COORDS, 	API_CACHE),
	API_ENTRY(CX2341X_OSD_GET_FLICKER_STATE, 	API_FAST_RESULT),
	API_ENTRY(CX2341X_OSD_SET_FLICKER_STATE, 	API_CACHE),
	API_ENTRY(CX2341X_OSD_BLT_COPY, 		API_RESULT),
	API_ENTRY(CX2341X_OSD_BLT_FILL, 		API_RESULT),
	API_ENTRY(CX2341X_OSD_BLT_TEXT, 		API_RESULT),
	API_ENTRY(CX2341X_OSD_SET_FRAMEBUFFER_WINDOW, 	API_CACHE),
	API_ENTRY(CX2341X_OSD_SET_CHROMA_KEY, 		API_CACHE),
	API_ENTRY(CX2341X_OSD_GET_ALPHA_CONTENT_INDEX, 	API_FAST_RESULT),
	API_ENTRY(CX2341X_OSD_SET_ALPHA_CONTENT_INDEX, 	API_CACHE)
};

static int try_mailbox(struct ivtv *itv, struct ivtv_mailbox_data *mbdata, int mb)
{
	u32 flags = readl(&mbdata->mbox[mb].flags);
	int is_free = flags == IVTV_MBOX_FREE || (flags & IVTV_MBOX_FIRMWARE_DONE);

	/* if the mailbox is free, then try to claim it */
	if (is_free && !test_and_set_bit(mb, &mbdata->busy)) {
		write_sync(IVTV_MBOX_DRIVER_BUSY, &mbdata->mbox[mb].flags);
		return 1;
	}
	return 0;
}

/* Try to find a free mailbox. Note mailbox 0 is reserved for DMA and so is not
   attempted here. */
static int get_mailbox(struct ivtv *itv, struct ivtv_mailbox_data *mbdata, int flags)
{
	unsigned long then = jiffies;
	int i, mb;
	int max_mbox = mbdata->max_mbox;
	int retries = 100;

	/* All slow commands use the same mailbox, serializing them and also
	   leaving the other mailbox free for simple fast commands. */
	if ((flags & API_FAST_RESULT) == API_RESULT)
		max_mbox = 1;

	/* find free non-DMA mailbox */
	for (i = 0; i < retries; i++) {
		for (mb = 1; mb <= max_mbox; mb++)
			if (try_mailbox(itv, mbdata, mb))
				return mb;

		/* Sleep before a retry, if not atomic */
		if (!(flags & API_NO_WAIT_MB)) {
			if (time_after(jiffies,
				       then + msecs_to_jiffies(10*retries)))
			       break;
			ivtv_msleep_timeout(10, 0);
		}
	}
	return -ENODEV;
}

static void write_mailbox(volatile struct ivtv_mailbox __iomem *mbox, int cmd, int args, u32 data[])
{
	int i;

	write_sync(cmd, &mbox->cmd);
	write_sync(IVTV_API_STD_TIMEOUT, &mbox->timeout);

	for (i = 0; i < CX2341X_MBOX_MAX_DATA; i++)
		write_sync(data[i], &mbox->data[i]);

	write_sync(IVTV_MBOX_DRIVER_DONE | IVTV_MBOX_DRIVER_BUSY, &mbox->flags);
}

static void clear_all_mailboxes(struct ivtv *itv, struct ivtv_mailbox_data *mbdata)
{
	int i;

	for (i = 0; i <= mbdata->max_mbox; i++) {
		IVTV_DEBUG_WARN("Clearing mailbox %d: cmd 0x%08x flags 0x%08x\n",
			i, readl(&mbdata->mbox[i].cmd), readl(&mbdata->mbox[i].flags));
		write_sync(0, &mbdata->mbox[i].flags);
		clear_bit(i, &mbdata->busy);
	}
}

static int ivtv_api_call(struct ivtv *itv, int cmd, int args, u32 data[])
{
	struct ivtv_mailbox_data *mbdata = (cmd >= 128) ? &itv->enc_mbox : &itv->dec_mbox;
	volatile struct ivtv_mailbox __iomem *mbox;
	int api_timeout = msecs_to_jiffies(1000);
	int flags, mb, i;
	unsigned long then;

	/* sanity checks */
	if (NULL == mbdata) {
		IVTV_ERR("No mailbox allocated\n");
		return -ENODEV;
	}
	if (args < 0 || args > CX2341X_MBOX_MAX_DATA ||
	    cmd < 0 || cmd > 255 || api_info[cmd].name == NULL) {
		IVTV_ERR("Invalid MB call: cmd = 0x%02x, args = %d\n", cmd, args);
		return -EINVAL;
	}

	if (api_info[cmd].flags & API_HIGH_VOL) {
	    IVTV_DEBUG_HI_MB("MB Call: %s\n", api_info[cmd].name);
	}
	else {
	    IVTV_DEBUG_MB("MB Call: %s\n", api_info[cmd].name);
	}

	/* clear possibly uninitialized part of data array */
	for (i = args; i < CX2341X_MBOX_MAX_DATA; i++)
		data[i] = 0;

	/* If this command was issued within the last 30 minutes and with identical
	   data, then just return 0 as there is no need to issue this command again.
	   Just an optimization to prevent unnecessary use of mailboxes. */
	if (itv->api_cache[cmd].last_jiffies &&
	    time_before(jiffies,
			itv->api_cache[cmd].last_jiffies +
			msecs_to_jiffies(1800000)) &&
	    !memcmp(data, itv->api_cache[cmd].data, sizeof(itv->api_cache[cmd].data))) {
		itv->api_cache[cmd].last_jiffies = jiffies;
		return 0;
	}

	flags = api_info[cmd].flags;

	if (flags & API_DMA) {
		for (i = 0; i < 100; i++) {
			mb = i % (mbdata->max_mbox + 1);
			if (try_mailbox(itv, mbdata, mb)) {
				write_mailbox(&mbdata->mbox[mb], cmd, args, data);
				clear_bit(mb, &mbdata->busy);
				return 0;
			}
			IVTV_DEBUG_WARN("%s: mailbox %d not free %08x\n",
					api_info[cmd].name, mb, readl(&mbdata->mbox[mb].flags));
		}
		IVTV_WARN("Could not find free DMA mailbox for %s\n", api_info[cmd].name);
		clear_all_mailboxes(itv, mbdata);
		return -EBUSY;
	}

	if ((flags & API_FAST_RESULT) == API_FAST_RESULT)
		api_timeout = msecs_to_jiffies(100);

	mb = get_mailbox(itv, mbdata, flags);
	if (mb < 0) {
		IVTV_DEBUG_WARN("No free mailbox found (%s)\n", api_info[cmd].name);
		clear_all_mailboxes(itv, mbdata);
		return -EBUSY;
	}
	mbox = &mbdata->mbox[mb];
	write_mailbox(mbox, cmd, args, data);
	if (flags & API_CACHE) {
		memcpy(itv->api_cache[cmd].data, data, sizeof(itv->api_cache[cmd].data));
		itv->api_cache[cmd].last_jiffies = jiffies;
	}
	if ((flags & API_RESULT) == 0) {
		clear_bit(mb, &mbdata->busy);
		return 0;
	}

	/* Get results */
	then = jiffies;

	if (!(flags & API_NO_POLL)) {
		/* First try to poll, then switch to delays */
		for (i = 0; i < 100; i++) {
			if (readl(&mbox->flags) & IVTV_MBOX_FIRMWARE_DONE)
				break;
		}
	}
	while (!(readl(&mbox->flags) & IVTV_MBOX_FIRMWARE_DONE)) {
		if (time_after(jiffies, then + api_timeout)) {
			IVTV_DEBUG_WARN("Could not get result (%s)\n", api_info[cmd].name);
			/* reset the mailbox, but it is likely too late already */
			write_sync(0, &mbox->flags);
			clear_bit(mb, &mbdata->busy);
			return -EIO;
		}
		if (flags & API_NO_WAIT_RES)
			mdelay(1);
		else
			ivtv_msleep_timeout(1, 0);
	}
	if (time_after(jiffies, then + msecs_to_jiffies(100)))
		IVTV_DEBUG_WARN("%s took %u jiffies\n",
				api_info[cmd].name,
				jiffies_to_msecs(jiffies - then));

	for (i = 0; i < CX2341X_MBOX_MAX_DATA; i++)
		data[i] = readl(&mbox->data[i]);
	write_sync(0, &mbox->flags);
	clear_bit(mb, &mbdata->busy);
	return 0;
}

int ivtv_api(struct ivtv *itv, int cmd, int args, u32 data[])
{
	int res = ivtv_api_call(itv, cmd, args, data);

	/* Allow a single retry, probably already too late though.
	   If there is no free mailbox then that is usually an indication
	   of a more serious problem. */
	return (res == -EBUSY) ? ivtv_api_call(itv, cmd, args, data) : res;
}

int ivtv_api_func(void *priv, u32 cmd, int in, int out, u32 data[CX2341X_MBOX_MAX_DATA])
{
	return ivtv_api(priv, cmd, in, data);
}

int ivtv_vapi_result(struct ivtv *itv, u32 data[CX2341X_MBOX_MAX_DATA], int cmd, int args, ...)
{
	va_list ap;
	int i;

	va_start(ap, args);
	for (i = 0; i < args; i++) {
		data[i] = va_arg(ap, u32);
	}
	va_end(ap);
	return ivtv_api(itv, cmd, args, data);
}

int ivtv_vapi(struct ivtv *itv, int cmd, int args, ...)
{
	u32 data[CX2341X_MBOX_MAX_DATA];
	va_list ap;
	int i;

	va_start(ap, args);
	for (i = 0; i < args; i++) {
		data[i] = va_arg(ap, u32);
	}
	va_end(ap);
	return ivtv_api(itv, cmd, args, data);
}

/* This one is for stuff that can't sleep.. irq handlers, etc.. */
void ivtv_api_get_data(struct ivtv_mailbox_data *mbdata, int mb,
		       int argc, u32 data[])
{
	volatile u32 __iomem *p = mbdata->mbox[mb].data;
	int i;
	for (i = 0; i < argc; i++, p++)
		data[i] = readl(p);
}
