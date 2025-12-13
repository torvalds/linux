// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2019-2020  Realtek Corporation
 */

#include <linux/devcoredump.h>

#include "cam.h"
#include "chan.h"
#include "debug.h"
#include "fw.h"
#include "mac.h"
#include "ps.h"
#include "reg.h"
#include "ser.h"
#include "util.h"

#define SER_RECFG_TIMEOUT 1000

enum ser_evt {
	SER_EV_NONE,
	SER_EV_STATE_IN,
	SER_EV_STATE_OUT,
	SER_EV_L1_RESET_PREPARE, /* pre-M0 */
	SER_EV_L1_RESET, /* M1 */
	SER_EV_DO_RECOVERY, /* M3 */
	SER_EV_MAC_RESET_DONE, /* M5 */
	SER_EV_L2_RESET,
	SER_EV_L2_RECFG_DONE,
	SER_EV_L2_RECFG_TIMEOUT,
	SER_EV_M1_TIMEOUT,
	SER_EV_M3_TIMEOUT,
	SER_EV_FW_M5_TIMEOUT,
	SER_EV_L0_RESET,
	SER_EV_MAXX
};

enum ser_state {
	SER_IDLE_ST,
	SER_L1_RESET_PRE_ST,
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

#define RTW89_DEF_SER_CD_TYPE(_name, _type, _size) \
struct ser_cd_ ## _name { \
	u32 type; \
	u32 type_size; \
	u64 padding; \
	u8 data[_size]; \
} __packed; \
static void ser_cd_ ## _name ## _init(struct ser_cd_ ## _name *p) \
{ \
	p->type = _type; \
	p->type_size = sizeof(p->data); \
	p->padding = 0x0123456789abcdef; \
}

enum rtw89_ser_cd_type {
	RTW89_SER_CD_FW_RSVD_PLE	= 0,
	RTW89_SER_CD_FW_BACKTRACE	= 1,
};

RTW89_DEF_SER_CD_TYPE(fw_rsvd_ple,
		      RTW89_SER_CD_FW_RSVD_PLE,
		      RTW89_FW_RSVD_PLE_SIZE);

RTW89_DEF_SER_CD_TYPE(fw_backtrace,
		      RTW89_SER_CD_FW_BACKTRACE,
		      RTW89_FW_BACKTRACE_MAX_SIZE);

struct rtw89_ser_cd_buffer {
	struct ser_cd_fw_rsvd_ple fwple;
	struct ser_cd_fw_backtrace fwbt;
} __packed;

static struct rtw89_ser_cd_buffer *rtw89_ser_cd_prep(struct rtw89_dev *rtwdev)
{
	struct rtw89_ser_cd_buffer *buf;

	buf = vzalloc(sizeof(*buf));
	if (!buf)
		return NULL;

	ser_cd_fw_rsvd_ple_init(&buf->fwple);
	ser_cd_fw_backtrace_init(&buf->fwbt);

	return buf;
}

static void rtw89_ser_cd_send(struct rtw89_dev *rtwdev,
			      struct rtw89_ser_cd_buffer *buf)
{
	rtw89_debug(rtwdev, RTW89_DBG_SER, "SER sends core dump\n");

	/* After calling dev_coredump, buf's lifetime is supposed to be
	 * handled by the device coredump framework. Note that a new dump
	 * will be discarded if a previous one hasn't been released by
	 * framework yet.
	 */
	dev_coredumpv(rtwdev->dev, buf, sizeof(*buf), GFP_KERNEL);
}

static void rtw89_ser_cd_free(struct rtw89_dev *rtwdev,
			      struct rtw89_ser_cd_buffer *buf, bool free_self)
{
	if (!free_self)
		return;

	rtw89_debug(rtwdev, RTW89_DBG_SER, "SER frees core dump by self\n");

	/* When some problems happen during filling data of core dump,
	 * we won't send it to device coredump framework. Instead, we
	 * free buf by ourselves.
	 */
	vfree(buf);
}

static void ser_state_run(struct rtw89_ser *ser, u8 evt)
{
	struct rtw89_dev *rtwdev = container_of(ser, struct rtw89_dev, ser);

	rtw89_debug(rtwdev, RTW89_DBG_SER, "ser: %s receive %s\n",
		    ser_st_name(ser), ser_ev_name(ser, evt));

	wiphy_lock(rtwdev->hw->wiphy);
	rtw89_leave_lps(rtwdev);
	wiphy_unlock(rtwdev->hw->wiphy);

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

	schedule_work(&ser->ser_hdl_work);
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
	struct rtw89_vif_link *rtwvif_link;
	unsigned int link_id;

	rtwvif->tdls_peer = 0;

	rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id) {
		rtw89_core_release_bit_map(rtwdev->hw_port, rtwvif_link->port);
		rtwvif_link->net_type = RTW89_NET_TYPE_NO_LINK;
		rtwvif_link->trigger = false;
		rtwvif_link->rand_tsf_done = false;

		rtw89_p2p_noa_once_deinit(rtwvif_link);
	}
}

static void ser_sta_deinit_cam_iter(void *data, struct ieee80211_sta *sta)
{
	struct rtw89_vif *target_rtwvif = (struct rtw89_vif *)data;
	struct rtw89_sta *rtwsta = sta_to_rtwsta(sta);
	struct rtw89_vif *rtwvif = rtwsta->rtwvif;
	struct rtw89_dev *rtwdev = rtwvif->rtwdev;
	struct rtw89_vif_link *rtwvif_link;
	struct rtw89_sta_link *rtwsta_link;
	unsigned int link_id;

	if (rtwvif != target_rtwvif)
		return;

	rtw89_sta_for_each_link(rtwsta, rtwsta_link, link_id) {
		rtwvif_link = rtwsta_link->rtwvif_link;

		if (rtwvif_link->net_type == RTW89_NET_TYPE_AP_MODE || sta->tdls)
			rtw89_cam_deinit_addr_cam(rtwdev, &rtwsta_link->addr_cam);
		if (sta->tdls)
			rtw89_cam_deinit_bssid_cam(rtwdev, &rtwsta_link->bssid_cam);

		INIT_LIST_HEAD(&rtwsta_link->ba_cam_list);
	}
}

static void ser_deinit_cam(struct rtw89_dev *rtwdev, struct rtw89_vif *rtwvif)
{
	struct rtw89_vif_link *rtwvif_link;
	unsigned int link_id;

	ieee80211_iterate_stations_atomic(rtwdev->hw,
					  ser_sta_deinit_cam_iter,
					  rtwvif);

	rtw89_vif_for_each_link(rtwvif, rtwvif_link, link_id)
		rtw89_cam_deinit(rtwdev, rtwvif_link);

	bitmap_zero(rtwdev->cam_info.ba_cam_map, RTW89_MAX_BA_CAM_NUM);
}

static void ser_reset_mac_binding(struct rtw89_dev *rtwdev)
{
	struct rtw89_vif *rtwvif;

	rtw89_cam_reset_keys(rtwdev);
	rtw89_for_each_rtwvif(rtwdev, rtwvif)
		ser_deinit_cam(rtwdev, rtwvif);

	rtw89_core_release_all_bits_map(rtwdev->mac_id_map, RTW89_MAX_MAC_ID_NUM);
	rtw89_for_each_rtwvif(rtwdev, rtwvif)
		ser_reset_vif(rtwdev, rtwvif);

	rtwdev->total_sta_assoc = 0;
	refcount_set(&rtwdev->refcount_ap_info, 0);
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
	else
		rtw89_debug(rtwdev, RTW89_DBG_SER,
			    "lv1 rcvy fail to start dma: %d\n", ret);

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
	else
		rtw89_debug(rtwdev, RTW89_DBG_SER,
			    "lv1 rcvy fail to stop dma: %d\n", ret);

	return ret;
}

static void hal_send_post_m0_event(struct rtw89_ser *ser)
{
	struct rtw89_dev *rtwdev = container_of(ser, struct rtw89_dev, ser);

	rtw89_mac_set_err_status(rtwdev, MAC_AX_ERR_L1_RESET_START_DMAC);
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
	struct rtw89_dev *rtwdev = container_of(ser, struct rtw89_dev, ser);

	switch (evt) {
	case SER_EV_STATE_IN:
		rtw89_hci_recovery_complete(rtwdev);
		clear_bit(RTW89_FLAG_SER_HANDLING, rtwdev->flags);
		clear_bit(RTW89_FLAG_CRASH_SIMULATING, rtwdev->flags);
		break;
	case SER_EV_L1_RESET_PREPARE:
		ser_state_goto(ser, SER_L1_RESET_PRE_ST);
		break;
	case SER_EV_L1_RESET:
		ser_state_goto(ser, SER_RESET_TRX_ST);
		break;
	case SER_EV_L2_RESET:
		ser_state_goto(ser, SER_L2_RESET_ST);
		break;
	case SER_EV_STATE_OUT:
		set_bit(RTW89_FLAG_SER_HANDLING, rtwdev->flags);
		rtw89_hci_recovery_start(rtwdev);
		break;
	default:
		break;
	}
}

static void ser_l1_reset_pre_st_hdl(struct rtw89_ser *ser, u8 evt)
{
	switch (evt) {
	case SER_EV_STATE_IN:
		ser->prehandle_l1 = true;
		hal_send_post_m0_event(ser);
		ser_set_alarm(ser, 1000, SER_EV_M1_TIMEOUT);
		break;
	case SER_EV_L1_RESET:
		ser_state_goto(ser, SER_RESET_TRX_ST);
		break;
	case SER_EV_M1_TIMEOUT:
		ser_state_goto(ser, SER_L2_RESET_ST);
		break;
	case SER_EV_STATE_OUT:
		ser_del_alarm(ser);
		break;
	default:
		break;
	}
}

static void ser_reset_trx_st_hdl(struct rtw89_ser *ser, u8 evt)
{
	struct rtw89_dev *rtwdev = container_of(ser, struct rtw89_dev, ser);
	struct wiphy *wiphy = rtwdev->hw->wiphy;

	switch (evt) {
	case SER_EV_STATE_IN:
		wiphy_lock(wiphy);
		wiphy_delayed_work_cancel(wiphy, &rtwdev->track_work);
		wiphy_delayed_work_cancel(wiphy, &rtwdev->track_ps_work);
		wiphy_unlock(wiphy);
		drv_stop_tx(ser);

		if (hal_stop_dma(ser)) {
			ser_state_goto(ser, SER_L2_RESET_ST);
			break;
		}

		drv_stop_rx(ser);
		wiphy_lock(wiphy);
		drv_trx_reset(ser);
		wiphy_unlock(wiphy);

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
		wiphy_delayed_work_queue(wiphy, &rtwdev->track_work,
					 RTW89_TRACK_WORK_PERIOD);
		wiphy_delayed_work_queue(wiphy, &rtwdev->track_ps_work,
					 RTW89_TRACK_PS_WORK_PERIOD);
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

static void ser_mac_mem_dump(struct rtw89_dev *rtwdev, u8 *buf,
			     u8 sel, u32 start_addr, u32 len)
{
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	u32 filter_model_addr = mac->filter_model_addr;
	u32 indir_access_addr = mac->indir_access_addr;
	u32 mem_page_size = mac->mem_page_size;
	u32 *ptr = (u32 *)buf;
	u32 base_addr, start_page, residue;
	u32 cnt = 0;
	u32 i;

	start_page = start_addr / mem_page_size;
	residue = start_addr % mem_page_size;
	base_addr = mac->mem_base_addrs[sel];
	base_addr += start_page * mem_page_size;

	while (cnt < len) {
		rtw89_write32(rtwdev, filter_model_addr, base_addr);

		for (i = indir_access_addr + residue;
		     i < indir_access_addr + mem_page_size;
		     i += 4, ptr++) {
			*ptr = rtw89_read32(rtwdev, i);
			cnt += 4;
			if (cnt >= len)
				break;
		}

		residue = 0;
		base_addr += mem_page_size;
	}
}

static void rtw89_ser_fw_rsvd_ple_dump(struct rtw89_dev *rtwdev, u8 *buf)
{
	u32 start_addr = rtwdev->chip->rsvd_ple_ofst;

	rtw89_debug(rtwdev, RTW89_DBG_SER,
		    "dump mem for fw rsvd payload engine (start addr: 0x%x)\n",
		    start_addr);
	ser_mac_mem_dump(rtwdev, buf, RTW89_MAC_MEM_SHARED_BUF, start_addr,
			 RTW89_FW_RSVD_PLE_SIZE);
}

struct __fw_backtrace_entry {
	u32 wcpu_addr;
	u32 size;
	u32 key;
} __packed;

struct __fw_backtrace_info {
	u32 ra;
	u32 sp;
} __packed;

static_assert(RTW89_FW_BACKTRACE_INFO_SIZE ==
	      sizeof(struct __fw_backtrace_info));

static u32 convert_addr_from_wcpu(u32 wcpu_addr)
{
	if (wcpu_addr < 0x30000000)
		return wcpu_addr;

	return wcpu_addr & GENMASK(28, 0);
}

static int rtw89_ser_fw_backtrace_dump(struct rtw89_dev *rtwdev, u8 *buf,
				       const struct __fw_backtrace_entry *ent)
{
	struct __fw_backtrace_info *ptr = (struct __fw_backtrace_info *)buf;
	const struct rtw89_mac_gen_def *mac = rtwdev->chip->mac_def;
	u32 filter_model_addr = mac->filter_model_addr;
	u32 indir_access_addr = mac->indir_access_addr;
	u32 fwbt_addr = convert_addr_from_wcpu(ent->wcpu_addr);
	u32 fwbt_size = ent->size;
	u32 fwbt_key = ent->key;
	u32 i;

	if (fwbt_addr == 0) {
		rtw89_warn(rtwdev, "FW backtrace invalid address: 0x%x\n",
			   fwbt_addr);
		return -EINVAL;
	}

	if (fwbt_key != RTW89_FW_BACKTRACE_KEY) {
		rtw89_warn(rtwdev, "FW backtrace invalid key: 0x%x\n",
			   fwbt_key);
		return -EINVAL;
	}

	if (fwbt_size == 0 || !RTW89_VALID_FW_BACKTRACE_SIZE(fwbt_size) ||
	    fwbt_size > RTW89_FW_BACKTRACE_MAX_SIZE) {
		rtw89_warn(rtwdev, "FW backtrace invalid size: 0x%x\n",
			   fwbt_size);
		return -EINVAL;
	}

	rtw89_debug(rtwdev, RTW89_DBG_SER, "dump fw backtrace start\n");
	rtw89_write32(rtwdev, filter_model_addr, fwbt_addr);

	for (i = indir_access_addr;
	     i < indir_access_addr + fwbt_size;
	     i += RTW89_FW_BACKTRACE_INFO_SIZE, ptr++) {
		*ptr = (struct __fw_backtrace_info){
			.ra = rtw89_read32(rtwdev, i),
			.sp = rtw89_read32(rtwdev, i + 4),
		};
		rtw89_debug(rtwdev, RTW89_DBG_SER,
			    "next sp: 0x%x, next ra: 0x%x\n",
			    ptr->sp, ptr->ra);
	}

	rtw89_debug(rtwdev, RTW89_DBG_SER, "dump fw backtrace end\n");
	return 0;
}

static void ser_l2_reset_st_pre_hdl(struct rtw89_ser *ser)
{
	struct rtw89_dev *rtwdev = container_of(ser, struct rtw89_dev, ser);
	struct rtw89_ser_cd_buffer *buf;
	struct __fw_backtrace_entry fwbt_ent;
	int ret = 0;

	buf = rtw89_ser_cd_prep(rtwdev);
	if (!buf) {
		ret = -ENOMEM;
		goto bottom;
	}

	rtw89_ser_fw_rsvd_ple_dump(rtwdev, buf->fwple.data);

	fwbt_ent = *(struct __fw_backtrace_entry *)buf->fwple.data;
	ret = rtw89_ser_fw_backtrace_dump(rtwdev, buf->fwbt.data, &fwbt_ent);
	if (ret)
		goto bottom;

	rtw89_ser_cd_send(rtwdev, buf);

bottom:
	rtw89_ser_cd_free(rtwdev, buf, !!ret);

	ser_reset_mac_binding(rtwdev);
	rtw89_core_stop(rtwdev);
	rtw89_entity_init(rtwdev);
	rtw89_fw_release_general_pkt_list(rtwdev, false);
	INIT_LIST_HEAD(&rtwdev->rtwvifs_list);
}

static void ser_l2_reset_st_hdl(struct rtw89_ser *ser, u8 evt)
{
	struct rtw89_dev *rtwdev = container_of(ser, struct rtw89_dev, ser);

	switch (evt) {
	case SER_EV_STATE_IN:
		wiphy_lock(rtwdev->hw->wiphy);
		ser_l2_reset_st_pre_hdl(ser);
		wiphy_unlock(rtwdev->hw->wiphy);

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

static const struct event_ent ser_ev_tbl[] = {
	{SER_EV_NONE, "SER_EV_NONE"},
	{SER_EV_STATE_IN, "SER_EV_STATE_IN"},
	{SER_EV_STATE_OUT, "SER_EV_STATE_OUT"},
	{SER_EV_L1_RESET_PREPARE, "SER_EV_L1_RESET_PREPARE pre-m0"},
	{SER_EV_L1_RESET, "SER_EV_L1_RESET m1"},
	{SER_EV_DO_RECOVERY, "SER_EV_DO_RECOVERY m3"},
	{SER_EV_MAC_RESET_DONE, "SER_EV_MAC_RESET_DONE m5"},
	{SER_EV_L2_RESET, "SER_EV_L2_RESET"},
	{SER_EV_L2_RECFG_DONE, "SER_EV_L2_RECFG_DONE"},
	{SER_EV_L2_RECFG_TIMEOUT, "SER_EV_L2_RECFG_TIMEOUT"},
	{SER_EV_M1_TIMEOUT, "SER_EV_M1_TIMEOUT"},
	{SER_EV_M3_TIMEOUT, "SER_EV_M3_TIMEOUT"},
	{SER_EV_FW_M5_TIMEOUT, "SER_EV_FW_M5_TIMEOUT"},
	{SER_EV_L0_RESET, "SER_EV_L0_RESET"},
	{SER_EV_MAXX, "SER_EV_MAX"}
};

static const struct state_ent ser_st_tbl[] = {
	{SER_IDLE_ST, "SER_IDLE_ST", ser_idle_st_hdl},
	{SER_L1_RESET_PRE_ST, "SER_L1_RESET_PRE_ST", ser_l1_reset_pre_st_hdl},
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

	rtw89_info(rtwdev, "SER catches error: 0x%x\n", err);

	switch (err) {
	case MAC_AX_ERR_L1_PREERR_DMAC: /* pre-M0 */
		event = SER_EV_L1_RESET_PREPARE;
		break;
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

	if (event == SER_EV_NONE) {
		rtw89_warn(rtwdev, "SER cannot recognize error: 0x%x\n", err);
		return -EINVAL;
	}

	ser_send_msg(&rtwdev->ser, event);
	return 0;
}
EXPORT_SYMBOL(rtw89_ser_notify);
