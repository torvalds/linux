/* Copyright (c) 2008 -2014 Espressif System.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * esp debug interface
 *  - debugfs
 *  - debug level control
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <net/mac80211.h>
#include "sip2_common.h"

#include "esp_debug.h"
#include "esp_sif.h"

#ifdef CONFIG_DEBUG_FS
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

struct dentry *esp_dump(const char *name, struct dentry *parent, void *data, int size, struct file_operations *fops) {
        struct dentry *rc;
        umode_t mode = 0644;

        if(!esp_debugfs_root)
                return NULL;

        if(!parent)
                parent = esp_debugfs_root;

	if (fops == NULL)
        	rc = debugfs_create_file(name, mode, parent, data, &esp_debugfs_fops); /* default */
	else
        	rc = debugfs_create_file(name, mode, parent, data, fops);

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

        if (!esp_debugfs_root || IS_ERR(esp_debugfs_root)) {
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

#ifdef DEBUGFS_BOOTMODE
static int fcc_w_cb(void *data)
{
	esp_dbg(ESP_DBG_TRACE, "%s callback", __func__);
	if (sif_get_esp_run() != 0) {
		esp_dbg(ESP_DBG_ERROR, "%s set fcc mode must after close driver ", __func__);
		return -EBUSY;
	}
	return 0;
}

static int ate_r_cb(void *data)
{
	esp_dbg(ESP_DBG_TRACE, "%s callback", __func__);
	
	((struct dbgfs_bootmode_item *)data)->var = sif_get_ate_config();
	
	return 0;
}


static int ate_w_cb(void *data)
{
	int var = ((struct dbgfs_bootmode_item *)data)->var;

	esp_dbg(ESP_DBG_TRACE, "%s callback", __func__);
	if (sif_get_esp_run() != 0) {
		esp_dbg(ESP_DBG_ERROR, "%s set ate mode must after close driver ", __func__);
		return -EBUSY;
	}
	sif_record_ate_config(var);
	return 0;
}

static int nor_r_cb(void *data)
{
	esp_dbg(ESP_DBG_TRACE, "%s callback", __func__);
	
	((struct dbgfs_bootmode_item *)data)->var = sif_get_esp_run();
	
	return 0;
}


static int nor_w_cb(void *data)
{
	int var;

	esp_dbg(ESP_DBG_TRACE, "%s callback", __func__);
	var = ((struct dbgfs_bootmode_item *)data)->var;
	switch (var) {
	case 0:
		if (sif_get_esp_run() != 0)
			esp_common_exit();
		else {
			esp_dbg(ESP_DBG_ERROR, "%s already down", __func__);
			return -EBUSY;
		}
		break;
	case 1:
		if (sif_get_esp_run() == 0)
			esp_common_init();
		else {
			esp_dbg(ESP_DBG_ERROR, "%s already up!", __func__);
			return -EBUSY;
		}
		break;
	case 2:
		if (sif_get_esp_run() == 0) {
			int i;
			struct dbgfs_bootmode_item *di = ((struct dbgfs_bootmode_item *)data) - DBGFS_NOR_MODE;
			for (i = 0; i < DBGFS_MODE_MAX; i++) {
				if (i == DBGFS_NOR_MODE)
					continue;
				di[i].var = 0;
				if (di[i].w_cb)
					di[i].w_cb(&di[i]);
			}

			esp_dbg(ESP_SHOW, "%s reset all the boot mode", __func__);
		} else {
			esp_dbg(ESP_DBG_ERROR, "%s must down pre!", __func__);
			return -EBUSY;
		}
		break;
	default:
		esp_dbg(ESP_DBG_ERROR, "%s unspec cmd", __func__);
		break;
	}

	return 0;
}

static struct dbgfs_bootmode_item di[DBGFS_MODE_MAX] = {
	{"fccmode", DBGFS_FCC_MODE, 0, NULL, NULL, fcc_w_cb},
	{"atemode", DBGFS_ATE_MODE, 0, NULL, ate_r_cb, ate_w_cb},
	{"normode", DBGFS_NOR_MODE, 0, NULL, nor_r_cb, nor_w_cb},
};

static int debugfs_bootmode_open(struct inode *inode, struct file *filp)
{
	int i;
	for (i = 0; i < DBGFS_MODE_MAX; i++) {
		if (strncmp(filp->f_dentry->d_name.name, di[i].name, sizeof(NAME_LEN_MAX)) == 0) {
			filp->private_data = &di[i];
			return 0;
		}
	}
        filp->private_data = inode->i_private;
        return 0;
}

static u8 called_flag[DBGFS_MODE_MAX] = {0, 0, 0};

static ssize_t debugfs_bootmode_read(struct file *filp, char __user *userbuf,
                                size_t count, loff_t *ppos)
{
	int ret;
	int id;
	char tmpbuf[32+1];
	
	if (*ppos >= 32)
                return 0;
        if (*ppos + count > 32)
                count = 32 - *ppos;

	id = ((struct dbgfs_bootmode_item*)filp->private_data)->id;
	if (called_flag[id] == 0 && di[id].r_cb) {
		di[id].r_cb(&di[id]);
		called_flag[id] = 1;
	}

	snprintf(tmpbuf, sizeof(tmpbuf), "0x%08x\n", di[id].var);
		
	ret = simple_read_from_buffer(userbuf, count, ppos, tmpbuf + *ppos, strlen(tmpbuf + *ppos));
	if (ret == 0)
		called_flag[id] = 0;

	esp_dbg(ESP_DBG_TRACE, "%s 0x%x, ret %d, count %zu, pos %d", __func__, di[id].var, ret, count, *(int *)ppos);
	return ret;
}

static ssize_t debugfs_bootmode_write(struct file *filp, const char __user *userbuf,
                                 size_t count, loff_t *ppos)
{
	int id;
	int ret;
	u32 org_var;
	char tmpbuf[32+1];

        if (*ppos >= 32)
                return 0;
        if (*ppos + count > 32)
                count = 32 - *ppos;

        if (copy_from_user(tmpbuf, userbuf, count))
                return -EFAULT;

	if (tmpbuf[count-1] == '\n')
		tmpbuf[count-1] = '\0';
	else
		tmpbuf[count] = '\0';

	id = ((struct dbgfs_bootmode_item*)filp->private_data)->id;

	org_var = di[id].var;

	if (tmpbuf[0] == '0' && (tmpbuf[1] == 'x' || tmpbuf[1] == 'X'))
		ret = strict_strtoul(tmpbuf, 16, (unsigned long *)&di[id].var);
	else 
		ret = strict_strtoul(tmpbuf, 16, (unsigned long *)&di[id].var);

	if (ret) {
		esp_dbg(ESP_DBG_ERROR, "invalid input");
		return count;
	}

	if (di[id].w_cb) {
		ret = di[id].w_cb(&di[id]);
		if (ret)
			di[id].var = org_var;
	}

	esp_dbg(ESP_DBG_TRACE, "%s 0x%x", __func__, di[id].var);
        return count;
}

struct file_operations debugfs_bootmode_fops = {
        .owner = THIS_MODULE,
        .open = debugfs_bootmode_open,
        .read = debugfs_bootmode_read,
        .write = debugfs_bootmode_write,
};

u32 dbgfs_get_bootmode_var(DBGFS_BOOTMODE_ID_t id)
{
	if (id >= DBGFS_MODE_MAX)
		return 0xffffffff;
	
	return di[id].var;
}

struct dbgfs_bootmode_item *dbgfs_get_bootmode_di(DBGFS_BOOTMODE_ID_t id)
{
	if (id >= DBGFS_MODE_MAX)
		return NULL;
	
	return &di[id];
}


void dbgfs_bootmode_init(void)
{
	int id;
	struct dentry *de;

        if(!esp_debugfs_root)
                return;

	for (id = 0; id < DBGFS_MODE_MAX; id++) {
		de = esp_dump(di[id].name, NULL, &di[id].var, 32, &debugfs_bootmode_fops);
		if (de == NULL) 
			esp_dbg(ESP_DBG_ERROR, "dbgfs bootmode init error, continue!");
	}
}

#endif /* DEBUGFS_BOOTMODE */
#else

inline struct dentry *esp_dump_var(const char *name, struct dentry *parent, void *value, esp_type type) {
        return NULL;
}

inline struct dentry *esp_dump_array(const char *name, struct dentry *parent, struct debugfs_blob_wrapper *blob) {
        return NULL;
}

inline struct dentry *esp_dump(const char *name, struct dentry *parent, void *data, int size, struct file_operations *fops) {
        return NULL;
}

struct dentry *esp_debugfs_add_sub_dir(const char *name) {
        return NULL;
}

inline int esp_debugfs_init(void)
{
        return -EPERM;
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
