/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Gasket common interrupt module. Defines functions for enabling
 * eventfd-triggered interrupts between a Gasket device and a host process.
 *
 * Copyright (C) 2018 Google, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
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
 * @type: Type of the interrupt. (See gasket_interrupt_type).
 * @name: The name to associate with these interrupts.
 * @interrupts: An array of all interrupt descriptions for this device.
 * @num_interrupts: The length of the @interrupts array.
 * @pack_width: The width, in bits, of a single field in a packed interrupt reg.
 * @bar_index: The bar containing all interrupt registers.
 *
 * Allocates and initializes data to track interrupt state for a device.
 * After this call, no interrupts will be configured/delivered; call
 * gasket_interrupt_set_vector[_packed] to associate each interrupt with an
 * __iomem location, then gasket_interrupt_set_eventfd to associate an eventfd
 * with an interrupt.
 *
 * If num_interrupts interrupts are not available, this call will return a
 * negative error code. In that case, gasket_interrupt_cleanup should still be
 * called. Returns 0 on success (which can include a device where interrupts
 * are not possible to set up, but is otherwise OK; that device will report
 * status LAMED.)
 */
int gasket_interrupt_init(
	struct gasket_dev *gasket_dev, const char *name, int type,
	const struct gasket_interrupt_desc *interrupts,
	int num_interrupts, int pack_width, int bar_index,
	const struct gasket_wire_interrupt_offsets *wire_int_offsets);

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
int gasket_interrupt_set_eventfd(
	struct gasket_interrupt_data *interrupt_data, int interrupt,
	int event_fd);

/*
 * Removes an interrupt-eventfd association.
 * @data: Pointer to device interrupt data.
 * @interrupt: The device interrupt to de-associate.
 *
 * Removes any eventfd associated with the specified interrupt, if any.
 */
int gasket_interrupt_clear_eventfd(
	struct gasket_interrupt_data *interrupt_data, int interrupt);

/*
 * Signals the eventfd associated with interrupt.
 * @data: Pointer to device interrupt data.
 * @interrupt: The device interrupt to signal for.
 *
 * Simulates a device interrupt by signaling the eventfd associated with
 * interrupt, if any.
 * Returns 0 if the eventfd was successfully triggered, a negative error code
 * otherwise (if, for example, no eventfd was associated with interrupt).
 */
int gasket_interrupt_trigger_eventfd(
	struct gasket_interrupt_data *interrupt_data, int interrupt);

/*
 * The below functions exist for backwards compatibility.
 * No new uses should be written.
 */
/*
 * Retrieve a pointer to data's MSI-X
 * entries.
 * @data: The interrupt data from which to extract.
 *
 * Returns the internal pointer to data's MSI-X entries.
 */
struct msix_entry *gasket_interrupt_get_msix_entries(
	struct gasket_interrupt_data *interrupt_data);

/*
 * Get a pointer to data's eventfd contexts.
 * @data: The interrupt data from which to extract.
 *
 * Returns the internal pointer to data's eventfd contexts.
 *
 * This function exists for backwards compatibility with older drivers.
 * No new uses should be written.
 */
struct eventfd_ctx **gasket_interrupt_get_eventfd_ctxs(
	struct gasket_interrupt_data *interrupt_data);

/*
 * Get the health of the interrupt subsystem.
 * @gasket_dev: The Gasket device struct.
 *
 * Returns DEAD if not set up, LAMED if initialization failed, and ALIVE
 * otherwise.
 */

int gasket_interrupt_system_status(struct gasket_dev *gasket_dev);

/*
 * Masks interrupts and de-register the handler.
 * After an interrupt pause it is not guaranteed that the chip registers will
 * be accessible anymore, since the chip may be in a power save mode,
 * which means that the interrupt handler (if it were to happen) may not
 * have a way to clear the interrupt condition.
 * @gasket_dev: The Gasket device struct
 * @enable_pause: Whether to pause or unpause the interrupts.
 */
void gasket_interrupt_pause(struct gasket_dev *gasket_dev, int enable_pause);

#endif
