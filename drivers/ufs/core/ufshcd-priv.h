/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef _UFSHCD_PRIV_H_
#define _UFSHCD_PRIV_H_

#include <linux/pm_runtime.h>
#include <ufs/ufshcd.h>

static inline bool ufshcd_is_user_access_allowed(struct ufs_hba *hba)
{
	return !hba->shutting_down;
}

void ufshcd_schedule_eh_work(struct ufs_hba *hba);

static inline bool ufshcd_keep_autobkops_enabled_except_suspend(
							struct ufs_hba *hba)
{
	return hba->caps & UFSHCD_CAP_KEEP_AUTO_BKOPS_ENABLED_EXCEPT_SUSPEND;
}

static inline u8 ufshcd_wb_get_query_index(struct ufs_hba *hba)
{
	if (hba->dev_info.wb_buffer_type == WB_BUF_MODE_LU_DEDICATED)
		return hba->dev_info.wb_dedicated_lu;
	return 0;
}

static inline bool ufshcd_is_wb_buf_flush_allowed(struct ufs_hba *hba)
{
	return ufshcd_is_wb_allowed(hba) &&
		!(hba->quirks & UFSHCI_QUIRK_SKIP_MANUAL_WB_FLUSH_CTRL);
}

#ifdef CONFIG_SCSI_UFS_HWMON
void ufs_hwmon_probe(struct ufs_hba *hba, u8 mask);
void ufs_hwmon_remove(struct ufs_hba *hba);
void ufs_hwmon_notify_event(struct ufs_hba *hba, u8 ee_mask);
#else
static inline void ufs_hwmon_probe(struct ufs_hba *hba, u8 mask) {}
static inline void ufs_hwmon_remove(struct ufs_hba *hba) {}
static inline void ufs_hwmon_notify_event(struct ufs_hba *hba, u8 ee_mask) {}
#endif

int ufshcd_query_descriptor_retry(struct ufs_hba *hba,
				  enum query_opcode opcode,
				  enum desc_idn idn, u8 index,
				  u8 selector,
				  u8 *desc_buf, int *buf_len);
int ufshcd_read_desc_param(struct ufs_hba *hba,
			   enum desc_idn desc_id,
			   int desc_index,
			   u8 param_offset,
			   u8 *param_read_buf,
			   u8 param_size);
int ufshcd_query_attr_retry(struct ufs_hba *hba, enum query_opcode opcode,
			    enum attr_idn idn, u8 index, u8 selector,
			    u32 *attr_val);
int ufshcd_query_attr(struct ufs_hba *hba, enum query_opcode opcode,
		      enum attr_idn idn, u8 index, u8 selector, u32 *attr_val);
int ufshcd_query_flag(struct ufs_hba *hba, enum query_opcode opcode,
	enum flag_idn idn, u8 index, bool *flag_res);
void ufshcd_auto_hibern8_update(struct ufs_hba *hba, u32 ahit);
void ufshcd_compl_one_cqe(struct ufs_hba *hba, int task_tag,
			  struct cq_entry *cqe);
int ufshcd_mcq_init(struct ufs_hba *hba);
int ufshcd_mcq_decide_queue_depth(struct ufs_hba *hba);
int ufshcd_mcq_memory_alloc(struct ufs_hba *hba);
void ufshcd_mcq_make_queues_operational(struct ufs_hba *hba);
void ufshcd_mcq_config_mac(struct ufs_hba *hba, u32 max_active_cmds);
u32 ufshcd_mcq_read_cqis(struct ufs_hba *hba, int i);
void ufshcd_mcq_write_cqis(struct ufs_hba *hba, u32 val, int i);
struct ufs_hw_queue *ufshcd_mcq_req_to_hwq(struct ufs_hba *hba,
					   struct request *req);
unsigned long ufshcd_mcq_poll_cqe_lock(struct ufs_hba *hba,
				       struct ufs_hw_queue *hwq);
void ufshcd_mcq_compl_all_cqes_lock(struct ufs_hba *hba,
				    struct ufs_hw_queue *hwq);
bool ufshcd_cmd_inflight(struct scsi_cmnd *cmd);
int ufshcd_mcq_sq_cleanup(struct ufs_hba *hba, int task_tag);
int ufshcd_mcq_abort(struct scsi_cmnd *cmd);
int ufshcd_try_to_abort_task(struct ufs_hba *hba, int tag);
void ufshcd_release_scsi_cmd(struct ufs_hba *hba,
			     struct ufshcd_lrb *lrbp);

#define SD_ASCII_STD true
#define SD_RAW false
int ufshcd_read_string_desc(struct ufs_hba *hba, u8 desc_index,
			    u8 **buf, bool ascii);

int ufshcd_send_uic_cmd(struct ufs_hba *hba, struct uic_command *uic_cmd);

int ufshcd_exec_raw_upiu_cmd(struct ufs_hba *hba,
			     struct utp_upiu_req *req_upiu,
			     struct utp_upiu_req *rsp_upiu,
			     enum upiu_request_transaction msgcode,
			     u8 *desc_buff, int *buff_len,
			     enum query_opcode desc_op);

int ufshcd_wb_toggle(struct ufs_hba *hba, bool enable);

/* Wrapper functions for safely calling variant operations */
static inline const char *ufshcd_get_var_name(struct ufs_hba *hba)
{
	if (hba->vops)
		return hba->vops->name;
	return "";
}

static inline void ufshcd_vops_exit(struct ufs_hba *hba)
{
	if (hba->vops && hba->vops->exit)
		return hba->vops->exit(hba);
}

static inline u32 ufshcd_vops_get_ufs_hci_version(struct ufs_hba *hba)
{
	if (hba->vops && hba->vops->get_ufs_hci_version)
		return hba->vops->get_ufs_hci_version(hba);

	return ufshcd_readl(hba, REG_UFS_VERSION);
}

static inline int ufshcd_vops_clk_scale_notify(struct ufs_hba *hba,
			bool up, enum ufs_notify_change_status status)
{
	if (hba->vops && hba->vops->clk_scale_notify)
		return hba->vops->clk_scale_notify(hba, up, status);
	return 0;
}

static inline void ufshcd_vops_event_notify(struct ufs_hba *hba,
					    enum ufs_event_type evt,
					    void *data)
{
	if (hba->vops && hba->vops->event_notify)
		hba->vops->event_notify(hba, evt, data);
}

static inline int ufshcd_vops_setup_clocks(struct ufs_hba *hba, bool on,
					enum ufs_notify_change_status status)
{
	if (hba->vops && hba->vops->setup_clocks)
		return hba->vops->setup_clocks(hba, on, status);
	return 0;
}

static inline int ufshcd_vops_hce_enable_notify(struct ufs_hba *hba,
						bool status)
{
	if (hba->vops && hba->vops->hce_enable_notify)
		return hba->vops->hce_enable_notify(hba, status);

	return 0;
}
static inline int ufshcd_vops_link_startup_notify(struct ufs_hba *hba,
						bool status)
{
	if (hba->vops && hba->vops->link_startup_notify)
		return hba->vops->link_startup_notify(hba, status);

	return 0;
}

static inline int ufshcd_vops_pwr_change_notify(struct ufs_hba *hba,
				  enum ufs_notify_change_status status,
				  struct ufs_pa_layer_attr *dev_max_params,
				  struct ufs_pa_layer_attr *dev_req_params)
{
	if (hba->vops && hba->vops->pwr_change_notify)
		return hba->vops->pwr_change_notify(hba, status,
					dev_max_params, dev_req_params);

	return -ENOTSUPP;
}

static inline void ufshcd_vops_setup_task_mgmt(struct ufs_hba *hba,
					int tag, u8 tm_function)
{
	if (hba->vops && hba->vops->setup_task_mgmt)
		return hba->vops->setup_task_mgmt(hba, tag, tm_function);
}

static inline void ufshcd_vops_hibern8_notify(struct ufs_hba *hba,
					enum uic_cmd_dme cmd,
					enum ufs_notify_change_status status)
{
	if (hba->vops && hba->vops->hibern8_notify)
		return hba->vops->hibern8_notify(hba, cmd, status);
}

static inline int ufshcd_vops_apply_dev_quirks(struct ufs_hba *hba)
{
	if (hba->vops && hba->vops->apply_dev_quirks)
		return hba->vops->apply_dev_quirks(hba);
	return 0;
}

static inline void ufshcd_vops_fixup_dev_quirks(struct ufs_hba *hba)
{
	if (hba->vops && hba->vops->fixup_dev_quirks)
		hba->vops->fixup_dev_quirks(hba);
}

static inline int ufshcd_vops_suspend(struct ufs_hba *hba, enum ufs_pm_op op,
				enum ufs_notify_change_status status)
{
	if (hba->vops && hba->vops->suspend)
		return hba->vops->suspend(hba, op, status);

	return 0;
}

static inline int ufshcd_vops_resume(struct ufs_hba *hba, enum ufs_pm_op op)
{
	if (hba->vops && hba->vops->resume)
		return hba->vops->resume(hba, op);

	return 0;
}

static inline void ufshcd_vops_dbg_register_dump(struct ufs_hba *hba)
{
	if (hba->vops && hba->vops->dbg_register_dump)
		hba->vops->dbg_register_dump(hba);
}

static inline int ufshcd_vops_device_reset(struct ufs_hba *hba)
{
	if (hba->vops && hba->vops->device_reset)
		return hba->vops->device_reset(hba);

	return -EOPNOTSUPP;
}

static inline void ufshcd_vops_config_scaling_param(struct ufs_hba *hba,
		struct devfreq_dev_profile *p,
		struct devfreq_simple_ondemand_data *data)
{
	if (hba->vops && hba->vops->config_scaling_param)
		hba->vops->config_scaling_param(hba, p, data);
}

static inline void ufshcd_vops_reinit_notify(struct ufs_hba *hba)
{
	if (hba->vops && hba->vops->reinit_notify)
		hba->vops->reinit_notify(hba);
}

static inline int ufshcd_vops_mcq_config_resource(struct ufs_hba *hba)
{
	if (hba->vops && hba->vops->mcq_config_resource)
		return hba->vops->mcq_config_resource(hba);

	return -EOPNOTSUPP;
}

static inline int ufshcd_mcq_vops_get_hba_mac(struct ufs_hba *hba)
{
	if (hba->vops && hba->vops->get_hba_mac)
		return hba->vops->get_hba_mac(hba);

	return -EOPNOTSUPP;
}

static inline int ufshcd_mcq_vops_op_runtime_config(struct ufs_hba *hba)
{
	if (hba->vops && hba->vops->op_runtime_config)
		return hba->vops->op_runtime_config(hba);

	return -EOPNOTSUPP;
}

static inline int ufshcd_vops_get_outstanding_cqs(struct ufs_hba *hba,
						  unsigned long *ocqs)
{
	if (hba->vops && hba->vops->get_outstanding_cqs)
		return hba->vops->get_outstanding_cqs(hba, ocqs);

	return -EOPNOTSUPP;
}

static inline int ufshcd_mcq_vops_config_esi(struct ufs_hba *hba)
{
	if (hba->vops && hba->vops->config_esi)
		return hba->vops->config_esi(hba);

	return -EOPNOTSUPP;
}

extern const struct ufs_pm_lvl_states ufs_pm_lvl_states[];

/**
 * ufshcd_scsi_to_upiu_lun - maps scsi LUN to UPIU LUN
 * @scsi_lun: scsi LUN id
 *
 * Return: UPIU LUN id
 */
static inline u8 ufshcd_scsi_to_upiu_lun(unsigned int scsi_lun)
{
	if (scsi_is_wlun(scsi_lun))
		return (scsi_lun & UFS_UPIU_MAX_UNIT_NUM_ID)
			| UFS_UPIU_WLUN_ID;
	else
		return scsi_lun & UFS_UPIU_MAX_UNIT_NUM_ID;
}

int __ufshcd_write_ee_control(struct ufs_hba *hba, u32 ee_ctrl_mask);
int ufshcd_write_ee_control(struct ufs_hba *hba);
int ufshcd_update_ee_control(struct ufs_hba *hba, u16 *mask,
			     const u16 *other_mask, u16 set, u16 clr);

static inline int ufshcd_update_ee_drv_mask(struct ufs_hba *hba,
					    u16 set, u16 clr)
{
	return ufshcd_update_ee_control(hba, &hba->ee_drv_mask,
					&hba->ee_usr_mask, set, clr);
}

static inline int ufshcd_update_ee_usr_mask(struct ufs_hba *hba,
					    u16 set, u16 clr)
{
	return ufshcd_update_ee_control(hba, &hba->ee_usr_mask,
					&hba->ee_drv_mask, set, clr);
}

static inline int ufshcd_rpm_get_sync(struct ufs_hba *hba)
{
	return pm_runtime_get_sync(&hba->ufs_device_wlun->sdev_gendev);
}

static inline int ufshcd_rpm_put_sync(struct ufs_hba *hba)
{
	return pm_runtime_put_sync(&hba->ufs_device_wlun->sdev_gendev);
}

static inline void ufshcd_rpm_get_noresume(struct ufs_hba *hba)
{
	pm_runtime_get_noresume(&hba->ufs_device_wlun->sdev_gendev);
}

static inline int ufshcd_rpm_resume(struct ufs_hba *hba)
{
	return pm_runtime_resume(&hba->ufs_device_wlun->sdev_gendev);
}

static inline int ufshcd_rpm_put(struct ufs_hba *hba)
{
	return pm_runtime_put(&hba->ufs_device_wlun->sdev_gendev);
}

/**
 * ufs_is_valid_unit_desc_lun - checks if the given LUN has a unit descriptor
 * @dev_info: pointer of instance of struct ufs_dev_info
 * @lun: LU number to check
 * @return: true if the lun has a matching unit descriptor, false otherwise
 */
static inline bool ufs_is_valid_unit_desc_lun(struct ufs_dev_info *dev_info, u8 lun)
{
	if (!dev_info || !dev_info->max_lu_supported) {
		pr_err("Max General LU supported by UFS isn't initialized\n");
		return false;
	}
	return lun == UFS_UPIU_RPMB_WLUN || (lun < dev_info->max_lu_supported);
}

static inline void ufshcd_inc_sq_tail(struct ufs_hw_queue *q)
	__must_hold(&q->sq_lock)
{
	u32 val;

	q->sq_tail_slot++;
	if (q->sq_tail_slot == q->max_entries)
		q->sq_tail_slot = 0;
	val = q->sq_tail_slot * sizeof(struct utp_transfer_req_desc);
	writel(val, q->mcq_sq_tail);
}

static inline void ufshcd_mcq_update_cq_tail_slot(struct ufs_hw_queue *q)
{
	u32 val = readl(q->mcq_cq_tail);

	q->cq_tail_slot = val / sizeof(struct cq_entry);
}

static inline bool ufshcd_mcq_is_cq_empty(struct ufs_hw_queue *q)
{
	return q->cq_head_slot == q->cq_tail_slot;
}

static inline void ufshcd_mcq_inc_cq_head_slot(struct ufs_hw_queue *q)
{
	q->cq_head_slot++;
	if (q->cq_head_slot == q->max_entries)
		q->cq_head_slot = 0;
}

static inline void ufshcd_mcq_update_cq_head(struct ufs_hw_queue *q)
{
	writel(q->cq_head_slot * sizeof(struct cq_entry), q->mcq_cq_head);
}

static inline struct cq_entry *ufshcd_mcq_cur_cqe(struct ufs_hw_queue *q)
{
	struct cq_entry *cqe = q->cqe_base_addr;

	return cqe + q->cq_head_slot;
}

static inline u32 ufshcd_mcq_get_sq_head_slot(struct ufs_hw_queue *q)
{
	u32 val = readl(q->mcq_sq_head);

	return val / sizeof(struct utp_transfer_req_desc);
}

#endif /* _UFSHCD_PRIV_H_ */
