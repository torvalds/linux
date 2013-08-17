/*
	Mantis PCI bridge driver

	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

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

#include <linux/mutex.h>
#include <linux/workqueue.h>
#include "dvb_ca_en50221.h"

enum mantis_sbuf_status {
	MANTIS_SBUF_DATA_AVAIL		= 1,
	MANTIS_SBUF_DATA_EMPTY		= 2,
	MANTIS_SBUF_DATA_OVFLW		= 3
};

struct mantis_slot {
	u32				timeout;
	u32				slave_cfg;
	u32				bar;
};

/* Physical layer */
enum mantis_slot_state {
	MODULE_INSERTED			= 3,
	MODULE_XTRACTED			= 4
};

struct mantis_ca {
	struct mantis_slot		slot[4];

	struct work_struct		hif_evm_work;

	u32				hif_event;
	wait_queue_head_t		hif_opdone_wq;
	wait_queue_head_t		hif_brrdyw_wq;
	wait_queue_head_t		hif_data_wq;
	wait_queue_head_t		hif_write_wq; /* HIF Write op */

	enum mantis_sbuf_status		sbuf_status;

	enum mantis_slot_state		slot_state;

	void				*ca_priv;

	struct dvb_ca_en50221		en50221;
	struct mutex			ca_lock;
};

/* CA */
extern void mantis_event_cam_plugin(struct mantis_ca *ca);
extern void mantis_event_cam_unplug(struct mantis_ca *ca);
extern int mantis_pcmcia_init(struct mantis_ca *ca);
extern void mantis_pcmcia_exit(struct mantis_ca *ca);
extern int mantis_evmgr_init(struct mantis_ca *ca);
extern void mantis_evmgr_exit(struct mantis_ca *ca);

/* HIF */
extern int mantis_hif_init(struct mantis_ca *ca);
extern void mantis_hif_exit(struct mantis_ca *ca);
extern int mantis_hif_read_mem(struct mantis_ca *ca, u32 addr);
extern int mantis_hif_write_mem(struct mantis_ca *ca, u32 addr, u8 data);
extern int mantis_hif_read_iom(struct mantis_ca *ca, u32 addr);
extern int mantis_hif_write_iom(struct mantis_ca *ca, u32 addr, u8 data);

#endif /* __MANTIS_LINK_H */
