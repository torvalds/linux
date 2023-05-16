// SPDX-License-Identifier: GPL-2.0
/*
 *	Macintosh Nubus Interface Code
 *
 *      Originally by Alan Cox
 *
 *      Mostly rewritten by David Huggins-Daines, C. Scott Ananian,
 *      and others.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/nubus.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/hwtest.h>

/* Constants */

/* This is, of course, the size in bytelanes, rather than the size in
   actual bytes */
#define FORMAT_BLOCK_SIZE 20
#define ROM_DIR_OFFSET 0x24

#define NUBUS_TEST_PATTERN 0x5A932BC7

/* Globals */

/* The "nubus.populate_procfs" parameter makes slot resources available in
 * procfs. It's deprecated and disabled by default because procfs is no longer
 * thought to be suitable for that and some board ROMs make it too expensive.
 */
bool nubus_populate_procfs;
module_param_named(populate_procfs, nubus_populate_procfs, bool, 0);

LIST_HEAD(nubus_func_rsrcs);

/* Meaning of "bytelanes":

   The card ROM may appear on any or all bytes of each long word in
   NuBus memory.  The low 4 bits of the "map" value found in the
   format block (at the top of the slot address space, as well as at
   the top of the MacOS ROM) tells us which bytelanes, i.e. which byte
   offsets within each longword, are valid.  Thus:

   A map of 0x0f, as found in the MacOS ROM, means that all bytelanes
   are valid.

   A map of 0xf0 means that no bytelanes are valid (We pray that we
   will never encounter this, but stranger things have happened)

   A map of 0xe1 means that only the MSB of each long word is actually
   part of the card ROM.  (We hope to never encounter NuBus on a
   little-endian machine.  Again, stranger things have happened)

   A map of 0x78 means that only the LSB of each long word is valid.

   Etcetera, etcetera.  Hopefully this clears up some confusion over
   what the following code actually does.  */

static inline int not_useful(void *p, int map)
{
	unsigned long pv = (unsigned long)p;

	pv &= 3;
	if (map & (1 << pv))
		return 0;
	return 1;
}

static unsigned long nubus_get_rom(unsigned char **ptr, int len, int map)
{
	/* This will hold the result */
	unsigned long v = 0;
	unsigned char *p = *ptr;

	while (len) {
		v <<= 8;
		while (not_useful(p, map))
			p++;
		v |= *p++;
		len--;
	}
	*ptr = p;
	return v;
}

static void nubus_rewind(unsigned char **ptr, int len, int map)
{
	unsigned char *p = *ptr;

	while (len) {
		do {
			p--;
		} while (not_useful(p, map));
		len--;
	}
	*ptr = p;
}

static void nubus_advance(unsigned char **ptr, int len, int map)
{
	unsigned char *p = *ptr;

	while (len) {
		while (not_useful(p, map))
			p++;
		p++;
		len--;
	}
	*ptr = p;
}

static void nubus_move(unsigned char **ptr, int len, int map)
{
	unsigned long slot_space = (unsigned long)*ptr & 0xFF000000;

	if (len > 0)
		nubus_advance(ptr, len, map);
	else if (len < 0)
		nubus_rewind(ptr, -len, map);

	if (((unsigned long)*ptr & 0xFF000000) != slot_space)
		pr_err("%s: moved out of slot address space!\n", __func__);
}

/* Now, functions to read the sResource tree */

/* Each sResource entry consists of a 1-byte ID and a 3-byte data
   field.  If that data field contains an offset, then obviously we
   have to expand it from a 24-bit signed number to a 32-bit signed
   number. */

static inline long nubus_expand32(long foo)
{
	if (foo & 0x00800000)	/* 24bit negative */
		foo |= 0xFF000000;
	return foo;
}

static inline void *nubus_rom_addr(int slot)
{
	/*
	 *	Returns the first byte after the card. We then walk
	 *	backwards to get the lane register and the config
	 */
	return (void *)(0xF1000000 + (slot << 24));
}

unsigned char *nubus_dirptr(const struct nubus_dirent *nd)
{
	unsigned char *p = nd->base;

	/* Essentially, just step over the bytelanes using whatever
	   offset we might have found */
	nubus_move(&p, nubus_expand32(nd->data), nd->mask);
	/* And return the value */
	return p;
}

/* These two are for pulling resource data blocks (i.e. stuff that's
   pointed to with offsets) out of the card ROM. */

void nubus_get_rsrc_mem(void *dest, const struct nubus_dirent *dirent,
			unsigned int len)
{
	unsigned char *t = dest;
	unsigned char *p = nubus_dirptr(dirent);

	while (len) {
		*t++ = nubus_get_rom(&p, 1, dirent->mask);
		len--;
	}
}
EXPORT_SYMBOL(nubus_get_rsrc_mem);

unsigned int nubus_get_rsrc_str(char *dest, const struct nubus_dirent *dirent,
				unsigned int len)
{
	char *t = dest;
	unsigned char *p = nubus_dirptr(dirent);

	while (len > 1) {
		unsigned char c = nubus_get_rom(&p, 1, dirent->mask);

		if (!c)
			break;
		*t++ = c;
		len--;
	}
	if (len > 0)
		*t = '\0';
	return t - dest;
}
EXPORT_SYMBOL(nubus_get_rsrc_str);

void nubus_seq_write_rsrc_mem(struct seq_file *m,
			      const struct nubus_dirent *dirent,
			      unsigned int len)
{
	unsigned long buf[32];
	unsigned int buf_size = sizeof(buf);
	unsigned char *p = nubus_dirptr(dirent);

	/* If possible, write out full buffers */
	while (len >= buf_size) {
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(buf); i++)
			buf[i] = nubus_get_rom(&p, sizeof(buf[0]),
					       dirent->mask);
		seq_write(m, buf, buf_size);
		len -= buf_size;
	}
	/* If not, write out individual bytes */
	while (len--)
		seq_putc(m, nubus_get_rom(&p, 1, dirent->mask));
}

int nubus_get_root_dir(const struct nubus_board *board,
		       struct nubus_dir *dir)
{
	dir->ptr = dir->base = board->directory;
	dir->done = 0;
	dir->mask = board->lanes;
	return 0;
}
EXPORT_SYMBOL(nubus_get_root_dir);

/* This is a slyly renamed version of the above */
int nubus_get_func_dir(const struct nubus_rsrc *fres, struct nubus_dir *dir)
{
	dir->ptr = dir->base = fres->directory;
	dir->done = 0;
	dir->mask = fres->board->lanes;
	return 0;
}
EXPORT_SYMBOL(nubus_get_func_dir);

int nubus_get_board_dir(const struct nubus_board *board,
			struct nubus_dir *dir)
{
	struct nubus_dirent ent;

	dir->ptr = dir->base = board->directory;
	dir->done = 0;
	dir->mask = board->lanes;

	/* Now dereference it (the first directory is always the board
	   directory) */
	if (nubus_readdir(dir, &ent) == -1)
		return -1;
	if (nubus_get_subdir(&ent, dir) == -1)
		return -1;
	return 0;
}
EXPORT_SYMBOL(nubus_get_board_dir);

int nubus_get_subdir(const struct nubus_dirent *ent,
		     struct nubus_dir *dir)
{
	dir->ptr = dir->base = nubus_dirptr(ent);
	dir->done = 0;
	dir->mask = ent->mask;
	return 0;
}
EXPORT_SYMBOL(nubus_get_subdir);

int nubus_readdir(struct nubus_dir *nd, struct nubus_dirent *ent)
{
	u32 resid;

	if (nd->done)
		return -1;

	/* Do this first, otherwise nubus_rewind & co are off by 4 */
	ent->base = nd->ptr;

	/* This moves nd->ptr forward */
	resid = nubus_get_rom(&nd->ptr, 4, nd->mask);

	/* EOL marker, as per the Apple docs */
	if ((resid & 0xff000000) == 0xff000000) {
		/* Mark it as done */
		nd->done = 1;
		return -1;
	}

	/* First byte is the resource ID */
	ent->type = resid >> 24;
	/* Low 3 bytes might contain data (or might not) */
	ent->data = resid & 0xffffff;
	ent->mask = nd->mask;
	return 0;
}
EXPORT_SYMBOL(nubus_readdir);

int nubus_rewinddir(struct nubus_dir *dir)
{
	dir->ptr = dir->base;
	dir->done = 0;
	return 0;
}
EXPORT_SYMBOL(nubus_rewinddir);

/* Driver interface functions, more or less like in pci.c */

struct nubus_rsrc *nubus_first_rsrc_or_null(void)
{
	return list_first_entry_or_null(&nubus_func_rsrcs, struct nubus_rsrc,
					list);
}
EXPORT_SYMBOL(nubus_first_rsrc_or_null);

struct nubus_rsrc *nubus_next_rsrc_or_null(struct nubus_rsrc *from)
{
	if (list_is_last(&from->list, &nubus_func_rsrcs))
		return NULL;
	return list_next_entry(from, list);
}
EXPORT_SYMBOL(nubus_next_rsrc_or_null);

int
nubus_find_rsrc(struct nubus_dir *dir, unsigned char rsrc_type,
		struct nubus_dirent *ent)
{
	while (nubus_readdir(dir, ent) != -1) {
		if (ent->type == rsrc_type)
			return 0;
	}
	return -1;
}
EXPORT_SYMBOL(nubus_find_rsrc);

/* Initialization functions - decide which slots contain stuff worth
   looking at, and print out lots and lots of information from the
   resource blocks. */

static int __init nubus_get_block_rsrc_dir(struct nubus_board *board,
					   struct proc_dir_entry *procdir,
					   const struct nubus_dirent *parent)
{
	struct nubus_dir dir;
	struct nubus_dirent ent;

	nubus_get_subdir(parent, &dir);
	dir.procdir = nubus_proc_add_rsrc_dir(procdir, parent, board);

	while (nubus_readdir(&dir, &ent) != -1) {
		u32 size;

		nubus_get_rsrc_mem(&size, &ent, 4);
		pr_debug("        block (0x%x), size %d\n", ent.type, size);
		nubus_proc_add_rsrc_mem(dir.procdir, &ent, size);
	}
	return 0;
}

static int __init nubus_get_display_vidmode(struct nubus_board *board,
					    struct proc_dir_entry *procdir,
					    const struct nubus_dirent *parent)
{
	struct nubus_dir dir;
	struct nubus_dirent ent;

	nubus_get_subdir(parent, &dir);
	dir.procdir = nubus_proc_add_rsrc_dir(procdir, parent, board);

	while (nubus_readdir(&dir, &ent) != -1) {
		switch (ent.type) {
		case 1: /* mVidParams */
		case 2: /* mTable */
		{
			u32 size;

			nubus_get_rsrc_mem(&size, &ent, 4);
			pr_debug("        block (0x%x), size %d\n", ent.type,
				size);
			nubus_proc_add_rsrc_mem(dir.procdir, &ent, size);
			break;
		}
		default:
			pr_debug("        unknown resource 0x%02x, data 0x%06x\n",
				ent.type, ent.data);
			nubus_proc_add_rsrc_mem(dir.procdir, &ent, 0);
		}
	}
	return 0;
}

static int __init nubus_get_display_resource(struct nubus_rsrc *fres,
					     struct proc_dir_entry *procdir,
					     const struct nubus_dirent *ent)
{
	switch (ent->type) {
	case NUBUS_RESID_GAMMADIR:
		pr_debug("    gamma directory offset: 0x%06x\n", ent->data);
		nubus_get_block_rsrc_dir(fres->board, procdir, ent);
		break;
	case 0x0080 ... 0x0085:
		pr_debug("    mode 0x%02x info offset: 0x%06x\n",
			ent->type, ent->data);
		nubus_get_display_vidmode(fres->board, procdir, ent);
		break;
	default:
		pr_debug("    unknown resource 0x%02x, data 0x%06x\n",
			ent->type, ent->data);
		nubus_proc_add_rsrc_mem(procdir, ent, 0);
	}
	return 0;
}

static int __init nubus_get_network_resource(struct nubus_rsrc *fres,
					     struct proc_dir_entry *procdir,
					     const struct nubus_dirent *ent)
{
	switch (ent->type) {
	case NUBUS_RESID_MAC_ADDRESS:
	{
		char addr[6];

		nubus_get_rsrc_mem(addr, ent, 6);
		pr_debug("    MAC address: %pM\n", addr);
		nubus_proc_add_rsrc_mem(procdir, ent, 6);
		break;
	}
	default:
		pr_debug("    unknown resource 0x%02x, data 0x%06x\n",
			ent->type, ent->data);
		nubus_proc_add_rsrc_mem(procdir, ent, 0);
	}
	return 0;
}

static int __init nubus_get_cpu_resource(struct nubus_rsrc *fres,
					 struct proc_dir_entry *procdir,
					 const struct nubus_dirent *ent)
{
	switch (ent->type) {
	case NUBUS_RESID_MEMINFO:
	{
		unsigned long meminfo[2];

		nubus_get_rsrc_mem(&meminfo, ent, 8);
		pr_debug("    memory: [ 0x%08lx 0x%08lx ]\n",
			meminfo[0], meminfo[1]);
		nubus_proc_add_rsrc_mem(procdir, ent, 8);
		break;
	}
	case NUBUS_RESID_ROMINFO:
	{
		unsigned long rominfo[2];

		nubus_get_rsrc_mem(&rominfo, ent, 8);
		pr_debug("    ROM:    [ 0x%08lx 0x%08lx ]\n",
			rominfo[0], rominfo[1]);
		nubus_proc_add_rsrc_mem(procdir, ent, 8);
		break;
	}
	default:
		pr_debug("    unknown resource 0x%02x, data 0x%06x\n",
			ent->type, ent->data);
		nubus_proc_add_rsrc_mem(procdir, ent, 0);
	}
	return 0;
}

static int __init nubus_get_private_resource(struct nubus_rsrc *fres,
					     struct proc_dir_entry *procdir,
					     const struct nubus_dirent *ent)
{
	switch (fres->category) {
	case NUBUS_CAT_DISPLAY:
		nubus_get_display_resource(fres, procdir, ent);
		break;
	case NUBUS_CAT_NETWORK:
		nubus_get_network_resource(fres, procdir, ent);
		break;
	case NUBUS_CAT_CPU:
		nubus_get_cpu_resource(fres, procdir, ent);
		break;
	default:
		pr_debug("    unknown resource 0x%02x, data 0x%06x\n",
			ent->type, ent->data);
		nubus_proc_add_rsrc_mem(procdir, ent, 0);
	}
	return 0;
}

static struct nubus_rsrc * __init
nubus_get_functional_resource(struct nubus_board *board, int slot,
			      const struct nubus_dirent *parent)
{
	struct nubus_dir dir;
	struct nubus_dirent ent;
	struct nubus_rsrc *fres;

	pr_debug("  Functional resource 0x%02x:\n", parent->type);
	nubus_get_subdir(parent, &dir);
	dir.procdir = nubus_proc_add_rsrc_dir(board->procdir, parent, board);

	/* Actually we should probably panic if this fails */
	fres = kzalloc(sizeof(*fres), GFP_ATOMIC);
	if (!fres)
		return NULL;
	fres->resid = parent->type;
	fres->directory = dir.base;
	fres->board = board;

	while (nubus_readdir(&dir, &ent) != -1) {
		switch (ent.type) {
		case NUBUS_RESID_TYPE:
		{
			unsigned short nbtdata[4];

			nubus_get_rsrc_mem(nbtdata, &ent, 8);
			fres->category = nbtdata[0];
			fres->type     = nbtdata[1];
			fres->dr_sw    = nbtdata[2];
			fres->dr_hw    = nbtdata[3];
			pr_debug("    type: [cat 0x%x type 0x%x sw 0x%x hw 0x%x]\n",
				nbtdata[0], nbtdata[1], nbtdata[2], nbtdata[3]);
			nubus_proc_add_rsrc_mem(dir.procdir, &ent, 8);
			break;
		}
		case NUBUS_RESID_NAME:
		{
			char name[64];
			unsigned int len;

			len = nubus_get_rsrc_str(name, &ent, sizeof(name));
			pr_debug("    name: %s\n", name);
			nubus_proc_add_rsrc_mem(dir.procdir, &ent, len + 1);
			break;
		}
		case NUBUS_RESID_DRVRDIR:
		{
			/* MacOS driver.  If we were NetBSD we might
			   use this :-) */
			pr_debug("    driver directory offset: 0x%06x\n",
				ent.data);
			nubus_get_block_rsrc_dir(board, dir.procdir, &ent);
			break;
		}
		case NUBUS_RESID_MINOR_BASEOS:
		{
			/* We will need this in order to support
			   multiple framebuffers.  It might be handy
			   for Ethernet as well */
			u32 base_offset;

			nubus_get_rsrc_mem(&base_offset, &ent, 4);
			pr_debug("    memory offset: 0x%08x\n", base_offset);
			nubus_proc_add_rsrc_mem(dir.procdir, &ent, 4);
			break;
		}
		case NUBUS_RESID_MINOR_LENGTH:
		{
			/* Ditto */
			u32 length;

			nubus_get_rsrc_mem(&length, &ent, 4);
			pr_debug("    memory length: 0x%08x\n", length);
			nubus_proc_add_rsrc_mem(dir.procdir, &ent, 4);
			break;
		}
		case NUBUS_RESID_FLAGS:
			pr_debug("    flags: 0x%06x\n", ent.data);
			nubus_proc_add_rsrc(dir.procdir, &ent);
			break;
		case NUBUS_RESID_HWDEVID:
			pr_debug("    hwdevid: 0x%06x\n", ent.data);
			nubus_proc_add_rsrc(dir.procdir, &ent);
			break;
		default:
			if (nubus_populate_procfs)
				nubus_get_private_resource(fres, dir.procdir,
							   &ent);
		}
	}

	return fres;
}

/* This is *really* cool. */
static int __init nubus_get_icon(struct nubus_board *board,
				 struct proc_dir_entry *procdir,
				 const struct nubus_dirent *ent)
{
	/* Should be 32x32 if my memory serves me correctly */
	u32 icon[32];
	int i;

	nubus_get_rsrc_mem(&icon, ent, 128);
	pr_debug("    icon:\n");
	for (i = 0; i < 8; i++)
		pr_debug("        %08x %08x %08x %08x\n",
			icon[i * 4 + 0], icon[i * 4 + 1],
			icon[i * 4 + 2], icon[i * 4 + 3]);
	nubus_proc_add_rsrc_mem(procdir, ent, 128);

	return 0;
}

static int __init nubus_get_vendorinfo(struct nubus_board *board,
				       struct proc_dir_entry *procdir,
				       const struct nubus_dirent *parent)
{
	struct nubus_dir dir;
	struct nubus_dirent ent;
	static char *vendor_fields[6] = { "ID", "serial", "revision",
	                                  "part", "date", "unknown field" };

	pr_debug("    vendor info:\n");
	nubus_get_subdir(parent, &dir);
	dir.procdir = nubus_proc_add_rsrc_dir(procdir, parent, board);

	while (nubus_readdir(&dir, &ent) != -1) {
		char name[64];
		unsigned int len;

		/* These are all strings, we think */
		len = nubus_get_rsrc_str(name, &ent, sizeof(name));
		if (ent.type < 1 || ent.type > 5)
			ent.type = 5;
		pr_debug("    %s: %s\n", vendor_fields[ent.type - 1], name);
		nubus_proc_add_rsrc_mem(dir.procdir, &ent, len + 1);
	}
	return 0;
}

static int __init nubus_get_board_resource(struct nubus_board *board, int slot,
					   const struct nubus_dirent *parent)
{
	struct nubus_dir dir;
	struct nubus_dirent ent;

	pr_debug("  Board resource 0x%02x:\n", parent->type);
	nubus_get_subdir(parent, &dir);
	dir.procdir = nubus_proc_add_rsrc_dir(board->procdir, parent, board);

	while (nubus_readdir(&dir, &ent) != -1) {
		switch (ent.type) {
		case NUBUS_RESID_TYPE:
		{
			unsigned short nbtdata[4];
			/* This type is always the same, and is not
			   useful except insofar as it tells us that
			   we really are looking at a board resource. */
			nubus_get_rsrc_mem(nbtdata, &ent, 8);
			pr_debug("    type: [cat 0x%x type 0x%x sw 0x%x hw 0x%x]\n",
				nbtdata[0], nbtdata[1], nbtdata[2], nbtdata[3]);
			if (nbtdata[0] != 1 || nbtdata[1] != 0 ||
			    nbtdata[2] != 0 || nbtdata[3] != 0)
				pr_err("Slot %X: sResource is not a board resource!\n",
				       slot);
			nubus_proc_add_rsrc_mem(dir.procdir, &ent, 8);
			break;
		}
		case NUBUS_RESID_NAME:
		{
			unsigned int len;

			len = nubus_get_rsrc_str(board->name, &ent,
						 sizeof(board->name));
			pr_debug("    name: %s\n", board->name);
			nubus_proc_add_rsrc_mem(dir.procdir, &ent, len + 1);
			break;
		}
		case NUBUS_RESID_ICON:
			nubus_get_icon(board, dir.procdir, &ent);
			break;
		case NUBUS_RESID_BOARDID:
			pr_debug("    board id: 0x%x\n", ent.data);
			nubus_proc_add_rsrc(dir.procdir, &ent);
			break;
		case NUBUS_RESID_PRIMARYINIT:
			pr_debug("    primary init offset: 0x%06x\n", ent.data);
			nubus_proc_add_rsrc(dir.procdir, &ent);
			break;
		case NUBUS_RESID_VENDORINFO:
			nubus_get_vendorinfo(board, dir.procdir, &ent);
			break;
		case NUBUS_RESID_FLAGS:
			pr_debug("    flags: 0x%06x\n", ent.data);
			nubus_proc_add_rsrc(dir.procdir, &ent);
			break;
		case NUBUS_RESID_HWDEVID:
			pr_debug("    hwdevid: 0x%06x\n", ent.data);
			nubus_proc_add_rsrc(dir.procdir, &ent);
			break;
		case NUBUS_RESID_SECONDINIT:
			pr_debug("    secondary init offset: 0x%06x\n",
				 ent.data);
			nubus_proc_add_rsrc(dir.procdir, &ent);
			break;
			/* WTF isn't this in the functional resources? */
		case NUBUS_RESID_VIDNAMES:
			pr_debug("    vidnames directory offset: 0x%06x\n",
				ent.data);
			nubus_get_block_rsrc_dir(board, dir.procdir, &ent);
			break;
			/* Same goes for this */
		case NUBUS_RESID_VIDMODES:
			pr_debug("    video mode parameter directory offset: 0x%06x\n",
				ent.data);
			nubus_proc_add_rsrc(dir.procdir, &ent);
			break;
		default:
			pr_debug("    unknown resource 0x%02x, data 0x%06x\n",
				ent.type, ent.data);
			nubus_proc_add_rsrc_mem(dir.procdir, &ent, 0);
		}
	}
	return 0;
}

static void __init nubus_add_board(int slot, int bytelanes)
{
	struct nubus_board *board;
	unsigned char *rp;
	unsigned long dpat;
	struct nubus_dir dir;
	struct nubus_dirent ent;
	int prev_resid = -1;

	/* Move to the start of the format block */
	rp = nubus_rom_addr(slot);
	nubus_rewind(&rp, FORMAT_BLOCK_SIZE, bytelanes);

	/* Actually we should probably panic if this fails */
	if ((board = kzalloc(sizeof(*board), GFP_ATOMIC)) == NULL)
		return;
	board->fblock = rp;

	/* Dump the format block for debugging purposes */
	pr_debug("Slot %X, format block at 0x%p:\n", slot, rp);
	pr_debug("%08lx\n", nubus_get_rom(&rp, 4, bytelanes));
	pr_debug("%08lx\n", nubus_get_rom(&rp, 4, bytelanes));
	pr_debug("%08lx\n", nubus_get_rom(&rp, 4, bytelanes));
	pr_debug("%02lx\n", nubus_get_rom(&rp, 1, bytelanes));
	pr_debug("%02lx\n", nubus_get_rom(&rp, 1, bytelanes));
	pr_debug("%08lx\n", nubus_get_rom(&rp, 4, bytelanes));
	pr_debug("%02lx\n", nubus_get_rom(&rp, 1, bytelanes));
	pr_debug("%02lx\n", nubus_get_rom(&rp, 1, bytelanes));
	rp = board->fblock;

	board->slot = slot;
	board->slot_addr = (unsigned long)nubus_slot_addr(slot);
	board->doffset = nubus_get_rom(&rp, 4, bytelanes);
	/* rom_length is *supposed* to be the total length of the
	 * ROM.  In practice it is the "amount of ROM used to compute
	 * the CRC."  So some jokers decide to set it to zero and
	 * set the crc to zero so they don't have to do any math.
	 * See the Performa 460 ROM, for example.  Those Apple "engineers".
	 */
	board->rom_length = nubus_get_rom(&rp, 4, bytelanes);
	board->crc = nubus_get_rom(&rp, 4, bytelanes);
	board->rev = nubus_get_rom(&rp, 1, bytelanes);
	board->format = nubus_get_rom(&rp, 1, bytelanes);
	board->lanes = bytelanes;

	/* Directory offset should be small and negative... */
	if (!(board->doffset & 0x00FF0000))
		pr_warn("Slot %X: Dodgy doffset!\n", slot);
	dpat = nubus_get_rom(&rp, 4, bytelanes);
	if (dpat != NUBUS_TEST_PATTERN)
		pr_warn("Slot %X: Wrong test pattern %08lx!\n", slot, dpat);

	/*
	 *	I wonder how the CRC is meant to work -
	 *		any takers ?
	 * CSA: According to MAC docs, not all cards pass the CRC anyway,
	 * since the initial Macintosh ROM releases skipped the check.
	 */

	/* Set up the directory pointer */
	board->directory = board->fblock;
	nubus_move(&board->directory, nubus_expand32(board->doffset),
	           board->lanes);

	nubus_get_root_dir(board, &dir);

	/* We're ready to rock */
	pr_debug("Slot %X resources:\n", slot);

	/* Each slot should have one board resource and any number of
	 * functional resources.  So we'll fill in some fields in the
	 * struct nubus_board from the board resource, then walk down
	 * the list of functional resources, spinning out a nubus_rsrc
	 * for each of them.
	 */
	if (nubus_readdir(&dir, &ent) == -1) {
		/* We can't have this! */
		pr_err("Slot %X: Board resource not found!\n", slot);
		kfree(board);
		return;
	}

	if (ent.type < 1 || ent.type > 127)
		pr_warn("Slot %X: Board resource ID is invalid!\n", slot);

	board->procdir = nubus_proc_add_board(board);

	nubus_get_board_resource(board, slot, &ent);

	while (nubus_readdir(&dir, &ent) != -1) {
		struct nubus_rsrc *fres;

		fres = nubus_get_functional_resource(board, slot, &ent);
		if (fres == NULL)
			continue;

		/* Resources should appear in ascending ID order. This sanity
		 * check prevents duplicate resource IDs.
		 */
		if (fres->resid <= prev_resid) {
			kfree(fres);
			continue;
		}
		prev_resid = fres->resid;

		list_add_tail(&fres->list, &nubus_func_rsrcs);
	}

	if (nubus_device_register(board))
		put_device(&board->dev);
}

static void __init nubus_probe_slot(int slot)
{
	unsigned char dp;
	unsigned char *rp;
	int i;

	rp = nubus_rom_addr(slot);
	for (i = 4; i; i--) {
		rp--;
		if (!hwreg_present(rp))
			continue;

		dp = *rp;

		/* The last byte of the format block consists of two
		   nybbles which are "mirror images" of each other.
		   These show us the valid bytelanes */
		if ((((dp >> 4) ^ dp) & 0x0F) != 0x0F)
			continue;
		/* Check that this value is actually *on* one of the
		   bytelanes it claims are valid! */
		if (not_useful(rp, dp))
			continue;

		/* Looks promising.  Let's put it on the list. */
		nubus_add_board(slot, dp);

		return;
	}
}

static void __init nubus_scan_bus(void)
{
	int slot;

	pr_info("NuBus: Scanning NuBus slots.\n");
	for (slot = 9; slot < 15; slot++) {
		nubus_probe_slot(slot);
	}
}

static int __init nubus_init(void)
{
	int err;

	if (!MACH_IS_MAC)
		return 0;

	nubus_proc_init();
	err = nubus_parent_device_register();
	if (err)
		return err;
	nubus_scan_bus();
	return 0;
}

subsys_initcall(nubus_init);
