/* vi: set sw=4 ts=4: */
/*
 * depmod - generate modules.dep
 * Copyright (c) 2008 Bernhard Reutner-Fischer
 * Copyrihgt (c) 2008 Timo Teras <timo.teras@iki.fi>
 * Copyright (c) 2008 Vladimir Dronnikov
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config DEPMOD
//config:	bool "depmod (26 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	depmod generates modules.dep (and potentially modules.alias
//config:	and modules.symbols) that contain dependency information
//config:	for modprobe.

//applet:IF_DEPMOD(IF_NOT_MODPROBE_SMALL(APPLET(depmod, BB_DIR_SBIN, BB_SUID_DROP)))

//kbuild:ifneq ($(CONFIG_MODPROBE_SMALL),y)
//kbuild:lib-$(CONFIG_DEPMOD) += depmod.o modutils.o
//kbuild:endif

#include "libbb.h"
#include "modutils.h"
#include <sys/utsname.h> /* uname() */

/*
 * Theory of operation:
 * - iterate over all modules and record their full path
 * - iterate over all modules looking for "depends=" entries
 *   for each depends, look through our list of full paths and emit if found
 */

static int FAST_FUNC parse_module(const char *fname, struct stat *sb UNUSED_PARAM,
				void *data, int depth UNUSED_PARAM)
{
	module_db *modules = data;
	char *image, *ptr;
	module_entry *e;

	/* Arbitrary. Was sb->st_size, but that breaks .gz etc */
	size_t len = (64*1024*1024 - 4096);

	if (strrstr(fname, ".ko") == NULL)
		return TRUE;

	image = xmalloc_open_zipped_read_close(fname, &len);

	e = moddb_get_or_create(modules, bb_get_last_path_component_nostrip(fname));
	e->name = xstrdup(fname + 2); /* skip "./" */

	for (ptr = image; ptr < image + len - 10; ptr++) {
		if (is_prefixed_with(ptr, "depends=")) {
			char *u;

			ptr += 8;
			for (u = ptr; *u; u++)
				if (*u == '-')
					*u = '_';
			ptr += string_to_llist(ptr, &e->deps, ",");
		} else if (ENABLE_FEATURE_MODUTILS_ALIAS
		 && is_prefixed_with(ptr, "alias=")
		) {
			llist_add_to(&e->aliases, xstrdup(ptr + 6));
			ptr += strlen(ptr);
		} else if (ENABLE_FEATURE_MODUTILS_SYMBOLS
		 && is_prefixed_with(ptr, "__ksymtab_")
		) {
			ptr += 10;
			if (is_prefixed_with(ptr, "gpl")
			 || strcmp(ptr, "strings") == 0
			) {
				continue;
			}
			llist_add_to(&e->symbols, xstrdup(ptr));
			ptr += strlen(ptr);
		}
	}
	free(image);

	return TRUE;
}

static void order_dep_list(module_db *modules, module_entry *start, llist_t *add)
{
	module_entry *m;
	llist_t *n;

	for (n = add; n != NULL; n = n->link) {
		m = moddb_get(modules, n->data);
		if (m == NULL)
			continue;

		/* unlink current entry */
		m->dnext->dprev = m->dprev;
		m->dprev->dnext = m->dnext;

		/* and add it to tail */
		m->dnext = start;
		m->dprev = start->dprev;
		start->dprev->dnext = m;
		start->dprev = m;

		/* recurse */
		order_dep_list(modules, start, m->deps);
	}
}

static void xfreopen_write(const char *file, FILE *f)
{
	if (freopen(file, "w", f) == NULL)
		bb_perror_msg_and_die("can't open '%s'", file);
}

//usage:#if !ENABLE_MODPROBE_SMALL
//usage:#define depmod_trivial_usage "[-n] [-b BASE] [VERSION] [MODFILES]..."
//usage:#define depmod_full_usage "\n\n"
//usage:       "Generate modules.dep, alias, and symbols files"
//usage:     "\n"
//usage:     "\n	-b BASE	Use BASE/lib/modules/VERSION"
//usage:     "\n	-n	Dry run: print files to stdout"
//usage:#endif

/* Upstream usage:
 * [-aAenv] [-C FILE or DIR] [-b BASE] [-F System.map] [VERSION] [MODFILES]...
 *	-a --all
 *		Probe all modules. Default if no MODFILES.
 *	-A --quick
 *		Check modules.dep's mtime against module files' mtimes.
 *	-b --basedir BASE
 *		Use $BASE/lib/modules/VERSION
 *	-C --config FILE or DIR
 *		Path to /etc/depmod.conf or /etc/depmod.d/
 *	-e --errsyms
 *		When combined with the -F option, this reports any symbols
 *		which are not supplied by other modules or kernel.
 *	-F --filesyms System.map
 *	-n --dry-run
 *		Print modules.dep etc to standard output
 *	-v --verbose
 *		Print to stdout all the symbols each module depends on
 *		and the module's file name which provides that symbol.
 *	-r	No-op
 *	-u	No-op
 *	-q	No-op
 *
 * So far we only support: [-n] [-b BASE] [VERSION] [MODFILES]...
 * Accepted but ignored:
 * -aAe
 * -F System.map
 * -C FILE/DIR
 *
 * Not accepted: -v
 */
enum {
	//OPT_a = (1 << 0), /* All modules, ignore mods in argv */
	//OPT_A = (1 << 1), /* Only emit .ko that are newer than modules.dep file */
	OPT_b = (1 << 2), /* base directory when modules are in staging area */
	//OPT_e = (1 << 3), /* with -F, print unresolved symbols */
	//OPT_F = (1 << 4), /* System.map that contains the symbols */
	OPT_n = (1 << 5), /* dry-run, print to stdout only */
	OPT_r = (1 << 6), /* Compat dummy. Linux Makefile uses it */
	OPT_u = (1 << 7), /* -u,--unresolved-error: ignored */
	OPT_q = (1 << 8), /* -q,--quiet: ignored */
	OPT_C = (1 << 9), /* -C,--config etc_modules_conf: ignored */
};

int depmod_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int depmod_main(int argc UNUSED_PARAM, char **argv)
{
	module_db modules;
	module_entry *m, *dep;
	const char *moddir_base = "/";
	char *moddir, *version;
	struct utsname uts;
	unsigned i;
	int tmp;

	getopt32(argv, "aAb:eF:nruqC:", &moddir_base, NULL, NULL);
	argv += optind;

	/* goto modules location */
	xchdir(moddir_base);

	/* If a version is provided, then that kernel version's module directory
	 * is used, rather than the current kernel version (as returned by
	 * "uname -r").  */
	if (*argv && sscanf(*argv, "%u.%u.%u", &tmp, &tmp, &tmp) == 3) {
		version = *argv++;
	} else {
		uname(&uts);
		version = uts.release;
	}
	moddir = concat_path_file(&CONFIG_DEFAULT_MODULES_DIR[1], version);
	xchdir(moddir);
	if (ENABLE_FEATURE_CLEAN_UP)
		free(moddir);

	/* Scan modules */
	memset(&modules, 0, sizeof(modules));
	if (*argv) {
		do {
			parse_module(*argv, /*sb (unused):*/ NULL, &modules, 0);
		} while (*++argv);
	} else {
		recursive_action(".", ACTION_RECURSE,
				parse_module, NULL, &modules, 0);
	}

	/* Generate dependency and alias files */
	if (!(option_mask32 & OPT_n))
		xfreopen_write(CONFIG_DEFAULT_DEPMOD_FILE, stdout);

	moddb_foreach_module(&modules, m, i) {
		printf("%s:", m->name);

		order_dep_list(&modules, m, m->deps);
		while (m->dnext != m) {
			dep = m->dnext;
			printf(" %s", dep->name);

			/* unlink current entry */
			dep->dnext->dprev = dep->dprev;
			dep->dprev->dnext = dep->dnext;
			dep->dnext = dep->dprev = dep;
		}
		bb_putchar('\n');
	}

#if ENABLE_FEATURE_MODUTILS_ALIAS
	if (!(option_mask32 & OPT_n))
		xfreopen_write("modules.alias", stdout);
	moddb_foreach_module(&modules, m, i) {
		while (m->aliases) {
			/*
			 * Last word used to be a basename
			 * (filename with path and .ko.* stripped)
			 * at the time of module-init-tools 3.4.
			 * kmod v.12 uses module name, i.e., s/-/_/g.
			 */
			printf("alias %s %s\n",
				(char*)llist_pop(&m->aliases),
				m->modname);
		}
	}
#endif
#if ENABLE_FEATURE_MODUTILS_SYMBOLS
	if (!(option_mask32 & OPT_n))
		xfreopen_write("modules.symbols", stdout);
	moddb_foreach_module(&modules, m, i) {
		while (m->symbols) {
			printf("alias symbol:%s %s\n",
				(char*)llist_pop(&m->symbols),
				m->modname);
		}
	}
#endif

	if (ENABLE_FEATURE_CLEAN_UP)
		moddb_free(&modules);

	return EXIT_SUCCESS;
}
