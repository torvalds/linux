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

#ifndef PT_RETSTACK_H
#define PT_RETSTACK_H

#include <stdint.h>


/* The size of the call/return stack in number of entries. */
enum {
	pt_retstack_size	= 64
};

/* A stack of return addresses used for return compression. */
struct pt_retstack {
	/* The stack of return addresses.
	 *
	 * We use one additional entry in order to distinguish a full from
	 * an empty stack.
	 */
	uint64_t stack[pt_retstack_size + 1];

	/* The top of the stack. */
	uint8_t top;

	/* The bottom of the stack. */
	uint8_t bottom;
};

/* Initialize (or reset) a call/return stack. */
extern void pt_retstack_init(struct pt_retstack *);

/* Test a call/return stack for emptiness.
 *
 * Returns zero if @retstack contains at least one element.
 * Returns a positive integer if @retstack is empty.
 * Returns -pte_invalid if @retstack is NULL.
 */
extern int pt_retstack_is_empty(const struct pt_retstack *retstack);

/* Pop and return the topmost IP.
 *
 * If @ip is not NULL, provides the topmost return address on success.
 * If @retstack is not empty, pops the topmost return address on success.
 *
 * Returns zero on success.
 * Returns -pte_invalid if @retstack is NULL.
 * Returns -pte_noip if @retstack is empty.
 */
extern int pt_retstack_pop(struct pt_retstack *retstack, uint64_t *ip);

/* Push a return address onto the stack.
 *
 * Pushes @ip onto @retstack.
 * If @retstack is full, drops the oldest return address.
 *
 * Returns zero on success.
 */
extern int pt_retstack_push(struct pt_retstack *retstack, uint64_t ip);

#endif /* PT_RETSTACK_H */
