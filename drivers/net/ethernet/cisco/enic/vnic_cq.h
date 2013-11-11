/*
 * Copyright 2008-2010 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef _VNIC_CQ_H_
#define _VNIC_CQ_H_

#include "cq_desc.h"
#include "vnic_dev.h"

/* Completion queue control */
struct vnic_cq_ctrl {
	u64 ring_base;			/* 0x00 */
	u32 ring_size;			/* 0x08 */
	u32 pad0;
	u32 flow_control_enable;	/* 0x10 */
	u32 pad1;
	u32 color_enable;		/* 0x18 */
	u32 pad2;
	u32 cq_head;			/* 0x20 */
	u32 pad3;
	u32 cq_tail;			/* 0x28 */
	u32 pad4;
	u32 cq_tail_color;		/* 0x30 */
	u32 pad5;
	u32 interrupt_enable;		/* 0x38 */
	u32 pad6;
	u32 cq_entry_enable;		/* 0x40 */
	u32 pad7;
	u32 cq_message_enable;		/* 0x48 */
	u32 pad8;
	u32 interrupt_offset;		/* 0x50 */
	u32 pad9;
	u64 cq_message_addr;		/* 0x58 */
	u32 pad10;
};

struct vnic_cq {
	unsigned int index;
	struct vnic_dev *vdev;
	struct vnic_cq_ctrl __iomem *ctrl;              /* memory-mapped */
	struct vnic_dev_ring ring;
	unsigned int to_clean;
	unsigned int last_color;
	unsigned int interrupt_offset;
};

static inline unsigned int vnic_cq_service(struct vnic_cq *cq,
	unsigned int work_to_do,
	int (*q_service)(struct vnic_dev *vdev, struct cq_desc *cq_desc,
	u8 type, u16 q_number, u16 completed_index, void *opaque),
	void *opaque)
{
	struct cq_desc *cq_desc;
	unsigned int work_done = 0;
	u16 q_number, completed_index;
	u8 type, color;

	cq_desc = (struct cq_desc *)((u8 *)cq->ring.descs +
		cq->ring.desc_size * cq->to_clean);
	cq_desc_dec(cq_desc, &type, &color,
		&q_number, &completed_index);

	while (color != cq->last_color) {

		if ((*q_service)(cq->vdev, cq_desc, type,
			q_number, completed_index, opaque))
			break;

		cq->to_clean++;
		if (cq->to_clean == cq->ring.desc_count) {
			cq->to_clean = 0;
			cq->last_color = cq->last_color ? 0 : 1;
		}

		cq_desc = (struct cq_desc *)((u8 *)cq->ring.descs +
			cq->ring.desc_size * cq->to_clean);
		cq_desc_dec(cq_desc, &type, &color,
			&q_number, &completed_index);

		work_done++;
		if (work_done >= work_to_do)
			break;
	}

	return work_done;
}

void vnic_cq_free(struct vnic_cq *cq);
int vnic_cq_alloc(struct vnic_dev *vdev, struct vnic_cq *cq, unsigned int index,
	unsigned int desc_count, unsigned int desc_size);
void vnic_cq_init(struct vnic_cq *cq, unsigned int flow_control_enable,
	unsigned int color_enable, unsigned int cq_head, unsigned int cq_tail,
	unsigned int cq_tail_color, unsigned int interrupt_enable,
	unsigned int cq_entry_enable, unsigned int message_enable,
	unsigned int interrupt_offset, u64 message_addr);
void vnic_cq_clean(struct vnic_cq *cq);

#endif /* _VNIC_CQ_H_ */
