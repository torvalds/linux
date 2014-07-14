/*
 * Copyright (c) 2011 Espressif System.
 */

#include <linux/types.h>
#include <linux/kernel.h>

#include <net/mac80211.h>
#include "sip2_common.h"

#include "esp_debug.h"

#if defined(CONFIG_DEBUG_FS) && defined(DEBUG_FS)

static struct dentry *esp_debugfs_root = NULL;

static int esp_debugfs_open(struct inode *inode, struct file *filp)
{
        filp->private_data = inode->i_private;
        return 0;
}

static ssize_t esp_debugfs_read(struct file *filp, char __user *buffer,
                                size_t count, loff_t *ppos)
{
        if (*ppos >= 32)
                return 0;
        if (*ppos + count > 32)
                count = 32 - *ppos;

        if (copy_to_user(buffer, filp->private_data + *ppos, count))
                return -EFAULT;

        *ppos += count;

        return count;
}

static ssize_t esp_debugfs_write(struct file *filp, const char __user *buffer,
                                 size_t count, loff_t *ppos)
{
        if (*ppos >= 32)
                return 0;
        if (*ppos + count > 32)
                count = 32 - *ppos;

        if (copy_from_user(filp->private_data + *ppos, buffer, count))
                return -EFAULT;

        *ppos += count;

        return count;
}

struct file_operations esp_debugfs_fops = {
        .owner = THIS_MODULE,
        .open = esp_debugfs_open,
        .read = esp_debugfs_read,
        .write = esp_debugfs_write,
};


struct dentry *esp_dump_var(const char *name, struct dentry *parent, void *value, esp_type type) {
        struct dentry *rc = NULL;
        umode_t mode = 0644;

        if(!esp_debugfs_root)
                return NULL;

        if(!parent)
                parent = esp_debugfs_root;

        switch(type) {
        case ESP_U8:
                rc = debugfs_create_u8(name, mode, parent, (u8*)value);
                break;
        case ESP_U16:
                rc = debugfs_create_u16(name, mode, parent, (u16*)value);
                break;
        case ESP_U32:
                rc = debugfs_create_u32(name, mode, parent, (u32*)value);
                break;
        case ESP_U64:
                rc = debugfs_create_u64(name, mode, parent, (u64*)value);
                break;
        case ESP_BOOL:
                rc = debugfs_create_bool(name, mode, parent, (u32*)value);
                break;
        default: //32
                rc = debugfs_create_u32(name, mode, parent, (u32*)value);
        }

        if (!rc)
                goto Fail;
        else
                return rc;
Fail:
        debugfs_remove_recursive(esp_debugfs_root);
        esp_debugfs_root = NULL;
        esp_dbg(ESP_DBG_ERROR, "%s failed, debugfs root removed; var name: %s\n", __FUNCTION__, name);
        return NULL;
}

struct dentry *esp_dump_array(const char *name, struct dentry *parent, struct debugfs_blob_wrapper *blob) {
        struct dentry * rc = NULL;
        umode_t mode = 0644;

        if(!esp_debugfs_root)
                return NULL;

        if(!parent)
                parent = esp_debugfs_root;

        rc = debugfs_create_blob(name, mode, parent, blob);

        if (!rc)
                goto Fail;
        else
                return rc;

Fail:
        debugfs_remove_recursive(esp_debugfs_root);
        esp_debugfs_root = NULL;
        esp_dbg(ESP_DBG_ERROR, "%s failed, debugfs root removed; var name: %s\n", __FUNCTION__, name);
        return NULL;
}

struct dentry *esp_dump(const char *name, struct dentry *parent, void *data, int size) {
        struct dentry *rc;
        umode_t mode = 0644;

        if(!esp_debugfs_root)
                return NULL;

        if(!parent)
                parent = esp_debugfs_root;

        rc = debugfs_create_file(name, mode, parent, data, &esp_debugfs_fops);

        if (!rc)
                goto Fail;
        else
                return rc;

Fail:
        debugfs_remove_recursive(esp_debugfs_root);
        esp_debugfs_root = NULL;
        esp_dbg(ESP_DBG_ERROR, "%s failed, debugfs root removed; var name: %s\n", __FUNCTION__, name);
        return NULL;
}

struct dentry *esp_debugfs_add_sub_dir(const char *name) {
        struct dentry *sub_dir = NULL;

        sub_dir = debugfs_create_dir(name, esp_debugfs_root);

        if (!sub_dir)
                goto Fail;

        return sub_dir;

Fail:
        debugfs_remove_recursive(esp_debugfs_root);
        esp_debugfs_root = NULL;
        esp_dbg(ESP_DBG_ERROR, "%s failed, debugfs root removed; dir name: %s\n", __FUNCTION__, name);
        return NULL;

}

int esp_debugfs_init(void)
{
        esp_dbg(ESP_DBG, "esp debugfs init\n");
        esp_debugfs_root = debugfs_create_dir("esp_debug", NULL);

        if (!esp_debugfs_root || IS_ERR_OR_NULL(esp_debugfs_root)) {
                return -ENOENT;
        }

        return 0;
}

void esp_debugfs_exit(void)
{
        esp_dbg(ESP_DBG, "esp debugfs exit");

        debugfs_remove_recursive(esp_debugfs_root);

        return;
}

#else

inline struct dentry *esp_dump_var(const char *name, struct dentry *parent, void *value, esp_type type) {
        return NULL;
}

inline struct dentry *esp_dump_array(const char *name, struct dentry *parent, struct debugfs_blob_wrapper *blob) {
        return NULL;
}

inline struct dentry *esp_dump(const char *name, struct dentry *parent, void *data, int size) {
        return NULL;
}

struct dentry *esp_debugfs_add_sub_dir(const char *name) {
        return NULL;
}

inline int esp_debugfs_init(void)
{
        return -1;
}

inline void esp_debugfs_exit(void)
{

}

#endif


void show_buf(u8 *buf, u32 len)
{
//	print_hex_dump(KERN_DEBUG, "",  DUMP_PREFIX_OFFSET, 16, 1, buf, len, true);
#if 1
        int i = 0, j;

        printk(KERN_INFO "\n++++++++++++++++show rbuf+++++++++++++++\n");
        for (i = 0; i < (len / 16); i++) {
                j = i * 16;
                printk(KERN_INFO "0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x \n", buf[j], buf[j+1],buf[j+2],buf[j+3],buf[j+4],buf[j+5],buf[j+6],buf[j+7],buf[j+8],buf[j+9],buf[j+10],buf[j+11],buf[j+12],buf[j+13],buf[j+14],buf[j+15]);
        }
        printk(KERN_INFO "\n++++++++++++++++++++++++++++++++++++++++\n");
#endif//0000
}

#ifdef HOST_RC
static u8 get_cnt(u32 cnt_store, int idx)
{
        int shift = idx << 2;

        return (u8)((cnt_store>>shift) & 0xf);
}

void esp_show_rcstatus(struct sip_rc_status *rcstatus)
{
        int i;
        char msg[82];
        char rcstr[16];
        u32 cnt_store = rcstatus->rc_cnt_store;

        memset(msg, 0 ,sizeof(msg));
        memset(rcstr, 0 ,sizeof(rcstr));

        printk(KERN_INFO "rcstatus map 0x%08x cntStore 0x%08x\n", rcstatus->rc_map, rcstatus->rc_cnt_store);

        for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
                if (rcstatus->rc_map & BIT(i)) {
                        sprintf(rcstr, "rcIdx %d, cnt %d ", i, get_cnt(cnt_store, i));
                        strcat(msg, rcstr);
                }
        }
        printk(KERN_INFO "%s \n", msg);
}

void esp_show_tx_rates(struct ieee80211_tx_rate* rates)
{
        int i;
        char msg[128];
        char rcstr[32];

        memset(msg, 0 ,sizeof(msg));
        memset(rcstr, 0 ,sizeof(rcstr));

        for (i = 0; i < IEEE80211_TX_MAX_RATES; i++) {
                if (rates->idx != -1 ) {
                        sprintf(rcstr, "Idx %d, cnt %d, flag %02x ", rates->idx, rates->count, rates->flags);
                        strcat(msg, rcstr);
                }
                rates++;
        }
        strcat(msg, "\n");
        printk(KERN_INFO "%s \n", msg);
}
#endif /* HOST_RC */
