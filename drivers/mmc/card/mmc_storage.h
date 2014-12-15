
#ifndef _MMC_SECURE_STORAGE_H
#define _MMC_SECURE_STORAGE_H

#include <linux/mmc/emmc_partitions.h>

#define MMC_MALLOC_FAILED  1
#define STORAGE_READ_FAILED 2
#define STORAGE_WRITE_FAILED 3

//#define STORE_DBG
#ifdef STORE_DBG
#define store_dbg(fmt, ...) printk( "%s: line:%d " fmt "\n", \
				  __func__, __LINE__, ##__VA_ARGS__)

#define store_msg(fmt, ...) printk( "%s: line:%d " fmt "\n", \
				  __func__, __LINE__, ##__VA_ARGS__)				  
#else
#define store_dbg(fmt, ...)
#define store_msg(fmt, ...) printk( fmt "\n",  ##__VA_ARGS__)
#endif

#define MMC_STORAGE_MAGIC		"mmc_storage"
#define MMC_STORAGE_AREA_SIZE		(256*1024)
#define MMC_STORAGE_AREA_COUNT	 2
#define MMC_STORAGE_OFFSET		(MMC_BOOT_PARTITION_SIZE+MMC_BOOT_PARTITION_RESERVED \
								+0x200000)

#define MMC_STORAGE_MAGIC_SIZE	16
#define MMC_STORAGE_AREA_HEAD_SIZE	(MMC_STORAGE_MAGIC_SIZE+4*6)
#define MMC_STORAGE_AREA_VALID_SIZE	(MMC_STORAGE_AREA_SIZE-MMC_STORAGE_AREA_HEAD_SIZE)

#define MMC_STORAGE_DEFAULT_SIZE	(128*1024)
#define MMC_STORAGE_MEM_SIZE		MMC_STORAGE_DEFAULT_SIZE

struct mmc_storage_head_t{
	unsigned char magic[MMC_STORAGE_MAGIC_SIZE];
	unsigned int magic_checksum;
	unsigned int version;
	unsigned int tag;
	unsigned int checksum; //data checksum
	unsigned int timestamp;
	unsigned int reserve;
	unsigned int data[MMC_STORAGE_AREA_VALID_SIZE];
};

struct storage_node_t{	
	uint64_t offset_addr;
	char valid_node_flag;
	char valid_flag;
	unsigned int timestamp;
	struct list_head storage_list;
};

struct mmc_storage_info_t{
	struct storage_node_t * valid_node;
	struct storage_node_t * free_node;
	uint64_t start_addr;
	uint64_t end_addr;
	unsigned char secure_valid;
	unsigned char secure_init;
};

extern int mmc_storage_probe(struct mmc_card *card);

#endif
