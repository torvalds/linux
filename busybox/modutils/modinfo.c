/* vi: set sw=4 ts=4: */
/*
 * modinfo - retrieve module info
 * Copyright (c) 2008 Pascal Bellard
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config MODINFO
//config:	bool "modinfo (25 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Show information about a Linux Kernel module

//applet:IF_MODINFO(APPLET_NOEXEC(modinfo, modinfo, BB_DIR_SBIN, BB_SUID_DROP, modinfo))

//kbuild:lib-$(CONFIG_MODINFO) += modinfo.o modutils.o

#include <fnmatch.h>
#include <sys/utsname.h> /* uname() */
#include "libbb.h"
#include "modutils.h"

static const char *const shortcuts[] = {
	"filename",	// -n
	"author",	// -a
	"description",	// -d
	"license",	// -l
	"parm",		// -p
	"version",	// the rest has no shortcut options
	"alias",
	"srcversion",
	"depends",
	"uts_release",
	"intree",
	"vermagic",
	"firmware",
};

enum {
	OPT_0 = (1 << 0), /* \0 as separator */
	OPT_F = (1 << 1), /* field name */
	/* first bits are for -nadlp options, the rest are for
	 * fields not selectable with "shortcut" options
	 */
	OPT_n = (1 << 2),
	OPT_TAGS = ((1 << ARRAY_SIZE(shortcuts)) - 1) << 2,
};

static void display(const char *data, const char *pattern)
{
	int flag = option_mask32 >> 1; /* shift out -0 bit */
	if (flag & (flag-1)) {
		/* more than one field to show: print "FIELD:" pfx */
		int n = printf("%s:", pattern);
		while (n++ < 16)
			bb_putchar(' ');
	}
	printf("%s%c", data, (option_mask32 & OPT_0) ? '\0' : '\n');
}

static void modinfo(const char *path, const char *version,
			const char *field)
{
	size_t len;
	int j;
	char *ptr, *the_module;
	char *allocated;
	int tags = option_mask32;

	allocated = NULL;
	len = MAXINT(ssize_t);
	the_module = xmalloc_open_zipped_read_close(path, &len);
	if (!the_module) {
		if (path[0] == '/')
			return;
		/* Newer depmod puts relative paths in modules.dep */
		path = allocated = xasprintf("%s/%s/%s", CONFIG_DEFAULT_MODULES_DIR, version, path);
		the_module = xmalloc_open_zipped_read_close(path, &len);
		if (!the_module) {
			bb_error_msg("module '%s' not found", path);
			goto ret;
		}
	}

	for (j = 1; (1<<j) & (OPT_TAGS|OPT_F); j++) {
		const char *pattern;

		if (!((1<<j) & tags))
			continue;

		pattern = field;
		if ((1<<j) & OPT_TAGS)
			pattern = shortcuts[j-2];

		if (strcmp(pattern, shortcuts[0]) == 0) {
			/* "-n" or "-F filename" */
			display(path, shortcuts[0]);
			continue;
		}

		ptr = the_module;
		while (1) {
			char *after_pattern;

			ptr = memchr(ptr, *pattern, len - (ptr - (char*)the_module));
			if (ptr == NULL) /* no occurrence left, done */
				break;
			after_pattern = is_prefixed_with(ptr, pattern);
			if (after_pattern && *after_pattern == '=') {
				/* field prefixes are 0x80 or 0x00 */
				if ((ptr[-1] & 0x7F) == 0x00) {
					ptr = after_pattern + 1;
					display(ptr, pattern);
					ptr += strlen(ptr);
				}
			}
			++ptr;
		}
	}
	free(the_module);
 ret:
	free(allocated);
}

//usage:#define modinfo_trivial_usage
//usage:       "[-adlpn0] [-F keyword] MODULE"
//usage:#define modinfo_full_usage "\n\n"
//usage:       "	-a		Shortcut for '-F author'"
//usage:     "\n	-d		Shortcut for '-F description'"
//usage:     "\n	-l		Shortcut for '-F license'"
//usage:     "\n	-p		Shortcut for '-F parm'"
////usage:     "\n	-n		Shortcut for '-F filename'"
//usage:     "\n	-F keyword	Keyword to look for"
//usage:     "\n	-0		Separate output with NULs"
//usage:#define modinfo_example_usage
//usage:       "$ modinfo -F vermagic loop\n"

int modinfo_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int modinfo_main(int argc UNUSED_PARAM, char **argv)
{
	const char *field;
	char name[MODULE_NAME_LEN];
	struct utsname uts;
	parser_t *parser;
	char *colon, *tokens[2];
	unsigned opts;
	unsigned i;

	field = NULL;
	opts = getopt32(argv, "^" "0F:nadlp" "\0" "-1"/*minimum one arg*/, &field);
	/* If no field selected, show all */
	if (!(opts & (OPT_TAGS|OPT_F)))
		option_mask32 |= OPT_TAGS;
	argv += optind;

	uname(&uts);
	parser = config_open2(
		xasprintf("%s/%s/%s", CONFIG_DEFAULT_MODULES_DIR, uts.release, CONFIG_DEFAULT_DEPMOD_FILE),
		xfopen_for_read
	);

	while (config_read(parser, tokens, 2, 1, "# \t", PARSE_NORMAL)) {
		colon = last_char_is(tokens[0], ':');
		if (colon == NULL)
			continue;
		*colon = '\0';
		filename2modname(bb_basename(tokens[0]), name);
		for (i = 0; argv[i]; i++) {
			if (fnmatch(argv[i], name, 0) == 0) {
				modinfo(tokens[0], uts.release, field);
				argv[i] = (char *) "";
			}
		}
	}
	if (ENABLE_FEATURE_CLEAN_UP)
		config_close(parser);

	for (i = 0; argv[i]; i++) {
		if (argv[i][0]) {
			modinfo(argv[i], uts.release, field);
		}
	}

	return 0;
}
