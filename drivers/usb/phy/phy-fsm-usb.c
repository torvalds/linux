/*
 * OTG Finite State Machine from OTG spec
 *
 * Copyright (C) 2007,2008 Freescale Semiconductor, Inc.
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
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/usb.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg.h>

#include "phy-fsm-usb.h"

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
void otg_leave_state(struct otg_fsm *fsm, enum usb_otg_state old_state)
{
	switch (old_state) {
	case OTG_STATE_B_IDLE:
		otg_del_timer(fsm, B_SE0_SRP);
		fsm->b_se0_srp = 0;
		break;
	case OTG_STATE_B_SRP_INIT:
		fsm->b_srp_done = 0;
		break;
	case OTG_STATE_B_PERIPHERAL:
		break;
	case OTG_STATE_B_WAIT_ACON:
		otg_del_timer(fsm, B_ASE0_BRST);
		fsm->b_ase0_brst_tmout = 0;
		break;
	case OTG_STATE_B_HOST:
		break;
	case OTG_STATE_A_IDLE:
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
int otg_set_state(struct otg_fsm *fsm, enum usb_otg_state new_state)
{
	state_changed = 1;
	if (fsm->otg->phy->state == new_state)
		return 0;
	VDBG("Set state: %s\n", usb_otg_state_string(new_state));
	otg_leave_state(fsm, fsm->otg->phy->state);
	switch (new_state) {
	case OTG_STATE_B_IDLE:
		otg_drv_vbus(fsm, 0);
		otg_chrg_vbus(fsm, 0);
		otg_loc_conn(fsm, 0);
		otg_loc_sof(fsm, 0);
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
		otg_loc_conn(fsm, 1);
		otg_loc_sof(fsm, 0);
		otg_set_protocol(fsm, PROTO_GADGET);
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
		break;
	case OTG_STATE_A_IDLE:
		otg_drv_vbus(fsm, 0);
		otg_chrg_vbus(fsm, 0);
		otg_loc_conn(fsm, 0);
		otg_loc_sof(fsm, 0);
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
		break;
	case OTG_STATE_A_SUSPEND:
		otg_drv_vbus(fsm, 1);
		otg_loc_conn(fsm, 0);
		otg_loc_sof(fsm, 0);
		otg_set_protocol(fsm, PROTO_HOST);
		otg_add_timer(fsm, A_AIDL_BDIS);

		break;
	case OTG_STATE_A_PERIPHERAL:
		otg_loc_conn(fsm, 1);
		otg_loc_sof(fsm, 0);
		otg_set_protocol(fsm, PROTO_GADGET);
		otg_drv_vbus(fsm, 1);
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

	fsm->otg->phy->state = new_state;
	return 0;
}

/* State change judgement */
int otg_statemachine(struct otg_fsm *fsm)
{
	enum usb_otg_state state;
	unsigned long flags;

	spin_lock_irqsave(&fsm->lock, flags);

	state = fsm->otg->phy->state;
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
		else if (fsm->b_bus_req && fsm->b_ssend_srp && fsm->b_se0_srp)
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
		else if (!fsm->b_bus_req || !fsm->a_conn)
			otg_set_state(fsm, OTG_STATE_B_PERIPHERAL);
		break;
	case OTG_STATE_A_IDLE:
		if (fsm->id)
			otg_set_state(fsm, OTG_STATE_B_IDLE);
		else if (!fsm->a_bus_drop && (fsm->a_bus_req || fsm->a_srp_det))
			otg_set_state(fsm, OTG_STATE_A_WAIT_VRISE);
		break;
	case OTG_STATE_A_WAIT_VRISE:
		if (fsm->id || fsm->a_bus_drop || fsm->a_vbus_vld ||
				fsm->a_wait_vrise_tmout) {
			otg_set_state(fsm, OTG_STATE_A_WAIT_BCON);
		}
		break;
	case OTG_STATE_A_WAIT_BCON:
		if (!fsm->a_vbus_vld)
			otg_set_state(fsm, OTG_STATE_A_VBUS_ERR);
		else if (fsm->b_conn)
			otg_set_state(fsm, OTG_STATE_A_HOST);
		else if (fsm->id | fsm->a_bus_drop | fsm->a_wait_bcon_tmout)
			otg_set_state(fsm, OTG_STATE_A_WAIT_VFALL);
		break;
	case OTG_STATE_A_HOST:
		if ((!fsm->a_bus_req || fsm->a_suspend_req_inf) &&
				fsm->otg->host->b_hnp_enable)
			otg_set_state(fsm, OTG_STATE_A_SUSPEND);
		else if (fsm->id || !fsm->b_conn || fsm->a_bus_drop)
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
		if (fsm->a_wait_vfall_tmout || fsm->id || fsm->a_bus_req ||
				(!fsm->a_sess_vld && !fsm->b_conn))
			otg_set_state(fsm, OTG_STATE_A_IDLE);
		break;
	case OTG_STATE_A_VBUS_ERR:
		if (fsm->id || fsm->a_bus_drop || fsm->a_clr_err)
			otg_set_state(fsm, OTG_STATE_A_WAIT_VFALL);
		break;
	default:
		break;
	}
	spin_unlock_irqrestore(&fsm->lock, flags);

	VDBG("quit statemachine, changed = %d\n", state_changed);
	return state_changed;
}
