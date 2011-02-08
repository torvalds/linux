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

#ifndef _SCI_BASE_MEMORY_DESCRIPTOR_LIST_H_
#define _SCI_BASE_MEMORY_DESCRIPTOR_LIST_H_

/**
 * This file contains the protected interface structures, constants and
 *    interface methods for the struct sci_base_memory_descriptor_list object.
 *
 *
 */


#include "sci_memory_descriptor_list.h"


/**
 * struct sci_base_memory_descriptor_list - This structure contains all of the
 *    fields necessary to implement a simple stack for managing the list of
 *    available controller indices.
 *
 *
 */
struct sci_base_memory_descriptor_list {
	/**
	 * This field indicates the length of the memory descriptor entry array.
	 */
	u32 length;

	/**
	 * This field is utilized to provide iterator pattern functionality.
	 * It indicates the index of the next memory descriptor in the iteration.
	 */
	u32 next_index;

	/**
	 * This field will point to the list of memory descriptors.
	 */
	struct sci_physical_memory_descriptor *mde_array;

	/**
	 * This field simply allows a user to chain memory descriptor lists
	 * together if desired.  This field will be initialized to NULL.
	 */
	struct sci_base_memory_descriptor_list *next_mdl;

};

/**
 * sci_base_mdl_construct() - This method is invoked to construct an memory
 *    descriptor list. It initializes the fields of the MDL.
 * @mdl: This parameter specifies the memory descriptor list to be constructed.
 * @mde_array: This parameter specifies the array of memory descriptor entries
 *    to be managed by this list.
 * @mde_array_length: This parameter specifies the size of the array of entries.
 * @next_mdl: This parameter specifies a subsequent MDL object to be managed by
 *    this MDL object.
 *
 * none.
 */
void sci_base_mdl_construct(
	struct sci_base_memory_descriptor_list *mdl,
	struct sci_physical_memory_descriptor *mde_array,
	u32 mde_array_length,
	struct sci_base_memory_descriptor_list *next_mdl);

/**
 * sci_base_mde_construct() -
 *
 * This macro constructs an memory descriptor entry with the given alignment
 * and size
 */
#define sci_base_mde_construct(mde, alignment, size, attributes) \
	{ \
		(mde)->constant_memory_alignment  = (alignment); \
		(mde)->constant_memory_size       = (size); \
		(mde)->constant_memory_attributes = (attributes); \
	}

/**
 * sci_base_mde_is_valid() - This method validates that the memory descriptor
 *    is correctly filled out by the SCI User
 * @mde: This parameter is the mde entry to validate
 * @alignment: This parameter specifies the expected alignment of the memory
 *    for the mde.
 * @size: This parameter specifies the memory size expected for the mde its
 *    value should not have been changed by the SCI User.
 * @attributes: This parameter specifies the attributes for the memory
 *    descriptor provided.
 *
 * bool This method returns an indication as to whether the supplied MDE is
 * valid or not. true The MDE is valid. false The MDE is not valid.
 */
bool sci_base_mde_is_valid(
	struct sci_physical_memory_descriptor *mde,
	u32 alignment,
	u32 size,
	u16 attributes);

#endif /* _SCI_BASE_MEMORY_DESCRIPTOR_LIST_H_ */
