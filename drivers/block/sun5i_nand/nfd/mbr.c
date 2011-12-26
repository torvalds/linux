#include "mbr.h"
#include "../src/include/nand_oal.h"
#include "nand_private.h"

MBR *mbr;


typedef struct tag_CRC32_DATA
{
	__u32 CRC;				//int的大小是32位
	__u32 CRC_32_Tbl[256];	//用来保存码表
}CRC32_DATA_t;

__u32 calc_crc32(void * buffer, __u32 length)
{
	__u32 i, j;
	CRC32_DATA_t crc32;		//
	__u32 CRC32 = 0xffffffff; //设置初始值
	crc32.CRC = 0;
	
	for( i = 0; i < 256; ++i)//用++i以提高效率
	{
		crc32.CRC = i;
		for( j = 0; j < 8 ; ++j)
		{
			//这个循环实际上就是用"计算法"来求取CRC的校验码
			if(crc32.CRC & 1)
				crc32.CRC = (crc32.CRC >> 1) ^ 0xEDB88320;
			else //0xEDB88320就是CRC-32多项表达式的值
				crc32.CRC >>= 1;
		}
		crc32.CRC_32_Tbl[i] = crc32.CRC;
	}
	
	CRC32 = 0xffffffff; //设置初始值
    for( i = 0; i < length; ++i)
    {
        CRC32 = crc32.CRC_32_Tbl[(CRC32^((unsigned char*)buffer)[i]) & 0xff] ^ (CRC32>>8); 
    }
    //return CRC32;
	return CRC32^0xffffffff;
}

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
			/*checksum*/
			if(*(__u32 *)mbr == calc_crc32((__u32 *)mbr + 1,MBR_SIZE - 4))
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
	int part_index;

	if(_get_mbr()){
		printk("get mbr error\n" );
		return part_cnt;
	}
	part_index = 0;
	//查找出所有的LINUX盘符
	for(part_cnt = 0; part_cnt < mbr->PartCount && part_cnt < MAX_PART_COUNT; part_cnt++)
	{
	    if((mbr->array[part_cnt].user_type == 2) || (mbr->array[part_cnt].user_type == 0))
	    {
			PRINT("The %d disk name = %s, class name = %s, disk size = %d\n", part_index, mbr->array[part_cnt].name, 
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

	return part_index;
}

