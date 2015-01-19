#include <linux/err.h>
#include <linux/device.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <mach/am_regs.h>
#include <mach/irqs.h>
#include <mach/card_io.h>
//#include <mach/mod_gate.h>
#include <linux/cardreader/card_block.h>
#include <linux/cardreader/cardreader.h>
#include <linux/cardreader/sdio.h>
#include <linux/cardreader/amlkey.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>


#include "sd/sd_protocol.h"




#ifdef CONFIG_AML_CARD_KEY

#define EMMC_MODE  2

#include <linux/efuse.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>

static  struct mtd_partition    *key_partitions;
static  struct memory_card *     key_card;

#define CARDSECTORSIZE (sd_mmc_info->blk_len)
#define KEYSECTORSIZE 1024*8
#define SELECTPART "nand_key"

static  emmc_init(struct memory_card * card)
{
	if(!(card->unit_state & CARD_UNIT_READY))
	{
		__card_claim_host(card->host, card);
		card->card_io_init(card);
		card->card_detector(card);
		card->card_insert_process(card);
		card_release_host(card->host);	
		card->unit_state |= CARD_UNIT_READY;
	}
}
#define PRINT_HASH(hash) {printk("%s:%d ",__func__,__LINE__);int __i;for(__i=0;__i<32;__i++)printk("%02x,",hash[__i]);printk("\n");}

static int32_t card_key_read(aml_keybox_provider_t * provider,uint8_t * buf,int bytes)
{
	SD_MMC_Card_Info_t *sd_mmc_info = key_card->card_info;
	int32_t ret=0;
#if (EMMC_MODE==0)	
	__card_claim_host(key_card->host, key_card);
	ret= sd_mmc_read_data(sd_mmc_info,(unsigned long)(key_partitions->offset)/CARDSECTORSIZE,KEYSECTORSIZE,buf);
	card_release_host(key_card->host);
#elif (EMMC_MODE==1)	
	struct card_blk_request brq;
	brq.crq.cmd=READ;
	brq.card_data.lba = (unsigned long)(key_partitions->offset)/CARDSECTORSIZE;
	brq.card_data.blk_size = CARDSECTORSIZE;
	brq.card_data.blk_nums = KEYSECTORSIZE/CARDSECTORSIZE;
	brq.crq.buf =buf ;
//	emmc_init(key_card);
	__card_claim_host(key_card->host, key_card);
	ret=key_card->card_request_process(key_card, &brq);
	card_release_host(key_card->host);
	printk("card_key_read ret=%d\n",ret);
#else
	struct file *fp; 
	mm_segment_t fs; 
	loff_t pos; 
	char *file = provider->priv;
	fp = filp_open(file, O_RDWR, 0644); 
	if (IS_ERR(fp)) { 
		printk("open file error\n"); 
		return -1; 
	} 
	fs = get_fs(); 
	set_fs(KERNEL_DS); 
	pos = 0; 
	vfs_read(fp, buf, bytes, &pos); 
	filp_close(fp, NULL); 
	set_fs(fs); 
	PRINT_HASH(buf);
#endif
	return	ret;
}
static int32_t card_key_write(aml_keybox_provider_t * provider,uint8_t * buf,int bytes)
{
	SD_MMC_Card_Info_t *sd_mmc_info = key_card->card_info;
	int32_t ret=0;

#if (EMMC_MODE==0)		
	__card_claim_host(key_card->host, key_card);
	ret= sd_mmc_write_data(sd_mmc_info,(unsigned long)(key_partitions->offset)/CARDSECTORSIZE,KEYSECTORSIZE,buf);
	card_release_host(key_card->host);
	return  ret;
#elif (EMMC_MODE==1)
	struct card_blk_request brq;
	brq.crq.cmd=WRITE;
	brq.card_data.lba = (unsigned long)(key_partitions->offset)/CARDSECTORSIZE;
	brq.card_data.blk_size = CARDSECTORSIZE;
	brq.card_data.blk_nums =  KEYSECTORSIZE/CARDSECTORSIZE;
	brq.crq.buf =buf ;
//	emmc_init(key_card);
	__card_claim_host(key_card->host, key_card);
	ret=key_card->card_request_process(key_card, &brq);
	card_release_host(key_card->host);
	printk("card_key_write ret=%d\n",ret);
#else
	struct file *fp; 
	mm_segment_t fs; 
	loff_t pos; 
	char *file = provider->priv;
	fp = filp_open(file, O_RDWR, 0644); 
	if (IS_ERR(fp)) { 
		printk("open file error\n"); 
		return -1; 
	} 
	fs = get_fs(); 
	set_fs(KERNEL_DS); 
	pos = 0; 
	vfs_write(fp, buf, bytes, &pos); 
	filp_close(fp, NULL); 
	set_fs(fs); 
	PRINT_HASH(buf);
#endif
	return	ret;
}

static aml_keybox_provider_t nand_provider={
 .name="nand_key",
 .read=card_key_read,
 .write=card_key_write,
};

int card_key_init(struct memory_card *card)
{
		if(card->card_plat_info->nr_partitions>0)
		{
			int  i;
		    int err;	
			char *file;

			err = aml_keybox_provider_register(&nand_provider);
			if(err){
				BUG();
			}
			
			for(i=0;i<card->card_plat_info->nr_partitions;i++){
				if(strcmp(card->card_plat_info->partitions[i].name,SELECTPART)==0)
					break;
			}
			if(i>=card->card_plat_info->nr_partitions){
				printk("nand_key partition is not exist\n");
				return -1;
			} 

			file=kzalloc(256, GFP_KERNEL);
			if(!file){
				printk("kzalloc nand_provider->priv \n");
				return -1;
			} 
			sprintf(file, "/dev/block/cardblk%s%d", card->name,1+i);
			nand_provider.priv=file;	
			key_card=card;
			key_partitions = card->card_plat_info->partitions+i;

			printk("aml_keybox_provider_register nand_key partition ok!\n");			
		}
		
	return 0;
}


#endif



