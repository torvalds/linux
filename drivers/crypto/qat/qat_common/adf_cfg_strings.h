/*
  This file is provided under a dual BSD/GPLv2 license.  When using or
  redistributing this file, you may do so under either license.

  GPL LICENSE SUMMARY
  Copyright(c) 2014 Intel Corporation.
  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  Contact Information:
  qat-linux@intel.com

  BSD LICENSE
  Copyright(c) 2014 Intel Corporation.
  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
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
#define ADF_RING_RND_TX "RingNrbgTx"
#define ADF_RING_ASYM_RX "RingAsymRx"
#define ADF_RING_SYM_RX "RinSymRx"
#define ADF_RING_RND_RX "RingNrbgRx"
#define ADF_RING_DC_TX "RingTx"
#define ADF_RING_DC_RX "RingRx"
#define ADF_ETRMGR_BANK "Bank"
#define ADF_RING_BANK_NUM "BankNumber"
#define ADF_CY "Cy"
#define ADF_DC "Dc"
#define ADF_ETRMGR_COALESCING_ENABLED "InterruptCoalescingEnabled"
#define ADF_ETRMGR_COALESCING_ENABLED_FORMAT \
	ADF_ETRMGR_BANK"%d"ADF_ETRMGR_COALESCING_ENABLED
#define ADF_ETRMGR_COALESCE_TIMER "InterruptCoalescingTimerNs"
#define ADF_ETRMGR_COALESCE_TIMER_FORMAT \
	ADF_ETRMGR_BANK"%d"ADF_ETRMGR_COALESCE_TIMER
#define ADF_ETRMGR_COALESCING_MSG_ENABLED "InterruptCoalescingNumResponses"
#define ADF_ETRMGR_COALESCING_MSG_ENABLED_FORMAT \
	ADF_ETRMGR_BANK"%d"ADF_ETRMGR_COALESCING_MSG_ENABLED
#define ADF_ETRMGR_CORE_AFFINITY "CoreAffinity"
#define ADF_ETRMGR_CORE_AFFINITY_FORMAT \
	ADF_ETRMGR_BANK"%d"ADF_ETRMGR_CORE_AFFINITY
#define ADF_ACCEL_STR "Accelerator%d"
#endif
