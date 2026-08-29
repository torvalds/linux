// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2020 Intel Corporation

#include <linux/debugfs.h>

#include "ufs-debugfs.h"
#include <ufs/ufshcd.h>
#include "ufshcd-priv.h"

static struct dentry *ufs_debugfs_root;

struct ufs_debugfs_attr {
	const char			*name;
	mode_t				mode;
	const struct file_operations	*fops;
};

/* @file corresponds to a debugfs attribute in directory hba->debugfs_root. */
static inline struct ufs_hba *hba_from_file(const struct file *file)
{
	return d_inode(file->f_path.dentry->d_parent)->i_private;
}

void __init ufs_debugfs_init(void)
{
	ufs_debugfs_root = debugfs_create_dir("ufshcd", NULL);
}

void ufs_debugfs_exit(void)
{
	debugfs_remove_recursive(ufs_debugfs_root);
}

static int ufs_debugfs_stats_show(struct seq_file *s, void *data)
{
	struct ufs_hba *hba = hba_from_file(s->file);
	struct ufs_event_hist *e = hba->ufs_stats.event;

#define PRT(fmt, typ) \
	seq_printf(s, fmt, e[UFS_EVT_ ## typ].cnt)

	PRT("PHY Adapter Layer errors (except LINERESET): %llu\n", PA_ERR);
	PRT("Data Link Layer errors: %llu\n", DL_ERR);
	PRT("Network Layer errors: %llu\n", NL_ERR);
	PRT("Transport Layer errors: %llu\n", TL_ERR);
	PRT("Generic DME errors: %llu\n", DME_ERR);
	PRT("Auto-hibernate errors: %llu\n", AUTO_HIBERN8_ERR);
	PRT("IS Fatal errors (CEFES, SBFES, HCFES, DFES): %llu\n", FATAL_ERR);
	PRT("DME Link Startup errors: %llu\n", LINK_STARTUP_FAIL);
	PRT("PM Resume errors: %llu\n", RESUME_ERR);
	PRT("PM Suspend errors : %llu\n", SUSPEND_ERR);
	PRT("Logical Unit Resets: %llu\n", DEV_RESET);
	PRT("Host Resets: %llu\n", HOST_RESET);
	PRT("SCSI command aborts: %llu\n", ABORT);
#undef PRT
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(ufs_debugfs_stats);

static int ee_usr_mask_get(void *data, u64 *val)
{
	struct ufs_hba *hba = data;

	*val = hba->ee_usr_mask;
	return 0;
}

token_context_lock(ufs_debugfs);

static int ufs_debugfs_get_user_access(struct ufs_hba *hba)
	__cond_acquires(0, ufs_debugfs)
{
	down(&hba->host_sem);
	if (!ufshcd_is_user_access_allowed(hba)) {
		up(&hba->host_sem);
		return -EBUSY;
	}
	ufshcd_rpm_get_sync(hba);
	__acquire(ufs_debugfs);
	return 0;
}

static void ufs_debugfs_put_user_access(struct ufs_hba *hba)
	__releases(ufs_debugfs)
{
	ufshcd_rpm_put_sync(hba);
	up(&hba->host_sem);
	__release(ufs_debugfs);
}

static int ee_usr_mask_set(void *data, u64 val)
{
	struct ufs_hba *hba = data;
	int err;

	if (val & ~(u64)MASK_EE_STATUS)
		return -EINVAL;
	err = ufs_debugfs_get_user_access(hba);
	if (err)
		return err;
	err = ufshcd_update_ee_usr_mask(hba, val, MASK_EE_STATUS);
	ufs_debugfs_put_user_access(hba);
	return err;
}

DEFINE_DEBUGFS_ATTRIBUTE(ee_usr_mask_fops, ee_usr_mask_get, ee_usr_mask_set, "%#llx\n");

void ufs_debugfs_exception_event(struct ufs_hba *hba, u16 status)
{
	bool chgd = false;
	u16 ee_ctrl_mask;
	int err = 0;

	if (!hba->debugfs_ee_rate_limit_ms || !status)
		return;

	mutex_lock(&hba->ee_ctrl_mutex);
	ee_ctrl_mask = hba->ee_drv_mask | (hba->ee_usr_mask & ~status);
	chgd = ee_ctrl_mask != hba->ee_ctrl_mask;
	if (chgd) {
		err = __ufshcd_write_ee_control(hba, ee_ctrl_mask);
		if (err)
			dev_err(hba->dev, "%s: failed to write ee control %d\n",
				__func__, err);
	}
	mutex_unlock(&hba->ee_ctrl_mutex);

	if (chgd && !err) {
		unsigned long delay = msecs_to_jiffies(hba->debugfs_ee_rate_limit_ms);

		queue_delayed_work(system_freezable_wq, &hba->debugfs_ee_work, delay);
	}
}

static void ufs_debugfs_restart_ee(struct work_struct *work)
{
	struct ufs_hba *hba = container_of(work, struct ufs_hba, debugfs_ee_work.work);

	if (!hba->ee_usr_mask || pm_runtime_suspended(hba->dev) ||
	    ufs_debugfs_get_user_access(hba))
		return;
	ufshcd_write_ee_control(hba);
	ufs_debugfs_put_user_access(hba);
}

static int ufs_saved_err_show(struct seq_file *s, void *data)
{
	struct ufs_debugfs_attr *attr = s->private;
	struct ufs_hba *hba = hba_from_file(s->file);
	const int *p;

	if (strcmp(attr->name, "saved_err") == 0) {
		p = &hba->saved_err;
	} else if (strcmp(attr->name, "saved_uic_err") == 0) {
		p = &hba->saved_uic_err;
	} else {
		return -ENOENT;
	}

	seq_printf(s, "%d\n", *p);
	return 0;
}

static ssize_t ufs_saved_err_write(struct file *file, const char __user *buf,
				   size_t count, loff_t *ppos)
{
	struct ufs_debugfs_attr *attr = file->f_inode->i_private;
	struct ufs_hba *hba = hba_from_file(file);
	char val_str[16] = { };
	int val, ret;

	if (count >= sizeof(val_str))
		return -EINVAL;
	if (copy_from_user(val_str, buf, count))
		return -EFAULT;
	ret = kstrtoint(val_str, 0, &val);
	if (ret < 0)
		return ret;

	spin_lock_irq(hba->host->host_lock);
	if (strcmp(attr->name, "saved_err") == 0) {
		hba->saved_err = val;
	} else if (strcmp(attr->name, "saved_uic_err") == 0) {
		hba->saved_uic_err = val;
	} else {
		ret = -ENOENT;
	}
	if (ret == 0)
		ufshcd_schedule_eh_work(hba);
	spin_unlock_irq(hba->host->host_lock);

	return ret < 0 ? ret : count;
}

static int ufs_saved_err_open(struct inode *inode, struct file *file)
{
	return single_open(file, ufs_saved_err_show, inode->i_private);
}

static const struct file_operations ufs_saved_err_fops = {
	.owner		= THIS_MODULE,
	.open		= ufs_saved_err_open,
	.read		= seq_read,
	.write		= ufs_saved_err_write,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct ufs_debugfs_attr ufs_attrs[] = {
	{ "stats", 0400, &ufs_debugfs_stats_fops },
	{ "saved_err", 0600, &ufs_saved_err_fops },
	{ "saved_uic_err", 0600, &ufs_saved_err_fops },
	{ }
};

static int ufs_tx_eq_params_show(struct seq_file *s, void *data)
{
	const char *file_name = s->file->f_path.dentry->d_name.name;
	u32 gear = (u32)(uintptr_t)s->file->f_inode->i_private;
	struct ufs_hba *hba = hba_from_file(s->file);
	struct ufshcd_tx_eq_settings *settings;
	struct ufs_pa_layer_attr *pwr_info;
	struct ufshcd_tx_eq_params *params;
	u32 rate = hba->pwr_info.hs_rate;
	u32 num_lanes;
	int lane;

	if (!ufshcd_is_tx_eq_supported(hba))
		return -EOPNOTSUPP;

	if (gear < UFS_HS_G1 || gear > UFS_HS_GEAR_MAX) {
		seq_printf(s, "Invalid gear selected: %u\n", gear);
		return 0;
	}

	if (!hba->max_pwr_info.is_valid) {
		seq_puts(s, "Max power info is invalid\n");
		return 0;
	}

	pwr_info = &hba->max_pwr_info.info;
	params = &hba->tx_eq_params[gear - 1];
	if (!params->is_valid) {
		seq_printf(s, "TX EQ params are invalid for HS-G%u, Rate-%s\n",
			   gear, ufs_hs_rate_to_str(rate));
		return 0;
	}

	if (strcmp(file_name, "host_tx_eq_params") == 0) {
		settings = params->host;
		num_lanes = pwr_info->lane_tx;
		seq_printf(s, "Host TX EQ PreShoot Cap: 0x%02x, DeEmphasis Cap: 0x%02x\n",
			   hba->host_preshoot_cap, hba->host_deemphasis_cap);
	} else if (strcmp(file_name, "device_tx_eq_params") == 0) {
		settings = params->device;
		num_lanes = pwr_info->lane_rx;
		seq_printf(s, "Device TX EQ PreShoot Cap: 0x%02x, DeEmphasis Cap: 0x%02x\n",
			   hba->device_preshoot_cap, hba->device_deemphasis_cap);
	} else {
		return -ENOENT;
	}

	seq_printf(s, "TX EQ setting for HS-G%u, Rate-%s:\n", gear,
		   ufs_hs_rate_to_str(rate));
	for (lane = 0; lane < num_lanes; lane++)
		seq_printf(s, "TX Lane %d - PreShoot: %d, DeEmphasis: %d, Pre-Coding %senabled\n",
			   lane, settings[lane].preshoot,
			   settings[lane].deemphasis,
			   settings[lane].precode_en ? "" : "not ");

	return 0;
}

static int ufs_tx_eq_params_open(struct inode *inode, struct file *file)
{
	return single_open(file, ufs_tx_eq_params_show, inode->i_private);
}

static const struct file_operations ufs_tx_eq_params_fops = {
	.owner		= THIS_MODULE,
	.open		= ufs_tx_eq_params_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static const struct ufs_debugfs_attr ufs_tx_eq_attrs[] = {
	{ "host_tx_eq_params", 0400, &ufs_tx_eq_params_fops },
	{ "device_tx_eq_params", 0400, &ufs_tx_eq_params_fops },
	{ }
};

static int ufs_tx_eqtr_record_show(struct seq_file *s, void *data)
{
	const char *file_name = s->file->f_path.dentry->d_name.name;
	u8 (*fom_array)[TX_HS_NUM_PRESHOOT][TX_HS_NUM_DEEMPHASIS];
	u32 gear = (u32)(uintptr_t)s->file->f_inode->i_private;
	unsigned long preshoot_bitmap, deemphasis_bitmap;
	struct ufs_hba *hba = hba_from_file(s->file);
	struct ufs_pa_layer_attr *pwr_info;
	struct ufshcd_tx_eq_params *params;
	struct ufshcd_tx_eqtr_record *rec;
	u32 rate = hba->pwr_info.hs_rate;
	u8 preshoot, deemphasis;
	u32 num_lanes;
	char name[32];
	int lane;

	if (!ufshcd_is_tx_eq_supported(hba))
		return -EOPNOTSUPP;

	if (gear < UFS_HS_G1 || gear > UFS_HS_GEAR_MAX) {
		seq_printf(s, "Invalid gear selected: %u\n", gear);
		return 0;
	}

	if (!hba->max_pwr_info.is_valid) {
		seq_puts(s, "Max power info is invalid\n");
		return 0;
	}

	pwr_info = &hba->max_pwr_info.info;
	params = &hba->tx_eq_params[gear - 1];
	if (!params->is_valid) {
		seq_printf(s, "TX EQ params are invalid for HS-G%u, Rate-%s\n",
			   gear, ufs_hs_rate_to_str(rate));
		return 0;
	}

	rec = params->eqtr_record;
	if (!rec || !rec->last_record_index) {
		seq_printf(s, "No TX EQTR records found for HS-G%u, Rate-%s.\n",
			   gear, ufs_hs_rate_to_str(rate));
		return 0;
	}

	if (strcmp(file_name, "host_tx_eqtr_record") == 0) {
		preshoot_bitmap = (hba->host_preshoot_cap << 0x1) | 0x1;
		deemphasis_bitmap = (hba->host_deemphasis_cap << 0x1) | 0x1;
		num_lanes = pwr_info->lane_tx;
		fom_array = rec->host_fom;
		snprintf(name, sizeof(name), "%s", "Host");
	} else if (strcmp(file_name, "device_tx_eqtr_record") == 0) {
		preshoot_bitmap = (hba->device_preshoot_cap << 0x1) | 0x1;
		deemphasis_bitmap = (hba->device_deemphasis_cap << 0x1) | 0x1;
		num_lanes = pwr_info->lane_rx;
		fom_array = rec->device_fom;
		snprintf(name, sizeof(name), "%s", "Device");
	} else {
		return -ENOENT;
	}

	seq_printf(s, "%s TX EQTR record summary -\n", name);
	seq_printf(s, "Target Power Mode: HS-G%u, Rate-%s\n", gear,
		   ufs_hs_rate_to_str(rate));
	seq_printf(s, "Most recent record index: %d\n",
		   rec->last_record_index);
	seq_printf(s, "Most recent record timestamp: %llu us\n",
		   ktime_to_us(rec->last_record_ts));

	for (lane = 0; lane < num_lanes; lane++) {
		seq_printf(s, "\nTX Lane %d FOM - %s\n", lane, "PreShoot\\DeEmphasis");
		seq_puts(s, "\\");
		/* Print DeEmphasis header as X-axis. */
		for (deemphasis = 0; deemphasis < TX_HS_NUM_DEEMPHASIS; deemphasis++)
			seq_printf(s, "%8d%s", deemphasis, " ");
		seq_puts(s, "\n");
		/* Print matrix rows with PreShoot as Y-axis. */
		for (preshoot = 0; preshoot < TX_HS_NUM_PRESHOOT; preshoot++) {
			seq_printf(s, "%d", preshoot);
			for (deemphasis = 0; deemphasis < TX_HS_NUM_DEEMPHASIS; deemphasis++) {
				if (test_bit(preshoot, &preshoot_bitmap) &&
				    test_bit(deemphasis, &deemphasis_bitmap)) {
					u8 fom = fom_array[lane][preshoot][deemphasis];
					u8 fom_val = fom & RX_FOM_VALUE_MASK;
					bool precode_en = fom & RX_FOM_PRECODING_EN_BIT;

					if (ufshcd_is_txeq_presets_used(hba) &&
					    !ufshcd_is_txeq_preset_selected(preshoot, deemphasis))
						seq_printf(s, "%8s%s", "-", " ");
					else
						seq_printf(s, "%8u%s", fom_val,
							   precode_en ? "*" : " ");
				} else {
					seq_printf(s, "%8s%s", "x", " ");
				}
			}
			seq_puts(s, "\n");
		}
	}

	return 0;
}

static int ufs_tx_eqtr_record_open(struct inode *inode, struct file *file)
{
	return single_open(file, ufs_tx_eqtr_record_show, inode->i_private);
}

static const struct file_operations ufs_tx_eqtr_record_fops = {
	.owner		= THIS_MODULE,
	.open		= ufs_tx_eqtr_record_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static ssize_t ufs_tx_eq_ctrl_write(struct file *file, const char __user *buf,
				    size_t count, loff_t *ppos)
{
	u32 gear = (u32)(uintptr_t)file->f_inode->i_private;
	struct ufs_hba *hba = hba_from_file(file);
	char kbuf[32];
	int ret;

	if (count >= sizeof(kbuf))
		return -EINVAL;

	if (copy_from_user(kbuf, buf, count))
		return -EFAULT;

	if (!ufshcd_is_tx_eq_supported(hba))
		return -EOPNOTSUPP;

	if (hba->ufshcd_state != UFSHCD_STATE_OPERATIONAL ||
	    !hba->max_pwr_info.is_valid)
		return -EBUSY;

	if (!hba->ufs_device_wlun)
		return -ENODEV;

	kbuf[count] = '\0';

	if (sysfs_streq(kbuf, "retrain")) {
		ret = ufs_debugfs_get_user_access(hba);
		if (ret)
			return ret;
		ret = ufshcd_retrain_tx_eq(hba, gear);
		ufs_debugfs_put_user_access(hba);
	} else {
		/* Unknown operation */
		return -EINVAL;
	}

	return ret ? ret : count;
}

static int ufs_tx_eq_ctrl_show(struct seq_file *s, void *data)
{
	seq_puts(s, "write 'retrain' to retrain TX Equalization settings\n");
	return 0;
}

static int ufs_tx_eq_ctrl_open(struct inode *inode, struct file *file)
{
	return single_open(file, ufs_tx_eq_ctrl_show, inode->i_private);
}

static const struct file_operations ufs_tx_eq_ctrl_fops = {
	.owner		= THIS_MODULE,
	.open		= ufs_tx_eq_ctrl_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.write		= ufs_tx_eq_ctrl_write,
	.release	= single_release,
};

static const struct ufs_debugfs_attr ufs_tx_eqtr_attrs[] = {
	{ "host_tx_eqtr_record", 0400, &ufs_tx_eqtr_record_fops },
	{ "device_tx_eqtr_record", 0400, &ufs_tx_eqtr_record_fops },
	{ "tx_eq_ctrl", 0600, &ufs_tx_eq_ctrl_fops },
	{ }
};

void ufs_debugfs_hba_init(struct ufs_hba *hba)
{
	const struct ufs_debugfs_attr *attr;
	struct dentry *root;

	/* Set default exception event rate limit period to 20ms */
	hba->debugfs_ee_rate_limit_ms = 20;
	INIT_DELAYED_WORK(&hba->debugfs_ee_work, ufs_debugfs_restart_ee);

	root = debugfs_create_dir(dev_name(hba->dev), ufs_debugfs_root);
	if (IS_ERR_OR_NULL(root))
		return;
	hba->debugfs_root = root;
	d_inode(root)->i_private = hba;
	for (attr = ufs_attrs; attr->name; attr++)
		debugfs_create_file(attr->name, attr->mode, root, (void *)attr,
				    attr->fops);
	debugfs_create_file("exception_event_mask", 0600, hba->debugfs_root,
			    hba, &ee_usr_mask_fops);
	debugfs_create_u32("exception_event_rate_limit_ms", 0600, hba->debugfs_root,
			   &hba->debugfs_ee_rate_limit_ms);

	if (!(hba->caps & UFSHCD_CAP_TX_EQUALIZATION))
		return;

	for (u32 gear = UFS_HS_G1; gear <= UFS_HS_GEAR_MAX; gear++) {
		struct dentry *txeq_dir;
		char name[32];

		snprintf(name, sizeof(name), "tx_eq_hs_gear%d", gear);
		txeq_dir = debugfs_create_dir(name, hba->debugfs_root);
		if (IS_ERR_OR_NULL(txeq_dir))
			return;

		d_inode(txeq_dir)->i_private = hba;

		/* Create files for TX Equalization parameters */
		for (attr = ufs_tx_eq_attrs; attr->name; attr++)
			debugfs_create_file(attr->name, attr->mode, txeq_dir,
					    (void *)(uintptr_t)gear,
					    attr->fops);

		/* TX EQTR is supported for HS-G4 and higher Gears */
		if (gear < UFS_HS_G4)
			continue;

		/* Create files for TX EQTR related attributes */
		for (attr = ufs_tx_eqtr_attrs; attr->name; attr++)
			debugfs_create_file(attr->name, attr->mode, txeq_dir,
					    (void *)(uintptr_t)gear,
					    attr->fops);
	}
}

void ufs_debugfs_hba_exit(struct ufs_hba *hba)
{
	debugfs_remove_recursive(hba->debugfs_root);
	cancel_delayed_work_sync(&hba->debugfs_ee_work);
}
