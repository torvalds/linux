/*
 * Copyright (c) 2015-2018, Intel Corporation
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

#include "intel-pt.h"


/* Read the configuration provided by a library user and zero-initialize
 * missing fields.
 *
 * We keep the user's size value if it is smaller than sizeof(*@config) to
 * allow decoders to detect missing configuration bits.
 *
 * Returns zero on success, a negative error code otherwise.
 * Returns -pte_internal if @config is NULL.
 * Returns -pte_invalid if @uconfig is NULL.
 * Returns -pte_bad_config if @config is too small to be useful.
 */
extern int pt_config_from_user(struct pt_config *config,
			       const struct pt_config *uconfig);

/* Get the configuration for the n'th address filter.
 *
 * Returns zero if @filter is NULL or @n is out of bounds.
 *
 * This corresponds to IA32_RTIT_CTL.ADDRn_CFG.
 */
extern uint32_t pt_filter_addr_cfg(const struct pt_conf_addr_filter *filter,
				   uint8_t n);

/* Get the lower bound (inclusive) of the n'th address filter.
 *
 * Returns zero if @filter is NULL or @n is out of bounds.
 *
 * This corresponds to IA32_RTIT_ADDRn_A.
 */
extern uint64_t pt_filter_addr_a(const struct pt_conf_addr_filter *filter,
				 uint8_t n);

/* Get the upper bound (inclusive) of the n'th address filter.
 *
 * Returns zero if @filter is NULL or @n is out of bounds.
 *
 * This corresponds to IA32_RTIT_ADDRn_B.
 */
extern uint64_t pt_filter_addr_b(const struct pt_conf_addr_filter *filter,
				 uint8_t n);

/* Check address filters.
 *
 * Checks @addr against @filter.
 *
 * Returns a positive number if @addr lies in a tracing-enabled region.
 * Returns zero if @addr lies in a tracing-disabled region.
 * Returns a negative pt_error_code otherwise.
 */
extern int pt_filter_addr_check(const struct pt_conf_addr_filter *filter,
				uint64_t addr);
