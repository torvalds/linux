// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.

#include <asm/arch_timer.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/esoc_client.h>
#include <linux/interconnect.h>
#include <linux/list.h>
#include <linux/of.h>
#include <linux/memblock.h>
#include <linux/module.h>
#include <linux/msm_pcie.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/mhi.h>
#include <linux/mhi_misc.h>
#include "mhi_qcom.h"

/**
 * struct mhi_bus_bw_cfg - Interconnect vote data
 * @avg_bw: Vote for average bandwidth
 * @peak_bw: Vote for peak bandwidth
 */
struct mhi_bus_bw_cfg {
	u32 avg_bw;
	u32 peak_bw;
};

struct arch_info {
	struct mhi_qcom_priv *mhi_priv;
	struct esoc_desc *esoc_client;
	struct esoc_client_hook esoc_ops;
	struct msm_pcie_register_event pcie_reg_event;
	struct pci_saved_state *pcie_state;
	struct notifier_block pm_notifier;
	struct completion pm_completion;
	struct icc_path *icc_path;
	const char *icc_name;
	struct mhi_bus_bw_cfg *bw_cfg_table;
	/* bootup logger */
	void *boot_ipc_log;
	struct mhi_device *boot_dev;
	size_t boot_len;
};

/* IPC log markers for bootup logger */
#define DLOG "Dev->Host: "
#define HLOG "Host: "

#define MHI_BOOT_LOG_PAGES (25)
#define MHI_BOOT_DEFAULT_RX_LEN (0x1000)

#define MHI_BUS_BW_CFG_COUNT (5)

static int mhi_arch_pm_notifier(struct notifier_block *nb,
				unsigned long event, void *unused)
{
	struct arch_info *arch_info =
		container_of(nb, struct arch_info, pm_notifier);

	switch (event) {
	case PM_SUSPEND_PREPARE:
		reinit_completion(&arch_info->pm_completion);
		break;

	case PM_POST_SUSPEND:
		complete_all(&arch_info->pm_completion);
		break;
	}

	return NOTIFY_DONE;
}

static int mhi_arch_set_bus_request(struct mhi_controller *mhi_cntrl, int index)
{
	struct mhi_qcom_priv *mhi_priv = mhi_controller_get_privdata(mhi_cntrl);
	struct arch_info *arch_info = mhi_priv->arch_info;
	int ret;

	if (index >= MHI_BUS_BW_CFG_COUNT)
		return -EINVAL;

	ret = icc_set_bw(arch_info->icc_path,
			 arch_info->bw_cfg_table[index].avg_bw,
			 arch_info->bw_cfg_table[index].peak_bw);
	if (ret) {
		MHI_CNTRL_ERR("Could not set BW cfg: %d (%d %d), ret: %d\n",
			      index, arch_info->bw_cfg_table[index].avg_bw,
			      arch_info->bw_cfg_table[index].peak_bw, ret);
		return ret;
	}

	return 0;
}

static void mhi_arch_pci_link_state_cb(struct msm_pcie_notify *notify)
{
	struct mhi_controller *mhi_cntrl = notify->data;
	struct mhi_qcom_priv *mhi_priv = mhi_controller_get_privdata(mhi_cntrl);

	switch (notify->event) {
	case MSM_PCIE_EVENT_WAKEUP:
		MHI_CNTRL_LOG("Received PCIE_WAKE signal\n");

		/* bring link out of d3cold */
		if (mhi_priv->powered_on) {
			pm_runtime_get(mhi_cntrl->cntrl_dev);
			pm_runtime_put_noidle(mhi_cntrl->cntrl_dev);
		}
		break;
	case MSM_PCIE_EVENT_L1SS_TIMEOUT:
		MHI_CNTRL_LOG("Received PCIE_L1SS_TIMEOUT signal\n");

		pm_runtime_mark_last_busy(mhi_cntrl->cntrl_dev);
		pm_request_autosuspend(mhi_cntrl->cntrl_dev);
		break;
	default:
		MHI_CNTRL_LOG("Unhandled event: 0x%x\n", notify->event);
	}
}

static int mhi_arch_esoc_ops_power_on(void *priv, unsigned int flags)
{
	struct mhi_controller *mhi_cntrl = priv;
	struct mhi_qcom_priv *mhi_priv = mhi_controller_get_privdata(mhi_cntrl);
	struct pci_dev *pci_dev = to_pci_dev(mhi_cntrl->cntrl_dev);
	int ret;

	mutex_lock(&mhi_cntrl->pm_mutex);
	if (mhi_priv->powered_on) {
		MHI_CNTRL_LOG("MHI still in active state\n");
		mutex_unlock(&mhi_cntrl->pm_mutex);
		return 0;
	}

	MHI_CNTRL_LOG("Enter: mdm_crashed: %d\n", flags & ESOC_HOOK_MDM_CRASH);

	/* reset rpm state */
	pm_runtime_set_active(mhi_cntrl->cntrl_dev);
	pm_runtime_enable(mhi_cntrl->cntrl_dev);
	mutex_unlock(&mhi_cntrl->pm_mutex);
	pm_runtime_forbid(mhi_cntrl->cntrl_dev);
	ret = pm_runtime_get_sync(mhi_cntrl->cntrl_dev);
	if (ret < 0) {
		MHI_CNTRL_ERR("Error with rpm resume, ret: %d\n", ret);
		return ret;
	}

	/* re-start the link & recover default cfg states */
	ret = msm_pcie_pm_control(MSM_PCIE_RESUME, pci_dev->bus->number,
				  pci_dev, NULL, 0);
	if (ret) {
		MHI_CNTRL_ERR("Failed to resume pcie bus ret: %d\n", ret);
		return ret;
	}

	mhi_priv->mdm_state = (flags & ESOC_HOOK_MDM_CRASH);

	return mhi_qcom_pci_probe(pci_dev, mhi_cntrl, mhi_priv);
}

static void mhi_arch_link_off(struct mhi_controller *mhi_cntrl)
{
	struct pci_dev *pci_dev = to_pci_dev(mhi_cntrl->cntrl_dev);

	MHI_CNTRL_LOG("Entered\n");

	pci_set_power_state(pci_dev, PCI_D3hot);

	/* release the resources */
	msm_pcie_pm_control(MSM_PCIE_SUSPEND, pci_dev->bus->number, pci_dev,
			    NULL, 0);
	mhi_arch_set_bus_request(mhi_cntrl, 0);

	MHI_CNTRL_LOG("Exited\n");
}

static void mhi_arch_esoc_ops_power_off(void *priv, unsigned int flags)
{
	struct mhi_controller *mhi_cntrl = priv;
	struct mhi_qcom_priv *mhi_priv = mhi_controller_get_privdata(mhi_cntrl);
	bool mdm_state = (flags & ESOC_HOOK_MDM_CRASH);
	struct arch_info *arch_info = mhi_priv->arch_info;

	MHI_CNTRL_LOG("Enter: mdm_crashed: %d\n", mdm_state);

	/*
	 * Abort system suspend if system is preparing to go to suspend
	 * by grabbing wake source.
	 * If system is suspended, wait for pm notifier callback to notify
	 * that resume has occurred with PM_POST_SUSPEND event.
	 */
	pm_stay_awake(&mhi_cntrl->mhi_dev->dev);
	wait_for_completion(&arch_info->pm_completion);

	/* if link is in suspend, wake it up */
	pm_runtime_get_sync(mhi_cntrl->cntrl_dev);

	mutex_lock(&mhi_cntrl->pm_mutex);
	if (!mhi_priv->powered_on) {
		MHI_CNTRL_LOG("Not in active state\n");
		mutex_unlock(&mhi_cntrl->pm_mutex);
		pm_runtime_put_noidle(mhi_cntrl->cntrl_dev);
		return;
	}
	mhi_priv->powered_on = false;
	mhi_priv->driver_remove = false;
	mutex_unlock(&mhi_cntrl->pm_mutex);

	pm_runtime_put_noidle(mhi_cntrl->cntrl_dev);

	MHI_CNTRL_LOG("Triggering shutdown process\n");
	mhi_power_down(mhi_cntrl, !mdm_state);

	/* turn the link off */
	mhi_arch_link_off(mhi_cntrl);
	mhi_arch_pcie_deinit(mhi_cntrl);
	mhi_deinit_pci_dev(to_pci_dev(mhi_cntrl->cntrl_dev),
			   mhi_priv->dev_info);

	pm_relax(&mhi_cntrl->mhi_dev->dev);
}

static void mhi_arch_esoc_ops_mdm_error(void *priv)
{
	struct mhi_controller *mhi_cntrl = priv;

	/* transition MHI state into error state */
	MHI_CNTRL_LOG("Enter: mdm asserted: ret: %d\n",
		      mhi_report_error(mhi_cntrl));
}

void mhi_arch_mission_mode_enter(struct mhi_controller *mhi_cntrl)
{
	struct mhi_qcom_priv *mhi_priv = mhi_controller_get_privdata(mhi_cntrl);
	const struct mhi_pci_dev_info *dev_info = mhi_priv->dev_info;
	struct pci_dev *pci_dev = to_pci_dev(mhi_cntrl->cntrl_dev);
	int ret;

	/* Set target PCIe link speed as maximum device/link is capable of */
	ret = msm_pcie_set_target_link_speed(pci_domain_nr(pci_dev->bus), 0,
					     true);
	if (ret) {
		MHI_CNTRL_ERR("Failed to set PCIe target link speed\n");
		return;
	}

	if (dev_info->skip_forced_suspend && !dev_info->allow_m1)
		msm_pcie_l1ss_timeout_enable(to_pci_dev(mhi_cntrl->cntrl_dev));
}

static int mhi_arch_pcie_scale_bw(struct mhi_controller *mhi_cntrl,
				  struct pci_dev *pci_dev,
				  struct mhi_link_info *link_info)
{
	int ret;

	ret = msm_pcie_set_link_bandwidth(pci_dev, link_info->target_link_speed,
					  link_info->target_link_width);
	if (ret)
		return ret;

	/* do a bus scale vote based on gen speeds */
	mhi_arch_set_bus_request(mhi_cntrl, link_info->target_link_speed);

	MHI_CNTRL_LOG("BW changed to speed:0x%x width:0x%x\n",
		      link_info->target_link_speed,
		      link_info->target_link_width);

	return 0;
}

static int mhi_arch_bw_scale(struct mhi_controller *mhi_cntrl,
			     struct mhi_link_info *link_info)
{
	return mhi_arch_pcie_scale_bw(mhi_cntrl, to_pci_dev(mhi_cntrl->cntrl_dev),
				      link_info);
}

static void mhi_bl_dl_cb(struct mhi_device *mhi_device,
			 struct mhi_result *mhi_result)
{
	struct mhi_controller *mhi_cntrl = mhi_device->mhi_cntrl;
	struct mhi_qcom_priv *mhi_priv = mhi_controller_get_privdata(mhi_cntrl);
	struct arch_info *arch_info = mhi_priv->arch_info;
	char *buf = mhi_result->buf_addr;
	char *token, *delim = "\n";
	int ret;

	if (mhi_result->transaction_status == -ENOTCONN) {
		kfree(buf);
		return;
	}

	/* force a null at last character */
	buf[mhi_result->bytes_xferd - 1] = 0;

	if (mhi_result->bytes_xferd >= MAX_MSG_SIZE) {
		do {
			token = strsep((char **)&buf, delim);
			if (token)
				ipc_log_string(arch_info->boot_ipc_log, "%s %s",
					       DLOG, token);
		} while (token);
	} else {
		ipc_log_string(arch_info->boot_ipc_log, "%s %s", DLOG, buf);
	}

	ret = mhi_queue_buf(mhi_device, DMA_FROM_DEVICE, buf,
			    arch_info->boot_len, MHI_EOT);
	if (ret) {
		kfree(buf);
		MHI_CNTRL_ERR("Failed to recycle BL channel buffer: %d\n", ret);
		return;
	}
}

static void mhi_bl_dummy_cb(struct mhi_device *mhi_dev,
			    struct mhi_result *mhi_result)
{
}

static void mhi_bl_remove(struct mhi_device *mhi_device)
{
	struct mhi_controller *mhi_cntrl = mhi_device->mhi_cntrl;
	struct mhi_qcom_priv *mhi_priv = mhi_controller_get_privdata(mhi_cntrl);
	struct arch_info *arch_info = mhi_priv->arch_info;

	arch_info->boot_dev = NULL;
	ipc_log_string(arch_info->boot_ipc_log, HLOG "Remove notification\n");

	mhi_unprepare_from_transfer(mhi_device);
}

int mhi_bl_queue_inbound(struct mhi_device *mhi_dev, size_t len)
{
	void *buf;
	int nr_trbs, ret, i;

	nr_trbs = mhi_get_free_desc_count(mhi_dev, DMA_FROM_DEVICE);
	for (i = 0; i < nr_trbs; i++) {
		buf = kmalloc(len, GFP_KERNEL);
		if (!buf)
			return -ENOMEM;

		ret = mhi_queue_buf(mhi_dev, DMA_FROM_DEVICE, buf, len,
				    MHI_EOT);
		if (ret) {
			kfree(buf);
			return -EIO;
		}
	}

	return 0;
}

static int mhi_bl_probe(struct mhi_device *mhi_device,
			const struct mhi_device_id *id)
{
	struct mhi_controller *mhi_cntrl = mhi_device->mhi_cntrl;
	struct mhi_qcom_priv *mhi_priv = mhi_controller_get_privdata(mhi_cntrl);
	struct arch_info *arch_info = mhi_priv->arch_info;
	int ret;

	arch_info->boot_dev = mhi_device;
	arch_info->boot_ipc_log =
		ipc_log_context_create(MHI_BOOT_LOG_PAGES,
				       dev_name(&mhi_device->dev), 0);
	ipc_log_string(arch_info->boot_ipc_log, HLOG
		       "Entered SBL, Session ID:0x%x\n", mhi_cntrl->session_id);

	ret = mhi_prepare_for_transfer(mhi_device);
	if (ret)
		return ret;

	arch_info->boot_len = id->driver_data;
	if (!arch_info->boot_len)
		arch_info->boot_len = MHI_BOOT_DEFAULT_RX_LEN;

	ret = mhi_bl_queue_inbound(mhi_device, arch_info->boot_len);
	if (ret) {
		mhi_unprepare_from_transfer(mhi_device);
		return ret;
	}

	return 0;
}

static const struct mhi_device_id mhi_bl_match_table[] = {
	{ .chan = "BL", .driver_data = 0x1000 },
	{},
};

static struct mhi_driver mhi_bl_driver = {
	.id_table = mhi_bl_match_table,
	.remove = mhi_bl_remove,
	.probe = mhi_bl_probe,
	.ul_xfer_cb = mhi_bl_dummy_cb,
	.dl_xfer_cb = mhi_bl_dl_cb,
	.driver = {
		.name = "MHI_BL",
		.owner = THIS_MODULE,
	},
};

void mhi_arch_pcie_deinit(struct mhi_controller *mhi_cntrl)
{
	struct mhi_qcom_priv *mhi_priv = mhi_controller_get_privdata(mhi_cntrl);
	struct pci_dev *pci_dev = to_pci_dev(mhi_cntrl->cntrl_dev);
	struct arch_info *arch_info = mhi_priv->arch_info;
	int ret;

	mhi_arch_set_bus_request(mhi_cntrl, 0);

	/* Reset target PCIe link speed to the original device tree entry */
	ret = msm_pcie_set_target_link_speed(pci_domain_nr(pci_dev->bus), 0,
					     false);
	if (ret)
		MHI_CNTRL_ERR("Failed to set PCIe target link speed\n");

	if (!mhi_priv->driver_remove)
		return;

	mhi_driver_unregister(&mhi_bl_driver);
	esoc_unregister_client_hook(arch_info->esoc_client,
				    &arch_info->esoc_ops);
	devm_unregister_esoc_client(mhi_cntrl->cntrl_dev,
				    arch_info->esoc_client);
	complete_all(&arch_info->pm_completion);
	unregister_pm_notifier(&arch_info->pm_notifier);
	msm_pcie_deregister_event(&arch_info->pcie_reg_event);
}

int mhi_arch_pcie_init(struct mhi_controller *mhi_cntrl)
{
	struct mhi_qcom_priv *mhi_priv = mhi_controller_get_privdata(mhi_cntrl);
	struct arch_info *arch_info = mhi_priv->arch_info;
	const struct mhi_pci_dev_info *dev_info = mhi_priv->dev_info;
	struct pci_dev *pci_dev = to_pci_dev(mhi_cntrl->cntrl_dev);
	struct mhi_link_info *cur_link_info;
	int ret;
	u16 linkstat;

	if (!arch_info) {
		struct msm_pcie_register_event *reg_event;
		struct device_node *of_node = pci_dev->dev.of_node;

		arch_info = devm_kzalloc(mhi_cntrl->cntrl_dev,
					 sizeof(*arch_info), GFP_KERNEL);
		if (!arch_info)
			return -ENOMEM;

		mhi_priv->arch_info = arch_info;
		arch_info->mhi_priv = mhi_priv;

		ret = of_property_read_string(of_node, "interconnect-names",
					      &arch_info->icc_name);
		if (ret) {
			MHI_CNTRL_ERR("No interconnect name specified\n");
			return -ENOENT;
		}

		arch_info->icc_path = of_icc_get(mhi_cntrl->cntrl_dev,
						 arch_info->icc_name);
		if (IS_ERR(arch_info->icc_path))  {
			ret = PTR_ERR(arch_info->icc_path);
			MHI_CNTRL_ERR("Interconnect path for %s not found, ret: %d\n",
				      arch_info->icc_name, ret);
			return ret;
		}

		arch_info->bw_cfg_table = devm_kzalloc(mhi_cntrl->cntrl_dev,
						sizeof(*arch_info->bw_cfg_table)
						* MHI_BUS_BW_CFG_COUNT,
						GFP_KERNEL);
		if (!arch_info->bw_cfg_table)
			return -ENOMEM;

		ret = of_property_read_u32_array(of_node, "qcom,mhi-bus-bw-cfg",
						 (u32 *)arch_info->bw_cfg_table,
						 MHI_BUS_BW_CFG_COUNT * 2);
		if (ret) {
			MHI_CNTRL_ERR("Invalid bus BW config table\n");
			return ret;
		}

		/* register with pcie rc for WAKE# or link state events */
		reg_event = &arch_info->pcie_reg_event;
		reg_event->events = dev_info->allow_m1 ?
			(MSM_PCIE_EVENT_WAKEUP) :
			(MSM_PCIE_EVENT_WAKEUP | MSM_PCIE_EVENT_L1SS_TIMEOUT);

		reg_event->user = pci_dev;
		reg_event->callback = mhi_arch_pci_link_state_cb;
		reg_event->notify.data = mhi_cntrl;
		ret = msm_pcie_register_event(reg_event);
		if (ret)
			MHI_CNTRL_ERR("Failed to register for link events\n");

		init_completion(&arch_info->pm_completion);

		/* register PM notifier to get post resume events */
		arch_info->pm_notifier.notifier_call = mhi_arch_pm_notifier;
		register_pm_notifier(&arch_info->pm_notifier);

		/*
		 * Mark as completed at initial boot-up to allow ESOC power on
		 * callback to proceed if system has not gone to suspend
		 */
		complete_all(&arch_info->pm_completion);

		arch_info->esoc_client = devm_register_esoc_client(
						mhi_cntrl->cntrl_dev, "mdm");
		if (IS_ERR_OR_NULL(arch_info->esoc_client)) {
			MHI_CNTRL_ERR("Failed to register esoc client\n");
		} else {
			/* register for power on/off hooks */
			struct esoc_client_hook *esoc_ops =
				&arch_info->esoc_ops;

			esoc_ops->priv = mhi_cntrl;
			esoc_ops->prio = ESOC_MHI_HOOK;
			esoc_ops->esoc_link_power_on =
				mhi_arch_esoc_ops_power_on;
			esoc_ops->esoc_link_power_off =
				mhi_arch_esoc_ops_power_off;
			esoc_ops->esoc_link_mdm_crash =
				mhi_arch_esoc_ops_mdm_error;

			ret = esoc_register_client_hook(arch_info->esoc_client,
							esoc_ops);
			if (ret)
				MHI_CNTRL_ERR("Failed to register esoc ops\n");
		}

		/*
		 * MHI host driver has full autonomy to manage power state.
		 * Disable all automatic power collapse features
		 */
		msm_pcie_pm_control(MSM_PCIE_DISABLE_PC, pci_dev->bus->number,
				    pci_dev, NULL, 0);

		mhi_controller_set_bw_scale_cb(mhi_cntrl, &mhi_arch_bw_scale);

		mhi_driver_register(&mhi_bl_driver);
	}

	/* store the current bw info */
	ret = pcie_capability_read_word(pci_dev, PCI_EXP_LNKSTA, &linkstat);
	if (ret)
		return ret;

	cur_link_info = &mhi_cntrl->mhi_link_info;
	cur_link_info->target_link_speed = linkstat & PCI_EXP_LNKSTA_CLS;
	cur_link_info->target_link_width = (linkstat & PCI_EXP_LNKSTA_NLW) >>
					    PCI_EXP_LNKSTA_NLW_SHIFT;

	return mhi_arch_set_bus_request(mhi_cntrl,
					cur_link_info->target_link_speed);
}

int mhi_arch_link_suspend(struct mhi_controller *mhi_cntrl)
{
	struct mhi_qcom_priv *mhi_priv = mhi_controller_get_privdata(mhi_cntrl);
	struct arch_info *arch_info = mhi_priv->arch_info;
	const struct mhi_pci_dev_info *dev_info = mhi_priv->dev_info;
	struct pci_dev *pci_dev = to_pci_dev(mhi_cntrl->cntrl_dev);
	int ret = 0;

	MHI_CNTRL_LOG("Entered with suspend_mode:%s\n",
		TO_MHI_SUSPEND_MODE_STR(mhi_priv->suspend_mode));

	/* disable inactivity timer */
	if (!dev_info->allow_m1)
		msm_pcie_l1ss_timeout_disable(pci_dev);

	if (mhi_priv->disable_pci_lpm)
		goto exit_suspend;

	switch (mhi_priv->suspend_mode) {
	case MHI_DEFAULT_SUSPEND:
		pci_clear_master(pci_dev);
		ret = pci_save_state(pci_dev);
		if (ret) {
			MHI_CNTRL_ERR("Failed with pci_save_state, ret:%d\n",
				      ret);
			goto exit_suspend;
		}

		arch_info->pcie_state = pci_store_saved_state(pci_dev);
		pci_disable_device(pci_dev);

		pci_set_power_state(pci_dev, PCI_D3hot);

		/* release the resources */
		msm_pcie_pm_control(MSM_PCIE_SUSPEND, pci_dev->bus->number,
				    pci_dev, NULL, 0);
		mhi_arch_set_bus_request(mhi_cntrl, 0);
		break;
	case MHI_FAST_LINK_OFF:
	case MHI_ACTIVE_STATE:
	case MHI_FAST_LINK_ON:/* keeping link on do nothing */
	default:
		break;
	}

exit_suspend:
	if (ret && !dev_info->allow_m1)
		msm_pcie_l1ss_timeout_enable(pci_dev);

	MHI_CNTRL_LOG("Exited with ret:%d\n", ret);

	return ret;
}

static int __mhi_arch_link_resume(struct mhi_controller *mhi_cntrl)
{
	struct mhi_qcom_priv *mhi_priv = mhi_controller_get_privdata(mhi_cntrl);
	struct arch_info *arch_info = mhi_priv->arch_info;
	struct pci_dev *pci_dev = to_pci_dev(mhi_cntrl->cntrl_dev);
	struct mhi_link_info *cur_info = &mhi_cntrl->mhi_link_info;
	int ret;

	/* request bus scale voting based on higher gen speed */
	ret = mhi_arch_set_bus_request(mhi_cntrl,
				       cur_info->target_link_speed);
	if (ret)
		MHI_CNTRL_LOG("Could not set bus frequency, ret:%d\n", ret);

	ret = msm_pcie_pm_control(MSM_PCIE_RESUME, pci_dev->bus->number,
				  pci_dev, NULL, 0);
	if (ret) {
		MHI_CNTRL_ERR("Link training failed, ret:%d\n", ret);
		return ret;
	}

	ret = pci_set_power_state(pci_dev, PCI_D0);
	if (ret) {
		MHI_CNTRL_ERR("Failed to set PCI_D0 state, ret:%d\n", ret);
		return ret;
	}

	ret = pci_enable_device(pci_dev);
	if (ret) {
		MHI_CNTRL_ERR("Failed to enable device, ret:%d\n", ret);
		return ret;
	}

	ret = pci_load_and_free_saved_state(pci_dev, &arch_info->pcie_state);
	if (ret)
		MHI_CNTRL_LOG("Failed to load saved cfg state\n");

	pci_restore_state(pci_dev);
	pci_set_master(pci_dev);

	return 0;
}

int mhi_arch_link_resume(struct mhi_controller *mhi_cntrl)
{
	struct mhi_qcom_priv *mhi_priv = mhi_controller_get_privdata(mhi_cntrl);
	struct pci_dev *pci_dev = to_pci_dev(mhi_cntrl->cntrl_dev);
	const struct mhi_pci_dev_info *dev_info = mhi_priv->dev_info;
	int ret = 0;

	MHI_CNTRL_LOG("Entered with suspend_mode: %s\n",
		      TO_MHI_SUSPEND_MODE_STR(mhi_priv->suspend_mode));

	if (mhi_priv->disable_pci_lpm)
		goto resume_no_pci_lpm;

	switch (mhi_priv->suspend_mode) {
	case MHI_DEFAULT_SUSPEND:
		ret = __mhi_arch_link_resume(mhi_cntrl);
		break;
	case MHI_FAST_LINK_OFF:
	case MHI_ACTIVE_STATE:
	case MHI_FAST_LINK_ON:
	default:
		break;
	}

	if (ret) {
		MHI_CNTRL_ERR("Link training failed, ret: %d\n", ret);
		return ret;
	}

resume_no_pci_lpm:
	if (!dev_info->allow_m1)
		msm_pcie_l1ss_timeout_enable(pci_dev);

	MHI_CNTRL_LOG("Exited with ret: %d\n", ret);

	return ret;
}

u64 mhi_arch_time_get(struct mhi_controller *mhi_cntrl)
{
	return __arch_counter_get_cntvct();
}

int mhi_arch_link_lpm_disable(struct mhi_controller *mhi_cntrl)
{
	struct pci_dev *pci_dev = to_pci_dev(mhi_cntrl->cntrl_dev);

	return msm_pcie_prevent_l1(pci_dev);
}

int mhi_arch_link_lpm_enable(struct mhi_controller *mhi_cntrl)
{
	struct pci_dev *pci_dev = to_pci_dev(mhi_cntrl->cntrl_dev);

	msm_pcie_allow_l1(pci_dev);

	return 0;
}
