/*
 * Copyright (c) 2014-2018, Intel Corporation
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

#ifndef PT_SYNC_H
#define PT_SYNC_H

#include <stdint.h>

struct pt_config;


/* Synchronize onto the trace stream.
 *
 * Search for the next synchronization point in forward or backward direction
 * starting at @pos using the trace configuration @config.
 *
 * On success, stores a pointer to the next synchronization point in @sync.
 *
 * Returns zero on success, a negative error code otherwise.
 *
 * Returns -pte_internal if @sync, @pos, or @config is NULL.
 * Returns -pte_nosync if @pos lies outside of @config's buffer.
 * Returns -pte_eos if no further synchronization point is found.
 */
extern int pt_sync_forward(const uint8_t **sync, const uint8_t *pos,
			   const struct pt_config *config);
extern int pt_sync_backward(const uint8_t **sync, const uint8_t *pos,
			    const struct pt_config *config);

/* Manually synchronize onto the trace stream.
 *
 * Validate that @pos is within the bounds of @config's trace buffer and that
 * there is a synchronization point at @pos.
 *
 * On success, stores @pos in @sync.
 *
 * Returns zero on success, a negative error code otherwise.
 *
 * Returns -pte_eos if @pos is outside of @config's trace buffer.
 * Returns -pte_internal if @sync, @pos, or @config is NULL.
 * Returns -pte_bad_packet if there is no PSB at @pos.
 */
extern int pt_sync_set(const uint8_t **sync, const uint8_t *pos,
		       const struct pt_config *config);

#endif /* PT_SYNC_H */
