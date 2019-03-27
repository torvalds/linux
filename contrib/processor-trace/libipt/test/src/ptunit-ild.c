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

#include "ptunit.h"

#include "pt_ild.h"

#include <string.h>


/* Check that an instruction is decoded correctly. */
static struct ptunit_result ptunit_ild_decode(uint8_t *raw, uint8_t size,
					      enum pt_exec_mode mode)
{
	struct pt_insn_ext iext;
	struct pt_insn insn;
	int errcode;

	memset(&iext, 0, sizeof(iext));
	memset(&insn, 0, sizeof(insn));

	memcpy(insn.raw, raw, size);
	insn.size = size;
	insn.mode = mode;

	errcode = pt_ild_decode(&insn, &iext);
	ptu_int_eq(errcode, 0);

	ptu_uint_eq(insn.size, size);
	ptu_int_eq(insn.iclass, ptic_other);
	ptu_int_eq(iext.iclass, PTI_INST_INVALID);

	return ptu_passed();
}

/* Check that an instruction is decoded and classified correctly. */
static struct ptunit_result ptunit_ild_classify(uint8_t *raw, uint8_t size,
						enum pt_exec_mode mode,
						pti_inst_enum_t iclass)
{
	struct pt_insn_ext iext;
	struct pt_insn insn;
	int errcode;

	memset(&iext, 0, sizeof(iext));
	memset(&insn, 0, sizeof(insn));

	memcpy(insn.raw, raw, size);
	insn.size = size;
	insn.mode = mode;

	errcode = pt_ild_decode(&insn, &iext);
	ptu_int_eq(errcode, 0);

	ptu_uint_eq(insn.size, size);
	ptu_int_eq(iext.iclass, iclass);

	return ptu_passed();
}

/* Check that an invalid instruction is detected correctly.
 *
 * Note that we intentionally do not detect all invalid instructions.  This test
 * therefore only covers some that we care about.
 */
static struct ptunit_result ptunit_ild_invalid(uint8_t *raw, uint8_t size,
					       enum pt_exec_mode mode)
{
	struct pt_insn_ext iext;
	struct pt_insn insn;
	int errcode;

	memset(&iext, 0, sizeof(iext));
	memset(&insn, 0, sizeof(insn));

	memcpy(insn.raw, raw, size);
	insn.size = size;
	insn.mode = mode;

	errcode = pt_ild_decode(&insn, &iext);
	ptu_int_eq(errcode, -pte_bad_insn);

	return ptu_passed();
}


/* Macros to automatically update the test location. */
#define ptu_decode(insn, size, mode)		\
	ptu_check(ptunit_ild_decode, insn, size, mode)

#define ptu_classify(insn, size, mode, iclass)			\
	ptu_check(ptunit_ild_classify, insn, size, mode, iclass)

/* Macros to also automatically supply the instruction size. */
#define ptu_decode_s(insn, mode)			\
	ptu_decode(insn, sizeof(insn), mode)

#define ptu_classify_s(insn, mode, iclass)		\
	ptu_classify(insn, sizeof(insn), mode, iclass)

#define ptu_invalid_s(insn, mode)				\
	ptu_check(ptunit_ild_invalid, insn, sizeof(insn), mode)


static struct ptunit_result push(void)
{
	uint8_t insn[] = { 0x68, 0x11, 0x22, 0x33, 0x44 };

	ptu_decode_s(insn, ptem_64bit);

	return ptu_passed();
}

static struct ptunit_result jmp_rel(void)
{
	uint8_t insn[] = { 0xE9, 0x60, 0xF9, 0xFF, 0xFF };

	ptu_classify_s(insn, ptem_64bit, PTI_INST_JMP_E9);

	return ptu_passed();
}

static struct ptunit_result long_nop(void)
{
	uint8_t insn[] = { 0x66, 0x66, 0x66, 0x66,
			       0x66, 0x66, 0X2E, 0X0F,
			       0X1F, 0x84, 0x00, 0x00,
			       0x00, 0x00, 0x00 };

	ptu_decode_s(insn, ptem_64bit);

	return ptu_passed();
}

static struct ptunit_result mov_al_64(void)
{
	uint8_t insn[] = { 0x48, 0xa0, 0x3f, 0xaa, 0xbb, 0xcc, 0xdd, 0xee,
			   0xff, 0x11 };

	ptu_decode_s(insn, ptem_64bit);

	return ptu_passed();
}

static struct ptunit_result mov_al_32_em64(void)
{
	uint8_t insn[] = { 0x67, 0xa0, 0x3f, 0xaa, 0xbb, 0xcc, 0xdd, 0xee,
			   0xff, 0X11 };

	ptu_decode(insn, 6, ptem_64bit);

	return ptu_passed();
}

static struct ptunit_result mov_al_32(void)
{
	uint8_t insn[] = { 0xa0, 0x3f, 0xaa, 0xbb, 0xcc, 0xdd, 0xee };

	ptu_decode(insn, 5, ptem_32bit);

	return ptu_passed();
}

static struct ptunit_result mov_al_32_em16(void)
{
	uint8_t insn[] = { 0x67, 0xa0, 0x3f, 0xaa, 0xbb, 0xcc, 0xdd, 0xee };

	ptu_decode(insn, 6, ptem_16bit);

	return ptu_passed();
}

static struct ptunit_result mov_al_16_em32(void)
{
	uint8_t insn[] = { 0x67, 0xa0, 0x3f, 0xaa, 0xbb, 0xcc, 0xdd, 0xee };

	ptu_decode(insn, 4, ptem_32bit);

	return ptu_passed();
}

static struct ptunit_result mov_al_16(void)
{
	uint8_t insn[] = { 0xa0, 0x3f, 0xaa, 0xbb, 0xcc, 0xdd, 0xee };

	ptu_decode(insn, 3, ptem_16bit);

	return ptu_passed();
}

static struct ptunit_result rdtsc(void)
{
	uint8_t insn[] = { 0x0f, 0x31 };

	ptu_decode_s(insn, ptem_64bit);

	return ptu_passed();
}

static struct ptunit_result pcmpistri(void)
{
	uint8_t insn[] = { 0x66, 0x0f, 0x3a, 0x63, 0x04, 0x16, 0x1a };

	ptu_decode_s(insn, ptem_64bit);

	return ptu_passed();
}

static struct ptunit_result vmovdqa(void)
{
	uint8_t insn[] = { 0xc5, 0xf9, 0x6f, 0x25, 0xa9, 0x55, 0x04, 0x00 };

	ptu_decode_s(insn, ptem_64bit);

	return ptu_passed();
}

static struct ptunit_result vpandn(void)
{
	uint8_t insn[] = { 0xc4, 0x41, 0x29, 0xdf, 0xd1 };

	ptu_decode_s(insn, ptem_64bit);

	return ptu_passed();
}

static struct ptunit_result syscall(void)
{
	uint8_t insn[] = { 0x0f, 0x05 };

	ptu_classify_s(insn, ptem_64bit, PTI_INST_SYSCALL);

	return ptu_passed();
}

static struct ptunit_result sysret(void)
{
	uint8_t insn[] = { 0x0f, 0x07 };

	ptu_classify_s(insn, ptem_64bit, PTI_INST_SYSRET);

	return ptu_passed();
}

static struct ptunit_result sysenter(void)
{
	uint8_t insn[] = { 0x0f, 0x34 };

	ptu_classify_s(insn, ptem_64bit, PTI_INST_SYSENTER);

	return ptu_passed();
}

static struct ptunit_result sysexit(void)
{
	uint8_t insn[] = { 0x0f, 0x35 };

	ptu_classify_s(insn, ptem_64bit, PTI_INST_SYSEXIT);

	return ptu_passed();
}

static struct ptunit_result int3(void)
{
	uint8_t insn[] = { 0xcc };

	ptu_classify_s(insn, ptem_64bit, PTI_INST_INT3);

	return ptu_passed();
}

static struct ptunit_result intn(void)
{
	uint8_t insn[] = { 0xcd, 0x06 };

	ptu_classify_s(insn, ptem_64bit, PTI_INST_INT);

	return ptu_passed();
}

static struct ptunit_result iret(void)
{
	uint8_t insn[] = { 0xcf };

	ptu_classify_s(insn, ptem_64bit, PTI_INST_IRET);

	return ptu_passed();
}

static struct ptunit_result call_9a_cd(void)
{
	uint8_t insn[] = { 0x9a, 0x00, 0x00, 0x00, 0x00 };

	ptu_classify_s(insn, ptem_16bit, PTI_INST_CALL_9A);

	return ptu_passed();
}

static struct ptunit_result call_9a_cp(void)
{
	uint8_t insn[] = { 0x9a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	ptu_classify_s(insn, ptem_32bit, PTI_INST_CALL_9A);

	return ptu_passed();
}

static struct ptunit_result call_ff_3(void)
{
	uint8_t insn[] = { 0xff, 0x1c, 0x25, 0x00, 0x00, 0x00, 0x00 };

	ptu_classify_s(insn, ptem_64bit, PTI_INST_CALL_FFr3);

	return ptu_passed();
}

static struct ptunit_result jmp_ff_5(void)
{
	uint8_t insn[] = { 0xff, 0x2c, 0x25, 0x00, 0x00, 0x00, 0x00 };

	ptu_classify_s(insn, ptem_64bit, PTI_INST_JMP_FFr5);

	return ptu_passed();
}

static struct ptunit_result jmp_ea_cd(void)
{
	uint8_t insn[] = { 0xea, 0x00, 0x00, 0x00, 0x00 };

	ptu_classify_s(insn, ptem_16bit, PTI_INST_JMP_EA);

	return ptu_passed();
}

static struct ptunit_result jmp_ea_cp(void)
{
	uint8_t insn[] = { 0xea, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	ptu_classify_s(insn, ptem_32bit, PTI_INST_JMP_EA);

	return ptu_passed();
}

static struct ptunit_result ret_ca(void)
{
	uint8_t insn[] = { 0xca, 0x00, 0x00 };

	ptu_classify_s(insn, ptem_64bit, PTI_INST_RET_CA);

	return ptu_passed();
}

static struct ptunit_result vmlaunch(void)
{
	uint8_t insn[] = { 0x0f, 0x01, 0xc2 };

	ptu_classify_s(insn, ptem_64bit, PTI_INST_VMLAUNCH);

	return ptu_passed();
}

static struct ptunit_result vmresume(void)
{
	uint8_t insn[] = { 0x0f, 0x01, 0xc3 };

	ptu_classify_s(insn, ptem_64bit, PTI_INST_VMRESUME);

	return ptu_passed();
}

static struct ptunit_result vmcall(void)
{
	uint8_t insn[] = { 0x0f, 0x01, 0xc1 };

	ptu_classify_s(insn, ptem_64bit, PTI_INST_VMCALL);

	return ptu_passed();
}

static struct ptunit_result vmptrld(void)
{
	uint8_t insn[] = { 0x0f, 0xc7, 0x30 };

	ptu_classify_s(insn, ptem_64bit, PTI_INST_VMPTRLD);

	return ptu_passed();
}

static struct ptunit_result jrcxz(void)
{
	uint8_t insn[] = { 0xe3, 0x00 };

	ptu_classify_s(insn, ptem_64bit, PTI_INST_JrCXZ);

	return ptu_passed();
}

static struct ptunit_result mov_eax_moffs64(void)
{
	uint8_t insn[] = { 0xa1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			   0x00 };

	ptu_decode_s(insn, ptem_64bit);

	return ptu_passed();
}

static struct ptunit_result mov_eax_moffs64_32(void)
{
	uint8_t insn[] = { 0x67, 0xa1, 0x00, 0x00, 0x00, 0x00 };

	ptu_decode_s(insn, ptem_64bit);

	return ptu_passed();
}

static struct ptunit_result mov_rax_moffs64(void)
{
	uint8_t insn[] = { 0x48, 0xa1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			   0x00, 0x00 };

	ptu_decode_s(insn, ptem_64bit);

	return ptu_passed();
}

static struct ptunit_result mov_rax_moffs64_32(void)
{
	uint8_t insn[] = { 0x67, 0x48, 0xa1, 0x00, 0x00, 0x00, 0x00 };

	ptu_decode_s(insn, ptem_64bit);

	return ptu_passed();
}

static struct ptunit_result mov_ax_moffs64(void)
{
	uint8_t insn[] = { 0x66, 0xa1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
			   0x00, 0x00 };

	ptu_decode_s(insn, ptem_64bit);

	return ptu_passed();
}

static struct ptunit_result mov_ax_moffs64_32(void)
{
	uint8_t insn[] = { 0x67, 0x66, 0xa1, 0x00, 0x00, 0x00, 0x00 };

	ptu_decode_s(insn, ptem_64bit);

	return ptu_passed();
}

static struct ptunit_result mov_eax_moffs32(void)
{
	uint8_t insn[] = { 0xa1, 0x00, 0x00, 0x00, 0x00 };

	ptu_decode_s(insn, ptem_32bit);

	return ptu_passed();
}

static struct ptunit_result mov_ax_moffs32(void)
{
	uint8_t insn[] = { 0x66, 0xa1, 0x00, 0x00, 0x00, 0x00 };

	ptu_decode_s(insn, ptem_32bit);

	return ptu_passed();
}

static struct ptunit_result mov_ax_moffs16(void)
{
	uint8_t insn[] = { 0xa1, 0x00, 0x00 };

	ptu_decode_s(insn, ptem_16bit);

	return ptu_passed();
}

static struct ptunit_result les(void)
{
	uint8_t insn[] = { 0xc4, 0x00 };

	ptu_decode_s(insn, ptem_16bit);
	ptu_decode_s(insn, ptem_32bit);

	return ptu_passed();
}

static struct ptunit_result les_disp16(void)
{
	uint8_t insn[] = { 0xc4, 0x06, 0x00, 0x00 };

	ptu_decode_s(insn, ptem_16bit);

	return ptu_passed();
}

static struct ptunit_result les_disp32(void)
{
	uint8_t insn[] = { 0xc4, 0x05, 0x00, 0x00, 0x00, 0x00 };

	ptu_decode_s(insn, ptem_32bit);

	return ptu_passed();
}

static struct ptunit_result les_ind_disp8(void)
{
	uint8_t insn[] = { 0xc4, 0x40, 0x00 };

	ptu_decode_s(insn, ptem_16bit);
	ptu_decode_s(insn, ptem_32bit);

	return ptu_passed();
}

static struct ptunit_result les_ind_disp16(void)
{
	uint8_t insn[] = { 0xc4, 0x80, 0x00, 0x00 };

	ptu_decode_s(insn, ptem_16bit);

	return ptu_passed();
}

static struct ptunit_result les_ind_disp32(void)
{
	uint8_t insn[] = { 0xc4, 0x80, 0x00, 0x00, 0x00, 0x00 };

	ptu_decode_s(insn, ptem_32bit);

	return ptu_passed();
}

static struct ptunit_result lds(void)
{
	uint8_t insn[] = { 0xc5, 0x00 };

	ptu_decode_s(insn, ptem_16bit);
	ptu_decode_s(insn, ptem_32bit);

	return ptu_passed();
}

static struct ptunit_result lds_disp16(void)
{
	uint8_t insn[] = { 0xc5, 0x06, 0x00, 0x00 };

	ptu_decode_s(insn, ptem_16bit);

	return ptu_passed();
}

static struct ptunit_result lds_disp32(void)
{
	uint8_t insn[] = { 0xc5, 0x05, 0x00, 0x00, 0x00, 0x00 };

	ptu_decode_s(insn, ptem_32bit);

	return ptu_passed();
}

static struct ptunit_result lds_ind_disp8(void)
{
	uint8_t insn[] = { 0xc5, 0x40, 0x00 };

	ptu_decode_s(insn, ptem_16bit);
	ptu_decode_s(insn, ptem_32bit);

	return ptu_passed();
}

static struct ptunit_result lds_ind_disp16(void)
{
	uint8_t insn[] = { 0xc5, 0x80, 0x00, 0x00 };

	ptu_decode_s(insn, ptem_16bit);

	return ptu_passed();
}

static struct ptunit_result lds_ind_disp32(void)
{
	uint8_t insn[] = { 0xc5, 0x80, 0x00, 0x00, 0x00, 0x00 };

	ptu_decode_s(insn, ptem_32bit);

	return ptu_passed();
}

static struct ptunit_result vpshufb(void)
{
	uint8_t insn[] = { 0x62, 0x02, 0x05, 0x00, 0x00, 0x00 };

	ptu_decode_s(insn, ptem_64bit);

	return ptu_passed();
}

static struct ptunit_result bound(void)
{
	uint8_t insn[] = { 0x62, 0x02 };

	ptu_decode_s(insn, ptem_32bit);
	ptu_decode_s(insn, ptem_16bit);

	return ptu_passed();
}

static struct ptunit_result evex_cutoff(void)
{
	uint8_t insn[] = { 0x62 };

	ptu_invalid_s(insn, ptem_64bit);
	ptu_invalid_s(insn, ptem_32bit);
	ptu_invalid_s(insn, ptem_16bit);

	return ptu_passed();
}

static struct ptunit_result ptwrite_r32(void)
{
	uint8_t insn[] = { 0xf3, 0x0f, 0xae, 0xe7 };

	ptu_classify_s(insn, ptem_64bit, PTI_INST_PTWRITE);
	ptu_classify_s(insn, ptem_32bit, PTI_INST_PTWRITE);
	ptu_classify_s(insn, ptem_16bit, PTI_INST_PTWRITE);

	return ptu_passed();
}

static struct ptunit_result ptwrite_m32(void)
{
	uint8_t insn[] = { 0xf3, 0x0f, 0xae, 0x67, 0xcc };

	ptu_classify_s(insn, ptem_64bit, PTI_INST_PTWRITE);
	ptu_classify_s(insn, ptem_32bit, PTI_INST_PTWRITE);
	ptu_classify_s(insn, ptem_16bit, PTI_INST_PTWRITE);

	return ptu_passed();
}

static struct ptunit_result ptwrite_r64(void)
{
	uint8_t insn[] = { 0xf3, 0x48, 0x0f, 0xae, 0xe7 };

	ptu_classify_s(insn, ptem_64bit, PTI_INST_PTWRITE);

	return ptu_passed();
}

static struct ptunit_result ptwrite_m64(void)
{
	uint8_t insn[] = { 0xf3, 0x48, 0x0f, 0xae, 0x67, 0xcc };

	ptu_classify_s(insn, ptem_64bit, PTI_INST_PTWRITE);

	return ptu_passed();
}

int main(int argc, char **argv)
{
	struct ptunit_suite suite;

	pt_ild_init();

	suite = ptunit_mk_suite(argc, argv);

	ptu_run(suite, push);
	ptu_run(suite, jmp_rel);
	ptu_run(suite, long_nop);
	ptu_run(suite, mov_al_64);
	ptu_run(suite, mov_al_32);
	ptu_run(suite, mov_al_32_em64);
	ptu_run(suite, mov_al_32_em16);
	ptu_run(suite, mov_al_16_em32);
	ptu_run(suite, mov_al_16);
	ptu_run(suite, rdtsc);
	ptu_run(suite, pcmpistri);
	ptu_run(suite, vmovdqa);
	ptu_run(suite, vpandn);
	ptu_run(suite, syscall);
	ptu_run(suite, sysret);
	ptu_run(suite, sysenter);
	ptu_run(suite, sysexit);
	ptu_run(suite, int3);
	ptu_run(suite, intn);
	ptu_run(suite, iret);
	ptu_run(suite, call_9a_cd);
	ptu_run(suite, call_9a_cp);
	ptu_run(suite, call_ff_3);
	ptu_run(suite, jmp_ff_5);
	ptu_run(suite, jmp_ea_cd);
	ptu_run(suite, jmp_ea_cp);
	ptu_run(suite, ret_ca);
	ptu_run(suite, vmlaunch);
	ptu_run(suite, vmresume);
	ptu_run(suite, vmcall);
	ptu_run(suite, vmptrld);
	ptu_run(suite, jrcxz);
	ptu_run(suite, mov_eax_moffs64);
	ptu_run(suite, mov_eax_moffs64_32);
	ptu_run(suite, mov_rax_moffs64);
	ptu_run(suite, mov_rax_moffs64_32);
	ptu_run(suite, mov_ax_moffs64);
	ptu_run(suite, mov_ax_moffs64_32);
	ptu_run(suite, mov_eax_moffs32);
	ptu_run(suite, mov_ax_moffs32);
	ptu_run(suite, mov_ax_moffs16);
	ptu_run(suite, les);
	ptu_run(suite, les_disp16);
	ptu_run(suite, les_disp32);
	ptu_run(suite, les_ind_disp8);
	ptu_run(suite, les_ind_disp16);
	ptu_run(suite, les_ind_disp32);
	ptu_run(suite, lds);
	ptu_run(suite, lds_disp16);
	ptu_run(suite, lds_disp32);
	ptu_run(suite, lds_ind_disp8);
	ptu_run(suite, lds_ind_disp16);
	ptu_run(suite, lds_ind_disp32);
	ptu_run(suite, vpshufb);
	ptu_run(suite, bound);
	ptu_run(suite, evex_cutoff);
	ptu_run(suite, ptwrite_r32);
	ptu_run(suite, ptwrite_m32);
	ptu_run(suite, ptwrite_r64);
	ptu_run(suite, ptwrite_m64);

	return ptunit_report(&suite);
}
