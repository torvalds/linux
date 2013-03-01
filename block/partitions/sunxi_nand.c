/*
 *  fs/partitions/sunxi_nand.c
 *  Code extracted from drivers/block/genhd.c
 */

#include "check.h"
#include <plat/mbr.h>
#include <linux/crc32.h>

int sunxi_nand_partition(struct parsed_partitions *state)
{
	Sector sect;
	MBR localmbr;
	MBR *mbr = &localmbr;
	char b[BDEVNAME_SIZE];
	int part_cnt;
	int i;

	mbr = read_part_sector(state, 0, &sect);
	if (!mbr)
		return -1;

	bdevname(state->bdev, b);

	for(i = 0; i < MBR_COPY_NUM; i++, mbr++) {
		__u32 iv=0xffffffff;
		if(*(__u32 *)mbr == (crc32_le(iv,(__u8 *)mbr + 4,MBR_SIZE - 4) ^ iv))
			break;
		printk(KERN_WARNING "Dev Sunxi %s header: CRC bad for MBR %d\n", b, i);
	}

	if (i == MBR_COPY_NUM) {
		put_dev_sector(sect);
		printk(KERN_WARNING "Dev Sunxi %s header: CRC bad for all MBR copies, header block corrupted\n", b);
		return 0;
	}

	for(part_cnt = 0; part_cnt < mbr->PartCount && part_cnt < MAX_PART_COUNT; part_cnt++) {
		/* special case: last partition uses up rest of NAND space */
		__u32 size = mbr->array[part_cnt].lenlo;
		if (part_cnt == mbr->PartCount - 1)
			size = get_capacity(state->bdev->bd_disk) - mbr->array[part_cnt].addrlo;
		printk(KERN_WARNING "Dev %s: part %d, start %d, size %d\n", b, part_cnt + 1,
			mbr->array[part_cnt].addrlo, size);
		put_partition(state, part_cnt + 1, mbr->array[part_cnt].addrlo, size);
	}
	put_dev_sector(sect);
	strlcat(state->pp_buf, "\n", PAGE_SIZE);
	return 1;
}
