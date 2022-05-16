/* SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only) */
/* Copyright(c) 2014 - 2020 Intel Corporation */
#ifndef ADF_CFG_STRINGS_H_
#define ADF_CFG_STRINGS_H_

#define ADF_GENERAL_SEC "GENERAL"
#define ADF_KERNEL_SEC "KERNEL"
#define ADF_ACCEL_SEC "Accelerator"
#define ADF_NUM_CY "NumberCyInstances"
#define ADF_NUM_DC "NumberDcInstances"
#define ADF_RING_SYM_SIZE "NumConcurrentSymRequests"
#define ADF_RING_ASYM_SIZE "NumConcurrentAsymRequests"
#define ADF_RING_DC_SIZE "NumConcurrentRequests"
#define ADF_RING_ASYM_TX "RingAsymTx"
#define ADF_RING_SYM_TX "RingSymTx"
#define ADF_RING_ASYM_RX "RingAsymRx"
#define ADF_RING_SYM_RX "RingSymRx"
#define ADF_RING_DC_TX "RingTx"
#define ADF_RING_DC_RX "RingRx"
#define ADF_ETRMGR_BANK "Bank"
#define ADF_RING_BANK_NUM "BankNumber"
#define ADF_CY "Cy"
#define ADF_DC "Dc"
#define ADF_ETRMGR_COALESCING_ENABLED "InterruptCoalescingEnabled"
#define ADF_ETRMGR_COALESCING_ENABLED_FORMAT \
	ADF_ETRMGR_BANK "%d" ADF_ETRMGR_COALESCING_ENABLED
#define ADF_ETRMGR_COALESCE_TIMER "InterruptCoalescingTimerNs"
#define ADF_ETRMGR_COALESCE_TIMER_FORMAT \
	ADF_ETRMGR_BANK "%d" ADF_ETRMGR_COALESCE_TIMER
#define ADF_ETRMGR_COALESCING_MSG_ENABLED "InterruptCoalescingNumResponses"
#define ADF_ETRMGR_COALESCING_MSG_ENABLED_FORMAT \
	ADF_ETRMGR_BANK "%d" ADF_ETRMGR_COALESCING_MSG_ENABLED
#define ADF_ETRMGR_CORE_AFFINITY "CoreAffinity"
#define ADF_ETRMGR_CORE_AFFINITY_FORMAT \
	ADF_ETRMGR_BANK "%d" ADF_ETRMGR_CORE_AFFINITY
#define ADF_ACCEL_STR "Accelerator%d"
#endif
