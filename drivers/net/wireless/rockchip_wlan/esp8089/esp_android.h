#ifndef _ESP_ANDROID_H
#define _ESP_ANDROID_H

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

#ifdef ANDROID
int android_readwrite_file(const char *filename, char *rbuf, const char *wbuf, size_t length);

int android_request_firmware(const struct firmware **firmware_p, const char *name, struct device *device);

void android_release_firmware(const struct firmware *firmware);

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

int android_request_init_conf(void);
void fix_init_data(u8 *init_data_buf, int buf_size);
void show_init_buf(u8 *buf, int size);


#endif

#if defined(ANDROID) && defined(ESP_ANDROID_LOGGER)
extern int logger_write( const unsigned char prio,
                         const char __kernel * const tag,
                         const char __kernel * const fmt,
                         ...);


#endif // ANDROID
#endif
