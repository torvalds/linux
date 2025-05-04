// SPDX-License-Identifier: GPL-2.0

#include "misc.h"

#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/insn.h>
#include <asm/pgtable_types.h>
#include <asm/ptrace.h>
#include <asm/sev.h>

#define __BOOT_COMPRESSED

/* Basic instruction decoding support needed */
#include "../../lib/inat.c"
#include "../../lib/insn.c"

/*
 * Copy a version of this function here - insn-eval.c can't be used in
 * pre-decompression code.
 */
bool insn_has_rep_prefix(struct insn *insn)
{
	insn_byte_t p;
	int i;

	insn_get_prefixes(insn);

	for_each_insn_prefix(insn, i, p) {
		if (p == 0xf2 || p == 0xf3)
			return true;
	}

	return false;
}

enum es_result vc_decode_insn(struct es_em_ctxt *ctxt)
{
	char buffer[MAX_INSN_SIZE];
	int ret;

	memcpy(buffer, (unsigned char *)ctxt->regs->ip, MAX_INSN_SIZE);

	ret = insn_decode(&ctxt->insn, buffer, MAX_INSN_SIZE, INSN_MODE_64);
	if (ret < 0)
		return ES_DECODE_FAILED;

	return ES_OK;
}

extern void sev_insn_decode_init(void) __alias(inat_init_tables);
