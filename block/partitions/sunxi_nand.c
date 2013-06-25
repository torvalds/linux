/*
 *  fs/partitions/sunxi_nand.c
 *  Code extracted from drivers/block/genhd.c
 */

#include "check.h"
#include <linux/crc32.h>

#define sector_size 512
#define mbr_sector  ((no-1) * MBR_SIZE / sector_size)
#define CRC_MAGIC 0xffffffff

/* no indexes from 1 - 0 is invalid */
static int sunxi_nand_validate_mbr(struct parsed_partitions *state, int no)
{
	__u32 iv = CRC_MAGIC;
	__u32 crc;
	__u32 calculated;
	__u32 sector = mbr_sector;
	Sector sect;
	struct MBR *mbr = 0;
	char b[BDEVNAME_SIZE];

	bdevname(state->bdev, b);

	mbr = read_part_sector(state, sector, &sect);
	if (!mbr) {
		printk(KERN_ERR "Dev Sunxi %s %s header: error reading sector %d\n",
				MBR_MAGIC, b, sector);
		goto error;
	}

	if (strncmp(MBR_MAGIC, mbr->tag.magic, 8)) {
		printk(KERN_WARNING "Dev Sunxi %s %s magic does not match for MBR %d: %8.8s\n",
				MBR_MAGIC, b, no, mbr->tag.magic);
		goto error;
	}
	if (MBR_VERSION != mbr->tag.version) {
		printk(KERN_WARNING "Dev Sunxi %s %s version does not match for MBR %d: 0x%x != 0x%x\n",
				MBR_MAGIC, b, no, mbr->tag.version,
				MBR_VERSION);
		goto error;
	}
	if (no - 1 != mbr->tag.index) {
		printk(KERN_WARNING "Dev Sunxi %s %s mbr number does not match for MBR %d: %d\n",
				MBR_MAGIC, b, no, mbr->tag.index);
	}
	/* actual MBR sizes are either << PAGE_SIZE (all four copies in one
	 * page) or multiple of page size so this should work regardless of
	 * internal details of undocumented read_part_sector */
	calculated = min(MBR_SIZE, PAGE_SIZE);
	crc = *(__u32 *)mbr;
	iv = (crc32_le(iv, (__u8 *)mbr + 4, calculated - 4));
	while (calculated < MBR_SIZE) {
		int chunk = min(PAGE_SIZE, MBR_SIZE - calculated);
		put_dev_sector(sect);
		sector += PAGE_SIZE / sector_size;
		mbr = read_part_sector(state, sector, &sect);
		if (!mbr) {
			printk(KERN_ERR "Dev Sunxi %s %s header: error reading sector %d\n",
					MBR_MAGIC, b, sector);
			goto error;
		}
		iv = (crc32_le(iv, (__u8 *)mbr, chunk));
		calculated += chunk;
	}

	if ((crc ^ CRC_MAGIC) != iv) {
		printk(KERN_WARNING "Dev Sunxi %s %s header: CRC bad for MBR %d\n",
				MBR_MAGIC, b, no);
		goto error;
	}

	goto done;

error:
	no = 0;
done:
	put_dev_sector(sect);
	return no;
}

static void sunxi_nand_parse_mbr(struct parsed_partitions *state, int no)
{
	Sector sect;
	int part_cnt;
	struct MBR *mbr = 0;
	char b[BDEVNAME_SIZE];

	bdevname(state->bdev, b);

	mbr = read_part_sector(state, mbr_sector, &sect);
	for (part_cnt = 0; part_cnt < mbr->tag.PartCount &&
			/* the sunxi mbr structure ver 0x200 allows for 120
			 * partitions but only 31 fit into a page so forget the
			 * ones that do not fit. */
			part_cnt < MAX_PART_COUNT && part_cnt < 31;
			part_cnt++) {
		/* special case: last partition uses up rest of NAND space */
		__u32 size = mbr->array[part_cnt].lenlo;
		if (part_cnt == mbr->tag.PartCount - 1)
			size = get_capacity(state->bdev->bd_disk) -
				mbr->array[part_cnt].addrlo;
		printk(KERN_WARNING "Dev Sunxi %s %s: part %d, start %d, size %d\n",
			MBR_MAGIC, b, part_cnt + 1,
			mbr->array[part_cnt].addrlo, size);
		put_partition(state, part_cnt + 1,
				mbr->array[part_cnt].addrlo, size);
	}
	strlcat(state->pp_buf, "\n", PAGE_SIZE);
	put_dev_sector(sect);
}

int sunxi_nand_partition(struct parsed_partitions *state)
{
	int valid_mbr = 0;
	int mbr;
	int i;

	for (i = 1; i <= MBR_COPY_NUM; i++, mbr++) {
		mbr = sunxi_nand_validate_mbr(state, i);
		if (mbr)
			valid_mbr = mbr;
	}

	if (!valid_mbr) {
		char b[BDEVNAME_SIZE];

		bdevname(state->bdev, b);
		printk(KERN_WARNING "Dev Sunxi %s %s header bad for all MBR copies, MBR corrupted or not present.\n",
				MBR_MAGIC, b);
		return 0;
	}

	sunxi_nand_parse_mbr(state, valid_mbr);

	return 1;
}
