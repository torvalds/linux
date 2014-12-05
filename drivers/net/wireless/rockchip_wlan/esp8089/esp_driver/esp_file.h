/*
 * Copyright (c) 2010 -2014 Espressif System.
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

#endif /* _ESP_FILE_H_ */
