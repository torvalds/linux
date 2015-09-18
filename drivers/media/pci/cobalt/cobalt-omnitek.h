/*
 *  Omnitek Scatter-Gather DMA Controller
 *
 *  Copyright 2012-2015 Cisco Systems, Inc. and/or its affiliates.
 *  All rights reserved.
 *
 *  This program is free software; you may redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 *  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *  MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 *  NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 *  BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 *  ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 *  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 */

#ifndef COBALT_OMNITEK_H
#define COBALT_OMNITEK_H

#include <linux/scatterlist.h>
#include "cobalt-driver.h"

struct sg_dma_descriptor {
	u32 pci_l;
	u32 pci_h;

	u32 local;
	u32 reserved0;

	u32 next_l;
	u32 next_h;

	u32 bytes;
	u32 reserved1;
};

int omni_sg_dma_init(struct cobalt *cobalt);
void omni_sg_dma_abort_channel(struct cobalt_stream *s);
void omni_sg_dma_start(struct cobalt_stream *s, struct sg_dma_desc_info *desc);
bool is_dma_done(struct cobalt_stream *s);

int descriptor_list_create(struct cobalt *cobalt,
	struct scatterlist *scatter_list, bool to_pci, unsigned sglen,
	unsigned size, unsigned width, unsigned stride,
	struct sg_dma_desc_info *desc);

void descriptor_list_chain(struct sg_dma_desc_info *this,
			   struct sg_dma_desc_info *next);
void descriptor_list_loopback(struct sg_dma_desc_info *desc);
void descriptor_list_end_of_chain(struct sg_dma_desc_info *desc);

void *descriptor_list_allocate(struct sg_dma_desc_info *desc, size_t bytes);
void descriptor_list_free(struct sg_dma_desc_info *desc);

void descriptor_list_interrupt_enable(struct sg_dma_desc_info *desc);
void descriptor_list_interrupt_disable(struct sg_dma_desc_info *desc);

#endif
