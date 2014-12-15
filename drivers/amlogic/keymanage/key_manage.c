#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/scatterlist.h>
#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/amlogic/efuse.h>
#include <linux/amlogic/securitykey.h>
#include <linux/of.h>
#include "key_manage.h"

/*
 * key_manage.c
 * this file support key read/write with key unite interface,and don't care about device for saving key
 * because there are many kinds of device and key, key should be configured in storage.c(board/amlogic/m6xxx/firmware/storage.c 
 *  or customer/board/m6_xxxx/firmware/storage.c)
 * 
 * the key_manage.c file should define below micro
 *     #define CONFIG_UNIFY_KEY_MANAGE
 * if the key unify interface can support all kinds of keys, depend on below micro
 * #define CONFIG_UNIFY_KEY_MANAGE
 *   #define CONFIG_OF
 *   #define CONFIG_SECURITYKEY
 *   #define CONFIG_AML_NAND_KEY or #define CONFIG_NAND_KEY
 *   #define CONFIG_AML_EMMC_KEY
 *   #define CONFIG_SECURE_NAND  1
 *   #define CONFIG_SPI_NOR_SECURE_STORAGE
 *   #define CONFIG_SECURESTORAGEKEY
 *   #define CONFIG_RANDOM_GENERATE
 *   #define CONFIG_EFUSE 1
 * 
 * 
 * */


#define UNIFYKEYS_MODULE_NAME    "aml_keys-t"
#define UNIFYKEYS_DRIVER_NAME	"aml_keys-t"
#define UNIFYKEYS_DEVICE_NAME    "unifykeys"
#define UNIFYKEYS_CLASS_NAME     "unifykeys"

#define KEY_NO_EXIST	0
#define KEY_BURNED		1

#define KEY_READ_PERMIT		10
#define KEY_READ_PROHIBIT	11

#define KEY_WRITE_MASK		(0x0f<<4)
#define KEY_WRITE_PERMIT	(10<<4)
#define KEY_WRITE_PROHIBIT	(11<<4)
//#define ACS_ADDR_ADDR	0xd9000200

static unifykey_dev_t *unifykey_devp=NULL;
static dev_t unifykey_devno;
static struct device * unifykey_device= NULL;

typedef int (* key_unify_dev_init)(char *buf,unsigned int len);
typedef int (* key_unify_dev_uninit)(void);


extern int efuse_read_item(char *buf, size_t count, loff_t *ppos);
extern int efuse_write_item(char *buf, size_t count, loff_t *ppos);
extern int efuse_getinfo_byTitle(char *title, efuseinfo_item_t *info);

static int key_general_nand_init(char *buf,unsigned int len)
{
	int err=0;
#ifdef CONFIG_SECURITYKEY
	err = extenal_api_key_set_version("auto3");
#endif
	return err;
}
static int key_general_nand_write(char *keyname,unsigned char *keydata,unsigned int datalen,enum key_manager_df_e flag)
{
	int err = -EINVAL;
#ifdef CONFIG_SECURITYKEY
	int ascii_flag;
	if((flag != KEY_M_HEXDATA) && (flag != KEY_M_HEXASCII) && (flag != KEY_M_ALLASCII)){
		printk("%s:%d,%s key config err\n",__func__,__LINE__,keyname);
		return -EINVAL;
	}
	if((flag == KEY_M_HEXDATA) || (flag == KEY_M_ALLASCII)){
		ascii_flag = 0;
	}
	else{
		ascii_flag = 1;
	}
	//err = uboot_key_put("auto",keyname, keydata,datalen,ascii_flag);
	err = extenal_api_key_write(keyname,(char *)keydata,(int)datalen,ascii_flag);
#endif
	return err;
}
static int key_general_nand_read(char *keyname,unsigned char *keydata,unsigned int datalen,unsigned int *reallen,enum key_manager_df_e flag)
{
	int err = -EINVAL;
#ifdef CONFIG_SECURITYKEY
	int ascii_flag;
	if((flag != KEY_M_HEXDATA) && (flag != KEY_M_HEXASCII) && (flag != KEY_M_ALLASCII)){
		printk("%s:%d,%s key config err\n",__func__,__LINE__,keyname);
		return -EINVAL;
	}
	if((flag == KEY_M_HEXDATA) || (flag == KEY_M_ALLASCII)){
		ascii_flag = 0;
	}
	else{
		ascii_flag = 1;
	}
	//err = uboot_key_get("auto",keyname, keydata,datalen,ascii_flag);
	err = extenal_api_key_read(keyname,(char *)keydata,(int)datalen,ascii_flag);
	if(reallen){
		*reallen = 0;
	}
	if(err >0){
		if(reallen){
			*reallen = err;
		}
	}
#endif
	return err;
}
/* function name: key_general_nand_query
 * keyname: key name
 * keystate: 0: key not exist, 1: key burned , other : reserve
 * return : <0: fail, >=0 ok
 * */
static int key_general_nand_query(char *keyname,unsigned int *keystate)
{
	int err = -EINVAL;
#ifdef CONFIG_SECURITYKEY
	err = extenal_api_key_query(keyname,keystate);
#endif
	return err;
}
static int key_securestorage_init(char *buf,unsigned int len)
{
#ifdef CONFIG_SECURESTORAGEKEY
	int err = -EINVAL;
	err = securestore_key_init(buf,(int)len);
	return err;
#else
	return 0;
#endif
}
static int key_securestorage_write(char *keyname,unsigned char *keydata,unsigned int datalen)
{
	int err = -EINVAL;
#ifdef CONFIG_SECURESTORAGEKEY
	err = securestore_key_write(keyname, (char *)keydata,datalen,0);
#endif
	return err;
}
static int key_securestorage_read(char *keyname,unsigned char *keydata,unsigned int datalen,unsigned int *reallen)
{
	int err = -EINVAL;
#ifdef CONFIG_SECURESTORAGEKEY
	err = securestore_key_read(keyname,(char *)keydata,datalen,reallen);
#endif
	return err;
}
static int key_securestorage_query(char *keyname,unsigned int *keystate)
{
	int err = -EINVAL;
#ifdef CONFIG_SECURESTORAGEKEY
	err = securestore_key_query(keyname, keystate);
#endif
	return err;
}
static int key_securestorage_uninit(void)
{
#ifdef CONFIG_SECURESTORAGEKEY
	int err = -EINVAL;
	err = securestore_key_uninit();
	return err;
#else
	return 0;
#endif
}

static int key_efuse_init(char *buf,unsigned int len)
{
	char ver;
	ver = unifykey_get_efuse_version();
	return 0;
}
static int key_efuse_write(char *keyname,unsigned char *keydata,unsigned int datalen)
{
#ifdef CONFIG_EFUSE
	char *title = keyname;
	efuseinfo_item_t info;
//int efuse_getinfo_byTitle(char *title, efuseinfo_item_t *info)
//int efuse_write_item(char *buf, size_t count, loff_t *ppos)
//int efuse_read_item(char *buf, size_t count, loff_t *ppos)
	if(efuse_getinfo_byTitle(title, &info) < 0)
		return -EINVAL;
	//if(!(info.we)){
	//	printk("%s write unsupport now. \n", title);
	//	return -EACCES;
	//}
	//if(efuse_write_item((char*)keydata, info.data_len, (loff_t*)&info.offset)<0){
	if(efuse_write_item((char*)keydata, (size_t)datalen, (loff_t*)&info.offset)<0){
		printk("error: efuse write fail.\n");
		return -1;
	}
	else{
		printk("%s written done.\n", info.title);
	}
	return 0;
#else
	return -EINVAL;
#endif
}
static int key_efuse_read(char *keyname,unsigned char *keydata,unsigned int datalen,unsigned int *reallen)
{
#ifdef CONFIG_EFUSE
	char *title = keyname;
	efuseinfo_item_t info;
	int err=0;
	char *buf;
	if(efuse_getinfo_byTitle(title, &info) < 0)
		return -EINVAL;
	
	buf = kzalloc(info.data_len, GFP_KERNEL);
	if(buf == NULL){
		printk("%s:%d,kzalloc mem fail\n",__func__,__LINE__);
		return -ENOMEM;
	}
	memset(buf,0,info.data_len);
	//err = efuse_read_usr((char*)keydata, info.data_len, (loff_t *)&info.offset);
	err = efuse_read_item((char*)buf, (size_t)info.data_len, (loff_t *)&info.offset);
	if(err>=0){
		*reallen = info.data_len;
		if(datalen > info.data_len){
			datalen = info.data_len;
		}
		memcpy(keydata,buf,datalen);
	}
	kfree(buf);
	return err;
#else
	return -EINVAL;
#endif
}
static int key_efuse_query(char *keyname,unsigned int *keystate)
{
	int err=-EINVAL;
#ifdef CONFIG_EFUSE
	int i;
	char *title = keyname;
	efuseinfo_item_t info;
	char *buf;
	if(efuse_getinfo_byTitle(title, &info) < 0)
		return -EINVAL;
	buf = kzalloc(info.data_len, GFP_KERNEL);
	if(buf == NULL){
		printk("%s:%d,kzalloc mem fail\n",__func__,__LINE__);
		return -ENOMEM;
	}
	memset(buf,0,info.data_len);
	err = efuse_read_item(buf, (size_t)info.data_len, (loff_t *)&info.offset);
	*keystate = KEY_NO_EXIST;
	if(err >0){
		for(i=0;i<info.data_len;i++){
			if(buf[i] != 0){
				*keystate = KEY_BURNED;
				break;
			}
		}
	}
	kfree(buf);
#endif
	return err;
}

/*
 * function name: key_unify_init
 * buf : input 
 * len  : > 0
 * return : >=0: ok, other: fail
 * */
int key_unify_init(char *buf,unsigned int len)
{
	int bakerr,err=-EINVAL;
	//enum key_manager_df_e key_dev[]={KEY_EFUSE_NORMAL,KEY_SECURE_STORAGE,KEY_GENERAL_NANDKEY};
	char *dev_node[]={"unkown","efuse","secure","general"};
	key_unify_dev_init dev_initfunc[]={NULL,key_efuse_init,key_securestorage_init,key_general_nand_init};
	int i,cnt;
	/*if(unifykey_dt_parse()){
		printk("%s:%d,unify key config table parse fail\n",__func__,__LINE__);
		return err;
	}
	*/
	bakerr = 0;
	cnt = sizeof(dev_initfunc)/sizeof(dev_initfunc[0]);
	for(i=0;i<cnt;i++){
		if(dev_initfunc[i]){
			err = dev_initfunc[i](buf,len);
			if(err < 0){
				printk("%s:%d,%s device ini fail\n",__func__,__LINE__,dev_node[i]);
				bakerr = err;
			}
		}
	}
	return bakerr;
}
EXPORT_SYMBOL(key_unify_init);

/* funtion name: key_unify_write
 * keyname : key name is ascii string
 * keydata : key data buf
 * datalen : key buf len
 * return  0: ok, -0x1fe: no space, other fail
 * */
int key_unify_write(char *keyname,unsigned char *keydata,unsigned int datalen)
{
	int err=0;
	struct key_item_t *key_manage;
	enum key_manager_df_e key_df;
	key_manage = unifykey_find_item_by_name(keyname);
	if(key_manage == NULL){
		printk("%s:%d,%s key name is not exist\n",__func__,__LINE__,keyname);
		return -EINVAL;
	}
	if(unifykey_item_verify_check(key_manage)){
		printk("%s:%d,%s key name is invalid\n",__func__,__LINE__,keyname);
		return -EINVAL;
	}
	if(key_manage->permit & KEY_M_PERMIT_WRITE){
		err = -EINVAL;
		key_df = key_manage->df;
		switch(key_manage->dev){
			case KEY_M_EFUSE_NORMAL:
				err = key_efuse_write(keyname,keydata,datalen);
				break;
			case KEY_M_SECURE_STORAGE:
				err = key_securestorage_write(keyname,keydata,datalen);
				if(err == 0x1fe){
					err = -0x1fe;
				}
				break;
			case KEY_M_GENERAL_NANDKEY:
				err = key_general_nand_write(keyname,keydata,datalen,key_df);
				break;
			case KEY_M_UNKNOW_DEV:
			default:
				printk("%s:%d,%s key not know device\n",__func__,__LINE__,keyname);
				break;
		}
	}
	return err;
}
EXPORT_SYMBOL(key_unify_write);
/*
 *function name: key_unify_read
 * keyname : key name is ascii string
 * keydata : key data buf
 * datalen : key buf len
 * reallen : key real len
 * return : <0 fail, >=0 ok
 * */
int key_unify_read(char *keyname,unsigned char *keydata,unsigned int datalen,unsigned int *reallen)
{
	int err=0;
	struct key_item_t *key_manage;
	enum key_manager_df_e key_df;
	key_manage = unifykey_find_item_by_name(keyname);
	if(key_manage == NULL){
		printk("%s:%d,%s key name is not exist\n",__func__,__LINE__,keyname);
		return -EINVAL;
	}
	if(unifykey_item_verify_check(key_manage)){
		printk("%s:%d,%s key name is invalid\n",__func__,__LINE__,keyname);
		return -EINVAL;
	}
	if(key_manage->permit & KEY_M_PERMIT_READ){
		err = -EINVAL;
		key_df = key_manage->df;
		switch(key_manage->dev){
			case KEY_M_EFUSE_NORMAL:
				err = key_efuse_read(keyname,keydata,datalen,reallen);
				break;
			case KEY_M_SECURE_STORAGE:
				err = key_securestorage_read(keyname,keydata,datalen,reallen);
				break;
			case KEY_M_GENERAL_NANDKEY:
				err = key_general_nand_read(keyname,keydata,datalen,reallen,key_df);
				break;
			case KEY_M_UNKNOW_DEV:
			default:
				printk("%s:%d,%s key not know device\n",__func__,__LINE__,keyname);
				break;
		}
	}
	return err;
}
EXPORT_SYMBOL(key_unify_read);

/*
*    key_unify_query - query whether key was burned.
*    @keyname : key name will be queried.
*    @keystate: query state value, 0: key was NOT burned; 1: key was burned; others: reserved.
*     keypermit: 
*    return: 0: successful; others: failed. 
*/
int key_unify_query(char *keyname,unsigned int *keystate,unsigned int *keypermit)
{
	int err=0;
	struct key_item_t *key_manage;
	enum key_manager_df_e key_df;
	key_manage = unifykey_find_item_by_name(keyname);
	if(key_manage == NULL){
		printk("%s:%d,%s key name is not exist\n",__func__,__LINE__,keyname);
		return -EINVAL;
	}
	if(unifykey_item_verify_check(key_manage)){
		printk("%s:%d,%s key name is invalid\n",__func__,__LINE__,keyname);
		return -EINVAL;
	}
	if(key_manage->permit & KEY_M_PERMIT_READ){
		err = -EINVAL;
		key_df = key_manage->df;
		switch(key_manage->dev){
			case KEY_M_EFUSE_NORMAL:
				err = key_efuse_query(keyname,keystate);
				*keypermit = KEY_READ_PERMIT;
				if(err >= 0){
					if(*keystate == KEY_BURNED){
						*keypermit |= KEY_WRITE_PROHIBIT;
					}
					else if(*keystate == KEY_NO_EXIST){
						*keypermit |= KEY_WRITE_PERMIT;
					}
				}
				break;
			case KEY_M_SECURE_STORAGE:
				err = key_securestorage_query(keyname,keystate);
				*keypermit = KEY_READ_PROHIBIT;
				*keypermit |= KEY_WRITE_PERMIT;
				break;
			case KEY_M_GENERAL_NANDKEY:
				err = key_general_nand_query(keyname,keystate);
				*keypermit = KEY_READ_PERMIT;
				*keypermit |= KEY_WRITE_PERMIT;
				break;
			case KEY_M_UNKNOW_DEV:
				printk("%s:%d,%s key not know device\n",__func__,__LINE__,keyname);
			default:
				break;
		}
	}
	return err;
}
EXPORT_SYMBOL(key_unify_query);
/* function name: key_unify_uninit
 * functiion : uninit 
 * return : >=0 ok, <0 fail
 * */
int key_unify_uninit(void)
{
	int bakerr,err=-EINVAL;
	int i,cnt;
	key_unify_dev_uninit dev_uninit[]={key_securestorage_uninit};
	bakerr = 0;
	cnt = sizeof(dev_uninit)/sizeof(dev_uninit[0]);
	for(i=0;i<cnt;i++){
		if(dev_uninit[i]){
			err = dev_uninit[i]();
			if(err){
				bakerr = err;
			}
		}
	}
	return bakerr;
}
EXPORT_SYMBOL(key_unify_uninit);

static int unifykey_open(struct inode *inode, struct file *file)
{
	unifykey_dev_t *devp;
	devp = container_of(inode->i_cdev, unifykey_dev_t, cdev);
	file->private_data = devp;
	return 0;
}
static int unifykey_release(struct inode *inode, struct file *file)
{
	unifykey_dev_t *devp;
	devp = file->private_data;
	return 0;
}
static loff_t unifykey_llseek(struct file *filp, loff_t off, int whence)
{
#if 1
	loff_t newpos;
	switch(whence) {
		case 0: /* SEEK_SET (start postion)*/
			newpos = off;
			break;

		case 1: /* SEEK_CUR */
			newpos = filp->f_pos + off;
			break;

		case 2: /* SEEK_END */
			newpos = (loff_t)unifykey_count_key() - 1;
			newpos = newpos + off;
			break;

		default: /* can't happen */
			return -EINVAL;
	}

	if (newpos < 0)
		return -EINVAL;
	if(newpos >= (loff_t)unifykey_count_key()){
		return -EINVAL;
	}
	filp->f_pos = newpos;
	return newpos;
#else
	return 0;
#endif
}
static long unifykey_unlocked_ioctl(struct file *file, unsigned int cmd, unsigned long arg )
{
	switch (cmd){
		case KEYUNIFY_INIT_INFO:
			{	struct key_item_t *appitem;
				char initvalue[KEY_UNIFY_NAME_LEN];
				int ret;
				appitem = (struct key_item_t*)arg;
				memcpy(initvalue,appitem->name,KEY_UNIFY_NAME_LEN);
				ret = key_unify_init(initvalue,KEY_UNIFY_NAME_LEN);
				if(ret < 0){
					printk("%s:%d,key unify init fail\n",__func__,__LINE__);
					return ret;
				}
			}
			break;
		case KEYUNIFY_ITEM_GEG:
			{
				struct key_item_t *appitem,*kitem;
				appitem = (struct key_item_t*)arg;
				kitem = unifykey_find_item_by_name(appitem->name);
				if(!kitem){
					return  -EFAULT;
				}
				//memcpy(appitem,kitem,sizeof(struct key_item_t));
				//strcpy(appitem->name,kitem->name);
				appitem->id = kitem->id;
				appitem->dev = kitem->dev;
				appitem->df = kitem->df;
				appitem->permit = kitem->permit;
				appitem->flag = kitem->flag;
				appitem->reserve = kitem->reserve;
			}
			break;
		default:
			return -ENOTTY;
	}
	return 0;
}

static ssize_t unifykey_read( struct file *file, char __user *buf, size_t count, loff_t *ppos )
{
	int ret;
	int id;
	unsigned int reallen;
	struct key_item_t *item;
	char *local_buf;
	local_buf = kzalloc(count, GFP_KERNEL);
	if(!local_buf){
		printk(KERN_INFO "memory not enough,%s:%d\n",__func__,__LINE__);
		return -ENOMEM;
	}
	id = (int)(*ppos);
	//printk("%s:%d,id=%d\n",__func__,__LINE__,id);
	item = unifykey_find_item_by_id(id);
	if(!item){
		ret =  -EINVAL;
		goto exit;
	}
	ret = key_unify_read(item->name,local_buf,count,&reallen);
	if(ret < 0){
		goto exit;
	}
	if(count > reallen){
		count = reallen;
	}
	if (copy_to_user((void*)buf, (void*)local_buf, count)) {
		ret =  -EFAULT;
		goto exit;
	}
	ret = count;
exit:
	kfree(local_buf);
	return ret;
}

static ssize_t unifykey_write( struct file *file, const char __user *buf, size_t count, loff_t *ppos )
{
	int ret;
	int id;
	struct key_item_t *item;
	char *local_buf;
	local_buf = kzalloc(count, GFP_KERNEL);
	if(!local_buf){
		printk(KERN_INFO "memory not enough,%s:%d\n",__func__,__LINE__);
		return -ENOMEM;
	}
	id = (int)(*ppos);
	item = unifykey_find_item_by_id(id);
	if(!item){
		ret =  -EINVAL;
		goto exit;
	}
	if (copy_from_user(local_buf, buf, count)){
		ret =  -EFAULT;
		goto exit;
	}
	ret = key_unify_write(item->name,local_buf,count);
	if(ret<0){
		goto exit;
	}
	ret = count;
exit:
	kfree(local_buf);
	return ret;
}

static ssize_t unifykey_version_show(struct device *dev,struct device_attribute *attr, char *buf)
{
	ssize_t n=0;
	n += sprintf(&buf[n], "version:1.0");
	buf[n] = 0;
	return n;
}


static const struct of_device_id meson6_unifykeys_dt_match[];
static char *get_unifykeys_drv_data(struct platform_device *pdev)
{
	char *key_dev = NULL;
	if (pdev->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_node(meson6_unifykeys_dt_match, pdev->dev.of_node);
		if(match){
			key_dev = (char*)match->data;
		}
	}
	return key_dev;
}

static const struct file_operations unifykey_fops = {
	.owner      = THIS_MODULE,
	.llseek     = unifykey_llseek,
	.open       = unifykey_open,
	.release    = unifykey_release,
	.read       = unifykey_read,
	.write      = unifykey_write,
	.unlocked_ioctl      = unifykey_unlocked_ioctl,
};

static struct device_attribute unifykey_class_attrs[] ={
	__ATTR_RO(unifykey_version),
//	__ATTR_RO(installed_keys), 
	__ATTR_NULL 
};
static struct class unifykey_class ={
	.name = UNIFYKEYS_CLASS_NAME,
	.dev_attrs = unifykey_class_attrs, 
};



//static volatile int aml_unifykey_test=1;

static int aml_unifykeys_probe(struct platform_device *pdev)
{
	int ret=-1;
	struct device *devp;
	//while(aml_unifykey_test);
	
	if (pdev->dev.of_node) {
		ret = unifykey_dt_create(pdev);
	}
	ret = alloc_chrdev_region(&unifykey_devno, 0, 1, UNIFYKEYS_DEVICE_NAME);
	if(ret<0){
		printk(KERN_ERR "unifykey: failed to allocate major number\n ");
		ret = -ENODEV;
		goto out;
	}
	printk("%s:%d=============unifykey_devno:%x\n",__func__,__LINE__,unifykey_devno);
    ret = class_register(&unifykey_class);
    if (ret){
        goto error1;
	}

	unifykey_devp = kzalloc(sizeof(unifykey_dev_t), GFP_KERNEL);
	if(!unifykey_devp){
		printk(KERN_ERR "unifykey: failed to allocate memory\n ");
		ret = -ENOMEM;
		goto error2;
	}
	/* connect the file operations with cdev */
	cdev_init(&unifykey_devp->cdev, &unifykey_fops);
	unifykey_devp->cdev.owner = THIS_MODULE;
	/* connect the major/minor number to the cdev */
	ret = cdev_add(&unifykey_devp->cdev, unifykey_devno, 1);
	if(ret){
		printk(KERN_ERR "unifykey: failed to add device\n");
		goto error3;
	}
	devp = device_create(&unifykey_class, NULL, unifykey_devno, NULL, UNIFYKEYS_DEVICE_NAME);
	if (IS_ERR(devp))
	{
		printk(KERN_ERR "unifykey: failed to create device node\n");
		ret = PTR_ERR(devp);
		goto error4;
	}
	devp->platform_data = get_unifykeys_drv_data(pdev);
	
	unifykey_device = devp;
	printk(KERN_INFO "unifykey: device %s created ok\n", UNIFYKEYS_DEVICE_NAME);
	return 0;
	
error4:
	cdev_del(&unifykey_devp->cdev);
error3:
	kfree(unifykey_devp);
error2:
	class_unregister(&unifykey_class);
error1:
	unregister_chrdev_region(unifykey_devno, 1);
out:
	return ret;
}

static int aml_unifykeys_remove(struct platform_device *pdev)
{
	if (pdev->dev.of_node) {
		unifykey_dt_release(pdev);
	}
	unregister_chrdev_region(unifykey_devno, 1);
	device_destroy(&unifykey_class, unifykey_devno);
	//device_destroy(&efuse_class, unifykey_devno);
	cdev_del(&unifykey_devp->cdev);
	kfree(unifykey_devp);
	class_unregister(&unifykey_class);
	return 0;
}



#ifdef CONFIG_OF
//static char * secure_device[3]={"nand_key","emmc_key",NULL};

static const struct of_device_id meson6_unifykeys_dt_match[]={
	{	.compatible = "amlogic,unifykey",
		.data		= NULL,
		//.data		= (void *)&secure_device[0],
	},
	{},
};
//MODULE_DEVICE_TABLE(of,meson6_rtc_dt_match);
#else
#define meson6_unifykeys_dt_match NULL
#endif

static struct platform_driver aml_unifykeys_management_driver =
{ 
	.probe = aml_unifykeys_probe, 
	.remove = aml_unifykeys_remove, 
	.driver =
     {
     	.name = UNIFYKEYS_DEVICE_NAME, 
        .owner = THIS_MODULE, 
        .of_match_table = meson6_unifykeys_dt_match,
     }, 
};

static int __init aml_unifykeys_management_init(void)
{
	int ret = -1;
	//printk(KERN_INFO "platform_driver_register--unifykey management  driver-------------start-------\n");
    ret = platform_driver_register(&aml_unifykeys_management_driver);
    if (ret != 0)
    {
        printk(KERN_ERR "failed to register unifykey management driver, error %d\n", ret);
        return -ENODEV;
    }
    printk(KERN_INFO "platform_driver_register--unifykey management  driver--------------------\n");

    return ret;
}

static void __exit aml_unifykeys_management_exit(void)
{
	platform_driver_unregister(&aml_unifykeys_management_driver);
}

module_init(aml_unifykeys_management_init);
module_exit(aml_unifykeys_management_exit);

MODULE_DESCRIPTION("Amlogic unifykeys management driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("bl zhou<benlong.zhou@amlogic.com>");


#if 0
int do_keyunify(cmd_tbl_t * cmdtp, int flag, int argc, char *argv[])
{
	int err;
	char *cmd,*name;
	unsigned int addr,len,reallen;
	unsigned int keystate,keypermit;

	if (argc < 2)
		goto usage;

	cmd = argv[1];
	if(!strcmp(cmd,"init")){
		if(argc > 3){
			addr = simple_strtoul(argv[2], NULL, 16);
			len  = simple_strtoul(argv[3], NULL, 16);
		}
		else{
			char initvalue[]={1,2,3,4};
			addr = (unsigned int)&initvalue[0];
			len  = sizeof(initvalue);
		}
		err = key_unify_init((char *)addr,len);
		return err;
	}
	if(!strcmp(cmd,"write")){
		if(argc < 5)
			goto usage;
		name = argv[2];
		addr = simple_strtoul(argv[3], NULL, 16);
		len  = simple_strtoul(argv[4], NULL, 16);
		err = key_unify_write(name,(unsigned char *)addr,len);
		if(err < 0){
			printk("%s:%d,%s key write fail\n",__func__,__LINE__,name);
		}
		return err;
	}
	if(!strcmp(cmd,"read")){
		if(argc < 6)
			goto usage;
		name = argv[2];
		addr = simple_strtoul(argv[3], NULL, 16);
		len  = simple_strtoul(argv[4], NULL, 16);
		reallen = simple_strtoul(argv[5], NULL, 16);
		err = key_unify_read(name,(unsigned char *)addr,len,(unsigned int*)reallen);
		if(err < 0){
			printk("%s:%d,%s key read fail\n",__func__,__LINE__,name);
		}
		return err;
	}
	if(!strcmp(cmd,"query")){
		if(argc < 5)
			goto usage;
		name = argv[2];
		keystate = simple_strtoul(argv[3], NULL, 16);
		keypermit  = simple_strtoul(argv[4], NULL, 16);
		err = key_unify_query(name,(unsigned int *)keystate,(unsigned int *)keypermit);
		if(err < 0){
			printk("%s:%d,%s key query fail\n",__func__,__LINE__,name);
		}
		if(err >=0){
			if(*(unsigned int*)keystate == KEY_BURNED){
				printk("%s key exist\n",name);
			}
			else{
				printk("%s key not exist\n",name);
			}
		}
		return err;
	}
	if(!strcmp(cmd,"uninit")){
		key_unify_uninit();
		return 0;
	}
	
usage:
	cmd_usage(cmdtp);
	return 1;
}




U_BOOT_CMD(keyunify, CONFIG_SYS_MAXARGS, 1, do_keyunify,
	"key unify sub-system",
	"init [addr] [len]- show available NAND key name\n"
	"keyunify uninit - init key in device\n"
	"keyunify write keyname data-addr len  ---- wirte key data \n"
	"keyunify read keyname data-addr len reallen-addr the key data\n"
	"keyunify query keyname state-addr permit-addr"
);
#endif
