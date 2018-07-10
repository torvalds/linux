/* vi: set sw=4 ts=4: */
/*
 * Copyright (C) 2008 Michele Sanges <michele.sanges@gmail.com>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * Usage:
 * - use kernel option 'vga=xxx' or otherwise enable framebuffer device.
 * - put somewhere fbsplash.cfg file and an image in .ppm format.
 * - run applet: $ setsid fbsplash [params] &
 *      -c: hide cursor
 *      -d /dev/fbN: framebuffer device (if not /dev/fb0)
 *      -s path_to_image_file (can be "-" for stdin)
 *      -i path_to_cfg_file
 *      -f path_to_fifo (can be "-" for stdin)
 * - if you want to run it only in presence of a kernel parameter
 *   (for example fbsplash=on), use:
 *   grep -q "fbsplash=on" </proc/cmdline && setsid fbsplash [params]
 * - commands for fifo:
 *   "NN" (ASCII decimal number) - percentage to show on progress bar.
 *   "exit" (or just close fifo) - well you guessed it.
 */
//config:config FBSPLASH
//config:	bool "fbsplash (27 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Shows splash image and progress bar on framebuffer device.
//config:	Can be used during boot phase of an embedded device.
//config:	Usage:
//config:	- use kernel option 'vga=xxx' or otherwise enable fb device.
//config:	- put somewhere fbsplash.cfg file and an image in .ppm format.
//config:	- $ setsid fbsplash [params] &
//config:	    -c: hide cursor
//config:	    -d /dev/fbN: framebuffer device (if not /dev/fb0)
//config:	    -s path_to_image_file (can be "-" for stdin)
//config:	    -i path_to_cfg_file (can be "-" for stdin)
//config:	    -f path_to_fifo (can be "-" for stdin)
//config:	- if you want to run it only in presence of kernel parameter:
//config:	    grep -q "fbsplash=on" </proc/cmdline && setsid fbsplash [params] &
//config:	- commands for fifo:
//config:	    "NN" (ASCII decimal number) - percentage to show on progress bar
//config:	    "exit" - well you guessed it

//applet:IF_FBSPLASH(APPLET(fbsplash, BB_DIR_SBIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_FBSPLASH) += fbsplash.o

//usage:#define fbsplash_trivial_usage
//usage:       "-s IMGFILE [-c] [-d DEV] [-i INIFILE] [-f CMD]"
//usage:#define fbsplash_full_usage "\n\n"
//usage:       "	-s	Image"
//usage:     "\n	-c	Hide cursor"
//usage:     "\n	-d	Framebuffer device (default /dev/fb0)"
//usage:     "\n	-i	Config file (var=value):"
//usage:     "\n			BAR_LEFT,BAR_TOP,BAR_WIDTH,BAR_HEIGHT"
//usage:     "\n			BAR_R,BAR_G,BAR_B,IMG_LEFT,IMG_TOP"
//usage:     "\n	-f	Control pipe (else exit after drawing image)"
//usage:     "\n			commands: 'NN' (% for progress bar) or 'exit'"

#include "libbb.h"
#include "common_bufsiz.h"
#include <linux/fb.h>

/* If you want logging messages on /tmp/fbsplash.log... */
#define DEBUG 0

#define ESC "\033"

struct globals {
#if DEBUG
	bool bdebug_messages;	// enable/disable logging
	FILE *logfile_fd;	// log file
#endif
	unsigned char *addr;	// pointer to framebuffer memory
	unsigned ns[9];		// n-parameters
	const char *image_filename;
	struct fb_var_screeninfo scr_var;
	struct fb_fix_screeninfo scr_fix;
	unsigned bytes_per_pixel;
	// cached (8 - scr_var.COLOR.length):
	unsigned red_shift;
	unsigned green_shift;
	unsigned blue_shift;
};
#define G (*ptr_to_globals)
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
} while (0)

#define nbar_width	ns[0]	// progress bar width
#define nbar_height	ns[1]	// progress bar height
#define nbar_posx	ns[2]	// progress bar horizontal position
#define nbar_posy	ns[3]	// progress bar vertical position
#define nbar_colr	ns[4]	// progress bar color red component
#define nbar_colg	ns[5]	// progress bar color green component
#define nbar_colb	ns[6]	// progress bar color blue component
#define img_posx	ns[7]	// image horizontal position
#define img_posy	ns[8]	// image vertical position

#if DEBUG
#define DEBUG_MESSAGE(strMessage, args...) \
	if (G.bdebug_messages) { \
		fprintf(G.logfile_fd, "[%s][%s] - %s\n", \
		__FILE__, __FUNCTION__, strMessage);	\
	}
#else
#define DEBUG_MESSAGE(...) ((void)0)
#endif

/**
 * Configure palette for RGB:332
 */
static void fb_setpal(int fd)
{
	struct fb_cmap cmap;
	/* fb colors are 16 bit */
	unsigned short red[256], green[256], blue[256];
	unsigned i;

	/* RGB:332 */
	for (i = 0; i < 256; i++) {
		/* Color is encoded in pixel value as rrrgggbb.
		 * 3-bit color is mapped to 16-bit one as:
		 * 000 -> 00000000 00000000
		 * 001 -> 00100100 10010010
		 * ...
		 * 011 -> 01101101 10110110
		 * 100 -> 10010010 01001001
		 * ...
		 * 111 -> 11111111 11111111
		 */
		red[i]   = (( i >> 5       ) * 0x9249) >> 2; // rrr * 00 10010010 01001001 >> 2
		green[i] = (((i >> 2) & 0x7) * 0x9249) >> 2; // ggg * 00 10010010 01001001 >> 2
		/* 2-bit color is easier: */
		blue[i]  =  ( i       & 0x3) * 0x5555; // bb * 01010101 01010101
	}

	cmap.start = 0;
	cmap.len   = 256;
	cmap.red   = red;
	cmap.green = green;
	cmap.blue  = blue;
	cmap.transp = 0;

	xioctl(fd, FBIOPUTCMAP, &cmap);
}

/**
 * Open and initialize the framebuffer device
 * \param *strfb_device pointer to framebuffer device
 */
static void fb_open(const char *strfb_device)
{
	int fbfd = xopen(strfb_device, O_RDWR);

	// framebuffer properties
	xioctl(fbfd, FBIOGET_VSCREENINFO, &G.scr_var);
	xioctl(fbfd, FBIOGET_FSCREENINFO, &G.scr_fix);

	switch (G.scr_var.bits_per_pixel) {
	case 8:
		fb_setpal(fbfd);
		break;

	case 16:
	case 24:
	case 32:
		break;

	default:
		bb_error_msg_and_die("unsupported %u bpp", (int)G.scr_var.bits_per_pixel);
		break;
	}

	G.red_shift   = 8 - G.scr_var.red.length;
	G.green_shift = 8 - G.scr_var.green.length;
	G.blue_shift  = 8 - G.scr_var.blue.length;
	G.bytes_per_pixel = (G.scr_var.bits_per_pixel + 7) >> 3;

	// map the device in memory
	G.addr = mmap(NULL,
			(G.scr_var.yres_virtual ?: G.scr_var.yres) * G.scr_fix.line_length,
			PROT_WRITE, MAP_SHARED, fbfd, 0);
	if (G.addr == MAP_FAILED)
		bb_perror_msg_and_die("mmap");

	// point to the start of the visible screen
	G.addr += G.scr_var.yoffset * G.scr_fix.line_length + G.scr_var.xoffset * G.bytes_per_pixel;
	close(fbfd);
}


/**
 * Return pixel value of the passed RGB color.
 * This is performance critical fn.
 */
static unsigned fb_pixel_value(unsigned r, unsigned g, unsigned b)
{
	/* We assume that the r,g,b values are <= 255 */

	if (G.bytes_per_pixel == 1) {
		r = r        & 0xe0; // 3-bit red
		g = (g >> 3) & 0x1c; // 3-bit green
		b =  b >> 6;         // 2-bit blue
		return r + g + b;
	}
	if (G.bytes_per_pixel == 2) {
		// ARM PL110 on Integrator/CP has RGBA5551 bit arrangement.
		// We want to support bit locations like that.
		//
		// First shift out unused bits
		r = r >> G.red_shift;
		g = g >> G.green_shift;
		b = b >> G.blue_shift;
		// Then shift the remaining bits to their offset
		return (r << G.scr_var.red.offset) +
			(g << G.scr_var.green.offset) +
			(b << G.scr_var.blue.offset);
	}
	// RGB 888
	return b + (g << 8) + (r << 16);
}

/**
 * Draw pixel on framebuffer
 */
static void fb_write_pixel(unsigned char *addr, unsigned pixel)
{
	switch (G.bytes_per_pixel) {
	case 1:
		*addr = pixel;
		break;
	case 2:
		*(uint16_t *)addr = pixel;
		break;
	case 4:
		*(uint32_t *)addr = pixel;
		break;
	default: // 24 bits per pixel
		addr[0] = pixel;
		addr[1] = pixel >> 8;
		addr[2] = pixel >> 16;
	}
}


/**
 * Draw hollow rectangle on framebuffer
 */
static void fb_drawrectangle(void)
{
	int cnt;
	unsigned thispix;
	unsigned char *ptr1, *ptr2;
	unsigned char nred = G.nbar_colr/2;
	unsigned char ngreen =  G.nbar_colg/2;
	unsigned char nblue = G.nbar_colb/2;

	thispix = fb_pixel_value(nred, ngreen, nblue);

	// horizontal lines
	ptr1 = G.addr + G.nbar_posy * G.scr_fix.line_length + G.nbar_posx * G.bytes_per_pixel;
	ptr2 = G.addr + (G.nbar_posy + G.nbar_height - 1) * G.scr_fix.line_length + G.nbar_posx * G.bytes_per_pixel;
	cnt = G.nbar_width - 1;
	do {
		fb_write_pixel(ptr1, thispix);
		fb_write_pixel(ptr2, thispix);
		ptr1 += G.bytes_per_pixel;
		ptr2 += G.bytes_per_pixel;
	} while (--cnt >= 0);

	// vertical lines
	ptr1 = G.addr + G.nbar_posy * G.scr_fix.line_length + G.nbar_posx * G.bytes_per_pixel;
	ptr2 = G.addr + G.nbar_posy * G.scr_fix.line_length + (G.nbar_posx + G.nbar_width - 1) * G.bytes_per_pixel;
	cnt = G.nbar_height - 1;
	do {
		fb_write_pixel(ptr1, thispix);
		fb_write_pixel(ptr2, thispix);
		ptr1 += G.scr_fix.line_length;
		ptr2 += G.scr_fix.line_length;
	} while (--cnt >= 0);
}


/**
 * Draw filled rectangle on framebuffer
 * \param nx1pos,ny1pos upper left position
 * \param nx2pos,ny2pos down right position
 * \param nred,ngreen,nblue rgb color
 */
static void fb_drawfullrectangle(int nx1pos, int ny1pos, int nx2pos, int ny2pos,
	unsigned char nred, unsigned char ngreen, unsigned char nblue)
{
	int cnt1, cnt2, nypos;
	unsigned thispix;
	unsigned char *ptr;

	thispix = fb_pixel_value(nred, ngreen, nblue);

	cnt1 = ny2pos - ny1pos;
	nypos = ny1pos;
	do {
		ptr = G.addr + nypos * G.scr_fix.line_length + nx1pos * G.bytes_per_pixel;
		cnt2 = nx2pos - nx1pos;
		do {
			fb_write_pixel(ptr, thispix);
			ptr += G.bytes_per_pixel;
		} while (--cnt2 >= 0);

		nypos++;
	} while (--cnt1 >= 0);
}


/**
 * Draw a progress bar on framebuffer
 * \param percent percentage of loading
 */
static void fb_drawprogressbar(unsigned percent)
{
	int left_x, top_y, pos_x;
	unsigned width, height;

	// outer box
	left_x = G.nbar_posx;
	top_y = G.nbar_posy;
	width = G.nbar_width - 1;
	height = G.nbar_height - 1;
	if ((int)(height | width) < 0)
		return;
	// NB: "width" of 1 actually makes rect with width of 2!
	fb_drawrectangle();

	// inner "empty" rectangle
	left_x++;
	top_y++;
	width -= 2;
	height -= 2;
	if ((int)(height | width) < 0)
		return;

	pos_x = left_x;
	if (percent > 0) {
		int i, y;

		// actual progress bar
		pos_x += (unsigned)(width * percent) / 100;

		y = top_y;
		i = height;
		if (height == 0)
			height++; // divide by 0 is bad
		while (i >= 0) {
			// draw one-line thick "rectangle"
			// top line will have gray lvl 200, bottom one 100
			unsigned gray_level = 100 + (unsigned)i*100 / height;
			fb_drawfullrectangle(
					left_x, y, pos_x, y,
					gray_level, gray_level, gray_level);
			y++;
			i--;
		}
	}

	fb_drawfullrectangle(
			pos_x, top_y,
			left_x + width, top_y + height,
			G.nbar_colr, G.nbar_colg, G.nbar_colb);
}


/**
 * Draw image from PPM file
 */
static void fb_drawimage(void)
{
	FILE *theme_file;
	char *read_ptr;
	unsigned char *pixline;
	unsigned i, j, width, height, line_size;

	if (LONE_DASH(G.image_filename)) {
		theme_file = stdin;
	} else {
		int fd = open_zipped(G.image_filename, /*fail_if_not_compressed:*/ 0);
		if (fd < 0)
			bb_simple_perror_msg_and_die(G.image_filename);
		theme_file = xfdopen_for_read(fd);
	}

	/* Parse ppm header:
	 * - Magic: two characters "P6".
	 * - Whitespace (blanks, TABs, CRs, LFs).
	 * - A width, formatted as ASCII characters in decimal.
	 * - Whitespace.
	 * - A height, ASCII decimal.
	 * - Whitespace.
	 * - The maximum color value, ASCII decimal, in 0..65535
	 * - Newline or other single whitespace character.
	 *   (we support newline only)
	 * - A raster of Width * Height pixels in triplets of rgb
	 *   in pure binary by 1 or 2 bytes. (we support only 1 byte)
	 */
#define concat_buf bb_common_bufsiz1
	setup_common_bufsiz();

	read_ptr = concat_buf;
	while (1) {
		int w, h, max_color_val;
		int rem = concat_buf + COMMON_BUFSIZE - read_ptr;
		if (rem < 2
		 || fgets(read_ptr, rem, theme_file) == NULL
		) {
			bb_error_msg_and_die("bad PPM file '%s'", G.image_filename);
		}
		read_ptr = strchrnul(read_ptr, '#');
		*read_ptr = '\0'; /* ignore #comments */
		if (sscanf(concat_buf, "P6 %u %u %u", &w, &h, &max_color_val) == 3
		 && max_color_val <= 255
		) {
			width = w; /* w is on stack, width may be in register */
			height = h;
			break;
		}
	}

	line_size = width*3;
	pixline = xmalloc(line_size);

	if ((width + G.img_posx) > G.scr_var.xres)
		width = G.scr_var.xres - G.img_posx;
	if ((height + G.img_posy) > G.scr_var.yres)
		height = G.scr_var.yres - G.img_posy;
	for (j = 0; j < height; j++) {
		unsigned char *pixel;
		unsigned char *src;

		if (fread(pixline, 1, line_size, theme_file) != line_size)
			bb_error_msg_and_die("bad PPM file '%s'", G.image_filename);
		pixel = pixline;
		src = G.addr + (G.img_posy + j) * G.scr_fix.line_length + G.img_posx * G.bytes_per_pixel;
		for (i = 0; i < width; i++) {
			unsigned thispix = fb_pixel_value(pixel[0], pixel[1], pixel[2]);
			fb_write_pixel(src, thispix);
			src += G.bytes_per_pixel;
			pixel += 3;
		}
	}
	free(pixline);
	fclose(theme_file);
}


/**
 * Parse configuration file
 * \param *cfg_filename name of the configuration file
 */
static void init(const char *cfg_filename)
{
	static const char param_names[] ALIGN1 =
		"BAR_WIDTH\0" "BAR_HEIGHT\0"
		"BAR_LEFT\0" "BAR_TOP\0"
		"BAR_R\0" "BAR_G\0" "BAR_B\0"
		"IMG_LEFT\0" "IMG_TOP\0"
#if DEBUG
		"DEBUG\0"
#endif
		;
	char *token[2];
	parser_t *parser = config_open2(cfg_filename, xfopen_stdin);
	while (config_read(parser, token, 2, 2, "#=",
				(PARSE_NORMAL | PARSE_MIN_DIE) & ~(PARSE_TRIM | PARSE_COLLAPSE))) {
		unsigned val = xatoi_positive(token[1]);
		int i = index_in_strings(param_names, token[0]);
		if (i < 0)
			bb_error_msg_and_die("syntax error: %s", token[0]);
		if (i >= 0 && i < 9)
			G.ns[i] = val;
#if DEBUG
		if (i == 9) {
			G.bdebug_messages = val;
			if (G.bdebug_messages)
				G.logfile_fd = xfopen_for_write("/tmp/fbsplash.log");
		}
#endif
	}
	config_close(parser);
}


int fbsplash_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int fbsplash_main(int argc UNUSED_PARAM, char **argv)
{
	const char *fb_device, *cfg_filename, *fifo_filename;
	FILE *fp = fp; // for compiler
	char *num_buf;
	unsigned num;
	bool bCursorOff;

	INIT_G();

	// parse command line options
	fb_device = "/dev/fb0";
	cfg_filename = NULL;
	fifo_filename = NULL;
	bCursorOff = 1 & getopt32(argv, "cs:d:i:f:",
			&G.image_filename, &fb_device, &cfg_filename, &fifo_filename);

	// parse configuration file
	if (cfg_filename)
		init(cfg_filename);

	// We must have -s IMG
	if (!G.image_filename)
		bb_show_usage();

	fb_open(fb_device);

	if (fifo_filename && bCursorOff) {
		// hide cursor (BEFORE any fb ops)
		full_write(STDOUT_FILENO, ESC"[?25l", 6);
	}

	fb_drawimage();

	if (!fifo_filename)
		return EXIT_SUCCESS;

	fp = xfopen_stdin(fifo_filename);
	if (fp != stdin) {
		// For named pipes, we want to support this:
		//  mkfifo cmd_pipe
		//  fbsplash -f cmd_pipe .... &
		//  ...
		//  echo 33 >cmd_pipe
		//  ...
		//  echo 66 >cmd_pipe
		// This means that we don't want fbsplash to get EOF
		// when last writer closes input end.
		// The simplest way is to open fifo for writing too
		// and become an additional writer :)
		open(fifo_filename, O_WRONLY); // errors are ignored
	}

	fb_drawprogressbar(0);
	// Block on read, waiting for some input.
	// Use of <stdio.h> style I/O allows to correctly
	// handle a case when we have many buffered lines
	// already in the pipe
	while ((num_buf = xmalloc_fgetline(fp)) != NULL) {
		if (is_prefixed_with(num_buf, "exit")) {
			DEBUG_MESSAGE("exit");
			break;
		}
		num = atoi(num_buf);
		if (isdigit(num_buf[0]) && (num <= 100)) {
#if DEBUG
			DEBUG_MESSAGE(itoa(num));
#endif
			fb_drawprogressbar(num);
		}
		free(num_buf);
	}

	if (bCursorOff) // restore cursor
		full_write(STDOUT_FILENO, ESC"[?25h", 6);

	return EXIT_SUCCESS;
}
