/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2014 Intel Corporation.
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
 * BSD LICENSE
 *
 * Copyright(c) 2014 Intel Corporation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of Intel Corporation nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
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
 *
 * Intel SCIF driver.
 */
#ifndef SCIF_RB_H
#define SCIF_RB_H
/*
 * This file describes a general purpose, byte based ring buffer. Writers to the
 * ring buffer need to synchronize using a lock. The same is true for readers,
 * although in practice, the ring buffer has a single reader. It is lockless
 * between producer and consumer so it can handle being used across the PCIe
 * bus. The ring buffer ensures that there are no reads across the PCIe bus for
 * performance reasons. Two of these are used to form a single bidirectional
 * queue-pair across PCIe.
 */
/*
 * struct scif_rb - SCIF Ring Buffer
 *
 * @rb_base: The base of the memory used for storing RB messages
 * @read_ptr: Pointer to the read offset
 * @write_ptr: Pointer to the write offset
 * @size: Size of the memory in rb_base
 * @current_read_offset: Cached read offset for performance
 * @current_write_offset: Cached write offset for performance
 */
struct scif_rb {
	void *rb_base;
	u32 *read_ptr;
	u32 *write_ptr;
	u32 size;
	u32 current_read_offset;
	u32 current_write_offset;
};

/* methods used by both */
void scif_rb_init(struct scif_rb *rb, u32 *read_ptr, u32 *write_ptr,
		  void *rb_base, u8 size);
/* writer only methods */
/* write a new command, then scif_rb_commit() */
int scif_rb_write(struct scif_rb *rb, void *msg, u32 size);
/* after write(), then scif_rb_commit() */
void scif_rb_commit(struct scif_rb *rb);
/* query space available for writing to a RB. */
u32 scif_rb_space(struct scif_rb *rb);

/* reader only methods */
/* read a new message from the ring buffer of size bytes */
u32 scif_rb_get_next(struct scif_rb *rb, void *msg, u32 size);
/* update the read pointer so that the space can be reused */
void scif_rb_update_read_ptr(struct scif_rb *rb);
/* count the number of bytes that can be read */
u32 scif_rb_count(struct scif_rb *rb, u32 size);
#endif
