/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * cimax2.h
 *
 * CIMax(R) SP2 driver in conjunction with NetUp Dual DVB-S2 CI card
 *
 * Copyright (C) 2009 NetUP Inc.
 * Copyright (C) 2009 Igor M. Liplianin <liplianin@netup.ru>
 * Copyright (C) 2009 Abylay Ospan <aospan@netup.ru>
 */

#ifndef CIMAX2_H
#define CIMAX2_H
#include <media/dvb_ca_en50221.h>

extern int netup_ci_read_attribute_mem(struct dvb_ca_en50221 *en50221,
						int slot, int addr);
extern int netup_ci_write_attribute_mem(struct dvb_ca_en50221 *en50221,
						int slot, int addr, u8 data);
extern int netup_ci_read_cam_ctl(struct dvb_ca_en50221 *en50221,
						int slot, u8 addr);
extern int netup_ci_write_cam_ctl(struct dvb_ca_en50221 *en50221,
						int slot, u8 addr, u8 data);
extern int netup_ci_slot_reset(struct dvb_ca_en50221 *en50221, int slot);
extern int netup_ci_slot_shutdown(struct dvb_ca_en50221 *en50221, int slot);
extern int netup_ci_slot_ts_ctl(struct dvb_ca_en50221 *en50221, int slot);
extern int netup_ci_slot_status(struct cx23885_dev *dev, u32 pci_status);
extern int netup_poll_ci_slot_status(struct dvb_ca_en50221 *en50221,
						int slot, int open);
extern int netup_ci_init(struct cx23885_tsport *port);
extern void netup_ci_exit(struct cx23885_tsport *port);

#endif
