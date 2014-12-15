#include <linux/err.h>
#include <linux/device.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <mach/am_regs.h>
#include <mach/irqs.h>
#include <mach/card_io.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/partitions.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/mtd/mtd.h>
#include <linux/list.h>
#include <linux/mmc/card.h>

#include "mmc_storage.h"

extern int mmc_read_internal (struct mmc_card *card, unsigned dev_addr,
        unsigned blocks, void *buf);
extern int mmc_write_internal (struct mmc_card *card, unsigned dev_addr,
        unsigned blocks, void *buf);

struct mmc_storage_info_t *mmc_storage = NULL;

int mmc_storage_test(struct mmc_card *card);


struct list_head  storage_node_list;
void print_storage_node_info(void)
{
	struct storage_node_t * node = NULL;

	list_for_each_entry(node, &storage_node_list, storage_list){
		store_msg("node->offset_addr %llx , node->timestamp %d ,node->valid_node_flag %d",\
			node->offset_addr, node->timestamp,node->valid_node_flag);
	}
	return;	
}

static void show_data_buf(unsigned char *data_buf)
{
	int i= 0;
	for(i=0;i<512;i++){
		store_msg("data_buf[%d] = %x",i,data_buf[i]);
	}
	return;
}

struct mmc_card * storage_device = NULL;

static unsigned  mmc_checksum(unsigned char *buf,unsigned int lenth)
{
	unsigned int checksum = 0;
	unsigned int cnt;
	for(cnt=0;cnt<lenth;cnt++){
		checksum += buf[cnt];
	}
	store_dbg("mmc_checksum : caculate checksum %d",checksum);
	return checksum;
}

static inline void init_magic(unsigned char * magic)
{
	unsigned char  *source_magic=MMC_STORAGE_MAGIC;
	int i=0;
	for(i = 0; i < 11; i++){
		magic[i] = source_magic[i];
	}
	return;
}

static int check_data(void * cmp)
{
	struct mmc_storage_head_t * head = (struct mmc_storage_head_t * )cmp;
	int ret =0;
	store_dbg("check_data :head->checksum = %d",head->checksum);
	if(head->checksum != mmc_checksum(&(head->data[0]),MMC_STORAGE_AREA_VALID_SIZE)){
		ret = -1;
		store_msg("mmc storage data check_sum failed\n");
	}

	if(!ret){
		store_dbg("mmc storage data check_sum OK\n");
	}
	return ret;

}

static int check_magic(void * cmp, unsigned char * magic)
{
	struct mmc_storage_head_t * head = (struct mmc_storage_head_t * )cmp;
	int ret =0, i = 0;

	for(i = 0; i < 11; i++ ){
		if(head->magic[i] != magic[i]){
			ret = -1;
			break;
		}
	}
	if(!ret)
	store_dbg("check_magic right");
	/*if(head->magic_checksum != mmc_checksum(&(head->magic),MMC_STORAGE_MAGIC_SIZE)){
		ret = -2;
	}*/
	
	return ret ;
}

static int storage_check(struct mmc_card *device, void * buf)
{
	struct mmc_storage_head_t * storage_data =(struct mmc_storage_head_t *)buf;
	unsigned char magic[MMC_STORAGE_MAGIC_SIZE];
	int ret = 0;
	
	init_magic(&magic[0]);
	store_dbg("storage_check :source magic : %s",magic);
	
	ret = check_magic(storage_data,&magic[0]);
	if(ret){
		store_msg("mmc read storage check magic name failed and ret = %d\n",ret);
		return ret;
	}

	ret = check_data(storage_data);
	if(ret){
		store_msg("mmc check data failed");
	}
	
	return ret;
}

// direct : 0 read  / 1 write
int mmc_storage_rw_kernel(struct mmc_card *device, struct storage_node_t * storage_node, unsigned char * databuf, int len, int direct)
{
    int ret=0, start_blk, size, force_size,blk_cnt,tmp_size;
    int bit = device->csd.read_blkbits;
    int blk_size = 1 << bit; // size of a block	
    
	mmc_claim_host(device->host);

    ret = mmc_blk_main_md_part_switch(device);
    if (ret) {
        pr_err("%s: error %d mmc_blk_main_md_part_switch\n", __FUNCTION__, ret);
        goto exit_err;
    }

	start_blk = (storage_node->offset_addr >> bit);
	size =len >> bit;
	force_size = MMC_STORAGE_DEFAULT_SIZE >> bit;

	if(direct == 0){
		while(size > 0){	
			ret = mmc_read_internal(device,start_blk,force_size,databuf);
			if(ret != 0){
				store_msg("mmc_storage_rw_kernel : read storage at block %d failed",start_blk);
			}
			start_blk += force_size;
			databuf += (force_size << bit);
			size -=force_size;
		}
	}else{
		while(size > 0){
			ret = mmc_write_internal(device,start_blk,force_size,databuf);
			if(ret != 0){
				store_msg("mmc_storage_rw_kernel : read storage at block %d failed",start_blk);
			}
			start_blk += force_size;
			databuf += (force_size << bit);
			size -=force_size;
		}
	}

exit_err: 
    mmc_release_host(device->host);
	return ret;
}

int mmc_storage_read(struct mmc_card *device, unsigned char * buf, int len)
{
	struct mmc_storage_head_t * storage_data= NULL;
	struct storage_node_t * storage_node = NULL;
	int part_num, ret = 0, read_len = 0;
	char valid_node_failed = 0;

	if(!mmc_storage->secure_valid){
		store_msg("mmc stoarge invaild : do not read");
		return - STORAGE_READ_FAILED;
	}
	storage_data = kmalloc(sizeof(struct mmc_storage_head_t ),GFP_KERNEL);
	if(storage_data == NULL){
		store_msg("mmc_storage_read : malloc failed !");
		return -1;
	}
	
	part_num = 0;
	read_len = MMC_STORAGE_AREA_SIZE;
	
	// read vaild node
	list_for_each_entry(storage_node,&storage_node_list,storage_list){
		if((storage_node != NULL) && (storage_node->valid_node_flag == 1)){
			memset((unsigned char *)storage_data,0x0,sizeof(struct mmc_storage_head_t));
			ret = mmc_storage_rw_kernel(device, storage_node, storage_data, read_len,0);
			if(ret){
				store_msg("storage read failed");
			}
			ret = storage_check(device,storage_data);
			if(ret){
				store_msg("storage check valid node failed");
				valid_node_failed  = 1;
			}
			if(!valid_node_failed){
				store_msg("mmc read storage ok at :  %llx",storage_node->offset_addr);
			}
		}
	}

	// if vaild node failed ,read the bak node
	if(valid_node_failed){
		valid_node_failed = 0;
		list_for_each_entry(storage_node,&storage_node_list,storage_list){
			if((storage_node != NULL) && (storage_node->valid_node_flag == 0)){
				memset((unsigned char *)storage_data,0x0,sizeof(struct mmc_storage_head_t));
				ret = mmc_storage_rw_kernel(device, storage_node, storage_data, read_len,0);
				if(ret){
					store_msg("storage read failed");
				}
				ret = storage_check(device,storage_data);
				if(ret){
					store_msg("storage check free node failed");
					valid_node_failed  = 1;
				}

				if(!valid_node_failed){
					store_msg("mmc read storage ok at :  %llx",storage_node->offset_addr);
				}
			}
		}
	}

	if(!valid_node_failed){
#ifdef MMC_STORAGE_DEBUG
		store_msg("mmc_storage_read : show read buf: ");
		show_data_buf(&storage_data->data[0]);
#endif
		memcpy(buf,&storage_data->data[0],len);
		ret = 0;
	}
	else{
		store_msg("mmc read storage failed at");
		ret = -STORAGE_READ_FAILED;
	}
	
	if(storage_data){
		kfree(storage_data);
		storage_data =NULL;
	}

	return ret;
}

int mmc_storage_write(struct mmc_card *device, unsigned char * buf, int len)
{
	struct mmc_storage_head_t * storage_data;
	struct storage_node_t * storage_node = NULL;
	int part_num, ret = 0, write_len = 0;
	char write_failed_flag = 0;
	store_dbg("%s %d",__func__,__LINE__);

	part_num = 0;
	write_len = MMC_STORAGE_AREA_SIZE;
	storage_data = kmalloc(sizeof(struct mmc_storage_head_t ),GFP_KERNEL);
	if(storage_data == NULL){
		store_msg("mmc_storage_write : malloc failed !");
		return -1;
	}
	
	init_magic(&storage_data->magic[0]);
	
	storage_data->magic_checksum = mmc_checksum(&storage_data->magic[0],MMC_STORAGE_MAGIC_SIZE);
	storage_data->timestamp = 0;
	storage_data->version = 0;
	
	memcpy(&storage_data->data[0], buf, len);
#ifdef MMC_STORAGE_DEBUG	
	store_msg("mmc_storage_write : show write buf : ");
	show_data_buf(&storage_data->data[0]);
#endif
	storage_data->checksum = mmc_checksum(&storage_data->data[0],MMC_STORAGE_AREA_VALID_SIZE);

	//show_data_buf(&storage_data);
	list_for_each_entry(storage_node,&storage_node_list,storage_list){
		if((storage_node != NULL) && (storage_node->valid_node_flag == 1)){
			storage_data->timestamp = storage_node->timestamp +1; //valid node
		}
	}
	
	list_for_each_entry(storage_node,&storage_node_list,storage_list){
		if((storage_node != NULL) && (part_num < MMC_STORAGE_AREA_COUNT)){
			write_failed_flag = 0;
			ret = mmc_storage_rw_kernel(device, storage_node, storage_data, write_len,1);
			if(ret){
				store_msg("storage write part %d failed at %llx",part_num,storage_node->offset_addr );
				write_failed_flag = 1; 
			}
			part_num++;
			
			if(!write_failed_flag){
				mmc_storage->secure_valid = 1;
				store_msg("storage write part %d success at %llx", part_num,storage_node->offset_addr);
			}
		}
	}
	

	if(storage_data){
		kfree(storage_data);
		storage_data =NULL;
	}
	return ret;
}

int mmc_storage_init(struct mmc_card *device)
{
	struct mmc_storage_head_t *data_buf = NULL;
	struct storage_node_t part0_node, part1_node,tmp_node;
	int cnt_num = 0, part_num = 0,ret = 0;
	uint64_t addr = 0;
	
	data_buf = kmalloc(sizeof(struct mmc_storage_head_t ),GFP_KERNEL);
	if(data_buf == NULL){
		store_msg("mmc_storage_init : data_buf malloc failed");
		goto exit;
	}
	
	//store_msg("sizeof(struct mmc_storage_head_t ) = %d, size = %d",sizeof(struct mmc_storage_head_t ),(sizeof(struct mmc_storage_head_t )>>9));
	memset(data_buf,0x0,sizeof(struct mmc_storage_head_t ));
	
	mmc_storage->secure_init = 1;
	part0_node.offset_addr = 0;
	part0_node.valid_flag  = 0;
	part1_node.valid_flag  = 0;
	part_num = MMC_STORAGE_AREA_COUNT;
	
	addr = MMC_STORAGE_OFFSET;
	for(cnt_num = 0; cnt_num < part_num; cnt_num++){

		tmp_node.offset_addr = addr;
		ret = mmc_storage_rw_kernel(device, &tmp_node, (unsigned char *)data_buf, MMC_STORAGE_AREA_SIZE,0);
		if(ret){
			store_msg("storage read failed");
			addr += MMC_STORAGE_AREA_SIZE;
			continue;
		}
		//show_data_buf(data_buf);
		ret = storage_check(device,data_buf);
		if(ret){
			store_msg("storage check : invalid storage in addr %llx",addr);
		}else{
			mmc_storage->secure_valid = 1;
			if(cnt_num == 0){
				part0_node.offset_addr = addr;
				part0_node.timestamp = data_buf->timestamp;
				part0_node.valid_flag = 1;
			}else if(cnt_num == 1){
				part1_node.offset_addr = addr;
				part1_node.timestamp = data_buf->timestamp;
				part1_node.valid_flag = 1;
			}else{
				store_msg("wrong cnt_num %d",cnt_num);
				break;
			}
		}
		addr += MMC_STORAGE_AREA_SIZE;
	}

	if(mmc_storage->secure_valid == 1){
		
		if(part0_node.valid_flag && part1_node.valid_flag ){
			if(part0_node.timestamp >= part1_node.timestamp){
				memcpy(mmc_storage->valid_node,&part0_node,sizeof(struct storage_node_t));
				memcpy(mmc_storage->free_node,&part1_node,sizeof(struct storage_node_t));
			}else{
				memcpy(mmc_storage->valid_node,&part1_node,sizeof(struct storage_node_t));
				memcpy(mmc_storage->free_node,&part0_node,sizeof(struct storage_node_t));
			}
			mmc_storage->valid_node->valid_node_flag = 1; 
			mmc_storage->free_node->valid_node_flag = 0; 

			list_add_tail(&mmc_storage->valid_node->storage_list,&storage_node_list);
			list_add_tail(&mmc_storage->free_node->storage_list,&storage_node_list);

			store_msg("mmc  storage node0  addr = %llx and node1 addr = %llx",part0_node.offset_addr,part1_node.offset_addr);
		}else if(part0_node.valid_flag && (!part1_node.valid_flag)){

			memcpy(mmc_storage->valid_node,&part0_node,sizeof(struct storage_node_t));
			mmc_storage->valid_node->valid_node_flag = 1; 
			list_add_tail(&mmc_storage->valid_node->storage_list,&storage_node_list);

			part1_node.offset_addr = MMC_STORAGE_OFFSET + MMC_STORAGE_AREA_SIZE;
			part1_node.timestamp = 0;
			part1_node.valid_flag = 0;
			part1_node.valid_node_flag = 0;

			memcpy(mmc_storage->free_node,&part1_node,sizeof(struct storage_node_t));
			mmc_storage->free_node->valid_node_flag = 0; 
			list_add_tail(&mmc_storage->free_node->storage_list,&storage_node_list);

			store_msg("mmc storage node0 in mmc addr = %llx",part0_node.offset_addr);
		}else if(part1_node.valid_flag && (!part0_node.valid_flag)){

			memcpy(mmc_storage->valid_node,&part1_node,sizeof(struct storage_node_t));
			mmc_storage->valid_node->valid_node_flag = 1; 
			list_add_tail(&mmc_storage->valid_node->storage_list,&storage_node_list);

			part0_node.offset_addr = MMC_STORAGE_OFFSET;
			part0_node.timestamp = 0;
			part0_node.valid_flag = 0;
			part0_node.valid_node_flag = 0;

			memcpy(mmc_storage->free_node,&part0_node,sizeof(struct storage_node_t));
			mmc_storage->free_node->valid_node_flag = 0; 
			list_add_tail(&mmc_storage->free_node->storage_list,&storage_node_list);

			store_msg("mmc storage node1 in mmc addr = %llx",part1_node.offset_addr);
		}
	}
	else{
		store_msg("##mmc do not find storage##");
	}
	
exit:
	if(data_buf){
		kfree(data_buf);
		data_buf = NULL;
	}
	
	return 0;
}

int mmc_storage_check(struct mmc_card *device)
{
	unsigned char *data_buf = NULL;
	int ret = 0,len;
	
	store_dbg("%s %d",__func__,__LINE__);
	mmc_storage->secure_valid = 0;
	mmc_storage->secure_init= 0;
	//device->storage_protect = 1;
	
	ret = mmc_storage_init(device);
	if(ret){
		store_msg("mmc_storage_init failed");
		goto exit_error;
	}

	if(mmc_storage->secure_valid == 0){
		

		mmc_storage->valid_node->offset_addr = MMC_STORAGE_OFFSET;
		mmc_storage->valid_node->valid_node_flag = 1;
		mmc_storage->valid_node->timestamp = 0;
		mmc_storage->valid_node->valid_flag = 0;
		list_add_tail(&mmc_storage->valid_node->storage_list,&storage_node_list);

		mmc_storage->free_node->offset_addr = MMC_STORAGE_OFFSET + MMC_STORAGE_AREA_SIZE;
		mmc_storage->free_node->valid_node_flag = 0;
		mmc_storage->free_node->timestamp = 0;
		mmc_storage->free_node->valid_flag = 0;
		list_add_tail(&mmc_storage->free_node->storage_list,&storage_node_list);

		data_buf = kmalloc(MMC_STORAGE_AREA_VALID_SIZE,GFP_KERNEL);
		if(data_buf == NULL){
			store_msg("mmc_storage_check : data_buf malloc failed");
			goto exit_error;
		}
		memset(data_buf,0x0,MMC_STORAGE_AREA_VALID_SIZE);
		len = MMC_STORAGE_AREA_VALID_SIZE;
		
		ret =  mmc_storage_write(device,data_buf,len);
		if(ret){
			store_msg("mmc_storage_write failed");
			ret = -STORAGE_WRITE_FAILED;
			goto exit_error;
		}
	}

exit_error:

	if(data_buf){
		kfree(data_buf);
		data_buf = NULL;
	}
	
	return ret;
}

static void storage_buf_free(void)
{
	if(mmc_storage->valid_node ){
		kfree(mmc_storage->valid_node );
		mmc_storage->valid_node  = NULL;
	}

	if(mmc_storage->free_node){
		kfree(mmc_storage->free_node);
		mmc_storage->free_node = NULL;
	}

	if(mmc_storage){
		kfree(mmc_storage);
		mmc_storage = NULL;
	}
}

static int storage_buf_malloc(struct mmc_card *device)
{
	int ret = 0;
	mmc_storage= kmalloc(sizeof(struct mmc_storage_info_t),GFP_KERNEL);
	if(mmc_storage == NULL){
		ret = -MMC_MALLOC_FAILED;
		goto exit_error;
	}
	memset(mmc_storage,0x0,sizeof(struct mmc_storage_info_t));
	mmc_storage->valid_node = kmalloc(sizeof(struct storage_node_t),GFP_KERNEL);
	if(mmc_storage->valid_node == NULL){
		ret = -MMC_MALLOC_FAILED;
		goto exit_error;
	}
	
	memset(mmc_storage->valid_node ,0x0,sizeof(struct storage_node_t));
	mmc_storage->free_node = kmalloc(sizeof(struct storage_node_t),GFP_KERNEL);
	if(mmc_storage->free_node == NULL){
		ret = -MMC_MALLOC_FAILED;
		goto exit_error;
	}
	memset(mmc_storage->free_node ,0x0,sizeof(struct storage_node_t));

	return ret;
	
exit_error:


	if(mmc_storage->free_node){
		kfree(mmc_storage->free_node);
		mmc_storage->free_node = NULL;
	}
	
	if(mmc_storage->valid_node ){
		kfree(mmc_storage->valid_node );
		mmc_storage->valid_node  = NULL;
	}
	if(mmc_storage){
		kfree(mmc_storage);
		mmc_storage = NULL;
	}

	return ret;
}

int mmc_storage_probe(struct mmc_card *card)
{
	int ret = 0;
	store_msg("%s %d",__func__,__LINE__);
	ret = storage_buf_malloc(card);
	if(ret){
		store_msg("mmc storage malloc failed");
		goto exit_error;
	}
	INIT_LIST_HEAD(&storage_node_list);
	
	ret = mmc_storage_check(card);
	if(ret){
		store_msg("mmc storage check failed");
		goto exit_error;
	}
	storage_device = card;
	
#ifdef MMC_STORAGE_DEBUG
	int err = mmc_storage_test(card);
	if(err){
		store_msg("test failed");
	}else{
		store_msg("test OK");
	}
#endif

	return ret;
	
exit_error:

	storage_buf_free();
	return ret;
}


int mmc_storage_exit(void)
{
	int ret =0;
	
	storage_buf_free();

	return ret;
}

int mmc_secure_storage_ops(unsigned char * buf, unsigned int len, int wr_flag)
{
	struct mmc_card *device= NULL;
	int err = 0;
	
	if(len > MMC_STORAGE_AREA_VALID_SIZE){
		store_msg("mmc secure storage write fail,len 0x%x is bigger than 0x%x,%s:%d",len,MMC_STORAGE_AREA_VALID_SIZE,__func__,__LINE__);
		return -1;
	}
	
	device =  storage_device;

	if(!device){
		store_msg("mmc storage write/read failed : NO storage device");
		return -1;
	}

	if(wr_flag){ // write
		err = mmc_storage_write(device,buf, len);
		if(err){
			store_msg("secure_storage_mmc_ops write failed");
		}
	}else{ //read
		err = mmc_storage_read(device,buf, len);
		if(err){
			store_msg("secure_storage_mmc_ops read failed");
		}
	}
	
	return err;
}

#ifdef MMC_STORAGE_DEBUG

int mmc_storage_test(struct mmc_card *card)
{
	unsigned char * buf = NULL;
	unsigned char * cmp = NULL;
	int ret = 0;

	buf = kmalloc(MMC_STORAGE_AREA_VALID_SIZE,GFP_KERNEL);
	if(!buf){
		store_msg("mmc_storage_test : malloc failed");
		return -1;
	}
	memset(buf,0x33,MMC_STORAGE_AREA_VALID_SIZE);

	cmp = kmalloc(MMC_STORAGE_AREA_VALID_SIZE,GFP_KERNEL);
	if(!cmp){
		store_msg("mmc_storage_test : malloc failed");
		return -1;
	}
	memset(cmp,0x0,MMC_STORAGE_AREA_VALID_SIZE);
	store_msg("mmc_storage_test : write ");

	//write
	ret  = mmc_secure_storage_ops(buf,MMC_STORAGE_AREA_VALID_SIZE,1);
	if(ret){
		store_msg("mmc_storage_test : ops write failed");
		return -1;
	}
	
	store_msg("mmc_storage_test : read ");
	//read
	ret  = mmc_secure_storage_ops(cmp,MMC_STORAGE_AREA_VALID_SIZE,0);
	if(ret){
		store_msg("mmc_storage_test : ops read failed");
		return -1;
	}
	//show_data_buf(cmp);
	if(memcmp(buf,cmp,MMC_STORAGE_AREA_VALID_SIZE)){
		store_msg("mmc storage test :  failed");
		return -1;
	}

	if(!ret)
	store_msg("mmc storage test :  right");

	return ret;
}

#endif

