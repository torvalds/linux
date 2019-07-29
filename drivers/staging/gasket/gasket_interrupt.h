/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Gasket common interrupt module. Defines functions for enabling
 * eventfd-triggered interrupts between a Gasket device and a host process.
 *
 * Copyright (C) 2018 Google, Inc.
 */
#ifndef __GASKET_INTERRUPT_H__
#define __GASKET_INTERRUPT_H__

#include <linux/eventfd.h>
#include <linux/pci.h>

#include "gasket_core.h"

/* Note that this currently assumes that device interrupts are a dense set,
 * numbered from 0 - (num_interrupts - 1). Should this have to change, these
 * APIs will have to be updated.
 */

/* Opaque type used to hold interrupt subsystem data. */
struct gasket_interrupt_data;

/*
 * Initialize the interrupt module.
 * @gasket_dev: The Gasket device structure for the device to be initted.
 */
int gasket_interrupt_init(struct gasket_dev *gasket_dev);

/*
 * Clean up a device's interrupt structure.
 * @gasket_dev: The Gasket information structure for this device.
 *
 * Cleans up the device's interrupts and deallocates data.
 */
void gasket_interrupt_cleanup(struct gasket_dev *gasket_dev);

/*
 * Clean up and re-initialize the MSI-x subsystem.
 * @gasket_dev: The Gasket information structure for this device.
 *
 * Performs a teardown of the MSI-x subsystem and re-initializes it. Does not
 * free the underlying data structures. Returns 0 on success and an error code
 * on error.
 */
int gasket_interrupt_reinit(struct gasket_dev *gasket_dev);

/*
 * Reset the counts stored in the interrupt subsystem.
 * @gasket_dev: The Gasket information structure for this device.
 *
 * Sets the counts of all interrupts in the subsystem to 0.
 */
int gasket_interrupt_reset_counts(struct gasket_dev *gasket_dev);

/*
 * Associates an eventfd with a device interrupt.
 * @data: Pointer to device interrupt data.
 * @interrupt: The device interrupt to configure.
 * @event_fd: The eventfd to associate with the interrupt.
 *
 * Prepares the host to receive notification of device interrupts by associating
 * event_fd with interrupt. Upon receipt of a device interrupt, event_fd will be
 * signaled, after successful configuration.
 *
 * Returns 0 on success, a negative error code otherwise.
 */
int gasket_interrupt_set_eventfd(struct gasket_interrupt_data *interrupt_data,
				 int interrupt, int event_fd);

/*
 * Removes an interrupt-eventfd association.
 * @data: Pointer to device interrupt data.
 * @interrupt: The device interrupt to de-associate.
 *
 * Removes any eventfd associated with the specified interrupt, if any.
 */
int gasket_interrupt_clear_eventfd(struct gasket_interrupt_data *interrupt_data,
				   int interrupt);

/*
 * The below functions exist for backwards compatibility.
 * No new uses should be written.
 */
/*
 * Get the health of the interrupt subsystem.
 * @gasket_dev: The Gasket device struct.
 *
 * Returns DEAD if not set up, LAMED if initialization failed, and ALIVE
 * otherwise.
 */

int gasket_interrupt_system_status(struct gasket_dev *gasket_dev);

#endif
