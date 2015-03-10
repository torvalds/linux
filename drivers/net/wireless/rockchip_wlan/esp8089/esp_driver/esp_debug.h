/* Copyright (c) 2008 -2014 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * esp debug
 */

#ifndef _DEBUG_H_

#ifdef ASSERT_PANIC
#define ESSERT(v) BUG_ON(!(v))
#else
#define ESSERT(v) if(!(v)) printk("ESSERT:%s %d\n", __FILE__, __LINE__)
#endif 


#include <linux/slab.h>
#include <linux/debugfs.h>
#include <asm/uaccess.h>

#if defined(CONFIG_DEBUG_FS) && defined(DEBUGFS_BOOTMODE)

#define NAME_LEN_MAX 12

typedef enum DBGFS_BOOTMODE_ID {
	DBGFS_FCC_MODE = 0,
	DBGFS_ATE_MODE,
	DBGFS_NOR_MODE,
	DBGFS_MODE_MAX,
}DBGFS_BOOTMODE_ID_t;

struct dbgfs_bootmode_item {
	char name[NAME_LEN_MAX];
	DBGFS_BOOTMODE_ID_t id;
	u32 var;
	void *priv_data;
	int (*r_cb)(void *data);
	int (*w_cb)(void *data);
};

u32 dbgfs_get_bootmode_var(DBGFS_BOOTMODE_ID_t id);
struct dbgfs_bootmode_item *dbgfs_get_bootmode_di(DBGFS_BOOTMODE_ID_t id);
void dbgfs_bootmode_init(void);
void dbgfs_fcctest_set(u8 *buf, int size);
#endif


typedef enum esp_type {
        ESP_BOOL,
        ESP_U8,
        ESP_U16,
        ESP_U32,
        ESP_U64
} esp_type;


struct dentry *esp_dump_var(const char *name, struct dentry *parent, void *value, esp_type type);

struct dentry *esp_dump_array(const char *name, struct dentry *parent, struct debugfs_blob_wrapper *blob);

struct dentry *esp_dump(const char *name, struct dentry *parent, void *data, int size, struct file_operations *fops);

struct dentry *esp_debugfs_add_sub_dir(const char *name);

int esp_debugfs_init(void);

void esp_debugfs_exit(void);

enum {
        ESP_DBG_ERROR = BIT(0),
        ESP_DBG_TRACE = BIT(1),
        ESP_DBG_LOG = BIT(2),
        ESP_DBG = BIT(3),
        ESP_SHOW = BIT(4),
        ESP_DBG_TXAMPDU = BIT(5),
        ESP_DBG_OP = BIT(6),
	ESP_DBG_PS = BIT(7),
	ESP_ATE = BIT(8),
        ESP_DBG_ALL = 0xffffffff
};

extern unsigned int esp_msg_level;

#ifdef ESP_ANDROID_LOGGER
extern bool log_off;
#endif /* ESP_ANDROID_LOGGER */

#ifdef ESP_ANDROID_LOGGER
#include "esp_file.h"
#define esp_dbg(mask, fmt, args...) do {                  \
        if (esp_msg_level & mask)   			  \
	{						  \
		if (log_off)      		          \
			printk(fmt, ##args);              \
		else 					              \
            		logger_write(4, "esp_wifi", fmt, ##args);     \
	}							      \
    } while (0)
#else
#define esp_dbg(mask, fmt, args...) do {                  \
        if (esp_msg_level & mask)                         \
            printk(fmt, ##args);                          \
    } while (0)
#endif /* ESP_ANDROID_LOGGER */

void show_buf(u8 *buf, u32 len);

#ifdef HOST_RC
struct sip_rc_status;
struct ieee80211_tx_rate;

void esp_show_rcstatus(struct sip_rc_status *rcstatus);

void esp_show_tx_rates(struct ieee80211_tx_rate *rates);
#endif /* HOST_RC */

#endif /* _DEBUG_H_ */
