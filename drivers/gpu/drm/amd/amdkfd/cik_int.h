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
 */

#ifndef CIK_INT_H_INCLUDED
#define CIK_INT_H_INCLUDED

#include <linux/types.h>

struct cik_ih_ring_entry {
	uint32_t source_id;
	uint32_t data;
	uint32_t ring_id;
	uint32_t reserved;
};

#define CIK_INTSRC_CP_END_OF_PIPE	0xB5
#define CIK_INTSRC_CP_BAD_OPCODE	0xB7
#define CIK_INTSRC_SDMA_TRAP		0xE0
#define CIK_INTSRC_SQ_INTERRUPT_MSG	0xEF
#define CIK_INTSRC_GFX_PAGE_INV_FAULT	0x92
#define CIK_INTSRC_GFX_MEM_PROT_FAULT	0x93

#endif

