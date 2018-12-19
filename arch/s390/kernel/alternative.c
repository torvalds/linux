#include <linux/module.h>
#include <asm/alternative.h>
#include <asm/facility.h>
#include <asm/nospec-branch.h>

#define MAX_PATCH_LEN (255 - 1)

static int __initdata_or_module alt_instr_disabled;

static int __init disable_alternative_instructions(char *str)
{
	alt_instr_disabled = 1;
	return 0;
}

early_param("noaltinstr", disable_alternative_instructions);

struct brcl_insn {
	u16 opc;
	s32 disp;
} __packed;

static u16 __initdata_or_module nop16 = 0x0700;
static u32 __initdata_or_module nop32 = 0x47000000;
static struct brcl_insn __initdata_or_module nop48 = {
	0xc004, 0
};

static const void *nops[] __initdata_or_module = {
	&nop16,
	&nop32,
	&nop48
};

static void __init_or_module add_jump_padding(void *insns, unsigned int len)
{
	struct brcl_insn brcl = {
		0xc0f4,
		len / 2
	};

	memcpy(insns, &brcl, sizeof(brcl));
	insns += sizeof(brcl);
	len -= sizeof(brcl);

	while (len > 0) {
		memcpy(insns, &nop16, 2);
		insns += 2;
		len -= 2;
	}
}

static void __init_or_module add_padding(void *insns, unsigned int len)
{
	if (len > 6)
		add_jump_padding(insns, len);
	else if (len >= 2)
		memcpy(insns, nops[len / 2 - 1], len);
}

static void __init_or_module __apply_alternatives(struct alt_instr *start,
						  struct alt_instr *end)
{
	struct alt_instr *a;
	u8 *instr, *replacement;
	u8 insnbuf[MAX_PATCH_LEN];

	/*
	 * The scan order should be from start to end. A later scanned
	 * alternative code can overwrite previously scanned alternative code.
	 */
	for (a = start; a < end; a++) {
		int insnbuf_sz = 0;

		instr = (u8 *)&a->instr_offset + a->instr_offset;
		replacement = (u8 *)&a->repl_offset + a->repl_offset;

		if (!__test_facility(a->facility,
				     S390_lowcore.alt_stfle_fac_list))
			continue;

		if (unlikely(a->instrlen % 2 || a->replacementlen % 2)) {
			WARN_ONCE(1, "cpu alternatives instructions length is "
				     "odd, skipping patching\n");
			continue;
		}

		memcpy(insnbuf, replacement, a->replacementlen);
		insnbuf_sz = a->replacementlen;

		if (a->instrlen > a->replacementlen) {
			add_padding(insnbuf + a->replacementlen,
				    a->instrlen - a->replacementlen);
			insnbuf_sz += a->instrlen - a->replacementlen;
		}

		s390_kernel_write(instr, insnbuf, insnbuf_sz);
	}
}

void __init_or_module apply_alternatives(struct alt_instr *start,
					 struct alt_instr *end)
{
	if (!alt_instr_disabled)
		__apply_alternatives(start, end);
}

extern struct alt_instr __alt_instructions[], __alt_instructions_end[];
void __init apply_alternative_instructions(void)
{
	apply_alternatives(__alt_instructions, __alt_instructions_end);
}
