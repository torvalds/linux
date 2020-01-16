// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/device.h>
#include <linux/cpu.h>
#include <asm/yesspec-branch.h>

static int __init yesbp_setup_early(char *str)
{
	bool enabled;
	int rc;

	rc = kstrtobool(str, &enabled);
	if (rc)
		return rc;
	if (enabled && test_facility(82)) {
		/*
		 * The user explicitely requested yesbp=1, enable it and
		 * disable the expoline support.
		 */
		__set_facility(82, S390_lowcore.alt_stfle_fac_list);
		if (IS_ENABLED(CONFIG_EXPOLINE))
			yesspec_disable = 1;
	} else {
		__clear_facility(82, S390_lowcore.alt_stfle_fac_list);
	}
	return 0;
}
early_param("yesbp", yesbp_setup_early);

static int __init yesspec_setup_early(char *str)
{
	__clear_facility(82, S390_lowcore.alt_stfle_fac_list);
	return 0;
}
early_param("yesspec", yesspec_setup_early);

static int __init yesspec_report(void)
{
	if (test_facility(156))
		pr_info("Spectre V2 mitigation: etokens\n");
	if (__is_defined(CC_USING_EXPOLINE) && !yesspec_disable)
		pr_info("Spectre V2 mitigation: execute trampolines\n");
	if (__test_facility(82, S390_lowcore.alt_stfle_fac_list))
		pr_info("Spectre V2 mitigation: limited branch prediction\n");
	return 0;
}
arch_initcall(yesspec_report);

#ifdef CONFIG_EXPOLINE

int yesspec_disable = IS_ENABLED(CONFIG_EXPOLINE_OFF);

static int __init yesspectre_v2_setup_early(char *str)
{
	yesspec_disable = 1;
	return 0;
}
early_param("yesspectre_v2", yesspectre_v2_setup_early);

void __init yesspec_auto_detect(void)
{
	if (test_facility(156) || cpu_mitigations_off()) {
		/*
		 * The machine supports etokens.
		 * Disable expolines and disable yesbp.
		 */
		if (__is_defined(CC_USING_EXPOLINE))
			yesspec_disable = 1;
		__clear_facility(82, S390_lowcore.alt_stfle_fac_list);
	} else if (__is_defined(CC_USING_EXPOLINE)) {
		/*
		 * The kernel has been compiled with expolines.
		 * Keep expolines enabled and disable yesbp.
		 */
		yesspec_disable = 0;
		__clear_facility(82, S390_lowcore.alt_stfle_fac_list);
	}
	/*
	 * If the kernel has yest been compiled with expolines the
	 * yesbp setting decides what is done, this depends on the
	 * CONFIG_KERNEL_NP option and the yesbp/yesspec parameters.
	 */
}

static int __init spectre_v2_setup_early(char *str)
{
	if (str && !strncmp(str, "on", 2)) {
		yesspec_disable = 0;
		__clear_facility(82, S390_lowcore.alt_stfle_fac_list);
	}
	if (str && !strncmp(str, "off", 3))
		yesspec_disable = 1;
	if (str && !strncmp(str, "auto", 4))
		yesspec_auto_detect();
	return 0;
}
early_param("spectre_v2", spectre_v2_setup_early);

static void __init_or_module __yesspec_revert(s32 *start, s32 *end)
{
	enum { BRCL_EXPOLINE, BRASL_EXPOLINE } type;
	u8 *instr, *thunk, *br;
	u8 insnbuf[6];
	s32 *epo;

	/* Second part of the instruction replace is always a yesp */
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
		/* Check for unconditional branch 0x07f? or 0x47f???? */
		if ((br[0] & 0xbf) != 0x07 || (br[1] & 0xf0) != 0xf0)
			continue;

		memcpy(insnbuf + 2, (char[]) { 0x47, 0x00, 0x07, 0x00 }, 4);
		switch (type) {
		case BRCL_EXPOLINE:
			insnbuf[0] = br[0];
			insnbuf[1] = (instr[1] & 0xf0) | (br[1] & 0x0f);
			if (br[0] == 0x47) {
				/* brcl to b, replace with bc + yespr */
				insnbuf[2] = br[2];
				insnbuf[3] = br[3];
			} else {
				/* brcl to br, replace with bcr + yesp */
			}
			break;
		case BRASL_EXPOLINE:
			insnbuf[1] = (instr[1] & 0xf0) | (br[1] & 0x0f);
			if (br[0] == 0x47) {
				/* brasl to b, replace with bas + yespr */
				insnbuf[0] = 0x4d;
				insnbuf[2] = br[2];
				insnbuf[3] = br[3];
			} else {
				/* brasl to br, replace with basr + yesp */
				insnbuf[0] = 0x0d;
			}
			break;
		}

		s390_kernel_write(instr, insnbuf, 6);
	}
}

void __init_or_module yesspec_revert(s32 *start, s32 *end)
{
	if (yesspec_disable)
		__yesspec_revert(start, end);
}

extern s32 __yesspec_call_start[], __yesspec_call_end[];
extern s32 __yesspec_return_start[], __yesspec_return_end[];
void __init yesspec_init_branches(void)
{
	yesspec_revert(__yesspec_call_start, __yesspec_call_end);
	yesspec_revert(__yesspec_return_start, __yesspec_return_end);
}

#endif /* CONFIG_EXPOLINE */
