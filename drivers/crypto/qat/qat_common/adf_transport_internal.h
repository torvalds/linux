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
#ifndef ADF_TRANSPORT_INTRN_H
#define ADF_TRANSPORT_INTRN_H

#include <linux/interrupt.h>
#include <linux/atomic.h>
#include <linux/spinlock_types.h>
#include "adf_transport.h"

struct adf_etr_ring_debug_entry {
	char ring_name[ADF_CFG_MAX_KEY_LEN_IN_BYTES];
	struct dentry *debug;
};

struct adf_etr_ring_data {
	void *base_addr;
	atomic_t *inflights;
	spinlock_t lock;	/* protects ring data struct */
	adf_callback_fn callback;
	struct adf_etr_bank_data *bank;
	dma_addr_t dma_addr;
	uint16_t head;
	uint16_t tail;
	uint8_t ring_number;
	uint8_t ring_size;
	uint8_t msg_size;
	uint8_t reserved;
	struct adf_etr_ring_debug_entry *ring_debug;
} __packed;

struct adf_etr_bank_data {
	struct adf_etr_ring_data rings[ADF_ETR_MAX_RINGS_PER_BANK];
	struct tasklet_struct resp_hanlder;
	void __iomem *csr_addr;
	struct adf_accel_dev *accel_dev;
	uint32_t irq_coalesc_timer;
	uint16_t ring_mask;
	uint16_t irq_mask;
	spinlock_t lock;	/* protects bank data struct */
	struct dentry *bank_debug_dir;
	struct dentry *bank_debug_cfg;
	uint32_t bank_number;
} __packed;

struct adf_etr_data {
	struct adf_etr_bank_data *banks;
	struct dentry *debug;
};

void adf_response_handler(unsigned long bank_addr);
#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
int adf_bank_debugfs_add(struct adf_etr_bank_data *bank);
void adf_bank_debugfs_rm(struct adf_etr_bank_data *bank);
int adf_ring_debugfs_add(struct adf_etr_ring_data *ring, const char *name);
void adf_ring_debugfs_rm(struct adf_etr_ring_data *ring);
#else
static inline int adf_bank_debugfs_add(struct adf_etr_bank_data *bank)
{
	return 0;
}
#define adf_bank_debugfs_rm(bank) do {} while (0)
static inline int adf_ring_debugfs_add(struct adf_etr_ring_data *ring,
				       const char *name)
{
	return 0;
}
#define adf_ring_debugfs_rm(ring) do {} while (0)
#endif
#endif
