/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef _ASM_POWERPC_PAPR_WATCHDOG_H
#define _ASM_POWERPC_PAPR_WATCHDOG_H

/*
 * H_WATCHDOG Input
 *
 * R4: "flags":
 *
 *         Bits 48-55: "operation"
 */
#define PSERIES_WDTF_OP_START	0x100UL		/* start timer */
#define PSERIES_WDTF_OP_STOP	0x200UL		/* stop timer */
#define PSERIES_WDTF_OP_QUERY	0x300UL		/* query timer capabilities */

/*
 *         Bits 56-63: "timeoutAction" (for "Start Watchdog" only)
 */
#define PSERIES_WDTF_ACTION_HARD_POWEROFF	0x1UL	/* poweroff */
#define PSERIES_WDTF_ACTION_HARD_RESTART	0x2UL	/* restart */
#define PSERIES_WDTF_ACTION_DUMP_RESTART	0x3UL	/* dump + restart */

/*
 * R5: "watchdogNumber":
 *       PAPR says use -1 (all ones) to stop all watchdogs.
 */
#define PSERIES_WDT_NUM_ALL	((unsigned long)-1)

/*
 * H_WATCHDOG Output
 *
 * R3: Return code
 *
 *     H_SUCCESS    The operation completed.
 *
 *     H_BUSY	    The hypervisor is too busy; retry the operation.
 *
 *     H_PARAMETER  The given "flags" are somehow invalid.  Either the
 *                  "operation" or "timeoutAction" is invalid, or a
 *                  reserved bit is set.
 *
 *     H_P2         The given "watchdogNumber" is zero or exceeds the
 *                  supported maximum value.
 *
 *     H_P3         The given "timeoutInMs" is below the supported
 *                  minimum value.
 *
 *     H_NOOP       The given "watchdogNumber" is already stopped.
 *
 *     H_HARDWARE   The operation failed for ineffable reasons.
 *
 *     H_FUNCTION   The H_WATCHDOG hypercall is not supported by this
 *                  hypervisor.
 *
 * R4:
 *
 * - For the "Query Watchdog Capabilities" operation, a 64-bit
 *   structure:
 */
#define PSERIES_WDTQ_MIN_TIMEOUT(cap)	(((cap) >> 48) & 0xffff)
#define PSERIES_WDTQ_MAX_NUMBER(cap)	(((cap) >> 32) & 0xffff)

#endif /* _ASM_POWERPC_PAPR_WATCHDOG_H */
