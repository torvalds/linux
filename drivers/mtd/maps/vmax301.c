/* ######################################################################

   Tempustech VMAX SBC301 MTD Driver.

   The VMAx 301 is a SBC based on . It
   comes with three builtin AMD 29F016B flash chips and a socket for SRAM or
   more flash. Each unit has it's own 8k mapping into a settable region
   (0xD8000). There are two 8k mappings for each MTD, the first is always set
   to the lower 8k of the device the second is paged. Writing a 16 bit page
   value to anywhere in the first 8k will cause the second 8k to page around.

   To boot the device a bios extension must be installed into the first 8k
   of flash that is smart enough to copy itself down, page in the rest of
   itself and begin executing.

   ##################################################################### */

#include <linux/module.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <asm/io.h>

#include <linux/mtd/map.h>
#include <linux/mtd/mtd.h>


#define WINDOW_START 0xd8000
#define WINDOW_LENGTH 0x2000
#define WINDOW_SHIFT 25
#define WINDOW_MASK 0x1FFF

/* Actually we could use two spinlocks, but we'd have to have
   more private space in the struct map_info. We lose a little
   performance like this, but we'd probably lose more by having
   the extra indirection from having one of the map->map_priv
   fields pointing to yet another private struct.
*/
static DEFINE_SPINLOCK(vmax301_spin);

static void __vmax301_page(struct map_info *map, unsigned long page)
{
	writew(page, map->map_priv_2 - WINDOW_LENGTH);
	map->map_priv_1 = page;
}

static inline void vmax301_page(struct map_info *map,
				  unsigned long ofs)
{
	unsigned long page = (ofs >> WINDOW_SHIFT);
	if (map->map_priv_1 != page)
		__vmax301_page(map, page);
}

static map_word vmax301_read8(struct map_info *map, unsigned long ofs)
{
	map_word ret;
	spin_lock(&vmax301_spin);
	vmax301_page(map, ofs);
	ret.x[0] = readb(map->map_priv_2 + (ofs & WINDOW_MASK));
	spin_unlock(&vmax301_spin);
	return ret;
}

static void vmax301_copy_from(struct map_info *map, void *to, unsigned long from, ssize_t len)
{
	while(len) {
		unsigned long thislen = len;
		if (len > (WINDOW_LENGTH - (from & WINDOW_MASK)))
			thislen = WINDOW_LENGTH-(from & WINDOW_MASK);
		spin_lock(&vmax301_spin);
		vmax301_page(map, from);
		memcpy_fromio(to, map->map_priv_2 + from, thislen);
		spin_unlock(&vmax301_spin);
		to += thislen;
		from += thislen;
		len -= thislen;
	}
}

static void vmax301_write8(struct map_info *map, map_word d, unsigned long adr)
{
	spin_lock(&vmax301_spin);
	vmax301_page(map, adr);
	writeb(d.x[0], map->map_priv_2 + (adr & WINDOW_MASK));
	spin_unlock(&vmax301_spin);
}

static void vmax301_copy_to(struct map_info *map, unsigned long to, const void *from, ssize_t len)
{
	while(len) {
		unsigned long thislen = len;
		if (len > (WINDOW_LENGTH - (to & WINDOW_MASK)))
			thislen = WINDOW_LENGTH-(to & WINDOW_MASK);

		spin_lock(&vmax301_spin);
		vmax301_page(map, to);
		memcpy_toio(map->map_priv_2 + to, from, thislen);
		spin_unlock(&vmax301_spin);
		to += thislen;
		from += thislen;
		len -= thislen;
	}
}

static struct map_info vmax_map[2] = {
	{
		.name = "VMAX301 Internal Flash",
		.phys = NO_XIP,
		.size = 3*2*1024*1024,
		.bankwidth = 1,
		.read = vmax301_read8,
		.copy_from = vmax301_copy_from,
		.write = vmax301_write8,
		.copy_to = vmax301_copy_to,
		.map_priv_1 = WINDOW_START + WINDOW_LENGTH,
		.map_priv_2 = 0xFFFFFFFF
	},
	{
		.name = "VMAX301 Socket",
		.phys = NO_XIP,
		.size = 0,
		.bankwidth = 1,
		.read = vmax301_read8,
		.copy_from = vmax301_copy_from,
		.write = vmax301_write8,
		.copy_to = vmax301_copy_to,
		.map_priv_1 = WINDOW_START + (3*WINDOW_LENGTH),
		.map_priv_2 = 0xFFFFFFFF
	}
};

static struct mtd_info *vmax_mtd[2] = {NULL, NULL};

static void __exit cleanup_vmax301(void)
{
	int i;

	for (i=0; i<2; i++) {
		if (vmax_mtd[i]) {
			del_mtd_device(vmax_mtd[i]);
			map_destroy(vmax_mtd[i]);
		}
	}
	iounmap((void *)vmax_map[0].map_priv_1 - WINDOW_START);
}

static int __init init_vmax301(void)
{
	int i;
	unsigned long iomapadr;
	// Print out our little header..
	printk("Tempustech VMAX 301 MEM:0x%x-0x%x\n",WINDOW_START,
	       WINDOW_START+4*WINDOW_LENGTH);

	iomapadr = (unsigned long)ioremap(WINDOW_START, WINDOW_LENGTH*4);
	if (!iomapadr) {
		printk("Failed to ioremap memory region\n");
		return -EIO;
	}
	/* Put the address in the map's private data area.
	   We store the actual MTD IO address rather than the
	   address of the first half, because it's used more
	   often.
	*/
	vmax_map[0].map_priv_2 = iomapadr + WINDOW_START;
	vmax_map[1].map_priv_2 = iomapadr + (3*WINDOW_START);

	for (i=0; i<2; i++) {
		vmax_mtd[i] = do_map_probe("cfi_probe", &vmax_map[i]);
		if (!vmax_mtd[i])
			vmax_mtd[i] = do_map_probe("jedec", &vmax_map[i]);
		if (!vmax_mtd[i])
			vmax_mtd[i] = do_map_probe("map_ram", &vmax_map[i]);
		if (!vmax_mtd[i])
			vmax_mtd[i] = do_map_probe("map_rom", &vmax_map[i]);
		if (vmax_mtd[i]) {
			vmax_mtd[i]->owner = THIS_MODULE;
			add_mtd_device(vmax_mtd[i]);
		}
	}

	if (!vmax_mtd[0] && !vmax_mtd[1]) {
		iounmap((void *)iomapadr);
		return -ENXIO;
	}

	return 0;
}

module_init(init_vmax301);
module_exit(cleanup_vmax301);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Woodhouse <dwmw2@infradead.org>");
MODULE_DESCRIPTION("MTD map driver for Tempustech VMAX SBC301 board");
