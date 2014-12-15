/*
 * E-FUSE char device driver.
 *
 * Author: Bo Yang <bo.yang@amlogic.com>
 *
 * Copyright (c) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the smems of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 */

#include <linux/cdev.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <asm/uaccess.h>
#include <linux/platform_device.h>
//#include <plat/io.h>
#include <linux/err.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <crypto/hash.h>
#include <linux/crypto.h>
#include <linux/module.h>
#include "aml_keys.h"
#include <linux/amlogic/securitykey.h>

#ifdef CONFIG_OF
#include <linux/of.h>
#endif

#define KEYS_MODULE_NAME    "aml_keys"
#define KEYS_DRIVER_NAME	"aml_keys"
#define KEYS_DEVICE_NAME    "aml_keys"
#define KEYS_CLASS_NAME     "aml_keys"

#define EFUSE_READ_ONLY     
static unsigned long efuse_status;
#define EFUSE_IS_OPEN           (0x01)



//#define TEST_NAND_KEY_WR


//#define CRYPTO_DEPEND_ON_KERENL

//#define KEY_NODE_CREATE

/*
typedef struct efuse_dev_s
{
    struct cdev cdev;
    unsigned int flags;
} efuse_dev_t;
*/
static uint8_t keys_version;
static uint8_t version_dirty = 0;
static securitykey_dev_t *keys_devp;
static dev_t keys_devno;
static aml_keys_schematic_t * key_schematic[256];
static aml_key_t * curkey;
///static aml_keys_schematic_t *  cur_schematic;
int32_t aml_keys_register(int32_t version, aml_keys_schematic_t * schematic)
{
    if (version < 1 || version > 255)
        return -1;
    if (key_schematic[version] != NULL)
        return -1;
    key_schematic[version] = schematic;
    return 0;
}
static aml_keybox_provider_t * providers[5];

static void trigger_key_init(void);



int32_t aml_keybox_provider_register(aml_keybox_provider_t * provider)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(providers); i++)
    {
        if (providers[i])
            continue;
        providers[i] = provider;
        printk("i=%d,register --- %s\n", i,provider->name);
		break;
    }
    return 0;
}
aml_keybox_provider_t * aml_keybox_provider_get(char * name)
{
    int i;
    for (i = 0; i < ARRAY_SIZE(providers); i++)
    {
        if (providers[i] == NULL)
            continue;
        printk("name=%s %s\n", providers[i]->name, name);
        if (strcmp(name, providers[i]->name))
            continue;
        return providers[i];
    }
    return NULL;

}

#ifdef CRYPTO_DEPEND_ON_KERENL
/**
 * Crypto API
 */
static struct crypto_blkcipher *aml_keybox_crypto_alloc_cipher(void)
{
    return crypto_alloc_blkcipher("cbc(aes)", 0, CRYPTO_ALG_ASYNC);
}

static int aml_keybox_aes_encrypt(const void *key, int key_len,
                                  const u8 *aes_iv, void *dst, size_t *dst_len,
                                  const void *src, size_t src_len)
{
    struct scatterlist sg_in[2], sg_out[1];
    struct crypto_blkcipher *tfm = aml_keybox_crypto_alloc_cipher();
    struct blkcipher_desc desc =
        { .tfm = tfm, .flags = 0 };
    int ret;
    void *iv;
    int ivsize;
    size_t zero_padding = (0x10 - (src_len & 0x0f));
    char pad[16];

    if (IS_ERR(tfm))
        return PTR_ERR(tfm);

    memset(pad, zero_padding, zero_padding);

    *dst_len = src_len + zero_padding;

    crypto_blkcipher_setkey((void *) tfm, key, key_len);
    sg_init_table(sg_in, 2);
    sg_set_buf(&sg_in[0], src, src_len);
    sg_set_buf(&sg_in[1], pad, zero_padding);
    sg_init_table(sg_out, 1);
    sg_set_buf(sg_out, dst, *dst_len);
    iv = crypto_blkcipher_crt(tfm)->iv;
    ivsize = crypto_blkcipher_ivsize(tfm);

    memcpy(iv, aes_iv, ivsize);
    /*
     print_hex_dump(KERN_ERR, "enc key: ", DUMP_PREFIX_NONE, 16, 1,
     key, key_len, 1);
     print_hex_dump(KERN_ERR, "enc src: ", DUMP_PREFIX_NONE, 16, 1,
     src, src_len, 1);
     print_hex_dump(KERN_ERR, "enc pad: ", DUMP_PREFIX_NONE, 16, 1,
     pad, zero_padding, 1);
     */
    ret = crypto_blkcipher_encrypt(&desc, sg_out, sg_in,
                                   src_len + zero_padding);
    crypto_free_blkcipher(tfm);
    if (ret < 0)
        pr_err("ceph_aes_crypt failed %d\n", ret);
    /*
     print_hex_dump(KERN_ERR, "enc out: ", DUMP_PREFIX_NONE, 16, 1,
     dst, *dst_len, 1);
     */
    return 0;
}
static int amlogic_keybox_aes_decrypt(const void *key, int key_len,
                                      const u8 *aes_iv, void *dst,
                                      size_t *dst_len, const void *src,
                                      size_t src_len)
{
    struct scatterlist sg_in[1], sg_out[2];
    struct crypto_blkcipher *tfm = aml_keybox_crypto_alloc_cipher();
    struct blkcipher_desc desc =
        { .tfm = tfm };
    char pad[16];
    void *iv;
    int ivsize;
    int ret;
    int last_byte;

    if (IS_ERR(tfm))
        return PTR_ERR(tfm);

    crypto_blkcipher_setkey((void *) tfm, key, key_len);
    sg_init_table(sg_in, 1);
    sg_init_table(sg_out, 2);
    sg_set_buf(sg_in, src, src_len);
    sg_set_buf(&sg_out[0], dst, *dst_len);
    sg_set_buf(&sg_out[1], pad, sizeof(pad));

    iv = crypto_blkcipher_crt(tfm)->iv;
    ivsize = crypto_blkcipher_ivsize(tfm);

    memcpy(iv, aes_iv, ivsize);

    /*
     print_hex_dump(KERN_ERR, "dec key: ", DUMP_PREFIX_NONE, 16, 1,
     key, key_len, 1);
     print_hex_dump(KERN_ERR, "dec  in: ", DUMP_PREFIX_NONE, 16, 1,
     src, src_len, 1);
     */

    ret = crypto_blkcipher_decrypt(&desc, sg_out, sg_in, src_len);
    crypto_free_blkcipher(tfm);
    if (ret < 0)
    {
        pr_err("ceph_aes_decrypt failed %d\n", ret);
        return ret;
    }

    if (src_len <= *dst_len)
        last_byte = ((char *) dst)[src_len - 1];
    else
        last_byte = pad[src_len - *dst_len - 1];
    if (last_byte <= 16 && src_len >= last_byte)
    {
        *dst_len = src_len - last_byte;
    } else
    {
        pr_err("ceph_aes_decrypt got bad padding %d on src len %d\n",
               last_byte, (int)src_len);
        return -EPERM; /* bad padding */
    }
    /*
     print_hex_dump(KERN_ERR, "dec out: ", DUMP_PREFIX_NONE, 16, 1,
     dst, *dst_len, 1);
     */
    return 0;
}
#if 0
static void test_aes(void)
{
    static char *key = "1234567890abcdef1234567890abcdef";
    static char dec[600];
    static char enc[600];
    static char orig[300];
    int i;
    size_t dst_len = 256;
    size_t tlen;
    for (i = 0; i < sizeof(orig); i++)
        orig[i] = i;
    printk("=================================\n");
    aml_keybox_aes_encrypt(key, sizeof(key), "1234567890abcdef1234567890abcdef",
                           enc, &dst_len, orig, sizeof(orig));
    printk("%d\n", dst_len);
    tlen = dst_len;
    dst_len = sizeof(orig);
    amlogic_keybox_aes_decrypt(key, sizeof(key),
                               "1234567890abcdef1234567890abcdef", dec,
                               &dst_len, enc, tlen);
    printk("%d\n", dst_len);
    for (i = 0; i < sizeof(orig); i++)
        if (orig[i] != dec[i])
            printk("%d %d\n", orig[i], dec[i]);
    printk("=================================\n");
}
#endif
#ifdef TEST_NAND_KEY_WR
#define PRINT_HASH(hash) {int __i;printk("%s:%d ",__func__,__LINE__);for(__i=0;__i<32;__i++)printk("%02x,",hash[__i]);printk("\n");}
#else
#define PRINT_HASH(hash) 
#endif

static int aml_key_hash(unsigned char *hash, const char *data, unsigned int len)
{
    struct scatterlist sg;

    struct crypto_hash *tfm;
    struct hash_desc desc;

    tfm = crypto_alloc_hash("sha256", 0, CRYPTO_ALG_ASYNC);
    PRINT_HASH(hash);
    if (IS_ERR(tfm))
        return -EINVAL;

    PRINT_HASH(hash);
    /* ... set up the scatterlists ... */
    sg_init_one(&sg, (u8 *) data, len);
    desc.tfm = tfm;
    desc.flags = 0;

    if (crypto_hash_digest(&desc, &sg,len, hash))
        return -EINVAL;
    PRINT_HASH(hash);
    crypto_free_hash(tfm);

    return 0;
}

static struct
{
    u8 *iv;
    u8 * key;
    int key_len;

} aml_root_key =
    { .iv = "amlogic_hello_iput_vector", .key =
              "1234567890abcdef1234567890abcdef",
      .key_len = 32 };
static int aml_key_encrypt(void *dst, uint16_t * dst_len, const void *src,
                           uint16_t src_len)
{
    int ret;
    size_t dstlen = (size_t) (*dst_len);
    size_t srclen = (size_t) (src_len);

    ret = aml_keybox_aes_encrypt(aml_root_key.key, aml_root_key.key_len,
                                 aml_root_key.iv, dst, &dstlen, src, srclen);
    if (ret)
    {
        pr_err("encrypt error ");
        return ret;
    }
    *dst_len = (uint16_t) dstlen;

    return ret;
}
static int aml_key_decrypt(void *dst, size_t *dst_len, const void *src,
                           size_t src_len)
{
    int ret;
    size_t dstlen = (size_t) (*dst_len);
    size_t srclen = (size_t) (src_len);

    ret = amlogic_keybox_aes_decrypt(aml_root_key.key, aml_root_key.key_len,
                                     aml_root_key.iv, dst, &dstlen, src,
                                     srclen);
    if (ret)
    {
        pr_err("encrypt error ");
        return ret;
    }
    *dst_len = (uint16_t) dstlen;
    return ret;
}
#else
extern int aml_algorithm_aes_enc_dec(int encFlag,unsigned char *out,int *outlen,unsigned char *in,int inlen);

static int aml_key_encrypt(void *dst, size_t * dst_len, const void *src,
                           size_t src_len)
{
	int ret=0;
	int keydatalen,buf_len;
	char *data;
	unsigned char bEncryptFlag = 1;
	size_t dstlen;
	keydatalen = src_len + 4;
	buf_len = ((keydatalen+15)>>4)<<4;
	dstlen = buf_len;
	
	data = kzalloc(buf_len, GFP_KERNEL);
	if(data == NULL){
		printk("malloc mem fail,%s:%d\n",__func__,__LINE__);
		return -ENOMEM;
	}
	memset(data,0,buf_len);
	memcpy(&data[0],&src_len,4);
	memcpy(&data[4],src,src_len);
	//the aml aes is used from 2013.12.19
	ret = aml_algorithm_aes_enc_dec(bEncryptFlag,(unsigned char *)dst,(int*)&dstlen,data,buf_len);
	*dst_len = dstlen;
	kfree(data);
	return ret;
}

static int aml_key_decrypt(void *dst, size_t *dst_len, const void *src,
                           size_t src_len)
{
	int ret=0;
	size_t srclen = src_len;
	size_t dstlen;
	size_t keydatalen;
	char *data;
	unsigned char bEncryptFlag = 0;
	srclen = ((src_len+15)>>4)<<4;
	if(src_len != ((src_len+15)>>4)<<4)
	{
		printk("data len is not 16 byte aligned  error!\n");
		return -ENOMEM; 
	}
	data = kzalloc(srclen, GFP_KERNEL);
	if(data == NULL){
		printk("malloc mem fail,%s:%d\n",__func__,__LINE__);
		return -ENOMEM;
	}
	dstlen=srclen;
	memset(data,0,srclen);
	//the aml aes is used from 2013.12.19
	ret = aml_algorithm_aes_enc_dec(bEncryptFlag,data,(int*)&dstlen,(unsigned char *)src,srclen);
	memcpy(&keydatalen,data,4);
	memcpy(dst,&data[4],keydatalen);
	*dst_len = keydatalen;
	
	kfree(data);
	return ret;
}

typedef int (*aes_algorithm_t)(void *dst,size_t * dst_len,const void *src,size_t src_len);
static aes_algorithm_t aes_algorithm_encrypt=NULL;
static aes_algorithm_t aes_algorithm_decrypt=NULL;

extern int aml_aes_encrypt(unsigned char *output,unsigned char *input,int size);
extern int aml_aes_decrypt(unsigned char *output,unsigned char *input,int size);
static int aml_keysafety_encrypt(void *dst, size_t * dst_len, const void *src,
                           size_t src_len)
{
	int ret=0;
	size_t srclen = src_len;
	//size_t dstlen;
	size_t keydatalen;
	unsigned char *data;

	keydatalen = src_len+4;
	srclen = ((keydatalen+15)>>4)<<4;

	data = kzalloc(srclen, GFP_KERNEL);
	if(data == NULL){
		printk("malloc mem fail,%s:%d\n",__func__,__LINE__);
		return -ENOMEM;
	}
	memset(data,0,srclen);
	memcpy(&data[0],&src_len,4);
	memcpy(&data[4],src,src_len);
	ret = aml_aes_encrypt((unsigned char *)dst,data,srclen);
	*dst_len = srclen;
	kfree(data);
	return ret;
}
static int aml_keysafety_decrypt(void *dst, size_t *dst_len, const void *src,
                           size_t src_len)
{
	int ret=0;
	size_t srclen = src_len;
	size_t keydatalen;
	unsigned char *data;

	srclen = ((src_len+15)>>4)<<4;
	if(src_len != ((src_len+15)>>4)<<4)
	{
		printk("data len is not 16 byte aligned error!\n");
		return -ENOMEM; 
	}
	data = kzalloc(srclen, GFP_KERNEL);
	if(data == NULL){
		printk("malloc mem fail,%s:%d\n",__func__,__LINE__);
		return -ENOMEM;
	}
	memset(data,0,srclen);
	ret = aml_aes_decrypt(data,(unsigned char*)src,srclen);
	memcpy(&keydatalen,data,4);
	if(keydatalen <= srclen){
		// this decrypt is ok
		memcpy(dst,&data[4],keydatalen);
		*dst_len = keydatalen;
	}
	else{
		// this decrypt is err
		memcpy(dst,&data[4],srclen);
		*dst_len = srclen;
	}
	kfree(data);
	return ret;
}
#if 0
extern int hash_sha256(unsigned char *buf,int len,unsigned char *hash);
static int aml_key_hash(unsigned char *hash, const char *data, unsigned int len)
{
	int ret;
	ret = hash_sha256((unsigned char *)data,(int)len,hash);
	return ret;
}
#endif
int register_aes_algorithm(int storage_version)
{
	int ret=-1;
	if(storage_version == 1){
		printk("%s:%d,old way\n",__func__,__LINE__);
		aes_algorithm_encrypt = aml_key_encrypt;
		aes_algorithm_decrypt = aml_key_decrypt;
		ret = 0;
	}
	else if(storage_version == 2){
		printk("%s:%d,new way\n",__func__,__LINE__);
		aes_algorithm_encrypt = aml_keysafety_encrypt;
		aes_algorithm_decrypt = aml_keysafety_decrypt;
		ret = 0;
	}
	return ret;
}

#endif

/**
 ssize_t (*show)(struct device *dev, struct device_attribute *attr,
 char *buf);
 ssize_t (*store)(struct device *dev, struct device_attribute *attr,
 const char *buf, size_t count);
 };
 */
/**
 * @todo key process , identy the key version
 */
static int debug_mode = 1;//1:debug,0:normal
static int postpone_write = 0; //1:postpone, 0:normal once write

static ssize_t mode_show(struct device *dev, struct device_attribute *attr,
                         char *buf)
{
    return sprintf(buf, "debug=%s,postpone_write=%s",
                   debug_mode ? "enable" : "disable",
                   postpone_write ? "enable" : "disable");
}
static ssize_t mode_store(struct device *dev, struct device_attribute *attr,
                          const char *buf, size_t count)
{
    if (strncmp(buf, "debug=", 6) == 0)
    {
        if (strncmp(&buf[6], "enable", 6) == 0)
        {
            debug_mode = 1;
        }
        if (strncmp(&buf[6], "disable", 7) == 0)
        {
            debug_mode = 0;
        } else
        {
            return -EINVAL;
        }

    } else if (strncmp(buf, "postpone_write=", 14) == 0)
    {

        if (strncmp(&buf[14], "enable", 6) == 0)
        {
            postpone_write = 1;
        }
        if (strncmp(&buf[14], "disable", 7) == 0)
        {
            postpone_write = 0;
        } else
        {
            return -EINVAL;
        }
    } else
    {
        return -EINVAL;
    }

    return count;
}
static DEVICE_ATTR(mode, 0660, mode_show, mode_store);
#ifdef CRYPTO_DEPEND_ON_KERENL
static int aml_key_write_hash(aml_key_t * key, char * hash)
{
    struct aml_key_hash_s key_hash;
    int slot= AML_KEY_GETSLOT(key);
    key_hash.size = key->valid_size;

    memcpy(key_hash.hash, hash, sizeof(key_hash.hash));
    PRINT_HASH(hash);
    key_schematic[keys_version]->hash.write(key,slot,
                                            (char*) &key_hash);
    if (debug_mode == 0 && postpone_write == 0)
        __v3_write_hash(slot, (char*) &key_hash);
    else
        key->st |= AML_KEY_ST_EFUSE_DIRTY;

    return 0;
}
static int aml_key_read_hash(aml_key_t * key, char * hash)
{
    int i;
    struct aml_key_hash_s key_hash;
	int slot= AML_KEY_GETSLOT(key);

    if (debug_mode == 0 )
    {
        __v3_read_hash(slot, (char*) &key_hash);
        key_schematic[keys_version]->hash.write(key,slot,
                                                (char*) &key_hash);
    }
    else
        key_schematic[keys_version]->hash.read(key,slot,
												(char*) &key_hash);
    PRINT_HASH(key_hash.hash);
    printk("key_hash.size:%d,key->valid_size:%d,%s:%d\n",key_hash.size,key->valid_size,__func__,__LINE__);
    if (key_hash.size != key->valid_size && key_hash.size != 0){
		pr_err("key_hash.size != key->valid_size");
        return -EINVAL;
    }
    PRINT_HASH(key_hash.hash);
    if (key_hash.size == 0)
    {
        for (i = 0; i < 32; i++)
            if (key_hash.hash[i]){
				pr_err("key_hash.hash[i]!=0");
                return -EIO;
            }
        return 1;
    }
    PRINT_HASH(key_hash.hash);
    memcpy(hash, key_hash.hash, sizeof(key_hash.hash));
    return 0;
}
#else
#if 0
static int aml_key_write_hash(aml_key_t * key, char * hash)
{
	struct aml_key_hash_s key_hash;
	int slot= 0;
	key_hash.size = key->valid_size;

	memcpy(key_hash.hash, hash, sizeof(key_hash.hash));
	if(key_schematic[keys_version]->hash.write){
	    key_schematic[keys_version]->hash.write(key,slot,(char*) &key_hash);
	}
	return 0;
}
#endif
static int aml_key_read_hash(aml_key_t * key, char * hash)
{
    int i;
    struct aml_key_hash_s key_hash;
    int slot=0;

	if(key_schematic[keys_version]->hash.read){
		key_schematic[keys_version]->hash.read(key,slot,(char*) &key_hash);
	}
	if (key_hash.size != key->valid_size && key_hash.size != 0){
		printk("%s:%d,key_hash.size != key->valid_size",__func__,__LINE__);
		return -EINVAL;
	}
	if (key_hash.size == 0)
	{
		for (i = 0; i < 32; i++){
			if (key_hash.hash[i]){
				printk("key_hash.hash[i]!=0");
				return -EIO;
			}
		}
		return 1;
    }
	memcpy(hash, key_hash.hash, sizeof(key_hash.hash));
	return 0;
}
#endif

static uint16_t aml_key_checksum(char *data,int lenth)
{
	uint16_t checksum;
	uint8_t *pdata;
	int i;
	checksum = 0;
	pdata = (uint8_t*)data;
	for(i=0;i<lenth;i++){
		checksum += pdata[i];
	}
	return checksum;
}


static ssize_t key_core_show(struct device *dev, struct device_attribute *attr,
                             char *buf)
{
#define aml_key_show_error_return(error,label) {printk("%s:%d",__func__,__LINE__);n=error;goto label;}
    ssize_t n = 0;
    uint16_t checksum=0;
    size_t readbuff_validlen;
    int i;
    aml_key_t * key = (aml_key_t *) attr;
    size_t size;
    size_t out_size;
    char * data=NULL,* dec_data=NULL;

    i = CONFIG_MAX_STORAGE_KEYSIZE;
    i = ((i+15)>>4)<<4;
    data = kzalloc(i, GFP_KERNEL);
    dec_data = kzalloc(i, GFP_KERNEL);

    size=i;

    if (IS_ERR_OR_NULL(data) || IS_ERR_OR_NULL(dec_data))
        aml_key_show_error_return(-EINVAL, core_show_return);
    memset(data,0,i);
    memset(dec_data,0,i);

    if(key->read(key, data))
    {
        printk("can't get valid key,%s,%d\n",__func__,__LINE__);
        aml_key_show_error_return(-EINVAL, core_show_return);
    }

#if 1
	if(aes_algorithm_decrypt){
		if (aes_algorithm_decrypt(dec_data, &size,data, key->storage_size))
			aml_key_show_error_return(-EINVAL, core_show_return);
	}
	else{
		aml_key_show_error_return(-EINVAL, core_show_return);
	}
#else
    if (aml_key_decrypt(dec_data, &size,data, key->storage_size))
        aml_key_show_error_return(-EINVAL, core_show_return);
#endif

	readbuff_validlen = ((key->valid_size+1)>>1);
	checksum = aml_key_checksum( dec_data,readbuff_validlen);
	if(checksum != key->checksum){
		#ifdef TEST_NAND_KEY_WR
		printk("checksum error: %d,%d,%s:%d\n",checksum,key->checksum,__func__,__LINE__);
		#endif
		aml_key_show_error_return(-EINVAL, core_show_return);
	}

#ifdef CRYPTO_DEPEND_ON_KERENL
    if(size!=key->valid_size)
        aml_key_show_error_return(-EINVAL, core_show_return);
    aml_key_hash(data, dec_data, key->valid_size);
    aml_key_read_hash(key, &data[64]);
    if (memcmp(data, &data[64], 32))
    {
        for(i=0;i<32;i++)
        {
            printk("%x %x |",data[i],data[64+i]);
        }
        printk("\n");
        aml_key_show_error_return(-EINVAL, core_show_return);
    }
#endif

#ifdef TEST_NAND_KEY_WR
	printk("key->valid_size:%d,key->storage_size:%d,%s:%d\n",key->valid_size,key->storage_size,__FILE__,__LINE__);
#endif

    out_size = key->valid_size;
    for (i = 0; i < out_size>>1; i++)
    {
        n += sprintf(&buf[n], "%02x", dec_data[i]);
        //printk("data[%d]:0x%x\n",i,dec_data[i]);
    }
    if(out_size%2)
    {
        n += sprintf(&buf[n], "%x", (dec_data[i]&0xf0)>>4);
        //printk("data[%d]:0x%x\n",i,(dec_data[i]&0xf0)>>4);
    }
#if 0
    // key hash valid don't output
    n += sprintf(&buf[n], "\n");
    for (i = 0; i < 32; i++)
    {
        n += sprintf(&buf[n], "%02x", data[i]);

    }
#endif
core_show_return:
    if (!IS_ERR_OR_NULL(data))
        kfree(data);
    if (!IS_ERR_OR_NULL(dec_data))
        kfree(dec_data);
    return n;
}
#define KEY_READ_ATTR  (S_IRUSR|S_IRGRP)
#define KEY_WRITE_ATTR (S_IWUSR|S_IWGRP)


#define ENABLE_AML_KEY_DEBUG 0
#define aml_key_store_error_return(error,label) {printk("error:%s:%d\n",__func__,__LINE__);err=error;goto label;}
static ssize_t aml_key_store(aml_key_t * key, const char *buf, size_t count)
{
    int i, j, err;
    char * data = NULL;
    char * enc_data = NULL;
#if ENABLE_AML_KEY_DEBUG
    char * temp = NULL;
#endif
    size_t in_key_len=0;
    uint16_t checksum=0;
    size_t readbuff_validlen;

    err = count;
#if 0
    if (!(key_is_not_installed(*key) || key_slot_is_inval(*key)))
    {
        printk("Key has been initialled\n");
        aml_key_store_error_return(-EINVAL, store_error_return);
    }
#endif
    i = CONFIG_MAX_STORAGE_KEYSIZE;
    i = ((i+15)>>4)<<4;
    data = kzalloc(i, GFP_KERNEL);
    enc_data = kzalloc(i, GFP_KERNEL);
#if ENABLE_AML_KEY_DEBUG
    temp=kzalloc(i,GFP_KERNEL);
    if(IS_ERR_OR_NULL(temp))
        aml_key_store_error_return(-EINVAL,store_error_return);
#endif

    if (IS_ERR_OR_NULL(data) || IS_ERR_OR_NULL(enc_data))
        aml_key_store_error_return(-EINVAL, store_error_return);

    if(CONFIG_MAX_VALID_KEYSIZE<count)
    {
		printk("key size is too much, count:%d,valid:%d\n",count,CONFIG_MAX_VALID_KEYSIZE);
		aml_key_store_error_return(-EINVAL, store_error_return);
    }
    memset(data,0,i);
    memset(enc_data,0,i);
    
    /**
     * if key is not
     */
    for (i = 0; i * 2 < count; i++, buf += 2)
    {
        for (j = 0; j < 2; j++)
        {
            switch (buf[j])
            {
                case '0' ... '9':
                    //data[i] |= (buf[j] - '0') << (j * 4);
                    data[i] |= (buf[j] - '0') << ((j?0:1) * 4);
                    in_key_len++;
                    break;
                case 'a' ... 'f':
                    //data[i] |= (buf[j] - 'a') << (j * 4);
                    data[i] |= (buf[j] - 'a' + 10) << ((j?0:1) * 4);
                    in_key_len++;
                    break;
                case 'A' ... 'F':
                    //data[i] |= (buf[j] - 'A') << (j * 4);
                    data[i] |= (buf[j] - 'A' + 10) << ((j?0:1) * 4);
                    in_key_len++;
                    break;
                case '\n':
                case '\r':
                case 0:
                    break;
                default:
                    aml_key_store_error_return(-EINVAL, store_error_return);
                    break;
            }
        }
        #if 0
        if (i >= key->valid_size)
        {
            printk("size is not legal %d %d\n", i, key->valid_size);
            aml_key_store_error_return(-EINVAL, store_error_return);
        }
        #endif
    }
    key->valid_size = in_key_len;
#ifdef TEST_NAND_KEY_WR
   	printk("key:valid_size:%d,storage_size:%d,%s:%d\n",key->valid_size,key->storage_size,__func__,__LINE__);
#endif

#ifdef CRYPTO_DEPEND_ON_KERENL
	PRINT_HASH(data);
    aml_key_hash(enc_data, data, key->valid_size);
	aml_key_read_hash(key, &enc_data[64]);
    if (key_is_not_installed(*key))
    {
        aml_key_write_hash(key, enc_data);
    } 
	else
    { ///key is inval
    #ifndef TEST_NAND_KEY_WR
        if (memcmp(enc_data, &enc_data[64], 32))
        {
            printk("Hash does not equal\n");
            aml_key_store_error_return(-EINVAL, store_error_return);
        }
    #else
		// for test
		aml_key_write_hash(key, enc_data);
    #endif
    }
#endif
    ///printk("xxxxxxjjjddddddd %d\n",key->storage_size);
    readbuff_validlen = ((key->valid_size+1)>>1);
    checksum = aml_key_checksum( data,readbuff_validlen);
    key->checksum = checksum;

#if 1
	if(aes_algorithm_encrypt){
		if(aes_algorithm_encrypt(enc_data, (size_t*)&key->storage_size, data, readbuff_validlen))
			aml_key_store_error_return(-EINVAL, store_error_return);
	}
	else{
		aml_key_store_error_return(-EINVAL, store_error_return);
	}
#else
    if(aml_key_encrypt(enc_data, (size_t*)&key->storage_size, data, readbuff_validlen))
        aml_key_store_error_return(-EINVAL, store_error_return);
#endif
#ifdef TEST_NAND_KEY_WR
	printk("key:valid_size:%d,storage_size:%d,%s\n",key->valid_size,key->storage_size,__func__);
#endif
#if 0

    aml_key_decrypt(temp, &dec_size, enc_data, enc_size);
    printk("%s:%d enc_size=%d dec_size=%d\n", __FILE__, __LINE__, enc_size,
           dec_size);
    for (i = 0; i < dec_size; i++)
    {
        if (temp[i] != data[i])
        {
            printk("%s:%d %d %x %x\n", __FILE__, __LINE__, i, temp[i],
                   data[i]);
        }
    }

#endif
    key->write(key, enc_data);
    err = count;

	if(!postpone_write){
	 	key_schematic[keys_version]->flush(key_schematic[keys_version]);
	 }
store_error_return: 
	if (!IS_ERR_OR_NULL(data))
        kfree(data);
    if (!IS_ERR_OR_NULL(enc_data))
        kfree(enc_data);
#if ENABLE_AML_KEY_DEBUG
    if (!IS_ERR_OR_NULL(temp))
        kfree(temp);
#endif
    return err;
}
#if 0
static ssize_t key_core_store(struct device *dev, struct device_attribute *attr,
                              const char *buf, size_t count)
{
	int err;
	//dump_stack();
	//printk("%s\n\n\n",__func__);
    /**
     * @todo add sysfs node operation here;
     */
	err = aml_key_store((aml_key_t*) attr, buf, count);
    //if( aml_key_store((aml_key_t*) attr, buf, count)>=0)
    if(err >= 0)
    {
#ifdef KEY_NODE_CREATE
		#ifndef TEST_NAND_KEY_WR
		attr->attr.mode &= (~KEY_WRITE_ATTR);  
		sysfs_chmod_file(&dev->kobj,attr,KEY_READ_ATTR);
		#endif
#endif
		//printk("%s,attr WR change to RD\n",__func__);
    }
    return err;
}
#endif
#if 0
static ssize_t key_core_control(struct device *class,
                                struct device_attribute *attr, const char *buf,
                                size_t count)
{
    /**
     * @todo next version function
     */
    return -1;
}
#endif
#if 0
static int key_check_inval(aml_key_t * key)
{
    uint8_t buf[32];
    int ret = aml_key_read_hash(key, buf);
    if (ret == -EINVAL)
    {
        key->st |= AML_KEY_ST_INVAL;
        return 1;
    }
    if (ret == 1)
    {
        key->st &= ~(AML_KEY_ST_INVAL | AML_KEY_ST_INSTALLED);
    }
    return 0;
}
#endif
static ssize_t key_node_set(struct device *dev);
int32_t aml_keys_set_version(struct device *dev, uint8_t version, int storer)
{
    aml_key_t * keys;
    char **keyfile = (char**) dev->platform_data;
#ifdef KEY_NODE_CREATE
    int i;
#endif
    int ret;
    int keyfile_index;
    //if (keys_version > 0 && keys_version < 256) ///has been initial
    //    return -1;

#ifdef TEST_NAND_KEY_WR
	printk("version:%d,%s\n",version,__func__);
#endif
    if (version < 1 || version > 255)
        return -EINVAL;

    if(storer < 0)
	return -EINVAL;
	
	if(version_dirty == 1){
		return 0;//done once init
	}
	
    keyfile_index = storer;
//    printk("keyfile:%s\n",keyfile[0]);
//    printk("keyfile:%s\n",keyfile[1]);
    if (key_schematic[version] == NULL
            || key_schematic[version]->init(key_schematic[version], keyfile[keyfile_index])
                    < 0) ///@todo Platform Data
    {
        printk(KERN_ERR KEYS_DEVICE_NAME ": version %d can not be init %p\n",
               keys_version, key_schematic[version]);
        return -EINVAL;
    }
    keys_version = version;

    keys = key_schematic[keys_version]->keys;
    
    ret = key_node_set(dev);
    if (ret < 0){
		printk("creat key dev fail,%s:%d\n",__func__,__LINE__);
		return -EINVAL;
	}
    version_dirty = 1;

	//printk("%s,keys_version:%d,key_schematic[keys_version]->count:%d\n",__func__,keys_version,key_schematic[keys_version]->count);
#ifdef KEY_NODE_CREATE
    for (i = 0; i < key_schematic[keys_version]->count; i++)
    {
        mode_t mode;
        if (key_slot_is_inval(keys[i]))
            continue;
        keys[i].update_status(&keys[i]);

        mode = KEY_READ_ATTR;
        if (key_is_otp_key(keys[i]))
        {
            keys[i].attr.show = key_core_show;
			#ifdef TEST_NAND_KEY_WR  //for test
                mode |= KEY_WRITE_ATTR;
                keys[i].attr.store = key_core_store;
			#endif
            if (key_is_not_installed(keys[i]) || key_check_inval(&keys[i]))
            {
                mode |= KEY_WRITE_ATTR;
                keys[i].attr.store = key_core_store;
            } else if (key_is_readable(keys[i]))
            {
                mode |= KEY_READ_ATTR;
                keys[i].attr.show = key_core_show;
            } else if (key_is_control_node(keys[i]))
            {
                ///@todo in the future we must implement it
                printk("%s:%d\n", __FILE__, __LINE__);
                return -1;
                ///keys[i].attr.store = key_core_control;
            }
            keys[i].attr.attr.name = &keys[i].name[0];
            keys[i].attr.attr.mode = mode;
        } else
        {
            printk("%s:%d\n", __FILE__, __LINE__);
            return -EINVAL; ///@todo NO not OTP key support Now

        }
        ///type , r ,w , OTP,
        ret = device_create_file(
                dev, (const struct device_attribute *) &keys[i].attr);
        if (ret < 0)
        {
            printk("%s:%d\n", __FILE__, __LINE__);
            return -EINVAL;
        }
	printk("keys[%d].name:%s, device_create_file ok,%s:%d\n",i,keys[i].name,__FILE__,__LINE__);
    }
#endif
    /**
     * @todo remove version write interface
     */
    return 0;
}
/***
 * version show and setting rounte
 * @return
 */
static int32_t version_check(void)
{
    /**
     * @todo add real efuse operation
     */
#if 0
    return efuse_read_version();
#else

    return 3;
#endif
}

#define NAND_STORE_KEY_INDEX    0
#define EMMC_STORE_KEY_INDEX    1
#define NAND_STORE_KEY     "nand"
#define EMMC_STORE_KEY     "emmc"
#define AUTO_STORE_KEY		"auto"
//static int keyfileindex=-1;

typedef struct{
	char dev_name[16];
	int  index;
}key_store_device_t;

static key_store_device_t key_store_device[]={
	[0]={
		.dev_name = "nand",
		.index	  = NAND_STORE_KEY_INDEX,
	},
	[1]={
		.dev_name = "emmc",
		.index	  = EMMC_STORE_KEY_INDEX,
	},
};
#define KEY_STORE_DEVICE_NUM	sizeof(key_store_device)/sizeof(key_store_device[0])

static ssize_t version_show(struct device *dev, struct device_attribute *attr,
                            char *buf)
{
    version_check();
    return sprintf(buf, "version=%d", keys_version);
}
static ssize_t version_store(struct device *dev, struct device_attribute *attr,
                             const char *buf, size_t count)
{
    char *endp;
    unsigned long new ;
    int ret = -EINVAL;
    int storer;
    int dev_type;

    if (strncmp(buf, NAND_STORE_KEY, 4) == 0)
    {	storer = NAND_STORE_KEY_INDEX;
		dev_type = 0;
    }
    else if(strncmp(buf, EMMC_STORE_KEY, 4) == 0)
    {	storer = EMMC_STORE_KEY_INDEX;
		dev_type = 0;
    }
    else  if(strncmp(buf, AUTO_STORE_KEY, 4) == 0)
    {
		dev_type = 1;
    }
    else{
		printk("store memory not know\n");
		return ret;
	}

	new = simple_strtoul(buf+4, &endp, 0);

	if (endp == (buf+4))
	{
		printk("version NO error\n");
		return ret;
	}

	if (new < 1 || new > 255)
		return ret;

	if(dev_type == 0){
		if (aml_keys_set_version(dev, new,storer) < 0)
		{
			return ret;
		}
	}
	else if(dev_type == 1)
	{
		int i;
		for(i=0;i<KEY_STORE_DEVICE_NUM;i++){
			storer = key_store_device[i].index;
			if (aml_keys_set_version(dev, new,storer) < 0)
			{
				continue;
			}
			else{
				break;
			}
		}
		if(i>=KEY_STORE_DEVICE_NUM){
			return ret;
		}
	}
	else {
		return ret;
	}
    /***
     * @todo remove write interface .
     */
    return count;
}
DEVICE_ATTR(version, 0660, version_show, version_store);
//static DEVICE_ATTR(version, 0770, version_show, version_store);

static ssize_t storer_show(struct device *dev, struct device_attribute *attr,
                            char *buf)
{
    int ret = -EINVAL;
    if (keys_version == 0)
        return ret;

    if (key_schematic[keys_version]->read(key_schematic[keys_version]) < 0)
        return ret;

    //to do , need read hash to efuse
    if (debug_mode == 0 && postpone_write == 1)
    {
    }
	else{
		//key_schematic[keys_version]->dump(key_schematic[keys_version]);
	}

    return sprintf(buf, "storer=%d\n", keys_version);
}
static ssize_t storer_store(struct device *dev, struct device_attribute *attr,
                             const char *buf, size_t count)
{
    int ret = -EINVAL;
    if (keys_version == 0)
        return ret;

    if(strncmp(buf, "write", 5) != 0)
    {
	printk("cmd error,%s:%d\n",__func__,__LINE__);
	return ret;
    }
    printk("cmd ok,%s:%d\n",__func__,__LINE__);

    if (key_schematic[keys_version]->flush(key_schematic[keys_version]) < 0)
        return -EINVAL;

    //to do , need write hash to efuse
    if (debug_mode == 0 && postpone_write == 1)
    {
    }
    printk("storer_store\n");
    return count;
}

DEVICE_ATTR(storer, 0660, storer_show, storer_store);

static ssize_t key_list_show(struct device *dev, struct device_attribute *attr,
                            char *buf)
{
	aml_key_t * keys;
	int i,n=0;
	if(keys_version == 0){
		return -EINVAL;
	}
	keys = key_schematic[keys_version]->keys;
	for(i=0;i<key_schematic[keys_version]->count;i++)
	{
		//printk("%s,%s:%d\n",keys[i].name,__func__,__LINE__);
		if(keys[i].name[0] != 0){
			n += sprintf(&buf[n], keys[i].name);
			n += sprintf(&buf[n], "\n");
		}
	}
	buf[n] = 0;
	return n;
}
#if 0
static ssize_t key_list_store(struct device *dev, struct device_attribute *attr,
                             const char *buf, size_t count)
{
	return -EINVAL;
}
#endif
//DEVICE_ATTR(key_list, 0660, key_list_show, key_list_store);

static ssize_t key_name_show(struct device *dev, struct device_attribute *attr,
                            char *buf)
{
	int n=0;
	if(curkey == NULL){
		printk("please set cur key name,%s:%d\n",__func__,__LINE__);
		return -EINVAL;
	}
	n += sprintf(&buf[n], curkey->name);
	n += sprintf(&buf[n], "\n");
	return n;
}
static char security_key_name[AML_KEY_NAMELEN];
static ssize_t key_name_store(struct device *dev, struct device_attribute *attr,
                             const char *buf, size_t count)
{
	aml_key_t * keys;
	int i,cnt;
	char *name;
	char *cmd,*oldname=NULL,*newname;
	if(keys_version == 0){
		return -EINVAL;
	}
	keys = key_schematic[keys_version]->keys;

	name = kzalloc(count+1, GFP_KERNEL);
	if(!name){
		printk("don't kzalloc mem,%s:%d\n",__func__,__LINE__);
		return -EINVAL;
	}
	memcpy(name,buf,count+1);
	
	//cmd = (char*)&buf[0];
	cmd = name;
	newname = cmd;
	oldname = cmd;
	cnt=0;
	while((*oldname != ' ')&&(*oldname !='\n')&&(*oldname !='\r')&&(*oldname !=0)){
		cnt++;
		oldname++;
	}
	newname[cnt]=0;
	if(cnt >= AML_KEY_NAMELEN){
		newname[AML_KEY_NAMELEN-1]=0;
	}
	curkey = NULL;
	security_key_name[0]=0;
	for(i=0;i<key_schematic[keys_version]->count;i++)
	{
		if(strcmp(newname,keys[i].name) == 0){
			curkey = &keys[i];
			break;
		}
	}
	if(curkey == NULL)
	{
		for(i=0;i<key_schematic[keys_version]->count;i++)
		{
			if(keys[i].name[0] == 0){
				curkey = &keys[i];
				break;
			}
		}
		if(curkey == NULL){
			printk("key count too much,%s:%d\n",__func__,__LINE__);
			if (!IS_ERR_OR_NULL(name))
				kfree(name);
			return -EINVAL;
		}
		strcpy(security_key_name,newname);
	}
	if (!IS_ERR_OR_NULL(name))
		kfree(name);
	return count;
}
//DEVICE_ATTR(key_name, 0660, key_name_show, key_name_store);
static ssize_t hdcp_store(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	return key_name_store(dev, attr, buf, count);
}

#if 0
static ssize_t key_write_show(struct device *dev, struct device_attribute *attr,
                            char *buf)
{
	return -EINVAL;
}
#endif
static ssize_t key_write_store(struct device *dev, struct device_attribute *attr,
                             const char *buf, size_t count)
{
	int err;
	if(curkey == NULL){
		printk("unkown current key-name,%s:%d\n",__func__,__LINE__);
		return -EINVAL;
	}
	if((security_key_name[0] != 0) &&(curkey->name[0] == 0))
	{
		strcpy(curkey->name,security_key_name);
	}
	err = aml_key_store(curkey, buf, count);
	return err;
}
//DEVICE_ATTR(key_write, 0660, key_write_show, key_write_store);

static ssize_t key_read_show(struct device *dev, struct device_attribute *attr,
                            char *buf)
{
	int err;
	if((curkey == NULL)||(curkey->name[0] == 0)){
		printk("unkown current key-name,%s:%d\n",__func__,__LINE__);
		return -EINVAL;
	}
	//printk("curkey->valid_size:%d,curkey->storage_size:%d,%s:%d\n",curkey->valid_size,curkey->storage_size,__func__,__LINE__);
	err = key_core_show(dev, (struct device_attribute*)curkey,buf);
	return err;
}

static ssize_t hdcp_show(struct device *dev, struct device_attribute *attr,
                            char *buf)
{
	int err;
	if((curkey == NULL)||(curkey->name[0] == 0)){
		printk("unkown current key-name,%s:%d\n",__func__,__LINE__);
		return -EINVAL;
	}
	if (!strncmp(curkey->name, "hdcp2lc128", sizeof("hdcp2lc128")) || !strncmp(curkey->name, "hdcp2key", sizeof("hdcp2key")))
		err = key_core_show(dev, (struct device_attribute*)curkey,buf);
	else
		err = -EINVAL;
	return err;
}
#if 0
static ssize_t key_read_store(struct device *dev, struct device_attribute *attr,
                             const char *buf, size_t count)
{
	int err = -EINVAL;
	return err;
}
#endif
//DEVICE_ATTR(key_read, 0660, key_read_show, key_read_store);

#define USID_KEY_NAME   "usid"
#define MAC_KEY_NAME	"mac"
#define MAC_BT_KEY_NAME "mac_bt"
#define MAC_WIFI_KEY_NAME "mac_wifi"
#define HDCP_KEY_NAME "hdcp"

static char twoASCByteToByte(char c1, char c2)
{
	char cha;
	if((c1>='0')&&(c1<='9'))
		c1 = c1-'0';
	else if((c1 >= 'a')&&(c1<='f'))
		c1 = c1 -'a' + 10;
	else if((c1 >='A')&&(c1<='F'))
		c1 = c1 -'A' + 10;
	if((c2>='0')&&(c2<='9'))
		c2 = c2-'0';
	else if((c2 >= 'a')&&(c2<='f'))
		c2 = c2 -'a' + 10;
	else if((c2 >='A')&&(c2<='F'))
		c2 = c2 -'A' + 10;
	cha = c1 &0x0f;
	cha = ((cha<<4)|(c2&0x0f));
	return cha;
}
static char hexToAscII(char c)
{
	char cha;
	cha = c & 0x0f;
	if((cha>=0)&&(cha<=9)){
		cha += '0';
	}
	else if((cha>=0xa) && (cha <= 0xf)){
		cha = cha - 0xa + 'a';
	}
	return cha;
}

static ssize_t key_usid_show(struct device *dev, struct device_attribute *attr,
                            char *buf)
{
	aml_key_t * keys,*usidkey=NULL;
	int i,j,err=0;
	int count;
	char * data=NULL;
	if(keys_version == 0){
		return -EINVAL;
	}
	keys = key_schematic[keys_version]->keys;
	for(i=0;i<key_schematic[keys_version]->count;i++)
	{
		if(strcmp(USID_KEY_NAME,keys[i].name) == 0){
			usidkey = &keys[i];
			break;
		}
	}
	if(usidkey == NULL){
		printk("don't set %s key-name and key-data,%s:%d\n",USID_KEY_NAME,__func__,__LINE__);
		return -EINVAL;
	}

	data = kzalloc(CONFIG_MAX_STORAGE_KEYSIZE, GFP_KERNEL);
	if(data == NULL){
		printk("don't kzalloc mem %s:%d\n",__func__,__LINE__);
		return -ENOMEM;
	}
	memset(data,0,CONFIG_MAX_STORAGE_KEYSIZE);
	err = key_core_show(dev, (struct device_attribute*)usidkey,data);
	if(err > 0){
		count = err;
		for(i=0,j=0;i<count;j++){
			buf[j] = twoASCByteToByte(data[i],data[i+1]);
			i += 2;
		}
		err = j;
	}
	
	if(data){
		kfree(data);
	}
	return err;
}
#if 0
static ssize_t key_usid_store(struct device *dev, struct device_attribute *attr,
                             const char *buf, size_t count)
{
	return -EINVAL;
}
#endif

struct key_new_node{
	struct device_attribute  attr;
	char name[16];
};
static struct key_new_node key_node_name[]={
	[0]={
		.name = "key_list",
	},
	[1]={
		.name = "key_name",
	},
	[2]={
		.name = "key_write",
	},
	[3]={
		.name = "key_read",
	},
	[4]={
		.name = USID_KEY_NAME,
	},
	[5]={
		.name = HDCP_KEY_NAME,
	},
};
static ssize_t key_node_set(struct device *dev)
{
	int ret,i;
	struct key_new_node *key_new;
	mode_t mode;
	mode = KEY_READ_ATTR;
	key_new = &key_node_name[0];
	i=0;
	key_new[i].attr.show = key_list_show;
	//key_new[i].attr.store = key_list_store;
	key_new[i].attr.attr.name = &key_new[i].name[0];
	key_new[i].attr.attr.mode = mode;
    ret = device_create_file(dev, (const struct device_attribute *) &key_new[i].attr);
    if (ret < 0)
    {
        printk("%s:%d\n", __FILE__, __LINE__);
        return -EINVAL;
    }
	i=1;
	mode = KEY_READ_ATTR | KEY_WRITE_ATTR;
	key_new[i].attr.show = key_name_show;
	key_new[i].attr.store = key_name_store;
	key_new[i].attr.attr.name = &key_new[i].name[0];
	key_new[i].attr.attr.mode = mode;
    ret = device_create_file(dev, (const struct device_attribute *) &key_new[i].attr);
    if (ret < 0)
    {
        printk("%s:%d\n", __FILE__, __LINE__);
        return -EINVAL;
    }
	i=2;
	mode =   KEY_WRITE_ATTR;
	//key_new[i].attr.show = key_write_show;
	key_new[i].attr.store = key_write_store;
	key_new[i].attr.attr.name = &key_new[i].name[0];
	key_new[i].attr.attr.mode = mode;
    ret = device_create_file(dev, (const struct device_attribute *) &key_new[i].attr);
    if (ret < 0)
    {
        printk("%s:%d\n", __FILE__, __LINE__);
        return -EINVAL;
    }
	i=3;
	mode = KEY_READ_ATTR ;
	key_new[i].attr.show = key_read_show;
	//key_new[i].attr.store = key_read_store;
	key_new[i].attr.attr.name = &key_new[i].name[0];
	key_new[i].attr.attr.mode = mode;
    ret = device_create_file(dev, (const struct device_attribute *) &key_new[i].attr);
    if (ret < 0)
    {
        printk("%s:%d\n", __FILE__, __LINE__);
        return -EINVAL;
    }
    i=4;
	mode = KEY_READ_ATTR ;
	key_new[i].attr.show = key_usid_show;
	//key_new[i].attr.store = key_usid_store;
	key_new[i].attr.attr.name = &key_new[i].name[0];
	key_new[i].attr.attr.mode = mode;
    ret = device_create_file(dev, (const struct device_attribute *) &key_new[i].attr);
    if (ret < 0)
    {
        printk("%s:%d\n", __FILE__, __LINE__);
        return -EINVAL;
    }
    i=5;
	mode = KEY_READ_ATTR | KEY_WRITE_ATTR;
	key_new[i].attr.show = hdcp_show;
	key_new[i].attr.store = hdcp_store;
	key_new[i].attr.attr.name = &key_new[i].name[0];
	key_new[i].attr.attr.mode = mode;
    ret = device_create_file(dev, (const struct device_attribute *) &key_new[i].attr);
    if (ret < 0)
    {
        printk("%s:%d\n", __FILE__, __LINE__);
        return -EINVAL;
    }
    return 0;
}

static ssize_t version_available_show(struct device *dev,
                                      struct device_attribute *attr, char *buf)
{
    static char data[CONFIG_MAX_STORAGE_KEYSIZE];
    int i,j;
    ssize_t n=0;
    aml_key_t * key;
    if(keys_version == 0){
		return -EINVAL;
	}
    key=key_schematic[keys_version]->keys;
    n+=sprintf(&buf[n],"key version %d\n",keys_version);
    for(i=0;i<key_schematic[keys_version]->count;i++)
    {
        if(key[i].name[0]){
            n+=snprintf(&buf[n],AML_KEY_NAMELEN,"%s",key[i].name);
            n+=sprintf(&buf[n],"\t%s\t",key_is_not_installed( key[i])?"notinst":"inst");
            n+=sprintf(&buf[n],"%s\t",key_is_efuse_dirty(key[i])?"dirty":"clean");
            n+=sprintf(&buf[n],"%s\t",key_slot_is_inval(key[i])?"invalid":"valid");
            n+=sprintf(&buf[n],"%s\t",key_is_otp_key(key[i])?"otpkey":"Normal");
            n+=sprintf(&buf[n],"\n");
            key[i].read(&key[i],data);
            for(j=0;j<(key[i].valid_size>32?32:key[i].valid_size);j++)
                n+=sprintf(&buf[n],"%02x",data[j]);
            n+=sprintf(&buf[n],"\n");
            aml_key_read_hash(&key[i],data);
            for(j=0;j<32;j++)
                n+=sprintf(&buf[n],"%02x",data[j]);

        }else{
            n+=sprintf(&buf[n],"slot%d no data",i);
        }

        n+=sprintf(&buf[n],"\n");


    }
    return n;
}
static ssize_t installed_keys_show(struct device *dev,
                                   struct device_attribute *attr, char *buf)
{
    /**
     * @todo add available versions .
     */
    return 0;
}
static ssize_t install_key(struct device *dev, struct device_attribute *attr,
                           const char *buf, size_t count)
{
    /**
     * @todo implement it later
     * formate keyname=hexvalue
     */
    aml_key_t * key;
    char * name, *data;
    int i;
    if(keys_version == 0){
		return -EINVAL;
	}
    data = strstr(buf, "=");
    *data++ = 0;
    name = (char*) buf;
    for (i = 0, key = key_schematic[keys_version]->keys;
            i < key_schematic[keys_version]->count; i++, key++)
    {
        if (strcmp(key->name, name))
            continue;
        if (key_is_not_installed((*key)))
            break;
        return -EINVAL;
    }
    if (i == key_schematic[keys_version]->count)
        return -EINVAL;

    return aml_key_store(key, data, count - 1 - strlen(name));
}
///__ATTR(install_key,022,NULL,install_key),
DEVICE_ATTR(install_key, 0220, NULL, install_key);
static struct device_attribute keys_class_attrs[] ={
	__ATTR_RO(version_available),
	__ATTR_RO(installed_keys), 
	__ATTR_NULL 
};
static struct class keys_class ={
	.name = KEYS_CLASS_NAME,
	.dev_attrs = keys_class_attrs, 
};
#include <linux/reboot.h>
static int aml_keys_notify_reboot(struct notifier_block *this,
                                  unsigned long code, void *x)
{
    int i;
    char key_hash[32];
    printk("%s:%d", __func__,__LINE__);
    if (keys_version == 0)
        return NOTIFY_DONE;
//    if (key_schematic[keys_version]->flush(key_schematic[keys_version]) < 0)
//        return NOTIFY_DONE;
    if (debug_mode == 0 && postpone_write == 1)
    {
        postpone_write = 0;

        for (i = 0; i < key_schematic[keys_version]->count; i++)
        {
            aml_key_t * key = &(key_schematic[keys_version]->keys[i]);
            printk("%p", key);
//key_is_installed(key_schematic[keys_version]->keys[i])&&
            if (key_is_efuse_dirty(key_schematic[keys_version]->keys[i]))
            {
           		int slot= AML_KEY_GETSLOT(key);
                key_schematic[keys_version]->hash.read(key,slot,
                                                       (char*) &key_hash);
                __v3_write_hash(slot, (char*) &key_hash);

            }
        }
    }
    return NOTIFY_DONE;
}
static struct notifier_block aml_keys_notify =
{ 
	.notifier_call = aml_keys_notify_reboot, 
	.next = NULL, 
	.priority = 0, 
};
static struct device * key_device= NULL;
void trigger_key_init(void)
{
    printk("amlkeys=%d\n", keys_version);
    if (key_device == NULL)
        return;
    aml_keys_set_version(key_device, version_check(), -1);
    register_reboot_notifier(&aml_keys_notify);
    version_dirty = 0;

}

#if 1
char asc_to_i(char para)
{
    if(para>='0' && para<='9')
        para = para-'0';
    else if(para>='a' && para<='f')
        para = para-'a'+0xa;
    else if(para>='A' && para<='F')
        para = para-'A'+0xa;
        
        return para;
}

#define MAX_BUF_LEN 2048

/* function: get_aml_key_kernel
 * key_name: key name
 * data: key data
 * ascii_flag:  0: ascii after merge bytes     1: asicii that don't merge bytes
 * return : >=0 ok,  <0 error
 * */
int get_aml_key_kernel(const char* key_name, unsigned char* data, int ascii_flag)
{
	int ret;
	int i, j;
	char* buf = NULL;
	if (!key_name) {
		printk("error, keyname or is null\n");
		return -1;
	}
	buf = kmalloc(MAX_BUF_LEN, GFP_KERNEL);
	if (!buf) {
		printk("no memory\n");
		return -1;
	}
	memset(buf, 0, MAX_BUF_LEN);
	//printk("11111111\n");
	ret = key_name_store(NULL, NULL, key_name, strlen(key_name));
	if(ret < 0){
		goto exit;
	}
	//printk("2222222\n");
	ret = key_read_show(NULL, NULL, buf);
	//printk("hdcp strlen is %d\n", strlen(buf));
	if (ret >= 0) {
		if (ascii_flag == 0) {
			for (i=0, j=0; (i < MAX_BUF_LEN) && (buf[i]!=0); i++, j++){
				//data[j]= (((asc_to_i(buf[i]))<<4) | (asc_to_i(buf[++i])));
				data[j]= (((asc_to_i(buf[i]))<<4) | (asc_to_i(buf[i+1]))); i++;
			}
			ret = ret >> 1;
		} else {
			strncpy(data, buf, MAX_BUF_LEN);
		}
	}
exit:
	kfree(buf);
	buf = NULL;
	return ret;
}
#endif
EXPORT_SYMBOL(get_aml_key_kernel);

/**
 *
 * @param inode
 * @param file
 * @return
 */
static int aml_keys_open(struct inode *inode, struct file *file)
{
    int ret = 0;
    securitykey_dev_t *devp;

    devp = container_of(inode->i_cdev, securitykey_dev_t, cdev);
    file->private_data = devp;

    return ret;
}
/**
 *
 * @param inode
 * @param file
 * @return
 */
static int aml_keys_release(struct inode *inode, struct file *file)
{
    int ret = 0;
    securitykey_dev_t *devp;

    devp = file->private_data;
    efuse_status &= ~EFUSE_IS_OPEN;
    return ret;
}

/**
 *
 * @param file
 * @param cmd
 * @param arg
 * @return
 */

static long aml_keys_unlocked_ioctl(struct file *file, unsigned int cmd,
                                    unsigned long arg)
{
    switch (cmd)
    {
        case AML_KEYS_SET_VERSION: ///@todo implement it later
            break;
        case AML_KEYS_INSTALL: ///@todo implement it later
            break;
        case AML_KEYS_INSTALL_ID: ///@todo implement it later
            break;
        default:
            return -ENOTTY;
    }
    return 0;
}
/**
 *
 */
static const struct file_operations keys_fops =
    { .owner = THIS_MODULE,
      .open = aml_keys_open,
      .release = aml_keys_release,
      .unlocked_ioctl = aml_keys_unlocked_ioctl, };


#ifdef CONFIG_OF
static char *get_keys_drv_data(struct platform_device *pdev);
#endif
static int aml_keys_probe(struct platform_device *pdev)
{
    int ret;
    int32_t version;
    struct device *devp;
    printk(KERN_INFO "keys===========================================\n");
    ret = alloc_chrdev_region(&keys_devno, 0, 1, KEYS_DEVICE_NAME);
    if (ret < 0)
    {
        printk(KERN_ERR "efuse: failed to allocate major number\n");
        ret = -ENODEV;
        goto out;
    }
    printk("keys_devno=%x\n", keys_devno);

    ret = class_register(&keys_class);
    if (ret)
        goto error1;

    keys_devp = kmalloc(sizeof(securitykey_dev_t), GFP_KERNEL);
    if (!keys_devp)
    {
        printk(KERN_ERR "securitykey: failed to allocate memory\n");
        ret = -ENOMEM;
        goto error2;
    }

    /* connect the file operations with cdev */
    cdev_init(&keys_devp->cdev, &keys_fops);
    keys_devp->cdev.owner = THIS_MODULE;
    /* connect the major/minor number to the cdev */
    ret = cdev_add(&keys_devp->cdev, keys_devno, 1);
    if (ret)
    {
        printk(KERN_ERR "securitykey: failed to add device\n");
        goto error3;
    }

    devp = device_create(&keys_class, NULL, keys_devno, NULL, KEYS_DEVICE_NAME);
    if (IS_ERR(devp))
    {
        printk(KERN_ERR "securitykey: failed to create device node\n");
        ret = PTR_ERR(devp);
        goto error4;
    }
    printk(KERN_INFO "securitykey: device %s created\n", KEYS_DEVICE_NAME);
    #ifdef CONFIG_OF
    devp->platform_data = get_keys_drv_data(pdev);
    #else
    if (pdev->dev.platform_data) ///@todo add some optimize here
        devp->platform_data = pdev->dev.platform_data;
    else
        devp->platform_data = NULL;
    #endif
    key_device = devp;
    if ((version = version_check()) < 0)
    {
        printk(KERN_ERR KEYS_DEVICE_NAME ": can not get current version\n");

    } else
    {
        dev_attr_version.attr.mode = 0660;
    }
    ret = device_create_file(devp, &dev_attr_version);
    if (ret < 0)
    {
        ret = -ENOMEM; ///change error
        printk("============\n");
        goto error4;
    }
    ret = device_create_file(devp, &dev_attr_mode);
    if (ret < 0)
    {
        ret = -ENOMEM; ///change error
        printk("============\n");
        goto error4;
    }
    ret = device_create_file(devp, &dev_attr_storer);
    if (ret < 0)
    {
        ret = -ENOMEM; ///change error
        printk("============\n");
        goto error4;
    }
    trigger_key_init();
    keys_version = 0;
    return 0;

    error4: cdev_del(&keys_devp->cdev);
    error3: kfree(keys_devp);
    error2: class_unregister(&keys_class);
    error1: unregister_chrdev_region(keys_devno, 1);
    out: return ret;
}

static int aml_keys_remove(struct platform_device *pdev)
{
    unregister_chrdev_region(keys_devno, 1);
    //device_destroy(efuse_clsp, efuse_devno);
    device_destroy(&keys_class, keys_devno);
    cdev_del(&keys_devp->cdev);
    kfree(keys_devp);
    //class_destroy(efuse_clsp);
    class_unregister(&keys_class);
    return 0;
}
#ifdef CONFIG_OF
static const struct of_device_id meson6_keys_dt_match[];
static char *get_keys_drv_data(struct platform_device *pdev)
{
	char *key_dev = NULL;
	if (pdev->dev.of_node) {
		const struct of_device_id *match;
		match = of_match_node(meson6_keys_dt_match, pdev->dev.of_node);
		if(match){
			key_dev = (char*)match->data;
		}
	}
	return key_dev;
}
#endif

#ifdef CONFIG_OF
static char * secure_device[3]={"nand_key","emmc_key",NULL};
//#define SECURE_DRV_DATA ((kernel_ulong_t)&secure_device)

static const struct of_device_id meson6_keys_dt_match[]={
	{	.compatible = "amlogic,aml_keys",
		//.data		= (void *)SECURE_DRV_DATA,
		.data		= (void *)&secure_device[0],
	},
	{},
};
//MODULE_DEVICE_TABLE(of,meson6_rtc_dt_match);
#else
#define meson6_keys_dt_match NULL
#endif

static struct platform_driver aml_keys_driver =
{ 
	.probe = aml_keys_probe, 
	.remove = aml_keys_remove, 
	.driver =
     { 
     	.name = KEYS_DEVICE_NAME, 
        .owner = THIS_MODULE, 
        .of_match_table = meson6_keys_dt_match,
     }, 
};

/* function: extenal_api_key_set_version
 * devvesion: nand3, emmc3,auto3
 * return : >= 0 ok, <0 error
 * */
int extenal_api_key_set_version(char *devvesion)
{
	int err=-1;

	if(key_device == NULL){
		printk("key device don't create,%s:%d\n",__func__,__LINE__);
		return -EINVAL;
	}
	err = version_store(key_device,NULL,devvesion,strlen(devvesion));
	return err;
}
EXPORT_SYMBOL(extenal_api_key_set_version);
/* function :extenal_api_key_write
 * keyname: key data
 * dsize: key data size
 * hexascii_flag: ==0 merge byte2, !=0 no merge bytes
 * */
int extenal_api_key_write(char *keyname,char *keydata,int dsize,int hexascii_flag)
{
	ssize_t ret=0;
	int i,j;
	int count;
	char* buf = NULL;
	//struct device_attribute attr;
	if(strlen(keyname) >= AML_KEY_NAMELEN){
		printk("keyname too lenth,%s:%d\n",__func__,__LINE__);
		return -EINVAL;
	}
	if(keys_version != version_check()){
		printk("don't set version,please set version,%s:%d\n",__func__,__LINE__);
		return -EINVAL;
	}
	if(key_device == NULL){
		printk("sysfs don't init success,%s:%d\n",__func__,__LINE__);
		return -EINVAL;
	}
	
	buf = kmalloc(KEYBUF_MAX_LEN, GFP_KERNEL);
	if(!buf){
		printk("no memory\n");
		return -1;
	}
	memset(buf, 0, KEYBUF_MAX_LEN);
	ret = key_name_store(NULL,NULL,keyname,strlen(keyname));
	if(ret < 0){
		printk("save keyname:%s,fail,%s:%d\n",keyname,__func__,__LINE__);
		kfree(buf);
		return -EINVAL;
	}
	if(hexascii_flag == 0){
		count = dsize;
		for(i=0,j=0;i<count;i++){
			buf[j] = hexToAscII((keydata[i]>>4)&0xf);
			buf[j+1] = hexToAscII(keydata[i]&0xf);
			j+=2;
		}
		count = j;
	}
	else{
		count = dsize;
		strncpy(buf,keydata,count);
	}

	ret = key_write_store(NULL,NULL,buf,count);
	kfree(buf);
	return ret;
}
EXPORT_SYMBOL(extenal_api_key_write);
/* function :extenal_api_key_read
 * keyname: key data
 * hexascii_flag: ==0 merge byte2, !=0 no merge bytes
 * */
int extenal_api_key_read(char *keyname,char *keydata,int dsize,int hexascii_flag)
{
	//aml_key_t * keys;
	//struct device_attribute attr;
	ssize_t ret=0;
	int i,j;
	int count;
	char* buf = NULL;
	if(strlen(keyname) >= AML_KEY_NAMELEN){
		printk("keyname too lenth,%s:%d\n",__func__,__LINE__);
		return -EINVAL;
	}
	if(keys_version != version_check()){
		printk("don't set version,please set version,%s:%d\n",__func__,__LINE__);
		return -EINVAL;
	}
	if(key_device == NULL){
		printk("sysfs don't init success,%s:%d\n",__func__,__LINE__);
		return -EINVAL;
	}

	buf = kmalloc(KEYBUF_MAX_LEN, GFP_KERNEL);
	if(!buf){
		printk("no memory\n");
		return -1;
	}
	memset(buf, 0, KEYBUF_MAX_LEN);
	ret = key_name_store(NULL, NULL, keyname, strlen(keyname));
	if(ret < 0){
		printk("don't found keyname:%s,%s:%d\n",keyname,__func__,__LINE__);
		kfree(buf);
		return -EINVAL;
	}
	ret = key_read_show(NULL, NULL,buf);
	if(ret > 0){
		if(ret > dsize){
			count = dsize;
		}
		else{
			count = ret;
		}
		if(hexascii_flag == 0){
			for(i=0,j=0;i<count;j++){
				keydata[j] = twoASCByteToByte(buf[i],buf[i+1]);
				i += 2;
			}
			ret = j;
		}
		else{
			strncpy(keydata,buf,count);
		}
	}
	kfree(buf);
	buf = NULL;
	return ret;
}
EXPORT_SYMBOL(extenal_api_key_read);

/*
 * function name: extenal_api_key_query
 * keyname: key name
 * keystate:  0: key not exist, 1: key burned , other : reserve
 * return : <0: fail, >=0 ok
 * */
int extenal_api_key_query(char *keyname,unsigned int *keystate)
{
	int i;
	aml_key_t * keys;
	ssize_t ret=0;
	if(strlen(keyname) >= AML_KEY_NAMELEN){
		printk("keyname too lenth,%s:%d\n",__func__,__LINE__);
		return -EINVAL;
	}
	if(keys_version != version_check()){
		printk("don't set version,please set version,%s:%d\n",__func__,__LINE__);
		return -EINVAL;
	}
	if(key_device == NULL){
		printk("sysfs don't init success,%s:%d\n",__func__,__LINE__);
		return -EINVAL;
	}

	*keystate = 0;
	keys = key_schematic[keys_version]->keys;
	for(i=0;i<key_schematic[keys_version]->count;i++)
	{
		if(keys[i].name[0] != 0){
			if(strcmp(keyname,keys[i].name) == 0){
				*keystate = 1;
				break;
			}
		}
	}
	return ret;
}
EXPORT_SYMBOL(extenal_api_key_query);

static int __init aml_keys_init(void)
{
    int ret = -1;

    ret = platform_driver_register(&aml_keys_driver);
    if (ret != 0)
    {
        printk(KERN_ERR "failed to register aml_keys driver, error %d\n", ret);
        return -ENODEV;
    }
    printk(KERN_INFO "platform_driver_register--aml_keys_driver--------------------\n");

    return ret;
}

static void __exit aml_keys_exit(void)
{
    platform_driver_unregister(&aml_keys_driver);
}

module_init(aml_keys_init);
module_exit(aml_keys_exit);

MODULE_DESCRIPTION("Amlogic keys driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jerry Yu<jerry.yu@amlogic.com>");

