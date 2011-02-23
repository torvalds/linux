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

/**
 * This file contains the implementation of the
 *    struct scic_sds_unsolicited_frame_control object and it's public, protected, and
 *    private methods.
 *
 *
 */

#include "scic_sds_unsolicited_frame_control.h"
#include "scu_registers.h"
#include "scic_sds_controller.h"
#include "sci_util.h"
#include "sci_environment.h"

/**
 * The UF buffer address table size must be programmed to a power of 2.  Find
 *    the first power of 2 that is equal to or greater then the number of
 *    unsolicited frame buffers to be utilized.
 * @uf_control: This parameter specifies the UF control object for which to
 *    update the address table count.
 *
 */
void scic_sds_unsolicited_frame_control_set_address_table_count(
	struct scic_sds_unsolicited_frame_control *uf_control)
{
	uf_control->address_table.count = SCU_MIN_UF_TABLE_ENTRIES;
	while (
		(uf_control->address_table.count < uf_control->buffers.count)
		&& (uf_control->address_table.count < SCU_ABSOLUTE_MAX_UNSOLICITED_FRAMES)
		) {
		uf_control->address_table.count <<= 1;
	}
}

/**
 * This method will program the unsolicited frames (UFs) into the UF address
 *    table and construct the UF frame structure being modeled in the core.  It
 *    will handle the case where some of the UFs are not being used and thus
 *    should have entries programmed to zero in the address table.
 * @uf_control: This parameter specifies the unsolicted frame control object
 *    for which to construct the unsolicited frames objects.
 * @uf_buffer_phys_address: This parameter specifies the physical address for
 *    the first unsolicited frame buffer.
 * @uf_buffer_virt_address: This parameter specifies the virtual address for
 *    the first unsolicited frame buffer.
 * @unused_uf_header_entries: This parameter specifies the number of unused UF
 *    headers.  This value can be non-zero when there are a non-power of 2
 *    number of unsolicited frames being supported.
 * @used_uf_header_entries: This parameter specifies the number of actually
 *    utilized UF headers.
 *
 */
static void scic_sds_unsolicited_frame_control_construct_frames(
	struct scic_sds_unsolicited_frame_control *uf_control,
	dma_addr_t uf_buffer_phys_address,
	void *uf_buffer_virt_address,
	u32 unused_uf_header_entries,
	u32 used_uf_header_entries)
{
	u32 index;
	struct scic_sds_unsolicited_frame *uf;

	/*
	 * Program the unused buffers into the UF address table and the
	 * controller's array of UFs.
	 */
	for (index = 0; index < unused_uf_header_entries; index++) {
		uf = &uf_control->buffers.array[index];

		sci_cb_make_physical_address(
			uf_control->address_table.array[index], 0, 0
			);
		uf->buffer = NULL;
		uf->header = &uf_control->headers.array[index];
		uf->state  = UNSOLICITED_FRAME_EMPTY;
	}

	/*
	 * Program the actual used UF buffers into the UF address table and
	 * the controller's array of UFs.
	 */
	for (index = unused_uf_header_entries;
	     index < unused_uf_header_entries + used_uf_header_entries;
	     index++) {
		uf = &uf_control->buffers.array[index];

		uf_control->address_table.array[index] = uf_buffer_phys_address;

		uf->buffer = uf_buffer_virt_address;
		uf->header = &uf_control->headers.array[index];
		uf->state  = UNSOLICITED_FRAME_EMPTY;

		/*
		 * Increment the address of the physical and virtual memory
		 * pointers. Everything is aligned on 1k boundary with an
		 * increment of 1k.
		 */
		uf_buffer_virt_address += SCU_UNSOLICITED_FRAME_BUFFER_SIZE;
		uf_buffer_phys_address += SCU_UNSOLICITED_FRAME_BUFFER_SIZE;
	}
}

/**
 * This method constructs the various members of the unsolicted frame control
 *    object (buffers, headers, address, table, etc).
 * @uf_control: This parameter specifies the unsolicited frame control object
 *    to construct.
 * @mde: This parameter specifies the memory descriptor from which to derive
 *    all of the address information needed to get the unsolicited frame
 *    functionality working.
 * @controller: This parameter specifies the controller object associated with
 *    the uf_control being constructed.
 *
 */
void scic_sds_unsolicited_frame_control_construct(
	struct scic_sds_unsolicited_frame_control *uf_control,
	struct sci_physical_memory_descriptor *mde,
	struct scic_sds_controller *controller)
{
	u32 unused_uf_header_entries;
	u32 used_uf_header_entries;
	u32 used_uf_buffer_bytes;
	u32 unused_uf_header_bytes;
	u32 used_uf_header_bytes;
	dma_addr_t uf_buffer_phys_address;
	void *uf_buffer_virt_address;

	/*
	 * Prepare all of the memory sizes for the UF headers, UF address
	 * table, and UF buffers themselves. */
	used_uf_buffer_bytes     = uf_control->buffers.count
				   * SCU_UNSOLICITED_FRAME_BUFFER_SIZE;
	unused_uf_header_entries = uf_control->address_table.count
				   - uf_control->buffers.count;
	used_uf_header_entries   = uf_control->buffers.count;
	unused_uf_header_bytes   = unused_uf_header_entries
				   * sizeof(struct scu_unsolicited_frame_header);
	used_uf_header_bytes     = used_uf_header_entries
				   * sizeof(struct scu_unsolicited_frame_header);

	/*
	 * The Unsolicited Frame buffers are set at the start of the UF
	 * memory descriptor entry. The headers and address table will be
	 * placed after the buffers.
	 */
	uf_buffer_phys_address = mde->physical_address;
	uf_buffer_virt_address = mde->virtual_address;

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
	uf_control->headers.physical_address =
				uf_buffer_phys_address +
				used_uf_buffer_bytes -
				unused_uf_header_bytes;

	uf_control->headers.array =
				uf_buffer_virt_address +
				used_uf_buffer_bytes -
				unused_uf_header_bytes;

	/*
	 * Program the location of the UF address table into the SCU.
	 * Notes:
	 * - The address must align on a 64-bit boundary. Guaranteed to be on 64
	 *   byte boundary already due to above programming headers being on a
	 *   64-bit boundary and headers are on a 64-bytes in size.
	 */
	uf_control->address_table.physical_address =
				uf_buffer_phys_address +
				used_uf_buffer_bytes +
				used_uf_header_bytes;

	uf_control->address_table.array =
				uf_buffer_virt_address +
				used_uf_buffer_bytes +
				used_uf_header_bytes;

	uf_control->get = 0;

	/*
	 * UF buffer requirements are:
	 * - The last entry in the UF queue is not NULL.
	 * - There is a power of 2 number of entries (NULL or not-NULL)
	 *   programmed into the queue.
	 * - Aligned on a 1KB boundary. */

	/*
	 * If the user provided less then the maximum amount of memory,
	 * then be sure that we programm the first entries in the UF
	 * address table to NULL. */
	scic_sds_unsolicited_frame_control_construct_frames(
		uf_control,
		uf_buffer_phys_address,
		mde->virtual_address,
		unused_uf_header_entries,
		used_uf_header_entries
		);
}

/**
 * This method returns the frame header for the specified frame index.
 * @uf_control:
 * @frame_index:
 * @frame_header:
 *
 * enum sci_status
 */
enum sci_status scic_sds_unsolicited_frame_control_get_header(
	struct scic_sds_unsolicited_frame_control *uf_control,
	u32 frame_index,
	void **frame_header)
{
	if (frame_index < uf_control->address_table.count) {
		/*
		 * Skip the first word in the frame since this is a controll word used
		 * by the hardware. */
		*frame_header = &uf_control->buffers.array[frame_index].header->data;

		return SCI_SUCCESS;
	}

	return SCI_FAILURE_INVALID_PARAMETER_VALUE;
}

/**
 * This method returns the frame buffer for the specified frame index.
 * @uf_control:
 * @frame_index:
 * @frame_buffer:
 *
 * enum sci_status
 */
enum sci_status scic_sds_unsolicited_frame_control_get_buffer(
	struct scic_sds_unsolicited_frame_control *uf_control,
	u32 frame_index,
	void **frame_buffer)
{
	if (frame_index < uf_control->address_table.count) {
		*frame_buffer = uf_control->buffers.array[frame_index].buffer;

		return SCI_SUCCESS;
	}

	return SCI_FAILURE_INVALID_PARAMETER_VALUE;
}

/**
 * This method releases the frame once this is done the frame is available for
 *    re-use by the hardware.  The data contained in the frame header and frame
 *    buffer is no longer valid.
 * @uf_control: This parameter specifies the UF control object
 * @frame_index: This parameter specifies the frame index to attempt to release.
 *
 * This method returns an indication to the caller as to whether the
 * unsolicited frame get pointer should be updated. true This value indicates
 * the unsolicited frame get pointer should be updated (i.e. write
 * SCU_UFQGP_WRITE). false This value indicates the get pointer should not be
 * updated.
 */
bool scic_sds_unsolicited_frame_control_release_frame(
	struct scic_sds_unsolicited_frame_control *uf_control,
	u32 frame_index)
{
	u32 frame_get;
	u32 frame_cycle;

	frame_get   = uf_control->get & (uf_control->address_table.count - 1);
	frame_cycle = uf_control->get & uf_control->address_table.count;

	/*
	 * In the event there are NULL entries in the UF table, we need to
	 * advance the get pointer in order to find out if this frame should
	 * be released (i.e. update the get pointer). */
	while (((lower_32_bits(uf_control->address_table.array[frame_get])
					== 0) &&
		(upper_32_bits(uf_control->address_table.array[frame_get])
					== 0)) &&
	       (frame_get < uf_control->address_table.count))
		frame_get++;

	/*
	 * The table has a NULL entry as it's last element.  This is
	 * illegal. */
	BUG_ON(frame_get >= uf_control->address_table.count);

	if (frame_index < uf_control->address_table.count) {
		uf_control->buffers.array[frame_index].state = UNSOLICITED_FRAME_RELEASED;

		/*
		 * The frame index is equal to the current get pointer so we
		 * can now free up all of the frame entries that */
		if (frame_get == frame_index) {
			while (
				uf_control->buffers.array[frame_get].state
				== UNSOLICITED_FRAME_RELEASED
				) {
				uf_control->buffers.array[frame_get].state = UNSOLICITED_FRAME_EMPTY;

				INCREMENT_QUEUE_GET(
					frame_get,
					frame_cycle,
					uf_control->address_table.count - 1,
					uf_control->address_table.count
					);
			}

			uf_control->get =
				(SCU_UFQGP_GEN_BIT(ENABLE_BIT) | frame_cycle | frame_get);

			return true;
		} else {
			/*
			 * Frames remain in use until we advance the get pointer
			 * so there is nothing we can do here */
		}
	}

	return false;
}

