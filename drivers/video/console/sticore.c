/*
 *  linux/drivers/video/console/sticore.c -
 *	core code for console driver using HP's STI firmware
 *
 *	Copyright (C) 2000 Philipp Rumpf <prumpf@tux.org>
 *	Copyright (C) 2001-2003 Helge Deller <deller@gmx.de>
 *	Copyright (C) 2001-2002 Thomas Bogendoerfer <tsbogend@alpha.franken.de>
 * 
 * TODO:
 * - call STI in virtual mode rather than in real mode
 * - screen blanking with state_mgmt() in text mode STI ? 
 * - try to make it work on m68k hp workstations ;)
 * 
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/font.h>

#include <asm/hardware.h>
#include <asm/parisc-device.h>
#include <asm/cacheflush.h>

#include "../sticore.h"

#define STI_DRIVERVERSION "Version 0.9a"

struct sti_struct *default_sti;

static int num_sti_roms;			  /* # of STI ROMS found */
static struct sti_struct *sti_roms[MAX_STI_ROMS]; /* ptr to each sti_struct */


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

int
sti_init_graph(struct sti_struct *sti) 
{
	struct sti_init_inptr_ext inptr_ext = { 0, };
	struct sti_init_inptr inptr = {
		.text_planes	= 3, /* # of text planes (max 3 for STI) */
		.ext_ptr	= STI_PTR(&inptr_ext)
	};
	struct sti_init_outptr outptr = { 0, };
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&sti->lock, flags);

	ret = STI_CALL(sti->init_graph, &default_init_flags, &inptr,
		&outptr, sti->glob_cfg);

	spin_unlock_irqrestore(&sti->lock, flags);

	if (ret < 0) {
		printk(KERN_ERR "STI init_graph failed (ret %d, errno %d)\n",ret,outptr.errno);
		return -1;
	}
	
	sti->text_planes = outptr.text_planes;
	return 0;
}

static const struct sti_conf_flags default_conf_flags = {
	.wait	= STI_WAIT,
};

void
sti_inq_conf(struct sti_struct *sti)
{
	struct sti_conf_inptr inptr = { 0, };
	unsigned long flags;
	s32 ret;

	sti->outptr.ext_ptr = STI_PTR(&sti->outptr_ext);
	
	do {
		spin_lock_irqsave(&sti->lock, flags);
		ret = STI_CALL(sti->inq_conf, &default_conf_flags,
			&inptr, &sti->outptr, sti->glob_cfg);
		spin_unlock_irqrestore(&sti->lock, flags);
	} while (ret == 1);
}

static const struct sti_font_flags default_font_flags = {
	.wait		= STI_WAIT,
	.non_text	= 0,
};

void
sti_putc(struct sti_struct *sti, int c, int y, int x)
{
	struct sti_font_inptr inptr = {
		.font_start_addr= STI_PTR(sti->font->raw),
		.index		= c_index(sti, c),
		.fg_color	= c_fg(sti, c),
		.bg_color	= c_bg(sti, c),
		.dest_x		= x * sti->font_width,
		.dest_y		= y * sti->font_height,
	};
	struct sti_font_outptr outptr = { 0, };
	s32 ret;
	unsigned long flags;

	do {
		spin_lock_irqsave(&sti->lock, flags);
		ret = STI_CALL(sti->font_unpmv, &default_font_flags,
			&inptr, &outptr, sti->glob_cfg);
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
	struct sti_blkmv_inptr inptr = {
		.fg_color	= color,
		.bg_color	= color,
		.src_x		= src_x,
		.src_y		= src_y,
		.dest_x		= src_x,
		.dest_y		= src_y,
		.width		= width,
		.height		= height,
	};
	struct sti_blkmv_outptr outptr = { 0, };
	s32 ret;
	unsigned long flags;
	
	do {
		spin_lock_irqsave(&sti->lock, flags);
		ret = STI_CALL(sti->block_move, &clear_blkmv_flags,
			&inptr, &outptr, sti->glob_cfg);
		spin_unlock_irqrestore(&sti->lock, flags);
	} while (ret == 1);
}

void
sti_clear(struct sti_struct *sti, int src_y, int src_x,
	  int height, int width, int c)
{
	struct sti_blkmv_inptr inptr = {
		.fg_color	= c_fg(sti, c),
		.bg_color	= c_bg(sti, c),
		.src_x		= src_x * sti->font_width,
		.src_y		= src_y * sti->font_height,
		.dest_x		= src_x * sti->font_width,
		.dest_y		= src_y * sti->font_height,
		.width		= width * sti->font_width,
		.height		= height* sti->font_height,
	};
	struct sti_blkmv_outptr outptr = { 0, };
	s32 ret;
	unsigned long flags;

	do {
		spin_lock_irqsave(&sti->lock, flags);
		ret = STI_CALL(sti->block_move, &clear_blkmv_flags,
			&inptr, &outptr, sti->glob_cfg);
		spin_unlock_irqrestore(&sti->lock, flags);
	} while (ret == 1);
}

static const struct sti_blkmv_flags default_blkmv_flags = {
	.wait = STI_WAIT, 
};

void
sti_bmove(struct sti_struct *sti, int src_y, int src_x,
	  int dst_y, int dst_x, int height, int width)
{
	struct sti_blkmv_inptr inptr = {
		.src_x		= src_x * sti->font_width,
		.src_y		= src_y * sti->font_height,
		.dest_x		= dst_x * sti->font_width,
		.dest_y		= dst_y * sti->font_height,
		.width		= width * sti->font_width,
		.height		= height* sti->font_height,
	};
	struct sti_blkmv_outptr outptr = { 0, };
	s32 ret;
	unsigned long flags;

	do {
		spin_lock_irqsave(&sti->lock, flags);
		ret = STI_CALL(sti->block_move, &default_blkmv_flags,
			&inptr, &outptr, sti->glob_cfg);
		spin_unlock_irqrestore(&sti->lock, flags);
	} while (ret == 1);
}


/* FIXME: Do we have another solution for this ? */
static void sti_flush(unsigned long from, unsigned long len)
{
	flush_data_cache();
	flush_kernel_dcache_range(from, len);
	flush_icache_range(from, from+len);
}

void __init
sti_rom_copy(unsigned long base, unsigned long count, void *dest)
{
	unsigned long dest_len = count;
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

	sti_flush(dest_start, dest_len);
}




static char default_sti_path[21];

#ifndef MODULE
static int __init sti_setup(char *str)
{
	if (str)
		strlcpy (default_sti_path, str, sizeof (default_sti_path));
	
	return 0;
}

/*	Assuming the machine has multiple STI consoles (=graphic cards) which
 *	all get detected by sticon, the user may define with the linux kernel
 *	parameter sti=<x> which of them will be the initial boot-console.
 *	<x> is a number between 0 and MAX_STI_ROMS, with 0 as the default 
 *	STI screen.
 */
__setup("sti=", sti_setup);
#endif



static char __initdata	*font_name[MAX_STI_ROMS] = { "VGA8x16", };
static int __initdata	font_index[MAX_STI_ROMS], 
			font_height[MAX_STI_ROMS],
			font_width[MAX_STI_ROMS];
#ifndef MODULE
static int __init sti_font_setup(char *str)
{
	char *x;
	int i = 0;

	/* we accept sti_font=VGA8x16, sti_font=10x20, sti_font=10*20 
	 * or sti_font=7 style command lines. */

	while (i<MAX_STI_ROMS && str && *str) {
		if (*str>='0' && *str<='9') {
			if ((x = strchr(str, 'x')) || (x = strchr(str, '*'))) {
				font_height[i] = simple_strtoul(str, NULL, 0);
				font_width[i] = simple_strtoul(x+1, NULL, 0);
			} else {
				font_index[i] = simple_strtoul(str, NULL, 0);
			}
		} else {
			font_name[i] = str;	/* fb font name */
		}

		if ((x = strchr(str, ',')))
			*x++ = 0;
		str = x;

		i++;
	}

	return 0;
}

/*	The optional linux kernel parameter "sti_font" defines which font
 *	should be used by the sticon driver to draw characters to the screen.
 *	Possible values are:
 *	- sti_font=<fb_fontname>:
 *		<fb_fontname> is the name of one of the linux-kernel built-in 
 *		framebuffer font names (e.g. VGA8x16, SUN22x18). 
 *		This is only available if the fonts have been statically compiled 
 *		in with e.g. the CONFIG_FONT_8x16 or CONFIG_FONT_SUN12x22 options.
 *	- sti_font=<number>
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


	
static void __init
sti_dump_globcfg(struct sti_glob_cfg *glob_cfg, unsigned int sti_mem_request)
{
	struct sti_glob_cfg_ext *cfg;
	
	DPRINTK((KERN_INFO
		"%d text planes\n"
		"%4d x %4d screen resolution\n"
		"%4d x %4d offscreen\n"
		"%4d x %4d layout\n"
		"regions at %08x %08x %08x %08x\n"
		"regions at %08x %08x %08x %08x\n"
		"reent_lvl %d\n"
		"save_addr %08x\n",
		glob_cfg->text_planes,
		glob_cfg->onscreen_x, glob_cfg->onscreen_y,
		glob_cfg->offscreen_x, glob_cfg->offscreen_y,
		glob_cfg->total_x, glob_cfg->total_y,
		glob_cfg->region_ptrs[0], glob_cfg->region_ptrs[1],
		glob_cfg->region_ptrs[2], glob_cfg->region_ptrs[3],
		glob_cfg->region_ptrs[4], glob_cfg->region_ptrs[5],
		glob_cfg->region_ptrs[6], glob_cfg->region_ptrs[7],
		glob_cfg->reent_lvl,
		glob_cfg->save_addr));

	/* dump extended cfg */ 
	cfg = PTR_STI(glob_cfg->ext_ptr);
	DPRINTK(( KERN_INFO
		"monitor %d\n"
		"in friendly mode: %d\n"
		"power consumption %d watts\n"
		"freq ref %d\n"
		"sti_mem_addr %08x (size=%d bytes)\n",
		cfg->curr_mon,
		cfg->friendly_boot,
		cfg->power,
		cfg->freq_ref,
		cfg->sti_mem_addr, sti_mem_request));
}

static void __init
sti_dump_outptr(struct sti_struct *sti)
{
	DPRINTK((KERN_INFO
		"%d bits per pixel\n"
		"%d used bits\n"
		"%d planes\n"
		"attributes %08x\n",
		 sti->outptr.bits_per_pixel,
		 sti->outptr.bits_used,
		 sti->outptr.planes,
		 sti->outptr.attributes));
}

static int __init
sti_init_glob_cfg(struct sti_struct *sti,
	    unsigned long rom_address, unsigned long hpa)
{
	struct sti_glob_cfg *glob_cfg;
	struct sti_glob_cfg_ext *glob_cfg_ext;
	void *save_addr;
	void *sti_mem_addr;
	const int save_addr_size = 1024;	/* XXX */
	int i;

	if (!sti->sti_mem_request)
		sti->sti_mem_request = 256; /* STI default */

	glob_cfg = kmalloc(sizeof(*sti->glob_cfg), GFP_KERNEL);
	glob_cfg_ext = kmalloc(sizeof(*glob_cfg_ext), GFP_KERNEL);
	save_addr = kmalloc(save_addr_size, GFP_KERNEL);
	sti_mem_addr = kmalloc(sti->sti_mem_request, GFP_KERNEL);

	if (!(glob_cfg && glob_cfg_ext && save_addr && sti_mem_addr)) {
		kfree(glob_cfg);
		kfree(glob_cfg_ext);
		kfree(save_addr);
		kfree(sti_mem_addr);
		return -ENOMEM;
	}

	memset(glob_cfg, 0, sizeof(*glob_cfg));
	memset(glob_cfg_ext, 0, sizeof(*glob_cfg_ext));
	memset(save_addr, 0, save_addr_size);
	memset(sti_mem_addr, 0, sti->sti_mem_request);

	glob_cfg->ext_ptr = STI_PTR(glob_cfg_ext);
	glob_cfg->save_addr = STI_PTR(save_addr);
	for (i=0; i<8; i++) {
		unsigned long newhpa, len;
	       
		if (sti->pd) {
			unsigned char offs = sti->rm_entry[i];
				
			if (offs == 0)
				continue;
			if (offs != PCI_ROM_ADDRESS &&
			    (offs < PCI_BASE_ADDRESS_0 ||
			     offs > PCI_BASE_ADDRESS_5)) {
				printk (KERN_WARNING
					"STI pci region maping for region %d (%02x) can't be mapped\n",
					i,sti->rm_entry[i]);
				continue;
			}
			newhpa = pci_resource_start (sti->pd, (offs - PCI_BASE_ADDRESS_0) / 4);
		} else
			newhpa = (i == 0) ? rom_address : hpa;

		sti->regions_phys[i] =
			REGION_OFFSET_TO_PHYS(sti->regions[i], newhpa);
		
		/* remap virtually */
		/* FIXME: add BTLB support if btlb==1 */
		len = sti->regions[i].region_desc.length * 4096;

/* XXX: Enabling IOREMAP debugging causes a crash, so we must be passing
 * a virtual address to something expecting a physical address that doesn't
 * go through a readX macro */
#if 0
		if (len)
		   glob_cfg->region_ptrs[i] = (unsigned long) (
			sti->regions[i].region_desc.cache ?
			ioremap(sti->regions_phys[i], len) :
			ioremap_nocache(sti->regions_phys[i], len) );
#else
		if (len)
			glob_cfg->region_ptrs[i] = sti->regions_phys[i];
#endif
		
		DPRINTK(("region #%d: phys %08lx, virt %08x, len=%lukB, "
			 "btlb=%d, sysonly=%d, cache=%d, last=%d\n",
			i, sti->regions_phys[i], glob_cfg->region_ptrs[i],
			len/1024,
			sti->regions[i].region_desc.btlb,
			sti->regions[i].region_desc.sys_only,
			sti->regions[i].region_desc.cache, 
			sti->regions[i].region_desc.last));

		/* last entry reached ? */
		if (sti->regions[i].region_desc.last)
			break;
	}

	if (++i<8 && sti->regions[i].region)
		printk(KERN_WARNING "%s: *future ptr (0x%8x) not yet supported !\n",
				__FILE__, sti->regions[i].region);

	glob_cfg_ext->sti_mem_addr = STI_PTR(sti_mem_addr);

	sti->glob_cfg = glob_cfg;
	
	return 0;
}

#ifdef CONFIG_FB
struct sti_cooked_font * __init
sti_select_fbfont( struct sti_cooked_rom *cooked_rom, char *fbfont_name )
{
	struct font_desc *fbfont;
	unsigned int size, bpc;
	void *dest;
	struct sti_rom_font *nf;
	struct sti_cooked_font *cooked_font;
	
	if (!fbfont_name || !strlen(fbfont_name))
	    return NULL;
	fbfont = find_font(fbfont_name);
	if (!fbfont)
	    fbfont = get_default_font(1024,768);
	if (!fbfont)
	    return NULL;

	DPRINTK((KERN_DEBUG "selected %dx%d fb-font %s\n",
			fbfont->width, fbfont->height, fbfont->name));
			
	bpc = ((fbfont->width+7)/8) * fbfont->height; 
	size = bpc * 256;
	size += sizeof(struct sti_rom_font);

	nf = kmalloc(size, GFP_KERNEL);
	if (!nf)
	    return NULL;
	memset(nf, 0, size);

	nf->first_char = 0;
	nf->last_char = 255;
	nf->width = fbfont->width;
	nf->height = fbfont->height;
	nf->font_type = STI_FONT_HPROMAN8;
	nf->bytes_per_char = bpc;
	nf->next_font = 0;
	nf->underline_height = 1;
	nf->underline_pos = fbfont->height - nf->underline_height;

	dest = nf;
	dest += sizeof(struct sti_rom_font);
	memcpy(dest, fbfont->data, bpc*256);

	cooked_font = kmalloc(sizeof(*cooked_font), GFP_KERNEL);
	if (!cooked_font) {
	    kfree(nf);
	    return NULL;
	}
	
	cooked_font->raw = nf;
	cooked_font->next_font = NULL;

	cooked_rom->font_start = cooked_font;

	return cooked_font;
}
#else
struct sti_cooked_font * __init
sti_select_fbfont(struct sti_cooked_rom *cooked_rom, char *fbfont_name)
{
	return NULL;
}
#endif

struct sti_cooked_font * __init
sti_select_font(struct sti_cooked_rom *rom,
	    int (*search_font_fnc) (struct sti_cooked_rom *,int,int) )
{
	struct sti_cooked_font *font;
	int i;
	int index = num_sti_roms;

	/* check for framebuffer-font first */
	if ((font = sti_select_fbfont(rom, font_name[index])))
		return font;

	if (font_width[index] && font_height[index])
		font_index[index] = search_font_fnc(rom, 
				font_height[index], font_width[index]);

	for (font = rom->font_start, i = font_index[index];
	    font && (i > 0);
	    font = font->next_font, i--);

	if (font)
		return font;
	else
		return rom->font_start;
}


static void __init 
sti_dump_rom(struct sti_rom *rom)
{
        printk(KERN_INFO "    id %04x-%04x, conforms to spec rev. %d.%02x\n",
		rom->graphics_id[0], 
		rom->graphics_id[1],
		rom->revno[0] >> 4, 
		rom->revno[0] & 0x0f);
	DPRINTK(("      supports %d monitors\n", rom->num_mons));
	DPRINTK(("      font start %08x\n", rom->font_start));
	DPRINTK(("      region list %08x\n", rom->region_list));
	DPRINTK(("      init_graph %08x\n", rom->init_graph));
	DPRINTK(("      bus support %02x\n", rom->bus_support));
	DPRINTK(("      ext bus support %02x\n", rom->ext_bus_support));
	DPRINTK(("      alternate code type %d\n", rom->alt_code_type));
}


static int __init 
sti_cook_fonts(struct sti_cooked_rom *cooked_rom,
			struct sti_rom *raw_rom)
{
	struct sti_rom_font *raw_font, *font_start;
	struct sti_cooked_font *cooked_font;
	
	cooked_font = kmalloc(sizeof(*cooked_font), GFP_KERNEL);
	if (!cooked_font)
		return 0;

	cooked_rom->font_start = cooked_font;

	raw_font = ((void *)raw_rom) + (raw_rom->font_start);

	font_start = raw_font;
	cooked_font->raw = raw_font;

	while (raw_font->next_font) {
		raw_font = ((void *)font_start) + (raw_font->next_font);

		cooked_font->next_font = kmalloc(sizeof(*cooked_font), GFP_KERNEL);
		if (!cooked_font->next_font)
			return 1;

		cooked_font = cooked_font->next_font;

		cooked_font->raw = raw_font;
	}

	cooked_font->next_font = NULL;
	return 1;
}


static int __init 
sti_search_font(struct sti_cooked_rom *rom, int height, int width)
{
	struct sti_cooked_font *font;
	int i = 0;
	
	for(font = rom->font_start; font; font = font->next_font, i++) {
	    if((font->raw->width == width) && (font->raw->height == height))
			return i;
	}
	return 0;
}

#define BMODE_RELOCATE(offset)        offset = (offset) / 4;
#define BMODE_LAST_ADDR_OFFS          0x50

static void * __init
sti_bmode_font_raw(struct sti_cooked_font *f)
{
	unsigned char *n, *p, *q;
	int size = f->raw->bytes_per_char*256+sizeof(struct sti_rom_font);
	
	n = kmalloc (4*size, GFP_KERNEL);
	if (!n)
		return NULL;
	memset (n, 0, 4*size);
	p = n + 3;
	q = (unsigned char *)f->raw;
	while (size--) {
		*p = *q++;
		p+=4;
	}
	return n + 3;
}

static void __init
sti_bmode_rom_copy(unsigned long base, unsigned long count, void *dest)
{
	unsigned long dest_len = count;
	unsigned long dest_start = (unsigned long) dest;

	while (count) {
		count--;
		*(u8 *)dest = gsc_readl(base);
		base += 4;
		dest++;
	}
	sti_flush(dest_start, dest_len);
}

static struct sti_rom * __init
sti_get_bmode_rom (unsigned long address)
{
	struct sti_rom *raw;
	u32 size;
        struct sti_rom_font *raw_font, *font_start;
    
	sti_bmode_rom_copy(address + BMODE_LAST_ADDR_OFFS, sizeof(size), &size);
    
        size = (size+3) / 4;
	raw = kmalloc(size, GFP_KERNEL);
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

struct sti_rom * __init
sti_get_wmode_rom (unsigned long address)
{
	struct sti_rom *raw;
	unsigned long size;
    
	/* read the ROM size directly from the struct in ROM */ 
	size = gsc_readl(address + offsetof(struct sti_rom,last_addr));

	raw = kmalloc(size, GFP_KERNEL);
	if(raw)
	        sti_rom_copy(address, size, raw);

        return raw;
}

int __init
sti_read_rom(int wordmode, struct sti_struct *sti, unsigned long address)
{
	struct sti_cooked_rom *cooked;
	struct sti_rom *raw = NULL;

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
		printk(KERN_ERR "No font found for STI at %08lx\n", address);
		goto out_err;
	}

	if (raw->region_list)
		memcpy(sti->regions, ((void *)raw)+raw->region_list, sizeof(sti->regions));

	address = (unsigned long) STI_PTR(raw);

	sti->font_unpmv = address + (raw->font_unpmv & 0x03ffffff);
	sti->block_move = address + (raw->block_move & 0x03ffffff);
	sti->init_graph = address + (raw->init_graph & 0x03ffffff);
	sti->inq_conf   = address + (raw->inq_conf   & 0x03ffffff);

	sti->rom = cooked;
	sti->rom->raw = raw;
	
	sti->font = sti_select_font(sti->rom, sti_search_font);
	sti->font_width = sti->font->raw->width;
	sti->font_height = sti->font->raw->height;
	if (!wordmode)
	        sti->font->raw = sti_bmode_font_raw(sti->font);

	sti->sti_mem_request = raw->sti_mem_req;
	sti->graphics_id[0] = raw->graphics_id[0];
	sti->graphics_id[1] = raw->graphics_id[1];
	
	sti_dump_rom(raw);
	
	return 1;

out_err:
	kfree(raw);
	kfree(cooked);
	return 0;
}

static struct sti_struct * __init
sti_try_rom_generic(unsigned long address, unsigned long hpa, struct pci_dev *pd)
{
	struct sti_struct *sti;
	int ok;
	u32 sig;

	if (num_sti_roms >= MAX_STI_ROMS) {
	    printk(KERN_WARNING "maximum number of STI ROMS reached !\n");
	    return NULL;
	}
	
	sti = kmalloc(sizeof(*sti), GFP_KERNEL);
	if (!sti) {
	    printk(KERN_ERR "Not enough memory !\n");
	    return NULL;
	}
		    
	memset(sti, 0, sizeof(*sti));
	spin_lock_init(&sti->lock);

test_rom:
	/* if we can't read the ROM, bail out early.  Not being able
	 * to read the hpa is okay, for romless sti */
	if (pdc_add_valid(address))
		goto out_err;

	sig = gsc_readl(address);

	/* check for a PCI ROM structure */
	if ((le32_to_cpu(sig)==0xaa55)) {
		unsigned int i, rm_offset;
		u32 *rm;
		i = gsc_readl(address+0x04);
		if (i != 1) {
			/* The ROM could have multiple architecture 
			 * dependent images (e.g. i386, parisc,...) */
			printk(KERN_WARNING 
				"PCI ROM is not a STI ROM type image (0x%8x)\n", i);
			goto out_err;
		}
		
		sti->pd = pd;

		i = gsc_readl(address+0x0c);
		DPRINTK(("PCI ROM size (from header) = %d kB\n",
			le16_to_cpu(i>>16)*512/1024));
		rm_offset = le16_to_cpu(i & 0xffff);
		if (rm_offset) { 
			/* read 16 bytes from the pci region mapper array */
			rm = (u32*) &sti->rm_entry;
			*rm++ = gsc_readl(address+rm_offset+0x00);
			*rm++ = gsc_readl(address+rm_offset+0x04);
			*rm++ = gsc_readl(address+rm_offset+0x08);
			*rm++ = gsc_readl(address+rm_offset+0x0c);
			DPRINTK(("PCI region Mapper offset = %08x: ",
				rm_offset));
			for (i=0; i<16; i++)
				DPRINTK(("%02x ", sti->rm_entry[i]));
			DPRINTK(("\n"));
		}

		address += le32_to_cpu(gsc_readl(address+8));
		DPRINTK(("sig %04x, PCI STI ROM at %08lx\n", sig, address));
		goto test_rom;
	}
	
	ok = 0;
	
	if ((sig & 0xff) == 0x01) {
		DPRINTK(("    byte mode ROM at %08lx, hpa at %08lx\n",
		       address, hpa));
		ok = sti_read_rom(0, sti, address);
	}

	if ((sig & 0xffff) == 0x0303) {
		DPRINTK(("    word mode ROM at %08lx, hpa at %08lx\n",
		       address, hpa));
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
		DPRINTK((KERN_DEBUG "STI PCI ROM disabled\n"));
	}

	if (sti_init_graph(sti))
		goto out_err;

	sti_inq_conf(sti);
	sti_dump_globcfg(sti->glob_cfg, sti->sti_mem_request);
	sti_dump_outptr(sti);
	
	printk(KERN_INFO "    graphics card name: %s\n", sti->outptr.dev_name );

	sti_roms[num_sti_roms] = sti;
	num_sti_roms++;
	
	return sti;

out_err:
	kfree(sti);
	return NULL;
}

static void __init sticore_check_for_default_sti(struct sti_struct *sti, char *path)
{
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
	unsigned long rom = 0;
	char pa_path[21];
	struct sti_struct *sti = NULL;
	
	if(dev->num_addrs) {
		rom = dev->addr[0];
	}
	if (!rom) {
		rom = dev->hpa;
		DPRINTK((KERN_DEBUG "Trying STI ROM at %08lx, hpa at %08lx\n", rom, dev->hpa));
		sti = sti_try_rom_generic(rom, dev->hpa, NULL);
		rom = PAGE0->proc_sti;
	}
	if (!sti) {
		DPRINTK((KERN_DEBUG "Trying STI ROM at %08lx, hpa at %08lx\n", rom, dev->hpa));
		sti = sti_try_rom_generic(rom, dev->hpa, NULL);
	}
	if (!sti)
		return 1;
	
	print_pa_hwpath(dev, pa_path);
	sticore_check_for_default_sti (sti, pa_path);
	return 0;
}


static int __devinit sticore_pci_init(struct pci_dev *pd,
		const struct pci_device_id *ent)
{
#ifdef CONFIG_PCI
	unsigned long fb_base, rom_base;
	unsigned int fb_len, rom_len;
	struct sti_struct *sti;
	
	pci_enable_device(pd);

	fb_base = pci_resource_start(pd, 0);
	fb_len = pci_resource_len(pd, 0);
	rom_base = pci_resource_start(pd, PCI_ROM_RESOURCE);
	rom_len = pci_resource_len(pd, PCI_ROM_RESOURCE);
	if (rom_base) {
		pci_write_config_dword(pd, PCI_ROM_ADDRESS, rom_base | PCI_ROM_ADDRESS_ENABLE);
		DPRINTK((KERN_DEBUG "STI PCI ROM enabled at 0x%08lx\n", rom_base));
	}

	printk(KERN_INFO "STI PCI graphic ROM found at %08lx (%u kB), fb at %08lx (%u MB)\n",
		rom_base, rom_len/1024, fb_base, fb_len/1024/1024);

	DPRINTK((KERN_DEBUG "Trying PCI STI ROM at %08lx, PCI hpa at %08lx\n",
		    rom_base, fb_base));

	sti = sti_try_rom_generic(rom_base, fb_base, pd);
	if (sti) {
		char pa_path[30];
		print_pci_hwpath(pd, pa_path);
		sticore_check_for_default_sti(sti, pa_path);
	}
	
	if (!sti) {
		printk(KERN_WARNING "Unable to handle STI device '%s'\n",
			pci_name(pd));
		return -ENODEV;
	}
#endif /* CONFIG_PCI */

	return 0;
}


static void __devexit sticore_pci_remove(struct pci_dev *pd)
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
	.remove		= sticore_pci_remove,
};

static struct parisc_device_id sti_pa_tbl[] = {
	{ HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x00077 },
	{ HPHW_FIO, HVERSION_REV_ANY_ID, HVERSION_ANY_ID, 0x00085 },
	{ 0, }
};

static struct parisc_driver pa_sti_driver = {
	.name		= "sti",
	.id_table	= sti_pa_tbl,
	.probe		= sticore_pa_init,
};


/*
 * sti_init_roms() - detects all STI ROMs and stores them in sti_roms[]
 */

static int sticore_initialized;

static void __init sti_init_roms(void)
{
	if (sticore_initialized)
		return;

	sticore_initialized = 1;

	printk(KERN_INFO "STI GSC/PCI core graphics driver "
			STI_DRIVERVERSION "\n");

	/* Register drivers for native & PCI cards */
	register_parisc_driver(&pa_sti_driver);
	pci_register_driver(&pci_sti_driver);

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

MODULE_AUTHOR("Philipp Rumpf, Helge Deller, Thomas Bogendoerfer");
MODULE_DESCRIPTION("Core STI driver for HP's NGLE series graphics cards in HP PARISC machines");
MODULE_LICENSE("GPL v2");

