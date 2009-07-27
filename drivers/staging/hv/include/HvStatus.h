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


/* begin_hvgdk */

/* Status codes for hypervisor operations. */

typedef u16 HV_STATUS, *PHV_STATUS;


/* MessageId: HV_STATUS_SUCCESS */

/* MessageText: */

/* The specified hypercall succeeded */

#define HV_STATUS_SUCCESS                ((HV_STATUS)0x0000)


/* MessageId: HV_STATUS_INVALID_HYPERCALL_CODE */

/* MessageText: */

/* The hypervisor does not support the operation because the specified hypercall code is not supported. */

#define HV_STATUS_INVALID_HYPERCALL_CODE ((HV_STATUS)0x0002)


/* MessageId: HV_STATUS_INVALID_HYPERCALL_INPUT */

/* MessageText: */

/* The hypervisor does not support the operation because the encoding for the hypercall input register is not supported. */

#define HV_STATUS_INVALID_HYPERCALL_INPUT ((HV_STATUS)0x0003)


/* MessageId: HV_STATUS_INVALID_ALIGNMENT */

/* MessageText: */

/* The hypervisor could not perform the operation beacuse a parameter has an invalid alignment. */

#define HV_STATUS_INVALID_ALIGNMENT      ((HV_STATUS)0x0004)


/* MessageId: HV_STATUS_INVALID_PARAMETER */

/* MessageText: */

/* The hypervisor could not perform the operation beacuse an invalid parameter was specified. */

#define HV_STATUS_INVALID_PARAMETER      ((HV_STATUS)0x0005)


/* MessageId: HV_STATUS_ACCESS_DENIED */

/* MessageText: */

/* Access to the specified object was denied. */

#define HV_STATUS_ACCESS_DENIED          ((HV_STATUS)0x0006)


/* MessageId: HV_STATUS_INVALID_PARTITION_STATE */

/* MessageText: */

/* The hypervisor could not perform the operation because the partition is entering or in an invalid state. */

#define HV_STATUS_INVALID_PARTITION_STATE ((HV_STATUS)0x0007)


/* MessageId: HV_STATUS_OPERATION_DENIED */

/* MessageText: */

/* The operation is not allowed in the current state. */

#define HV_STATUS_OPERATION_DENIED       ((HV_STATUS)0x0008)


/* MessageId: HV_STATUS_UNKNOWN_PROPERTY */

/* MessageText: */

/* The hypervisor does not recognize the specified partition property. */

#define HV_STATUS_UNKNOWN_PROPERTY       ((HV_STATUS)0x0009)


/* MessageId: HV_STATUS_PROPERTY_VALUE_OUT_OF_RANGE */

/* MessageText: */

/* The specified value of a partition property is out of range or violates an invariant. */

#define HV_STATUS_PROPERTY_VALUE_OUT_OF_RANGE ((HV_STATUS)0x000A)


/* MessageId: HV_STATUS_INSUFFICIENT_MEMORY */

/* MessageText: */

/* There is not enough memory in the hypervisor pool to complete the operation. */

#define HV_STATUS_INSUFFICIENT_MEMORY    ((HV_STATUS)0x000B)


/* MessageId: HV_STATUS_PARTITION_TOO_DEEP */

/* MessageText: */

/* The maximum partition depth has been exceeded for the partition hierarchy. */

#define HV_STATUS_PARTITION_TOO_DEEP     ((HV_STATUS)0x000C)


/* MessageId: HV_STATUS_INVALID_PARTITION_ID */

/* MessageText: */

/* A partition with the specified partition Id does not exist. */

#define HV_STATUS_INVALID_PARTITION_ID   ((HV_STATUS)0x000D)


/* MessageId: HV_STATUS_INVALID_VP_INDEX */

/* MessageText: */

/* The hypervisor could not perform the operation because the specified VP index is invalid. */

#define HV_STATUS_INVALID_VP_INDEX       ((HV_STATUS)0x000E)


/* MessageId: HV_STATUS_NOT_FOUND */

/* MessageText: */

/* The iteration is complete; no addition items in the iteration could be found. */

#define HV_STATUS_NOT_FOUND              ((HV_STATUS)0x0010)


/* MessageId: HV_STATUS_INVALID_PORT_ID */

/* MessageText: */

/* The hypervisor could not perform the operation because the specified port identifier is invalid. */

#define HV_STATUS_INVALID_PORT_ID        ((HV_STATUS)0x0011)


/* MessageId: HV_STATUS_INVALID_CONNECTION_ID */

/* MessageText: */

/* The hypervisor could not perform the operation because the specified connection identifier is invalid. */

#define HV_STATUS_INVALID_CONNECTION_ID  ((HV_STATUS)0x0012)


/* MessageId: HV_STATUS_INSUFFICIENT_BUFFERS */

/* MessageText: */

/* You did not supply enough message buffers to send a message. */

#define HV_STATUS_INSUFFICIENT_BUFFERS   ((HV_STATUS)0x0013)


/* MessageId: HV_STATUS_NOT_ACKNOWLEDGED */

/* MessageText: */

/* The previous virtual interrupt has not been acknowledged. */

#define HV_STATUS_NOT_ACKNOWLEDGED       ((HV_STATUS)0x0014)


/* MessageId: HV_STATUS_INVALID_VP_STATE */

/* MessageText: */

/* A virtual processor is not in the correct state for the performance of the indicated operation. */

#define HV_STATUS_INVALID_VP_STATE       ((HV_STATUS)0x0015)


/* MessageId: HV_STATUS_ACKNOWLEDGED */

/* MessageText: */

/* The previous virtual interrupt has already been acknowledged. */

#define HV_STATUS_ACKNOWLEDGED           ((HV_STATUS)0x0016)


/* MessageId: HV_STATUS_INVALID_SAVE_RESTORE_STATE */

/* MessageText: */

/* The indicated partition is not in a valid state for saving or restoring. */

#define HV_STATUS_INVALID_SAVE_RESTORE_STATE ((HV_STATUS)0x0017)


/* MessageId: HV_STATUS_INVALID_SYNIC_STATE */

/* MessageText: */

/* The hypervisor could not complete the operation because a required feature of the synthetic interrupt controller (SynIC) was disabled. */

#define HV_STATUS_INVALID_SYNIC_STATE    ((HV_STATUS)0x0018)


/* MessageId: HV_STATUS_OBJECT_IN_USE */

/* MessageText: */

/* The hypervisor could not perform the operation because the object or value was either already in use or being used for a purpose that would not permit completing the operation. */

#define HV_STATUS_OBJECT_IN_USE          ((HV_STATUS)0x0019)


/* MessageId: HV_STATUS_INVALID_PROXIMITY_DOMAIN_INFO */

/* MessageText: */

/* The proximity domain information is invalid. */

#define HV_STATUS_INVALID_PROXIMITY_DOMAIN_INFO ((HV_STATUS)0x001A)


/* MessageId: HV_STATUS_NO_DATA */

/* MessageText: */

/* An attempt to retrieve debugging data failed because none was available. */

#define HV_STATUS_NO_DATA                ((HV_STATUS)0x001B)


/* MessageId: HV_STATUS_INACTIVE */

/* MessageText: */

/* The physical connection being used for debuggging has not recorded any receive activity since the last operation. */

#define HV_STATUS_INACTIVE               ((HV_STATUS)0x001C)


/* MessageId: HV_STATUS_NO_RESOURCES */

/* MessageText: */

/* There are not enough resources to complete the operation. */

#define HV_STATUS_NO_RESOURCES           ((HV_STATUS)0x001D)


/* MessageId: HV_STATUS_FEATURE_UNAVAILABLE */

/* MessageText: */

/* A hypervisor feature is not available to the user. */

#define HV_STATUS_FEATURE_UNAVAILABLE    ((HV_STATUS)0x001E)

/* end_hvgdk */


/* MessageId: HV_STATUS_UNSUCCESSFUL */

/* MessageText: */

/* {Operation Failed} */
/* The requested operation was unsuccessful. */

#define HV_STATUS_UNSUCCESSFUL           ((HV_STATUS)0x1001)


/* MessageId: HV_STATUS_INSUFFICIENT_BUFFER */

/* MessageText: */

/* The specified buffer was too small to contain all of the requested data. */

#define HV_STATUS_INSUFFICIENT_BUFFER    ((HV_STATUS)0x1002)


/* MessageId: HV_STATUS_GPA_NOT_PRESENT */

/* MessageText: */

/* The guest physical address is not currently associated with a system physical address. */

#define HV_STATUS_GPA_NOT_PRESENT        ((HV_STATUS)0x1003)


/* MessageId: HV_STATUS_GUEST_PAGE_FAULT */

/* MessageText: */

/* The operation would have resulted in a page fault in the guest. */

#define HV_STATUS_GUEST_PAGE_FAULT       ((HV_STATUS)0x1004)


/* MessageId: HV_STATUS_RUNDOWN_DISABLED */

/* MessageText: */

/* The operation cannot proceed as the rundown object was marked disabled. */

#define HV_STATUS_RUNDOWN_DISABLED       ((HV_STATUS)0x1005)


/* MessageId: HV_STATUS_KEY_ALREADY_EXISTS */

/* MessageText: */

/* The entry cannot be added as another entry with the same key already exists. */

#define HV_STATUS_KEY_ALREADY_EXISTS     ((HV_STATUS)0x1006)


/* MessageId: HV_STATUS_GPA_INTERCEPT */

/* MessageText: */

/* The operation resulted an intercept on a region of guest physical memory. */

#define HV_STATUS_GPA_INTERCEPT          ((HV_STATUS)0x1007)


/* MessageId: HV_STATUS_GUEST_GENERAL_PROTECTION_FAULT */

/* MessageText: */

/* The operation would have resulted in a general protection fault in the guest. */

#define HV_STATUS_GUEST_GENERAL_PROTECTION_FAULT ((HV_STATUS)0x1008)


/* MessageId: HV_STATUS_GUEST_STACK_FAULT */

/* MessageText: */

/* The operation would have resulted in a stack fault in the guest. */

#define HV_STATUS_GUEST_STACK_FAULT      ((HV_STATUS)0x1009)


/* MessageId: HV_STATUS_GUEST_INVALID_OPCODE_FAULT */

/* MessageText: */

/* The operation would have resulted in an invalid opcode fault in the guest. */

#define HV_STATUS_GUEST_INVALID_OPCODE_FAULT ((HV_STATUS)0x100A)


/* MessageId: HV_STATUS_FINALIZE_INCOMPLETE */

/* MessageText: */

/* The partition is not completely finalized. */

#define HV_STATUS_FINALIZE_INCOMPLETE    ((HV_STATUS)0x100B)


/* MessageId: HV_STATUS_GUEST_MACHINE_CHECK_ABORT */

/* MessageText: */

/* The operation would have resulted in an machine check abort in the guest. */

#define HV_STATUS_GUEST_MACHINE_CHECK_ABORT ((HV_STATUS)0x100C)


/* MessageId: HV_STATUS_ILLEGAL_OVERLAY_ACCESS */

/* MessageText: */

/* An illegal access was attempted to an overlay page. */

#define HV_STATUS_ILLEGAL_OVERLAY_ACCESS ((HV_STATUS)0x100D)


/* MessageId: HV_STATUS_INSUFFICIENT_SYSTEM_VA */

/* MessageText: */

/* There is not enough system VA space available to satisfy the request, */

#define HV_STATUS_INSUFFICIENT_SYSTEM_VA ((HV_STATUS)0x100E)


/* MessageId: HV_STATUS_VIRTUAL_ADDRESS_NOT_MAPPED */

/* MessageText: */

/* The passed virtual address was not mapped in the hypervisor address space. */

#define HV_STATUS_VIRTUAL_ADDRESS_NOT_MAPPED ((HV_STATUS)0x100F)


/* MessageId: HV_STATUS_NOT_IMPLEMENTED */

/* MessageText: */

/* The requested operation is not implemented in this version of the hypervisor. */

#define HV_STATUS_NOT_IMPLEMENTED        ((HV_STATUS)0x1010)


/* MessageId: HV_STATUS_VMX_INSTRUCTION_FAILED */

/* MessageText: */

/* The requested VMX instruction failed to complete succesfully. */

#define HV_STATUS_VMX_INSTRUCTION_FAILED ((HV_STATUS)0x1011)


/* MessageId: HV_STATUS_VMX_INSTRUCTION_FAILED_WITH_STATUS */

/* MessageText: */

/* The requested VMX instruction failed to complete succesfully indicating status. */

#define HV_STATUS_VMX_INSTRUCTION_FAILED_WITH_STATUS ((HV_STATUS)0x1012)


/* MessageId: HV_STATUS_MSR_ACCESS_FAILED */

/* MessageText: */

/* The requested access to the model specific register failed. */

#define HV_STATUS_MSR_ACCESS_FAILED      ((HV_STATUS)0x1013)


/* MessageId: HV_STATUS_CR_ACCESS_FAILED */

/* MessageText: */

/* The requested access to the control register failed. */

#define HV_STATUS_CR_ACCESS_FAILED       ((HV_STATUS)0x1014)


/* MessageId: HV_STATUS_TIMEOUT */

/* MessageText: */

/* The specified timeout expired before the operation completed. */

#define HV_STATUS_TIMEOUT                ((HV_STATUS)0x1016)


/* MessageId: HV_STATUS_MSR_INTERCEPT */

/* MessageText: */

/* The requested access to the model specific register generated an intercept. */

#define HV_STATUS_MSR_INTERCEPT          ((HV_STATUS)0x1017)


/* MessageId: HV_STATUS_CPUID_INTERCEPT */

/* MessageText: */

/* The CPUID instruction generated an intercept. */

#define HV_STATUS_CPUID_INTERCEPT        ((HV_STATUS)0x1018)


/* MessageId: HV_STATUS_REPEAT_INSTRUCTION */

/* MessageText: */

/* The current instruction should be repeated and the instruction pointer not advanced. */

#define HV_STATUS_REPEAT_INSTRUCTION     ((HV_STATUS)0x1019)


/* MessageId: HV_STATUS_PAGE_PROTECTION_VIOLATION */

/* MessageText: */

/* The current instruction should be repeated and the instruction pointer not advanced. */

#define HV_STATUS_PAGE_PROTECTION_VIOLATION ((HV_STATUS)0x101A)


/* MessageId: HV_STATUS_PAGE_TABLE_INVALID */

/* MessageText: */

/* The current instruction should be repeated and the instruction pointer not advanced. */

#define HV_STATUS_PAGE_TABLE_INVALID     ((HV_STATUS)0x101B)


/* MessageId: HV_STATUS_PAGE_NOT_PRESENT */

/* MessageText: */

/* The current instruction should be repeated and the instruction pointer not advanced. */

#define HV_STATUS_PAGE_NOT_PRESENT       ((HV_STATUS)0x101C)


/* MessageId: HV_STATUS_IO_INTERCEPT */

/* MessageText: */

/* The requested access to the I/O port generated an intercept. */

#define HV_STATUS_IO_INTERCEPT           ((HV_STATUS)0x101D)


/* MessageId: HV_STATUS_NOTHING_TO_DO */

/* MessageText: */

/* There is nothing to do. */

#define HV_STATUS_NOTHING_TO_DO          ((HV_STATUS)0x101E)


/* MessageId: HV_STATUS_THREAD_TERMINATING */

/* MessageText: */

/* The requested thread is terminating. */

#define HV_STATUS_THREAD_TERMINATING     ((HV_STATUS)0x101F)


/* MessageId: HV_STATUS_SECTION_ALREADY_CONSTRUCTED */

/* MessageText: */

/* The specified section was already constructed. */

#define HV_STATUS_SECTION_ALREADY_CONSTRUCTED ((HV_STATUS)0x1020)


/* MessageId: HV_STATUS_SECTION_NOT_ALREADY_CONSTRUCTED */

/* MessageText: */

/* The specified section was not already constructed. */

#define HV_STATUS_SECTION_NOT_ALREADY_CONSTRUCTED ((HV_STATUS)0x1021)


/* MessageId: HV_STATUS_PAGE_ALREADY_COMMITTED */

/* MessageText: */

/* The specified virtual address was already backed by physical memory. */

#define HV_STATUS_PAGE_ALREADY_COMMITTED ((HV_STATUS)0x1022)


/* MessageId: HV_STATUS_PAGE_NOT_ALREADY_COMMITTED */

/* MessageText: */

/* The specified virtual address was not already backed by physical memory. */

#define HV_STATUS_PAGE_NOT_ALREADY_COMMITTED ((HV_STATUS)0x1023)


/* MessageId: HV_STATUS_COMMITTED_PAGES_REMAIN */

/* MessageText: */

/* Committed pages remain in the section. */

#define HV_STATUS_COMMITTED_PAGES_REMAIN ((HV_STATUS)0x1024)


/* MessageId: HV_STATUS_NO_REMAINING_COMMITTED_PAGES */

/* MessageText: */

/* No additional committed pages beyond the specified page exist in the section. */

#define HV_STATUS_NO_REMAINING_COMMITTED_PAGES ((HV_STATUS)0x1025)


/* MessageId: HV_STATUS_INSUFFICIENT_COMPARTMENT_VA */

/* MessageText: */

/* The VA space of the compartment is exhausted. */

#define HV_STATUS_INSUFFICIENT_COMPARTMENT_VA ((HV_STATUS)0x1026)


/* MessageId: HV_STATUS_DEREF_SPA_LIST_FULL */

/* MessageText: */

/* The SPA dereference list is full, and there are additional entries */
/* to be added to it. */

#define HV_STATUS_DEREF_SPA_LIST_FULL ((HV_STATUS)0x1027)


/* MessageId: HV_STATUS_GPA_OUT_OF_RANGE */

/* MessageText: */

/* The supplied GPA is out of range. */

#define HV_STATUS_GPA_OUT_OF_RANGE ((HV_STATUS)0x1027)


/* MessageId: HV_STATUS_NONVOLATILE_XMM_STALE */

/* MessageText: */

/* The XMM register that was being accessed is stale. */

#define HV_STATUS_NONVOLATILE_XMM_STALE ((HV_STATUS)0x1028)


/* MessageId: HV_STATUS_UNSUPPORTED_PROCESSOR */

/* MessageText: */

/* The hypervisor does not support the processors in this system. */

#define HV_STATUS_UNSUPPORTED_PROCESSOR ((HV_STATUS)0x1029)


/* MessageId: HV_STATUS_INSUFFICIENT_CROM_SPACE */

/* MessageText: */

/* Insufficient space existed for copying over the CROM contents. */

#define HV_STATUS_INSUFFICIENT_CROM_SPACE ((HV_STATUS)0x2000)


/* MessageId: HV_STATUS_BAD_CROM_FORMAT */

/* MessageText: */

/* The contents of the CROM failed validation attempts. */

#define HV_STATUS_BAD_CROM_FORMAT        ((HV_STATUS)0x2001)


/* MessageId: HV_STATUS_UNSUPPORTED_CROM_FORMAT */

/* MessageText: */

/* The contents of the CROM contain contents the parser doesn't support. */

#define HV_STATUS_UNSUPPORTED_CROM_FORMAT ((HV_STATUS)0x2002)


/* MessageId: HV_STATUS_UNSUPPORTED_CONTROLLER */

/* MessageText: */

/* The register format of the OHCI controller specified for debugging is not supported. */

#define HV_STATUS_UNSUPPORTED_CONTROLLER ((HV_STATUS)0x2003)


/* MessageId: HV_STATUS_CROM_TOO_LARGE */

/* MessageText: */

/* The CROM contents were to large to copy over. */

#define HV_STATUS_CROM_TOO_LARGE         ((HV_STATUS)0x2004)


/* MessageId: HV_STATUS_CONTROLLER_IN_USE */

/* MessageText: */

/* The OHCI controller specified for debugging cannot be used as it is already in use. */

#define HV_STATUS_CONTROLLER_IN_USE      ((HV_STATUS)0x2005)

