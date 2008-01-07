/*
    ivtv driver initialization and card probing
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

/* Main Driver file for the ivtv project:
 * Driver for the Conexant CX23415/CX23416 chip.
 * Author: Kevin Thayer (nufan_wfk at yahoo.com)
 * License: GPL
 * http://www.ivtvdriver.org
 *
 * -----
 * MPG600/MPG160 support by  T.Adachi <tadachi@tadachi-net.com>
 *                      and Takeru KOMORIYA<komoriya@paken.org>
 *
 * AVerMedia M179 GPIO info by Chris Pinkham <cpinkham@bc2va.org>
 *                using information provided by Jiun-Kuei Jung @ AVerMedia.
 *
 * Kurouto Sikou CX23416GYC-STVLP tested by K.Ohta <alpha292@bremen.or.jp>
 *                using information from T.Adachi,Takeru KOMORIYA and others :-)
 *
 * Nagase TRANSGEAR 5000TV, Aopen VA2000MAX-STN6 and I/O data GV-MVP/RX
 *                version by T.Adachi. Special thanks  Mr.Suzuki
 */

#include "ivtv-driver.h"
#include "ivtv-version.h"
#include "ivtv-fileops.h"
#include "ivtv-i2c.h"
#include "ivtv-firmware.h"
#include "ivtv-queue.h"
#include "ivtv-udma.h"
#include "ivtv-irq.h"
#include "ivtv-mailbox.h"
#include "ivtv-streams.h"
#include "ivtv-ioctl.h"
#include "ivtv-cards.h"
#include "ivtv-vbi.h"
#include "ivtv-routing.h"
#include "ivtv-gpio.h"
#include "ivtv-yuv.h"

#include <media/tveeprom.h>
#include <media/saa7115.h>
#include <media/v4l2-chip-ident.h>

/* var to keep track of the number of array elements in use */
int ivtv_cards_active = 0;

/* If you have already X v4l cards, then set this to X. This way
   the device numbers stay matched. Example: you have a WinTV card
   without radio and a PVR-350 with. Normally this would give a
   video1 device together with a radio0 device for the PVR. By
   setting this to 1 you ensure that radio0 is now also radio1. */
int ivtv_first_minor = 0;

/* Master variable for all ivtv info */
struct ivtv *ivtv_cards[IVTV_MAX_CARDS];

/* Protects ivtv_cards_active */
DEFINE_SPINLOCK(ivtv_cards_lock);

/* add your revision and whatnot here */
static struct pci_device_id ivtv_pci_tbl[] __devinitdata = {
	{PCI_VENDOR_ID_ICOMP, PCI_DEVICE_ID_IVTV15,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_ICOMP, PCI_DEVICE_ID_IVTV16,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0,}
};

MODULE_DEVICE_TABLE(pci,ivtv_pci_tbl);

/* Parameter declarations */
static int cardtype[IVTV_MAX_CARDS];
static int tuner[IVTV_MAX_CARDS] = { -1, -1, -1, -1, -1, -1, -1, -1,
				     -1, -1, -1, -1, -1, -1, -1, -1,
				     -1, -1, -1, -1, -1, -1, -1, -1,
				     -1, -1, -1, -1, -1, -1, -1, -1 };
static int radio[IVTV_MAX_CARDS] = { -1, -1, -1, -1, -1, -1, -1, -1,
				     -1, -1, -1, -1, -1, -1, -1, -1,
				     -1, -1, -1, -1, -1, -1, -1, -1,
				     -1, -1, -1, -1, -1, -1, -1, -1 };

static unsigned int cardtype_c = 1;
static unsigned int tuner_c = 1;
static unsigned int radio_c = 1;
static char pal[] = "--";
static char secam[] = "--";
static char ntsc[] = "-";

/* Buffers */

/* DMA Buffers, Default size in MB allocated */
#define IVTV_DEFAULT_ENC_MPG_BUFFERS 4
#define IVTV_DEFAULT_ENC_YUV_BUFFERS 2
#define IVTV_DEFAULT_ENC_VBI_BUFFERS 1
/* Exception: size in kB for this stream (MB is overkill) */
#define IVTV_DEFAULT_ENC_PCM_BUFFERS 320
#define IVTV_DEFAULT_DEC_MPG_BUFFERS 1
#define IVTV_DEFAULT_DEC_YUV_BUFFERS 1
/* Exception: size in kB for this stream (MB is way overkill) */
#define IVTV_DEFAULT_DEC_VBI_BUFFERS 64

static int enc_mpg_buffers = IVTV_DEFAULT_ENC_MPG_BUFFERS;
static int enc_yuv_buffers = IVTV_DEFAULT_ENC_YUV_BUFFERS;
static int enc_vbi_buffers = IVTV_DEFAULT_ENC_VBI_BUFFERS;
static int enc_pcm_buffers = IVTV_DEFAULT_ENC_PCM_BUFFERS;
static int dec_mpg_buffers = IVTV_DEFAULT_DEC_MPG_BUFFERS;
static int dec_yuv_buffers = IVTV_DEFAULT_DEC_YUV_BUFFERS;
static int dec_vbi_buffers = IVTV_DEFAULT_DEC_VBI_BUFFERS;

static int ivtv_yuv_mode = 0;
static int ivtv_yuv_threshold=-1;
static int ivtv_pci_latency = 1;

int ivtv_debug = 0;

static int newi2c = -1;

module_param_array(tuner, int, &tuner_c, 0644);
module_param_array(radio, bool, &radio_c, 0644);
module_param_array(cardtype, int, &cardtype_c, 0644);
module_param_string(pal, pal, sizeof(pal), 0644);
module_param_string(secam, secam, sizeof(secam), 0644);
module_param_string(ntsc, ntsc, sizeof(ntsc), 0644);
module_param_named(debug,ivtv_debug, int, 0644);
module_param(ivtv_pci_latency, int, 0644);
module_param(ivtv_yuv_mode, int, 0644);
module_param(ivtv_yuv_threshold, int, 0644);
module_param(ivtv_first_minor, int, 0644);

module_param(enc_mpg_buffers, int, 0644);
module_param(enc_yuv_buffers, int, 0644);
module_param(enc_vbi_buffers, int, 0644);
module_param(enc_pcm_buffers, int, 0644);
module_param(dec_mpg_buffers, int, 0644);
module_param(dec_yuv_buffers, int, 0644);
module_param(dec_vbi_buffers, int, 0644);

module_param(newi2c, int, 0644);

MODULE_PARM_DESC(tuner, "Tuner type selection,\n"
			"\t\t\tsee tuner.h for values");
MODULE_PARM_DESC(radio,
		 "Enable or disable the radio. Use only if autodetection\n"
		 "\t\t\tfails. 0 = disable, 1 = enable");
MODULE_PARM_DESC(cardtype,
		 "Only use this option if your card is not detected properly.\n"
		 "\t\tSpecify card type:\n"
		 "\t\t\t 1 = WinTV PVR 250\n"
		 "\t\t\t 2 = WinTV PVR 350\n"
		 "\t\t\t 3 = WinTV PVR-150 or PVR-500\n"
		 "\t\t\t 4 = AVerMedia M179\n"
		 "\t\t\t 5 = YUAN MPG600/Kuroutoshikou iTVC16-STVLP\n"
		 "\t\t\t 6 = YUAN MPG160/Kuroutoshikou iTVC15-STVLP\n"
		 "\t\t\t 7 = YUAN PG600/DIAMONDMM PVR-550 (CX Falcon 2)\n"
		 "\t\t\t 8 = Adaptec AVC-2410\n"
		 "\t\t\t 9 = Adaptec AVC-2010\n"
		 "\t\t\t10 = NAGASE TRANSGEAR 5000TV\n"
		 "\t\t\t11 = AOpen VA2000MAX-STN6\n"
		 "\t\t\t12 = YUAN MPG600GR/Kuroutoshikou CX23416GYC-STVLP\n"
		 "\t\t\t13 = I/O Data GV-MVP/RX\n"
		 "\t\t\t14 = I/O Data GV-MVP/RX2E\n"
		 "\t\t\t15 = GOTVIEW PCI DVD\n"
		 "\t\t\t16 = GOTVIEW PCI DVD2 Deluxe\n"
		 "\t\t\t17 = Yuan MPC622\n"
		 "\t\t\t18 = Digital Cowboy DCT-MTVP1\n"
		 "\t\t\t19 = Yuan PG600V2/GotView PCI DVD Lite\n"
		 "\t\t\t20 = Club3D ZAP-TV1x01\n"
		 "\t\t\t21 = AverTV MCE 116 Plus\n"
		 "\t\t\t 0 = Autodetect (default)\n"
		 "\t\t\t-1 = Ignore this card\n\t\t");
MODULE_PARM_DESC(pal, "Set PAL standard: B, G, H, D, K, I, M, N, Nc, 60");
MODULE_PARM_DESC(secam, "Set SECAM standard: B, G, H, D, K, L, LC");
MODULE_PARM_DESC(ntsc, "Set NTSC standard: M, J, K");
MODULE_PARM_DESC(debug,
		 "Debug level (bitmask). Default: 0\n"
		 "\t\t\t   1/0x0001: warning\n"
		 "\t\t\t   2/0x0002: info\n"
		 "\t\t\t   4/0x0004: mailbox\n"
		 "\t\t\t   8/0x0008: ioctl\n"
		 "\t\t\t  16/0x0010: file\n"
		 "\t\t\t  32/0x0020: dma\n"
		 "\t\t\t  64/0x0040: irq\n"
		 "\t\t\t 128/0x0080: decoder\n"
		 "\t\t\t 256/0x0100: yuv\n"
		 "\t\t\t 512/0x0200: i2c\n"
		 "\t\t\t1024/0x0400: high volume\n");
MODULE_PARM_DESC(ivtv_pci_latency,
		 "Change the PCI latency to 64 if lower: 0 = No, 1 = Yes,\n"
		 "\t\t\tDefault: Yes");
MODULE_PARM_DESC(ivtv_yuv_mode,
		 "Specify the yuv playback mode:\n"
		 "\t\t\t0 = interlaced\n\t\t\t1 = progressive\n\t\t\t2 = auto\n"
		 "\t\t\tDefault: 0 (interlaced)");
MODULE_PARM_DESC(ivtv_yuv_threshold,
		 "If ivtv_yuv_mode is 2 (auto) then playback content as\n\t\tprogressive if src height <= ivtv_yuvthreshold\n"
		 "\t\t\tDefault: 480");;
MODULE_PARM_DESC(enc_mpg_buffers,
		 "Encoder MPG Buffers (in MB)\n"
		 "\t\t\tDefault: " __stringify(IVTV_DEFAULT_ENC_MPG_BUFFERS));
MODULE_PARM_DESC(enc_yuv_buffers,
		 "Encoder YUV Buffers (in MB)\n"
		 "\t\t\tDefault: " __stringify(IVTV_DEFAULT_ENC_YUV_BUFFERS));
MODULE_PARM_DESC(enc_vbi_buffers,
		 "Encoder VBI Buffers (in MB)\n"
		 "\t\t\tDefault: " __stringify(IVTV_DEFAULT_ENC_VBI_BUFFERS));
MODULE_PARM_DESC(enc_pcm_buffers,
		 "Encoder PCM buffers (in kB)\n"
		 "\t\t\tDefault: " __stringify(IVTV_DEFAULT_ENC_PCM_BUFFERS));
MODULE_PARM_DESC(dec_mpg_buffers,
		 "Decoder MPG buffers (in MB)\n"
		 "\t\t\tDefault: " __stringify(IVTV_DEFAULT_DEC_MPG_BUFFERS));
MODULE_PARM_DESC(dec_yuv_buffers,
		 "Decoder YUV buffers (in MB)\n"
		 "\t\t\tDefault: " __stringify(IVTV_DEFAULT_DEC_YUV_BUFFERS));
MODULE_PARM_DESC(dec_vbi_buffers,
		 "Decoder VBI buffers (in kB)\n"
		 "\t\t\tDefault: " __stringify(IVTV_DEFAULT_DEC_VBI_BUFFERS));
MODULE_PARM_DESC(newi2c,
		 "Use new I2C implementation\n"
		 "\t\t\t-1 is autodetect, 0 is off, 1 is on\n"
		 "\t\t\tDefault is autodetect");

MODULE_PARM_DESC(ivtv_first_minor, "Set minor assigned to first card");

MODULE_AUTHOR("Kevin Thayer, Chris Kennedy, Hans Verkuil");
MODULE_DESCRIPTION("CX23415/CX23416 driver");
MODULE_SUPPORTED_DEVICE
    ("CX23415/CX23416 MPEG2 encoder (WinTV PVR-150/250/350/500,\n"
		"\t\t\tYuan MPG series and similar)");
MODULE_LICENSE("GPL");

MODULE_VERSION(IVTV_VERSION);

void ivtv_clear_irq_mask(struct ivtv *itv, u32 mask)
{
	itv->irqmask &= ~mask;
	write_reg_sync(itv->irqmask, IVTV_REG_IRQMASK);
}

void ivtv_set_irq_mask(struct ivtv *itv, u32 mask)
{
	itv->irqmask |= mask;
	write_reg_sync(itv->irqmask, IVTV_REG_IRQMASK);
}

int ivtv_set_output_mode(struct ivtv *itv, int mode)
{
    int old_mode;

    spin_lock(&itv->lock);
    old_mode = itv->output_mode;
    if (old_mode == 0)
	itv->output_mode = old_mode = mode;
    spin_unlock(&itv->lock);
    return old_mode;
}

struct ivtv_stream *ivtv_get_output_stream(struct ivtv *itv)
{
	switch (itv->output_mode) {
	case OUT_MPG:
		return &itv->streams[IVTV_DEC_STREAM_TYPE_MPG];
	case OUT_YUV:
		return &itv->streams[IVTV_DEC_STREAM_TYPE_YUV];
	default:
		return NULL;
	}
}

int ivtv_waitq(wait_queue_head_t *waitq)
{
	DEFINE_WAIT(wait);

	prepare_to_wait(waitq, &wait, TASK_INTERRUPTIBLE);
	schedule();
	finish_wait(waitq, &wait);
	return signal_pending(current) ? -EINTR : 0;
}

/* Generic utility functions */
int ivtv_msleep_timeout(unsigned int msecs, int intr)
{
	int ret;
	int timeout = msecs_to_jiffies(msecs);

	do {
		set_current_state(intr ? TASK_INTERRUPTIBLE : TASK_UNINTERRUPTIBLE);
		timeout = schedule_timeout(timeout);
		if (intr && (ret = signal_pending(current)))
			return ret;
	} while (timeout);
	return 0;
}

/* Release ioremapped memory */
static void ivtv_iounmap(struct ivtv *itv)
{
	if (itv == NULL)
		return;

	/* Release registers memory */
	if (itv->reg_mem != NULL) {
		IVTV_DEBUG_INFO("releasing reg_mem\n");
		iounmap(itv->reg_mem);
		itv->reg_mem = NULL;
	}
	/* Release io memory */
	if (itv->has_cx23415 && itv->dec_mem != NULL) {
		IVTV_DEBUG_INFO("releasing dec_mem\n");
		iounmap(itv->dec_mem);
	}
	itv->dec_mem = NULL;

	/* Release io memory */
	if (itv->enc_mem != NULL) {
		IVTV_DEBUG_INFO("releasing enc_mem\n");
		iounmap(itv->enc_mem);
		itv->enc_mem = NULL;
	}
}

/* Hauppauge card? get values from tveeprom */
void ivtv_read_eeprom(struct ivtv *itv, struct tveeprom *tv)
{
	u8 eedata[256];

	itv->i2c_client.addr = 0xA0 >> 1;
	tveeprom_read(&itv->i2c_client, eedata, sizeof(eedata));
	tveeprom_hauppauge_analog(&itv->i2c_client, tv, eedata);
}

static void ivtv_process_eeprom(struct ivtv *itv)
{
	struct tveeprom tv;
	int pci_slot = PCI_SLOT(itv->dev->devfn);

	ivtv_read_eeprom(itv, &tv);

	/* Many thanks to Steven Toth from Hauppauge for providing the
	   model numbers */
	switch (tv.model) {
		/* In a few cases the PCI subsystem IDs do not correctly
		   identify the card. A better method is to check the
		   model number from the eeprom instead. */
		case 30012 ... 30039:  /* Low profile PVR250 */
		case 32000 ... 32999:
		case 48000 ... 48099:  /* 48??? range are PVR250s with a cx23415 */
		case 48400 ... 48599:
			itv->card = ivtv_get_card(IVTV_CARD_PVR_250);
			break;
		case 48100 ... 48399:
		case 48600 ... 48999:
			itv->card = ivtv_get_card(IVTV_CARD_PVR_350);
			break;
		case 23000 ... 23999:  /* PVR500 */
		case 25000 ... 25999:  /* Low profile PVR150 */
		case 26000 ... 26999:  /* Regular PVR150 */
			itv->card = ivtv_get_card(IVTV_CARD_PVR_150);
			break;
		case 0:
			IVTV_ERR("Invalid EEPROM\n");
			return;
		default:
			IVTV_ERR("Unknown model %d, defaulting to PVR-150\n", tv.model);
			itv->card = ivtv_get_card(IVTV_CARD_PVR_150);
			break;
	}

	switch (tv.model) {
		/* Old style PVR350 (with an saa7114) uses this input for
		   the tuner. */
		case 48254:
			itv->card = ivtv_get_card(IVTV_CARD_PVR_350_V1);
			break;
		default:
			break;
	}

	itv->v4l2_cap = itv->card->v4l2_capabilities;
	itv->card_name = itv->card->name;

	/* If this is a PVR500 then it should be possible to detect whether it is the
	   first or second unit by looking at the subsystem device ID: is bit 4 is
	   set, then it is the second unit (according to info from Hauppauge).

	   However, while this works for most cards, I have seen a few PVR500 cards
	   where both units have the same subsystem ID.

	   So instead I look at the reported 'PCI slot' (which is the slot on the PVR500
	   PCI bridge) and if it is 8, then it is assumed to be the first unit, otherwise
	   it is the second unit. It is possible that it is a different slot when ivtv is
	   used in Xen, in that case I ignore this card here. The worst that can happen
	   is that the card presents itself with a non-working radio device.

	   This detection is needed since the eeprom reports incorrectly that a radio is
	   present on the second unit. */
	if (tv.model / 1000 == 23) {
		itv->card_name = "WinTV PVR 500";
		if (pci_slot == 8 || pci_slot == 9) {
			int is_first = (pci_slot & 1) == 0;

			itv->card_name = is_first ? "WinTV PVR 500 (unit #1)" :
						    "WinTV PVR 500 (unit #2)";
			if (!is_first) {
				IVTV_INFO("Correcting tveeprom data: no radio present on second unit\n");
				tv.has_radio = 0;
			}
		}
	}
	IVTV_INFO("Autodetected %s\n", itv->card_name);

	switch (tv.tuner_hauppauge_model) {
		case 85:
		case 99:
		case 112:
			itv->pvr150_workaround = 1;
			break;
		default:
			break;
	}
	if (tv.tuner_type == TUNER_ABSENT)
		IVTV_ERR("tveeprom cannot autodetect tuner!");

	if (itv->options.tuner == -1)
		itv->options.tuner = tv.tuner_type;
	if (itv->options.radio == -1)
		itv->options.radio = (tv.has_radio != 0);
	/* only enable newi2c if an IR blaster is present */
	/* FIXME: for 2.6.20 the test against 2 should be removed */
	if (itv->options.newi2c == -1 && tv.has_ir != -1 && tv.has_ir != 2) {
		itv->options.newi2c = (tv.has_ir & 2) ? 1 : 0;
		if (itv->options.newi2c) {
		    IVTV_INFO("Reopen i2c bus for IR-blaster support\n");
		    exit_ivtv_i2c(itv);
		    init_ivtv_i2c(itv);
		}
	}

	if (itv->std != 0)
		/* user specified tuner standard */
		return;

	/* autodetect tuner standard */
	if (tv.tuner_formats & V4L2_STD_PAL) {
		IVTV_DEBUG_INFO("PAL tuner detected\n");
		itv->std |= V4L2_STD_PAL_BG | V4L2_STD_PAL_H;
	} else if (tv.tuner_formats & V4L2_STD_NTSC) {
		IVTV_DEBUG_INFO("NTSC tuner detected\n");
		itv->std |= V4L2_STD_NTSC_M;
	} else if (tv.tuner_formats & V4L2_STD_SECAM) {
		IVTV_DEBUG_INFO("SECAM tuner detected\n");
		itv->std |= V4L2_STD_SECAM_L;
	} else {
		IVTV_INFO("No tuner detected, default to NTSC-M\n");
		itv->std |= V4L2_STD_NTSC_M;
	}
}

static v4l2_std_id ivtv_parse_std(struct ivtv *itv)
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
			IVTV_WARN("pal= argument not recognised\n");
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
			IVTV_WARN("secam= argument not recognised\n");
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
			IVTV_WARN("ntsc= argument not recognised\n");
			return 0;
	}

	/* no match found */
	return 0;
}

static void ivtv_process_options(struct ivtv *itv)
{
	const char *chipname;
	int i, j;

	itv->options.kilobytes[IVTV_ENC_STREAM_TYPE_MPG] = enc_mpg_buffers * 1024;
	itv->options.kilobytes[IVTV_ENC_STREAM_TYPE_YUV] = enc_yuv_buffers * 1024;
	itv->options.kilobytes[IVTV_ENC_STREAM_TYPE_VBI] = enc_vbi_buffers * 1024;
	itv->options.kilobytes[IVTV_ENC_STREAM_TYPE_PCM] = enc_pcm_buffers;
	itv->options.kilobytes[IVTV_DEC_STREAM_TYPE_MPG] = dec_mpg_buffers * 1024;
	itv->options.kilobytes[IVTV_DEC_STREAM_TYPE_YUV] = dec_yuv_buffers * 1024;
	itv->options.kilobytes[IVTV_DEC_STREAM_TYPE_VBI] = dec_vbi_buffers;
	itv->options.cardtype = cardtype[itv->num];
	itv->options.tuner = tuner[itv->num];
	itv->options.radio = radio[itv->num];
	itv->options.newi2c = newi2c;

	itv->std = ivtv_parse_std(itv);
	itv->has_cx23415 = (itv->dev->device == PCI_DEVICE_ID_IVTV15);
	chipname = itv->has_cx23415 ? "cx23415" : "cx23416";
	if (itv->options.cardtype == -1) {
		IVTV_INFO("Ignore card (detected %s based chip)\n", chipname);
		return;
	}
	if ((itv->card = ivtv_get_card(itv->options.cardtype - 1))) {
		IVTV_INFO("User specified %s card (detected %s based chip)\n",
				itv->card->name, chipname);
	} else if (itv->options.cardtype != 0) {
		IVTV_ERR("Unknown user specified type, trying to autodetect card\n");
	}
	if (itv->card == NULL) {
		if (itv->dev->subsystem_vendor == IVTV_PCI_ID_HAUPPAUGE ||
		    itv->dev->subsystem_vendor == IVTV_PCI_ID_HAUPPAUGE_ALT1 ||
		    itv->dev->subsystem_vendor == IVTV_PCI_ID_HAUPPAUGE_ALT2) {
			itv->card = ivtv_get_card(itv->has_cx23415 ? IVTV_CARD_PVR_350 : IVTV_CARD_PVR_150);
			IVTV_INFO("Autodetected Hauppauge card (%s based)\n",
					chipname);
		}
	}
	if (itv->card == NULL) {
		for (i = 0; (itv->card = ivtv_get_card(i)); i++) {
			if (itv->card->pci_list == NULL)
				continue;
			for (j = 0; itv->card->pci_list[j].device; j++) {
				if (itv->dev->device !=
				    itv->card->pci_list[j].device)
					continue;
				if (itv->dev->subsystem_vendor !=
				    itv->card->pci_list[j].subsystem_vendor)
					continue;
				if (itv->dev->subsystem_device !=
				    itv->card->pci_list[j].subsystem_device)
					continue;
				IVTV_INFO("Autodetected %s card (%s based)\n",
						itv->card->name, chipname);
				goto done;
			}
		}
	}
done:

	if (itv->card == NULL) {
		itv->card = ivtv_get_card(IVTV_CARD_PVR_150);
		IVTV_ERR("Unknown card: vendor/device: %04x/%04x\n",
		     itv->dev->vendor, itv->dev->device);
		IVTV_ERR("              subsystem vendor/device: %04x/%04x\n",
		     itv->dev->subsystem_vendor, itv->dev->subsystem_device);
		IVTV_ERR("              %s based\n", chipname);
		IVTV_ERR("Defaulting to %s card\n", itv->card->name);
		IVTV_ERR("Please mail the vendor/device and subsystem vendor/device IDs and what kind of\n");
		IVTV_ERR("card you have to the ivtv-devel mailinglist (www.ivtvdriver.org)\n");
		IVTV_ERR("Prefix your subject line with [UNKNOWN CARD].\n");
	}
	itv->v4l2_cap = itv->card->v4l2_capabilities;
	itv->card_name = itv->card->name;
}

/* Precondition: the ivtv structure has been memset to 0. Only
   the dev and num fields have been filled in.
   No assumptions on the card type may be made here (see ivtv_init_struct2
   for that).
 */
static int __devinit ivtv_init_struct1(struct ivtv *itv)
{
	itv->base_addr = pci_resource_start(itv->dev, 0);
	itv->enc_mbox.max_mbox = 2; /* the encoder has 3 mailboxes (0-2) */
	itv->dec_mbox.max_mbox = 1; /* the decoder has 2 mailboxes (0-1) */

	mutex_init(&itv->serialize_lock);
	mutex_init(&itv->i2c_bus_lock);
	mutex_init(&itv->udma.lock);

	spin_lock_init(&itv->lock);
	spin_lock_init(&itv->dma_reg_lock);

	itv->irq_work_queues = create_workqueue(itv->name);
	if (itv->irq_work_queues == NULL) {
		IVTV_ERR("Could not create ivtv workqueue\n");
		return -1;
	}

	INIT_WORK(&itv->irq_work_queue, ivtv_irq_work_handler);

	/* start counting open_id at 1 */
	itv->open_id = 1;

	/* Initial settings */
	cx2341x_fill_defaults(&itv->params);
	itv->params.port = CX2341X_PORT_MEMORY;
	itv->params.capabilities = CX2341X_CAP_HAS_SLICED_VBI;
	init_waitqueue_head(&itv->eos_waitq);
	init_waitqueue_head(&itv->event_waitq);
	init_waitqueue_head(&itv->vsync_waitq);
	init_waitqueue_head(&itv->dma_waitq);
	init_timer(&itv->dma_timer);
	itv->dma_timer.function = ivtv_unfinished_dma;
	itv->dma_timer.data = (unsigned long)itv;

	itv->cur_dma_stream = -1;
	itv->cur_pio_stream = -1;
	itv->audio_stereo_mode = AUDIO_STEREO;
	itv->audio_bilingual_mode = AUDIO_MONO_LEFT;

	/* Ctrls */
	itv->speed = 1000;

	/* VBI */
	itv->vbi.in.type = V4L2_BUF_TYPE_SLICED_VBI_CAPTURE;
	itv->vbi.sliced_in = &itv->vbi.in.fmt.sliced;

	/* OSD */
	itv->osd_global_alpha_state = 1;
	itv->osd_global_alpha = 255;

	/* YUV */
	atomic_set(&itv->yuv_info.next_dma_frame, -1);
	itv->yuv_info.lace_mode = ivtv_yuv_mode;
	itv->yuv_info.lace_threshold = ivtv_yuv_threshold;
	return 0;
}

/* Second initialization part. Here the card type has been
   autodetected. */
static void __devinit ivtv_init_struct2(struct ivtv *itv)
{
	int i;

	for (i = 0; i < IVTV_CARD_MAX_VIDEO_INPUTS; i++)
		if (itv->card->video_inputs[i].video_type == 0)
			break;
	itv->nof_inputs = i;
	for (i = 0; i < IVTV_CARD_MAX_AUDIO_INPUTS; i++)
		if (itv->card->audio_inputs[i].audio_type == 0)
			break;
	itv->nof_audio_inputs = i;

	if (itv->card->hw_all & IVTV_HW_CX25840) {
		itv->vbi.sliced_size = 288;  /* multiple of 16, real size = 284 */
	} else {
		itv->vbi.sliced_size = 64;   /* multiple of 16, real size = 52 */
	}

	/* Find tuner input */
	for (i = 0; i < itv->nof_inputs; i++) {
		if (itv->card->video_inputs[i].video_type ==
				IVTV_CARD_INPUT_VID_TUNER)
			break;
	}
	if (i == itv->nof_inputs)
		i = 0;
	itv->active_input = i;
	itv->audio_input = itv->card->video_inputs[i].audio_index;
	if (itv->card->hw_all & IVTV_HW_CX25840)
		itv->video_dec_func = ivtv_cx25840;
	else if (itv->card->hw_all & IVTV_HW_SAA717X)
		itv->video_dec_func = ivtv_saa717x;
	else
		itv->video_dec_func = ivtv_saa7115;
}

static int ivtv_setup_pci(struct ivtv *itv, struct pci_dev *dev,
			  const struct pci_device_id *pci_id)
{
	u16 cmd;
	u8 card_rev;
	unsigned char pci_latency;

	IVTV_DEBUG_INFO("Enabling pci device\n");

	if (pci_enable_device(dev)) {
		IVTV_ERR("Can't enable device %d!\n", itv->num);
		return -EIO;
	}
	if (pci_set_dma_mask(dev, 0xffffffff)) {
		IVTV_ERR("No suitable DMA available on card %d.\n", itv->num);
		return -EIO;
	}
	if (!request_mem_region(itv->base_addr, IVTV_ENCODER_SIZE, "ivtv encoder")) {
		IVTV_ERR("Cannot request encoder memory region on card %d.\n", itv->num);
		return -EIO;
	}

	if (!request_mem_region(itv->base_addr + IVTV_REG_OFFSET,
				IVTV_REG_SIZE, "ivtv registers")) {
		IVTV_ERR("Cannot request register memory region on card %d.\n", itv->num);
		release_mem_region(itv->base_addr, IVTV_ENCODER_SIZE);
		return -EIO;
	}

	if (itv->has_cx23415 &&
	    !request_mem_region(itv->base_addr + IVTV_DECODER_OFFSET,
				IVTV_DECODER_SIZE, "ivtv decoder")) {
		IVTV_ERR("Cannot request decoder memory region on card %d.\n", itv->num);
		release_mem_region(itv->base_addr, IVTV_ENCODER_SIZE);
		release_mem_region(itv->base_addr + IVTV_REG_OFFSET, IVTV_REG_SIZE);
		return -EIO;
	}

	/* Check for bus mastering */
	pci_read_config_word(dev, PCI_COMMAND, &cmd);
	if (!(cmd & PCI_COMMAND_MASTER)) {
		IVTV_DEBUG_INFO("Attempting to enable Bus Mastering\n");
		pci_set_master(dev);
		pci_read_config_word(dev, PCI_COMMAND, &cmd);
		if (!(cmd & PCI_COMMAND_MASTER)) {
			IVTV_ERR("Bus Mastering is not enabled\n");
			return -ENXIO;
		}
	}
	IVTV_DEBUG_INFO("Bus Mastering Enabled.\n");

	pci_read_config_byte(dev, PCI_CLASS_REVISION, &card_rev);
	pci_read_config_byte(dev, PCI_LATENCY_TIMER, &pci_latency);

	if (pci_latency < 64 && ivtv_pci_latency) {
		IVTV_INFO("Unreasonably low latency timer, "
			       "setting to 64 (was %d)\n", pci_latency);
		pci_write_config_byte(dev, PCI_LATENCY_TIMER, 64);
		pci_read_config_byte(dev, PCI_LATENCY_TIMER, &pci_latency);
	}
	/* This config space value relates to DMA latencies. The
	   default value 0x8080 is too low however and will lead
	   to DMA errors. 0xffff is the max value which solves
	   these problems. */
	pci_write_config_dword(dev, 0x40, 0xffff);

	IVTV_DEBUG_INFO("%d (rev %d) at %02x:%02x.%x, "
		   "irq: %d, latency: %d, memory: 0x%lx\n",
		   itv->dev->device, card_rev, dev->bus->number,
		   PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn),
		   itv->dev->irq, pci_latency, (unsigned long)itv->base_addr);

	return 0;
}

static void ivtv_request_module(struct ivtv *itv, const char *name)
{
	if (request_module(name) != 0) {
		IVTV_ERR("Failed to load module %s\n", name);
	} else {
		IVTV_DEBUG_INFO("Loaded module %s\n", name);
	}
}

static void ivtv_load_and_init_modules(struct ivtv *itv)
{
	u32 hw = itv->card->hw_all;
	int i;

	/* load modules */
#ifndef CONFIG_VIDEO_TUNER
	if (hw & IVTV_HW_TUNER) {
		if (itv->options.tuner == TUNER_XCEIVE_XC3028) {
			IVTV_INFO("Xceive tuner not yet supported, only composite and S-Video inputs will be available\n");
			itv->tunerid = 1;
		}
		else {
			ivtv_request_module(itv, "tuner");
		}
	}
#endif
#ifndef CONFIG_VIDEO_CX25840
	if (hw & IVTV_HW_CX25840)
		ivtv_request_module(itv, "cx25840");
#endif
#ifndef CONFIG_VIDEO_SAA711X
	if (hw & IVTV_HW_SAA711X)
		ivtv_request_module(itv, "saa7115");
#endif
#ifndef CONFIG_VIDEO_SAA7127
	if (hw & IVTV_HW_SAA7127)
		ivtv_request_module(itv, "saa7127");
#endif
	if (hw & IVTV_HW_SAA717X)
		ivtv_request_module(itv, "saa717x");
#ifndef CONFIG_VIDEO_UPD64031A
	if (hw & IVTV_HW_UPD64031A)
		ivtv_request_module(itv, "upd64031a");
#endif
#ifndef CONFIG_VIDEO_UPD64083
	if (hw & IVTV_HW_UPD6408X)
		ivtv_request_module(itv, "upd64083");
#endif
#ifndef CONFIG_VIDEO_MSP3400
	if (hw & IVTV_HW_MSP34XX)
		ivtv_request_module(itv, "msp3400");
#endif
#ifndef CONFIG_VIDEO_VP27SMPX
	if (hw & IVTV_HW_VP27SMPX)
		ivtv_request_module(itv, "vp27smpx");
#endif
	if (hw & IVTV_HW_TVAUDIO)
		ivtv_request_module(itv, "tvaudio");
#ifndef CONFIG_VIDEO_WM8775
	if (hw & IVTV_HW_WM8775)
		ivtv_request_module(itv, "wm8775");
#endif
#ifndef CONFIG_VIDEO_WM8739
	if (hw & IVTV_HW_WM8739)
		ivtv_request_module(itv, "wm8739");
#endif
#ifndef CONFIG_VIDEO_CS53L32A
	if (hw & IVTV_HW_CS53L32A)
		ivtv_request_module(itv, "cs53l32a");
#endif

	/* check which i2c devices are actually found */
	for (i = 0; i < 32; i++) {
		u32 device = 1 << i;

		if (!(device & hw))
			continue;
		if (device == IVTV_HW_GPIO) {
			/* GPIO is always available */
			itv->hw_flags |= IVTV_HW_GPIO;
			continue;
		}
		if (ivtv_i2c_hw_addr(itv, device) > 0)
			itv->hw_flags |= device;
	}

	hw = itv->hw_flags;

	if (itv->card->type == IVTV_CARD_CX23416GYC) {
		/* Several variations of this card exist, detect which card
		   type should be used. */
		if ((hw & (IVTV_HW_UPD64031A | IVTV_HW_UPD6408X)) == 0)
			itv->card = ivtv_get_card(IVTV_CARD_CX23416GYC_NOGRYCS);
		else if ((hw & IVTV_HW_UPD64031A) == 0)
			itv->card = ivtv_get_card(IVTV_CARD_CX23416GYC_NOGR);
	}
	else if (itv->card->type == IVTV_CARD_GV_MVPRX ||
		 itv->card->type == IVTV_CARD_GV_MVPRX2E) {
		struct v4l2_crystal_freq crystal_freq;

		/* The crystal frequency of GVMVPRX is 24.576MHz */
		crystal_freq.freq = SAA7115_FREQ_24_576_MHZ;
		crystal_freq.flags = SAA7115_FREQ_FL_UCGC;
		itv->video_dec_func(itv, VIDIOC_INT_S_CRYSTAL_FREQ, &crystal_freq);
	}

	if (hw & IVTV_HW_CX25840) {
		itv->vbi.raw_decoder_line_size = 1444;
		itv->vbi.raw_decoder_sav_odd_field = 0x20;
		itv->vbi.raw_decoder_sav_even_field = 0x60;
		itv->vbi.sliced_decoder_line_size = 272;
		itv->vbi.sliced_decoder_sav_odd_field = 0xB0;
		itv->vbi.sliced_decoder_sav_even_field = 0xF0;
	}

	if (hw & IVTV_HW_SAA711X) {
		struct v4l2_chip_ident v = { V4L2_CHIP_MATCH_I2C_DRIVER, I2C_DRIVERID_SAA711X };

		/* determine the exact saa711x model */
		itv->hw_flags &= ~IVTV_HW_SAA711X;

		ivtv_saa7115(itv, VIDIOC_G_CHIP_IDENT, &v);
		if (v.ident == V4L2_IDENT_SAA7114) {
			itv->hw_flags |= IVTV_HW_SAA7114;
			/* VBI is not yet supported by the saa7114 driver. */
			itv->v4l2_cap &= ~(V4L2_CAP_SLICED_VBI_CAPTURE|V4L2_CAP_VBI_CAPTURE);
		}
		else {
			itv->hw_flags |= IVTV_HW_SAA7115;
		}
		itv->vbi.raw_decoder_line_size = 1443;
		itv->vbi.raw_decoder_sav_odd_field = 0x25;
		itv->vbi.raw_decoder_sav_even_field = 0x62;
		itv->vbi.sliced_decoder_line_size = 51;
		itv->vbi.sliced_decoder_sav_odd_field = 0xAB;
		itv->vbi.sliced_decoder_sav_even_field = 0xEC;
	}

	if (hw & IVTV_HW_SAA717X) {
		itv->vbi.raw_decoder_line_size = 1443;
		itv->vbi.raw_decoder_sav_odd_field = 0x25;
		itv->vbi.raw_decoder_sav_even_field = 0x62;
		itv->vbi.sliced_decoder_line_size = 51;
		itv->vbi.sliced_decoder_sav_odd_field = 0xAB;
		itv->vbi.sliced_decoder_sav_even_field = 0xEC;
	}
}

static int __devinit ivtv_probe(struct pci_dev *dev,
				const struct pci_device_id *pci_id)
{
	int retval = 0;
	int yuv_buf_size;
	int vbi_buf_size;
	struct ivtv *itv;

	spin_lock(&ivtv_cards_lock);

	/* Make sure we've got a place for this card */
	if (ivtv_cards_active == IVTV_MAX_CARDS) {
		printk(KERN_ERR "ivtv:  Maximum number of cards detected (%d)\n",
			      ivtv_cards_active);
		spin_unlock(&ivtv_cards_lock);
		return -ENOMEM;
	}

	itv = kzalloc(sizeof(struct ivtv), GFP_ATOMIC);
	if (itv == 0) {
		spin_unlock(&ivtv_cards_lock);
		return -ENOMEM;
	}
	ivtv_cards[ivtv_cards_active] = itv;
	itv->dev = dev;
	itv->num = ivtv_cards_active++;
	snprintf(itv->name, sizeof(itv->name) - 1, "ivtv%d", itv->num);
	IVTV_INFO("Initializing card #%d\n", itv->num);

	spin_unlock(&ivtv_cards_lock);

	ivtv_process_options(itv);
	if (itv->options.cardtype == -1) {
		retval = -ENODEV;
		goto err;
	}
	if (ivtv_init_struct1(itv)) {
		retval = -ENOMEM;
		goto err;
	}

	IVTV_DEBUG_INFO("base addr: 0x%08x\n", itv->base_addr);

	/* PCI Device Setup */
	if ((retval = ivtv_setup_pci(itv, dev, pci_id)) != 0) {
		if (retval == -EIO)
			goto free_workqueue;
		else if (retval == -ENXIO)
			goto free_mem;
	}
	/* save itv in the pci struct for later use */
	pci_set_drvdata(dev, itv);

	/* map io memory */
	IVTV_DEBUG_INFO("attempting ioremap at 0x%08x len 0x%08x\n",
		   itv->base_addr + IVTV_ENCODER_OFFSET, IVTV_ENCODER_SIZE);
	itv->enc_mem = ioremap_nocache(itv->base_addr + IVTV_ENCODER_OFFSET,
				       IVTV_ENCODER_SIZE);
	if (!itv->enc_mem) {
		IVTV_ERR("ioremap failed, perhaps increasing __VMALLOC_RESERVE in page.h\n");
		IVTV_ERR("or disabling CONFIG_HIMEM4G into the kernel would help\n");
		retval = -ENOMEM;
		goto free_mem;
	}

	if (itv->has_cx23415) {
		IVTV_DEBUG_INFO("attempting ioremap at 0x%08x len 0x%08x\n",
				itv->base_addr + IVTV_DECODER_OFFSET, IVTV_DECODER_SIZE);
		itv->dec_mem = ioremap_nocache(itv->base_addr + IVTV_DECODER_OFFSET,
				IVTV_DECODER_SIZE);
		if (!itv->dec_mem) {
			IVTV_ERR("ioremap failed, perhaps increasing __VMALLOC_RESERVE in page.h\n");
			IVTV_ERR("or disabling CONFIG_HIMEM4G into the kernel would help\n");
			retval = -ENOMEM;
			goto free_mem;
		}
	}
	else {
		itv->dec_mem = itv->enc_mem;
	}

	/* map registers memory */
	IVTV_DEBUG_INFO("attempting ioremap at 0x%08x len 0x%08x\n",
		   itv->base_addr + IVTV_REG_OFFSET, IVTV_REG_SIZE);
	itv->reg_mem =
	    ioremap_nocache(itv->base_addr + IVTV_REG_OFFSET, IVTV_REG_SIZE);
	if (!itv->reg_mem) {
		IVTV_ERR("ioremap failed, perhaps increasing __VMALLOC_RESERVE in page.h\n");
		IVTV_ERR("or disabling CONFIG_HIMEM4G into the kernel would help\n");
		retval = -ENOMEM;
		goto free_io;
	}

	/* Check yuv output filter table */
	if (itv->has_cx23415) ivtv_yuv_filter_check(itv);

	ivtv_gpio_init(itv);

	/* active i2c  */
	IVTV_DEBUG_INFO("activating i2c...\n");
	if (init_ivtv_i2c(itv)) {
		IVTV_ERR("Could not initialize i2c\n");
		goto free_io;
	}

	IVTV_DEBUG_INFO("Active card count: %d.\n", ivtv_cards_active);

	if (itv->card->hw_all & IVTV_HW_TVEEPROM) {
#ifdef CONFIG_VIDEO_TVEEPROM_MODULE
		ivtv_request_module(itv, "tveeprom");
#endif
		/* Based on the model number the cardtype may be changed.
		   The PCI IDs are not always reliable. */
		ivtv_process_eeprom(itv);
	}

	/* The mspx4xx chips need a longer delay for some reason */
	if (!(itv->hw_flags & IVTV_HW_MSP34XX))
		itv->i2c_algo.udelay = 5;

	if (itv->std == 0) {
		itv->std = V4L2_STD_NTSC_M;
	}

	if (itv->options.tuner == -1) {
		int i;

		for (i = 0; i < IVTV_CARD_MAX_TUNERS; i++) {
			if ((itv->std & itv->card->tuners[i].std) == 0)
				continue;
			itv->options.tuner = itv->card->tuners[i].tuner;
			break;
		}
	}
	/* if no tuner was found, then pick the first tuner in the card list */
	if (itv->options.tuner == -1 && itv->card->tuners[0].std) {
		itv->std = itv->card->tuners[0].std;
		itv->options.tuner = itv->card->tuners[0].tuner;
	}
	if (itv->options.radio == -1)
		itv->options.radio = (itv->card->radio_input.audio_type != 0);

	/* The card is now fully identified, continue with card-specific
	   initialization. */
	ivtv_init_struct2(itv);

	ivtv_load_and_init_modules(itv);

	if (itv->std & V4L2_STD_525_60) {
		itv->is_60hz = 1;
		itv->is_out_60hz = 1;
	} else {
		itv->is_50hz = 1;
		itv->is_out_50hz = 1;
	}
	itv->params.video_gop_size = itv->is_60hz ? 15 : 12;

	itv->stream_buf_size[IVTV_ENC_STREAM_TYPE_MPG] = 0x08000;
	itv->stream_buf_size[IVTV_ENC_STREAM_TYPE_PCM] = 0x01200;
	itv->stream_buf_size[IVTV_DEC_STREAM_TYPE_MPG] = 0x10000;

	/* 0x15180 == 720 * 480 / 4, 0x19500 == 720 * 576 / 4 */
	yuv_buf_size = itv->is_60hz ? 0x15180 : 0x19500;
	itv->stream_buf_size[IVTV_DEC_STREAM_TYPE_YUV] = yuv_buf_size / 2;
	itv->stream_buf_size[IVTV_ENC_STREAM_TYPE_YUV] = yuv_buf_size / 8;

	/* Setup VBI Raw Size. Should be big enough to hold PAL.
	   It is possible to switch between PAL and NTSC, so we need to
	   take the largest size here. */
	/* 1456 is multiple of 16, real size = 1444 */
	itv->vbi.raw_size = 1456;
	/* We use a buffer size of 1/2 of the total size needed for a
	   frame. This is actually very useful, since we now receive
	   a field at a time and that makes 'compressing' the raw data
	   down to size by stripping off the SAV codes a lot easier.
	   Note: having two different buffer sizes prevents standard
	   switching on the fly. We need to find a better solution... */
	vbi_buf_size = itv->vbi.raw_size * (itv->is_60hz ? 24 : 36) / 2;
	itv->stream_buf_size[IVTV_ENC_STREAM_TYPE_VBI] = vbi_buf_size;
	itv->stream_buf_size[IVTV_DEC_STREAM_TYPE_VBI] = sizeof(struct v4l2_sliced_vbi_data) * 36;

	if (itv->options.radio > 0)
		itv->v4l2_cap |= V4L2_CAP_RADIO;

	if (itv->options.tuner > -1 && itv->tunerid == 0) {
		struct tuner_setup setup;

		setup.addr = ADDR_UNSET;
		setup.type = itv->options.tuner;
		setup.mode_mask = T_ANALOG_TV;  /* matches TV tuners */
		ivtv_call_i2c_clients(itv, TUNER_SET_TYPE_ADDR, &setup);
	}

	/* The tuner is fixed to the standard. The other inputs (e.g. S-Video)
	   are not. */
	itv->tuner_std = itv->std;

	if (itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT) {
		ivtv_call_i2c_clients(itv, VIDIOC_INT_S_STD_OUTPUT, &itv->std);
	}

	retval = ivtv_streams_setup(itv);
	if (retval) {
		IVTV_ERR("Error %d setting up streams\n", retval);
		goto free_i2c;
	}

	IVTV_DEBUG_IRQ("Masking interrupts\n");
	/* clear interrupt mask, effectively disabling interrupts */
	ivtv_set_irq_mask(itv, 0xffffffff);

	/* Register IRQ */
	retval = request_irq(itv->dev->irq, ivtv_irq_handler,
			     IRQF_SHARED | IRQF_DISABLED, itv->name, (void *)itv);
	if (retval) {
		IVTV_ERR("Failed to register irq %d\n", retval);
		goto free_streams;
	}
	retval = ivtv_streams_register(itv);
	if (retval) {
		IVTV_ERR("Error %d registering devices\n", retval);
		goto free_irq;
	}
	IVTV_INFO("Initialized card #%d: %s\n", itv->num, itv->card_name);
	return 0;

      free_irq:
	free_irq(itv->dev->irq, (void *)itv);
      free_streams:
	ivtv_streams_cleanup(itv);
      free_i2c:
	exit_ivtv_i2c(itv);
      free_io:
	ivtv_iounmap(itv);
      free_mem:
	release_mem_region(itv->base_addr, IVTV_ENCODER_SIZE);
	release_mem_region(itv->base_addr + IVTV_REG_OFFSET, IVTV_REG_SIZE);
	if (itv->has_cx23415)
		release_mem_region(itv->base_addr + IVTV_DECODER_OFFSET, IVTV_DECODER_SIZE);
      free_workqueue:
	destroy_workqueue(itv->irq_work_queues);
      err:
	if (retval == 0)
		retval = -ENODEV;
	IVTV_ERR("Error %d on initialization\n", retval);

	spin_lock(&ivtv_cards_lock);
	kfree(ivtv_cards[ivtv_cards_active]);
	ivtv_cards[ivtv_cards_active] = NULL;
	spin_unlock(&ivtv_cards_lock);
	return retval;
}

int ivtv_init_on_first_open(struct ivtv *itv)
{
	struct v4l2_frequency vf;
	int fw_retry_count = 3;
	int video_input;

	if (test_bit(IVTV_F_I_FAILED, &itv->i_flags))
		return -ENXIO;

	if (test_and_set_bit(IVTV_F_I_INITED, &itv->i_flags))
		return 0;

	while (--fw_retry_count > 0) {
		/* load firmware */
		if (ivtv_firmware_init(itv) == 0)
			break;
		if (fw_retry_count > 1)
			IVTV_WARN("Retry loading firmware\n");
	}

	if (fw_retry_count == 0) {
		set_bit(IVTV_F_I_FAILED, &itv->i_flags);
		return -ENXIO;
	}

	/* Try and get firmware versions */
	IVTV_DEBUG_INFO("Getting firmware version..\n");
	ivtv_firmware_versions(itv);

	if (itv->card->hw_all & IVTV_HW_CX25840) {
		struct v4l2_control ctrl;

		/* CX25840_CID_ENABLE_PVR150_WORKAROUND */
		ctrl.id = V4L2_CID_PRIVATE_BASE;
		ctrl.value = itv->pvr150_workaround;
		itv->video_dec_func(itv, VIDIOC_S_CTRL, &ctrl);
	}

	vf.tuner = 0;
	vf.type = V4L2_TUNER_ANALOG_TV;
	vf.frequency = 6400; /* the tuner 'baseline' frequency */

	/* Set initial frequency. For PAL/SECAM broadcasts no
	   'default' channel exists AFAIK. */
	if (itv->std == V4L2_STD_NTSC_M_JP) {
		vf.frequency = 1460;	/* ch. 1 91250*16/1000 */
	}
	else if (itv->std & V4L2_STD_NTSC_M) {
		vf.frequency = 1076;	/* ch. 4 67250*16/1000 */
	}

	video_input = itv->active_input;
	itv->active_input++;	/* Force update of input */
	ivtv_v4l2_ioctls(itv, NULL, VIDIOC_S_INPUT, &video_input);

	/* Let the VIDIOC_S_STD ioctl do all the work, keeps the code
	   in one place. */
	itv->std++;		/* Force full standard initialization */
	itv->std_out = itv->std;
	ivtv_v4l2_ioctls(itv, NULL, VIDIOC_S_FREQUENCY, &vf);

	if (itv->card->v4l2_capabilities & V4L2_CAP_VIDEO_OUTPUT) {
		ivtv_init_mpeg_decoder(itv);
	}
	ivtv_v4l2_ioctls(itv, NULL, VIDIOC_S_STD, &itv->tuner_std);

	/* On a cx23416 this seems to be able to enable DMA to the chip? */
	if (!itv->has_cx23415)
		write_reg_sync(0x03, IVTV_REG_DMACONTROL);

	/* Default interrupts enabled. For the PVR350 this includes the
	   decoder VSYNC interrupt, which is always on. It is not only used
	   during decoding but also by the OSD.
	   Some old PVR250 cards had a cx23415, so testing for that is too
	   general. Instead test if the card has video output capability. */
	if (itv->v4l2_cap & V4L2_CAP_VIDEO_OUTPUT) {
		ivtv_clear_irq_mask(itv, IVTV_IRQ_MASK_INIT | IVTV_IRQ_DEC_VSYNC);
		ivtv_set_osd_alpha(itv);
	}
	else
		ivtv_clear_irq_mask(itv, IVTV_IRQ_MASK_INIT);
	return 0;
}

static void ivtv_remove(struct pci_dev *pci_dev)
{
	struct ivtv *itv = pci_get_drvdata(pci_dev);

	IVTV_DEBUG_INFO("Removing Card #%d\n", itv->num);

	if (test_bit(IVTV_F_I_INITED, &itv->i_flags)) {
		/* Stop all captures */
		IVTV_DEBUG_INFO("Stopping all streams\n");
		if (atomic_read(&itv->capturing) > 0)
			ivtv_stop_all_captures(itv);

		/* Stop all decoding */
		IVTV_DEBUG_INFO("Stopping decoding\n");
		if (atomic_read(&itv->decoding) > 0) {
			int type;

			if (test_bit(IVTV_F_I_DEC_YUV, &itv->i_flags))
				type = IVTV_DEC_STREAM_TYPE_YUV;
			else
				type = IVTV_DEC_STREAM_TYPE_MPG;
			ivtv_stop_v4l2_decode_stream(&itv->streams[type],
				VIDEO_CMD_STOP_TO_BLACK | VIDEO_CMD_STOP_IMMEDIATELY, 0);
		}
		ivtv_halt_firmware(itv);
	}

	/* Interrupts */
	ivtv_set_irq_mask(itv, 0xffffffff);
	del_timer_sync(&itv->dma_timer);

	/* Stop all Work Queues */
	flush_workqueue(itv->irq_work_queues);
	destroy_workqueue(itv->irq_work_queues);

	ivtv_streams_cleanup(itv);
	ivtv_udma_free(itv);

	exit_ivtv_i2c(itv);

	free_irq(itv->dev->irq, (void *)itv);
	ivtv_iounmap(itv);

	release_mem_region(itv->base_addr, IVTV_ENCODER_SIZE);
	release_mem_region(itv->base_addr + IVTV_REG_OFFSET, IVTV_REG_SIZE);
	if (itv->has_cx23415)
		release_mem_region(itv->base_addr + IVTV_DECODER_OFFSET, IVTV_DECODER_SIZE);

	pci_disable_device(itv->dev);

	IVTV_INFO("Removed %s, card #%d\n", itv->card_name, itv->num);
}

/* define a pci_driver for card detection */
static struct pci_driver ivtv_pci_driver = {
      .name =     "ivtv",
      .id_table = ivtv_pci_tbl,
      .probe =    ivtv_probe,
      .remove =   ivtv_remove,
};

static int module_start(void)
{
	printk(KERN_INFO "ivtv:  Start initialization, version %s\n", IVTV_VERSION);

	memset(ivtv_cards, 0, sizeof(ivtv_cards));

	/* Validate parameters */
	if (ivtv_first_minor < 0 || ivtv_first_minor >= IVTV_MAX_CARDS) {
		printk(KERN_ERR "ivtv:  Exiting, ivtv_first_minor must be between 0 and %d\n",
		     IVTV_MAX_CARDS - 1);
		return -1;
	}

	if (ivtv_debug < 0 || ivtv_debug > 2047) {
		ivtv_debug = 0;
		printk(KERN_INFO "ivtv:  Debug value must be >= 0 and <= 2047\n");
	}

	if (pci_register_driver(&ivtv_pci_driver)) {
		printk(KERN_ERR "ivtv:  Error detecting PCI card\n");
		return -ENODEV;
	}
	printk(KERN_INFO "ivtv:  End initialization\n");
	return 0;
}

static void module_cleanup(void)
{
	int i, j;

	pci_unregister_driver(&ivtv_pci_driver);

	spin_lock(&ivtv_cards_lock);
	for (i = 0; i < ivtv_cards_active; i++) {
		if (ivtv_cards[i] == NULL)
			continue;
		for (j = 0; j < IVTV_VBI_FRAMES; j++) {
			kfree(ivtv_cards[i]->vbi.sliced_mpeg_data[j]);
		}
		kfree(ivtv_cards[i]);
	}
	spin_unlock(&ivtv_cards_lock);
}

/* Note: These symbols are exported because they are used by the ivtvfb
   framebuffer module and an infrared module for the IR-blaster. */
EXPORT_SYMBOL(ivtv_set_irq_mask);
EXPORT_SYMBOL(ivtv_cards_active);
EXPORT_SYMBOL(ivtv_cards);
EXPORT_SYMBOL(ivtv_cards_lock);
EXPORT_SYMBOL(ivtv_api);
EXPORT_SYMBOL(ivtv_vapi);
EXPORT_SYMBOL(ivtv_vapi_result);
EXPORT_SYMBOL(ivtv_clear_irq_mask);
EXPORT_SYMBOL(ivtv_debug);
EXPORT_SYMBOL(ivtv_reset_ir_gpio);
EXPORT_SYMBOL(ivtv_udma_setup);
EXPORT_SYMBOL(ivtv_udma_unmap);
EXPORT_SYMBOL(ivtv_udma_alloc);
EXPORT_SYMBOL(ivtv_udma_prepare);
EXPORT_SYMBOL(ivtv_init_on_first_open);

module_init(module_start);
module_exit(module_cleanup);
