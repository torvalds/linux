/*
 *
 * Copyright 1999 Digi International (www.digi.com)
 *     James Puzzo <jamesp at digi dot com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, EXPRESS OR IMPLIED; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 */

/*
 *
 *  Filename:
 *
 *     dgrp_common.c
 *
 *  Description:
 *
 *     Definitions of global variables and functions which are either
 *     shared by the tty, mon, and net drivers; or which cross them
 *     functionally (like the poller).
 *
 *  Author:
 *
 *     James A. Puzzo
 *
 */

#include <linux/errno.h>
#include <linux/tty.h>
#include <linux/sched.h>
#include <linux/cred.h>

#include "dgrp_common.h"

/**
 * dgrp_carrier -- check for carrier change state and act
 * @ch: struct ch_struct *
 */
void dgrp_carrier(struct ch_struct *ch)
{
	struct nd_struct *nd;

	int virt_carrier = 0;
	int phys_carrier = 0;

	/* fix case when the tty has already closed. */

	if (!ch)
		return;
	nd  = ch->ch_nd;
	if (!nd)
		return;

	/*
	 *  If we are currently waiting to determine the status of the port,
	 *  we don't yet know the state of the modem lines.  As a result,
	 *  we ignore state changes when we are waiting for the modem lines
	 *  to be established.  We know, as a result of code in dgrp_net_ops,
	 *  that we will be called again immediately following the reception
	 *  of the status message with the true modem status flags in it.
	 */
	if (ch->ch_expect & RR_STATUS)
		return;

	/*
	 * If CH_HANGUP is set, we gotta keep trying to get all the processes
	 * that have the port open to close the port.
	 * So lets just keep sending a hangup every time we get here.
	 */
	if ((ch->ch_flag & CH_HANGUP) &&
	    (ch->ch_tun.un_open_count > 0))
		tty_hangup(ch->ch_tun.un_tty);

	/*
	 *  Compute the effective state of both the physical and virtual
	 *  senses of carrier.
	 */

	if (ch->ch_s_mlast & DM_CD)
		phys_carrier = 1;

	if ((ch->ch_s_mlast & DM_CD) ||
	    (ch->ch_digi.digi_flags & DIGI_FORCEDCD) ||
	    (ch->ch_flag & CH_CLOCAL))
		virt_carrier = 1;

	/*
	 *  Test for a VIRTUAL carrier transition to HIGH.
	 *
	 *  The CH_HANGUP condition is intended to prevent any action
	 *  except for close.  As a result, we ignore positive carrier
	 *  transitions during CH_HANGUP.
	 */
	if (((ch->ch_flag & CH_HANGUP)  == 0) &&
	    ((ch->ch_flag & CH_VIRT_CD) == 0) &&
	    (virt_carrier == 1)) {
		/*
		 * When carrier rises, wake any threads waiting
		 * for carrier in the open routine.
		 */
		nd->nd_tx_work = 1;

		if (waitqueue_active(&ch->ch_flag_wait))
			wake_up_interruptible(&ch->ch_flag_wait);
	}

	/*
	 *  Test for a PHYSICAL transition to low, so long as we aren't
	 *  currently ignoring physical transitions (which is what "virtual
	 *  carrier" indicates).
	 *
	 *  The transition of the virtual carrier to low really doesn't
	 *  matter... it really only means "ignore carrier state", not
	 *  "make pretend that carrier is there".
	 */
	if ((virt_carrier == 0) &&
	    ((ch->ch_flag & CH_PHYS_CD) != 0) &&
	    (phys_carrier == 0)) {
		/*
		 * When carrier drops:
		 *
		 *   Do a Hard Hangup if that is called for.
		 *
		 *   Drop carrier on all open units.
		 *
		 *   Flush queues, waking up any task waiting in the
		 *   line discipline.
		 *
		 *   Send a hangup to the control terminal.
		 *
		 *   Enable all select calls.
		 */

		nd->nd_tx_work = 1;

		ch->ch_flag &= ~(CH_LOW | CH_EMPTY | CH_DRAIN | CH_INPUT);

		if (waitqueue_active(&ch->ch_flag_wait))
			wake_up_interruptible(&ch->ch_flag_wait);

		if (ch->ch_tun.un_open_count > 0)
			tty_hangup(ch->ch_tun.un_tty);

		if (ch->ch_pun.un_open_count > 0)
			tty_hangup(ch->ch_pun.un_tty);
	}

	/*
	 *  Make sure that our cached values reflect the current reality.
	 */
	if (virt_carrier == 1)
		ch->ch_flag |= CH_VIRT_CD;
	else
		ch->ch_flag &= ~CH_VIRT_CD;

	if (phys_carrier == 1)
		ch->ch_flag |= CH_PHYS_CD;
	else
		ch->ch_flag &= ~CH_PHYS_CD;

}
