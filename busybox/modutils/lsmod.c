/* vi: set sw=4 ts=4: */
/*
 * Mini lsmod implementation for busybox
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 * Copyright (C) 2008 by Vladimir Dronnikov <dronnikov@gmail.com>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config LSMOD
//config:	bool "lsmod (4.3 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	lsmod is used to display a list of loaded modules.
//config:
//config:config FEATURE_LSMOD_PRETTY_2_6_OUTPUT
//config:	bool "Pretty output"
//config:	default y
//config:	depends on LSMOD && !MODPROBE_SMALL
//config:	help
//config:	This option makes output format of lsmod adjusted to
//config:	the format of module-init-tools for Linux kernel 2.6.
//config:	Increases size somewhat.

//applet:IF_LSMOD(IF_NOT_MODPROBE_SMALL(APPLET_NOEXEC(lsmod, lsmod, BB_DIR_SBIN, BB_SUID_DROP, lsmod)))

//kbuild:ifneq ($(CONFIG_MODPROBE_SMALL),y)
//kbuild:lib-$(CONFIG_LSMOD) += lsmod.o modutils.o
//kbuild:endif

//usage:#if !ENABLE_MODPROBE_SMALL
//usage:#define lsmod_trivial_usage
//usage:       ""
//usage:#define lsmod_full_usage "\n\n"
//usage:       "List loaded kernel modules"
//usage:#endif

#include "libbb.h"
#include "unicode.h"

#if ENABLE_FEATURE_CHECK_TAINTED_MODULE
enum {
	TAINT_PROPRIETORY_MODULE = (1 << 0),
	TAINT_FORCED_MODULE      = (1 << 1),
	TAINT_UNSAFE_SMP         = (1 << 2),
};

static void check_tainted(void)
{
	int tainted = 0;
	char *buf = xmalloc_open_read_close("/proc/sys/kernel/tainted", NULL);
	if (buf) {
		tainted = atoi(buf);
		if (ENABLE_FEATURE_CLEAN_UP)
			free(buf);
	}

	if (tainted) {
		printf("    Tainted: %c%c%c\n",
				tainted & TAINT_PROPRIETORY_MODULE      ? 'P' : 'G',
				tainted & TAINT_FORCED_MODULE           ? 'F' : ' ',
				tainted & TAINT_UNSAFE_SMP              ? 'S' : ' ');
	} else {
		puts("    Not tainted");
	}
}
#else
static void check_tainted(void) { putchar('\n'); }
#endif

int lsmod_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int lsmod_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
#if ENABLE_FEATURE_LSMOD_PRETTY_2_6_OUTPUT
	char *token[4];
	parser_t *parser = config_open("/proc/modules");
	init_unicode();

	printf("%-24sSize  Used by", "Module");
	check_tainted();

	if (ENABLE_FEATURE_2_4_MODULES
	 && get_linux_version_code() < KERNEL_VERSION(2,6,0)
	) {
		while (config_read(parser, token, 4, 3, "# \t", PARSE_NORMAL)) {
			if (token[3] != NULL && token[3][0] == '[') {
				token[3]++;
				token[3][strlen(token[3])-1] = '\0';
			} else
				token[3] = (char *) "";
# if ENABLE_UNICODE_SUPPORT
			{
				uni_stat_t uni_stat;
				char *uni_name = unicode_conv_to_printable(&uni_stat, token[0]);
				unsigned pad_len = (uni_stat.unicode_width > 19) ? 0 : 19 - uni_stat.unicode_width;
				printf("%s%*s %8s %2s %s\n", uni_name, pad_len, "", token[1], token[2], token[3]);
				free(uni_name);
			}
# else
			printf("%-19s %8s %2s %s\n", token[0], token[1], token[2], token[3]);
# endif
		}
	} else {
		while (config_read(parser, token, 4, 4, "# \t", PARSE_NORMAL & ~PARSE_GREEDY)) {
			// N.B. token[3] is either '-' (module is not used by others)
			// or comma-separated list ended by comma
			// so trimming the trailing char is just what we need!
			if (token[3][0])
				token[3][strlen(token[3]) - 1] = '\0';
# if ENABLE_UNICODE_SUPPORT
			{
				uni_stat_t uni_stat;
				char *uni_name = unicode_conv_to_printable(&uni_stat, token[0]);
				unsigned pad_len = (uni_stat.unicode_width > 19) ? 0 : 19 - uni_stat.unicode_width;
				printf("%s%*s %8s %2s %s\n", uni_name, pad_len, "", token[1], token[2], token[3]);
				free(uni_name);
			}
# else
			printf("%-19s %8s %2s %s\n", token[0], token[1], token[2], token[3]);
# endif
		}
	}
	if (ENABLE_FEATURE_CLEAN_UP)
		config_close(parser);
#else
	check_tainted();
	xprint_and_close_file(xfopen_for_read("/proc/modules"));
#endif
	return EXIT_SUCCESS;
}
