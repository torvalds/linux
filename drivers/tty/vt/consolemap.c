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

static unsigned short translations[][256] = {
  /* 8-bit Latin-1 mapped to Unicode -- trivial mapping */
  {
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
  {
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
  {
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
  {
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

static int inv_translate[MAX_NR_CONSOLES];

struct uni_pagedir {
	u16 		**uni_pgdir[32];
	unsigned long	refcount;
	unsigned long	sum;
	unsigned char	*inverse_translations[4];
	u16		*inverse_trans_unicode;
};

static struct uni_pagedir *dflt;

static void set_inverse_transl(struct vc_data *conp, struct uni_pagedir *p, int i)
{
	int j, glyph;
	unsigned short *t = translations[i];
	unsigned char *q;
	
	if (!p) return;
	q = p->inverse_translations[i];

	if (!q) {
		q = p->inverse_translations[i] = kmalloc(MAX_GLYPH, GFP_KERNEL);
		if (!q) return;
	}
	memset(q, 0, MAX_GLYPH);

	for (j = 0; j < E_TABSZ; j++) {
		glyph = conv_uni_to_pc(conp, t[j]);
		if (glyph >= 0 && glyph < MAX_GLYPH && q[glyph] < 32) {
			/* prefer '-' above SHY etc. */
		  	q[glyph] = j;
		}
	}
}

static void set_inverse_trans_unicode(struct vc_data *conp,
				      struct uni_pagedir *p)
{
	int i, j, k, glyph;
	u16 **p1, *p2;
	u16 *q;

	if (!p) return;
	q = p->inverse_trans_unicode;
	if (!q) {
		q = p->inverse_trans_unicode =
			kmalloc_array(MAX_GLYPH, sizeof(u16), GFP_KERNEL);
		if (!q)
			return;
	}
	memset(q, 0, MAX_GLYPH * sizeof(u16));

	for (i = 0; i < 32; i++) {
		p1 = p->uni_pgdir[i];
		if (!p1)
			continue;
		for (j = 0; j < 32; j++) {
			p2 = p1[j];
			if (!p2)
				continue;
			for (k = 0; k < 64; k++) {
				glyph = p2[k];
				if (glyph >= 0 && glyph < MAX_GLYPH
					       && q[glyph] < 32)
		  			q[glyph] = (i << 11) + (j << 6) + k;
			}
		}
	}
}

unsigned short *set_translate(int m, struct vc_data *vc)
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
u16 inverse_translate(const struct vc_data *conp, int glyph, int use_unicode)
{
	struct uni_pagedir *p;
	int m;
	if (glyph < 0 || glyph >= MAX_GLYPH)
		return 0;
	else {
		p = *conp->vc_uni_pagedir_loc;
		if (!p)
			return glyph;
		else if (use_unicode) {
			if (!p->inverse_trans_unicode)
				return glyph;
			else
				return p->inverse_trans_unicode[glyph];
			} else {
			m = inv_translate[conp->vc_num];
			if (!p->inverse_translations[m])
				return glyph;
			else
				return p->inverse_translations[m][glyph];
			}
	}
}
EXPORT_SYMBOL_GPL(inverse_translate);

static void update_user_maps(void)
{
	int i;
	struct uni_pagedir *p, *q = NULL;
	
	for (i = 0; i < MAX_NR_CONSOLES; i++) {
		if (!vc_cons_allocated(i))
			continue;
		p = *vc_cons[i].d->vc_uni_pagedir_loc;
		if (p && p != q) {
			set_inverse_transl(vc_cons[i].d, p, USER_MAP);
			set_inverse_trans_unicode(vc_cons[i].d, p);
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
	int i;
	unsigned short inbuf[E_TABSZ];
	unsigned char ubuf[E_TABSZ];

	if (copy_from_user(ubuf, arg, E_TABSZ))
		return -EFAULT;

	for (i = 0; i < E_TABSZ ; i++)
		inbuf[i] = UNI_DIRECT_BASE | ubuf[i];

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
	for (i = 0; i < E_TABSZ ; i++)
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

static void con_release_unimap(struct uni_pagedir *p)
{
	u16 **p1;
	int i, j;

	if (p == dflt) dflt = NULL;  
	for (i = 0; i < 32; i++) {
		p1 = p->uni_pgdir[i];
		if (p1 != NULL) {
			for (j = 0; j < 32; j++)
				kfree(p1[j]);
			kfree(p1);
		}
		p->uni_pgdir[i] = NULL;
	}
	for (i = 0; i < 4; i++) {
		kfree(p->inverse_translations[i]);
		p->inverse_translations[i] = NULL;
	}
	kfree(p->inverse_trans_unicode);
	p->inverse_trans_unicode = NULL;
}

/* Caller must hold the console lock */
void con_free_unimap(struct vc_data *vc)
{
	struct uni_pagedir *p;

	p = *vc->vc_uni_pagedir_loc;
	if (!p)
		return;
	*vc->vc_uni_pagedir_loc = NULL;
	if (--p->refcount)
		return;
	con_release_unimap(p);
	kfree(p);
}
  
static int con_unify_unimap(struct vc_data *conp, struct uni_pagedir *p)
{
	int i, j, k;
	struct uni_pagedir *q;
	
	for (i = 0; i < MAX_NR_CONSOLES; i++) {
		if (!vc_cons_allocated(i))
			continue;
		q = *vc_cons[i].d->vc_uni_pagedir_loc;
		if (!q || q == p || q->sum != p->sum)
			continue;
		for (j = 0; j < 32; j++) {
			u16 **p1, **q1;
			p1 = p->uni_pgdir[j]; q1 = q->uni_pgdir[j];
			if (!p1 && !q1)
				continue;
			if (!p1 || !q1)
				break;
			for (k = 0; k < 32; k++) {
				if (!p1[k] && !q1[k])
					continue;
				if (!p1[k] || !q1[k])
					break;
				if (memcmp(p1[k], q1[k], 64*sizeof(u16)))
					break;
			}
			if (k < 32)
				break;
		}
		if (j == 32) {
			q->refcount++;
			*conp->vc_uni_pagedir_loc = q;
			con_release_unimap(p);
			kfree(p);
			return 1;
		}
	}
	return 0;
}

static int
con_insert_unipair(struct uni_pagedir *p, u_short unicode, u_short fontpos)
{
	int i, n;
	u16 **p1, *p2;

	p1 = p->uni_pgdir[n = unicode >> 11];
	if (!p1) {
		p1 = p->uni_pgdir[n] = kmalloc_array(32, sizeof(u16 *),
						     GFP_KERNEL);
		if (!p1) return -ENOMEM;
		for (i = 0; i < 32; i++)
			p1[i] = NULL;
	}

	p2 = p1[n = (unicode >> 6) & 0x1f];
	if (!p2) {
		p2 = p1[n] = kmalloc_array(64, sizeof(u16), GFP_KERNEL);
		if (!p2) return -ENOMEM;
		memset(p2, 0xff, 64*sizeof(u16)); /* No glyphs for the characters (yet) */
	}

	p2[unicode & 0x3f] = fontpos;
	
	p->sum += (fontpos << 20) + unicode;

	return 0;
}

/* Caller must hold the lock */
static int con_do_clear_unimap(struct vc_data *vc)
{
	struct uni_pagedir *p, *q;

	p = *vc->vc_uni_pagedir_loc;
	if (!p || --p->refcount) {
		q = kzalloc(sizeof(*p), GFP_KERNEL);
		if (!q) {
			if (p)
				p->refcount++;
			return -ENOMEM;
		}
		q->refcount=1;
		*vc->vc_uni_pagedir_loc = q;
	} else {
		if (p == dflt) dflt = NULL;
		p->refcount++;
		p->sum = 0;
		con_release_unimap(p);
	}
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
	
int con_set_unimap(struct vc_data *vc, ushort ct, struct unipair __user *list)
{
	int err = 0, err1, i;
	struct uni_pagedir *p, *q;
	struct unipair *unilist, *plist;

	if (!ct)
		return 0;

	unilist = vmemdup_user(list, array_size(sizeof(struct unipair), ct));
	if (IS_ERR(unilist))
		return PTR_ERR(unilist);

	console_lock();

	/* Save original vc_unipagdir_loc in case we allocate a new one */
	p = *vc->vc_uni_pagedir_loc;

	if (!p) {
		err = -EINVAL;

		goto out_unlock;
	}
	
	if (p->refcount > 1) {
		int j, k;
		u16 **p1, *p2, l;
		
		err1 = con_do_clear_unimap(vc);
		if (err1) {
			err = err1;
			goto out_unlock;
		}
		
		/*
		 * Since refcount was > 1, con_clear_unimap() allocated a
		 * a new uni_pagedir for this vc.  Re: p != q
		 */
		q = *vc->vc_uni_pagedir_loc;

		/*
		 * uni_pgdir is a 32*32*64 table with rows allocated
		 * when its first entry is added.  The unicode value must
		 * still be incremented for empty rows.  We are copying
		 * entries from "p" (old) to "q" (new).
		 */
		l = 0;		/* unicode value */
		for (i = 0; i < 32; i++) {
		p1 = p->uni_pgdir[i];
		if (p1)
			for (j = 0; j < 32; j++) {
			p2 = p1[j];
			if (p2) {
				for (k = 0; k < 64; k++, l++)
				if (p2[k] != 0xffff) {
					/*
					 * Found one, copy entry for unicode
					 * l with fontpos value p2[k].
					 */
					err1 = con_insert_unipair(q, l, p2[k]);
					if (err1) {
						p->refcount++;
						*vc->vc_uni_pagedir_loc = p;
						con_release_unimap(q);
						kfree(q);
						err = err1;
						goto out_unlock;
					}
				}
			} else {
				/* Account for row of 64 empty entries */
				l += 64;
			}
		}
		else
			/* Account for empty table */
			l += 32 * 64;
		}

		/*
		 * Finished copying font table, set vc_uni_pagedir to new table
		 */
		p = q;
	} else if (p == dflt) {
		dflt = NULL;
	}

	/*
	 * Insert user specified unicode pairs into new table.
	 */
	for (plist = unilist; ct; ct--, plist++) {
		err1 = con_insert_unipair(p, plist->unicode, plist->fontpos);
		if (err1)
			err = err1;
	}
	
	/*
	 * Merge with fontmaps of any other virtual consoles.
	 */
	if (con_unify_unimap(vc, p))
		goto out_unlock;

	for (i = 0; i <= 3; i++)
		set_inverse_transl(vc, p, i); /* Update inverse translations */
	set_inverse_trans_unicode(vc, p);

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
	int i, j, err = 0, err1;
	u16 *q;
	struct uni_pagedir *p;

	if (dflt) {
		p = *vc->vc_uni_pagedir_loc;
		if (p == dflt)
			return 0;

		dflt->refcount++;
		*vc->vc_uni_pagedir_loc = dflt;
		if (p && !--p->refcount) {
			con_release_unimap(p);
			kfree(p);
		}
		return 0;
	}
	
	/* The default font is always 256 characters */

	err = con_do_clear_unimap(vc);
	if (err)
		return err;
    
	p = *vc->vc_uni_pagedir_loc;
	q = dfont_unitable;
	
	for (i = 0; i < 256; i++)
		for (j = dfont_unicount[i]; j; j--) {
			err1 = con_insert_unipair(p, *(q++), i);
			if (err1)
				err = err1;
		}
			
	if (con_unify_unimap(vc, p)) {
		dflt = *vc->vc_uni_pagedir_loc;
		return err;
	}

	for (i = 0; i <= 3; i++)
		set_inverse_transl(vc, p, i);	/* Update all inverse translations */
	set_inverse_trans_unicode(vc, p);
	dflt = p;
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
	struct uni_pagedir *q;

	if (!*src_vc->vc_uni_pagedir_loc)
		return -EINVAL;
	if (*dst_vc->vc_uni_pagedir_loc == *src_vc->vc_uni_pagedir_loc)
		return 0;
	con_free_unimap(dst_vc);
	q = *src_vc->vc_uni_pagedir_loc;
	q->refcount++;
	*dst_vc->vc_uni_pagedir_loc = q;
	return 0;
}
EXPORT_SYMBOL(con_copy_unimap);

/*
 *	con_get_unimap		-	get the unicode map
 *
 *	Read the console unicode data for this console. Called from the ioctl
 *	handlers.
 */
int con_get_unimap(struct vc_data *vc, ushort ct, ushort __user *uct, struct unipair __user *list)
{
	int i, j, k, ret = 0;
	ushort ect;
	u16 **p1, *p2;
	struct uni_pagedir *p;
	struct unipair *unilist;

	unilist = kvmalloc_array(ct, sizeof(struct unipair), GFP_KERNEL);
	if (!unilist)
		return -ENOMEM;

	console_lock();

	ect = 0;
	if (*vc->vc_uni_pagedir_loc) {
		p = *vc->vc_uni_pagedir_loc;
		for (i = 0; i < 32; i++) {
		p1 = p->uni_pgdir[i];
		if (p1)
			for (j = 0; j < 32; j++) {
			p2 = *(p1++);
			if (p2)
				for (k = 0; k < 64; k++, p2++) {
					if (*p2 >= MAX_GLYPH)
						continue;
					if (ect < ct) {
						unilist[ect].unicode =
							(i<<11)+(j<<6)+k;
						unilist[ect].fontpos = *p2;
					}
					ect++;
				}
			}
		}
	}
	console_unlock();
	if (copy_to_user(list, unilist, min(ect, ct) * sizeof(struct unipair)))
		ret = -EFAULT;
	put_user(ect, uct);
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
	for (c = 0; c < 0x100; c++)
		if (translations[USER_MAP][c] == uni ||
		   (translations[USER_MAP][c] == (c | 0xf000) && uni == c))
			return c;
	return -1;
}

int
conv_uni_to_pc(struct vc_data *conp, long ucs) 
{
	int h;
	u16 **p1, *p2;
	struct uni_pagedir *p;
  
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
  
	if (!*conp->vc_uni_pagedir_loc)
		return -3;

	p = *conp->vc_uni_pagedir_loc;
	if ((p1 = p->uni_pgdir[ucs >> 11]) &&
	    (p2 = p1[(ucs >> 6) & 0x1f]) &&
	    (h = p2[ucs & 0x3f]) < MAX_GLYPH)
		return h;

	return -4;		/* not found */
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
		if (vc_cons_allocated(i) && !*vc_cons[i].d->vc_uni_pagedir_loc)
			con_set_default_unimap(vc_cons[i].d);
}

