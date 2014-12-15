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

#define KEYSECTORSIZE_SHIFT 9
#define KEYSECTORSIZE (1<<KEYSECTORSIZE_SHIFT)
#define KEYBLOCKSIZE_SHIFT  20
#define KEYBLOCKSIZE  (1<<KEYBLOCKSIZE_SHIFT)

#if 0
#define KEYSIZE (CONFIG_KEYSIZE - (sizeof(uint32_t)))
typedef	struct  {
	uint32_t	crc;		/* CRC32 over data bytes	*/
	unsigned char	data[KEYSIZE]; /* Environment data		*/
} mesonkey_t;
#endif

int  default_keyironment_size =  0;
#if 0
static int aml_card_read_key (struct memory_card  *card, size_t offset, u_char * buf)
{
	struct key_oobinfo_t *key_oobinfo;
	int error = 0, start_blk, total_blk;
	size_t addr = offset;
	size_t amount_loaded = 0;
	size_t len;
	unsigned char *data_buf;

	if (!card->key_info->env_valid)
		return 1;
#if 1	
	addr = card->key_info->env_valid_node->phy_blk_addr;
	addr *= (1<<KEYBLOCKSIZE_SHIFT);
	addr += card->key_info->env_valid_node->phy_page_addr * KEYSECTORSIZE;
#endif

	data_buf = kzalloc(KEYSECTORSIZE, GFP_KERNEL);
	if (data_buf == NULL)
		return -ENOMEM;

	key_oobinfo = (struct key_oobinfo_t *)data_buf;
	while (amount_loaded < CONFIG_KEYSIZE ) {

		error = sd_mmc_read_data(card->card_info,addr>>KEYSECTORSIZE_SHIFT,KEYSECTORSIZE,data_buf);
		if ((error != 0) && (error != -EUCLEAN)) {
			printk("blk check good but read failed: %llx, %d\n", (uint64_t)addr, error);
			return 1;
		}

		if (memcmp(key_oobinfo->name, ENV_KEY_MAGIC, 4)) 
			printk("invalid card key magic: %llx\n", (uint64_t)addr);

		addr += KEYSECTORSIZE;
		len = min(KEYSECTORSIZE, CONFIG_KEYSIZE - amount_loaded);
		memcpy(buf + amount_loaded, data_buf, len);
		amount_loaded += KEYSECTORSIZE;
	}
	if (amount_loaded < CONFIG_KEYSIZE)
		return 1;

	kfree(data_buf);
	return 0;
}

static int aml_card_write_key(struct memory_card  *card, loff_t offset, u_char *buf)
{
	struct key_oobinfo_t *key_oobinfo;
	int error = 0;
	loff_t addr = 0;
	size_t amount_saved = 0;
	size_t len;
	unsigned char *data_buf;

	data_buf = kzalloc(KEYSECTORSIZE, GFP_KERNEL);
	if (data_buf == NULL)
		return -ENOMEM;

	addr = offset;
	key_oobinfo = (struct key_oobinfo_t *)data_buf;
	memcpy(key_oobinfo->name, ENV_KEY_MAGIC, 4);
	key_oobinfo->ec = card->key_info->env_valid_node->ec;
	key_oobinfo->timestamp = card->key_info->env_valid_node->timestamp;
	key_oobinfo->status_page = 1;

	while (amount_saved < CONFIG_KEYSIZE ) {

		len = min(KEYSECTORSIZE, CONFIG_KEYSIZE - amount_saved);

		error = sd_mmc_write_data(card->card_info,addr>>KEYSECTORSIZE_SHIFT,KEYSECTORSIZE,data_buf);
		if (error) {
			printk("blk check good but write failed: %llx, %d\n", (uint64_t)addr, error);
			return 1;
		}

		addr += KEYSECTORSIZE;;
		amount_saved += KEYSECTORSIZE;
	}
	if (amount_saved < CONFIG_KEYSIZE)
		return 1;

	card->key_info->env_valid=1;
	kfree(data_buf);
	return 0;
}

static int aml_card_save_key(struct memory_card  *card, u_char *buf)
{
	struct key_free_node_t *env_free_node, *key_tmp_node;
	int error = 0, pages_per_blk, i = 1;
	loff_t addr = 0;
	mesonkey_t *key_ptr = (mesonkey_t *)buf;

	if (!card->key_info->env_init) 
		return 1;

	pages_per_blk = KEYBLOCKSIZE / KEYSECTORSIZE;
	if ((KEYSECTORSIZE < CONFIG_KEYSIZE) && (card->key_info->env_valid == 1))
		i = (CONFIG_KEYSIZE + KEYSECTORSIZE - 1) / KEYSECTORSIZE;

	if (card->key_info->env_valid) {
		card->key_info->env_valid_node->phy_page_addr += i;
		if ((card->key_info->env_valid_node->phy_page_addr + i) > pages_per_blk) {

			env_free_node = kzalloc(sizeof(struct key_free_node_t), GFP_KERNEL);
			if (env_free_node == NULL)
				return -ENOMEM;

			env_free_node->phy_blk_addr = card->key_info->env_valid_node->phy_blk_addr;
			env_free_node->ec = card->key_info->env_valid_node->ec;
			key_tmp_node = card->key_info->env_free_node;
			while (key_tmp_node->next != NULL) {
				key_tmp_node = key_tmp_node->next;
			}
			key_tmp_node->next = env_free_node;

			key_tmp_node = card->key_info->env_free_node;
			card->key_info->env_valid_node->phy_blk_addr = key_tmp_node->phy_blk_addr;
			card->key_info->env_valid_node->phy_page_addr = 0;
			card->key_info->env_valid_node->ec = key_tmp_node->ec;
			card->key_info->env_valid_node->timestamp += 1;
			card->key_info->env_free_node = key_tmp_node->next;
			kfree(key_tmp_node);
		}
	}
	else {

		key_tmp_node = card->key_info->env_free_node;
		card->key_info->env_valid_node->phy_blk_addr = key_tmp_node->phy_blk_addr;
		card->key_info->env_valid_node->phy_page_addr = 0;
		card->key_info->env_valid_node->ec = key_tmp_node->ec;
		card->key_info->env_valid_node->timestamp += 1;
		card->key_info->env_free_node = key_tmp_node->next;
		kfree(key_tmp_node);
	}

	addr = card->key_info->env_valid_node->phy_blk_addr;
	addr *= KEYBLOCKSIZE;
	addr += card->key_info->env_valid_node->phy_page_addr * KEYSECTORSIZE;
	if (card->key_info->env_valid_node->phy_page_addr == 0) {
		card->key_info->env_valid_node->ec++;
	}

	if (aml_card_write_key(card, addr, (u_char *) key_ptr)) {
		printk("update card key FAILED!\n");
		return 1;
	}
	
	return error;
}

static int aml_card_key_init(struct memory_card  *card)
{
	struct key_oobinfo_t *key_oobinfo;
	struct key_free_node_t *env_free_node, *key_tmp_node, *key_prev_node;
	int error = 0, start_blk, total_blk, key_blk, i, pages_per_blk, bad_blk_cnt = 0, max_key_blk, phys_erase_shift;
	loff_t offset;
	unsigned char *data_buf;

	data_buf = kzalloc(KEYSECTORSIZE, GFP_KERNEL);
	if (data_buf == NULL)
		return -ENOMEM;

	card->key_info = kzalloc(sizeof(struct aml_key_info_t), GFP_KERNEL);
	if (card->key_info == NULL)
		return -ENOMEM;

	card->key_info->owner= card;
	card->key_info->env_valid_node = kzalloc(sizeof(struct key_valid_node_t), GFP_KERNEL);
	if (card->key_info->env_valid_node == NULL)
		return -ENOMEM;
	card->key_info->env_valid_node->phy_blk_addr = -1;

	phys_erase_shift = KEYBLOCKSIZE_SHIFT;
	max_key_blk = (MINIKEY_PART_SIZE >> phys_erase_shift);
	if (max_key_blk < 2)
		max_key_blk = 2;

	offset = (card->card_plat_info->partitions[0].offset+card->card_plat_info->partitions[0].size-MINIKEY_PART_SIZE);
	if(offset<0)
		offset = MINIKEY_PART_SIZE;
	start_blk = (int)(offset >> phys_erase_shift);
	total_blk = (int)(card->capacity >> phys_erase_shift);
	key_oobinfo = (struct key_oobinfo_t *)data_buf;

	key_blk = 0;
	do {
		
		error = sd_mmc_read_data(card->card_info, start_blk, KEYSECTORSIZE,data_buf);
		if ((error != 0) && (error != -EUCLEAN)) {
			printk("blk check good but read failed: %llx, %d\n", (uint64_t)offset, error);
			continue;
		}

		card->key_info->env_init = 1;
		if (!memcmp(key_oobinfo->name, ENV_KEY_MAGIC, 4)) {
			card->key_info->env_valid = 1;
			if (card->key_info->env_valid_node->phy_blk_addr >= 0) {
				env_free_node = kzalloc(sizeof(struct key_free_node_t), GFP_KERNEL);
				if (env_free_node == NULL)
					return -ENOMEM;

				env_free_node->dirty_flag = 1;
				if (key_oobinfo->timestamp > card->key_info->env_valid_node->timestamp) {

					env_free_node->phy_blk_addr = card->key_info->env_valid_node->phy_blk_addr;
					env_free_node->ec = card->key_info->env_valid_node->ec;
					card->key_info->env_valid_node->phy_blk_addr = start_blk;
					card->key_info->env_valid_node->phy_page_addr = 0;
					card->key_info->env_valid_node->ec = key_oobinfo->ec;
					card->key_info->env_valid_node->timestamp = key_oobinfo->timestamp;	
				}
				else {
					env_free_node->phy_blk_addr = start_blk;
					env_free_node->ec = key_oobinfo->ec;
				}
				if (card->key_info->env_free_node == NULL)
					card->key_info->env_free_node = env_free_node;
				else {
					key_tmp_node = card->key_info->env_free_node;
					while (key_tmp_node->next != NULL) {
						key_tmp_node = key_tmp_node->next;
					}
					key_tmp_node->next = env_free_node;
				}
			}
			else {

				card->key_info->env_valid_node->phy_blk_addr = start_blk;
				card->key_info->env_valid_node->phy_page_addr = 0;
				card->key_info->env_valid_node->ec = key_oobinfo->ec;
				card->key_info->env_valid_node->timestamp = key_oobinfo->timestamp;	
			}
		}
		else if (key_blk < max_key_blk) {
			env_free_node = kzalloc(sizeof(struct key_free_node_t), GFP_KERNEL);
			if (env_free_node == NULL)
				return -ENOMEM;

			env_free_node->phy_blk_addr = start_blk;
			env_free_node->ec = key_oobinfo->ec;
			if (card->key_info->env_free_node == NULL)
				card->key_info->env_free_node = env_free_node;
			else {
				key_tmp_node = card->key_info->env_free_node;
				key_prev_node = key_tmp_node;
				while (key_tmp_node != NULL) {
					if (key_tmp_node->dirty_flag == 1)
						break;
					key_prev_node = key_tmp_node;
					key_tmp_node = key_tmp_node->next;
				}
				if (key_prev_node == key_tmp_node) {
					env_free_node->next = key_tmp_node;
					card->key_info->env_free_node = env_free_node;
				}
				else {
					key_prev_node->next = env_free_node;
					env_free_node->next = key_tmp_node;
				}
			}
		}
		key_blk++;
		if ((key_blk >= max_key_blk) && (card->key_info->env_valid == 1))
			break;

	} while ((++start_blk) < total_blk);

	if ((KEYSECTORSIZE < CONFIG_KEYSIZE) && (card->key_info->env_valid == 1)) {
		i = (CONFIG_KEYSIZE + KEYSECTORSIZE - 1) / KEYSECTORSIZE;
		card->key_info->env_valid_node->phy_page_addr -= (i - 1);
	}

	offset = card->key_info->env_valid_node->phy_blk_addr;
	offset *= KEYBLOCKSIZE;
	offset += card->key_info->env_valid_node->phy_page_addr * KEYSECTORSIZE;
	printk("aml card key valid addr: %llx \n", (uint64_t)offset);
	printk(KERN_DEBUG "CONFIG_KEYSIZE=0x%x; KEYSIZE=0x%x;\n",
		CONFIG_KEYSIZE, KEYSIZE);
	kfree(data_buf);
	return 0;
}

static int aml_card_key_check(struct memory_card  *card)
{
	mesonkey_t *key_ptr;
	int error = 0, start_blk, total_blk, update_key_flag = 0, i, j, nr, phys_erase_shift;
	loff_t offset;

	error = aml_card_key_init(card);
	if (error)
		return error;
	key_ptr = kzalloc(sizeof(mesonkey_t), GFP_KERNEL);
	if (key_ptr == NULL)
		return -ENOMEM;

	if (card->key_info->env_valid == 1) {
#if 1
		offset = card->key_info->env_valid_node->phy_blk_addr;
		offset *= KEYBLOCKSIZE;
		offset += card->key_info->env_valid_node->phy_page_addr * KEYSECTORSIZE;

		error = aml_card_read_key (card, offset, (u_char *)key_ptr);
		if (error) {
			printk("card key read failed: %llx, %d\n", (uint64_t)offset, error);
			goto exit;
		}
#endif 	
	}else {
			update_key_flag =1 ;
	}
		
	if (update_key_flag) {
		error = aml_card_save_key(card, (u_char *)key_ptr);
		if (error) {
			printk("card key save failed: %d\n", error);
			goto exit;
		}
	}

exit:
	kfree(key_ptr);
	return 0;
}


static struct memory_card  *card_key_card = NULL;
static int card_key_open(struct inode * inode, struct file * filp)
{
	return 0;
}
/*
 * This funcion reads the u-boot keyionment variables. 
 * The f_pos points directly to the key location.
 */
static ssize_t card_key_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	mesonkey_t *key_ptr = NULL;
	ssize_t read_size;
	int error = 0;
	if(*ppos == CONFIG_KEYSIZE)
	{
		return 0;
	}

	if(*ppos >= CONFIG_KEYSIZE)
	{
		printk(KERN_ERR "card key: data access violation!\n");
		return -EFAULT;
	}

	key_ptr = kzalloc(sizeof(mesonkey_t), GFP_KERNEL);
	if (key_ptr == NULL)
	{
		return -ENOMEM;
	}
	
	error = aml_card_read_key (card_key_card, 0, (u_char *)key_ptr);
	if (error) 
	{
		printk("card_key_read: card key read failed: %llx, %d\n", (uint64_t)*ppos, error);
		kfree(key_ptr);
		return -EFAULT;
	}

	if((*ppos + count) > CONFIG_KEYSIZE)
	{
		read_size = CONFIG_KEYSIZE - *ppos;
	}
	else
	{
		read_size = count;
	}

	copy_to_user(buf, (key_ptr + *ppos), read_size);
	*ppos += read_size;
	
	kfree(key_ptr);
	return read_size;
}

static ssize_t card_key_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	u_char *key_ptr = NULL;
	ssize_t write_size;
	int error = 0;
	
	if(*ppos == CONFIG_KEYSIZE)
	{
		return 0;
	}

	if(*ppos >= CONFIG_KEYSIZE)
	{
		printk(KERN_ERR "card key: data access violation!\n");
		return -EFAULT;
	}

	key_ptr = kzalloc(sizeof(mesonkey_t), GFP_KERNEL);
	if (key_ptr == NULL)
	{
		return -ENOMEM;
	}
	error = aml_card_read_key (card_key_card, 0, (u_char *)key_ptr);
	if (error) 
	{
		printk("card_key_read: card key read failed: %llx, %d\n", (uint64_t)*ppos, error);
		kfree(key_ptr);
		return -EFAULT;
	}

	if((*ppos + count) > CONFIG_KEYSIZE)
	{
		write_size = CONFIG_KEYSIZE - *ppos;
	}
	else
	{
		write_size = count;
	}

	copy_from_user((key_ptr + *ppos), buf, write_size);

	error = aml_card_save_key(card_key_card, key_ptr);

	if (error) 
	{
		printk("card_key_read: card key read failed: %llx, %d\n", (uint64_t)*ppos, error);
		kfree(key_ptr);
		return -EFAULT;
	}

	*ppos += write_size;
	
	kfree(key_ptr);
	return write_size;
}

static int card_key_close(struct inode *inode, struct file *file)
{
	return 0;
}

//static int card_key_ioctl(struct inode *inode, struct file *file,
//		     u_int cmd, u_long arg)
//{
//	return 0;
//}
static struct file_operations card_key_fops = {
    .owner	= THIS_MODULE,
    .open	= card_key_open,
    .read	= card_key_read,
    .write	= card_key_write,
    .release	= card_key_close,
//    .ioctl	= card_key_ioctl,
};

static int card_key_cls_suspend(struct device *dev, pm_message_t state)
{
		return 0;
}

static int card_key_cls_resume(struct device *dev)
{
	return 0;
}

static struct class card_key_class = {
    
	.name = "card_key",
	.owner = THIS_MODULE,
	.suspend = card_key_cls_suspend,
	.resume = card_key_cls_resume,
};


int card_key_init(struct memory_card *card)
{

	int ret;
	struct device *devp;
	static dev_t card_key_devno;
	pr_info("card key: card_key_probe. \n");
	int err = 0;

	err = aml_card_key_check(card);
	if(err){
		printk("invalid card key\n");
	}

	card_key_card = card;
	
	ret = alloc_chrdev_region(&card_key_devno, 0, 1, AML_KEY_DEVICE_NAME);
	if (ret < 0) {
		pr_err("card_key: failed to allocate chrdev. \n");
		return 0;
	}
	/* connect the file operations with cdev */
	cdev_init(&card->key_cdev, &card_key_fops);
	card->card_key_cdev.owner = THIS_MODULE;

	/* connect the major/minor number to the cdev */
	ret = cdev_add(&card->key_cdev, card_key_devno, 1);
	if (ret) {
		pr_err("card key: failed to add device. \n");
		/* @todo do with error */
		return ret;
	}

	ret = class_register(&card_key_class);
	if (ret < 0) {
		printk(KERN_NOTICE "class_register(&card_key_class) failed!\n");
	}
	devp = device_create(&card_key_class, NULL, card_key_devno, NULL, "card_key");
	if (IS_ERR(devp)) {
	 	printk(KERN_ERR "card_key: failed to create device node\n");
	 	ret = PTR_ERR(devp);
	}
	return 0;
}

#else

#include "sd/sd_port.h"

//struct memory_card *card_find_card(struct card_host *host, u8 card_type); 
static int aml_card_base_read_key (struct memory_card  *card, size_t offset, u_char * buf,unsigned int data_size)
{
	unsigned char *data_buf;
	int error = 0;
	uint64_t addr;
	SD_MMC_Card_Info_t *sd_mmc_info;
	unsigned int phy_size,i ,count,cnt_per;
#if 0
	SD_MMC_Card_Info_t *sd_mmc_info_sdio;
	struct memory_card *sdio_card;
	struct card_host *host = card->host;
#endif

	sd_mmc_info = card->card_info;
	if(!sd_mmc_info->sd_mmc_buf)
	{
		printk("sd_mmc_info->sd_mmc_buf NULL,%s:%d\n",__func__,__LINE__);
		return -1;
	}
	if((data_size > card->key_info->key_phy_size)||(data_size > (1024*128)))
	{
		printk("data size is bigger storeroom size,%s:%d\n",__func__,__LINE__);
		return -1;//
	}
	addr = card->key_info->key_phy_addr;
	addr >>=9;
	data_buf = sd_mmc_info->sd_mmc_buf;//the sd_mmc_buf is 128k byte
	
	sdio_close_host_interrupt(SDIO_IF_INT);
	sd_sdio_enable(sd_mmc_info->io_pad_type);
	error = sd_mmc_read_data(card->card_info,addr,data_size,data_buf);
	if(error){
		printk("workaround for card_data.error,%s:%d\n",__FILE__,__LINE__);
		card->card_io_init(card);
		card->card_detector(card);
		if(card->card_status == CARD_INSERTED){
			error = sd_mmc_init(sd_mmc_info);
			if(!error){
				error = sd_mmc_read_data(card->card_info,addr,data_size,data_buf);
			}
		}
	}
	if ((error != 0) && (error != -EUCLEAN)) {
		printk("emmc read failed: %llx, %d\n,%s:%d", (uint64_t)addr, error,__FILE__,__LINE__);
		return -1;
	}
	sd_gpio_enable(sd_mmc_info->io_pad_type);
#if 0
	sdio_card = card_find_card(host, CARD_SDIO);
	if(sdio_card)
	{
		sd_mmc_info_sdio = (SD_MMC_Card_Info_t *)sdio_card->card_info;
		sd_sdio_enable(sd_mmc_info_sdio->io_pad_type);
		sdio_open_host_interrupt(SDIO_IF_INT);
		if (sd_mmc_info_sdio->sd_save_hw_io_flag) {
	    		WRITE_CBUS_REG(SDIO_CONFIG, sd_mmc_info_sdio->sd_save_hw_io_config);
	      		WRITE_CBUS_REG(SDIO_MULT_CONFIG, sd_mmc_info_sdio->sd_save_hw_io_mult_config);
    		}
	}
#endif

	memcpy(buf,data_buf,data_size);

	return error;
}
static int aml_card_base_write_key(struct memory_card  *card, loff_t offset, u_char *buf, unsigned int data_size)
{
	unsigned char *data_buf;
	int error = 0;
	SD_MMC_Card_Info_t *sd_mmc_info;
	uint64_t addr;
	unsigned int phy_size;
#if 0
	SD_MMC_Card_Info_t *sd_mmc_info_sdio;
	struct memory_card *sdio_card;
	struct card_host *host = card->host;

	if(card->key_info->key_valid)
	{
		return 0;//wrote key, can't write key again
	}
#endif

	sd_mmc_info = card->card_info;
	if(!sd_mmc_info->sd_mmc_buf)
	{
		printk("sd_mmc_info->sd_mmc_buf NULL,%s:%d\n",__func__,__LINE__);
		return -1;
	}
	if((data_size > card->key_info->key_phy_size)||(data_size > (1024*128)))
	{
		printk("data size is bigger storeroom size,%s:%d\n",__func__,__LINE__);
		return -1;//
	}
	data_buf = sd_mmc_info->sd_mmc_buf;//the sd_mmc_buf is 128k byte, data size <=128k

	addr = card->key_info->key_phy_addr;
	addr >>=9;
	memcpy(data_buf,buf,data_size);

	//strcpy(data_buf,"test 333333333333333abcdef123456");
	//printk("%s,%s:%d\n",data_buf,__func__,__LINE__);

	sdio_close_host_interrupt(SDIO_IF_INT);
	sd_sdio_enable(sd_mmc_info->io_pad_type);
	error = sd_mmc_write_data(card->card_info,addr,data_size,data_buf);
	if (error) {
		printk("emmc write failed: %llx, %d,%s:%d\n", (uint64_t)addr, error,__FILE__,__LINE__);
		return -1;
	}
	sd_gpio_enable(sd_mmc_info->io_pad_type);

	return error;
}
#define EMMC_KEY_HEAD_LEN    100
static int aml_card_read_key (struct memory_card  *card, size_t offset, u_char * buf,unsigned int data_size)
{
	unsigned char *data_buf;
	int error = 0;
	SD_MMC_Card_Info_t *sd_mmc_info;
	unsigned int key_head_size,key_size;

	if(!card->key_info->key_init)
	{
		printk("not find key part\n");
		return -1;
	}

	sd_mmc_info = card->card_info;
	if(!sd_mmc_info->sd_mmc_buf)
	{
		printk("sd_mmc_info->sd_mmc_buf NULL,%s:%d\n",__func__,__LINE__);
		return -1;
	}

	key_head_size = EMMC_KEY_HEAD_LEN;
	key_size = key_head_size + data_size;
	if((key_size > card->key_info->key_phy_size) ||(key_size > 1024*128))
	{
		printk("data size is too big,%s:%d\n",__func__,__LINE__);
		return -1;
	}
	data_buf = kzalloc(card->key_info->key_phy_size, GFP_KERNEL);
	if (data_buf == NULL)
		return -ENOMEM;

	error = aml_card_base_read_key (card, offset, data_buf,key_size);
	if(error)
	{
		printk("read key error:%d,%s:%d\n",error,__FILE__,__LINE__);
	}
	if (!memcmp(data_buf, ENV_KEY_MAGIC, 4))
	{
		card->key_info->key_valid = 1;
		memcpy(buf,&data_buf[key_head_size],data_size);
		printk("had write security key : %s,%s\n",ENV_KEY_MAGIC,__func__);
	}

	kfree(data_buf);
	return error;
}
static int aml_card_write_key(struct memory_card  *card, loff_t offset, u_char *buf, unsigned int data_size)
{
	unsigned char *data_buf;
	int error = 0;
	SD_MMC_Card_Info_t *sd_mmc_info;
	unsigned int key_head_size,key_size;

	if(!card->key_info->key_init)
	{
		printk("not find key part\n");
		return -1;
	}
#if 0
	if(card->key_info->key_valid)
	{	printk("security key is valid %s:%d\n",__FILE__,__LINE__);
		return 0;//wrote key, can't write key again
	}
#endif	
	sd_mmc_info = card->card_info;
	if(!sd_mmc_info->sd_mmc_buf)
	{
		printk("sd_mmc_info->sd_mmc_buf NULL,%s:%d\n",__func__,__LINE__);
		return -1;
	}
	key_head_size = EMMC_KEY_HEAD_LEN;
	if(((key_head_size+data_size) > card->key_info->key_phy_size) ||((key_head_size+data_size) > 1024*128))
	{
		printk("data size is too big,%s:%d\n",__func__,__LINE__);
		return -1;
	}
	data_buf = kzalloc(card->key_info->key_phy_size, GFP_KERNEL);
	if (data_buf == NULL)
		return -ENOMEM;

	memcpy(data_buf,ENV_KEY_MAGIC,sizeof(ENV_KEY_MAGIC)+1);
	memcpy(&data_buf[key_head_size],buf,data_size);
	key_size = key_head_size + data_size;
	error = aml_card_base_write_key(card, offset, data_buf, key_size);

	kfree(data_buf);
	return error;
}
static int aml_card_save_key(struct memory_card  *card, u_char *buf, unsigned int data_size)
{
	aml_card_write_key(card, 0, buf,data_size);
}
static int aml_card_key_init(struct memory_card  *card)
{
	unsigned char *data_buf;
	struct aml_card_info *card_plat_info;
	//struct card_key_info    *key_info;
	int err = 0;
	unsigned int partition;
	int start_blk;
	uint64_t offset;

	card->key_info = kzalloc(sizeof(struct aml_key_info_t), GFP_KERNEL);
	if (card->key_info == NULL)
		return -ENOMEM;

	card->key_info->key_init = 0;
	card->key_info->key_valid = 0;
	card_plat_info = card->card_plat_info;
	printk("%s,%s\n",card_plat_info->name,__func__);
	if (!strcmp("inand_card", card_plat_info->name))
	{
		struct mtd_partition * part;
		part = card_plat_info->partitions;
		for(partition=0;partition<card_plat_info->nr_partitions;partition++)
		{
			if(!strcmp("nand_key", card_plat_info->partitions[partition].name))
			{
				card->key_info->key_phy_addr = card_plat_info->partitions[partition].offset;
				card->key_info->key_phy_size = card_plat_info->partitions[partition].size;

				printk("emmc_key: offset=0x%x,size=0x%x\n",card->key_info->key_phy_addr,card->key_info->key_phy_size);
				card->key_info->key_init = 1;
				break;
			}
		}
		//data_buf = kzalloc(card->key_info->key_phy_size, GFP_KERNEL);
		//if (data_buf == NULL)
		//{	err = -ENOMEM;
		//	printk("kzalloc mem fail for data_buf,%s\n",__func__);
		//	goto exit1;
		//}
		//start_blk = card->key_info->key_phy_addr;
		//offset = start_blk;
#if 0
		err = sd_mmc_read_data(card->card_info, start_blk, card->key_info->key_phy_size,data_buf);
		if ((err != 0) && (err != -EUCLEAN)) {
			printk("blk check good but read failed: %llx, %d\n", (uint64_t)offset, err);
			//continue;
		}
#endif
#if 0		
		if (!memcmp(data_buf, ENV_KEY_MAGIC, 4))
		{
			card->key_info->key_valid = 1;
			printk("had write security key : %s,%s",ENV_KEY_MAGIC,__func__);
		}
#endif
	}
	else
	{	printk("card key is not inand\n");
		err=-1;
	}
//exit2:
//	kfree(data_buf);
exit1:
	return err;//0: ok, -1: err
}
static int aml_card_key_check(struct memory_card  *card)
{
	mesonkey_t *key_ptr;
	int error = 0, start_blk, total_blk, update_key_flag = 0, i, j, nr, phys_erase_shift;
	loff_t offset;

	error = aml_card_key_init(card);
	if (error)
		return error;
	key_ptr = kzalloc(sizeof(mesonkey_t), GFP_KERNEL);
	if (key_ptr == NULL)
		return -ENOMEM;

	if (card->key_info->key_valid == 1) {
#if 0
// the code can discard, the code is remained for test
		offset = card->key_info->key_phy_addr;

		error = aml_card_read_key (card, offset, (u_char *)key_ptr,0x2000);
		if (error) {
			printk("card key read failed: %llx, %d\n", (uint64_t)offset, error);
			goto exit;
		}
#endif
	}
	else
	{
		update_key_flag = 1;
	}
	if (update_key_flag)
	{
#if 0
		printk("\nallow write card key\n");
		error = aml_card_save_key(card, (u_char *)key_ptr);
		if (error) {
			printk("card key save failed: %d\n", error);
			goto exit;
		}
#endif
	}

exit:
	kfree(key_ptr);
	return 0;
}




static struct memory_card  *card_key_card = NULL;
static int card_key_open(struct inode * inode, struct file * filp)
{
	return 0;
}

/*
 * This funcion reads the u-boot keyionment variables. 
 * The f_pos points directly to the key location.
 */
static ssize_t card_key_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
}

static ssize_t card_key_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
}

static int card_key_close(struct inode *inode, struct file *file)
{
	return 0;

}

//static int card_key_ioctl(struct inode *inode, struct file *file,
//		     u_int cmd, u_long arg)

//{
//	return 0;
//}
static struct file_operations card_key_fops = {

    .owner	= THIS_MODULE,
    .open	= card_key_open,
    .read	= card_key_read,
    .write	= card_key_write,
    .release	= card_key_close,

//    .ioctl	= card_key_ioctl,
};

static int card_key_cls_suspend(struct device *dev, pm_message_t state)

{
		return 0;
}

static int card_key_cls_resume(struct device *dev)

{
	return 0;
}


static struct class card_key_class = {
    
	.name = "card_key",
	.owner = THIS_MODULE,

	.suspend = card_key_cls_suspend,
	.resume = card_key_cls_resume,
};

#include <linux/efuse.h>
#define  TESTMODE  0
static int32_t card_keybox_read(aml_keybox_provider_t * provider, uint8_t *buf,int32_t size)
{
	int error;
	unsigned int data_size;
	struct memory_card *card;
	char abcdefadd[20]={0};

	card = card_key_card;
#if 0
	if(card->key_info == NULL)
	{
		error = aml_card_key_check(card);
		if(error){
			printk("invalid nand key\n");
		}
	}
#endif

#if !TESTMODE
	data_size = size;
	error = aml_card_read_key (card_key_card, 0, (u_char *)buf,data_size);
	printk("error=%d,%s\n",error,__func__);
#else	
	data_size = 12;
	error = aml_card_read_key (card_key_card, 0, (u_char *)abcdefadd,data_size);
	printk("error=%d,%s,%s\n",error,abcdefadd,__func__);
#endif 	
	if (error) 
	{
		return -EFAULT;
	}
	return 0;
}
//extern int rewrite_nandkey_get(void);
static int32_t card_keybox_write(aml_keybox_provider_t * provider, uint8_t *buf,int32_t size)
{
	u_char *key_ptr = NULL;
	ssize_t write_size;
	int error = 0;
	unsigned int data_size;
	struct memory_card *card;
	uint8_t test_buff[100];
	char abcdef[]={"1234567890"};

	card = card_key_card;
#if 0
	if(card->key_info == NULL)
	{
		error = aml_card_key_check(card);
		if(error){
			printk("invalid nand key\n");
		}
		error = aml_card_read_key (card, 0, test_buff,sizeof(test_buff));
		printk("%s,%s:%d\n",test_buff,__FILE__,__LINE__);
	}
#endif
	// the read is need for checking if keys is wrote
	if(card->key_info->key_valid == 0)
	{
		error = aml_card_read_key (card, 0, test_buff,sizeof(test_buff));
		printk("7733%s,%s:%d\n",test_buff,__FILE__,__LINE__);
	}
	//card->key_info->key_valid = rewrite_nandkey_get(); //for test
	card->key_info->key_valid = 0;//if run to here, data must be wrote in emmc

#if !TESTMODE
	printk("start write key,%s:%d\n",__func__,__LINE__);
	data_size = size;
	error = aml_card_save_key(card_key_card, buf,data_size);
	printk("error=%d,%s\n",error,__func__);
#else
	data_size = 11;
	error = aml_card_save_key(card_key_card, abcdef,data_size);
	printk("error=%d,%s,%s\n",error,abcdef,__func__);
#endif

	if (error) 
	{
		return -EFAULT;
	}
	
	return 0;
}


static aml_keybox_provider_t card_provider={
		.name="nand_key",
		.read=card_keybox_read,
		.write=card_keybox_write,

};



int card_key_init(struct memory_card *card)
{
	printk("card key: card_reader_initialize. \n");
	int err = 0;

	aml_card_key_check(card);
	card_key_card = card;
	//card_provider.priv=card_key_card;
	card_provider.priv=card;
	err = aml_keybox_provider_register(&card_provider);
	if(err){
		BUG();
	}

	return 0;
}


#endif


