/******************************************************************************
 *
 * Module Name: utlock - Reader/Writer lock interfaces
 *
 *****************************************************************************/

/*
 * Copyright (C) 2000 - 2012, Intel Corp.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    substantially similar to the "NO WARRANTY" disclaimer below
 *    ("Disclaimer") and any redistribution must be conditioned upon
 *    including a substantially similar Disclaimer requirement for further
 *    binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGES.
 */

#include <acpi/acpi.h>
#include "accommon.h"

#define _COMPONENT          ACPI_UTILITIES
ACPI_MODULE_NAME("utlock")

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_create_rw_lock
 *              acpi_ut_delete_rw_lock
 *
 * PARAMETERS:  Lock                - Pointer to a valid RW lock
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
		return status;
	}

	status = acpi_os_create_mutex(&lock->writer_mutex);
	return status;
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
 * PARAMETERS:  Lock                - Pointer to a valid RW lock
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
		return status;
	}

	/* Acquire the write lock only for the first reader */

	lock->num_readers++;
	if (lock->num_readers == 1) {
		status =
		    acpi_os_acquire_mutex(lock->writer_mutex,
					  ACPI_WAIT_FOREVER);
	}

	acpi_os_release_mutex(lock->reader_mutex);
	return status;
}

acpi_status acpi_ut_release_read_lock(struct acpi_rw_lock *lock)
{
	acpi_status status;

	status = acpi_os_acquire_mutex(lock->reader_mutex, ACPI_WAIT_FOREVER);
	if (ACPI_FAILURE(status)) {
		return status;
	}

	/* Release the write lock only for the very last reader */

	lock->num_readers--;
	if (lock->num_readers == 0) {
		acpi_os_release_mutex(lock->writer_mutex);
	}

	acpi_os_release_mutex(lock->reader_mutex);
	return status;
}

/*******************************************************************************
 *
 * FUNCTION:    acpi_ut_acquire_write_lock
 *              acpi_ut_release_write_lock
 *
 * PARAMETERS:  Lock                - Pointer to a valid RW lock
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
	return status;
}

void acpi_ut_release_write_lock(struct acpi_rw_lock *lock)
{

	acpi_os_release_mutex(lock->writer_mutex);
}
