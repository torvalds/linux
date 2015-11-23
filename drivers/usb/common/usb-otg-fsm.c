/*
 * OTG Finite State Machine from OTG spec
 *
 * Copyright (C) 2007-2015 Freescale Semiconductor, Inc.
 *
 * Author:	Li Yang <LeoLi@freescale.com>
 *		Jerry Huang <Chang-Ming.Huang@freescale.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the  GNU General Public License along
 * with this program; if not, write  to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/usb.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>
#include <linux/usb/otg-fsm.h>

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

static int state_changed;

/* Called when leaving a state.  Do state clean up jobs here */
static void otg_leave_state(struct otg_fsm *fsm, enum usb_otg_state old_state)
{
	switch (old_state) {
	case OTG_STATE_B_IDLE:
		otg_del_timer(fsm, B_SE0_SRP);
		otg_del_timer(fsm, B_SRP_FAIL);
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
		if (fsm->otg_hnp_reqd) {
			fsm->otg_hnp_reqd = 0;
			fsm->b_bus_req = 0;
		}
		fsm->a_conn = 0;
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

/* Called when entering a state */
static int otg_set_state(struct otg_fsm *fsm, enum usb_otg_state new_state)
{
	state_changed = 1;
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
		if (fsm->otg_hnp_reqd) {
			fsm->otg_hnp_reqd = 0;
			fsm->b_bus_req = 0;
		}
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
		fsm->b_bus_req = 0;
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
		otg_add_timer(fsm, HNP_POLLING);
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
		otg_add_timer(fsm, HNP_POLLING);
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
	return 0;
}

/* State change judgement */
int otg_statemachine(struct otg_fsm *fsm)
{
	enum usb_otg_state state;

	mutex_lock(&fsm->lock);

	state = fsm->otg->state;
	state_changed = 0;
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
		else if (!fsm->a_bus_req || fsm->a_suspend_req_inf)
			otg_set_state(fsm, OTG_STATE_A_SUSPEND);
		else if (!fsm->b_conn)
			otg_set_state(fsm, OTG_STATE_A_WAIT_BCON);
		else if (!fsm->a_vbus_vld)
			otg_set_state(fsm, OTG_STATE_A_VBUS_ERR);
		break;
	case OTG_STATE_A_SUSPEND:
		if (!fsm->b_conn && fsm->a_set_b_hnp_en)
			otg_set_state(fsm, OTG_STATE_A_PERIPHERAL);
		else if (!fsm->b_conn && !fsm->a_set_b_hnp_en)
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

	VDBG("quit statemachine, changed = %d\n", state_changed);
	return state_changed;
}
EXPORT_SYMBOL_GPL(otg_statemachine);

static int otg_handle_role_switch(struct otg_fsm *fsm, struct usb_device *udev)
{
	int err;
	enum usb_otg_state state = fsm->otg->state;

	if (state == OTG_STATE_A_HOST) {
		/* Set b_hnp_enable */
		if (!fsm->a_set_b_hnp_en) {
			err = usb_control_msg(udev,
				usb_sndctrlpipe(udev, 0),
				USB_REQ_SET_FEATURE, 0,
				USB_DEVICE_B_HNP_ENABLE,
				0, NULL, 0,
				USB_CTRL_SET_TIMEOUT);
			if (err >= 0)
				fsm->a_set_b_hnp_en = 1;
		}
		fsm->a_bus_req = 0;
		if (fsm->tst_maint) {
			fsm->tst_maint = 0;
			fsm->otg_vbus_off = 0;
			otg_del_timer(fsm, A_TST_MAINT);
		}
		return HOST_REQUEST_FLAG;
	} else if (state == OTG_STATE_B_HOST) {
		fsm->b_bus_req = 0;
		return HOST_REQUEST_FLAG;
	}

	return -EINVAL;
}

/*
 * Called by host to poll peripheral if it wants to be host
 * Return value:
 * - host request flag(1) if the device wants to be host,
 * - host request flag(0) if the device keeps peripheral role,
 * - otherwise, error code.
 */
int otg_hnp_polling(struct otg_fsm *fsm)
{
	struct usb_device *udev;
	int retval;
	enum usb_otg_state state = fsm->otg->state;
	struct usb_otg_descriptor *desc = NULL;
	u8 host_request_flag;

	if ((state != OTG_STATE_A_HOST || !fsm->b_hnp_enable) &&
					state != OTG_STATE_B_HOST)
		return -EINVAL;

	udev = usb_hub_find_child(fsm->otg->host->root_hub, 1);
	if (!udev) {
		dev_err(fsm->otg->host->controller,
			"no usb dev connected, can't start HNP polling\n");
		return -ENODEV;
	}

	if (udev->state != USB_STATE_CONFIGURED) {
		dev_dbg(&udev->dev, "the B dev is not resumed!\n");
		otg_add_timer(fsm, HNP_POLLING);
		return -EPERM;
	}

	/*
	 * Legacy otg test device does not support HNP polling,
	 * start HNP directly for legacy otg test device.
	 */
	if (fsm->tst_maint &&
		(__usb_get_extra_descriptor(udev->rawdescriptors[0],
		le16_to_cpu(udev->config[0].desc.wTotalLength),
				USB_DT_OTG, (void **) &desc) == 0)) {
		/* shorter bLength of OTG 1.3 or earlier */
		if (desc->bLength < 5) {
			fsm->a_bus_req = 0;
			fsm->tst_maint = 0;
			otg_del_timer(fsm, A_TST_MAINT);
			return HOST_REQUEST_FLAG;
		}
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
	if (retval == 1) {
		host_request_flag = *fsm->host_req_flag;
		if (host_request_flag == HOST_REQUEST_FLAG) {
			retval = otg_handle_role_switch(fsm, udev);
		} else if (host_request_flag == 0) {
			/* Continue polling */
			otg_add_timer(fsm, HNP_POLLING);
			retval = 0;
		} else {
			dev_err(&udev->dev, "host request flag is invalid\n");
			retval = -EINVAL;
		}
	} else {
		dev_warn(&udev->dev, "Get one byte OTG status failed\n");
		retval = -EIO;
	}
	return retval;
}
EXPORT_SYMBOL_GPL(otg_hnp_polling);
