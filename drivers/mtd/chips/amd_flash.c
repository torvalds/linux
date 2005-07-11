/*
 * MTD map driver for AMD compatible flash chips (non-CFI)
 *
 * Author: Jonas Holmberg <jonas.holmberg@axis.com>
 *
 * $Id: amd_flash.c,v 1.27 2005/02/04 07:43:09 jonashg Exp $
 *
 * Copyright (c) 2001 Axis Communications AB
 *
 * This file is under GPL.
 *
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/mtd/map.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/flashchip.h>

/* There's no limit. It exists only to avoid realloc. */
#define MAX_AMD_CHIPS 8

#define DEVICE_TYPE_X8	(8 / 8)
#define DEVICE_TYPE_X16	(16 / 8)
#define DEVICE_TYPE_X32	(32 / 8)

/* Addresses */
#define ADDR_MANUFACTURER		0x0000
#define ADDR_DEVICE_ID			0x0001
#define ADDR_SECTOR_LOCK		0x0002
#define ADDR_HANDSHAKE			0x0003
#define ADDR_UNLOCK_1			0x0555
#define ADDR_UNLOCK_2			0x02AA

/* Commands */
#define CMD_UNLOCK_DATA_1		0x00AA
#define CMD_UNLOCK_DATA_2		0x0055
#define CMD_MANUFACTURER_UNLOCK_DATA	0x0090
#define CMD_UNLOCK_BYPASS_MODE		0x0020
#define CMD_PROGRAM_UNLOCK_DATA		0x00A0
#define CMD_RESET_DATA			0x00F0
#define CMD_SECTOR_ERASE_UNLOCK_DATA	0x0080
#define CMD_SECTOR_ERASE_UNLOCK_DATA_2	0x0030

#define CMD_UNLOCK_SECTOR		0x0060

/* Manufacturers */
#define MANUFACTURER_AMD	0x0001
#define MANUFACTURER_ATMEL	0x001F
#define MANUFACTURER_FUJITSU	0x0004
#define MANUFACTURER_ST		0x0020
#define MANUFACTURER_SST	0x00BF
#define MANUFACTURER_TOSHIBA	0x0098

/* AMD */
#define AM29F800BB	0x2258
#define AM29F800BT	0x22D6
#define AM29LV800BB	0x225B
#define AM29LV800BT	0x22DA
#define AM29LV160DT	0x22C4
#define AM29LV160DB	0x2249
#define AM29BDS323D     0x22D1

/* Atmel */
#define AT49xV16x	0x00C0
#define AT49xV16xT	0x00C2

/* Fujitsu */
#define MBM29LV160TE	0x22C4
#define MBM29LV160BE	0x2249
#define MBM29LV800BB	0x225B

/* ST - www.st.com */
#define M29W800T	0x00D7
#define M29W160DT	0x22C4
#define M29W160DB	0x2249

/* SST */
#define SST39LF800	0x2781
#define SST39LF160	0x2782

/* Toshiba */
#define TC58FVT160	0x00C2
#define TC58FVB160	0x0043

#define D6_MASK	0x40

struct amd_flash_private {
	int device_type;	
	int interleave;	
	int numchips;	
	unsigned long chipshift;
//	const char *im_name;
	struct flchip chips[0];
};

struct amd_flash_info {
	const __u16 mfr_id;
	const __u16 dev_id;
	const char *name;
	const u_long size;
	const int numeraseregions;
	const struct mtd_erase_region_info regions[4];
};



static int amd_flash_read(struct mtd_info *, loff_t, size_t, size_t *,
			  u_char *);
static int amd_flash_write(struct mtd_info *, loff_t, size_t, size_t *,
			   const u_char *);
static int amd_flash_erase(struct mtd_info *, struct erase_info *);
static void amd_flash_sync(struct mtd_info *);
static int amd_flash_suspend(struct mtd_info *);
static void amd_flash_resume(struct mtd_info *);
static void amd_flash_destroy(struct mtd_info *);
static struct mtd_info *amd_flash_probe(struct map_info *map);


static struct mtd_chip_driver amd_flash_chipdrv = {
	.probe = amd_flash_probe,
	.destroy = amd_flash_destroy,
	.name = "amd_flash",
	.module = THIS_MODULE
};



static const char im_name[] = "amd_flash";



static inline __u32 wide_read(struct map_info *map, __u32 addr)
{
	if (map->buswidth == 1) {
		return map_read8(map, addr);
	} else if (map->buswidth == 2) {
		return map_read16(map, addr);
	} else if (map->buswidth == 4) {
		return map_read32(map, addr);
        }

	return 0;
}

static inline void wide_write(struct map_info *map, __u32 val, __u32 addr)
{
	if (map->buswidth == 1) {
		map_write8(map, val, addr);
	} else if (map->buswidth == 2) {
		map_write16(map, val, addr);
	} else if (map->buswidth == 4) {
		map_write32(map, val, addr);
	}
}

static inline __u32 make_cmd(struct map_info *map, __u32 cmd)
{
	const struct amd_flash_private *private = map->fldrv_priv;
	if ((private->interleave == 2) &&
	    (private->device_type == DEVICE_TYPE_X16)) {
		cmd |= (cmd << 16);
	}

	return cmd;
}

static inline void send_unlock(struct map_info *map, unsigned long base)
{
	wide_write(map, (CMD_UNLOCK_DATA_1 << 16) | CMD_UNLOCK_DATA_1,
		   base + (map->buswidth * ADDR_UNLOCK_1));
	wide_write(map, (CMD_UNLOCK_DATA_2 << 16) | CMD_UNLOCK_DATA_2,
		   base + (map->buswidth * ADDR_UNLOCK_2));
}

static inline void send_cmd(struct map_info *map, unsigned long base, __u32 cmd)
{
	send_unlock(map, base);
	wide_write(map, make_cmd(map, cmd),
		   base + (map->buswidth * ADDR_UNLOCK_1));
}

static inline void send_cmd_to_addr(struct map_info *map, unsigned long base,
				    __u32 cmd, unsigned long addr)
{
	send_unlock(map, base);
	wide_write(map, make_cmd(map, cmd), addr);
}

static inline int flash_is_busy(struct map_info *map, unsigned long addr,
				int interleave)
{

	if ((interleave == 2) && (map->buswidth == 4)) {
		__u32 read1, read2;

		read1 = wide_read(map, addr);
		read2 = wide_read(map, addr);

		return (((read1 >> 16) & D6_MASK) !=
			((read2 >> 16) & D6_MASK)) ||
		       (((read1 & 0xffff) & D6_MASK) !=
			((read2 & 0xffff) & D6_MASK));
	}

	return ((wide_read(map, addr) & D6_MASK) !=
		(wide_read(map, addr) & D6_MASK));
}

static inline void unlock_sector(struct map_info *map, unsigned long sect_addr,
				 int unlock)
{
	/* Sector lock address. A6 = 1 for unlock, A6 = 0 for lock */
	int SLA = unlock ?
		(sect_addr |  (0x40 * map->buswidth)) :
		(sect_addr & ~(0x40 * map->buswidth)) ;

	__u32 cmd = make_cmd(map, CMD_UNLOCK_SECTOR);

	wide_write(map, make_cmd(map, CMD_RESET_DATA), 0);
	wide_write(map, cmd, SLA); /* 1st cycle: write cmd to any address */
	wide_write(map, cmd, SLA); /* 2nd cycle: write cmd to any address */
	wide_write(map, cmd, SLA); /* 3rd cycle: write cmd to SLA */
}

static inline int is_sector_locked(struct map_info *map,
				   unsigned long sect_addr)
{
	int status;

	wide_write(map, CMD_RESET_DATA, 0);
	send_cmd(map, sect_addr, CMD_MANUFACTURER_UNLOCK_DATA);

	/* status is 0x0000 for unlocked and 0x0001 for locked */
	status = wide_read(map, sect_addr + (map->buswidth * ADDR_SECTOR_LOCK));
	wide_write(map, CMD_RESET_DATA, 0);
	return status;
}

static int amd_flash_do_unlock(struct mtd_info *mtd, loff_t ofs, size_t len,
			       int is_unlock)
{
	struct map_info *map;
	struct mtd_erase_region_info *merip;
	int eraseoffset, erasesize, eraseblocks;
	int i;
	int retval = 0;
	int lock_status;
      
	map = mtd->priv;

	/* Pass the whole chip through sector by sector and check for each
	   sector if the sector and the given interval overlap */
	for(i = 0; i < mtd->numeraseregions; i++) {
		merip = &mtd->eraseregions[i];

		eraseoffset = merip->offset;
		erasesize = merip->erasesize;
		eraseblocks = merip->numblocks;

		if (ofs > eraseoffset + erasesize)
			continue;

		while (eraseblocks > 0) {
			if (ofs < eraseoffset + erasesize && ofs + len > eraseoffset) {
				unlock_sector(map, eraseoffset, is_unlock);

				lock_status = is_sector_locked(map, eraseoffset);
				
				if (is_unlock && lock_status) {
					printk("Cannot unlock sector at address %x length %xx\n",
					       eraseoffset, merip->erasesize);
					retval = -1;
				} else if (!is_unlock && !lock_status) {
					printk("Cannot lock sector at address %x length %x\n",
					       eraseoffset, merip->erasesize);
					retval = -1;
				}
			}
			eraseoffset += erasesize;
			eraseblocks --;
		}
	}
	return retval;
}

static int amd_flash_unlock(struct mtd_info *mtd, loff_t ofs, size_t len)
{
	return amd_flash_do_unlock(mtd, ofs, len, 1);
}

static int amd_flash_lock(struct mtd_info *mtd, loff_t ofs, size_t len)
{
	return amd_flash_do_unlock(mtd, ofs, len, 0);
}


/*
 * Reads JEDEC manufacturer ID and device ID and returns the index of the first
 * matching table entry (-1 if not found or alias for already found chip).
 */ 
static int probe_new_chip(struct mtd_info *mtd, __u32 base,
			  struct flchip *chips,
			  struct amd_flash_private *private,
			  const struct amd_flash_info *table, int table_size)
{
	__u32 mfr_id;
	__u32 dev_id;
	struct map_info *map = mtd->priv;
	struct amd_flash_private temp;
	int i;

	temp.device_type = DEVICE_TYPE_X16;	// Assume X16 (FIXME)
	temp.interleave = 2;
	map->fldrv_priv = &temp;

	/* Enter autoselect mode. */
	send_cmd(map, base, CMD_RESET_DATA);
	send_cmd(map, base, CMD_MANUFACTURER_UNLOCK_DATA);

	mfr_id = wide_read(map, base + (map->buswidth * ADDR_MANUFACTURER));
	dev_id = wide_read(map, base + (map->buswidth * ADDR_DEVICE_ID));

	if ((map->buswidth == 4) && ((mfr_id >> 16) == (mfr_id & 0xffff)) &&
	    ((dev_id >> 16) == (dev_id & 0xffff))) {
		mfr_id &= 0xffff;
		dev_id &= 0xffff;
	} else {
		temp.interleave = 1;
	}

	for (i = 0; i < table_size; i++) {
		if ((mfr_id == table[i].mfr_id) &&
		    (dev_id == table[i].dev_id)) {
			if (chips) {
				int j;

				/* Is this an alias for an already found chip?
				 * In that case that chip should be in
				 * autoselect mode now.
				 */
				for (j = 0; j < private->numchips; j++) {
					__u32 mfr_id_other;
					__u32 dev_id_other;

					mfr_id_other =
						wide_read(map, chips[j].start +
							       (map->buswidth *
								ADDR_MANUFACTURER
							       ));
					dev_id_other =
						wide_read(map, chips[j].start +
					    		       (map->buswidth *
							        ADDR_DEVICE_ID));
					if (temp.interleave == 2) {
						mfr_id_other &= 0xffff;
						dev_id_other &= 0xffff;
					}
					if ((mfr_id_other == mfr_id) &&
					    (dev_id_other == dev_id)) {

						/* Exit autoselect mode. */
						send_cmd(map, base,
							 CMD_RESET_DATA);

						return -1;
					}
				}

				if (private->numchips == MAX_AMD_CHIPS) {
					printk(KERN_WARNING
					       "%s: Too many flash chips "
					       "detected. Increase "
					       "MAX_AMD_CHIPS from %d.\n",
					       map->name, MAX_AMD_CHIPS);

					return -1;
				}

				chips[private->numchips].start = base;
				chips[private->numchips].state = FL_READY;
				chips[private->numchips].mutex =
					&chips[private->numchips]._spinlock;
				private->numchips++;
			}

			printk("%s: Found %d x %ldMiB %s at 0x%x\n", map->name,
			       temp.interleave, (table[i].size)/(1024*1024),
			       table[i].name, base);

			mtd->size += table[i].size * temp.interleave;
			mtd->numeraseregions += table[i].numeraseregions;

			break;
		}
	}

	/* Exit autoselect mode. */
	send_cmd(map, base, CMD_RESET_DATA);

	if (i == table_size) {
		printk(KERN_DEBUG "%s: unknown flash device at 0x%x, "
		       "mfr id 0x%x, dev id 0x%x\n", map->name,
		       base, mfr_id, dev_id);
		map->fldrv_priv = NULL;

		return -1;
	}

	private->device_type = temp.device_type;
	private->interleave = temp.interleave;

	return i;
}



static struct mtd_info *amd_flash_probe(struct map_info *map)
{
	static const struct amd_flash_info table[] = {
	{
		.mfr_id = MANUFACTURER_AMD,
		.dev_id = AM29LV160DT,
		.name = "AMD AM29LV160DT",
		.size = 0x00200000,
		.numeraseregions = 4,
		.regions = {
			{ .offset = 0x000000, .erasesize = 0x10000, .numblocks = 31 },
			{ .offset = 0x1F0000, .erasesize = 0x08000, .numblocks =  1 },
			{ .offset = 0x1F8000, .erasesize = 0x02000, .numblocks =  2 },
			{ .offset = 0x1FC000, .erasesize = 0x04000, .numblocks =  1 }
		}
	}, {
		.mfr_id = MANUFACTURER_AMD,
		.dev_id = AM29LV160DB,
		.name = "AMD AM29LV160DB",
		.size = 0x00200000,
		.numeraseregions = 4,
		.regions = {
			{ .offset = 0x000000, .erasesize = 0x04000, .numblocks =  1 },
			{ .offset = 0x004000, .erasesize = 0x02000, .numblocks =  2 },
			{ .offset = 0x008000, .erasesize = 0x08000, .numblocks =  1 },
			{ .offset = 0x010000, .erasesize = 0x10000, .numblocks = 31 }
		}
	}, {
		.mfr_id = MANUFACTURER_TOSHIBA,
		.dev_id = TC58FVT160,
		.name = "Toshiba TC58FVT160",
		.size = 0x00200000,
		.numeraseregions = 4,
		.regions = {
			{ .offset = 0x000000, .erasesize = 0x10000, .numblocks = 31 },
			{ .offset = 0x1F0000, .erasesize = 0x08000, .numblocks =  1 },
			{ .offset = 0x1F8000, .erasesize = 0x02000, .numblocks =  2 },
			{ .offset = 0x1FC000, .erasesize = 0x04000, .numblocks =  1 }
		}
	}, {
		.mfr_id = MANUFACTURER_FUJITSU,
		.dev_id = MBM29LV160TE,
		.name = "Fujitsu MBM29LV160TE",
		.size = 0x00200000,
		.numeraseregions = 4,
		.regions = {
			{ .offset = 0x000000, .erasesize = 0x10000, .numblocks = 31 },
			{ .offset = 0x1F0000, .erasesize = 0x08000, .numblocks =  1 },
			{ .offset = 0x1F8000, .erasesize = 0x02000, .numblocks =  2 },
			{ .offset = 0x1FC000, .erasesize = 0x04000, .numblocks =  1 }
		}
	}, {
		.mfr_id = MANUFACTURER_TOSHIBA,
		.dev_id = TC58FVB160,
		.name = "Toshiba TC58FVB160",
		.size = 0x00200000,
		.numeraseregions = 4,
		.regions = {
			{ .offset = 0x000000, .erasesize = 0x04000, .numblocks =  1 },
			{ .offset = 0x004000, .erasesize = 0x02000, .numblocks =  2 },
			{ .offset = 0x008000, .erasesize = 0x08000, .numblocks =  1 },
			{ .offset = 0x010000, .erasesize = 0x10000, .numblocks = 31 }
		}
	}, {
		.mfr_id = MANUFACTURER_FUJITSU,
		.dev_id = MBM29LV160BE,
		.name = "Fujitsu MBM29LV160BE",
		.size = 0x00200000,
		.numeraseregions = 4,
		.regions = {
			{ .offset = 0x000000, .erasesize = 0x04000, .numblocks =  1 },
			{ .offset = 0x004000, .erasesize = 0x02000, .numblocks =  2 },
			{ .offset = 0x008000, .erasesize = 0x08000, .numblocks =  1 },
			{ .offset = 0x010000, .erasesize = 0x10000, .numblocks = 31 }
		}
	}, {
		.mfr_id = MANUFACTURER_AMD,
		.dev_id = AM29LV800BB,
		.name = "AMD AM29LV800BB",
		.size = 0x00100000,
		.numeraseregions = 4,
		.regions = {
			{ .offset = 0x000000, .erasesize = 0x04000, .numblocks =  1 },
			{ .offset = 0x004000, .erasesize = 0x02000, .numblocks =  2 },
			{ .offset = 0x008000, .erasesize = 0x08000, .numblocks =  1 },
			{ .offset = 0x010000, .erasesize = 0x10000, .numblocks = 15 }
		}
	}, {
		.mfr_id = MANUFACTURER_AMD,
		.dev_id = AM29F800BB,
		.name = "AMD AM29F800BB",
		.size = 0x00100000,
		.numeraseregions = 4,
		.regions = {
			{ .offset = 0x000000, .erasesize = 0x04000, .numblocks =  1 },
			{ .offset = 0x004000, .erasesize = 0x02000, .numblocks =  2 },
			{ .offset = 0x008000, .erasesize = 0x08000, .numblocks =  1 },
			{ .offset = 0x010000, .erasesize = 0x10000, .numblocks = 15 }
		}
	}, {
		.mfr_id = MANUFACTURER_AMD,
		.dev_id = AM29LV800BT,
		.name = "AMD AM29LV800BT",
		.size = 0x00100000,
		.numeraseregions = 4,
		.regions = {
			{ .offset = 0x000000, .erasesize = 0x10000, .numblocks = 15 },
			{ .offset = 0x0F0000, .erasesize = 0x08000, .numblocks =  1 },
			{ .offset = 0x0F8000, .erasesize = 0x02000, .numblocks =  2 },
			{ .offset = 0x0FC000, .erasesize = 0x04000, .numblocks =  1 }
		}
	}, {
		.mfr_id = MANUFACTURER_AMD,
		.dev_id = AM29F800BT,
		.name = "AMD AM29F800BT",
		.size = 0x00100000,
		.numeraseregions = 4,
		.regions = {
			{ .offset = 0x000000, .erasesize = 0x10000, .numblocks = 15 },
			{ .offset = 0x0F0000, .erasesize = 0x08000, .numblocks =  1 },
			{ .offset = 0x0F8000, .erasesize = 0x02000, .numblocks =  2 },
			{ .offset = 0x0FC000, .erasesize = 0x04000, .numblocks =  1 }
		}
	}, {
		.mfr_id = MANUFACTURER_AMD,
		.dev_id = AM29LV800BB,
		.name = "AMD AM29LV800BB",
		.size = 0x00100000,
		.numeraseregions = 4,
		.regions = {
			{ .offset = 0x000000, .erasesize = 0x10000, .numblocks = 15 },
			{ .offset = 0x0F0000, .erasesize = 0x08000, .numblocks =  1 },
			{ .offset = 0x0F8000, .erasesize = 0x02000, .numblocks =  2 },
			{ .offset = 0x0FC000, .erasesize = 0x04000, .numblocks =  1 }
		}
	}, {
		.mfr_id = MANUFACTURER_FUJITSU,
		.dev_id = MBM29LV800BB,
		.name = "Fujitsu MBM29LV800BB",
		.size = 0x00100000,
		.numeraseregions = 4,
		.regions = {
			{ .offset = 0x000000, .erasesize = 0x04000, .numblocks =  1 },
			{ .offset = 0x004000, .erasesize = 0x02000, .numblocks =  2 },
			{ .offset = 0x008000, .erasesize = 0x08000, .numblocks =  1 },
			{ .offset = 0x010000, .erasesize = 0x10000, .numblocks = 15 }
		}
	}, {
		.mfr_id = MANUFACTURER_ST,
		.dev_id = M29W800T,
		.name = "ST M29W800T",
		.size = 0x00100000,
		.numeraseregions = 4,
		.regions = {
			{ .offset = 0x000000, .erasesize = 0x10000, .numblocks = 15 },
			{ .offset = 0x0F0000, .erasesize = 0x08000, .numblocks =  1 },
			{ .offset = 0x0F8000, .erasesize = 0x02000, .numblocks =  2 },
			{ .offset = 0x0FC000, .erasesize = 0x04000, .numblocks =  1 }
		}
	}, {
		.mfr_id = MANUFACTURER_ST,
		.dev_id = M29W160DT,
		.name = "ST M29W160DT",
		.size = 0x00200000,
		.numeraseregions = 4,
		.regions = {
			{ .offset = 0x000000, .erasesize = 0x10000, .numblocks = 31 },
			{ .offset = 0x1F0000, .erasesize = 0x08000, .numblocks =  1 },
			{ .offset = 0x1F8000, .erasesize = 0x02000, .numblocks =  2 },
			{ .offset = 0x1FC000, .erasesize = 0x04000, .numblocks =  1 }
		}
	}, {
		.mfr_id = MANUFACTURER_ST,
		.dev_id = M29W160DB,
		.name = "ST M29W160DB",
		.size = 0x00200000,
		.numeraseregions = 4,
		.regions = {
			{ .offset = 0x000000, .erasesize = 0x04000, .numblocks =  1 },
			{ .offset = 0x004000, .erasesize = 0x02000, .numblocks =  2 },
			{ .offset = 0x008000, .erasesize = 0x08000, .numblocks =  1 },
			{ .offset = 0x010000, .erasesize = 0x10000, .numblocks = 31 }
		}
	}, {
		.mfr_id = MANUFACTURER_AMD,
		.dev_id = AM29BDS323D,
		.name = "AMD AM29BDS323D",
		.size = 0x00400000,
		.numeraseregions = 3,
		.regions = {
			{ .offset = 0x000000, .erasesize = 0x10000, .numblocks = 48 },
			{ .offset = 0x300000, .erasesize = 0x10000, .numblocks = 15 },
			{ .offset = 0x3f0000, .erasesize = 0x02000, .numblocks =  8 },
		}
	}, {
		.mfr_id = MANUFACTURER_ATMEL,
		.dev_id = AT49xV16x,
		.name = "Atmel AT49xV16x",
		.size = 0x00200000,
		.numeraseregions = 2,
		.regions = {
			{ .offset = 0x000000, .erasesize = 0x02000, .numblocks =  8 },
			{ .offset = 0x010000, .erasesize = 0x10000, .numblocks = 31 }
		}
	}, {
		.mfr_id = MANUFACTURER_ATMEL,
		.dev_id = AT49xV16xT,
		.name = "Atmel AT49xV16xT",
		.size = 0x00200000,
		.numeraseregions = 2,
		.regions = {
			{ .offset = 0x000000, .erasesize = 0x10000, .numblocks = 31 },
			{ .offset = 0x1F0000, .erasesize = 0x02000, .numblocks =  8 }
		}
	} 
	};

	struct mtd_info *mtd;
	struct flchip chips[MAX_AMD_CHIPS];
	int table_pos[MAX_AMD_CHIPS];
	struct amd_flash_private temp;
	struct amd_flash_private *private;
	u_long size;
	unsigned long base;
	int i;
	int reg_idx;
	int offset;

	mtd = (struct mtd_info*)kmalloc(sizeof(*mtd), GFP_KERNEL);
	if (!mtd) {
		printk(KERN_WARNING
		       "%s: kmalloc failed for info structure\n", map->name);
		return NULL;
	}
	memset(mtd, 0, sizeof(*mtd));
	mtd->priv = map;

	memset(&temp, 0, sizeof(temp));

	printk("%s: Probing for AMD compatible flash...\n", map->name);

	if ((table_pos[0] = probe_new_chip(mtd, 0, NULL, &temp, table,
					   sizeof(table)/sizeof(table[0])))
	    == -1) {
		printk(KERN_WARNING
		       "%s: Found no AMD compatible device at location zero\n",
		       map->name);
		kfree(mtd);

		return NULL;
	}

	chips[0].start = 0;
	chips[0].state = FL_READY;
	chips[0].mutex = &chips[0]._spinlock;
	temp.numchips = 1;
	for (size = mtd->size; size > 1; size >>= 1) {
		temp.chipshift++;
	}
	switch (temp.interleave) {
		case 2:
			temp.chipshift += 1;
			break;
		case 4:
			temp.chipshift += 2;
			break;
	}

	/* Find out if there are any more chips in the map. */
	for (base = (1 << temp.chipshift);
	     base < map->size;
	     base += (1 << temp.chipshift)) {
	     	int numchips = temp.numchips;
		table_pos[numchips] = probe_new_chip(mtd, base, chips,
			&temp, table, sizeof(table)/sizeof(table[0]));
	}

	mtd->eraseregions = kmalloc(sizeof(struct mtd_erase_region_info) *
				    mtd->numeraseregions, GFP_KERNEL);
	if (!mtd->eraseregions) { 
		printk(KERN_WARNING "%s: Failed to allocate "
		       "memory for MTD erase region info\n", map->name);
		kfree(mtd);
		map->fldrv_priv = NULL;
		return NULL;
	}

	reg_idx = 0;
	offset = 0;
	for (i = 0; i < temp.numchips; i++) {
		int dev_size;
		int j;

		dev_size = 0;
		for (j = 0; j < table[table_pos[i]].numeraseregions; j++) {
			mtd->eraseregions[reg_idx].offset = offset +
				(table[table_pos[i]].regions[j].offset *
				 temp.interleave);
			mtd->eraseregions[reg_idx].erasesize =
				table[table_pos[i]].regions[j].erasesize *
				temp.interleave;
			mtd->eraseregions[reg_idx].numblocks =
				table[table_pos[i]].regions[j].numblocks;
			if (mtd->erasesize <
			    mtd->eraseregions[reg_idx].erasesize) {
				mtd->erasesize =
					mtd->eraseregions[reg_idx].erasesize;
			}
			dev_size += mtd->eraseregions[reg_idx].erasesize *
				    mtd->eraseregions[reg_idx].numblocks;
			reg_idx++;
		}
		offset += dev_size;
	}
	mtd->type = MTD_NORFLASH;
	mtd->flags = MTD_CAP_NORFLASH;
	mtd->name = map->name;
	mtd->erase = amd_flash_erase;	
	mtd->read = amd_flash_read;	
	mtd->write = amd_flash_write;	
	mtd->sync = amd_flash_sync;	
	mtd->suspend = amd_flash_suspend;	
	mtd->resume = amd_flash_resume;	
	mtd->lock = amd_flash_lock;
	mtd->unlock = amd_flash_unlock;

	private = kmalloc(sizeof(*private) + (sizeof(struct flchip) *
					      temp.numchips), GFP_KERNEL);
	if (!private) {
		printk(KERN_WARNING
		       "%s: kmalloc failed for private structure\n", map->name);
		kfree(mtd);
		map->fldrv_priv = NULL;
		return NULL;
	}
	memcpy(private, &temp, sizeof(temp));
	memcpy(private->chips, chips,
	       sizeof(struct flchip) * private->numchips);
	for (i = 0; i < private->numchips; i++) {
		init_waitqueue_head(&private->chips[i].wq);
		spin_lock_init(&private->chips[i]._spinlock);
	}

	map->fldrv_priv = private;

	map->fldrv = &amd_flash_chipdrv;

	__module_get(THIS_MODULE);
	return mtd;
}



static inline int read_one_chip(struct map_info *map, struct flchip *chip,
			       loff_t adr, size_t len, u_char *buf)
{
	DECLARE_WAITQUEUE(wait, current);
	unsigned long timeo = jiffies + HZ;

retry:
	spin_lock_bh(chip->mutex);

	if (chip->state != FL_READY){
		printk(KERN_INFO "%s: waiting for chip to read, state = %d\n",
		       map->name, chip->state);
		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&chip->wq, &wait);
                
		spin_unlock_bh(chip->mutex);

		schedule();
		remove_wait_queue(&chip->wq, &wait);

		if(signal_pending(current)) {
			return -EINTR;
		}

		timeo = jiffies + HZ;

		goto retry;
	}	

	adr += chip->start;

	chip->state = FL_READY;

	map_copy_from(map, buf, adr, len);

	wake_up(&chip->wq);
	spin_unlock_bh(chip->mutex);

	return 0;
}



static int amd_flash_read(struct mtd_info *mtd, loff_t from, size_t len,
			  size_t *retlen, u_char *buf)
{
	struct map_info *map = mtd->priv;
	struct amd_flash_private *private = map->fldrv_priv;
	unsigned long ofs;
	int chipnum;
	int ret = 0;

	if ((from + len) > mtd->size) {
		printk(KERN_WARNING "%s: read request past end of device "
		       "(0x%lx)\n", map->name, (unsigned long)from + len);

		return -EINVAL;
	}

	/* Offset within the first chip that the first read should start. */
	chipnum = (from >> private->chipshift);
	ofs = from - (chipnum <<  private->chipshift);

	*retlen = 0;

	while (len) {
		unsigned long this_len;

		if (chipnum >= private->numchips) {
			break;
		}

		if ((len + ofs - 1) >> private->chipshift) {
			this_len = (1 << private->chipshift) - ofs;
		} else {
			this_len = len;
		}

		ret = read_one_chip(map, &private->chips[chipnum], ofs,
				    this_len, buf);
		if (ret) {
			break;
		}

		*retlen += this_len;
		len -= this_len;
		buf += this_len;

		ofs = 0;
		chipnum++;
	}

	return ret;
}



static int write_one_word(struct map_info *map, struct flchip *chip,
			  unsigned long adr, __u32 datum)
{
	unsigned long timeo = jiffies + HZ;
	struct amd_flash_private *private = map->fldrv_priv;
	DECLARE_WAITQUEUE(wait, current);
	int ret = 0;
	int times_left;

retry:
	spin_lock_bh(chip->mutex);

	if (chip->state != FL_READY){
		printk("%s: waiting for chip to write, state = %d\n",
		       map->name, chip->state);
		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&chip->wq, &wait);
                
		spin_unlock_bh(chip->mutex);

		schedule();
		remove_wait_queue(&chip->wq, &wait);
		printk(KERN_INFO "%s: woke up to write\n", map->name);
		if(signal_pending(current))
			return -EINTR;

		timeo = jiffies + HZ;

		goto retry;
	}	

	chip->state = FL_WRITING;

	adr += chip->start;
	ENABLE_VPP(map);
	send_cmd(map, chip->start, CMD_PROGRAM_UNLOCK_DATA);
	wide_write(map, datum, adr);

	times_left = 500000;
	while (times_left-- && flash_is_busy(map, adr, private->interleave)) { 
		if (need_resched()) {
			spin_unlock_bh(chip->mutex);
			schedule();
			spin_lock_bh(chip->mutex);
		}
	}

	if (!times_left) {
		printk(KERN_WARNING "%s: write to 0x%lx timed out!\n",
		       map->name, adr);
		ret = -EIO;
	} else {
		__u32 verify;
		if ((verify = wide_read(map, adr)) != datum) {
			printk(KERN_WARNING "%s: write to 0x%lx failed. "
			       "datum = %x, verify = %x\n",
			       map->name, adr, datum, verify);
			ret = -EIO;
		}
	}

	DISABLE_VPP(map);
	chip->state = FL_READY;
	wake_up(&chip->wq);
	spin_unlock_bh(chip->mutex);

	return ret;
}



static int amd_flash_write(struct mtd_info *mtd, loff_t to , size_t len,
			   size_t *retlen, const u_char *buf)
{
	struct map_info *map = mtd->priv;
	struct amd_flash_private *private = map->fldrv_priv;
	int ret = 0;
	int chipnum;
	unsigned long ofs;
	unsigned long chipstart;

	*retlen = 0;
	if (!len) {
		return 0;
	}

	chipnum = to >> private->chipshift;
	ofs = to  - (chipnum << private->chipshift);
	chipstart = private->chips[chipnum].start;

	/* If it's not bus-aligned, do the first byte write. */
	if (ofs & (map->buswidth - 1)) {
		unsigned long bus_ofs = ofs & ~(map->buswidth - 1);
		int i = ofs - bus_ofs;
		int n = 0;
		u_char tmp_buf[4];
		__u32 datum;

		map_copy_from(map, tmp_buf,
			       bus_ofs + private->chips[chipnum].start,
			       map->buswidth);
		while (len && i < map->buswidth)
			tmp_buf[i++] = buf[n++], len--;

		if (map->buswidth == 2) {
			datum = *(__u16*)tmp_buf;
		} else if (map->buswidth == 4) {
			datum = *(__u32*)tmp_buf;
		} else {
			return -EINVAL;  /* should never happen, but be safe */
		}

		ret = write_one_word(map, &private->chips[chipnum], bus_ofs,
				     datum);
		if (ret) {
			return ret;
		}
		
		ofs += n;
		buf += n;
		(*retlen) += n;

		if (ofs >> private->chipshift) {
			chipnum++;
			ofs = 0;
			if (chipnum == private->numchips) {
				return 0;
			}
		}
	}
	
	/* We are now aligned, write as much as possible. */
	while(len >= map->buswidth) {
		__u32 datum;

		if (map->buswidth == 1) {
			datum = *(__u8*)buf;
		} else if (map->buswidth == 2) {
			datum = *(__u16*)buf;
		} else if (map->buswidth == 4) {
			datum = *(__u32*)buf;
		} else {
			return -EINVAL;
		}

		ret = write_one_word(map, &private->chips[chipnum], ofs, datum);

		if (ret) {
			return ret;
		}

		ofs += map->buswidth;
		buf += map->buswidth;
		(*retlen) += map->buswidth;
		len -= map->buswidth;

		if (ofs >> private->chipshift) {
			chipnum++;
			ofs = 0;
			if (chipnum == private->numchips) {
				return 0;
			}
			chipstart = private->chips[chipnum].start;
		}
	}

	if (len & (map->buswidth - 1)) {
		int i = 0, n = 0;
		u_char tmp_buf[2];
		__u32 datum;

		map_copy_from(map, tmp_buf,
			       ofs + private->chips[chipnum].start,
			       map->buswidth);
		while (len--) {
			tmp_buf[i++] = buf[n++];
		}

		if (map->buswidth == 2) {
			datum = *(__u16*)tmp_buf;
		} else if (map->buswidth == 4) {
			datum = *(__u32*)tmp_buf;
		} else {
			return -EINVAL;  /* should never happen, but be safe */
		}

		ret = write_one_word(map, &private->chips[chipnum], ofs, datum);

		if (ret) {
			return ret;
		}
		
		(*retlen) += n;
	}

	return 0;
}



static inline int erase_one_block(struct map_info *map, struct flchip *chip,
				  unsigned long adr, u_long size)
{
	unsigned long timeo = jiffies + HZ;
	struct amd_flash_private *private = map->fldrv_priv;
	DECLARE_WAITQUEUE(wait, current);

retry:
	spin_lock_bh(chip->mutex);

	if (chip->state != FL_READY){
		set_current_state(TASK_UNINTERRUPTIBLE);
		add_wait_queue(&chip->wq, &wait);
                
		spin_unlock_bh(chip->mutex);

		schedule();
		remove_wait_queue(&chip->wq, &wait);

		if (signal_pending(current)) {
			return -EINTR;
		}

		timeo = jiffies + HZ;

		goto retry;
	}	

	chip->state = FL_ERASING;

	adr += chip->start;
	ENABLE_VPP(map);
	send_cmd(map, chip->start, CMD_SECTOR_ERASE_UNLOCK_DATA);
	send_cmd_to_addr(map, chip->start, CMD_SECTOR_ERASE_UNLOCK_DATA_2, adr);
	
	timeo = jiffies + (HZ * 20);

	spin_unlock_bh(chip->mutex);
	msleep(1000);
	spin_lock_bh(chip->mutex);
	
	while (flash_is_busy(map, adr, private->interleave)) {

		if (chip->state != FL_ERASING) {
			/* Someone's suspended the erase. Sleep */
			set_current_state(TASK_UNINTERRUPTIBLE);
			add_wait_queue(&chip->wq, &wait);
			
			spin_unlock_bh(chip->mutex);
			printk(KERN_INFO "%s: erase suspended. Sleeping\n",
			       map->name);
			schedule();
			remove_wait_queue(&chip->wq, &wait);
			
			if (signal_pending(current)) {
				return -EINTR;
			}
			
			timeo = jiffies + (HZ*2); /* FIXME */
			spin_lock_bh(chip->mutex);
			continue;
		}

		/* OK Still waiting */
		if (time_after(jiffies, timeo)) {
			chip->state = FL_READY;
			spin_unlock_bh(chip->mutex);
			printk(KERN_WARNING "%s: waiting for erase to complete "
			       "timed out.\n", map->name);
			DISABLE_VPP(map);

			return -EIO;
		}
		
		/* Latency issues. Drop the lock, wait a while and retry */
		spin_unlock_bh(chip->mutex);

		if (need_resched())
			schedule();
		else
			udelay(1);
		
		spin_lock_bh(chip->mutex);
	}

	/* Verify every single word */
	{
		int address;
		int error = 0;
		__u8 verify;

		for (address = adr; address < (adr + size); address++) {
			if ((verify = map_read8(map, address)) != 0xFF) {
				error = 1;
				break;
			}
		}
		if (error) {
			chip->state = FL_READY;
			spin_unlock_bh(chip->mutex);
			printk(KERN_WARNING
			       "%s: verify error at 0x%x, size %ld.\n",
			       map->name, address, size);
			DISABLE_VPP(map);

			return -EIO;
		}
	}
	
	DISABLE_VPP(map);
	chip->state = FL_READY;
	wake_up(&chip->wq);
	spin_unlock_bh(chip->mutex);

	return 0;
}



static int amd_flash_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	struct map_info *map = mtd->priv;
	struct amd_flash_private *private = map->fldrv_priv;
	unsigned long adr, len;
	int chipnum;
	int ret = 0;
	int i;
	int first;
	struct mtd_erase_region_info *regions = mtd->eraseregions;

	if (instr->addr > mtd->size) {
		return -EINVAL;
	}

	if ((instr->len + instr->addr) > mtd->size) {
		return -EINVAL;
	}

	/* Check that both start and end of the requested erase are
	 * aligned with the erasesize at the appropriate addresses.
	 */

	i = 0;

        /* Skip all erase regions which are ended before the start of
           the requested erase. Actually, to save on the calculations,
           we skip to the first erase region which starts after the
           start of the requested erase, and then go back one.
        */

        while ((i < mtd->numeraseregions) &&
	       (instr->addr >= regions[i].offset)) {
               i++;
	}
        i--;

	/* OK, now i is pointing at the erase region in which this
	 * erase request starts. Check the start of the requested
	 * erase range is aligned with the erase size which is in
	 * effect here.
	 */

	if (instr->addr & (regions[i].erasesize-1)) {
		return -EINVAL;
	}

	/* Remember the erase region we start on. */

	first = i;

	/* Next, check that the end of the requested erase is aligned
	 * with the erase region at that address.
	 */

	while ((i < mtd->numeraseregions) && 
	       ((instr->addr + instr->len) >= regions[i].offset)) {
                i++;
	}

	/* As before, drop back one to point at the region in which
	 * the address actually falls.
	 */

	i--;

	if ((instr->addr + instr->len) & (regions[i].erasesize-1)) {
                return -EINVAL;
	}

	chipnum = instr->addr >> private->chipshift;
	adr = instr->addr - (chipnum << private->chipshift);
	len = instr->len;

	i = first;

	while (len) {
		ret = erase_one_block(map, &private->chips[chipnum], adr,
				      regions[i].erasesize);

		if (ret) {
			return ret;
		}

		adr += regions[i].erasesize;
		len -= regions[i].erasesize;

		if ((adr % (1 << private->chipshift)) ==
		    ((regions[i].offset + (regions[i].erasesize *
		    			   regions[i].numblocks))
		     % (1 << private->chipshift))) {
			i++;
		}

		if (adr >> private->chipshift) {
			adr = 0;
			chipnum++;
			if (chipnum >= private->numchips) {
				break;
			}
		}
	}
		
	instr->state = MTD_ERASE_DONE;
	mtd_erase_callback(instr);
	
	return 0;
}



static void amd_flash_sync(struct mtd_info *mtd)
{
	struct map_info *map = mtd->priv;
	struct amd_flash_private *private = map->fldrv_priv;
	int i;
	struct flchip *chip;
	int ret = 0;
	DECLARE_WAITQUEUE(wait, current);

	for (i = 0; !ret && (i < private->numchips); i++) {
		chip = &private->chips[i];

	retry:
		spin_lock_bh(chip->mutex);

		switch(chip->state) {
		case FL_READY:
		case FL_STATUS:
		case FL_CFI_QUERY:
		case FL_JEDEC_QUERY:
			chip->oldstate = chip->state;
			chip->state = FL_SYNCING;
			/* No need to wake_up() on this state change - 
			 * as the whole point is that nobody can do anything
			 * with the chip now anyway.
			 */
		case FL_SYNCING:
			spin_unlock_bh(chip->mutex);
			break;

		default:
			/* Not an idle state */
			add_wait_queue(&chip->wq, &wait);
			
			spin_unlock_bh(chip->mutex);

			schedule();

		        remove_wait_queue(&chip->wq, &wait);
			
			goto retry;
		}
	}

	/* Unlock the chips again */
	for (i--; i >= 0; i--) {
		chip = &private->chips[i];

		spin_lock_bh(chip->mutex);
		
		if (chip->state == FL_SYNCING) {
			chip->state = chip->oldstate;
			wake_up(&chip->wq);
		}
		spin_unlock_bh(chip->mutex);
	}
}



static int amd_flash_suspend(struct mtd_info *mtd)
{
printk("amd_flash_suspend(): not implemented!\n");
	return -EINVAL;
}



static void amd_flash_resume(struct mtd_info *mtd)
{
printk("amd_flash_resume(): not implemented!\n");
}



static void amd_flash_destroy(struct mtd_info *mtd)
{
	struct map_info *map = mtd->priv;
	struct amd_flash_private *private = map->fldrv_priv;
	kfree(private);
}

int __init amd_flash_init(void)
{
	register_mtd_chip_driver(&amd_flash_chipdrv);
	return 0;
}

void __exit amd_flash_exit(void)
{
	unregister_mtd_chip_driver(&amd_flash_chipdrv);
}

module_init(amd_flash_init);
module_exit(amd_flash_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jonas Holmberg <jonas.holmberg@axis.com>");
MODULE_DESCRIPTION("Old MTD chip driver for AMD flash chips");
