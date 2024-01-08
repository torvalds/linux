// SPDX-License-Identifier: GPL-2.0
/*
 * consolemap.c
 *
 * Mapping from internal code (such as Latin-1 or Unicode or IBM PC code)
 * to font positions.
 *
 * aeb, 950210
 *
 * Support for multiple unimaps by Jakub Jelinek <jj@ultra.linux.cz>, July 1998
 *
 * Fix bug in inverse translation. Stanislav Voronyi <stas@cnti.uanet.kharkov.ua>, Dec 1998
 *
 * In order to prevent the following circular lock dependency:
 *   &mm->mmap_lock --> cpu_hotplug.lock --> console_lock --> &mm->mmap_lock
 *
 * We cannot allow page fault to happen while holding the console_lock.
 * Therefore, all the userspace copy operations have to be done outside
 * the console_lock critical sections.
 *
 * As all the affected functions are all called directly from vt_ioctl(), we
 * can allocate some small buffers directly on stack without worrying about
 * stack overflow.
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/module.h>
#include <linux/kd.h>
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/tty.h>
#include <linux/uaccess.h>
#include <linux/console.h>
#include <linux/consolemap.h>
#include <linux/vt_kern.h>
#include <linux/string.h>

static unsigned short translations[][E_TABSZ] = {
  /* 8-bit Latin-1 mapped to Unicode -- trivial mapping */
  [LAT1_MAP] = {
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
    0x0008, 0x0009, 0x000a, 0x000b, 0x000c, 0x000d, 0x000e, 0x000f,
    0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
    0x0018, 0x0019, 0x001a, 0x001b, 0x001c, 0x001d, 0x001e, 0x001f,
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
    0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f,
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005a, 0x005b, 0x005c, 0x005d, 0x005e, 0x005f,
    0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
    0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f,
    0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
    0x0078, 0x0079, 0x007a, 0x007b, 0x007c, 0x007d, 0x007e, 0x007f,
    0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
    0x0088, 0x0089, 0x008a, 0x008b, 0x008c, 0x008d, 0x008e, 0x008f,
    0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
    0x0098, 0x0099, 0x009a, 0x009b, 0x009c, 0x009d, 0x009e, 0x009f,
    0x00a0, 0x00a1, 0x00a2, 0x00a3, 0x00a4, 0x00a5, 0x00a6, 0x00a7,
    0x00a8, 0x00a9, 0x00aa, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x00af,
    0x00b0, 0x00b1, 0x00b2, 0x00b3, 0x00b4, 0x00b5, 0x00b6, 0x00b7,
    0x00b8, 0x00b9, 0x00ba, 0x00bb, 0x00bc, 0x00bd, 0x00be, 0x00bf,
    0x00c0, 0x00c1, 0x00c2, 0x00c3, 0x00c4, 0x00c5, 0x00c6, 0x00c7,
    0x00c8, 0x00c9, 0x00ca, 0x00cb, 0x00cc, 0x00cd, 0x00ce, 0x00cf,
    0x00d0, 0x00d1, 0x00d2, 0x00d3, 0x00d4, 0x00d5, 0x00d6, 0x00d7,
    0x00d8, 0x00d9, 0x00da, 0x00db, 0x00dc, 0x00dd, 0x00de, 0x00df,
    0x00e0, 0x00e1, 0x00e2, 0x00e3, 0x00e4, 0x00e5, 0x00e6, 0x00e7,
    0x00e8, 0x00e9, 0x00ea, 0x00eb, 0x00ec, 0x00ed, 0x00ee, 0x00ef,
    0x00f0, 0x00f1, 0x00f2, 0x00f3, 0x00f4, 0x00f5, 0x00f6, 0x00f7,
    0x00f8, 0x00f9, 0x00fa, 0x00fb, 0x00fc, 0x00fd, 0x00fe, 0x00ff
  },
  /* VT100 graphics mapped to Unicode */
  [GRAF_MAP] = {
    0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007,
    0x0008, 0x0009, 0x000a, 0x000b, 0x000c, 0x000d, 0x000e, 0x000f,
    0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
    0x0018, 0x0019, 0x001a, 0x001b, 0x001c, 0x001d, 0x001e, 0x001f,
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
    0x0028, 0x0029, 0x002a, 0x2192, 0x2190, 0x2191, 0x2193, 0x002f,
    0x2588, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f,
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005a, 0x005b, 0x005c, 0x005d, 0x005e, 0x00a0,
    0x25c6, 0x2592, 0x2409, 0x240c, 0x240d, 0x240a, 0x00b0, 0x00b1,
    0x2591, 0x240b, 0x2518, 0x2510, 0x250c, 0x2514, 0x253c, 0x23ba,
    0x23bb, 0x2500, 0x23bc, 0x23bd, 0x251c, 0x2524, 0x2534, 0x252c,
    0x2502, 0x2264, 0x2265, 0x03c0, 0x2260, 0x00a3, 0x00b7, 0x007f,
    0x0080, 0x0081, 0x0082, 0x0083, 0x0084, 0x0085, 0x0086, 0x0087,
    0x0088, 0x0089, 0x008a, 0x008b, 0x008c, 0x008d, 0x008e, 0x008f,
    0x0090, 0x0091, 0x0092, 0x0093, 0x0094, 0x0095, 0x0096, 0x0097,
    0x0098, 0x0099, 0x009a, 0x009b, 0x009c, 0x009d, 0x009e, 0x009f,
    0x00a0, 0x00a1, 0x00a2, 0x00a3, 0x00a4, 0x00a5, 0x00a6, 0x00a7,
    0x00a8, 0x00a9, 0x00aa, 0x00ab, 0x00ac, 0x00ad, 0x00ae, 0x00af,
    0x00b0, 0x00b1, 0x00b2, 0x00b3, 0x00b4, 0x00b5, 0x00b6, 0x00b7,
    0x00b8, 0x00b9, 0x00ba, 0x00bb, 0x00bc, 0x00bd, 0x00be, 0x00bf,
    0x00c0, 0x00c1, 0x00c2, 0x00c3, 0x00c4, 0x00c5, 0x00c6, 0x00c7,
    0x00c8, 0x00c9, 0x00ca, 0x00cb, 0x00cc, 0x00cd, 0x00ce, 0x00cf,
    0x00d0, 0x00d1, 0x00d2, 0x00d3, 0x00d4, 0x00d5, 0x00d6, 0x00d7,
    0x00d8, 0x00d9, 0x00da, 0x00db, 0x00dc, 0x00dd, 0x00de, 0x00df,
    0x00e0, 0x00e1, 0x00e2, 0x00e3, 0x00e4, 0x00e5, 0x00e6, 0x00e7,
    0x00e8, 0x00e9, 0x00ea, 0x00eb, 0x00ec, 0x00ed, 0x00ee, 0x00ef,
    0x00f0, 0x00f1, 0x00f2, 0x00f3, 0x00f4, 0x00f5, 0x00f6, 0x00f7,
    0x00f8, 0x00f9, 0x00fa, 0x00fb, 0x00fc, 0x00fd, 0x00fe, 0x00ff
  },
  /* IBM Codepage 437 mapped to Unicode */
  [IBMPC_MAP] = {
    0x0000, 0x263a, 0x263b, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022,
    0x25d8, 0x25cb, 0x25d9, 0x2642, 0x2640, 0x266a, 0x266b, 0x263c,
    0x25b6, 0x25c0, 0x2195, 0x203c, 0x00b6, 0x00a7, 0x25ac, 0x21a8,
    0x2191, 0x2193, 0x2192, 0x2190, 0x221f, 0x2194, 0x25b2, 0x25bc,
    0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
    0x0028, 0x0029, 0x002a, 0x002b, 0x002c, 0x002d, 0x002e, 0x002f,
    0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
    0x0038, 0x0039, 0x003a, 0x003b, 0x003c, 0x003d, 0x003e, 0x003f,
    0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
    0x0048, 0x0049, 0x004a, 0x004b, 0x004c, 0x004d, 0x004e, 0x004f,
    0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
    0x0058, 0x0059, 0x005a, 0x005b, 0x005c, 0x005d, 0x005e, 0x005f,
    0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
    0x0068, 0x0069, 0x006a, 0x006b, 0x006c, 0x006d, 0x006e, 0x006f,
    0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
    0x0078, 0x0079, 0x007a, 0x007b, 0x007c, 0x007d, 0x007e, 0x2302,
    0x00c7, 0x00fc, 0x00e9, 0x00e2, 0x00e4, 0x00e0, 0x00e5, 0x00e7,
    0x00ea, 0x00eb, 0x00e8, 0x00ef, 0x00ee, 0x00ec, 0x00c4, 0x00c5,
    0x00c9, 0x00e6, 0x00c6, 0x00f4, 0x00f6, 0x00f2, 0x00fb, 0x00f9,
    0x00ff, 0x00d6, 0x00dc, 0x00a2, 0x00a3, 0x00a5, 0x20a7, 0x0192,
    0x00e1, 0x00ed, 0x00f3, 0x00fa, 0x00f1, 0x00d1, 0x00aa, 0x00ba,
    0x00bf, 0x2310, 0x00ac, 0x00bd, 0x00bc, 0x00a1, 0x00ab, 0x00bb,
    0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
    0x2555, 0x2563, 0x2551, 0x2557, 0x255d, 0x255c, 0x255b, 0x2510,
    0x2514, 0x2534, 0x252c, 0x251c, 0x2500, 0x253c, 0x255e, 0x255f,
    0x255a, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256c, 0x2567,
    0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256b,
    0x256a, 0x2518, 0x250c, 0x2588, 0x2584, 0x258c, 0x2590, 0x2580,
    0x03b1, 0x00df, 0x0393, 0x03c0, 0x03a3, 0x03c3, 0x00b5, 0x03c4,
    0x03a6, 0x0398, 0x03a9, 0x03b4, 0x221e, 0x03c6, 0x03b5, 0x2229,
    0x2261, 0x00b1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00f7, 0x2248,
    0x00b0, 0x2219, 0x00b7, 0x221a, 0x207f, 0x00b2, 0x25a0, 0x00a0
  },
  /* User mapping -- default to codes for direct font mapping */
  [USER_MAP] = {
    0xf000, 0xf001, 0xf002, 0xf003, 0xf004, 0xf005, 0xf006, 0xf007,
    0xf008, 0xf009, 0xf00a, 0xf00b, 0xf00c, 0xf00d, 0xf00e, 0xf00f,
    0xf010, 0xf011, 0xf012, 0xf013, 0xf014, 0xf015, 0xf016, 0xf017,
    0xf018, 0xf019, 0xf01a, 0xf01b, 0xf01c, 0xf01d, 0xf01e, 0xf01f,
    0xf020, 0xf021, 0xf022, 0xf023, 0xf024, 0xf025, 0xf026, 0xf027,
    0xf028, 0xf029, 0xf02a, 0xf02b, 0xf02c, 0xf02d, 0xf02e, 0xf02f,
    0xf030, 0xf031, 0xf032, 0xf033, 0xf034, 0xf035, 0xf036, 0xf037,
    0xf038, 0xf039, 0xf03a, 0xf03b, 0xf03c, 0xf03d, 0xf03e, 0xf03f,
    0xf040, 0xf041, 0xf042, 0xf043, 0xf044, 0xf045, 0xf046, 0xf047,
    0xf048, 0xf049, 0xf04a, 0xf04b, 0xf04c, 0xf04d, 0xf04e, 0xf04f,
    0xf050, 0xf051, 0xf052, 0xf053, 0xf054, 0xf055, 0xf056, 0xf057,
    0xf058, 0xf059, 0xf05a, 0xf05b, 0xf05c, 0xf05d, 0xf05e, 0xf05f,
    0xf060, 0xf061, 0xf062, 0xf063, 0xf064, 0xf065, 0xf066, 0xf067,
    0xf068, 0xf069, 0xf06a, 0xf06b, 0xf06c, 0xf06d, 0xf06e, 0xf06f,
    0xf070, 0xf071, 0xf072, 0xf073, 0xf074, 0xf075, 0xf076, 0xf077,
    0xf078, 0xf079, 0xf07a, 0xf07b, 0xf07c, 0xf07d, 0xf07e, 0xf07f,
    0xf080, 0xf081, 0xf082, 0xf083, 0xf084, 0xf085, 0xf086, 0xf087,
    0xf088, 0xf089, 0xf08a, 0xf08b, 0xf08c, 0xf08d, 0xf08e, 0xf08f,
    0xf090, 0xf091, 0xf092, 0xf093, 0xf094, 0xf095, 0xf096, 0xf097,
    0xf098, 0xf099, 0xf09a, 0xf09b, 0xf09c, 0xf09d, 0xf09e, 0xf09f,
    0xf0a0, 0xf0a1, 0xf0a2, 0xf0a3, 0xf0a4, 0xf0a5, 0xf0a6, 0xf0a7,
    0xf0a8, 0xf0a9, 0xf0aa, 0xf0ab, 0xf0ac, 0xf0ad, 0xf0ae, 0xf0af,
    0xf0b0, 0xf0b1, 0xf0b2, 0xf0b3, 0xf0b4, 0xf0b5, 0xf0b6, 0xf0b7,
    0xf0b8, 0xf0b9, 0xf0ba, 0xf0bb, 0xf0bc, 0xf0bd, 0xf0be, 0xf0bf,
    0xf0c0, 0xf0c1, 0xf0c2, 0xf0c3, 0xf0c4, 0xf0c5, 0xf0c6, 0xf0c7,
    0xf0c8, 0xf0c9, 0xf0ca, 0xf0cb, 0xf0cc, 0xf0cd, 0xf0ce, 0xf0cf,
    0xf0d0, 0xf0d1, 0xf0d2, 0xf0d3, 0xf0d4, 0xf0d5, 0xf0d6, 0xf0d7,
    0xf0d8, 0xf0d9, 0xf0da, 0xf0db, 0xf0dc, 0xf0dd, 0xf0de, 0xf0df,
    0xf0e0, 0xf0e1, 0xf0e2, 0xf0e3, 0xf0e4, 0xf0e5, 0xf0e6, 0xf0e7,
    0xf0e8, 0xf0e9, 0xf0ea, 0xf0eb, 0xf0ec, 0xf0ed, 0xf0ee, 0xf0ef,
    0xf0f0, 0xf0f1, 0xf0f2, 0xf0f3, 0xf0f4, 0xf0f5, 0xf0f6, 0xf0f7,
    0xf0f8, 0xf0f9, 0xf0fa, 0xf0fb, 0xf0fc, 0xf0fd, 0xf0fe, 0xf0ff
  }
};

/* The standard kernel character-to-font mappings are not invertible
   -- this is just a best effort. */

#define MAX_GLYPH 512		/* Max possible glyph value */

static enum translation_map inv_translate[MAX_NR_CONSOLES];

#define UNI_DIRS	32U
#define UNI_DIR_ROWS	32U
#define UNI_ROW_GLYPHS	64U

#define UNI_DIR_BITS		GENMASK(15, 11)
#define UNI_ROW_BITS		GENMASK(10,  6)
#define UNI_GLYPH_BITS		GENMASK( 5,  0)

#define UNI_DIR(uni)		FIELD_GET(UNI_DIR_BITS, (uni))
#define UNI_ROW(uni)		FIELD_GET(UNI_ROW_BITS, (uni))
#define UNI_GLYPH(uni)		FIELD_GET(UNI_GLYPH_BITS, (uni))

#define UNI(dir, row, glyph)	(FIELD_PREP(UNI_DIR_BITS, (dir)) | \
				 FIELD_PREP(UNI_ROW_BITS, (row)) | \
				 FIELD_PREP(UNI_GLYPH_BITS, (glyph)))

/**
 * struct uni_pagedict - unicode directory
 *
 * @uni_pgdir: 32*32*64 table with glyphs
 * @refcount: reference count of this structure
 * @sum: checksum
 * @inverse_translations: best-effort inverse mapping
 * @inverse_trans_unicode: best-effort inverse mapping to unicode
 */
struct uni_pagedict {
	u16		**uni_pgdir[UNI_DIRS];
	unsigned long	refcount;
	unsigned long	sum;
	unsigned char	*inverse_translations[LAST_MAP + 1];
	u16		*inverse_trans_unicode;
};

static struct uni_pagedict *dflt;

static void set_inverse_transl(struct vc_data *conp, struct uni_pagedict *dict,
	       enum translation_map m)
{
	unsigned short *t = translations[m];
	unsigned char *inv;

	if (!dict)
		return;
	inv = dict->inverse_translations[m];

	if (!inv) {
		inv = dict->inverse_translations[m] = kmalloc(MAX_GLYPH,
				GFP_KERNEL);
		if (!inv)
			return;
	}
	memset(inv, 0, MAX_GLYPH);

	for (unsigned int ch = 0; ch < ARRAY_SIZE(translations[m]); ch++) {
		int glyph = conv_uni_to_pc(conp, t[ch]);
		if (glyph >= 0 && glyph < MAX_GLYPH && inv[glyph] < 32) {
			/* prefer '-' above SHY etc. */
			inv[glyph] = ch;
		}
	}
}

static void set_inverse_trans_unicode(struct uni_pagedict *dict)
{
	unsigned int d, r, g;
	u16 *inv;

	if (!dict)
		return;

	inv = dict->inverse_trans_unicode;
	if (!inv) {
		inv = dict->inverse_trans_unicode = kmalloc_array(MAX_GLYPH,
				sizeof(*inv), GFP_KERNEL);
		if (!inv)
			return;
	}
	memset(inv, 0, MAX_GLYPH * sizeof(*inv));

	for (d = 0; d < UNI_DIRS; d++) {
		u16 **dir = dict->uni_pgdir[d];
		if (!dir)
			continue;
		for (r = 0; r < UNI_DIR_ROWS; r++) {
			u16 *row = dir[r];
			if (!row)
				continue;
			for (g = 0; g < UNI_ROW_GLYPHS; g++) {
				u16 glyph = row[g];
				if (glyph < MAX_GLYPH && inv[glyph] < 32)
					inv[glyph] = UNI(d, r, g);
			}
		}
	}
}

unsigned short *set_translate(enum translation_map m, struct vc_data *vc)
{
	inv_translate[vc->vc_num] = m;
	return translations[m];
}

/*
 * Inverse translation is impossible for several reasons:
 * 1. The font<->character maps are not 1-1.
 * 2. The text may have been written while a different translation map
 *    was active.
 * Still, it is now possible to a certain extent to cut and paste non-ASCII.
 */
u16 inverse_translate(const struct vc_data *conp, u16 glyph, bool use_unicode)
{
	struct uni_pagedict *p;
	enum translation_map m;

	if (glyph >= MAX_GLYPH)
		return 0;

	p = *conp->uni_pagedict_loc;
	if (!p)
		return glyph;

	if (use_unicode) {
		if (!p->inverse_trans_unicode)
			return glyph;

		return p->inverse_trans_unicode[glyph];
	}

	m = inv_translate[conp->vc_num];
	if (!p->inverse_translations[m])
		return glyph;

	return p->inverse_translations[m][glyph];
}
EXPORT_SYMBOL_GPL(inverse_translate);

static void update_user_maps(void)
{
	int i;
	struct uni_pagedict *p, *q = NULL;

	for (i = 0; i < MAX_NR_CONSOLES; i++) {
		if (!vc_cons_allocated(i))
			continue;
		p = *vc_cons[i].d->uni_pagedict_loc;
		if (p && p != q) {
			set_inverse_transl(vc_cons[i].d, p, USER_MAP);
			set_inverse_trans_unicode(p);
			q = p;
		}
	}
}

/*
 * Load customizable translation table
 * arg points to a 256 byte translation table.
 *
 * The "old" variants are for translation directly to font (using the
 * 0xf000-0xf0ff "transparent" Unicodes) whereas the "new" variants set
 * Unicodes explicitly.
 */
int con_set_trans_old(unsigned char __user * arg)
{
	unsigned short inbuf[E_TABSZ];
	unsigned int i;
	unsigned char ch;

	for (i = 0; i < ARRAY_SIZE(inbuf); i++) {
		if (get_user(ch, &arg[i]))
			return -EFAULT;
		inbuf[i] = UNI_DIRECT_BASE | ch;
	}

	console_lock();
	memcpy(translations[USER_MAP], inbuf, sizeof(inbuf));
	update_user_maps();
	console_unlock();
	return 0;
}

int con_get_trans_old(unsigned char __user * arg)
{
	int i, ch;
	unsigned short *p = translations[USER_MAP];
	unsigned char outbuf[E_TABSZ];

	console_lock();
	for (i = 0; i < ARRAY_SIZE(outbuf); i++)
	{
		ch = conv_uni_to_pc(vc_cons[fg_console].d, p[i]);
		outbuf[i] = (ch & ~0xff) ? 0 : ch;
	}
	console_unlock();

	return copy_to_user(arg, outbuf, sizeof(outbuf)) ? -EFAULT : 0;
}

int con_set_trans_new(ushort __user * arg)
{
	unsigned short inbuf[E_TABSZ];

	if (copy_from_user(inbuf, arg, sizeof(inbuf)))
		return -EFAULT;

	console_lock();
	memcpy(translations[USER_MAP], inbuf, sizeof(inbuf));
	update_user_maps();
	console_unlock();
	return 0;
}

int con_get_trans_new(ushort __user * arg)
{
	unsigned short outbuf[E_TABSZ];

	console_lock();
	memcpy(outbuf, translations[USER_MAP], sizeof(outbuf));
	console_unlock();

	return copy_to_user(arg, outbuf, sizeof(outbuf)) ? -EFAULT : 0;
}

/*
 * Unicode -> current font conversion
 *
 * A font has at most 512 chars, usually 256.
 * But one font position may represent several Unicode chars.
 * A hashtable is somewhat of a pain to deal with, so use a
 * "paged table" instead.  Simulation has shown the memory cost of
 * this 3-level paged table scheme to be comparable to a hash table.
 */

extern u8 dfont_unicount[];	/* Defined in console_defmap.c */
extern u16 dfont_unitable[];

static void con_release_unimap(struct uni_pagedict *dict)
{
	unsigned int d, r;

	if (dict == dflt)
		dflt = NULL;

	for (d = 0; d < UNI_DIRS; d++) {
		u16 **dir = dict->uni_pgdir[d];
		if (dir != NULL) {
			for (r = 0; r < UNI_DIR_ROWS; r++)
				kfree(dir[r]);
			kfree(dir);
		}
		dict->uni_pgdir[d] = NULL;
	}

	for (r = 0; r < ARRAY_SIZE(dict->inverse_translations); r++) {
		kfree(dict->inverse_translations[r]);
		dict->inverse_translations[r] = NULL;
	}

	kfree(dict->inverse_trans_unicode);
	dict->inverse_trans_unicode = NULL;
}

/* Caller must hold the console lock */
void con_free_unimap(struct vc_data *vc)
{
	struct uni_pagedict *p;

	p = *vc->uni_pagedict_loc;
	if (!p)
		return;
	*vc->uni_pagedict_loc = NULL;
	if (--p->refcount)
		return;
	con_release_unimap(p);
	kfree(p);
}

static int con_unify_unimap(struct vc_data *conp, struct uni_pagedict *dict1)
{
	struct uni_pagedict *dict2;
	unsigned int cons, d, r;

	for (cons = 0; cons < MAX_NR_CONSOLES; cons++) {
		if (!vc_cons_allocated(cons))
			continue;
		dict2 = *vc_cons[cons].d->uni_pagedict_loc;
		if (!dict2 || dict2 == dict1 || dict2->sum != dict1->sum)
			continue;
		for (d = 0; d < UNI_DIRS; d++) {
			u16 **dir1 = dict1->uni_pgdir[d];
			u16 **dir2 = dict2->uni_pgdir[d];
			if (!dir1 && !dir2)
				continue;
			if (!dir1 || !dir2)
				break;
			for (r = 0; r < UNI_DIR_ROWS; r++) {
				if (!dir1[r] && !dir2[r])
					continue;
				if (!dir1[r] || !dir2[r])
					break;
				if (memcmp(dir1[r], dir2[r], UNI_ROW_GLYPHS *
							sizeof(*dir1[r])))
					break;
			}
			if (r < UNI_DIR_ROWS)
				break;
		}
		if (d == UNI_DIRS) {
			dict2->refcount++;
			*conp->uni_pagedict_loc = dict2;
			con_release_unimap(dict1);
			kfree(dict1);
			return 1;
		}
	}
	return 0;
}

static int
con_insert_unipair(struct uni_pagedict *p, u_short unicode, u_short fontpos)
{
	u16 **dir, *row;
	unsigned int n;

	n = UNI_DIR(unicode);
	dir = p->uni_pgdir[n];
	if (!dir) {
		dir = p->uni_pgdir[n] = kcalloc(UNI_DIR_ROWS, sizeof(*dir),
				GFP_KERNEL);
		if (!dir)
			return -ENOMEM;
	}

	n = UNI_ROW(unicode);
	row = dir[n];
	if (!row) {
		row = dir[n] = kmalloc_array(UNI_ROW_GLYPHS, sizeof(*row),
				GFP_KERNEL);
		if (!row)
			return -ENOMEM;
		/* No glyphs for the characters (yet) */
		memset(row, 0xff, UNI_ROW_GLYPHS * sizeof(*row));
	}

	row[UNI_GLYPH(unicode)] = fontpos;

	p->sum += (fontpos << 20U) + unicode;

	return 0;
}

static int con_allocate_new(struct vc_data *vc)
{
	struct uni_pagedict *new, *old = *vc->uni_pagedict_loc;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new)
		return -ENOMEM;

	new->refcount = 1;
	*vc->uni_pagedict_loc = new;

	if (old)
		old->refcount--;

	return 0;
}

/* Caller must hold the lock */
static int con_do_clear_unimap(struct vc_data *vc)
{
	struct uni_pagedict *old = *vc->uni_pagedict_loc;

	if (!old || old->refcount > 1)
		return con_allocate_new(vc);

	old->sum = 0;
	con_release_unimap(old);

	return 0;
}

int con_clear_unimap(struct vc_data *vc)
{
	int ret;
	console_lock();
	ret = con_do_clear_unimap(vc);
	console_unlock();
	return ret;
}

static struct uni_pagedict *con_unshare_unimap(struct vc_data *vc,
		struct uni_pagedict *old)
{
	struct uni_pagedict *new;
	unsigned int d, r, g;
	int ret;
	u16 uni = 0;

	ret = con_allocate_new(vc);
	if (ret)
		return ERR_PTR(ret);

	new = *vc->uni_pagedict_loc;

	/*
	 * uni_pgdir is a 32*32*64 table with rows allocated when its first
	 * entry is added. The unicode value must still be incremented for
	 * empty rows. We are copying entries from "old" to "new".
	 */
	for (d = 0; d < UNI_DIRS; d++) {
		u16 **dir = old->uni_pgdir[d];
		if (!dir) {
			/* Account for empty table */
			uni += UNI_DIR_ROWS * UNI_ROW_GLYPHS;
			continue;
		}

		for (r = 0; r < UNI_DIR_ROWS; r++) {
			u16 *row = dir[r];
			if (!row) {
				/* Account for row of 64 empty entries */
				uni += UNI_ROW_GLYPHS;
				continue;
			}

			for (g = 0; g < UNI_ROW_GLYPHS; g++, uni++) {
				if (row[g] == 0xffff)
					continue;
				/*
				 * Found one, copy entry for unicode uni with
				 * fontpos value row[g].
				 */
				ret = con_insert_unipair(new, uni, row[g]);
				if (ret) {
					old->refcount++;
					*vc->uni_pagedict_loc = old;
					con_release_unimap(new);
					kfree(new);
					return ERR_PTR(ret);
				}
			}
		}
	}

	return new;
}

int con_set_unimap(struct vc_data *vc, ushort ct, struct unipair __user *list)
{
	int err = 0, err1;
	struct uni_pagedict *dict;
	struct unipair *unilist, *plist;

	if (!ct)
		return 0;

	unilist = vmemdup_user(list, array_size(sizeof(*unilist), ct));
	if (IS_ERR(unilist))
		return PTR_ERR(unilist);

	console_lock();

	/* Save original vc_unipagdir_loc in case we allocate a new one */
	dict = *vc->uni_pagedict_loc;
	if (!dict) {
		err = -EINVAL;
		goto out_unlock;
	}

	if (dict->refcount > 1) {
		dict = con_unshare_unimap(vc, dict);
		if (IS_ERR(dict)) {
			err = PTR_ERR(dict);
			goto out_unlock;
		}
	} else if (dict == dflt) {
		dflt = NULL;
	}

	/*
	 * Insert user specified unicode pairs into new table.
	 */
	for (plist = unilist; ct; ct--, plist++) {
		err1 = con_insert_unipair(dict, plist->unicode, plist->fontpos);
		if (err1)
			err = err1;
	}

	/*
	 * Merge with fontmaps of any other virtual consoles.
	 */
	if (con_unify_unimap(vc, dict))
		goto out_unlock;

	for (enum translation_map m = FIRST_MAP; m <= LAST_MAP; m++)
		set_inverse_transl(vc, dict, m);
	set_inverse_trans_unicode(dict);

out_unlock:
	console_unlock();
	kvfree(unilist);
	return err;
}

/**
 *	con_set_default_unimap	-	set default unicode map
 *	@vc: the console we are updating
 *
 *	Loads the unimap for the hardware font, as defined in uni_hash.tbl.
 *	The representation used was the most compact I could come up
 *	with.  This routine is executed at video setup, and when the
 *	PIO_FONTRESET ioctl is called.
 *
 *	The caller must hold the console lock
 */
int con_set_default_unimap(struct vc_data *vc)
{
	struct uni_pagedict *dict;
	unsigned int fontpos, count;
	int err = 0, err1;
	u16 *dfont;

	if (dflt) {
		dict = *vc->uni_pagedict_loc;
		if (dict == dflt)
			return 0;

		dflt->refcount++;
		*vc->uni_pagedict_loc = dflt;
		if (dict && !--dict->refcount) {
			con_release_unimap(dict);
			kfree(dict);
		}
		return 0;
	}

	/* The default font is always 256 characters */

	err = con_do_clear_unimap(vc);
	if (err)
		return err;

	dict = *vc->uni_pagedict_loc;
	dfont = dfont_unitable;

	for (fontpos = 0; fontpos < 256U; fontpos++)
		for (count = dfont_unicount[fontpos]; count; count--) {
			err1 = con_insert_unipair(dict, *(dfont++), fontpos);
			if (err1)
				err = err1;
		}

	if (con_unify_unimap(vc, dict)) {
		dflt = *vc->uni_pagedict_loc;
		return err;
	}

	for (enum translation_map m = FIRST_MAP; m <= LAST_MAP; m++)
		set_inverse_transl(vc, dict, m);
	set_inverse_trans_unicode(dict);
	dflt = dict;
	return err;
}
EXPORT_SYMBOL(con_set_default_unimap);

/**
 *	con_copy_unimap		-	copy unimap between two vts
 *	@dst_vc: target
 *	@src_vc: source
 *
 *	The caller must hold the console lock when invoking this method
 */
int con_copy_unimap(struct vc_data *dst_vc, struct vc_data *src_vc)
{
	struct uni_pagedict *src;

	if (!*src_vc->uni_pagedict_loc)
		return -EINVAL;
	if (*dst_vc->uni_pagedict_loc == *src_vc->uni_pagedict_loc)
		return 0;
	con_free_unimap(dst_vc);
	src = *src_vc->uni_pagedict_loc;
	src->refcount++;
	*dst_vc->uni_pagedict_loc = src;
	return 0;
}
EXPORT_SYMBOL(con_copy_unimap);

/*
 *	con_get_unimap		-	get the unicode map
 *
 *	Read the console unicode data for this console. Called from the ioctl
 *	handlers.
 */
int con_get_unimap(struct vc_data *vc, ushort ct, ushort __user *uct,
		struct unipair __user *list)
{
	ushort ect;
	struct uni_pagedict *dict;
	struct unipair *unilist;
	unsigned int d, r, g;
	int ret = 0;

	unilist = kvmalloc_array(ct, sizeof(*unilist), GFP_KERNEL);
	if (!unilist)
		return -ENOMEM;

	console_lock();

	ect = 0;
	dict = *vc->uni_pagedict_loc;
	if (!dict)
		goto unlock;

	for (d = 0; d < UNI_DIRS; d++) {
		u16 **dir = dict->uni_pgdir[d];
		if (!dir)
			continue;

		for (r = 0; r < UNI_DIR_ROWS; r++) {
			u16 *row = dir[r];
			if (!row)
				continue;

			for (g = 0; g < UNI_ROW_GLYPHS; g++, row++) {
				if (*row >= MAX_GLYPH)
					continue;
				if (ect < ct) {
					unilist[ect].unicode = UNI(d, r, g);
					unilist[ect].fontpos = *row;
				}
				ect++;
			}
		}
	}
unlock:
	console_unlock();
	if (copy_to_user(list, unilist, min(ect, ct) * sizeof(*unilist)))
		ret = -EFAULT;
	if (put_user(ect, uct))
		ret = -EFAULT;
	kvfree(unilist);
	return ret ? ret : (ect <= ct) ? 0 : -ENOMEM;
}

/*
 * Always use USER_MAP. These functions are used by the keyboard,
 * which shouldn't be affected by G0/G1 switching, etc.
 * If the user map still contains default values, i.e. the
 * direct-to-font mapping, then assume user is using Latin1.
 *
 * FIXME: at some point we need to decide if we want to lock the table
 * update element itself via the keyboard_event_lock for consistency with the
 * keyboard driver as well as the consoles
 */
/* may be called during an interrupt */
u32 conv_8bit_to_uni(unsigned char c)
{
	unsigned short uni = translations[USER_MAP][c];
	return uni == (0xf000 | c) ? c : uni;
}

int conv_uni_to_8bit(u32 uni)
{
	int c;
	for (c = 0; c < ARRAY_SIZE(translations[USER_MAP]); c++)
		if (translations[USER_MAP][c] == uni ||
		   (translations[USER_MAP][c] == (c | 0xf000) && uni == c))
			return c;
	return -1;
}

int conv_uni_to_pc(struct vc_data *conp, long ucs)
{
	struct uni_pagedict *dict;
	u16 **dir, *row, glyph;

	/* Only 16-bit codes supported at this time */
	if (ucs > 0xffff)
		return -4;		/* Not found */
	else if (ucs < 0x20)
		return -1;		/* Not a printable character */
	else if (ucs == 0xfeff || (ucs >= 0x200b && ucs <= 0x200f))
		return -2;			/* Zero-width space */
	/*
	 * UNI_DIRECT_BASE indicates the start of the region in the User Zone
	 * which always has a 1:1 mapping to the currently loaded font.  The
	 * UNI_DIRECT_MASK indicates the bit span of the region.
	 */
	else if ((ucs & ~UNI_DIRECT_MASK) == UNI_DIRECT_BASE)
		return ucs & UNI_DIRECT_MASK;

	dict = *conp->uni_pagedict_loc;
	if (!dict)
		return -3;

	dir = dict->uni_pgdir[UNI_DIR(ucs)];
	if (!dir)
		return -4;

	row = dir[UNI_ROW(ucs)];
	if (!row)
		return -4;

	glyph = row[UNI_GLYPH(ucs)];
	if (glyph >= MAX_GLYPH)
		return -4;

	return glyph;
}

/*
 * This is called at sys_setup time, after memory and the console are
 * initialized.  It must be possible to call kmalloc(..., GFP_KERNEL)
 * from this function, hence the call from sys_setup.
 */
void __init
console_map_init(void)
{
	int i;

	for (i = 0; i < MAX_NR_CONSOLES; i++)
		if (vc_cons_allocated(i) && !*vc_cons[i].d->uni_pagedict_loc)
			con_set_default_unimap(vc_cons[i].d);
}

