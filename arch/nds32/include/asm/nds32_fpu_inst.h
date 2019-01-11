/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2005-2018 Andes Technology Corporation */

#ifndef __NDS32_FPU_INST_H
#define __NDS32_FPU_INST_H

#define cop0_op	0x35

/*
 * COP0 field of opcodes.
 */
#define fs1_op	0x0
#define fs2_op  0x4
#define fd1_op  0x8
#define fd2_op  0xc

/*
 * FS1 opcode.
 */
enum fs1 {
	fadds_op, fsubs_op, fcpynss_op, fcpyss_op,
	fmadds_op, fmsubs_op, fcmovns_op, fcmovzs_op,
	fnmadds_op, fnmsubs_op,
	fmuls_op = 0xc, fdivs_op,
	fs1_f2op_op = 0xf
};

/*
 * FS1/F2OP opcode.
 */
enum fs1_f2 {
	fs2d_op, fsqrts_op,
	fui2s_op = 0x8, fsi2s_op = 0xc,
	fs2ui_op = 0x10, fs2ui_z_op = 0x14,
	fs2si_op = 0x18, fs2si_z_op = 0x1c
};

/*
 * FS2 opcode.
 */
enum fs2 {
	fcmpeqs_op, fcmpeqs_e_op, fcmplts_op, fcmplts_e_op,
	fcmples_op, fcmples_e_op, fcmpuns_op, fcmpuns_e_op
};

/*
 * FD1 opcode.
 */
enum fd1 {
	faddd_op, fsubd_op, fcpynsd_op, fcpysd_op,
	fmaddd_op, fmsubd_op, fcmovnd_op, fcmovzd_op,
	fnmaddd_op, fnmsubd_op,
	fmuld_op = 0xc, fdivd_op, fd1_f2op_op = 0xf
};

/*
 * FD1/F2OP opcode.
 */
enum fd1_f2 {
	fd2s_op, fsqrtd_op,
	fui2d_op = 0x8, fsi2d_op = 0xc,
	fd2ui_op = 0x10, fd2ui_z_op = 0x14,
	fd2si_op = 0x18, fd2si_z_op = 0x1c
};

/*
 * FD2 opcode.
 */
enum fd2 {
	fcmpeqd_op, fcmpeqd_e_op, fcmpltd_op, fcmpltd_e_op,
	fcmpled_op, fcmpled_e_op, fcmpund_op, fcmpund_e_op
};

#define NDS32Insn(x) x

#define I_OPCODE_off			25
#define NDS32Insn_OPCODE(x)		(NDS32Insn(x) >> I_OPCODE_off)

#define I_OPCODE_offRt			20
#define I_OPCODE_mskRt			(0x1fUL << I_OPCODE_offRt)
#define NDS32Insn_OPCODE_Rt(x) \
	((NDS32Insn(x) & I_OPCODE_mskRt) >> I_OPCODE_offRt)

#define I_OPCODE_offRa			15
#define I_OPCODE_mskRa			(0x1fUL << I_OPCODE_offRa)
#define NDS32Insn_OPCODE_Ra(x) \
	((NDS32Insn(x) & I_OPCODE_mskRa) >> I_OPCODE_offRa)

#define I_OPCODE_offRb			10
#define I_OPCODE_mskRb			(0x1fUL << I_OPCODE_offRb)
#define NDS32Insn_OPCODE_Rb(x) \
	((NDS32Insn(x) & I_OPCODE_mskRb) >> I_OPCODE_offRb)

#define I_OPCODE_offbit1014		10
#define I_OPCODE_mskbit1014		(0x1fUL << I_OPCODE_offbit1014)
#define NDS32Insn_OPCODE_BIT1014(x) \
	((NDS32Insn(x) & I_OPCODE_mskbit1014) >> I_OPCODE_offbit1014)

#define I_OPCODE_offbit69		6
#define I_OPCODE_mskbit69		(0xfUL << I_OPCODE_offbit69)
#define NDS32Insn_OPCODE_BIT69(x) \
	((NDS32Insn(x) & I_OPCODE_mskbit69) >> I_OPCODE_offbit69)

#define I_OPCODE_offCOP0		0
#define I_OPCODE_mskCOP0		(0x3fUL << I_OPCODE_offCOP0)
#define NDS32Insn_OPCODE_COP0(x) \
	((NDS32Insn(x) & I_OPCODE_mskCOP0) >> I_OPCODE_offCOP0)

#endif /* __NDS32_FPU_INST_H */
