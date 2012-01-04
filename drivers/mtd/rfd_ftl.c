/*
 * rfd_ftl.c -- resident flash disk (flash translation layer)
 *
 * Copyright © 2005  Sean Young <sean@mess.org>
 *
 * This type of flash translation layer (FTL) is used by the Embedded BIOS
 * by General Software. It is known as the Resident Flash Disk (RFD), see:
 *
 *	http://www.gensw.com/pages/prod/bios/rfd.htm
 *
 * based on ftl.c
 */

#include <linux/hdreg.h>
#include <linux/init.h>
#include <linux/mtd/blktrans.h>
#include <linux/mtd/mtd.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/module.h>

#include <asm/types.h>

static int block_size = 0;
module_param(block_size, int, 0);
MODULE_PARM_DESC(block_size, "Block size to use by RFD, defaults to erase unit size");

#define PREFIX "rfd_ftl: "

/* This major has been assigned by device@lanana.org */
#ifndef RFD_FTL_MAJOR
#define RFD_FTL_MAJOR		256
#endif

/* Maximum number of partitions in an FTL region */
#define PART_BITS		4

/* An erase unit should start with this value */
#define RFD_MAGIC		0x9193

/* the second value is 0xffff or 0xffc8; function unknown */

/* the third value is always 0xffff, ignored */

/* next is an array of mapping for each corresponding sector */
#define HEADER_MAP_OFFSET	3
#define SECTOR_DELETED		0x0000
#define SECTOR_ZERO		0xfffe
#define SECTOR_FREE		0xffff

#define SECTOR_SIZE		512

#define SECTORS_PER_TRACK	63

struct block {
	enum {
		BLOCK_OK,
		BLOCK_ERASING,
		BLOCK_ERASED,
		BLOCK_UNUSED,
		BLOCK_FAILED
	} state;
	int free_sectors;
	int used_sectors;
	int erases;
	u_long offset;
};

struct partition {
	struct mtd_blktrans_dev mbd;

	u_int block_size;		/* size of erase unit */
	u_int total_blocks;		/* number of erase units */
	u_int header_sectors_per_block;	/* header sectors in erase unit */
	u_int data_sectors_per_block;	/* data sectors in erase unit */
	u_int sector_count;		/* sectors in translated disk */
	u_int header_size;		/* bytes in header sector */
	int reserved_block;		/* block next up for reclaim */
	int current_block;		/* block to write to */
	u16 *header_cache;		/* cached header */

	int is_reclaiming;
	int cylinders;
	int errors;
	u_long *sector_map;
	struct block *blocks;
};

static int rfd_ftl_writesect(struct mtd_blktrans_dev *dev, u_long sector, char *buf);

static int build_block_map(struct partition *part, int block_no)
{
	struct block *block = &part->blocks[block_no];
	int i;

	block->offset = part->block_size * block_no;

	if (le16_to_cpu(part->header_cache[0]) != RFD_MAGIC) {
		block->state = BLOCK_UNUSED;
		return -ENOENT;
	}

	block->state = BLOCK_OK;

	for (i=0; i<part->data_sectors_per_block; i++) {
		u16 entry;

		entry = le16_to_cpu(part->header_cache[HEADER_MAP_OFFSET + i]);

		if (entry == SECTOR_DELETED)
			continue;

		if (entry == SECTOR_FREE) {
			block->free_sectors++;
			continue;
		}

		if (entry == SECTOR_ZERO)
			entry = 0;

		if (entry >= part->sector_count) {
			printk(KERN_WARNING PREFIX
				"'%s': unit #%d: entry %d corrupt, "
				"sector %d out of range\n",
				part->mbd.mtd->name, block_no, i, entry);
			continue;
		}

		if (part->sector_map[entry] != -1) {
			printk(KERN_WARNING PREFIX
				"'%s': more than one entry for sector %d\n",
				part->mbd.mtd->name, entry);
			part->errors = 1;
			continue;
		}

		part->sector_map[entry] = block->offset +
			(i + part->header_sectors_per_block) * SECTOR_SIZE;

		block->used_sectors++;
	}

	if (block->free_sectors == part->data_sectors_per_block)
		part->reserved_block = block_no;

	return 0;
}

static int scan_header(struct partition *part)
{
	int sectors_per_block;
	int i, rc = -ENOMEM;
	int blocks_found;
	size_t retlen;

	sectors_per_block = part->block_size / SECTOR_SIZE;
	part->total_blocks = (u32)part->mbd.mtd->size / part->block_size;

	if (part->total_blocks < 2)
		return -ENOENT;

	/* each erase block has three bytes header, followed by the map */
	part->header_sectors_per_block =
			((HEADER_MAP_OFFSET + sectors_per_block) *
			sizeof(u16) + SECTOR_SIZE - 1) / SECTOR_SIZE;

	part->data_sectors_per_block = sectors_per_block -
			part->header_sectors_per_block;

	part->header_size = (HEADER_MAP_OFFSET +
			part->data_sectors_per_block) * sizeof(u16);

	part->cylinders = (part->data_sectors_per_block *
			(part->total_blocks - 1) - 1) / SECTORS_PER_TRACK;

	part->sector_count = part->cylinders * SECTORS_PER_TRACK;

	part->current_block = -1;
	part->reserved_block = -1;
	part->is_reclaiming = 0;

	part->header_cache = kmalloc(part->header_size, GFP_KERNEL);
	if (!part->header_cache)
		goto err;

	part->blocks = kcalloc(part->total_blocks, sizeof(struct block),
			GFP_KERNEL);
	if (!part->blocks)
		goto err;

	part->sector_map = vmalloc(part->sector_count * sizeof(u_long));
	if (!part->sector_map) {
		printk(KERN_ERR PREFIX "'%s': unable to allocate memory for "
			"sector map", part->mbd.mtd->name);
		goto err;
	}

	for (i=0; i<part->sector_count; i++)
		part->sector_map[i] = -1;

	for (i=0, blocks_found=0; i<part->total_blocks; i++) {
		rc = part->mbd.mtd->read(part->mbd.mtd,
				i * part->block_size, part->header_size,
				&retlen, (u_char*)part->header_cache);

		if (!rc && retlen != part->header_size)
			rc = -EIO;

		if (rc)
			goto err;

		if (!build_block_map(part, i))
			blocks_found++;
	}

	if (blocks_found == 0) {
		printk(KERN_NOTICE PREFIX "no RFD magic found in '%s'\n",
				part->mbd.mtd->name);
		rc = -ENOENT;
		goto err;
	}

	if (part->reserved_block == -1) {
		printk(KERN_WARNING PREFIX "'%s': no empty erase unit found\n",
				part->mbd.mtd->name);

		part->errors = 1;
	}

	return 0;

err:
	vfree(part->sector_map);
	kfree(part->header_cache);
	kfree(part->blocks);

	return rc;
}

static int rfd_ftl_readsect(struct mtd_blktrans_dev *dev, u_long sector, char *buf)
{
	struct partition *part = (struct partition*)dev;
	u_long addr;
	size_t retlen;
	int rc;

	if (sector >= part->sector_count)
		return -EIO;

	addr = part->sector_map[sector];
	if (addr != -1) {
		rc = part->mbd.mtd->read(part->mbd.mtd, addr, SECTOR_SIZE,
						&retlen, (u_char*)buf);
		if (!rc && retlen != SECTOR_SIZE)
			rc = -EIO;

		if (rc) {
			printk(KERN_WARNING PREFIX "error reading '%s' at "
				"0x%lx\n", part->mbd.mtd->name, addr);
			return rc;
		}
	} else
		memset(buf, 0, SECTOR_SIZE);

	return 0;
}

static void erase_callback(struct erase_info *erase)
{
	struct partition *part;
	u16 magic;
	int i, rc;
	size_t retlen;

	part = (struct partition*)erase->priv;

	i = (u32)erase->addr / part->block_size;
	if (i >= part->total_blocks || part->blocks[i].offset != erase->addr ||
	    erase->addr > UINT_MAX) {
		printk(KERN_ERR PREFIX "erase callback for unknown offset %llx "
				"on '%s'\n", (unsigned long long)erase->addr, part->mbd.mtd->name);
		return;
	}

	if (erase->state != MTD_ERASE_DONE) {
		printk(KERN_WARNING PREFIX "erase failed at 0x%llx on '%s', "
				"state %d\n", (unsigned long long)erase->addr,
				part->mbd.mtd->name, erase->state);

		part->blocks[i].state = BLOCK_FAILED;
		part->blocks[i].free_sectors = 0;
		part->blocks[i].used_sectors = 0;

		kfree(erase);

		return;
	}

	magic = cpu_to_le16(RFD_MAGIC);

	part->blocks[i].state = BLOCK_ERASED;
	part->blocks[i].free_sectors = part->data_sectors_per_block;
	part->blocks[i].used_sectors = 0;
	part->blocks[i].erases++;

	rc = part->mbd.mtd->write(part->mbd.mtd,
		part->blocks[i].offset, sizeof(magic), &retlen,
		(u_char*)&magic);

	if (!rc && retlen != sizeof(magic))
		rc = -EIO;

	if (rc) {
		printk(KERN_ERR PREFIX "'%s': unable to write RFD "
				"header at 0x%lx\n",
				part->mbd.mtd->name,
				part->blocks[i].offset);
		part->blocks[i].state = BLOCK_FAILED;
	}
	else
		part->blocks[i].state = BLOCK_OK;

	kfree(erase);
}

static int erase_block(struct partition *part, int block)
{
	struct erase_info *erase;
	int rc = -ENOMEM;

	erase = kmalloc(sizeof(struct erase_info), GFP_KERNEL);
	if (!erase)
		goto err;

	erase->mtd = part->mbd.mtd;
	erase->callback = erase_callback;
	erase->addr = part->blocks[block].offset;
	erase->len = part->block_size;
	erase->priv = (u_long)part;

	part->blocks[block].state = BLOCK_ERASING;
	part->blocks[block].free_sectors = 0;

	rc = part->mbd.mtd->erase(part->mbd.mtd, erase);

	if (rc) {
		printk(KERN_ERR PREFIX "erase of region %llx,%llx on '%s' "
				"failed\n", (unsigned long long)erase->addr,
				(unsigned long long)erase->len, part->mbd.mtd->name);
		kfree(erase);
	}

err:
	return rc;
}

static int move_block_contents(struct partition *part, int block_no, u_long *old_sector)
{
	void *sector_data;
	u16 *map;
	size_t retlen;
	int i, rc = -ENOMEM;

	part->is_reclaiming = 1;

	sector_data = kmalloc(SECTOR_SIZE, GFP_KERNEL);
	if (!sector_data)
		goto err3;

	map = kmalloc(part->header_size, GFP_KERNEL);
	if (!map)
		goto err2;

	rc = part->mbd.mtd->read(part->mbd.mtd,
		part->blocks[block_no].offset, part->header_size,
		&retlen, (u_char*)map);

	if (!rc && retlen != part->header_size)
		rc = -EIO;

	if (rc) {
		printk(KERN_ERR PREFIX "error reading '%s' at "
			"0x%lx\n", part->mbd.mtd->name,
			part->blocks[block_no].offset);

		goto err;
	}

	for (i=0; i<part->data_sectors_per_block; i++) {
		u16 entry = le16_to_cpu(map[HEADER_MAP_OFFSET + i]);
		u_long addr;


		if (entry == SECTOR_FREE || entry == SECTOR_DELETED)
			continue;

		if (entry == SECTOR_ZERO)
			entry = 0;

		/* already warned about and ignored in build_block_map() */
		if (entry >= part->sector_count)
			continue;

		addr = part->blocks[block_no].offset +
			(i + part->header_sectors_per_block) * SECTOR_SIZE;

		if (*old_sector == addr) {
			*old_sector = -1;
			if (!part->blocks[block_no].used_sectors--) {
				rc = erase_block(part, block_no);
				break;
			}
			continue;
		}
		rc = part->mbd.mtd->read(part->mbd.mtd, addr,
			SECTOR_SIZE, &retlen, sector_data);

		if (!rc && retlen != SECTOR_SIZE)
			rc = -EIO;

		if (rc) {
			printk(KERN_ERR PREFIX "'%s': Unable to "
				"read sector for relocation\n",
				part->mbd.mtd->name);

			goto err;
		}

		rc = rfd_ftl_writesect((struct mtd_blktrans_dev*)part,
				entry, sector_data);

		if (rc)
			goto err;
	}

err:
	kfree(map);
err2:
	kfree(sector_data);
err3:
	part->is_reclaiming = 0;

	return rc;
}

static int reclaim_block(struct partition *part, u_long *old_sector)
{
	int block, best_block, score, old_sector_block;
	int rc;

	/* we have a race if sync doesn't exist */
	if (part->mbd.mtd->sync)
		part->mbd.mtd->sync(part->mbd.mtd);

	score = 0x7fffffff; /* MAX_INT */
	best_block = -1;
	if (*old_sector != -1)
		old_sector_block = *old_sector / part->block_size;
	else
		old_sector_block = -1;

	for (block=0; block<part->total_blocks; block++) {
		int this_score;

		if (block == part->reserved_block)
			continue;

		/*
		 * Postpone reclaiming if there is a free sector as
		 * more removed sectors is more efficient (have to move
		 * less).
		 */
		if (part->blocks[block].free_sectors)
			return 0;

		this_score = part->blocks[block].used_sectors;

		if (block == old_sector_block)
			this_score--;
		else {
			/* no point in moving a full block */
			if (part->blocks[block].used_sectors ==
					part->data_sectors_per_block)
				continue;
		}

		this_score += part->blocks[block].erases;

		if (this_score < score) {
			best_block = block;
			score = this_score;
		}
	}

	if (best_block == -1)
		return -ENOSPC;

	part->current_block = -1;
	part->reserved_block = best_block;

	pr_debug("reclaim_block: reclaiming block #%d with %d used "
		 "%d free sectors\n", best_block,
		 part->blocks[best_block].used_sectors,
		 part->blocks[best_block].free_sectors);

	if (part->blocks[best_block].used_sectors)
		rc = move_block_contents(part, best_block, old_sector);
	else
		rc = erase_block(part, best_block);

	return rc;
}

/*
 * IMPROVE: It would be best to choose the block with the most deleted sectors,
 * because if we fill that one up first it'll have the most chance of having
 * the least live sectors at reclaim.
 */
static int find_free_block(struct partition *part)
{
	int block, stop;

	block = part->current_block == -1 ?
			jiffies % part->total_blocks : part->current_block;
	stop = block;

	do {
		if (part->blocks[block].free_sectors &&
				block != part->reserved_block)
			return block;

		if (part->blocks[block].state == BLOCK_UNUSED)
			erase_block(part, block);

		if (++block >= part->total_blocks)
			block = 0;

	} while (block != stop);

	return -1;
}

static int find_writable_block(struct partition *part, u_long *old_sector)
{
	int rc, block;
	size_t retlen;

	block = find_free_block(part);

	if (block == -1) {
		if (!part->is_reclaiming) {
			rc = reclaim_block(part, old_sector);
			if (rc)
				goto err;

			block = find_free_block(part);
		}

		if (block == -1) {
			rc = -ENOSPC;
			goto err;
		}
	}

	rc = part->mbd.mtd->read(part->mbd.mtd, part->blocks[block].offset,
		part->header_size, &retlen, (u_char*)part->header_cache);

	if (!rc && retlen != part->header_size)
		rc = -EIO;

	if (rc) {
		printk(KERN_ERR PREFIX "'%s': unable to read header at "
				"0x%lx\n", part->mbd.mtd->name,
				part->blocks[block].offset);
		goto err;
	}

	part->current_block = block;

err:
	return rc;
}

static int mark_sector_deleted(struct partition *part, u_long old_addr)
{
	int block, offset, rc;
	u_long addr;
	size_t retlen;
	u16 del = cpu_to_le16(SECTOR_DELETED);

	block = old_addr / part->block_size;
	offset = (old_addr % part->block_size) / SECTOR_SIZE -
		part->header_sectors_per_block;

	addr = part->blocks[block].offset +
			(HEADER_MAP_OFFSET + offset) * sizeof(u16);
	rc = part->mbd.mtd->write(part->mbd.mtd, addr,
		sizeof(del), &retlen, (u_char*)&del);

	if (!rc && retlen != sizeof(del))
		rc = -EIO;

	if (rc) {
		printk(KERN_ERR PREFIX "error writing '%s' at "
			"0x%lx\n", part->mbd.mtd->name, addr);
		if (rc)
			goto err;
	}
	if (block == part->current_block)
		part->header_cache[offset + HEADER_MAP_OFFSET] = del;

	part->blocks[block].used_sectors--;

	if (!part->blocks[block].used_sectors &&
	    !part->blocks[block].free_sectors)
		rc = erase_block(part, block);

err:
	return rc;
}

static int find_free_sector(const struct partition *part, const struct block *block)
{
	int i, stop;

	i = stop = part->data_sectors_per_block - block->free_sectors;

	do {
		if (le16_to_cpu(part->header_cache[HEADER_MAP_OFFSET + i])
				== SECTOR_FREE)
			return i;

		if (++i == part->data_sectors_per_block)
			i = 0;
	}
	while(i != stop);

	return -1;
}

static int do_writesect(struct mtd_blktrans_dev *dev, u_long sector, char *buf, ulong *old_addr)
{
	struct partition *part = (struct partition*)dev;
	struct block *block;
	u_long addr;
	int i;
	int rc;
	size_t retlen;
	u16 entry;

	if (part->current_block == -1 ||
		!part->blocks[part->current_block].free_sectors) {

		rc = find_writable_block(part, old_addr);
		if (rc)
			goto err;
	}

	block = &part->blocks[part->current_block];

	i = find_free_sector(part, block);

	if (i < 0) {
		rc = -ENOSPC;
		goto err;
	}

	addr = (i + part->header_sectors_per_block) * SECTOR_SIZE +
		block->offset;
	rc = part->mbd.mtd->write(part->mbd.mtd,
		addr, SECTOR_SIZE, &retlen, (u_char*)buf);

	if (!rc && retlen != SECTOR_SIZE)
		rc = -EIO;

	if (rc) {
		printk(KERN_ERR PREFIX "error writing '%s' at 0x%lx\n",
				part->mbd.mtd->name, addr);
		if (rc)
			goto err;
	}

	part->sector_map[sector] = addr;

	entry = cpu_to_le16(sector == 0 ? SECTOR_ZERO : sector);

	part->header_cache[i + HEADER_MAP_OFFSET] = entry;

	addr = block->offset + (HEADER_MAP_OFFSET + i) * sizeof(u16);
	rc = part->mbd.mtd->write(part->mbd.mtd, addr,
			sizeof(entry), &retlen, (u_char*)&entry);

	if (!rc && retlen != sizeof(entry))
		rc = -EIO;

	if (rc) {
		printk(KERN_ERR PREFIX "error writing '%s' at 0x%lx\n",
				part->mbd.mtd->name, addr);
		if (rc)
			goto err;
	}
	block->used_sectors++;
	block->free_sectors--;

err:
	return rc;
}

static int rfd_ftl_writesect(struct mtd_blktrans_dev *dev, u_long sector, char *buf)
{
	struct partition *part = (struct partition*)dev;
	u_long old_addr;
	int i;
	int rc = 0;

	pr_debug("rfd_ftl_writesect(sector=0x%lx)\n", sector);

	if (part->reserved_block == -1) {
		rc = -EACCES;
		goto err;
	}

	if (sector >= part->sector_count) {
		rc = -EIO;
		goto err;
	}

	old_addr = part->sector_map[sector];

	for (i=0; i<SECTOR_SIZE; i++) {
		if (!buf[i])
			continue;

		rc = do_writesect(dev, sector, buf, &old_addr);
		if (rc)
			goto err;
		break;
	}

	if (i == SECTOR_SIZE)
		part->sector_map[sector] = -1;

	if (old_addr != -1)
		rc = mark_sector_deleted(part, old_addr);

err:
	return rc;
}

static int rfd_ftl_getgeo(struct mtd_blktrans_dev *dev, struct hd_geometry *geo)
{
	struct partition *part = (struct partition*)dev;

	geo->heads = 1;
	geo->sectors = SECTORS_PER_TRACK;
	geo->cylinders = part->cylinders;

	return 0;
}

static void rfd_ftl_add_mtd(struct mtd_blktrans_ops *tr, struct mtd_info *mtd)
{
	struct partition *part;

	if (mtd->type != MTD_NORFLASH || mtd->size > UINT_MAX)
		return;

	part = kzalloc(sizeof(struct partition), GFP_KERNEL);
	if (!part)
		return;

	part->mbd.mtd = mtd;

	if (block_size)
		part->block_size = block_size;
	else {
		if (!mtd->erasesize) {
			printk(KERN_WARNING PREFIX "please provide block_size");
			goto out;
		} else
			part->block_size = mtd->erasesize;
	}

	if (scan_header(part) == 0) {
		part->mbd.size = part->sector_count;
		part->mbd.tr = tr;
		part->mbd.devnum = -1;
		if (!(mtd->flags & MTD_WRITEABLE))
			part->mbd.readonly = 1;
		else if (part->errors) {
			printk(KERN_WARNING PREFIX "'%s': errors found, "
					"setting read-only\n", mtd->name);
			part->mbd.readonly = 1;
		}

		printk(KERN_INFO PREFIX "name: '%s' type: %d flags %x\n",
				mtd->name, mtd->type, mtd->flags);

		if (!add_mtd_blktrans_dev((void*)part))
			return;
	}
out:
	kfree(part);
}

static void rfd_ftl_remove_dev(struct mtd_blktrans_dev *dev)
{
	struct partition *part = (struct partition*)dev;
	int i;

	for (i=0; i<part->total_blocks; i++) {
		pr_debug("rfd_ftl_remove_dev:'%s': erase unit #%02d: %d erases\n",
			part->mbd.mtd->name, i, part->blocks[i].erases);
	}

	del_mtd_blktrans_dev(dev);
	vfree(part->sector_map);
	kfree(part->header_cache);
	kfree(part->blocks);
}

static struct mtd_blktrans_ops rfd_ftl_tr = {
	.name		= "rfd",
	.major		= RFD_FTL_MAJOR,
	.part_bits	= PART_BITS,
	.blksize 	= SECTOR_SIZE,

	.readsect	= rfd_ftl_readsect,
	.writesect	= rfd_ftl_writesect,
	.getgeo		= rfd_ftl_getgeo,
	.add_mtd	= rfd_ftl_add_mtd,
	.remove_dev	= rfd_ftl_remove_dev,
	.owner		= THIS_MODULE,
};

static int __init init_rfd_ftl(void)
{
	return register_mtd_blktrans(&rfd_ftl_tr);
}

static void __exit cleanup_rfd_ftl(void)
{
	deregister_mtd_blktrans(&rfd_ftl_tr);
}

module_init(init_rfd_ftl);
module_exit(cleanup_rfd_ftl);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Sean Young <sean@mess.org>");
MODULE_DESCRIPTION("Support code for RFD Flash Translation Layer, "
		"used by General Software's Embedded BIOS");

