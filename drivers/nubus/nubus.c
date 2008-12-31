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
#include <linux/delay.h>
#include <linux/module.h>
#include <asm/setup.h>
#include <asm/system.h>
#include <asm/page.h>
#include <asm/hwtest.h>
#include <linux/proc_fs.h>
#include <asm/mac_via.h>
#include <asm/mac_oss.h>

extern void via_nubus_init(void);
extern void oss_nubus_init(void);

/* Constants */

/* This is, of course, the size in bytelanes, rather than the size in
   actual bytes */
#define FORMAT_BLOCK_SIZE 20
#define ROM_DIR_OFFSET 0x24

#define NUBUS_TEST_PATTERN 0x5A932BC7

/* Define this if you like to live dangerously - it is known not to
   work on pretty much every machine except the Quadra 630 and the LC
   III. */
#undef I_WANT_TO_PROBE_SLOT_ZERO

/* This sometimes helps combat failure to boot */
#undef TRY_TO_DODGE_WSOD

/* Globals */

struct nubus_dev*   nubus_devices;
struct nubus_board* nubus_boards;

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
	unsigned long pv=(unsigned long)p;
	pv &= 3;
	if(map & (1<<pv))
		return 0;
	return 1;
}
 
static unsigned long nubus_get_rom(unsigned char **ptr, int len, int map)
{
	/* This will hold the result */
	unsigned long v = 0;
	unsigned char *p = *ptr;

	while(len)
	{
		v <<= 8;
		while(not_useful(p,map))
			p++;
		v |= *p++;
		len--;
	}
	*ptr = p;
	return v;
}

static void nubus_rewind(unsigned char **ptr, int len, int map)
{
	unsigned char *p=*ptr;

	/* Sanity check */
	if(len > 65536)
		printk(KERN_ERR "rewind of 0x%08x!\n", len);
	while(len)
	{
		do
		{
			p--;
		}
		while(not_useful(p, map));
		len--;
	}
	*ptr=p;
}

static void nubus_advance(unsigned char **ptr, int len, int map)
{
	unsigned char *p = *ptr;
	if(len>65536)
		printk(KERN_ERR "advance of 0x%08x!\n", len);
	while(len)
	{
		while(not_useful(p,map))
			p++;
		p++;
		len--;
	}
	*ptr = p;
}

static void nubus_move(unsigned char **ptr, int len, int map)
{
	if(len > 0)
		nubus_advance(ptr, len, map);
	else if(len < 0)
		nubus_rewind(ptr, -len, map);
}

/* Now, functions to read the sResource tree */

/* Each sResource entry consists of a 1-byte ID and a 3-byte data
   field.  If that data field contains an offset, then obviously we
   have to expand it from a 24-bit signed number to a 32-bit signed
   number. */

static inline long nubus_expand32(long foo)
{
	if(foo & 0x00800000)	/* 24bit negative */
		foo |= 0xFF000000;
	return foo;
}

static inline void *nubus_rom_addr(int slot)
{	
	/*
	 *	Returns the first byte after the card. We then walk
	 *	backwards to get the lane register and the config
	 */
	return (void *)(0xF1000000+(slot<<24));
}

static unsigned char *nubus_dirptr(const struct nubus_dirent *nd)
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

void nubus_get_rsrc_mem(void *dest, const struct nubus_dirent* dirent,
			int len)
{
	unsigned char *t = (unsigned char *)dest;
	unsigned char *p = nubus_dirptr(dirent);
	while(len)
	{
		*t++ = nubus_get_rom(&p, 1, dirent->mask);
		len--;
	}
}
EXPORT_SYMBOL(nubus_get_rsrc_mem);

void nubus_get_rsrc_str(void *dest, const struct nubus_dirent* dirent,
			int len)
{
	unsigned char *t=(unsigned char *)dest;
	unsigned char *p = nubus_dirptr(dirent);
	while(len)
	{
		*t = nubus_get_rom(&p, 1, dirent->mask);
		if(!*t++)
			break;
		len--;
	}
}
EXPORT_SYMBOL(nubus_get_rsrc_str);

int nubus_get_root_dir(const struct nubus_board* board,
		       struct nubus_dir* dir)
{
	dir->ptr = dir->base = board->directory;
	dir->done = 0;
	dir->mask = board->lanes;
	return 0;
}
EXPORT_SYMBOL(nubus_get_root_dir);

/* This is a slyly renamed version of the above */
int nubus_get_func_dir(const struct nubus_dev* dev,
		       struct nubus_dir* dir)
{
	dir->ptr = dir->base = dev->directory;
	dir->done = 0;
	dir->mask = dev->board->lanes;
	return 0;
}
EXPORT_SYMBOL(nubus_get_func_dir);

int nubus_get_board_dir(const struct nubus_board* board,
			struct nubus_dir* dir)
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
	if((resid&0xff000000) == 0xff000000)
	{
		/* Mark it as done */
		nd->done = 1;
		return -1;
	}

	/* First byte is the resource ID */
	ent->type  = resid >> 24;
	/* Low 3 bytes might contain data (or might not) */
	ent->data = resid & 0xffffff;
	ent->mask  = nd->mask;
	return 0;
}
EXPORT_SYMBOL(nubus_readdir);

int nubus_rewinddir(struct nubus_dir* dir)
{
	dir->ptr = dir->base;
	return 0;
}
EXPORT_SYMBOL(nubus_rewinddir);

/* Driver interface functions, more or less like in pci.c */

struct nubus_dev*
nubus_find_device(unsigned short category,
		  unsigned short type,
		  unsigned short dr_hw,
		  unsigned short dr_sw,
		  const struct nubus_dev* from)
{
	struct nubus_dev* itor =
		from ? from->next : nubus_devices;

	while (itor) {
		if (itor->category == category
		    && itor->type == type
		    && itor->dr_hw == dr_hw
		    && itor->dr_sw == dr_sw)
			return itor;
		itor = itor->next;
	}
	return NULL;
}
EXPORT_SYMBOL(nubus_find_device);

struct nubus_dev*
nubus_find_type(unsigned short category,
		unsigned short type,
		const struct nubus_dev* from)
{
	struct nubus_dev* itor =
		from ? from->next : nubus_devices;

	while (itor) {
		if (itor->category == category
		    && itor->type == type)
			return itor;
		itor = itor->next;
	}
	return NULL;
}
EXPORT_SYMBOL(nubus_find_type);

struct nubus_dev*
nubus_find_slot(unsigned int slot,
		const struct nubus_dev* from)
{
	struct nubus_dev* itor =
		from ? from->next : nubus_devices;
	
	while (itor) {
		if (itor->board->slot == slot)
			return itor;
		itor = itor->next;
	}
	return NULL;
}
EXPORT_SYMBOL(nubus_find_slot);

int
nubus_find_rsrc(struct nubus_dir* dir, unsigned char rsrc_type,
		struct nubus_dirent* ent)
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

/* FIXME: A lot of this stuff will eventually be useful after
   initialization, for intelligently probing Ethernet and video chips,
   among other things.  The rest of it should go in the /proc code.
   For now, we just use it to give verbose boot logs. */

static int __init nubus_show_display_resource(struct nubus_dev* dev,
					      const struct nubus_dirent* ent)
{
	switch (ent->type) {
	case NUBUS_RESID_GAMMADIR:
		printk(KERN_INFO "    gamma directory offset: 0x%06x\n", ent->data);
		break;
	case 0x0080 ... 0x0085:
		printk(KERN_INFO "    mode %02X info offset: 0x%06x\n",
		       ent->type, ent->data);
		break;
	default:
		printk(KERN_INFO "    unknown resource %02X, data 0x%06x\n",
		       ent->type, ent->data);
	}
	return 0;
}

static int __init nubus_show_network_resource(struct nubus_dev* dev,
					      const struct nubus_dirent* ent)
{
	switch (ent->type) {
	case NUBUS_RESID_MAC_ADDRESS:
	{
		char addr[6];
		int i;
		
		nubus_get_rsrc_mem(addr, ent, 6);
		printk(KERN_INFO "    MAC address: ");
		for (i = 0; i < 6; i++)
			printk("%02x%s", addr[i] & 0xff,
			       i == 5 ? "" : ":");
		printk("\n");
		break;
	}
	default:
		printk(KERN_INFO "    unknown resource %02X, data 0x%06x\n",
		       ent->type, ent->data);
	}
	return 0;
}

static int __init nubus_show_cpu_resource(struct nubus_dev* dev,
					  const struct nubus_dirent* ent)
{
	switch (ent->type) {
	case NUBUS_RESID_MEMINFO:
	{
		unsigned long meminfo[2];
		nubus_get_rsrc_mem(&meminfo, ent, 8);
		printk(KERN_INFO "    memory: [ 0x%08lx 0x%08lx ]\n",
		       meminfo[0], meminfo[1]);
		break;
	}
	case NUBUS_RESID_ROMINFO:
	{
		unsigned long rominfo[2];
		nubus_get_rsrc_mem(&rominfo, ent, 8);
		printk(KERN_INFO "    ROM:    [ 0x%08lx 0x%08lx ]\n",
		       rominfo[0], rominfo[1]);
		break;
	}
	default:
		printk(KERN_INFO "    unknown resource %02X, data 0x%06x\n",
		       ent->type, ent->data);
	}
	return 0;
}

static int __init nubus_show_private_resource(struct nubus_dev* dev,
					      const struct nubus_dirent* ent)
{
	switch (dev->category) {
	case NUBUS_CAT_DISPLAY:
		nubus_show_display_resource(dev, ent);
		break;
	case NUBUS_CAT_NETWORK:
		nubus_show_network_resource(dev, ent);
		break;
	case NUBUS_CAT_CPU:
		nubus_show_cpu_resource(dev, ent);
		break;
	default:
		printk(KERN_INFO "    unknown resource %02X, data 0x%06x\n",
		       ent->type, ent->data);
	}
	return 0;
}

static struct nubus_dev* __init
	   nubus_get_functional_resource(struct nubus_board* board,
					 int slot,
					 const struct nubus_dirent* parent)
{
	struct nubus_dir    dir;
	struct nubus_dirent ent;
	struct nubus_dev*   dev;
	
	printk(KERN_INFO "  Function 0x%02x:\n", parent->type);
	nubus_get_subdir(parent, &dir);

	/* Apple seems to have botched the ROM on the IIx */
	if (slot == 0 && (unsigned long)dir.base % 2)
		dir.base += 1;
	
	if (console_loglevel >= 10)
		printk(KERN_DEBUG "nubus_get_functional_resource: parent is 0x%p, dir is 0x%p\n",
		       parent->base, dir.base);

	/* Actually we should probably panic if this fails */
	if ((dev = kzalloc(sizeof(*dev), GFP_ATOMIC)) == NULL)
		return NULL;	
	dev->resid = parent->type;
	dev->directory = dir.base;
	dev->board = board;
	
	while (nubus_readdir(&dir, &ent) != -1)
	{
		switch(ent.type)
		{
		case NUBUS_RESID_TYPE:
		{
			unsigned short nbtdata[4];
			nubus_get_rsrc_mem(nbtdata, &ent, 8);
			dev->category = nbtdata[0];
			dev->type     = nbtdata[1];
			dev->dr_sw    = nbtdata[2];
			dev->dr_hw    = nbtdata[3];
			printk(KERN_INFO "    type: [cat 0x%x type 0x%x hw 0x%x sw 0x%x]\n",
			       nbtdata[0], nbtdata[1], nbtdata[2], nbtdata[3]);
			break;
		}
		case NUBUS_RESID_NAME:
		{
			nubus_get_rsrc_str(dev->name, &ent, 64);
			printk(KERN_INFO "    name: %s\n", dev->name);
			break;
		}
		case NUBUS_RESID_DRVRDIR:
		{
			/* MacOS driver.  If we were NetBSD we might
			   use this :-) */
			struct nubus_dir drvr_dir;
			struct nubus_dirent drvr_ent;
			nubus_get_subdir(&ent, &drvr_dir);
			nubus_readdir(&drvr_dir, &drvr_ent);
			dev->driver = nubus_dirptr(&drvr_ent);
			printk(KERN_INFO "    driver at: 0x%p\n",
			       dev->driver);
			break;
		}
		case NUBUS_RESID_MINOR_BASEOS:
			/* We will need this in order to support
			   multiple framebuffers.  It might be handy
			   for Ethernet as well */
			nubus_get_rsrc_mem(&dev->iobase, &ent, 4);
			printk(KERN_INFO "    memory offset: 0x%08lx\n",
			       dev->iobase);
			break;
		case NUBUS_RESID_MINOR_LENGTH:
			/* Ditto */
			nubus_get_rsrc_mem(&dev->iosize, &ent, 4);
			printk(KERN_INFO "    memory length: 0x%08lx\n",
			       dev->iosize);
			break;			
		case NUBUS_RESID_FLAGS:
			dev->flags = ent.data;
			printk(KERN_INFO "    flags: 0x%06x\n", dev->flags);
			break;
		case NUBUS_RESID_HWDEVID:
			dev->hwdevid = ent.data;
			printk(KERN_INFO "    hwdevid: 0x%06x\n", dev->hwdevid);
			break;
		default:
			/* Local/Private resources have their own
			   function */
			nubus_show_private_resource(dev, &ent);
		}
	}
		
	return dev;
}

/* This is cool. */
static int __init nubus_get_vidnames(struct nubus_board* board,
				     const struct nubus_dirent* parent)
{
	struct nubus_dir    dir;
	struct nubus_dirent ent;
	/* FIXME: obviously we want to put this in a header file soon */
	struct vidmode {
		u32 size;
		/* Don't know what this is yet */
		u16 id;
		/* Longest one I've seen so far is 26 characters */
		char name[32];
	};

	printk(KERN_INFO "    video modes supported:\n");
	nubus_get_subdir(parent, &dir);
	if (console_loglevel >= 10)
		printk(KERN_DEBUG "nubus_get_vidnames: parent is 0x%p, dir is 0x%p\n",
		       parent->base, dir.base);

	while(nubus_readdir(&dir, &ent) != -1)
	{
		struct vidmode mode;
		u32 size;

		/* First get the length */
		nubus_get_rsrc_mem(&size, &ent, 4);
		
		/* Now clobber the whole thing */
		if (size > sizeof(mode) - 1)
			size = sizeof(mode) - 1;
		memset(&mode, 0, sizeof(mode));
		nubus_get_rsrc_mem(&mode, &ent, size);
		printk (KERN_INFO "      %02X: (%02X) %s\n", ent.type,
			mode.id, mode.name);
	}
	return 0;
}

/* This is *really* cool. */
static int __init nubus_get_icon(struct nubus_board* board,
				 const struct nubus_dirent* ent)
{
	/* Should be 32x32 if my memory serves me correctly */
	unsigned char icon[128];
	int x, y;
	
	nubus_get_rsrc_mem(&icon, ent, 128);
	printk(KERN_INFO "    icon:\n");

	/* We should actually plot these somewhere in the framebuffer
	   init.  This is just to demonstrate that they do, in fact,
	   exist */
	for (y = 0; y < 32; y++) {
		printk(KERN_INFO "      ");
		for (x = 0; x < 32; x++) {
			if (icon[y*4 + x/8]
			    & (0x80 >> (x%8)))
				printk("*");
			else
				printk(" ");
		}
		printk("\n");
	}
	return 0;
}

static int __init nubus_get_vendorinfo(struct nubus_board* board,
				       const struct nubus_dirent* parent)
{
	struct nubus_dir    dir;
	struct nubus_dirent ent;
	static char* vendor_fields[6] = {"ID", "serial", "revision",
					 "part", "date", "unknown field"};

	printk(KERN_INFO "    vendor info:\n");
	nubus_get_subdir(parent, &dir);
	if (console_loglevel >= 10)
		printk(KERN_DEBUG "nubus_get_vendorinfo: parent is 0x%p, dir is 0x%p\n",
		       parent->base, dir.base);

	while(nubus_readdir(&dir, &ent) != -1)
	{
		char name[64];
		
		/* These are all strings, we think */
		nubus_get_rsrc_str(name, &ent, 64);
		if (ent.type > 5)
			ent.type = 5;
		printk(KERN_INFO "    %s: %s\n",
		       vendor_fields[ent.type-1], name);
	}
	return 0;
}

static int __init nubus_get_board_resource(struct nubus_board* board, int slot,
					   const struct nubus_dirent* parent)
{
	struct nubus_dir    dir;
	struct nubus_dirent ent;
	
	nubus_get_subdir(parent, &dir);
	if (console_loglevel >= 10)
		printk(KERN_DEBUG "nubus_get_board_resource: parent is 0x%p, dir is 0x%p\n",
		       parent->base, dir.base);

	while(nubus_readdir(&dir, &ent) != -1)
	{
		switch (ent.type) {
		case NUBUS_RESID_TYPE:
		{
			unsigned short nbtdata[4];
			/* This type is always the same, and is not
			   useful except insofar as it tells us that
			   we really are looking at a board resource. */
			nubus_get_rsrc_mem(nbtdata, &ent, 8);
			printk(KERN_INFO "    type: [cat 0x%x type 0x%x hw 0x%x sw 0x%x]\n",
			       nbtdata[0], nbtdata[1], nbtdata[2],
			       nbtdata[3]);
			if (nbtdata[0] != 1 || nbtdata[1] != 0 ||
			    nbtdata[2] != 0 || nbtdata[3] != 0)
				printk(KERN_ERR "this sResource is not a board resource!\n");
			break;
		}
		case NUBUS_RESID_NAME:
			nubus_get_rsrc_str(board->name, &ent, 64);
			printk(KERN_INFO "    name: %s\n", board->name);
			break;
		case NUBUS_RESID_ICON:
			nubus_get_icon(board, &ent);
			break;
		case NUBUS_RESID_BOARDID:
			printk(KERN_INFO "    board id: 0x%x\n", ent.data);
			break;
		case NUBUS_RESID_PRIMARYINIT:
			printk(KERN_INFO "    primary init offset: 0x%06x\n", ent.data);
			break;
		case NUBUS_RESID_VENDORINFO:
			nubus_get_vendorinfo(board, &ent);
			break;
		case NUBUS_RESID_FLAGS:
			printk(KERN_INFO "    flags: 0x%06x\n", ent.data);
			break;
		case NUBUS_RESID_HWDEVID:
			printk(KERN_INFO "    hwdevid: 0x%06x\n", ent.data);
			break;
		case NUBUS_RESID_SECONDINIT:
			printk(KERN_INFO "    secondary init offset: 0x%06x\n", ent.data);
			break;
			/* WTF isn't this in the functional resources? */ 
		case NUBUS_RESID_VIDNAMES:
			nubus_get_vidnames(board, &ent);
			break;
			/* Same goes for this */
		case NUBUS_RESID_VIDMODES:
			printk(KERN_INFO "    video mode parameter directory offset: 0x%06x\n",
			       ent.data);
			break;			
		default:
			printk(KERN_INFO "    unknown resource %02X, data 0x%06x\n",
			       ent.type, ent.data);
		}
	}
	return 0;
}

/* Attempt to bypass the somewhat non-obvious arrangement of
   sResources in the motherboard ROM */
static void __init nubus_find_rom_dir(struct nubus_board* board)
{
	unsigned char* rp;
	unsigned char* romdir;
	struct nubus_dir dir;
	struct nubus_dirent ent;

	/* Check for the extra directory just under the format block */
	rp = board->fblock;
	nubus_rewind(&rp, 4, board->lanes);
	if (nubus_get_rom(&rp, 4, board->lanes) != NUBUS_TEST_PATTERN) {
		/* OK, the ROM was telling the truth */
		board->directory = board->fblock;
		nubus_move(&board->directory,
			   nubus_expand32(board->doffset),
			   board->lanes);
		return;
	}

	/* On "slot zero", you have to walk down a few more
	   directories to get to the equivalent of a real card's root
	   directory.  We don't know what they were smoking when they
	   came up with this. */
	romdir = nubus_rom_addr(board->slot);
	nubus_rewind(&romdir, ROM_DIR_OFFSET, board->lanes);
	dir.base = dir.ptr = romdir;
	dir.done = 0;
	dir.mask = board->lanes;

	/* This one points to an "Unknown Macintosh" directory */
	if (nubus_readdir(&dir, &ent) == -1)
		goto badrom;

	if (console_loglevel >= 10)
		printk(KERN_INFO "nubus_get_rom_dir: entry %02x %06x\n", ent.type, ent.data);
	/* This one takes us to where we want to go. */
	if (nubus_readdir(&dir, &ent) == -1) 
		goto badrom;
	if (console_loglevel >= 10)
		printk(KERN_DEBUG "nubus_get_rom_dir: entry %02x %06x\n", ent.type, ent.data);
	nubus_get_subdir(&ent, &dir);

	/* Resource ID 01, also an "Unknown Macintosh" */
	if (nubus_readdir(&dir, &ent) == -1) 
		goto badrom;
	if (console_loglevel >= 10)
		printk(KERN_DEBUG "nubus_get_rom_dir: entry %02x %06x\n", ent.type, ent.data);

	/* FIXME: the first one is *not* always the right one.  We
	   suspect this has something to do with the ROM revision.
	   "The HORROR ROM" (LC-series) uses 0x7e, while "The HORROR
	   Continues" (Q630) uses 0x7b.  The DAFB Macs evidently use
	   something else.  Please run "Slots" on your Mac (see
	   include/linux/nubus.h for where to get this program) and
	   tell us where the 'SiDirPtr' for Slot 0 is.  If you feel
	   brave, you should also use MacsBug to walk down the ROM
	   directories like this function does and try to find the
	   path to that address... */
	if (nubus_readdir(&dir, &ent) == -1)
		goto badrom;
	if (console_loglevel >= 10)
		printk(KERN_DEBUG "nubus_get_rom_dir: entry %02x %06x\n", ent.type, ent.data);
	
	/* Bwahahahaha... */
	nubus_get_subdir(&ent, &dir);
	board->directory = dir.base;
	return;
	
	/* Even more evil laughter... */
 badrom:
	board->directory = board->fblock;
	nubus_move(&board->directory, nubus_expand32(board->doffset), board->lanes);
	printk(KERN_ERR "nubus_get_rom_dir: ROM weirdness!  Notify the developers...\n");
}

/* Add a board (might be many devices) to the list */
static struct nubus_board* __init nubus_add_board(int slot, int bytelanes)
{
	struct nubus_board* board;
	struct nubus_board** boardp;

	unsigned char *rp;
	unsigned long dpat;
	struct nubus_dir dir;
	struct nubus_dirent ent;

	/* Move to the start of the format block */
	rp = nubus_rom_addr(slot);		
	nubus_rewind(&rp, FORMAT_BLOCK_SIZE, bytelanes);

	/* Actually we should probably panic if this fails */
	if ((board = kzalloc(sizeof(*board), GFP_ATOMIC)) == NULL)
		return NULL;	
	board->fblock = rp;

	/* Dump the format block for debugging purposes */
	if (console_loglevel >= 10) {
		int i;
		printk(KERN_DEBUG "Slot %X, format block at 0x%p\n",
		       slot, rp);
		printk(KERN_DEBUG "Format block: ");
		for (i = 0; i < FORMAT_BLOCK_SIZE; i += 4) {
			unsigned short foo, bar;
			foo = nubus_get_rom(&rp, 2, bytelanes);
			bar = nubus_get_rom(&rp, 2, bytelanes);
			printk("%04x %04x  ", foo, bar);
		}
		printk("\n");
		rp = board->fblock;
	}
	
	board->slot = slot;
	board->slot_addr = (unsigned long) nubus_slot_addr(slot);
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
	board->format = nubus_get_rom(&rp,1, bytelanes);
	board->lanes = bytelanes;

	/* Directory offset should be small and negative... */
	if(!(board->doffset & 0x00FF0000))
		printk(KERN_WARNING "Dodgy doffset!\n");
	dpat = nubus_get_rom(&rp, 4, bytelanes);
	if(dpat != NUBUS_TEST_PATTERN)
		printk(KERN_WARNING "Wrong test pattern %08lx!\n", dpat);
		
	/*
	 *	I wonder how the CRC is meant to work -
	 *		any takers ?
	 * CSA: According to MAC docs, not all cards pass the CRC anyway,
	 * since the initial Macintosh ROM releases skipped the check.
	 */

	/* Attempt to work around slot zero weirdness */
	nubus_find_rom_dir(board);
	nubus_get_root_dir(board, &dir);	

	/* We're ready to rock */
	printk(KERN_INFO "Slot %X:\n", slot);

	/* Each slot should have one board resource and any number of
	   functional resources.  So we'll fill in some fields in the
	   struct nubus_board from the board resource, then walk down
	   the list of functional resources, spinning out a nubus_dev
	   for each of them. */
	if (nubus_readdir(&dir, &ent) == -1) {
		/* We can't have this! */
		printk(KERN_ERR "Board resource not found!\n");
		return NULL;
	} else {
		printk(KERN_INFO "  Board resource:\n");
		nubus_get_board_resource(board, slot, &ent);
	}

	/* Aaaarrrrgghh!  The LC III motherboard has *two* board
	   resources.  I have no idea WTF to do about this. */

	while (nubus_readdir(&dir, &ent) != -1) {
		struct nubus_dev*  dev;
		struct nubus_dev** devp;
		dev = nubus_get_functional_resource(board, slot, &ent);
		if (dev == NULL)
			continue;

		/* We zeroed this out above */
		if (board->first_dev == NULL)
			board->first_dev = dev;
		
		/* Put it on the global NuBus device chain. Keep entries in order. */
		for (devp=&nubus_devices; *devp!=NULL; devp=&((*devp)->next))
			/* spin */;
		*devp = dev;
		dev->next = NULL;		
	}

	/* Put it on the global NuBus board chain. Keep entries in order. */
	for (boardp=&nubus_boards; *boardp!=NULL; boardp=&((*boardp)->next))
		/* spin */;
	*boardp = board;
	board->next = NULL;
	
	return board;
}

void __init nubus_probe_slot(int slot)
{
	unsigned char dp;
	unsigned char* rp;
	int i;

	rp = nubus_rom_addr(slot);	
	for(i = 4; i; i--)
	{
		unsigned long flags;
		int card_present;

		rp--;
		local_irq_save(flags);
		card_present = hwreg_present(rp);
		local_irq_restore(flags);
	       
		if (!card_present)
			continue;

		printk(KERN_DEBUG "Now probing slot %X at %p\n", slot, rp);
		dp = *rp;
		if(dp == 0)
			continue;

		/* The last byte of the format block consists of two
		   nybbles which are "mirror images" of each other.
		   These show us the valid bytelanes */
		if ((((dp>>4) ^ dp) & 0x0F) != 0x0F)
			continue;
		/* Check that this value is actually *on* one of the
		   bytelanes it claims are valid! */
		if ((dp & 0x0F) >= (1<<i))
			continue;

		/* Looks promising.  Let's put it on the list. */
		nubus_add_board(slot, dp);

		return;
	}
}

#if defined(CONFIG_PROC_FS)

/* /proc/nubus stuff */

static int sprint_nubus_board(struct nubus_board* board, char* ptr, int len)
{
	if(len < 100)
		return -1;
	
	sprintf(ptr, "Slot %X: %s\n",
		board->slot, board->name);
	
	return strlen(ptr);
}

static int nubus_read_proc(char *page, char **start, off_t off,
				int count, int *eof, void *data)
{
	int nprinted, len, begin = 0;
	int size = PAGE_SIZE;
	struct nubus_board* board;
	
	len   = sprintf(page, "Nubus devices found:\n");
	/* Walk the list of NuBus boards */
	for (board = nubus_boards; board != NULL; board = board->next)
	{
		nprinted = sprint_nubus_board(board, page + len, size - len);
		if (nprinted < 0)
			break;
		len += nprinted;
		if (len+begin < off) {
			begin += len;
			len = 0;
		}
		if (len+begin >= off+count)
			break;
	}
	if (len+begin < off)
		*eof = 1;
	off -= begin;
	*start = page + off;
	len -= off;
	if (len>count)
		len = count;
	if (len<0)
		len = 0;
	return len;
}
#endif

void __init nubus_scan_bus(void)
{
	int slot;
	/* This might not work on your machine */
#ifdef I_WANT_TO_PROBE_SLOT_ZERO
	nubus_probe_slot(0);
#endif
	for(slot = 9; slot < 15; slot++)
	{
		nubus_probe_slot(slot);
	}
}

static int __init nubus_init(void)
{
	if (!MACH_IS_MAC) 
		return 0;

	/* Initialize the NuBus interrupts */
	if (oss_present) {
		oss_nubus_init();
	} else {
		via_nubus_init();
	}

#ifdef TRY_TO_DODGE_WSOD
	/* Rogue Ethernet interrupts can kill the machine if we don't
	   do this.  Obviously this is bogus.  Hopefully the local VIA
	   gurus can fix the real cause of the problem. */
	mdelay(1000);
#endif
	
	/* And probe */
	printk("NuBus: Scanning NuBus slots.\n");
	nubus_devices = NULL;
	nubus_boards  = NULL;
	nubus_scan_bus();

#ifdef CONFIG_PROC_FS
	create_proc_read_entry("nubus", 0, NULL, nubus_read_proc, NULL);
	nubus_proc_init();
#endif
	return 0;
}

subsys_initcall(nubus_init);
