// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2024 Takashi Sakamoto

#include <linux/types.h>
#include <linux/err.h>
#include "packet-header-definitions.h"
#include "phy-packet-definitions.h"

#define CREATE_TRACE_POINTS
#include <trace/events/firewire.h>

#ifdef TRACEPOINTS_ENABLED
void copy_port_status(u8 *port_status, unsigned int port_capacity,
		      const u32 *self_id_sequence, unsigned int quadlet_count)
{
	unsigned int port_index;

	for (port_index = 0; port_index < port_capacity; ++port_index) {
		port_status[port_index] =
			self_id_sequence_get_port_status(self_id_sequence, quadlet_count, port_index);
	}
}
#endif
