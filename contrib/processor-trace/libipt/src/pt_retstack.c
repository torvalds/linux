/*
 * Copyright (c) 2013-2018, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of Intel Corporation nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include "pt_retstack.h"

#include "intel-pt.h"


void pt_retstack_init(struct pt_retstack *retstack)
{
	if (!retstack)
		return;

	retstack->top = 0;
	retstack->bottom = 0;
}

int pt_retstack_is_empty(const struct pt_retstack *retstack)
{
	if (!retstack)
		return -pte_invalid;

	return (retstack->top == retstack->bottom ? 1 : 0);
}

int pt_retstack_pop(struct pt_retstack *retstack, uint64_t *ip)
{
	uint8_t top;

	if (!retstack)
		return -pte_invalid;

	top = retstack->top;

	if (top == retstack->bottom)
		return -pte_retstack_empty;

	top = (!top ? pt_retstack_size : top - 1);

	retstack->top = top;

	if (ip)
		*ip = retstack->stack[top];

	return 0;
}

int pt_retstack_push(struct pt_retstack *retstack, uint64_t ip)
{
	uint8_t top, bottom;

	if (!retstack)
		return -pte_invalid;

	top = retstack->top;
	bottom = retstack->bottom;

	retstack->stack[top] = ip;

	top = (top == pt_retstack_size ? 0 : top + 1);

	if (bottom == top)
		bottom = (bottom == pt_retstack_size ? 0 : bottom + 1);

	retstack->top = top;
	retstack->bottom = bottom;

	return 0;
}
