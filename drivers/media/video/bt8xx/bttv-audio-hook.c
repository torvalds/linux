/*
 * Handlers for board audio hooks, splitted from bttv-cards
 *
 * Copyright (c) 2006 Mauro Carvalho Chehab (mchehab@infradead.org)
 * This code is placed under the terms of the GNU General Public License
 */

/* ----------------------------------------------------------------------- */
/* winview                                                                 */

#include "bttvp.h"
#include <linux/videodev.h>

static void winview_audio(struct bttv *btv, struct video_audio *v, int set)
{
	/* PT2254A programming Jon Tombs, jon@gte.esi.us.es */
	int bits_out, loops, vol, data;

	if (!set) {
		/* Fixed by Leandro Lucarella <luca@linuxmendoza.org.ar (07/31/01) */
		v->flags |= VIDEO_AUDIO_VOLUME;
		return;
	}

	/* 32 levels logarithmic */
	vol = 32 - ((v->volume>>11));
	/* units */
	bits_out = (PT2254_DBS_IN_2>>(vol%5));
	/* tens */
	bits_out |= (PT2254_DBS_IN_10>>(vol/5));
	bits_out |= PT2254_L_CHANNEL | PT2254_R_CHANNEL;
	data = gpio_read();
	data &= ~(WINVIEW_PT2254_CLK| WINVIEW_PT2254_DATA|
		  WINVIEW_PT2254_STROBE);
	for (loops = 17; loops >= 0 ; loops--) {
		if (bits_out & (1<<loops))
			data |=  WINVIEW_PT2254_DATA;
		else
			data &= ~WINVIEW_PT2254_DATA;
		gpio_write(data);
		udelay(5);
		data |= WINVIEW_PT2254_CLK;
		gpio_write(data);
		udelay(5);
		data &= ~WINVIEW_PT2254_CLK;
		gpio_write(data);
	}
	data |=  WINVIEW_PT2254_STROBE;
	data &= ~WINVIEW_PT2254_DATA;
	gpio_write(data);
	udelay(10);
	data &= ~WINVIEW_PT2254_STROBE;
	gpio_write(data);
}

/* ----------------------------------------------------------------------- */
/* mono/stereo control for various cards (which don't use i2c chips but    */
/* connect something to the GPIO pins                                      */

static void
gvbctv3pci_audio(struct bttv *btv, struct video_audio *v, int set)
{
	unsigned int con = 0;

	if (set) {
		gpio_inout(0x300, 0x300);
		if (v->mode & VIDEO_SOUND_LANG1)
			con = 0x000;
		if (v->mode & VIDEO_SOUND_LANG2)
			con = 0x300;
		if (v->mode & VIDEO_SOUND_STEREO)
			con = 0x200;
/*		if (v->mode & VIDEO_SOUND_MONO)
 *			con = 0x100; */
		gpio_bits(0x300, con);
	} else {
		v->mode = VIDEO_SOUND_STEREO |
			  VIDEO_SOUND_LANG1  | VIDEO_SOUND_LANG2;
	}
}

static void
gvbctv5pci_audio(struct bttv *btv, struct video_audio *v, int set)
{
	unsigned int val, con;

	if (btv->radio_user)
		return;

	val = gpio_read();
	if (set) {
		con = 0x000;
		if (v->mode & VIDEO_SOUND_LANG2) {
			if (v->mode & VIDEO_SOUND_LANG1) {
				/* LANG1 + LANG2 */
				con = 0x100;
			}
			else {
				/* LANG2 */
				con = 0x300;
			}
		}
		if (con != (val & 0x300)) {
			gpio_bits(0x300, con);
			if (bttv_gpio)
				bttv_gpio_tracking(btv,"gvbctv5pci");
		}
	} else {
		switch (val & 0x70) {
		  case 0x10:
			v->mode = VIDEO_SOUND_LANG1 | VIDEO_SOUND_LANG2;
			break;
		  case 0x30:
			v->mode = VIDEO_SOUND_LANG2;
			break;
		  case 0x50:
			v->mode = VIDEO_SOUND_LANG1;
			break;
		  case 0x60:
			v->mode = VIDEO_SOUND_STEREO;
			break;
		  case 0x70:
			v->mode = VIDEO_SOUND_MONO;
			break;
		  default:
			v->mode = VIDEO_SOUND_MONO | VIDEO_SOUND_STEREO |
				  VIDEO_SOUND_LANG1 | VIDEO_SOUND_LANG2;
		}
	}
}

/*
 * Mario Medina Nussbaum <medisoft@alohabbs.org.mx>
 *  I discover that on BT848_GPIO_DATA address a byte 0xcce enable stereo,
 *  0xdde enables mono and 0xccd enables sap
 *
 * Petr Vandrovec <VANDROVE@vc.cvut.cz>
 *  P.S.: At least mask in line above is wrong - GPIO pins 3,2 select
 *  input/output sound connection, so both must be set for output mode.
 *
 * Looks like it's needed only for the "tvphone", the "tvphone 98"
 * handles this with a tda9840
 *
 */
static void
avermedia_tvphone_audio(struct bttv *btv, struct video_audio *v, int set)
{
	int val = 0;

	if (set) {
		if (v->mode & VIDEO_SOUND_LANG2)   /* SAP */
			val = 0x02;
		if (v->mode & VIDEO_SOUND_STEREO)
			val = 0x01;
		if (val) {
			gpio_bits(0x03,val);
			if (bttv_gpio)
				bttv_gpio_tracking(btv,"avermedia");
		}
	} else {
		v->mode = VIDEO_SOUND_MONO | VIDEO_SOUND_STEREO |
			VIDEO_SOUND_LANG1;
		return;
	}
}

static void
avermedia_tv_stereo_audio(struct bttv *btv, struct video_audio *v, int set)
{
	int val = 0;

	if (set) {
		if (v->mode & VIDEO_SOUND_LANG2)   /* SAP */
			val = 0x01;
		if (v->mode & VIDEO_SOUND_STEREO)  /* STEREO */
			val = 0x02;
		btaor(val, ~0x03, BT848_GPIO_DATA);
		if (bttv_gpio)
			bttv_gpio_tracking(btv,"avermedia");
	} else {
		v->mode = VIDEO_SOUND_MONO | VIDEO_SOUND_STEREO |
			VIDEO_SOUND_LANG1 | VIDEO_SOUND_LANG2;
		return;
	}
}

/* Lifetec 9415 handling */
static void
lt9415_audio(struct bttv *btv, struct video_audio *v, int set)
{
	int val = 0;

	if (gpio_read() & 0x4000) {
		v->mode = VIDEO_SOUND_MONO;
		return;
	}

	if (set) {
		if (v->mode & VIDEO_SOUND_LANG2)  /* A2 SAP */
			val = 0x0080;
		if (v->mode & VIDEO_SOUND_STEREO) /* A2 stereo */
			val = 0x0880;
		if ((v->mode & VIDEO_SOUND_LANG1) ||
		    (v->mode & VIDEO_SOUND_MONO))
			val = 0;
		gpio_bits(0x0880, val);
		if (bttv_gpio)
			bttv_gpio_tracking(btv,"lt9415");
	} else {
		/* autodetect doesn't work with this card :-( */
		v->mode = VIDEO_SOUND_MONO | VIDEO_SOUND_STEREO |
			VIDEO_SOUND_LANG1 | VIDEO_SOUND_LANG2;
		return;
	}
}

/* TDA9821 on TerraTV+ Bt848, Bt878 */
static void
terratv_audio(struct bttv *btv, struct video_audio *v, int set)
{
	unsigned int con = 0;

	if (set) {
		gpio_inout(0x180000,0x180000);
		if (v->mode & VIDEO_SOUND_LANG2)
			con = 0x080000;
		if (v->mode & VIDEO_SOUND_STEREO)
			con = 0x180000;
		gpio_bits(0x180000, con);
		if (bttv_gpio)
			bttv_gpio_tracking(btv,"terratv");
	} else {
		v->mode = VIDEO_SOUND_MONO | VIDEO_SOUND_STEREO |
			VIDEO_SOUND_LANG1 | VIDEO_SOUND_LANG2;
	}
}

static void
winfast2000_audio(struct bttv *btv, struct video_audio *v, int set)
{
	unsigned long val = 0;

	if (set) {
		/*btor (0xc32000, BT848_GPIO_OUT_EN);*/
		if (v->mode & VIDEO_SOUND_MONO)		/* Mono */
			val = 0x420000;
		if (v->mode & VIDEO_SOUND_LANG1)	/* Mono */
			val = 0x420000;
		if (v->mode & VIDEO_SOUND_LANG2)	/* SAP */
			val = 0x410000;
		if (v->mode & VIDEO_SOUND_STEREO)	/* Stereo */
			val = 0x020000;
		if (val) {
			gpio_bits(0x430000, val);
			if (bttv_gpio)
				bttv_gpio_tracking(btv,"winfast2000");
		}
	} else {
		v->mode = VIDEO_SOUND_MONO | VIDEO_SOUND_STEREO |
			  VIDEO_SOUND_LANG1 | VIDEO_SOUND_LANG2;
	}
}

/*
 * Dariusz Kowalewski <darekk@automex.pl>
 * sound control for Prolink PV-BT878P+9B (PixelView PlayTV Pro FM+NICAM
 * revision 9B has on-board TDA9874A sound decoder).
 *
 * Note: There are card variants without tda9874a. Forcing the "stereo sound route"
 *       will mute this cards.
 */
static void
pvbt878p9b_audio(struct bttv *btv, struct video_audio *v, int set)
{
	unsigned int val = 0;

	if (btv->radio_user)
		return;

	if (set) {
		if (v->mode & VIDEO_SOUND_MONO)	{
			val = 0x01;
		}
		if ((v->mode & (VIDEO_SOUND_LANG1 | VIDEO_SOUND_LANG2))
		    || (v->mode & VIDEO_SOUND_STEREO)) {
			val = 0x02;
		}
		if (val) {
			gpio_bits(0x03,val);
			if (bttv_gpio)
				bttv_gpio_tracking(btv,"pvbt878p9b");
		}
	} else {
		v->mode = VIDEO_SOUND_MONO | VIDEO_SOUND_STEREO |
			VIDEO_SOUND_LANG1 | VIDEO_SOUND_LANG2;
	}
}

/*
 * Dariusz Kowalewski <darekk@automex.pl>
 * sound control for FlyVideo 2000S (with tda9874 decoder)
 * based on pvbt878p9b_audio() - this is not tested, please fix!!!
 */
static void
fv2000s_audio(struct bttv *btv, struct video_audio *v, int set)
{
	unsigned int val = 0xffff;

	if (btv->radio_user)
		return;

	if (set) {
		if (v->mode & VIDEO_SOUND_MONO)	{
			val = 0x0000;
		}
		if ((v->mode & (VIDEO_SOUND_LANG1 | VIDEO_SOUND_LANG2))
		    || (v->mode & VIDEO_SOUND_STEREO)) {
			val = 0x1080; /*-dk-???: 0x0880, 0x0080, 0x1800 ... */
		}
		if (val != 0xffff) {
			gpio_bits(0x1800, val);
			if (bttv_gpio)
				bttv_gpio_tracking(btv,"fv2000s");
		}
	} else {
		v->mode = VIDEO_SOUND_MONO | VIDEO_SOUND_STEREO |
			VIDEO_SOUND_LANG1 | VIDEO_SOUND_LANG2;
	}
}

/*
 * sound control for Canopus WinDVR PCI
 * Masaki Suzuki <masaki@btree.org>
 */
static void
windvr_audio(struct bttv *btv, struct video_audio *v, int set)
{
	unsigned long val = 0;

	if (set) {
		if (v->mode & VIDEO_SOUND_MONO)
			val = 0x040000;
		if (v->mode & VIDEO_SOUND_LANG1)
			val = 0;
		if (v->mode & VIDEO_SOUND_LANG2)
			val = 0x100000;
		if (v->mode & VIDEO_SOUND_STEREO)
			val = 0;
		if (val) {
			gpio_bits(0x140000, val);
			if (bttv_gpio)
				bttv_gpio_tracking(btv,"windvr");
		}
	} else {
		v->mode = VIDEO_SOUND_MONO | VIDEO_SOUND_STEREO |
			  VIDEO_SOUND_LANG1 | VIDEO_SOUND_LANG2;
	}
}

/*
 * sound control for AD-TVK503
 * Hiroshi Takekawa <sian@big.or.jp>
 */
static void
adtvk503_audio(struct bttv *btv, struct video_audio *v, int set)
{
	unsigned int con = 0xffffff;

	/* btaor(0x1e0000, ~0x1e0000, BT848_GPIO_OUT_EN); */

	if (set) {
		/* btor(***, BT848_GPIO_OUT_EN); */
		if (v->mode & VIDEO_SOUND_LANG1)
			con = 0x00000000;
		if (v->mode & VIDEO_SOUND_LANG2)
			con = 0x00180000;
		if (v->mode & VIDEO_SOUND_STEREO)
			con = 0x00000000;
		if (v->mode & VIDEO_SOUND_MONO)
			con = 0x00060000;
		if (con != 0xffffff) {
			gpio_bits(0x1e0000,con);
			if (bttv_gpio)
				bttv_gpio_tracking(btv, "adtvk503");
		}
	} else {
		v->mode = VIDEO_SOUND_MONO | VIDEO_SOUND_STEREO |
			  VIDEO_SOUND_LANG1  | VIDEO_SOUND_LANG2;
	}
}
