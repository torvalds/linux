/* vi: set sw=4 ts=4: */
/*
 * simplified modprobe
 *
 * Copyright (c) 2008 Vladimir Dronnikov
 * Copyright (c) 2008 Bernhard Reutner-Fischer (initial depmod code)
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

/* modprobe-small configs are defined in Config.src to ensure better
 * "make config" order */

//applet:IF_LSMOD(   IF_MODPROBE_SMALL(APPLET_NOEXEC( lsmod,    lsmod,    BB_DIR_SBIN, BB_SUID_DROP, lsmod   )))
//applet:IF_MODPROBE(IF_MODPROBE_SMALL(APPLET_NOEXEC( modprobe, modprobe, BB_DIR_SBIN, BB_SUID_DROP, modprobe)))
//                                     APPLET_ODDNAME:name      main      location     suid_type     help
//applet:IF_DEPMOD(  IF_MODPROBE_SMALL(APPLET_ODDNAME(depmod,   modprobe, BB_DIR_SBIN, BB_SUID_DROP, depmod  )))
//applet:IF_INSMOD(  IF_MODPROBE_SMALL(APPLET_NOEXEC( insmod,   modprobe, BB_DIR_SBIN, BB_SUID_DROP, insmod  )))
//applet:IF_RMMOD(   IF_MODPROBE_SMALL(APPLET_NOEXEC( rmmod,    modprobe, BB_DIR_SBIN, BB_SUID_DROP, rmmod   )))
/* noexec speeds up boot with many modules loaded (need SH_STANDALONE=y) */
/* I measured about ~5 times faster insmod */
/* depmod is not noexec, it runs longer and benefits from memory trimming via exec */

//kbuild:lib-$(CONFIG_MODPROBE_SMALL) += modprobe-small.o

#include "libbb.h"
/* After libbb.h, since it needs sys/types.h on some systems */
#include <sys/utsname.h> /* uname() */
#include <fnmatch.h>
#include <sys/syscall.h>

#define init_module(mod, len, opts) syscall(__NR_init_module, mod, len, opts)
#define delete_module(mod, flags) syscall(__NR_delete_module, mod, flags)
#ifdef __NR_finit_module
# define finit_module(fd, uargs, flags) syscall(__NR_finit_module, fd, uargs, flags)
#endif
/* linux/include/linux/module.h has limit of 64 chars on module names */
#undef MODULE_NAME_LEN
#define MODULE_NAME_LEN 64


#if 1
# define dbg1_error_msg(...) ((void)0)
# define dbg2_error_msg(...) ((void)0)
#else
# define dbg1_error_msg(...) bb_error_msg(__VA_ARGS__)
# define dbg2_error_msg(...) bb_error_msg(__VA_ARGS__)
#endif

#define DEPFILE_BB CONFIG_DEFAULT_DEPMOD_FILE".bb"

//usage:#if ENABLE_MODPROBE_SMALL

//usage:#define lsmod_trivial_usage
//usage:       ""
//usage:#define lsmod_full_usage "\n\n"
//usage:       "List loaded kernel modules"

//usage:#endif

#if ENABLE_LSMOD
int lsmod_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int lsmod_main(int argc UNUSED_PARAM, char **argv UNUSED_PARAM)
{
	xprint_and_close_file(xfopen_for_read("/proc/modules"));
	return EXIT_SUCCESS;
}
#endif

/* Num of applets that use modprobe_main() entry point. */
/* lsmod is not here. */
#define MOD_APPLET_CNT (ENABLE_MODPROBE + ENABLE_DEPMOD + ENABLE_INSMOD + ENABLE_RMMOD)

/* Do not bother if MODPROBE_SMALL=y but no applets selected. */
/* The rest of the file is in this if block. */
#if MOD_APPLET_CNT > 0

#define ONLY_APPLET (MOD_APPLET_CNT == 1)
#define is_modprobe (ENABLE_MODPROBE && (ONLY_APPLET || applet_name[0] == 'm'))
#define is_depmod   (ENABLE_DEPMOD   && (ONLY_APPLET || applet_name[0] == 'd'))
#define is_insmod   (ENABLE_INSMOD   && (ONLY_APPLET || applet_name[0] == 'i'))
#define is_rmmod    (ENABLE_RMMOD    && (ONLY_APPLET || applet_name[0] == 'r'))

enum {
	DEPMOD_OPT_n = (1 << 0), /* dry-run, print to stdout */
	OPT_q = (1 << 0), /* be quiet */
	OPT_r = (1 << 1), /* module removal instead of loading */
};

typedef struct module_info {
	char *pathname;
	char *aliases;
	char *deps;
	smallint open_read_failed;
} module_info;

/*
 * GLOBALS
 */
struct globals {
	module_info *modinfo;
	char *module_load_options;
	smallint dep_bb_seen;
	smallint wrote_dep_bb_ok;
	unsigned module_count;
	int module_found_idx;
	unsigned stringbuf_idx;
	unsigned stringbuf_size;
	char *stringbuf; /* some modules have lots of stuff */
	/* for example, drivers/media/video/saa7134/saa7134.ko */
	/* therefore having a fixed biggish buffer is not wise */
};
#define G (*ptr_to_globals)
#define modinfo             (G.modinfo            )
#define dep_bb_seen         (G.dep_bb_seen        )
#define wrote_dep_bb_ok     (G.wrote_dep_bb_ok    )
#define module_count        (G.module_count       )
#define module_found_idx    (G.module_found_idx   )
#define module_load_options (G.module_load_options)
#define stringbuf_idx       (G.stringbuf_idx      )
#define stringbuf_size      (G.stringbuf_size     )
#define stringbuf           (G.stringbuf          )
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
} while (0)

static void append(const char *s)
{
	unsigned len = strlen(s);
	if (stringbuf_idx + len + 15 > stringbuf_size) {
		stringbuf_size = stringbuf_idx + len + 127;
		dbg2_error_msg("grow stringbuf to %u", stringbuf_size);
		stringbuf = xrealloc(stringbuf, stringbuf_size);
	}
	memcpy(stringbuf + stringbuf_idx, s, len);
	stringbuf_idx += len;
}

static void appendc(char c)
{
	/* We appendc() only after append(), + 15 trick in append()
	 * makes it unnecessary to check for overflow here */
	stringbuf[stringbuf_idx++] = c;
}

static void bksp(void)
{
	if (stringbuf_idx)
		stringbuf_idx--;
}

static void reset_stringbuf(void)
{
	stringbuf_idx = 0;
}

static char* copy_stringbuf(void)
{
	char *copy = xzalloc(stringbuf_idx + 1); /* terminating NUL */
	return memcpy(copy, stringbuf, stringbuf_idx);
}

static char* find_keyword(char *ptr, size_t len, const char *word)
{
	if (!ptr) /* happens if xmalloc_open_zipped_read_close cannot read it */
		return NULL;

	len -= strlen(word) - 1;
	while ((ssize_t)len > 0) {
		char *old = ptr;
		char *after_word;

		/* search for the first char in word */
		ptr = memchr(ptr, word[0], len);
		if (ptr == NULL) /* no occurrence left, done */
			break;
		after_word = is_prefixed_with(ptr, word);
		if (after_word)
			return after_word; /* found, return ptr past it */
		++ptr;
		len -= (ptr - old);
	}
	return NULL;
}

static void replace(char *s, char what, char with)
{
	while (*s) {
		if (what == *s)
			*s = with;
		++s;
	}
}

static char *filename2modname(const char *filename, char *modname)
{
	int i;
	const char *from;

	// Disabled since otherwise "modprobe dir/name" would work
	// as if it is "modprobe name". It is unclear why
	// 'basenamization' was here in the first place.
	//from = bb_get_last_path_component_nostrip(filename);
	from = filename;
	for (i = 0; i < (MODULE_NAME_LEN-1) && from[i] != '\0' && from[i] != '.'; i++)
		modname[i] = (from[i] == '-') ? '_' : from[i];
	modname[i] = '\0';

	return modname;
}

static int pathname_matches_modname(const char *pathname, const char *modname)
{
	int r;
	char name[MODULE_NAME_LEN];
	filename2modname(bb_get_last_path_component_nostrip(pathname), name);
	r = (strcmp(name, modname) == 0);
	return r;
}

/* Take "word word", return malloced "word",NUL,"word",NUL,NUL */
static char* str_2_list(const char *str)
{
	int len = strlen(str) + 1;
	char *dst = xmalloc(len + 1);

	dst[len] = '\0';
	memcpy(dst, str, len);
//TODO: protect against 2+ spaces: "word  word"
	replace(dst, ' ', '\0');
	return dst;
}

/* We use error numbers in a loose translation... */
static const char *moderror(int err)
{
	switch (err) {
	case ENOEXEC:
		return "invalid module format";
	case ENOENT:
		return "unknown symbol in module or invalid parameter";
	case ESRCH:
		return "module has wrong symbol version";
	case EINVAL: /* "invalid parameter" */
		return "unknown symbol in module or invalid parameter"
		+ sizeof("unknown symbol in module or");
	default:
		return strerror(err);
	}
}

static int load_module(const char *fname, const char *options)
{
#if 1
	int r;
	size_t len = MAXINT(ssize_t);
	char *module_image;

	if (!options)
		options = "";

	dbg1_error_msg("load_module('%s','%s')", fname, options);

	/*
	 * First we try finit_module if available.  Some kernels are configured
	 * to only allow loading of modules off of secure storage (like a read-
	 * only rootfs) which needs the finit_module call.  If it fails, we fall
	 * back to normal module loading to support compressed modules.
	 */
	r = 1;
# ifdef __NR_finit_module
	{
		int fd = open(fname, O_RDONLY | O_CLOEXEC);
		if (fd >= 0) {
			r = finit_module(fd, options, 0) != 0;
			close(fd);
		}
	}
# endif
	if (r != 0) {
		module_image = xmalloc_open_zipped_read_close(fname, &len);
		r = (!module_image || init_module(module_image, len, options) != 0);
		free(module_image);
	}

	dbg1_error_msg("load_module:%d", r);
	return r; /* 0 = success */
#else
	/* For testing */
	dbg1_error_msg("load_module('%s','%s')", fname, options);
	return 1;
#endif
}

/* Returns !0 if open/read was unsuccessful */
static int parse_module(module_info *info, const char *pathname)
{
	char *module_image;
	char *ptr;
	size_t len;
	size_t pos;
	dbg1_error_msg("parse_module('%s')", pathname);

	/* Read (possibly compressed) module */
	errno = 0;
	len = 64 * 1024 * 1024; /* 64 Mb at most */
	module_image = xmalloc_open_zipped_read_close(pathname, &len);
	/* module_image == NULL is ok here, find_keyword handles it */
//TODO: optimize redundant module body reads

	/* "alias1 symbol:sym1 alias2 symbol:sym2" */
	reset_stringbuf();
	pos = 0;
	while (1) {
		unsigned start = stringbuf_idx;
		ptr = find_keyword(module_image + pos, len - pos, "alias=");
		if (!ptr) {
			ptr = find_keyword(module_image + pos, len - pos, "__ksymtab_");
			if (!ptr)
				break;
			/* DOCME: __ksymtab_gpl and __ksymtab_strings occur
			 * in many modules. What do they mean? */
			if (strcmp(ptr, "gpl") == 0 || strcmp(ptr, "strings") == 0)
				goto skip;
			dbg2_error_msg("alias:'symbol:%s'", ptr);
			append("symbol:");
		} else {
			dbg2_error_msg("alias:'%s'", ptr);
		}
		append(ptr);
		appendc(' ');
		/*
		 * Don't add redundant aliases, such as:
		 * libcrc32c.ko symbol:crc32c symbol:crc32c
		 */
		if (start) { /* "if we aren't the first alias" */
			char *found, *last;
			stringbuf[stringbuf_idx] = '\0';
			last = stringbuf + start;
			/*
			 * String at last-1 is " symbol:crc32c "
			 * (with both leading and trailing spaces).
			 */
			if (strncmp(stringbuf, last, stringbuf_idx - start) == 0)
				/* First alias matches us */
				found = stringbuf;
			else
				/* Does any other alias match? */
				found = strstr(stringbuf, last-1);
			if (found < last-1) {
				/* There is absolutely the same string before us */
				dbg2_error_msg("redundant:'%s'", last);
				stringbuf_idx = start;
				goto skip;
			}
		}
 skip:
		pos = (ptr - module_image);
	}
	bksp(); /* remove last ' ' */
	info->aliases = copy_stringbuf();
	replace(info->aliases, '-', '_');

	/* "dependency1 depandency2" */
	reset_stringbuf();
	ptr = find_keyword(module_image, len, "depends=");
	if (ptr && *ptr) {
		replace(ptr, ',', ' ');
		replace(ptr, '-', '_');
		dbg2_error_msg("dep:'%s'", ptr);
		append(ptr);
	}
	free(module_image);
	info->deps = copy_stringbuf();

	info->open_read_failed = (module_image == NULL);
	return info->open_read_failed;
}

static FAST_FUNC int fileAction(const char *pathname,
		struct stat *sb UNUSED_PARAM,
		void *modname_to_match,
		int depth UNUSED_PARAM)
{
	int cur;
	const char *fname;
	bool is_remove = (ENABLE_RMMOD && ONLY_APPLET)
		|| ((ENABLE_RMMOD || ENABLE_MODPROBE) && (option_mask32 & OPT_r));

	pathname += 2; /* skip "./" */
	fname = bb_get_last_path_component_nostrip(pathname);
	if (!strrstr(fname, ".ko")) {
		dbg1_error_msg("'%s' is not a module", pathname);
		return TRUE; /* not a module, continue search */
	}

	cur = module_count++;
	modinfo = xrealloc_vector(modinfo, 12, cur);
	modinfo[cur].pathname = xstrdup(pathname);
	/*modinfo[cur].aliases = NULL; - xrealloc_vector did it */
	/*modinfo[cur+1].pathname = NULL;*/

	if (!pathname_matches_modname(fname, modname_to_match)) {
		dbg1_error_msg("'%s' module name doesn't match", pathname);
		return TRUE; /* module name doesn't match, continue search */
	}

	dbg1_error_msg("'%s' module name matches", pathname);
	module_found_idx = cur;
	if (parse_module(&modinfo[cur], pathname) != 0)
		return TRUE; /* failed to open/read it, no point in trying loading */

	if (!is_remove) {
		if (load_module(pathname, module_load_options) == 0) {
			/* Load was successful, there is nothing else to do.
			 * This can happen ONLY for "top-level" module load,
			 * not a dep, because deps don't do dirscan. */
			exit(EXIT_SUCCESS);
		}
	}

	return TRUE;
}

static int load_dep_bb(void)
{
	char *line;
	FILE *fp = fopen_for_read(DEPFILE_BB);

	if (!fp)
		return 0;

	dep_bb_seen = 1;
	dbg1_error_msg("loading "DEPFILE_BB);

	/* Why? There is a rare scenario: we did not find modprobe.dep.bb,
	 * we scanned the dir and found no module by name, then we search
	 * for alias (full scan), and we decided to generate modprobe.dep.bb.
	 * But we see modprobe.dep.bb.new! Other modprobe is at work!
	 * We wait and other modprobe renames it to modprobe.dep.bb.
	 * Now we can use it.
	 * But we already have modinfo[] filled, and "module_count = 0"
	 * makes us start anew. Yes, we leak modinfo[].xxx pointers -
	 * there is not much of data there anyway. */
	module_count = 0;
	memset(&modinfo[0], 0, sizeof(modinfo[0]));

	while ((line = xmalloc_fgetline(fp)) != NULL) {
		char* space;
		char* linebuf;
		int cur;

		if (!line[0]) {
			free(line);
			continue;
		}
		space = strchrnul(line, ' ');
		cur = module_count++;
		modinfo = xrealloc_vector(modinfo, 12, cur);
		/*modinfo[cur+1].pathname = NULL; - xrealloc_vector did it */
		modinfo[cur].pathname = line; /* we take ownership of malloced block here */
		if (*space)
			*space++ = '\0';
		modinfo[cur].aliases = space;
		linebuf = xmalloc_fgetline(fp);
		modinfo[cur].deps = linebuf ? linebuf : xzalloc(1);
		if (modinfo[cur].deps[0]) {
			/* deps are not "", so next line must be empty */
			line = xmalloc_fgetline(fp);
			/* Refuse to work with damaged config file */
			if (line && line[0])
				bb_error_msg_and_die("error in %s at '%s'", DEPFILE_BB, line);
			free(line);
		}
	}
	return 1;
}

static int start_dep_bb_writeout(void)
{
	int fd;

	/* depmod -n: write result to stdout */
	if (is_depmod && (option_mask32 & DEPMOD_OPT_n))
		return STDOUT_FILENO;

	fd = open(DEPFILE_BB".new", O_WRONLY | O_CREAT | O_TRUNC | O_EXCL, 0644);
	if (fd < 0) {
		if (errno == EEXIST) {
			int count = 5 * 20;
			dbg1_error_msg(DEPFILE_BB".new exists, waiting for "DEPFILE_BB);
			while (1) {
				usleep(1000*1000 / 20);
				if (load_dep_bb()) {
					dbg1_error_msg(DEPFILE_BB" appeared");
					return -2; /* magic number */
				}
				if (!--count)
					break;
			}
			bb_error_msg("deleting stale %s", DEPFILE_BB".new");
			fd = open_or_warn(DEPFILE_BB".new", O_WRONLY | O_CREAT | O_TRUNC);
		}
	}
	dbg1_error_msg("opened "DEPFILE_BB".new:%d", fd);
	return fd;
}

static void write_out_dep_bb(int fd)
{
	int i;
	FILE *fp;

	/* We want good error reporting. fdprintf is not good enough. */
	fp = xfdopen_for_write(fd);
	i = 0;
	while (modinfo[i].pathname) {
		fprintf(fp, "%s%s%s\n" "%s%s\n",
			modinfo[i].pathname, modinfo[i].aliases[0] ? " " : "", modinfo[i].aliases,
			modinfo[i].deps, modinfo[i].deps[0] ? "\n" : "");
		i++;
	}
	/* Badly formatted depfile is a no-no. Be paranoid. */
	errno = 0;
	if (ferror(fp) | fclose(fp)) /* | instead of || is intended */
		goto err;

	if (fd == STDOUT_FILENO) /* it was depmod -n */
		goto ok;

	if (rename(DEPFILE_BB".new", DEPFILE_BB) != 0) {
 err:
		bb_perror_msg("can't create '%s'", DEPFILE_BB);
		unlink(DEPFILE_BB".new");
	} else {
 ok:
		wrote_dep_bb_ok = 1;
		dbg1_error_msg("created "DEPFILE_BB);
	}
}

static module_info** find_alias(const char *alias)
{
	int i;
	int dep_bb_fd;
	int infoidx;
	module_info **infovec;
	dbg1_error_msg("find_alias('%s')", alias);

 try_again:
	/* First try to find by name (cheaper) */
	i = 0;
	while (modinfo[i].pathname) {
		if (pathname_matches_modname(modinfo[i].pathname, alias)) {
			dbg1_error_msg("found '%s' in module '%s'",
					alias, modinfo[i].pathname);
			if (!modinfo[i].aliases) {
				parse_module(&modinfo[i], modinfo[i].pathname);
			}
			infovec = xzalloc(2 * sizeof(infovec[0]));
			infovec[0] = &modinfo[i];
			return infovec;
		}
		i++;
	}

	/* Ok, we definitely have to scan module bodies. This is a good
	 * moment to generate modprobe.dep.bb, if it does not exist yet */
	dep_bb_fd = dep_bb_seen ? -1 : start_dep_bb_writeout();
	if (dep_bb_fd == -2) /* modprobe.dep.bb appeared? */
		goto try_again;

	/* Scan all module bodies, extract modinfo (it contains aliases) */
	i = 0;
	infoidx = 0;
	infovec = NULL;
	while (modinfo[i].pathname) {
		char *desc, *s;
		if (!modinfo[i].aliases) {
			parse_module(&modinfo[i], modinfo[i].pathname);
		}
		/* "alias1 symbol:sym1 alias2 symbol:sym2" */
		desc = str_2_list(modinfo[i].aliases);
		/* Does matching substring exist? */
		for (s = desc; *s; s += strlen(s) + 1) {
			/* Aliases in module bodies can be defined with
			 * shell patterns. Example:
			 * "pci:v000010DEd000000D9sv*sd*bc*sc*i*".
			 * Plain strcmp() won't catch that */
			if (fnmatch(s, alias, 0) == 0) {
				dbg1_error_msg("found alias '%s' in module '%s'",
						alias, modinfo[i].pathname);
				infovec = xrealloc_vector(infovec, 1, infoidx);
				infovec[infoidx++] = &modinfo[i];
				break;
			}
		}
		free(desc);
		i++;
	}

	/* Create module.dep.bb if needed */
	if (dep_bb_fd >= 0) {
		write_out_dep_bb(dep_bb_fd);
	}

	dbg1_error_msg("find_alias '%s' returns %d results", alias, infoidx);
	return infovec;
}

#if ENABLE_FEATURE_MODPROBE_SMALL_CHECK_ALREADY_LOADED
// TODO: open only once, invent config_rewind()
static int already_loaded(const char *name)
{
	int ret;
	char *line;
	FILE *fp;

	ret = 5 * 2;
 again:
	fp = fopen_for_read("/proc/modules");
	if (!fp)
		return 0;
	while ((line = xmalloc_fgetline(fp)) != NULL) {
		char *live;
		char *after_name;

		// Examples from kernel 3.14.6:
		//pcspkr 12718 0 - Live 0xffffffffa017e000
		//snd_timer 28690 2 snd_seq,snd_pcm, Live 0xffffffffa025e000
		//i915 801405 2 - Live 0xffffffffa0096000
		after_name = is_prefixed_with(line, name);
		if (!after_name || *after_name != ' ') {
			free(line);
			continue;
		}
		live = strstr(line, " Live");
		free(line);
		if (!live) {
			/* State can be Unloading, Loading, or Live.
			 * modprobe must not return prematurely if we see "Loading":
			 * it can cause further programs to assume load completed,
			 * but it did not (yet)!
			 * Wait up to 5*20 ms for it to resolve.
			 */
			ret -= 2;
			if (ret == 0)
				break;  /* huh? report as "not loaded" */
			fclose(fp);
			usleep(20*1000);
			goto again;
		}
		ret = 1;
		break;
	}
	fclose(fp);

	return ret & 1;
}
#else
#define already_loaded(name) 0
#endif

static int rmmod(const char *filename)
{
	int r;
	char modname[MODULE_NAME_LEN];

	filename2modname(filename, modname);
	r = delete_module(modname, O_NONBLOCK | O_EXCL);
	dbg1_error_msg("delete_module('%s', O_NONBLOCK | O_EXCL):%d", modname, r);
	if (r != 0 && !(option_mask32 & OPT_q)) {
		bb_perror_msg("remove '%s'", modname);
	}
	return r;
}

/*
 * Given modules definition and module name (or alias, or symbol)
 * load/remove the module respecting dependencies.
 * NB: also called by depmod with bogus name "/",
 * just in order to force modprobe.dep.bb creation.
*/
#if !ENABLE_FEATURE_CMDLINE_MODULE_OPTIONS
#define process_module(a,b) process_module(a)
#define cmdline_options ""
#endif
static int process_module(char *name, const char *cmdline_options)
{
	char *s, *deps, *options;
	module_info **infovec;
	module_info *info;
	int infoidx;
	bool is_remove = (ENABLE_RMMOD && ONLY_APPLET)
		|| ((ENABLE_RMMOD || ENABLE_MODPROBE) && (option_mask32 & OPT_r));
	int exitcode = EXIT_SUCCESS;

	dbg1_error_msg("process_module('%s','%s')", name, cmdline_options);

	replace(name, '-', '_');

	dbg1_error_msg("already_loaded:%d is_remove:%d", already_loaded(name), is_remove);

	if (is_rmmod) {
		/* Does not remove dependencies, no need to scan, just remove.
		 * (compat note: this allows and strips .ko suffix)
		 */
		rmmod(name);
		return EXIT_SUCCESS;
	}

	/*
	 * We used to have "is_remove != already_loaded(name)" check here, but
	 *  modprobe -r pci:v00008086d00007010sv00000000sd00000000bc01sc01i80
	 * won't unload modules (there are more than one)
	 * which have this alias.
	 */
	if (!is_remove && already_loaded(name)) {
		dbg1_error_msg("nothing to do for '%s'", name);
		return EXIT_SUCCESS;
	}

	options = NULL;
	if (!is_remove) {
		char *opt_filename = xasprintf("/etc/modules/%s", name);
		options = xmalloc_open_read_close(opt_filename, NULL);
		if (options)
			replace(options, '\n', ' ');
#if ENABLE_FEATURE_CMDLINE_MODULE_OPTIONS
		if (cmdline_options) {
			/* NB: cmdline_options always have one leading ' '
			 * (see main()), we remove it here */
			char *op = xasprintf(options ? "%s %s" : "%s %s" + 3,
						cmdline_options + 1, options);
			free(options);
			options = op;
		}
#endif
		free(opt_filename);
		module_load_options = options;
		dbg1_error_msg("process_module('%s'): options:'%s'", name, options);
	}

	if (!module_count) {
		/* Scan module directory. This is done only once.
		 * It will attempt module load, and will exit(EXIT_SUCCESS)
		 * on success.
		 */
		module_found_idx = -1;
		recursive_action(".",
			ACTION_RECURSE, /* flags */
			fileAction, /* file action */
			NULL, /* dir action */
			name, /* user data */
			0 /* depth */
		);
		dbg1_error_msg("dirscan complete");
		/* Module was not found, or load failed, or is_remove */
		if (module_found_idx >= 0) { /* module was found */
			infovec = xzalloc(2 * sizeof(infovec[0]));
			infovec[0] = &modinfo[module_found_idx];
		} else { /* search for alias, not a plain module name */
			infovec = find_alias(name);
		}
	} else {
		infovec = find_alias(name);
	}

	if (!infovec) {
		/* both dirscan and find_alias found nothing */
		if (!is_remove && !is_depmod) { /* it wasn't rmmod or depmod */
			bb_error_msg("module '%s' not found", name);
//TODO: _and_die()? or should we continue (un)loading modules listed on cmdline?
			/* "modprobe non-existing-module; echo $?" must print 1 */
			exitcode = EXIT_FAILURE;
		}
		goto ret;
	}

	/* There can be more than one module for the given alias. For example,
	 * "pci:v00008086d00007010sv00000000sd00000000bc01sc01i80" matches
	 * ata_piix because it has alias "pci:v00008086d00007010sv*sd*bc*sc*i*"
	 * and ata_generic, it has alias "pci:v*d*sv*sd*bc01sc01i*"
	 * Standard modprobe loads them both. We achieve it by returning
	 * a *list* of modinfo pointers from find_alias().
	 */

	/* modprobe -r? unload module(s) */
	if (is_remove) {
		infoidx = 0;
		while ((info = infovec[infoidx++]) != NULL) {
			int r = rmmod(bb_get_last_path_component_nostrip(info->pathname));
			if (r != 0) {
				goto ret; /* error */
			}
		}
		/* modprobe -r: we do not stop here -
		 * continue to unload modules on which the module depends:
		 * "-r --remove: option causes modprobe to remove a module.
		 * If the modules it depends on are also unused, modprobe
		 * will try to remove them, too."
		 */
	}

	infoidx = 0;
	while ((info = infovec[infoidx++]) != NULL) {
		/* Iterate thru dependencies, trying to (un)load them */
		deps = str_2_list(info->deps);
		for (s = deps; *s; s += strlen(s) + 1) {
			//if (strcmp(name, s) != 0) // N.B. do loops exist?
			dbg1_error_msg("recurse on dep '%s'", s);
			process_module(s, NULL);
			dbg1_error_msg("recurse on dep '%s' done", s);
		}
		free(deps);

		if (is_remove)
			continue;

		/* We are modprobe: load it */
		if (options && strstr(options, "blacklist")) {
			dbg1_error_msg("'%s': blacklisted", info->pathname);
			continue;
		}
		if (info->open_read_failed) {
			/* We already tried it, didn't work. Don't try load again */
			exitcode = EXIT_FAILURE;
			continue;
		}
		errno = 0;
		if (load_module(info->pathname, options) != 0) {
			if (EEXIST != errno) {
				bb_error_msg("'%s': %s",
					info->pathname,
					moderror(errno));
			} else {
				dbg1_error_msg("'%s': %s",
					info->pathname,
					moderror(errno));
			}
			exitcode = EXIT_FAILURE;
		}
	}
 ret:
	free(infovec);
	free(options);

	return exitcode;
}
#undef cmdline_options


/* For reference, module-init-tools v3.4 options:

# insmod
Usage: insmod filename [args]

# rmmod --help
Usage: rmmod [-fhswvV] modulename ...
 -f (or --force) forces a module unload, and may crash your
    machine. This requires the Forced Module Removal option
    when the kernel was compiled.
 -h (or --help) prints this help text
 -s (or --syslog) says use syslog, not stderr
 -v (or --verbose) enables more messages
 -V (or --version) prints the version code
 -w (or --wait) begins module removal even if it is used
    and will stop new users from accessing the module (so it
    should eventually fall to zero).

# modprobe
Usage: modprobe [-v] [-V] [-C config-file] [-d <dirname> ] [-n] [-i] [-q]
    [-b] [-o <modname>] [ --dump-modversions ] <modname> [parameters...]
modprobe -r [-n] [-i] [-v] <modulename> ...
modprobe -l -t <dirname> [ -a <modulename> ...]

# depmod --help
depmod 3.13 -- part of module-init-tools
depmod -[aA] [-n -e -v -q -V -r -u -w -m]
      [-b basedirectory] [forced_version]
depmod [-n -e -v -q -r -u -w] [-F kernelsyms] module1.ko module2.ko ...
If no arguments (except options) are given, "depmod -a" is assumed.
depmod will output a dependency list suitable for the modprobe utility.
Options:
    -a, --all           Probe all modules
    -A, --quick         Only does the work if there's a new module
    -e, --errsyms       Report not supplied symbols
    -m, --map           Create the legacy map files
    -n, --show          Write the dependency file on stdout only
    -P, --symbol-prefix Architecture symbol prefix
    -V, --version       Print the release version
    -v, --verbose       Enable verbose mode
    -w, --warn          Warn on duplicates
    -h, --help          Print this usage message
The following options are useful for people managing distributions:
    -b basedirectory
    --basedir basedirectory
                        Use an image of a module tree
    -F kernelsyms
    --filesyms kernelsyms
                        Use the file instead of the current kernel symbols
    -E Module.symvers
    --symvers Module.symvers
                        Use Module.symvers file to check symbol versions
*/

//usage:#if ENABLE_MODPROBE_SMALL

//usage:#define depmod_trivial_usage "[-n]"
//usage:#define depmod_full_usage "\n\n"
//usage:       "Generate modules.dep.bb"
//usage:     "\n"
//usage:     "\n	-n	Dry run: print file to stdout"

//usage:#define insmod_trivial_usage
//usage:	"FILE" IF_FEATURE_CMDLINE_MODULE_OPTIONS(" [SYMBOL=VALUE]...")
//usage:#define insmod_full_usage "\n\n"
//usage:       "Load kernel module"

//usage:#define rmmod_trivial_usage
//usage:       "MODULE..."
//usage:#define rmmod_full_usage "\n\n"
//usage:       "Unload kernel modules"

//usage:#define modprobe_trivial_usage
//usage:	"[-rq] MODULE" IF_FEATURE_CMDLINE_MODULE_OPTIONS(" [SYMBOL=VALUE]...")
//usage:#define modprobe_full_usage "\n\n"
//usage:       "	-r	Remove MODULE"
//usage:     "\n	-q	Quiet"

//usage:#endif

int modprobe_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int modprobe_main(int argc UNUSED_PARAM, char **argv)
{
#if ENABLE_MODPROBE || ENABLE_INSMOD || ENABLE_RMMOD
	int exitcode;
#endif
	struct utsname uts;
	IF_FEATURE_CMDLINE_MODULE_OPTIONS(char *options = NULL;)

	INIT_G();

	/* Prevent ugly corner cases with no modules at all */
	modinfo = xzalloc(sizeof(modinfo[0]));

	if ((MOD_APPLET_CNT == 2 && ENABLE_DEPMOD && ENABLE_MODPROBE)
	 || is_depmod || is_modprobe
	) {
		/* Goto modules directory */
		xchdir(CONFIG_DEFAULT_MODULES_DIR);
		uname(&uts); /* never fails */
	}

	/* depmod? */
	if (is_depmod) {
		/* Supported:
		 * -n: print result to stdout
		 * -a: process all modules (default)
		 * optional VERSION parameter
		 * Ignored:
		 * -A: do work only if a module is newer than depfile
		 * -e: report any symbols which a module needs
		 *  which are not supplied by other modules or the kernel
		 * -F FILE: System.map (symbols for -e)
		 * -q, -r, -u: noop
		 * Not supported:
		 * -b BASEDIR: (TODO!) modules are in
		 *  $BASEDIR/lib/modules/$VERSION
		 * -m: create legacy "modules.*map" files (deprecated; in
		 *  kmod's depmod, prints a warning message and continues)
		 * -v: human readable deps to stdout
		 * -V: version (don't want to support it - people may depend
		 *  on it as an indicator of "standard" depmod)
		 * -h: help (well duh)
		 * module1.o module2.o parameters (just ignored for now)
		 */
		getopt32(argv, "na" "AeF:qru" /* "b:vV", NULL */, NULL);
		argv += optind;
		/* if (argv[0] && argv[1]) bb_show_usage(); */
		/* Goto $VERSION directory */
		xchdir(argv[0] ? argv[0] : uts.release);
		/* Force full module scan by asking to find a bogus module.
		 * This will generate modules.dep.bb as a side effect. */
		process_module((char*)"/", NULL);
		return !wrote_dep_bb_ok;
	}

#if ENABLE_MODPROBE || ENABLE_INSMOD || ENABLE_RMMOD
	/* modprobe, insmod, rmmod require at least one argument */
	/* only -q (quiet) and -r (rmmod),
	 * the rest are accepted and ignored (compat) */
	getopt32(argv, "^" "qrfsvwb" "\0" "-1");
	argv += optind;

	if (is_modprobe) {
		/* Goto $VERSION directory */
		xchdir(uts.release);
	}

	/* are we rmmod? -> simulate modprobe -r, but don't bother the flag if
	 * there're no other applets here */
	if (is_rmmod) {
		if (!ONLY_APPLET)
			option_mask32 |= OPT_r;
	} else if (!ENABLE_MODPROBE || !(option_mask32 & OPT_r)) {
# if ENABLE_FEATURE_CMDLINE_MODULE_OPTIONS
	/* If not rmmod/-r, parse possible module options given on command line.
	 * insmod/modprobe takes one module name, the rest are parameters. */
		char **arg = argv;
		while (*++arg) {
			/* Enclose options in quotes */
			char *s = options;
			options = xasprintf("%s \"%s\"", s ? s : "", *arg);
			free(s);
			*arg = NULL;
		}
# else
		argv[1] = NULL;
# endif
	}

	if (is_insmod) {
		size_t len;
		void *map;

		len = MAXINT(ssize_t);
		map = xmalloc_open_zipped_read_close(*argv, &len);
		if (!map)
			bb_perror_msg_and_die("can't read '%s'", *argv);
		if (init_module(map, len,
			(IF_FEATURE_CMDLINE_MODULE_OPTIONS(options ? options : ) "")
			) != 0
		) {
			bb_error_msg_and_die("can't insert '%s': %s",
					*argv, moderror(errno));
		}
		return EXIT_SUCCESS;
	}

	/* Try to load modprobe.dep.bb */
	if (!is_rmmod) {
		load_dep_bb();
	}

	/* Load/remove modules.
	 * Only rmmod/modprobe -r loops here, insmod/modprobe has only argv[0] */
	exitcode = EXIT_SUCCESS;
	do {
		exitcode |= process_module(*argv, options);
	} while (*++argv);

	if (ENABLE_FEATURE_CLEAN_UP) {
		IF_FEATURE_CMDLINE_MODULE_OPTIONS(free(options);)
	}
	return exitcode;
#endif /* MODPROBE || INSMOD || RMMOD */
}

#endif /* MOD_APPLET_CNT > 0 */
