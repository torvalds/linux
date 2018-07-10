/* vi: set sw=4 ts=4: */
/*
 * Modprobe written from scratch for BusyBox
 *
 * Copyright (c) 2008 Timo Teras <timo.teras@iki.fi>
 * Copyright (c) 2008 Vladimir Dronnikov
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config MODPROBE
//config:	bool "modprobe (29 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Handle the loading of modules, and their dependencies on a high
//config:	level.
//config:
//config:config FEATURE_MODPROBE_BLACKLIST
//config:	bool "Blacklist support"
//config:	default y
//config:	depends on MODPROBE && !MODPROBE_SMALL
//config:	help
//config:	Say 'y' here to enable support for the 'blacklist' command in
//config:	modprobe.conf. This prevents the alias resolver to resolve
//config:	blacklisted modules. This is useful if you want to prevent your
//config:	hardware autodetection scripts to load modules like evdev, frame
//config:	buffer drivers etc.

//applet:IF_MODPROBE(IF_NOT_MODPROBE_SMALL(APPLET_NOEXEC(modprobe, modprobe, BB_DIR_SBIN, BB_SUID_DROP, modprobe)))

//kbuild:ifneq ($(CONFIG_MODPROBE_SMALL),y)
//kbuild:lib-$(CONFIG_MODPROBE) += modprobe.o modutils.o
//kbuild:endif

#include "libbb.h"
#include "modutils.h"
#include <sys/utsname.h>
#include <fnmatch.h>

#if 1
#define DBG(...) ((void)0)
#else
#define DBG(fmt, ...) bb_error_msg("%s: " fmt, __func__, ## __VA_ARGS__)
#endif

/* Note that unlike older versions of modules.dep/depmod (busybox and m-i-t),
 * we expect the full dependency list to be specified in modules.dep.
 * Older versions would only export the direct dependency list.
 */


//usage:#if !ENABLE_MODPROBE_SMALL
//usage:#define modprobe_notes_usage
//usage:	"modprobe can (un)load a stack of modules, passing each module options (when\n"
//usage:	"loading). modprobe uses a configuration file to determine what option(s) to\n"
//usage:	"pass each module it loads.\n"
//usage:	"\n"
//usage:	"The configuration file is searched (in this order):\n"
//usage:	"\n"
//usage:	"    /etc/modprobe.conf (2.6 only)\n"
//usage:	"    /etc/modules.conf\n"
//usage:	"    /etc/conf.modules (deprecated)\n"
//usage:	"\n"
//usage:	"They all have the same syntax (see below). If none is present, it is\n"
//usage:	"_not_ an error; each loaded module is then expected to load without\n"
//usage:	"options. Once a file is found, the others are tested for.\n"
//usage:	"\n"
//usage:	"/etc/modules.conf entry format:\n"
//usage:	"\n"
//usage:	"  alias <alias_name> <mod_name>\n"
//usage:	"    Makes it possible to modprobe alias_name, when there is no such module.\n"
//usage:	"    It makes sense if your mod_name is long, or you want a more representative\n"
//usage:	"    name for that module (eg. 'scsi' in place of 'aha7xxx').\n"
//usage:	"    This makes it also possible to use a different set of options (below) for\n"
//usage:	"    the module and the alias.\n"
//usage:	"    A module can be aliased more than once.\n"
//usage:	"\n"
//usage:	"  options <mod_name|alias_name> <symbol=value...>\n"
//usage:	"    When loading module mod_name (or the module aliased by alias_name), pass\n"
//usage:	"    the \"symbol=value\" pairs as option to that module.\n"
//usage:	"\n"
//usage:	"Sample /etc/modules.conf file:\n"
//usage:	"\n"
//usage:	"  options tulip irq=3\n"
//usage:	"  alias tulip tulip2\n"
//usage:	"  options tulip2 irq=4 io=0x308\n"
//usage:	"\n"
//usage:	"Other functionality offered by 'classic' modprobe is not available in\n"
//usage:	"this implementation.\n"
//usage:	"\n"
//usage:	"If module options are present both in the config file, and on the command line,\n"
//usage:	"then the options from the command line will be passed to the module _after_\n"
//usage:	"the options from the config file. That way, you can have defaults in the config\n"
//usage:	"file, and override them for a specific usage from the command line.\n"
//usage:#define modprobe_example_usage
//usage:       "(with the above /etc/modules.conf):\n\n"
//usage:       "$ modprobe tulip\n"
//usage:       "   will load the module 'tulip' with default option 'irq=3'\n\n"
//usage:       "$ modprobe tulip irq=5\n"
//usage:       "   will load the module 'tulip' with option 'irq=5', thus overriding the default\n\n"
//usage:       "$ modprobe tulip2\n"
//usage:       "   will load the module 'tulip' with default options 'irq=4 io=0x308',\n"
//usage:       "   which are the default for alias 'tulip2'\n\n"
//usage:       "$ modprobe tulip2 irq=8\n"
//usage:       "   will load the module 'tulip' with default options 'irq=4 io=0x308 irq=8',\n"
//usage:       "   which are the default for alias 'tulip2' overridden by the option 'irq=8'\n\n"
//usage:       "   from the command line\n\n"
//usage:       "$ modprobe tulip2 irq=2 io=0x210\n"
//usage:       "   will load the module 'tulip' with default options 'irq=4 io=0x308 irq=4 io=0x210',\n"
//usage:       "   which are the default for alias 'tulip2' overridden by the options 'irq=2 io=0x210'\n\n"
//usage:       "   from the command line\n"
//usage:
//usage:#define modprobe_trivial_usage
//usage:	"[-alrqvsD" IF_FEATURE_MODPROBE_BLACKLIST("b") "]"
//usage:	" MODULE" IF_FEATURE_CMDLINE_MODULE_OPTIONS(" [SYMBOL=VALUE]...")
//usage:#define modprobe_full_usage "\n\n"
//usage:       "	-a	Load multiple MODULEs"
//usage:     "\n	-l	List (MODULE is a pattern)"
//usage:     "\n	-r	Remove MODULE (stacks) or do autoclean"
//usage:     "\n	-q	Quiet"
//usage:     "\n	-v	Verbose"
//usage:     "\n	-s	Log to syslog"
//usage:     "\n	-D	Show dependencies"
//usage:	IF_FEATURE_MODPROBE_BLACKLIST(
//usage:     "\n	-b	Apply blacklist to module names too"
//usage:	)
//usage:#endif /* !ENABLE_MODPROBE_SMALL */

/* Note: usage text doesn't document various 2.4 options
 * we pull in through INSMOD_OPTS define
 * Note2: -b is always accepted, but if !FEATURE_MODPROBE_BLACKLIST,
 * it is a no-op.
 */
#define MODPROBE_OPTS  "alrDb"
/* -a and -D _are_ in fact compatible */
#define MODPROBE_COMPLEMENTARY "q-v:v-q:l--arD:r--alD:a--lr:D--rl"
//#define MODPROBE_OPTS  "acd:lnrt:C:b"
//#define MODPROBE_COMPLEMENTARY "q-v:v-q:l--acr:a--lr:r--al"
enum {
	OPT_INSERT_ALL   = (INSMOD_OPT_UNUSED << 0), /* a */
	//OPT_DUMP_ONLY  = (INSMOD_OPT_UNUSED << x), /* c */
	//OPT_DIRNAME    = (INSMOD_OPT_UNUSED << x), /* d */
	OPT_LIST_ONLY    = (INSMOD_OPT_UNUSED << 1), /* l */
	//OPT_SHOW_ONLY  = (INSMOD_OPT_UNUSED << x), /* n */
	OPT_REMOVE       = (INSMOD_OPT_UNUSED << 2), /* r */
	//OPT_RESTRICT   = (INSMOD_OPT_UNUSED << x), /* t */
	//OPT_VERONLY    = (INSMOD_OPT_UNUSED << x), /* V */
	//OPT_CONFIGFILE = (INSMOD_OPT_UNUSED << x), /* C */
	OPT_SHOW_DEPS    = (INSMOD_OPT_UNUSED << 3), /* D */
	OPT_BLACKLIST    = (INSMOD_OPT_UNUSED << 4) * ENABLE_FEATURE_MODPROBE_BLACKLIST,
};
#if ENABLE_LONG_OPTS
static const char modprobe_longopts[] ALIGN1 =
	/* nobody asked for long opts (yet) */
	// "all\0"          No_argument "a"
	// "list\0"         No_argument "l"
	// "remove\0"       No_argument "r"
	// "quiet\0"        No_argument "q"
	// "verbose\0"      No_argument "v"
	// "syslog\0"       No_argument "s"
	/* module-init-tools 3.11.1 has only long opt --show-depends
	 * but no short -D, we provide long opt for scripts which
	 * were written for 3.11.1: */
	"show-depends\0"     No_argument "D"
	// "use-blacklist\0" No_argument "b"
	;
#endif

#define MODULE_FLAG_LOADED              0x0001
#define MODULE_FLAG_NEED_DEPS           0x0002
/* "was seen in modules.dep": */
#define MODULE_FLAG_FOUND_IN_MODDEP     0x0004
#define MODULE_FLAG_BLACKLISTED         0x0008
#define MODULE_FLAG_BUILTIN             0x0010

struct globals {
	llist_t *probes; /* MEs of module(s) requested on cmdline */
#if ENABLE_FEATURE_CMDLINE_MODULE_OPTIONS
	char *cmdline_mopts; /* module options from cmdline */
#endif
	int num_unresolved_deps;
	/* bool. "Did we have 'symbol:FOO' requested on cmdline?" */
	smallint need_symbols;
	struct utsname uts;
	module_db db;
} FIX_ALIASING;
#define G (*ptr_to_globals)
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
} while (0)


static int read_config(const char *path);

static char *gather_options_str(char *opts, const char *append)
{
	/* Speed-optimized. We call gather_options_str many times. */
	if (append) {
		if (opts == NULL) {
			opts = xstrdup(append);
		} else {
			int optlen = strlen(opts);
			opts = xrealloc(opts, optlen + strlen(append) + 2);
			sprintf(opts + optlen, " %s", append);
		}
	}
	return opts;
}

static struct module_entry *get_or_add_modentry(const char *module)
{
	return moddb_get_or_create(&G.db, module);
}

static void add_probe(const char *name)
{
	struct module_entry *m;

	m = get_or_add_modentry(name);
	if (!(option_mask32 & (OPT_REMOVE | OPT_SHOW_DEPS))
	 && (m->flags & (MODULE_FLAG_LOADED | MODULE_FLAG_BUILTIN))
	) {
		DBG("skipping %s, it is already loaded", name);
		return;
	}

	DBG("queuing %s", name);
	m->probed_name = name;
	m->flags |= MODULE_FLAG_NEED_DEPS;
	llist_add_to_end(&G.probes, m);
	G.num_unresolved_deps++;
	if (ENABLE_FEATURE_MODUTILS_SYMBOLS
	 && is_prefixed_with(m->modname, "symbol:")
	) {
		G.need_symbols = 1;
	}
}

static int FAST_FUNC config_file_action(const char *filename,
					struct stat *statbuf UNUSED_PARAM,
					void *userdata UNUSED_PARAM,
					int depth)
{
	char *tokens[3];
	parser_t *p;
	struct module_entry *m;
	int rc = TRUE;
	const char *base, *ext;

	/* Skip files that begin with a "." */
	base = bb_basename(filename);
	if (base[0] == '.')
		goto error;

	/* "man modprobe.d" from kmod version 22 suggests
	 * that we shouldn't recurse into /etc/modprobe.d/dir/
	 * _subdirectories_:
	 */
	if (depth > 1)
		return SKIP; /* stop recursing */
//TODO: instead, can use dirAction in recursive_action() to SKIP dirs
//on depth == 1 level. But that's more code...

	/* In dir recursion, skip files that do not end with a ".conf"
	 * depth==0: read_config("modules.{symbols,alias}") must work,
	 * "include FILE_NOT_ENDING_IN_CONF" must work too.
	 */
	if (depth != 0) {
		ext = strrchr(base, '.');
		if (ext == NULL || strcmp(ext + 1, "conf"))
			goto error;
	}

	p = config_open2(filename, fopen_for_read);
	if (p == NULL) {
		rc = FALSE;
		goto error;
	}

	while (config_read(p, tokens, 3, 2, "# \t", PARSE_NORMAL)) {
//Use index_in_strings?
		if (strcmp(tokens[0], "alias") == 0) {
			/* alias <wildcard> <modulename> */
			llist_t *l;
			char wildcard[MODULE_NAME_LEN];
			char *rmod;

			if (tokens[2] == NULL)
				continue;
			filename2modname(tokens[1], wildcard);

			for (l = G.probes; l; l = l->link) {
				m = (struct module_entry *) l->data;
				if (fnmatch(wildcard, m->modname, 0) != 0)
					continue;
				rmod = filename2modname(tokens[2], NULL);
				llist_add_to(&m->realnames, rmod);

				if (m->flags & MODULE_FLAG_NEED_DEPS) {
					m->flags &= ~MODULE_FLAG_NEED_DEPS;
					G.num_unresolved_deps--;
				}

				m = get_or_add_modentry(rmod);
				if (!(m->flags & MODULE_FLAG_NEED_DEPS)) {
					m->flags |= MODULE_FLAG_NEED_DEPS;
					G.num_unresolved_deps++;
				}
			}
		} else if (strcmp(tokens[0], "options") == 0) {
			/* options <modulename> <option...> */
			if (tokens[2] == NULL)
				continue;
			m = get_or_add_modentry(tokens[1]);
			m->options = gather_options_str(m->options, tokens[2]);
		} else if (strcmp(tokens[0], "include") == 0) {
			/* include <filename>/<dirname> (yes, directories also must work) */
			read_config(tokens[1]);
		} else if (ENABLE_FEATURE_MODPROBE_BLACKLIST
		 && strcmp(tokens[0], "blacklist") == 0
		) {
			/* blacklist <modulename> */
			get_or_add_modentry(tokens[1])->flags |= MODULE_FLAG_BLACKLISTED;
		}
	}
	config_close(p);
 error:
	return rc;
}

static int read_config(const char *path)
{
	return recursive_action(path, ACTION_RECURSE | ACTION_QUIET,
				config_file_action, NULL, NULL,
				/*depth:*/ 0);
}

static const char *humanly_readable_name(struct module_entry *m)
{
	/* probed_name may be NULL. modname always exists. */
	return m->probed_name ? m->probed_name : m->modname;
}

/* Like strsep(&stringp, "\n\t ") but quoted text goes to single token
 * even if it contains whitespace.
 */
static char *strsep_quotes(char **stringp)
{
	char *s, *start = *stringp;

	if (!start)
		return NULL;

	for (s = start; ; s++) {
		switch (*s) {
		case '"':
			s = strchrnul(s + 1, '"'); /* find trailing quote */
			if (*s != '\0')
				s++; /* skip trailing quote */
			/* fall through */
		case '\0':
		case '\n':
		case '\t':
		case ' ':
			if (*s != '\0') {
				*s = '\0';
				*stringp = s + 1;
			} else {
				*stringp = NULL;
			}
			return start;
		}
	}
}

static char *parse_and_add_kcmdline_module_options(char *options, const char *modulename)
{
	char *kcmdline_buf;
	char *kcmdline;
	char *kptr;

	kcmdline_buf = xmalloc_open_read_close("/proc/cmdline", NULL);
	if (!kcmdline_buf)
		return options;

	kcmdline = kcmdline_buf;
	while ((kptr = strsep_quotes(&kcmdline)) != NULL) {
		char *after_modulename = is_prefixed_with(kptr, modulename);
		if (!after_modulename || *after_modulename != '.')
			continue;
		/* It is "modulename.xxxx" */
		kptr = after_modulename + 1;
		if (strchr(kptr, '=') != NULL) {
			/* It is "modulename.opt=[val]" */
			options = gather_options_str(options, kptr);
		}
	}
	free(kcmdline_buf);

	return options;
}

/* Return: similar to bb_init_module:
 * 0 on success,
 * -errno on open/read error,
 * errno on init_module() error
 */
/* NB: INSMOD_OPT_SILENT bit suppresses ONLY non-existent modules,
 * not deleted ones (those are still listed in modules.dep).
 * module-init-tools version 3.4:
 * # modprobe bogus
 * FATAL: Module bogus not found. [exitcode 1]
 * # modprobe -q bogus            [silent, exitcode still 1]
 * but:
 * # rm kernel/drivers/net/dummy.ko
 * # modprobe -q dummy
 * FATAL: Could not open '/lib/modules/xxx/kernel/drivers/net/dummy.ko': No such file or directory
 * [exitcode 1]
 */
static int do_modprobe(struct module_entry *m)
{
	int rc, first;

	if (!(m->flags & MODULE_FLAG_FOUND_IN_MODDEP)) {
		if (!(option_mask32 & INSMOD_OPT_SILENT))
			bb_error_msg((m->flags & MODULE_FLAG_BUILTIN) ?
				     "module %s is builtin" :
				     "module %s not found in modules.dep",
				     humanly_readable_name(m));
		return -ENOENT;
	}
	DBG("do_modprob'ing %s", m->modname);

	if (!(option_mask32 & OPT_REMOVE))
		m->deps = llist_rev(m->deps);

	if (0) {
		llist_t *l;
		for (l = m->deps; l; l = l->link)
			DBG("dep: %s", l->data);
	}

	first = 1;
	rc = 0;
	while (m->deps) {
		struct module_entry *m2;
		char *fn, *options;

		rc = 0;
		fn = llist_pop(&m->deps); /* we leak it */
		m2 = get_or_add_modentry(bb_get_last_path_component_nostrip(fn));

		if (option_mask32 & OPT_REMOVE) {
			/* modprobe -r */
			if (m2->flags & MODULE_FLAG_LOADED) {
				rc = bb_delete_module(m2->modname, O_EXCL);
				if (rc) {
					if (first) {
						bb_perror_msg("can't unload module '%s'",
							humanly_readable_name(m2));
						break;
					}
				} else {
					m2->flags &= ~MODULE_FLAG_LOADED;
				}
			}
			/* do not error out if *deps* fail to unload */
			first = 0;
			continue;
		}

		options = m2->options;
		m2->options = NULL;
		options = parse_and_add_kcmdline_module_options(options, m2->modname);
#if ENABLE_FEATURE_CMDLINE_MODULE_OPTIONS
		if (m == m2)
			options = gather_options_str(options, G.cmdline_mopts);
#endif

		if (option_mask32 & OPT_SHOW_DEPS) {
			printf(options ? "insmod %s/%s/%s %s\n"
					: "insmod %s/%s/%s\n",
				CONFIG_DEFAULT_MODULES_DIR, G.uts.release, fn,
				options);
			free(options);
			continue;
		}

		if (m2->flags & MODULE_FLAG_LOADED) {
			DBG("%s is already loaded, skipping", fn);
			free(options);
			continue;
		}

		rc = bb_init_module(fn, options);
		DBG("loaded %s '%s', rc:%d", fn, options, rc);
		if (rc == EEXIST)
			rc = 0;
		free(options);
		if (rc) {
			bb_error_msg("can't load module %s (%s): %s",
				humanly_readable_name(m2),
				fn,
				moderror(rc)
			);
			break;
		}
		m2->flags |= MODULE_FLAG_LOADED;
	}

	return rc;
}

static void load_modules_dep(void)
{
	struct module_entry *m;
	char *colon, *tokens[2];
	parser_t *p;

	/* Modprobe does not work at all without modules.dep,
	 * even if the full module name is given. Returning error here
	 * was making us later confuse user with this message:
	 * "module /full/path/to/existing/file/module.ko not found".
	 * It's better to die immediately, with good message.
	 * xfopen_for_read provides that. */
	p = config_open2(CONFIG_DEFAULT_DEPMOD_FILE, xfopen_for_read);

	while (G.num_unresolved_deps
	 && config_read(p, tokens, 2, 1, "# \t", PARSE_NORMAL)
	) {
		colon = last_char_is(tokens[0], ':');
		if (colon == NULL)
			continue;
		*colon = '\0';

		m = moddb_get(&G.db, bb_get_last_path_component_nostrip(tokens[0]));
		if (m == NULL)
			continue;

		/* Optimization... */
		if ((m->flags & MODULE_FLAG_LOADED)
		 && !(option_mask32 & (OPT_REMOVE | OPT_SHOW_DEPS))
		) {
			DBG("skip deps of %s, it's already loaded", tokens[0]);
			continue;
		}

		m->flags |= MODULE_FLAG_FOUND_IN_MODDEP;
		if ((m->flags & MODULE_FLAG_NEED_DEPS) && (m->deps == NULL)) {
			G.num_unresolved_deps--;
			llist_add_to(&m->deps, xstrdup(tokens[0]));
			if (tokens[1])
				string_to_llist(tokens[1], &m->deps, " \t");
		} else
			DBG("skipping dep line");
	}
	config_close(p);
}

int modprobe_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int modprobe_main(int argc UNUSED_PARAM, char **argv)
{
	int rc;
	unsigned opt;
	struct module_entry *me;

	INIT_G();

	opt = getopt32long(argv, "^" INSMOD_OPTS MODPROBE_OPTS "\0" MODPROBE_COMPLEMENTARY,
			modprobe_longopts
			INSMOD_ARGS
	);
	argv += optind;

	/* Goto modules location */
	xchdir(CONFIG_DEFAULT_MODULES_DIR);
	uname(&G.uts);
	xchdir(G.uts.release);

	if (opt & OPT_LIST_ONLY) {
		int i;
		char *colon, *tokens[2];
		parser_t *p = config_open2(CONFIG_DEFAULT_DEPMOD_FILE, xfopen_for_read);

		for (i = 0; argv[i]; i++)
			replace(argv[i], '-', '_');

		while (config_read(p, tokens, 2, 1, "# \t", PARSE_NORMAL)) {
			colon = last_char_is(tokens[0], ':');
			if (!colon)
				continue;
			*colon = '\0';
			if (!argv[0])
				puts(tokens[0]);
			else {
				char name[MODULE_NAME_LEN];
				filename2modname(
					bb_get_last_path_component_nostrip(tokens[0]),
					name
				);
				for (i = 0; argv[i]; i++) {
					if (fnmatch(argv[i], name, 0) == 0) {
						puts(tokens[0]);
					}
				}
			}
		}
		return EXIT_SUCCESS;
	}

	/* Yes, for some reason -l ignores -s... */
	if (opt & INSMOD_OPT_SYSLOG)
		logmode = LOGMODE_SYSLOG;

	if (!argv[0]) {
		if (opt & OPT_REMOVE) {
			/* "modprobe -r" (w/o params).
			 * "If name is NULL, all unused modules marked
			 * autoclean will be removed".
			 */
			if (bb_delete_module(NULL, O_NONBLOCK | O_EXCL) != 0)
				bb_perror_nomsg_and_die();
		}
		return EXIT_SUCCESS;
	}

	/* Retrieve module names of already loaded modules */
	{
		char *s;
		parser_t *parser = config_open2("/proc/modules", fopen_for_read);
		while (config_read(parser, &s, 1, 1, "# \t", PARSE_NORMAL & ~PARSE_GREEDY))
			get_or_add_modentry(s)->flags |= MODULE_FLAG_LOADED;
		config_close(parser);

		parser = config_open2("modules.builtin", fopen_for_read);
		while (config_read(parser, &s, 1, 1, "# \t", PARSE_NORMAL))
			get_or_add_modentry(s)->flags |= MODULE_FLAG_BUILTIN;
		config_close(parser);
	}

	if (opt & (OPT_INSERT_ALL | OPT_REMOVE)) {
		/* Each argument is a module name */
		do {
			DBG("adding module %s", *argv);
			add_probe(*argv++);
		} while (*argv);
	} else {
		/* First argument is module name, rest are parameters */
		DBG("probing just module %s", *argv);
		add_probe(argv[0]);
#if ENABLE_FEATURE_CMDLINE_MODULE_OPTIONS
		G.cmdline_mopts = parse_cmdline_module_options(argv, /*quote_spaces:*/ 1);
#endif
	}

	/* Happens if all requested modules are already loaded */
	if (G.probes == NULL)
		return EXIT_SUCCESS;

	read_config("/etc/modprobe.conf");
	read_config("/etc/modprobe.d");
	if (ENABLE_FEATURE_MODUTILS_SYMBOLS && G.need_symbols)
		read_config("modules.symbols");
	load_modules_dep();
	if (ENABLE_FEATURE_MODUTILS_ALIAS && G.num_unresolved_deps) {
		read_config("modules.alias");
		load_modules_dep();
	}

	rc = 0;
	while ((me = llist_pop(&G.probes)) != NULL) {
		if (me->realnames == NULL) {
			DBG("probing by module name");
			/* This is not an alias. Literal names are blacklisted
			 * only if '-b' is given.
			 */
			if (!(opt & OPT_BLACKLIST)
			 || !(me->flags & MODULE_FLAG_BLACKLISTED)
			) {
				rc |= do_modprobe(me);
			}
			continue;
		}

		/* Probe all real names for the alias */
		do {
			char *realname = llist_pop(&me->realnames);
			struct module_entry *m2;

			DBG("probing alias %s by realname %s", me->modname, realname);
			m2 = get_or_add_modentry(realname);
			if (!(m2->flags & MODULE_FLAG_BLACKLISTED)
			 && (!(m2->flags & MODULE_FLAG_LOADED)
			    || (opt & (OPT_REMOVE | OPT_SHOW_DEPS)))
			) {
//TODO: we can pass "me" as 2nd param to do_modprobe,
//and make do_modprobe emit more meaningful error messages
//with alias name included, not just module name alias resolves to.
				rc |= do_modprobe(m2);
			}
			free(realname);
		} while (me->realnames != NULL);
	}

	if (ENABLE_FEATURE_CLEAN_UP)
		moddb_free(&G.db);

	return (rc != 0);
}
