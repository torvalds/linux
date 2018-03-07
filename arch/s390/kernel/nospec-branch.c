// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <asm/nospec-branch.h>

int nospec_call_disable = IS_ENABLED(EXPOLINE_OFF);
int nospec_return_disable = !IS_ENABLED(EXPOLINE_FULL);

static int __init nospectre_v2_setup_early(char *str)
{
	nospec_call_disable = 1;
	nospec_return_disable = 1;
	return 0;
}
early_param("nospectre_v2", nospectre_v2_setup_early);

static int __init spectre_v2_setup_early(char *str)
{
	if (str && !strncmp(str, "on", 2)) {
		nospec_call_disable = 0;
		nospec_return_disable = 0;
	}
	if (str && !strncmp(str, "off", 3)) {
		nospec_call_disable = 1;
		nospec_return_disable = 1;
	}
	if (str && !strncmp(str, "auto", 4)) {
		nospec_call_disable = 0;
		nospec_return_disable = 1;
	}
	return 0;
}
early_param("spectre_v2", spectre_v2_setup_early);

static void __init_or_module __nospec_revert(s32 *start, s32 *end)
{
	enum { BRCL_EXPOLINE, BRASL_EXPOLINE } type;
	u8 *instr, *thunk, *br;
	u8 insnbuf[6];
	s32 *epo;

	/* Second part of the instruction replace is always a nop */
	memcpy(insnbuf + 2, (char[]) { 0x47, 0x00, 0x00, 0x00 }, 4);
	for (epo = start; epo < end; epo++) {
		instr = (u8 *) epo + *epo;
		if (instr[0] == 0xc0 && (instr[1] & 0x0f) == 0x04)
			type = BRCL_EXPOLINE;	/* brcl instruction */
		else if (instr[0] == 0xc0 && (instr[1] & 0x0f) == 0x05)
			type = BRASL_EXPOLINE;	/* brasl instruction */
		else
			continue;
		thunk = instr + (*(int *)(instr + 2)) * 2;
		if (thunk[0] == 0xc6 && thunk[1] == 0x00)
			/* exrl %r0,<target-br> */
			br = thunk + (*(int *)(thunk + 2)) * 2;
		else if (thunk[0] == 0xc0 && (thunk[1] & 0x0f) == 0x00 &&
			 thunk[6] == 0x44 && thunk[7] == 0x00 &&
			 (thunk[8] & 0x0f) == 0x00 && thunk[9] == 0x00 &&
			 (thunk[1] & 0xf0) == (thunk[8] & 0xf0))
			/* larl %rx,<target br> + ex %r0,0(%rx) */
			br = thunk + (*(int *)(thunk + 2)) * 2;
		else
			continue;
		if (br[0] != 0x07 || (br[1] & 0xf0) != 0xf0)
			continue;
		switch (type) {
		case BRCL_EXPOLINE:
			/* brcl to thunk, replace with br + nop */
			insnbuf[0] = br[0];
			insnbuf[1] = (instr[1] & 0xf0) | (br[1] & 0x0f);
			break;
		case BRASL_EXPOLINE:
			/* brasl to thunk, replace with basr + nop */
			insnbuf[0] = 0x0d;
			insnbuf[1] = (instr[1] & 0xf0) | (br[1] & 0x0f);
			break;
		}

		s390_kernel_write(instr, insnbuf, 6);
	}
}

void __init_or_module nospec_call_revert(s32 *start, s32 *end)
{
	if (nospec_call_disable)
		__nospec_revert(start, end);
}

void __init_or_module nospec_return_revert(s32 *start, s32 *end)
{
	if (nospec_return_disable)
		__nospec_revert(start, end);
}

extern s32 __nospec_call_start[], __nospec_call_end[];
extern s32 __nospec_return_start[], __nospec_return_end[];
void __init nospec_init_branches(void)
{
	nospec_call_revert(__nospec_call_start, __nospec_call_end);
	nospec_return_revert(__nospec_return_start, __nospec_return_end);
}
