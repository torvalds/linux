/*
 * IPWireless 3G PCMCIA Network Driver
 *
 * Original code
 *   by Stephen Blackheath <stephen@blacksapphire.com>,
 *      Ben Martel <benm@symmetric.co.nz>
 *
 * Copyrighted as follows:
 *   Copyright (C) 2004 by Symmetric Systems Ltd (NZ)
 *
 * Various driver changes and rewrites, port to new kernels
 *   Copyright (C) 2006-2007 Jiri Kosina
 *
 * Misc code cleanups and updates
 *   Copyright (C) 2007 David Sterba
 */

#ifndef _IPWIRELESS_CS_TTY_H_
#define _IPWIRELESS_CS_TTY_H_

#include <linux/types.h>
#include <linux/sched.h>

#include <pcmcia/cs.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

struct ipw_tty;
struct ipw_network;
struct ipw_hardware;

int ipwireless_tty_init(void);
void ipwireless_tty_release(void);

struct ipw_tty *ipwireless_tty_create(struct ipw_hardware *hw,
				      struct ipw_network *net);
void ipwireless_tty_free(struct ipw_tty *tty);
void ipwireless_tty_received(struct ipw_tty *tty, unsigned char *data,
			     unsigned int length);
int ipwireless_tty_is_modem(struct ipw_tty *tty);
void ipwireless_tty_notify_control_line_change(struct ipw_tty *tty,
					       unsigned int channel_idx,
					       unsigned int control_lines,
					       unsigned int changed_mask);

#endif
