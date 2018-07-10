/* vi: set sw=4 ts=4: */
/*
 * loadfont.c - Eugene Crosser & Andries Brouwer
 *
 * Version 0.96bb
 *
 * Loads the console font, and possibly the corresponding screen map(s).
 * (Adapted for busybox by Matej Vela.)
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config LOADFONT
//config:	bool "loadfont (5.4 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	This program loads a console font from standard input.
//config:
//config:config SETFONT
//config:	bool "setfont (26 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Allows to load console screen map. Useful for i18n.
//config:
//config:config FEATURE_SETFONT_TEXTUAL_MAP
//config:	bool "Support reading textual screen maps"
//config:	default y
//config:	depends on SETFONT
//config:	help
//config:	Support reading textual screen maps.
//config:
//config:config DEFAULT_SETFONT_DIR
//config:	string "Default directory for console-tools files"
//config:	default ""
//config:	depends on SETFONT
//config:	help
//config:	Directory to use if setfont's params are simple filenames
//config:	(not /path/to/file or ./file). Default is "" (no default directory).
//config:
//config:comment "Common options for loadfont and setfont"
//config:	depends on LOADFONT || SETFONT
//config:
//config:config FEATURE_LOADFONT_PSF2
//config:	bool "Support PSF2 console fonts"
//config:	default y
//config:	depends on LOADFONT || SETFONT
//config:
//config:config FEATURE_LOADFONT_RAW
//config:	bool "Support old (raw) console fonts"
//config:	default y
//config:	depends on LOADFONT || SETFONT

//applet:IF_LOADFONT(APPLET_NOEXEC(loadfont, loadfont, BB_DIR_USR_SBIN, BB_SUID_DROP, loadfont))
//applet:IF_SETFONT(APPLET_NOEXEC(setfont, setfont, BB_DIR_USR_SBIN, BB_SUID_DROP, setfont))

//kbuild:lib-$(CONFIG_LOADFONT) += loadfont.o
//kbuild:lib-$(CONFIG_SETFONT) += loadfont.o

#include "libbb.h"
#include <sys/kd.h>

#ifndef KDFONTOP
# define KDFONTOP 0x4B72
struct console_font_op {
	unsigned op;            /* KD_FONT_OP_* */
	unsigned flags;         /* KD_FONT_FLAG_* */
	unsigned width, height;
	unsigned charcount;
	unsigned char *data;    /* font data with height fixed to 32 */
};
# define KD_FONT_OP_SET          0  /* Set font */
# define KD_FONT_OP_GET          1  /* Get font */
# define KD_FONT_OP_SET_DEFAULT  2  /* Set font to default, data points to name / NULL */
# define KD_FONT_OP_COPY         3  /* Copy from another console */
# define KD_FONT_FLAG_OLD        0x80000000 /* Invoked via old interface */
# define KD_FONT_FLAG_DONT_RECALC 1 /* Don't call adjust_height() */
                                   /* (Used internally for PIO_FONT support) */
#endif /* KDFONTOP */


enum {
	PSF1_MAGIC0 = 0x36,
	PSF1_MAGIC1 = 0x04,
	PSF1_MODE512 = 0x01,
	PSF1_MODEHASTAB = 0x02,
	PSF1_MODEHASSEQ = 0x04,
	PSF1_MAXMODE = 0x05,
	PSF1_STARTSEQ = 0xfffe,
	PSF1_SEPARATOR = 0xffff,
};

struct psf1_header {
	unsigned char magic[2];         /* Magic number */
	unsigned char mode;             /* PSF font mode */
	unsigned char charsize;         /* Character size */
};

#define psf1h(x) ((struct psf1_header*)(x))

#define PSF1_MAGIC_OK(x) ( \
     (x)->magic[0] == PSF1_MAGIC0 \
  && (x)->magic[1] == PSF1_MAGIC1 \
)

#if ENABLE_FEATURE_LOADFONT_PSF2
enum {
	PSF2_MAGIC0 = 0x72,
	PSF2_MAGIC1 = 0xb5,
	PSF2_MAGIC2 = 0x4a,
	PSF2_MAGIC3 = 0x86,
	PSF2_HAS_UNICODE_TABLE = 0x01,
	PSF2_MAXVERSION = 0,
	PSF2_STARTSEQ = 0xfe,
	PSF2_SEPARATOR = 0xff
};

struct psf2_header {
	unsigned char magic[4];
	unsigned int version;
	unsigned int headersize;    /* offset of bitmaps in file */
	unsigned int flags;
	unsigned int length;        /* number of glyphs */
	unsigned int charsize;      /* number of bytes for each character */
	unsigned int height;        /* max dimensions of glyphs */
	unsigned int width;         /* charsize = height * ((width + 7) / 8) */
};

#define psf2h(x) ((struct psf2_header*)(x))

#define PSF2_MAGIC_OK(x) ( \
     (x)->magic[0] == PSF2_MAGIC0 \
  && (x)->magic[1] == PSF2_MAGIC1 \
  && (x)->magic[2] == PSF2_MAGIC2 \
  && (x)->magic[3] == PSF2_MAGIC3 \
)
#endif /* ENABLE_FEATURE_LOADFONT_PSF2 */


static void do_loadfont(int fd, unsigned char *inbuf, int height, int width, int charsize, int fontsize)
{
	unsigned char *buf;
	int charwidth = 32 * ((width+7)/8);
	int i;

	if (height < 1 || height > 32 || width < 1 || width > 32)
		bb_error_msg_and_die("bad character size %dx%d", height, width);

	buf = xzalloc(charwidth * ((fontsize < 128) ? 128 : fontsize));
	for (i = 0; i < fontsize; i++)
		memcpy(buf + (i*charwidth), inbuf + (i*charsize), charsize);

	{ /* KDFONTOP */
		struct console_font_op cfo;
		cfo.op = KD_FONT_OP_SET;
		cfo.flags = 0;
		cfo.width = width;
		cfo.height = height;
		cfo.charcount = fontsize;
		cfo.data = buf;
		xioctl(fd, KDFONTOP, &cfo);
	}

	free(buf);
}

/*
 * Format of the Unicode information:
 *
 * For each font position <uc>*<seq>*<term>
 * where <uc> is a 2-byte little endian Unicode value (PSF1)
 * or an UTF-8 coded value (PSF2),
 * <seq> = <ss><uc><uc>*, <ss> = psf1 ? 0xFFFE : 0xFE,
 * <term> = psf1 ? 0xFFFF : 0xFF.
 * and * denotes zero or more occurrences of the preceding item.
 *
 * Semantics:
 * The leading <uc>* part gives Unicode symbols that are all
 * represented by this font position. The following sequences
 * are sequences of Unicode symbols - probably a symbol
 * together with combining accents - also represented by
 * this font position.
 *
 * Example:
 * At the font position for a capital A-ring glyph, we
 * may have:
 *   00C5,212B,FFFE,0041,030A,FFFF
 * Some font positions may be described by sequences only,
 * namely when there is no precomposed Unicode value for the glyph.
 */
#if !ENABLE_FEATURE_LOADFONT_PSF2
#define do_loadtable(fd, inbuf, tailsz, fontsize, psf2) \
	do_loadtable(fd, inbuf, tailsz, fontsize)
#endif
static void do_loadtable(int fd, unsigned char *inbuf, int tailsz, int fontsize, int psf2)
{
#if !ENABLE_FEATURE_LOADFONT_PSF2
/* gcc 4.3.1 code size: */
# define psf2 0 /* +0 bytes */
//	const int psf2 = 0; /* +8 bytes */
//	enum { psf2 = 0 }; /* +13 bytes */
#endif
	struct unimapinit advice;
	struct unimapdesc ud;
	struct unipair *up;
	int ct = 0, maxct;
	int glyph;
	uint16_t unicode;

	maxct = tailsz; /* more than enough */
	up = xmalloc(maxct * sizeof(*up));

	for (glyph = 0; glyph < fontsize; glyph++) {
		while (tailsz > 0) {
			if (!psf2) { /* PSF1 */
				unicode = (((uint16_t) inbuf[1]) << 8) + inbuf[0];
				tailsz -= 2;
				inbuf += 2;
				if (unicode == PSF1_SEPARATOR)
					break;
			} else { /* PSF2 */
#if ENABLE_FEATURE_LOADFONT_PSF2
				--tailsz;
				unicode = *inbuf++;
				if (unicode == PSF2_SEPARATOR) {
					break;
				} else if (unicode == PSF2_STARTSEQ) {
					bb_error_msg_and_die("unicode sequences not implemented");
				} else if (unicode >= 0xC0) {
					if (unicode >= 0xFC)
						unicode &= 0x01, maxct = 5;
					else if (unicode >= 0xF8)
						unicode &= 0x03, maxct = 4;
					else if (unicode >= 0xF0)
						unicode &= 0x07, maxct = 3;
					else if (unicode >= 0xE0)
						unicode &= 0x0F, maxct = 2;
					else
						unicode &= 0x1F, maxct = 1;
					do {
						if (tailsz <= 0 || *inbuf < 0x80 || *inbuf > 0xBF)
							bb_error_msg_and_die("illegal UTF-8 character");
						--tailsz;
						unicode = (unicode << 6) + (*inbuf++ & 0x3F);
					} while (--maxct > 0);
				} else if (unicode >= 0x80) {
					bb_error_msg_and_die("illegal UTF-8 character");
				}
#else
				return;
#endif
			}
			up[ct].unicode = unicode;
			up[ct].fontpos = glyph;
			ct++;
		}
	}

	/* Note: after PIO_UNIMAPCLR and before PIO_UNIMAP
	 * this printf did not work on many kernels */

	advice.advised_hashsize = 0;
	advice.advised_hashstep = 0;
	advice.advised_hashlevel = 0;
	xioctl(fd, PIO_UNIMAPCLR, &advice);
	ud.entry_ct = ct;
	ud.entries = up;
	xioctl(fd, PIO_UNIMAP, &ud);
#undef psf2
}

static void do_load(int fd, unsigned char *buffer, size_t len)
{
	int height;
	int width = 8;
	int charsize;
	int fontsize = 256;
	int has_table = 0;
	unsigned char *font = buffer;
	unsigned char *table;

	if (len >= sizeof(struct psf1_header) && PSF1_MAGIC_OK(psf1h(buffer))) {
		if (psf1h(buffer)->mode > PSF1_MAXMODE)
			bb_error_msg_and_die("unsupported psf file mode");
		if (psf1h(buffer)->mode & PSF1_MODE512)
			fontsize = 512;
		if (psf1h(buffer)->mode & PSF1_MODEHASTAB)
			has_table = 1;
		height = charsize = psf1h(buffer)->charsize;
		font += sizeof(struct psf1_header);
	} else
#if ENABLE_FEATURE_LOADFONT_PSF2
	if (len >= sizeof(struct psf2_header) && PSF2_MAGIC_OK(psf2h(buffer))) {
		if (psf2h(buffer)->version > PSF2_MAXVERSION)
			bb_error_msg_and_die("unsupported psf file version");
		fontsize = psf2h(buffer)->length;
		if (psf2h(buffer)->flags & PSF2_HAS_UNICODE_TABLE)
			has_table = 2;
		charsize = psf2h(buffer)->charsize;
		height = psf2h(buffer)->height;
		width = psf2h(buffer)->width;
		font += psf2h(buffer)->headersize;
	} else
#endif
#if ENABLE_FEATURE_LOADFONT_RAW
	if (len == 9780) {  /* file with three code pages? */
		charsize = height = 16;
		font += 40;
	} else if ((len & 0377) == 0) {  /* bare font */
		charsize = height = len / 256;
	} else
#endif
	{
		bb_error_msg_and_die("input file: bad length or unsupported font type");
	}

#if !defined(PIO_FONTX) || defined(__sparc__)
	if (fontsize != 256)
		bb_error_msg_and_die("only fontsize 256 supported");
#endif

	table = font + fontsize * charsize;
	buffer += len;

	if (table > buffer || (!has_table && table != buffer))
		bb_error_msg_and_die("input file: bad length");

	do_loadfont(fd, font, height, width, charsize, fontsize);

	if (has_table)
		do_loadtable(fd, table, buffer - table, fontsize, has_table - 1);
}


#if ENABLE_LOADFONT
//usage:#define loadfont_trivial_usage
//usage:       "< font"
//usage:#define loadfont_full_usage "\n\n"
//usage:       "Load a console font from stdin"
/* //usage:     "\n	-C TTY	Affect TTY instead of /dev/tty" */
//usage:
//usage:#define loadfont_example_usage
//usage:       "$ loadfont < /etc/i18n/fontname\n"
int loadfont_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int loadfont_main(int argc UNUSED_PARAM, char **argv)
{
	size_t len;
	unsigned char *buffer;

	// no arguments allowed!
	getopt32(argv, "^" "" "\0" "=0");

	/*
	 * We used to look at the length of the input file
	 * with stat(); now that we accept compressed files,
	 * just read the entire file.
	 * Len was 32k, but latarcyrheb-sun32.psfu is 34377 bytes
	 * (it has largish Unicode map).
	 */
	len = 128*1024;
	buffer = xmalloc_read(STDIN_FILENO, &len);
	// xmalloc_open_zipped_read_close(filename, &len);
	if (!buffer)
		bb_perror_msg_and_die("error reading input font");
	do_load(get_console_fd_or_die(), buffer, len);

	return EXIT_SUCCESS;
}
#endif


#if ENABLE_SETFONT
/* kbd-1.12:
setfont [-O font+umap.orig] [-o font.orig] [-om cmap.orig]
[-ou umap.orig] [-N] [font.new ...] [-m cmap] [-u umap] [-C console]
[-hNN] [-v] [-V]

-h NN  Override font height
-o file
       Save previous font in file
-O file
       Save previous font and Unicode map in file
-om file
       Store console map in file
-ou file
       Save previous Unicode map in file
-m file
       Load console map or Unicode console map from file
-u file
       Load Unicode table describing the font from file
       Example:
       # cp866
       0x00-0x7f       idem
       #
       0x80    U+0410  # CYRILLIC CAPITAL LETTER A
       0x81    U+0411  # CYRILLIC CAPITAL LETTER BE
       0x82    U+0412  # CYRILLIC CAPITAL LETTER VE
-C console
       Set the font for the indicated console
-v     Verbose
-V     Version
*/
//usage:#define setfont_trivial_usage
//usage:       "FONT [-m MAPFILE] [-C TTY]"
//usage:#define setfont_full_usage "\n\n"
//usage:       "Load a console font\n"
//usage:     "\n	-m MAPFILE	Load console screen map"
//usage:     "\n	-C TTY		Affect TTY instead of /dev/tty"
//usage:
//usage:#define setfont_example_usage
//usage:       "$ setfont -m koi8-r /etc/i18n/fontname\n"

# if ENABLE_FEATURE_SETFONT_TEXTUAL_MAP
static int ctoi(char *s)
{
	if (s[0] == '\'' && s[1] != '\0' && s[2] == '\'' && s[3] == '\0')
		return s[1];
	// U+ means 0x
	if (s[0] == 'U' && s[1] == '+') {
		s[0] = '0';
		s[1] = 'x';
	}
	if (!isdigit(s[0]))
		return -1;
	return xstrtoul(s, 0);
}
# endif

int setfont_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int setfont_main(int argc UNUSED_PARAM, char **argv)
{
	size_t len;
	unsigned opts;
	int fd;
	unsigned char *buffer;
	char *mapfilename;
	const char *tty_name = CURRENT_TTY;

	opts = getopt32(argv, "^" "m:C:" "\0" "=1", &mapfilename, &tty_name);
	argv += optind;

	fd = xopen_nonblocking(tty_name);

	if (sizeof(CONFIG_DEFAULT_SETFONT_DIR) > 1) { // if not ""
		if (*argv[0] != '/') {
			// goto default fonts location. don't die if doesn't exist
			chdir(CONFIG_DEFAULT_SETFONT_DIR "/consolefonts");
		}
	}
	// load font
	len = 128*1024;
	buffer = xmalloc_open_zipped_read_close(*argv, &len);
	if (!buffer)
		bb_simple_perror_msg_and_die(*argv);
	do_load(fd, buffer, len);

	// load the screen map, if any
	if (opts & 1) { // -m
		unsigned mode = PIO_SCRNMAP;
		void *map;

		if (sizeof(CONFIG_DEFAULT_SETFONT_DIR) > 1) { // if not ""
			if (mapfilename[0] != '/') {
				// goto default keymaps location
				chdir(CONFIG_DEFAULT_SETFONT_DIR "/consoletrans");
			}
		}
		// fetch keymap
		map = xmalloc_open_zipped_read_close(mapfilename, &len);
		if (!map)
			bb_simple_perror_msg_and_die(mapfilename);
		// file size is 256 or 512 bytes? -> assume binary map
		if (len == E_TABSZ || len == 2*E_TABSZ) {
			if (len == 2*E_TABSZ)
				mode = PIO_UNISCRNMAP;
		}
# if ENABLE_FEATURE_SETFONT_TEXTUAL_MAP
		// assume textual Unicode console maps:
		// 0x00 U+0000  #  NULL (NUL)
		// 0x01 U+0001  #  START OF HEADING (SOH)
		// 0x02 U+0002  #  START OF TEXT (STX)
		// 0x03 U+0003  #  END OF TEXT (ETX)
		else {
			int i;
			char *token[2];
			parser_t *parser;

			if (ENABLE_FEATURE_CLEAN_UP)
				free(map);
			map = xmalloc(E_TABSZ * sizeof(unsigned short));

#define unicodes ((unsigned short *)map)
			// fill vanilla map
			for (i = 0; i < E_TABSZ; i++)
				unicodes[i] = 0xf000 + i;

			parser = config_open(mapfilename);
			while (config_read(parser, token, 2, 2, "# \t", PARSE_NORMAL | PARSE_MIN_DIE)) {
				// parse code/value pair
				int a = ctoi(token[0]);
				int b = ctoi(token[1]);
				if (a < 0 || a >= E_TABSZ
				 || b < 0 || b > 65535
				) {
					bb_error_msg_and_die("map format");
				}
				// patch map
				unicodes[a] = b;
				// unicode character is met?
				if (b > 255)
					mode = PIO_UNISCRNMAP;
			}
			if (ENABLE_FEATURE_CLEAN_UP)
				config_close(parser);

			if (mode != PIO_UNISCRNMAP) {
#define asciis ((unsigned char *)map)
				for (i = 0; i < E_TABSZ; i++)
					asciis[i] = unicodes[i];
#undef asciis
			}
#undef unicodes
		}
# endif // ENABLE_FEATURE_SETFONT_TEXTUAL_MAP

		// do set screen map
		xioctl(fd, mode, map);

		if (ENABLE_FEATURE_CLEAN_UP)
			free(map);
	}

	return EXIT_SUCCESS;
}
#endif
