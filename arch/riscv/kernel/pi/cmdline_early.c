// SPDX-License-Identifier: GPL-2.0-only
#include <linux/types.h>
#include <linux/init.h>
#include <linux/libfdt.h>
#include <linux/string.h>
#include <asm/pgtable.h>
#include <asm/setup.h>

static char early_cmdline[COMMAND_LINE_SIZE];

/*
 * Declare the functions that are exported (but prefixed) here so that LLVM
 * does not complain it lacks the 'static' keyword (which, if added, makes
 * LLVM complain because the function is actually unused in this file).
 */
u64 set_satp_mode_from_cmdline(uintptr_t dtb_pa);
bool set_nokaslr_from_cmdline(uintptr_t dtb_pa);

static char *get_early_cmdline(uintptr_t dtb_pa)
{
	const char *fdt_cmdline = NULL;
	unsigned int fdt_cmdline_size = 0;
	int chosen_node;

	if (!IS_ENABLED(CONFIG_CMDLINE_FORCE)) {
		chosen_node = fdt_path_offset((void *)dtb_pa, "/chosen");
		if (chosen_node >= 0) {
			fdt_cmdline = fdt_getprop((void *)dtb_pa, chosen_node,
						  "bootargs", NULL);
			if (fdt_cmdline) {
				fdt_cmdline_size = strlen(fdt_cmdline);
				strscpy(early_cmdline, fdt_cmdline,
					COMMAND_LINE_SIZE);
			}
		}
	}

	if (IS_ENABLED(CONFIG_CMDLINE_EXTEND) ||
	    IS_ENABLED(CONFIG_CMDLINE_FORCE) ||
	    fdt_cmdline_size == 0 /* CONFIG_CMDLINE_FALLBACK */) {
		strncat(early_cmdline, CONFIG_CMDLINE,
			COMMAND_LINE_SIZE - fdt_cmdline_size);
	}

	return early_cmdline;
}

static u64 match_noXlvl(char *cmdline)
{
	if (strstr(cmdline, "no4lvl"))
		return SATP_MODE_48;
	else if (strstr(cmdline, "no5lvl"))
		return SATP_MODE_57;

	return 0;
}

u64 set_satp_mode_from_cmdline(uintptr_t dtb_pa)
{
	char *cmdline = get_early_cmdline(dtb_pa);

	return match_noXlvl(cmdline);
}

static bool match_nokaslr(char *cmdline)
{
	return strstr(cmdline, "nokaslr");
}

bool set_nokaslr_from_cmdline(uintptr_t dtb_pa)
{
	char *cmdline = get_early_cmdline(dtb_pa);

	return match_nokaslr(cmdline);
}
