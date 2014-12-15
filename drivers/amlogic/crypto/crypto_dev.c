/*
 *  /drivers/amlogic/crypto/crypto_dev.c
 *
 *  Driver for amlogic crypto api for user...
 *
 *
 *  Copyright (C) 2013 Amlogic. By Qi.Duan
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/ctype.h>
#include <linux/console.h>
#include <linux/sysrq.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/nmi.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <asm/io.h>
#include <crypto/hash.h>
#include <linux/err.h>
#include <linux/gfp.h>
#include <linux/scatterlist.h>
#include <linux/string.h>
#include <crypto/algapi.h>
#include <linux/of.h>

#define CRYPTO_STATUS_NONE      0
#define CRYPTO_STATUS_START     1
#define CRYPTO_STATUS_FINISH    2
#define CRYPTO_STATUS_ERROR     3

static DECLARE_WAIT_QUEUE_HEAD(crypto_wq);
typedef struct crypto_device_struct{
    char algname[32];

    unsigned int    keyaddr;
    unsigned int    keysize;

    unsigned int    srcaddr;
    unsigned int    dstaddr;
    unsigned int    size;

    char    dir;

    char    status;
}crypto_device_t;

static struct crypto_device_struct crypt_info;

int crypto_done(crypto_device_t * pcrypt_info)
{
    struct crypto_blkcipher *tfm;
    struct blkcipher_desc desc;
    struct scatterlist sg[1];
    int keysize = pcrypt_info->keysize;
    char *key;
    char *src;
    int ret=CRYPTO_STATUS_ERROR;
    unsigned int min_block_size,pad_byte;
    
    tfm = crypto_alloc_blkcipher(pcrypt_info->algname, 0, CRYPTO_ALG_ASYNC);
    if (IS_ERR(tfm)) {
	printk("failed to load transform for %s: %ld\n",pcrypt_info->algname,PTR_ERR(tfm));       
	return ret;
    }

    desc.tfm = tfm;
    desc.flags = 0;

    min_block_size = crypto_blkcipher_blocksize(tfm);
    if(min_block_size!=0)
    {
        pad_byte = pcrypt_info->size%min_block_size;
    }
    else
        pad_byte = 0;

    if((strcmp(pcrypt_info->algname,"cbc-aes-aml")==0)||(strcmp(pcrypt_info->algname,"ctr-aes-aml")==0))
    {
        keysize +=16; 
    }
    key = (char *)kmalloc(keysize,GFP_KERNEL);
    if(key==NULL)
    {
        return ret;
    }
    if(copy_from_user((void *)key,(void*)pcrypt_info->keyaddr,keysize))
	{
		kfree(key);
		return ret;
	}
    
    src = (char *)kmalloc(pcrypt_info->size,GFP_KERNEL);
    if(src==NULL)
    {
        kfree(key);
        return ret;
    }
    if(copy_from_user((void *)src,(void*)pcrypt_info->srcaddr,pcrypt_info->size))
	{	
		kfree(src);
		kfree(key);
		return ret;
	}
 
    sg_init_table(sg, 1);
    sg_set_buf(&sg[0], src, pcrypt_info->size-pad_byte);
            
    ret = crypto_blkcipher_setkey(tfm, (unsigned char *)key, keysize);
    if (ret) {
	printk("setkey() failed flags=%x\n",
			crypto_blkcipher_get_flags(tfm));
	goto out;
    }

    if(pcrypt_info->dir == 0)
        ret = crypto_blkcipher_encrypt(&desc, sg, sg, pcrypt_info->size-pad_byte);
    else
        ret = crypto_blkcipher_decrypt(&desc, sg, sg, pcrypt_info->size-pad_byte);
    if (ret)
    {
        printk("crypt() failed %x\n",ret);
        ret =  CRYPTO_STATUS_ERROR;
        goto out;
     }

    if(copy_to_user((void *)pcrypt_info->dstaddr,src,pcrypt_info->size))
		goto out;

    ret = CRYPTO_STATUS_FINISH;
out:

    kfree(src);
    kfree(key);
    
    crypto_free_blkcipher(tfm);

    return ret;
}

static ssize_t crypto_algname_show(struct device *_dev,
                          struct device_attribute *attr,
			     char *buf);
static ssize_t crypto_algname_store(struct device *_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count);
static ssize_t crypto_keyaddr_show(struct device *_dev,
			     struct device_attribute *attr,
			     char *buf);
static ssize_t crypto_keyaddr_store(struct device *_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count);
static ssize_t crypto_keysize_show(struct device *_dev,
			     struct device_attribute *attr,
			     char *buf);
static ssize_t crypto_keysize_store(struct device *_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count);
static ssize_t crypto_srcaddr_show(struct device *_dev,
                          struct device_attribute *attr,
			     char *buf);
static ssize_t crypto_srcaddr_store(struct device *_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count);
static ssize_t crypto_dstaddr_show(struct device *_dev,
			     struct device_attribute *attr,
			     char *buf);
static ssize_t crypto_dstaddr_store(struct device *_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count);
static ssize_t crypto_size_show(struct device *_dev,
			     struct device_attribute *attr,
			     char *buf);
static ssize_t crypto_size_store(struct device *_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count);
static ssize_t crypto_dir_show(struct device *_dev,
                          struct device_attribute *attr,
			     char *buf);
static ssize_t crypto_dir_store(struct device *_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count);
static ssize_t crypto_status_show(struct device *_dev,
			     struct device_attribute *attr,
			     char *buf);
static ssize_t crypto_status_store(struct device *_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count);


DEVICE_ATTR(algname, S_IRUGO | S_IWUSR, crypto_algname_show, crypto_algname_store);
DEVICE_ATTR(keyaddr, S_IRUGO | S_IWUSR, crypto_keyaddr_show, crypto_keyaddr_store);
DEVICE_ATTR(keysize, S_IRUGO | S_IWUSR, crypto_keysize_show, crypto_keysize_store);
DEVICE_ATTR(srcaddr, S_IRUGO | S_IWUSR, crypto_srcaddr_show, crypto_srcaddr_store);
DEVICE_ATTR(dstaddr, S_IRUGO | S_IWUSR, crypto_dstaddr_show, crypto_dstaddr_store);
DEVICE_ATTR(size, S_IRUGO | S_IWUSR, crypto_size_show, crypto_size_store);
DEVICE_ATTR(dir, S_IRUGO | S_IWUSR, crypto_dir_show, crypto_dir_store);
DEVICE_ATTR(status, S_IRUGO | S_IWUSR, crypto_status_show, crypto_status_store);


static ssize_t crypto_algname_show(struct device *_dev,
			     struct device_attribute *attr,
			     char *buf)
{
	crypto_device_t * pcrypt_info = dev_get_drvdata(_dev);

	
	if (pcrypt_info->algname!= NULL) {
		return snprintf(buf,strlen(pcrypt_info->algname) + 1,"%s", pcrypt_info->algname);
	} else {
		dev_err(_dev, "Invalid alg name\n");
		return sprintf(buf, "invalid alg name\n");
	}
}


static ssize_t crypto_algname_store(struct device *_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	crypto_device_t * pcrypt_info = dev_get_drvdata(_dev);

	strcpy(pcrypt_info->algname,buf);

	return count;
	
}

static ssize_t crypto_keyaddr_show(struct device *_dev,
			     struct device_attribute *attr,
			     char *buf)
{
	crypto_device_t * pcrypt_info = dev_get_drvdata(_dev);

	return snprintf(buf, sizeof("00000000\n") + 1,"%x\n", pcrypt_info->keyaddr);
}

/**
 * Store the value in the register of PHY power
 * 
 * 
 */
static ssize_t crypto_keyaddr_store(struct device *_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	crypto_device_t * pcrypt_info = dev_get_drvdata(_dev);
	pcrypt_info->keyaddr = simple_strtoul(buf,NULL,16);

	return count;
	
}

static ssize_t crypto_keysize_show(struct device *_dev,
			     struct device_attribute *attr,
			     char *buf)
{
	crypto_device_t * pcrypt_info = dev_get_drvdata(_dev);

	return snprintf(buf, sizeof("32\n") + 1,"%d\n", pcrypt_info->keysize);
}

/**
 * Store the value in the register of PHY power
 * 
 * 
 */
static ssize_t crypto_keysize_store(struct device *_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	crypto_device_t * pcrypt_info = dev_get_drvdata(_dev);
	pcrypt_info->keysize = simple_strtoul(buf,NULL,10);

	return count;
	
}

static ssize_t crypto_srcaddr_show(struct device *_dev,
			     struct device_attribute *attr,
			     char *buf)
{
	crypto_device_t * pcrypt_info = dev_get_drvdata(_dev);

	return snprintf(buf, sizeof("00000000\n") + 1,"%x\n", pcrypt_info->srcaddr);
}

/**
 * Store the value in the register of PHY power
 * 
 * 
 */
static ssize_t crypto_srcaddr_store(struct device *_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	crypto_device_t * pcrypt_info = dev_get_drvdata(_dev);
	pcrypt_info->srcaddr = simple_strtoul(buf,NULL,16);

	return count;
	
}

static ssize_t crypto_dstaddr_show(struct device *_dev,
			     struct device_attribute *attr,
			     char *buf)
{
	crypto_device_t * pcrypt_info = dev_get_drvdata(_dev);

	return snprintf(buf, sizeof("00000000\n") + 1,"%x\n", pcrypt_info->dstaddr);
}

/**
 * Store the value in the register of PHY power
 * 
 * 
 */
static ssize_t crypto_dstaddr_store(struct device *_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	crypto_device_t * pcrypt_info = dev_get_drvdata(_dev);
	pcrypt_info->dstaddr = simple_strtoul(buf,NULL,16);

	return count;
	
}

static ssize_t crypto_size_show(struct device *_dev,
			     struct device_attribute *attr,
			     char *buf)
{
	crypto_device_t * pcrypt_info = dev_get_drvdata(_dev);

	return snprintf(buf, sizeof("00000000\n") + 1,"%x\n", pcrypt_info->size);
}

/**
 * Store the value in the register of PHY power
 * 
 * 
 */
static ssize_t crypto_size_store(struct device *_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	crypto_device_t * pcrypt_info = dev_get_drvdata(_dev);
	pcrypt_info->size = simple_strtoul(buf,NULL,16);

	return count;
	
}

static ssize_t crypto_dir_show(struct device *_dev,
			     struct device_attribute *attr,
			     char *buf)
{
	crypto_device_t * pcrypt_info = dev_get_drvdata(_dev);

	return snprintf(buf, sizeof("1\n") + 1,"%d\n", pcrypt_info->dir);
}

/**
 * Store the value in the register of PHY power
 * 
 * 
 */
static ssize_t crypto_dir_store(struct device *_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	crypto_device_t * pcrypt_info = dev_get_drvdata(_dev);
	pcrypt_info->dir = simple_strtoul(buf,NULL,10);

	return count;
	
}

static ssize_t crypto_status_show(struct device *_dev,
			     struct device_attribute *attr,
			     char *buf)
{
	crypto_device_t * pcrypt_info = dev_get_drvdata(_dev);

    if (pcrypt_info->status < CRYPTO_STATUS_FINISH)
        wait_event_interruptible(crypto_wq, pcrypt_info->status >= CRYPTO_STATUS_FINISH );
	return snprintf(buf, sizeof("1\n") + 1,"%d\n", pcrypt_info->status);
}

/**
 * Store the value in the register of PHY power
 * 
 * 
 */
static ssize_t crypto_status_store(struct device *_dev,
			      struct device_attribute *attr,
			      const char *buf, size_t count)
{
	crypto_device_t * pcrypt_info = dev_get_drvdata(_dev);
	pcrypt_info->status = simple_strtoul(buf,NULL,10);

       if(pcrypt_info->status==CRYPTO_STATUS_START)
       {
            pcrypt_info->status = CRYPTO_STATUS_NONE;
            pcrypt_info->status = crypto_done(pcrypt_info);
            wake_up_interruptible(&crypto_wq);
       }

	return count;
	
}
    
/******************************************************/
static void create_device_attribs(struct device *dev)
{
    device_create_file(dev, &dev_attr_algname);
    device_create_file(dev, &dev_attr_keyaddr);
    device_create_file(dev, &dev_attr_keysize);
    device_create_file(dev, &dev_attr_srcaddr);
    device_create_file(dev, &dev_attr_dstaddr);
    device_create_file(dev, &dev_attr_size);
    device_create_file(dev, &dev_attr_dir);
    device_create_file(dev, &dev_attr_status);
         
}
static void remove_device_attribs(struct device *dev)
{
    device_remove_file(dev, &dev_attr_algname);
    device_remove_file(dev, &dev_attr_keyaddr);
    device_remove_file(dev, &dev_attr_keysize);
    device_remove_file(dev, &dev_attr_srcaddr);
    device_remove_file(dev, &dev_attr_dstaddr);
    device_remove_file(dev, &dev_attr_size);
    device_remove_file(dev, &dev_attr_dir);
    device_remove_file(dev, &dev_attr_status);
    
}

/******************************************************/
static int crypto_dev_probe(struct platform_device *pdev) 
{
	int ret = 0;

	memset(&crypt_info,0,sizeof(struct crypto_device_struct));

	dev_set_drvdata(&pdev->dev,&crypt_info);
	create_device_attribs(&pdev->dev);
	
	return ret;
}



static int crypto_dev_remove(struct platform_device *pdev) 
{
	remove_device_attribs(&pdev->dev);
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id crypto_device_dt_match[]={
	{	.compatible = "amlogic,crypto-device",
	},
	{},
};
#else
#define amlogic_cpufreq_meson_dt_match NULL
#endif

static struct platform_driver crypto_dev_driver = { 
	.probe = crypto_dev_probe, 
	.remove = crypto_dev_remove, 
//	.shutdown
//	.resume
//	.suspend
	.driver = {
			.name = "crypto_device",						
			.owner = THIS_MODULE,
			.of_match_table = crypto_device_dt_match,
	}, 
};

static int __init crypto_dev_init(void) 
{
	init_waitqueue_head(&crypto_wq);
	return platform_driver_register(&crypto_dev_driver);
}

static void __exit crypto_dev_exit(void) 
{
	platform_driver_unregister(&crypto_dev_driver);
} 

arch_initcall(crypto_dev_init);
module_exit(crypto_dev_exit);

MODULE_AUTHOR("AMLOGIC");
MODULE_DESCRIPTION("crypto device for user can use crypto alg in kernel");
MODULE_LICENSE("GPL");

