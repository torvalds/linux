/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "host.h"
#include "unsolicited_frame_control.h"
#include "registers.h"

int sci_unsolicited_frame_control_construct(struct isci_host *ihost)
{
	struct sci_unsolicited_frame_control *uf_control = &ihost->uf_control;
	struct sci_unsolicited_frame *uf;
	u32 buf_len, header_len, i;
	dma_addr_t dma;
	size_t size;
	void *virt;

	/*
	 * Prepare all of the memory sizes for the UF headers, UF address
	 * table, and UF buffers themselves.
	 */
	buf_len = SCU_MAX_UNSOLICITED_FRAMES * SCU_UNSOLICITED_FRAME_BUFFER_SIZE;
	header_len = SCU_MAX_UNSOLICITED_FRAMES * sizeof(struct scu_unsolicited_frame_header);
	size = buf_len + header_len + SCU_MAX_UNSOLICITED_FRAMES * sizeof(dma_addr_t);

	/*
	 * The Unsolicited Frame buffers are set at the start of the UF
	 * memory descriptor entry. The headers and address table will be
	 * placed after the buffers.
	 */
	virt = dmam_alloc_coherent(&ihost->pdev->dev, size, &dma, GFP_KERNEL);
	if (!virt)
		return -ENOMEM;

	/*
	 * Program the location of the UF header table into the SCU.
	 * Notes:
	 * - The address must align on a 64-byte boundary. Guaranteed to be
	 *   on 64-byte boundary already 1KB boundary for unsolicited frames.
	 * - Program unused header entries to overlap with the last
	 *   unsolicited frame.  The silicon will never DMA to these unused
	 *   headers, since we program the UF address table pointers to
	 *   NULL.
	 */
	uf_control->headers.physical_address = dma + buf_len;
	uf_control->headers.array = virt + buf_len;

	/*
	 * Program the location of the UF address table into the SCU.
	 * Notes:
	 * - The address must align on a 64-bit boundary. Guaranteed to be on 64
	 *   byte boundary already due to above programming headers being on a
	 *   64-bit boundary and headers are on a 64-bytes in size.
	 */
	uf_control->address_table.physical_address = dma + buf_len + header_len;
	uf_control->address_table.array = virt + buf_len + header_len;
	uf_control->get = 0;

	/*
	 * UF buffer requirements are:
	 * - The last entry in the UF queue is not NULL.
	 * - There is a power of 2 number of entries (NULL or not-NULL)
	 *   programmed into the queue.
	 * - Aligned on a 1KB boundary. */

	/*
	 * Program the actual used UF buffers into the UF address table and
	 * the controller's array of UFs.
	 */
	for (i = 0; i < SCU_MAX_UNSOLICITED_FRAMES; i++) {
		uf = &uf_control->buffers.array[i];

		uf_control->address_table.array[i] = dma;

		uf->buffer = virt;
		uf->header = &uf_control->headers.array[i];
		uf->state  = UNSOLICITED_FRAME_EMPTY;

		/*
		 * Increment the address of the physical and virtual memory
		 * pointers. Everything is aligned on 1k boundary with an
		 * increment of 1k.
		 */
		virt += SCU_UNSOLICITED_FRAME_BUFFER_SIZE;
		dma += SCU_UNSOLICITED_FRAME_BUFFER_SIZE;
	}

	return 0;
}

enum sci_status sci_unsolicited_frame_control_get_header(struct sci_unsolicited_frame_control *uf_control,
							 u32 frame_index,
							 void **frame_header)
{
	if (frame_index < SCU_MAX_UNSOLICITED_FRAMES) {
		/* Skip the first word in the frame since this is a controll word used
		 * by the hardware.
		 */
		*frame_header = &uf_control->buffers.array[frame_index].header->data;

		return SCI_SUCCESS;
	}

	return SCI_FAILURE_INVALID_PARAMETER_VALUE;
}

enum sci_status sci_unsolicited_frame_control_get_buffer(struct sci_unsolicited_frame_control *uf_control,
							 u32 frame_index,
							 void **frame_buffer)
{
	if (frame_index < SCU_MAX_UNSOLICITED_FRAMES) {
		*frame_buffer = uf_control->buffers.array[frame_index].buffer;

		return SCI_SUCCESS;
	}

	return SCI_FAILURE_INVALID_PARAMETER_VALUE;
}

bool sci_unsolicited_frame_control_release_frame(struct sci_unsolicited_frame_control *uf_control,
						 u32 frame_index)
{
	u32 frame_get;
	u32 frame_cycle;

	frame_get   = uf_control->get & (SCU_MAX_UNSOLICITED_FRAMES - 1);
	frame_cycle = uf_control->get & SCU_MAX_UNSOLICITED_FRAMES;

	/*
	 * In the event there are NULL entries in the UF table, we need to
	 * advance the get pointer in order to find out if this frame should
	 * be released (i.e. update the get pointer)
	 */
	while (lower_32_bits(uf_control->address_table.array[frame_get]) == 0 &&
	       upper_32_bits(uf_control->address_table.array[frame_get]) == 0 &&
	       frame_get < SCU_MAX_UNSOLICITED_FRAMES)
		frame_get++;

	/*
	 * The table has a NULL entry as it's last element.  This is
	 * illegal.
	 */
	BUG_ON(frame_get >= SCU_MAX_UNSOLICITED_FRAMES);
	if (frame_index >= SCU_MAX_UNSOLICITED_FRAMES)
		return false;

	uf_control->buffers.array[frame_index].state = UNSOLICITED_FRAME_RELEASED;

	if (frame_get != frame_index) {
		/*
		 * Frames remain in use until we advance the get pointer
		 * so there is nothing we can do here
		 */
		return false;
	}

	/*
	 * The frame index is equal to the current get pointer so we
	 * can now free up all of the frame entries that
	 */
	while (uf_control->buffers.array[frame_get].state == UNSOLICITED_FRAME_RELEASED) {
		uf_control->buffers.array[frame_get].state = UNSOLICITED_FRAME_EMPTY;

		if (frame_get+1 == SCU_MAX_UNSOLICITED_FRAMES-1) {
			frame_cycle ^= SCU_MAX_UNSOLICITED_FRAMES;
			frame_get = 0;
		} else
			frame_get++;
	}

	uf_control->get = SCU_UFQGP_GEN_BIT(ENABLE_BIT) | frame_cycle | frame_get;

	return true;
}
