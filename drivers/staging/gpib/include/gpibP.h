/* SPDX-License-Identifier: GPL-2.0 */

/***************************************************************************
 *    copyright		   : (C) 2002,2003 by Frank Mori Hess
 ***************************************************************************/

#ifndef _GPIB_P_H
#define _GPIB_P_H

#include <linux/types.h>

#include "gpib_types.h"
#include "gpib_proto.h"
#include "gpib_cmd.h"
#include "gpib.h"
#include "gpib_ioctl.h"

#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/io.h>

int gpib_register_driver(struct gpib_interface *interface, struct module *mod);
void gpib_unregister_driver(struct gpib_interface *interface);
struct pci_dev *gpib_pci_get_device(const struct gpib_board_config *config, unsigned int vendor_id,
				    unsigned int device_id, struct pci_dev *from);
struct pci_dev *gpib_pci_get_subsys(const struct gpib_board_config *config, unsigned int vendor_id,
				    unsigned int device_id, unsigned int ss_vendor,
				    unsigned int ss_device, struct pci_dev *from);
unsigned int num_gpib_events(const struct gpib_event_queue *queue);
int push_gpib_event(struct gpib_board *board, short event_type);
int pop_gpib_event(struct gpib_board *board, struct gpib_event_queue *queue, short *event_type);
int gpib_request_pseudo_irq(struct gpib_board *board, irqreturn_t (*handler)(int, void *));
void gpib_free_pseudo_irq(struct gpib_board *board);
int gpib_match_device_path(struct device *dev, const char *device_path_in);

extern struct gpib_board board_array[GPIB_MAX_NUM_BOARDS];

extern struct list_head registered_drivers;

#endif	// _GPIB_P_H

