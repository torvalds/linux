/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 *****************************************************************************/

#include <linux/ctype.h>	/* tolower() */
#include <drv_types.h>
#include <hal_data.h>
#include "rtw_proc.h"
#include <rtw_btcoex.h>

#ifdef CONFIG_PROC_DEBUG

static struct proc_dir_entry *rtw_proc = NULL;

inline struct proc_dir_entry *get_rtw_drv_proc(void)
{
	return rtw_proc;
}

#define RTW_PROC_NAME DRV_NAME

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 9, 0))
#define file_inode(file) ((file)->f_dentry->d_inode)
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(3, 10, 0))
#define PDE_DATA(inode) PDE((inode))->data
#define proc_get_parent_data(inode) PDE((inode))->parent->data
#endif

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 24))
#define get_proc_net proc_net
#else
#define get_proc_net init_net.proc_net
#endif

inline struct proc_dir_entry *rtw_proc_create_dir(const char *name, struct proc_dir_entry *parent, void *data)
{
	struct proc_dir_entry *entry;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
	entry = proc_mkdir_data(name, S_IRUGO | S_IXUGO, parent, data);
#else
	/* entry = proc_mkdir_mode(name, S_IRUGO|S_IXUGO, parent); */
	entry = proc_mkdir(name, parent);
	if (entry)
		entry->data = data;
#endif

	return entry;
}

inline struct proc_dir_entry *rtw_proc_create_entry(const char *name, struct proc_dir_entry *parent,
	const struct file_operations *fops, void * data)
{
	struct proc_dir_entry *entry;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 26))
	entry = proc_create_data(name,  S_IFREG | S_IRUGO | S_IWUGO, parent, fops, data);
#else
	entry = create_proc_entry(name, S_IFREG | S_IRUGO | S_IWUGO, parent);
	if (entry) {
		entry->data = data;
		entry->proc_fops = fops;
	}
#endif

	return entry;
}

static int proc_get_dummy(struct seq_file *m, void *v)
{
	return 0;
}

static int proc_get_drv_version(struct seq_file *m, void *v)
{
	dump_drv_version(m);
	return 0;
}

static int proc_get_log_level(struct seq_file *m, void *v)
{
	dump_log_level(m);
	return 0;
}

static int proc_get_drv_cfg(struct seq_file *m, void *v)
{
	dump_drv_cfg(m);
	return 0;
}

static ssize_t proc_set_log_level(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	char tmp[32];
	int log_level;

	if (count < 1)
		return -EINVAL;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

#ifdef CONFIG_RTW_DEBUG
	if (buffer && !copy_from_user(tmp, buffer, count)) {

		int num = sscanf(tmp, "%d ", &log_level);

		if (log_level >= _DRV_NONE_ && log_level <= _DRV_MAX_) {
			rtw_drv_log_level = log_level;
			printk("rtw_drv_log_level:%d\n", rtw_drv_log_level);
		}
	} else
		return -EFAULT;
#else
	printk("CONFIG_RTW_DEBUG is disabled\n");
#endif

	return count;
}

#ifdef DBG_MEM_ALLOC
static int proc_get_mstat(struct seq_file *m, void *v)
{
	rtw_mstat_dump(m);
	return 0;
}
#endif /* DBG_MEM_ALLOC */

static int proc_get_country_chplan_map(struct seq_file *m, void *v)
{
	dump_country_chplan_map(m);
	return 0;
}

static int proc_get_chplan_id_list(struct seq_file *m, void *v)
{
	dump_chplan_id_list(m);
	return 0;
}

static int proc_get_chplan_test(struct seq_file *m, void *v)
{
	dump_chplan_test(m);
	return 0;
}

#ifdef RTW_HALMAC
extern void rtw_halmac_get_version(char *str, u32 len);

static int proc_get_halmac_info(struct seq_file *m, void *v)
{
	char ver[30] = {0};


	rtw_halmac_get_version(ver, 30);
	RTW_PRINT_SEL(m, "version: %s\n", ver);

	return 0;
}
#endif

/*
* rtw_drv_proc:
* init/deinit when register/unregister driver
*/
const struct rtw_proc_hdl drv_proc_hdls[] = {
	RTW_PROC_HDL_SSEQ("ver_info", proc_get_drv_version, NULL),
	RTW_PROC_HDL_SSEQ("log_level", proc_get_log_level, proc_set_log_level),
	RTW_PROC_HDL_SSEQ("drv_cfg", proc_get_drv_cfg, NULL),
#ifdef DBG_MEM_ALLOC
	RTW_PROC_HDL_SSEQ("mstat", proc_get_mstat, NULL),
#endif /* DBG_MEM_ALLOC */
	RTW_PROC_HDL_SSEQ("country_chplan_map", proc_get_country_chplan_map, NULL),
	RTW_PROC_HDL_SSEQ("chplan_id_list", proc_get_chplan_id_list, NULL),
	RTW_PROC_HDL_SSEQ("chplan_test", proc_get_chplan_test, NULL),
#ifdef RTW_HALMAC
	RTW_PROC_HDL_SSEQ("halmac_info", proc_get_halmac_info, NULL),
#endif /* RTW_HALMAC */
};

const int drv_proc_hdls_num = sizeof(drv_proc_hdls) / sizeof(struct rtw_proc_hdl);

static int rtw_drv_proc_open(struct inode *inode, struct file *file)
{
	/* struct net_device *dev = proc_get_parent_data(inode); */
	ssize_t index = (ssize_t)PDE_DATA(inode);
	const struct rtw_proc_hdl *hdl = drv_proc_hdls + index;
	void *private = NULL;

	if (hdl->type == RTW_PROC_HDL_TYPE_SEQ) {
		int res = seq_open(file, hdl->u.seq_op);

		if (res == 0)
			((struct seq_file *)file->private_data)->private = private;

		return res;
	} else if (hdl->type == RTW_PROC_HDL_TYPE_SSEQ) {
		int (*show)(struct seq_file *, void *) = hdl->u.show ? hdl->u.show : proc_get_dummy;

		return single_open(file, show, private);
	} else {
		return -EROFS;
	}
}

static ssize_t rtw_drv_proc_write(struct file *file, const char __user *buffer, size_t count, loff_t *pos)
{
	ssize_t index = (ssize_t)PDE_DATA(file_inode(file));
	const struct rtw_proc_hdl *hdl = drv_proc_hdls + index;
	ssize_t (*write)(struct file *, const char __user *, size_t, loff_t *, void *) = hdl->write;

	if (write)
		return write(file, buffer, count, pos, NULL);

	return -EROFS;
}

static const struct file_operations rtw_drv_proc_seq_fops = {
	.owner = THIS_MODULE,
	.open = rtw_drv_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
	.write = rtw_drv_proc_write,
};

static const struct file_operations rtw_drv_proc_sseq_fops = {
	.owner = THIS_MODULE,
	.open = rtw_drv_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = rtw_drv_proc_write,
};

int rtw_drv_proc_init(void)
{
	int ret = _FAIL;
	ssize_t i;
	struct proc_dir_entry *entry = NULL;

	if (rtw_proc != NULL) {
		rtw_warn_on(1);
		goto exit;
	}

	rtw_proc = rtw_proc_create_dir(RTW_PROC_NAME, get_proc_net, NULL);

	if (rtw_proc == NULL) {
		rtw_warn_on(1);
		goto exit;
	}

	for (i = 0; i < drv_proc_hdls_num; i++) {
		if (drv_proc_hdls[i].type == RTW_PROC_HDL_TYPE_SEQ)
			entry = rtw_proc_create_entry(drv_proc_hdls[i].name, rtw_proc, &rtw_drv_proc_seq_fops, (void *)i);
		else if (drv_proc_hdls[i].type == RTW_PROC_HDL_TYPE_SSEQ)
			entry = rtw_proc_create_entry(drv_proc_hdls[i].name, rtw_proc, &rtw_drv_proc_sseq_fops, (void *)i);
		else
			entry = NULL;

		if (!entry) {
			rtw_warn_on(1);
			goto exit;
		}
	}

	ret = _SUCCESS;

exit:
	return ret;
}

void rtw_drv_proc_deinit(void)
{
	int i;

	if (rtw_proc == NULL)
		return;

	for (i = 0; i < drv_proc_hdls_num; i++)
		remove_proc_entry(drv_proc_hdls[i].name, rtw_proc);

	remove_proc_entry(RTW_PROC_NAME, get_proc_net);
	rtw_proc = NULL;
}

#ifndef RTW_SEQ_FILE_TEST
#define RTW_SEQ_FILE_TEST 0
#endif

#if RTW_SEQ_FILE_TEST
#define RTW_SEQ_FILE_TEST_SHOW_LIMIT 300
static void *proc_start_seq_file_test(struct seq_file *m, loff_t *pos)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	RTW_PRINT(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(adapter));
	if (*pos >= RTW_SEQ_FILE_TEST_SHOW_LIMIT) {
		RTW_PRINT(FUNC_ADPT_FMT" pos:%llu, out of range return\n", FUNC_ADPT_ARG(adapter), *pos);
		return NULL;
	}

	RTW_PRINT(FUNC_ADPT_FMT" return pos:%lld\n", FUNC_ADPT_ARG(adapter), *pos);
	return pos;
}
void proc_stop_seq_file_test(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	RTW_PRINT(FUNC_ADPT_FMT"\n", FUNC_ADPT_ARG(adapter));
}

void *proc_next_seq_file_test(struct seq_file *m, void *v, loff_t *pos)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	(*pos)++;
	if (*pos >= RTW_SEQ_FILE_TEST_SHOW_LIMIT) {
		RTW_PRINT(FUNC_ADPT_FMT" pos:%lld, out of range return\n", FUNC_ADPT_ARG(adapter), *pos);
		return NULL;
	}

	RTW_PRINT(FUNC_ADPT_FMT" return pos:%lld\n", FUNC_ADPT_ARG(adapter), *pos);
	return pos;
}

static int proc_get_seq_file_test(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	u32 pos = *((loff_t *)(v));
	RTW_PRINT(FUNC_ADPT_FMT" pos:%d\n", FUNC_ADPT_ARG(adapter), pos);
	RTW_PRINT_SEL(m, FUNC_ADPT_FMT" pos:%d\n", FUNC_ADPT_ARG(adapter), pos);
	return 0;
}

struct seq_operations seq_file_test = {
	.start = proc_start_seq_file_test,
	.stop  = proc_stop_seq_file_test,
	.next  = proc_next_seq_file_test,
	.show  = proc_get_seq_file_test,
};
#endif /* RTW_SEQ_FILE_TEST */

#ifdef CONFIG_SDIO_HCI
static int proc_get_sd_f0_reg_dump(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	sd_f0_reg_dump(m, adapter);

	return 0;
}

static int proc_get_sdio_local_reg_dump(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	sdio_local_reg_dump(m, adapter);

	return 0;
}
static int proc_get_sdio_card_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	dump_sdio_card_info(m, adapter_to_dvobj(adapter));

	return 0;
}
#endif /* CONFIG_SDIO_HCI */

static int proc_get_fw_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	rtw_dump_fw_info(m, adapter);
	return 0;
}
static int proc_get_mac_reg_dump(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	mac_reg_dump(m, adapter);

	return 0;
}

static int proc_get_bb_reg_dump(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	bb_reg_dump(m, adapter);

	return 0;
}

static int proc_get_bb_reg_dump_ex(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	bb_reg_dump_ex(m, adapter);

	return 0;
}

static int proc_get_rf_reg_dump(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	rf_reg_dump(m, adapter);

	return 0;
}

static int proc_get_dump_tx_rate_bmp(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	dump_tx_rate_bmp(m, adapter_to_dvobj(adapter));

	return 0;
}

static int proc_get_dump_adapters_status(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	dump_adapters_status(m, adapter_to_dvobj(adapter));

	return 0;
}

#ifdef CONFIG_RTW_CUSTOMER_STR
static int proc_get_customer_str(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	u8 cstr[RTW_CUSTOMER_STR_LEN];

	rtw_ps_deny(adapter, PS_DENY_IOCTL);
	if (rtw_pwr_wakeup(adapter) == _FAIL)
		goto exit;

	if (rtw_hal_customer_str_read(adapter, cstr) != _SUCCESS)
		goto exit;

	RTW_PRINT_SEL(m, RTW_CUSTOMER_STR_FMT"\n", RTW_CUSTOMER_STR_ARG(cstr));

exit:
	rtw_ps_deny_cancel(adapter, PS_DENY_IOCTL);
	return 0;
}
#endif /* CONFIG_RTW_CUSTOMER_STR */

/* gpio setting */
#ifdef CONFIG_GPIO_API
static ssize_t proc_set_config_gpio(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32] = {0};
	int num = 0, gpio_pin = 0, gpio_mode = 0; /* gpio_mode:0 input  1:output; */

	if (count < 2)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		num	= sscanf(tmp, "%d %d", &gpio_pin, &gpio_mode);
		RTW_INFO("num=%d gpio_pin=%d mode=%d\n", num, gpio_pin, gpio_mode);
		padapter->pre_gpio_pin = gpio_pin;

		if (gpio_mode == 0 || gpio_mode == 1)
			rtw_hal_config_gpio(padapter, gpio_pin, gpio_mode);
	}
	return count;

}
static ssize_t proc_set_gpio_output_value(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32] = {0};
	int num = 0, gpio_pin = 0, pin_mode = 0; /* pin_mode: 1 high         0:low */

	if (count < 2)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		num	= sscanf(tmp, "%d %d", &gpio_pin, &pin_mode);
		RTW_INFO("num=%d gpio_pin=%d pin_high=%d\n", num, gpio_pin, pin_mode);
		padapter->pre_gpio_pin = gpio_pin;

		if (pin_mode == 0 || pin_mode == 1)
			rtw_hal_set_gpio_output_value(padapter, gpio_pin, pin_mode);
	}
	return count;
}
static int proc_get_gpio(struct seq_file *m, void *v)
{
	u8 gpioreturnvalue = 0;
	struct net_device *dev = m->private;

	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	if (!padapter)
		return -EFAULT;
	gpioreturnvalue = rtw_hal_get_gpio(padapter, padapter->pre_gpio_pin);
	RTW_PRINT_SEL(m, "get_gpio %d:%d\n", padapter->pre_gpio_pin, gpioreturnvalue);

	return 0;

}
static ssize_t proc_set_gpio(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32] = {0};
	int num = 0, gpio_pin = 0;

	if (count < 1)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		num	= sscanf(tmp, "%d", &gpio_pin);
		RTW_INFO("num=%d gpio_pin=%d\n", num, gpio_pin);
		padapter->pre_gpio_pin = gpio_pin;

	}
	return count;
}
#endif

static ssize_t proc_set_rx_info_msg(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{

	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct recv_priv *precvpriv = &(padapter->recvpriv);
	char tmp[32] = {0};
	int phy_info_flag = 0;

	if (!padapter)
		return -EFAULT;

	if (count < 1) {
		RTW_INFO("argument size is less than 1\n");
		return -EFAULT;
	}

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		int num = sscanf(tmp, "%d", &phy_info_flag);

		precvpriv->store_law_data_flag = (BOOLEAN) phy_info_flag;

		/*RTW_INFO("precvpriv->store_law_data_flag = %d\n",( BOOLEAN )(precvpriv->store_law_data_flag));*/
	}
	return count;
}
static int proc_get_rx_info_msg(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);

	rtw_hal_set_odm_var(padapter, HAL_ODM_RX_Dframe_INFO, m, _FALSE);
	return 0;
}
static int proc_get_tx_info_msg(struct seq_file *m, void *v)
{
	_irqL irqL;
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);
	struct sta_info *psta;
	u8 bc_addr[6] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
	u8 null_addr[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct sta_priv *pstapriv = &padapter->stapriv;
	int i;
	_list	*plist, *phead;
	u8 current_rate_id = 0, current_sgi = 0;

	char *BW, *status;

	_enter_critical_bh(&pstapriv->sta_hash_lock, &irqL);

	if (MLME_IS_STA(padapter))
		status = "station mode";
	else if (MLME_IS_AP(padapter))
		status = "AP mode";
	else if (MLME_IS_MESH(padapter))
		status = "mesh mode";
	else
		status = " ";
	_RTW_PRINT_SEL(m, "status=%s\n", status);
	for (i = 0; i < NUM_STA; i++) {
		phead = &(pstapriv->sta_hash[i]);
		plist = get_next(phead);

		while ((rtw_end_of_queue_search(phead, plist)) == _FALSE) {

			psta = LIST_CONTAINOR(plist, struct sta_info, hash_list);

			plist = get_next(plist);

			if ((_rtw_memcmp(psta->cmn.mac_addr, bc_addr, 6)  !=  _TRUE)
				&& (_rtw_memcmp(psta->cmn.mac_addr, null_addr, 6) != _TRUE)
				&& (_rtw_memcmp(psta->cmn.mac_addr, adapter_mac_addr(padapter), 6) != _TRUE)) {

				switch (psta->cmn.bw_mode) {

				case CHANNEL_WIDTH_20:
					BW = "20M";
					break;

				case CHANNEL_WIDTH_40:
					BW = "40M";
					break;

				case CHANNEL_WIDTH_80:
					BW = "80M";
					break;

				case CHANNEL_WIDTH_160:
					BW = "160M";
					break;

				default:
					BW = "";
					break;
				}
				current_rate_id = rtw_get_current_tx_rate(adapter, psta);
				current_sgi = rtw_get_current_tx_sgi(adapter, psta);

				RTW_PRINT_SEL(m, "==============================\n");
				_RTW_PRINT_SEL(m, "macaddr=" MAC_FMT"\n", MAC_ARG(psta->cmn.mac_addr));
				_RTW_PRINT_SEL(m, "Tx_Data_Rate=%s\n", HDATA_RATE(current_rate_id));
				_RTW_PRINT_SEL(m, "BW=%s,sgi=%u\n", BW, current_sgi);

			}
		}
	}

	_exit_critical_bh(&pstapriv->sta_hash_lock, &irqL);

	return 0;

}


static int proc_get_linked_info_dump(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	if (padapter)
		RTW_PRINT_SEL(m, "linked_info_dump :%s\n", (padapter->bLinkInfoDump) ? "enable" : "disable");

	return 0;
}


static ssize_t proc_set_linked_info_dump(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);

	char tmp[32] = {0};
	int mode = 0, pre_mode = 0;
	int num = 0;

	if (count < 1)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	pre_mode = padapter->bLinkInfoDump;
	RTW_INFO("pre_mode=%d\n", pre_mode);

	if (buffer && !copy_from_user(tmp, buffer, count)) {

		num	= sscanf(tmp, "%d ", &mode);
		RTW_INFO("num=%d mode=%d\n", num, mode);

		if (num != 1) {
			RTW_INFO("argument number is wrong\n");
			return -EFAULT;
		}

		if (mode == 1 || (mode == 0 && pre_mode == 1)) /* not consider pwr_saving 0: */
			padapter->bLinkInfoDump = mode;

		else if ((mode == 2) || (mode == 0 && pre_mode == 2)) { /* consider power_saving */
			/* RTW_INFO("linked_info_dump =%s\n", (padapter->bLinkInfoDump)?"enable":"disable") */
			linked_info_dump(padapter, mode);
		}
	}
	return count;
}


static int proc_get_sta_tp_dump(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);

	if (padapter)
		RTW_PRINT_SEL(m, "sta_tp_dump :%s\n", (padapter->bsta_tp_dump) ? "enable" : "disable");

	return 0;
}

static ssize_t proc_set_sta_tp_dump(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);

	char tmp[32] = {0};
	int mode = 0;
	int num = 0;

	if (count < 1)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {

		num	= sscanf(tmp, "%d ", &mode);

		if (num != 1) {
			RTW_INFO("argument number is wrong\n");
			return -EFAULT;
		}
		if (padapter)
			padapter->bsta_tp_dump = mode;
	}
	return count;
}

static int proc_get_sta_tp_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);

	if (padapter)
		rtw_sta_traffic_info(m, padapter);

	return 0;
}

static int proc_get_turboedca_ctrl(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(padapter);

	if (hal_data)
		RTW_PRINT_SEL(m, "Turbo-EDCA :%s\n", (hal_data->dis_turboedca) ? "Disable" : "Enable");

	return 0;
}

static ssize_t proc_set_turboedca_ctrl(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(padapter);

	char tmp[32] = {0};
	int mode = 0, num = 0;

	if (count < 1)
		return -EFAULT;

	if (count > sizeof(tmp))
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, count)) {

		num	= sscanf(tmp, "%d ", &mode);

		if (num != 1) {
			RTW_INFO("argument number is wrong\n");
			return -EFAULT;
		}
		hal_data->dis_turboedca = mode;
	}
	return count;
}
#ifdef CONFIG_WOWLAN
static int proc_get_wow_lps_ctrl(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);

	if (pwrctl)
		RTW_PRINT_SEL(m, "WOW lps :%s\n", (pwrctl->wowlan_dis_lps) ? "Disable" : "Enable");

	return 0;
}

static ssize_t proc_set_wow_lps_ctrl(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct pwrctrl_priv *pwrctl = adapter_to_pwrctl(padapter);

	char tmp[32] = {0};
	int mode = 0, num = 0;

	if (count < 1)
		return -EFAULT;

	if (count > sizeof(tmp))
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, count)) {

		num	= sscanf(tmp, "%d ", &mode);

		if (num != 1) {
			RTW_INFO("argument number is wrong\n");
			return -EFAULT;
		}
		pwrctl->wowlan_dis_lps = mode;
		RTW_INFO("WOW lps :%s\n", (pwrctl->wowlan_dis_lps) ? "Disable" : "Enable");
	}
	return count;
}
#endif

static int proc_get_mac_qinfo(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	rtw_hal_get_hwreg(adapter, HW_VAR_DUMP_MAC_QUEUE_INFO, (u8 *)m);

	return 0;
}

int proc_get_wifi_spec(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct registry_priv	*pregpriv = &padapter->registrypriv;

	RTW_PRINT_SEL(m, "wifi_spec=%d\n", pregpriv->wifi_spec);
	return 0;
}

static int proc_get_chan_plan(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	dump_cur_chset(m, adapter);

	return 0;
}

static ssize_t proc_set_chan_plan(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	u8 chan_plan = RTW_CHPLAN_UNSPECIFIED;

	if (!padapter)
		return -EFAULT;

	if (count < 1) {
		RTW_INFO("argument size is less than 1\n");
		return -EFAULT;
	}

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		int num = sscanf(tmp, "%hhx", &chan_plan);
		if (num !=  1)
			return count;
	}

	rtw_set_channel_plan(padapter, chan_plan);

	return count;
}

static int proc_get_country_code(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);

	if (rfctl->country_ent)
		dump_country_chplan(m, rfctl->country_ent);
	else
		RTW_PRINT_SEL(m, "unspecified\n");

	return 0;
}

static ssize_t proc_set_country_code(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	char alpha2[2];
	int num;

	if (count < 1)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (!buffer || copy_from_user(tmp, buffer, count))
		goto exit;

	num = sscanf(tmp, "%c%c", &alpha2[0], &alpha2[1]);
	if (num !=	2)
		return count;

	rtw_set_country(padapter, alpha2);

exit:
	return count;
}

#if CONFIG_RTW_MACADDR_ACL
static int proc_get_macaddr_acl(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	dump_macaddr_acl(m, adapter);
	return 0;
}

ssize_t proc_set_macaddr_acl(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *mlme = &adapter->mlmepriv;
	struct mlme_ext_priv *mlmeext = &adapter->mlmeextpriv;
	char tmp[17 * NUM_ACL + 32] = {0};
	u8 mode;
	u8 addr[ETH_ALEN];

	if (count < 1)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		/* mode [<macaddr>] */
		char *c, *next;

		next = tmp;
		c = strsep(&next, " \t");

		if (sscanf(c, "%hhu", &mode) != 1)
			return count;

		if (mode >= RTW_ACL_MODE_MAX)
			mode = RTW_ACL_MODE_DISABLED;

		rtw_set_macaddr_acl(adapter, RTW_ACL_MODE_DISABLED); /* deinit first */
		if (mode == RTW_ACL_MODE_DISABLED)
			return count;

		rtw_set_macaddr_acl(adapter, mode);

		/* macaddr list */
		c = strsep(&next, " \t");
		while (c != NULL) {
			if (sscanf(c, MAC_SFMT, MAC_SARG(addr)) != 6)
				break;

			if (rtw_check_invalid_mac_address(addr, 0) == _FALSE)
				rtw_acl_add_sta(adapter, addr);

			c = strsep(&next, " \t");
		}

	}

exit:
	return count;
}
#endif /* CONFIG_RTW_MACADDR_ACL */

#if CONFIG_RTW_PRE_LINK_STA
static int proc_get_pre_link_sta(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	dump_pre_link_sta_ctl(m, &adapter->stapriv);
	return 0;
}

ssize_t proc_set_pre_link_sta(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *mlme = &adapter->mlmepriv;
	struct mlme_ext_priv *mlmeext = &adapter->mlmeextpriv;
	char tmp[17 * RTW_PRE_LINK_STA_NUM + 32] = {0};
	char arg0[16] = {0};
	u8 addr[ETH_ALEN];

#define PRE_LINK_STA_CMD_RESET	0
#define PRE_LINK_STA_CMD_ADD	1
#define PRE_LINK_STA_CMD_DEL	2
#define PRE_LINK_STA_CMD_NUM	3

	static const char * const pre_link_sta_cmd_str[] = {
		"reset",
		"add",
		"del"
	};
	u8 cmd_id = PRE_LINK_STA_CMD_NUM;

	if (count < 1)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		/* cmd [<macaddr>] */
		char *c, *next;
		int i;

		next = tmp;
		c = strsep(&next, " \t");

		if (sscanf(c, "%s", arg0) != 1)
			goto exit;

		for (i = 0; i < PRE_LINK_STA_CMD_NUM; i++)
			if (strcmp(pre_link_sta_cmd_str[i], arg0) == 0)
				cmd_id = i;

		switch (cmd_id) {
		case PRE_LINK_STA_CMD_RESET:
			rtw_pre_link_sta_ctl_reset(&adapter->stapriv);
			goto exit;
		case PRE_LINK_STA_CMD_ADD:
		case PRE_LINK_STA_CMD_DEL:
			break;
		default:
			goto exit;
		}

		/* macaddr list */
		c = strsep(&next, " \t");
		while (c != NULL) {
			if (sscanf(c, MAC_SFMT, MAC_SARG(addr)) != 6)
				break;

			if (rtw_check_invalid_mac_address(addr, 0) == _FALSE) {
				if (cmd_id == PRE_LINK_STA_CMD_ADD)
					rtw_pre_link_sta_add(&adapter->stapriv, addr);
				else
					rtw_pre_link_sta_del(&adapter->stapriv, addr);
			}

			c = strsep(&next, " \t");
		}
	}

exit:
	return count;
}
#endif /* CONFIG_RTW_PRE_LINK_STA */

#ifdef CONFIG_DFS_MASTER
ssize_t proc_set_update_non_ocp(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	char tmp[32];
	u8 ch, bw = CHANNEL_WIDTH_20, offset = HAL_PRIME_CHNL_OFFSET_DONT_CARE;
	int ms = -1;

	if (count < 1)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {

		int num = sscanf(tmp, "%hhu %hhu %hhu %d", &ch, &bw, &offset, &ms);

		if (num < 1 || (bw != CHANNEL_WIDTH_20 && num < 3))
			goto exit;

		if (bw == CHANNEL_WIDTH_20)
			rtw_chset_update_non_ocp_ms(rfctl->channel_set
				, ch, bw, HAL_PRIME_CHNL_OFFSET_DONT_CARE, ms);
		else
			rtw_chset_update_non_ocp_ms(rfctl->channel_set
				, ch, bw, offset, ms);
	}

exit:
	return count;
}

ssize_t proc_set_radar_detect(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	char tmp[32];
	u8 fake_radar_detect_cnt = 0;

	if (count < 1)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {

		int num = sscanf(tmp, "%hhu", &fake_radar_detect_cnt);

		if (num < 1)
			goto exit;

		rfctl->dbg_dfs_master_fake_radar_detect_cnt = fake_radar_detect_cnt;
	}

exit:
	return count;
}

static int proc_get_dfs_ch_sel_d_flags(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);

	RTW_PRINT_SEL(m, "0x%02x\n", rfctl->dfs_ch_sel_d_flags);

	return 0;
}

static ssize_t proc_set_dfs_ch_sel_d_flags(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	struct rf_ctl_t *rfctl = adapter_to_rfctl(adapter);
	char tmp[32];
	u8 d_flags;
	int num;

	if (count < 1)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (!buffer || copy_from_user(tmp, buffer, count))
		goto exit;

	num = sscanf(tmp, "%hhx", &d_flags);
	if (num !=	1)
		goto exit;

	rfctl->dfs_ch_sel_d_flags = d_flags;

exit:
	return count;
}
#endif /* CONFIG_DFS_MASTER */

#ifdef CONFIG_80211N_HT
int proc_get_rx_ampdu_size_limit(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	dump_regsty_rx_ampdu_size_limit(m, adapter);

	return 0;
}

ssize_t proc_set_rx_ampdu_size_limit(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	struct registry_priv *regsty = adapter_to_regsty(adapter);
	char tmp[32];
	u8 nss;
	u8 limit_by_bw[4] = {0xFF};

	if (count < 1)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		int i;
		int num = sscanf(tmp, "%hhu %hhu %hhu %hhu %hhu"
			, &nss, &limit_by_bw[0], &limit_by_bw[1], &limit_by_bw[2], &limit_by_bw[3]);

		if (num < 2)
			goto exit;
		if (nss == 0 || nss > 4)
			goto exit;

		for (i = 0; i < num - 1; i++)
			regsty->rx_ampdu_sz_limit_by_nss_bw[nss - 1][i] = limit_by_bw[i];

		rtw_rx_ampdu_apply(adapter);
	}

exit:
	return count;
}
#endif /* CONFIG_80211N_HT */

static int proc_get_udpport(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct recv_priv *precvpriv = &(padapter->recvpriv);

	RTW_PRINT_SEL(m, "%d\n", precvpriv->sink_udpport);
	return 0;
}
static ssize_t proc_set_udpport(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct recv_priv *precvpriv = &(padapter->recvpriv);
	int sink_udpport = 0;
	char tmp[32];


	if (!padapter)
		return -EFAULT;

	if (count < 1) {
		RTW_INFO("argument size is less than 1\n");
		return -EFAULT;
	}

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {

		int num = sscanf(tmp, "%d", &sink_udpport);

		if (num !=  1) {
			RTW_INFO("invalid input parameter number!\n");
			return count;
		}

	}
	precvpriv->sink_udpport = sink_udpport;

	return count;

}

static int proc_get_mi_ap_bc_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);
	u8 i;

	for (i = 0; i < dvobj->iface_nums; i++)
		RTW_PRINT_SEL(m, "iface_id:%d, mac_id && sec_cam_id = %d\n", i, macid_ctl->iface_bmc[i]);

	return 0;
}
static int proc_get_macid_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct macid_ctl_t *macid_ctl = dvobj_to_macidctl(dvobj);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);
	u8 i;
	u8 null_addr[ETH_ALEN] = {0};
	u8 *macaddr;

	RTW_PRINT_SEL(m, "max_num:%u\n", macid_ctl->num);
	RTW_PRINT_SEL(m, "\n");

	RTW_PRINT_SEL(m, "used:\n");
	dump_macid_map(m, &macid_ctl->used, macid_ctl->num);
	RTW_PRINT_SEL(m, "\n");

	RTW_PRINT_SEL(m, "%-3s %-3s %-5s %-4s %-17s %-6s %-3s"
		, "id", "bmc", "ifbmp", "ch_g", "macaddr", "bw", "vht");

	if (hal_spec->tx_nss_num > 2)
		_RTW_PRINT_SEL(m, " %-10s", "rate_bmp1");

	_RTW_PRINT_SEL(m, " %-10s %s\n", "rate_bmp0", "status");

	for (i = 0; i < macid_ctl->num; i++) {
		if (rtw_macid_is_used(macid_ctl, i)
			|| macid_ctl->h2c_msr[i]
		) {
			if (macid_ctl->sta[i])
				macaddr = macid_ctl->sta[i]->cmn.mac_addr;
			else
				macaddr = null_addr;

			RTW_PRINT_SEL(m, "%3u %3u  0x%02x %4d "MAC_FMT" %6s %3u"
				, i
				, rtw_macid_is_bmc(macid_ctl, i)
				, rtw_macid_get_iface_bmp(macid_ctl, i)
				, rtw_macid_get_ch_g(macid_ctl, i)
				, MAC_ARG(macaddr)
				, ch_width_str(macid_ctl->bw[i])
				, macid_ctl->vht_en[i]
			);

			if (hal_spec->tx_nss_num > 2)
				_RTW_PRINT_SEL(m, " 0x%08X", macid_ctl->rate_bmp1[i]);

			_RTW_PRINT_SEL(m, " 0x%08X "H2C_MSR_FMT" %s\n"
				, macid_ctl->rate_bmp0[i]
				, H2C_MSR_ARG(&macid_ctl->h2c_msr[i])
				, rtw_macid_is_used(macid_ctl, i) ? "" : "[unused]"
			);
		}
	}

	return 0;
}

static int proc_get_sec_cam(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;

	RTW_PRINT_SEL(m, "sec_cap:0x%02x\n", cam_ctl->sec_cap);
	RTW_PRINT_SEL(m, "flags:0x%08x\n", cam_ctl->flags);
	RTW_PRINT_SEL(m, "\n");

	RTW_PRINT_SEL(m, "max_num:%u\n", cam_ctl->num);
	RTW_PRINT_SEL(m, "used:\n");
	dump_sec_cam_map(m, &cam_ctl->used, cam_ctl->num);
	RTW_PRINT_SEL(m, "\n");

	RTW_PRINT_SEL(m, "reg_scr:0x%04x\n", rtw_read16(adapter, 0x680));
	RTW_PRINT_SEL(m, "\n");

	dump_sec_cam(m, adapter);

	return 0;
}

static ssize_t proc_set_sec_cam(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	struct dvobj_priv *dvobj = adapter_to_dvobj(adapter);
	struct cam_ctl_t *cam_ctl = &dvobj->cam_ctl;
	char tmp[32] = {0};
	char cmd[4];
	u8 id_1 = 0, id_2 = 0;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {

		/* c <id_1>: clear specific cam entry */
		/* wfc <id_1>: write specific cam entry from cam cache */
		/* sw <id_1> <id_2>: sec_cam 1/2 swap */

		int num = sscanf(tmp, "%s %hhu %hhu", cmd, &id_1, &id_2);

		if (num < 2)
			return count;

		if ((id_1 >= cam_ctl->num) || (id_2 >= cam_ctl->num)) {
			RTW_ERR(FUNC_ADPT_FMT" invalid id_1:%u id_2:%u\n", FUNC_ADPT_ARG(adapter), id_1, id_2);
			return count;
		}

		if (strcmp("c", cmd) == 0) {
			_clear_cam_entry(adapter, id_1);
			adapter->securitypriv.hw_decrypted = _FALSE; /* temporarily set this for TX path to use SW enc */
		} else if (strcmp("wfc", cmd) == 0)
			write_cam_from_cache(adapter, id_1);
		else if (strcmp("sw", cmd) == 0)
			rtw_sec_cam_swap(adapter, id_1, id_2);
		else if (strcmp("cdk", cmd) == 0)
			rtw_clean_dk_section(adapter);
#ifdef DBG_SEC_CAM_MOVE
		else if (strcmp("sgd", cmd) == 0)
			rtw_hal_move_sta_gk_to_dk(adapter);
		else if (strcmp("rsd", cmd) == 0)
			rtw_hal_read_sta_dk_key(adapter, id_1);
#endif
	}

	return count;
}

static int proc_get_sec_cam_cache(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	dump_sec_cam_cache(m, adapter);
	return 0;
}

static ssize_t proc_set_change_bss_chbw(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	struct mlme_priv *mlme = &(adapter->mlmepriv);
	struct mlme_ext_priv *mlmeext = &(adapter->mlmeextpriv);
	char tmp[32];
	s16 ch;
	s8 bw = -1, offset = -1;

	if (count < 1)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {

		int num = sscanf(tmp, "%hd %hhd %hhd", &ch, &bw, &offset);

		if (num < 1 || (bw != CHANNEL_WIDTH_20 && num < 3))
			goto exit;

		if ((MLME_IS_AP(adapter) || MLME_IS_MESH(adapter))
			&& check_fwstate(mlme, WIFI_ASOC_STATE))
			rtw_change_bss_chbw_cmd(adapter, RTW_CMDF_WAIT_ACK, ch, bw, offset);
	}

exit:
	return count;
}

static int proc_get_tx_bw_mode(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	RTW_PRINT_SEL(m, "0x%02x\n", adapter->driver_tx_bw_mode);
	RTW_PRINT_SEL(m, "2.4G:%s\n", ch_width_str(ADAPTER_TX_BW_2G(adapter)));
	RTW_PRINT_SEL(m, "5G:%s\n", ch_width_str(ADAPTER_TX_BW_5G(adapter)));

	return 0;
}

static ssize_t proc_set_tx_bw_mode(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	struct macid_ctl_t *macid_ctl = &adapter->dvobj->macid_ctl;
	struct mlme_priv *mlme = &(adapter->mlmepriv);
	struct mlme_ext_priv *mlmeext = &(adapter->mlmeextpriv);
	char tmp[32];
	u8 bw_mode;

	if (count < 1)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {

		u8 update = _FALSE;
		int num = sscanf(tmp, "%hhx", &bw_mode);

		if (num < 1 || bw_mode == adapter->driver_tx_bw_mode)
			goto exit;

		if ((MLME_STATE(adapter) & WIFI_ASOC_STATE)
			&& ((mlmeext->cur_channel <= 14 && BW_MODE_2G(bw_mode) != ADAPTER_TX_BW_2G(adapter))
				|| (mlmeext->cur_channel >= 36 && BW_MODE_5G(bw_mode) != ADAPTER_TX_BW_5G(adapter)))
		) {
			/* RA mask update needed */
			update = _TRUE;
		}
		adapter->driver_tx_bw_mode = bw_mode;

		if (update == _TRUE) {
			struct sta_info *sta;
			int i;

			for (i = 0; i < MACID_NUM_SW_LIMIT; i++) {
				sta = macid_ctl->sta[i];
				if (sta && !is_broadcast_mac_addr(sta->cmn.mac_addr))
					rtw_dm_ra_mask_wk_cmd(adapter, (u8 *)sta);
			}
		}
	}

exit:
	return count;
}

static int proc_get_hal_txpwr_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	struct hal_spec_t *hal_spec = GET_HAL_SPEC(adapter);

	if (hal_is_band_support(adapter, BAND_ON_2_4G))
		dump_hal_txpwr_info_2g(m, adapter, hal_spec->rfpath_num_2g, hal_spec->max_tx_cnt);

#ifdef CONFIG_IEEE80211_BAND_5GHZ
	if (hal_is_band_support(adapter, BAND_ON_5G))
		dump_hal_txpwr_info_5g(m, adapter, hal_spec->rfpath_num_5g, hal_spec->max_tx_cnt);
#endif

	return 0;
}

static int proc_get_target_tx_power(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	dump_target_tx_power(m, adapter);

	return 0;
}

static int proc_get_tx_power_by_rate(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	dump_tx_power_by_rate(m, adapter);

	return 0;
}

#ifdef CONFIG_TXPWR_LIMIT
static int proc_get_tx_power_limit(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	dump_txpwr_lmt(m, adapter);

	return 0;
}
#endif /* CONFIG_TXPWR_LIMIT */

static int proc_get_tx_power_ext_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	dump_tx_power_ext_info(m, adapter);

	return 0;
}

static ssize_t proc_set_tx_power_ext_info(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	char tmp[32] = {0};
	char cmd[16] = {0};

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {

		int num = sscanf(tmp, "%s", cmd);

		if (num < 1)
			return count;

		#ifdef CONFIG_LOAD_PHY_PARA_FROM_FILE
		phy_free_filebuf_mask(adapter, LOAD_BB_PG_PARA_FILE | LOAD_RF_TXPWR_LMT_PARA_FILE);
		#endif

		rtw_ps_deny(adapter, PS_DENY_IOCTL);
		LeaveAllPowerSaveModeDirect(adapter);

		if (strcmp("default", cmd) == 0)
			rtw_run_in_thread_cmd(adapter, ((void *)(phy_reload_default_tx_power_ext_info)), adapter);
		else
			rtw_run_in_thread_cmd(adapter, ((void *)(phy_reload_tx_power_ext_info)), adapter);

		rtw_ps_deny_cancel(adapter, PS_DENY_IOCTL);
	}

	return count;
}

static void *proc_start_tx_power_idx(struct seq_file *m, loff_t *pos)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	u8 path = ((*pos) & 0xFF00) >> 8;
	u8 rs = *pos & 0xFF;

	if (path >= RF_PATH_MAX)
		return NULL;

	return pos;
}
static void proc_stop_tx_power_idx(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
}

static void *proc_next_tx_power_idx(struct seq_file *m, void *v, loff_t *pos)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	u8 path = ((*pos) & 0xFF00) >> 8;
	u8 rs = *pos & 0xFF;

	rs++;
	if (rs >= RATE_SECTION_NUM) {
		rs = 0;
		path++;
	}

	if (path >= RF_PATH_MAX)
		return NULL;

	*pos = (path << 8) | rs;

	return pos;
}

static int proc_get_tx_power_idx(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	u32 pos = *((loff_t *)(v));
	u8 path = (pos & 0xFF00) >> 8;
	u8 rs = pos & 0xFF;

	if (0)
		RTW_INFO("%s path=%u, rs=%u\n", __func__, path, rs);

	if (path == RF_PATH_A && rs == CCK)
		dump_tx_power_idx_title(m, adapter);
	dump_tx_power_idx_by_path_rs(m, adapter, path, rs);

	return 0;
}

static struct seq_operations seq_ops_tx_power_idx = {
	.start = proc_start_tx_power_idx,
	.stop  = proc_stop_tx_power_idx,
	.next  = proc_next_tx_power_idx,
	.show  = proc_get_tx_power_idx,
};

#ifdef CONFIG_RF_POWER_TRIM
static int proc_get_kfree_flag(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	struct kfree_data_t *kfree_data = GET_KFREE_DATA(adapter);

	RTW_PRINT_SEL(m, "0x%02x\n", kfree_data->flag);

	return 0;
}

static ssize_t proc_set_kfree_flag(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	struct kfree_data_t *kfree_data = GET_KFREE_DATA(adapter);
	char tmp[32] = {0};
	u8 flag;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {

		int num = sscanf(tmp, "%hhx", &flag);

		if (num < 1)
			return count;

		kfree_data->flag = flag;
	}

	return count;
}

static int proc_get_kfree_bb_gain(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct kfree_data_t *kfree_data = GET_KFREE_DATA(adapter);
	u8 i, j;

	for (i = 0; i < BB_GAIN_NUM; i++) {
		if (i == 0)
			_RTW_PRINT_SEL(m, "2G: ");
		else if (i == 1)
			_RTW_PRINT_SEL(m, "5GLB1: ");
		else if (i == 2)
			_RTW_PRINT_SEL(m, "5GLB2: ");
		else if (i == 3)
			_RTW_PRINT_SEL(m, "5GMB1: ");
		else if (i == 4)
			_RTW_PRINT_SEL(m, "5GMB2: ");
		else if (i == 5)
			_RTW_PRINT_SEL(m, "5GHB: ");

		for (j = 0; j < hal_data->NumTotalRFPath; j++)
			_RTW_PRINT_SEL(m, "%d ", kfree_data->bb_gain[i][j]);
		_RTW_PRINT_SEL(m, "\n");
	}

	return 0;
}

static ssize_t proc_set_kfree_bb_gain(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	HAL_DATA_TYPE *hal_data = GET_HAL_DATA(adapter);
	struct kfree_data_t *kfree_data = GET_KFREE_DATA(adapter);
	char tmp[BB_GAIN_NUM * RF_PATH_MAX] = {0};
	u8 path, chidx;
	s8 bb_gain[BB_GAIN_NUM];
	char ch_band_Group[6];

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		char *c, *next;
		int i = 0;

		next = tmp;
		c = strsep(&next, " \t");

		if (sscanf(c, "%s", ch_band_Group) != 1) {
			RTW_INFO("Error Head Format, channel Group select\n,Please input:\t 2G , 5GLB1 , 5GLB2 , 5GMB1 , 5GMB2 , 5GHB\n");
			return count;
		}
		if (strcmp("2G", ch_band_Group) == 0)
			chidx = BB_GAIN_2G;
#ifdef CONFIG_IEEE80211_BAND_5GHZ
		else if (strcmp("5GLB1", ch_band_Group) == 0)
			chidx = BB_GAIN_5GLB1;
		else if (strcmp("5GLB2", ch_band_Group) == 0)
			chidx = BB_GAIN_5GLB2;
		else if (strcmp("5GMB1", ch_band_Group) == 0)
			chidx = BB_GAIN_5GMB1;
		else if (strcmp("5GMB2", ch_band_Group) == 0)
			chidx = BB_GAIN_5GMB2;
		else if (strcmp("5GHB", ch_band_Group) == 0)
			chidx = BB_GAIN_5GHB;
#endif /*CONFIG_IEEE80211_BAND_5GHZ*/
		else {
			RTW_INFO("Error Head Format, channel Group select\n,Please input:\t 2G , 5GLB1 , 5GLB2 , 5GMB1 , 5GMB2 , 5GHB\n");
			return count;
		}
		c = strsep(&next, " \t");

		while (c != NULL) {
			if (sscanf(c, "%hhx", &bb_gain[i]) != 1)
				break;

			kfree_data->bb_gain[chidx][i] = bb_gain[i];
			RTW_INFO("%s,kfree_data->bb_gain[%d][%d]=%x\n", __func__, chidx, i, kfree_data->bb_gain[chidx][i]);

			c = strsep(&next, " \t");
			i++;
		}

	}

	return count;

}

static int proc_get_kfree_thermal(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	struct kfree_data_t *kfree_data = GET_KFREE_DATA(adapter);

	_RTW_PRINT_SEL(m, "%d\n", kfree_data->thermal);

	return 0;
}

static ssize_t proc_set_kfree_thermal(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	struct kfree_data_t *kfree_data = GET_KFREE_DATA(adapter);
	char tmp[32] = {0};
	s8 thermal;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {

		int num = sscanf(tmp, "%hhd", &thermal);

		if (num < 1)
			return count;

		kfree_data->thermal = thermal;
	}

	return count;
}

static ssize_t proc_set_tx_gain_offset(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *adapter;
	char tmp[32] = {0};
	u8 rf_path;
	s8 offset;

	adapter = (_adapter *)rtw_netdev_priv(dev);
	if (!adapter)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		u8 write_value;
		int num = sscanf(tmp, "%hhu %hhd", &rf_path, &offset);

		if (num < 2)
			return count;

		RTW_INFO("write rf_path:%u tx gain offset:%d\n", rf_path, offset);
		rtw_rf_set_tx_gain_offset(adapter, rf_path, offset);
	}

	return count;
}
#endif /* CONFIG_RF_POWER_TRIM */

#ifdef CONFIG_BT_COEXIST
ssize_t proc_set_btinfo_evt(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	u8 btinfo[8];

	if (count < 6)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		int num = 0;

		_rtw_memset(btinfo, 0, 8);

		num = sscanf(tmp, "%hhx %hhx %hhx %hhx %hhx %hhx %hhx %hhx"
			, &btinfo[0], &btinfo[1], &btinfo[2], &btinfo[3]
			, &btinfo[4], &btinfo[5], &btinfo[6], &btinfo[7]);

		if (num < 6)
			return -EINVAL;

		btinfo[1] = num - 2;

		rtw_btinfo_cmd(padapter, btinfo, btinfo[1] + 2);
	}

	return count;
}

static u8 btreg_read_type = 0;
static u16 btreg_read_addr = 0;
static int btreg_read_error = 0;
static u8 btreg_write_type = 0;
static u16 btreg_write_addr = 0;
static int btreg_write_error = 0;

static u8 *btreg_type[] = {
	"rf",
	"modem",
	"bluewize",
	"vendor",
	"le"
};

static int btreg_parse_str(char const *input, u8 *type, u16 *addr, u16 *val)
{
	u32 num;
	u8 str[80] = {0};
	u8 t = 0;
	u32 a, v;
	u8 i, n;
	u8 *p;


	num = sscanf(input, "%s %x %x", str, &a, &v);
	if (num < 2) {
		RTW_INFO("%s: INVALID input!(%s)\n", __FUNCTION__, input);
		return -EINVAL;
	}
	if ((num < 3) && val) {
		RTW_INFO("%s: INVALID input!(%s)\n", __FUNCTION__, input);
		return -EINVAL;
	}

	/* convert to lower case for following type compare */
	p = str;
	for (; *p; ++p)
		*p = tolower(*p);
	n = sizeof(btreg_type) / sizeof(btreg_type[0]);
	for (i = 0; i < n; i++) {
		if (!strcmp(str, btreg_type[i])) {
			t = i;
			break;
		}
	}
	if (i == n) {
		RTW_INFO("%s: unknown type(%s)!\n", __FUNCTION__, str);
		return -EINVAL;
	}

	switch (t) {
	case 0:
		/* RF */
		if (a & 0xFFFFFF80) {
			RTW_INFO("%s: INVALID address(0x%X) for type %s(%d)!\n",
				 __FUNCTION__, a, btreg_type[t], t);
			return -EINVAL;
		}
		break;
	case 1:
		/* Modem */
		if (a & 0xFFFFFE00) {
			RTW_INFO("%s: INVALID address(0x%X) for type %s(%d)!\n",
				 __FUNCTION__, a, btreg_type[t], t);
			return -EINVAL;
		}
		break;
	default:
		/* Others(Bluewize, Vendor, LE) */
		if (a & 0xFFFFF000) {
			RTW_INFO("%s: INVALID address(0x%X) for type %s(%d)!\n",
				 __FUNCTION__, a, btreg_type[t], t);
			return -EINVAL;
		}
		break;
	}

	if (val) {
		if (v & 0xFFFF0000) {
			RTW_INFO("%s: INVALID value(0x%x)!\n", __FUNCTION__, v);
			return -EINVAL;
		}
		*val = (u16)v;
	}

	*type = (u8)t;
	*addr = (u16)a;

	return 0;
}

int proc_get_btreg_read(struct seq_file *m, void *v)
{
	struct net_device *dev;
	PADAPTER padapter;
	u16 ret;
	u32 data;


	if (btreg_read_error)
		return btreg_read_error;

	dev = m->private;
	padapter = (PADAPTER)rtw_netdev_priv(dev);

	ret = rtw_btcoex_btreg_read(padapter, btreg_read_type, btreg_read_addr, &data);
	if (CHECK_STATUS_CODE_FROM_BT_MP_OPER_RET(ret, BT_STATUS_BT_OP_SUCCESS))
		RTW_PRINT_SEL(m, "BTREG read: (%s)0x%04X = 0x%08x\n", btreg_type[btreg_read_type], btreg_read_addr, data);
	else
		RTW_PRINT_SEL(m, "BTREG read: (%s)0x%04X read fail. error code = 0x%04x.\n", btreg_type[btreg_read_type], btreg_read_addr, ret);

	return 0;
}

ssize_t proc_set_btreg_read(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	PADAPTER padapter;
	u8 tmp[80] = {0};
	u32 num;
	int err;


	padapter = (PADAPTER)rtw_netdev_priv(dev);

	if (NULL == buffer) {
		RTW_INFO(FUNC_ADPT_FMT ": input buffer is NULL!\n",
			 FUNC_ADPT_ARG(padapter));
		err = -EFAULT;
		goto exit;
	}

	if (count < 1) {
		RTW_INFO(FUNC_ADPT_FMT ": input length is 0!\n",
			 FUNC_ADPT_ARG(padapter));
		err = -EFAULT;
		goto exit;
	}

	num = count;
	if (num > (sizeof(tmp) - 1))
		num = (sizeof(tmp) - 1);

	if (copy_from_user(tmp, buffer, num)) {
		RTW_INFO(FUNC_ADPT_FMT ": copy buffer from user space FAIL!\n",
			 FUNC_ADPT_ARG(padapter));
		err = -EFAULT;
		goto exit;
	}
	/* [Coverity] sure tmp end with '\0'(string terminal) */
	tmp[sizeof(tmp) - 1] = 0;

	err = btreg_parse_str(tmp, &btreg_read_type, &btreg_read_addr, NULL);
	if (err)
		goto exit;

	RTW_INFO(FUNC_ADPT_FMT ": addr=(%s)0x%X\n",
		FUNC_ADPT_ARG(padapter), btreg_type[btreg_read_type], btreg_read_addr);

exit:
	btreg_read_error = err;

	return count;
}

int proc_get_btreg_write(struct seq_file *m, void *v)
{
	struct net_device *dev;
	PADAPTER padapter;
	u16 ret;
	u32 data;


	if (btreg_write_error < 0)
		return btreg_write_error;
	else if (btreg_write_error > 0) {
		RTW_PRINT_SEL(m, "BTREG write: (%s)0x%04X write fail. error code = 0x%04x.\n", btreg_type[btreg_write_type], btreg_write_addr, btreg_write_error);
		return 0;
	}

	dev = m->private;
	padapter = (PADAPTER)rtw_netdev_priv(dev);

	ret = rtw_btcoex_btreg_read(padapter, btreg_write_type, btreg_write_addr, &data);
	if (CHECK_STATUS_CODE_FROM_BT_MP_OPER_RET(ret, BT_STATUS_BT_OP_SUCCESS))
		RTW_PRINT_SEL(m, "BTREG read: (%s)0x%04X = 0x%08x\n", btreg_type[btreg_write_type], btreg_write_addr, data);
	else
		RTW_PRINT_SEL(m, "BTREG read: (%s)0x%04X read fail. error code = 0x%04x.\n", btreg_type[btreg_write_type], btreg_write_addr, ret);

	return 0;
}

ssize_t proc_set_btreg_write(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	PADAPTER padapter;
	u8 tmp[80] = {0};
	u32 num;
	u16 val;
	u16 ret;
	int err;


	padapter = (PADAPTER)rtw_netdev_priv(dev);

	if (NULL == buffer) {
		RTW_INFO(FUNC_ADPT_FMT ": input buffer is NULL!\n",
			 FUNC_ADPT_ARG(padapter));
		err = -EFAULT;
		goto exit;
	}

	if (count < 1) {
		RTW_INFO(FUNC_ADPT_FMT ": input length is 0!\n",
			 FUNC_ADPT_ARG(padapter));
		err = -EFAULT;
		goto exit;
	}

	num = count;
	if (num > (sizeof(tmp) - 1))
		num = (sizeof(tmp) - 1);

	if (copy_from_user(tmp, buffer, num)) {
		RTW_INFO(FUNC_ADPT_FMT ": copy buffer from user space FAIL!\n",
			 FUNC_ADPT_ARG(padapter));
		err = -EFAULT;
		goto exit;
	}

	err = btreg_parse_str(tmp, &btreg_write_type, &btreg_write_addr, &val);
	if (err)
		goto exit;

	RTW_INFO(FUNC_ADPT_FMT ": Set (%s)0x%X = 0x%x\n",
		FUNC_ADPT_ARG(padapter), btreg_type[btreg_write_type], btreg_write_addr, val);

	ret = rtw_btcoex_btreg_write(padapter, btreg_write_type, btreg_write_addr, val);
	if (!CHECK_STATUS_CODE_FROM_BT_MP_OPER_RET(ret, BT_STATUS_BT_OP_SUCCESS))
		err = ret;

exit:
	btreg_write_error = err;

	return count;
}
#endif /* CONFIG_BT_COEXIST */

#ifdef CONFIG_MBSSID_CAM
int proc_get_mbid_cam_cache(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	rtw_mbid_cam_cache_dump(m, __func__, adapter);
	rtw_mbid_cam_dump(m, __func__, adapter);
	return 0;
}
#endif /* CONFIG_MBSSID_CAM */

int proc_get_mac_addr(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	rtw_hal_dump_macaddr(m, adapter);
	return 0;
}

static int proc_get_skip_band(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	int bandskip;

	bandskip = RTW_GET_SCAN_BAND_SKIP(adapter);
	RTW_PRINT_SEL(m, "bandskip:0x%02x\n", bandskip);
	return 0;
}

static ssize_t proc_set_skip_band(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[6];
	u8 skip_band;

	if (count < 1)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}
	if (buffer && !copy_from_user(tmp, buffer, count)) {

		int num = sscanf(tmp, "%hhu", &skip_band);

		if (num < 1)
			return -EINVAL;

		if (1 == skip_band)
			RTW_SET_SCAN_BAND_SKIP(padapter, BAND_24G);
		else if (2 == skip_band)
			RTW_SET_SCAN_BAND_SKIP(padapter, BAND_5G);
		else if (3 == skip_band)
			RTW_CLR_SCAN_BAND_SKIP(padapter, BAND_24G);
		else if (4 == skip_band)
			RTW_CLR_SCAN_BAND_SKIP(padapter, BAND_5G);
	}
	return count;

}

#ifdef CONFIG_RTW_ACS
static int proc_get_chan_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	rtw_acs_chan_info_dump(m, adapter);
	return 0;
}

static int proc_get_best_chan(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	rtw_acs_info_dump(m, adapter);
	return 0;
}

static ssize_t proc_set_acs(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	u8 acs_state = 0;

	if (count < 1)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}
	if (buffer && !copy_from_user(tmp, buffer, count)) {

		int num = sscanf(tmp, "%hhu", &acs_state);

		if (num < 1)
			return -EINVAL;

		if (acs_state)
			rtw_acs_start(padapter);
		else
			rtw_acs_stop(padapter);

	}
	return count;
}
#endif /*CONFIG_RTW_ACS*/

#ifdef CONFIG_BACKGROUND_NOISE_MONITOR
static int proc_get_nm(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	rtw_noise_info_dump(m, adapter);
	return 0;
}

static ssize_t proc_set_nm(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	u8 nm_state = 0;

	if (count < 1)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}
	if (buffer && !copy_from_user(tmp, buffer, count)) {

		int num = sscanf(tmp, "%hhu", &nm_state);

		if (num < 1)
			return -EINVAL;

		if (nm_state)
			rtw_nm_enable(padapter);
		else
			rtw_nm_disable(padapter);

	}
	return count;
}
#endif /*CONFIG_RTW_ACS*/

static int proc_get_hal_spec(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	dump_hal_spec(m, adapter);
	return 0;
}

static int proc_get_phy_cap(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	rtw_dump_phy_cap(m, adapter);
	rtw_dump_drv_phy_cap(m, adapter);
	rtw_get_dft_phy_cap(m, adapter);
	return 0;
}

#ifdef CONFIG_SUPPORT_TRX_SHARED
#include "../../hal/hal_halmac.h"
static int proc_get_trx_share_mode(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	dump_trx_share_mode(m, adapter);
	return 0;
}
#endif

static int proc_dump_rsvd_page(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	rtw_dump_rsvd_page(m, adapter, adapter->rsvd_page_offset, adapter->rsvd_page_num);
	return 0;
}
static ssize_t proc_set_rsvd_page_info(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	u8 page_offset, page_num;

	if (count < 2)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}
	if (buffer && !copy_from_user(tmp, buffer, count)) {

		int num = sscanf(tmp, "%hhu %hhu", &page_offset, &page_num);

		if (num < 2)
			return -EINVAL;
		padapter->rsvd_page_offset = page_offset;
		padapter->rsvd_page_num = page_num;
	}
	return count;
}

#ifdef CONFIG_SUPPORT_FIFO_DUMP
static int proc_dump_fifo(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);

	rtw_dump_fifo(m, adapter, adapter->fifo_sel, adapter->fifo_addr, adapter->fifo_size);
	return 0;
}
static ssize_t proc_set_fifo_info(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	u8 fifo_sel = 0;
	u32 fifo_addr = 0;
	u32 fifo_size = 0;

	if (count < 3)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}
	if (buffer && !copy_from_user(tmp, buffer, count)) {

		int num = sscanf(tmp, "%hhu %x %d", &fifo_sel, &fifo_addr, &fifo_size);

		if (num < 3)
			return -EINVAL;

		padapter->fifo_sel = fifo_sel;
		padapter->fifo_addr = fifo_addr;
		padapter->fifo_size = fifo_size;
	}
	return count;
}
#endif

#ifdef CONFIG_WOW_PATTERN_HW_CAM
int proc_dump_pattern_cam(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	struct pwrctrl_priv *pwrpriv = adapter_to_pwrctl(padapter);
	int i;
	struct  rtl_wow_pattern context;

	for (i = 0 ; i < pwrpriv->wowlan_pattern_idx; i++) {
		rtw_wow_pattern_read_cam_ent(padapter, i, &context);
		rtw_dump_wow_pattern(m, &context, i);
	}

	return 0;
}
#endif

static int proc_get_napi_info(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	struct registry_priv *pregistrypriv = &adapter->registrypriv;
	u8 napi = 0, gro = 0;
	u32 weight = 0;
	struct dvobj_priv *d;
	d = adapter_to_dvobj(adapter);


#ifdef CONFIG_RTW_NAPI
	if (pregistrypriv->en_napi) {
		napi = 1;
		weight = RTL_NAPI_WEIGHT;
	}

#ifdef CONFIG_RTW_GRO
	if (pregistrypriv->en_gro)
		gro = 1;
#endif /* CONFIG_RTW_GRO */
#endif /* CONFIG_RTW_NAPI */

	if (napi) {
		RTW_PRINT_SEL(m, "NAPI enable, weight=%d\n", weight);
#ifdef CONFIG_RTW_NAPI_DYNAMIC
		RTW_PRINT_SEL(m, "Dynamaic NAPI mechanism is on, current NAPI %s\n",
			      d->en_napi_dynamic ? "enable" : "disable");
		RTW_PRINT_SEL(m, "Dynamaic NAPI info:\n"
				 "\ttcp_rx_threshold = %d Mbps\n"
				 "\tcur_rx_tp = %d Mbps\n",
			      pregistrypriv->napi_threshold,
			      d->traffic_stat.cur_rx_tp);
#endif /* CONFIG_RTW_NAPI_DYNAMIC */
	} else {
		RTW_PRINT_SEL(m, "NAPI disable\n");
	}
	RTW_PRINT_SEL(m, "GRO %s\n", gro?"enable":"disable");

	return 0;

}

#ifdef CONFIG_RTW_NAPI_DYNAMIC
static ssize_t proc_set_napi_th(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	struct _ADAPTER *adapter = (struct _ADAPTER *)rtw_netdev_priv(dev);
	struct registry_priv *registry = &adapter->registrypriv;
	char tmp[32] = {0};
	int thrshld = 0;
	int num = 0;


	if (count < 1)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	RTW_INFO("%s: Last threshold = %d Mbps\n", __FUNCTION__, registry->napi_threshold);

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		num = sscanf(tmp, "%d", &thrshld);
		if (num > 0) {
			if (thrshld > 0)
				registry->napi_threshold = thrshld;
		}
	}
	RTW_INFO("%s: New threshold = %d Mbps\n", __FUNCTION__, registry->napi_threshold);
	RTW_INFO("%s: Current RX throughput = %d Mbps\n",
		 __FUNCTION__, adapter_to_dvobj(adapter)->traffic_stat.cur_rx_tp);

	return count;
}
#endif /* CONFIG_RTW_NAPI_DYNAMIC */


ssize_t proc_set_dynamic_agg_enable(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	int enable = 0, i = 0;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {

		struct dvobj_priv *dvobj = adapter_to_dvobj(padapter);
		PADAPTER iface = NULL;
		int num = sscanf(tmp, "%d", &enable);

		if (num !=  1) {
			RTW_INFO("invalid parameter!\n");
			return count;
		}

		RTW_INFO("dynamic_agg_enable:%d\n", enable);

		for (i = 0; i < dvobj->iface_nums; i++) {
			iface = dvobj->padapters[i];
			if (iface)
				iface->registrypriv.dynamic_agg_enable = enable;
		}

	}

	return count;

}

static int proc_get_dynamic_agg_enable(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *adapter = (_adapter *)rtw_netdev_priv(dev);
	struct registry_priv *pregistrypriv = &adapter->registrypriv;

	RTW_PRINT_SEL(m, "dynamic_agg_enable:%d\n", pregistrypriv->dynamic_agg_enable);

	return 0;
}

/*
* rtw_adapter_proc:
* init/deinit when register/unregister net_device
*/
const struct rtw_proc_hdl adapter_proc_hdls[] = {
#if RTW_SEQ_FILE_TEST
	RTW_PROC_HDL_SEQ("seq_file_test", &seq_file_test, NULL),
#endif
	RTW_PROC_HDL_SSEQ("write_reg", NULL, proc_set_write_reg),
	RTW_PROC_HDL_SSEQ("read_reg", proc_get_read_reg, proc_set_read_reg),
	RTW_PROC_HDL_SSEQ("tx_rate_bmp", proc_get_dump_tx_rate_bmp, NULL),
	RTW_PROC_HDL_SSEQ("adapters_status", proc_get_dump_adapters_status, NULL),
#ifdef CONFIG_RTW_CUSTOMER_STR
	RTW_PROC_HDL_SSEQ("customer_str", proc_get_customer_str, NULL),
#endif
	RTW_PROC_HDL_SSEQ("fwstate", proc_get_fwstate, NULL),
	RTW_PROC_HDL_SSEQ("sec_info", proc_get_sec_info, NULL),
	RTW_PROC_HDL_SSEQ("mlmext_state", proc_get_mlmext_state, NULL),
	RTW_PROC_HDL_SSEQ("qos_option", proc_get_qos_option, NULL),
	RTW_PROC_HDL_SSEQ("ht_option", proc_get_ht_option, NULL),
	RTW_PROC_HDL_SSEQ("rf_info", proc_get_rf_info, NULL),
	RTW_PROC_HDL_SSEQ("scan_param", proc_get_scan_param, proc_set_scan_param),
	RTW_PROC_HDL_SSEQ("scan_abort", proc_get_scan_abort, NULL),
#ifdef CONFIG_SCAN_BACKOP
	RTW_PROC_HDL_SSEQ("backop_flags_sta", proc_get_backop_flags_sta, proc_set_backop_flags_sta),
	RTW_PROC_HDL_SSEQ("backop_flags_ap", proc_get_backop_flags_ap, proc_set_backop_flags_ap),
#endif
#ifdef CONFIG_RTW_REPEATER_SON
	RTW_PROC_HDL_SSEQ("rson_data", proc_get_rson_data, proc_set_rson_data),
#endif
	RTW_PROC_HDL_SSEQ("survey_info", proc_get_survey_info, proc_set_survey_info),
	RTW_PROC_HDL_SSEQ("ap_info", proc_get_ap_info, NULL),
	RTW_PROC_HDL_SSEQ("trx_info", proc_get_trx_info, proc_reset_trx_info),
	RTW_PROC_HDL_SSEQ("tx_power_offset", proc_get_tx_power_offset, proc_set_tx_power_offset),
	RTW_PROC_HDL_SSEQ("rate_ctl", proc_get_rate_ctl, proc_set_rate_ctl),
	RTW_PROC_HDL_SSEQ("bw_ctl", proc_get_bw_ctl, proc_set_bw_ctl),
	RTW_PROC_HDL_SSEQ("dis_pwt_ctl", proc_get_dis_pwt, proc_set_dis_pwt),
	RTW_PROC_HDL_SSEQ("mac_qinfo", proc_get_mac_qinfo, NULL),
	RTW_PROC_HDL_SSEQ("macid_info", proc_get_macid_info, NULL),
	RTW_PROC_HDL_SSEQ("bcmc_info", proc_get_mi_ap_bc_info, NULL),
	RTW_PROC_HDL_SSEQ("sec_cam", proc_get_sec_cam, proc_set_sec_cam),
	RTW_PROC_HDL_SSEQ("sec_cam_cache", proc_get_sec_cam_cache, NULL),
	RTW_PROC_HDL_SSEQ("ps_dbg_info", proc_get_ps_dbg_info, proc_set_ps_dbg_info),
	RTW_PROC_HDL_SSEQ("wifi_spec", proc_get_wifi_spec, NULL),
#ifdef CONFIG_LAYER2_ROAMING
	RTW_PROC_HDL_SSEQ("roam_flags", proc_get_roam_flags, proc_set_roam_flags),
	RTW_PROC_HDL_SSEQ("roam_param", proc_get_roam_param, proc_set_roam_param),
	RTW_PROC_HDL_SSEQ("roam_tgt_addr", NULL, proc_set_roam_tgt_addr),
#endif /* CONFIG_LAYER2_ROAMING */

#ifdef CONFIG_RTW_80211R
	RTW_PROC_HDL_SSEQ("ft_flags", proc_get_ft_flags, proc_set_ft_flags),
#endif

#ifdef CONFIG_SDIO_HCI
	RTW_PROC_HDL_SSEQ("sd_f0_reg_dump", proc_get_sd_f0_reg_dump, NULL),
	RTW_PROC_HDL_SSEQ("sdio_local_reg_dump", proc_get_sdio_local_reg_dump, NULL),
	RTW_PROC_HDL_SSEQ("sdio_card_info", proc_get_sdio_card_info, NULL),
#endif /* CONFIG_SDIO_HCI */

	RTW_PROC_HDL_SSEQ("fwdl_test_case", NULL, proc_set_fwdl_test_case),
	RTW_PROC_HDL_SSEQ("del_rx_ampdu_test_case", NULL, proc_set_del_rx_ampdu_test_case),
	RTW_PROC_HDL_SSEQ("wait_hiq_empty", NULL, proc_set_wait_hiq_empty),
	RTW_PROC_HDL_SSEQ("sta_linking_test", NULL, proc_set_sta_linking_test),

	RTW_PROC_HDL_SSEQ("mac_reg_dump", proc_get_mac_reg_dump, NULL),
	RTW_PROC_HDL_SSEQ("bb_reg_dump", proc_get_bb_reg_dump, NULL),
	RTW_PROC_HDL_SSEQ("bb_reg_dump_ex", proc_get_bb_reg_dump_ex, NULL),
	RTW_PROC_HDL_SSEQ("rf_reg_dump", proc_get_rf_reg_dump, NULL),

#ifdef CONFIG_AP_MODE
	RTW_PROC_HDL_SSEQ("all_sta_info", proc_get_all_sta_info, NULL),
	RTW_PROC_HDL_SSEQ("bmc_tx_rate", proc_get_bmc_tx_rate, proc_set_bmc_tx_rate),
#endif /* CONFIG_AP_MODE */

#ifdef DBG_MEMORY_LEAK
	RTW_PROC_HDL_SSEQ("_malloc_cnt", proc_get_malloc_cnt, NULL),
#endif /* DBG_MEMORY_LEAK */

#ifdef CONFIG_FIND_BEST_CHANNEL
	RTW_PROC_HDL_SSEQ("best_channel", proc_get_best_channel, proc_set_best_channel),
#endif

	RTW_PROC_HDL_SSEQ("rx_signal", proc_get_rx_signal, proc_set_rx_signal),
	RTW_PROC_HDL_SSEQ("hw_info", proc_get_hw_status, proc_set_hw_status),

#ifdef CONFIG_80211N_HT
	RTW_PROC_HDL_SSEQ("ht_enable", proc_get_ht_enable, proc_set_ht_enable),
	RTW_PROC_HDL_SSEQ("bw_mode", proc_get_bw_mode, proc_set_bw_mode),
	RTW_PROC_HDL_SSEQ("ampdu_enable", proc_get_ampdu_enable, proc_set_ampdu_enable),
	RTW_PROC_HDL_SSEQ("rx_ampdu", proc_get_rx_ampdu, proc_set_rx_ampdu),
	RTW_PROC_HDL_SSEQ("rx_ampdu_size_limit", proc_get_rx_ampdu_size_limit, proc_set_rx_ampdu_size_limit),
	RTW_PROC_HDL_SSEQ("rx_ampdu_factor", proc_get_rx_ampdu_factor, proc_set_rx_ampdu_factor),
	RTW_PROC_HDL_SSEQ("rx_ampdu_density", proc_get_rx_ampdu_density, proc_set_rx_ampdu_density),
	RTW_PROC_HDL_SSEQ("tx_ampdu_density", proc_get_tx_ampdu_density, proc_set_tx_ampdu_density),
#ifdef CONFIG_TX_AMSDU
	RTW_PROC_HDL_SSEQ("tx_amsdu", proc_get_tx_amsdu, proc_set_tx_amsdu),
	RTW_PROC_HDL_SSEQ("tx_amsdu_rate", proc_get_tx_amsdu_rate, proc_set_tx_amsdu_rate),
#endif
#endif /* CONFIG_80211N_HT */
	RTW_PROC_HDL_SSEQ("tx_max_agg_num", proc_get_tx_max_agg_num, proc_set_tx_max_agg_num),

	RTW_PROC_HDL_SSEQ("en_fwps", proc_get_en_fwps, proc_set_en_fwps),
	RTW_PROC_HDL_SSEQ("mac_rptbuf", proc_get_mac_rptbuf, NULL),

	/* RTW_PROC_HDL_SSEQ("path_rssi", proc_get_two_path_rssi, NULL),
	* 	RTW_PROC_HDL_SSEQ("rssi_disp",proc_get_rssi_disp, proc_set_rssi_disp), */

#ifdef CONFIG_BT_COEXIST
	RTW_PROC_HDL_SSEQ("btcoex_dbg", proc_get_btcoex_dbg, proc_set_btcoex_dbg),
	RTW_PROC_HDL_SSEQ("btcoex", proc_get_btcoex_info, NULL),
	RTW_PROC_HDL_SSEQ("btinfo_evt", NULL, proc_set_btinfo_evt),
	RTW_PROC_HDL_SSEQ("btreg_read", proc_get_btreg_read, proc_set_btreg_read),
	RTW_PROC_HDL_SSEQ("btreg_write", proc_get_btreg_write, proc_set_btreg_write),
#ifdef CONFIG_RF4CE_COEXIST
	RTW_PROC_HDL_SSEQ("rf4ce_state", proc_get_rf4ce_state, proc_set_rf4ce_state),
#endif
#endif /* CONFIG_BT_COEXIST */

#if defined(DBG_CONFIG_ERROR_DETECT)
	RTW_PROC_HDL_SSEQ("sreset", proc_get_sreset, proc_set_sreset),
#endif /* DBG_CONFIG_ERROR_DETECT */
	RTW_PROC_HDL_SSEQ("trx_info_debug", proc_get_trx_info_debug, NULL),
	RTW_PROC_HDL_SSEQ("linked_info_dump", proc_get_linked_info_dump, proc_set_linked_info_dump),
	RTW_PROC_HDL_SSEQ("sta_tp_dump", proc_get_sta_tp_dump, proc_set_sta_tp_dump),
	RTW_PROC_HDL_SSEQ("sta_tp_info", proc_get_sta_tp_info, NULL),
	RTW_PROC_HDL_SSEQ("dis_turboedca", proc_get_turboedca_ctrl, proc_set_turboedca_ctrl),
	RTW_PROC_HDL_SSEQ("tx_info_msg", proc_get_tx_info_msg, NULL),
	RTW_PROC_HDL_SSEQ("rx_info_msg", proc_get_rx_info_msg, proc_set_rx_info_msg),

#ifdef CONFIG_GPIO_API
	RTW_PROC_HDL_SSEQ("gpio_info", proc_get_gpio, proc_set_gpio),
	RTW_PROC_HDL_SSEQ("gpio_set_output_value", NULL, proc_set_gpio_output_value),
	RTW_PROC_HDL_SSEQ("gpio_set_direction", NULL, proc_set_config_gpio),
#endif

#ifdef CONFIG_DBG_COUNTER
	RTW_PROC_HDL_SSEQ("rx_logs", proc_get_rx_logs, NULL),
	RTW_PROC_HDL_SSEQ("tx_logs", proc_get_tx_logs, NULL),
	RTW_PROC_HDL_SSEQ("int_logs", proc_get_int_logs, NULL),
#endif

#ifdef CONFIG_DBG_RF_CAL
	RTW_PROC_HDL_SSEQ("iqk", proc_get_iqk_info, proc_set_iqk),
	RTW_PROC_HDL_SSEQ("lck", proc_get_lck_info, proc_set_lck),
#endif

#ifdef CONFIG_PCI_HCI
	RTW_PROC_HDL_SSEQ("rx_ring", proc_get_rx_ring, NULL),
	RTW_PROC_HDL_SSEQ("tx_ring", proc_get_tx_ring, NULL),
#ifdef DBG_TXBD_DESC_DUMP
	RTW_PROC_HDL_SSEQ("tx_ring_ext", proc_get_tx_ring_ext, proc_set_tx_ring_ext),
#endif
	RTW_PROC_HDL_SSEQ("pci_aspm", proc_get_pci_aspm, NULL),
#endif

#ifdef CONFIG_WOWLAN
	RTW_PROC_HDL_SSEQ("wow_pattern_info", proc_get_pattern_info, proc_set_pattern_info),
	RTW_PROC_HDL_SSEQ("wow_wakeup_event", proc_get_wakeup_event,
			  proc_set_wakeup_event),
	RTW_PROC_HDL_SSEQ("wowlan_last_wake_reason", proc_get_wakeup_reason, NULL),
#ifdef CONFIG_WOW_PATTERN_HW_CAM
	RTW_PROC_HDL_SSEQ("wow_pattern_cam", proc_dump_pattern_cam, NULL),
#endif
	RTW_PROC_HDL_SSEQ("dis_wow_lps", proc_get_wow_lps_ctrl, proc_set_wow_lps_ctrl),
#endif

#ifdef CONFIG_GPIO_WAKEUP
	RTW_PROC_HDL_SSEQ("wowlan_gpio_info", proc_get_wowlan_gpio_info, proc_set_wowlan_gpio_info),
#endif
#ifdef CONFIG_P2P_WOWLAN
	RTW_PROC_HDL_SSEQ("p2p_wowlan_info", proc_get_p2p_wowlan_info, NULL),
#endif
	RTW_PROC_HDL_SSEQ("country_code", proc_get_country_code, proc_set_country_code),
	RTW_PROC_HDL_SSEQ("chan_plan", proc_get_chan_plan, proc_set_chan_plan),
#if CONFIG_RTW_MACADDR_ACL
	RTW_PROC_HDL_SSEQ("macaddr_acl", proc_get_macaddr_acl, proc_set_macaddr_acl),
#endif
#if CONFIG_RTW_PRE_LINK_STA
	RTW_PROC_HDL_SSEQ("pre_link_sta", proc_get_pre_link_sta, proc_set_pre_link_sta),
#endif
#ifdef CONFIG_DFS_MASTER
	RTW_PROC_HDL_SSEQ("dfs_master_test_case", proc_get_dfs_master_test_case, proc_set_dfs_master_test_case),
	RTW_PROC_HDL_SSEQ("update_non_ocp", NULL, proc_set_update_non_ocp),
	RTW_PROC_HDL_SSEQ("radar_detect", NULL, proc_set_radar_detect),
	RTW_PROC_HDL_SSEQ("dfs_ch_sel_d_flags", proc_get_dfs_ch_sel_d_flags, proc_set_dfs_ch_sel_d_flags),
#endif
	RTW_PROC_HDL_SSEQ("new_bcn_max", proc_get_new_bcn_max, proc_set_new_bcn_max),
	RTW_PROC_HDL_SSEQ("sink_udpport", proc_get_udpport, proc_set_udpport),
#ifdef DBG_RX_COUNTER_DUMP
	RTW_PROC_HDL_SSEQ("dump_rx_cnt_mode", proc_get_rx_cnt_dump, proc_set_rx_cnt_dump),
#endif
	RTW_PROC_HDL_SSEQ("change_bss_chbw", NULL, proc_set_change_bss_chbw),
	RTW_PROC_HDL_SSEQ("tx_bw_mode", proc_get_tx_bw_mode, proc_set_tx_bw_mode),
	RTW_PROC_HDL_SSEQ("hal_txpwr_info", proc_get_hal_txpwr_info, NULL),
	RTW_PROC_HDL_SSEQ("target_tx_power", proc_get_target_tx_power, NULL),
	RTW_PROC_HDL_SSEQ("tx_power_by_rate", proc_get_tx_power_by_rate, NULL),
#ifdef CONFIG_TXPWR_LIMIT
	RTW_PROC_HDL_SSEQ("tx_power_limit", proc_get_tx_power_limit, NULL),
#endif
	RTW_PROC_HDL_SSEQ("tx_power_ext_info", proc_get_tx_power_ext_info, proc_set_tx_power_ext_info),
	RTW_PROC_HDL_SEQ("tx_power_idx", &seq_ops_tx_power_idx, NULL),
#ifdef CONFIG_RF_POWER_TRIM
	RTW_PROC_HDL_SSEQ("tx_gain_offset", NULL, proc_set_tx_gain_offset),
	RTW_PROC_HDL_SSEQ("kfree_flag", proc_get_kfree_flag, proc_set_kfree_flag),
	RTW_PROC_HDL_SSEQ("kfree_bb_gain", proc_get_kfree_bb_gain, proc_set_kfree_bb_gain),
	RTW_PROC_HDL_SSEQ("kfree_thermal", proc_get_kfree_thermal, proc_set_kfree_thermal),
#endif
#ifdef CONFIG_POWER_SAVING
	RTW_PROC_HDL_SSEQ("ps_info", proc_get_ps_info, NULL),
#ifdef CONFIG_WMMPS_STA
	RTW_PROC_HDL_SSEQ("wmmps_info", proc_get_wmmps_info, proc_set_wmmps_info),
#endif /* CONFIG_WMMPS_STA */	
#endif
#ifdef CONFIG_TDLS
	RTW_PROC_HDL_SSEQ("tdls_info", proc_get_tdls_info, NULL),
	RTW_PROC_HDL_SSEQ("tdls_enable", proc_get_tdls_enable, proc_set_tdls_enable),
#endif
	RTW_PROC_HDL_SSEQ("monitor", proc_get_monitor, proc_set_monitor),

#ifdef CONFIG_RTW_ACS
	RTW_PROC_HDL_SSEQ("acs", proc_get_best_chan, proc_set_acs),
	RTW_PROC_HDL_SSEQ("chan_info", proc_get_chan_info, NULL),
#endif

#ifdef CONFIG_BACKGROUND_NOISE_MONITOR
	RTW_PROC_HDL_SSEQ("noise_monitor", proc_get_nm, proc_set_nm),
#endif

#ifdef CONFIG_PREALLOC_RX_SKB_BUFFER
	RTW_PROC_HDL_SSEQ("rtkm_info", proc_get_rtkm_info, NULL),
#endif
	RTW_PROC_HDL_SSEQ("efuse_map", proc_get_efuse_map, NULL),
#ifdef CONFIG_IEEE80211W
	RTW_PROC_HDL_SSEQ("11w_tx_sa_query", proc_get_tx_sa_query, proc_set_tx_sa_query),
	RTW_PROC_HDL_SSEQ("11w_tx_deauth", proc_get_tx_deauth, proc_set_tx_deauth),
	RTW_PROC_HDL_SSEQ("11w_tx_auth", proc_get_tx_auth, proc_set_tx_auth),
#endif /* CONFIG_IEEE80211W */

#ifdef CONFIG_MBSSID_CAM
	RTW_PROC_HDL_SSEQ("mbid_cam", proc_get_mbid_cam_cache, NULL),
#endif
	RTW_PROC_HDL_SSEQ("mac_addr", proc_get_mac_addr, NULL),
	RTW_PROC_HDL_SSEQ("skip_band", proc_get_skip_band, proc_set_skip_band),
	RTW_PROC_HDL_SSEQ("hal_spec", proc_get_hal_spec, NULL),

	RTW_PROC_HDL_SSEQ("rx_stat", proc_get_rx_stat, NULL),

	RTW_PROC_HDL_SSEQ("tx_stat", proc_get_tx_stat, NULL),
	/**** PHY Capability ****/
	RTW_PROC_HDL_SSEQ("phy_cap", proc_get_phy_cap, NULL),

	RTW_PROC_HDL_SSEQ("rx_stbc", proc_get_rx_stbc, proc_set_rx_stbc),
	RTW_PROC_HDL_SSEQ("stbc_cap", proc_get_stbc_cap, proc_set_stbc_cap),
	RTW_PROC_HDL_SSEQ("ldpc_cap", proc_get_ldpc_cap, proc_set_ldpc_cap),
#ifdef CONFIG_BEAMFORMING
	RTW_PROC_HDL_SSEQ("txbf_cap", proc_get_txbf_cap, proc_set_txbf_cap),
#endif

#ifdef CONFIG_SUPPORT_TRX_SHARED
	RTW_PROC_HDL_SSEQ("trx_share_mode", proc_get_trx_share_mode, NULL),
#endif
	RTW_PROC_HDL_SSEQ("napi_info", proc_get_napi_info, NULL),
#ifdef CONFIG_RTW_NAPI_DYNAMIC
	RTW_PROC_HDL_SSEQ("napi_th", proc_get_napi_info, proc_set_napi_th),
#endif /* CONFIG_RTW_NAPI_DYNAMIC */

	RTW_PROC_HDL_SSEQ("rsvd_page", proc_dump_rsvd_page, proc_set_rsvd_page_info),

#ifdef CONFIG_SUPPORT_FIFO_DUMP
	RTW_PROC_HDL_SSEQ("fifo_dump", proc_dump_fifo, proc_set_fifo_info),
#endif
	RTW_PROC_HDL_SSEQ("fw_info", proc_get_fw_info, NULL),

#ifdef DBG_XMIT_BLOCK
	RTW_PROC_HDL_SSEQ("xmit_block", proc_get_xmit_block, proc_set_xmit_block),
#endif

	RTW_PROC_HDL_SSEQ("ack_timeout", proc_get_ack_timeout, proc_set_ack_timeout),

	RTW_PROC_HDL_SSEQ("dynamic_agg_enable", proc_get_dynamic_agg_enable, proc_set_dynamic_agg_enable),
	RTW_PROC_HDL_SSEQ("iqk_fw_offload", proc_get_iqk_fw_offload, proc_set_iqk_fw_offload),

};

const int adapter_proc_hdls_num = sizeof(adapter_proc_hdls) / sizeof(struct rtw_proc_hdl);

static int rtw_adapter_proc_open(struct inode *inode, struct file *file)
{
	ssize_t index = (ssize_t)PDE_DATA(inode);
	const struct rtw_proc_hdl *hdl = adapter_proc_hdls + index;
	void *private = proc_get_parent_data(inode);

	if (hdl->type == RTW_PROC_HDL_TYPE_SEQ) {
		int res = seq_open(file, hdl->u.seq_op);

		if (res == 0)
			((struct seq_file *)file->private_data)->private = private;

		return res;
	} else if (hdl->type == RTW_PROC_HDL_TYPE_SSEQ) {
		int (*show)(struct seq_file *, void *) = hdl->u.show ? hdl->u.show : proc_get_dummy;

		return single_open(file, show, private);
	} else {
		return -EROFS;
	}
}

static ssize_t rtw_adapter_proc_write(struct file *file, const char __user *buffer, size_t count, loff_t *pos)
{
	ssize_t index = (ssize_t)PDE_DATA(file_inode(file));
	const struct rtw_proc_hdl *hdl = adapter_proc_hdls + index;
	ssize_t (*write)(struct file *, const char __user *, size_t, loff_t *, void *) = hdl->write;

	if (write)
		return write(file, buffer, count, pos, ((struct seq_file *)file->private_data)->private);

	return -EROFS;
}

static const struct file_operations rtw_adapter_proc_seq_fops = {
	.owner = THIS_MODULE,
	.open = rtw_adapter_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
	.write = rtw_adapter_proc_write,
};

static const struct file_operations rtw_adapter_proc_sseq_fops = {
	.owner = THIS_MODULE,
	.open = rtw_adapter_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = rtw_adapter_proc_write,
};

int proc_get_odm_adaptivity(struct seq_file *m, void *v)
{
	struct net_device *dev = m->private;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);

	rtw_odm_adaptivity_parm_msg(m, padapter);

	return 0;
}

ssize_t proc_set_odm_adaptivity(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *dev = data;
	_adapter *padapter = (_adapter *)rtw_netdev_priv(dev);
	char tmp[32];
	u32 th_l2h_ini;
	u32 th_l2h_ini_mode2;
	s8 th_edcca_hl_diff;
	s8 th_edcca_hl_diff_mode2;
	u8 edcca_enable;

	if (count < 1)
		return -EFAULT;

	if (count > sizeof(tmp)) {
		rtw_warn_on(1);
		return -EFAULT;
	}

	if (buffer && !copy_from_user(tmp, buffer, count)) {

		int num = sscanf(tmp, "%x %hhd %x %hhd %hhu", &th_l2h_ini, &th_edcca_hl_diff, &th_l2h_ini_mode2, &th_edcca_hl_diff_mode2, &edcca_enable);

		if (num != 5)
			return count;

		rtw_odm_adaptivity_parm_set(padapter, (s8)th_l2h_ini, th_edcca_hl_diff, (s8)th_l2h_ini_mode2, th_edcca_hl_diff_mode2, edcca_enable);
	}

	return count;
}

static char *phydm_msg = NULL;
#define PHYDM_MSG_LEN	80*24

int proc_get_phydm_cmd(struct seq_file *m, void *v)
{
	struct net_device *netdev;
	PADAPTER padapter;
	struct PHY_DM_STRUCT *phydm;


	netdev = m->private;
	padapter = (PADAPTER)rtw_netdev_priv(netdev);
	phydm = adapter_to_phydm(padapter);

	if (NULL == phydm_msg) {
		phydm_msg = rtw_zmalloc(PHYDM_MSG_LEN);
		if (NULL == phydm_msg)
			return -ENOMEM;

		phydm_cmd(phydm, NULL, 0, 0, phydm_msg, PHYDM_MSG_LEN);
	}

	_RTW_PRINT_SEL(m, "%s\n", phydm_msg);

	rtw_mfree(phydm_msg, PHYDM_MSG_LEN);
	phydm_msg = NULL;

	return 0;
}

ssize_t proc_set_phydm_cmd(struct file *file, const char __user *buffer, size_t count, loff_t *pos, void *data)
{
	struct net_device *netdev;
	PADAPTER padapter;
	struct PHY_DM_STRUCT *phydm;
	char tmp[64] = {0};


	netdev = (struct net_device *)data;
	padapter = (PADAPTER)rtw_netdev_priv(netdev);
	phydm = adapter_to_phydm(padapter);

	if (count < 1)
		return -EFAULT;

	if (count > sizeof(tmp))
		return -EFAULT;

	if (buffer && !copy_from_user(tmp, buffer, count)) {
		if (NULL == phydm_msg) {
			phydm_msg = rtw_zmalloc(PHYDM_MSG_LEN);
			if (NULL == phydm_msg)
				return -ENOMEM;
		} else
			_rtw_memset(phydm_msg, 0, PHYDM_MSG_LEN);

		phydm_cmd(phydm, tmp, count, 1, phydm_msg, PHYDM_MSG_LEN);

		if (strlen(phydm_msg) == 0) {
			rtw_mfree(phydm_msg, PHYDM_MSG_LEN);
			phydm_msg = NULL;
		}
	}

	return count;
}

/*
* rtw_odm_proc:
* init/deinit when register/unregister net_device, along with rtw_adapter_proc
*/
const struct rtw_proc_hdl odm_proc_hdls[] = {
	RTW_PROC_HDL_SSEQ("adaptivity", proc_get_odm_adaptivity, proc_set_odm_adaptivity),
	RTW_PROC_HDL_SSEQ("cmd", proc_get_phydm_cmd, proc_set_phydm_cmd),
};

const int odm_proc_hdls_num = sizeof(odm_proc_hdls) / sizeof(struct rtw_proc_hdl);

static int rtw_odm_proc_open(struct inode *inode, struct file *file)
{
	ssize_t index = (ssize_t)PDE_DATA(inode);
	const struct rtw_proc_hdl *hdl = odm_proc_hdls + index;
	void *private = proc_get_parent_data(inode);

	if (hdl->type == RTW_PROC_HDL_TYPE_SEQ) {
		int res = seq_open(file, hdl->u.seq_op);

		if (res == 0)
			((struct seq_file *)file->private_data)->private = private;

		return res;
	} else if (hdl->type == RTW_PROC_HDL_TYPE_SSEQ) {
		int (*show)(struct seq_file *, void *) = hdl->u.show ? hdl->u.show : proc_get_dummy;

		return single_open(file, show, private);
	} else {
		return -EROFS;
	}
}

static ssize_t rtw_odm_proc_write(struct file *file, const char __user *buffer, size_t count, loff_t *pos)
{
	ssize_t index = (ssize_t)PDE_DATA(file_inode(file));
	const struct rtw_proc_hdl *hdl = odm_proc_hdls + index;
	ssize_t (*write)(struct file *, const char __user *, size_t, loff_t *, void *) = hdl->write;

	if (write)
		return write(file, buffer, count, pos, ((struct seq_file *)file->private_data)->private);

	return -EROFS;
}

static const struct file_operations rtw_odm_proc_seq_fops = {
	.owner = THIS_MODULE,
	.open = rtw_odm_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
	.write = rtw_odm_proc_write,
};

static const struct file_operations rtw_odm_proc_sseq_fops = {
	.owner = THIS_MODULE,
	.open = rtw_odm_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = rtw_odm_proc_write,
};

struct proc_dir_entry *rtw_odm_proc_init(struct net_device *dev)
{
	struct proc_dir_entry *dir_odm = NULL;
	struct proc_dir_entry *entry = NULL;
	_adapter	*adapter = rtw_netdev_priv(dev);
	ssize_t i;

	if (adapter->dir_dev == NULL) {
		rtw_warn_on(1);
		goto exit;
	}

	if (adapter->dir_odm != NULL) {
		rtw_warn_on(1);
		goto exit;
	}

	dir_odm = rtw_proc_create_dir("odm", adapter->dir_dev, dev);
	if (dir_odm == NULL) {
		rtw_warn_on(1);
		goto exit;
	}

	adapter->dir_odm = dir_odm;

	for (i = 0; i < odm_proc_hdls_num; i++) {
		if (odm_proc_hdls[i].type == RTW_PROC_HDL_TYPE_SEQ)
			entry = rtw_proc_create_entry(odm_proc_hdls[i].name, dir_odm, &rtw_odm_proc_seq_fops, (void *)i);
		else if (odm_proc_hdls[i].type == RTW_PROC_HDL_TYPE_SSEQ)
			entry = rtw_proc_create_entry(odm_proc_hdls[i].name, dir_odm, &rtw_odm_proc_sseq_fops, (void *)i);
		else
			entry = NULL;

		if (!entry) {
			rtw_warn_on(1);
			goto exit;
		}
	}

exit:
	return dir_odm;
}

void rtw_odm_proc_deinit(_adapter	*adapter)
{
	struct proc_dir_entry *dir_odm = NULL;
	int i;

	dir_odm = adapter->dir_odm;

	if (dir_odm == NULL) {
		rtw_warn_on(1);
		return;
	}

	for (i = 0; i < odm_proc_hdls_num; i++)
		remove_proc_entry(odm_proc_hdls[i].name, dir_odm);

	remove_proc_entry("odm", adapter->dir_dev);

	adapter->dir_odm = NULL;

	if (phydm_msg) {
		rtw_mfree(phydm_msg, PHYDM_MSG_LEN);
		phydm_msg = NULL;
	}
}

#ifdef CONFIG_MCC_MODE
/*
* rtw_mcc_proc:
* init/deinit when register/unregister net_device, along with rtw_adapter_proc
*/
const struct rtw_proc_hdl mcc_proc_hdls[] = {
	RTW_PROC_HDL_SSEQ("mcc_info", proc_get_mcc_info, NULL),
	RTW_PROC_HDL_SSEQ("mcc_enable", proc_get_mcc_info, proc_set_mcc_enable),
	RTW_PROC_HDL_SSEQ("mcc_single_tx_criteria", proc_get_mcc_info, proc_set_mcc_single_tx_criteria),
	RTW_PROC_HDL_SSEQ("mcc_ap_bw20_target_tp", proc_get_mcc_info, proc_set_mcc_ap_bw20_target_tp),
	RTW_PROC_HDL_SSEQ("mcc_ap_bw40_target_tp", proc_get_mcc_info, proc_set_mcc_ap_bw40_target_tp),
	RTW_PROC_HDL_SSEQ("mcc_ap_bw80_target_tp", proc_get_mcc_info, proc_set_mcc_ap_bw80_target_tp),
	RTW_PROC_HDL_SSEQ("mcc_sta_bw20_target_tp", proc_get_mcc_info, proc_set_mcc_sta_bw20_target_tp),
	RTW_PROC_HDL_SSEQ("mcc_sta_bw40_target_tp", proc_get_mcc_info, proc_set_mcc_sta_bw40_target_tp),
	RTW_PROC_HDL_SSEQ("mcc_sta_bw80_target_tp", proc_get_mcc_info, proc_set_mcc_sta_bw80_target_tp),
	RTW_PROC_HDL_SSEQ("mcc_policy_table", proc_get_mcc_policy_table, proc_set_mcc_policy_table),
};

const int mcc_proc_hdls_num = sizeof(mcc_proc_hdls) / sizeof(struct rtw_proc_hdl);

static int rtw_mcc_proc_open(struct inode *inode, struct file *file)
{
	ssize_t index = (ssize_t)PDE_DATA(inode);
	const struct rtw_proc_hdl *hdl = mcc_proc_hdls + index;
	void *private = proc_get_parent_data(inode);

	if (hdl->type == RTW_PROC_HDL_TYPE_SEQ) {
		int res = seq_open(file, hdl->u.seq_op);

		if (res == 0)
			((struct seq_file *)file->private_data)->private = private;

		return res;
	} else if (hdl->type == RTW_PROC_HDL_TYPE_SSEQ) {
		int (*show)(struct seq_file *, void *) = hdl->u.show ? hdl->u.show : proc_get_dummy;

		return single_open(file, show, private);
	} else {
		return -EROFS;
	}
}

static ssize_t rtw_mcc_proc_write(struct file *file, const char __user *buffer, size_t count, loff_t *pos)
{
	ssize_t index = (ssize_t)PDE_DATA(file_inode(file));
	const struct rtw_proc_hdl *hdl = mcc_proc_hdls + index;
	ssize_t (*write)(struct file *, const char __user *, size_t, loff_t *, void *) = hdl->write;

	if (write)
		return write(file, buffer, count, pos, ((struct seq_file *)file->private_data)->private);

	return -EROFS;
}

static const struct file_operations rtw_mcc_proc_seq_fops = {
	.owner = THIS_MODULE,
	.open = rtw_mcc_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
	.write = rtw_mcc_proc_write,
};

static const struct file_operations rtw_mcc_proc_sseq_fops = {
	.owner = THIS_MODULE,
	.open = rtw_mcc_proc_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.write = rtw_mcc_proc_write,
};

struct proc_dir_entry *rtw_mcc_proc_init(struct net_device *dev)
{
	struct proc_dir_entry *dir_mcc = NULL;
	struct proc_dir_entry *entry = NULL;
	_adapter	*adapter = rtw_netdev_priv(dev);
	ssize_t i;

	if (adapter->dir_dev == NULL) {
		rtw_warn_on(1);
		goto exit;
	}

	if (adapter->dir_mcc != NULL) {
		rtw_warn_on(1);
		goto exit;
	}

	dir_mcc = rtw_proc_create_dir("mcc", adapter->dir_dev, dev);
	if (dir_mcc == NULL) {
		rtw_warn_on(1);
		goto exit;
	}

	adapter->dir_mcc = dir_mcc;

	for (i = 0; i < mcc_proc_hdls_num; i++) {
		if (mcc_proc_hdls[i].type == RTW_PROC_HDL_TYPE_SEQ)
			entry = rtw_proc_create_entry(mcc_proc_hdls[i].name, dir_mcc, &rtw_mcc_proc_seq_fops, (void *)i);
		else if (mcc_proc_hdls[i].type == RTW_PROC_HDL_TYPE_SSEQ)
			entry = rtw_proc_create_entry(mcc_proc_hdls[i].name, dir_mcc, &rtw_mcc_proc_sseq_fops, (void *)i);
		else
			entry = NULL;

		if (!entry) {
			rtw_warn_on(1);
			goto exit;
		}
	}

exit:
	return dir_mcc;
}

void rtw_mcc_proc_deinit(_adapter	*adapter)
{
	struct proc_dir_entry *dir_mcc = NULL;
	int i;

	dir_mcc = adapter->dir_mcc;

	if (dir_mcc == NULL) {
		rtw_warn_on(1);
		return;
	}

	for (i = 0; i < mcc_proc_hdls_num; i++)
		remove_proc_entry(mcc_proc_hdls[i].name, dir_mcc);

	remove_proc_entry("mcc", adapter->dir_dev);

	adapter->dir_mcc = NULL;
}
#endif /* CONFIG_MCC_MODE */

struct proc_dir_entry *rtw_adapter_proc_init(struct net_device *dev)
{
	struct proc_dir_entry *drv_proc = get_rtw_drv_proc();
	struct proc_dir_entry *dir_dev = NULL;
	struct proc_dir_entry *entry = NULL;
	_adapter *adapter = rtw_netdev_priv(dev);
	u8 rf_type;
	ssize_t i;

	if (drv_proc == NULL) {
		rtw_warn_on(1);
		goto exit;
	}

	if (adapter->dir_dev != NULL) {
		rtw_warn_on(1);
		goto exit;
	}

	dir_dev = rtw_proc_create_dir(dev->name, drv_proc, dev);
	if (dir_dev == NULL) {
		rtw_warn_on(1);
		goto exit;
	}

	adapter->dir_dev = dir_dev;

	for (i = 0; i < adapter_proc_hdls_num; i++) {
		if (adapter_proc_hdls[i].type == RTW_PROC_HDL_TYPE_SEQ)
			entry = rtw_proc_create_entry(adapter_proc_hdls[i].name, dir_dev, &rtw_adapter_proc_seq_fops, (void *)i);
		else if (adapter_proc_hdls[i].type == RTW_PROC_HDL_TYPE_SSEQ)
			entry = rtw_proc_create_entry(adapter_proc_hdls[i].name, dir_dev, &rtw_adapter_proc_sseq_fops, (void *)i);
		else
			entry = NULL;

		if (!entry) {
			rtw_warn_on(1);
			goto exit;
		}
	}

	rtw_odm_proc_init(dev);

#ifdef CONFIG_MCC_MODE
	rtw_mcc_proc_init(dev);
#endif /* CONFIG_MCC_MODE */

exit:
	return dir_dev;
}

void rtw_adapter_proc_deinit(struct net_device *dev)
{
	struct proc_dir_entry *drv_proc = get_rtw_drv_proc();
	struct proc_dir_entry *dir_dev = NULL;
	_adapter *adapter = rtw_netdev_priv(dev);
	int i;

	dir_dev = adapter->dir_dev;

	if (dir_dev == NULL) {
		rtw_warn_on(1);
		return;
	}

	for (i = 0; i < adapter_proc_hdls_num; i++)
		remove_proc_entry(adapter_proc_hdls[i].name, dir_dev);

	rtw_odm_proc_deinit(adapter);

#ifdef CONFIG_MCC_MODE
	rtw_mcc_proc_deinit(adapter);
#endif /* CONFIG_MCC_MODE */

	remove_proc_entry(dev->name, drv_proc);

	adapter->dir_dev = NULL;
}

void rtw_adapter_proc_replace(struct net_device *dev)
{
	struct proc_dir_entry *drv_proc = get_rtw_drv_proc();
	struct proc_dir_entry *dir_dev = NULL;
	_adapter *adapter = rtw_netdev_priv(dev);
	int i;

	dir_dev = adapter->dir_dev;

	if (dir_dev == NULL) {
		rtw_warn_on(1);
		return;
	}

	for (i = 0; i < adapter_proc_hdls_num; i++)
		remove_proc_entry(adapter_proc_hdls[i].name, dir_dev);

	rtw_odm_proc_deinit(adapter);

#ifdef CONFIG_MCC_MODE
	rtw_mcc_proc_deinit(adapter);
#endif /* CONIG_MCC_MODE */

	remove_proc_entry(adapter->old_ifname, drv_proc);

	adapter->dir_dev = NULL;

	rtw_adapter_proc_init(dev);

}

#endif /* CONFIG_PROC_DEBUG */
