/*
	Mantis PCI bridge driver

	Copyright (C) 2005, 2006 Manu Abraham (abraham.manu@gmail.com)

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the Free Software
	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef __MANTIS_LINK_H
#define __MANTIS_LINK_H

enum mantis_sbuf_status {
	MANTIS_SBUF_DATA_AVAIL		= 1,
	MANTIS_SBUF_DATA_EMPTY		= 2,
	MANTIS_SBUF_DATA_OVFLW		= 3
};

struct mantis_slot {
	u32				timeout;
};

struct mantis_ca {
	struct mantis_slot		slot;

	struct tasklet_struct		hif_evm_tasklet;

	u32				hif_event;
	wait_queue_head_t		hif_opdone_wq;
	wait_queue_head_t		hif_brrdyw_wq;
	wait_queue_head_t		hif_data_wq;
	u32				hif_job_queue

	enum mantis_sbuf_status		sbuf_status;

	struct dvb_device		*ca_dev;
	void				*ca_priv;
};

#endif // __MANTIS_LINK_H
