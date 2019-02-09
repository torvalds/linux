/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Common Gasket device kernel and user space declarations.
 *
 * Copyright (C) 2018 Google, Inc.
 */
#ifndef __GASKET_H__
#define __GASKET_H__

#include <linux/ioctl.h>
#include <linux/types.h>

/* ioctl structure declarations */

/* Ioctl structures are padded to a multiple of 64 bits */
/* and padded to put 64 bit values on 64 bit boundaries. */
/* Unsigned 64 bit integers are used to hold pointers. */
/* This helps compatibility between 32 and 64 bits. */

/*
 * Common structure for ioctls associating an eventfd with a device interrupt,
 * when using the Gasket interrupt module.
 */
struct gasket_interrupt_eventfd {
	u64 interrupt;
	u64 event_fd;
};

/*
 * Common structure for ioctls mapping and unmapping buffers when using the
 * Gasket page_table module.
 */
struct gasket_page_table_ioctl {
	u64 page_table_index;
	u64 size;
	u64 host_address;
	u64 device_address;
};

/*
 * Common structure for ioctls mapping and unmapping buffers when using the
 * Gasket page_table module.
 * dma_address: phys addr start of coherent memory, allocated by kernel
 */
struct gasket_coherent_alloc_config_ioctl {
	u64 page_table_index;
	u64 enable;
	u64 size;
	u64 dma_address;
};

/* Base number for all Gasket-common IOCTLs */
#define GASKET_IOCTL_BASE 0xDC

/* Reset the device. */
#define GASKET_IOCTL_RESET _IO(GASKET_IOCTL_BASE, 0)

/* Associate the specified [event]fd with the specified interrupt. */
#define GASKET_IOCTL_SET_EVENTFD                                               \
	_IOW(GASKET_IOCTL_BASE, 1, struct gasket_interrupt_eventfd)

/*
 * Clears any eventfd associated with the specified interrupt. The (ulong)
 * argument is the interrupt number to clear.
 */
#define GASKET_IOCTL_CLEAR_EVENTFD _IOW(GASKET_IOCTL_BASE, 2, unsigned long)

/*
 * [Loopbacks only] Requests that the loopback device send the specified
 * interrupt to the host. The (ulong) argument is the number of the interrupt to
 * send.
 */
#define GASKET_IOCTL_LOOPBACK_INTERRUPT                                        \
	_IOW(GASKET_IOCTL_BASE, 3, unsigned long)

/* Queries the kernel for the number of page tables supported by the device. */
#define GASKET_IOCTL_NUMBER_PAGE_TABLES _IOR(GASKET_IOCTL_BASE, 4, u64)

/*
 * Queries the kernel for the maximum size of the page table.  Only the size and
 * page_table_index fields are used from the struct gasket_page_table_ioctl.
 */
#define GASKET_IOCTL_PAGE_TABLE_SIZE                                           \
	_IOWR(GASKET_IOCTL_BASE, 5, struct gasket_page_table_ioctl)

/*
 * Queries the kernel for the current simple page table size.  Only the size and
 * page_table_index fields are used from the struct gasket_page_table_ioctl.
 */
#define GASKET_IOCTL_SIMPLE_PAGE_TABLE_SIZE                                    \
	_IOWR(GASKET_IOCTL_BASE, 6, struct gasket_page_table_ioctl)

/*
 * Tells the kernel to change the split between the number of simple and
 * extended entries in the given page table. Only the size and page_table_index
 * fields are used from the struct gasket_page_table_ioctl.
 */
#define GASKET_IOCTL_PARTITION_PAGE_TABLE                                      \
	_IOW(GASKET_IOCTL_BASE, 7, struct gasket_page_table_ioctl)

/*
 * Tells the kernel to map size bytes at host_address to device_address in
 * page_table_index page table.
 */
#define GASKET_IOCTL_MAP_BUFFER                                                \
	_IOW(GASKET_IOCTL_BASE, 8, struct gasket_page_table_ioctl)

/*
 * Tells the kernel to unmap size bytes at host_address from device_address in
 * page_table_index page table.
 */
#define GASKET_IOCTL_UNMAP_BUFFER                                              \
	_IOW(GASKET_IOCTL_BASE, 9, struct gasket_page_table_ioctl)

/* Clear the interrupt counts stored for this device. */
#define GASKET_IOCTL_CLEAR_INTERRUPT_COUNTS _IO(GASKET_IOCTL_BASE, 10)

/* Enable/Disable and configure the coherent allocator. */
#define GASKET_IOCTL_CONFIG_COHERENT_ALLOCATOR                                 \
	_IOWR(GASKET_IOCTL_BASE, 11, struct gasket_coherent_alloc_config_ioctl)

#endif /* __GASKET_H__ */
