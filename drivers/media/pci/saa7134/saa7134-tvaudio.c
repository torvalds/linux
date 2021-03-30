// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *
 * device driver for philips saa7134 based TV cards
 * tv audio decoder (fm stereo, nicam, ...)
 *
 * (c) 2001-03 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
 */

#include "saa7134.h"
#include "saa7134-reg.h"

#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/freezer.h>
#include <asm/div64.h>

/* ------------------------------------------------------------------ */

static unsigned int audio_debug;
module_param(audio_debug, int, 0644);
MODULE_PARM_DESC(audio_debug,"enable debug messages [tv audio]");

static unsigned int audio_ddep;
module_param(audio_ddep, int, 0644);
MODULE_PARM_DESC(audio_ddep,"audio ddep overwrite");

static int audio_clock_override = UNSET;
module_param(audio_clock_override, int, 0644);

static int audio_clock_tweak;
module_param(audio_clock_tweak, int, 0644);
MODULE_PARM_DESC(audio_clock_tweak, "Audio clock tick fine tuning for cards with audio crystal that's slightly off (range [-1024 .. 1024])");

#define audio_dbg(level, fmt, arg...) do { \
	if (audio_debug >= level) \
		printk(KERN_DEBUG pr_fmt("audio: " fmt), ## arg); \
	} while (0)

/* msecs */
#define SCAN_INITIAL_DELAY     1000
#define SCAN_SAMPLE_DELAY       200
#define SCAN_SUBCARRIER_DELAY  2000

/* ------------------------------------------------------------------ */
/* saa7134 code                                                       */

static struct mainscan {
	char         *name;
	v4l2_std_id  std;
	int          carr;
} mainscan[] = {
	{
		.name = "MN",
		.std  = V4L2_STD_MN,
		.carr = 4500,
	},{
		.name = "BGH",
		.std  = V4L2_STD_B | V4L2_STD_GH,
		.carr = 5500,
	},{
		.name = "I",
		.std  = V4L2_STD_PAL_I,
		.carr = 6000,
	},{
		.name = "DKL",
		.std  = V4L2_STD_DK | V4L2_STD_SECAM_L | V4L2_STD_SECAM_LC,
		.carr = 6500,
	}
};

static struct saa7134_tvaudio tvaudio[] = {
	{
		.name          = "PAL-B/G FM-stereo",
		.std           = V4L2_STD_PAL_BG,
		.mode          = TVAUDIO_FM_BG_STEREO,
		.carr1         = 5500,
		.carr2         = 5742,
	},{
		.name          = "PAL-D/K1 FM-stereo",
		.std           = V4L2_STD_PAL_DK,
		.carr1         = 6500,
		.carr2         = 6258,
		.mode          = TVAUDIO_FM_BG_STEREO,
	},{
		.name          = "PAL-D/K2 FM-stereo",
		.std           = V4L2_STD_PAL_DK,
		.carr1         = 6500,
		.carr2         = 6742,
		.mode          = TVAUDIO_FM_BG_STEREO,
	},{
		.name          = "PAL-D/K3 FM-stereo",
		.std           = V4L2_STD_PAL_DK,
		.carr1         = 6500,
		.carr2         = 5742,
		.mode          = TVAUDIO_FM_BG_STEREO,
	},{
		.name          = "PAL-B/G NICAM",
		.std           = V4L2_STD_PAL_BG,
		.carr1         = 5500,
		.carr2         = 5850,
		.mode          = TVAUDIO_NICAM_FM,
	},{
		.name          = "PAL-I NICAM",
		.std           = V4L2_STD_PAL_I,
		.carr1         = 6000,
		.carr2         = 6552,
		.mode          = TVAUDIO_NICAM_FM,
	},{
		.name          = "PAL-D/K NICAM",
		.std           = V4L2_STD_PAL_DK,
		.carr1         = 6500,
		.carr2         = 5850,
		.mode          = TVAUDIO_NICAM_FM,
	},{
		.name          = "SECAM-L NICAM",
		.std           = V4L2_STD_SECAM_L,
		.carr1         = 6500,
		.carr2         = 5850,
		.mode          = TVAUDIO_NICAM_AM,
	},{
		.name          = "SECAM-D/K NICAM",
		.std           = V4L2_STD_SECAM_DK,
		.carr1         = 6500,
		.carr2         = 5850,
		.mode          = TVAUDIO_NICAM_FM,
	},{
		.name          = "NTSC-A2 FM-stereo",
		.std           = V4L2_STD_NTSC,
		.carr1         = 4500,
		.carr2         = 4724,
		.mode          = TVAUDIO_FM_K_STEREO,
	},{
		.name          = "NTSC-M",
		.std           = V4L2_STD_NTSC,
		.carr1         = 4500,
		.carr2         = -1,
		.mode          = TVAUDIO_FM_MONO,
	}
};
#define TVAUDIO ARRAY_SIZE(tvaudio)

/* ------------------------------------------------------------------ */

static u32 tvaudio_carr2reg(u32 carrier)
{
	u64 a = carrier;

	a <<= 24;
	do_div(a,12288);
	return a;
}

static void tvaudio_setcarrier(struct saa7134_dev *dev,
			       int primary, int secondary)
{
	if (-1 == secondary)
		secondary = primary;
	saa_writel(SAA7134_CARRIER1_FREQ0 >> 2, tvaudio_carr2reg(primary));
	saa_writel(SAA7134_CARRIER2_FREQ0 >> 2, tvaudio_carr2reg(secondary));
}

#define SAA7134_MUTE_MASK 0xbb
#define SAA7134_MUTE_ANALOG 0x04
#define SAA7134_MUTE_I2S 0x40

static void mute_input_7134(struct saa7134_dev *dev)
{
	unsigned int mute;
	struct saa7134_input *in;
	int ausel=0, ics=0, ocs=0;
	int mask;

	/* look what is to do ... */
	in   = dev->input;
	mute = (dev->ctl_mute ||
		(dev->automute  &&  (&card(dev).radio) != in));
	if (card(dev).mute.type) {
		/*
		 * 7130 - we'll mute using some unconnected audio input
		 * 7134 - we'll probably should switch external mux with gpio
		 */
		if (mute)
			in = &card(dev).mute;
	}

	if (dev->hw_mute  == mute &&
		dev->hw_input == in && !dev->insuspend) {
		audio_dbg(1, "mute/input: nothing to do [mute=%d,input=%s]\n",
			  mute, saa7134_input_name[in->type]);
		return;
	}

	audio_dbg(1, "ctl_mute=%d automute=%d input=%s  =>  mute=%d input=%s\n",
		  dev->ctl_mute, dev->automute,
		  saa7134_input_name[dev->input->type], mute,
		  saa7134_input_name[in->type]);
	dev->hw_mute  = mute;
	dev->hw_input = in;

	if (PCI_DEVICE_ID_PHILIPS_SAA7134 == dev->pci->device)
		/* 7134 mute */
		saa_writeb(SAA7134_AUDIO_MUTE_CTRL, mute ?
						    SAA7134_MUTE_MASK |
						    SAA7134_MUTE_ANALOG |
						    SAA7134_MUTE_I2S :
						    SAA7134_MUTE_MASK);

	/* switch internal audio mux */
	switch (in->amux) {
	case TV:         ausel=0xc0; ics=0x00; ocs=0x02; break;
	case LINE1:      ausel=0x80; ics=0x00; ocs=0x00; break;
	case LINE2:      ausel=0x80; ics=0x08; ocs=0x01; break;
	case LINE2_LEFT: ausel=0x80; ics=0x08; ocs=0x05; break;
	}
	saa_andorb(SAA7134_AUDIO_FORMAT_CTRL, 0xc0, ausel);
	saa_andorb(SAA7134_ANALOG_IO_SELECT, 0x08, ics);
	saa_andorb(SAA7134_ANALOG_IO_SELECT, 0x07, ocs);
	// for oss, we need to change the clock configuration
	if (in->amux == TV)
		saa_andorb(SAA7134_SIF_SAMPLE_FREQ,   0x03, 0x00);
	else
		saa_andorb(SAA7134_SIF_SAMPLE_FREQ,   0x03, 0x01);

	/* switch gpio-connected external audio mux */
	if (0 == card(dev).gpiomask)
		return;

	mask = card(dev).gpiomask;
	saa_andorl(SAA7134_GPIO_GPMODE0 >> 2,   mask, mask);
	saa_andorl(SAA7134_GPIO_GPSTATUS0 >> 2, mask, in->gpio);
	saa7134_track_gpio(dev, saa7134_input_name[in->type]);
}

static void tvaudio_setmode(struct saa7134_dev *dev,
			    struct saa7134_tvaudio *audio,
			    char *note)
{
	int acpf, tweak = 0;

	if (dev->tvnorm->id == V4L2_STD_NTSC) {
		acpf = 0x19066;
	} else {
		acpf = 0x1e000;
	}
	if (audio_clock_tweak > -1024 && audio_clock_tweak < 1024)
		tweak = audio_clock_tweak;

	if (note)
		audio_dbg(1, "tvaudio_setmode: %s %s [%d.%03d/%d.%03d MHz] acpf=%d%+d\n",
			note, audio->name,
			audio->carr1 / 1000, audio->carr1 % 1000,
			audio->carr2 / 1000, audio->carr2 % 1000,
			acpf, tweak);

	acpf += tweak;
	saa_writeb(SAA7134_AUDIO_CLOCKS_PER_FIELD0, (acpf & 0x0000ff) >> 0);
	saa_writeb(SAA7134_AUDIO_CLOCKS_PER_FIELD1, (acpf & 0x00ff00) >> 8);
	saa_writeb(SAA7134_AUDIO_CLOCKS_PER_FIELD2, (acpf & 0x030000) >> 16);
	tvaudio_setcarrier(dev,audio->carr1,audio->carr2);

	switch (audio->mode) {
	case TVAUDIO_FM_MONO:
	case TVAUDIO_FM_BG_STEREO:
		saa_writeb(SAA7134_DEMODULATOR,               0x00);
		saa_writeb(SAA7134_DCXO_IDENT_CTRL,           0x00);
		saa_writeb(SAA7134_FM_DEEMPHASIS,             0x22);
		saa_writeb(SAA7134_FM_DEMATRIX,               0x80);
		saa_writeb(SAA7134_STEREO_DAC_OUTPUT_SELECT,  0xa0);
		break;
	case TVAUDIO_FM_K_STEREO:
		saa_writeb(SAA7134_DEMODULATOR,               0x00);
		saa_writeb(SAA7134_DCXO_IDENT_CTRL,           0x01);
		saa_writeb(SAA7134_FM_DEEMPHASIS,             0x22);
		saa_writeb(SAA7134_FM_DEMATRIX,               0x80);
		saa_writeb(SAA7134_STEREO_DAC_OUTPUT_SELECT,  0xa0);
		break;
	case TVAUDIO_NICAM_FM:
		saa_writeb(SAA7134_DEMODULATOR,               0x10);
		saa_writeb(SAA7134_DCXO_IDENT_CTRL,           0x00);
		saa_writeb(SAA7134_FM_DEEMPHASIS,             0x44);
		saa_writeb(SAA7134_STEREO_DAC_OUTPUT_SELECT,  0xa1);
		saa_writeb(SAA7134_NICAM_CONFIG,              0x00);
		break;
	case TVAUDIO_NICAM_AM:
		saa_writeb(SAA7134_DEMODULATOR,               0x12);
		saa_writeb(SAA7134_DCXO_IDENT_CTRL,           0x00);
		saa_writeb(SAA7134_FM_DEEMPHASIS,             0x44);
		saa_writeb(SAA7134_STEREO_DAC_OUTPUT_SELECT,  0xa1);
		saa_writeb(SAA7134_NICAM_CONFIG,              0x00);
		break;
	case TVAUDIO_FM_SAT_STEREO:
		/* not implemented (yet) */
		break;
	}
}

static int tvaudio_sleep(struct saa7134_dev *dev, int timeout)
{
	if (dev->thread.scan1 == dev->thread.scan2 &&
	    !kthread_should_stop()) {
		if (timeout < 0) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule();
		} else {
			schedule_timeout_interruptible
						(msecs_to_jiffies(timeout));
		}
	}
	return dev->thread.scan1 != dev->thread.scan2;
}

static int tvaudio_checkcarrier(struct saa7134_dev *dev, struct mainscan *scan)
{
	__s32 left,right,value;

	if (!(dev->tvnorm->id & scan->std)) {
		audio_dbg(1, "skipping %d.%03d MHz [%4s]\n",
			  scan->carr / 1000, scan->carr % 1000, scan->name);
		return 0;
	}

	if (audio_debug > 1) {
		int i;
		audio_dbg(1, "debug %d:", scan->carr);
		for (i = -150; i <= 150; i += 30) {
			tvaudio_setcarrier(dev,scan->carr+i,scan->carr+i);
			saa_readl(SAA7134_LEVEL_READOUT1 >> 2);
			if (tvaudio_sleep(dev,SCAN_SAMPLE_DELAY))
				return -1;
			value = saa_readl(SAA7134_LEVEL_READOUT1 >> 2);
			if (0 == i)
				pr_cont("  #  %6d  # ", value >> 16);
			else
				pr_cont(" %6d", value >> 16);
		}
		pr_cont("\n");
	}

	tvaudio_setcarrier(dev,scan->carr-90,scan->carr-90);
	saa_readl(SAA7134_LEVEL_READOUT1 >> 2);
	if (tvaudio_sleep(dev,SCAN_SAMPLE_DELAY))
		return -1;
	left = saa_readl(SAA7134_LEVEL_READOUT1 >> 2);

	tvaudio_setcarrier(dev,scan->carr+90,scan->carr+90);
	saa_readl(SAA7134_LEVEL_READOUT1 >> 2);
	if (tvaudio_sleep(dev,SCAN_SAMPLE_DELAY))
		return -1;
	right = saa_readl(SAA7134_LEVEL_READOUT1 >> 2);

	left >>= 16;
	right >>= 16;
	value = left > right ? left - right : right - left;
	audio_dbg(1, "scanning %d.%03d MHz [%4s] =>  dc is %5d [%d/%d]\n",
		  scan->carr / 1000, scan->carr % 1000,
		  scan->name, value, left, right);
	return value;
}


static int tvaudio_getstereo(struct saa7134_dev *dev, struct saa7134_tvaudio *audio)
{
	__u32 idp, nicam, nicam_status;
	int retval = -1;

	switch (audio->mode) {
	case TVAUDIO_FM_MONO:
		return V4L2_TUNER_SUB_MONO;
	case TVAUDIO_FM_K_STEREO:
	case TVAUDIO_FM_BG_STEREO:
		idp = (saa_readb(SAA7134_IDENT_SIF) & 0xe0) >> 5;
		audio_dbg(1, "getstereo: fm/stereo: idp=0x%x\n", idp);
		if (0x03 == (idp & 0x03))
			retval = V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;
		else if (0x05 == (idp & 0x05))
			retval = V4L2_TUNER_SUB_MONO | V4L2_TUNER_SUB_STEREO;
		else if (0x01 == (idp & 0x01))
			retval = V4L2_TUNER_SUB_MONO;
		break;
	case TVAUDIO_FM_SAT_STEREO:
		/* not implemented (yet) */
		break;
	case TVAUDIO_NICAM_FM:
	case TVAUDIO_NICAM_AM:
		nicam = saa_readb(SAA7134_AUDIO_STATUS);
		audio_dbg(1, "getstereo: nicam=0x%x\n", nicam);
		if (nicam & 0x1) {
			nicam_status = saa_readb(SAA7134_NICAM_STATUS);
			audio_dbg(1, "getstereo: nicam_status=0x%x\n",
				  nicam_status);

			switch (nicam_status & 0x03) {
			    case 0x01:
				retval = V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;
				break;
			    case 0x02:
				retval = V4L2_TUNER_SUB_MONO | V4L2_TUNER_SUB_STEREO;
				break;
			    default:
				retval = V4L2_TUNER_SUB_MONO;
			}
		} else {
			/* No nicam detected */
		}
		break;
	}
	if (retval != -1)
		audio_dbg(1, "found audio subchannels:%s%s%s%s\n",
			(retval & V4L2_TUNER_SUB_MONO)   ? " mono"   : "",
			(retval & V4L2_TUNER_SUB_STEREO) ? " stereo" : "",
			(retval & V4L2_TUNER_SUB_LANG1)  ? " lang1"  : "",
			(retval & V4L2_TUNER_SUB_LANG2)  ? " lang2"  : "");
	return retval;
}

static int tvaudio_setstereo(struct saa7134_dev *dev, struct saa7134_tvaudio *audio,
			     u32 mode)
{
	static char *name[] = {
		[ V4L2_TUNER_MODE_MONO   ] = "mono",
		[ V4L2_TUNER_MODE_STEREO ] = "stereo",
		[ V4L2_TUNER_MODE_LANG1  ] = "lang1",
		[ V4L2_TUNER_MODE_LANG2  ] = "lang2",
		[ V4L2_TUNER_MODE_LANG1_LANG2  ] = "lang1+lang2",
	};
	static u32 fm[] = {
		[ V4L2_TUNER_MODE_MONO   ] = 0x00,  /* ch1  */
		[ V4L2_TUNER_MODE_STEREO ] = 0x80,  /* auto */
		[ V4L2_TUNER_MODE_LANG1  ] = 0x00,  /* ch1  */
		[ V4L2_TUNER_MODE_LANG2  ] = 0x01,  /* ch2  */
		[ V4L2_TUNER_MODE_LANG1_LANG2 ] = 0x80,  /* auto */
	};
	u32 reg;

	switch (audio->mode) {
	case TVAUDIO_FM_MONO:
		/* nothing to do ... */
		break;
	case TVAUDIO_FM_K_STEREO:
	case TVAUDIO_FM_BG_STEREO:
	case TVAUDIO_NICAM_AM:
	case TVAUDIO_NICAM_FM:
		audio_dbg(1, "setstereo [fm] => %s\n",
			  name[mode % ARRAY_SIZE(name)]);
		reg = fm[ mode % ARRAY_SIZE(fm) ];
		saa_writeb(SAA7134_FM_DEMATRIX, reg);
		break;
	case TVAUDIO_FM_SAT_STEREO:
		/* Not implemented */
		break;
	}
	return 0;
}

static int tvaudio_thread(void *data)
{
	struct saa7134_dev *dev = data;
	int carr_vals[ARRAY_SIZE(mainscan)];
	unsigned int i, audio, nscan;
	int max1,max2,carrier,rx,mode,lastmode,default_carrier;

	set_freezable();

	for (;;) {
		tvaudio_sleep(dev,-1);
		if (kthread_should_stop())
			goto done;

	restart:
		try_to_freeze();

		dev->thread.scan1 = dev->thread.scan2;
		audio_dbg(1, "tvaudio thread scan start [%d]\n",
			  dev->thread.scan1);
		dev->tvaudio  = NULL;

		saa_writeb(SAA7134_MONITOR_SELECT,   0xa0);
		saa_writeb(SAA7134_FM_DEMATRIX,      0x80);

		if (dev->ctl_automute)
			dev->automute = 1;

		mute_input_7134(dev);

		/* give the tuner some time */
		if (tvaudio_sleep(dev,SCAN_INITIAL_DELAY))
			goto restart;

		max1 = 0;
		max2 = 0;
		nscan = 0;
		carrier = 0;
		default_carrier = 0;
		for (i = 0; i < ARRAY_SIZE(mainscan); i++) {
			if (!(dev->tvnorm->id & mainscan[i].std))
				continue;
			if (!default_carrier)
				default_carrier = mainscan[i].carr;
			nscan++;
		}

		if (1 == nscan) {
			/* only one candidate -- skip scan ;) */
			audio_dbg(1, "only one main carrier candidate - skipping scan\n");
			max1 = 12345;
			carrier = default_carrier;
		} else {
			/* scan for the main carrier */
			saa_writeb(SAA7134_MONITOR_SELECT,0x00);
			tvaudio_setmode(dev,&tvaudio[0],NULL);
			for (i = 0; i < ARRAY_SIZE(mainscan); i++) {
				carr_vals[i] = tvaudio_checkcarrier(dev, mainscan+i);
				if (dev->thread.scan1 != dev->thread.scan2)
					goto restart;
			}
			for (max1 = 0, max2 = 0, i = 0; i < ARRAY_SIZE(mainscan); i++) {
				if (max1 < carr_vals[i]) {
					max2 = max1;
					max1 = carr_vals[i];
					carrier = mainscan[i].carr;
				} else if (max2 < carr_vals[i]) {
					max2 = carr_vals[i];
				}
			}
		}

		if (0 != carrier && max1 > 2000 && max1 > max2*3) {
			/* found good carrier */
			audio_dbg(1, "found %s main sound carrier @ %d.%03d MHz [%d/%d]\n",
				  dev->tvnorm->name, carrier/1000, carrier%1000,
				  max1, max2);
			dev->last_carrier = carrier;
			dev->automute = 0;

		} else if (0 != dev->last_carrier) {
			/* no carrier -- try last detected one as fallback */
			carrier = dev->last_carrier;
			audio_dbg(1, "audio carrier scan failed, using %d.%03d MHz [last detected]\n",
				  carrier/1000, carrier%1000);
			dev->automute = 1;

		} else {
			/* no carrier + no fallback -- use default */
			carrier = default_carrier;
			audio_dbg(1, "audio carrier scan failed, using %d.%03d MHz [default]\n",
				  carrier/1000, carrier%1000);
			dev->automute = 1;
		}
		tvaudio_setcarrier(dev,carrier,carrier);
		saa_andorb(SAA7134_STEREO_DAC_OUTPUT_SELECT, 0x30, 0x00);
		saa7134_tvaudio_setmute(dev);
		/* find the exact tv audio norm */
		for (audio = UNSET, i = 0; i < TVAUDIO; i++) {
			if (dev->tvnorm->id != UNSET &&
				!(dev->tvnorm->id & tvaudio[i].std))
				continue;
			if (tvaudio[i].carr1 != carrier)
				continue;
			/* Note: at least the primary carrier is right here */
			if (UNSET == audio)
				audio = i;
			tvaudio_setmode(dev,&tvaudio[i],"trying");
			if (tvaudio_sleep(dev,SCAN_SUBCARRIER_DELAY))
				goto restart;
			if (-1 != tvaudio_getstereo(dev,&tvaudio[i])) {
				audio = i;
				break;
			}
		}
		saa_andorb(SAA7134_STEREO_DAC_OUTPUT_SELECT, 0x30, 0x30);
		if (UNSET == audio)
			continue;
		tvaudio_setmode(dev,&tvaudio[audio],"using");

		tvaudio_setstereo(dev,&tvaudio[audio],V4L2_TUNER_MODE_MONO);
		dev->tvaudio = &tvaudio[audio];

		lastmode = 42;
		for (;;) {

			try_to_freeze();

			if (tvaudio_sleep(dev,5000))
				goto restart;
			if (kthread_should_stop())
				break;
			if (UNSET == dev->thread.mode) {
				rx = tvaudio_getstereo(dev, &tvaudio[audio]);
				mode = saa7134_tvaudio_rx2mode(rx);
			} else {
				mode = dev->thread.mode;
			}
			if (lastmode != mode) {
				tvaudio_setstereo(dev,&tvaudio[audio],mode);
				lastmode = mode;
			}
		}
	}

 done:
	dev->thread.stopped = 1;
	return 0;
}

/* ------------------------------------------------------------------ */
/* saa7133 / saa7135 code                                             */

static char *stdres[0x20] = {
	[0x00] = "no standard detected",
	[0x01] = "B/G (in progress)",
	[0x02] = "D/K (in progress)",
	[0x03] = "M (in progress)",

	[0x04] = "B/G A2",
	[0x05] = "B/G NICAM",
	[0x06] = "D/K A2 (1)",
	[0x07] = "D/K A2 (2)",
	[0x08] = "D/K A2 (3)",
	[0x09] = "D/K NICAM",
	[0x0a] = "L NICAM",
	[0x0b] = "I NICAM",

	[0x0c] = "M Korea",
	[0x0d] = "M BTSC ",
	[0x0e] = "M EIAJ",

	[0x0f] = "FM radio / IF 10.7 / 50 deemp",
	[0x10] = "FM radio / IF 10.7 / 75 deemp",
	[0x11] = "FM radio / IF sel / 50 deemp",
	[0x12] = "FM radio / IF sel / 75 deemp",

	[0x13 ... 0x1e ] = "unknown",
	[0x1f] = "??? [in progress]",
};

#define DSP_RETRY 32
#define DSP_DELAY 16
#define SAA7135_DSP_RWCLEAR_RERR 1

static inline int saa_dsp_reset_error_bit(struct saa7134_dev *dev)
{
	int state = saa_readb(SAA7135_DSP_RWSTATE);
	if (unlikely(state & SAA7135_DSP_RWSTATE_ERR)) {
		audio_dbg(2, "%s: resetting error bit\n", dev->name);
		saa_writeb(SAA7135_DSP_RWCLEAR, SAA7135_DSP_RWCLEAR_RERR);
	}
	return 0;
}

static inline int saa_dsp_wait_bit(struct saa7134_dev *dev, int bit)
{
	int state, count = DSP_RETRY;

	state = saa_readb(SAA7135_DSP_RWSTATE);
	if (unlikely(state & SAA7135_DSP_RWSTATE_ERR)) {
		pr_warn("%s: dsp access error\n", dev->name);
		saa_dsp_reset_error_bit(dev);
		return -EIO;
	}
	while (0 == (state & bit)) {
		if (unlikely(0 == count)) {
			pr_err("dsp access wait timeout [bit=%s]\n",
				 (bit & SAA7135_DSP_RWSTATE_WRR) ? "WRR" :
				 (bit & SAA7135_DSP_RWSTATE_RDB) ? "RDB" :
				 (bit & SAA7135_DSP_RWSTATE_IDA) ? "IDA" :
				 "???");
			return -EIO;
		}
		saa_wait(DSP_DELAY);
		state = saa_readb(SAA7135_DSP_RWSTATE);
		count--;
	}
	return 0;
}


int saa_dsp_writel(struct saa7134_dev *dev, int reg, u32 value)
{
	int err;

	audio_dbg(2, "dsp write reg 0x%x = 0x%06x\n",
		  (reg << 2) & 0xffffffff, value);
	err = saa_dsp_wait_bit(dev,SAA7135_DSP_RWSTATE_WRR);
	if (err < 0)
		return err;
	saa_writel(reg,value);
	err = saa_dsp_wait_bit(dev,SAA7135_DSP_RWSTATE_WRR);
	if (err < 0)
		return err;
	return 0;
}

static int getstereo_7133(struct saa7134_dev *dev)
{
	int retval = V4L2_TUNER_SUB_MONO;
	u32 value;

	value = saa_readl(0x528 >> 2);
	if (value & 0x20)
		retval = V4L2_TUNER_SUB_MONO | V4L2_TUNER_SUB_STEREO;
	if (value & 0x40)
		retval = V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;
	return retval;
}

static int mute_input_7133(struct saa7134_dev *dev)
{
	u32 reg = 0;
	u32 xbarin, xbarout;
	int mask;
	struct saa7134_input *in;

	xbarin = 0x03;
	switch (dev->input->amux) {
	case TV:
		reg = 0x02;
		xbarin = 0;
		break;
	case LINE1:
		reg = 0x00;
		break;
	case LINE2:
	case LINE2_LEFT:
		reg = 0x09;
		break;
	}
	saa_dsp_writel(dev, 0x464 >> 2, xbarin);
	if (dev->ctl_mute) {
		reg = 0x07;
		xbarout = 0xbbbbbb;
	} else
		xbarout = 0xbbbb10;
	saa_dsp_writel(dev, 0x46c >> 2, xbarout);

	saa_writel(0x594 >> 2, reg);


	/* switch gpio-connected external audio mux */
	if (0 != card(dev).gpiomask) {
		mask = card(dev).gpiomask;

		if (card(dev).mute.type && dev->ctl_mute)
			in = &card(dev).mute;
		else
			in = dev->input;

		saa_andorl(SAA7134_GPIO_GPMODE0 >> 2,   mask, mask);
		saa_andorl(SAA7134_GPIO_GPSTATUS0 >> 2, mask, in->gpio);
		saa7134_track_gpio(dev, saa7134_input_name[in->type]);
	}

	return 0;
}

static int tvaudio_thread_ddep(void *data)
{
	struct saa7134_dev *dev = data;
	u32 value, norms;

	set_freezable();
	for (;;) {
		tvaudio_sleep(dev,-1);
		if (kthread_should_stop())
			goto done;
	restart:
		try_to_freeze();

		dev->thread.scan1 = dev->thread.scan2;
		audio_dbg(1, "tvaudio thread scan start [%d]\n",
			  dev->thread.scan1);

		if (audio_ddep >= 0x04 && audio_ddep <= 0x0e) {
			/* insmod option override */
			norms = (audio_ddep << 2) | 0x01;
			audio_dbg(1, "ddep override: %s\n",
				  stdres[audio_ddep]);
		} else if (&card(dev).radio == dev->input) {
			audio_dbg(1, "FM Radio\n");
			if (dev->tuner_type == TUNER_PHILIPS_TDA8290) {
				norms = (0x11 << 2) | 0x01;
				/* set IF frequency to 5.5 MHz */
				saa_dsp_writel(dev, 0x42c >> 2, 0x729555);
			} else {
				norms = (0x0f << 2) | 0x01;
			}
		} else {
			/* (let chip) scan for sound carrier */
			norms = 0;
			if (dev->tvnorm->id & (V4L2_STD_B | V4L2_STD_GH))
				norms |= 0x04;
			if (dev->tvnorm->id & V4L2_STD_PAL_I)
				norms |= 0x20;
			if (dev->tvnorm->id & V4L2_STD_DK)
				norms |= 0x08;
			if (dev->tvnorm->id & V4L2_STD_MN)
				norms |= 0x40;
			if (dev->tvnorm->id & (V4L2_STD_SECAM_L | V4L2_STD_SECAM_LC))
				norms |= 0x10;
			if (0 == norms)
				norms = 0x7c; /* all */
			audio_dbg(1, "scanning:%s%s%s%s%s\n",
				  (norms & 0x04) ? " B/G"  : "",
				  (norms & 0x08) ? " D/K"  : "",
				  (norms & 0x10) ? " L/L'" : "",
				  (norms & 0x20) ? " I"    : "",
				  (norms & 0x40) ? " M"    : "");
		}

		/* kick automatic standard detection */
		saa_dsp_writel(dev, 0x454 >> 2, 0);
		saa_dsp_writel(dev, 0x454 >> 2, norms | 0x80);

		/* setup crossbars */
		saa_dsp_writel(dev, 0x464 >> 2, 0x000000);
		saa_dsp_writel(dev, 0x470 >> 2, 0x101010);

		if (tvaudio_sleep(dev,3000))
			goto restart;
		value = saa_readl(0x528 >> 2) & 0xffffff;

		audio_dbg(1, "tvaudio thread status: 0x%x [%s%s%s]\n",
			  value, stdres[value & 0x1f],
			  (value & 0x000020) ? ",stereo" : "",
			  (value & 0x000040) ? ",dual"   : "");
		audio_dbg(1, "detailed status: %s#%s#%s#%s#%s#%s#%s#%s#%s#%s#%s#%s#%s#%s\n",
			  (value & 0x000080) ? " A2/EIAJ pilot tone "     : "",
			  (value & 0x000100) ? " A2/EIAJ dual "           : "",
			  (value & 0x000200) ? " A2/EIAJ stereo "         : "",
			  (value & 0x000400) ? " A2/EIAJ noise mute "     : "",

			  (value & 0x000800) ? " BTSC/FM radio pilot "    : "",
			  (value & 0x001000) ? " SAP carrier "            : "",
			  (value & 0x002000) ? " BTSC stereo noise mute " : "",
			  (value & 0x004000) ? " SAP noise mute "         : "",
			  (value & 0x008000) ? " VDSP "                   : "",

			  (value & 0x010000) ? " NICST "                  : "",
			  (value & 0x020000) ? " NICDU "                  : "",
			  (value & 0x040000) ? " NICAM muted "            : "",
			  (value & 0x080000) ? " NICAM reserve sound "    : "",

			  (value & 0x100000) ? " init done "              : "");
	}

 done:
	dev->thread.stopped = 1;
	return 0;
}

/* ------------------------------------------------------------------ */
/* common stuff + external entry points                               */

void saa7134_enable_i2s(struct saa7134_dev *dev)
{
	int i2s_format;

	if (!card_is_empress(dev))
		return;

	if (dev->pci->device == PCI_DEVICE_ID_PHILIPS_SAA7130)
		return;

	/* configure GPIO for out */
	saa_andorl(SAA7134_GPIO_GPMODE0 >> 2, 0x0E000000, 0x00000000);

	switch (dev->pci->device) {
	case PCI_DEVICE_ID_PHILIPS_SAA7133:
	case PCI_DEVICE_ID_PHILIPS_SAA7135:
		/* Set I2S format (SONY)  */
		saa_writeb(SAA7133_I2S_AUDIO_CONTROL, 0x00);
		/* Start I2S */
		saa_writeb(SAA7134_I2S_AUDIO_OUTPUT, 0x11);
		break;

	case PCI_DEVICE_ID_PHILIPS_SAA7134:
		i2s_format = (dev->input->amux == TV) ? 0x00 : 0x01;

		/* enable I2S audio output for the mpeg encoder */
		saa_writeb(SAA7134_I2S_OUTPUT_SELECT, 0x80);
		saa_writeb(SAA7134_I2S_OUTPUT_FORMAT, i2s_format);
		saa_writeb(SAA7134_I2S_OUTPUT_LEVEL,  0x0F);
		saa_writeb(SAA7134_I2S_AUDIO_OUTPUT,  0x01);
		break;

	default:
		break;
	}
}

int saa7134_tvaudio_rx2mode(u32 rx)
{
	u32 mode;

	mode = V4L2_TUNER_MODE_MONO;
	if (rx & V4L2_TUNER_SUB_STEREO)
		mode = V4L2_TUNER_MODE_STEREO;
	else if (rx & V4L2_TUNER_SUB_LANG1)
		mode = V4L2_TUNER_MODE_LANG1;
	else if (rx & V4L2_TUNER_SUB_LANG2)
		mode = V4L2_TUNER_MODE_LANG2;
	return mode;
}

void saa7134_tvaudio_setmute(struct saa7134_dev *dev)
{
	switch (dev->pci->device) {
	case PCI_DEVICE_ID_PHILIPS_SAA7130:
	case PCI_DEVICE_ID_PHILIPS_SAA7134:
		mute_input_7134(dev);
		break;
	case PCI_DEVICE_ID_PHILIPS_SAA7133:
	case PCI_DEVICE_ID_PHILIPS_SAA7135:
		mute_input_7133(dev);
		break;
	}
}

void saa7134_tvaudio_setinput(struct saa7134_dev *dev,
			      struct saa7134_input *in)
{
	dev->input = in;
	switch (dev->pci->device) {
	case PCI_DEVICE_ID_PHILIPS_SAA7130:
	case PCI_DEVICE_ID_PHILIPS_SAA7134:
		mute_input_7134(dev);
		break;
	case PCI_DEVICE_ID_PHILIPS_SAA7133:
	case PCI_DEVICE_ID_PHILIPS_SAA7135:
		mute_input_7133(dev);
		break;
	}
	saa7134_enable_i2s(dev);
}

void saa7134_tvaudio_setvolume(struct saa7134_dev *dev, int level)
{
	switch (dev->pci->device) {
	case PCI_DEVICE_ID_PHILIPS_SAA7134:
		saa_writeb(SAA7134_CHANNEL1_LEVEL,     level & 0x1f);
		saa_writeb(SAA7134_CHANNEL2_LEVEL,     level & 0x1f);
		saa_writeb(SAA7134_NICAM_LEVEL_ADJUST, level & 0x1f);
		break;
	}
}

int saa7134_tvaudio_getstereo(struct saa7134_dev *dev)
{
	int retval = V4L2_TUNER_SUB_MONO;

	switch (dev->pci->device) {
	case PCI_DEVICE_ID_PHILIPS_SAA7134:
		if (dev->tvaudio)
			retval = tvaudio_getstereo(dev,dev->tvaudio);
		break;
	case PCI_DEVICE_ID_PHILIPS_SAA7133:
	case PCI_DEVICE_ID_PHILIPS_SAA7135:
		retval = getstereo_7133(dev);
		break;
	}
	return retval;
}

void saa7134_tvaudio_init(struct saa7134_dev *dev)
{
	int clock = saa7134_boards[dev->board].audio_clock;

	if (UNSET != audio_clock_override)
		clock = audio_clock_override;

	switch (dev->pci->device) {
	case PCI_DEVICE_ID_PHILIPS_SAA7134:
		/* init all audio registers */
		saa_writeb(SAA7134_AUDIO_PLL_CTRL,   0x00);
		if (need_resched())
			schedule();
		else
			udelay(10);

		saa_writeb(SAA7134_AUDIO_CLOCK0,      clock        & 0xff);
		saa_writeb(SAA7134_AUDIO_CLOCK1,     (clock >>  8) & 0xff);
		saa_writeb(SAA7134_AUDIO_CLOCK2,     (clock >> 16) & 0xff);
		/* frame locked audio is mandatory for NICAM */
		saa_writeb(SAA7134_AUDIO_PLL_CTRL,   0x01);
		saa_writeb(SAA7134_NICAM_ERROR_LOW,  0x14);
		saa_writeb(SAA7134_NICAM_ERROR_HIGH, 0x50);
		break;
	case PCI_DEVICE_ID_PHILIPS_SAA7133:
	case PCI_DEVICE_ID_PHILIPS_SAA7135:
		saa_writel(0x598 >> 2, clock);
		saa_dsp_writel(dev, 0x474 >> 2, 0x00);
		saa_dsp_writel(dev, 0x450 >> 2, 0x00);
	}
}

int saa7134_tvaudio_init2(struct saa7134_dev *dev)
{
	int (*my_thread)(void *data) = NULL;

	switch (dev->pci->device) {
	case PCI_DEVICE_ID_PHILIPS_SAA7134:
		my_thread = tvaudio_thread;
		break;
	case PCI_DEVICE_ID_PHILIPS_SAA7133:
	case PCI_DEVICE_ID_PHILIPS_SAA7135:
		my_thread = tvaudio_thread_ddep;
		break;
	}

	dev->thread.thread = NULL;
	dev->thread.scan1 = dev->thread.scan2 = 0;
	if (my_thread) {
		saa7134_tvaudio_init(dev);
		/* start tvaudio thread */
		dev->thread.thread = kthread_run(my_thread, dev, "%s", dev->name);
		if (IS_ERR(dev->thread.thread)) {
			pr_warn("%s: kernel_thread() failed\n",
			       dev->name);
			/* XXX: missing error handling here */
		}
	}

	saa7134_enable_i2s(dev);
	return 0;
}

int saa7134_tvaudio_close(struct saa7134_dev *dev)
{
	dev->automute = 1;
	/* anything else to undo? */
	return 0;
}

int saa7134_tvaudio_fini(struct saa7134_dev *dev)
{
	/* shutdown tvaudio thread */
	if (dev->thread.thread && !dev->thread.stopped)
		kthread_stop(dev->thread.thread);

	saa_andorb(SAA7134_ANALOG_IO_SELECT, 0x07, 0x00); /* LINE1 */
	return 0;
}

int saa7134_tvaudio_do_scan(struct saa7134_dev *dev)
{
	if (dev->input->amux != TV) {
		audio_dbg(1, "sound IF not in use, skipping scan\n");
		dev->automute = 0;
		saa7134_tvaudio_setmute(dev);
	} else if (dev->thread.thread) {
		dev->thread.mode = UNSET;
		dev->thread.scan2++;

		if (!dev->insuspend && !dev->thread.stopped)
			wake_up_process(dev->thread.thread);
	} else {
		dev->automute = 0;
		saa7134_tvaudio_setmute(dev);
	}
	return 0;
}

EXPORT_SYMBOL(saa_dsp_writel);
EXPORT_SYMBOL(saa7134_tvaudio_setmute);
