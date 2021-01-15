/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#ifndef __AMDGPU_IRQ_H__
#define __AMDGPU_IRQ_H__

#include <linux/irqdomain.h>
#include "soc15_ih_clientid.h"
#include "amdgpu_ih.h"

#define AMDGPU_MAX_IRQ_SRC_ID		0x100
#define AMDGPU_MAX_IRQ_CLIENT_ID	0x100

#define AMDGPU_IRQ_CLIENTID_LEGACY	0
#define AMDGPU_IRQ_CLIENTID_MAX		SOC15_IH_CLIENTID_MAX

#define AMDGPU_IRQ_SRC_DATA_MAX_SIZE_DW	4

struct amdgpu_device;

enum amdgpu_interrupt_state {
	AMDGPU_IRQ_STATE_DISABLE,
	AMDGPU_IRQ_STATE_ENABLE,
};

struct amdgpu_iv_entry {
	struct amdgpu_ih_ring *ih;
	unsigned client_id;
	unsigned src_id;
	unsigned ring_id;
	unsigned vmid;
	unsigned vmid_src;
	uint64_t timestamp;
	unsigned timestamp_src;
	unsigned pasid;
	unsigned pasid_src;
	unsigned src_data[AMDGPU_IRQ_SRC_DATA_MAX_SIZE_DW];
	const uint32_t *iv_entry;
};

struct amdgpu_irq_src {
	unsigned				num_types;
	atomic_t				*enabled_types;
	const struct amdgpu_irq_src_funcs	*funcs;
	void *data;
};

struct amdgpu_irq_client {
	struct amdgpu_irq_src **sources;
};

/* provided by interrupt generating IP blocks */
struct amdgpu_irq_src_funcs {
	int (*set)(struct amdgpu_device *adev, struct amdgpu_irq_src *source,
		   unsigned type, enum amdgpu_interrupt_state state);

	int (*process)(struct amdgpu_device *adev,
		       struct amdgpu_irq_src *source,
		       struct amdgpu_iv_entry *entry);
};

struct amdgpu_irq {
	bool				installed;
	spinlock_t			lock;
	/* interrupt sources */
	struct amdgpu_irq_client	client[AMDGPU_IRQ_CLIENTID_MAX];

	/* status, etc. */
	bool				msi_enabled; /* msi enabled */

	/* interrupt rings */
	struct amdgpu_ih_ring		ih, ih1, ih2, ih_soft;
	const struct amdgpu_ih_funcs    *ih_funcs;
	struct work_struct		ih1_work, ih2_work, ih_soft_work;
	struct amdgpu_irq_src		self_irq;

	/* gen irq stuff */
	struct irq_domain		*domain; /* GPU irq controller domain */
	unsigned			virq[AMDGPU_MAX_IRQ_SRC_ID];
	uint32_t                        srbm_soft_reset;
};

void amdgpu_irq_disable_all(struct amdgpu_device *adev);
irqreturn_t amdgpu_irq_handler(int irq, void *arg);

int amdgpu_irq_init(struct amdgpu_device *adev);
void amdgpu_irq_fini(struct amdgpu_device *adev);
int amdgpu_irq_add_id(struct amdgpu_device *adev,
		      unsigned client_id, unsigned src_id,
		      struct amdgpu_irq_src *source);
void amdgpu_irq_dispatch(struct amdgpu_device *adev,
			 struct amdgpu_ih_ring *ih);
void amdgpu_irq_delegate(struct amdgpu_device *adev,
			 struct amdgpu_iv_entry *entry,
			 unsigned int num_dw);
int amdgpu_irq_update(struct amdgpu_device *adev, struct amdgpu_irq_src *src,
		      unsigned type);
int amdgpu_irq_get(struct amdgpu_device *adev, struct amdgpu_irq_src *src,
		   unsigned type);
int amdgpu_irq_put(struct amdgpu_device *adev, struct amdgpu_irq_src *src,
		   unsigned type);
bool amdgpu_irq_enabled(struct amdgpu_device *adev, struct amdgpu_irq_src *src,
			unsigned type);
void amdgpu_irq_gpu_reset_resume_helper(struct amdgpu_device *adev);

int amdgpu_irq_add_domain(struct amdgpu_device *adev);
void amdgpu_irq_remove_domain(struct amdgpu_device *adev);
unsigned amdgpu_irq_create_mapping(struct amdgpu_device *adev, unsigned src_id);

#endif
