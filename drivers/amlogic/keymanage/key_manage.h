#ifndef __KEY_MANAGE_H__
#define __KEY_MANAGE_H__

#include <linux/cdev.h>
#include <linux/ioctl.h>

#define KEYUNIFY_INIT_INFO				_IO('f', 0x60)
#define KEYUNIFY_ITEM_GEG				_IO('f', 0x61)

enum key_manager_dev_e{
       KEY_M_UNKNOW_DEV=0,
       KEY_M_EFUSE_NORMAL,
       KEY_M_SECURE_STORAGE,                     /*secure storage key*/
       KEY_M_GENERAL_NANDKEY,  /*include general key(nand key,emmc key)*/
       KEY_M_MAX_DEV,
};

/*key data format*/
enum key_manager_df_e{
       KEY_M_HEXDATA=0,
       KEY_M_HEXASCII,
       KEY_M_ALLASCII,
       KEY_M_MAX_DF,
};
enum key_manager_permit_e{
       KEY_M_PERMIT_READ = (1<<0),
       KEY_M_PERMIT_WRITE = (1<<1),
       KEY_M_PERMIT_DEL    = (1<<2),
       KEY_M_PERMIT_MASK   = 0Xf,
};

#if 0
struct key_manager_t{
       char *name;
       union{
               unsigned int keydevice;
               struct keydevice_t{
                       unsigned int dev:8;
                       unsigned int df:8;
                       unsigned int permit:4;
                       unsigned int flag:8;
                       unsigned int other:4;
               }devcfg;
       }k;
       unsigned int reserve;
};
#else
#define KEY_UNIFY_NAME_LEN	48
struct key_item_t{
	char name[KEY_UNIFY_NAME_LEN];
	int id;
	unsigned int dev; //key save in device //efuse,
	unsigned int df;
	unsigned int permit;
	int flag;
	int reserve;
	struct key_item_t *next;
};

struct key_info_t{
	int key_num;
	int efuse_version;
	int key_flag;
};

#endif


typedef struct 
{
    struct cdev cdev;
    unsigned int flags;
} unifykey_dev_t;

extern int unifykey_dt_create(struct platform_device *pdev);
extern int unifykey_dt_release(struct platform_device *pdev);
extern struct key_item_t *unifykey_find_item_by_name(char *name);
extern struct key_item_t *unifykey_find_item_by_id(int id);
extern int unifykey_item_verify_check(struct key_item_t *key_item);
extern int unifykey_count_key(void);
extern char unifykey_get_efuse_version(void);

#endif

