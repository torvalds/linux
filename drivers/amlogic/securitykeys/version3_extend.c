/*
 * version3_extend.c
 *
 *  Created on: Jul 18, 2012
 *      Author: jerry.yu
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
#include <linux/syscalls.h>
#include "aml_keys.h"
#include <linux/amlogic/securitykey.h>
#include <mach/cpu.h>
#define key_schem_print(a...) printk(a)

static char secure_device[PATH_MAX];

#define KEY_NEW_METHOD

#define KEYS_V3_MAX_COUNT 4
#define Keys_V4_MAX_COUNT  32
#pragma pack(1)

struct v3_key_storage{
    char name[AML_KEY_NAMELEN];
#ifdef KEY_NEW_METHOD
    uint16_t slot;
#endif
    uint16_t type;
    uint16_t valid_size;
    uint16_t storage_size;
#ifndef KEY_NEW_METHOD
    char content[CONFIG_MAX_STORAGE_KEYSIZE];
#endif
#ifdef KEY_NEW_METHOD
	uint16_t state;
	uint16_t checksum;
    char hash[36];
    char  reserve[60];
    char *content;
#endif
};
#ifdef KEY_NEW_METHOD
struct v3_key_storage storage_v4[Keys_V4_MAX_COUNT];
#else
struct v3_key_storage storage[KEYS_V3_MAX_COUNT+1];
#endif

struct key_head_item{
	uint32_t position;
	uint32_t state;
	uint32_t reserve;
};
struct v3_key_storage_head{
	char mark[16];
	uint32_t version;
	uint32_t inver; //inver = ~version + 1
	uint32_t tag;
	uint32_t size; //tatol size
	uint32_t item_cnt;
	struct key_head_item item[Keys_V4_MAX_COUNT];
	char reserve[92];
};
#define KEY_HEAD_MARK	"keyexist"
struct v3_key_storage_head storage_head={
	.mark=KEY_HEAD_MARK,
#if MESON_CPU_TYPE >= MESON_CPU_TYPE_MESON8
	.version = 2,	/* 	m8 version is 2,m8 version is 1
					 *  version 2: key is encrypted with aml_keysafety_encrypt(aes)
					 */
#else
	.version = 1,   /* version 1: key was encrypted with aml_key_encrypt(aes),
					 * version 2: key is encrypted with aml_keysafety_encrypt(aes)
					 * above two aes way is different, 
					 */
#endif
};

#pragma pack()

static aml_keys_schematic_t version3;

/**
 * struct aml_key_s{
    struct device_attribute  attr;///@todo it is written by core program
    char     name[16];
    char    * description;
    uint16_t type;
    uint16_t valid_size;///output size
    uint16_t storage_size;///the size in media
    uint16_t st;

    int32_t (* update_status)(aml_key_t * key);
    int32_t (* write)(aml_key_t * key,uint8_t *data);
    int32_t (* read)(aml_key_t * key,uint8_t *data);
};
typedef struct {
    int32_t (*read)(uint32_t id, char * buf);
    int32_t (*write)(uint32_t id, char * buf);
    int32_t (*installed)(uint32_t id,char * buf);
}fake_efuse_hash_t;
*/
/**
typedef struct aml_keys_schematic_s aml_keys_schematic_t;
struct aml_keys_schematic_s{
    char * name;
    char * description;
    uint32_t count;
    int32_t (* init)(aml_keys_schematic_t * ,char * secure_dev);
    int32_t (* install)(aml_keys_schematic_t * ,aml_install_key_t *);
    int32_t (* flush)(aml_keys_schematic_t *);
    fake_efuse_hash_t  hash;
    aml_key_t * keys;
};
 */
static int32_t v3_key_update_status(aml_key_t * key)
{
    /**
     * @todo implement it later
     */
    return -EINVAL;
}
#ifdef KEY_NEW_METHOD
static int32_t v3_key_write(aml_key_t * key, uint8_t *data)
{
    //int32_t key_slot=AML_KEY_GETSLOT(key);
    int i;
    struct v3_key_storage *key_storage=NULL;
	//key_schem_print("#v3_key_write id=%d !!!\n",key_slot);
	if(version3.state!=KEYDATATRUE)
	{
		version3.state=KEYTOLOAD;
		version3.init(&version3,secure_device);
	}
	for(i=0;i<Keys_V4_MAX_COUNT;i++)
	{
		if(strcmp(key->name,storage_v4[i].name) == 0){
			key_storage = &storage_v4[i];
			break;
		}
	}
	if(key_storage == NULL){
		for(i=0;i<Keys_V4_MAX_COUNT;i++)
		{
			if(storage_v4[i].name[0] == 0)
			{
				key_storage = &storage_v4[i];
				break;
			}
		}
		if(key_storage == NULL){
			printk("key count too much,%s:%d\n",__func__,__LINE__);
			return -EINVAL;
		}

		if(key_storage->content == NULL){
			key_storage->content = kzalloc(key->storage_size, GFP_KERNEL);
			if (key_storage->content == NULL){
				printk("kzalloc mem fail,%s:%d\n",__func__,__LINE__);
				return -EINVAL;
			}
		}
	}
	else{
		if(key_storage->content == NULL){
			key_storage->content = kzalloc(key->storage_size, GFP_KERNEL);
			if (key_storage->content == NULL){
				printk("kzalloc mem fail,%s:%d\n",__func__,__LINE__);
				return -EINVAL;
			}
		}
		else if(key->storage_size > key_storage->storage_size)
		{
			kfree(key_storage->content);
			key_storage->content = kzalloc(key->storage_size, GFP_KERNEL);
			if (key_storage->content == NULL){
				printk("kzalloc mem fail,%s:%d\n",__func__,__LINE__);
				return -EINVAL;
			}
		}
	}
    strcpy(key_storage->name,key->name);
    key_storage->type=key->type;
    key_storage->storage_size=key->storage_size;
    key_storage->valid_size=key->valid_size;
    key_storage->checksum = key->checksum;
    key_schem_print("%s:%d %x %x",__func__,__LINE__,key_storage->type,key->type);
    memcpy(key_storage->content,data,key_storage->storage_size);
    key->st|=AML_KEY_ST_DIRTY;

    //printk("key_storage->name:%s,key_storage->valid_size:%d,key_storage->storage_size:%d,%s:%d\n",
	//	key_storage->name,key_storage->valid_size,key_storage->storage_size,__func__,__LINE__);
	//printk("key->name:%s,key->valid_size:%d,key->storage_size:%d,%s:%d\n",
	//	key->name,key->valid_size,key->storage_size,__func__,__LINE__);

    return 0;
}
static int32_t v3_key_read(aml_key_t * key, uint8_t *data)
{
    //int32_t key_slot = AML_KEY_GETSLOT(key);
    struct v3_key_storage *key_storage=NULL;
    int i;
	//key_schem_print("#key_read id=%d !!!\n",key_slot);
	if(version3.state!=KEYDATATRUE)
	{
		version3.state=KEYTOLOAD;
		version3.init(&version3,secure_device);
	}
	for(i=0;i<Keys_V4_MAX_COUNT;i++)
	{
		if(strcmp(key->name,storage_v4[i].name) == 0){
			key_storage = &storage_v4[i];
			break;
		}
	}
	if(key_storage == NULL){
		printk("invalid key name,%s:%d\n",__func__,__LINE__);
		return -EINVAL;
	}
    key->valid_size = key_storage->valid_size;
    key->storage_size = key_storage->storage_size;
    key->type = key_storage->type;
    key->checksum = key_storage->checksum;
    memcpy(data, key_storage->content, key_storage->storage_size);

    //printk("key_storage->name:%s,key_storage->valid_size:%d,key_storage->storage_size:%d,%s:%d\n",
	//	key_storage->name,key_storage->valid_size,key_storage->storage_size,__func__,__LINE__);
	//printk("key->name:%s,key->valid_size:%d,key->storage_size:%d,%s:%d\n",
	//	key->name,key->valid_size,key->storage_size,__func__,__LINE__);

	return 0;
#if 0
    if (strcmp(storage[key_slot].name, key->name)
            || storage[key_slot].type != key->type
//            || storage[key_slot].size != key->storage_size)
			)
	{
        key_schem_print("%s:%d ",__func__,__LINE__);
        key_schem_print("\n%d %x %d %s \n ",key_slot,storage[key_slot].type,storage[key_slot].storage_size,storage[key_slot].name);
        key_schem_print("%d %x %d %s \n ",key_slot,key->type,key->storage_size,key->name);
        return -EINVAL;
    }
    if((storage[key_slot].valid_size > storage[key_slot].storage_size)
		|| (storage[key_slot].storage_size > CONFIG_MAX_STORAGE_KEYSIZE))
		{
			return -EINVAL;
		}
    key->valid_size = storage[key_slot].valid_size;
    key->storage_size = storage[key_slot].storage_size;
    memcpy(data, storage[key_slot].content, storage[key_slot].storage_size);
    return 0;
#endif
}
static  aml_key_t version4_keys[Keys_V4_MAX_COUNT];
#else
static int32_t v3_key_write(aml_key_t * key, uint8_t *data)
{
    int32_t key_slot=AML_KEY_GETSLOT(key);
	key_schem_print("#v3_key_write id=%d !!!\n",key_slot);
	if(version3.state!=KEYDATATRUE)
	{
		version3.state=KEYTOLOAD;
		version3.init(&version3,secure_device);
	}
		
    strcpy(storage[key_slot].name,key->name);
    storage[key_slot].type=key->type;
    storage[key_slot].storage_size=key->storage_size;
    storage[key_slot].valid_size=key->valid_size;
    key_schem_print("%s:%d %x %x",__func__,__LINE__,storage[key_slot].type,key->type);
    memcpy(storage[key_slot].content,data,storage[key_slot].storage_size);
    key->st|=AML_KEY_ST_DIRTY;
    return 0;
}
static int32_t v3_key_read(aml_key_t * key, uint8_t *data)
{
    int32_t key_slot = AML_KEY_GETSLOT(key);
	key_schem_print("#key_read id=%d !!!\n",key_slot);
	if(version3.state!=KEYDATATRUE)
	{
		version3.state=KEYTOLOAD;
		version3.init(&version3,secure_device);
	}		

    if (strcmp(storage[key_slot].name, key->name)
            || storage[key_slot].type != key->type
//            || storage[key_slot].size != key->storage_size)
			)
	{
        key_schem_print("%s:%d ",__func__,__LINE__);
        key_schem_print("\n%d %x %d %s \n ",key_slot,storage[key_slot].type,storage[key_slot].storage_size,storage[key_slot].name);
        key_schem_print("%d %x %d %s \n ",key_slot,key->type,key->storage_size,key->name);
        return -EINVAL;
    }
    if((storage[key_slot].valid_size > storage[key_slot].storage_size)
		|| (storage[key_slot].storage_size > CONFIG_MAX_STORAGE_KEYSIZE))
		{
			return -EINVAL;
		}
    key->valid_size = storage[key_slot].valid_size;
    key->storage_size = storage[key_slot].storage_size;
    memcpy(data, storage[key_slot].content, storage[key_slot].storage_size);
    return 0;
}

static  aml_key_t version3_keys[KEYS_V3_MAX_COUNT]={
    [0]={
        .name="hdcp",
        .description="keys for HDMI",
        .type=AML_KEY_ENC|AML_KEY_OTP_HASH_SLOT(0),
        .valid_size=310,
        .storage_size=320,
    },
    [1]={
        .name="windewine",
        .description="keys for WindeVine",
        .type=AML_KEY_ENC|AML_KEY_OTP_HASH_SLOT(1),
        .valid_size=128,
        .storage_size=144,
    },
    [2]={
        .name="key2",
        .description="key2 for key2", //key2 for reserved
        .type=AML_KEY_ENC|AML_KEY_OTP_HASH_SLOT(2),
    },
    [3]={
        .name="key3",
        .description="key3 for key3", //key3 for reserved
        .type=AML_KEY_ENC|AML_KEY_OTP_HASH_SLOT(3),
    },
};
#endif //KEY_NEW_METHOD

#ifdef KEY_NEW_METHOD
static int32_t hash_read(aml_key_t * key,uint32_t id, char * buf)
{
	struct v3_key_storage *key_storage=NULL;
	int i;
	if(version3.state!=KEYDATATRUE)
	{
		version3.state=KEYTOLOAD;
		version3.init(&version3,secure_device);
	}
	for(i=0;i<Keys_V4_MAX_COUNT;i++)
	{
		if(strcmp(key->name,storage_v4[i].name) == 0){
			key_storage = &storage_v4[i];
			break;
		}
	}
	if(key_storage == NULL){
		printk("don't have valid key name,%s:%d\n",__func__,__LINE__);
		return -EINVAL;
	}

	memcpy(buf,key_storage->hash,34);
	return 0;
}
static int32_t hash_write(aml_key_t * key,uint32_t id, char * buf)
{
	struct v3_key_storage *key_storage=NULL;
	int i;

	if(version3.state!=KEYDATATRUE)
	{
		version3.state=KEYTOLOAD;
		version3.init(&version3,secure_device);
	}
	for(i=0;i<Keys_V4_MAX_COUNT;i++){
		if(strcmp(key->name,storage_v4[i].name) == 0){
			key_storage = &storage_v4[i];
			break;
		}
	}
	if(key_storage == NULL){
		printk("don't have valid key name,%s:%d\n",__func__,__LINE__);
		return -EINVAL;
	}
	memcpy(key_storage->hash, buf, 34);
	return 0;
}
static int32_t key_installed(aml_key_t * key,uint32_t id, char * buf)
{
	return -EINVAL;
}
#else
static int32_t hash_read(aml_key_t * key,uint32_t id, char * buf)
{
    char * hash_buf=(char*)&storage[KEYS_V3_MAX_COUNT];
	key_schem_print("#hash_read id=%d !!!\n",id);
	if(version3.state!=KEYDATATRUE)
	{
		version3.state=KEYTOLOAD;
		version3.init(&version3,secure_device);
	}	

    memcpy(buf,&hash_buf[id*36],34);
    return 0;
}
static int32_t hash_write(aml_key_t * key,uint32_t id, char * buf)
{
    char * hash_buf = (char*) &storage[KEYS_V3_MAX_COUNT];
	key_schem_print("#hash_write id=%d !!!\n",id);
 
	if(version3.state!=KEYDATATRUE)
	{
		version3.state=KEYTOLOAD;
		version3.init(&version3,secure_device);
	}	
	
    memcpy(&hash_buf[id * 36], buf, 34);
    return 0;
}
static int32_t key_installed(aml_key_t * key,uint32_t id, char * buf)
{
    return -EINVAL;
}
#endif //KEY_NEW_METHOD

#ifdef KEY_NEW_METHOD
static int32_t key_item_clean(void)
{
	int i;
	struct v3_key_storage *tmp_node;
	tmp_node = &storage_v4[0];
	for(i=0;i<Keys_V4_MAX_COUNT;i++){
		if(tmp_node->content){
			kfree(tmp_node->content);
			tmp_node->content = NULL;
		}
		memset(tmp_node,0,sizeof(*tmp_node));
		tmp_node++;
	}
#if 0
	tmp_node = &storage_v4[0];
	for(i=0;i<Keys_V4_MAX_COUNT;i++){
		printk("name:%s \n",tmp_node->name);
		tmp_node++;
	}
#endif
	return 0;
}

static int32_t key_item_parse(struct v3_key_storage_head *head)
{
	//struct v3_key_storage *tmp_content_storage,*valid_node,*tmp_node,*pre_node,*free_node;
	struct v3_key_storage *tmp_content_storage,*tmp_node;
	char *temp_content,*item_content;
	aml_key_t *aml_key;
	int i,item_cnt;
	int err=0;
	struct v3_key_storage_head *key_head;
	if(strcmp(head->mark,KEY_HEAD_MARK) != 0){
		printk("don't wrote key,%s:%d\n",__func__,__LINE__);
		return -EINVAL;
	}
	temp_content = (char*)&head[1];
	key_head = &storage_head;
	tmp_node = &storage_v4[0];
	aml_key = version4_keys;
	item_cnt = head->item_cnt;
	//printk("item_cnt:%d,%s:%d\n",item_cnt,__func__,__LINE__);
	for(i=0;i<item_cnt;i++){
		tmp_content_storage = (struct v3_key_storage *)&temp_content[head->item[i].position];
		if(tmp_node->content){
			kfree(tmp_node->content);
			tmp_node->content = NULL;
			//printk("%s:%d,tmp_node->content is exist\n",__func__,__LINE__);
		}
		if(tmp_node->content == NULL){
			item_content = kzalloc(tmp_content_storage->storage_size, GFP_KERNEL);
			if(item_content == NULL)
				return -ENOMEM;
			tmp_node->content = item_content;
		}
		tmp_node->slot = tmp_content_storage->slot;
		tmp_node->state = tmp_content_storage->state;
		tmp_node->storage_size = tmp_content_storage->storage_size;
		tmp_node->valid_size = tmp_content_storage->valid_size;
		tmp_node->type = tmp_content_storage->type;
		tmp_node->checksum = tmp_content_storage->checksum;
		strcpy(tmp_node->name,tmp_content_storage->name);
		strcpy(aml_key->name,tmp_content_storage->name);
		memcpy(tmp_node->hash,tmp_content_storage->hash,sizeof(tmp_node->hash));
		memcpy(tmp_node->content,&temp_content[head->item[i].position + sizeof(struct v3_key_storage)],tmp_node->storage_size);

		//memcpy(tmp_node,temp_content[head->item[i].position],sizeof(struct v3_key_storage));
		tmp_node++;aml_key++;
	}
	key_head->version = head->version;
	key_head->inver = head->inver;
	key_head->tag = head->tag;
	key_head->item_cnt = head->item_cnt;
	key_head->size = head->size;
	return err;
}

static int32_t key_item_merge(char **mem,uint32_t *size)
{
	struct v3_key_storage *tmp_node;
	struct v3_key_storage_head *key_head,*sv_head;
	char *room,*temp_content;
	uint32_t tatol_size=0,position;
	int i,item_cnt;
	tatol_size += sizeof(struct v3_key_storage_head);
	tmp_node = &storage_v4[0];
	item_cnt = 0;
	for(i=0;i<Keys_V4_MAX_COUNT;i++,tmp_node++){
		if(tmp_node->name[0] == 0){
			//item_cnt++;
			break;
		}
		else{
			tatol_size += sizeof(struct v3_key_storage);
			tatol_size += tmp_node->storage_size;
		}
	}
	item_cnt = i;
	room = kzalloc(tatol_size, GFP_KERNEL);
	if(room == NULL){
		printk("kzalloc mem fail,%s\n",__func__);
		return -ENOMEM;
	}
	sv_head = (struct v3_key_storage_head *)&room[0];
	temp_content = (char*)&sv_head[1];
	position = 0;
	tmp_node = &storage_v4[0];
	key_head = &storage_head;
	for(i=0;i<item_cnt;i++){
		key_head->item[i].position = position;
		memcpy(&temp_content[position],tmp_node,sizeof(struct v3_key_storage));
		position += sizeof(struct v3_key_storage);
		memcpy(&temp_content[position],tmp_node->content,tmp_node->storage_size);
		position += tmp_node->storage_size;
		tmp_node++;
	}
	key_head->item_cnt = item_cnt;
	key_head->size = tatol_size;
	memcpy(sv_head,key_head,sizeof(struct v3_key_storage_head));
	//sv_head->size = key_head->size;
	//sv_head->item_cnt = key_head->item_cnt;
	//sv_head->tag = key_head->tag;
	//sv_head->version = key_head->version;
	//sv_head->inver = key_head->inver;
	//strcpy(sv_head->mark,key_head->mark);
	*mem = room;
	*size = tatol_size;
	return 0;
}
#endif //KEY_NEW_METHOD


static int32_t version3_init(aml_keys_schematic_t * schematic, char * secure_dev)
{
	aml_keybox_provider_t * prov= aml_keybox_provider_get(secure_dev);
    int i;
    struct v3_key_storage_head *head;
    uint32_t headsize;
    if (IS_ERR_OR_NULL(prov))
    {
    	printk("device name=%s open error \n",secure_dev);
        return -EINVAL;
    }
#if 0    
    else if(schematic->state==KEYDATANOTTRUE){
        strcpy(secure_device,secure_dev);
		for(i=0;i<schematic->count;i++)
		{
			schematic->keys[i].read=v3_key_read;
			schematic->keys[i].write=v3_key_write;
			schematic->keys[i].update_status=v3_key_update_status;		
		}
    	return 0;
    }
#endif    
    else
    {
#ifdef KEY_NEW_METHOD
		version3.state=KEYDATATRUE;
		strcpy(secure_device,secure_dev);
		head = kzalloc(1024, GFP_KERNEL);
		if(head == NULL){
			printk("kzalloc mem fail,%s\n",__func__);
			return -ENOMEM;
		}
		
		if(prov->read){
			prov->read(prov,(uint8_t *)head,1024,0);
		}
		if(strcmp(head->mark,KEY_HEAD_MARK) == 0){
			if(head->size > 1024){
				headsize = head->size;
				kfree(head);
				head = kzalloc(headsize, GFP_KERNEL);
				if(head == NULL){
					printk("kzalloc mem fail,%s\n",__func__);
					return -ENOMEM;
				}
				if(prov->read){
					prov->read(prov,(uint8_t *)head,headsize,0);
				}
			}
			key_item_parse(head);
		}
		kfree(head);
#else
        key_schem_print("#load from media!!!\n");
        key_schem_print("device name=%s\n",secure_dev);
		memset(&storage,0,sizeof(storage));
        prov->read(prov,&storage,sizeof(storage));
		version3.state=KEYDATATRUE;
        strcpy(secure_device,secure_dev);
        for(i=0;i<KEYS_V3_MAX_COUNT;i++)
            key_schem_print("%s:%d,%s:\n",__func__,__LINE__,storage[i].name);
#endif
    }
#ifdef KEY_NEW_METHOD
	for(i=0;i<schematic->count;i++)
	{
		schematic->keys[i].type |= (AML_KEY_PROTO | AML_KEY_ENC | AML_KEY_OTP_HASH|AML_KEY_OTP);
		schematic->keys[i].read=v3_key_read;
		schematic->keys[i].write=v3_key_write;
		schematic->keys[i].update_status=v3_key_update_status;
	}
#endif
#ifndef KEY_NEW_METHOD
    for(i=0;i<schematic->count;i++)
    {
        if (schematic->keys[i].name[0])
        {
            key_schem_print("%s:%d,%s:\t",__func__,__LINE__,schematic->keys[i].name);
            schematic->keys[i].type |= AML_KEY_PROTO;
            if (strcmp(schematic->keys[i].name, storage[i].name) == 0
                    && schematic->keys[i].type == storage[i].type
            //        && schematic->keys[i].storage_size == storage[i].size)
					)
            {
                schematic->keys[i].st |= AML_KEY_ST_INSTALLED;
                ///@todo last modify jerry.yu

            }
            schematic->keys[i].read=v3_key_read;
            schematic->keys[i].write=v3_key_write;
            schematic->keys[i].update_status=v3_key_update_status;
            key_schem_print("valid_size=%d,storage_size=%d %s\n", schematic->keys[i].valid_size,
                            schematic->keys[i].storage_size, schematic->keys[i].st&AML_KEY_ST_INSTALLED?"installed":"notinstalled");

        } else
        {
            schematic->keys[i].st|=AML_KEY_ST_INVAL;
            ///@todo unused slot could not be access now

        }
    }
#endif
	if(register_aes_algorithm(storage_head.version)<0){
		printk("%s:%d, storage_head.version:%d register key encrypt algorithm fail\n",__func__,__LINE__,storage_head.version);
		return -EINVAL;
	}
    return 0;
}
static int32_t version3_inst(aml_keys_schematic_t * schematic, aml_install_key_t * key)
{

    ///int32_t key_slot=(key->type>>5)&0x7;

    /**
     * @todo this function is used to define a new key .
     */
    return -EINVAL;
}
static int32_t version3_flush(aml_keys_schematic_t * schematic)
{
	aml_keybox_provider_t * prov = aml_keybox_provider_get(secure_device);
	//int i, j;
	int i, err=0;
	uint16_t st=0;
	uint32_t size=0;
	char *room=NULL;
	if (IS_ERR_OR_NULL(prov)) {
		key_schem_print("#put to media!!!\n");
		key_schem_print("device name=%s open error \n", secure_device);
		return -EINVAL;

	} else 	if(version3.state!=KEYDATATRUE){
 		return 0;
	}else{
		for(i=0;i<schematic->count;i++)
		 {
			 //printk("%s:%d,keys[%d].st=0x%x\n",__func__,__LINE__,i,schematic->keys[i].st);
			 if(schematic->keys[i].st & AML_KEY_ST_DIRTY)
			 {
				 st |= AML_KEY_ST_DIRTY;
				 schematic->keys[i].st &= ~AML_KEY_ST_DIRTY;
				 //printk("%s:%d,keys[%d].st=0x%x\n",__func__,__LINE__,i,schematic->keys[i].st);
			 }
		 }
		 if(st & AML_KEY_ST_DIRTY)
		 {
			#ifndef KEY_NEW_METHOD
			prov->write(prov, (uint8_t *)&storage,sizeof(storage));
			#endif
			#ifdef KEY_NEW_METHOD
			//int32_t key_item_merge(char **mem,uint32_t *size);
			err = key_item_merge(&room,&size);
			if(err == 0){
				printk("write key code %s:%d,prov->write:%pf\n",__func__,__LINE__,prov->write);\
				prov->write(prov, (uint8_t *)room,(int)size);
				kfree(room);
			}
			return err;
			#endif
		 }
		 return 0;
	}

	return -EINVAL;
}
static int32_t version3_read(aml_keys_schematic_t * schematic)
{
	aml_keybox_provider_t * prov = aml_keybox_provider_get(secure_device);

	if (IS_ERR_OR_NULL(prov)) {
		key_schem_print("device name=%s open error \n", secure_device);
		return -EINVAL;

	} else {
#ifndef KEY_NEW_METHOD
		printk("read key code %s:%d,prov->read:%pf\n",__func__,__LINE__,prov->read);
		prov->read(prov, (uint8_t *)&storage,sizeof(storage),0);
		version3.state=KEYDATATRUE;
		return 0;
#endif
#ifdef KEY_NEW_METHOD
		struct v3_key_storage_head *head;
		uint32_t headsize;
		head = kzalloc(1024, GFP_KERNEL);
		if(head == NULL){
			printk("kzalloc mem fail,%s\n",__func__);
			return -ENOMEM;
		}
		if(prov->read){
			prov->read(prov,(uint8_t *)head,1024,0);
		}
		key_item_clean();
		if(strcmp(head->mark,KEY_HEAD_MARK) == 0){
			if(head->size > 1024){
				headsize = head->size;
				kfree(head);
				head = kzalloc(headsize, GFP_KERNEL);
				if(head == NULL){
					printk("kzalloc mem fail,%s\n",__func__);
					return -ENOMEM;
				}
				if(prov->read){
					prov->read(prov,(uint8_t *)head,headsize,0);
				}
			}
			key_item_parse(head);
		}
		kfree(head);

		version3.state=KEYDATATRUE;
		return 0;
#endif
	}
	return -EINVAL;
}

static int32_t version3_dump(aml_keys_schematic_t * schematic)
{
	aml_keybox_provider_t * prov = aml_keybox_provider_get(secure_device);

	if (IS_ERR_OR_NULL(prov)) {
		key_schem_print("device name=%s open error \n", secure_device);
		return -EINVAL;

	} else {
#if 0
		int __i;
		char *p=(char *)storage;
		for(__i=0;__i<(sizeof(struct v3_key_storage)+36)*KEYS_V3_MAX_COUNT;__i++){
			if(!(__i%16))
				printk("%08x\t",__i);

			printk("%02x ",p[__i]);

			if(!((__i+1)%16))
				printk("\n");
			if(!((__i+1)%sizeof(struct v3_key_storage)))
				printk("\n");
		}
#endif
	}
	return 0;
}


static aml_keys_schematic_t version3 =
{
  .name = "version3", 
  .description = " First test version",
#ifdef KEY_NEW_METHOD
  .count=Keys_V4_MAX_COUNT,
  .keys=version4_keys,
#else
  .count=KEYS_V3_MAX_COUNT,
  .keys=version3_keys,
#endif
  .hash={
      .read=hash_read,
      .write=hash_write,
      .installed=key_installed,
  },
  .init=version3_init,
  .install=version3_inst,
  .flush=version3_flush,
  .read=version3_read,
  .dump =version3_dump
};

static int __init v3_keys_init(void)
{
    int err=0;
    err = aml_keys_register(3,&version3);
    return err;
}

static void __exit v3_keys_exit(void)
{
    ///platform_driver_unregister(&efuse_driver);
    key_schem_print("mod v3_key exit\n");
}

arch_initcall(v3_keys_init);
module_exit(v3_keys_exit);
