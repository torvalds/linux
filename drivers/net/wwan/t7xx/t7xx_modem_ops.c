// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021-2022, Intel Corporation.
 *
 * Authors:
 *  Haijun Liu <haijun.liu@mediatek.com>
 *  Eliot Lee <eliot.lee@intel.com>
 *  Moises Veleta <moises.veleta@intel.com>
 *  Ricardo Martinez <ricardo.martinez@linux.intel.com>
 *
 * Contributors:
 *  Amir Hanania <amir.hanania@intel.com>
 *  Chiranjeevi Rapolu <chiranjeevi.rapolu@intel.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 */

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/bitfield.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/gfp.h>
#include <linux/io.h>
#include <linux/irqreturn.h>
#include <linux/kthread.h>
#include <linux/skbuff.h>
#include <linux/spinlock.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#include "t7xx_cldma.h"
#include "t7xx_hif_cldma.h"
#include "t7xx_mhccif.h"
#include "t7xx_modem_ops.h"
#include "t7xx_netdev.h"
#include "t7xx_pci.h"
#include "t7xx_pcie_mac.h"
#include "t7xx_port.h"
#include "t7xx_port_proxy.h"
#include "t7xx_reg.h"
#include "t7xx_state_monitor.h"

#define RT_ID_MD_PORT_ENUM	0
#define RT_ID_AP_PORT_ENUM	1
/* Modem feature query identification code - "ICCC" */
#define MD_FEATURE_QUERY_ID	0x49434343

#define FEATURE_VER		GENMASK(7, 4)
#define FEATURE_MSK		GENMASK(3, 0)

#define RGU_RESET_DELAY_MS	10
#define PORT_RESET_DELAY_MS	2000
#define EX_HS_TIMEOUT_MS	5000
#define EX_HS_POLL_DELAY_MS	10

enum mtk_feature_support_type {
	MTK_FEATURE_DOES_NOT_EXIST,
	MTK_FEATURE_NOT_SUPPORTED,
	MTK_FEATURE_MUST_BE_SUPPORTED,
};

static unsigned int t7xx_get_interrupt_status(struct t7xx_pci_dev *t7xx_dev)
{
	return t7xx_mhccif_read_sw_int_sts(t7xx_dev) & D2H_SW_INT_MASK;
}

/**
 * t7xx_pci_mhccif_isr() - Process MHCCIF interrupts.
 * @t7xx_dev: MTK device.
 *
 * Check the interrupt status and queue commands accordingly.
 *
 * Returns:
 ** 0		- Success.
 ** -EINVAL	- Failure to get FSM control.
 */
int t7xx_pci_mhccif_isr(struct t7xx_pci_dev *t7xx_dev)
{
	struct t7xx_modem *md = t7xx_dev->md;
	struct t7xx_fsm_ctl *ctl;
	unsigned int int_sta;
	int ret = 0;
	u32 mask;

	ctl = md->fsm_ctl;
	if (!ctl) {
		dev_err_ratelimited(&t7xx_dev->pdev->dev,
				    "MHCCIF interrupt received before initializing MD monitor\n");
		return -EINVAL;
	}

	spin_lock_bh(&md->exp_lock);
	int_sta = t7xx_get_interrupt_status(t7xx_dev);
	md->exp_id |= int_sta;
	if (md->exp_id & D2H_INT_EXCEPTION_INIT) {
		if (ctl->md_state == MD_STATE_INVALID ||
		    ctl->md_state == MD_STATE_WAITING_FOR_HS1 ||
		    ctl->md_state == MD_STATE_WAITING_FOR_HS2 ||
		    ctl->md_state == MD_STATE_READY) {
			md->exp_id &= ~D2H_INT_EXCEPTION_INIT;
			ret = t7xx_fsm_recv_md_intr(ctl, MD_IRQ_CCIF_EX);
		}
	} else if (md->exp_id & D2H_INT_PORT_ENUM) {
		md->exp_id &= ~D2H_INT_PORT_ENUM;

		if (ctl->curr_state == FSM_STATE_INIT || ctl->curr_state == FSM_STATE_PRE_START ||
		    ctl->curr_state == FSM_STATE_STOPPED)
			ret = t7xx_fsm_recv_md_intr(ctl, MD_IRQ_PORT_ENUM);
	} else if (ctl->md_state == MD_STATE_WAITING_FOR_HS1) {
		mask = t7xx_mhccif_mask_get(t7xx_dev);
		if ((md->exp_id & D2H_INT_ASYNC_MD_HK) && !(mask & D2H_INT_ASYNC_MD_HK)) {
			md->exp_id &= ~D2H_INT_ASYNC_MD_HK;
			queue_work(md->handshake_wq, &md->handshake_work);
		}
	}
	spin_unlock_bh(&md->exp_lock);

	return ret;
}

static void t7xx_clr_device_irq_via_pcie(struct t7xx_pci_dev *t7xx_dev)
{
	struct t7xx_addr_base *pbase_addr = &t7xx_dev->base_addr;
	void __iomem *reset_pcie_reg;
	u32 val;

	reset_pcie_reg = pbase_addr->pcie_ext_reg_base + TOPRGU_CH_PCIE_IRQ_STA -
			  pbase_addr->pcie_dev_reg_trsl_addr;
	val = ioread32(reset_pcie_reg);
	iowrite32(val, reset_pcie_reg);
}

void t7xx_clear_rgu_irq(struct t7xx_pci_dev *t7xx_dev)
{
	/* Clear L2 */
	t7xx_clr_device_irq_via_pcie(t7xx_dev);
	/* Clear L1 */
	t7xx_pcie_mac_clear_int_status(t7xx_dev, SAP_RGU_INT);
}

static int t7xx_acpi_reset(struct t7xx_pci_dev *t7xx_dev, char *fn_name)
{
#ifdef CONFIG_ACPI
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct device *dev = &t7xx_dev->pdev->dev;
	acpi_status acpi_ret;
	acpi_handle handle;

	handle = ACPI_HANDLE(dev);
	if (!handle) {
		dev_err(dev, "ACPI handle not found\n");
		return -EFAULT;
	}

	if (!acpi_has_method(handle, fn_name)) {
		dev_err(dev, "%s method not found\n", fn_name);
		return -EFAULT;
	}

	acpi_ret = acpi_evaluate_object(handle, fn_name, NULL, &buffer);
	if (ACPI_FAILURE(acpi_ret)) {
		dev_err(dev, "%s method fail: %s\n", fn_name, acpi_format_exception(acpi_ret));
		return -EFAULT;
	}

#endif
	return 0;
}

int t7xx_acpi_fldr_func(struct t7xx_pci_dev *t7xx_dev)
{
	return t7xx_acpi_reset(t7xx_dev, "_RST");
}

static void t7xx_reset_device_via_pmic(struct t7xx_pci_dev *t7xx_dev)
{
	u32 val;

	val = ioread32(IREG_BASE(t7xx_dev) + T7XX_PCIE_MISC_DEV_STATUS);
	if (val & MISC_RESET_TYPE_PLDR)
		t7xx_acpi_reset(t7xx_dev, "MRST._RST");
	else if (val & MISC_RESET_TYPE_FLDR)
		t7xx_acpi_fldr_func(t7xx_dev);
}

static irqreturn_t t7xx_rgu_isr_thread(int irq, void *data)
{
	struct t7xx_pci_dev *t7xx_dev = data;

	msleep(RGU_RESET_DELAY_MS);
	t7xx_reset_device_via_pmic(t7xx_dev);
	return IRQ_HANDLED;
}

static irqreturn_t t7xx_rgu_isr_handler(int irq, void *data)
{
	struct t7xx_pci_dev *t7xx_dev = data;
	struct t7xx_modem *modem;

	t7xx_clear_rgu_irq(t7xx_dev);
	if (!t7xx_dev->rgu_pci_irq_en)
		return IRQ_HANDLED;

	modem = t7xx_dev->md;
	modem->rgu_irq_asserted = true;
	t7xx_pcie_mac_clear_int(t7xx_dev, SAP_RGU_INT);
	return IRQ_WAKE_THREAD;
}

static void t7xx_pcie_register_rgu_isr(struct t7xx_pci_dev *t7xx_dev)
{
	/* Registers RGU callback ISR with PCIe driver */
	t7xx_pcie_mac_clear_int(t7xx_dev, SAP_RGU_INT);
	t7xx_pcie_mac_clear_int_status(t7xx_dev, SAP_RGU_INT);

	t7xx_dev->intr_handler[SAP_RGU_INT] = t7xx_rgu_isr_handler;
	t7xx_dev->intr_thread[SAP_RGU_INT] = t7xx_rgu_isr_thread;
	t7xx_dev->callback_param[SAP_RGU_INT] = t7xx_dev;
	t7xx_pcie_mac_set_int(t7xx_dev, SAP_RGU_INT);
}

/**
 * t7xx_cldma_exception() - CLDMA exception handler.
 * @md_ctrl: modem control struct.
 * @stage: exception stage.
 *
 * Part of the modem exception recovery.
 * Stages are one after the other as describe below:
 * HIF_EX_INIT:		Disable and clear TXQ.
 * HIF_EX_CLEARQ_DONE:	Disable RX, flush TX/RX workqueues and clear RX.
 * HIF_EX_ALLQ_RESET:	HW is back in safe mode for re-initialization and restart.
 */

/* Modem Exception Handshake Flow
 *
 * Modem HW Exception interrupt received
 *           (MD_IRQ_CCIF_EX)
 *                   |
 *         +---------v--------+
 *         |   HIF_EX_INIT    | : Disable and clear TXQ
 *         +------------------+
 *                   |
 *         +---------v--------+
 *         | HIF_EX_INIT_DONE | : Wait for the init to be done
 *         +------------------+
 *                   |
 *         +---------v--------+
 *         |HIF_EX_CLEARQ_DONE| : Disable and clear RXQ
 *         +------------------+ : Flush TX/RX workqueues
 *                   |
 *         +---------v--------+
 *         |HIF_EX_ALLQ_RESET | : Restart HW and CLDMA
 *         +------------------+
 */
static void t7xx_cldma_exception(struct cldma_ctrl *md_ctrl, enum hif_ex_stage stage)
{
	switch (stage) {
	case HIF_EX_INIT:
		t7xx_cldma_stop_all_qs(md_ctrl, MTK_TX);
		t7xx_cldma_clear_all_qs(md_ctrl, MTK_TX);
		break;

	case HIF_EX_CLEARQ_DONE:
		/* We do not want to get CLDMA IRQ when MD is
		 * resetting CLDMA after it got clearq_ack.
		 */
		t7xx_cldma_stop_all_qs(md_ctrl, MTK_RX);
		t7xx_cldma_stop(md_ctrl);

		if (md_ctrl->hif_id == CLDMA_ID_MD)
			t7xx_cldma_hw_reset(md_ctrl->t7xx_dev->base_addr.infracfg_ao_base);

		t7xx_cldma_clear_all_qs(md_ctrl, MTK_RX);
		break;

	case HIF_EX_ALLQ_RESET:
		t7xx_cldma_hw_init(&md_ctrl->hw_info);
		t7xx_cldma_start(md_ctrl);
		break;

	default:
		break;
	}
}

static void t7xx_md_exception(struct t7xx_modem *md, enum hif_ex_stage stage)
{
	struct t7xx_pci_dev *t7xx_dev = md->t7xx_dev;

	if (stage == HIF_EX_CLEARQ_DONE) {
		/* Give DHL time to flush data */
		msleep(PORT_RESET_DELAY_MS);
		t7xx_port_proxy_reset(md->port_prox);
	}

	t7xx_cldma_exception(md->md_ctrl[CLDMA_ID_MD], stage);
	t7xx_cldma_exception(md->md_ctrl[CLDMA_ID_AP], stage);

	if (stage == HIF_EX_INIT)
		t7xx_mhccif_h2d_swint_trigger(t7xx_dev, H2D_CH_EXCEPTION_ACK);
	else if (stage == HIF_EX_CLEARQ_DONE)
		t7xx_mhccif_h2d_swint_trigger(t7xx_dev, H2D_CH_EXCEPTION_CLEARQ_ACK);
}

static int t7xx_wait_hif_ex_hk_event(struct t7xx_modem *md, int event_id)
{
	unsigned int waited_time_ms = 0;

	do {
		if (md->exp_id & event_id)
			return 0;

		waited_time_ms += EX_HS_POLL_DELAY_MS;
		msleep(EX_HS_POLL_DELAY_MS);
	} while (waited_time_ms < EX_HS_TIMEOUT_MS);

	return -EFAULT;
}

static void t7xx_md_sys_sw_init(struct t7xx_pci_dev *t7xx_dev)
{
	/* Register the MHCCIF ISR for MD exception, port enum and
	 * async handshake notifications.
	 */
	t7xx_mhccif_mask_set(t7xx_dev, D2H_SW_INT_MASK);
	t7xx_mhccif_mask_clr(t7xx_dev, D2H_INT_PORT_ENUM);

	/* Register RGU IRQ handler for sAP exception notification */
	t7xx_dev->rgu_pci_irq_en = true;
	t7xx_pcie_register_rgu_isr(t7xx_dev);
}

struct feature_query {
	__le32 head_pattern;
	u8 feature_set[FEATURE_COUNT];
	__le32 tail_pattern;
};

static void t7xx_prepare_host_rt_data_query(struct t7xx_sys_info *core)
{
	struct feature_query *ft_query;
	struct sk_buff *skb;

	skb = t7xx_ctrl_alloc_skb(sizeof(*ft_query));
	if (!skb)
		return;

	ft_query = skb_put(skb, sizeof(*ft_query));
	ft_query->head_pattern = cpu_to_le32(MD_FEATURE_QUERY_ID);
	memcpy(ft_query->feature_set, core->feature_set, FEATURE_COUNT);
	ft_query->tail_pattern = cpu_to_le32(MD_FEATURE_QUERY_ID);

	/* Send HS1 message to device */
	t7xx_port_send_ctl_skb(core->ctl_port, skb, CTL_ID_HS1_MSG, 0);
}

static int t7xx_prepare_device_rt_data(struct t7xx_sys_info *core, struct device *dev,
				       void *data)
{
	struct feature_query *md_feature = data;
	struct mtk_runtime_feature *rt_feature;
	unsigned int i, rt_data_len = 0;
	struct sk_buff *skb;

	/* Parse MD runtime data query */
	if (le32_to_cpu(md_feature->head_pattern) != MD_FEATURE_QUERY_ID ||
	    le32_to_cpu(md_feature->tail_pattern) != MD_FEATURE_QUERY_ID) {
		dev_err(dev, "Invalid feature pattern: head 0x%x, tail 0x%x\n",
			le32_to_cpu(md_feature->head_pattern),
			le32_to_cpu(md_feature->tail_pattern));
		return -EINVAL;
	}

	for (i = 0; i < FEATURE_COUNT; i++) {
		if (FIELD_GET(FEATURE_MSK, md_feature->feature_set[i]) !=
		    MTK_FEATURE_MUST_BE_SUPPORTED)
			rt_data_len += sizeof(*rt_feature);
	}

	skb = t7xx_ctrl_alloc_skb(rt_data_len);
	if (!skb)
		return -ENOMEM;

	rt_feature = skb_put(skb, rt_data_len);
	memset(rt_feature, 0, rt_data_len);

	/* Fill runtime feature */
	for (i = 0; i < FEATURE_COUNT; i++) {
		u8 md_feature_mask = FIELD_GET(FEATURE_MSK, md_feature->feature_set[i]);

		if (md_feature_mask == MTK_FEATURE_MUST_BE_SUPPORTED)
			continue;

		rt_feature->feature_id = i;
		if (md_feature_mask == MTK_FEATURE_DOES_NOT_EXIST)
			rt_feature->support_info = md_feature->feature_set[i];

		rt_feature++;
	}

	/* Send HS3 message to device */
	t7xx_port_send_ctl_skb(core->ctl_port, skb, CTL_ID_HS3_MSG, 0);
	return 0;
}

static int t7xx_parse_host_rt_data(struct t7xx_fsm_ctl *ctl, struct t7xx_sys_info *core,
				   struct device *dev, void *data, int data_length)
{
	enum mtk_feature_support_type ft_spt_st, ft_spt_cfg;
	struct mtk_runtime_feature *rt_feature;
	int i, offset;

	offset = sizeof(struct feature_query);
	for (i = 0; i < FEATURE_COUNT && offset < data_length; i++) {
		rt_feature = data + offset;
		offset += sizeof(*rt_feature) + le32_to_cpu(rt_feature->data_len);

		ft_spt_cfg = FIELD_GET(FEATURE_MSK, core->feature_set[i]);
		if (ft_spt_cfg != MTK_FEATURE_MUST_BE_SUPPORTED)
			continue;

		ft_spt_st = FIELD_GET(FEATURE_MSK, rt_feature->support_info);
		if (ft_spt_st != MTK_FEATURE_MUST_BE_SUPPORTED)
			return -EINVAL;

		if (i == RT_ID_MD_PORT_ENUM || i == RT_ID_AP_PORT_ENUM)
			t7xx_port_enum_msg_handler(ctl->md, rt_feature->data);
	}

	return 0;
}

static int t7xx_core_reset(struct t7xx_modem *md)
{
	struct device *dev = &md->t7xx_dev->pdev->dev;
	struct t7xx_fsm_ctl *ctl = md->fsm_ctl;

	md->core_md.ready = false;

	if (!ctl) {
		dev_err(dev, "FSM is not initialized\n");
		return -EINVAL;
	}

	if (md->core_md.handshake_ongoing) {
		int ret = t7xx_fsm_append_event(ctl, FSM_EVENT_MD_HS2_EXIT, NULL, 0);

		if (ret)
			return ret;
	}

	md->core_md.handshake_ongoing = false;
	return 0;
}

static void t7xx_core_hk_handler(struct t7xx_modem *md, struct t7xx_sys_info *core_info,
				 struct t7xx_fsm_ctl *ctl,
				 enum t7xx_fsm_event_state event_id,
				 enum t7xx_fsm_event_state err_detect)
{
	struct t7xx_fsm_event *event = NULL, *event_next;
	struct device *dev = &md->t7xx_dev->pdev->dev;
	unsigned long flags;
	int ret;

	t7xx_prepare_host_rt_data_query(core_info);

	while (!kthread_should_stop()) {
		bool event_received = false;

		spin_lock_irqsave(&ctl->event_lock, flags);
		list_for_each_entry_safe(event, event_next, &ctl->event_queue, entry) {
			if (event->event_id == err_detect) {
				list_del(&event->entry);
				spin_unlock_irqrestore(&ctl->event_lock, flags);
				dev_err(dev, "Core handshake error event received\n");
				goto err_free_event;
			} else if (event->event_id == event_id) {
				list_del(&event->entry);
				event_received = true;
				break;
			}
		}
		spin_unlock_irqrestore(&ctl->event_lock, flags);

		if (event_received)
			break;

		wait_event_interruptible(ctl->event_wq, !list_empty(&ctl->event_queue) ||
					 kthread_should_stop());
		if (kthread_should_stop())
			goto err_free_event;
	}

	if (!event || ctl->exp_flg)
		goto err_free_event;

	ret = t7xx_parse_host_rt_data(ctl, core_info, dev, event->data, event->length);
	if (ret) {
		dev_err(dev, "Host failure parsing runtime data: %d\n", ret);
		goto err_free_event;
	}

	if (ctl->exp_flg)
		goto err_free_event;

	ret = t7xx_prepare_device_rt_data(core_info, dev, event->data);
	if (ret) {
		dev_err(dev, "Device failure parsing runtime data: %d", ret);
		goto err_free_event;
	}

	core_info->ready = true;
	core_info->handshake_ongoing = false;
	wake_up(&ctl->async_hk_wq);
err_free_event:
	kfree(event);
}

static void t7xx_md_hk_wq(struct work_struct *work)
{
	struct t7xx_modem *md = container_of(work, struct t7xx_modem, handshake_work);
	struct t7xx_fsm_ctl *ctl = md->fsm_ctl;

	/* Clear the HS2 EXIT event appended in core_reset() */
	t7xx_fsm_clr_event(ctl, FSM_EVENT_MD_HS2_EXIT);
	t7xx_cldma_switch_cfg(md->md_ctrl[CLDMA_ID_MD]);
	t7xx_cldma_start(md->md_ctrl[CLDMA_ID_MD]);
	t7xx_fsm_broadcast_state(ctl, MD_STATE_WAITING_FOR_HS2);
	md->core_md.handshake_ongoing = true;
	t7xx_core_hk_handler(md, &md->core_md, ctl, FSM_EVENT_MD_HS2, FSM_EVENT_MD_HS2_EXIT);
}

static void t7xx_ap_hk_wq(struct work_struct *work)
{
	struct t7xx_modem *md = container_of(work, struct t7xx_modem, ap_handshake_work);
	struct t7xx_fsm_ctl *ctl = md->fsm_ctl;

	 /* Clear the HS2 EXIT event appended in t7xx_core_reset(). */
	t7xx_fsm_clr_event(ctl, FSM_EVENT_AP_HS2_EXIT);
	t7xx_cldma_stop(md->md_ctrl[CLDMA_ID_AP]);
	t7xx_cldma_switch_cfg(md->md_ctrl[CLDMA_ID_AP]);
	t7xx_cldma_start(md->md_ctrl[CLDMA_ID_AP]);
	md->core_ap.handshake_ongoing = true;
	t7xx_core_hk_handler(md, &md->core_ap, ctl, FSM_EVENT_AP_HS2, FSM_EVENT_AP_HS2_EXIT);
}

void t7xx_md_event_notify(struct t7xx_modem *md, enum md_event_id evt_id)
{
	struct t7xx_fsm_ctl *ctl = md->fsm_ctl;
	unsigned int int_sta;
	unsigned long flags;

	switch (evt_id) {
	case FSM_PRE_START:
		t7xx_mhccif_mask_clr(md->t7xx_dev, D2H_INT_PORT_ENUM | D2H_INT_ASYNC_MD_HK |
						   D2H_INT_ASYNC_AP_HK);
		break;

	case FSM_START:
		t7xx_mhccif_mask_set(md->t7xx_dev, D2H_INT_PORT_ENUM);

		spin_lock_irqsave(&md->exp_lock, flags);
		int_sta = t7xx_get_interrupt_status(md->t7xx_dev);
		md->exp_id |= int_sta;
		if (md->exp_id & D2H_INT_EXCEPTION_INIT) {
			ctl->exp_flg = true;
			md->exp_id &= ~D2H_INT_EXCEPTION_INIT;
			md->exp_id &= ~D2H_INT_ASYNC_MD_HK;
			md->exp_id &= ~D2H_INT_ASYNC_AP_HK;
		} else if (ctl->exp_flg) {
			md->exp_id &= ~D2H_INT_ASYNC_MD_HK;
			md->exp_id &= ~D2H_INT_ASYNC_AP_HK;
		} else {
			void __iomem *mhccif_base = md->t7xx_dev->base_addr.mhccif_rc_base;

			if (md->exp_id & D2H_INT_ASYNC_MD_HK) {
				queue_work(md->handshake_wq, &md->handshake_work);
				md->exp_id &= ~D2H_INT_ASYNC_MD_HK;
				iowrite32(D2H_INT_ASYNC_MD_HK, mhccif_base + REG_EP2RC_SW_INT_ACK);
				t7xx_mhccif_mask_set(md->t7xx_dev, D2H_INT_ASYNC_MD_HK);
			}

			if (md->exp_id & D2H_INT_ASYNC_AP_HK) {
				queue_work(md->ap_handshake_wq, &md->ap_handshake_work);
				md->exp_id &= ~D2H_INT_ASYNC_AP_HK;
				iowrite32(D2H_INT_ASYNC_AP_HK, mhccif_base + REG_EP2RC_SW_INT_ACK);
				t7xx_mhccif_mask_set(md->t7xx_dev, D2H_INT_ASYNC_AP_HK);
			}
		}
		spin_unlock_irqrestore(&md->exp_lock, flags);

		t7xx_mhccif_mask_clr(md->t7xx_dev,
				     D2H_INT_EXCEPTION_INIT |
				     D2H_INT_EXCEPTION_INIT_DONE |
				     D2H_INT_EXCEPTION_CLEARQ_DONE |
				     D2H_INT_EXCEPTION_ALLQ_RESET);
		break;

	case FSM_READY:
		t7xx_mhccif_mask_set(md->t7xx_dev, D2H_INT_ASYNC_MD_HK);
		t7xx_mhccif_mask_set(md->t7xx_dev, D2H_INT_ASYNC_AP_HK);
		break;

	default:
		break;
	}
}

void t7xx_md_exception_handshake(struct t7xx_modem *md)
{
	struct device *dev = &md->t7xx_dev->pdev->dev;
	int ret;

	t7xx_md_exception(md, HIF_EX_INIT);
	ret = t7xx_wait_hif_ex_hk_event(md, D2H_INT_EXCEPTION_INIT_DONE);
	if (ret)
		dev_err(dev, "EX CCIF HS timeout, RCH 0x%lx\n", D2H_INT_EXCEPTION_INIT_DONE);

	t7xx_md_exception(md, HIF_EX_INIT_DONE);
	ret = t7xx_wait_hif_ex_hk_event(md, D2H_INT_EXCEPTION_CLEARQ_DONE);
	if (ret)
		dev_err(dev, "EX CCIF HS timeout, RCH 0x%lx\n", D2H_INT_EXCEPTION_CLEARQ_DONE);

	t7xx_md_exception(md, HIF_EX_CLEARQ_DONE);
	ret = t7xx_wait_hif_ex_hk_event(md, D2H_INT_EXCEPTION_ALLQ_RESET);
	if (ret)
		dev_err(dev, "EX CCIF HS timeout, RCH 0x%lx\n", D2H_INT_EXCEPTION_ALLQ_RESET);

	t7xx_md_exception(md, HIF_EX_ALLQ_RESET);
}

static struct t7xx_modem *t7xx_md_alloc(struct t7xx_pci_dev *t7xx_dev)
{
	struct device *dev = &t7xx_dev->pdev->dev;
	struct t7xx_modem *md;

	md = devm_kzalloc(dev, sizeof(*md), GFP_KERNEL);
	if (!md)
		return NULL;

	md->t7xx_dev = t7xx_dev;
	t7xx_dev->md = md;
	spin_lock_init(&md->exp_lock);
	md->handshake_wq = alloc_workqueue("%s", WQ_UNBOUND | WQ_MEM_RECLAIM | WQ_HIGHPRI,
					   0, "md_hk_wq");
	if (!md->handshake_wq)
		return NULL;

	INIT_WORK(&md->handshake_work, t7xx_md_hk_wq);
	md->core_md.feature_set[RT_ID_MD_PORT_ENUM] &= ~FEATURE_MSK;
	md->core_md.feature_set[RT_ID_MD_PORT_ENUM] |=
		FIELD_PREP(FEATURE_MSK, MTK_FEATURE_MUST_BE_SUPPORTED);

	md->ap_handshake_wq = alloc_workqueue("%s", WQ_UNBOUND | WQ_MEM_RECLAIM | WQ_HIGHPRI,
					      0, "ap_hk_wq");
	if (!md->ap_handshake_wq) {
		destroy_workqueue(md->handshake_wq);
		return NULL;
	}

	INIT_WORK(&md->ap_handshake_work, t7xx_ap_hk_wq);
	md->core_ap.feature_set[RT_ID_AP_PORT_ENUM] &= ~FEATURE_MSK;
	md->core_ap.feature_set[RT_ID_AP_PORT_ENUM] |=
		FIELD_PREP(FEATURE_MSK, MTK_FEATURE_MUST_BE_SUPPORTED);

	return md;
}

int t7xx_md_reset(struct t7xx_pci_dev *t7xx_dev)
{
	struct t7xx_modem *md = t7xx_dev->md;

	md->md_init_finish = false;
	md->exp_id = 0;
	t7xx_fsm_reset(md);
	t7xx_cldma_reset(md->md_ctrl[CLDMA_ID_MD]);
	t7xx_cldma_reset(md->md_ctrl[CLDMA_ID_AP]);
	t7xx_port_proxy_reset(md->port_prox);
	md->md_init_finish = true;
	return t7xx_core_reset(md);
}

/**
 * t7xx_md_init() - Initialize modem.
 * @t7xx_dev: MTK device.
 *
 * Allocate and initialize MD control block, and initialize data path.
 * Register MHCCIF ISR and RGU ISR, and start the state machine.
 *
 * Return:
 ** 0		- Success.
 ** -ENOMEM	- Allocation failure.
 */
int t7xx_md_init(struct t7xx_pci_dev *t7xx_dev)
{
	struct t7xx_modem *md;
	int ret;

	md = t7xx_md_alloc(t7xx_dev);
	if (!md)
		return -ENOMEM;

	ret = t7xx_cldma_alloc(CLDMA_ID_MD, t7xx_dev);
	if (ret)
		goto err_destroy_hswq;

	ret = t7xx_cldma_alloc(CLDMA_ID_AP, t7xx_dev);
	if (ret)
		goto err_destroy_hswq;

	ret = t7xx_fsm_init(md);
	if (ret)
		goto err_destroy_hswq;

	ret = t7xx_ccmni_init(t7xx_dev);
	if (ret)
		goto err_uninit_fsm;

	ret = t7xx_cldma_init(md->md_ctrl[CLDMA_ID_MD]);
	if (ret)
		goto err_uninit_ccmni;

	ret = t7xx_cldma_init(md->md_ctrl[CLDMA_ID_AP]);
	if (ret)
		goto err_uninit_md_cldma;

	ret = t7xx_port_proxy_init(md);
	if (ret)
		goto err_uninit_ap_cldma;

	ret = t7xx_fsm_append_cmd(md->fsm_ctl, FSM_CMD_START, 0);
	if (ret) /* t7xx_fsm_uninit() flushes cmd queue */
		goto err_uninit_proxy;

	t7xx_md_sys_sw_init(t7xx_dev);
	md->md_init_finish = true;
	return 0;

err_uninit_proxy:
	t7xx_port_proxy_uninit(md->port_prox);

err_uninit_ap_cldma:
	t7xx_cldma_exit(md->md_ctrl[CLDMA_ID_AP]);

err_uninit_md_cldma:
	t7xx_cldma_exit(md->md_ctrl[CLDMA_ID_MD]);

err_uninit_ccmni:
	t7xx_ccmni_exit(t7xx_dev);

err_uninit_fsm:
	t7xx_fsm_uninit(md);

err_destroy_hswq:
	destroy_workqueue(md->handshake_wq);
	destroy_workqueue(md->ap_handshake_wq);
	dev_err(&t7xx_dev->pdev->dev, "Modem init failed\n");
	return ret;
}

void t7xx_md_exit(struct t7xx_pci_dev *t7xx_dev)
{
	struct t7xx_modem *md = t7xx_dev->md;

	t7xx_pcie_mac_clear_int(t7xx_dev, SAP_RGU_INT);

	if (!md->md_init_finish)
		return;

	t7xx_fsm_append_cmd(md->fsm_ctl, FSM_CMD_PRE_STOP, FSM_CMD_FLAG_WAIT_FOR_COMPLETION);
	t7xx_port_proxy_uninit(md->port_prox);
	t7xx_cldma_exit(md->md_ctrl[CLDMA_ID_AP]);
	t7xx_cldma_exit(md->md_ctrl[CLDMA_ID_MD]);
	t7xx_ccmni_exit(t7xx_dev);
	t7xx_fsm_uninit(md);
	destroy_workqueue(md->handshake_wq);
	destroy_workqueue(md->ap_handshake_wq);
}
