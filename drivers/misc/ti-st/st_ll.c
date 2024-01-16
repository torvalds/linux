// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Shared Transport driver
 *	HCI-LL module responsible for TI proprietary HCI_LL protocol
 *  Copyright (C) 2009-2010 Texas Instruments
 *  Author: Pavan Savoy <pavan_savoy@ti.com>
 */

#define pr_fmt(fmt) "(stll) :" fmt
#include <linux/skbuff.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/ti_wilink_st.h>

/**********************************************************************/
/* internal functions */
static void send_ll_cmd(struct st_data_s *st_data,
	unsigned char cmd)
{

	pr_debug("%s: writing %x", __func__, cmd);
	st_int_write(st_data, &cmd, 1);
	return;
}

static void ll_device_want_to_sleep(struct st_data_s *st_data)
{
	struct kim_data_s	*kim_data;
	struct ti_st_plat_data	*pdata;

	pr_debug("%s", __func__);
	/* sanity check */
	if (st_data->ll_state != ST_LL_AWAKE)
		pr_err("ERR hcill: ST_LL_GO_TO_SLEEP_IND"
			  "in state %ld", st_data->ll_state);

	send_ll_cmd(st_data, LL_SLEEP_ACK);
	/* update state */
	st_data->ll_state = ST_LL_ASLEEP;

	/* communicate to platform about chip asleep */
	kim_data = st_data->kim_data;
	pdata = kim_data->kim_pdev->dev.platform_data;
	if (pdata->chip_asleep)
		pdata->chip_asleep(NULL);
}

static void ll_device_want_to_wakeup(struct st_data_s *st_data)
{
	struct kim_data_s	*kim_data;
	struct ti_st_plat_data	*pdata;

	/* diff actions in diff states */
	switch (st_data->ll_state) {
	case ST_LL_ASLEEP:
		send_ll_cmd(st_data, LL_WAKE_UP_ACK);	/* send wake_ack */
		break;
	case ST_LL_ASLEEP_TO_AWAKE:
		/* duplicate wake_ind */
		pr_err("duplicate wake_ind while waiting for Wake ack");
		break;
	case ST_LL_AWAKE:
		/* duplicate wake_ind */
		pr_err("duplicate wake_ind already AWAKE");
		break;
	case ST_LL_AWAKE_TO_ASLEEP:
		/* duplicate wake_ind */
		pr_err("duplicate wake_ind");
		break;
	}
	/* update state */
	st_data->ll_state = ST_LL_AWAKE;

	/* communicate to platform about chip wakeup */
	kim_data = st_data->kim_data;
	pdata = kim_data->kim_pdev->dev.platform_data;
	if (pdata->chip_awake)
		pdata->chip_awake(NULL);
}

/**********************************************************************/
/* functions invoked by ST Core */

/* called when ST Core wants to
 * enable ST LL */
void st_ll_enable(struct st_data_s *ll)
{
	ll->ll_state = ST_LL_AWAKE;
}

/* called when ST Core /local module wants to
 * disable ST LL */
void st_ll_disable(struct st_data_s *ll)
{
	ll->ll_state = ST_LL_INVALID;
}

/* called when ST Core wants to update the state */
void st_ll_wakeup(struct st_data_s *ll)
{
	if (likely(ll->ll_state != ST_LL_AWAKE)) {
		send_ll_cmd(ll, LL_WAKE_UP_IND);	/* WAKE_IND */
		ll->ll_state = ST_LL_ASLEEP_TO_AWAKE;
	} else {
		/* don't send the duplicate wake_indication */
		pr_err(" Chip already AWAKE ");
	}
}

/* called when ST Core wants the state */
unsigned long st_ll_getstate(struct st_data_s *ll)
{
	pr_debug(" returning state %ld", ll->ll_state);
	return ll->ll_state;
}

/* called from ST Core, when a PM related packet arrives */
unsigned long st_ll_sleep_state(struct st_data_s *st_data,
	unsigned char cmd)
{
	switch (cmd) {
	case LL_SLEEP_IND:	/* sleep ind */
		pr_debug("sleep indication recvd");
		ll_device_want_to_sleep(st_data);
		break;
	case LL_SLEEP_ACK:	/* sleep ack */
		pr_err("sleep ack rcvd: host shouldn't");
		break;
	case LL_WAKE_UP_IND:	/* wake ind */
		pr_debug("wake indication recvd");
		ll_device_want_to_wakeup(st_data);
		break;
	case LL_WAKE_UP_ACK:	/* wake ack */
		pr_debug("wake ack rcvd");
		st_data->ll_state = ST_LL_AWAKE;
		break;
	default:
		pr_err(" unknown input/state ");
		return -EINVAL;
	}
	return 0;
}

/* Called from ST CORE to initialize ST LL */
long st_ll_init(struct st_data_s *ll)
{
	/* set state to invalid */
	ll->ll_state = ST_LL_INVALID;
	return 0;
}

/* Called from ST CORE to de-initialize ST LL */
long st_ll_deinit(struct st_data_s *ll)
{
	return 0;
}
