// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/******************************************************************************
 *
 * Module Name: utlock - Reader/Writer lock interfaces
 *
 * Copyright (C) 2000 - 2019, Intel Corp.
 *
 *****************************************************************************/

#include <acpi/acpi.h>
#include "accommon.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utlock")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_create_rw_lock
 *              acpi_ut_delete_rw_lock
 *
 * PARAMETERS:  lock                - Pointer to a valid RW lock
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Reader/writer lock creation and deletion interfaces.
 *
 ******************************************************************************/
acpi_status acpi_ut_create_rw_lock(struct acpi_rw_lock *lock)
{
	acpi_status status;

	lock->num_readers = 0;
	status = acpi_os_create_mutex(&lock->reader_mutex);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	status = acpi_os_create_mutex(&lock->writer_mutex);
	return (status);
}

void acpi_ut_delete_rw_lock(struct acpi_rw_lock *lock)
{

	acpi_os_delete_mutex(lock->reader_mutex);
	acpi_os_delete_mutex(lock->writer_mutex);

	lock->num_readers = 0;
	lock->reader_mutex = NULL;
	lock->writer_mutex = NULL;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_acquire_read_lock
 *              acpi_ut_release_read_lock
 *
 * PARAMETERS:  lock                - Pointer to a valid RW lock
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Reader interfaces for reader/writer locks. On acquisition,
 *              only the first reader acquires the write mutex. On release,
 *              only the last reader releases the write mutex. Although this
 *              algorithm can in theory starve writers, this should not be a
 *              problem with ACPICA since the subsystem is infrequently used
 *              in comparison to (for example) an I/O system.
 *
 ******************************************************************************/

acpi_status acpi_ut_acquire_read_lock(struct acpi_rw_lock *lock)
{
	acpi_status status;

	status = acpi_os_acquire_mutex(lock->reader_mutex, ACPI_WAIT_FOREVER);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/* Acquire the write lock only for the first reader */

	lock->num_readers++;
	if (lock->num_readers == 1) {
		status =
		    acpi_os_acquire_mutex(lock->writer_mutex,
					  ACPI_WAIT_FOREVER);
	}

	acpi_os_release_mutex(lock->reader_mutex);
	return (status);
}

acpi_status acpi_ut_release_read_lock(struct acpi_rw_lock *lock)
{
	acpi_status status;

	status = acpi_os_acquire_mutex(lock->reader_mutex, ACPI_WAIT_FOREVER);
	if (ACPI_FAILURE(status)) {
		return (status);
	}

	/* Release the write lock only for the very last reader */

	lock->num_readers--;
	if (lock->num_readers == 0) {
		acpi_os_release_mutex(lock->writer_mutex);
	}

	acpi_os_release_mutex(lock->reader_mutex);
	return (status);
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_acquire_write_lock
 *              acpi_ut_release_write_lock
 *
 * PARAMETERS:  lock                - Pointer to a valid RW lock
 *
 * RETURN:      Status
 *
 * DESCRIPTION: Writer interfaces for reader/writer locks. Simply acquire or
 *              release the writer mutex associated with the lock. Acquisition
 *              of the lock is fully exclusive and will block all readers and
 *              writers until it is released.
 *
 ******************************************************************************/

acpi_status acpi_ut_acquire_write_lock(struct acpi_rw_lock *lock)
{
	acpi_status status;

	status = acpi_os_acquire_mutex(lock->writer_mutex, ACPI_WAIT_FOREVER);
	return (status);
}

void acpi_ut_release_write_lock(struct acpi_rw_lock *lock)
{

	acpi_os_release_mutex(lock->writer_mutex);
}
