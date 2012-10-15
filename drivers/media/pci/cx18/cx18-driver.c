/*
 *  cx18 driver initialization and card probing
 *
 *  Derived from ivtv-driver.c
 *
 *  Copyright (C) 2007  Hans Verkuil <hverkuil@xs4all.nl>
 *  Copyright (C) 2008  Andy Walls <awalls@md.metrocast.net>
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
#include "cx18-io.h"
#include "cx18-version.h"
#include "cx18-cards.h"
#include "cx18-i2c.h"
#include "cx18-irq.h"
#include "cx18-gpio.h"
#include "cx18-firmware.h"
#include "cx18-queue.h"
#include "cx18-streams.h"
#include "cx18-av-core.h"
#include "cx18-scb.h"
#include "cx18-mailbox.h"
#include "cx18-ioctl.h"
#include "cx18-controls.h"
#include "tuner-xc2028.h"
#include <linux/dma-mapping.h>
#include <media/tveeprom.h>

/* If you have already X v4l cards, then set this to X. This way
   the device numbers stay matched. Example: you have a WinTV card
   without radio and a Compro H900 with. Normally this would give a
   video1 device together with a radio0 device for the Compro. By
   setting this to 1 you ensure that radio0 is now also radio1. */
int cx18_first_minor;

/* Callback for registering extensions */
int (*cx18_ext_init)(struct cx18 *);
EXPORT_SYMBOL(cx18_ext_init);

/* add your revision and whatnot here */
static struct pci_device_id cx18_pci_tbl[] __devinitdata = {
	{PCI_VENDOR_ID_CX, PCI_DEVICE_ID_CX23418,
	 PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{0,}
};

MODULE_DEVICE_TABLE(pci, cx18_pci_tbl);

static atomic_t cx18_instance = ATOMIC_INIT(0);

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
static unsigned cardtype_c = 1;
static unsigned tuner_c = 1;
static unsigned radio_c = 1;
static char pal[] = "--";
static char secam[] = "--";
static char ntsc[] = "-";

/* Buffers */
static int enc_ts_buffers = CX18_DEFAULT_ENC_TS_BUFFERS;
static int enc_mpg_buffers = CX18_DEFAULT_ENC_MPG_BUFFERS;
static int enc_idx_buffers = CX18_DEFAULT_ENC_IDX_BUFFERS;
static int enc_yuv_buffers = CX18_DEFAULT_ENC_YUV_BUFFERS;
static int enc_vbi_buffers = CX18_DEFAULT_ENC_VBI_BUFFERS;
static int enc_pcm_buffers = CX18_DEFAULT_ENC_PCM_BUFFERS;

static int enc_ts_bufsize = CX18_DEFAULT_ENC_TS_BUFSIZE;
static int enc_mpg_bufsize = CX18_DEFAULT_ENC_MPG_BUFSIZE;
static int enc_idx_bufsize = CX18_DEFAULT_ENC_IDX_BUFSIZE;
static int enc_yuv_bufsize = CX18_DEFAULT_ENC_YUV_BUFSIZE;
static int enc_pcm_bufsize = CX18_DEFAULT_ENC_PCM_BUFSIZE;

static int enc_ts_bufs = -1;
static int enc_mpg_bufs = -1;
static int enc_idx_bufs = CX18_MAX_FW_MDLS_PER_STREAM;
static int enc_yuv_bufs = -1;
static int enc_vbi_bufs = -1;
static int enc_pcm_bufs = -1;


static int cx18_pci_latency = 1;

static int mmio_ndelay;
static int retry_mmio = 1;

int cx18_debug;

module_param_array(tuner, int, &tuner_c, 0644);
module_param_array(radio, int, &radio_c, 0644);
module_param_array(cardtype, int, &cardtype_c, 0644);
module_param_string(pal, pal, sizeof(pal), 0644);
module_param_string(secam, secam, sizeof(secam), 0644);
module_param_string(ntsc, ntsc, sizeof(ntsc), 0644);
module_param_named(debug, cx18_debug, int, 0644);
module_param(mmio_ndelay, int, 0644);
module_param(retry_mmio, int, 0644);
module_param(cx18_pci_latency, int, 0644);
module_param(cx18_first_minor, int, 0644);

module_param(enc_ts_buffers, int, 0644);
module_param(enc_mpg_buffers, int, 0644);
module_param(enc_idx_buffers, int, 0644);
module_param(enc_yuv_buffers, int, 0644);
module_param(enc_vbi_buffers, int, 0644);
module_param(enc_pcm_buffers, int, 0644);

module_param(enc_ts_bufsize, int, 0644);
module_param(enc_mpg_bufsize, int, 0644);
module_param(enc_idx_bufsize, int, 0644);
module_param(enc_yuv_bufsize, int, 0644);
module_param(enc_pcm_bufsize, int, 0644);

module_param(enc_ts_bufs, int, 0644);
module_param(enc_mpg_bufs, int, 0644);
module_param(enc_idx_bufs, int, 0644);
module_param(enc_yuv_bufs, int, 0644);
module_param(enc_vbi_bufs, int, 0644);
module_param(enc_pcm_bufs, int, 0644);

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
		 "\t\t\t 5 = Conexant Raptor PAL/SECAM\n"
		 "\t\t\t 6 = Toshiba Qosmio DVB-T/Analog\n"
		 "\t\t\t 7 = Leadtek WinFast PVR2100\n"
		 "\t\t\t 8 = Leadtek WinFast DVR3100 H\n"
		 "\t\t\t 9 = GoTView PCI DVD3 Hybrid\n"
		 "\t\t\t 10 = Hauppauge HVR 1600 (S5H1411)\n"
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
MODULE_PARM_DESC(retry_mmio,
		 "(Deprecated) MMIO writes are now always checked and retried\n"
		 "\t\t\tEffectively: 1 [Yes]");
MODULE_PARM_DESC(mmio_ndelay,
		 "(Deprecated) MMIO accesses are now never purposely delayed\n"
		 "\t\t\tEffectively: 0 ns");
MODULE_PARM_DESC(enc_ts_buffers,
		 "Encoder TS buffer memory (MB). (enc_ts_bufs can override)\n"
		 "\t\t\tDefault: " __stringify(CX18_DEFAULT_ENC_TS_BUFFERS));
MODULE_PARM_DESC(enc_ts_bufsize,
		 "Size of an encoder TS buffer (kB)\n"
		 "\t\t\tDefault: " __stringify(CX18_DEFAULT_ENC_TS_BUFSIZE));
MODULE_PARM_DESC(enc_ts_bufs,
		 "Number of encoder TS buffers\n"
		 "\t\t\tDefault is computed from other enc_ts_* parameters");
MODULE_PARM_DESC(enc_mpg_buffers,
		 "Encoder MPG buffer memory (MB). (enc_mpg_bufs can override)\n"
		 "\t\t\tDefault: " __stringify(CX18_DEFAULT_ENC_MPG_BUFFERS));
MODULE_PARM_DESC(enc_mpg_bufsize,
		 "Size of an encoder MPG buffer (kB)\n"
		 "\t\t\tDefault: " __stringify(CX18_DEFAULT_ENC_MPG_BUFSIZE));
MODULE_PARM_DESC(enc_mpg_bufs,
		 "Number of encoder MPG buffers\n"
		 "\t\t\tDefault is computed from other enc_mpg_* parameters");
MODULE_PARM_DESC(enc_idx_buffers,
		 "(Deprecated) Encoder IDX buffer memory (MB)\n"
		 "\t\t\tIgnored, except 0 disables IDX buffer allocations\n"
		 "\t\t\tDefault: 1 [Enabled]");
MODULE_PARM_DESC(enc_idx_bufsize,
		 "Size of an encoder IDX buffer (kB)\n"
		 "\t\t\tAllowed values are multiples of 1.5 kB rounded up\n"
		 "\t\t\t(multiples of size required for 64 index entries)\n"
		 "\t\t\tDefault: 2");
MODULE_PARM_DESC(enc_idx_bufs,
		 "Number of encoder IDX buffers\n"
		 "\t\t\tDefault: " __stringify(CX18_MAX_FW_MDLS_PER_STREAM));
MODULE_PARM_DESC(enc_yuv_buffers,
		 "Encoder YUV buffer memory (MB). (enc_yuv_bufs can override)\n"
		 "\t\t\tDefault: " __stringify(CX18_DEFAULT_ENC_YUV_BUFFERS));
MODULE_PARM_DESC(enc_yuv_bufsize,
		 "Size of an encoder YUV buffer (kB)\n"
		 "\t\t\tAllowed values are multiples of 33.75 kB rounded up\n"
		 "\t\t\t(multiples of size required for 32 screen lines)\n"
		 "\t\t\tDefault: 102");
MODULE_PARM_DESC(enc_yuv_bufs,
		 "Number of encoder YUV buffers\n"
		 "\t\t\tDefault is computed from other enc_yuv_* parameters");
MODULE_PARM_DESC(enc_vbi_buffers,
		 "Encoder VBI buffer memory (MB). (enc_vbi_bufs can override)\n"
		 "\t\t\tDefault: " __stringify(CX18_DEFAULT_ENC_VBI_BUFFERS));
MODULE_PARM_DESC(enc_vbi_bufs,
		 "Number of encoder VBI buffers\n"
		 "\t\t\tDefault is computed from enc_vbi_buffers");
MODULE_PARM_DESC(enc_pcm_buffers,
		 "Encoder PCM buffer memory (MB). (enc_pcm_bufs can override)\n"
		 "\t\t\tDefault: " __stringify(CX18_DEFAULT_ENC_PCM_BUFFERS));
MODULE_PARM_DESC(enc_pcm_bufsize,
		 "Size of an encoder PCM buffer (kB)\n"
		 "\t\t\tDefault: " __stringify(CX18_DEFAULT_ENC_PCM_BUFSIZE));
MODULE_PARM_DESC(enc_pcm_bufs,
		 "Number of encoder PCM buffers\n"
		 "\t\t\tDefault is computed from other enc_pcm_* parameters");

MODULE_PARM_DESC(cx18_first_minor,
		 "Set device node number assigned to first card");

MODULE_AUTHOR("Hans Verkuil");
MODULE_DESCRIPTION("CX23418 driver");
MODULE_SUPPORTED_DEVICE("CX23418 MPEG2 encoder");
MODULE_LICENSE("GPL");

MODULE_VERSION(CX18_VERSION);

#if defined(CONFIG_MODULES) && defined(MODULE)
static void request_module_async(struct work_struct *work)
{
	struct cx18 *dev = container_of(work, struct cx18, request_module_wk);

	/* Make sure cx18-alsa module is loaded */
	request_module("cx18-alsa");

	/* Initialize cx18-alsa for this instance of the cx18 device */
	if (cx18_ext_init != NULL)
		cx18_ext_init(dev);
}

static void request_modules(struct cx18 *dev)
{
	INIT_WORK(&dev->request_module_wk, request_module_async);
	schedule_work(&dev->request_module_wk);
}

static void flush_request_modules(struct cx18 *dev)
{
	flush_work(&dev->request_module_wk);
}
#else
#define request_modules(dev)
#define flush_request_modules(dev)
#endif /* CONFIG_MODULES */

/* Generic utility functions */
int cx18_msleep_timeout(unsigned int msecs, int intr)
{
	long int timeout = msecs_to_jiffies(msecs);
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

static void cx18_eeprom_dump(struct cx18 *cx, unsigned char *eedata, int len)
{
	int i;

	CX18_INFO("eeprom dump:\n");
	for (i = 0; i < len; i++) {
		if (0 == (i % 16))
			CX18_INFO("eeprom %02x:", i);
		printk(KERN_CONT " %02x", eedata[i]);
		if (15 == (i % 16))
			printk(KERN_CONT "\n");
	}
}

/* Hauppauge card? get values from tveeprom */
void cx18_read_eeprom(struct cx18 *cx, struct tveeprom *tv)
{
	struct i2c_client c;
	u8 eedata[256];

	memset(&c, 0, sizeof(c));
	strlcpy(c.name, "cx18 tveeprom tmp", sizeof(c.name));
	c.adapter = &cx->i2c_adap[0];
	c.addr = 0xA0 >> 1;

	memset(tv, 0, sizeof(*tv));
	if (tveeprom_read(&c, eedata, sizeof(eedata)))
		return;

	switch (cx->card->type) {
	case CX18_CARD_HVR_1600_ESMT:
	case CX18_CARD_HVR_1600_SAMSUNG:
	case CX18_CARD_HVR_1600_S5H1411:
		tveeprom_hauppauge_analog(&c, tv, eedata);
		break;
	case CX18_CARD_YUAN_MPC718:
	case CX18_CARD_GOTVIEW_PCI_DVD3:
		tv->model = 0x718;
		cx18_eeprom_dump(cx, eedata, sizeof(eedata));
		CX18_INFO("eeprom PCI ID: %02x%02x:%02x%02x\n",
			  eedata[2], eedata[1], eedata[4], eedata[3]);
		break;
	default:
		tv->model = 0xffffffff;
		cx18_eeprom_dump(cx, eedata, sizeof(eedata));
		break;
	}
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
	case 74301: /* Retail models */
	case 74321:
	case 74351: /* OEM models */
	case 74361:
		/* Digital side is s5h1411/tda18271 */
		cx->card = cx18_get_card(CX18_CARD_HVR_1600_S5H1411);
		break;
	case 74021: /* Retail models */
	case 74031:
	case 74041:
	case 74141:
	case 74541: /* OEM models */
	case 74551:
	case 74591:
	case 74651:
	case 74691:
	case 74751:
	case 74891:
		/* Digital side is s5h1409/mxl5005s */
		cx->card = cx18_get_card(CX18_CARD_HVR_1600_ESMT);
		break;
	case 0x718:
		return;
	case 0xffffffff:
		CX18_INFO("Unknown EEPROM encoding\n");
		return;
	case 0:
		CX18_ERR("Invalid EEPROM\n");
		return;
	default:
		CX18_ERR("Unknown model %d, defaulting to original HVR-1600 "
			 "(cardtype=1)\n", tv.model);
		cx->card = cx18_get_card(CX18_CARD_HVR_1600_ESMT);
		break;
	}

	cx->v4l2_cap = cx->card->v4l2_capabilities;
	cx->card_name = cx->card->name;
	cx->card_i2c = cx->card->i2c;

	CX18_INFO("Autodetected %s\n", cx->card_name);

	if (tv.tuner_type == TUNER_ABSENT)
		CX18_ERR("tveeprom cannot autodetect tuner!\n");

	if (cx->options.tuner == -1)
		cx->options.tuner = tv.tuner_type;
	if (cx->options.radio == -1)
		cx->options.radio = (tv.has_radio != 0);

	if (cx->std != 0)
		/* user specified tuner standard */
		return;

	/* autodetect tuner standard */
#define TVEEPROM_TUNER_FORMAT_ALL (V4L2_STD_B  | V4L2_STD_GH | \
				   V4L2_STD_MN | \
				   V4L2_STD_PAL_I | \
				   V4L2_STD_SECAM_L | V4L2_STD_SECAM_LC | \
				   V4L2_STD_DK)
	if ((tv.tuner_formats & TVEEPROM_TUNER_FORMAT_ALL)
					== TVEEPROM_TUNER_FORMAT_ALL) {
		CX18_DEBUG_INFO("Worldwide tuner detected\n");
		cx->std = V4L2_STD_ALL;
	} else if (tv.tuner_formats & V4L2_STD_PAL) {
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

	cx->options.megabytes[CX18_ENC_STREAM_TYPE_TS] = enc_ts_buffers;
	cx->options.megabytes[CX18_ENC_STREAM_TYPE_MPG] = enc_mpg_buffers;
	cx->options.megabytes[CX18_ENC_STREAM_TYPE_IDX] = enc_idx_buffers;
	cx->options.megabytes[CX18_ENC_STREAM_TYPE_YUV] = enc_yuv_buffers;
	cx->options.megabytes[CX18_ENC_STREAM_TYPE_VBI] = enc_vbi_buffers;
	cx->options.megabytes[CX18_ENC_STREAM_TYPE_PCM] = enc_pcm_buffers;
	cx->options.megabytes[CX18_ENC_STREAM_TYPE_RAD] = 0; /* control only */

	cx->stream_buffers[CX18_ENC_STREAM_TYPE_TS] = enc_ts_bufs;
	cx->stream_buffers[CX18_ENC_STREAM_TYPE_MPG] = enc_mpg_bufs;
	cx->stream_buffers[CX18_ENC_STREAM_TYPE_IDX] = enc_idx_bufs;
	cx->stream_buffers[CX18_ENC_STREAM_TYPE_YUV] = enc_yuv_bufs;
	cx->stream_buffers[CX18_ENC_STREAM_TYPE_VBI] = enc_vbi_bufs;
	cx->stream_buffers[CX18_ENC_STREAM_TYPE_PCM] = enc_pcm_bufs;
	cx->stream_buffers[CX18_ENC_STREAM_TYPE_RAD] = 0; /* control, no data */

	cx->stream_buf_size[CX18_ENC_STREAM_TYPE_TS] = enc_ts_bufsize;
	cx->stream_buf_size[CX18_ENC_STREAM_TYPE_MPG] = enc_mpg_bufsize;
	cx->stream_buf_size[CX18_ENC_STREAM_TYPE_IDX] = enc_idx_bufsize;
	cx->stream_buf_size[CX18_ENC_STREAM_TYPE_YUV] = enc_yuv_bufsize;
	cx->stream_buf_size[CX18_ENC_STREAM_TYPE_VBI] = vbi_active_samples * 36;
	cx->stream_buf_size[CX18_ENC_STREAM_TYPE_PCM] = enc_pcm_bufsize;
	cx->stream_buf_size[CX18_ENC_STREAM_TYPE_RAD] = 0; /* control no data */

	/* Ensure stream_buffers & stream_buf_size are valid */
	for (i = 0; i < CX18_MAX_STREAMS; i++) {
		if (cx->stream_buffers[i] == 0 ||     /* User said 0 buffers */
		    cx->options.megabytes[i] <= 0 ||  /* User said 0 MB total */
		    cx->stream_buf_size[i] <= 0) {    /* User said buf size 0 */
			cx->options.megabytes[i] = 0;
			cx->stream_buffers[i] = 0;
			cx->stream_buf_size[i] = 0;
			continue;
		}
		/*
		 * YUV is a special case where the stream_buf_size needs to be
		 * an integral multiple of 33.75 kB (storage for 32 screens
		 * lines to maintain alignment in case of lost buffers).
		 *
		 * IDX is a special case where the stream_buf_size should be
		 * an integral multiple of 1.5 kB (storage for 64 index entries
		 * to maintain alignment in case of lost buffers).
		 *
		 */
		if (i == CX18_ENC_STREAM_TYPE_YUV) {
			cx->stream_buf_size[i] *= 1024;
			cx->stream_buf_size[i] -=
			   (cx->stream_buf_size[i] % CX18_UNIT_ENC_YUV_BUFSIZE);

			if (cx->stream_buf_size[i] < CX18_UNIT_ENC_YUV_BUFSIZE)
				cx->stream_buf_size[i] =
						CX18_UNIT_ENC_YUV_BUFSIZE;
		} else if (i == CX18_ENC_STREAM_TYPE_IDX) {
			cx->stream_buf_size[i] *= 1024;
			cx->stream_buf_size[i] -=
			   (cx->stream_buf_size[i] % CX18_UNIT_ENC_IDX_BUFSIZE);

			if (cx->stream_buf_size[i] < CX18_UNIT_ENC_IDX_BUFSIZE)
				cx->stream_buf_size[i] =
						CX18_UNIT_ENC_IDX_BUFSIZE;
		}
		/*
		 * YUV and IDX are special cases where the stream_buf_size is
		 * now in bytes.
		 * VBI is a special case where the stream_buf_size is fixed
		 * and already in bytes
		 */
		if (i == CX18_ENC_STREAM_TYPE_VBI ||
		    i == CX18_ENC_STREAM_TYPE_YUV ||
		    i == CX18_ENC_STREAM_TYPE_IDX) {
			if (cx->stream_buffers[i] < 0) {
				cx->stream_buffers[i] =
					cx->options.megabytes[i] * 1024 * 1024
					/ cx->stream_buf_size[i];
			} else {
				/* N.B. This might round down to 0 */
				cx->options.megabytes[i] =
					cx->stream_buffers[i]
					* cx->stream_buf_size[i]/(1024 * 1024);
			}
		} else {
			/* All other streams have stream_buf_size in kB here */
			if (cx->stream_buffers[i] < 0) {
				cx->stream_buffers[i] =
						cx->options.megabytes[i] * 1024
						/ cx->stream_buf_size[i];
			} else {
				/* N.B. This might round down to 0 */
				cx->options.megabytes[i] =
						cx->stream_buffers[i]
						* cx->stream_buf_size[i] / 1024;
			}
			/* convert from kB to bytes */
			cx->stream_buf_size[i] *= 1024;
		}
		CX18_DEBUG_INFO("Stream type %d options: %d MB, %d buffers, "
				"%d bytes\n", i, cx->options.megabytes[i],
				cx->stream_buffers[i], cx->stream_buf_size[i]);
	}

	cx->options.cardtype = cardtype[cx->instance];
	cx->options.tuner = tuner[cx->instance];
	cx->options.radio = radio[cx->instance];

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
		if (cx->pci_dev->subsystem_vendor == CX18_PCI_ID_HAUPPAUGE) {
			cx->card = cx18_get_card(CX18_CARD_HVR_1600_ESMT);
			CX18_INFO("Autodetected Hauppauge card\n");
		}
	}
	if (cx->card == NULL) {
		for (i = 0; (cx->card = cx18_get_card(i)); i++) {
			if (cx->card->pci_list == NULL)
				continue;
			for (j = 0; cx->card->pci_list[j].device; j++) {
				if (cx->pci_dev->device !=
				    cx->card->pci_list[j].device)
					continue;
				if (cx->pci_dev->subsystem_vendor !=
				    cx->card->pci_list[j].subsystem_vendor)
					continue;
				if (cx->pci_dev->subsystem_device !=
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
		CX18_ERR("Unknown card: vendor/device: [%04x:%04x]\n",
			 cx->pci_dev->vendor, cx->pci_dev->device);
		CX18_ERR("              subsystem vendor/device: [%04x:%04x]\n",
			 cx->pci_dev->subsystem_vendor,
			 cx->pci_dev->subsystem_device);
		CX18_ERR("Defaulting to %s card\n", cx->card->name);
		CX18_ERR("Please mail the vendor/device and subsystem vendor/device IDs and what kind of\n");
		CX18_ERR("card you have to the ivtv-devel mailinglist (www.ivtvdriver.org)\n");
		CX18_ERR("Prefix your subject line with [UNKNOWN CX18 CARD].\n");
	}
	cx->v4l2_cap = cx->card->v4l2_capabilities;
	cx->card_name = cx->card->name;
	cx->card_i2c = cx->card->i2c;
}

static int __devinit cx18_create_in_workq(struct cx18 *cx)
{
	snprintf(cx->in_workq_name, sizeof(cx->in_workq_name), "%s-in",
		 cx->v4l2_dev.name);
	cx->in_work_queue = alloc_ordered_workqueue(cx->in_workq_name, 0);
	if (cx->in_work_queue == NULL) {
		CX18_ERR("Unable to create incoming mailbox handler thread\n");
		return -ENOMEM;
	}
	return 0;
}

static void __devinit cx18_init_in_work_orders(struct cx18 *cx)
{
	int i;
	for (i = 0; i < CX18_MAX_IN_WORK_ORDERS; i++) {
		cx->in_work_order[i].cx = cx;
		cx->in_work_order[i].str = cx->epu_debug_str;
		INIT_WORK(&cx->in_work_order[i].work, cx18_in_work_handler);
	}
}

/* Precondition: the cx18 structure has been memset to 0. Only
   the dev and instance fields have been filled in.
   No assumptions on the card type may be made here (see cx18_init_struct2
   for that).
 */
static int __devinit cx18_init_struct1(struct cx18 *cx)
{
	int ret;

	cx->base_addr = pci_resource_start(cx->pci_dev, 0);

	mutex_init(&cx->serialize_lock);
	mutex_init(&cx->gpio_lock);
	mutex_init(&cx->epu2apu_mb_lock);
	mutex_init(&cx->epu2cpu_mb_lock);

	ret = cx18_create_in_workq(cx);
	if (ret)
		return ret;

	cx18_init_in_work_orders(cx);

	/* start counting open_id at 1 */
	cx->open_id = 1;

	/* Initial settings */
	cx->cxhdl.port = CX2341X_PORT_MEMORY;
	cx->cxhdl.capabilities = CX2341X_CAP_HAS_TS | CX2341X_CAP_HAS_SLICED_VBI;
	cx->cxhdl.ops = &cx18_cxhdl_ops;
	cx->cxhdl.func = cx18_api_func;
	cx->cxhdl.priv = &cx->streams[CX18_ENC_STREAM_TYPE_MPG];
	ret = cx2341x_handler_init(&cx->cxhdl, 50);
	if (ret)
		return ret;
	cx->v4l2_dev.ctrl_handler = &cx->cxhdl.hdl;

	cx->temporal_strength = cx->cxhdl.video_temporal_filter->cur.val;
	cx->spatial_strength = cx->cxhdl.video_spatial_filter->cur.val;
	cx->filter_mode = cx->cxhdl.video_spatial_filter_mode->cur.val |
		(cx->cxhdl.video_temporal_filter_mode->cur.val << 1) |
		(cx->cxhdl.video_median_filter_type->cur.val << 2);

	init_waitqueue_head(&cx->cap_w);
	init_waitqueue_head(&cx->mb_apu_waitq);
	init_waitqueue_head(&cx->mb_cpu_waitq);
	init_waitqueue_head(&cx->dma_waitq);

	/* VBI */
	cx->vbi.in.type = V4L2_BUF_TYPE_VBI_CAPTURE;
	cx->vbi.sliced_in = &cx->vbi.in.fmt.sliced;

	/* IVTV style VBI insertion into MPEG streams */
	INIT_LIST_HEAD(&cx->vbi.sliced_mpeg_buf.list);
	INIT_LIST_HEAD(&cx->vbi.sliced_mpeg_mdl.list);
	INIT_LIST_HEAD(&cx->vbi.sliced_mpeg_mdl.buf_list);
	list_add(&cx->vbi.sliced_mpeg_buf.list,
		 &cx->vbi.sliced_mpeg_mdl.buf_list);
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
}

static int cx18_setup_pci(struct cx18 *cx, struct pci_dev *pci_dev,
			  const struct pci_device_id *pci_id)
{
	u16 cmd;
	unsigned char pci_latency;

	CX18_DEBUG_INFO("Enabling pci device\n");

	if (pci_enable_device(pci_dev)) {
		CX18_ERR("Can't enable device %d!\n", cx->instance);
		return -EIO;
	}
	if (pci_set_dma_mask(pci_dev, DMA_BIT_MASK(32))) {
		CX18_ERR("No suitable DMA available, card %d\n", cx->instance);
		return -EIO;
	}
	if (!request_mem_region(cx->base_addr, CX18_MEM_SIZE, "cx18 encoder")) {
		CX18_ERR("Cannot request encoder memory region, card %d\n",
			 cx->instance);
		return -EIO;
	}

	/* Enable bus mastering and memory mapped IO for the CX23418 */
	pci_read_config_word(pci_dev, PCI_COMMAND, &cmd);
	cmd |= PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER;
	pci_write_config_word(pci_dev, PCI_COMMAND, cmd);

	cx->card_rev = pci_dev->revision;
	pci_read_config_byte(pci_dev, PCI_LATENCY_TIMER, &pci_latency);

	if (pci_latency < 64 && cx18_pci_latency) {
		CX18_INFO("Unreasonably low latency timer, "
			       "setting to 64 (was %d)\n", pci_latency);
		pci_write_config_byte(pci_dev, PCI_LATENCY_TIMER, 64);
		pci_read_config_byte(pci_dev, PCI_LATENCY_TIMER, &pci_latency);
	}

	CX18_DEBUG_INFO("cx%d (rev %d) at %02x:%02x.%x, "
		   "irq: %d, latency: %d, memory: 0x%llx\n",
		   cx->pci_dev->device, cx->card_rev, pci_dev->bus->number,
		   PCI_SLOT(pci_dev->devfn), PCI_FUNC(pci_dev->devfn),
		   cx->pci_dev->irq, pci_latency, (u64)cx->base_addr);

	return 0;
}

static void cx18_init_subdevs(struct cx18 *cx)
{
	u32 hw = cx->card->hw_all;
	u32 device;
	int i;

	for (i = 0, device = 1; i < 32; i++, device <<= 1) {

		if (!(device & hw))
			continue;

		switch (device) {
		case CX18_HW_DVB:
		case CX18_HW_TVEEPROM:
			/* These subordinate devices do not use probing */
			cx->hw_flags |= device;
			break;
		case CX18_HW_418_AV:
			/* The A/V decoder gets probed earlier to set PLLs */
			/* Just note that the card uses it (i.e. has analog) */
			cx->hw_flags |= device;
			break;
		case CX18_HW_GPIO_RESET_CTRL:
			/*
			 * The Reset Controller gets probed and added to
			 * hw_flags earlier for i2c adapter/bus initialization
			 */
			break;
		case CX18_HW_GPIO_MUX:
			if (cx18_gpio_register(cx, device) == 0)
				cx->hw_flags |= device;
			break;
		default:
			if (cx18_i2c_register(cx, i) == 0)
				cx->hw_flags |= device;
			break;
		}
	}

	if (cx->hw_flags & CX18_HW_418_AV)
		cx->sd_av = cx18_find_hw(cx, CX18_HW_418_AV);

	if (cx->card->hw_muxer != 0)
		cx->sd_extmux = cx18_find_hw(cx, cx->card->hw_muxer);
}

static int __devinit cx18_probe(struct pci_dev *pci_dev,
				const struct pci_device_id *pci_id)
{
	int retval = 0;
	int i;
	u32 devtype;
	struct cx18 *cx;

	/* FIXME - module parameter arrays constrain max instances */
	i = atomic_inc_return(&cx18_instance) - 1;
	if (i >= CX18_MAX_CARDS) {
		printk(KERN_ERR "cx18: cannot manage card %d, driver has a "
		       "limit of 0 - %d\n", i, CX18_MAX_CARDS - 1);
		return -ENOMEM;
	}

	cx = kzalloc(sizeof(struct cx18), GFP_ATOMIC);
	if (cx == NULL) {
		printk(KERN_ERR "cx18: cannot manage card %d, out of memory\n",
		       i);
		return -ENOMEM;
	}
	cx->pci_dev = pci_dev;
	cx->instance = i;

	retval = v4l2_device_register(&pci_dev->dev, &cx->v4l2_dev);
	if (retval) {
		printk(KERN_ERR "cx18: v4l2_device_register of card %d failed"
		       "\n", cx->instance);
		kfree(cx);
		return retval;
	}
	snprintf(cx->v4l2_dev.name, sizeof(cx->v4l2_dev.name), "cx18-%d",
		 cx->instance);
	CX18_INFO("Initializing card %d\n", cx->instance);

	cx18_process_options(cx);
	if (cx->options.cardtype == -1) {
		retval = -ENODEV;
		goto err;
	}

	retval = cx18_init_struct1(cx);
	if (retval)
		goto err;

	CX18_DEBUG_INFO("base addr: 0x%llx\n", (u64)cx->base_addr);

	/* PCI Device Setup */
	retval = cx18_setup_pci(cx, pci_dev, pci_id);
	if (retval != 0)
		goto free_workqueues;

	/* map io memory */
	CX18_DEBUG_INFO("attempting ioremap at 0x%llx len 0x%08x\n",
		   (u64)cx->base_addr + CX18_MEM_OFFSET, CX18_MEM_SIZE);
	cx->enc_mem = ioremap_nocache(cx->base_addr + CX18_MEM_OFFSET,
				       CX18_MEM_SIZE);
	if (!cx->enc_mem) {
		CX18_ERR("ioremap failed. Can't get a window into CX23418 "
			 "memory and register space\n");
		CX18_ERR("Each capture card with a CX23418 needs 64 MB of "
			 "vmalloc address space for the window\n");
		CX18_ERR("Check the output of 'grep Vmalloc /proc/meminfo'\n");
		CX18_ERR("Use the vmalloc= kernel command line option to set "
			 "VmallocTotal to a larger value\n");
		retval = -ENOMEM;
		goto free_mem;
	}
	cx->reg_mem = cx->enc_mem + CX18_REG_OFFSET;
	devtype = cx18_read_reg(cx, 0xC72028);
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

	cx->scb = (struct cx18_scb __iomem *)(cx->enc_mem + SCB_OFFSET);
	cx18_init_scb(cx);

	cx18_gpio_init(cx);

	/* Initialize integrated A/V decoder early to set PLLs, just in case */
	retval = cx18_av_probe(cx);
	if (retval) {
		CX18_ERR("Could not register A/V decoder subdevice\n");
		goto free_map;
	}

	/* Initialize GPIO Reset Controller to do chip resets during i2c init */
	if (cx->card->hw_all & CX18_HW_GPIO_RESET_CTRL) {
		if (cx18_gpio_register(cx, CX18_HW_GPIO_RESET_CTRL) != 0)
			CX18_WARN("Could not register GPIO reset controller"
				  "subdevice; proceeding anyway.\n");
		else
			cx->hw_flags |= CX18_HW_GPIO_RESET_CTRL;
	}

	/* active i2c  */
	CX18_DEBUG_INFO("activating i2c...\n");
	retval = init_cx18_i2c(cx);
	if (retval) {
		CX18_ERR("Could not initialize i2c\n");
		goto free_map;
	}

	if (cx->card->hw_all & CX18_HW_TVEEPROM) {
		/* Based on the model number the cardtype may be changed.
		   The PCI IDs are not always reliable. */
		const struct cx18_card *orig_card = cx->card;
		cx18_process_eeprom(cx);

		if (cx->card != orig_card) {
			/* Changed the cardtype; re-reset the I2C chips */
			cx18_gpio_init(cx);
			cx18_call_hw(cx, CX18_HW_GPIO_RESET_CTRL,
					core, reset, (u32) CX18_GPIO_RESET_I2C);
		}
	}
	if (cx->card->comment)
		CX18_INFO("%s", cx->card->comment);
	if (cx->card->v4l2_capabilities == 0) {
		retval = -ENODEV;
		goto free_i2c;
	}
	cx18_init_memory(cx);
	cx18_init_scb(cx);

	/* Register IRQ */
	retval = request_irq(cx->pci_dev->irq, cx18_irq_handler,
			     IRQF_SHARED | IRQF_DISABLED,
			     cx->v4l2_dev.name, (void *)cx);
	if (retval) {
		CX18_ERR("Failed to register irq %d\n", retval);
		goto free_i2c;
	}

	if (cx->std == 0)
		cx->std = V4L2_STD_NTSC_M;

	if (cx->options.tuner == -1) {
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
		if (cx->std & V4L2_STD_PAL)
			cx->std = V4L2_STD_PAL_BG | V4L2_STD_PAL_H;
		else if (cx->std & V4L2_STD_NTSC)
			cx->std = V4L2_STD_NTSC_M;
		else if (cx->std & V4L2_STD_SECAM)
			cx->std = V4L2_STD_SECAM_L;
		cx->options.tuner = cx->card->tuners[0].tuner;
	}
	if (cx->options.radio == -1)
		cx->options.radio = (cx->card->radio_input.audio_type != 0);

	/* The card is now fully identified, continue with card-specific
	   initialization. */
	cx18_init_struct2(cx);

	cx18_init_subdevs(cx);

	if (cx->std & V4L2_STD_525_60)
		cx->is_60hz = 1;
	else
		cx->is_50hz = 1;

	cx2341x_handler_set_50hz(&cx->cxhdl, !cx->is_60hz);

	if (cx->options.radio > 0)
		cx->v4l2_cap |= V4L2_CAP_RADIO;

	if (cx->options.tuner > -1) {
		struct tuner_setup setup;

		setup.addr = ADDR_UNSET;
		setup.type = cx->options.tuner;
		setup.mode_mask = T_ANALOG_TV;  /* matches TV tuners */
		if (cx->options.radio > 0)
			setup.mode_mask |= T_RADIO;
		setup.tuner_callback = (setup.type == TUNER_XC2028) ?
			cx18_reset_tuner_gpio : NULL;
		cx18_call_all(cx, tuner, s_type_addr, &setup);
		if (setup.type == TUNER_XC2028) {
			static struct xc2028_ctrl ctrl = {
				.fname = XC2028_DEFAULT_FIRMWARE,
				.max_len = 64,
			};
			struct v4l2_priv_tun_config cfg = {
				.tuner = cx->options.tuner,
				.priv = &ctrl,
			};
			cx18_call_all(cx, tuner, s_config, &cfg);
		}
	}

	/* The tuner is fixed to the standard. The other inputs (e.g. S-Video)
	   are not. */
	cx->tuner_std = cx->std;
	if (cx->std == V4L2_STD_ALL)
		cx->std = V4L2_STD_NTSC_M;

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

	CX18_INFO("Initialized card: %s\n", cx->card_name);

	/* Load cx18 submodules (cx18-alsa) */
	request_modules(cx);
	return 0;

free_streams:
	cx18_streams_cleanup(cx, 1);
free_irq:
	free_irq(cx->pci_dev->irq, (void *)cx);
free_i2c:
	exit_cx18_i2c(cx);
free_map:
	cx18_iounmap(cx);
free_mem:
	release_mem_region(cx->base_addr, CX18_MEM_SIZE);
free_workqueues:
	destroy_workqueue(cx->in_work_queue);
err:
	if (retval == 0)
		retval = -ENODEV;
	CX18_ERR("Error %d on initialization\n", retval);

	v4l2_device_unregister(&cx->v4l2_dev);
	kfree(cx);
	return retval;
}

int cx18_init_on_first_open(struct cx18 *cx)
{
	int video_input;
	int fw_retry_count = 3;
	struct v4l2_frequency vf;
	struct cx18_open_id fh;
	v4l2_std_id std;

	fh.cx = cx;

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

	/*
	 * Init the firmware twice to work around a silicon bug
	 * with the digital TS.
	 *
	 * The second firmware load requires us to normalize the APU state,
	 * or the audio for the first analog capture will be badly incorrect.
	 *
	 * I can't seem to call APU_RESETAI and have it succeed without the
	 * APU capturing audio, so we start and stop it here to do the reset
	 */

	/* MPEG Encoding, 224 kbps, MPEG Layer II, 48 ksps */
	cx18_vapi(cx, CX18_APU_START, 2, CX18_APU_ENCODING_METHOD_MPEG|0xb9, 0);
	cx18_vapi(cx, CX18_APU_RESETAI, 0);
	cx18_vapi(cx, CX18_APU_STOP, 1, CX18_APU_ENCODING_METHOD_MPEG);

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

	/*
	 * The second firmware load requires us to normalize the APU state,
	 * or the audio for the first analog capture will be badly incorrect.
	 *
	 * I can't seem to call APU_RESETAI and have it succeed without the
	 * APU capturing audio, so we start and stop it here to do the reset
	 */

	/* MPEG Encoding, 224 kbps, MPEG Layer II, 48 ksps */
	cx18_vapi(cx, CX18_APU_START, 2, CX18_APU_ENCODING_METHOD_MPEG|0xb9, 0);
	cx18_vapi(cx, CX18_APU_RESETAI, 0);
	cx18_vapi(cx, CX18_APU_STOP, 1, CX18_APU_ENCODING_METHOD_MPEG);

	/* Init the A/V decoder, if it hasn't been already */
	v4l2_subdev_call(cx->sd_av, core, load_fw);

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
	cx18_s_input(NULL, &fh, video_input);

	/* Let the VIDIOC_S_STD ioctl do all the work, keeps the code
	   in one place. */
	cx->std++;		/* Force full standard initialization */
	std = (cx->tuner_std == V4L2_STD_ALL) ? V4L2_STD_NTSC_M : cx->tuner_std;
	cx18_s_std(NULL, &fh, &std);
	cx18_s_frequency(NULL, &fh, &vf);
	return 0;
}

static void cx18_cancel_in_work_orders(struct cx18 *cx)
{
	int i;
	for (i = 0; i < CX18_MAX_IN_WORK_ORDERS; i++)
		cancel_work_sync(&cx->in_work_order[i].work);
}

static void cx18_cancel_out_work_orders(struct cx18 *cx)
{
	int i;
	for (i = 0; i < CX18_MAX_STREAMS; i++)
		if (&cx->streams[i].video_dev != NULL)
			cancel_work_sync(&cx->streams[i].out_work_order);
}

static void cx18_remove(struct pci_dev *pci_dev)
{
	struct v4l2_device *v4l2_dev = pci_get_drvdata(pci_dev);
	struct cx18 *cx = to_cx18(v4l2_dev);
	int i;

	CX18_DEBUG_INFO("Removing Card\n");

	flush_request_modules(cx);

	/* Stop all captures */
	CX18_DEBUG_INFO("Stopping all streams\n");
	if (atomic_read(&cx->tot_capturing) > 0)
		cx18_stop_all_captures(cx);

	/* Stop interrupts that cause incoming work to be queued */
	cx18_sw1_irq_disable(cx, IRQ_CPU_TO_EPU | IRQ_APU_TO_EPU);

	/* Incoming work can cause outgoing work, so clean up incoming first */
	cx18_cancel_in_work_orders(cx);
	cx18_cancel_out_work_orders(cx);

	/* Stop ack interrupts that may have been needed for work to finish */
	cx18_sw2_irq_disable(cx, IRQ_CPU_TO_EPU_ACK | IRQ_APU_TO_EPU_ACK);

	cx18_halt_firmware(cx);

	destroy_workqueue(cx->in_work_queue);

	cx18_streams_cleanup(cx, 1);

	exit_cx18_i2c(cx);

	free_irq(cx->pci_dev->irq, (void *)cx);

	cx18_iounmap(cx);

	release_mem_region(cx->base_addr, CX18_MEM_SIZE);

	pci_disable_device(cx->pci_dev);

	if (cx->vbi.sliced_mpeg_data[0] != NULL)
		for (i = 0; i < CX18_VBI_FRAMES; i++)
			kfree(cx->vbi.sliced_mpeg_data[i]);

	v4l2_ctrl_handler_free(&cx->av_state.hdl);

	CX18_INFO("Removed %s\n", cx->card_name);

	v4l2_device_unregister(v4l2_dev);
	kfree(cx);
}


/* define a pci_driver for card detection */
static struct pci_driver cx18_pci_driver = {
      .name =     "cx18",
      .id_table = cx18_pci_tbl,
      .probe =    cx18_probe,
      .remove =   cx18_remove,
};

static int __init module_start(void)
{
	printk(KERN_INFO "cx18:  Start initialization, version %s\n",
	       CX18_VERSION);

	/* Validate parameters */
	if (cx18_first_minor < 0 || cx18_first_minor >= CX18_MAX_CARDS) {
		printk(KERN_ERR "cx18:  Exiting, cx18_first_minor must be between 0 and %d\n",
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

static void __exit module_cleanup(void)
{
	pci_unregister_driver(&cx18_pci_driver);
}

module_init(module_start);
module_exit(module_cleanup);
MODULE_FIRMWARE(XC2028_DEFAULT_FIRMWARE);
