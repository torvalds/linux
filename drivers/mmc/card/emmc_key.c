
#include <linux/types.h>
//#include <linux/device.h>
//#include <linux/blkdev.h>
#include <linux/err.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/slab.h>

#include <linux/mmc/card.h>
#include <linux/mmc/emmc_partitions.h>
#include "emmc_key.h"

/*
 * kernel head file
 * 
 ********************************** */

static struct aml_emmckey_info_t *emmckey_info;

static u32 emmckey_calculate_checksum(u8 *buf,u32 lenth)
{
	u32 checksum = 0;
	u32 cnt;
	for(cnt=0;cnt<lenth;cnt++){
		checksum += buf[cnt];
	}
	return checksum;
}

static int emmc_key_transfer(u8 *buf,u32 *value,u32 len,u32 direct)
{
	u8 checksum=0;
	u32 i;
	if(direct){
		for(i=0;i<len;i++){
			checksum += buf[i];
		}
		for(i=0;i<len;i++){
			buf[i] ^= checksum;
		}
		*value = checksum;
		return 0;
	}
	else{
		checksum = *value;
		for(i=0;i<len;i++){
			buf[i] ^= checksum;
		}
		checksum = 0;
		for(i=0;i<len;i++){
			checksum += buf[i];
		}
		if(checksum == *value){
			return 0;
		}
		return -1;
	}
}

static int emmc_key_kernel_rw (struct mmc_card *card, struct emmckey_valid_node_t *emmckey_valid_node,u8 *buf,u32 direct)
{
	int blk,cnt;
    int bit = card->csd.read_blkbits;
    int ret = -1;

	mmc_claim_host(card->host);
    ret = mmc_blk_main_md_part_switch(card);
    if (ret) {
        pr_err("%s: error %d mmc_blk_main_md_part_switch\n", __FUNCTION__, ret);
        goto exit_err;
    }

	blk = emmckey_valid_node->phy_addr >> bit;
	cnt = emmckey_valid_node->phy_size >> bit;
	if(direct){
        ret = mmc_write_internal(card, blk, cnt, buf);
	}
	else{
        ret = mmc_read_internal(card, blk, cnt, buf);
	}

exit_err: 
    mmc_release_host(card->host);

	return ret;
}

static int emmc_key_rw(aml_keybox_provider_t *provider,struct emmckey_valid_node_t *emmckey_valid_node,u8 *buf,u32 direct)
{
	struct mmc_card *card= (struct mmc_card *)provider->priv;
	return emmc_key_kernel_rw(card,emmckey_valid_node, buf,direct);
}

static int aml_emmc_key_check(aml_keybox_provider_t *provider)
{
	u8 keypart_cnt;
	u64 part_size;
	struct emmckey_valid_node_t *emmckey_valid_node,*temp_valid_node;
	emmckey_info->key_part_count = emmckey_info->keyarea_phy_size / EMMC_KEYAREA_SIZE;
	if(emmckey_info->key_part_count > EMMC_KEYAREA_COUNT){
		emmckey_info->key_part_count = EMMC_KEYAREA_COUNT;
	}
	keypart_cnt = 0;
	part_size = EMMC_KEYAREA_SIZE;
	do{
		emmckey_valid_node = kmalloc(sizeof(*emmckey_valid_node), GFP_KERNEL);
		if(emmckey_valid_node == NULL){
			printk("%s:%d,kmalloc memory fail\n",__func__,__LINE__);
			return -ENOMEM;
		}
		emmckey_valid_node->phy_addr = emmckey_info->keyarea_phy_addr + part_size * keypart_cnt;
		emmckey_valid_node->phy_size = EMMC_KEYAREA_SIZE;
		emmckey_valid_node->next = NULL;
		emmckey_info->key_valid = 0;
		if(emmckey_info->key_valid_node == NULL){
			emmckey_info->key_valid_node = emmckey_valid_node;
		}
		else{
			temp_valid_node = emmckey_info->key_valid_node;
			while(temp_valid_node->next != NULL){
				temp_valid_node = temp_valid_node->next;
			}
			temp_valid_node->next = emmckey_valid_node;
		}
	}while(++keypart_cnt < emmckey_info->key_part_count);
	
#if 0
	struct emmckey_data_t *emmckey_data;
	u32 checksum;
	/*read key data from emmc key area*/
	temp_valid_node = emmckey_info->key_valid_node;
	if(temp_valid_node == NULL){
		printk("%s:%d,don't find emmc key valid node\n",__func__,__LINE__);
		return -1;
	}
	emmckey_data = kmalloc(sizeof(*emmckey_data), GFP_KERNEL);
	if(emmckey_data == NULL){
		printk("%s:%d,kmalloc memory fail\n",__func__,__LINE__);
		return -ENOMEM;
	}
	/*read key data */
	memset(emmckey_data,0,sizeof(*emmckey_data));
	emmc_key_rw(provider,temp_valid_node,(u8*)emmckey_data,0);
	if (!memcmp(emmckey_data->keyarea_mark, EMMC_KEY_AREA_SIGNAL, 8)) {
		checksum = emmckey_calculate_checksum(emmckey_data->data,EMMCKEY_DATA_VALID_LEN);
		if(checksum == emmckey_data->checksum){
			emmckey_info->key_valid = 1;
		}
	}
	/*write key data to emmc key area*/
	if(emmckey_info->key_valid == 0){
		temp_valid_node = emmckey_info->key_valid_node;
		while(temp_valid_node){
			memset(emmckey_data,0,sizeof(*emmckey_data));
			memcpy(emmckey_data->keyarea_mark, EMMC_KEY_AREA_SIGNAL, 8);
			emmckey_data->checksum = emmckey_calculate_checksum(emmckey_data->data,EMMCKEY_DATA_VALID_LEN);
			emmc_key_rw(provider,temp_valid_node,(u8*)emmckey_data,1);
			temp_valid_node = temp_valid_node->next;
		}
		emmckey_info->key_valid = 1;
	}
	kfree(emmckey_data);
#else
	emmckey_info->key_valid = 1;
#endif
	return 0;
}

static int32_t emmc_keybox_read(aml_keybox_provider_t * provider, uint8_t *buf,int32_t size,int flags)
{
	int err = -1;
	u32 checksum;
	struct emmckey_valid_node_t *emmckey_valid_node;
	struct emmckey_data_t *emmckey_data;

	if(!emmckey_info->key_valid){
		printk("%s:%d,can't read emmc key\n",__func__,__LINE__);
		return -1;
	}
	if(size > EMMCKEY_DATA_VALID_LEN){
		printk("%s:%d,size is too big,fact:0x%x,need:0x%x\n",__func__,__LINE__,size,EMMCKEY_DATA_VALID_LEN);
		return -1;
	}
	emmckey_data = kmalloc(sizeof(*emmckey_data), GFP_KERNEL);
	if(emmckey_data == NULL){
		printk("%s:%d,kmalloc memory fail\n",__func__,__LINE__);
		return -ENOMEM;
	}

	emmckey_valid_node = emmckey_info->key_valid_node;
	while(emmckey_valid_node){
		memset(emmckey_data,0,sizeof(*emmckey_data));
		err = emmc_key_rw(provider,emmckey_valid_node,(u8*)emmckey_data,0);
		if(err == 0){
			emmc_key_transfer(emmckey_data->keyarea_mark,&emmckey_data->keyarea_mark_checksum,8,0);
			if (!memcmp(emmckey_data->keyarea_mark, EMMC_KEY_AREA_SIGNAL, 8)){
				checksum = emmckey_calculate_checksum(emmckey_data->data,EMMCKEY_DATA_VALID_LEN);
				if(checksum == emmckey_data->checksum){
					memcpy(buf,emmckey_data->data,size);
					err = 0;
					break;
				}
			}
		}
		emmckey_valid_node = emmckey_valid_node->next;
	}
	if(emmckey_data){
		kfree(emmckey_data);
	}
	return err;
}


static int32_t emmc_keybox_write(aml_keybox_provider_t * provider, uint8_t *buf,int32_t size)
{
	int err = 0;
	struct emmckey_valid_node_t *emmckey_valid_node;
	struct emmckey_data_t *emmckey_data;

	if(!emmckey_info->key_valid){
		printk("%s:%d,can't write emmc key\n",__func__,__LINE__);
		return -1;
	}
	if(size > EMMCKEY_DATA_VALID_LEN){
		printk("%s:%d,size is too big,fact:0x%x,need:0x%x\n",__func__,__LINE__,size,EMMCKEY_DATA_VALID_LEN);
		return -1;
	}
	emmckey_data = kmalloc(sizeof(*emmckey_data), GFP_KERNEL);
	if(emmckey_data == NULL){
		printk("%s:%d,kmalloc memory fail\n",__func__,__LINE__);
		return -ENOMEM;
	}
	
	memset(emmckey_data,0,sizeof(*emmckey_data));
	memcpy(emmckey_data->keyarea_mark, EMMC_KEY_AREA_SIGNAL, 8);
	emmc_key_transfer(emmckey_data->keyarea_mark,&emmckey_data->keyarea_mark_checksum,8,1);
	memcpy(emmckey_data->data,buf,size);
	emmckey_data->checksum = emmckey_calculate_checksum(emmckey_data->data,EMMCKEY_DATA_VALID_LEN);
	emmckey_valid_node = emmckey_info->key_valid_node;
	while(emmckey_valid_node){
		err = emmc_key_rw(provider,emmckey_valid_node,(u8*)emmckey_data,1);
		if(err != 0){
			printk("%s:%d,write key data fail,err:%d\n",__func__,__LINE__,err);
		}
		emmckey_valid_node = emmckey_valid_node->next;
	}
	if(emmckey_data){
		kfree(emmckey_data);
	}
	return err;
}


/*
 * 1  when key data is wrote to emmc, add checksum to verify if the data is correct;
 *    when key data is read from emmc, use checksum to verify if the data is correct.
 * 2  read/write size is constant 
 * 3  setup link table to link different area same as nand.
 * 4  key area is split 2 or more area to save key data
 * */

#if 0
//static char write_buf[2][50]={"write 2 : 22222222222","write 3 : 33333333333333"};
static char write_buf[50]={"write 2 : 22222222222"};

static char read_buf[100]={0};

#include <asm-generic/uaccess.h>
static int32_t emmc_keybox_read_file(aml_keybox_provider_t * provider, uint8_t *buf,int32_t size,int flags)
{
	char filename[3][30]={"/dev/cardblkinand2","/dev/cardblkinand3","/dev/cardblkinand4"};
	struct file *fp; 
	mm_segment_t fs; 
	loff_t pos;
	int i;
	//char *file = provider->priv;
	char *file;
	memset(read_buf,0,sizeof(read_buf));
	for(i=0;i<3;i++){
		file = &filename[i][0];
		fp = filp_open(file, O_RDWR, 0644); 
		if (IS_ERR(fp)) { 
			printk("open file error:%s \n",file); 
			return -1; 
		}
		printk("open file ok:%s \n",file); 
		fs = get_fs(); 
		set_fs(KERNEL_DS); 
		pos = 0; 
		//buf = &read_buf[i][0];
		//size = 55;
		vfs_read(fp, buf, size, &pos); 
		filp_close(fp, NULL); 
		set_fs(fs); 
		memcpy(&read_buf[0],buf,55);
		printk("%s:%d,read data:%s \n",__func__,__LINE__,&read_buf[0]); 
	}
	return 0;
}
static int32_t emmc_keybox_write_file(aml_keybox_provider_t * provider, uint8_t *buf,int32_t size)
{
	char filename[2][30]={"/dev/cardblkinand2","/dev/cardblkinand3"};
	struct file *fp; 
	mm_segment_t fs; 
	loff_t pos;
	int i;
	char *file;// = provider->priv;
	for(i=0;i<2;i++){
		file = &filename[i][0];
		fp = filp_open(file, O_RDWR, 0644); 
		if (IS_ERR(fp)) { 
			printk("open file error\n"); 
			return -1; 
		} 
		printk("%s:%d,open file ok:%s \n",__func__,__LINE__,file); 
		fs = get_fs(); 
		set_fs(KERNEL_DS); 
		pos = 0; 
		memcpy(buf,&write_buf[0],55);
		//size = 55;
		vfs_write(fp, buf, size, &pos); 
		filp_close(fp, NULL); 
		set_fs(fs); 
	}
	return 0;
}
#endif

static aml_keybox_provider_t emmc_provider;

static unsigned char *test_emmc_read_buf;
static unsigned char *test_emmc_write_buf;// ={"fffffffffffff  gggggggggg"};

#define TEST_STRING_1  "kernel test 11113451111111111"
#define TEST_STRING_2  "kernel 2222222789222"
#define TEST_STRING_3  "kernel test 3333333333333333 3333333333333333"
static void fill_data(void)
{
	//int i;
	memset(test_emmc_write_buf,0,2048);
	//for(i=0;i<EMMCKEY_DATA_VALID_LEN;i++){
	//	test_emmc_write_buf[i]= 'a';
	//}
	memcpy(&test_emmc_write_buf[0],TEST_STRING_1,sizeof(TEST_STRING_1));
	memcpy(&test_emmc_write_buf[512],TEST_STRING_2,sizeof(TEST_STRING_2));
	memcpy(&test_emmc_write_buf[1024],TEST_STRING_3,sizeof(TEST_STRING_3));

}

int emmc_key_read(void)
{
    int ret;

    test_emmc_read_buf = kmalloc(EMMCKEY_DATA_VALID_LEN*2, GFP_KERNEL);
    if (!test_emmc_read_buf) {
        printk("%s:%d,kmalloc memory fail\n",__func__,__LINE__);
        return -1;
    }

	memset(test_emmc_read_buf,0,1024*3);
	ret = emmc_keybox_read(&emmc_provider, test_emmc_read_buf,EMMCKEY_DATA_VALID_LEN,0);
    printk("%s:%d, read %s\n",__func__,__LINE__, (ret)? "error":"ok");
    
	printk("%s:%d,%s\n",__func__,__LINE__,test_emmc_read_buf);
	printk("%s:%d,%s\n",__func__,__LINE__,&test_emmc_read_buf[512]);
	printk("%s:%d,%s\n",__func__,__LINE__,&test_emmc_read_buf[1024]);
    kfree(test_emmc_read_buf);

    return ret;
}

int emmc_key_write(void)
{
    int ret;

    test_emmc_write_buf = kmalloc(EMMCKEY_DATA_VALID_LEN*2, GFP_KERNEL);
    if (!test_emmc_write_buf) {
        printk("%s:%d,kmalloc memory fail\n",__func__,__LINE__);
        return -1;
    }

	fill_data();
	ret = emmc_keybox_write(&emmc_provider,test_emmc_write_buf,EMMCKEY_DATA_VALID_LEN);
    printk("%s:%d, write %s\n",__func__,__LINE__, (ret)? "error":"ok");

    kfree(test_emmc_write_buf);
    
    return ret;
}

static aml_keybox_provider_t emmc_provider={
		.name="emmc_key",
		.read=emmc_keybox_read,
		.write=emmc_keybox_write,
	//	.read=emmc_keybox_read_file,
	//	.write=emmc_keybox_write_file,

};


int emmc_key_init (struct mmc_card *card)
{
	u64  addr=0;
	u32  size=0;
	u64  lba_start=0,lba_end=0;
	int err = 0;
    int bit = card->csd.read_blkbits;

    printk("card key: card_blk_probe. \n");
	emmckey_info = kmalloc(sizeof(*emmckey_info), GFP_KERNEL);
	if(emmckey_info == NULL){
		printk("%s:%d,kmalloc memory fail\n",__func__,__LINE__);
		return -ENOMEM;
	}
	memset(emmckey_info,0,sizeof(*emmckey_info));
	emmckey_info->key_init = 0;

    size = EMMCKEY_AREA_PHY_SIZE;
    addr = get_reserve_partition_off_from_tbl() + EMMCKEY_RESERVE_OFFSET;
    if(addr < 0){
        err = -EINVAL;
        goto exit_err;
    }
    lba_start = addr >> bit; 
    lba_end = (addr + size) >> bit;
    emmckey_info->key_init = 1;
    printk("%s:%d emmc key lba_start:0x%llx,lba_end:0x%llx \n",__func__,__LINE__,lba_start,lba_end);

	if(!emmckey_info->key_init){
        err = -EINVAL;

		printk("%s:%d,emmc key init fail\n",__func__,__LINE__);
        goto exit_err;
	}
	emmckey_info->keyarea_phy_addr = addr;
	emmckey_info->keyarea_phy_size = size;
	emmckey_info->lba_start = lba_start;
	emmckey_info->lba_end   = lba_end;
	emmc_provider.priv=card;
	err = aml_emmc_key_check(&emmc_provider);
	if(err){
		printk("%s:%d,emmc key check fail\n",__func__,__LINE__);
        goto exit_err;
	}
	err = aml_keybox_provider_register(&emmc_provider);
	if(err){
        printk("emmc key register fail.\n");
	}
	printk("emmc key: %s:%d ok. \n",__func__,__LINE__);
	return err;

exit_err:
	if(emmckey_info) {
        kfree(emmckey_info);
	}
	return err;
}

