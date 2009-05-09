/*
 * Line6 Linux USB driver - 0.8.0
 *
 * Copyright (C) 2004-2009 Markus Grabner (grabner@icg.tugraz.at)
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation, version 2.
 *
 */

#include "driver.h"

#include "audio.h"
#include "capture.h"
#include "control.h"
#include "playback.h"
#include "pod.h"


#define POD_SYSEX_CODE 3
#define POD_BYTES_PER_FRAME 6  /* 24bit audio (stereo) */


enum {
	POD_SYSEX_CLIP      = 0x0f,
	POD_SYSEX_SAVE      = 0x24,
	POD_SYSEX_SYSTEM    = 0x56,
	POD_SYSEX_SYSTEMREQ = 0x57,
	/* POD_SYSEX_UPDATE    = 0x6c, */  /* software update! */
	POD_SYSEX_STORE     = 0x71,
	POD_SYSEX_FINISH    = 0x72,
	POD_SYSEX_DUMPMEM   = 0x73,
	POD_SYSEX_DUMP      = 0x74,
	POD_SYSEX_DUMPREQ   = 0x75
	/* POD_SYSEX_DUMPMEM2  = 0x76 */   /* dumps entire internal memory of PODxt Pro */
};

enum {
	POD_monitor_level  = 0x04,
	POD_routing        = 0x05,
	POD_tuner_mute     = 0x13,
	POD_tuner_freq     = 0x15,
	POD_tuner_note     = 0x16,
	POD_tuner_pitch    = 0x17,
	POD_system_invalid = 0x7fff
};

enum {
	POD_DUMP_MEMORY = 2
};

enum {
	POD_BUSY_READ,
	POD_BUSY_WRITE,
	POD_CHANNEL_DIRTY,
	POD_SAVE_PRESSED,
	POD_BUSY_MIDISEND
};


static struct snd_ratden pod_ratden = {
	.num_min = 78125,
	.num_max = 78125,
	.num_step = 1,
	.den = 2
};

static struct line6_pcm_properties pod_pcm_properties = {
  .snd_line6_playback_hw = {
		.info = (SNDRV_PCM_INFO_MMAP |
						 SNDRV_PCM_INFO_INTERLEAVED |
						 SNDRV_PCM_INFO_BLOCK_TRANSFER |
						 SNDRV_PCM_INFO_MMAP_VALID |
						 SNDRV_PCM_INFO_PAUSE |
						 SNDRV_PCM_INFO_SYNC_START),
		.formats =          SNDRV_PCM_FMTBIT_S24_3LE,
		.rates =            SNDRV_PCM_RATE_KNOT,
		.rate_min =         39062,
		.rate_max =         39063,
		.channels_min =     2,
		.channels_max =     2,
		.buffer_bytes_max = 60000,
		.period_bytes_min = LINE6_ISO_PACKET_SIZE_MAX * POD_BYTES_PER_FRAME,  /* at least one URB must fit into one period */
		.period_bytes_max = 8192,
		.periods_min =      1,
		.periods_max =      1024
	},
  .snd_line6_capture_hw = {
		.info = (SNDRV_PCM_INFO_MMAP |
						 SNDRV_PCM_INFO_INTERLEAVED |
						 SNDRV_PCM_INFO_BLOCK_TRANSFER |
						 SNDRV_PCM_INFO_MMAP_VALID |
						 SNDRV_PCM_INFO_SYNC_START),
		.formats =          SNDRV_PCM_FMTBIT_S24_3LE,
		.rates =            SNDRV_PCM_RATE_KNOT,
		.rate_min =         39062,
		.rate_max =         39063,
		.channels_min =     2,
		.channels_max =     2,
		.buffer_bytes_max = 60000,
		.period_bytes_min = LINE6_ISO_PACKET_SIZE_MAX * POD_BYTES_PER_FRAME,  /* at least one URB must fit into one period */
		.period_bytes_max = 8192,
		.periods_min =      1,
		.periods_max =      1024
	},
	.snd_line6_rates = {
		.nrats = 1,
		.rats = &pod_ratden
	},
	.bytes_per_frame = POD_BYTES_PER_FRAME
};

static const char pod_request_version[] = { 0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7 };
static const char pod_request_channel[] = { 0xf0, 0x00, 0x01, 0x0c, 0x03, 0x75, 0xf7 };
static const char pod_version_header[]  = { 0xf2, 0x7e, 0x7f, 0x06, 0x02 };


/*
	Mark all parameters as dirty and notify waiting processes.
*/
static void pod_mark_batch_all_dirty(struct usb_line6_pod *pod)
{
	int i;

	for (i = POD_CONTROL_SIZE; i--;)
		set_bit(i, pod->param_dirty);
}

/*
	Send an asynchronous request for the POD firmware version and device ID.
*/
static int pod_version_request_async(struct usb_line6_pod *pod)
{
	return line6_send_raw_message_async(&pod->line6, pod->buffer_versionreq, sizeof(pod_request_version));
}

static void pod_create_files_work(struct work_struct *work)
{
	struct usb_line6_pod *pod = container_of(work, struct usb_line6_pod, create_files_work);

	pod_create_files(pod->firmware_version, pod->line6.properties->device_bit, pod->line6.ifcdev);
}

static void pod_startup_timeout(unsigned long arg)
{
	enum {
		REQUEST_NONE,
		REQUEST_DUMP,
		REQUEST_VERSION
	};

	int request = REQUEST_NONE;
	struct usb_line6_pod *pod = (struct usb_line6_pod *)arg;

	if (pod->dumpreq.ok) {
		if (!pod->versionreq_ok)
			request = REQUEST_VERSION;
	} else {
		if (pod->versionreq_ok)
			request = REQUEST_DUMP;
		else if (pod->startup_count++ & 1)
			request = REQUEST_DUMP;
		else
			request = REQUEST_VERSION;
	}

	switch (request) {
	case REQUEST_DUMP:
		line6_dump_request_async(&pod->dumpreq, &pod->line6, 0);
		break;

	case REQUEST_VERSION:
		pod_version_request_async(pod);
		break;

	default:
		return;
	}

	line6_startup_delayed(&pod->dumpreq, 1, pod_startup_timeout, pod);
}

static char *pod_alloc_sysex_buffer(struct usb_line6_pod *pod, int code, int size)
{
	return line6_alloc_sysex_buffer(&pod->line6, POD_SYSEX_CODE, code, size);
}

/*
	Send channel dump data to the PODxt Pro.
*/
static void pod_dump(struct usb_line6_pod *pod, const unsigned char *data)
{
	int size = 1 + sizeof(pod->prog_data);
	char *sysex = pod_alloc_sysex_buffer(pod, POD_SYSEX_DUMP, size);
	if (!sysex)
		return;
	/* Don't know what this is good for, but PODxt Pro transmits it, so we
	 * also do... */
	sysex[SYSEX_DATA_OFS] = 5;
	memcpy(sysex + SYSEX_DATA_OFS + 1, data, sizeof(pod->prog_data));
	line6_send_sysex_message(&pod->line6, sysex, size);
	memcpy(&pod->prog_data, data, sizeof(pod->prog_data));
	pod_mark_batch_all_dirty(pod);
	kfree(sysex);
}

/*
	Store parameter value in driver memory and mark it as dirty.
*/
static void pod_store_parameter(struct usb_line6_pod *pod, int param, int value)
{
	pod->prog_data.control[param] = value;
	set_bit(param, pod->param_dirty);
	pod->dirty = 1;
}

/*
	Handle SAVE button
*/
static void pod_save_button_pressed(struct usb_line6_pod *pod, int type, int index)
{
	pod->dirty = 0;
	set_bit(POD_SAVE_PRESSED, &pod->atomic_flags);
}

/*
	Process a completely received message.
*/
void pod_process_message(struct usb_line6_pod *pod)
{
	const unsigned char *buf = pod->line6.buffer_message;

	/* filter messages by type */
	switch (buf[0] & 0xf0) {
	case LINE6_PARAM_CHANGE:
	case LINE6_PROGRAM_CHANGE:
	case LINE6_SYSEX_BEGIN:
		break;  /* handle these further down */

	default:
		return;  /* ignore all others */
	}

	/* process all remaining messages */
	switch (buf[0]) {
	case LINE6_PARAM_CHANGE | LINE6_CHANNEL_DEVICE:
		pod_store_parameter(pod, buf[1], buf[2]);
		/* intentionally no break here! */

	case LINE6_PARAM_CHANGE | LINE6_CHANNEL_HOST:
		if ((buf[1] == POD_amp_model_setup) ||
		    (buf[1] == POD_effect_setup))
			/* these also affect other settings */
			line6_dump_request_async(&pod->dumpreq, &pod->line6, 0);

		break;

	case LINE6_PROGRAM_CHANGE | LINE6_CHANNEL_DEVICE:
	case LINE6_PROGRAM_CHANGE | LINE6_CHANNEL_HOST:
		pod->channel_num = buf[1];
		pod->dirty = 0;
		set_bit(POD_CHANNEL_DIRTY, &pod->atomic_flags);
		line6_dump_request_async(&pod->dumpreq, &pod->line6, 0);
		break;

	case LINE6_SYSEX_BEGIN | LINE6_CHANNEL_DEVICE:
	case LINE6_SYSEX_BEGIN | LINE6_CHANNEL_UNKNOWN:
		if (memcmp(buf + 1, line6_midi_id, sizeof(line6_midi_id)) == 0) {
			switch (buf[5]) {
			case POD_SYSEX_DUMP:
				if (pod->line6.message_length == sizeof(pod->prog_data) + 7) {
					switch (pod->dumpreq.in_progress) {
					case LINE6_DUMP_CURRENT:
						memcpy(&pod->prog_data, buf + 7, sizeof(pod->prog_data));
						pod_mark_batch_all_dirty(pod);
						pod->dumpreq.ok = 1;
						break;

					case POD_DUMP_MEMORY:
						memcpy(&pod->prog_data_buf, buf + 7, sizeof(pod->prog_data_buf));
						break;

					default:
						DEBUG_MESSAGES(dev_err(pod->line6.ifcdev, "unknown dump code %02X\n", pod->dumpreq.in_progress));
					}

					line6_dump_finished(&pod->dumpreq);
				} else
					DEBUG_MESSAGES(dev_err(pod->line6.ifcdev, "wrong size of channel dump message (%d instead of %d)\n",
																 pod->line6.message_length, (int)sizeof(pod->prog_data) + 7));

				break;

			case POD_SYSEX_SYSTEM: {
				short value = ((int)buf[7] << 12) | ((int)buf[8] << 8) | ((int)buf[9] << 4) | (int)buf[10];

#define PROCESS_SYSTEM_PARAM(x) \
					case POD_ ## x: \
						pod->x.value = value; \
						wake_up_interruptible(&pod->x.wait); \
						break;

				switch (buf[6]) {
					PROCESS_SYSTEM_PARAM(monitor_level);
					PROCESS_SYSTEM_PARAM(routing);
					PROCESS_SYSTEM_PARAM(tuner_mute);
					PROCESS_SYSTEM_PARAM(tuner_freq);
					PROCESS_SYSTEM_PARAM(tuner_note);
					PROCESS_SYSTEM_PARAM(tuner_pitch);

#undef PROCESS_SYSTEM_PARAM

				default:
					DEBUG_MESSAGES(dev_err(pod->line6.ifcdev, "unknown tuner/system response %02X\n", buf[6]));
				}

				break;
			}

			case POD_SYSEX_FINISH:
				/* do we need to respond to this? */
				break;

			case POD_SYSEX_SAVE:
				pod_save_button_pressed(pod, buf[6], buf[7]);
				break;

			case POD_SYSEX_CLIP:
				DEBUG_MESSAGES(dev_err(pod->line6.ifcdev, "audio clipped\n"));
				pod->clipping.value = 1;
				wake_up_interruptible(&pod->clipping.wait);
				break;

			case POD_SYSEX_STORE:
				DEBUG_MESSAGES(dev_err(pod->line6.ifcdev, "message %02X not yet implemented\n", buf[5]));
				break;

			default:
				DEBUG_MESSAGES(dev_err(pod->line6.ifcdev, "unknown sysex message %02X\n", buf[5]));
			}
		} else if (memcmp(buf, pod_version_header, sizeof(pod_version_header)) == 0) {
			if (pod->versionreq_ok == 0) {
				pod->firmware_version = buf[13] * 100 + buf[14] * 10 + buf[15];
				pod->device_id = ((int)buf[8] << 16) | ((int)buf[9] << 8) | (int)buf[10];
				pod->versionreq_ok = 1;

				/* Now we know the firmware version, so we schedule a bottom half
					 handler to create the special files: */
				INIT_WORK(&pod->create_files_work, pod_create_files_work);
				queue_work(line6_workqueue, &pod->create_files_work);
			} else
				DEBUG_MESSAGES(dev_err(pod->line6.ifcdev, "multiple firmware version message\n"));
		} else
			DEBUG_MESSAGES(dev_err(pod->line6.ifcdev, "unknown sysex header\n"));

		break;

	case LINE6_SYSEX_END:
		break;

	default:
		DEBUG_MESSAGES(dev_err(pod->line6.ifcdev, "POD: unknown message %02X\n", buf[0]));
	}
}

/*
	Detect some cases that require a channel dump after sending a command to the
	device. Important notes:
	*) The actual dump request can not be sent here since we are not allowed to
	wait for the completion of the first message in this context, and sending
	the dump request before completion of the previous message leaves the POD
	in an undefined state. The dump request will be sent when the echoed
	commands are received.
	*) This method fails if a param change message is "chopped" after the first
	byte.
*/
void pod_midi_postprocess(struct usb_line6_pod *pod, unsigned char *data, int length)
{
	int i;

	if (!pod->midi_postprocess)
		return;

	for (i = 0; i < length; ++i) {
		if (data[i] == (LINE6_PROGRAM_CHANGE | LINE6_CHANNEL_HOST)) {
			line6_invalidate_current(&pod->dumpreq);
			break;
		} else if ((data[i] == (LINE6_PARAM_CHANGE | LINE6_CHANNEL_HOST)) && (i < length - 1))
			if ((data[i + 1] == POD_amp_model_setup) || (data[i + 1] == POD_effect_setup)) {
				line6_invalidate_current(&pod->dumpreq);
				break;
			}
	}
}

/*
	Send channel number (i.e., switch to a different sound).
*/
static void pod_send_channel(struct usb_line6_pod *pod, int value)
{
	line6_invalidate_current(&pod->dumpreq);

	if (line6_send_program(&pod->line6, value) == 0)
		pod->channel_num = value;
	else
		line6_dump_finished(&pod->dumpreq);
}

/*
	Transmit PODxt Pro control parameter.
*/
void pod_transmit_parameter(struct usb_line6_pod *pod, int param, int value)
{
	if (line6_transmit_parameter(&pod->line6, param, value) == 0)
		pod_store_parameter(pod, param, value);

	if ((param == POD_amp_model_setup) || (param == POD_effect_setup))  /* these also affect other settings */
		line6_invalidate_current(&pod->dumpreq);
}

/*
	Resolve value to memory location.
*/
static void pod_resolve(const char *buf, short block0, short block1, unsigned char *location)
{
	int value = simple_strtoul(buf, NULL, 10);
	short block = (value < 0x40) ? block0 : block1;
	value &= 0x3f;
	location[0] = block >> 7;
	location[1] = value | (block & 0x7f);
}

/*
	Send command to store channel/effects setup/amp setup to PODxt Pro.
*/
static ssize_t pod_send_store_command(struct device *dev, const char *buf, size_t count, short block0, short block1)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_pod *pod = usb_get_intfdata(interface);

	int size = 3 + sizeof(pod->prog_data_buf);
	char *sysex = pod_alloc_sysex_buffer(pod, POD_SYSEX_STORE, size);
	if (!sysex)
		return 0;

	sysex[SYSEX_DATA_OFS] = 5;  /* see pod_dump() */
	pod_resolve(buf, block0, block1, sysex + SYSEX_DATA_OFS + 1);
	memcpy(sysex + SYSEX_DATA_OFS + 3, &pod->prog_data_buf, sizeof(pod->prog_data_buf));

	line6_send_sysex_message(&pod->line6, sysex, size);
	kfree(sysex);
	/* needs some delay here on AMD64 platform */
	return count;
}

/*
	Send command to retrieve channel/effects setup/amp setup to PODxt Pro.
*/
static ssize_t pod_send_retrieve_command(struct device *dev, const char *buf, size_t count, short block0, short block1)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_pod *pod = usb_get_intfdata(interface);
	int size = 4;
	char *sysex = pod_alloc_sysex_buffer(pod, POD_SYSEX_DUMPMEM, size);

	if (!sysex)
		return 0;

	pod_resolve(buf, block0, block1, sysex + SYSEX_DATA_OFS);
	sysex[SYSEX_DATA_OFS + 2] = 0;
	sysex[SYSEX_DATA_OFS + 3] = 0;
	line6_dump_started(&pod->dumpreq, POD_DUMP_MEMORY);

	if (line6_send_sysex_message(&pod->line6, sysex, size) < size)
		line6_dump_finished(&pod->dumpreq);

	kfree(sysex);
	/* needs some delay here on AMD64 platform */
	return count;
}

/*
	Generic get name function.
*/
static ssize_t get_name_generic(struct usb_line6_pod *pod, const char *str, char *buf)
{
	int length = 0;
	const char *p1;
	char *p2;
	char *last_non_space = buf;

	int retval = line6_wait_dump(&pod->dumpreq, 0);
	if (retval < 0)
		return retval;

	for (p1 = str, p2 = buf; *p1; ++p1, ++p2) {
		*p2 = *p1;
		if (*p2 != ' ')
			last_non_space = p2;
		if (++length == POD_NAME_LENGTH)
			break;
	}

	*(last_non_space + 1) = '\n';
	return last_non_space - buf + 2;
}

/*
	"read" request on "channel" special file.
*/
static ssize_t pod_get_channel(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_pod *pod = usb_get_intfdata(interface);
	return sprintf(buf, "%d\n", pod->channel_num);
}

/*
	"write" request on "channel" special file.
*/
static ssize_t pod_set_channel(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_pod *pod = usb_get_intfdata(interface);
	int value = simple_strtoul(buf, NULL, 10);
	pod_send_channel(pod, value);
	return count;
}

/*
	"read" request on "name" special file.
*/
static ssize_t pod_get_name(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_pod *pod = usb_get_intfdata(interface);
	return get_name_generic(pod, pod->prog_data.header + POD_NAME_OFFSET, buf);
}

/*
	"read" request on "name" special file.
*/
static ssize_t pod_get_name_buf(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_pod *pod = usb_get_intfdata(interface);
	return get_name_generic(pod, pod->prog_data_buf.header + POD_NAME_OFFSET, buf);
}

/*
	"read" request on "dump" special file.
*/
static ssize_t pod_get_dump(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_pod *pod = usb_get_intfdata(interface);
	int retval = line6_wait_dump(&pod->dumpreq, 0);
	if (retval < 0)
		return retval;
	memcpy(buf, &pod->prog_data, sizeof(pod->prog_data));
	return sizeof(pod->prog_data);
}

/*
	"write" request on "dump" special file.
*/
static ssize_t pod_set_dump(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_pod *pod = usb_get_intfdata(interface);

	if (count != sizeof(pod->prog_data)) {
		dev_err(pod->line6.ifcdev,
						"data block must be exactly %d bytes\n",
						(int)sizeof(pod->prog_data));
		return -EINVAL;
	}

	pod_dump(pod, buf);
	return sizeof(pod->prog_data);
}

/*
	Request system parameter.
	@param tuner non-zero, if code refers to a tuner parameter
*/
static ssize_t pod_get_system_param(struct usb_line6_pod *pod, char *buf, int code, struct ValueWait *param, int tuner, int sign)
{
	char *sysex;
	int value;
	static const int size = 1;
	int retval = 0;
	DECLARE_WAITQUEUE(wait, current);

	if (((pod->prog_data.control[POD_tuner] & 0x40) == 0) && tuner)
		return -ENODEV;

	/* send value request to tuner: */
	param->value = POD_system_invalid;
	sysex = pod_alloc_sysex_buffer(pod, POD_SYSEX_SYSTEMREQ, size);
	if (!sysex)
		return 0;
	sysex[SYSEX_DATA_OFS] = code;
	line6_send_sysex_message(&pod->line6, sysex, size);
	kfree(sysex);

	/* wait for tuner to respond: */
	add_wait_queue(&param->wait, &wait);
	current->state = TASK_INTERRUPTIBLE;

	while (param->value == POD_system_invalid) {
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		} else
			schedule();
	}

	current->state = TASK_RUNNING;
	remove_wait_queue(&param->wait, &wait);

	if (retval < 0)
		return retval;

	value = sign ? (int)(signed short)param->value : (int)(unsigned short)param->value;
	return sprintf(buf, "%d\n", value);
}

/*
	Send system parameter.
	@param tuner non-zero, if code refers to a tuner parameter
*/
static ssize_t pod_set_system_param(struct usb_line6_pod *pod, const char *buf,
				    int count, int code, unsigned short mask,
				    int tuner)
{
	char *sysex;
	static const int size = 5;
	unsigned short value;

	if (((pod->prog_data.control[POD_tuner] & 0x40) == 0) && tuner)
		return -EINVAL;

	/* send value to tuner: */
	sysex = pod_alloc_sysex_buffer(pod, POD_SYSEX_SYSTEM, size);
	if (!sysex)
		return 0;
	value = simple_strtoul(buf, NULL, 10) & mask;
	sysex[SYSEX_DATA_OFS] = code;
	sysex[SYSEX_DATA_OFS + 1] = (value >> 12) & 0x0f;
	sysex[SYSEX_DATA_OFS + 2] = (value >>  8) & 0x0f;
	sysex[SYSEX_DATA_OFS + 3] = (value >>  4) & 0x0f;
	sysex[SYSEX_DATA_OFS + 4] = (value      ) & 0x0f;
	line6_send_sysex_message(&pod->line6, sysex, size);
	kfree(sysex);
	return count;
}

/*
	"read" request on "dump_buf" special file.
*/
static ssize_t pod_get_dump_buf(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_pod *pod = usb_get_intfdata(interface);
	int retval = line6_wait_dump(&pod->dumpreq, 0);
	if (retval < 0)
		return retval;
	memcpy(buf, &pod->prog_data_buf, sizeof(pod->prog_data_buf));
	return sizeof(pod->prog_data_buf);
}

/*
	"write" request on "dump_buf" special file.
*/
static ssize_t pod_set_dump_buf(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_pod *pod = usb_get_intfdata(interface);

	if (count != sizeof(pod->prog_data)) {
		dev_err(pod->line6.ifcdev,
						"data block must be exactly %d bytes\n",
						(int)sizeof(pod->prog_data));
		return -EINVAL;
	}

	memcpy(&pod->prog_data_buf, buf, sizeof(pod->prog_data));
	return sizeof(pod->prog_data);
}

/*
	"write" request on "finish" special file.
*/
static ssize_t pod_set_finish(struct device *dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_pod *pod = usb_get_intfdata(interface);
	int size = 0;
	char *sysex = pod_alloc_sysex_buffer(pod, POD_SYSEX_FINISH, size);
	if (!sysex)
		return 0;
	line6_send_sysex_message(&pod->line6, sysex, size);
	kfree(sysex);
	return count;
}

/*
	"write" request on "store_channel" special file.
*/
static ssize_t pod_set_store_channel(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	return pod_send_store_command(dev, buf, count, 0x0000, 0x00c0);
}

/*
	"write" request on "store_effects_setup" special file.
*/
static ssize_t pod_set_store_effects_setup(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	return pod_send_store_command(dev, buf, count, 0x0080, 0x0080);
}

/*
	"write" request on "store_amp_setup" special file.
*/
static ssize_t pod_set_store_amp_setup(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	return pod_send_store_command(dev, buf, count, 0x0040, 0x0100);
}

/*
	"write" request on "retrieve_channel" special file.
*/
static ssize_t pod_set_retrieve_channel(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	return pod_send_retrieve_command(dev, buf, count, 0x0000, 0x00c0);
}

/*
	"write" request on "retrieve_effects_setup" special file.
*/
static ssize_t pod_set_retrieve_effects_setup(struct device *dev,
					      struct device_attribute *attr,
					      const char *buf, size_t count)
{
	return pod_send_retrieve_command(dev, buf, count, 0x0080, 0x0080);
}

/*
	"write" request on "retrieve_amp_setup" special file.
*/
static ssize_t pod_set_retrieve_amp_setup(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	return pod_send_retrieve_command(dev, buf, count, 0x0040, 0x0100);
}

/*
	"read" request on "dirty" special file.
*/
static ssize_t pod_get_dirty(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_pod *pod = usb_get_intfdata(interface);
	buf[0] = pod->dirty ? '1' : '0';
	buf[1] = '\n';
	return 2;
}

/*
	"read" request on "midi_postprocess" special file.
*/
static ssize_t pod_get_midi_postprocess(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_pod *pod = usb_get_intfdata(interface);
	return sprintf(buf, "%d\n", pod->midi_postprocess);
}

/*
	"write" request on "midi_postprocess" special file.
*/
static ssize_t pod_set_midi_postprocess(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_pod *pod = usb_get_intfdata(interface);
	int value = simple_strtoul(buf, NULL, 10);
	pod->midi_postprocess = value ? 1 : 0;
	return count;
}

/*
	"read" request on "serial_number" special file.
*/
static ssize_t pod_get_serial_number(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_pod *pod = usb_get_intfdata(interface);
	return sprintf(buf, "%d\n", pod->serial_number);
}

/*
	"read" request on "firmware_version" special file.
*/
static ssize_t pod_get_firmware_version(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_pod *pod = usb_get_intfdata(interface);
	return sprintf(buf, "%d.%02d\n", pod->firmware_version / 100,
		       pod->firmware_version % 100);
}

/*
	"read" request on "device_id" special file.
*/
static ssize_t pod_get_device_id(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_pod *pod = usb_get_intfdata(interface);
	return sprintf(buf, "%d\n", pod->device_id);
}

/*
	"read" request on "clip" special file.
*/
static ssize_t pod_wait_for_clip(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct usb_interface *interface = to_usb_interface(dev);
	struct usb_line6_pod *pod = usb_get_intfdata(interface);
	int err = 0;
	DECLARE_WAITQUEUE(wait, current);
	pod->clipping.value = 0;
	add_wait_queue(&pod->clipping.wait, &wait);
	current->state = TASK_INTERRUPTIBLE;

	while (pod->clipping.value == 0) {
		if (signal_pending(current)) {
			err = -ERESTARTSYS;
			break;
		} else
			schedule();
	}

	current->state = TASK_RUNNING;
	remove_wait_queue(&pod->clipping.wait, &wait);
	return err;
}

#define POD_GET_SYSTEM_PARAM(code, tuner, sign) \
static ssize_t pod_get_ ## code(struct device *dev, \
				struct device_attribute *attr, char *buf) \
{ \
	struct usb_interface *interface = to_usb_interface(dev); \
	struct usb_line6_pod *pod = usb_get_intfdata(interface); \
	return pod_get_system_param(pod, buf, POD_ ## code, &pod->code, \
				    tuner, sign); \
}

#define POD_GET_SET_SYSTEM_PARAM(code, mask, tuner, sign) \
POD_GET_SYSTEM_PARAM(code, tuner, sign) \
static ssize_t pod_set_ ## code(struct device *dev, \
				struct device_attribute *attr, \
				const char *buf, size_t count) \
{ \
	struct usb_interface *interface = to_usb_interface(dev); \
	struct usb_line6_pod *pod = usb_get_intfdata(interface); \
	return pod_set_system_param(pod, buf, count, POD_ ## code, mask, \
				    tuner); \
}

POD_GET_SET_SYSTEM_PARAM(monitor_level, 0xffff, 0, 0);
POD_GET_SET_SYSTEM_PARAM(routing, 0x0003, 0, 0);
POD_GET_SET_SYSTEM_PARAM(tuner_mute, 0x0001, 1, 0);
POD_GET_SET_SYSTEM_PARAM(tuner_freq, 0xffff, 1, 0);
POD_GET_SYSTEM_PARAM(tuner_note, 1, 1);
POD_GET_SYSTEM_PARAM(tuner_pitch, 1, 1);

#undef GET_SET_SYSTEM_PARAM
#undef GET_SYSTEM_PARAM

/* POD special files: */
static DEVICE_ATTR(channel, S_IWUGO | S_IRUGO, pod_get_channel, pod_set_channel);
static DEVICE_ATTR(clip, S_IRUGO, pod_wait_for_clip, line6_nop_write);
static DEVICE_ATTR(device_id, S_IRUGO, pod_get_device_id, line6_nop_write);
static DEVICE_ATTR(dirty, S_IRUGO, pod_get_dirty, line6_nop_write);
static DEVICE_ATTR(dump, S_IWUGO | S_IRUGO, pod_get_dump, pod_set_dump);
static DEVICE_ATTR(dump_buf, S_IWUGO | S_IRUGO, pod_get_dump_buf, pod_set_dump_buf);
static DEVICE_ATTR(finish, S_IWUGO, line6_nop_read, pod_set_finish);
static DEVICE_ATTR(firmware_version, S_IRUGO, pod_get_firmware_version, line6_nop_write);
static DEVICE_ATTR(midi_postprocess, S_IWUGO | S_IRUGO, pod_get_midi_postprocess, pod_set_midi_postprocess);
static DEVICE_ATTR(monitor_level, S_IWUGO | S_IRUGO, pod_get_monitor_level, pod_set_monitor_level);
static DEVICE_ATTR(name, S_IRUGO, pod_get_name, line6_nop_write);
static DEVICE_ATTR(name_buf, S_IRUGO, pod_get_name_buf, line6_nop_write);
static DEVICE_ATTR(retrieve_amp_setup, S_IWUGO, line6_nop_read, pod_set_retrieve_amp_setup);
static DEVICE_ATTR(retrieve_channel, S_IWUGO, line6_nop_read, pod_set_retrieve_channel);
static DEVICE_ATTR(retrieve_effects_setup, S_IWUGO, line6_nop_read, pod_set_retrieve_effects_setup);
static DEVICE_ATTR(routing, S_IWUGO | S_IRUGO, pod_get_routing, pod_set_routing);
static DEVICE_ATTR(serial_number, S_IRUGO, pod_get_serial_number, line6_nop_write);
static DEVICE_ATTR(store_amp_setup, S_IWUGO, line6_nop_read, pod_set_store_amp_setup);
static DEVICE_ATTR(store_channel, S_IWUGO, line6_nop_read, pod_set_store_channel);
static DEVICE_ATTR(store_effects_setup, S_IWUGO, line6_nop_read, pod_set_store_effects_setup);
static DEVICE_ATTR(tuner_freq, S_IWUGO | S_IRUGO, pod_get_tuner_freq, pod_set_tuner_freq);
static DEVICE_ATTR(tuner_mute, S_IWUGO | S_IRUGO, pod_get_tuner_mute, pod_set_tuner_mute);
static DEVICE_ATTR(tuner_note, S_IRUGO, pod_get_tuner_note, line6_nop_write);
static DEVICE_ATTR(tuner_pitch, S_IRUGO, pod_get_tuner_pitch, line6_nop_write);

#if CREATE_RAW_FILE
static DEVICE_ATTR(raw, S_IWUGO, line6_nop_read, line6_set_raw);
#endif

/*
	POD destructor.
*/
static void pod_destruct(struct usb_interface *interface)
{
	struct usb_line6_pod *pod = usb_get_intfdata(interface);
	struct usb_line6 *line6;

	if (pod == NULL)
		return;
	line6 = &pod->line6;
	if (line6 == NULL)
		return;
	line6_cleanup_audio(line6);

	/* free dump request data: */
	line6_dumpreq_destruct(&pod->dumpreq);

	kfree(pod->buffer_versionreq);
}

/*
	Create sysfs entries.
*/
static int pod_create_files2(struct device *dev)
{
	int err;

	CHECK_RETURN(device_create_file(dev, &dev_attr_channel));
	CHECK_RETURN(device_create_file(dev, &dev_attr_clip));
	CHECK_RETURN(device_create_file(dev, &dev_attr_device_id));
	CHECK_RETURN(device_create_file(dev, &dev_attr_dirty));
	CHECK_RETURN(device_create_file(dev, &dev_attr_dump));
	CHECK_RETURN(device_create_file(dev, &dev_attr_dump_buf));
	CHECK_RETURN(device_create_file(dev, &dev_attr_finish));
	CHECK_RETURN(device_create_file(dev, &dev_attr_firmware_version));
	CHECK_RETURN(device_create_file(dev, &dev_attr_midi_postprocess));
	CHECK_RETURN(device_create_file(dev, &dev_attr_monitor_level));
	CHECK_RETURN(device_create_file(dev, &dev_attr_name));
	CHECK_RETURN(device_create_file(dev, &dev_attr_name_buf));
	CHECK_RETURN(device_create_file(dev, &dev_attr_retrieve_amp_setup));
	CHECK_RETURN(device_create_file(dev, &dev_attr_retrieve_channel));
	CHECK_RETURN(device_create_file(dev, &dev_attr_retrieve_effects_setup));
	CHECK_RETURN(device_create_file(dev, &dev_attr_routing));
	CHECK_RETURN(device_create_file(dev, &dev_attr_serial_number));
	CHECK_RETURN(device_create_file(dev, &dev_attr_store_amp_setup));
	CHECK_RETURN(device_create_file(dev, &dev_attr_store_channel));
	CHECK_RETURN(device_create_file(dev, &dev_attr_store_effects_setup));
	CHECK_RETURN(device_create_file(dev, &dev_attr_tuner_freq));
	CHECK_RETURN(device_create_file(dev, &dev_attr_tuner_mute));
	CHECK_RETURN(device_create_file(dev, &dev_attr_tuner_note));
	CHECK_RETURN(device_create_file(dev, &dev_attr_tuner_pitch));

#if CREATE_RAW_FILE
	CHECK_RETURN(device_create_file(dev, &dev_attr_raw));
#endif

	return 0;
}

/*
	 Init POD device.
*/
int pod_init(struct usb_interface *interface, struct usb_line6_pod *pod)
{
	int err;
	struct usb_line6 *line6 = &pod->line6;

	if ((interface == NULL) || (pod == NULL))
		return -ENODEV;

	pod->channel_num = 255;

	/* initialize wait queues: */
	init_waitqueue_head(&pod->monitor_level.wait);
	init_waitqueue_head(&pod->routing.wait);
	init_waitqueue_head(&pod->tuner_mute.wait);
	init_waitqueue_head(&pod->tuner_freq.wait);
	init_waitqueue_head(&pod->tuner_note.wait);
	init_waitqueue_head(&pod->tuner_pitch.wait);
	init_waitqueue_head(&pod->clipping.wait);

	memset(pod->param_dirty, 0xff, sizeof(pod->param_dirty));

	/* initialize USB buffers: */
	err = line6_dumpreq_init(&pod->dumpreq, pod_request_channel,
				 sizeof(pod_request_channel));
	if (err < 0) {
		dev_err(&interface->dev, "Out of memory\n");
		pod_destruct(interface);
		return -ENOMEM;
	}

	pod->buffer_versionreq = kmalloc(sizeof(pod_request_version),
					 GFP_KERNEL);

	if (pod->buffer_versionreq == NULL) {
		dev_err(&interface->dev, "Out of memory\n");
		pod_destruct(interface);
		return -ENOMEM;
	}

	memcpy(pod->buffer_versionreq, pod_request_version,
	       sizeof(pod_request_version));

	/* create sysfs entries: */
	err = pod_create_files2(&interface->dev);
	if (err < 0) {
		pod_destruct(interface);
		return err;
	}

	/* initialize audio system: */
	err = line6_init_audio(line6);
	if (err < 0) {
		pod_destruct(interface);
		return err;
	}

	/* initialize MIDI subsystem: */
	err = line6_init_midi(line6);
	if (err < 0) {
		pod_destruct(interface);
		return err;
	}

	/* initialize PCM subsystem: */
	err = line6_init_pcm(line6, &pod_pcm_properties);
	if (err < 0) {
		pod_destruct(interface);
		return err;
	}

	/* register audio system: */
	err = line6_register_audio(line6);
	if (err < 0) {
		pod_destruct(interface);
		return err;
	}

	if (pod->line6.properties->capabilities & LINE6_BIT_CONTROL) {
		/* query some data: */
		line6_startup_delayed(&pod->dumpreq, POD_STARTUP_DELAY,
				      pod_startup_timeout, pod);
		line6_read_serial_number(&pod->line6, &pod->serial_number);
	}

	return 0;
}

/*
	POD device disconnected.
*/
void pod_disconnect(struct usb_interface *interface)
{
	struct usb_line6_pod *pod;

	if (interface == NULL)
		return;
	pod = usb_get_intfdata(interface);

	if (pod != NULL) {
		struct snd_line6_pcm *line6pcm = pod->line6.line6pcm;
		struct device *dev = &interface->dev;

		if (line6pcm != NULL) {
			unlink_wait_clear_audio_out_urbs(line6pcm);
			unlink_wait_clear_audio_in_urbs(line6pcm);
		}

		if (dev != NULL) {
			/* remove sysfs entries: */
			if (pod->versionreq_ok)
				pod_remove_files(pod->firmware_version, pod->line6.properties->device_bit, dev);

			device_remove_file(dev, &dev_attr_channel);
			device_remove_file(dev, &dev_attr_clip);
			device_remove_file(dev, &dev_attr_device_id);
			device_remove_file(dev, &dev_attr_dirty);
			device_remove_file(dev, &dev_attr_dump);
			device_remove_file(dev, &dev_attr_dump_buf);
			device_remove_file(dev, &dev_attr_finish);
			device_remove_file(dev, &dev_attr_firmware_version);
			device_remove_file(dev, &dev_attr_midi_postprocess);
			device_remove_file(dev, &dev_attr_monitor_level);
			device_remove_file(dev, &dev_attr_name);
			device_remove_file(dev, &dev_attr_name_buf);
			device_remove_file(dev, &dev_attr_retrieve_amp_setup);
			device_remove_file(dev, &dev_attr_retrieve_channel);
			device_remove_file(dev, &dev_attr_retrieve_effects_setup);
			device_remove_file(dev, &dev_attr_routing);
			device_remove_file(dev, &dev_attr_serial_number);
			device_remove_file(dev, &dev_attr_store_amp_setup);
			device_remove_file(dev, &dev_attr_store_channel);
			device_remove_file(dev, &dev_attr_store_effects_setup);
			device_remove_file(dev, &dev_attr_tuner_freq);
			device_remove_file(dev, &dev_attr_tuner_mute);
			device_remove_file(dev, &dev_attr_tuner_note);
			device_remove_file(dev, &dev_attr_tuner_pitch);

#if CREATE_RAW_FILE
			device_remove_file(dev, &dev_attr_raw);
#endif
		}
	}

	pod_destruct(interface);
}
