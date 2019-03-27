/*
 * Copyright (c) 2016-2018, Intel Corporation
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

#ifndef PT_BLOCK_DECODER_H
#define PT_BLOCK_DECODER_H

#include "pt_query_decoder.h"
#include "pt_image.h"
#include "pt_retstack.h"
#include "pt_ild.h"
#include "pt_msec_cache.h"


/* A block decoder.
 *
 * It decodes Intel(R) Processor Trace into a sequence of instruction blocks
 * such that the instructions in each block can be decoded without further need
 * of trace.
 */
struct pt_block_decoder {
	/* The Intel(R) Processor Trace query decoder. */
	struct pt_query_decoder query;

	/* The configuration flags.
	 *
	 * Those are our flags set by the user.  In @query.config.flags, we set
	 * the flags we need for the query decoder.
	 */
	struct pt_conf_flags flags;

	/* The default image. */
	struct pt_image default_image;

	/* The image. */
	struct pt_image *image;

	/* The current cached section. */
	struct pt_msec_cache scache;

	/* The current address space. */
	struct pt_asid asid;

	/* The current Intel(R) Processor Trace event. */
	struct pt_event event;

	/* The call/return stack for ret compression. */
	struct pt_retstack retstack;

	/* The current instruction.
	 *
	 * This is only valid if @process_insn is set.
	 */
	struct pt_insn insn;
	struct pt_insn_ext iext;

	/* The start IP of the next block.
	 *
	 * If tracing is disabled, this is the IP at which we assume tracing to
	 * be resumed.
	 */
	uint64_t ip;

	/* The current execution mode. */
	enum pt_exec_mode mode;

	/* The status of the last successful decoder query.
	 *
	 * Errors are reported directly; the status is always a non-negative
	 * pt_status_flag bit-vector.
	 */
	int status;

	/* A collection of flags defining how to proceed flow reconstruction:
	 *
	 * - tracing is enabled.
	 */
	uint32_t enabled:1;

	/* - process @event. */
	uint32_t process_event:1;

	/* - instructions are executed speculatively. */
	uint32_t speculative:1;

	/* - process @insn/@iext.
	 *
	 *   We have started processing events binding to @insn/@iext.  The
	 *   instruction has been accounted for in the previous block, but we
	 *   have not yet proceeded past it.
	 *
	 *   We will do so in pt_blk_event() after processing all events that
	 *   bind to it.
	 */
	uint32_t process_insn:1;

	/* - a paging event has already been bound to @insn/@iext. */
	uint32_t bound_paging:1;

	/* - a vmcs event has already been bound to @insn/@iext. */
	uint32_t bound_vmcs:1;

	/* - a ptwrite event has already been bound to @insn/@iext. */
	uint32_t bound_ptwrite:1;
};


/* Initialize a block decoder.
 *
 * Returns zero on success; a negative error code otherwise.
 * Returns -pte_internal, if @decoder or @config is NULL.
 */
extern int pt_blk_decoder_init(struct pt_block_decoder *decoder,
			       const struct pt_config *config);

/* Finalize a block decoder. */
extern void pt_blk_decoder_fini(struct pt_block_decoder *decoder);

#endif /* PT_BLOCK_DECODER_H */
