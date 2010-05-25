/*
 *  HID driver for the Prodikeys PC-MIDI Keyboard
 *  providing midi & extra multimedia keys functionality
 *
 *  Copyright (c) 2009 Don Prince <dhprince.devel@yahoo.co.uk>
 *
 *  Controls for Octave Shift Up/Down, Channel, and
 *  Sustain Duration available via sysfs.
 *
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/mutex.h>
#include <linux/hid.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/rawmidi.h>
#include "usbhid/usbhid.h"
#include "hid-ids.h"


#define pk_debug(format, arg...) \
	pr_debug("hid-prodikeys: " format "\n" , ## arg)
#define pk_error(format, arg...) \
	pr_err("hid-prodikeys: " format "\n" , ## arg)

struct pcmidi_snd;

struct pk_device {
	unsigned long		quirks;

	struct hid_device	*hdev;
	struct pcmidi_snd	*pm; /* pcmidi device context */
};

struct pcmidi_snd;

struct pcmidi_sustain {
	unsigned long		in_use;
	struct pcmidi_snd	*pm;
	struct timer_list	timer;
	unsigned char		status;
	unsigned char		note;
	unsigned char		velocity;
};

#define PCMIDI_SUSTAINED_MAX	32
struct pcmidi_snd {
	struct pk_device		*pk;
	unsigned short			ifnum;
	struct hid_report		*pcmidi_report6;
	struct input_dev		*input_ep82;
	unsigned short			midi_mode;
	unsigned short			midi_sustain_mode;
	unsigned short			midi_sustain;
	unsigned short			midi_channel;
	short				midi_octave;
	struct pcmidi_sustain		sustained_notes[PCMIDI_SUSTAINED_MAX];
	unsigned short			fn_state;
	unsigned short			last_key[24];
	spinlock_t			rawmidi_in_lock;
	struct snd_card			*card;
	struct snd_rawmidi		*rwmidi;
	struct snd_rawmidi_substream	*in_substream;
	struct snd_rawmidi_substream	*out_substream;
	unsigned long			in_triggered;
	unsigned long			out_active;
};

#define PK_QUIRK_NOGET	0x00010000
#define PCMIDI_MIDDLE_C 60
#define PCMIDI_CHANNEL_MIN 0
#define PCMIDI_CHANNEL_MAX 15
#define PCMIDI_OCTAVE_MIN (-2)
#define PCMIDI_OCTAVE_MAX 2
#define PCMIDI_SUSTAIN_MIN 0
#define PCMIDI_SUSTAIN_MAX 5000

static const char shortname[] = "PC-MIDI";
static const char longname[] = "Prodikeys PC-MIDI Keyboard";

static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static int enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;

module_param_array(index, int, NULL, 0444);
module_param_array(id, charp, NULL, 0444);
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for the PC-MIDI virtual audio driver");
MODULE_PARM_DESC(id, "ID string for the PC-MIDI virtual audio driver");
MODULE_PARM_DESC(enable, "Enable for the PC-MIDI virtual audio driver");


/* Output routine for the sysfs channel file */
static ssize_t show_channel(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct pk_device *pk = (struct pk_device *)hid_get_drvdata(hdev);

	dbg_hid("pcmidi sysfs read channel=%u\n", pk->pm->midi_channel);

	return sprintf(buf, "%u (min:%u, max:%u)\n", pk->pm->midi_channel,
		PCMIDI_CHANNEL_MIN, PCMIDI_CHANNEL_MAX);
}

/* Input routine for the sysfs channel file */
static ssize_t store_channel(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct pk_device *pk = (struct pk_device *)hid_get_drvdata(hdev);

	unsigned channel = 0;

	if (sscanf(buf, "%u", &channel) > 0 && channel <= PCMIDI_CHANNEL_MAX) {
		dbg_hid("pcmidi sysfs write channel=%u\n", channel);
		pk->pm->midi_channel = channel;
		return strlen(buf);
	}
	return -EINVAL;
}

static DEVICE_ATTR(channel, S_IRUGO | S_IWUGO, show_channel,
		store_channel);

static struct device_attribute *sysfs_device_attr_channel = {
		&dev_attr_channel,
		};

/* Output routine for the sysfs sustain file */
static ssize_t show_sustain(struct device *dev,
 struct device_attribute *attr, char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct pk_device *pk = (struct pk_device *)hid_get_drvdata(hdev);

	dbg_hid("pcmidi sysfs read sustain=%u\n", pk->pm->midi_sustain);

	return sprintf(buf, "%u (off:%u, max:%u (ms))\n", pk->pm->midi_sustain,
		PCMIDI_SUSTAIN_MIN, PCMIDI_SUSTAIN_MAX);
}

/* Input routine for the sysfs sustain file */
static ssize_t store_sustain(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct pk_device *pk = (struct pk_device *)hid_get_drvdata(hdev);

	unsigned sustain = 0;

	if (sscanf(buf, "%u", &sustain) > 0 && sustain <= PCMIDI_SUSTAIN_MAX) {
		dbg_hid("pcmidi sysfs write sustain=%u\n", sustain);
		pk->pm->midi_sustain = sustain;
		pk->pm->midi_sustain_mode =
			(0 == sustain || !pk->pm->midi_mode) ? 0 : 1;
		return strlen(buf);
	}
	return -EINVAL;
}

static DEVICE_ATTR(sustain, S_IRUGO | S_IWUGO, show_sustain,
		store_sustain);

static struct device_attribute *sysfs_device_attr_sustain = {
		&dev_attr_sustain,
		};

/* Output routine for the sysfs octave file */
static ssize_t show_octave(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct pk_device *pk = (struct pk_device *)hid_get_drvdata(hdev);

	dbg_hid("pcmidi sysfs read octave=%d\n", pk->pm->midi_octave);

	return sprintf(buf, "%d (min:%d, max:%d)\n", pk->pm->midi_octave,
		PCMIDI_OCTAVE_MIN, PCMIDI_OCTAVE_MAX);
}

/* Input routine for the sysfs octave file */
static ssize_t store_octave(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	struct hid_device *hdev = container_of(dev, struct hid_device, dev);
	struct pk_device *pk = (struct pk_device *)hid_get_drvdata(hdev);

	int octave = 0;

	if (sscanf(buf, "%d", &octave) > 0 &&
		octave >= PCMIDI_OCTAVE_MIN && octave <= PCMIDI_OCTAVE_MAX) {
		dbg_hid("pcmidi sysfs write octave=%d\n", octave);
		pk->pm->midi_octave = octave;
		return strlen(buf);
	}
	return -EINVAL;
}

static DEVICE_ATTR(octave, S_IRUGO | S_IWUGO, show_octave,
		store_octave);

static struct device_attribute *sysfs_device_attr_octave = {
		&dev_attr_octave,
		};


static void pcmidi_send_note(struct pcmidi_snd *pm,
	unsigned char status, unsigned char note, unsigned char velocity)
{
	unsigned long flags;
	unsigned char buffer[3];

	buffer[0] = status;
	buffer[1] = note;
	buffer[2] = velocity;

	spin_lock_irqsave(&pm->rawmidi_in_lock, flags);

	if (!pm->in_substream)
		goto drop_note;
	if (!test_bit(pm->in_substream->number, &pm->in_triggered))
		goto drop_note;

	snd_rawmidi_receive(pm->in_substream, buffer, 3);

drop_note:
	spin_unlock_irqrestore(&pm->rawmidi_in_lock, flags);

	return;
}

void pcmidi_sustained_note_release(unsigned long data)
{
	struct pcmidi_sustain *pms = (struct pcmidi_sustain *)data;

	pcmidi_send_note(pms->pm, pms->status, pms->note, pms->velocity);
	pms->in_use = 0;
}

void init_sustain_timers(struct pcmidi_snd *pm)
{
	struct pcmidi_sustain *pms;
	unsigned i;

	for (i = 0; i < PCMIDI_SUSTAINED_MAX; i++) {
		pms = &pm->sustained_notes[i];
		pms->in_use = 0;
		pms->pm = pm;
		setup_timer(&pms->timer, pcmidi_sustained_note_release,
			(unsigned long)pms);
	}
}

void stop_sustain_timers(struct pcmidi_snd *pm)
{
	struct pcmidi_sustain *pms;
	unsigned i;

	for (i = 0; i < PCMIDI_SUSTAINED_MAX; i++) {
		pms = &pm->sustained_notes[i];
		pms->in_use = 1;
		del_timer_sync(&pms->timer);
	}
}

static int pcmidi_get_output_report(struct pcmidi_snd *pm)
{
	struct hid_device *hdev = pm->pk->hdev;
	struct hid_report *report;

	list_for_each_entry(report,
		&hdev->report_enum[HID_OUTPUT_REPORT].report_list, list) {
		if (!(6 == report->id))
			continue;

		if (report->maxfield < 1) {
			dev_err(&hdev->dev, "output report is empty\n");
			break;
		}
		if (report->field[0]->report_count != 2) {
			dev_err(&hdev->dev, "field count too low\n");
			break;
		}
		pm->pcmidi_report6 = report;
		return 0;
	}
	/* should never get here */
	return -ENODEV;
}

static void pcmidi_submit_output_report(struct pcmidi_snd *pm, int state)
{
	struct hid_device *hdev = pm->pk->hdev;
	struct hid_report *report = pm->pcmidi_report6;
	report->field[0]->value[0] = 0x01;
	report->field[0]->value[1] = state;

	usbhid_submit_report(hdev, report, USB_DIR_OUT);
}

static int pcmidi_handle_report1(struct pcmidi_snd *pm, u8 *data)
{
	u32 bit_mask;

	bit_mask = data[1];
	bit_mask = (bit_mask << 8) | data[2];
	bit_mask = (bit_mask << 8) | data[3];

	dbg_hid("pcmidi mode: %d\n", pm->midi_mode);

	/*KEY_MAIL or octave down*/
	if (pm->midi_mode && bit_mask == 0x004000) {
		/* octave down */
		pm->midi_octave--;
		if (pm->midi_octave < -2)
			pm->midi_octave = -2;
		dbg_hid("pcmidi mode: %d octave: %d\n",
			pm->midi_mode, pm->midi_octave);
		return 1;
	}
	/*KEY_WWW or sustain*/
	else if (pm->midi_mode && bit_mask == 0x000004) {
		/* sustain on/off*/
		pm->midi_sustain_mode ^= 0x1;
		return 1;
	}

	return 0; /* continue key processing */
}

static int pcmidi_handle_report3(struct pcmidi_snd *pm, u8 *data, int size)
{
	struct pcmidi_sustain *pms;
	unsigned i, j;
	unsigned char status, note, velocity;

	unsigned num_notes = (size-1)/2;
	for (j = 0; j < num_notes; j++)	{
		note = data[j*2+1];
		velocity = data[j*2+2];

		if (note < 0x81) { /* note on */
			status = 128 + 16 + pm->midi_channel; /* 1001nnnn */
			note = note - 0x54 + PCMIDI_MIDDLE_C +
				(pm->midi_octave * 12);
			if (0 == velocity)
				velocity = 1; /* force note on */
		} else { /* note off */
			status = 128 + pm->midi_channel; /* 1000nnnn */
			note = note - 0x94 + PCMIDI_MIDDLE_C +
				(pm->midi_octave*12);

			if (pm->midi_sustain_mode) {
				for (i = 0; i < PCMIDI_SUSTAINED_MAX; i++) {
					pms = &pm->sustained_notes[i];
					if (!pms->in_use) {
						pms->status = status;
						pms->note = note;
						pms->velocity = velocity;
						pms->in_use = 1;

						mod_timer(&pms->timer,
							jiffies +
					msecs_to_jiffies(pm->midi_sustain));
						return 1;
					}
				}
			}
		}
		pcmidi_send_note(pm, status, note, velocity);
	}

	return 1;
}

static int pcmidi_handle_report4(struct pcmidi_snd *pm, u8 *data)
{
	unsigned	key;
	u32		bit_mask;
	u32		bit_index;

	bit_mask = data[1];
	bit_mask = (bit_mask << 8) | data[2];
	bit_mask = (bit_mask << 8) | data[3];

	/* break keys */
	for (bit_index = 0; bit_index < 24; bit_index++) {
		key = pm->last_key[bit_index];
		if (!((0x01 << bit_index) & bit_mask)) {
			input_event(pm->input_ep82, EV_KEY,
				pm->last_key[bit_index], 0);
				pm->last_key[bit_index] = 0;
		}
	}

	/* make keys */
	for (bit_index = 0; bit_index < 24; bit_index++) {
		key = 0;
		switch ((0x01 << bit_index) & bit_mask) {
		case 0x000010: /* Fn lock*/
			pm->fn_state ^= 0x000010;
			if (pm->fn_state)
				pcmidi_submit_output_report(pm, 0xc5);
			else
				pcmidi_submit_output_report(pm, 0xc6);
			continue;
		case 0x020000: /* midi launcher..send a key (qwerty) or not? */
			pcmidi_submit_output_report(pm, 0xc1);
			pm->midi_mode ^= 0x01;

			dbg_hid("pcmidi mode: %d\n", pm->midi_mode);
			continue;
		case 0x100000: /* KEY_MESSENGER or octave up */
			dbg_hid("pcmidi mode: %d\n", pm->midi_mode);
			if (pm->midi_mode) {
				pm->midi_octave++;
				if (pm->midi_octave > 2)
					pm->midi_octave = 2;
				dbg_hid("pcmidi mode: %d octave: %d\n",
					pm->midi_mode, pm->midi_octave);
			    continue;
			} else
				key = KEY_MESSENGER;
			break;
		case 0x400000:
			key = KEY_CALENDAR;
			break;
		case 0x080000:
			key = KEY_ADDRESSBOOK;
			break;
		case 0x040000:
			key = KEY_DOCUMENTS;
			break;
		case 0x800000:
			key = KEY_WORDPROCESSOR;
			break;
		case 0x200000:
			key = KEY_SPREADSHEET;
			break;
		case 0x010000:
			key = KEY_COFFEE;
			break;
		case 0x000100:
			key = KEY_HELP;
			break;
		case 0x000200:
			key = KEY_SEND;
			break;
		case 0x000400:
			key = KEY_REPLY;
			break;
		case 0x000800:
			key = KEY_FORWARDMAIL;
			break;
		case 0x001000:
			key = KEY_NEW;
			break;
		case 0x002000:
			key = KEY_OPEN;
			break;
		case 0x004000:
			key = KEY_CLOSE;
			break;
		case 0x008000:
			key = KEY_SAVE;
			break;
		case 0x000001:
			key = KEY_UNDO;
			break;
		case 0x000002:
			key = KEY_REDO;
			break;
		case 0x000004:
			key = KEY_SPELLCHECK;
			break;
		case 0x000008:
			key = KEY_PRINT;
			break;
		}
		if (key) {
			input_event(pm->input_ep82, EV_KEY, key, 1);
			pm->last_key[bit_index] = key;
		}
	}

	return 1;
}

int pcmidi_handle_report(
	struct pcmidi_snd *pm, unsigned report_id, u8 *data, int size)
{
	int ret = 0;

	switch (report_id) {
	case 0x01: /* midi keys (qwerty)*/
		ret = pcmidi_handle_report1(pm, data);
		break;
	case 0x03: /* midi keyboard (musical)*/
		ret = pcmidi_handle_report3(pm, data, size);
		break;
	case 0x04: /* multimedia/midi keys (qwerty)*/
		ret = pcmidi_handle_report4(pm, data);
		break;
	}
	return ret;
}

void pcmidi_setup_extra_keys(struct pcmidi_snd *pm, struct input_dev *input)
{
	/* reassigned functionality for N/A keys
		MY PICTURES =>	KEY_WORDPROCESSOR
		MY MUSIC=>	KEY_SPREADSHEET
	*/
	unsigned int keys[] = {
		KEY_FN,
		KEY_MESSENGER, KEY_CALENDAR,
		KEY_ADDRESSBOOK, KEY_DOCUMENTS,
		KEY_WORDPROCESSOR,
		KEY_SPREADSHEET,
		KEY_COFFEE,
		KEY_HELP, KEY_SEND,
		KEY_REPLY, KEY_FORWARDMAIL,
		KEY_NEW, KEY_OPEN,
		KEY_CLOSE, KEY_SAVE,
		KEY_UNDO, KEY_REDO,
		KEY_SPELLCHECK,	KEY_PRINT,
		0
	};

	unsigned int *pkeys = &keys[0];
	unsigned short i;

	if (pm->ifnum != 1)  /* only set up ONCE for interace 1 */
		return;

	pm->input_ep82 = input;

	for (i = 0; i < 24; i++)
		pm->last_key[i] = 0;

	while (*pkeys != 0) {
		set_bit(*pkeys, pm->input_ep82->keybit);
		++pkeys;
	}
}

static int pcmidi_set_operational(struct pcmidi_snd *pm)
{
	if (pm->ifnum != 1)
		return 0; /* only set up ONCE for interace 1 */

	pcmidi_get_output_report(pm);
	pcmidi_submit_output_report(pm, 0xc1);
	return 0;
}

static int pcmidi_snd_free(struct snd_device *dev)
{
	return 0;
}

static int pcmidi_in_open(struct snd_rawmidi_substream *substream)
{
	struct pcmidi_snd *pm = substream->rmidi->private_data;

	dbg_hid("pcmidi in open\n");
	pm->in_substream = substream;
	return 0;
}

static int pcmidi_in_close(struct snd_rawmidi_substream *substream)
{
	dbg_hid("pcmidi in close\n");
	return 0;
}

static void pcmidi_in_trigger(struct snd_rawmidi_substream *substream, int up)
{
	struct pcmidi_snd *pm = substream->rmidi->private_data;

	dbg_hid("pcmidi in trigger %d\n", up);

	pm->in_triggered = up;
}

static struct snd_rawmidi_ops pcmidi_in_ops = {
	.open = pcmidi_in_open,
	.close = pcmidi_in_close,
	.trigger = pcmidi_in_trigger
};

int pcmidi_snd_initialise(struct pcmidi_snd *pm)
{
	static int dev;
	struct snd_card *card;
	struct snd_rawmidi *rwmidi;
	int err;

	static struct snd_device_ops ops = {
		.dev_free = pcmidi_snd_free,
	};

	if (pm->ifnum != 1)
		return 0; /* only set up midi device ONCE for interace 1 */

	if (dev >= SNDRV_CARDS)
		return -ENODEV;

	if (!enable[dev]) {
		dev++;
		return -ENOENT;
	}

	/* Setup sound card */

	err = snd_card_create(index[dev], id[dev], THIS_MODULE, 0, &card);
	if (err < 0) {
		pk_error("failed to create pc-midi sound card\n");
		err = -ENOMEM;
		goto fail;
	}
	pm->card = card;

	/* Setup sound device */
	err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, pm, &ops);
	if (err < 0) {
		pk_error("failed to create pc-midi sound device: error %d\n",
			err);
		goto fail;
	}

	strncpy(card->driver, shortname, sizeof(card->driver));
	strncpy(card->shortname, shortname, sizeof(card->shortname));
	strncpy(card->longname, longname, sizeof(card->longname));

	/* Set up rawmidi */
	err = snd_rawmidi_new(card, card->shortname, 0,
			      0, 1, &rwmidi);
	if (err < 0) {
		pk_error("failed to create pc-midi rawmidi device: error %d\n",
			err);
		goto fail;
	}
	pm->rwmidi = rwmidi;
	strncpy(rwmidi->name, card->shortname, sizeof(rwmidi->name));
	rwmidi->info_flags = SNDRV_RAWMIDI_INFO_INPUT;
	rwmidi->private_data = pm;

	snd_rawmidi_set_ops(rwmidi, SNDRV_RAWMIDI_STREAM_INPUT,
		&pcmidi_in_ops);

	snd_card_set_dev(card, &pm->pk->hdev->dev);

	/* create sysfs variables */
	err = device_create_file(&pm->pk->hdev->dev,
				 sysfs_device_attr_channel);
	if (err < 0) {
		pk_error("failed to create sysfs attribute channel: error %d\n",
			err);
		goto fail;
	}

	err = device_create_file(&pm->pk->hdev->dev,
				sysfs_device_attr_sustain);
	if (err < 0) {
		pk_error("failed to create sysfs attribute sustain: error %d\n",
			err);
		goto fail_attr_sustain;
	}

	err = device_create_file(&pm->pk->hdev->dev,
			 sysfs_device_attr_octave);
	if (err < 0) {
		pk_error("failed to create sysfs attribute octave: error %d\n",
			err);
		goto fail_attr_octave;
	}

	spin_lock_init(&pm->rawmidi_in_lock);

	init_sustain_timers(pm);
	pcmidi_set_operational(pm);

	/* register it */
	err = snd_card_register(card);
	if (err < 0) {
		pk_error("failed to register pc-midi sound card: error %d\n",
			 err);
			 goto fail_register;
	}

	dbg_hid("pcmidi_snd_initialise finished ok\n");
	return 0;

fail_register:
	stop_sustain_timers(pm);
	device_remove_file(&pm->pk->hdev->dev, sysfs_device_attr_octave);
fail_attr_octave:
	device_remove_file(&pm->pk->hdev->dev, sysfs_device_attr_sustain);
fail_attr_sustain:
	device_remove_file(&pm->pk->hdev->dev, sysfs_device_attr_channel);
fail:
	if (pm->card) {
		snd_card_free(pm->card);
		pm->card = NULL;
	}
	return err;
}

int pcmidi_snd_terminate(struct pcmidi_snd *pm)
{
	if (pm->card) {
		stop_sustain_timers(pm);

		device_remove_file(&pm->pk->hdev->dev,
			sysfs_device_attr_channel);
		device_remove_file(&pm->pk->hdev->dev,
			sysfs_device_attr_sustain);
		device_remove_file(&pm->pk->hdev->dev,
			sysfs_device_attr_octave);

		snd_card_disconnect(pm->card);
		snd_card_free_when_closed(pm->card);
	}

	return 0;
}

/*
 * PC-MIDI report descriptor for report id is wrong.
 */
static void pk_report_fixup(struct hid_device *hdev, __u8 *rdesc,
		unsigned int rsize)
{
	if (rsize == 178 &&
	      rdesc[111] == 0x06 && rdesc[112] == 0x00 &&
	      rdesc[113] == 0xff) {
		dev_info(&hdev->dev, "fixing up pc-midi keyboard report "
			"descriptor\n");

		rdesc[144] = 0x18; /* report 4: was 0x10 report count */
	}
}

static int pk_input_mapping(struct hid_device *hdev, struct hid_input *hi,
		struct hid_field *field, struct hid_usage *usage,
		unsigned long **bit, int *max)
{
	struct pk_device *pk = (struct pk_device *)hid_get_drvdata(hdev);
	struct pcmidi_snd *pm;

	pm = pk->pm;

	if (HID_UP_MSVENDOR == (usage->hid & HID_USAGE_PAGE) &&
		1 == pm->ifnum) {
		pcmidi_setup_extra_keys(pm, hi->input);
		return 0;
	}

	return 0;
}


static int pk_raw_event(struct hid_device *hdev, struct hid_report *report,
	u8 *data, int size)
{
	struct pk_device *pk = (struct pk_device *)hid_get_drvdata(hdev);
	int ret = 0;

	if (1 == pk->pm->ifnum) {
		if (report->id == data[0])
			switch (report->id) {
			case 0x01: /* midi keys (qwerty)*/
			case 0x03: /* midi keyboard (musical)*/
			case 0x04: /* extra/midi keys (qwerty)*/
				ret = pcmidi_handle_report(pk->pm,
						report->id, data, size);
				break;
			}
	}

	return ret;
}

static int pk_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;
	struct usb_interface *intf = to_usb_interface(hdev->dev.parent);
	unsigned short ifnum = intf->cur_altsetting->desc.bInterfaceNumber;
	unsigned long quirks = id->driver_data;
	struct pk_device *pk;
	struct pcmidi_snd *pm = NULL;

	pk = kzalloc(sizeof(*pk), GFP_KERNEL);
	if (pk == NULL) {
		dev_err(&hdev->dev, "prodikeys: can't alloc descriptor\n");
		return -ENOMEM;
	}

	pk->hdev = hdev;

	pm = kzalloc(sizeof(*pm), GFP_KERNEL);
	if (pm == NULL) {
		dev_err(&hdev->dev,
			"prodikeys: can't alloc descriptor\n");
		ret = -ENOMEM;
		goto err_free;
	}

	pm->pk = pk;
	pk->pm = pm;
	pm->ifnum = ifnum;

	hid_set_drvdata(hdev, pk);

	ret = hid_parse(hdev);
	if (ret) {
		dev_err(&hdev->dev, "prodikeys: hid parse failed\n");
		goto err_free;
	}

	if (quirks & PK_QUIRK_NOGET) { /* hid_parse cleared all the quirks */
		hdev->quirks |= HID_QUIRK_NOGET;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_DEFAULT);
	if (ret) {
		dev_err(&hdev->dev, "prodikeys: hw start failed\n");
		goto err_free;
	}

	ret = pcmidi_snd_initialise(pm);
	if (ret < 0)
		goto err_stop;

	return 0;
err_stop:
	hid_hw_stop(hdev);
err_free:
	if (pm != NULL)
		kfree(pm);

	kfree(pk);
	return ret;
}

static void pk_remove(struct hid_device *hdev)
{
	struct pk_device *pk = (struct pk_device *)hid_get_drvdata(hdev);
	struct pcmidi_snd *pm;

	pm = pk->pm;
	if (pm) {
		pcmidi_snd_terminate(pm);
		kfree(pm);
	}

	hid_hw_stop(hdev);

	kfree(pk);
}

static const struct hid_device_id pk_devices[] = {
	{HID_USB_DEVICE(USB_VENDOR_ID_CREATIVELABS,
		USB_DEVICE_ID_PRODIKEYS_PCMIDI),
	    .driver_data = PK_QUIRK_NOGET},
	{ }
};
MODULE_DEVICE_TABLE(hid, pk_devices);

static struct hid_driver pk_driver = {
	.name = "prodikeys",
	.id_table = pk_devices,
	.report_fixup = pk_report_fixup,
	.input_mapping = pk_input_mapping,
	.raw_event = pk_raw_event,
	.probe = pk_probe,
	.remove = pk_remove,
};

static int pk_init(void)
{
	int ret;

	ret = hid_register_driver(&pk_driver);
	if (ret)
		printk(KERN_ERR "can't register prodikeys driver\n");

	return ret;
}

static void pk_exit(void)
{
	hid_unregister_driver(&pk_driver);
}

module_init(pk_init);
module_exit(pk_exit);
MODULE_LICENSE("GPL");
