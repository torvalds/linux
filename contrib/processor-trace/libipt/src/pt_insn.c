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

#include "pt_insn.h"
#include "pt_ild.h"
#include "pt_image.h"
#include "pt_compiler.h"

#include "intel-pt.h"


int pt_insn_changes_cpl(const struct pt_insn *insn,
			const struct pt_insn_ext *iext)
{
	(void) insn;

	if (!iext)
		return 0;

	switch (iext->iclass) {
	default:
		return 0;

	case PTI_INST_INT:
	case PTI_INST_INT3:
	case PTI_INST_INT1:
	case PTI_INST_INTO:
	case PTI_INST_IRET:
	case PTI_INST_SYSCALL:
	case PTI_INST_SYSENTER:
	case PTI_INST_SYSEXIT:
	case PTI_INST_SYSRET:
		return 1;
	}
}

int pt_insn_changes_cr3(const struct pt_insn *insn,
			const struct pt_insn_ext *iext)
{
	(void) insn;

	if (!iext)
		return 0;

	switch (iext->iclass) {
	default:
		return 0;

	case PTI_INST_MOV_CR3:
		return 1;
	}
}

int pt_insn_is_branch(const struct pt_insn *insn,
		      const struct pt_insn_ext *iext)
{
	(void) iext;

	if (!insn)
		return 0;

	switch (insn->iclass) {
	default:
		return 0;

	case ptic_call:
	case ptic_return:
	case ptic_jump:
	case ptic_cond_jump:
	case ptic_far_call:
	case ptic_far_return:
	case ptic_far_jump:
		return 1;
	}
}

int pt_insn_is_far_branch(const struct pt_insn *insn,
			  const struct pt_insn_ext *iext)
{
	(void) iext;

	if (!insn)
		return 0;

	switch (insn->iclass) {
	default:
		return 0;

	case ptic_far_call:
	case ptic_far_return:
	case ptic_far_jump:
		return 1;
	}
}

int pt_insn_binds_to_pip(const struct pt_insn *insn,
			 const struct pt_insn_ext *iext)
{
	if (!iext)
		return 0;

	switch (iext->iclass) {
	default:
		return pt_insn_is_far_branch(insn, iext);

	case PTI_INST_MOV_CR3:
	case PTI_INST_VMLAUNCH:
	case PTI_INST_VMRESUME:
		return 1;
	}
}

int pt_insn_binds_to_vmcs(const struct pt_insn *insn,
			  const struct pt_insn_ext *iext)
{
	if (!iext)
		return 0;

	switch (iext->iclass) {
	default:
		return pt_insn_is_far_branch(insn, iext);

	case PTI_INST_VMPTRLD:
	case PTI_INST_VMLAUNCH:
	case PTI_INST_VMRESUME:
		return 1;
	}
}

int pt_insn_is_ptwrite(const struct pt_insn *insn,
		       const struct pt_insn_ext *iext)
{
	(void) iext;

	if (!insn)
		return 0;

	switch (insn->iclass) {
	default:
		return 0;

	case ptic_ptwrite:
		return 1;
	}
}

int pt_insn_next_ip(uint64_t *pip, const struct pt_insn *insn,
		    const struct pt_insn_ext *iext)
{
	uint64_t ip;

	if (!insn || !iext)
		return -pte_internal;

	ip = insn->ip + insn->size;

	switch (insn->iclass) {
	case ptic_ptwrite:
	case ptic_other:
		break;

	case ptic_call:
	case ptic_jump:
		if (iext->variant.branch.is_direct) {
			ip += iext->variant.branch.displacement;
			break;
		}

		fallthrough;
	default:
		return -pte_bad_query;

	case ptic_error:
		return -pte_bad_insn;
	}

	if (pip)
		*pip = ip;

	return 0;
}

/* Retry decoding an instruction after a preceding decode error.
 *
 * Instruction length decode typically fails due to 'not enough
 * bytes'.
 *
 * This may be caused by partial updates of text sections
 * represented via new image sections overlapping the original
 * text section's image section.  We stop reading memory at the
 * end of the section so we do not read the full instruction if
 * parts of it have been overwritten by the update.
 *
 * Try to read the remaining bytes and decode the instruction again.  If we
 * succeed, set @insn->truncated to indicate that the instruction is truncated
 * in @insn->isid.
 *
 * Returns zero on success, a negative error code otherwise.
 * Returns -pte_bad_insn if the instruction could not be decoded.
 */
static int pt_insn_decode_retry(struct pt_insn *insn, struct pt_insn_ext *iext,
				struct pt_image *image,
				const struct pt_asid *asid)
{
	int size, errcode, isid;
	uint8_t isize, remaining;

	if (!insn)
		return -pte_internal;

	isize = insn->size;
	remaining = sizeof(insn->raw) - isize;

	/* We failed for real if we already read the maximum number of bytes for
	 * an instruction.
	 */
	if (!remaining)
		return -pte_bad_insn;

	/* Read the remaining bytes from the image. */
	size = pt_image_read(image, &isid, &insn->raw[isize], remaining, asid,
			     insn->ip + isize);
	if (size <= 0) {
		/* We should have gotten an error if we were not able to read at
		 * least one byte.  Check this to guarantee termination.
		 */
		if (!size)
			return -pte_internal;

		/* Preserve the original error if there are no more bytes. */
		if (size == -pte_nomap)
			size = -pte_bad_insn;

		return size;
	}

	/* Add the newly read bytes to the instruction's size. */
	insn->size += (uint8_t) size;

	/* Store the new size to avoid infinite recursion in case instruction
	 * decode fails after length decode, which would set @insn->size to the
	 * actual length.
	 */
	size = insn->size;

	/* Try to decode the instruction again.
	 *
	 * If we fail again, we recursively retry again until we either fail to
	 * read more bytes or reach the maximum number of bytes for an
	 * instruction.
	 */
	errcode = pt_ild_decode(insn, iext);
	if (errcode < 0) {
		if (errcode != -pte_bad_insn)
			return errcode;

		/* If instruction length decode already determined the size,
		 * there's no point in reading more bytes.
		 */
		if (insn->size != (uint8_t) size)
			return errcode;

		return pt_insn_decode_retry(insn, iext, image, asid);
	}

	/* We succeeded this time, so the instruction crosses image section
	 * boundaries.
	 *
	 * This poses the question which isid to use for the instruction.
	 *
	 * To reconstruct exactly this instruction at a later time, we'd need to
	 * store all isids involved together with the number of bytes read for
	 * each isid.  Since @insn already provides the exact bytes for this
	 * instruction, we assume that the isid will be used solely for source
	 * correlation.  In this case, it should refer to the first byte of the
	 * instruction - as it already does.
	 */
	insn->truncated = 1;

	return errcode;
}

int pt_insn_decode(struct pt_insn *insn, struct pt_insn_ext *iext,
		   struct pt_image *image, const struct pt_asid *asid)
{
	int size, errcode;

	if (!insn)
		return -pte_internal;

	/* Read the memory at the current IP in the current address space. */
	size = pt_image_read(image, &insn->isid, insn->raw, sizeof(insn->raw),
			     asid, insn->ip);
	if (size < 0)
		return size;

	/* We initialize @insn->size to the maximal possible size.  It will be
	 * set to the actual size during instruction decode.
	 */
	insn->size = (uint8_t) size;

	errcode = pt_ild_decode(insn, iext);
	if (errcode < 0) {
		if (errcode != -pte_bad_insn)
			return errcode;

		/* If instruction length decode already determined the size,
		 * there's no point in reading more bytes.
		 */
		if (insn->size != (uint8_t) size)
			return errcode;

		return pt_insn_decode_retry(insn, iext, image, asid);
	}

	return errcode;
}

int pt_insn_range_is_contiguous(uint64_t begin, uint64_t end,
				enum pt_exec_mode mode, struct pt_image *image,
				const struct pt_asid *asid, size_t steps)
{
	struct pt_insn_ext iext;
	struct pt_insn insn;

	memset(&insn, 0, sizeof(insn));

	insn.mode = mode;
	insn.ip = begin;

	while (insn.ip != end) {
		int errcode;

		if (!steps--)
			return 0;

		errcode = pt_insn_decode(&insn, &iext, image, asid);
		if (errcode < 0)
			return errcode;

		errcode = pt_insn_next_ip(&insn.ip, &insn, &iext);
		if (errcode < 0)
			return errcode;
	}

	return 1;
}
