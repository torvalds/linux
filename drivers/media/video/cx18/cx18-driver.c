/*
 *  cx18 driver initialization and card probing
 *
 *  Derived from ivtv-driver.c
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 *  02111-1307  USA
 */

#include "cx18-driver.h"
#include "cx18-version.h"
#include "cx18-cards.h"
#include "cx18-i2c.h"
#include "cx18-irq.h"
#include "cx18-gpio.h"
#include "cx18-firmware.h"
#include "cx18-streams.h"
#include "cx18-av-core.h"
#include "cx18-scb.h"
#include "cx18-mailbox.h"
#include "cx18-ioctl.h"
#include "tuner-xc2028.h"

#include <media/tveeprom.h>


/* var to keep track of the number of array elements in use */
int cx18_cards_active;

/* If you have already X v4l cards, then set this to X. This way
   the device numbers stay matched. Example: you have a WinTV card
   without radio and a Compro H900 with. Normally this would give a
   video1 device together with a radio0 device for the Compro. By
   setting this to 1 you ensure that radio0 is now also radio1. */
int cx18_first_minor;

/* Master variable for all cx18 info */
struct cx18 *cx18_cards[CX18_MAX_CARDS];

/* Protects cx18_cards_active */
DEFINE_SPINLOCK(cx18_cards_lock);

/* add your revision and whatnot here */
static struct pci_device_id cx18_pci_tbl[] __devinitdata = {
	{PCI_VENDOR_ID_CX, PCI_DEVICE_ID_CX23418,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0,}
};

MODULE_DEVICE_TABLE(pci, cx18_pci_tbl);

/* Parameter declarations */
static int cardtype[CX18_MAX_CARDS];
static int tuner[CX18_MAX_CARDS] = { -1, -1, -1, -1, -1, -1, -1, -1,
				     -1, -1, -1, -1, -1, -1, -1, -1,
				     -1, -1, -1, -1, -1, -1, -1, -1,
				     -1, -1, -1, -1, -1, -1, -1, -1 };
static int radio[CX18_MAX_CARDS] = { -1, -1, -1, -1, -1, -1, -1, -1,
				     -1, -1, -1, -1, -1, -1, -1, -1,
				     -1, -1, -1, -1, -1, -1, -1, -1,
				     -1, -1, -1, -1, -1, -1, -1, -1 };

static int cardtype_c = 1;
static int tuner_c = 1;
static int radio_c = 1;
static char pal[] = "--";
static char secam[] = "--";
static char ntsc[] = "-";

/* Buffers */
static int enc_mpg_buffers = CX18_DEFAULT_ENC_MPG_BUFFERS;
static int enc_ts_buffers = CX18_DEFAULT_ENC_TS_BUFFERS;
static int enc_yuv_buffers = CX18_DEFAULT_ENC_YUV_BUFFERS;
static int enc_vbi_buffers = CX18_DEFAULT_ENC_VBI_BUFFERS;
static int enc_pcm_buffers = CX18_DEFAULT_ENC_PCM_BUFFERS;

static int cx18_pci_latency = 1;

int cx18_debug;

module_param_array(tuner, int, &tuner_c, 0644);
module_param_array(radio, bool, &radio_c, 0644);
module_param_array(cardtype, int, &cardtype_c, 0644);
module_param_string(pal, pal, sizeof(pal), 0644);
module_param_string(secam, secam, sizeof(secam), 0644);
module_param_string(ntsc, ntsc, sizeof(ntsc), 0644);
module_param_named(debug, cx18_debug, int, 0644);
module_param(cx18_pci_latency, int, 0644);
module_param(cx18_first_minor, int, 0644);

module_param(enc_mpg_buffers, int, 0644);
module_param(enc_ts_buffers, int, 0644);
module_param(enc_yuv_buffers, int, 0644);
module_param(enc_vbi_buffers, int, 0644);
module_param(enc_pcm_buffers, int, 0644);

MODULE_PARM_DESC(tuner, "Tuner type selection,\n"
			"\t\t\tsee tuner.h for values");
MODULE_PARM_DESC(radio,
		 "Enable or disable the radio. Use only if autodetection\n"
		 "\t\t\tfails. 0 = disable, 1 = enable");
MODULE_PARM_DESC(cardtype,
		 "Only use this option if your card is not detected properly.\n"
		 "\t\tSpecify card type:\n"
		 "\t\t\t 1 = Hauppauge HVR 1600 (ESMT memory)\n"
		 "\t\t\t 2 = Hauppauge HVR 1600 (Samsung memory)\n"
		 "\t\t\t 3 = Compro VideoMate H900\n"
		 "\t\t\t 4 = Yuan MPC718\n"
		 "\t\t\t 0 = Autodetect (default)\n"
		 "\t\t\t-1 = Ignore this card\n\t\t");
MODULE_PARM_DESC(pal, "Set PAL standard: B, G, H, D, K, I, M, N, Nc, 60");
MODULE_PARM_DESC(secam, "Set SECAM standard: B, G, H, D, K, L, LC");
MODULE_PARM_DESC(ntsc, "Set NTSC standard: M, J, K");
MODULE_PARM_DESC(debug,
		 "Debug level (bitmask). Default: 0\n"
		 "\t\t\t  1/0x0001: warning\n"
		 "\t\t\t  2/0x0002: info\n"
		 "\t\t\t  4/0x0004: mailbox\n"
		 "\t\t\t  8/0x0008: dma\n"
		 "\t\t\t 16/0x0010: ioctl\n"
		 "\t\t\t 32/0x0020: file\n"
		 "\t\t\t 64/0x0040: i2c\n"
		 "\t\t\t128/0x0080: irq\n"
		 "\t\t\t256/0x0100: high volume\n");
MODULE_PARM_DESC(cx18_pci_latency,
		 "Change the PCI latency to 64 if lower: 0 = No, 1 = Yes,\n"
		 "\t\t\tDefault: Yes");
MODULE_PARM_DESC(enc_mpg_buffers,
		 "Encoder MPG Buffers (in MB)\n"
		 "\t\t\tDefault: " __stringify(CX18_DEFAULT_ENC_MPG_BUFFERS));
MODULE_PARM_DESC(enc_ts_buffers,
		 "Encoder TS Buffers (in MB)\n"
		 "\t\t\tDefault: " __stringify(CX18_DEFAULT_ENC_TS_BUFFERS));
MODULE_PARM_DESC(enc_yuv_buffers,
		 "Encoder YUV Buffers (in MB)\n"
		 "\t\t\tDefault: " __stringify(CX18_DEFAULT_ENC_YUV_BUFFERS));
MODULE_PARM_DESC(enc_vbi_buffers,
		 "Encoder VBI Buffers (in MB)\n"
		 "\t\t\tDefault: " __stringify(CX18_DEFAULT_ENC_VBI_BUFFERS));
MODULE_PARM_DESC(enc_pcm_buffers,
		 "Encoder PCM buffers (in MB)\n"
		 "\t\t\tDefault: " __stringify(CX18_DEFAULT_ENC_PCM_BUFFERS));

MODULE_PARM_DESC(cx18_first_minor, "Set minor assigned to first card");

MODULE_AUTHOR("Hans Verkuil");
MODULE_DESCRIPTION("CX23418 driver");
MODULE_SUPPORTED_DEVICE("CX23418 MPEG2 encoder");
MODULE_LICENSE("GPL");

MODULE_VERSION(CX18_VERSION);

/* Generic utility functions */
int cx18_msleep_timeout(unsigned int msecs, int intr)
{
	int timeout = msecs_to_jiffies(msecs);
	int sig;

	do {
		set_current_state(intr ? TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE);
		timeout = schedule_timeout(timeout);
		sig = intr ? signal_pending(current) : 0;
	} while (!sig && timeout);
	return sig;
}

/* Release ioremapped memory */
static void cx18_iounmap(struct cx18 *cx)
{
	if (cx == NULL)
		return;

	/* Release io memory */
	if (cx->enc_mem != NULL) {
		CX18_DEBUG_INFO("releasing enc_mem\n");
		iounmap(cx->enc_mem);
		cx->enc_mem = NULL;
	}
}

/* Hauppauge card? get values from tveeprom */
void cx18_read_eeprom(struct cx18 *cx, struct tveeprom *tv)
{
	u8 eedata[256];

	cx->i2c_client[0].addr = 0xA0 >> 1;
	tveeprom_read(&cx->i2c_client[0], eedata, sizeof(eedata));
	tveeprom_hauppauge_analog(&cx->i2c_client[0], tv, eedata);
}

static void cx18_process_eeprom(struct cx18 *cx)
{
	struct tveeprom tv;

	cx18_read_eeprom(cx, &tv);

	/* Many thanks to Steven Toth from Hauppauge for providing the
	   model numbers */
	/* Note: the Samsung memory models cannot be reliably determined
	   from the model number. Use the cardtype module option if you
	   have one of these preproduction models. */
	switch (tv.model) {
	case 74000 ... 74999:
		cx->card = cx18_get_card(CX18_CARD_HVR_1600_ESMT);
		break;
	case 0:
		CX18_ERR("Invalid EEPROM\n");
		return;
	default:
		CX18_ERR("Unknown model %d, defaulting to HVR-1600\n", tv.model);
		cx->card = cx18_get_card(CX18_CARD_HVR_1600_ESMT);
		break;
	}

	cx->v4l2_cap = cx->card->v4l2_capabilities;
	cx->card_name = cx->card->name;
	cx->card_i2c = cx->card->i2c;

	CX18_INFO("Autodetected %s\n", cx->card_name);

	if (tv.tuner_type == TUNER_ABSENT)
		CX18_ERR("tveeprom cannot autodetect tuner!");

	if (cx->options.tuner == -1)
		cx->options.tuner = tv.tuner_type;
	if (cx->options.radio == -1)
		cx->options.radio = (tv.has_radio != 0);

	if (cx->std != 0)
		/* user specified tuner standard */
		return;

	/* autodetect tuner standard */
	if (tv.tuner_formats & V4L2_STD_PAL) {
		CX18_DEBUG_INFO("PAL tuner detected\n");
		cx->std |= V4L2_STD_PAL_BG | V4L2_STD_PAL_H;
	} else if (tv.tuner_formats & V4L2_STD_NTSC) {
		CX18_DEBUG_INFO("NTSC tuner detected\n");
		cx->std |= V4L2_STD_NTSC_M;
	} else if (tv.tuner_formats & V4L2_STD_SECAM) {
		CX18_DEBUG_INFO("SECAM tuner detected\n");
		cx->std |= V4L2_STD_SECAM_L;
	} else {
		CX18_INFO("No tuner detected, default to NTSC-M\n");
		cx->std |= V4L2_STD_NTSC_M;
	}
}

static v4l2_std_id cx18_parse_std(struct cx18 *cx)
{
	switch (pal[0]) {
	case '6':
		return V4L2_STD_PAL_60;
	case 'b':
	case 'B':
	case 'g':
	case 'G':
		return V4L2_STD_PAL_BG;
	case 'h':
	case 'H':
		return V4L2_STD_PAL_H;
	case 'n':
	case 'N':
		if (pal[1] == 'c' || pal[1] == 'C')
			return V4L2_STD_PAL_Nc;
		return V4L2_STD_PAL_N;
	case 'i':
	case 'I':
		return V4L2_STD_PAL_I;
	case 'd':
	case 'D':
	case 'k':
	case 'K':
		return V4L2_STD_PAL_DK;
	case 'M':
	case 'm':
		return V4L2_STD_PAL_M;
	case '-':
		break;
	default:
		CX18_WARN("pal= argument not recognised\n");
		return 0;
	}

	switch (secam[0]) {
	case 'b':
	case 'B':
	case 'g':
	case 'G':
	case 'h':
	case 'H':
		return V4L2_STD_SECAM_B | V4L2_STD_SECAM_G | V4L2_STD_SECAM_H;
	case 'd':
	case 'D':
	case 'k':
	case 'K':
		return V4L2_STD_SECAM_DK;
	case 'l':
	case 'L':
		if (secam[1] == 'C' || secam[1] == 'c')
			return V4L2_STD_SECAM_LC;
		return V4L2_STD_SECAM_L;
	case '-':
		break;
	default:
		CX18_WARN("secam= argument not recognised\n");
		return 0;
	}

	switch (ntsc[0]) {
	case 'm':
	case 'M':
		return V4L2_STD_NTSC_M;
	case 'j':
	case 'J':
		return V4L2_STD_NTSC_M_JP;
	case 'k':
	case 'K':
		return V4L2_STD_NTSC_M_KR;
	case '-':
		break;
	default:
		CX18_WARN("ntsc= argument not recognised\n");
		return 0;
	}

	/* no match found */
	return 0;
}

static void cx18_process_options(struct cx18 *cx)
{
	int i, j;

	cx->options.megabytes[CX18_ENC_STREAM_TYPE_MPG] = enc_mpg_buffers;
	cx->options.megabytes[CX18_ENC_STREAM_TYPE_TS] = enc_ts_buffers;
	cx->options.megabytes[CX18_ENC_STREAM_TYPE_YUV] = enc_yuv_buffers;
	cx->options.megabytes[CX18_ENC_STREAM_TYPE_VBI] = enc_vbi_buffers;
	cx->options.megabytes[CX18_ENC_STREAM_TYPE_PCM] = enc_pcm_buffers;
	cx->options.cardtype = cardtype[cx->num];
	cx->options.tuner = tuner[cx->num];
	cx->options.radio = radio[cx->num];

	cx->std = cx18_parse_std(cx);
	if (cx->options.cardtype == -1) {
		CX18_INFO("Ignore card\n");
		return;
	}
	cx->card = cx18_get_card(cx->options.cardtype - 1);
	if (cx->card)
		CX18_INFO("User specified %s card\n", cx->card->name);
	else if (cx->options.cardtype != 0)
		CX18_ERR("Unknown user specified type, trying to autodetect card\n");
	if (cx->card == NULL) {
		if (cx->dev->subsystem_vendor == CX18_PCI_ID_HAUPPAUGE) {
			cx->card = cx18_get_card(CX18_CARD_HVR_1600_ESMT);
			CX18_INFO("Autodetected Hauppauge card\n");
		}
	}
	if (cx->card == NULL) {
		for (i = 0; (cx->card = cx18_get_card(i)); i++) {
			if (cx->card->pci_list == NULL)
				continue;
			for (j = 0; cx->card->pci_list[j].device; j++) {
				if (cx->dev->device !=
				    cx->card->pci_list[j].device)
					continue;
				if (cx->dev->subsystem_vendor !=
				    cx->card->pci_list[j].subsystem_vendor)
					continue;
				if (cx->dev->subsystem_device !=
				    cx->card->pci_list[j].subsystem_device)
					continue;
				CX18_INFO("Autodetected %s card\n", cx->card->name);
				goto done;
			}
		}
	}
done:

	if (cx->card == NULL) {
		cx->card = cx18_get_card(CX18_CARD_HVR_1600_ESMT);
		CX18_ERR("Unknown card: vendor/device: %04x/%04x\n",
		     cx->dev->vendor, cx->dev->device);
		CX18_ERR("              subsystem vendor/device: %04x/%04x\n",
		     cx->dev->subsystem_vendor, cx->dev->subsystem_device);
		CX18_ERR("Defaulting to %s card\n", cx->card->name);
		CX18_ERR("Please mail the vendor/device and subsystem vendor/device IDs and what kind of\n");
		CX18_ERR("card you have to the ivtv-devel mailinglist (www.ivtvdriver.org)\n");
		CX18_ERR("Prefix your subject line with [UNKNOWN CX18 CARD].\n");
	}
	cx->v4l2_cap = cx->card->v4l2_capabilities;
	cx->card_name = cx->card->name;
	cx->card_i2c = cx->card->i2c;
}

/* Precondition: the cx18 structure has been memset to 0. Only
   the dev and num fields have been filled in.
   No assumptions on the card type may be made here (see cx18_init_struct2
   for that).
 */
static int __devinit cx18_init_struct1(struct cx18 *cx)
{
	cx->base_addr = pci_resource_start(cx->dev, 0);

	mutex_init(&cx->serialize_lock);
	mutex_init(&cx->i2c_bus_lock[0]);
	mutex_init(&cx->i2c_bus_lock[1]);

	spin_lock_init(&cx->lock);
	spin_lock_init(&cx->dma_reg_lock);

	/* start counting open_id at 1 */
	cx->open_id = 1;

	/* Initial settings */
	cx2341x_fill_defaults(&cx->params);
	cx->temporal_strength = cx->params.video_temporal_filter;
	cx->spatial_strength = cx->params.video_spatial_filter;
	cx->filter_mode = cx->params.video_spatial_filter_mode |
		(cx->params.video_temporal_filter_mode << 1) |
		(cx->params.video_median_filter_type << 2);
	cx->params.port = CX2341X_PORT_MEMORY;
	cx->params.capabilities = CX2341X_CAP_HAS_SLICED_VBI;
	init_waitqueue_head(&cx->cap_w);
	init_waitqueue_head(&cx->mb_apu_waitq);
	init_waitqueue_head(&cx->mb_cpu_waitq);
	init_waitqueue_head(&cx->mb_epu_waitq);
	init_waitqueue_head(&cx->mb_hpu_waitq);
	init_waitqueue_head(&cx->dma_waitq);

	/* VBI */
	cx->vbi.in.type = V4L2_BUF_TYPE_SLICED_VBI_CAPTURE;
	cx->vbi.sliced_in = &cx->vbi.in.fmt.sliced;
	cx->vbi.raw_size = 1456;
	cx->vbi.raw_decoder_line_size = 1456;
	cx->vbi.raw_decoder_sav_odd_field = 0x20;
	cx->vbi.raw_decoder_sav_even_field = 0x60;
	cx->vbi.sliced_decoder_line_size = 272;
	cx->vbi.sliced_decoder_sav_odd_field = 0xB0;
	cx->vbi.sliced_decoder_sav_even_field = 0xF0;
	return 0;
}

/* Second initialization part. Here the card type has been
   autodetected. */
static void __devinit cx18_init_struct2(struct cx18 *cx)
{
	int i;

	for (i = 0; i < CX18_CARD_MAX_VIDEO_INPUTS; i++)
		if (cx->card->video_inputs[i].video_type == 0)
			break;
	cx->nof_inputs = i;
	for (i = 0; i < CX18_CARD_MAX_AUDIO_INPUTS; i++)
		if (cx->card->audio_inputs[i].audio_type == 0)
			break;
	cx->nof_audio_inputs = i;

	/* Find tuner input */
	for (i = 0; i < cx->nof_inputs; i++) {
		if (cx->card->video_inputs[i].video_type ==
				CX18_CARD_INPUT_VID_TUNER)
			break;
	}
	if (i == cx->nof_inputs)
		i = 0;
	cx->active_input = i;
	cx->audio_input = cx->card->video_inputs[i].audio_index;
	cx->av_state.vid_input = CX18_AV_COMPOSITE7;
	cx->av_state.aud_input = CX18_AV_AUDIO8;
	cx->av_state.audclk_freq = 48000;
	cx->av_state.audmode = V4L2_TUNER_MODE_LANG1;
	cx->av_state.vbi_line_offset = 8;
}

static int cx18_setup_pci(struct cx18 *cx, struct pci_dev *dev,
			  const struct pci_device_id *pci_id)
{
	u16 cmd;
	unsigned char pci_latency;

	CX18_DEBUG_INFO("Enabling pci device\n");

	if (pci_enable_device(dev)) {
		CX18_ERR("Can't enable device %d!\n", cx->num);
		return -EIO;
	}
	if (pci_set_dma_mask(dev, 0xffffffff)) {
		CX18_ERR("No suitable DMA available on card %d.\n", cx->num);
		return -EIO;
	}
	if (!request_mem_region(cx->base_addr, CX18_MEM_SIZE, "cx18 encoder")) {
		CX18_ERR("Cannot request encoder memory region on card %d.\n", cx->num);
		return -EIO;
	}

	/* Check for bus mastering */
	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	cmd |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
	pci_write_config_word(dev, PCI_COMMAND, cmd);

	pci_read_config_byte(dev, PCI_CLASS_REVISION, &cx->card_rev);
	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &pci_latency);

	if (pci_latency < 64 && cx18_pci_latency) {
		CX18_INFO("Unreasonably low latency timer, "
			       "setting to 64 (was %d)\n", pci_latency);
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 64);
		pci_read_config_byte(dev, PCI_LATENCY_TIMER, &pci_latency);
	}
	/* This config space value relates to DMA latencies. The
	   default value 0x8080 is too low however and will lead
	   to DMA errors. 0xffff is the max value which solves
	   these problems. */
	pci_write_config_dword(dev, 0x40, 0xffff);

	CX18_DEBUG_INFO("cx%d (rev %d) at %02x:%02x.%x, "
		   "irq: %d, latency: %d, memory: 0x%lx\n",
		   cx->dev->device, cx->card_rev, dev->bus->number,
		   PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn),
		   cx->dev->irq, pci_latency, (unsigned long)cx->base_addr);

	return 0;
}

#ifdef MODULE
static u32 cx18_request_module(struct cx18 *cx, u32 hw,
		const char *name, u32 id)
{
	if ((hw & id) == 0)
		return hw;
	if (request_module(name) != 0) {
		CX18_ERR("Failed to load module %s\n", name);
		return hw & ~id;
	}
	CX18_DEBUG_INFO("Loaded module %s\n", name);
	return hw;
}
#endif

static void cx18_load_and_init_modules(struct cx18 *cx)
{
	u32 hw = cx->card->hw_all;
	int i;

#ifdef MODULE
	/* load modules */
#ifndef CONFIG_MEDIA_TUNER
	hw = cx18_request_module(cx, hw, "tuner", CX18_HW_TUNER);
#endif
#ifndef CONFIG_VIDEO_CS5345
	hw = cx18_request_module(cx, hw, "cs5345", CX18_HW_CS5345);
#endif
#endif

	/* check which i2c devices are actually found */
	for (i = 0; i < 32; i++) {
		u32 device = 1 << i;

		if (!(device & hw))
			continue;
		if (device == CX18_HW_GPIO || device == CX18_HW_TVEEPROM ||
		    device == CX18_HW_CX23418 || device == CX18_HW_DVB) {
			/* These 'devices' do not use i2c probing */
			cx->hw_flags |= device;
			continue;
		}
		cx18_i2c_register(cx, i);
		if (cx18_i2c_hw_addr(cx, device) > 0)
			cx->hw_flags |= device;
	}

	hw = cx->hw_flags;
}

static int __devinit cx18_probe(struct pci_dev *dev,
				const struct pci_device_id *pci_id)
{
	int retval = 0;
	int vbi_buf_size;
	u32 devtype;
	struct cx18 *cx;

	spin_lock(&cx18_cards_lock);

	/* Make sure we've got a place for this card */
	if (cx18_cards_active == CX18_MAX_CARDS) {
		printk(KERN_ERR "cx18:  Maximum number of cards detected (%d).\n",
			      cx18_cards_active);
		spin_unlock(&cx18_cards_lock);
		return -ENOMEM;
	}

	cx = kzalloc(sizeof(struct cx18), GFP_ATOMIC);
	if (!cx) {
		spin_unlock(&cx18_cards_lock);
		return -ENOMEM;
	}
	cx18_cards[cx18_cards_active] = cx;
	cx->dev = dev;
	cx->num = cx18_cards_active++;
	snprintf(cx->name, sizeof(cx->name) - 1, "cx18-%d", cx->num);
	CX18_INFO("Initializing card #%d\n", cx->num);

	spin_unlock(&cx18_cards_lock);

	cx18_process_options(cx);
	if (cx->options.cardtype == -1) {
		retval = -ENODEV;
		goto err;
	}
	if (cx18_init_struct1(cx)) {
		retval = -ENOMEM;
		goto err;
	}

	CX18_DEBUG_INFO("base addr: 0x%08x\n", cx->base_addr);

	/* PCI Device Setup */
	retval = cx18_setup_pci(cx, dev, pci_id);
	if (retval != 0) {
		if (retval == -EIO)
			goto free_workqueue;
		else if (retval == -ENXIO)
			goto free_mem;
	}
	/* save cx in the pci struct for later use */
	pci_set_drvdata(dev, cx);

	/* map io memory */
	CX18_DEBUG_INFO("attempting ioremap at 0x%08x len 0x%08x\n",
		   cx->base_addr + CX18_MEM_OFFSET, CX18_MEM_SIZE);
	cx->enc_mem = ioremap_nocache(cx->base_addr + CX18_MEM_OFFSET,
				       CX18_MEM_SIZE);
	if (!cx->enc_mem) {
		CX18_ERR("ioremap failed, perhaps increasing __VMALLOC_RESERVE in page.h\n");
		CX18_ERR("or disabling CONFIG_HIGHMEM4G into the kernel would help\n");
		retval = -ENOMEM;
		goto free_mem;
	}
	cx->reg_mem = cx->enc_mem + CX18_REG_OFFSET;
	devtype = read_reg(0xC72028);
	switch (devtype & 0xff000000) {
	case 0xff000000:
		CX18_INFO("cx23418 revision %08x (A)\n", devtype);
		break;
	case 0x01000000:
		CX18_INFO("cx23418 revision %08x (B)\n", devtype);
		break;
	default:
		CX18_INFO("cx23418 revision %08x (Unknown)\n", devtype);
		break;
	}

	cx18_init_power(cx, 1);
	cx18_init_memory(cx);

	cx->scb = (struct cx18_scb *)(cx->enc_mem + SCB_OFFSET);
	cx18_init_scb(cx);

	cx18_gpio_init(cx);

	/* active i2c  */
	CX18_DEBUG_INFO("activating i2c...\n");
	if (init_cx18_i2c(cx)) {
		CX18_ERR("Could not initialize i2c\n");
		goto free_map;
	}

	CX18_DEBUG_INFO("Active card count: %d.\n", cx18_cards_active);

	if (cx->card->hw_all & CX18_HW_TVEEPROM) {
		/* Based on the model number the cardtype may be changed.
		   The PCI IDs are not always reliable. */
		cx18_process_eeprom(cx);
	}
	if (cx->card->comment)
		CX18_INFO("%s", cx->card->comment);
	if (cx->card->v4l2_capabilities == 0) {
		retval = -ENODEV;
		goto free_i2c;
	}
	cx18_init_memory(cx);

	/* Register IRQ */
	retval = request_irq(cx->dev->irq, cx18_irq_handler,
			     IRQF_SHARED | IRQF_DISABLED, cx->name, (void *)cx);
	if (retval) {
		CX18_ERR("Failed to register irq %d\n", retval);
		goto free_i2c;
	}

	if (cx->std == 0)
		cx->std = V4L2_STD_NTSC_M;

	if (cx->options.tuner == -1) {
		int i;

		for (i = 0; i < CX18_CARD_MAX_TUNERS; i++) {
			if ((cx->std & cx->card->tuners[i].std) == 0)
				continue;
			cx->options.tuner = cx->card->tuners[i].tuner;
			break;
		}
	}
	/* if no tuner was found, then pick the first tuner in the card list */
	if (cx->options.tuner == -1 && cx->card->tuners[0].std) {
		cx->std = cx->card->tuners[0].std;
		cx->options.tuner = cx->card->tuners[0].tuner;
	}
	if (cx->options.radio == -1)
		cx->options.radio = (cx->card->radio_input.audio_type != 0);

	/* The card is now fully identified, continue with card-specific
	   initialization. */
	cx18_init_struct2(cx);

	cx18_load_and_init_modules(cx);

	if (cx->std & V4L2_STD_525_60) {
		cx->is_60hz = 1;
		cx->is_out_60hz = 1;
	} else {
		cx->is_50hz = 1;
		cx->is_out_50hz = 1;
	}
	cx->params.video_gop_size = cx->is_60hz ? 15 : 12;

	cx->stream_buf_size[CX18_ENC_STREAM_TYPE_MPG] = 0x08000;
	cx->stream_buf_size[CX18_ENC_STREAM_TYPE_TS] = 0x08000;
	cx->stream_buf_size[CX18_ENC_STREAM_TYPE_PCM] = 0x01200;
	cx->stream_buf_size[CX18_ENC_STREAM_TYPE_YUV] = 0x20000;
	vbi_buf_size = cx->vbi.raw_size * (cx->is_60hz ? 24 : 36) / 2;
	cx->stream_buf_size[CX18_ENC_STREAM_TYPE_VBI] = vbi_buf_size;

	if (cx->options.radio > 0)
		cx->v4l2_cap |= V4L2_CAP_RADIO;

	retval = cx18_streams_setup(cx);
	if (retval) {
		CX18_ERR("Error %d setting up streams\n", retval);
		goto free_irq;
	}
	retval = cx18_streams_register(cx);
	if (retval) {
		CX18_ERR("Error %d registering devices\n", retval);
		goto free_streams;
	}

	if (cx->options.tuner > -1) {
		struct tuner_setup setup;

		setup.addr = ADDR_UNSET;
		setup.type = cx->options.tuner;
		setup.mode_mask = T_ANALOG_TV;  /* matches TV tuners */
		setup.tuner_callback = (setup.type == TUNER_XC2028) ?
			cx18_reset_tuner_gpio : NULL;
		cx18_call_i2c_clients(cx, TUNER_SET_TYPE_ADDR, &setup);
		if (setup.type == TUNER_XC2028) {
			static struct xc2028_ctrl ctrl = {
				.fname = XC2028_DEFAULT_FIRMWARE,
				.max_len = 64,
			};
			struct v4l2_priv_tun_config cfg = {
				.tuner = cx->options.tuner,
				.priv = &ctrl,
			};
			cx18_call_i2c_clients(cx, TUNER_SET_CONFIG, &cfg);
		}
	}

	/* The tuner is fixed to the standard. The other inputs (e.g. S-Video)
	   are not. */
	cx->tuner_std = cx->std;

	cx18_init_on_first_open(cx);

	CX18_INFO("Initialized card #%d: %s\n", cx->num, cx->card_name);

	return 0;

free_streams:
	cx18_streams_cleanup(cx, 1);
free_irq:
	free_irq(cx->dev->irq, (void *)cx);
free_i2c:
	exit_cx18_i2c(cx);
free_map:
	cx18_iounmap(cx);
free_mem:
	release_mem_region(cx->base_addr, CX18_MEM_SIZE);
free_workqueue:
err:
	if (retval == 0)
		retval = -ENODEV;
	CX18_ERR("Error %d on initialization\n", retval);

	kfree(cx18_cards[cx18_cards_active]);
	cx18_cards[cx18_cards_active] = NULL;
	return retval;
}

int cx18_init_on_first_open(struct cx18 *cx)
{
	int video_input;
	int fw_retry_count = 3;
	struct v4l2_frequency vf;

	if (test_bit(CX18_F_I_FAILED, &cx->i_flags))
		return -ENXIO;

	if (test_and_set_bit(CX18_F_I_INITED, &cx->i_flags))
		return 0;

	while (--fw_retry_count > 0) {
		/* load firmware */
		if (cx18_firmware_init(cx) == 0)
			break;
		if (fw_retry_count > 1)
			CX18_WARN("Retry loading firmware\n");
	}

	if (fw_retry_count == 0) {
		set_bit(CX18_F_I_FAILED, &cx->i_flags);
		return -ENXIO;
	}
	set_bit(CX18_F_I_LOADED_FW, &cx->i_flags);

	/* Init the firmware twice to work around a silicon bug
	 * transport related. */

	fw_retry_count = 3;
	while (--fw_retry_count > 0) {
		/* load firmware */
		if (cx18_firmware_init(cx) == 0)
			break;
		if (fw_retry_count > 1)
			CX18_WARN("Retry loading firmware\n");
	}

	if (fw_retry_count == 0) {
		set_bit(CX18_F_I_FAILED, &cx->i_flags);
		return -ENXIO;
	}

	vf.tuner = 0;
	vf.type = V4L2_TUNER_ANALOG_TV;
	vf.frequency = 6400; /* the tuner 'baseline' frequency */

	/* Set initial frequency. For PAL/SECAM broadcasts no
	   'default' channel exists AFAIK. */
	if (cx->std == V4L2_STD_NTSC_M_JP)
		vf.frequency = 1460;	/* ch. 1 91250*16/1000 */
	else if (cx->std & V4L2_STD_NTSC_M)
		vf.frequency = 1076;	/* ch. 4 67250*16/1000 */

	video_input = cx->active_input;
	cx->active_input++;	/* Force update of input */
	cx18_v4l2_ioctls(cx, NULL, VIDIOC_S_INPUT, &video_input);

	/* Let the VIDIOC_S_STD ioctl do all the work, keeps the code
	   in one place. */
	cx->std++;		/* Force full standard initialization */
	cx18_v4l2_ioctls(cx, NULL, VIDIOC_S_STD, &cx->tuner_std);
	cx18_v4l2_ioctls(cx, NULL, VIDIOC_S_FREQUENCY, &vf);
	return 0;
}

static void cx18_remove(struct pci_dev *pci_dev)
{
	struct cx18 *cx = pci_get_drvdata(pci_dev);

	CX18_DEBUG_INFO("Removing Card #%d\n", cx->num);

	/* Stop all captures */
	CX18_DEBUG_INFO("Stopping all streams\n");
	if (atomic_read(&cx->capturing) > 0)
		cx18_stop_all_captures(cx);

	/* Interrupts */
	sw1_irq_disable(IRQ_CPU_TO_EPU | IRQ_APU_TO_EPU);
	sw2_irq_disable(IRQ_CPU_TO_EPU_ACK | IRQ_APU_TO_EPU_ACK);

	cx18_halt_firmware(cx);

	cx18_streams_cleanup(cx, 1);

	exit_cx18_i2c(cx);

	free_irq(cx->dev->irq, (void *)cx);

	cx18_iounmap(cx);

	release_mem_region(cx->base_addr, CX18_MEM_SIZE);

	pci_disable_device(cx->dev);

	CX18_INFO("Removed %s, card #%d\n", cx->card_name, cx->num);
}

/* define a pci_driver for card detection */
static struct pci_driver cx18_pci_driver = {
      .name =     "cx18",
      .id_table = cx18_pci_tbl,
      .probe =    cx18_probe,
      .remove =   cx18_remove,
};

static int module_start(void)
{
	printk(KERN_INFO "cx18:  Start initialization, version %s\n", CX18_VERSION);

	memset(cx18_cards, 0, sizeof(cx18_cards));

	/* Validate parameters */
	if (cx18_first_minor < 0 || cx18_first_minor >= CX18_MAX_CARDS) {
		printk(KERN_ERR "cx18:  Exiting, ivtv_first_minor must be between 0 and %d\n",
		     CX18_MAX_CARDS - 1);
		return -1;
	}

	if (cx18_debug < 0 || cx18_debug > 511) {
		cx18_debug = 0;
		printk(KERN_INFO "cx18:   Debug value must be >= 0 and <= 511!\n");
	}

	if (pci_register_driver(&cx18_pci_driver)) {
		printk(KERN_ERR "cx18:   Error detecting PCI card\n");
		return -ENODEV;
	}
	printk(KERN_INFO "cx18:  End initialization\n");
	return 0;
}

static void module_cleanup(void)
{
	int i;

	pci_unregister_driver(&cx18_pci_driver);

	for (i = 0; i < cx18_cards_active; i++) {
		if (cx18_cards[i] == NULL)
			continue;
		kfree(cx18_cards[i]);
	}
}

module_init(module_start);
module_exit(module_cleanup);
