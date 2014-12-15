/*
 * aml_keys.h
 *
 *  Created on: Jul 17, 2012
 *      Author: jerry.yu
 */

#ifndef AML_KEYS_H_
#define AML_KEYS_H_
#include <linux/amlogic/efuse.h>
/***
 * Inner exchanged
 */
#define CONFIG_MAX_STORAGE_KEYSIZE 4096 //4k
#define CONFIG_MAX_VALID_KEYSIZE    (CONFIG_MAX_STORAGE_KEYSIZE)
#define KEYBUF_MAX_LEN	(CONFIG_MAX_STORAGE_KEYSIZE*2)
typedef struct aml_key_s aml_key_t;
#define AML_KEY_NAMELEN  16
struct aml_key_s{
    struct device_attribute  attr;///@todo it is written by core program
    char     name[AML_KEY_NAMELEN];
    char    * description;
    uint16_t type;
    uint16_t valid_size;///output size
    uint16_t storage_size;///the size in media
    uint16_t st;
    uint16_t checksum;

    int32_t (* update_status)(aml_key_t * key);
    int32_t (* write)(aml_key_t * key,uint8_t *data);
    int32_t (* read)(aml_key_t * key,uint8_t *data);
};
typedef struct {
    int32_t (*read)(aml_key_t * key,uint32_t id, char * buf);
    int32_t (*write)(aml_key_t * key,uint32_t id, char * buf);
    int32_t (*installed)(aml_key_t * key,uint32_t id,char * buf);
}fake_efuse_hash_t;
extern fake_efuse_hash_t fake_efuse;
typedef struct {///exported structure
    char name[AML_KEY_NAMELEN];
    uint16_t size;
    uint16_t type;
    char key[1024];
}aml_install_key_t;
typedef struct aml_keys_schematic_s aml_keys_schematic_t;
struct aml_keys_schematic_s{
    char * name;
    char * description;
    uint32_t count;
	int    state;
    int32_t (* init)(aml_keys_schematic_t * ,char * secure_dev);
    int32_t (* install)(aml_keys_schematic_t * ,aml_install_key_t *);
	int32_t (* read)(aml_keys_schematic_t * schematic);
    int32_t (* flush)(aml_keys_schematic_t *);
    int32_t (* dump)(aml_keys_schematic_t *);
    fake_efuse_hash_t  hash;
    aml_key_t * keys;
};
#define  KEYDATANOTTRUE 0
#define  KEYTOLOAD 1
#define  KEYDATATRUE 2

int32_t aml_keys_register(int32_t version,aml_keys_schematic_t * schematic);
int register_aes_algorithm(int storage_version);


#define AML_KEY_READ        (1<<0)
#define AML_KEY_WRITE       (1<<1)
#define AML_KEY_OTP         (1<<2)
#define AML_KEY_OTP_HASH    (1<<4)
#define AML_KEY_ENC         (1<<3)
#define AML_KEY_OTP_HASH_MASK   (0x1f<<5)
#define AML_KEY_OTP_HASH_SLOT(slot) (((slot<<5)&(AML_KEY_OTP_HASH_MASK))|AML_KEY_OTP_HASH|AML_KEY_OTP)
#define AML_KEY_PROTO       (1<<10)
#define AML_KEY_GETSLOT(aml_key)   ((aml_key->type&AML_KEY_OTP_HASH_MASK)>>5)

/**
 * status
 */
#define AML_KEY_ST_INSTALLED    (1<<0)
#define AML_KEY_ST_INVAL        (1<<1)
#define AML_KEY_ST_DIRTY        (1<<2)
#define AML_KEY_ST_EFUSE_DIRTY  (1<<3)
#define key_is_readable(key) ((key).type&AML_KEY_READ)
#define key_is_otp_key(key)  ((key).type&AML_KEY_OTP)
#define key_is_control_node(key) (0) ///@todo not implement yet
#define key_is_not_installed(key)  (!((key).st&AML_KEY_ST_INSTALLED))
#define key_slot_is_inval(key)  ((key).st&AML_KEY_ST_INVAL) ///@todo not implement yet
#define key_is_efuse_dirty(key)  ((key).st&AML_KEY_ST_EFUSE_DIRTY) ///@todo not implement yet
///#define aml_key_decrypt(buf)    (0) ///@todo not implement yet
///#define aml_key_encrypt(buf)    (0) ///@todo not implement yet
struct aml_key_hash_s{
  uint16_t size;
  uint8_t  hash[32];
};


int32_t aml_key_hash_verify(struct aml_key_hash_s * hash,const char * data);
#endif /* AML_KEYS_H_ */
