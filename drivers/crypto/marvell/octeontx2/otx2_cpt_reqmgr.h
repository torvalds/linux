/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2020 Marvell.
 */

#ifndef __OTX2_CPT_REQMGR_H
#define __OTX2_CPT_REQMGR_H

#include "otx2_cpt_common.h"

/* Completion code size and initial value */
#define OTX2_CPT_COMPLETION_CODE_SIZE 8
#define OTX2_CPT_COMPLETION_CODE_INIT OTX2_CPT_COMP_E_NOTDONE

union otx2_cpt_opcode {
	u16 flags;
	struct {
		u8 major;
		u8 minor;
	} s;
};

/*
 * CPT_INST_S software command definitions
 * Words EI (0-3)
 */
union otx2_cpt_iq_cmd_word0 {
	u64 u;
	struct {
		__be16 opcode;
		__be16 param1;
		__be16 param2;
		__be16 dlen;
	} s;
};

union otx2_cpt_iq_cmd_word3 {
	u64 u;
	struct {
		u64 cptr:61;
		u64 grp:3;
	} s;
};

struct otx2_cpt_iq_command {
	union otx2_cpt_iq_cmd_word0 cmd;
	u64 dptr;
	u64 rptr;
	union otx2_cpt_iq_cmd_word3 cptr;
};

#endif /* __OTX2_CPT_REQMGR_H */
