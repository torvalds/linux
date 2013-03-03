/*
 * drivers/block/sunxi_nand/nfd/mbr.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include "mbr.h"
#include "../src/include/nand_oal.h"
#include "nand_private.h"
#include <linux/crc32.h>

MBR *mbr;

__s32 _get_mbr(void)
{
	__u32 	i;
	__s32  mbr_get_sucess = 0;

	/*request mbr space*/
	mbr = MALLOC(sizeof(MBR));
	if(mbr == NULL)
	{
		PRINT("%s : request memory fail\n",__FUNCTION__);
		return -ENOMEM;
	}

	/*get mbr from nand device*/
	for(i = 0; i < MBR_COPY_NUM; i++)
	{
		if(LML_Read((MBR_START_ADDRESS + MBR_SIZE*i)/512,MBR_SIZE/512,mbr) == 0)
		{
			__u32 iv=0xffffffff;
			/*checksum*/
			if(*(__u32 *)mbr == (crc32_le(iv,(__u8 *)mbr + 4,MBR_SIZE - 4) ^ iv))
			{
				mbr_get_sucess = 1;
				break;
			}
		}
	}

	if(mbr_get_sucess)
		return 0;
	else
		return -1;

}

__s32 _free_mbr(void)
{
	if(mbr)
	{
		FREE(mbr,sizeof(MBR));
		mbr = 0;
	}

	return 0;
}

int mbr2disks(struct nand_disk* disk_array)
{
	int part_cnt = 0;
	int part_index = 0;

	PRINT("The %d disk name = %s, class name = %s, disk start = 0, disk size = %d\n",
		part_index, "DEVICE", "NAND", DiskSize);

	/*
	 * for nand recovery in case it's trashed during formatting always
	 * make the nand device before possibly failing on a bad mbr
	 */
	disk_array[part_index].offset = 0;
	disk_array[part_index].size = DiskSize;
	part_index++;

#ifdef CONFIG_SUNXI_NAND_COMPAT_DEV
	if(_get_mbr()){
		printk("get mbr error\n" );
		return part_index;
	}

	//查找出所有的LINUX盘符
	for(part_cnt = 0; part_cnt < mbr->PartCount && part_cnt < MAX_PART_COUNT; part_cnt++)
	{
		if (mbr->array[part_cnt].user_type < 3) {
			PRINT("The %d disk name = %s, type = %u, class name = %s, disk size = %d\n",
			      part_index,
			      mbr->array[part_cnt].name, mbr->array[part_cnt].user_type,
			      mbr->array[part_cnt].classname, mbr->array[part_cnt].lenlo);

			disk_array[part_index].offset = mbr->array[part_cnt].addrlo;
			disk_array[part_index].size = mbr->array[part_cnt].lenlo;
			part_index ++;
		}
	}
	disk_array[part_index - 1].size = DiskSize - mbr->array[mbr->PartCount - 1].addrlo;
	_free_mbr();
	PRINT("The %d disk size = %lu\n", part_index - 1, disk_array[part_index - 1].size);
	PRINT("part total count = %d\n", part_index);
#endif

	return part_index;
}

