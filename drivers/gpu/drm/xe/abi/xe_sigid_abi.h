/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2026 Intel Corporation
 */

#ifndef _ABI_XE_SIGID_ABI_H_
#define _ABI_XE_SIGID_ABI_H_

/**
 * DOC: Xe Error Signatures (SIGID)
 *
 * What SIGID stands for
 * ---------------------
 *
 * SIGID is short for *Signature Identifier*. It is a small, stable integer
 * that names one of the *recognised fault sites* -- nothing more. It is the
 * primary handle used for triage and maps directly to a specific report site.
 *
 * Numbering
 * ---------
 *
 * SIGIDs are a single flat list numbered sequentially within the assigned range,
 * in the order the fault sites were introduced. Values are stable: once assigned
 * they are only ever appended, never renumbered or reused. A retired fault site
 * SIGID value is deprecated in place, never re-purposed.
 *
 * Why this exists
 * ---------------
 *
 * Today the driver reports faults with ad-hoc ``xe_err()`` / ``xe_gt_err()``
 * strings that have no stable shape. That is fine for a human reading dmesg,
 * but it gives fleet tooling nothing durable to match on: the wording changes
 * between releases, lines can be rate-limited or dropped under an error storm,
 * and there is no consistent way to ask "which recognised fault just happened?"
 *
 * A SIGID answers exactly that one question, identically across driver and
 * firmware versions, and (eventually) across other Intel devices in a node.
 *
 * What a SIGID is not
 * -------------------
 *
 * SIGID deliberately does not encode the detailed reason or the outcome. Those
 * are carried alongside it::
 *
 *   SIGID    -> which recognised fault site is being reported
 *   severity -> how serious this instance is
 *   errno    -> the failing operation's error, if available, shown with %pe
 *   message  -> free-form human-readable context
 *
 * Severity is independent of the SIGID. The same SIGID can be reported at
 * different severities depending on the instance and the recovery taken.
 *
 * When to use SIGID logging
 * -------------------------
 *
 * The xe_log_*() helpers are for these recognised fault sites only --
 * important, operator-relevant faults and events. The driver's only job is to
 * emit the right SIGID next to the usual human-readable text.

 * They are not a replacement for ``xe_info()`` / ``xe_dbg()`` / tracing, nor
 * for one-off diagnostics; using them for ordinary logging would dilute the
 * fault stream. Not every ``xe_err()`` needs to become a SIGID report -- only
 * those that correspond to a published fault site.
 *
 * SIGID log output (dmesg vs. the machine record)
 * -----------------------------------------------
 *
 * The dmesg line stays close to a normal xe error message so it remains
 * readable for admins; the only stable, machine-matchable token on it is
 * ``SIGID=<n>`` (``dmesg | grep SIGID=``).
 *
 * The full dmesg line is not an ABI: the surrounding text may change freely,
 * and lines may be dropped. The durable record for tooling is the CPER record
 * carrying the same SIGID (generation is a planned follow-up).
 *
 * How to pick a SIGID (the uniqueness rule)
 * -----------------------------------------
 *
 * Pick per *report site*, not per incident. Each site emits the single most
 * specific recognised SIGID *for that site* -- so the question is never
 * "classify this whole failure", it is "what does this site detect?", which has
 * one answer. A single underlying failure therefore legitimately produces a
 * *chain* of reports from different layers, each with its own SIGID -- e.g. a
 * GuC communication failure is reported as %XE_SIGID_RUNTIME_FW by the firmware
 * path, the failed recovery as %XE_SIGID_GT_TDR by the reset path, and an
 * aborted bind as %XE_SIGID_PROBE by the probe path. That chain lets triage
 * follow a fault from origin to final effect; it is not a duplicate.
 *
 * If a site does not match any defined SIGID, keep using the ordinary
 * ``xe_err()`` / ``xe_gt_err()`` logging rather than forcing a SIGID: a wrong
 * or over-broad classification is harder to retire than a missing one. When a
 * new report site is genuinely worth triaging, add it to the list below.
 *
 * Usage of the existing SIGID reports must reevaluated according to this section
 * after making significant changes to the site that emits this SIGID.
 *
 * Scope: software vs hardware emitted signatures
 * ----------------------------------------------
 *
 * Some SIGID represents fault sites that the *driver itself* detects and
 * reports from the software POV: probe abort, wedged, survivability, driver-
 * detected firmware failures, engine TDR, memory faults and IO/bus faults.
 * These are the only values the driver assigns on its own.
 *
 * Signatures that *originate* in firmware or hardware are a different thing:
 * they are produced and identified by the firmware or the hardware itself
 * (e.g. via their own records or error counters), and the driver merely logs
 * them as they are given to us. They are deliberately enumerated separately.
 *
 * The two driver-detected firmware report sites below (%XE_SIGID_RUNTIME_FW,
 * %XE_SIGID_DEVICE_FW) are software signatures: they mark that *the driver*
 * observed a firmware problem, not a signature reported by the firmware.
 */

/*
 * Top level Intel Error Signature Identifiers.
 */
#define INTEL_SIGID_INVALID			0
#define INTEL_SIGID_BATCH			100
#define INTEL_SIGID_RANGE_START(n)		((n) * INTEL_SIGID_BATCH)
#define INTEL_SIGID_RANGE_END(n)		(INTEL_SIGID_RANGE_START((n) + 1) - 1)

/* SIGIDs 1xx are reserved for Xe GPU software and 2xx for Xe GPU hardware */
#define INTEL_SIGID_GPU_XE_SOFTWARE_START	INTEL_SIGID_RANGE_START(1)
#define INTEL_SIGID_GPU_XE_SOFTWARE_END		INTEL_SIGID_RANGE_END(1)
#define INTEL_SIGID_GPU_XE_HARDWARE_START	INTEL_SIGID_RANGE_START(2)
#define INTEL_SIGID_GPU_XE_HARDWARE_END		INTEL_SIGID_RANGE_END(2)

/**
 * enum xe_sigid - Stable Xe Error Signature Identifiers (SIGID).
 * @XE_SIGID_SW: Software component failure.
 * @XE_SIGID_PROBE: Device probe/bind was aborted.
 * @XE_SIGID_WEDGED: Device was declared wedged and is no longer usable.
 * @XE_SIGID_SURVIVABILITY: Device entered survivability mode.
 * @XE_SIGID_RUNTIME_FW: Driver-detected runtime firmware failure, GuC/HuC/GSC.
 * @XE_SIGID_DEVICE_FW: Driver-detected device firmware failure, PCODE/sysctrl.
 * @XE_SIGID_GT_TDR: Engine hang / timeout detection and recovery (reset).
 * @XE_SIGID_MEM_FAULT: VM bind, page fault or GTT fault.
 * @XE_SIGID_IO_BUS: Runtime PCIe / IOMMU / MMIO access fault.
 * @XE_SIGID_HW: Generic hardware failure.
 * @XE_SIGID_PCIE: PCIe interface errors.
 * @XE_SIGID_DEVICE_MEMORY: Device memory errors.
 * @XE_SIGID_CORE_COMPUTE: Compute/shader core errors.
 * @XE_SIGID_FABRIC: Fabric errors.
 * @XE_SIGID_SOC_INTERNAL: SoC-internal errors.
 *
 * Each SIGID represents the report sites the driver detects and reports.
 * Values are numbered sequentially, are only ever appended, and are never
 * renumbered or reused.
 *
 * Firmware- and hardware-originated signatures are numbered separately.
 */
enum xe_sigid {
	XE_SIGID_SW			= INTEL_SIGID_GPU_XE_SOFTWARE_START,
	XE_SIGID_PROBE			= INTEL_SIGID_GPU_XE_SOFTWARE_START + 1,
	XE_SIGID_WEDGED			= INTEL_SIGID_GPU_XE_SOFTWARE_START + 2,
	XE_SIGID_SURVIVABILITY		= INTEL_SIGID_GPU_XE_SOFTWARE_START + 3,
	XE_SIGID_RUNTIME_FW		= INTEL_SIGID_GPU_XE_SOFTWARE_START + 4,
	XE_SIGID_DEVICE_FW		= INTEL_SIGID_GPU_XE_SOFTWARE_START + 5,
	XE_SIGID_GT_TDR			= INTEL_SIGID_GPU_XE_SOFTWARE_START + 6,
	XE_SIGID_MEM_FAULT		= INTEL_SIGID_GPU_XE_SOFTWARE_START + 7,
	XE_SIGID_IO_BUS			= INTEL_SIGID_GPU_XE_SOFTWARE_START + 8,

	XE_SIGID_HW			= INTEL_SIGID_GPU_XE_HARDWARE_START,
	XE_SIGID_PCIE			= INTEL_SIGID_GPU_XE_HARDWARE_START + 1,
	XE_SIGID_DEVICE_MEMORY		= INTEL_SIGID_GPU_XE_HARDWARE_START + 2,
	XE_SIGID_CORE_COMPUTE		= INTEL_SIGID_GPU_XE_HARDWARE_START + 3,
	XE_SIGID_FABRIC			= INTEL_SIGID_GPU_XE_HARDWARE_START + 4,
	XE_SIGID_SOC_INTERNAL		= INTEL_SIGID_GPU_XE_HARDWARE_START + 5,
};

#endif
