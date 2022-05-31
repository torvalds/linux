// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include "cam.h"
#include "debug.h"
#include "mac.h"
#include "ps.h"
#include "ser.h"
#include "util.h"

#define SER_RECFG_TIMEOUT 1000

enum ser_evt {
	SER_EV_NONE,
	SER_EV_STATE_IN,
	SER_EV_STATE_OUT,
	SER_EV_L1_RESET, /* M1 */
	SER_EV_DO_RECOVERY, /* M3 */
	SER_EV_MAC_RESET_DONE, /* M5 */
	SER_EV_L2_RESET,
	SER_EV_L2_RECFG_DONE,
	SER_EV_L2_RECFG_TIMEOUT,
	SER_EV_M3_TIMEOUT,
	SER_EV_FW_M5_TIMEOUT,
	SER_EV_L0_RESET,
	SER_EV_MAXX
};

enum ser_state {
	SER_IDLE_ST,
	SER_RESET_TRX_ST,
	SER_DO_HCI_ST,
	SER_L2_RESET_ST,
	SER_ST_MAX_ST
};

struct ser_msg {
	struct list_head list;
	u8 event;
};

struct state_ent {
	u8 state;
	char *name;
	void (*st_func)(struct rtw89_ser *ser, u8 event);
};

struct event_ent {
	u8 event;
	char *name;
};

static char *ser_ev_name(struct rtw89_ser *ser, u8 event)
{
	if (event < SER_EV_MAXX)
		return ser->ev_tbl[event].name;

	return "err_ev_name";
}

static char *ser_st_name(struct rtw89_ser *ser)
{
	if (ser->state < SER_ST_MAX_ST)
		return ser->st_tbl[ser->state].name;

	return "err_st_name";
}

static void ser_state_run(struct rtw89_ser *ser, u8 evt)
{
	struct rtw89_dev *rtwdev = container_of(ser, struct rtw89_dev, ser);

	rtw89_debug(rtwdev, RTW89_DBG_SER, "ser: %s receive %s\n",
		    ser_st_name(ser), ser_ev_name(ser, evt));

	rtw89_leave_lps(rtwdev);
	ser->st_tbl[ser->state].st_func(ser, evt);
}

static void ser_state_goto(struct rtw89_ser *ser, u8 new_state)
{
	struct rtw89_dev *rtwdev = container_of(ser, struct rtw89_dev, ser);

	if (ser->state == new_state || new_state >= SER_ST_MAX_ST)
		return;
	ser_state_run(ser, SER_EV_STATE_OUT);

	rtw89_debug(rtwdev, RTW89_DBG_SER, "ser: %s goto -> %s\n",
		    ser_st_name(ser), ser->st_tbl[new_state].name);

	ser->state = new_state;
	ser_state_run(ser, SER_EV_STATE_IN);
}

static struct ser_msg *__rtw89_ser_dequeue_msg(struct rtw89_ser *ser)
{
	struct ser_msg *msg;

	spin_lock_irq(&ser->msg_q_lock);
	msg = list_first_entry_or_null(&ser->msg_q, struct ser_msg, list);
	if (msg)
		list_del(&msg->list);
	spin_unlock_irq(&ser->msg_q_lock);

	return msg;
}

static void rtw89_ser_hdl_work(struct work_struct *work)
{
	struct ser_msg *msg;
	struct rtw89_ser *ser = container_of(work, struct rtw89_ser,
					     ser_hdl_work);

	while ((msg = __rtw89_ser_dequeue_msg(ser))) {
		ser_state_run(ser, msg->event);
		kfree(msg);
	}
}

static int ser_send_msg(struct rtw89_ser *ser, u8 event)
{
	struct rtw89_dev *rtwdev = container_of(ser, struct rtw89_dev, ser);
	struct ser_msg *msg = NULL;

	if (test_bit(RTW89_SER_DRV_STOP_RUN, ser->flags))
		return -EIO;

	msg = kmalloc(sizeof(*msg), GFP_ATOMIC);
	if (!msg)
		return -ENOMEM;

	msg->event = event;

	spin_lock_irq(&ser->msg_q_lock);
	list_add(&msg->list, &ser->msg_q);
	spin_unlock_irq(&ser->msg_q_lock);

	ieee80211_queue_work(rtwdev->hw, &ser->ser_hdl_work);
	return 0;
}

static void rtw89_ser_alarm_work(struct work_struct *work)
{
	struct rtw89_ser *ser = container_of(work, struct rtw89_ser,
					     ser_alarm_work.work);

	ser_send_msg(ser, ser->alarm_event);
	ser->alarm_event = SER_EV_NONE;
}

static void ser_set_alarm(struct rtw89_ser *ser, u32 ms, u8 event)
{
	struct rtw89_dev *rtwdev = container_of(ser, struct rtw89_dev, ser);

	if (test_bit(RTW89_SER_DRV_STOP_RUN, ser->flags))
		return;

	ser->alarm_event = event;
	ieee80211_queue_delayed_work(rtwdev->hw, &ser->ser_alarm_work,
				     msecs_to_jiffies(ms));
}

static void ser_del_alarm(struct rtw89_ser *ser)
{
	cancel_delayed_work(&ser->ser_alarm_work);
	ser->alarm_event = SER_EV_NONE;
}

/* driver function */
static void drv_stop_tx(struct rtw89_ser *ser)
{
	struct rtw89_dev *rtwdev = container_of(ser, struct rtw89_dev, ser);

	ieee80211_stop_queues(rtwdev->hw);
	set_bit(RTW89_SER_DRV_STOP_TX, ser->flags);
}

static void drv_stop_rx(struct rtw89_ser *ser)
{
	struct rtw89_dev *rtwdev = container_of(ser, struct rtw89_dev, ser);

	clear_bit(RTW89_FLAG_RUNNING, rtwdev->flags);
	set_bit(RTW89_SER_DRV_STOP_RX, ser->flags);
}

static void drv_trx_reset(struct rtw89_ser *ser)
{
	struct rtw89_dev *rtwdev = container_of(ser, struct rtw89_dev, ser);

	rtw89_hci_reset(rtwdev);
}

static void drv_resume_tx(struct rtw89_ser *ser)
{
	struct rtw89_dev *rtwdev = container_of(ser, struct rtw89_dev, ser);

	if (!test_bit(RTW89_SER_DRV_STOP_TX, ser->flags))
		return;

	ieee80211_wake_queues(rtwdev->hw);
	clear_bit(RTW89_SER_DRV_STOP_TX, ser->flags);
}

static void drv_resume_rx(struct rtw89_ser *ser)
{
	struct rtw89_dev *rtwdev = container_of(ser, struct rtw89_dev, ser);

	if (!test_bit(RTW89_SER_DRV_STOP_RX, ser->flags))
		return;

	set_bit(RTW89_FLAG_RUNNING, rtwdev->flags);
	clear_bit(RTW89_SER_DRV_STOP_RX, ser->flags);
}

static void ser_reset_vif(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif)
{
	rtw89_core_release_bit_map(rtwdev->hw_port, rtwvif->port);
	rtwvif->net_type = RTW89_NET_TYPE_NO_LINK;
	rtwvif->trigger = false;
}

static void ser_reset_mac_binding(struct rtw89_dev *rtwdev)
{
	struct rtw89_vif *rtwvif;

	rtw89_cam_reset_keys(rtwdev);
	rtw89_core_release_all_bits_map(rtwdev->mac_id_map, RTW89_MAX_MAC_ID_NUM);
	rtw89_for_each_rtwvif(rtwdev, rtwvif)
		ser_reset_vif(rtwdev, rtwvif);
}

/* hal function */
static int hal_enable_dma(struct rtw89_ser *ser)
{
	struct rtw89_dev *rtwdev = container_of(ser, struct rtw89_dev, ser);
	int ret;

	if (!test_bit(RTW89_SER_HAL_STOP_DMA, ser->flags))
		return 0;

	if (!rtwdev->hci.ops->mac_lv1_rcvy)
		return -EIO;

	ret = rtwdev->hci.ops->mac_lv1_rcvy(rtwdev, RTW89_LV1_RCVY_STEP_2);
	if (!ret)
		clear_bit(RTW89_SER_HAL_STOP_DMA, ser->flags);

	return ret;
}

static int hal_stop_dma(struct rtw89_ser *ser)
{
	struct rtw89_dev *rtwdev = container_of(ser, struct rtw89_dev, ser);
	int ret;

	if (!rtwdev->hci.ops->mac_lv1_rcvy)
		return -EIO;

	ret = rtwdev->hci.ops->mac_lv1_rcvy(rtwdev, RTW89_LV1_RCVY_STEP_1);
	if (!ret)
		set_bit(RTW89_SER_HAL_STOP_DMA, ser->flags);

	return ret;
}

static void hal_send_m2_event(struct rtw89_ser *ser)
{
	struct rtw89_dev *rtwdev = container_of(ser, struct rtw89_dev, ser);

	rtw89_mac_set_err_status(rtwdev, MAC_AX_ERR_L1_DISABLE_EN);
}

static void hal_send_m4_event(struct rtw89_ser *ser)
{
	struct rtw89_dev *rtwdev = container_of(ser, struct rtw89_dev, ser);

	rtw89_mac_set_err_status(rtwdev, MAC_AX_ERR_L1_RCVY_EN);
}

/* state handler */
static void ser_idle_st_hdl(struct rtw89_ser *ser, u8 evt)
{
	switch (evt) {
	case SER_EV_STATE_IN:
		break;
	case SER_EV_L1_RESET:
		ser_state_goto(ser, SER_RESET_TRX_ST);
		break;
	case SER_EV_L2_RESET:
		ser_state_goto(ser, SER_L2_RESET_ST);
		break;
	case SER_EV_STATE_OUT:
	default:
		break;
	}
}

static void ser_reset_trx_st_hdl(struct rtw89_ser *ser, u8 evt)
{
	switch (evt) {
	case SER_EV_STATE_IN:
		drv_stop_tx(ser);

		if (hal_stop_dma(ser)) {
			ser_state_goto(ser, SER_L2_RESET_ST);
			break;
		}

		drv_stop_rx(ser);
		drv_trx_reset(ser);

		/* wait m3 */
		hal_send_m2_event(ser);

		/* set alarm to prevent FW response timeout */
		ser_set_alarm(ser, 1000, SER_EV_M3_TIMEOUT);
		break;

	case SER_EV_DO_RECOVERY:
		ser_state_goto(ser, SER_DO_HCI_ST);
		break;

	case SER_EV_M3_TIMEOUT:
		ser_state_goto(ser, SER_L2_RESET_ST);
		break;

	case SER_EV_STATE_OUT:
		ser_del_alarm(ser);
		hal_enable_dma(ser);
		drv_resume_rx(ser);
		drv_resume_tx(ser);
		break;

	default:
		break;
	}
}

static void ser_do_hci_st_hdl(struct rtw89_ser *ser, u8 evt)
{
	switch (evt) {
	case SER_EV_STATE_IN:
		/* wait m5 */
		hal_send_m4_event(ser);

		/* prevent FW response timeout */
		ser_set_alarm(ser, 1000, SER_EV_FW_M5_TIMEOUT);
		break;

	case SER_EV_FW_M5_TIMEOUT:
		ser_state_goto(ser, SER_L2_RESET_ST);
		break;

	case SER_EV_MAC_RESET_DONE:
		ser_state_goto(ser, SER_IDLE_ST);
		break;

	case SER_EV_STATE_OUT:
		ser_del_alarm(ser);
		break;

	default:
		break;
	}
}

static void ser_l2_reset_st_hdl(struct rtw89_ser *ser, u8 evt)
{
	struct rtw89_dev *rtwdev = container_of(ser, struct rtw89_dev, ser);

	switch (evt) {
	case SER_EV_STATE_IN:
		mutex_lock(&rtwdev->mutex);
		ser_reset_mac_binding(rtwdev);
		rtw89_core_stop(rtwdev);
		mutex_unlock(&rtwdev->mutex);

		ieee80211_restart_hw(rtwdev->hw);
		ser_set_alarm(ser, SER_RECFG_TIMEOUT, SER_EV_L2_RECFG_TIMEOUT);
		break;

	case SER_EV_L2_RECFG_TIMEOUT:
		rtw89_info(rtwdev, "Err: ser L2 re-config timeout\n");
		fallthrough;
	case SER_EV_L2_RECFG_DONE:
		ser_state_goto(ser, SER_IDLE_ST);
		break;

	case SER_EV_STATE_OUT:
		ser_del_alarm(ser);
		break;

	default:
		break;
	}
}

static struct event_ent ser_ev_tbl[] = {
	{SER_EV_NONE, "SER_EV_NONE"},
	{SER_EV_STATE_IN, "SER_EV_STATE_IN"},
	{SER_EV_STATE_OUT, "SER_EV_STATE_OUT"},
	{SER_EV_L1_RESET, "SER_EV_L1_RESET"},
	{SER_EV_DO_RECOVERY, "SER_EV_DO_RECOVERY m3"},
	{SER_EV_MAC_RESET_DONE, "SER_EV_MAC_RESET_DONE m5"},
	{SER_EV_L2_RESET, "SER_EV_L2_RESET"},
	{SER_EV_L2_RECFG_DONE, "SER_EV_L2_RECFG_DONE"},
	{SER_EV_L2_RECFG_TIMEOUT, "SER_EV_L2_RECFG_TIMEOUT"},
	{SER_EV_M3_TIMEOUT, "SER_EV_M3_TIMEOUT"},
	{SER_EV_FW_M5_TIMEOUT, "SER_EV_FW_M5_TIMEOUT"},
	{SER_EV_L0_RESET, "SER_EV_L0_RESET"},
	{SER_EV_MAXX, "SER_EV_MAX"}
};

static struct state_ent ser_st_tbl[] = {
	{SER_IDLE_ST, "SER_IDLE_ST", ser_idle_st_hdl},
	{SER_RESET_TRX_ST, "SER_RESET_TRX_ST", ser_reset_trx_st_hdl},
	{SER_DO_HCI_ST, "SER_DO_HCI_ST", ser_do_hci_st_hdl},
	{SER_L2_RESET_ST, "SER_L2_RESET_ST", ser_l2_reset_st_hdl}
};

int rtw89_ser_init(struct rtw89_dev *rtwdev)
{
	struct rtw89_ser *ser = &rtwdev->ser;

	memset(ser, 0, sizeof(*ser));
	INIT_LIST_HEAD(&ser->msg_q);
	ser->state = SER_IDLE_ST;
	ser->st_tbl = ser_st_tbl;
	ser->ev_tbl = ser_ev_tbl;

	bitmap_zero(ser->flags, RTW89_NUM_OF_SER_FLAGS);
	spin_lock_init(&ser->msg_q_lock);
	INIT_WORK(&ser->ser_hdl_work, rtw89_ser_hdl_work);
	INIT_DELAYED_WORK(&ser->ser_alarm_work, rtw89_ser_alarm_work);
	return 0;
}

int rtw89_ser_deinit(struct rtw89_dev *rtwdev)
{
	struct rtw89_ser *ser = (struct rtw89_ser *)&rtwdev->ser;

	set_bit(RTW89_SER_DRV_STOP_RUN, ser->flags);
	cancel_delayed_work_sync(&ser->ser_alarm_work);
	cancel_work_sync(&ser->ser_hdl_work);
	clear_bit(RTW89_SER_DRV_STOP_RUN, ser->flags);
	return 0;
}

void rtw89_ser_recfg_done(struct rtw89_dev *rtwdev)
{
	ser_send_msg(&rtwdev->ser, SER_EV_L2_RECFG_DONE);
}

int rtw89_ser_notify(struct rtw89_dev *rtwdev, u32 err)
{
	u8 event = SER_EV_NONE;

	rtw89_info(rtwdev, "ser event = 0x%04x\n", err);

	switch (err) {
	case MAC_AX_ERR_L1_ERR_DMAC:
	case MAC_AX_ERR_L0_PROMOTE_TO_L1:
		event = SER_EV_L1_RESET; /* M1 */
		break;
	case MAC_AX_ERR_L1_RESET_DISABLE_DMAC_DONE:
		event = SER_EV_DO_RECOVERY; /* M3 */
		break;
	case MAC_AX_ERR_L1_RESET_RECOVERY_DONE:
		event = SER_EV_MAC_RESET_DONE; /* M5 */
		break;
	case MAC_AX_ERR_L0_ERR_CMAC0:
	case MAC_AX_ERR_L0_ERR_CMAC1:
	case MAC_AX_ERR_L0_RESET_DONE:
		event = SER_EV_L0_RESET;
		break;
	default:
		if (err == MAC_AX_ERR_L1_PROMOTE_TO_L2 ||
		    (err >= MAC_AX_ERR_L2_ERR_AH_DMA &&
		     err <= MAC_AX_GET_ERR_MAX))
			event = SER_EV_L2_RESET;
		break;
	}

	if (event == SER_EV_NONE)
		return -EINVAL;

	ser_send_msg(&rtwdev->ser, event);
	return 0;
}
EXPORT_SYMBOL(rtw89_ser_notify);
