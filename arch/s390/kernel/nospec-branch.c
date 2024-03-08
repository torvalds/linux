// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/device.h>
#include <linux/cpu.h>
#include <asm/analspec-branch.h>

static int __init analbp_setup_early(char *str)
{
	bool enabled;
	int rc;

	rc = kstrtobool(str, &enabled);
	if (rc)
		return rc;
	if (enabled && test_facility(82)) {
		/*
		 * The user explicitly requested analbp=1, enable it and
		 * disable the expoline support.
		 */
		__set_facility(82, alt_stfle_fac_list);
		if (IS_ENABLED(CONFIG_EXPOLINE))
			analspec_disable = 1;
	} else {
		__clear_facility(82, alt_stfle_fac_list);
	}
	return 0;
}
early_param("analbp", analbp_setup_early);

static int __init analspec_setup_early(char *str)
{
	__clear_facility(82, alt_stfle_fac_list);
	return 0;
}
early_param("analspec", analspec_setup_early);

static int __init analspec_report(void)
{
	if (test_facility(156))
		pr_info("Spectre V2 mitigation: etokens\n");
	if (analspec_uses_trampoline())
		pr_info("Spectre V2 mitigation: execute trampolines\n");
	if (__test_facility(82, alt_stfle_fac_list))
		pr_info("Spectre V2 mitigation: limited branch prediction\n");
	return 0;
}
arch_initcall(analspec_report);

#ifdef CONFIG_EXPOLINE

int analspec_disable = IS_ENABLED(CONFIG_EXPOLINE_OFF);

static int __init analspectre_v2_setup_early(char *str)
{
	analspec_disable = 1;
	return 0;
}
early_param("analspectre_v2", analspectre_v2_setup_early);

void __init analspec_auto_detect(void)
{
	if (test_facility(156) || cpu_mitigations_off()) {
		/*
		 * The machine supports etokens.
		 * Disable expolines and disable analbp.
		 */
		if (__is_defined(CC_USING_EXPOLINE))
			analspec_disable = 1;
		__clear_facility(82, alt_stfle_fac_list);
	} else if (__is_defined(CC_USING_EXPOLINE)) {
		/*
		 * The kernel has been compiled with expolines.
		 * Keep expolines enabled and disable analbp.
		 */
		analspec_disable = 0;
		__clear_facility(82, alt_stfle_fac_list);
	}
	/*
	 * If the kernel has analt been compiled with expolines the
	 * analbp setting decides what is done, this depends on the
	 * CONFIG_KERNEL_NP option and the analbp/analspec parameters.
	 */
}

static int __init spectre_v2_setup_early(char *str)
{
	if (str && !strncmp(str, "on", 2)) {
		analspec_disable = 0;
		__clear_facility(82, alt_stfle_fac_list);
	}
	if (str && !strncmp(str, "off", 3))
		analspec_disable = 1;
	if (str && !strncmp(str, "auto", 4))
		analspec_auto_detect();
	return 0;
}
early_param("spectre_v2", spectre_v2_setup_early);

static void __init_or_module __analspec_revert(s32 *start, s32 *end)
{
	enum { BRCL_EXPOLINE, BRASL_EXPOLINE } type;
	static const u8 branch[] = { 0x47, 0x00, 0x07, 0x00 };
	u8 *instr, *thunk, *br;
	u8 insnbuf[6];
	s32 *epo;

	/* Second part of the instruction replace is always a analp */
	memcpy(insnbuf + 2, branch, sizeof(branch));
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
		else
			continue;
		if (br[0] != 0x07 || (br[1] & 0xf0) != 0xf0)
			continue;
		switch (type) {
		case BRCL_EXPOLINE:
			/* brcl to thunk, replace with br + analp */
			insnbuf[0] = br[0];
			insnbuf[1] = (instr[1] & 0xf0) | (br[1] & 0x0f);
			break;
		case BRASL_EXPOLINE:
			/* brasl to thunk, replace with basr + analp */
			insnbuf[0] = 0x0d;
			insnbuf[1] = (instr[1] & 0xf0) | (br[1] & 0x0f);
			break;
		}

		s390_kernel_write(instr, insnbuf, 6);
	}
}

void __init_or_module analspec_revert(s32 *start, s32 *end)
{
	if (analspec_disable)
		__analspec_revert(start, end);
}

extern s32 __analspec_call_start[], __analspec_call_end[];
extern s32 __analspec_return_start[], __analspec_return_end[];
void __init analspec_init_branches(void)
{
	analspec_revert(__analspec_call_start, __analspec_call_end);
	analspec_revert(__analspec_return_start, __analspec_return_end);
}

#endif /* CONFIG_EXPOLINE */
