/* Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/*
 * Module Name:
 *  controlframework.h
 *
 * Abstract: This file defines common structures in the unmanaged
 *	     Ultravisor (mostly EFI) space.
 *
 */

#ifndef _CONTROL_FRAMEWORK_H_
#define _CONTROL_FRAMEWORK_H_

#include <linux/types.h>
#include "channel.h"

struct spar_segment_state  {
	u16 enabled:1;		/* Bit 0: May enter other states */
	u16 active:1;		/* Bit 1: Assigned to active partition */
	u16 alive:1;		/* Bit 2: Configure message sent to
				 * service/server */
	u16 revoked:1;		/* Bit 3: similar to partition state
				 * ShuttingDown */
	u16 allocated:1;	/* Bit 4: memory (device/port number)
				 * has been selected by Command */
	u16 known:1;		/* Bit 5: has been introduced to the
				 * service/guest partition */
	u16 ready:1;		/* Bit 6: service/Guest partition has
				 * responded to introduction */
	u16 operating:1;	/* Bit 7: resource is configured and
				 * operating */
	/* Note: don't use high bit unless we need to switch to ushort
	 * which is non-compliant */
};

static const struct spar_segment_state segment_state_running = {
	1, 1, 1, 0, 1, 1, 1, 1
};

static const struct spar_segment_state segment_state_paused = {
	1, 1, 1, 0, 1, 1, 1, 0
};

static const struct spar_segment_state segment_state_standby = {
	1, 1, 0, 0, 1, 1, 1, 0
};

#endif				/* _CONTROL_FRAMEWORK_H_ not defined */
