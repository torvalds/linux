#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/moduleparam.h>
#include <linux/firmware.h>
#include <linux/netdevice.h>
#include <linux/aio.h>

#include "esp_android.h"
#include "esp_debug.h"
#include "esp_sif.h"

#ifdef ANDROID
#include "esp_path.h"
#include "esp_conf.h"

int android_readwrite_file(const char *filename, char *rbuf, const char *wbuf, size_t length)
{
        int ret = 0;
        struct file *filp = (struct file *)-ENOENT;
        mm_segment_t oldfs;
        oldfs = get_fs();
        set_fs(KERNEL_DS);
        do {
                int mode = (wbuf) ? O_RDWR | O_CREAT : O_RDONLY;
                filp = filp_open(filename, mode, S_IRUSR);
                if (IS_ERR(filp) || !filp->f_op) {
                        esp_dbg(ESP_DBG_ERROR, "%s: file %s filp_open error\n", __FUNCTION__, filename);
                        ret = -ENOENT;
                        break;
                }

                if (length==0) {
                        /* Read the length of the file only */
                        struct inode    *inode;

                        inode = GET_INODE_FROM_FILEP(filp);
                        if (!inode) {
                                esp_dbg(ESP_DBG_ERROR, "%s: Get inode from %s failed\n", __FUNCTION__, filename);
                                ret = -ENOENT;
                                break;
                        }
                        ret = i_size_read(inode->i_mapping->host);
                        break;
                }

                if (wbuf) {
                        if ( (ret=filp->f_op->write(filp, wbuf, length, &filp->f_pos)) < 0) {
                                esp_dbg(ESP_DBG_ERROR, "%s: Write %u bytes to file %s error %d\n", __FUNCTION__,
                                        length, filename, ret);
                                break;
                        }
                } else {
                        if ( (ret=filp->f_op->read(filp, rbuf, length, &filp->f_pos)) < 0) {
                                esp_dbg(ESP_DBG_ERROR, "%s: Read %u bytes from file %s error %d\n", __FUNCTION__,
                                        length, filename, ret);
                                break;
                        }
                }
        } while (0);

        if (!IS_ERR(filp)) {
                filp_close(filp, NULL);
        }
        set_fs(oldfs);

        return ret;
}

int android_request_firmware(const struct firmware **firmware_p, const char *name,
                             struct device *device)
{
        int ret = 0;
        struct firmware *firmware;
        char filename[256];
        const char *raw_filename = name;
        *firmware_p = firmware = kmalloc((sizeof(*firmware)), GFP_KERNEL);
        if (!firmware)
                return -ENOMEM;

        memset(firmware, 0, sizeof(*firmware));

	if (mod_eagle_path_get() == NULL)
        	sprintf(filename, "%s/%s", FWPATH, raw_filename);
	else 
        	sprintf(filename, "%s/%s", mod_eagle_path_get(), raw_filename);

        do {
                size_t length, bufsize, bmisize;

                if ( (ret=android_readwrite_file(filename, NULL, NULL, 0)) < 0) {
                        break;
                } else {
                        length = ret;
                }

                bufsize = ALIGN(length, PAGE_SIZE);
                bmisize = E_ROUND_UP(length, 4);
                bufsize = max(bmisize, bufsize);
                firmware->data = vmalloc(bufsize);
                firmware->size = length;
                if (!firmware->data) {
                        esp_dbg(ESP_DBG_ERROR, "%s: Cannot allocate buffer for firmware\n", __FUNCTION__);
                        ret = -ENOMEM;
                        break;
                }

                if ( (ret=android_readwrite_file(filename, (char*)firmware->data, NULL, length)) != length) {
                        esp_dbg(ESP_DBG_ERROR, "%s: file read error, ret %d request %d\n", __FUNCTION__, ret, length);
                        ret = -1;
                        break;
                }

        } while (0);

        if (ret<0) {
                if (firmware) {
                        if (firmware->data)
                                vfree(firmware->data);

                        kfree(firmware);
                }
                *firmware_p = NULL;
        } else {
                ret = 0;
        }

        return ret;
}

void android_release_firmware(const struct firmware *firmware)
{
        if (firmware) {
                if (firmware->data)
                        vfree(firmware->data);

                kfree((struct firmware *)firmware);
        }
}

int logger_write( const unsigned char prio,
                  const char __kernel * const tag,
                  const char __kernel * const fmt,
                  ...)
{
        int ret = 0;
        va_list vargs;
        struct file *filp = (struct file *)-ENOENT;
        mm_segment_t oldfs;
        struct iovec vec[3];
        int tag_bytes = strlen(tag) + 1, msg_bytes;
        char *msg;
        va_start(vargs, fmt);
        msg = kvasprintf(GFP_ATOMIC, fmt, vargs);
        va_end(vargs);
        if (!msg)
                return -ENOMEM;
        if (in_interrupt()) {
                /* we have no choice since aio_write may be blocked */
                printk(KERN_ALERT "%s", msg);
                goto out_free_message;
        }
        msg_bytes = strlen(msg) + 1;
        if (msg_bytes <= 1) /* empty message? */
                goto out_free_message; /* don't bother, then */
        if ((msg_bytes + tag_bytes + 1) > 2048) {
                ret = -E2BIG;
                goto out_free_message;
        }

        vec[0].iov_base  = (unsigned char *) &prio;
        vec[0].iov_len    = 1;
        vec[1].iov_base   = (void *) tag;
        vec[1].iov_len    = strlen(tag) + 1;
        vec[2].iov_base   = (void *) msg;
        vec[2].iov_len    = strlen(msg) + 1;

        oldfs = get_fs();
        set_fs(KERNEL_DS);
        do {
                filp = filp_open("/dev/log/main", O_WRONLY, S_IRUSR);
                if (IS_ERR(filp) || !filp->f_op) {

                        esp_dbg(ESP_DBG_ERROR, "%s: filp open /dev/log/main error\n", __FUNCTION__);
                        ret = -ENOENT;
                        break;
                }

                if (filp->f_op->aio_write) {
                        int nr_segs = sizeof(vec) / sizeof(vec[0]);
                        int len = vec[0].iov_len + vec[1].iov_len + vec[2].iov_len;
                        struct kiocb kiocb;
                        init_sync_kiocb(&kiocb, filp);
                        kiocb.ki_pos = 0;
                        kiocb.ki_left = len;
                        kiocb.ki_nbytes = len;
                        ret = filp->f_op->aio_write(&kiocb, vec, nr_segs, kiocb.ki_pos);
                }

        } while (0);

        if (!IS_ERR(filp)) {
                filp_close(filp, NULL);
        }
        set_fs(oldfs);
out_free_message:
        if (msg) {
                kfree(msg);
        }
        return ret;
}



struct esp_init_table_elem esp_init_table[MAX_ATTR_NUM] = {
	{"crystal_26M_en", 	48, -1}, 
	{"test_xtal", 		49, -1},
	{"sdio_configure", 	50, -1},
	{"bt_configure", 	51, -1},
	{"bt_protocol", 	52, -1},
	{"dual_ant_configure", 	53, -1},
	{"test_uart_configure", 54, -1},
	{"share_xtal", 		55, -1},
	{"gpio_wake", 		56, -1},
	{"no_auto_sleep", 	57, -1},
	{"attr10", 		-1, -1},
	{"attr11", 		-1, -1},
	{"attr12", 		-1, -1},
	{"attr13", 		-1, -1},
	{"attr14", 		-1, -1},
	{"attr15", 		-1, -1},
	//attr that is not send to target
	{"ext_rst",              -1, -1},
	{"wakeup_gpio",         -1, -1},
        {"attr18",              -1, -1},
        {"attr19",              -1, -1},
        {"attr20",              -1, -1},
        {"attr21",              -1, -1},
        {"attr22",              -1, -1},
        {"attr23",              -1, -1},
	
};

int esp_atoi(char *str)
{
        int num = 0;
        int ng_flag = 0;

        if (*str == '-') {
                str++;
                ng_flag = 1;
        }

        while(*str != '\0') {
                num = num * 10 + *str++ - '0';
        }

        return ng_flag ? 0-num : num;
}

void show_esp_init_table(struct esp_init_table_elem *econf)
{
	int i;
	for (i = 0; i < MAX_ATTR_NUM; i++)
		if (esp_init_table[i].offset > -1)
			esp_dbg(ESP_DBG_ERROR, "%s: esp_init_table[%d] attr[%s] offset[%d] value[%d]\n", 
				__FUNCTION__, i,
				esp_init_table[i].attr,
				esp_init_table[i].offset,
				esp_init_table[i].value);
}
	
int android_request_init_conf(void)
{

	u8 *conf_buf;
	u8 *pbuf;
	int flag;
	int str_len;	
	int length;
	int ret;
	int i;
	char attr_name[CONF_ATTR_LEN];
	char num_buf[CONF_VAL_LEN];
#ifdef INIT_DATA_CONF
	char filename[256];

	if (mod_eagle_path_get() == NULL)
        	sprintf(filename, "%s/%s", FWPATH, INIT_CONF_FILE);
	else
        	sprintf(filename, "%s/%s", mod_eagle_path_get(), INIT_CONF_FILE);

	if ((ret=android_readwrite_file(filename, NULL, NULL, 0)) < 0 || ret > MAX_BUF_LEN) {
		esp_dbg(ESP_DBG_ERROR, "%s: file read length error, ret %d\n", __FUNCTION__, ret);
		return -1;
	} else {
                length = ret;
        }
#endif /* INIT_DATA_CONF */
	conf_buf = (u8 *)kmalloc(MAX_BUF_LEN, GFP_KERNEL);
        if (conf_buf == NULL) {
                esp_dbg(ESP_DBG_ERROR, "%s: failed kmalloc memory for read init_data_conf", __func__);
                return -ENOMEM;
        }

#ifdef INIT_DATA_CONF
	if ((ret=android_readwrite_file(filename, conf_buf, NULL, length)) != length) {
		esp_dbg(ESP_DBG_ERROR, "%s: file read error, ret %d request %d\n", __FUNCTION__, ret, length);
		goto failed;
	}
#else
	length = strlen(INIT_DATA_CONF_BUF);
	strncpy(conf_buf, INIT_DATA_CONF_BUF, length); 
#endif
	conf_buf[length] = '\0';

	flag = 0;
	str_len = 0;
	for (pbuf = conf_buf; *pbuf != '$' && *pbuf != '\n'; pbuf++) {
		if (*pbuf == '=') {
			flag = 1;
			*(attr_name+str_len) = '\0';
			str_len = 0;
			continue;
		}

		if (*pbuf == ';') {
			int value;
			flag = 0;
			*(num_buf+str_len) = '\0';
			if((value = esp_atoi(num_buf)) > 255 || value < 0){
				esp_dbg(ESP_DBG_ERROR, "%s: value is too big", __FUNCTION__);
				goto failed;
			}

			for (i = 0; i < MAX_ATTR_NUM; i++) {
				if (esp_init_table[i].value > -1)
					continue;
				if (strcmp(esp_init_table[i].attr, attr_name) == 0) {
					esp_dbg(ESP_DBG_TRACE, "%s: attr_name[%s]", __FUNCTION__, attr_name); /* add by th */
					esp_init_table[i].value = value;
				}
				if(strcmp(esp_init_table[i].attr, "share_xtal") == 0){
					sif_record_bt_config(esp_init_table[i].value);
				}

				if(strcmp(esp_init_table[i].attr, "ext_rst") == 0){
					sif_record_rst_config(esp_init_table[i].value);
				}

				if(strcmp(esp_init_table[i].attr, "wakeup_gpio") == 0){
					sif_record_wakeup_gpio_config(esp_init_table[i].value);
				}
			}
			str_len = 0;
			continue;
		}

		if (flag == 0) {
			*(attr_name+str_len) = *pbuf;
			if (++str_len > CONF_ATTR_LEN) {
				esp_dbg(ESP_DBG_ERROR, "%s: attr len is too long", __FUNCTION__);
				goto failed;
			}
		} else {
			*(num_buf+str_len) = *pbuf;
			if (++str_len > CONF_VAL_LEN) {
				esp_dbg(ESP_DBG_ERROR, "%s: value len is too long", __FUNCTION__);
				goto failed;
			}	
		}
	}

	//show_esp_init_table(esp_init_table);

	ret = 0;
failed:
	if (conf_buf)
		kfree(conf_buf);
	return ret;
}

void fix_init_data(u8 *init_data_buf, int buf_size)
{
	int i;

	for (i = 0; i < MAX_FIX_ATTR_NUM; i++) {
		if (esp_init_table[i].offset > -1 && esp_init_table[i].offset < buf_size && esp_init_table[i].value > -1) {
			*(u8 *)(init_data_buf + esp_init_table[i].offset) = esp_init_table[i].value;
                } else if (esp_init_table[i].offset > buf_size) {
			esp_dbg(ESP_DBG_ERROR, "%s: offset[%d] longer than init_data_buf len[%d] Ignore\n", __FUNCTION__, esp_init_table[i].offset, buf_size);
		}
        }

}

void show_init_buf(u8 *buf, int size)
{
	int i = 0;
	
	for (i = 0; i < size; i++)
			printk(KERN_ERR "offset[%d] [0x%02x]", i, buf[i]);
	printk(KERN_ERR "\n");
		
}

#endif //ANDROID
