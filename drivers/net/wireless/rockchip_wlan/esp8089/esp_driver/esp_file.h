/* Copyright (c) 2008 -2014 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 *   file operation in kernel space
 *
 */

#ifndef _ESP_FILE_H_
#define _ESP_FILE_H_

#include <linux/version.h>
#include <linux/firmware.h>


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,20)
#define GET_INODE_FROM_FILEP(filp) \
    (filp)->f_path.dentry->d_inode
#else
#define GET_INODE_FROM_FILEP(filp) \
    (filp)->f_dentry->d_inode
#endif

#define E_ROUND_UP(x, y)  ((((x) + ((y) - 1)) / (y)) * (y))

int esp_readwrite_file(const char *filename, char *rbuf, const char *wbuf, size_t length);

int esp_request_firmware(const struct firmware **firmware_p, const char *name, struct device *device);

void esp_release_firmware(const struct firmware *firmware);

#ifdef INIT_DATA_CONF
#define INIT_CONF_FILE "init_data.conf"
#endif /* def INIT_DATA_CONF */

#define CONF_ATTR_LEN 24
#define CONF_VAL_LEN 3
#define MAX_ATTR_NUM 24
#define MAX_FIX_ATTR_NUM 16
#define MAX_BUF_LEN ((CONF_ATTR_LEN + CONF_VAL_LEN + 2) * MAX_ATTR_NUM + 2)

struct esp_init_table_elem {
	char attr[CONF_ATTR_LEN];
	int offset;
	short value;
};

int request_init_conf(void);
void fix_init_data(u8 *init_data_buf, int buf_size);


#ifdef ESP_ANDROID_LOGGER
extern int logger_write( const unsigned char prio,
                         const char __kernel * const tag,
                         const char __kernel * const fmt,
                         ...);

#endif

#if (defined(CONFIG_DEBUG_FS) && defined(DEBUGFS_BOOTMODE)) || defined(ESP_CLASS)

#define FCC_MODE_ID_MASK		0x000000ff
#define FCC_MODE_ID_SHIFT		0
#define FM_GET_ID(mode) ((mode&FCC_MODE_ID_MASK)>>FCC_MODE_ID_SHIFT)
/* fcc tx / fcc continue tx */
#define FCC_MODE_CHANNEL_MASK		0x0000ff00
#define FCC_MODE_CHANNEL_SHIFT		8
#define FM_GET_CHANNEL(mode) ((mode&FCC_MODE_CHAN_MASK)>>FCC_MODE_CHANNEL_SHIFT)
#define FCC_MODE_RATE_OFFSET_MASK	0x00ff0000
#define FCC_MODE_RATE_OFFSET_SHIFT	16
#define FM_GET_RATE_OFFSET(mode) ((mode&FCC_MODE_RATE_OFFSET_MASK)>>FCC_MODE_RATE_OFFSET_SHIFT)

typedef enum FCC_MODE_ID {
	FCC_MODE_ILDE = 0,
	FCC_MODE_SELFTEST = 1,
	FCC_MODE_CONT_TX,
	FCC_MODE_NORM_TX,
	FCC_MODE_SDIO_NOISE,
	FCC_MODE_MAX,
} FCC_MODE_ID_t;


/* little ending: 
 * low bit <-- --> high bit *
 * id channel rate reserve  */
struct fcc_mode {
	u8 id;
	union {
		struct {
			u8 reserve[3];
		} fcc_selftest;
		struct {
			u8 channel;
			u8 rate_offset;
			u8 reserve;
		} fcc_norm_tx;
		struct {
			u8 channel;
			u8 rate_offset;
			u8 reserve;
		} fcc_cont_tx;
	} u;
} __packed;
#endif /* FCC */

#ifdef ESP_CLASS
int esp_class_init(void);	
void esp_class_deinit(void);
#endif

#endif /* _ESP_FILE_H_ */
