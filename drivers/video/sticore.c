// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/drivers/video/console/sticore.c -
 *	core code for console driver using HP's STI firmware
 *
 *	Copyright (C) 2000 Philipp Rumpf <prumpf@tux.org>
 *	Copyright (C) 2001-2023 Helge Deller <deller@gmx.de>
 *	Copyright (C) 2001-2002 Thomas Bogendoerfer <tsbogend@alpha.franken.de>
 *
 * TODO:
 * - call STI in virtual mode rather than in real mode
 * - screen blanking with state_mgmt() in text mode STI ?
 * - try to make it work on m68k hp workstations ;)
 *
 */

#define pr_fmt(fmt) "%s: " fmt, KBUILD_MODNAME

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/font.h>

#include <asm/hardware.h>
#include <asm/page.h>
#include <asm/parisc-device.h>
#include <asm/pdc.h>
#include <asm/cacheflush.h>
#include <asm/grfioctl.h>

#include <video/sticore.h>

#define STI_DRIVERVERSION "Version 0.9c"

static struct sti_struct *default_sti __read_mostly;

/* number of STI ROMS found and their ptrs to each struct */
static int num_sti_roms __read_mostly;
static struct sti_struct *sti_roms[MAX_STI_ROMS] __read_mostly;

static void *store_sti_val(struct sti_struct *sti, void *ptr, unsigned long val)
{
	u32 *ptr32 = ptr;

	if (IS_ENABLED(CONFIG_64BIT) && sti->do_call64) {
		/* used for 64-bit STI ROM */
		unsigned long *ptr64 = ptr;

		ptr64 = PTR_ALIGN(ptr64, sizeof(void *));
		*ptr64++ = val;
		return ptr64;
	}

	/* used for 32-bit STI ROM */
	*ptr32++ = val;
	return ptr32;
}

#define store_sti_ptr(sti, dest, ptr)	\
		store_sti_val(sti, dest, STI_PTR(ptr))

/* The colour indices used by STI are
 *   0 - Black
 *   1 - White
 *   2 - Red
 *   3 - Yellow/Brown
 *   4 - Green
 *   5 - Cyan
 *   6 - Blue
 *   7 - Magenta
 *
 * So we have the same colours as VGA (basically one bit each for R, G, B),
 * but have to translate them, anyway. */

static const u8 col_trans[8] = {
        0, 6, 4, 5,
        2, 7, 3, 1
};

#define c_fg(sti, c) col_trans[((c>> 8) & 7)]
#define c_bg(sti, c) col_trans[((c>>11) & 7)]
#define c_index(sti, c) ((c) & 0xff)

static const struct sti_init_flags default_init_flags = {
	.wait	= STI_WAIT,
	.reset	= 1,
	.text	= 1,
	.nontext = 1,
	.no_chg_bet = 1,
	.no_chg_bei = 1,
	.init_cmap_tx = 1,
};

static int sti_init_graph(struct sti_struct *sti)
{
	struct sti_init_inptr *inptr = &sti->sti_data->init_inptr;
	struct sti_init_inptr_ext *inptr_ext = &sti->sti_data->init_inptr_ext;
	struct sti_init_outptr *outptr = &sti->sti_data->init_outptr;
	unsigned long flags;
	int ret, err;

	spin_lock_irqsave(&sti->lock, flags);

	memset(inptr, 0, sizeof(*inptr));
	inptr->text_planes = 3; /* # of text planes (max 3 for STI) */
	memset(inptr_ext, 0, sizeof(*inptr_ext));
	store_sti_ptr(sti, &inptr->ext_ptr, inptr_ext);
	outptr->errno = 0;

	ret = sti_call(sti, sti->init_graph, &default_init_flags, inptr,
		outptr, sti->glob_cfg);

	if (ret >= 0)
		sti->text_planes = outptr->text_planes;
	err = outptr->errno;

	spin_unlock_irqrestore(&sti->lock, flags);

	if (ret < 0) {
		pr_err("STI init_graph failed (ret %d, errno %d)\n", ret, err);
		return -1;
	}

	return 0;
}

static const struct sti_conf_flags default_conf_flags = {
	.wait	= STI_WAIT,
};

static void sti_inq_conf(struct sti_struct *sti)
{
	struct sti_conf_inptr *inptr = &sti->sti_data->inq_inptr;
	struct sti_conf_outptr *outptr = &sti->sti_data->inq_outptr;
	unsigned long flags;
	s32 ret;

	store_sti_ptr(sti, &outptr->ext_ptr, &sti->sti_data->inq_outptr_ext);

	do {
		spin_lock_irqsave(&sti->lock, flags);
		memset(inptr, 0, sizeof(*inptr));
		ret = sti_call(sti, sti->inq_conf, &default_conf_flags,
			inptr, outptr, sti->glob_cfg);
		spin_unlock_irqrestore(&sti->lock, flags);
	} while (ret == 1);
}

static const struct sti_font_flags default_font_flags = {
	.wait		= STI_WAIT,
	.non_text	= 0,
};

void
sti_putc(struct sti_struct *sti, int c, int y, int x,
	 struct sti_cooked_font *font)
{
	struct sti_font_inptr *inptr;
	struct sti_font_inptr inptr_default = {
		.font_start_addr = (void *)STI_PTR(font->raw),
		.index		= c_index(sti, c),
		.fg_color	= c_fg(sti, c),
		.bg_color	= c_bg(sti, c),
		.dest_x		= x * font->width,
		.dest_y		= y * font->height,
	};
	struct sti_font_outptr *outptr = &sti->sti_data->font_outptr;
	s32 ret;
	unsigned long flags;

	do {
		spin_lock_irqsave(&sti->lock, flags);
		inptr = &inptr_default;
		if (IS_ENABLED(CONFIG_64BIT) && !sti->do_call64) {
			/* copy below 4G if calling 32-bit on LP64 kernel */
			inptr = &sti->sti_data->font_inptr;
			*inptr = inptr_default;
			/* skip first 4 bytes for 32-bit STI call */
			inptr = (void *)(((unsigned long)inptr) + sizeof(u32));
		}
		ret = sti_call(sti, sti->font_unpmv, &default_font_flags,
			inptr, outptr, sti->glob_cfg);
		spin_unlock_irqrestore(&sti->lock, flags);
	} while (ret == 1);
}

static const struct sti_blkmv_flags clear_blkmv_flags = {
	.wait	= STI_WAIT,
	.color	= 1,
	.clear	= 1,
};

void
sti_set(struct sti_struct *sti, int src_y, int src_x,
	int height, int width, u8 color)
{
	struct sti_blkmv_inptr *inptr;
	struct sti_blkmv_inptr inptr_default = {
		.fg_color	= color,
		.bg_color	= color,
		.src_x		= src_x,
		.src_y		= src_y,
		.dest_x		= src_x,
		.dest_y		= src_y,
		.width		= width,
		.height		= height,
	};
	struct sti_blkmv_outptr *outptr = &sti->sti_data->blkmv_outptr;
	s32 ret;
	unsigned long flags;

	do {
		spin_lock_irqsave(&sti->lock, flags);
		inptr = &inptr_default;
		if (IS_ENABLED(CONFIG_64BIT) && !sti->do_call64) {
			/* copy below 4G if calling 32-bit on LP64 kernel */
			inptr = &sti->sti_data->blkmv_inptr;
			*inptr = inptr_default;
		}
		ret = sti_call(sti, sti->block_move, &clear_blkmv_flags,
			inptr, outptr, sti->glob_cfg);
		spin_unlock_irqrestore(&sti->lock, flags);
	} while (ret == 1);
}

void
sti_clear(struct sti_struct *sti, int src_y, int src_x,
	  int height, int width, int c, struct sti_cooked_font *font)
{
	struct sti_blkmv_inptr *inptr;
	struct sti_blkmv_inptr inptr_default = {
		.fg_color	= c_fg(sti, c),
		.bg_color	= c_bg(sti, c),
		.src_x		= src_x * font->width,
		.src_y		= src_y * font->height,
		.dest_x		= src_x * font->width,
		.dest_y		= src_y * font->height,
		.width		= width * font->width,
		.height		= height * font->height,
	};
	struct sti_blkmv_outptr *outptr = &sti->sti_data->blkmv_outptr;
	s32 ret;
	unsigned long flags;

	do {
		spin_lock_irqsave(&sti->lock, flags);
		inptr = &inptr_default;
		if (IS_ENABLED(CONFIG_64BIT) && !sti->do_call64) {
			/* copy below 4G if calling 32-bit on LP64 kernel */
			inptr = &sti->sti_data->blkmv_inptr;
			*inptr = inptr_default;
		}
		ret = sti_call(sti, sti->block_move, &clear_blkmv_flags,
			inptr, outptr, sti->glob_cfg);
		spin_unlock_irqrestore(&sti->lock, flags);
	} while (ret == 1);
}

static const struct sti_blkmv_flags default_blkmv_flags = {
	.wait = STI_WAIT,
};

void
sti_bmove(struct sti_struct *sti, int src_y, int src_x,
	  int dst_y, int dst_x, int height, int width,
	  struct sti_cooked_font *font)
{
	struct sti_blkmv_inptr *inptr;
	struct sti_blkmv_inptr inptr_default = {
		.src_x		= src_x * font->width,
		.src_y		= src_y * font->height,
		.dest_x		= dst_x * font->width,
		.dest_y		= dst_y * font->height,
		.width		= width * font->width,
		.height		= height * font->height,
	};
	struct sti_blkmv_outptr *outptr = &sti->sti_data->blkmv_outptr;
	s32 ret;
	unsigned long flags;

	do {
		spin_lock_irqsave(&sti->lock, flags);
		inptr = &inptr_default;
		if (IS_ENABLED(CONFIG_64BIT) && !sti->do_call64) {
			/* copy below 4G if calling 32-bit on LP64 kernel */
			inptr = &sti->sti_data->blkmv_inptr;
			*inptr = inptr_default;
		}
		ret = sti_call(sti, sti->block_move, &default_blkmv_flags,
			inptr, outptr, sti->glob_cfg);
		spin_unlock_irqrestore(&sti->lock, flags);
	} while (ret == 1);
}


static void sti_flush(unsigned long start, unsigned long end)
{
	flush_icache_range(start, end);
}

static void sti_rom_copy(unsigned long base, unsigned long count, void *dest)
{
	unsigned long dest_start = (unsigned long) dest;

	/* this still needs to be revisited (see arch/parisc/mm/init.c:246) ! */
	while (count >= 4) {
		count -= 4;
		*(u32 *)dest = gsc_readl(base);
		base += 4;
		dest += 4;
	}
	while (count) {
		count--;
		*(u8 *)dest = gsc_readb(base);
		base++;
		dest++;
	}

	sti_flush(dest_start, (unsigned long)dest);
}




static char default_sti_path[21] __read_mostly;

#ifndef MODULE
static int __init sti_setup(char *str)
{
	if (str)
		strscpy(default_sti_path, str, sizeof(default_sti_path));

	return 1;
}

/*	Assuming the machine has multiple STI consoles (=graphic cards) which
 *	all get detected by sticon, the user may define with the linux kernel
 *	parameter sti=<x> which of them will be the initial boot-console.
 *	<x> is a number between 0 and MAX_STI_ROMS, with 0 as the default
 *	STI screen.
 */
__setup("sti=", sti_setup);
#endif



static char *font_name;
static int font_index,
	   font_height,
	   font_width;
#ifndef MODULE
static int sti_font_setup(char *str)
{
	/*
	 * The default font can be selected in various ways.
	 * a) sti_font=VGA8x16, sti_font=10x20, sti_font=10*20 selects
	 *    an built-in Linux framebuffer font.
	 * b) sti_font=<index>, where index is (1..x) with 1 selecting
	 *    the first HP STI ROM built-in font..
	 */

	if (*str >= '0' && *str <= '9') {
		char *x;

		if ((x = strchr(str, 'x')) || (x = strchr(str, '*'))) {
			font_height = simple_strtoul(str, NULL, 0);
			font_width = simple_strtoul(x+1, NULL, 0);
		} else {
			font_index = simple_strtoul(str, NULL, 0);
		}
	} else {
		font_name = str;	/* fb font name */
	}

	return 1;
}

/*	The optional linux kernel parameter "sti_font" defines which font
 *	should be used by the sticon driver to draw characters to the screen.
 *	Possible values are:
 *	- sti_font=<fb_fontname>:
 *		<fb_fontname> is the name of one of the linux-kernel built-in
 *		framebuffer font names (e.g. VGA8x16, SUN22x18).
 *		This is only available if the fonts have been statically compiled
 *		in with e.g. the CONFIG_FONT_8x16 or CONFIG_FONT_SUN12x22 options.
 *	- sti_font=<number>	(<number> = 1,2,3,...)
 *		most STI ROMs have built-in HP specific fonts, which can be selected
 *		by giving the desired number to the sticon driver.
 *		NOTE: This number is machine and STI ROM dependend.
 *	- sti_font=<height>x<width>  (e.g. sti_font=16x8)
 *		<height> and <width> gives hints to the height and width of the
 *		font which the user wants. The sticon driver will try to use
 *		a font with this height and width, but if no suitable font is
 *		found, sticon will use the default 8x8 font.
 */
__setup("sti_font=", sti_font_setup);
#endif



static void sti_dump_globcfg(struct sti_struct *sti)
{
	struct sti_glob_cfg *glob_cfg = sti->glob_cfg;
	struct sti_glob_cfg_ext *cfg = &sti->sti_data->glob_cfg_ext;

	pr_debug("%d text planes\n"
		"%4d x %4d screen resolution\n"
		"%4d x %4d offscreen\n"
		"%4d x %4d layout\n",
		glob_cfg->text_planes,
		glob_cfg->onscreen_x, glob_cfg->onscreen_y,
		glob_cfg->offscreen_x, glob_cfg->offscreen_y,
		glob_cfg->total_x, glob_cfg->total_y);

	/* dump extended cfg */
	pr_debug("monitor %d\n"
		"in friendly mode: %d\n"
		"power consumption %d watts\n"
		"freq ref %d\n"
		"sti_mem_addr %px (size=%d bytes)\n",
		cfg->curr_mon,
		cfg->friendly_boot,
		cfg->power,
		cfg->freq_ref,
		cfg->sti_mem_addr, sti->sti_mem_request);
}

static void sti_dump_outptr(struct sti_struct *sti)
{
	pr_debug("%d bits per pixel\n"
		"%d used bits\n"
		"%d planes\n"
		"attributes %08x\n",
		 sti->sti_data->inq_outptr.bits_per_pixel,
		 sti->sti_data->inq_outptr.bits_used,
		 sti->sti_data->inq_outptr.planes,
		 sti->sti_data->inq_outptr.attributes);
}

static int sti_init_glob_cfg(struct sti_struct *sti, unsigned long rom_address,
			     unsigned long hpa)
{
	struct sti_glob_cfg *glob_cfg;
	struct sti_glob_cfg_ext *glob_cfg_ext;
	void *save_addr, *ptr;
	void *sti_mem_addr;
	int i, size;

	if (sti->sti_mem_request < 256)
		sti->sti_mem_request = 256; /* STI default */

	size = sizeof(struct sti_all_data) + sti->sti_mem_request - 256;

	sti->sti_data = kzalloc(size, STI_LOWMEM);
	if (!sti->sti_data)
		return -ENOMEM;

	glob_cfg	= &sti->sti_data->glob_cfg;
	glob_cfg_ext	= &sti->sti_data->glob_cfg_ext;
	save_addr	= &sti->sti_data->save_addr;
	sti_mem_addr	= &sti->sti_data->sti_mem_addr;

	for (i = 0; i < STI_REGION_MAX; i++) {
		unsigned long newhpa, len;

		if (sti->pd) {
			unsigned char offs = sti->rm_entry[i];

			if (offs == 0)
				continue;
			if (offs != PCI_ROM_ADDRESS &&
			    (offs < PCI_BASE_ADDRESS_0 ||
			     offs > PCI_BASE_ADDRESS_5)) {
				pr_warn("STI pci region mapping for region %d (%02x) can't be mapped\n",
					i,sti->rm_entry[i]);
				continue;
			}
			newhpa = pci_resource_start (sti->pd, (offs - PCI_BASE_ADDRESS_0) / 4);
		} else
			newhpa = (i == 0) ? rom_address : hpa;

		sti->regions_phys[i] =
			REGION_OFFSET_TO_PHYS(sti->regions[i], newhpa);

		len = sti->regions[i].region_desc.length * 4096;

		pr_debug("region #%d: phys %08lx, len=%lukB, "
			 "btlb=%d, sysonly=%d, cache=%d, last=%d\n",
			i, sti->regions_phys[i], len / 1024,
			sti->regions[i].region_desc.btlb,
			sti->regions[i].region_desc.sys_only,
			sti->regions[i].region_desc.cache,
			sti->regions[i].region_desc.last);

		/* last entry reached ? */
		if (sti->regions[i].region_desc.last)
			break;
	}

	ptr = &glob_cfg->region_ptrs;
	for (i = 0; i < STI_REGION_MAX; i++)
		ptr = store_sti_val(sti, ptr, sti->regions_phys[i]);

	*(s32 *)ptr = 0;	/* set reent_lvl */
	ptr += sizeof(s32);
	ptr = store_sti_ptr(sti, ptr, save_addr);
	ptr = store_sti_ptr(sti, ptr, glob_cfg_ext);

	store_sti_ptr(sti, &glob_cfg_ext->sti_mem_addr, sti_mem_addr);

	sti->glob_cfg = glob_cfg;

	return 0;
}

#ifdef CONFIG_FONT_SUPPORT
static struct sti_cooked_font *
sti_select_fbfont(struct sti_cooked_rom *cooked_rom, const char *fbfont_name)
{
	const struct font_desc *fbfont = NULL;
	unsigned int size, bpc;
	void *dest;
	struct sti_rom_font *nf;
	struct sti_cooked_font *cooked_font;

	if (fbfont_name && strlen(fbfont_name))
		fbfont = find_font(fbfont_name);
	if (!fbfont)
		fbfont = get_default_font(1024, 768, NULL, NULL);
	if (!fbfont)
		return NULL;

	pr_info("    using %ux%u framebuffer font %s\n",
			fbfont->width, fbfont->height, fbfont->name);

	bpc = ((fbfont->width+7)/8) * fbfont->height;
	size = bpc * fbfont->charcount;
	size += sizeof(struct sti_rom_font);

	nf = kzalloc(size, STI_LOWMEM);
	if (!nf)
		return NULL;

	nf->first_char = 0;
	nf->last_char = fbfont->charcount - 1;
	nf->width = fbfont->width;
	nf->height = fbfont->height;
	nf->font_type = STI_FONT_HPROMAN8;
	nf->bytes_per_char = bpc;
	nf->next_font = 0;
	nf->underline_height = 1;
	nf->underline_pos = fbfont->height - nf->underline_height;

	dest = nf;
	dest += sizeof(struct sti_rom_font);
	memcpy(dest, fbfont->data, bpc * fbfont->charcount);

	cooked_font = kzalloc(sizeof(*cooked_font), GFP_KERNEL);
	if (!cooked_font) {
		kfree(nf);
		return NULL;
	}

	cooked_font->raw = nf;
	cooked_font->raw_ptr = nf;
	cooked_font->next_font = NULL;

	cooked_rom->font_start = cooked_font;

	return cooked_font;
}
#else
static struct sti_cooked_font *
sti_select_fbfont(struct sti_cooked_rom *cooked_rom, const char *fbfont_name)
{
	return NULL;
}
#endif

static void sti_dump_font(struct sti_cooked_font *font)
{
#ifdef STI_DUMP_FONT
	unsigned char *p = (unsigned char *)font->raw;
	int n;

	p += sizeof(struct sti_rom_font);
	pr_debug("  w %d h %d bpc %d\n", font->width, font->height,
					font->raw->bytes_per_char);

	for (n = 0; n < 256 * font->raw->bytes_per_char; n += 16, p += 16) {
		pr_debug("        0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x,"
			" 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x,"
			" 0x%02x, 0x%02x, 0x%02x, 0x%02x,\n",
			p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8],
			p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
	}
#endif
}

static int sti_search_font(struct sti_cooked_rom *rom, int height, int width)
{
	struct sti_cooked_font *font;
	int i = 0;

	for (font = rom->font_start; font; font = font->next_font, i++) {
		if ((font->raw->width == width) &&
		    (font->raw->height == height))
			return i;
	}
	return 0;
}

static struct sti_cooked_font *sti_select_font(struct sti_cooked_rom *rom)
{
	struct sti_cooked_font *font;
	int i;

	/* check for framebuffer-font first */
	if (!font_index) {
		font = sti_select_fbfont(rom, font_name);
		if (font)
			return font;
	}

	if (font_width && font_height)
		font_index = sti_search_font(rom,
				font_height, font_width);

	for (font = rom->font_start, i = font_index - 1;
		font && (i > 0);
		font = font->next_font, i--);

	if (font)
		return font;
	else
		return rom->font_start;
}


static void sti_dump_rom(struct sti_struct *sti)
{
	struct sti_rom *rom = sti->rom->raw;
	struct sti_cooked_font *font_start;
	int nr;

	pr_info("  id %04x-%04x, conforms to spec rev. %d.%02x\n",
		rom->graphics_id[0],
		rom->graphics_id[1],
		rom->revno[0] >> 4,
		rom->revno[0] & 0x0f);
	pr_debug("  supports %d monitors\n", rom->num_mons);
	pr_debug("  font start %08x\n", rom->font_start);
	pr_debug("  region list %08x\n", rom->region_list);
	pr_debug("  init_graph %08x\n", rom->init_graph);
	pr_debug("  bus support %02x\n", rom->bus_support);
	pr_debug("  ext bus support %02x\n", rom->ext_bus_support);
	pr_debug("  alternate code type %d\n", rom->alt_code_type);

	font_start = sti->rom->font_start;
	nr = 0;
	while (font_start) {
		struct sti_rom_font *f = font_start->raw;

		pr_info("    built-in font #%d: size %dx%d, chars %d-%d, bpc %d\n", ++nr,
			f->width, f->height,
			f->first_char, f->last_char, f->bytes_per_char);
		font_start = font_start->next_font;
	}
}


static int sti_cook_fonts(struct sti_cooked_rom *cooked_rom,
			  struct sti_rom *raw_rom)
{
	struct sti_rom_font *raw_font, *font_start;
	struct sti_cooked_font *cooked_font;

	cooked_font = kzalloc(sizeof(*cooked_font), GFP_KERNEL);
	if (!cooked_font)
		return 0;

	cooked_rom->font_start = cooked_font;

	raw_font = ((void *)raw_rom) + (raw_rom->font_start);

	font_start = raw_font;
	cooked_font->raw = raw_font;

	while (raw_font->next_font) {
		raw_font = ((void *)font_start) + (raw_font->next_font);

		cooked_font->next_font = kzalloc(sizeof(*cooked_font), GFP_KERNEL);
		if (!cooked_font->next_font)
			return 1;

		cooked_font = cooked_font->next_font;

		cooked_font->raw = raw_font;
	}

	cooked_font->next_font = NULL;
	return 1;
}

#define BMODE_RELOCATE(offset)		offset = (offset) / 4;
#define BMODE_LAST_ADDR_OFFS		0x50

void sti_font_convert_bytemode(struct sti_struct *sti, struct sti_cooked_font *f)
{
	unsigned char *n, *p, *q;
	int size = f->raw->bytes_per_char * (f->raw->last_char + 1) + sizeof(struct sti_rom_font);
	struct sti_rom_font *old_font;

	if (sti->wordmode)
		return;

	old_font = f->raw_ptr;
	n = kcalloc(4, size, STI_LOWMEM);
	f->raw_ptr = n;
	if (!n)
		return;
	p = n + 3;
	q = (unsigned char *) f->raw;
	while (size--) {
		*p = *q++;
		p += 4;
	}
	/* store new ptr to byte-mode font and delete old font */
	f->raw = (struct sti_rom_font *) (n + 3);
	kfree(old_font);
}
EXPORT_SYMBOL(sti_font_convert_bytemode);

static void sti_bmode_rom_copy(unsigned long base, unsigned long count,
			       void *dest)
{
	unsigned long dest_start = (unsigned long) dest;

	while (count) {
		count--;
		*(u8 *)dest = gsc_readl(base);
		base += 4;
		dest++;
	}

	sti_flush(dest_start, (unsigned long)dest);
}

static struct sti_rom *sti_get_bmode_rom (unsigned long address)
{
	struct sti_rom *raw;
	u32 size;
	struct sti_rom_font *raw_font, *font_start;

	sti_bmode_rom_copy(address + BMODE_LAST_ADDR_OFFS, sizeof(size), &size);

	size = (size+3) / 4;
	raw = kmalloc(size, STI_LOWMEM);
	if (raw) {
		sti_bmode_rom_copy(address, size, raw);
		memmove (&raw->res004, &raw->type[0], 0x3c);
		raw->type[3] = raw->res004;

		BMODE_RELOCATE (raw->region_list);
		BMODE_RELOCATE (raw->font_start);

		BMODE_RELOCATE (raw->init_graph);
		BMODE_RELOCATE (raw->state_mgmt);
		BMODE_RELOCATE (raw->font_unpmv);
		BMODE_RELOCATE (raw->block_move);
		BMODE_RELOCATE (raw->inq_conf);

		raw_font = ((void *)raw) + raw->font_start;
		font_start = raw_font;

		while (raw_font->next_font) {
			BMODE_RELOCATE (raw_font->next_font);
			raw_font = ((void *)font_start) + raw_font->next_font;
		}
	}
	return raw;
}

static struct sti_rom *sti_get_wmode_rom(unsigned long address)
{
	struct sti_rom *raw;
	unsigned long size;

	/* read the ROM size directly from the struct in ROM */
	size = gsc_readl(address + offsetof(struct sti_rom,last_addr));

	raw = kmalloc(size, STI_LOWMEM);
	if (raw)
		sti_rom_copy(address, size, raw);

	return raw;
}

static int sti_read_rom(int wordmode, struct sti_struct *sti,
			unsigned long address)
{
	struct sti_cooked_rom *cooked;
	struct sti_rom *raw = NULL;
	unsigned long revno;

	cooked = kmalloc(sizeof *cooked, GFP_KERNEL);
	if (!cooked)
		goto out_err;

	if (wordmode)
		raw = sti_get_wmode_rom (address);
	else
		raw = sti_get_bmode_rom (address);

	if (!raw)
		goto out_err;

	if (!sti_cook_fonts(cooked, raw)) {
		pr_warn("No font found for STI at %08lx\n", address);
		goto out_err;
	}

	if (raw->region_list)
		memcpy(sti->regions, ((void *)raw)+raw->region_list, sizeof(sti->regions));

	address = (unsigned long) STI_PTR(raw);

	pr_info("STI %s ROM supports 32 %sbit firmware functions.\n",
		wordmode ? "word mode" : "byte mode",
		raw->alt_code_type == ALT_CODE_TYPE_PA_RISC_64
		? "and 64 " : "");

	if (IS_ENABLED(CONFIG_64BIT) &&
	    raw->alt_code_type == ALT_CODE_TYPE_PA_RISC_64) {
		sti->do_call64 = 1;
		sti->font_unpmv = address + (raw->font_unp_addr   & 0x03ffffff);
		sti->block_move = address + (raw->block_move_addr & 0x03ffffff);
		sti->init_graph = address + (raw->init_graph_addr & 0x03ffffff);
		sti->inq_conf   = address + (raw->inq_conf_addr   & 0x03ffffff);
	} else {
		sti->font_unpmv = address + (raw->font_unpmv & 0x03ffffff);
		sti->block_move = address + (raw->block_move & 0x03ffffff);
		sti->init_graph = address + (raw->init_graph & 0x03ffffff);
		sti->inq_conf   = address + (raw->inq_conf   & 0x03ffffff);
	}

	sti->rom = cooked;
	sti->rom->raw = raw;
	sti_dump_rom(sti);

	sti->wordmode = wordmode;
	sti->font = sti_select_font(sti->rom);
	sti->font->width = sti->font->raw->width;
	sti->font->height = sti->font->raw->height;
	sti_font_convert_bytemode(sti, sti->font);
	sti_dump_font(sti->font);

	pr_info("    using %d-bit STI ROM functions\n",
		(IS_ENABLED(CONFIG_64BIT) && sti->do_call64) ? 64 : 32);

	sti->sti_mem_request = raw->sti_mem_req;
	pr_debug("    mem_request = %d,  reentsize %d\n",
		 sti->sti_mem_request, raw->reentsize);

	sti->graphics_id[0] = raw->graphics_id[0];
	sti->graphics_id[1] = raw->graphics_id[1];

	/* check if the ROM routines in this card are compatible */
	if (wordmode || sti->graphics_id[1] != 0x09A02587)
		goto ok;

	revno = (raw->revno[0] << 8) | raw->revno[1];

	switch (sti->graphics_id[0]) {
	case S9000_ID_HCRX:
		/* HyperA or HyperB ? */
		if (revno == 0x8408 || revno == 0x840b)
			goto msg_not_supported;
		break;
	case CRT_ID_THUNDER:
		if (revno == 0x8509)
			goto msg_not_supported;
		break;
	case CRT_ID_THUNDER2:
		if (revno == 0x850c)
			goto msg_not_supported;
	}
ok:
	return 1;

msg_not_supported:
	pr_warn("Sorry, this GSC/STI card is not yet supported.\n");
	pr_warn("Please see https://parisc.wiki.kernel.org/"
		"index.php/Graphics_howto for more info.\n");
	/* fall through */
out_err:
	kfree(raw);
	kfree(cooked);
	return 0;
}

static struct sti_struct *sti_try_rom_generic(unsigned long address,
					      unsigned long hpa,
					      struct pci_dev *pd)
{
	struct sti_struct *sti;
	int ok;
	u32 sig;

	if (num_sti_roms >= MAX_STI_ROMS) {
		pr_warn("maximum number of STI ROMS reached !\n");
		return NULL;
	}

	sti = kzalloc(sizeof(*sti), GFP_KERNEL);
	if (!sti)
		return NULL;

	spin_lock_init(&sti->lock);

test_rom:
	/* pdc_add_valid() works only on 32-bit kernels */
	if ((!IS_ENABLED(CONFIG_64BIT) ||
	     (boot_cpu_data.pdc.capabilities & PDC_MODEL_OS32)) &&
	    pdc_add_valid(address)) {
		goto out_err;
	}

	sig = gsc_readl(address);

	/* check for a PCI ROM structure */
	if ((le32_to_cpu(sig)==0xaa55)) {
		unsigned int i, rm_offset;
		u32 *rm;
		i = gsc_readl(address+0x04);
		if (i != 1) {
			/* The ROM could have multiple architecture
			 * dependent images (e.g. i386, parisc,...) */
			pr_warn("PCI ROM is not a STI ROM type image (0x%8x)\n", i);
			goto out_err;
		}

		sti->pd = pd;

		i = gsc_readl(address+0x0c);
		pr_debug("PCI ROM size (from header) = %d kB\n",
			le16_to_cpu(i>>16)*512/1024);
		rm_offset = le16_to_cpu(i & 0xffff);
		if (rm_offset) {
			/* read 16 bytes from the pci region mapper array */
			rm = (u32*) &sti->rm_entry;
			*rm++ = gsc_readl(address+rm_offset+0x00);
			*rm++ = gsc_readl(address+rm_offset+0x04);
			*rm++ = gsc_readl(address+rm_offset+0x08);
			*rm++ = gsc_readl(address+rm_offset+0x0c);
		}

		address += le32_to_cpu(gsc_readl(address+8));
		pr_debug("sig %04x, PCI STI ROM at %08lx\n", sig, address);
		goto test_rom;
	}

	ok = 0;

	if ((sig & 0xff) == 0x01) {
		pr_debug("    byte mode ROM at %08lx, hpa at %08lx\n",
		       address, hpa);
		ok = sti_read_rom(0, sti, address);
	}

	if ((sig & 0xffff) == 0x0303) {
		pr_debug("    word mode ROM at %08lx, hpa at %08lx\n",
		       address, hpa);
		ok = sti_read_rom(1, sti, address);
	}

	if (!ok)
		goto out_err;

	if (sti_init_glob_cfg(sti, address, hpa))
		goto out_err; /* not enough memory */

	/* disable STI PCI ROM. ROM and card RAM overlap and
	 * leaving it enabled would force HPMCs
	 */
	if (sti->pd) {
		unsigned long rom_base;
		rom_base = pci_resource_start(sti->pd, PCI_ROM_RESOURCE);
		pci_write_config_dword(sti->pd, PCI_ROM_ADDRESS, rom_base & ~PCI_ROM_ADDRESS_ENABLE);
		pr_debug("STI PCI ROM disabled\n");
	}

	if (sti_init_graph(sti))
		goto out_err;

	sti_inq_conf(sti);
	sti_dump_globcfg(sti);
	sti_dump_outptr(sti);

	pr_info("    graphics card name: %s\n",
		sti->sti_data->inq_outptr.dev_name);

	sti_roms[num_sti_roms] = sti;
	num_sti_roms++;

	return sti;

out_err:
	kfree(sti);
	return NULL;
}

static void sticore_check_for_default_sti(struct sti_struct *sti, char *path)
{
	pr_info("    located at [%s]\n", sti->pa_path);
	if (strcmp (path, default_sti_path) == 0)
		default_sti = sti;
}

/*
 * on newer systems PDC gives the address of the ROM
 * in the additional address field addr[1] while on
 * older Systems the PDC stores it in page0->proc_sti
 */
static int __init sticore_pa_init(struct parisc_device *dev)
{
	struct sti_struct *sti = NULL;
	int hpa = dev->hpa.start;

	if (dev->num_addrs && dev->addr[0])
		sti = sti_try_rom_generic(dev->addr[0], hpa, NULL);
	if (!sti)
		sti = sti_try_rom_generic(hpa, hpa, NULL);
	if (!sti)
		sti = sti_try_rom_generic(PAGE0->proc_sti, hpa, NULL);
	if (!sti)
		return 1;

	print_pa_hwpath(dev, sti->pa_path);
	sticore_check_for_default_sti(sti, sti->pa_path);

	sti->dev = &dev->dev;

	return 0;
}


static int sticore_pci_init(struct pci_dev *pd, const struct pci_device_id *ent)
{
#ifdef CONFIG_PCI
	unsigned long fb_base, rom_base;
	unsigned int fb_len, rom_len;
	int err;
	struct sti_struct *sti;

	err = pci_enable_device(pd);
	if (err < 0) {
		dev_err(&pd->dev, "Cannot enable PCI device\n");
		return err;
	}

	fb_base = pci_resource_start(pd, 0);
	fb_len = pci_resource_len(pd, 0);
	rom_base = pci_resource_start(pd, PCI_ROM_RESOURCE);
	rom_len = pci_resource_len(pd, PCI_ROM_RESOURCE);
	if (rom_base) {
		pci_write_config_dword(pd, PCI_ROM_ADDRESS, rom_base | PCI_ROM_ADDRESS_ENABLE);
		pr_debug("STI PCI ROM enabled at 0x%08lx\n", rom_base);
	}

	pr_info("STI PCI graphic ROM found at %08lx (%u kB), fb at %08lx (%u MB)\n",
		rom_base, rom_len/1024, fb_base, fb_len/1024/1024);

	pr_debug("Trying PCI STI ROM at %08lx, PCI hpa at %08lx\n",
		    rom_base, fb_base);

	sti = sti_try_rom_generic(rom_base, fb_base, pd);
	if (sti) {
		print_pci_hwpath(pd, sti->pa_path);
		sticore_check_for_default_sti(sti, sti->pa_path);
	}

	if (!sti) {
		pr_warn("Unable to handle STI device '%s'\n", pci_name(pd));
		return -ENODEV;
	}

	sti->dev = &pd->dev;
#endif /* CONFIG_PCI */

	return 0;
}


static void __exit sticore_pci_remove(struct pci_dev *pd)
{
	BUG();
}


static struct pci_device_id sti_pci_tbl[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_HP, PCI_DEVICE_ID_HP_VISUALIZE_EG) },
	{ PCI_DEVICE(PCI_VENDOR_ID_HP, PCI_DEVICE_ID_HP_VISUALIZE_FX6) },
	{ PCI_DEVICE(PCI_VENDOR_ID_HP, PCI_DEVICE_ID_HP_VISUALIZE_FX4) },
	{ PCI_DEVICE(PCI_VENDOR_ID_HP, PCI_DEVICE_ID_HP_VISUALIZE_FX2) },
	{ PCI_DEVICE(PCI_VENDOR_ID_HP, PCI_DEVICE_ID_HP_VISUALIZE_FXE) },
	{ 0, } /* terminate list */
};
MODULE_DEVICE_TABLE(pci, sti_pci_tbl);

static struct pci_driver pci_sti_driver = {
	.name		= "sti",
	.id_table	= sti_pci_tbl,
	.probe		= sticore_pci_init,
	.remove		= __exit_p(sticore_pci_remove),
};

static struct parisc_device_id sti_pa_tbl[] = {
	{ HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x00077 },
	{ HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x00085 },
	{ 0, }
};
MODULE_DEVICE_TABLE(parisc, sti_pa_tbl);

static struct parisc_driver pa_sti_driver __refdata = {
	.name		= "sti",
	.id_table	= sti_pa_tbl,
	.probe		= sticore_pa_init,
};


/*
 * sti_init_roms() - detects all STI ROMs and stores them in sti_roms[]
 */

static int sticore_initialized __read_mostly;

static void sti_init_roms(void)
{
	if (sticore_initialized)
		return;

	sticore_initialized = 1;

	pr_info("STI GSC/PCI core graphics driver "
			STI_DRIVERVERSION "\n");

	/* Register drivers for native & PCI cards */
	register_parisc_driver(&pa_sti_driver);
	WARN_ON(pci_register_driver(&pci_sti_driver));

	/* if we didn't find the given default sti, take the first one */
	if (!default_sti)
		default_sti = sti_roms[0];

}

/*
 * index = 0 gives default sti
 * index > 0 gives other stis in detection order
 */
struct sti_struct * sti_get_rom(unsigned int index)
{
	if (!sticore_initialized)
		sti_init_roms();

	if (index == 0)
		return default_sti;

	if (index > num_sti_roms)
		return NULL;

	return sti_roms[index-1];
}
EXPORT_SYMBOL(sti_get_rom);


int sti_call(const struct sti_struct *sti, unsigned long func,
		const void *flags, void *inptr, void *outptr,
		struct sti_glob_cfg *glob_cfg)
{
	unsigned long _flags = STI_PTR(flags);
	unsigned long _inptr = STI_PTR(inptr);
	unsigned long _outptr = STI_PTR(outptr);
	unsigned long _glob_cfg = STI_PTR(glob_cfg);
	int ret;

	/* Check for overflow when using 32bit STI on 64bit kernel. */
	if (WARN_ONCE(IS_ENABLED(CONFIG_64BIT) && !sti->do_call64 &&
		      (upper_32_bits(_flags) || upper_32_bits(_inptr) ||
		      upper_32_bits(_outptr) || upper_32_bits(_glob_cfg)),
			"Out of 32bit-range pointers!"))
		return -1;

	ret = pdc_sti_call(func, _flags, _inptr, _outptr, _glob_cfg,
			   sti->do_call64);

	return ret;
}

MODULE_AUTHOR("Philipp Rumpf, Helge Deller, Thomas Bogendoerfer");
MODULE_DESCRIPTION("Core STI driver for HP's NGLE series graphics cards in HP PARISC machines");
MODULE_LICENSE("GPL v2");

