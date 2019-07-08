/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2014 Intel Corporation.
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
