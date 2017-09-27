/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * Intel SCIF driver.
 */
#ifndef _SCIF_PEER_BUS_H_
#define _SCIF_PEER_BUS_H_

#include <linux/device.h>
#include <linux/mic_common.h>
#include <linux/scif.h>

struct scif_dev;

void scif_add_peer_device(struct work_struct *work);
void scif_peer_register_device(struct scif_dev *sdev);
int scif_peer_unregister_device(struct scif_dev *scifdev);
int scif_peer_bus_init(void);
void scif_peer_bus_exit(void);
#endif /* _SCIF_PEER_BUS_H */
