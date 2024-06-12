// SPDX-License-Identifier: GPL-2.0+
/*
 * OTG Finite State Machine from OTG spec
 *
 * Copyright (C) 2007,2008 Freescale Semiconductor, Inc.
 *
 * Author:	Li Yang <LeoLi@freescale.com>
 *		Jerry Huang <Chang-Ming.Huang@freescale.com>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/usb.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>
#include <linux/usb/otg-fsm.h>

#ifdef VERBOSE
#define VDBG(fmt, args...) pr_debug("[%s]  " fmt, \
				 __func__, ## args)
#else
#define VDBG(stuff...)	do {} while (0)
#endif

/* Change USB protocol when there is a protocol change */
static int otg_set_protocol(struct otg_fsm *fsm, int protocol)
{
	int ret = 0;

	if (fsm->protocol != protocol) {
		VDBG("Changing role fsm->protocol= %d; new protocol= %d\n",
			fsm->protocol, protocol);
		/* stop old protocol */
		if (fsm->protocol == PROTO_HOST)
			ret = otg_start_host(fsm, 0);
		else if (fsm->protocol == PROTO_GADGET)
			ret = otg_start_gadget(fsm, 0);
		if (ret)
			return ret;

		/* start new protocol */
		if (protocol == PROTO_HOST)
			ret = otg_start_host(fsm, 1);
		else if (protocol == PROTO_GADGET)
			ret = otg_start_gadget(fsm, 1);
		if (ret)
			return ret;

		fsm->protocol = protocol;
		return 0;
	}

	return 0;
}

/* Called when leaving a state.  Do state clean up jobs here */
static void otg_leave_state(struct otg_fsm *fsm, enum usb_otg_state old_state)
{
	switch (old_state) {
	case OTG_STATE_B_IDLE:
		otg_del_timer(fsm, B_SE0_SRP);
		fsm->b_se0_srp = 0;
		fsm->adp_sns = 0;
		fsm->adp_prb = 0;
		break;
	case OTG_STATE_B_SRP_INIT:
		fsm->data_pulse = 0;
		fsm->b_srp_done = 0;
		break;
	case OTG_STATE_B_PERIPHERAL:
		if (fsm->otg->gadget)
			fsm->otg->gadget->host_request_flag = 0;
		break;
	case OTG_STATE_B_WAIT_ACON:
		otg_del_timer(fsm, B_ASE0_BRST);
		fsm->b_ase0_brst_tmout = 0;
		break;
	case OTG_STATE_B_HOST:
		break;
	case OTG_STATE_A_IDLE:
		fsm->adp_prb = 0;
		break;
	case OTG_STATE_A_WAIT_VRISE:
		otg_del_timer(fsm, A_WAIT_VRISE);
		fsm->a_wait_vrise_tmout = 0;
		break;
	case OTG_STATE_A_WAIT_BCON:
		otg_del_timer(fsm, A_WAIT_BCON);
		fsm->a_wait_bcon_tmout = 0;
		break;
	case OTG_STATE_A_HOST:
		otg_del_timer(fsm, A_WAIT_ENUM);
		break;
	case OTG_STATE_A_SUSPEND:
		otg_del_timer(fsm, A_AIDL_BDIS);
		fsm->a_aidl_bdis_tmout = 0;
		fsm->a_suspend_req_inf = 0;
		break;
	case OTG_STATE_A_PERIPHERAL:
		otg_del_timer(fsm, A_BIDL_ADIS);
		fsm->a_bidl_adis_tmout = 0;
		if (fsm->otg->gadget)
			fsm->otg->gadget->host_request_flag = 0;
		break;
	case OTG_STATE_A_WAIT_VFALL:
		otg_del_timer(fsm, A_WAIT_VFALL);
		fsm->a_wait_vfall_tmout = 0;
		otg_del_timer(fsm, A_WAIT_VRISE);
		break;
	case OTG_STATE_A_VBUS_ERR:
		break;
	default:
		break;
	}
}

static void otg_hnp_polling_work(struct work_struct *work)
{
	struct otg_fsm *fsm = container_of(to_delayed_work(work),
				struct otg_fsm, hnp_polling_work);
	struct usb_device *udev;
	enum usb_otg_state state = fsm->otg->state;
	u8 flag;
	int retval;

	if (state != OTG_STATE_A_HOST && state != OTG_STATE_B_HOST)
		return;

	udev = usb_hub_find_child(fsm->otg->host->root_hub, 1);
	if (!udev) {
		dev_err(fsm->otg->host->controller,
			"no usb dev connected, can't start HNP polling\n");
		return;
	}

	*fsm->host_req_flag = 0;
	/* Get host request flag from connected USB device */
	retval = usb_control_msg(udev,
				usb_rcvctrlpipe(udev, 0),
				USB_REQ_GET_STATUS,
				USB_DIR_IN | USB_RECIP_DEVICE,
				0,
				OTG_STS_SELECTOR,
				fsm->host_req_flag,
				1,
				USB_CTRL_GET_TIMEOUT);
	if (retval != 1) {
		dev_err(&udev->dev, "Get one byte OTG status failed\n");
		return;
	}

	flag = *fsm->host_req_flag;
	if (flag == 0) {
		/* Continue HNP polling */
		schedule_delayed_work(&fsm->hnp_polling_work,
					msecs_to_jiffies(T_HOST_REQ_POLL));
		return;
	} else if (flag != HOST_REQUEST_FLAG) {
		dev_err(&udev->dev, "host request flag %d is invalid\n", flag);
		return;
	}

	/* Host request flag is set */
	if (state == OTG_STATE_A_HOST) {
		/* Set b_hnp_enable */
		if (!fsm->otg->host->b_hnp_enable) {
			retval = usb_control_msg(udev,
					usb_sndctrlpipe(udev, 0),
					USB_REQ_SET_FEATURE, 0,
					USB_DEVICE_B_HNP_ENABLE,
					0, NULL, 0,
					USB_CTRL_SET_TIMEOUT);
			if (retval >= 0)
				fsm->otg->host->b_hnp_enable = 1;
		}
		fsm->a_bus_req = 0;
	} else if (state == OTG_STATE_B_HOST) {
		fsm->b_bus_req = 0;
	}

	otg_statemachine(fsm);
}

static void otg_start_hnp_polling(struct otg_fsm *fsm)
{
	/*
	 * The memory of host_req_flag should be allocated by
	 * controller driver, otherwise, hnp polling is not started.
	 */
	if (!fsm->host_req_flag)
		return;

	if (!fsm->hnp_work_inited) {
		INIT_DELAYED_WORK(&fsm->hnp_polling_work, otg_hnp_polling_work);
		fsm->hnp_work_inited = true;
	}

	schedule_delayed_work(&fsm->hnp_polling_work,
					msecs_to_jiffies(T_HOST_REQ_POLL));
}

/* Called when entering a state */
static int otg_set_state(struct otg_fsm *fsm, enum usb_otg_state new_state)
{
	if (fsm->otg->state == new_state)
		return 0;
	VDBG("Set state: %s\n", usb_otg_state_string(new_state));
	otg_leave_state(fsm, fsm->otg->state);
	switch (new_state) {
	case OTG_STATE_B_IDLE:
		otg_drv_vbus(fsm, 0);
		otg_chrg_vbus(fsm, 0);
		otg_loc_conn(fsm, 0);
		otg_loc_sof(fsm, 0);
		/*
		 * Driver is responsible for starting ADP probing
		 * if ADP sensing times out.
		 */
		otg_start_adp_sns(fsm);
		otg_set_protocol(fsm, PROTO_UNDEF);
		otg_add_timer(fsm, B_SE0_SRP);
		break;
	case OTG_STATE_B_SRP_INIT:
		otg_start_pulse(fsm);
		otg_loc_sof(fsm, 0);
		otg_set_protocol(fsm, PROTO_UNDEF);
		otg_add_timer(fsm, B_SRP_FAIL);
		break;
	case OTG_STATE_B_PERIPHERAL:
		otg_chrg_vbus(fsm, 0);
		otg_loc_sof(fsm, 0);
		otg_set_protocol(fsm, PROTO_GADGET);
		otg_loc_conn(fsm, 1);
		break;
	case OTG_STATE_B_WAIT_ACON:
		otg_chrg_vbus(fsm, 0);
		otg_loc_conn(fsm, 0);
		otg_loc_sof(fsm, 0);
		otg_set_protocol(fsm, PROTO_HOST);
		otg_add_timer(fsm, B_ASE0_BRST);
		fsm->a_bus_suspend = 0;
		break;
	case OTG_STATE_B_HOST:
		otg_chrg_vbus(fsm, 0);
		otg_loc_conn(fsm, 0);
		otg_loc_sof(fsm, 1);
		otg_set_protocol(fsm, PROTO_HOST);
		usb_bus_start_enum(fsm->otg->host,
				fsm->otg->host->otg_port);
		otg_start_hnp_polling(fsm);
		break;
	case OTG_STATE_A_IDLE:
		otg_drv_vbus(fsm, 0);
		otg_chrg_vbus(fsm, 0);
		otg_loc_conn(fsm, 0);
		otg_loc_sof(fsm, 0);
		otg_start_adp_prb(fsm);
		otg_set_protocol(fsm, PROTO_HOST);
		break;
	case OTG_STATE_A_WAIT_VRISE:
		otg_drv_vbus(fsm, 1);
		otg_loc_conn(fsm, 0);
		otg_loc_sof(fsm, 0);
		otg_set_protocol(fsm, PROTO_HOST);
		otg_add_timer(fsm, A_WAIT_VRISE);
		break;
	case OTG_STATE_A_WAIT_BCON:
		otg_drv_vbus(fsm, 1);
		otg_loc_conn(fsm, 0);
		otg_loc_sof(fsm, 0);
		otg_set_protocol(fsm, PROTO_HOST);
		otg_add_timer(fsm, A_WAIT_BCON);
		break;
	case OTG_STATE_A_HOST:
		otg_drv_vbus(fsm, 1);
		otg_loc_conn(fsm, 0);
		otg_loc_sof(fsm, 1);
		otg_set_protocol(fsm, PROTO_HOST);
		/*
		 * When HNP is triggered while a_bus_req = 0, a_host will
		 * suspend too fast to complete a_set_b_hnp_en
		 */
		if (!fsm->a_bus_req || fsm->a_suspend_req_inf)
			otg_add_timer(fsm, A_WAIT_ENUM);
		otg_start_hnp_polling(fsm);
		break;
	case OTG_STATE_A_SUSPEND:
		otg_drv_vbus(fsm, 1);
		otg_loc_conn(fsm, 0);
		otg_loc_sof(fsm, 0);
		otg_set_protocol(fsm, PROTO_HOST);
		otg_add_timer(fsm, A_AIDL_BDIS);

		break;
	case OTG_STATE_A_PERIPHERAL:
		otg_loc_sof(fsm, 0);
		otg_set_protocol(fsm, PROTO_GADGET);
		otg_drv_vbus(fsm, 1);
		otg_loc_conn(fsm, 1);
		otg_add_timer(fsm, A_BIDL_ADIS);
		break;
	case OTG_STATE_A_WAIT_VFALL:
		otg_drv_vbus(fsm, 0);
		otg_loc_conn(fsm, 0);
		otg_loc_sof(fsm, 0);
		otg_set_protocol(fsm, PROTO_HOST);
		otg_add_timer(fsm, A_WAIT_VFALL);
		break;
	case OTG_STATE_A_VBUS_ERR:
		otg_drv_vbus(fsm, 0);
		otg_loc_conn(fsm, 0);
		otg_loc_sof(fsm, 0);
		otg_set_protocol(fsm, PROTO_UNDEF);
		break;
	default:
		break;
	}

	fsm->otg->state = new_state;
	fsm->state_changed = 1;
	return 0;
}

/* State change judgement */
int otg_statemachine(struct otg_fsm *fsm)
{
	enum usb_otg_state state;

	mutex_lock(&fsm->lock);

	state = fsm->otg->state;
	fsm->state_changed = 0;
	/* State machine state change judgement */

	switch (state) {
	case OTG_STATE_UNDEFINED:
		VDBG("fsm->id = %d\n", fsm->id);
		if (fsm->id)
			otg_set_state(fsm, OTG_STATE_B_IDLE);
		else
			otg_set_state(fsm, OTG_STATE_A_IDLE);
		break;
	case OTG_STATE_B_IDLE:
		if (!fsm->id)
			otg_set_state(fsm, OTG_STATE_A_IDLE);
		else if (fsm->b_sess_vld && fsm->otg->gadget)
			otg_set_state(fsm, OTG_STATE_B_PERIPHERAL);
		else if ((fsm->b_bus_req || fsm->adp_change || fsm->power_up) &&
				fsm->b_ssend_srp && fsm->b_se0_srp)
			otg_set_state(fsm, OTG_STATE_B_SRP_INIT);
		break;
	case OTG_STATE_B_SRP_INIT:
		if (!fsm->id || fsm->b_srp_done)
			otg_set_state(fsm, OTG_STATE_B_IDLE);
		break;
	case OTG_STATE_B_PERIPHERAL:
		if (!fsm->id || !fsm->b_sess_vld)
			otg_set_state(fsm, OTG_STATE_B_IDLE);
		else if (fsm->b_bus_req && fsm->otg->
				gadget->b_hnp_enable && fsm->a_bus_suspend)
			otg_set_state(fsm, OTG_STATE_B_WAIT_ACON);
		break;
	case OTG_STATE_B_WAIT_ACON:
		if (fsm->a_conn)
			otg_set_state(fsm, OTG_STATE_B_HOST);
		else if (!fsm->id || !fsm->b_sess_vld)
			otg_set_state(fsm, OTG_STATE_B_IDLE);
		else if (fsm->a_bus_resume || fsm->b_ase0_brst_tmout) {
			fsm->b_ase0_brst_tmout = 0;
			otg_set_state(fsm, OTG_STATE_B_PERIPHERAL);
		}
		break;
	case OTG_STATE_B_HOST:
		if (!fsm->id || !fsm->b_sess_vld)
			otg_set_state(fsm, OTG_STATE_B_IDLE);
		else if (!fsm->b_bus_req || !fsm->a_conn || fsm->test_device)
			otg_set_state(fsm, OTG_STATE_B_PERIPHERAL);
		break;
	case OTG_STATE_A_IDLE:
		if (fsm->id)
			otg_set_state(fsm, OTG_STATE_B_IDLE);
		else if (!fsm->a_bus_drop && (fsm->a_bus_req ||
			  fsm->a_srp_det || fsm->adp_change || fsm->power_up))
			otg_set_state(fsm, OTG_STATE_A_WAIT_VRISE);
		break;
	case OTG_STATE_A_WAIT_VRISE:
		if (fsm->a_vbus_vld)
			otg_set_state(fsm, OTG_STATE_A_WAIT_BCON);
		else if (fsm->id || fsm->a_bus_drop ||
				fsm->a_wait_vrise_tmout)
			otg_set_state(fsm, OTG_STATE_A_WAIT_VFALL);
		break;
	case OTG_STATE_A_WAIT_BCON:
		if (!fsm->a_vbus_vld)
			otg_set_state(fsm, OTG_STATE_A_VBUS_ERR);
		else if (fsm->b_conn)
			otg_set_state(fsm, OTG_STATE_A_HOST);
		else if (fsm->id || fsm->a_bus_drop || fsm->a_wait_bcon_tmout)
			otg_set_state(fsm, OTG_STATE_A_WAIT_VFALL);
		break;
	case OTG_STATE_A_HOST:
		if (fsm->id || fsm->a_bus_drop)
			otg_set_state(fsm, OTG_STATE_A_WAIT_VFALL);
		else if ((!fsm->a_bus_req || fsm->a_suspend_req_inf) &&
				fsm->otg->host->b_hnp_enable)
			otg_set_state(fsm, OTG_STATE_A_SUSPEND);
		else if (!fsm->b_conn)
			otg_set_state(fsm, OTG_STATE_A_WAIT_BCON);
		else if (!fsm->a_vbus_vld)
			otg_set_state(fsm, OTG_STATE_A_VBUS_ERR);
		break;
	case OTG_STATE_A_SUSPEND:
		if (!fsm->b_conn && fsm->otg->host->b_hnp_enable)
			otg_set_state(fsm, OTG_STATE_A_PERIPHERAL);
		else if (!fsm->b_conn && !fsm->otg->host->b_hnp_enable)
			otg_set_state(fsm, OTG_STATE_A_WAIT_BCON);
		else if (fsm->a_bus_req || fsm->b_bus_resume)
			otg_set_state(fsm, OTG_STATE_A_HOST);
		else if (fsm->id || fsm->a_bus_drop || fsm->a_aidl_bdis_tmout)
			otg_set_state(fsm, OTG_STATE_A_WAIT_VFALL);
		else if (!fsm->a_vbus_vld)
			otg_set_state(fsm, OTG_STATE_A_VBUS_ERR);
		break;
	case OTG_STATE_A_PERIPHERAL:
		if (fsm->id || fsm->a_bus_drop)
			otg_set_state(fsm, OTG_STATE_A_WAIT_VFALL);
		else if (fsm->a_bidl_adis_tmout || fsm->b_bus_suspend)
			otg_set_state(fsm, OTG_STATE_A_WAIT_BCON);
		else if (!fsm->a_vbus_vld)
			otg_set_state(fsm, OTG_STATE_A_VBUS_ERR);
		break;
	case OTG_STATE_A_WAIT_VFALL:
		if (fsm->a_wait_vfall_tmout)
			otg_set_state(fsm, OTG_STATE_A_IDLE);
		break;
	case OTG_STATE_A_VBUS_ERR:
		if (fsm->id || fsm->a_bus_drop || fsm->a_clr_err)
			otg_set_state(fsm, OTG_STATE_A_WAIT_VFALL);
		break;
	default:
		break;
	}
	mutex_unlock(&fsm->lock);

	VDBG("quit statemachine, changed = %d\n", fsm->state_changed);
	return fsm->state_changed;
}
EXPORT_SYMBOL_GPL(otg_statemachine);
MODULE_DESCRIPTION("OTG Finite State Machine");
MODULE_LICENSE("GPL");
