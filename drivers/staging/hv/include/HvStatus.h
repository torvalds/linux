/*
 *
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 *
 */

#ifndef __HVSTATUS_H
#define __HVSTATUS_H

/* Status codes for hypervisor operations. */

/*
 * HV_STATUS_SUCCESS
 * The specified hypercall succeeded
 */
#define HV_STATUS_SUCCESS				((u16)0x0000)

/*
 * HV_STATUS_INVALID_HYPERCALL_CODE
 * The hypervisor does not support the operation because the specified
 * hypercall code is not supported.
 */
#define HV_STATUS_INVALID_HYPERCALL_CODE		((u16)0x0002)

/*
 * HV_STATUS_INVALID_HYPERCALL_INPUT
 * The hypervisor does not support the operation because the encoding for the
 * hypercall input register is not supported.
 */
#define HV_STATUS_INVALID_HYPERCALL_INPUT		((u16)0x0003)

/*
 * HV_STATUS_INVALID_ALIGNMENT
 * The hypervisor could not perform the operation beacuse a parameter has an
 * invalid alignment.
 */
#define HV_STATUS_INVALID_ALIGNMENT			((u16)0x0004)

/*
 * HV_STATUS_INVALID_PARAMETER
 * The hypervisor could not perform the operation beacuse an invalid parameter
 * was specified.
 */
#define HV_STATUS_INVALID_PARAMETER			((u16)0x0005)

/*
 * HV_STATUS_ACCESS_DENIED
 * Access to the specified object was denied.
 */
#define HV_STATUS_ACCESS_DENIED				((u16)0x0006)

/*
 * HV_STATUS_INVALID_PARTITION_STATE
 * The hypervisor could not perform the operation because the partition is
 * entering or in an invalid state.
 */
#define HV_STATUS_INVALID_PARTITION_STATE		((u16)0x0007)

/*
 * HV_STATUS_OPERATION_DENIED
 * The operation is not allowed in the current state.
 */
#define HV_STATUS_OPERATION_DENIED			((u16)0x0008)

/*
 * HV_STATUS_UNKNOWN_PROPERTY
 * The hypervisor does not recognize the specified partition property.
 */
#define HV_STATUS_UNKNOWN_PROPERTY			((u16)0x0009)

/*
 * HV_STATUS_PROPERTY_VALUE_OUT_OF_RANGE
 * The specified value of a partition property is out of range or violates an
 * invariant.
 */
#define HV_STATUS_PROPERTY_VALUE_OUT_OF_RANGE		((u16)0x000A)

/*
 * HV_STATUS_INSUFFICIENT_MEMORY
 * There is not enough memory in the hypervisor pool to complete the operation.
 */
#define HV_STATUS_INSUFFICIENT_MEMORY			((u16)0x000B)

/*
 * HV_STATUS_PARTITION_TOO_DEEP
 * The maximum partition depth has been exceeded for the partition hierarchy.
 */
#define HV_STATUS_PARTITION_TOO_DEEP			((u16)0x000C)

/*
 * HV_STATUS_INVALID_PARTITION_ID
 * A partition with the specified partition Id does not exist.
 */
#define HV_STATUS_INVALID_PARTITION_ID			((u16)0x000D)

/*
 * HV_STATUS_INVALID_VP_INDEX
 * The hypervisor could not perform the operation because the specified VP
 * index is invalid.
 */
#define HV_STATUS_INVALID_VP_INDEX			((u16)0x000E)

/*
 * HV_STATUS_NOT_FOUND
 * The iteration is complete; no addition items in the iteration could be
 * found.
 */
#define HV_STATUS_NOT_FOUND				((u16)0x0010)

/*
 * HV_STATUS_INVALID_PORT_ID
 * The hypervisor could not perform the operation because the specified port
 * identifier is invalid.
 */
#define HV_STATUS_INVALID_PORT_ID			((u16)0x0011)

/*
 * HV_STATUS_INVALID_CONNECTION_ID
 * The hypervisor could not perform the operation because the specified
 * connection identifier is invalid.
 */
#define HV_STATUS_INVALID_CONNECTION_ID			((u16)0x0012)

/*
 * HV_STATUS_INSUFFICIENT_BUFFERS
 * You did not supply enough message buffers to send a message.
 */
#define HV_STATUS_INSUFFICIENT_BUFFERS			((u16)0x0013)

/*
 * HV_STATUS_NOT_ACKNOWLEDGED
 * The previous virtual interrupt has not been acknowledged.
 */
#define HV_STATUS_NOT_ACKNOWLEDGED			((u16)0x0014)

/*
 * HV_STATUS_INVALID_VP_STATE
 * A virtual processor is not in the correct state for the performance of the
 * indicated operation.
 */
#define HV_STATUS_INVALID_VP_STATE			((u16)0x0015)

/*
 * HV_STATUS_ACKNOWLEDGED
 * The previous virtual interrupt has already been acknowledged.
 */
#define HV_STATUS_ACKNOWLEDGED				((u16)0x0016)

/*
 * HV_STATUS_INVALID_SAVE_RESTORE_STATE
 * The indicated partition is not in a valid state for saving or restoring.
 */
#define HV_STATUS_INVALID_SAVE_RESTORE_STATE		((u16)0x0017)

/*
 * HV_STATUS_INVALID_SYNIC_STATE
 * The hypervisor could not complete the operation because a required feature
 * of the synthetic interrupt controller (SynIC) was disabled.
 */
#define HV_STATUS_INVALID_SYNIC_STATE			((u16)0x0018)

/*
 * HV_STATUS_OBJECT_IN_USE
 * The hypervisor could not perform the operation because the object or value
 * was either already in use or being used for a purpose that would not permit
 * completing the operation.
 */
#define HV_STATUS_OBJECT_IN_USE				((u16)0x0019)

/*
 * HV_STATUS_INVALID_PROXIMITY_DOMAIN_INFO
 * The proximity domain information is invalid.
 */
#define HV_STATUS_INVALID_PROXIMITY_DOMAIN_INFO		((u16)0x001A)

/*
 * HV_STATUS_NO_DATA
 * An attempt to retrieve debugging data failed because none was available.
 */
#define HV_STATUS_NO_DATA				((u16)0x001B)

/*
 * HV_STATUS_INACTIVE
 * The physical connection being used for debuggging has not recorded any
 * receive activity since the last operation.
 */
#define HV_STATUS_INACTIVE				((u16)0x001C)

/*
 * HV_STATUS_NO_RESOURCES
 * There are not enough resources to complete the operation.
 */
#define HV_STATUS_NO_RESOURCES				((u16)0x001D)

/*
 * HV_STATUS_FEATURE_UNAVAILABLE
 * A hypervisor feature is not available to the user.
 */
#define HV_STATUS_FEATURE_UNAVAILABLE			((u16)0x001E)

/*
 * HV_STATUS_UNSUCCESSFUL
 * {Operation Failed} The requested operation was unsuccessful.
 */
#define HV_STATUS_UNSUCCESSFUL				((u16)0x1001)

/*
 * HV_STATUS_INSUFFICIENT_BUFFER
 * The specified buffer was too small to contain all of the requested data.
 */
#define HV_STATUS_INSUFFICIENT_BUFFER			((u16)0x1002)

/*
 * HV_STATUS_GPA_NOT_PRESENT
 * The guest physical address is not currently associated with a system
 * physical address.
 */
#define HV_STATUS_GPA_NOT_PRESENT			((u16)0x1003)

/*
 * HV_STATUS_GUEST_PAGE_FAULT
 * The operation would have resulted in a page fault in the guest.
 */
#define HV_STATUS_GUEST_PAGE_FAULT			((u16)0x1004)

/*
 * HV_STATUS_RUNDOWN_DISABLED
 * The operation cannot proceed as the rundown object was marked disabled.
 */
#define HV_STATUS_RUNDOWN_DISABLED			((u16)0x1005)

/*
 * HV_STATUS_KEY_ALREADY_EXISTS
 * The entry cannot be added as another entry with the same key already exists.
 */
#define HV_STATUS_KEY_ALREADY_EXISTS			((u16)0x1006)

/*
 * HV_STATUS_GPA_INTERCEPT
 * The operation resulted an intercept on a region of guest physical memory.
 */
#define HV_STATUS_GPA_INTERCEPT				((u16)0x1007)

/*
 * HV_STATUS_GUEST_GENERAL_PROTECTION_FAULT
 * The operation would have resulted in a general protection fault in the
 * guest.
 */
#define HV_STATUS_GUEST_GENERAL_PROTECTION_FAULT	((u16)0x1008)

/*
 * HV_STATUS_GUEST_STACK_FAULT
 * The operation would have resulted in a stack fault in the guest.
 */
#define HV_STATUS_GUEST_STACK_FAULT			((u16)0x1009)

/*
 * HV_STATUS_GUEST_INVALID_OPCODE_FAULT
 * The operation would have resulted in an invalid opcode fault in the guest.
 */
#define HV_STATUS_GUEST_INVALID_OPCODE_FAULT		((u16)0x100A)

/*
 * HV_STATUS_FINALIZE_INCOMPLETE
 * The partition is not completely finalized.
 */
#define HV_STATUS_FINALIZE_INCOMPLETE			((u16)0x100B)

/*
 * HV_STATUS_GUEST_MACHINE_CHECK_ABORT
 * The operation would have resulted in an machine check abort in the guest.
 */
#define HV_STATUS_GUEST_MACHINE_CHECK_ABORT		((u16)0x100C)

/*
 * HV_STATUS_ILLEGAL_OVERLAY_ACCESS
 * An illegal access was attempted to an overlay page.
 */
#define HV_STATUS_ILLEGAL_OVERLAY_ACCESS		((u16)0x100D)

/*
 * HV_STATUS_INSUFFICIENT_SYSTEM_VA
 * There is not enough system VA space available to satisfy the request,
 */
#define HV_STATUS_INSUFFICIENT_SYSTEM_VA		((u16)0x100E)

/*
 * HV_STATUS_VIRTUAL_ADDRESS_NOT_MAPPED
 * The passed virtual address was not mapped in the hypervisor address space.
 */
#define HV_STATUS_VIRTUAL_ADDRESS_NOT_MAPPED		((u16)0x100F)

/*
 * HV_STATUS_NOT_IMPLEMENTED
 * The requested operation is not implemented in this version of the
 * hypervisor.
 */
#define HV_STATUS_NOT_IMPLEMENTED			((u16)0x1010)

/*
 * HV_STATUS_VMX_INSTRUCTION_FAILED
 * The requested VMX instruction failed to complete succesfully.
 */
#define HV_STATUS_VMX_INSTRUCTION_FAILED		((u16)0x1011)

/*
 * HV_STATUS_VMX_INSTRUCTION_FAILED_WITH_STATUS
 * The requested VMX instruction failed to complete succesfully indicating
 * status.
 */
#define HV_STATUS_VMX_INSTRUCTION_FAILED_WITH_STATUS	((u16)0x1012)

/*
 * HV_STATUS_MSR_ACCESS_FAILED
 * The requested access to the model specific register failed.
 */
#define HV_STATUS_MSR_ACCESS_FAILED		((u16)0x1013)

/*
 * HV_STATUS_CR_ACCESS_FAILED
 * The requested access to the control register failed.
 */
#define HV_STATUS_CR_ACCESS_FAILED		((u16)0x1014)

/*
 * HV_STATUS_TIMEOUT
 * The specified timeout expired before the operation completed.
 */
#define HV_STATUS_TIMEOUT			((u16)0x1016)

/*
 * HV_STATUS_MSR_INTERCEPT
 * The requested access to the model specific register generated an intercept.
 */
#define HV_STATUS_MSR_INTERCEPT			((u16)0x1017)

/*
 * HV_STATUS_CPUID_INTERCEPT
 * The CPUID instruction generated an intercept.
 */
#define HV_STATUS_CPUID_INTERCEPT		((u16)0x1018)

/*
 * HV_STATUS_REPEAT_INSTRUCTION
 * The current instruction should be repeated and the instruction pointer not
 * advanced.
 */
#define HV_STATUS_REPEAT_INSTRUCTION		((u16)0x1019)

/*
 * HV_STATUS_PAGE_PROTECTION_VIOLATION
 * The current instruction should be repeated and the instruction pointer not
 * advanced.
 */
#define HV_STATUS_PAGE_PROTECTION_VIOLATION	((u16)0x101A)

/*
 * HV_STATUS_PAGE_TABLE_INVALID
 * The current instruction should be repeated and the instruction pointer not
 * advanced.
 */
#define HV_STATUS_PAGE_TABLE_INVALID		((u16)0x101B)

/*
 * HV_STATUS_PAGE_NOT_PRESENT
 * The current instruction should be repeated and the instruction pointer not
 * advanced.
 */
#define HV_STATUS_PAGE_NOT_PRESENT		((u16)0x101C)

/*
 * HV_STATUS_IO_INTERCEPT
 * The requested access to the I/O port generated an intercept.
 */
#define HV_STATUS_IO_INTERCEPT				((u16)0x101D)

/*
 * HV_STATUS_NOTHING_TO_DO
 * There is nothing to do.
 */
#define HV_STATUS_NOTHING_TO_DO				((u16)0x101E)

/*
 * HV_STATUS_THREAD_TERMINATING
 * The requested thread is terminating.
 */
#define HV_STATUS_THREAD_TERMINATING			((u16)0x101F)

/*
 * HV_STATUS_SECTION_ALREADY_CONSTRUCTED
 * The specified section was already constructed.
 */
#define HV_STATUS_SECTION_ALREADY_CONSTRUCTED		((u16)0x1020)

/* HV_STATUS_SECTION_NOT_ALREADY_CONSTRUCTED
 * The specified section was not already constructed.
 */
#define HV_STATUS_SECTION_NOT_ALREADY_CONSTRUCTED	((u16)0x1021)

/*
 * HV_STATUS_PAGE_ALREADY_COMMITTED
 * The specified virtual address was already backed by physical memory.
 */
#define HV_STATUS_PAGE_ALREADY_COMMITTED		((u16)0x1022)

/*
 * HV_STATUS_PAGE_NOT_ALREADY_COMMITTED
 * The specified virtual address was not already backed by physical memory.
 */
#define HV_STATUS_PAGE_NOT_ALREADY_COMMITTED		((u16)0x1023)

/*
 * HV_STATUS_COMMITTED_PAGES_REMAIN
 * Committed pages remain in the section.
 */
#define HV_STATUS_COMMITTED_PAGES_REMAIN		((u16)0x1024)

/*
 * HV_STATUS_NO_REMAINING_COMMITTED_PAGES
 * No additional committed pages beyond the specified page exist in the
 * section.
 */
#define HV_STATUS_NO_REMAINING_COMMITTED_PAGES		((u16)0x1025)

/*
 * HV_STATUS_INSUFFICIENT_COMPARTMENT_VA
 * The VA space of the compartment is exhausted.
 */
#define HV_STATUS_INSUFFICIENT_COMPARTMENT_VA		((u16)0x1026)

/*
 * HV_STATUS_DEREF_SPA_LIST_FULL
 * The SPA dereference list is full, and there are additional entries to be
 * added to it.
 */
#define HV_STATUS_DEREF_SPA_LIST_FULL			((u16)0x1027)

/*
 * HV_STATUS_GPA_OUT_OF_RANGE
 * The supplied GPA is out of range.
 */
#define HV_STATUS_GPA_OUT_OF_RANGE			((u16)0x1027)

/*
 * HV_STATUS_NONVOLATILE_XMM_STALE
 * The XMM register that was being accessed is stale.
 */
#define HV_STATUS_NONVOLATILE_XMM_STALE			((u16)0x1028)

/* HV_STATUS_UNSUPPORTED_PROCESSOR
 * The hypervisor does not support the processors in this system.
 */
#define HV_STATUS_UNSUPPORTED_PROCESSOR			((u16)0x1029)

/*
 * HV_STATUS_INSUFFICIENT_CROM_SPACE
 * Insufficient space existed for copying over the CROM contents.
 */
#define HV_STATUS_INSUFFICIENT_CROM_SPACE		((u16)0x2000)

/*
 * HV_STATUS_BAD_CROM_FORMAT
 * The contents of the CROM failed validation attempts.
 */
#define HV_STATUS_BAD_CROM_FORMAT			((u16)0x2001)

/*
 * HV_STATUS_UNSUPPORTED_CROM_FORMAT
 * The contents of the CROM contain contents the parser doesn't support.
 */
#define HV_STATUS_UNSUPPORTED_CROM_FORMAT		((u16)0x2002)

/*
 * HV_STATUS_UNSUPPORTED_CONTROLLER
 * The register format of the OHCI controller specified for debugging is not
 * supported.
 */
#define HV_STATUS_UNSUPPORTED_CONTROLLER		((u16)0x2003)

/*
 * HV_STATUS_CROM_TOO_LARGE
 * The CROM contents were to large to copy over.
 */
#define HV_STATUS_CROM_TOO_LARGE			((u16)0x2004)

/*
 * HV_STATUS_CONTROLLER_IN_USE
 * The OHCI controller specified for debugging cannot be used as it is already
 * in use.
 */
#define HV_STATUS_CONTROLLER_IN_USE			((u16)0x2005)

#endif
