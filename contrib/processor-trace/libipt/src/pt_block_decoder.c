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

#include "pt_block_decoder.h"
#include "pt_block_cache.h"
#include "pt_section.h"
#include "pt_image.h"
#include "pt_insn.h"
#include "pt_config.h"
#include "pt_asid.h"
#include "pt_compiler.h"

#include "intel-pt.h"

#include <string.h>
#include <stdlib.h>


static int pt_blk_proceed_trailing_event(struct pt_block_decoder *,
					 struct pt_block *);


static int pt_blk_status(const struct pt_block_decoder *decoder, int flags)
{
	int status;

	if (!decoder)
		return -pte_internal;

	status = decoder->status;

	/* Indicate whether tracing is disabled or enabled.
	 *
	 * This duplicates the indication in struct pt_insn and covers the case
	 * where we indicate the status after synchronizing.
	 */
	if (!decoder->enabled)
		flags |= pts_ip_suppressed;

	/* Forward end-of-trace indications.
	 *
	 * Postpone it as long as we're still processing events, though.
	 */
	if ((status & pts_eos) && !decoder->process_event)
		flags |= pts_eos;

	return flags;
}

static void pt_blk_reset(struct pt_block_decoder *decoder)
{
	if (!decoder)
		return;

	decoder->mode = ptem_unknown;
	decoder->ip = 0ull;
	decoder->status = 0;
	decoder->enabled = 0;
	decoder->process_event = 0;
	decoder->speculative = 0;
	decoder->process_insn = 0;
	decoder->bound_paging = 0;
	decoder->bound_vmcs = 0;
	decoder->bound_ptwrite = 0;

	memset(&decoder->event, 0, sizeof(decoder->event));
	pt_retstack_init(&decoder->retstack);
	pt_asid_init(&decoder->asid);
}

/* Initialize the query decoder flags based on our flags. */

static int pt_blk_init_qry_flags(struct pt_conf_flags *qflags,
				 const struct pt_conf_flags *flags)
{
	if (!qflags || !flags)
		return -pte_internal;

	memset(qflags, 0, sizeof(*qflags));

	return 0;
}

int pt_blk_decoder_init(struct pt_block_decoder *decoder,
			const struct pt_config *uconfig)
{
	struct pt_config config;
	int errcode;

	if (!decoder)
		return -pte_internal;

	errcode = pt_config_from_user(&config, uconfig);
	if (errcode < 0)
		return errcode;

	/* The user supplied decoder flags. */
	decoder->flags = config.flags;

	/* Set the flags we need for the query decoder we use. */
	errcode = pt_blk_init_qry_flags(&config.flags, &decoder->flags);
	if (errcode < 0)
		return errcode;

	errcode = pt_qry_decoder_init(&decoder->query, &config);
	if (errcode < 0)
		return errcode;

	pt_image_init(&decoder->default_image, NULL);
	decoder->image = &decoder->default_image;

	errcode = pt_msec_cache_init(&decoder->scache);
	if (errcode < 0)
		return errcode;

	pt_blk_reset(decoder);

	return 0;
}

void pt_blk_decoder_fini(struct pt_block_decoder *decoder)
{
	if (!decoder)
		return;

	pt_msec_cache_fini(&decoder->scache);
	pt_image_fini(&decoder->default_image);
	pt_qry_decoder_fini(&decoder->query);
}

struct pt_block_decoder *
pt_blk_alloc_decoder(const struct pt_config *config)
{
	struct pt_block_decoder *decoder;
	int errcode;

	decoder = malloc(sizeof(*decoder));
	if (!decoder)
		return NULL;

	errcode = pt_blk_decoder_init(decoder, config);
	if (errcode < 0) {
		free(decoder);
		return NULL;
	}

	return decoder;
}

void pt_blk_free_decoder(struct pt_block_decoder *decoder)
{
	if (!decoder)
		return;

	pt_blk_decoder_fini(decoder);
	free(decoder);
}

/* Maybe synthesize a tick event.
 *
 * If we're not already processing events, check the current time against the
 * last event's time.  If it changed, synthesize a tick event with the new time.
 *
 * Returns zero if no tick event has been created.
 * Returns a positive integer if a tick event has been created.
 * Returns a negative error code otherwise.
 */
static int pt_blk_tick(struct pt_block_decoder *decoder, uint64_t ip)
{
	struct pt_event *ev;
	uint64_t tsc;
	uint32_t lost_mtc, lost_cyc;
	int errcode;

	if (!decoder)
		return -pte_internal;

	/* We're not generating tick events if tracing is disabled. */
	if (!decoder->enabled)
		return -pte_internal;

	/* Events already provide a timestamp so there is no need to synthesize
	 * an artificial tick event.  There's no room, either, since this would
	 * overwrite the in-progress event.
	 *
	 * In rare cases where we need to proceed to an event location using
	 * trace this may cause us to miss a timing update if the event is not
	 * forwarded to the user.
	 *
	 * The only case I can come up with at the moment is a MODE.EXEC binding
	 * to the TIP IP of a far branch.
	 */
	if (decoder->process_event)
		return 0;

	errcode = pt_qry_time(&decoder->query, &tsc, &lost_mtc, &lost_cyc);
	if (errcode < 0) {
		/* If we don't have wall-clock time, we use relative time. */
		if (errcode != -pte_no_time)
			return errcode;
	}

	ev = &decoder->event;

	/* We're done if time has not changed since the last event. */
	if (tsc == ev->tsc)
		return 0;

	/* Time has changed so we create a new tick event. */
	memset(ev, 0, sizeof(*ev));
	ev->type = ptev_tick;
	ev->variant.tick.ip = ip;

	/* Indicate if we have wall-clock time or only relative time. */
	if (errcode != -pte_no_time)
		ev->has_tsc = 1;
	ev->tsc = tsc;
	ev->lost_mtc = lost_mtc;
	ev->lost_cyc = lost_cyc;

	/* We now have an event to process. */
	decoder->process_event = 1;

	return 1;
}

/* Query an indirect branch.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int pt_blk_indirect_branch(struct pt_block_decoder *decoder,
				  uint64_t *ip)
{
	uint64_t evip;
	int status, errcode;

	if (!decoder)
		return -pte_internal;

	evip = decoder->ip;

	status = pt_qry_indirect_branch(&decoder->query, ip);
	if (status < 0)
		return status;

	if (decoder->flags.variant.block.enable_tick_events) {
		errcode = pt_blk_tick(decoder, evip);
		if (errcode < 0)
			return errcode;
	}

	return status;
}

/* Query a conditional branch.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int pt_blk_cond_branch(struct pt_block_decoder *decoder, int *taken)
{
	int status, errcode;

	if (!decoder)
		return -pte_internal;

	status = pt_qry_cond_branch(&decoder->query, taken);
	if (status < 0)
		return status;

	if (decoder->flags.variant.block.enable_tick_events) {
		errcode = pt_blk_tick(decoder, decoder->ip);
		if (errcode < 0)
			return errcode;
	}

	return status;
}

static int pt_blk_start(struct pt_block_decoder *decoder, int status)
{
	if (!decoder)
		return -pte_internal;

	if (status < 0)
		return status;

	decoder->status = status;
	if (!(status & pts_ip_suppressed))
		decoder->enabled = 1;

	/* We will always have an event.
	 *
	 * If we synchronized onto an empty PSB+, tracing is disabled and we'll
	 * process events until the enabled event.
	 *
	 * If tracing is enabled, PSB+ must at least provide the execution mode,
	 * which we're going to forward to the user.
	 */
	return pt_blk_proceed_trailing_event(decoder, NULL);
}

static int pt_blk_sync_reset(struct pt_block_decoder *decoder)
{
	if (!decoder)
		return -pte_internal;

	pt_blk_reset(decoder);

	return 0;
}

int pt_blk_sync_forward(struct pt_block_decoder *decoder)
{
	int errcode, status;

	if (!decoder)
		return -pte_invalid;

	errcode = pt_blk_sync_reset(decoder);
	if (errcode < 0)
		return errcode;

	status = pt_qry_sync_forward(&decoder->query, &decoder->ip);

	return pt_blk_start(decoder, status);
}

int pt_blk_sync_backward(struct pt_block_decoder *decoder)
{
	int errcode, status;

	if (!decoder)
		return -pte_invalid;

	errcode = pt_blk_sync_reset(decoder);
	if (errcode < 0)
		return errcode;

	status = pt_qry_sync_backward(&decoder->query, &decoder->ip);

	return pt_blk_start(decoder, status);
}

int pt_blk_sync_set(struct pt_block_decoder *decoder, uint64_t offset)
{
	int errcode, status;

	if (!decoder)
		return -pte_invalid;

	errcode = pt_blk_sync_reset(decoder);
	if (errcode < 0)
		return errcode;

	status = pt_qry_sync_set(&decoder->query, &decoder->ip, offset);

	return pt_blk_start(decoder, status);
}

int pt_blk_get_offset(const struct pt_block_decoder *decoder, uint64_t *offset)
{
	if (!decoder)
		return -pte_invalid;

	return pt_qry_get_offset(&decoder->query, offset);
}

int pt_blk_get_sync_offset(const struct pt_block_decoder *decoder,
			   uint64_t *offset)
{
	if (!decoder)
		return -pte_invalid;

	return pt_qry_get_sync_offset(&decoder->query, offset);
}

struct pt_image *pt_blk_get_image(struct pt_block_decoder *decoder)
{
	if (!decoder)
		return NULL;

	return decoder->image;
}

int pt_blk_set_image(struct pt_block_decoder *decoder, struct pt_image *image)
{
	if (!decoder)
		return -pte_invalid;

	if (!image)
		image = &decoder->default_image;

	decoder->image = image;
	return 0;
}

const struct pt_config *
pt_blk_get_config(const struct pt_block_decoder *decoder)
{
	if (!decoder)
		return NULL;

	return pt_qry_get_config(&decoder->query);
}

int pt_blk_time(struct pt_block_decoder *decoder, uint64_t *time,
		uint32_t *lost_mtc, uint32_t *lost_cyc)
{
	if (!decoder || !time)
		return -pte_invalid;

	return pt_qry_time(&decoder->query, time, lost_mtc, lost_cyc);
}

int pt_blk_core_bus_ratio(struct pt_block_decoder *decoder, uint32_t *cbr)
{
	if (!decoder || !cbr)
		return -pte_invalid;

	return pt_qry_core_bus_ratio(&decoder->query, cbr);
}

int pt_blk_asid(const struct pt_block_decoder *decoder, struct pt_asid *asid,
		size_t size)
{
	if (!decoder || !asid)
		return -pte_invalid;

	return pt_asid_to_user(asid, &decoder->asid, size);
}

/* Fetch the next pending event.
 *
 * Checks for pending events.  If an event is pending, fetches it (if not
 * already in process).
 *
 * Returns zero if no event is pending.
 * Returns a positive integer if an event is pending or in process.
 * Returns a negative error code otherwise.
 */
static inline int pt_blk_fetch_event(struct pt_block_decoder *decoder)
{
	int status;

	if (!decoder)
		return -pte_internal;

	if (decoder->process_event)
		return 1;

	if (!(decoder->status & pts_event_pending))
		return 0;

	status = pt_qry_event(&decoder->query, &decoder->event,
			      sizeof(decoder->event));
	if (status < 0)
		return status;

	decoder->process_event = 1;
	decoder->status = status;

	return 1;
}

static inline int pt_blk_block_is_empty(const struct pt_block *block)
{
	if (!block)
		return 1;

	return !block->ninsn;
}

static inline int block_to_user(struct pt_block *ublock, size_t size,
				const struct pt_block *block)
{
	if (!ublock || !block)
		return -pte_internal;

	if (ublock == block)
		return 0;

	/* Zero out any unknown bytes. */
	if (sizeof(*block) < size) {
		memset(ublock + sizeof(*block), 0, size - sizeof(*block));

		size = sizeof(*block);
	}

	memcpy(ublock, block, size);

	return 0;
}

static int pt_insn_false(const struct pt_insn *insn,
			 const struct pt_insn_ext *iext)
{
	(void) insn;
	(void) iext;

	return 0;
}

/* Determine the next IP using trace.
 *
 * Tries to determine the IP of the next instruction using trace and provides it
 * in @pip.
 *
 * Not requiring trace to determine the IP is treated as an internal error.
 *
 * Does not update the return compression stack for indirect calls.  This is
 * expected to have been done, already, when trying to determine the next IP
 * without using trace.
 *
 * Does not update @decoder->status.  The caller is expected to do that.
 *
 * Returns a non-negative pt_status_flag bit-vector on success, a negative error
 * code otherwise.
 * Returns -pte_internal if @pip, @decoder, @insn, or @iext are NULL.
 * Returns -pte_internal if no trace is required.
 */
static int pt_blk_next_ip(uint64_t *pip, struct pt_block_decoder *decoder,
			  const struct pt_insn *insn,
			  const struct pt_insn_ext *iext)
{
	int status, errcode;

	if (!pip || !decoder || !insn || !iext)
		return -pte_internal;

	/* We handle non-taken conditional branches, and compressed returns
	 * directly in the switch.
	 *
	 * All kinds of branches are handled below the switch.
	 */
	switch (insn->iclass) {
	case ptic_cond_jump: {
		uint64_t ip;
		int taken;

		status = pt_blk_cond_branch(decoder, &taken);
		if (status < 0)
			return status;

		ip = insn->ip + insn->size;
		if (taken)
			ip += iext->variant.branch.displacement;

		*pip = ip;
		return status;
	}

	case ptic_return: {
		int taken;

		/* Check for a compressed return. */
		status = pt_blk_cond_branch(decoder, &taken);
		if (status < 0) {
			if (status != -pte_bad_query)
				return status;

			break;
		}

		/* A compressed return is indicated by a taken conditional
		 * branch.
		 */
		if (!taken)
			return -pte_bad_retcomp;

		errcode = pt_retstack_pop(&decoder->retstack, pip);
		if (errcode < 0)
			return errcode;

		return status;
	}

	case ptic_jump:
	case ptic_call:
		/* A direct jump or call wouldn't require trace. */
		if (iext->variant.branch.is_direct)
			return -pte_internal;

		break;

	case ptic_far_call:
	case ptic_far_return:
	case ptic_far_jump:
		break;

	case ptic_ptwrite:
	case ptic_other:
		return -pte_internal;

	case ptic_error:
		return -pte_bad_insn;
	}

	/* Process an indirect branch.
	 *
	 * This covers indirect jumps and calls, non-compressed returns, and all
	 * flavors of far transfers.
	 */
	return pt_blk_indirect_branch(decoder, pip);
}

/* Proceed to the next IP using trace.
 *
 * We failed to proceed without trace.  This ends the current block.  Now use
 * trace to do one final step to determine the start IP of the next block.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int pt_blk_proceed_with_trace(struct pt_block_decoder *decoder,
				     const struct pt_insn *insn,
				     const struct pt_insn_ext *iext)
{
	int status;

	if (!decoder)
		return -pte_internal;

	status = pt_blk_next_ip(&decoder->ip, decoder, insn, iext);
	if (status < 0)
		return status;

	/* Preserve the query decoder's response which indicates upcoming
	 * events.
	 */
	decoder->status = status;

	/* We do need an IP in order to proceed. */
	if (status & pts_ip_suppressed)
		return -pte_noip;

	return 0;
}

/* Decode one instruction in a known section.
 *
 * Decode the instruction at @insn->ip in @msec assuming execution mode
 * @insn->mode.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int pt_blk_decode_in_section(struct pt_insn *insn,
				    struct pt_insn_ext *iext,
				    const struct pt_mapped_section *msec)
{
	int status;

	if (!insn || !iext)
		return -pte_internal;

	/* We know that @ip is contained in @section.
	 *
	 * Note that we need to translate @ip into a section offset.
	 */
	status = pt_msec_read(msec, insn->raw, sizeof(insn->raw), insn->ip);
	if (status < 0)
		return status;

	/* We initialize @insn->size to the maximal possible size.  It will be
	 * set to the actual size during instruction decode.
	 */
	insn->size = (uint8_t) status;

	return pt_ild_decode(insn, iext);
}

/* Update the return-address stack if @insn is a near call.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static inline int pt_blk_log_call(struct pt_block_decoder *decoder,
				  const struct pt_insn *insn,
				  const struct pt_insn_ext *iext)
{
	if (!decoder || !insn || !iext)
		return -pte_internal;

	if (insn->iclass != ptic_call)
		return 0;

	/* Ignore direct calls to the next instruction that are used for
	 * position independent code.
	 */
	if (iext->variant.branch.is_direct &&
	    !iext->variant.branch.displacement)
		return 0;

	return pt_retstack_push(&decoder->retstack, insn->ip + insn->size);
}

/* Proceed by one instruction.
 *
 * Tries to decode the instruction at @decoder->ip and, on success, adds it to
 * @block and provides it in @pinsn and @piext.
 *
 * The instruction will not be added if:
 *
 *   - the memory could not be read:  return error
 *   - it could not be decoded:       return error
 *   - @block is already full:        return zero
 *   - @block would switch sections:  return zero
 *
 * Returns a positive integer if the instruction was added.
 * Returns zero if the instruction didn't fit into @block.
 * Returns a negative error code otherwise.
 */
static int pt_blk_proceed_one_insn(struct pt_block_decoder *decoder,
				   struct pt_block *block,
				   struct pt_insn *pinsn,
				   struct pt_insn_ext *piext)
{
	struct pt_insn_ext iext;
	struct pt_insn insn;
	uint16_t ninsn;
	int status;

	if (!decoder || !block || !pinsn || !piext)
		return -pte_internal;

	/* There's nothing to do if there is no room in @block. */
	ninsn = block->ninsn + 1;
	if (!ninsn)
		return 0;

	/* The truncated instruction must be last. */
	if (block->truncated)
		return 0;

	memset(&insn, 0, sizeof(insn));
	memset(&iext, 0, sizeof(iext));

	insn.mode = decoder->mode;
	insn.ip = decoder->ip;

	status = pt_insn_decode(&insn, &iext, decoder->image, &decoder->asid);
	if (status < 0)
		return status;

	/* We do not switch sections inside a block. */
	if (insn.isid != block->isid) {
		if (!pt_blk_block_is_empty(block))
			return 0;

		block->isid = insn.isid;
	}

	/* If we couldn't read @insn's memory in one chunk from @insn.isid, we
	 * provide the memory in @block.
	 */
	if (insn.truncated) {
		memcpy(block->raw, insn.raw, insn.size);
		block->size = insn.size;
		block->truncated = 1;
	}

	/* Log calls' return addresses for return compression. */
	status = pt_blk_log_call(decoder, &insn, &iext);
	if (status < 0)
		return status;

	/* We have a new instruction. */
	block->iclass = insn.iclass;
	block->end_ip = insn.ip;
	block->ninsn = ninsn;

	*pinsn = insn;
	*piext = iext;

	return 1;
}


/* Proceed to a particular type of instruction without using trace.
 *
 * Proceed until we reach an instruction for which @predicate returns a positive
 * integer or until:
 *
 *   - @predicate returns an error:  return error
 *   - @block is full:               return zero
 *   - @block would switch sections: return zero
 *   - we would need trace:          return -pte_bad_query
 *
 * Provide the last instruction that was reached in @insn and @iext.
 *
 * Update @decoder->ip to point to the last IP that was reached.  If we fail due
 * to lack of trace or if we reach a desired instruction, this is @insn->ip;
 * otherwise this is the next instruction's IP.
 *
 * Returns a positive integer if a suitable instruction was reached.
 * Returns zero if no such instruction was reached.
 * Returns a negative error code otherwise.
 */
static int pt_blk_proceed_to_insn(struct pt_block_decoder *decoder,
				  struct pt_block *block,
				  struct pt_insn *insn,
				  struct pt_insn_ext *iext,
				  int (*predicate)(const struct pt_insn *,
						   const struct pt_insn_ext *))
{
	int status;

	if (!decoder || !insn || !predicate)
		return -pte_internal;

	for (;;) {
		status = pt_blk_proceed_one_insn(decoder, block, insn, iext);
		if (status <= 0)
			return status;

		/* We're done if this instruction matches the spec (positive
		 * status) or we run into an error (negative status).
		 */
		status = predicate(insn, iext);
		if (status != 0)
			return status;

		/* Let's see if we can proceed to the next IP without trace. */
		status = pt_insn_next_ip(&decoder->ip, insn, iext);
		if (status < 0)
			return status;

		/* End the block if the user asked us to.
		 *
		 * We only need to take care about direct near branches.
		 * Indirect and far branches require trace and will naturally
		 * end a block.
		 */
		if ((decoder->flags.variant.block.end_on_call &&
		     (insn->iclass == ptic_call)) ||
		    (decoder->flags.variant.block.end_on_jump &&
		     (insn->iclass == ptic_jump)))
			return 0;
	}
}

/* Proceed to a particular IP without using trace.
 *
 * Proceed until we reach @ip or until:
 *
 *   - @block is full:               return zero
 *   - @block would switch sections: return zero
 *   - we would need trace:          return -pte_bad_query
 *
 * Provide the last instruction that was reached in @insn and @iext.  If we
 * reached @ip, this is the instruction preceding it.
 *
 * Update @decoder->ip to point to the last IP that was reached.  If we fail due
 * to lack of trace, this is @insn->ip; otherwise this is the next instruction's
 * IP.
 *
 * Returns a positive integer if @ip was reached.
 * Returns zero if no such instruction was reached.
 * Returns a negative error code otherwise.
 */
static int pt_blk_proceed_to_ip(struct pt_block_decoder *decoder,
				struct pt_block *block, struct pt_insn *insn,
				struct pt_insn_ext *iext, uint64_t ip)
{
	int status;

	if (!decoder || !insn)
		return -pte_internal;

	for (;;) {
		/* We're done when we reach @ip.  We may not even have to decode
		 * a single instruction in some cases.
		 */
		if (decoder->ip == ip)
			return 1;

		status = pt_blk_proceed_one_insn(decoder, block, insn, iext);
		if (status <= 0)
			return status;

		/* Let's see if we can proceed to the next IP without trace. */
		status = pt_insn_next_ip(&decoder->ip, insn, iext);
		if (status < 0)
			return status;

		/* End the block if the user asked us to.
		 *
		 * We only need to take care about direct near branches.
		 * Indirect and far branches require trace and will naturally
		 * end a block.
		 *
		 * The call at the end of the block may have reached @ip; make
		 * sure to indicate that.
		 */
		if ((decoder->flags.variant.block.end_on_call &&
		     (insn->iclass == ptic_call)) ||
		    (decoder->flags.variant.block.end_on_jump &&
		     (insn->iclass == ptic_jump))) {
			return (decoder->ip == ip ? 1 : 0);
		}
	}
}

/* Proceed to a particular IP with trace, if necessary.
 *
 * Proceed until we reach @ip or until:
 *
 *   - @block is full:               return zero
 *   - @block would switch sections: return zero
 *   - we need trace:                return zero
 *
 * Update @decoder->ip to point to the last IP that was reached.
 *
 * A return of zero ends @block.
 *
 * Returns a positive integer if @ip was reached.
 * Returns zero if no such instruction was reached.
 * Returns a negative error code otherwise.
 */
static int pt_blk_proceed_to_ip_with_trace(struct pt_block_decoder *decoder,
					   struct pt_block *block,
					   uint64_t ip)
{
	struct pt_insn_ext iext;
	struct pt_insn insn;
	int status;

	/* Try to reach @ip without trace.
	 *
	 * We're also OK if @block overflowed or we switched sections and we
	 * have to try again in the next iteration.
	 */
	status = pt_blk_proceed_to_ip(decoder, block, &insn, &iext, ip);
	if (status != -pte_bad_query)
		return status;

	/* Needing trace is not an error.  We use trace to determine the next
	 * start IP and end the block.
	 */
	return pt_blk_proceed_with_trace(decoder, &insn, &iext);
}

static int pt_insn_skl014(const struct pt_insn *insn,
			  const struct pt_insn_ext *iext)
{
	if (!insn || !iext)
		return 0;

	switch (insn->iclass) {
	default:
		return 0;

	case ptic_call:
	case ptic_jump:
		return iext->variant.branch.is_direct;

	case ptic_other:
		return pt_insn_changes_cr3(insn, iext);
	}
}

/* Proceed to the location of a synchronous disabled event with suppressed IP
 * considering SKL014.
 *
 * We have a (synchronous) disabled event pending.  Proceed to the event
 * location and indicate whether we were able to reach it.
 *
 * With SKL014 a TIP.PGD with suppressed IP may also be generated by a direct
 * unconditional branch that clears FilterEn by jumping out of a filter region
 * or into a TraceStop region.  Use the filter configuration to determine the
 * exact branch the event binds to.
 *
 * The last instruction that was reached is stored in @insn/@iext.
 *
 * Returns a positive integer if the event location was reached.
 * Returns zero if the event location was not reached.
 * Returns a negative error code otherwise.
 */
static int pt_blk_proceed_skl014(struct pt_block_decoder *decoder,
				 struct pt_block *block, struct pt_insn *insn,
				 struct pt_insn_ext *iext)
{
	const struct pt_conf_addr_filter *addr_filter;
	int status;

	if (!decoder || !block || !insn || !iext)
		return -pte_internal;

	addr_filter = &decoder->query.config.addr_filter;
	for (;;) {
		uint64_t ip;

		status = pt_blk_proceed_to_insn(decoder, block, insn, iext,
						pt_insn_skl014);
		if (status <= 0)
			break;

		/* The erratum doesn't apply if we can bind the event to a
		 * CR3-changing instruction.
		 */
		if (pt_insn_changes_cr3(insn, iext))
			break;

		/* Check the filter against the branch target. */
		status = pt_insn_next_ip(&ip, insn, iext);
		if (status < 0)
			break;

		status = pt_filter_addr_check(addr_filter, ip);
		if (status <= 0) {
			/* We need to flip the indication.
			 *
			 * We reached the event location when @ip lies inside a
			 * tracing-disabled region.
			 */
			if (!status)
				status = 1;

			break;
		}

		/* This is not the correct instruction.  Proceed past it and try
		 * again.
		 */
		decoder->ip = ip;

		/* End the block if the user asked us to.
		 *
		 * We only need to take care about direct near branches.
		 * Indirect and far branches require trace and will naturally
		 * end a block.
		 */
		if ((decoder->flags.variant.block.end_on_call &&
		    (insn->iclass == ptic_call)) ||
		    (decoder->flags.variant.block.end_on_jump &&
		    (insn->iclass == ptic_jump)))
			break;
	}

	return status;
}

/* Proceed to the event location for a disabled event.
 *
 * We have a (synchronous) disabled event pending.  Proceed to the event
 * location and indicate whether we were able to reach it.
 *
 * The last instruction that was reached is stored in @insn/@iext.
 *
 * Returns a positive integer if the event location was reached.
 * Returns zero if the event location was not reached.
 * Returns a negative error code otherwise.
 */
static int pt_blk_proceed_to_disabled(struct pt_block_decoder *decoder,
				      struct pt_block *block,
				      struct pt_insn *insn,
				      struct pt_insn_ext *iext,
				      const struct pt_event *ev)
{
	if (!decoder || !block || !ev)
		return -pte_internal;

	if (ev->ip_suppressed) {
		/* Due to SKL014 the TIP.PGD payload may be suppressed also for
		 * direct branches.
		 *
		 * If we don't have a filter configuration we assume that no
		 * address filters were used and the erratum does not apply.
		 *
		 * We might otherwise disable tracing too early.
		 */
		if (decoder->query.config.addr_filter.config.addr_cfg &&
		    decoder->query.config.errata.skl014)
			return pt_blk_proceed_skl014(decoder, block, insn,
						     iext);

		/* A synchronous disabled event also binds to far branches and
		 * CPL-changing instructions.  Both would require trace,
		 * however, and are thus implicitly handled by erroring out.
		 *
		 * The would-require-trace error is handled by our caller.
		 */
		return pt_blk_proceed_to_insn(decoder, block, insn, iext,
					      pt_insn_changes_cr3);
	} else
		return pt_blk_proceed_to_ip(decoder, block, insn, iext,
					    ev->variant.disabled.ip);
}

/* Set the expected resume address for a synchronous disable.
 *
 * On a synchronous disable, @decoder->ip still points to the instruction to
 * which the event bound.  That's not where we expect tracing to resume.
 *
 * For calls, a fair assumption is that tracing resumes after returning from the
 * called function.  For other types of instructions, we simply don't know.
 *
 * Returns zero on success, a negative pt_error_code otherwise.
 */
static int pt_blk_set_disable_resume_ip(struct pt_block_decoder *decoder,
					const struct pt_insn *insn)
{
	if (!decoder || !insn)
		return -pte_internal;

	switch (insn->iclass) {
	case ptic_call:
	case ptic_far_call:
		decoder->ip = insn->ip + insn->size;
		break;

	default:
		decoder->ip = 0ull;
		break;
	}

	return 0;
}

/* Proceed to the event location for an async paging event.
 *
 * We have an async paging event pending.  Proceed to the event location and
 * indicate whether we were able to reach it.  Needing trace in order to proceed
 * is not an error in this case but ends the block.
 *
 * Returns a positive integer if the event location was reached.
 * Returns zero if the event location was not reached.
 * Returns a negative error code otherwise.
 */
static int pt_blk_proceed_to_async_paging(struct pt_block_decoder *decoder,
					  struct pt_block *block,
					  const struct pt_event *ev)
{
	int status;

	if (!decoder || !ev)
		return -pte_internal;

	/* Apply the event immediately if we don't have an IP. */
	if (ev->ip_suppressed)
		return 1;

	status = pt_blk_proceed_to_ip_with_trace(decoder, block,
						 ev->variant.async_paging.ip);
	if (status < 0)
		return status;

	/* We may have reached the IP. */
	return (decoder->ip == ev->variant.async_paging.ip ? 1 : 0);
}

/* Proceed to the event location for an async vmcs event.
 *
 * We have an async vmcs event pending.  Proceed to the event location and
 * indicate whether we were able to reach it.  Needing trace in order to proceed
 * is not an error in this case but ends the block.
 *
 * Returns a positive integer if the event location was reached.
 * Returns zero if the event location was not reached.
 * Returns a negative error code otherwise.
 */
static int pt_blk_proceed_to_async_vmcs(struct pt_block_decoder *decoder,
					struct pt_block *block,
					const struct pt_event *ev)
{
	int status;

	if (!decoder || !ev)
		return -pte_internal;

	/* Apply the event immediately if we don't have an IP. */
	if (ev->ip_suppressed)
		return 1;

	status = pt_blk_proceed_to_ip_with_trace(decoder, block,
						 ev->variant.async_vmcs.ip);
	if (status < 0)
		return status;

	/* We may have reached the IP. */
	return (decoder->ip == ev->variant.async_vmcs.ip ? 1 : 0);
}

/* Proceed to the event location for an exec mode event.
 *
 * We have an exec mode event pending.  Proceed to the event location and
 * indicate whether we were able to reach it.  Needing trace in order to proceed
 * is not an error in this case but ends the block.
 *
 * Returns a positive integer if the event location was reached.
 * Returns zero if the event location was not reached.
 * Returns a negative error code otherwise.
 */
static int pt_blk_proceed_to_exec_mode(struct pt_block_decoder *decoder,
				       struct pt_block *block,
				       const struct pt_event *ev)
{
	int status;

	if (!decoder || !ev)
		return -pte_internal;

	/* Apply the event immediately if we don't have an IP. */
	if (ev->ip_suppressed)
		return 1;

	status = pt_blk_proceed_to_ip_with_trace(decoder, block,
						 ev->variant.exec_mode.ip);
	if (status < 0)
		return status;

	/* We may have reached the IP. */
	return (decoder->ip == ev->variant.exec_mode.ip ? 1 : 0);
}

/* Proceed to the event location for a ptwrite event.
 *
 * We have a ptwrite event pending.  Proceed to the event location and indicate
 * whether we were able to reach it.
 *
 * In case of the event binding to a ptwrite instruction, we pass beyond that
 * instruction and update the event to provide the instruction's IP.
 *
 * In the case of the event binding to an IP provided in the event, we move
 * beyond the instruction at that IP.
 *
 * Returns a positive integer if the event location was reached.
 * Returns zero if the event location was not reached.
 * Returns a negative error code otherwise.
 */
static int pt_blk_proceed_to_ptwrite(struct pt_block_decoder *decoder,
				     struct pt_block *block,
				     struct pt_insn *insn,
				     struct pt_insn_ext *iext,
				     struct pt_event *ev)
{
	int status;

	if (!insn || !ev)
		return -pte_internal;

	/* If we don't have an IP, the event binds to the next PTWRITE
	 * instruction.
	 *
	 * If we have an IP it still binds to the next PTWRITE instruction but
	 * now the IP tells us where that instruction is.  This makes most sense
	 * when tracing is disabled and we don't have any other means of finding
	 * the PTWRITE instruction.  We nevertheless distinguish the two cases,
	 * here.
	 *
	 * In both cases, we move beyond the PTWRITE instruction, so it will be
	 * the last instruction in the current block and @decoder->ip will point
	 * to the instruction following it.
	 */
	if (ev->ip_suppressed) {
		status = pt_blk_proceed_to_insn(decoder, block, insn, iext,
						pt_insn_is_ptwrite);
		if (status <= 0)
			return status;

		/* We now know the IP of the PTWRITE instruction corresponding
		 * to this event.  Fill it in to make it more convenient for the
		 * user to process the event.
		 */
		ev->variant.ptwrite.ip = insn->ip;
		ev->ip_suppressed = 0;
	} else {
		status = pt_blk_proceed_to_ip(decoder, block, insn, iext,
					      ev->variant.ptwrite.ip);
		if (status <= 0)
			return status;

		/* We reached the PTWRITE instruction and @decoder->ip points to
		 * it; @insn/@iext still contain the preceding instruction.
		 *
		 * Proceed beyond the PTWRITE to account for it.  Note that we
		 * may still overflow the block, which would cause us to
		 * postpone both instruction and event to the next block.
		 */
		status = pt_blk_proceed_one_insn(decoder, block, insn, iext);
		if (status <= 0)
			return status;
	}

	return 1;
}

/* Try to work around erratum SKD022.
 *
 * If we get an asynchronous disable on VMLAUNCH or VMRESUME, the FUP that
 * caused the disable to be asynchronous might have been bogous.
 *
 * Returns a positive integer if the erratum has been handled.
 * Returns zero if the erratum does not apply.
 * Returns a negative error code otherwise.
 */
static int pt_blk_handle_erratum_skd022(struct pt_block_decoder *decoder,
					struct pt_event *ev)
{
	struct pt_insn_ext iext;
	struct pt_insn insn;
	int errcode;

	if (!decoder || !ev)
		return -pte_internal;

	insn.mode = decoder->mode;
	insn.ip = ev->variant.async_disabled.at;

	errcode = pt_insn_decode(&insn, &iext, decoder->image, &decoder->asid);
	if (errcode < 0)
		return 0;

	switch (iext.iclass) {
	default:
		/* The erratum does not apply. */
		return 0;

	case PTI_INST_VMLAUNCH:
	case PTI_INST_VMRESUME:
		/* The erratum may apply.  We can't be sure without a lot more
		 * analysis.  Let's assume it does.
		 *
		 * We turn the async disable into a sync disable.  Our caller
		 * will restart event processing.
		 */
		ev->type = ptev_disabled;
		ev->variant.disabled.ip = ev->variant.async_disabled.ip;

		return 1;
	}
}

/* Postpone proceeding past @insn/@iext and indicate a pending event.
 *
 * There may be further events pending on @insn/@iext.  Postpone proceeding past
 * @insn/@iext until we processed all events that bind to it.
 *
 * Returns a non-negative pt_status_flag bit-vector indicating a pending event
 * on success, a negative pt_error_code otherwise.
 */
static int pt_blk_postpone_insn(struct pt_block_decoder *decoder,
				const struct pt_insn *insn,
				const struct pt_insn_ext *iext)
{
	if (!decoder || !insn || !iext)
		return -pte_internal;

	/* Only one can be active. */
	if (decoder->process_insn)
		return -pte_internal;

	decoder->process_insn = 1;
	decoder->insn = *insn;
	decoder->iext = *iext;

	return pt_blk_status(decoder, pts_event_pending);
}

/* Remove any postponed instruction from @decoder.
 *
 * Returns zero on success, a negative pt_error_code otherwise.
 */
static int pt_blk_clear_postponed_insn(struct pt_block_decoder *decoder)
{
	if (!decoder)
		return -pte_internal;

	decoder->process_insn = 0;
	decoder->bound_paging = 0;
	decoder->bound_vmcs = 0;
	decoder->bound_ptwrite = 0;

	return 0;
}

/* Proceed past a postponed instruction.
 *
 * If an instruction has been postponed in @decoder, proceed past it.
 *
 * Returns zero on success, a negative pt_error_code otherwise.
 */
static int pt_blk_proceed_postponed_insn(struct pt_block_decoder *decoder)
{
	int status;

	if (!decoder)
		return -pte_internal;

	/* There's nothing to do if we have no postponed instruction. */
	if (!decoder->process_insn)
		return 0;

	/* There's nothing to do if tracing got disabled. */
	if (!decoder->enabled)
		return pt_blk_clear_postponed_insn(decoder);

	status = pt_insn_next_ip(&decoder->ip, &decoder->insn, &decoder->iext);
	if (status < 0) {
		if (status != -pte_bad_query)
			return status;

		status = pt_blk_proceed_with_trace(decoder, &decoder->insn,
						   &decoder->iext);
		if (status < 0)
			return status;
	}

	return pt_blk_clear_postponed_insn(decoder);
}

/* Proceed to the next event.
 *
 * We have an event pending.  Proceed to the event location and indicate the
 * event to the user.
 *
 * On our way to the event location we may also be forced to postpone the event
 * to the next block, e.g. if we overflow the number of instructions in the
 * block or if we need trace in order to reach the event location.
 *
 * If we're not able to reach the event location, we return zero.  This is what
 * pt_blk_status() would return since:
 *
 *   - we suppress pts_eos as long as we're processing events
 *   - we do not set pts_ip_suppressed since tracing must be enabled
 *
 * Returns a non-negative pt_status_flag bit-vector on success, a negative error
 * code otherwise.
 */
static int pt_blk_proceed_event(struct pt_block_decoder *decoder,
				struct pt_block *block)
{
	struct pt_insn_ext iext;
	struct pt_insn insn;
	struct pt_event *ev;
	int status;

	if (!decoder || !decoder->process_event || !block)
		return -pte_internal;

	ev = &decoder->event;
	switch (ev->type) {
	case ptev_enabled:
		break;

	case ptev_disabled:
		status = pt_blk_proceed_to_disabled(decoder, block, &insn,
						    &iext, ev);
		if (status <= 0) {
			/* A synchronous disable event also binds to the next
			 * indirect or conditional branch, i.e. to any branch
			 * that would have required trace.
			 */
			if (status != -pte_bad_query)
				return status;

			status = pt_blk_set_disable_resume_ip(decoder, &insn);
			if (status < 0)
				return status;
		}

		break;

	case ptev_async_disabled:
		status = pt_blk_proceed_to_ip(decoder, block, &insn, &iext,
					      ev->variant.async_disabled.at);
		if (status <= 0)
			return status;

		if (decoder->query.config.errata.skd022) {
			status = pt_blk_handle_erratum_skd022(decoder, ev);
			if (status != 0) {
				if (status < 0)
					return status;

				/* If the erratum hits, we modify the event.
				 * Try again.
				 */
				return pt_blk_proceed_event(decoder, block);
			}
		}

		break;

	case ptev_async_branch:
		status = pt_blk_proceed_to_ip(decoder, block, &insn, &iext,
					      ev->variant.async_branch.from);
		if (status <= 0)
			return status;

		break;

	case ptev_paging:
		if (!decoder->enabled)
			break;

		status = pt_blk_proceed_to_insn(decoder, block, &insn, &iext,
						pt_insn_binds_to_pip);
		if (status <= 0)
			return status;

		/* We bound a paging event.  Make sure we do not bind further
		 * paging events to this instruction.
		 */
		decoder->bound_paging = 1;

		return pt_blk_postpone_insn(decoder, &insn, &iext);

	case ptev_async_paging:
		status = pt_blk_proceed_to_async_paging(decoder, block, ev);
		if (status <= 0)
			return status;

		break;

	case ptev_vmcs:
		if (!decoder->enabled)
			break;

		status = pt_blk_proceed_to_insn(decoder, block, &insn, &iext,
						pt_insn_binds_to_vmcs);
		if (status <= 0)
			return status;

		/* We bound a vmcs event.  Make sure we do not bind further vmcs
		 * events to this instruction.
		 */
		decoder->bound_vmcs = 1;

		return pt_blk_postpone_insn(decoder, &insn, &iext);

	case ptev_async_vmcs:
		status = pt_blk_proceed_to_async_vmcs(decoder, block, ev);
		if (status <= 0)
			return status;

		break;

	case ptev_overflow:
		break;

	case ptev_exec_mode:
		status = pt_blk_proceed_to_exec_mode(decoder, block, ev);
		if (status <= 0)
			return status;

		break;

	case ptev_tsx:
		if (ev->ip_suppressed)
			break;

		status = pt_blk_proceed_to_ip(decoder, block, &insn, &iext,
					      ev->variant.tsx.ip);
		if (status <= 0)
			return status;

		break;

	case ptev_stop:
		break;

	case ptev_exstop:
		if (!decoder->enabled || ev->ip_suppressed)
			break;

		status = pt_blk_proceed_to_ip(decoder, block, &insn, &iext,
					      ev->variant.exstop.ip);
		if (status <= 0)
			return status;

		break;

	case ptev_mwait:
		if (!decoder->enabled || ev->ip_suppressed)
			break;

		status = pt_blk_proceed_to_ip(decoder, block, &insn, &iext,
					      ev->variant.mwait.ip);
		if (status <= 0)
			return status;

		break;

	case ptev_pwre:
	case ptev_pwrx:
		break;

	case ptev_ptwrite:
		if (!decoder->enabled)
			break;

		status = pt_blk_proceed_to_ptwrite(decoder, block, &insn,
						   &iext, ev);
		if (status <= 0)
			return status;

		/* We bound a ptwrite event.  Make sure we do not bind further
		 * ptwrite events to this instruction.
		 */
		decoder->bound_ptwrite = 1;

		return pt_blk_postpone_insn(decoder, &insn, &iext);

	case ptev_tick:
	case ptev_cbr:
	case ptev_mnt:
		break;
	}

	return pt_blk_status(decoder, pts_event_pending);
}

/* Proceed to the next decision point without using the block cache.
 *
 * Tracing is enabled and we don't have an event pending.  Proceed as far as
 * we get without trace.  Stop when we either:
 *
 *   - need trace in order to continue
 *   - overflow the max number of instructions in a block
 *
 * We actually proceed one instruction further to get the start IP for the next
 * block.  This only updates @decoder's internal state, though.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int pt_blk_proceed_no_event_uncached(struct pt_block_decoder *decoder,
					    struct pt_block *block)
{
	struct pt_insn_ext iext;
	struct pt_insn insn;
	int status;

	if (!decoder || !block)
		return -pte_internal;

	/* This is overly conservative, really.  We shouldn't get a bad-query
	 * status unless we decoded at least one instruction successfully.
	 */
	memset(&insn, 0, sizeof(insn));
	memset(&iext, 0, sizeof(iext));

	/* Proceed as far as we get without trace. */
	status = pt_blk_proceed_to_insn(decoder, block, &insn, &iext,
					pt_insn_false);
	if (status < 0) {
		if (status != -pte_bad_query)
			return status;

		return pt_blk_proceed_with_trace(decoder, &insn, &iext);
	}

	return 0;
}

/* Check if @ip is contained in @section loaded at @laddr.
 *
 * Returns non-zero if it is.
 * Returns zero if it isn't or of @section is NULL.
 */
static inline int pt_blk_is_in_section(const struct pt_mapped_section *msec,
				       uint64_t ip)
{
	uint64_t begin, end;

	begin = pt_msec_begin(msec);
	end = pt_msec_end(msec);

	return (begin <= ip && ip < end);
}

/* Insert a trampoline block cache entry.
 *
 * Add a trampoline block cache entry at @ip to continue at @nip, where @nip
 * must be the next instruction after @ip.
 *
 * Both @ip and @nip must be section-relative
 *
 * Returns zero on success, a negative error code otherwise.
 */
static inline int pt_blk_add_trampoline(struct pt_block_cache *bcache,
					uint64_t ip, uint64_t nip,
					enum pt_exec_mode mode)
{
	struct pt_bcache_entry bce;
	int64_t disp;

	/* The displacement from @ip to @nip for the trampoline. */
	disp = (int64_t) (nip - ip);

	memset(&bce, 0, sizeof(bce));
	bce.displacement = (int32_t) disp;
	bce.ninsn = 1;
	bce.mode = mode;
	bce.qualifier = ptbq_again;

	/* If we can't reach @nip without overflowing the displacement field, we
	 * have to stop and re-decode the instruction at @ip.
	 */
	if ((int64_t) bce.displacement != disp) {

		memset(&bce, 0, sizeof(bce));
		bce.ninsn = 1;
		bce.mode = mode;
		bce.qualifier = ptbq_decode;
	}

	return pt_bcache_add(bcache, ip, bce);
}

/* Insert a decode block cache entry.
 *
 * Add a decode block cache entry at @ioff.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static inline int pt_blk_add_decode(struct pt_block_cache *bcache,
				    uint64_t ioff, enum pt_exec_mode mode)
{
	struct pt_bcache_entry bce;

	memset(&bce, 0, sizeof(bce));
	bce.ninsn = 1;
	bce.mode = mode;
	bce.qualifier = ptbq_decode;

	return pt_bcache_add(bcache, ioff, bce);
}

enum {
	/* The maximum number of steps when filling the block cache. */
	bcache_fill_steps	= 0x400
};

/* Proceed to the next instruction and fill the block cache for @decoder->ip.
 *
 * Tracing is enabled and we don't have an event pending.  The current IP is not
 * yet cached.
 *
 * Proceed one instruction without using the block cache, then try to proceed
 * further using the block cache.
 *
 * On our way back, add a block cache entry for the IP before proceeding.  Note
 * that the recursion is bounded by @steps and ultimately by the maximum number
 * of instructions in a block.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int
pt_blk_proceed_no_event_fill_cache(struct pt_block_decoder *decoder,
				   struct pt_block *block,
				   struct pt_block_cache *bcache,
				   const struct pt_mapped_section *msec,
				   size_t steps)
{
	struct pt_bcache_entry bce;
	struct pt_insn_ext iext;
	struct pt_insn insn;
	uint64_t nip, dip;
	int64_t disp, ioff, noff;
	int status;

	if (!decoder || !steps)
		return -pte_internal;

	/* Proceed one instruction by decoding and examining it.
	 *
	 * Note that we also return on a status of zero that indicates that the
	 * instruction didn't fit into @block.
	 */
	status = pt_blk_proceed_one_insn(decoder, block, &insn, &iext);
	if (status <= 0)
		return status;

	ioff = pt_msec_unmap(msec, insn.ip);

	/* Let's see if we can proceed to the next IP without trace.
	 *
	 * If we can't, this is certainly a decision point.
	 */
	status = pt_insn_next_ip(&decoder->ip, &insn, &iext);
	if (status < 0) {
		if (status != -pte_bad_query)
			return status;

		memset(&bce, 0, sizeof(bce));
		bce.ninsn = 1;
		bce.mode = insn.mode;
		bce.isize = insn.size;

		/* Clear the instruction size in case of overflows. */
		if ((uint8_t) bce.isize != insn.size)
			bce.isize = 0;

		switch (insn.iclass) {
		case ptic_ptwrite:
		case ptic_error:
		case ptic_other:
			return -pte_internal;

		case ptic_jump:
			/* A direct jump doesn't require trace. */
			if (iext.variant.branch.is_direct)
				return -pte_internal;

			bce.qualifier = ptbq_indirect;
			break;

		case ptic_call:
			/* A direct call doesn't require trace. */
			if (iext.variant.branch.is_direct)
				return -pte_internal;

			bce.qualifier = ptbq_ind_call;
			break;

		case ptic_return:
			bce.qualifier = ptbq_return;
			break;

		case ptic_cond_jump:
			bce.qualifier = ptbq_cond;
			break;

		case ptic_far_call:
		case ptic_far_return:
		case ptic_far_jump:
			bce.qualifier = ptbq_indirect;
			break;
		}

		/* If the block was truncated, we have to decode its last
		 * instruction each time.
		 *
		 * We could have skipped the above switch and size assignment in
		 * this case but this is already a slow and hopefully infrequent
		 * path.
		 */
		if (block->truncated)
			bce.qualifier = ptbq_decode;

		status = pt_bcache_add(bcache, ioff, bce);
		if (status < 0)
			return status;

		return pt_blk_proceed_with_trace(decoder, &insn, &iext);
	}

	/* The next instruction's IP. */
	nip = decoder->ip;
	noff = pt_msec_unmap(msec, nip);

	/* Even if we were able to proceed without trace, we might have to stop
	 * here for various reasons:
	 *
	 *   - at near direct calls to update the return-address stack
	 *
	 *     We are forced to re-decode @insn to get the branch displacement.
	 *
	 *     Even though it is constant, we don't cache it to avoid increasing
	 *     the size of a cache entry.  Note that the displacement field is
	 *     zero for this entry and we might be tempted to use it - but other
	 *     entries that point to this decision point will have non-zero
	 *     displacement.
	 *
	 *     We could proceed after a near direct call but we migh as well
	 *     postpone it to the next iteration.  Make sure to end the block if
	 *     @decoder->flags.variant.block.end_on_call is set, though.
	 *
	 *   - at near direct backwards jumps to detect section splits
	 *
	 *     In case the current section is split underneath us, we must take
	 *     care to detect that split.
	 *
	 *     There is one corner case where the split is in the middle of a
	 *     linear sequence of instructions that branches back into the
	 *     originating section.
	 *
	 *     Calls, indirect branches, and far branches are already covered
	 *     since they either require trace or already require us to stop
	 *     (i.e. near direct calls) for other reasons.  That leaves near
	 *     direct backward jumps.
	 *
	 *     Instead of the decode stop at the jump instruction we're using we
	 *     could have made sure that other block cache entries that extend
	 *     this one insert a trampoline to the jump's entry.  This would
	 *     have been a bit more complicated.
	 *
	 *   - if we switched sections
	 *
	 *     This ends a block just like a branch that requires trace.
	 *
	 *     We need to re-decode @insn in order to determine the start IP of
	 *     the next block.
	 *
	 *   - if the block is truncated
	 *
	 *     We need to read the last instruction's memory from multiple
	 *     sections and provide it to the user.
	 *
	 *     We could still use the block cache but then we'd have to handle
	 *     this case for each qualifier.  Truncation is hopefully rare and
	 *     having to read the memory for the instruction from multiple
	 *     sections is already slow.  Let's rather keep things simple and
	 *     route it through the decode flow, where we already have
	 *     everything in place.
	 */
	switch (insn.iclass) {
	case ptic_call:
		return pt_blk_add_decode(bcache, ioff, insn.mode);

	case ptic_jump:
		/* An indirect branch requires trace and should have been
		 * handled above.
		 */
		if (!iext.variant.branch.is_direct)
			return -pte_internal;

		if (iext.variant.branch.displacement < 0 ||
		    decoder->flags.variant.block.end_on_jump)
			return pt_blk_add_decode(bcache, ioff, insn.mode);

		fallthrough;
	default:
		if (!pt_blk_is_in_section(msec, nip) || block->truncated)
			return pt_blk_add_decode(bcache, ioff, insn.mode);

		break;
	}

	/* We proceeded one instruction.  Let's see if we have a cache entry for
	 * the next instruction.
	 */
	status = pt_bcache_lookup(&bce, bcache, noff);
	if (status < 0)
		return status;

	/* If we don't have a valid cache entry, yet, fill the cache some more.
	 *
	 * On our way back, we add a cache entry for this instruction based on
	 * the cache entry of the succeeding instruction.
	 */
	if (!pt_bce_is_valid(bce)) {
		/* If we exceeded the maximum number of allowed steps, we insert
		 * a trampoline to the next instruction.
		 *
		 * The next time we encounter the same code, we will use the
		 * trampoline to jump directly to where we left off this time
		 * and continue from there.
		 */
		steps -= 1;
		if (!steps)
			return pt_blk_add_trampoline(bcache, ioff, noff,
						     insn.mode);

		status = pt_blk_proceed_no_event_fill_cache(decoder, block,
							    bcache, msec,
							    steps);
		if (status < 0)
			return status;

		/* Let's see if we have more luck this time. */
		status = pt_bcache_lookup(&bce, bcache, noff);
		if (status < 0)
			return status;

		/* If we still don't have a valid cache entry, we're done.  Most
		 * likely, @block overflowed and we couldn't proceed past the
		 * next instruction.
		 */
		if (!pt_bce_is_valid(bce))
			return 0;
	}

	/* We must not have switched execution modes.
	 *
	 * This would require an event and we're on the no-event flow.
	 */
	if (pt_bce_exec_mode(bce) != insn.mode)
		return -pte_internal;

	/* The decision point IP and the displacement from @insn.ip. */
	dip = nip + bce.displacement;
	disp = (int64_t) (dip - insn.ip);

	/* We may have switched sections if the section was split.  See
	 * pt_blk_proceed_no_event_cached() for a more elaborate comment.
	 *
	 * We're not adding a block cache entry since this won't apply to the
	 * original section which may be shared with other decoders.
	 *
	 * We will instead take the slow path until the end of the section.
	 */
	if (!pt_blk_is_in_section(msec, dip))
		return 0;

	/* Let's try to reach @nip's decision point from @insn.ip.
	 *
	 * There are two fields that may overflow: @bce.ninsn and
	 * @bce.displacement.
	 */
	bce.ninsn += 1;
	bce.displacement = (int32_t) disp;

	/* If none of them overflowed, we're done.
	 *
	 * If one or both overflowed, let's try to insert a trampoline, i.e. we
	 * try to reach @dip via a ptbq_again entry to @nip.
	 */
	if (!bce.ninsn || ((int64_t) bce.displacement != disp))
		return pt_blk_add_trampoline(bcache, ioff, noff, insn.mode);

	/* We're done.  Add the cache entry.
	 *
	 * There's a chance that other decoders updated the cache entry in the
	 * meantime.  They should have come to the same conclusion as we,
	 * though, and the cache entries should be identical.
	 *
	 * Cache updates are atomic so even if the two versions were not
	 * identical, we wouldn't care because they are both correct.
	 */
	return pt_bcache_add(bcache, ioff, bce);
}

/* Proceed at a potentially truncated instruction.
 *
 * We were not able to decode the instruction at @decoder->ip in @decoder's
 * cached section.  This is typically caused by not having enough bytes.
 *
 * Try to decode the instruction again using the entire image.  If this succeeds
 * we expect to end up with an instruction that was truncated in the section it
 * started.  We provide the full instruction in this case and end the block.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int pt_blk_proceed_truncated(struct pt_block_decoder *decoder,
				    struct pt_block *block)
{
	struct pt_insn_ext iext;
	struct pt_insn insn;
	int errcode;

	if (!decoder || !block)
		return -pte_internal;

	memset(&iext, 0, sizeof(iext));
	memset(&insn, 0, sizeof(insn));

	insn.mode = decoder->mode;
	insn.ip = decoder->ip;

	errcode = pt_insn_decode(&insn, &iext, decoder->image, &decoder->asid);
	if (errcode < 0)
		return errcode;

	/* We shouldn't use this function if the instruction isn't truncated. */
	if (!insn.truncated)
		return -pte_internal;

	/* Provide the instruction in the block.  This ends the block. */
	memcpy(block->raw, insn.raw, insn.size);
	block->iclass = insn.iclass;
	block->size = insn.size;
	block->truncated = 1;

	/* Log calls' return addresses for return compression. */
	errcode = pt_blk_log_call(decoder, &insn, &iext);
	if (errcode < 0)
		return errcode;

	/* Let's see if we can proceed to the next IP without trace.
	 *
	 * The truncated instruction ends the block but we still need to get the
	 * next block's start IP.
	 */
	errcode = pt_insn_next_ip(&decoder->ip, &insn, &iext);
	if (errcode < 0) {
		if (errcode != -pte_bad_query)
			return errcode;

		return pt_blk_proceed_with_trace(decoder, &insn, &iext);
	}

	return 0;
}

/* Proceed to the next decision point using the block cache.
 *
 * Tracing is enabled and we don't have an event pending.  We already set
 * @block's isid.  All reads are done within @msec as we're not switching
 * sections between blocks.
 *
 * Proceed as far as we get without trace.  Stop when we either:
 *
 *   - need trace in order to continue
 *   - overflow the max number of instructions in a block
 *
 * We actually proceed one instruction further to get the start IP for the next
 * block.  This only updates @decoder's internal state, though.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int pt_blk_proceed_no_event_cached(struct pt_block_decoder *decoder,
					  struct pt_block *block,
					  struct pt_block_cache *bcache,
					  const struct pt_mapped_section *msec)
{
	struct pt_bcache_entry bce;
	uint16_t binsn, ninsn;
	uint64_t offset, nip;
	int status;

	if (!decoder || !block)
		return -pte_internal;

	offset = pt_msec_unmap(msec, decoder->ip);
	status = pt_bcache_lookup(&bce, bcache, offset);
	if (status < 0)
		return status;

	/* If we don't find a valid cache entry, fill the cache. */
	if (!pt_bce_is_valid(bce))
		return pt_blk_proceed_no_event_fill_cache(decoder, block,
							  bcache, msec,
							  bcache_fill_steps);

	/* If we switched sections, the origianl section must have been split
	 * underneath us.  A split preserves the block cache of the original
	 * section.
	 *
	 * Crossing sections requires ending the block so we can indicate the
	 * proper isid for the entire block.
	 *
	 * Plus there's the chance that the new section that caused the original
	 * section to split changed instructions.
	 *
	 * This check will also cover changes to a linear sequence of code we
	 * would otherwise have jumped over as long as the start and end are in
	 * different sub-sections.
	 *
	 * Since we stop on every (backwards) branch (through an artificial stop
	 * in the case of a near direct backward branch) we will detect all
	 * section splits.
	 *
	 * Switch to the slow path until we reach the end of this section.
	 */
	nip = decoder->ip + bce.displacement;
	if (!pt_blk_is_in_section(msec, nip))
		return pt_blk_proceed_no_event_uncached(decoder, block);

	/* We have a valid cache entry.  Let's first check if the way to the
	 * decision point still fits into @block.
	 *
	 * If it doesn't, we end the block without filling it as much as we
	 * could since this would require us to switch to the slow path.
	 *
	 * On the next iteration, we will start with an empty block, which is
	 * guaranteed to have enough room for at least one block cache entry.
	 */
	binsn = block->ninsn;
	ninsn = binsn + (uint16_t) bce.ninsn;
	if (ninsn < binsn)
		return 0;

	/* Jump ahead to the decision point and proceed from there.
	 *
	 * We're not switching execution modes so even if @block already has an
	 * execution mode, it will be the one we're going to set.
	 */
	decoder->ip = nip;

	/* We don't know the instruction class so we should be setting it to
	 * ptic_error.  Since we will be able to fill it back in later in most
	 * cases, we move the clearing to the switch cases that don't.
	 */
	block->end_ip = nip;
	block->ninsn = ninsn;
	block->mode = pt_bce_exec_mode(bce);


	switch (pt_bce_qualifier(bce)) {
	case ptbq_again:
		/* We're not able to reach the actual decision point due to
		 * overflows so we inserted a trampoline.
		 *
		 * We don't know the instruction and it is not guaranteed that
		 * we will proceed further (e.g. if @block overflowed).  Let's
		 * clear any previously stored instruction class which has
		 * become invalid when we updated @block->ninsn.
		 */
		block->iclass = ptic_error;

		return pt_blk_proceed_no_event_cached(decoder, block, bcache,
						      msec);

	case ptbq_cond:
		/* We're at a conditional branch. */
		block->iclass = ptic_cond_jump;

		/* Let's first check whether we know the size of the
		 * instruction.  If we do, we might get away without decoding
		 * the instruction.
		 *
		 * If we don't know the size we might as well do the full decode
		 * and proceed-with-trace flow we do for ptbq_decode.
		 */
		if (bce.isize) {
			uint64_t ip;
			int taken;

			/* If the branch is not taken, we don't need to decode
			 * the instruction at @decoder->ip.
			 *
			 * If it is taken, we have to implement everything here.
			 * We can't use the normal decode and proceed-with-trace
			 * flow since we already consumed the TNT bit.
			 */
			status = pt_blk_cond_branch(decoder, &taken);
			if (status < 0)
				return status;

			/* Preserve the query decoder's response which indicates
			 * upcoming events.
			 */
			decoder->status = status;

			ip = decoder->ip;
			if (taken) {
				struct pt_insn_ext iext;
				struct pt_insn insn;

				memset(&iext, 0, sizeof(iext));
				memset(&insn, 0, sizeof(insn));

				insn.mode = pt_bce_exec_mode(bce);
				insn.ip = ip;

				status = pt_blk_decode_in_section(&insn, &iext,
								  msec);
				if (status < 0)
					return status;

				ip += iext.variant.branch.displacement;
			}

			decoder->ip = ip + bce.isize;
			break;
		}

		fallthrough;
	case ptbq_decode: {
		struct pt_insn_ext iext;
		struct pt_insn insn;

		/* We need to decode the instruction at @decoder->ip and decide
		 * what to do based on that.
		 *
		 * We already accounted for the instruction so we can't just
		 * call pt_blk_proceed_one_insn().
		 */

		memset(&iext, 0, sizeof(iext));
		memset(&insn, 0, sizeof(insn));

		insn.mode = pt_bce_exec_mode(bce);
		insn.ip = decoder->ip;

		status = pt_blk_decode_in_section(&insn, &iext, msec);
		if (status < 0) {
			if (status != -pte_bad_insn)
				return status;

			return pt_blk_proceed_truncated(decoder, block);
		}

		/* We just decoded @insn so we know the instruction class. */
		block->iclass = insn.iclass;

		/* Log calls' return addresses for return compression. */
		status = pt_blk_log_call(decoder, &insn, &iext);
		if (status < 0)
			return status;

		/* Let's see if we can proceed to the next IP without trace.
		 *
		 * Note that we also stop due to displacement overflows or to
		 * maintain the return-address stack for near direct calls.
		 */
		status = pt_insn_next_ip(&decoder->ip, &insn, &iext);
		if (status < 0) {
			if (status != -pte_bad_query)
				return status;

			/* We can't, so let's proceed with trace, which
			 * completes the block.
			 */
			return pt_blk_proceed_with_trace(decoder, &insn, &iext);
		}

		/* End the block if the user asked us to.
		 *
		 * We only need to take care about direct near branches.
		 * Indirect and far branches require trace and will naturally
		 * end a block.
		 */
		if ((decoder->flags.variant.block.end_on_call &&
		     (insn.iclass == ptic_call)) ||
		    (decoder->flags.variant.block.end_on_jump &&
		     (insn.iclass == ptic_jump)))
			break;

		/* If we can proceed without trace and we stay in @msec we may
		 * proceed further.
		 *
		 * We're done if we switch sections, though.
		 */
		if (!pt_blk_is_in_section(msec, decoder->ip))
			break;

		return pt_blk_proceed_no_event_cached(decoder, block, bcache,
						      msec);
	}

	case ptbq_ind_call: {
		uint64_t ip;

		/* We're at a near indirect call. */
		block->iclass = ptic_call;

		/* We need to update the return-address stack and query the
		 * destination IP.
		 */
		ip = decoder->ip;

		/* If we already know the size of the instruction, we don't need
		 * to re-decode it.
		 */
		if (bce.isize)
			ip += bce.isize;
		else {
			struct pt_insn_ext iext;
			struct pt_insn insn;

			memset(&iext, 0, sizeof(iext));
			memset(&insn, 0, sizeof(insn));

			insn.mode = pt_bce_exec_mode(bce);
			insn.ip = ip;

			status = pt_blk_decode_in_section(&insn, &iext, msec);
			if (status < 0)
				return status;

			ip += insn.size;
		}

		status = pt_retstack_push(&decoder->retstack, ip);
		if (status < 0)
			return status;

		status = pt_blk_indirect_branch(decoder, &decoder->ip);
		if (status < 0)
			return status;

		/* Preserve the query decoder's response which indicates
		 * upcoming events.
		 */
		decoder->status = status;
		break;
	}

	case ptbq_return: {
		int taken;

		/* We're at a near return. */
		block->iclass = ptic_return;

		/* Check for a compressed return. */
		status = pt_blk_cond_branch(decoder, &taken);
		if (status < 0) {
			if (status != -pte_bad_query)
				return status;

			/* The return is not compressed.  We need another query
			 * to determine the destination IP.
			 */
			status = pt_blk_indirect_branch(decoder, &decoder->ip);
			if (status < 0)
				return status;

			/* Preserve the query decoder's response which indicates
			 * upcoming events.
			 */
			decoder->status = status;
			break;
		}

		/* Preserve the query decoder's response which indicates
		 * upcoming events.
		 */
		decoder->status = status;

		/* A compressed return is indicated by a taken conditional
		 * branch.
		 */
		if (!taken)
			return -pte_bad_retcomp;

		return pt_retstack_pop(&decoder->retstack, &decoder->ip);
	}

	case ptbq_indirect:
		/* We're at an indirect jump or far transfer.
		 *
		 * We don't know the exact instruction class and there's no
		 * reason to decode the instruction for any other purpose.
		 *
		 * Indicate that we don't know the instruction class and leave
		 * it to our caller to decode the instruction if needed.
		 */
		block->iclass = ptic_error;

		/* This is neither a near call nor return so we don't need to
		 * touch the return-address stack.
		 *
		 * Just query the destination IP.
		 */
		status = pt_blk_indirect_branch(decoder, &decoder->ip);
		if (status < 0)
			return status;

		/* Preserve the query decoder's response which indicates
		 * upcoming events.
		 */
		decoder->status = status;
		break;
	}

	return 0;
}

static int pt_blk_msec_fill(struct pt_block_decoder *decoder,
			    const struct pt_mapped_section **pmsec)
{
	const struct pt_mapped_section *msec;
	struct pt_section *section;
	int isid, errcode;

	if (!decoder || !pmsec)
		return -pte_internal;

	isid = pt_msec_cache_fill(&decoder->scache, &msec,  decoder->image,
				  &decoder->asid, decoder->ip);
	if (isid < 0)
		return isid;

	section = pt_msec_section(msec);
	if (!section)
		return -pte_internal;

	*pmsec = msec;

	errcode = pt_section_request_bcache(section);
	if (errcode < 0)
		return errcode;

	return isid;
}

static inline int pt_blk_msec_lookup(struct pt_block_decoder *decoder,
				     const struct pt_mapped_section **pmsec)
{
	int isid;

	if (!decoder)
		return -pte_internal;

	isid = pt_msec_cache_read(&decoder->scache, pmsec, decoder->image,
				  decoder->ip);
	if (isid < 0) {
		if (isid != -pte_nomap)
			return isid;

		return pt_blk_msec_fill(decoder, pmsec);
	}

	return isid;
}

/* Proceed to the next decision point - try using the cache.
 *
 * Tracing is enabled and we don't have an event pending.  Proceed as far as
 * we get without trace.  Stop when we either:
 *
 *   - need trace in order to continue
 *   - overflow the max number of instructions in a block
 *
 * We actually proceed one instruction further to get the start IP for the next
 * block.  This only updates @decoder's internal state, though.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int pt_blk_proceed_no_event(struct pt_block_decoder *decoder,
				   struct pt_block *block)
{
	const struct pt_mapped_section *msec;
	struct pt_block_cache *bcache;
	struct pt_section *section;
	int isid;

	if (!decoder || !block)
		return -pte_internal;

	isid = pt_blk_msec_lookup(decoder, &msec);
	if (isid < 0) {
		if (isid != -pte_nomap)
			return isid;

		/* Even if there is no such section in the image, we may still
		 * read the memory via the callback function.
		 */
		return pt_blk_proceed_no_event_uncached(decoder, block);
	}

	/* We do not switch sections inside a block. */
	if (isid != block->isid) {
		if (!pt_blk_block_is_empty(block))
			return 0;

		block->isid = isid;
	}

	section = pt_msec_section(msec);
	if (!section)
		return -pte_internal;

	bcache = pt_section_bcache(section);
	if (!bcache)
		return pt_blk_proceed_no_event_uncached(decoder, block);

	return pt_blk_proceed_no_event_cached(decoder, block, bcache, msec);
}

/* Proceed to the next event or decision point.
 *
 * Returns a non-negative pt_status_flag bit-vector on success, a negative error
 * code otherwise.
 */
static int pt_blk_proceed(struct pt_block_decoder *decoder,
			  struct pt_block *block)
{
	int status;

	status = pt_blk_fetch_event(decoder);
	if (status != 0) {
		if (status < 0)
			return status;

		return pt_blk_proceed_event(decoder, block);
	}

	/* If tracing is disabled we should either be out of trace or we should
	 * have taken the event flow above.
	 */
	if (!decoder->enabled) {
		if (decoder->status & pts_eos)
			return -pte_eos;

		return -pte_no_enable;
	}

	status = pt_blk_proceed_no_event(decoder, block);
	if (status < 0)
		return status;

	return pt_blk_proceed_trailing_event(decoder, block);
}

enum {
	/* The maximum number of steps to take when determining whether the
	 * event location can be reached.
	 */
	bdm64_max_steps	= 0x100
};

/* Try to work around erratum BDM64.
 *
 * If we got a transaction abort immediately following a branch that produced
 * trace, the trace for that branch might have been corrupted.
 *
 * Returns a positive integer if the erratum was handled.
 * Returns zero if the erratum does not seem to apply.
 * Returns a negative error code otherwise.
 */
static int pt_blk_handle_erratum_bdm64(struct pt_block_decoder *decoder,
				       const struct pt_block *block,
				       const struct pt_event *ev)
{
	struct pt_insn_ext iext;
	struct pt_insn insn;
	int status;

	if (!decoder || !block || !ev)
		return -pte_internal;

	/* This only affects aborts. */
	if (!ev->variant.tsx.aborted)
		return 0;

	/* This only affects branches that require trace.
	 *
	 * If the erratum hits, that branch ended the current block and brought
	 * us to the trailing event flow.
	 */
	if (pt_blk_block_is_empty(block))
		return 0;

	insn.mode = block->mode;
	insn.ip = block->end_ip;

	status = pt_insn_decode(&insn, &iext, decoder->image, &decoder->asid);
	if (status < 0)
		return 0;

	if (!pt_insn_is_branch(&insn, &iext))
		return 0;

	/* Let's check if we can reach the event location from here.
	 *
	 * If we can, let's assume the erratum did not hit.  We might still be
	 * wrong but we're not able to tell.
	 */
	status = pt_insn_range_is_contiguous(decoder->ip, ev->variant.tsx.ip,
					     decoder->mode, decoder->image,
					     &decoder->asid, bdm64_max_steps);
	if (status > 0)
		return status;

	/* We can't reach the event location.  This could either mean that we
	 * stopped too early (and status is zero) or that the erratum hit.
	 *
	 * We assume the latter and pretend that the previous branch brought us
	 * to the event location, instead.
	 */
	decoder->ip = ev->variant.tsx.ip;

	return 1;
}

/* Check whether a trailing TSX event should be postponed.
 *
 * This involves handling erratum BDM64.
 *
 * Returns a positive integer if the event is to be postponed.
 * Returns zero if the event should be processed.
 * Returns a negative error code otherwise.
 */
static inline int pt_blk_postpone_trailing_tsx(struct pt_block_decoder *decoder,
					       struct pt_block *block,
					       const struct pt_event *ev)
{
	int status;

	if (!decoder || !ev)
		return -pte_internal;

	if (ev->ip_suppressed)
		return 0;

	if (block && decoder->query.config.errata.bdm64) {
		status = pt_blk_handle_erratum_bdm64(decoder, block, ev);
		if (status < 0)
			return 1;
	}

	if (decoder->ip != ev->variant.tsx.ip)
		return 1;

	return 0;
}

/* Proceed with events that bind to the current decoder IP.
 *
 * This function is used in the following scenarios:
 *
 *   - we just synchronized onto the trace stream
 *   - we ended a block and proceeded to the next IP
 *   - we processed an event that was indicated by this function
 *
 * Check if there is an event at the current IP that needs to be indicated to
 * the user.
 *
 * Returns a non-negative pt_status_flag bit-vector on success, a negative error
 * code otherwise.
 */
static int pt_blk_proceed_trailing_event(struct pt_block_decoder *decoder,
					 struct pt_block *block)
{
	struct pt_event *ev;
	int status;

	if (!decoder)
		return -pte_internal;

	status = pt_blk_fetch_event(decoder);
	if (status <= 0) {
		if (status < 0)
			return status;

		status = pt_blk_proceed_postponed_insn(decoder);
		if (status < 0)
			return status;

		return pt_blk_status(decoder, 0);
	}

	ev = &decoder->event;
	switch (ev->type) {
	case ptev_disabled:
		/* Synchronous disable events are normally indicated on the
		 * event flow.
		 */
		if (!decoder->process_insn)
			break;

		/* A sync disable may bind to a CR3 changing instruction. */
		if (ev->ip_suppressed &&
		    pt_insn_changes_cr3(&decoder->insn, &decoder->iext))
			return pt_blk_status(decoder, pts_event_pending);

		/* Or it binds to the next branch that would require trace.
		 *
		 * Try to complete processing the current instruction by
		 * proceeding past it.  If that fails because it would require
		 * trace, we can apply the disabled event.
		 */
		status = pt_insn_next_ip(&decoder->ip, &decoder->insn,
					 &decoder->iext);
		if (status < 0) {
			if (status != -pte_bad_query)
				return status;

			status = pt_blk_set_disable_resume_ip(decoder,
							      &decoder->insn);
			if (status < 0)
				return status;

			return pt_blk_status(decoder, pts_event_pending);
		}

		/* We proceeded past the current instruction. */
		status = pt_blk_clear_postponed_insn(decoder);
		if (status < 0)
			return status;

		/* This might have brought us to the disable IP. */
		if (!ev->ip_suppressed &&
		    decoder->ip == ev->variant.disabled.ip)
			return pt_blk_status(decoder, pts_event_pending);

		break;

	case ptev_enabled:
		/* This event does not bind to an instruction. */
		status = pt_blk_proceed_postponed_insn(decoder);
		if (status < 0)
			return status;

		return pt_blk_status(decoder, pts_event_pending);

	case ptev_async_disabled:
		/* This event does not bind to an instruction. */
		status = pt_blk_proceed_postponed_insn(decoder);
		if (status < 0)
			return status;

		if (decoder->ip != ev->variant.async_disabled.at)
			break;

		if (decoder->query.config.errata.skd022) {
			status = pt_blk_handle_erratum_skd022(decoder, ev);
			if (status != 0) {
				if (status < 0)
					return status;

				/* If the erratum applies, the event is modified
				 * to a synchronous disable event that will be
				 * processed on the next pt_blk_proceed_event()
				 * call.  We're done.
				 */
				break;
			}
		}

		return pt_blk_status(decoder, pts_event_pending);

	case ptev_async_branch:
		/* This event does not bind to an instruction. */
		status = pt_blk_proceed_postponed_insn(decoder);
		if (status < 0)
			return status;

		if (decoder->ip != ev->variant.async_branch.from)
			break;

		return pt_blk_status(decoder, pts_event_pending);

	case ptev_paging:
		/* We apply the event immediately if we're not tracing. */
		if (!decoder->enabled)
			return pt_blk_status(decoder, pts_event_pending);

		/* Synchronous paging events are normally indicated on the event
		 * flow, unless they bind to the same instruction as a previous
		 * event.
		 *
		 * We bind at most one paging event to an instruction, though.
		 */
		if (!decoder->process_insn || decoder->bound_paging)
			break;

		/* We're done if we're not binding to the currently postponed
		 * instruction.  We will process the event on the normal event
		 * flow in the next iteration.
		 */
		if (!pt_insn_binds_to_pip(&decoder->insn, &decoder->iext))
			break;

		/* We bound a paging event.  Make sure we do not bind further
		 * paging events to this instruction.
		 */
		decoder->bound_paging = 1;

		return pt_blk_status(decoder, pts_event_pending);

	case ptev_async_paging:
		/* This event does not bind to an instruction. */
		status = pt_blk_proceed_postponed_insn(decoder);
		if (status < 0)
			return status;

		if (!ev->ip_suppressed &&
		    decoder->ip != ev->variant.async_paging.ip)
			break;

		return pt_blk_status(decoder, pts_event_pending);

	case ptev_vmcs:
		/* We apply the event immediately if we're not tracing. */
		if (!decoder->enabled)
			return pt_blk_status(decoder, pts_event_pending);

		/* Synchronous vmcs events are normally indicated on the event
		 * flow, unless they bind to the same instruction as a previous
		 * event.
		 *
		 * We bind at most one vmcs event to an instruction, though.
		 */
		if (!decoder->process_insn || decoder->bound_vmcs)
			break;

		/* We're done if we're not binding to the currently postponed
		 * instruction.  We will process the event on the normal event
		 * flow in the next iteration.
		 */
		if (!pt_insn_binds_to_vmcs(&decoder->insn, &decoder->iext))
			break;

		/* We bound a vmcs event.  Make sure we do not bind further vmcs
		 * events to this instruction.
		 */
		decoder->bound_vmcs = 1;

		return pt_blk_status(decoder, pts_event_pending);

	case ptev_async_vmcs:
		/* This event does not bind to an instruction. */
		status = pt_blk_proceed_postponed_insn(decoder);
		if (status < 0)
			return status;

		if (!ev->ip_suppressed &&
		    decoder->ip != ev->variant.async_vmcs.ip)
			break;

		return pt_blk_status(decoder, pts_event_pending);

	case ptev_overflow:
		/* This event does not bind to an instruction. */
		status = pt_blk_proceed_postponed_insn(decoder);
		if (status < 0)
			return status;

		return pt_blk_status(decoder, pts_event_pending);

	case ptev_exec_mode:
		/* This event does not bind to an instruction. */
		status = pt_blk_proceed_postponed_insn(decoder);
		if (status < 0)
			return status;

		if (!ev->ip_suppressed &&
		    decoder->ip != ev->variant.exec_mode.ip)
			break;

		return pt_blk_status(decoder, pts_event_pending);

	case ptev_tsx:
		/* This event does not bind to an instruction. */
		status = pt_blk_proceed_postponed_insn(decoder);
		if (status < 0)
			return status;

		status = pt_blk_postpone_trailing_tsx(decoder, block, ev);
		if (status != 0) {
			if (status < 0)
				return status;

			break;
		}

		return pt_blk_status(decoder, pts_event_pending);

	case ptev_stop:
		/* This event does not bind to an instruction. */
		status = pt_blk_proceed_postponed_insn(decoder);
		if (status < 0)
			return status;

		return pt_blk_status(decoder, pts_event_pending);

	case ptev_exstop:
		/* This event does not bind to an instruction. */
		status = pt_blk_proceed_postponed_insn(decoder);
		if (status < 0)
			return status;

		if (!ev->ip_suppressed && decoder->enabled &&
		    decoder->ip != ev->variant.exstop.ip)
			break;

		return pt_blk_status(decoder, pts_event_pending);

	case ptev_mwait:
		/* This event does not bind to an instruction. */
		status = pt_blk_proceed_postponed_insn(decoder);
		if (status < 0)
			return status;

		if (!ev->ip_suppressed && decoder->enabled &&
		    decoder->ip != ev->variant.mwait.ip)
			break;

		return pt_blk_status(decoder, pts_event_pending);

	case ptev_pwre:
	case ptev_pwrx:
		/* This event does not bind to an instruction. */
		status = pt_blk_proceed_postponed_insn(decoder);
		if (status < 0)
			return status;

		return pt_blk_status(decoder, pts_event_pending);

	case ptev_ptwrite:
		/* We apply the event immediately if we're not tracing. */
		if (!decoder->enabled)
			return pt_blk_status(decoder, pts_event_pending);

		/* Ptwrite events are normally indicated on the event flow,
		 * unless they bind to the same instruction as a previous event.
		 *
		 * We bind at most one ptwrite event to an instruction, though.
		 */
		if (!decoder->process_insn || decoder->bound_ptwrite)
			break;

		/* We're done if we're not binding to the currently postponed
		 * instruction.  We will process the event on the normal event
		 * flow in the next iteration.
		 */
		if (!ev->ip_suppressed ||
		    !pt_insn_is_ptwrite(&decoder->insn, &decoder->iext))
			break;

		/* We bound a ptwrite event.  Make sure we do not bind further
		 * ptwrite events to this instruction.
		 */
		decoder->bound_ptwrite = 1;

		return pt_blk_status(decoder, pts_event_pending);

	case ptev_tick:
	case ptev_cbr:
	case ptev_mnt:
		/* This event does not bind to an instruction. */
		status = pt_blk_proceed_postponed_insn(decoder);
		if (status < 0)
			return status;

		return pt_blk_status(decoder, pts_event_pending);
	}

	/* No further events.  Proceed past any postponed instruction. */
	status = pt_blk_proceed_postponed_insn(decoder);
	if (status < 0)
		return status;

	return pt_blk_status(decoder, 0);
}

int pt_blk_next(struct pt_block_decoder *decoder, struct pt_block *ublock,
		size_t size)
{
	struct pt_block block, *pblock;
	int errcode, status;

	if (!decoder || !ublock)
		return -pte_invalid;

	pblock = size == sizeof(block) ? ublock : &block;

	/* Zero-initialize the block in case of error returns. */
	memset(pblock, 0, sizeof(*pblock));

	/* Fill in a few things from the current decode state.
	 *
	 * This reflects the state of the last pt_blk_next() or pt_blk_start()
	 * call.  Note that, unless we stop with tracing disabled, we proceed
	 * already to the start IP of the next block.
	 *
	 * Some of the state may later be overwritten as we process events.
	 */
	pblock->ip = decoder->ip;
	pblock->mode = decoder->mode;
	if (decoder->speculative)
		pblock->speculative = 1;

	/* Proceed one block. */
	status = pt_blk_proceed(decoder, pblock);

	errcode = block_to_user(ublock, size, pblock);
	if (errcode < 0)
		return errcode;

	return status;
}

/* Process an enabled event.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int pt_blk_process_enabled(struct pt_block_decoder *decoder,
				  const struct pt_event *ev)
{
	if (!decoder || !ev)
		return -pte_internal;

	/* This event can't be a status update. */
	if (ev->status_update)
		return -pte_bad_context;

	/* We must have an IP in order to start decoding. */
	if (ev->ip_suppressed)
		return -pte_noip;

	/* We must currently be disabled. */
	if (decoder->enabled)
		return -pte_bad_context;

	decoder->ip = ev->variant.enabled.ip;
	decoder->enabled = 1;
	decoder->process_event = 0;

	return 0;
}

/* Process a disabled event.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int pt_blk_process_disabled(struct pt_block_decoder *decoder,
				   const struct pt_event *ev)
{
	if (!decoder || !ev)
		return -pte_internal;

	/* This event can't be a status update. */
	if (ev->status_update)
		return -pte_bad_context;

	/* We must currently be enabled. */
	if (!decoder->enabled)
		return -pte_bad_context;

	/* We preserve @decoder->ip.  This is where we expect tracing to resume
	 * and we'll indicate that on the subsequent enabled event if tracing
	 * actually does resume from there.
	 */
	decoder->enabled = 0;
	decoder->process_event = 0;

	return 0;
}

/* Process an asynchronous branch event.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int pt_blk_process_async_branch(struct pt_block_decoder *decoder,
				       const struct pt_event *ev)
{
	if (!decoder || !ev)
		return -pte_internal;

	/* This event can't be a status update. */
	if (ev->status_update)
		return -pte_bad_context;

	/* We must currently be enabled. */
	if (!decoder->enabled)
		return -pte_bad_context;

	/* Jump to the branch destination.  We will continue from there in the
	 * next iteration.
	 */
	decoder->ip = ev->variant.async_branch.to;
	decoder->process_event = 0;

	return 0;
}

/* Process a paging event.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int pt_blk_process_paging(struct pt_block_decoder *decoder,
				 const struct pt_event *ev)
{
	uint64_t cr3;
	int errcode;

	if (!decoder || !ev)
		return -pte_internal;

	cr3 = ev->variant.paging.cr3;
	if (decoder->asid.cr3 != cr3) {
		errcode = pt_msec_cache_invalidate(&decoder->scache);
		if (errcode < 0)
			return errcode;

		decoder->asid.cr3 = cr3;
	}

	decoder->process_event = 0;

	return 0;
}

/* Process a vmcs event.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int pt_blk_process_vmcs(struct pt_block_decoder *decoder,
			       const struct pt_event *ev)
{
	uint64_t vmcs;
	int errcode;

	if (!decoder || !ev)
		return -pte_internal;

	vmcs = ev->variant.vmcs.base;
	if (decoder->asid.vmcs != vmcs) {
		errcode = pt_msec_cache_invalidate(&decoder->scache);
		if (errcode < 0)
			return errcode;

		decoder->asid.vmcs = vmcs;
	}

	decoder->process_event = 0;

	return 0;
}

/* Process an overflow event.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int pt_blk_process_overflow(struct pt_block_decoder *decoder,
				   const struct pt_event *ev)
{
	if (!decoder || !ev)
		return -pte_internal;

	/* This event can't be a status update. */
	if (ev->status_update)
		return -pte_bad_context;

	/* If the IP is suppressed, the overflow resolved while tracing was
	 * disabled.  Otherwise it resolved while tracing was enabled.
	 */
	if (ev->ip_suppressed) {
		/* Tracing is disabled.  It doesn't make sense to preserve the
		 * previous IP.  This will just be misleading.  Even if tracing
		 * had been disabled before, as well, we might have missed the
		 * re-enable in the overflow.
		 */
		decoder->enabled = 0;
		decoder->ip = 0ull;
	} else {
		/* Tracing is enabled and we're at the IP at which the overflow
		 * resolved.
		 */
		decoder->enabled = 1;
		decoder->ip = ev->variant.overflow.ip;
	}

	/* We don't know the TSX state.  Let's assume we execute normally.
	 *
	 * We also don't know the execution mode.  Let's keep what we have
	 * in case we don't get an update before we have to decode the next
	 * instruction.
	 */
	decoder->speculative = 0;
	decoder->process_event = 0;

	return 0;
}

/* Process an exec mode event.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int pt_blk_process_exec_mode(struct pt_block_decoder *decoder,
				    const struct pt_event *ev)
{
	enum pt_exec_mode mode;

	if (!decoder || !ev)
		return -pte_internal;

	/* Use status update events to diagnose inconsistencies. */
	mode = ev->variant.exec_mode.mode;
	if (ev->status_update && decoder->enabled &&
	    decoder->mode != ptem_unknown && decoder->mode != mode)
		return -pte_bad_status_update;

	decoder->mode = mode;
	decoder->process_event = 0;

	return 0;
}

/* Process a tsx event.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int pt_blk_process_tsx(struct pt_block_decoder *decoder,
			      const struct pt_event *ev)
{
	if (!decoder || !ev)
		return -pte_internal;

	decoder->speculative = ev->variant.tsx.speculative;
	decoder->process_event = 0;

	return 0;
}

/* Process a stop event.
 *
 * Returns zero on success, a negative error code otherwise.
 */
static int pt_blk_process_stop(struct pt_block_decoder *decoder,
			       const struct pt_event *ev)
{
	if (!decoder || !ev)
		return -pte_internal;

	/* This event can't be a status update. */
	if (ev->status_update)
		return -pte_bad_context;

	/* Tracing is always disabled before it is stopped. */
	if (decoder->enabled)
		return -pte_bad_context;

	decoder->process_event = 0;

	return 0;
}

int pt_blk_event(struct pt_block_decoder *decoder, struct pt_event *uevent,
		 size_t size)
{
	struct pt_event *ev;
	int status;

	if (!decoder || !uevent)
		return -pte_invalid;

	/* We must currently process an event. */
	if (!decoder->process_event)
		return -pte_bad_query;

	ev = &decoder->event;
	switch (ev->type) {
	case ptev_enabled:
		/* Indicate that tracing resumes from the IP at which tracing
		 * had been disabled before (with some special treatment for
		 * calls).
		 */
		if (ev->variant.enabled.ip == decoder->ip)
			ev->variant.enabled.resumed = 1;

		status = pt_blk_process_enabled(decoder, ev);
		if (status < 0)
			return status;

		break;

	case ptev_async_disabled:
		if (decoder->ip != ev->variant.async_disabled.at)
			return -pte_bad_query;

		fallthrough;
	case ptev_disabled:

		status = pt_blk_process_disabled(decoder, ev);
		if (status < 0)
			return status;

		break;

	case ptev_async_branch:
		if (decoder->ip != ev->variant.async_branch.from)
			return -pte_bad_query;

		status = pt_blk_process_async_branch(decoder, ev);
		if (status < 0)
			return status;

		break;

	case ptev_async_paging:
		if (!ev->ip_suppressed &&
		    decoder->ip != ev->variant.async_paging.ip)
			return -pte_bad_query;

		fallthrough;
	case ptev_paging:
		status = pt_blk_process_paging(decoder, ev);
		if (status < 0)
			return status;

		break;

	case ptev_async_vmcs:
		if (!ev->ip_suppressed &&
		    decoder->ip != ev->variant.async_vmcs.ip)
			return -pte_bad_query;

		fallthrough;
	case ptev_vmcs:
		status = pt_blk_process_vmcs(decoder, ev);
		if (status < 0)
			return status;

		break;

	case ptev_overflow:
		status = pt_blk_process_overflow(decoder, ev);
		if (status < 0)
			return status;

		break;

	case ptev_exec_mode:
		if (!ev->ip_suppressed &&
		    decoder->ip != ev->variant.exec_mode.ip)
			return -pte_bad_query;

		status = pt_blk_process_exec_mode(decoder, ev);
		if (status < 0)
			return status;

		break;

	case ptev_tsx:
		if (!ev->ip_suppressed && decoder->ip != ev->variant.tsx.ip)
			return -pte_bad_query;

		status = pt_blk_process_tsx(decoder, ev);
		if (status < 0)
			return status;

		break;

	case ptev_stop:
		status = pt_blk_process_stop(decoder, ev);
		if (status < 0)
			return status;

		break;

	case ptev_exstop:
		if (!ev->ip_suppressed && decoder->enabled &&
		    decoder->ip != ev->variant.exstop.ip)
			return -pte_bad_query;

		decoder->process_event = 0;
		break;

	case ptev_mwait:
		if (!ev->ip_suppressed && decoder->enabled &&
		    decoder->ip != ev->variant.mwait.ip)
			return -pte_bad_query;

		decoder->process_event = 0;
		break;

	case ptev_pwre:
	case ptev_pwrx:
	case ptev_ptwrite:
	case ptev_tick:
	case ptev_cbr:
	case ptev_mnt:
		decoder->process_event = 0;
		break;
	}

	/* Copy the event to the user.  Make sure we're not writing beyond the
	 * memory provided by the user.
	 *
	 * We might truncate details of an event but only for those events the
	 * user can't know about, anyway.
	 */
	if (sizeof(*ev) < size)
		size = sizeof(*ev);

	memcpy(uevent, ev, size);

	/* Indicate further events. */
	return pt_blk_proceed_trailing_event(decoder, NULL);
}
