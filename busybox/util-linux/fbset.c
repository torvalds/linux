/* vi: set sw=4 ts=4: */
/*
 * Mini fbset implementation for busybox
 *
 * Copyright (C) 1999 by Randolph Chung <tausq@debian.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * This is a from-scratch implementation of fbset; but the de facto fbset
 * implementation was a good reference. fbset (original) is released under
 * the GPL, and is (c) 1995-1999 by:
 *     Geert Uytterhoeven (Geert.Uytterhoeven@cs.kuleuven.ac.be)
 */
//config:config FBSET
//config:	bool "fbset (5.8 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	fbset is used to show or change the settings of a Linux frame buffer
//config:	device. The frame buffer device provides a simple and unique
//config:	interface to access a graphics display. Enable this option
//config:	if you wish to enable the 'fbset' utility.
//config:
//config:config FEATURE_FBSET_FANCY
//config:	bool "Enable extra options"
//config:	default y
//config:	depends on FBSET
//config:	help
//config:	This option enables extended fbset options, allowing one to set the
//config:	framebuffer size, color depth, etc. interface to access a graphics
//config:	display. Enable this option if you wish to enable extended fbset
//config:	options.
//config:
//config:config FEATURE_FBSET_READMODE
//config:	bool "Enable readmode support"
//config:	default y
//config:	depends on FBSET
//config:	help
//config:	This option allows fbset to read the video mode database stored by
//config:	default as /etc/fb.modes, which can be used to set frame buffer
//config:	device to pre-defined video modes.

//applet:IF_FBSET(APPLET(fbset, BB_DIR_USR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_FBSET) += fbset.o

//usage:#define fbset_trivial_usage
//usage:       "[OPTIONS] [MODE]"
//usage:#define fbset_full_usage "\n\n"
//usage:       "Show and modify frame buffer settings"
//usage:
//usage:#define fbset_example_usage
//usage:       "$ fbset\n"
//usage:       "mode \"1024x768-76\"\n"
//usage:       "	# D: 78.653 MHz, H: 59.949 kHz, V: 75.694 Hz\n"
//usage:       "	geometry 1024 768 1024 768 16\n"
//usage:       "	timings 12714 128 32 16 4 128 4\n"
//usage:       "	accel false\n"
//usage:       "	rgba 5/11,6/5,5/0,0/0\n"
//usage:       "endmode\n"

#include "libbb.h"

#define DEFAULTFBDEV  FB_0
#define DEFAULTFBMODE "/etc/fb.modes"

/* Stuff stolen from the kernel's fb.h */
#define FB_ACTIVATE_ALL 64
enum {
	FBIOGET_VSCREENINFO = 0x4600,
	FBIOPUT_VSCREENINFO = 0x4601
};

struct fb_bitfield {
	uint32_t offset;                /* beginning of bitfield */
	uint32_t length;                /* length of bitfield */
	uint32_t msb_right;             /* !=0: Most significant bit is right */
};
struct fb_var_screeninfo {
	uint32_t xres;                  /* visible resolution */
	uint32_t yres;
	uint32_t xres_virtual;          /* virtual resolution */
	uint32_t yres_virtual;
	uint32_t xoffset;               /* offset from virtual to visible */
	uint32_t yoffset;               /* resolution */

	uint32_t bits_per_pixel;
	uint32_t grayscale;             /* !=0 Graylevels instead of colors */

	struct fb_bitfield red;         /* bitfield in fb mem if true color, */
	struct fb_bitfield green;       /* else only length is significant */
	struct fb_bitfield blue;
	struct fb_bitfield transp;      /* transparency */

	uint32_t nonstd;                /* !=0 Non standard pixel format */

	uint32_t activate;              /* see FB_ACTIVATE_x */

	uint32_t height;                /* height of picture in mm */
	uint32_t width;                 /* width of picture in mm */

	uint32_t accel_flags;           /* acceleration flags (hints) */

	/* Timing: All values in pixclocks, except pixclock (of course) */
	uint32_t pixclock;              /* pixel clock in ps (pico seconds) */
	uint32_t left_margin;           /* time from sync to picture */
	uint32_t right_margin;          /* time from picture to sync */
	uint32_t upper_margin;          /* time from sync to picture */
	uint32_t lower_margin;
	uint32_t hsync_len;             /* length of horizontal sync */
	uint32_t vsync_len;             /* length of vertical sync */
	uint32_t sync;                  /* see FB_SYNC_x */
	uint32_t vmode;                 /* see FB_VMODE_x */
	uint32_t reserved[6];           /* Reserved for future compatibility */
};

static void copy_if_gt0(uint32_t *src, uint32_t *dst, unsigned cnt)
{
	do {
		if ((int32_t) *src > 0)
			*dst = *src;
		src++;
		dst++;
	} while (--cnt);
}

static NOINLINE void copy_changed_values(
		struct fb_var_screeninfo *base,
		struct fb_var_screeninfo *set)
{
	//if ((int32_t) set->xres > 0) base->xres = set->xres;
	//if ((int32_t) set->yres > 0) base->yres = set->yres;
	//if ((int32_t) set->xres_virtual > 0)   base->xres_virtual = set->xres_virtual;
	//if ((int32_t) set->yres_virtual > 0)   base->yres_virtual = set->yres_virtual;
	copy_if_gt0(&set->xres, &base->xres, 4);

	if ((int32_t) set->bits_per_pixel > 0) base->bits_per_pixel = set->bits_per_pixel;
	//copy_if_gt0(&set->bits_per_pixel, &base->bits_per_pixel, 1);

	//if ((int32_t) set->pixclock > 0)       base->pixclock = set->pixclock;
	//if ((int32_t) set->left_margin > 0)    base->left_margin = set->left_margin;
	//if ((int32_t) set->right_margin > 0)   base->right_margin = set->right_margin;
	//if ((int32_t) set->upper_margin > 0)   base->upper_margin = set->upper_margin;
	//if ((int32_t) set->lower_margin > 0)   base->lower_margin = set->lower_margin;
	//if ((int32_t) set->hsync_len > 0) base->hsync_len = set->hsync_len;
	//if ((int32_t) set->vsync_len > 0) base->vsync_len = set->vsync_len;
	//if ((int32_t) set->sync > 0)  base->sync = set->sync;
	//if ((int32_t) set->vmode > 0) base->vmode = set->vmode;
	copy_if_gt0(&set->pixclock, &base->pixclock, 9);
}


enum {
	CMD_FB = 1,
	CMD_DB = 2,
	CMD_GEOMETRY = 3,
	CMD_TIMING = 4,
	CMD_ACCEL = 5,
	CMD_HSYNC = 6,
	CMD_VSYNC = 7,
	CMD_LACED = 8,
	CMD_DOUBLE = 9,
/*	CMD_XCOMPAT =     10, */
	CMD_ALL = 11,
	CMD_INFO = 12,
	CMD_SHOW = 13,
	CMD_CHANGE = 14,

#if ENABLE_FEATURE_FBSET_FANCY
	CMD_XRES = 100,
	CMD_YRES = 101,
	CMD_VXRES = 102,
	CMD_VYRES = 103,
	CMD_DEPTH = 104,
	CMD_MATCH = 105,
	CMD_PIXCLOCK = 106,
	CMD_LEFT = 107,
	CMD_RIGHT = 108,
	CMD_UPPER = 109,
	CMD_LOWER = 110,
	CMD_HSLEN = 111,
	CMD_VSLEN = 112,
	CMD_CSYNC = 113,
	CMD_GSYNC = 114,
	CMD_EXTSYNC = 115,
	CMD_BCAST = 116,
	CMD_RGBA = 117,
	CMD_STEP = 118,
	CMD_MOVE = 119,
#endif
};

static const struct cmdoptions_t {
	const char name[9];
	const unsigned char param_count;
	const unsigned char code;
} g_cmdoptions[] = {
	/*"12345678" + NUL */
//TODO: convert to index_in_strings()
	{ "fb"      , 1, CMD_FB       },
	{ "db"      , 1, CMD_DB       },
	{ "a"       , 0, CMD_ALL      },
	{ "i"       , 0, CMD_INFO     },
	{ "g"       , 5, CMD_GEOMETRY },
	{ "t"       , 7, CMD_TIMING   },
	{ "accel"   , 1, CMD_ACCEL    },
	{ "hsync"   , 1, CMD_HSYNC    },
	{ "vsync"   , 1, CMD_VSYNC    },
	{ "laced"   , 1, CMD_LACED    },
	{ "double"  , 1, CMD_DOUBLE   },
	{ "show"    , 0, CMD_SHOW     },
	{ "s"       , 0, CMD_SHOW     },
#if ENABLE_FEATURE_FBSET_FANCY
	{ "all"     , 0, CMD_ALL      },
	{ "xres"    , 1, CMD_XRES     },
	{ "yres"    , 1, CMD_YRES     },
	{ "vxres"   , 1, CMD_VXRES    },
	{ "vyres"   , 1, CMD_VYRES    },
	{ "depth"   , 1, CMD_DEPTH    },
	{ "match"   , 0, CMD_MATCH    },
	{ "geometry", 5, CMD_GEOMETRY },
	{ "pixclock", 1, CMD_PIXCLOCK },
	{ "left"    , 1, CMD_LEFT     },
	{ "right"   , 1, CMD_RIGHT    },
	{ "upper"   , 1, CMD_UPPER    },
	{ "lower"   , 1, CMD_LOWER    },
	{ "hslen"   , 1, CMD_HSLEN    },
	{ "vslen"   , 1, CMD_VSLEN    },
	{ "timings" , 7, CMD_TIMING   },
	{ "csync"   , 1, CMD_CSYNC    },
	{ "gsync"   , 1, CMD_GSYNC    },
	{ "extsync" , 1, CMD_EXTSYNC  },
	{ "bcast"   , 1, CMD_BCAST    },
	{ "rgba"    , 1, CMD_RGBA     },
	{ "step"    , 1, CMD_STEP     },
	{ "move"    , 1, CMD_MOVE     },
#endif
};

/* taken from linux/fb.h */
enum {
	FB_SYNC_HOR_HIGH_ACT = 1,       /* horizontal sync high active */
	FB_SYNC_VERT_HIGH_ACT = 2,      /* vertical sync high active */
#if ENABLE_FEATURE_FBSET_READMODE
	FB_VMODE_INTERLACED = 1,        /* interlaced */
	FB_VMODE_DOUBLE = 2,            /* double scan */
	FB_SYNC_EXT = 4,                /* external sync */
	FB_SYNC_COMP_HIGH_ACT = 8,      /* composite sync high active */
#endif
};

#if ENABLE_FEATURE_FBSET_READMODE
static void ss(uint32_t *x, uint32_t flag, char *buf, const char *what)
{
	if (strcmp(buf, what) == 0)
		*x &= ~flag;
	else
		*x |= flag;
}

/* Mode db file contains mode definitions like this:
 * mode "800x600-48-lace"
 *     # D: 36.00 MHz, H: 33.835 kHz, V: 96.39 Hz
 *     geometry 800 600 800 600 8
 *     timings 27778 56 80 79 11 128 12
 *     laced true
 *     hsync high
 *     vsync high
 * endmode
 */
static int read_mode_db(struct fb_var_screeninfo *base, const char *fn,
					const char *mode)
{
	char *token[2], *p, *s;
	parser_t *parser = config_open(fn);

	while (config_read(parser, token, 2, 1, "# \t\r", PARSE_NORMAL)) {
		if (strcmp(token[0], "mode") != 0 || !token[1])
			continue;
		p = strstr(token[1], mode);
		if (!p)
			continue;
		s = p + strlen(mode);
		//bb_error_msg("CHECK[%s][%s][%d]", mode, p-1, *s);
		/* exact match? */
		if (((!*s || isspace(*s)) && '"' != s[-1]) /* end-of-token */
		 || ('"' == *s && '"' == p[-1]) /* ends with " but starts with " too! */
		) {
			//bb_error_msg("FOUND[%s][%s][%s][%d]", token[1], p, mode, isspace(*s));
			break;
		}
	}

	if (!token[0])
		return 0;

	while (config_read(parser, token, 2, 1, "# \t", PARSE_NORMAL)) {
		int i;

//bb_error_msg("???[%s][%s]", token[0], token[1]);
		if (strcmp(token[0], "endmode") == 0) {
//bb_error_msg("OK[%s]", mode);
			return 1;
		}
		p = token[1];
		i = index_in_strings(
			"geometry\0timings\0interlaced\0double\0vsync\0hsync\0csync\0extsync\0rgba\0",
			token[0]);
		switch (i) {
		case 0:
			if (sizeof(int) == sizeof(base->xres)) {
				sscanf(p, "%d %d %d %d %d",
					&base->xres, &base->yres,
					&base->xres_virtual, &base->yres_virtual,
					&base->bits_per_pixel);
			} else {
				int base_xres, base_yres;
				int base_xres_virtual, base_yres_virtual;
				int base_bits_per_pixel;
				sscanf(p, "%d %d %d %d %d",
					&base_xres, &base_yres,
					&base_xres_virtual, &base_yres_virtual,
					&base_bits_per_pixel);
				base->xres = base_xres;
				base->yres = base_yres;
				base->xres_virtual = base_xres_virtual;
				base->yres_virtual = base_yres_virtual;
				base->bits_per_pixel = base_bits_per_pixel;
			}
//bb_error_msg("GEO[%s]", p);
			break;
		case 1:
			if (sizeof(int) == sizeof(base->xres)) {
				sscanf(p, "%d %d %d %d %d %d %d",
					&base->pixclock,
					&base->left_margin, &base->right_margin,
					&base->upper_margin, &base->lower_margin,
					&base->hsync_len, &base->vsync_len);
			} else {
				int base_pixclock;
				int base_left_margin, base_right_margin;
				int base_upper_margin, base_lower_margin;
				int base_hsync_len, base_vsync_len;
				sscanf(p, "%d %d %d %d %d %d %d",
					&base_pixclock,
					&base_left_margin, &base_right_margin,
					&base_upper_margin, &base_lower_margin,
					&base_hsync_len, &base_vsync_len);
				base->pixclock = base_pixclock;
				base->left_margin = base_left_margin;
				base->right_margin = base_right_margin;
				base->upper_margin = base_upper_margin;
				base->lower_margin = base_lower_margin;
				base->hsync_len = base_hsync_len;
				base->vsync_len = base_vsync_len;
			}
//bb_error_msg("TIM[%s]", p);
			break;
		case 2:
		case 3: {
			static const uint32_t syncs[] = {FB_VMODE_INTERLACED, FB_VMODE_DOUBLE};
			ss(&base->vmode, syncs[i-2], p, "false");
//bb_error_msg("VMODE[%s]", p);
			break;
		}
		case 4:
		case 5:
		case 6: {
			static const uint32_t syncs[] = {FB_SYNC_VERT_HIGH_ACT, FB_SYNC_HOR_HIGH_ACT, FB_SYNC_COMP_HIGH_ACT};
			ss(&base->sync, syncs[i-4], p, "low");
//bb_error_msg("SYNC[%s]", p);
			break;
		}
		case 7:
			ss(&base->sync, FB_SYNC_EXT, p, "false");
//bb_error_msg("EXTSYNC[%s]", p);
			break;
		case 8: {
			int red_offset, red_length;
			int green_offset, green_length;
			int blue_offset, blue_length;
			int transp_offset, transp_length;

			sscanf(p, "%d/%d,%d/%d,%d/%d,%d/%d",
				&red_length, &red_offset,
				&green_length, &green_offset,
				&blue_length, &blue_offset,
				&transp_length, &transp_offset);
			base->red.offset = red_offset;
			base->red.length = red_length;
			base->red.msb_right = 0;
			base->green.offset = green_offset;
			base->green.length = green_length;
			base->green.msb_right = 0;
			base->blue.offset = blue_offset;
			base->blue.length = blue_length;
			base->blue.msb_right = 0;
			base->transp.offset = transp_offset;
			base->transp.length = transp_length;
			base->transp.msb_right = 0;
		}
		}
	}
	return 0;
}
#endif

static NOINLINE void showmode(struct fb_var_screeninfo *v)
{
	double drate = 0, hrate = 0, vrate = 0;

	if (v->pixclock) {
		drate = 1e12 / v->pixclock;
		hrate = drate / (v->left_margin + v->xres + v->right_margin + v->hsync_len);
		vrate = hrate / (v->upper_margin + v->yres + v->lower_margin + v->vsync_len);
	}
	printf("\nmode \"%ux%u-%u\"\n"
#if ENABLE_FEATURE_FBSET_FANCY
	"\t# D: %.3f MHz, H: %.3f kHz, V: %.3f Hz\n"
#endif
	"\tgeometry %u %u %u %u %u\n"
	"\ttimings %u %u %u %u %u %u %u\n"
	"\taccel %s\n"
	"\trgba %u/%u,%u/%u,%u/%u,%u/%u\n"
	"endmode\n\n",
		v->xres, v->yres, (int) (vrate + 0.5),
#if ENABLE_FEATURE_FBSET_FANCY
		drate / 1e6, hrate / 1e3, vrate,
#endif
		v->xres, v->yres, v->xres_virtual, v->yres_virtual, v->bits_per_pixel,
		v->pixclock, v->left_margin, v->right_margin, v->upper_margin, v->lower_margin,
			v->hsync_len, v->vsync_len,
		(v->accel_flags > 0 ? "true" : "false"),
		v->red.length, v->red.offset, v->green.length, v->green.offset,
			v->blue.length, v->blue.offset, v->transp.length, v->transp.offset);
}

int fbset_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int fbset_main(int argc, char **argv)
{
	enum {
		OPT_CHANGE   = (1 << 0),
		OPT_SHOW     = (1 << 1),
		OPT_READMODE = (1 << 2),
		OPT_ALL      = (1 << 3),
	};
	struct fb_var_screeninfo var_old, var_set;
	int fh, i;
	unsigned options = 0;

	const char *fbdev = DEFAULTFBDEV;
	IF_FEATURE_FBSET_READMODE(const char *modefile = DEFAULTFBMODE;)
	char *thisarg;
	char *mode = mode; /* for compiler */

	memset(&var_set, 0xff, sizeof(var_set)); /* set all to -1 */

	/* parse cmd args.... why do they have to make things so difficult? */
	argv++;
	argc--;
	for (; argc > 0 && (thisarg = *argv) != NULL; argc--, argv++) {
		if (thisarg[0] != '-') {
			if (!ENABLE_FEATURE_FBSET_READMODE || argc != 1)
				bb_show_usage();
			mode = thisarg;
			options |= OPT_READMODE;
			goto contin;
		}
		for (i = 0; i < ARRAY_SIZE(g_cmdoptions); i++) {
			if (strcmp(thisarg + 1, g_cmdoptions[i].name) != 0)
				continue;
			if (argc <= g_cmdoptions[i].param_count)
				bb_show_usage();

			switch (g_cmdoptions[i].code) {
			case CMD_FB:
				fbdev = argv[1];
				break;
			case CMD_DB:
				IF_FEATURE_FBSET_READMODE(modefile = argv[1];)
				break;
			case CMD_ALL:
				options |= OPT_ALL;
				break;
			case CMD_SHOW:
				options |= OPT_SHOW;
				break;
			case CMD_GEOMETRY:
				var_set.xres = xatou32(argv[1]);
				var_set.yres = xatou32(argv[2]);
				var_set.xres_virtual = xatou32(argv[3]);
				var_set.yres_virtual = xatou32(argv[4]);
				var_set.bits_per_pixel = xatou32(argv[5]);
				break;
			case CMD_TIMING:
				var_set.pixclock = xatou32(argv[1]);
				var_set.left_margin = xatou32(argv[2]);
				var_set.right_margin = xatou32(argv[3]);
				var_set.upper_margin = xatou32(argv[4]);
				var_set.lower_margin = xatou32(argv[5]);
				var_set.hsync_len = xatou32(argv[6]);
				var_set.vsync_len = xatou32(argv[7]);
				break;
			case CMD_ACCEL:
				break;
			case CMD_HSYNC:
				var_set.sync |= FB_SYNC_HOR_HIGH_ACT;
				break;
			case CMD_VSYNC:
				var_set.sync |= FB_SYNC_VERT_HIGH_ACT;
				break;
#if ENABLE_FEATURE_FBSET_FANCY
			case CMD_XRES:
				var_set.xres = xatou32(argv[1]);
				break;
			case CMD_YRES:
				var_set.yres = xatou32(argv[1]);
				break;
			case CMD_DEPTH:
				var_set.bits_per_pixel = xatou32(argv[1]);
				break;
#endif
			}
			switch (g_cmdoptions[i].code) {
			case CMD_FB:
			case CMD_DB:
			case CMD_ALL:
			case CMD_SHOW:
				break;
			default:
				/* other commands imply changes */
				options |= OPT_CHANGE;
			}
			argc -= g_cmdoptions[i].param_count;
			argv += g_cmdoptions[i].param_count;
			goto contin;
		}
		bb_show_usage();
 contin: ;
	}

	fh = xopen(fbdev, O_RDONLY);
	xioctl(fh, FBIOGET_VSCREENINFO, &var_old);

	if (options & OPT_READMODE) {
#if ENABLE_FEATURE_FBSET_READMODE
		if (!read_mode_db(&var_old, modefile, mode)) {
			bb_error_msg_and_die("unknown video mode '%s'", mode);
		}
		options |= OPT_CHANGE;
#endif
	}

	if (options & OPT_CHANGE) {
		copy_changed_values(&var_old, &var_set);
		if (options & OPT_ALL)
			var_old.activate = FB_ACTIVATE_ALL;
		xioctl(fh, FBIOPUT_VSCREENINFO, &var_old);
	}

	if (options == 0 || (options & OPT_SHOW))
		showmode(&var_old);

	if (ENABLE_FEATURE_CLEAN_UP)
		close(fh);

	return EXIT_SUCCESS;
}
