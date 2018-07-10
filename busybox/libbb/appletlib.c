/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) tons of folks.  Tracking down who wrote what
 * isn't something I'm going to worry about...  If you wrote something
 * here, please feel free to acknowledge your work.
 *
 * Based in part on code from sash, Copyright (c) 1999 by David I. Bell
 * Permission has been granted to redistribute this code under GPL.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
/* We are trying to not use printf, this benefits the case when selected
 * applets are really simple. Example:
 *
 * $ ./busybox
 * ...
 * Currently defined functions:
 *         basename, false, true
 *
 * $ size busybox
 *    text    data     bss     dec     hex filename
 *    4473      52      72    4597    11f5 busybox
 *
 * FEATURE_INSTALLER or FEATURE_SUID will still link printf routines in. :(
 */
#include "busybox.h"

#if !(defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) \
    || defined(__APPLE__) \
    )
# include <malloc.h> /* for mallopt */
#endif

/* Declare <applet>_main() */
#define PROTOTYPES
#include "applets.h"
#undef PROTOTYPES

/* Include generated applet names, pointers to <applet>_main, etc */
#include "applet_tables.h"
/* ...and if applet_tables generator says we have only one applet... */
#ifdef SINGLE_APPLET_MAIN
# undef ENABLE_FEATURE_INDIVIDUAL
# define ENABLE_FEATURE_INDIVIDUAL 1
# undef IF_FEATURE_INDIVIDUAL
# define IF_FEATURE_INDIVIDUAL(...) __VA_ARGS__
#endif

#include "usage_compressed.h"


/* "Do not compress usage text if uncompressed text is small
 *  and we don't include bunzip2 code for other reasons"
 *
 * Useful for mass one-applet rebuild (bunzip2 code is ~2.7k).
 *
 * Unlike BUNZIP2, if FEATURE_SEAMLESS_BZ2 is on, bunzip2 code is built but
 * still may be unused if none of the selected applets calls open_zipped()
 * or its friends; we test for (FEATURE_SEAMLESS_BZ2 && <APPLET>) instead.
 * For example, only if TAR and FEATURE_SEAMLESS_BZ2 are both selected,
 * then bunzip2 code will be linked in anyway, and disabling help compression
 * would be not optimal:
 */
#if UNPACKED_USAGE_LENGTH < 4*1024 \
 && !(ENABLE_FEATURE_SEAMLESS_BZ2 && ENABLE_TAR) \
 && !(ENABLE_FEATURE_SEAMLESS_BZ2 && ENABLE_MODPROBE) \
 && !(ENABLE_FEATURE_SEAMLESS_BZ2 && ENABLE_INSMOD) \
 && !(ENABLE_FEATURE_SEAMLESS_BZ2 && ENABLE_DEPMOD) \
 && !(ENABLE_FEATURE_SEAMLESS_BZ2 && ENABLE_MAN) \
 && !ENABLE_BUNZIP2 \
 && !ENABLE_BZCAT
# undef  ENABLE_FEATURE_COMPRESS_USAGE
# define ENABLE_FEATURE_COMPRESS_USAGE 0
#endif


unsigned FAST_FUNC string_array_len(char **argv)
{
	char **start = argv;

	while (*argv)
		argv++;

	return argv - start;
}


#if ENABLE_SHOW_USAGE && !ENABLE_FEATURE_COMPRESS_USAGE
static const char usage_messages[] ALIGN1 = UNPACKED_USAGE;
#else
# define usage_messages 0
#endif

#if ENABLE_FEATURE_COMPRESS_USAGE

static const char packed_usage[] ALIGN1 = { PACKED_USAGE };
# include "bb_archive.h"
static const char *unpack_usage_messages(void)
{
	char *outbuf = NULL;
	bunzip_data *bd;
	int i;
	jmp_buf jmpbuf;

	/* Setup for I/O error handling via longjmp */
	i = setjmp(jmpbuf);
	if (i == 0) {
		i = start_bunzip(&jmpbuf,
			&bd,
			/* src_fd: */ -1,
			/* inbuf:  */ packed_usage,
			/* len:    */ sizeof(packed_usage)
		);
	}
	/* read_bunzip can longjmp and end up here with i != 0
	 * on read data errors! Not trivial */
	if (i == 0) {
		/* Cannot use xmalloc: will leak bd in NOFORK case! */
		outbuf = malloc_or_warn(sizeof(UNPACKED_USAGE));
		if (outbuf)
			read_bunzip(bd, outbuf, sizeof(UNPACKED_USAGE));
	}
	dealloc_bunzip(bd);
	return outbuf;
}
# define dealloc_usage_messages(s) free(s)

#else

# define unpack_usage_messages() usage_messages
# define dealloc_usage_messages(s) ((void)(s))

#endif /* FEATURE_COMPRESS_USAGE */


void FAST_FUNC bb_show_usage(void)
{
	if (ENABLE_SHOW_USAGE) {
#ifdef SINGLE_APPLET_STR
		/* Imagine that this applet is "true". Dont suck in printf! */
		const char *usage_string = unpack_usage_messages();

		if (*usage_string == '\b') {
			full_write2_str("No help available.\n\n");
		} else {
			full_write2_str("Usage: "SINGLE_APPLET_STR" ");
			full_write2_str(usage_string);
			full_write2_str("\n\n");
		}
		if (ENABLE_FEATURE_CLEAN_UP)
			dealloc_usage_messages((char*)usage_string);
#else
		const char *p;
		const char *usage_string = p = unpack_usage_messages();
		int ap = find_applet_by_name(applet_name);

		if (ap < 0) /* never happens, paranoia */
			xfunc_die();
		while (ap) {
			while (*p++) continue;
			ap--;
		}
		full_write2_str(bb_banner);
		full_write2_str(" multi-call binary.\n");
		if (*p == '\b')
			full_write2_str("\nNo help available.\n\n");
		else {
			full_write2_str("\nUsage: ");
			full_write2_str(applet_name);
			if (p[0]) {
				if (p[0] != '\n')
					full_write2_str(" ");
				full_write2_str(p);
			}
			full_write2_str("\n");
		}
		if (ENABLE_FEATURE_CLEAN_UP)
			dealloc_usage_messages((char*)usage_string);
#endif
	}
	xfunc_die();
}

int FAST_FUNC find_applet_by_name(const char *name)
{
	unsigned i, max;
	int j;
	const char *p;

/* The commented-out word-at-a-time code is ~40% faster, but +160 bytes.
 * "Faster" here saves ~0.5 microsecond of real time - not worth it.
 */
#if 0 /*BB_UNALIGNED_MEMACCESS_OK && BB_LITTLE_ENDIAN*/
	uint32_t n32;

	/* Handle all names < 2 chars long early */
	if (name[0] == '\0')
		return -1; /* "" is not a valid applet name */
	if (name[1] == '\0') {
		if (!ENABLE_TEST)
			return -1; /* 1-char name is not valid */
		if (name[0] != ']')
			return -1; /* 1-char name which isn't "[" is not valid */
		/* applet "[" is always applet #0: */
		return 0;
	}
#endif

	p = applet_names;
	i = 0;
#if KNOWN_APPNAME_OFFSETS <= 0
	max = NUM_APPLETS;
#else
	max = NUM_APPLETS * KNOWN_APPNAME_OFFSETS;
	for (j = ARRAY_SIZE(applet_nameofs)-1; j >= 0; j--) {
		const char *pp = applet_names + applet_nameofs[j];
		if (strcmp(name, pp) >= 0) {
			//bb_error_msg("name:'%s' >= pp:'%s'", name, pp);
			p = pp;
			i = max - NUM_APPLETS;
			break;
		}
		max -= NUM_APPLETS;
	}
	max /= (unsigned)KNOWN_APPNAME_OFFSETS;
	i /= (unsigned)KNOWN_APPNAME_OFFSETS;
	//bb_error_msg("name:'%s' starting from:'%s' i:%u max:%u", name, p, i, max);
#endif

	/* Open-coded linear search without strcmp/strlen calls for speed */

#if 0 /*BB_UNALIGNED_MEMACCESS_OK && BB_LITTLE_ENDIAN*/
	/* skip "[\0" name, it's surely not it */
	if (ENABLE_TEST && LONE_CHAR(p, '['))
		i++, p += 2;
	/* All remaining applet names in p[] are at least 2 chars long */
	/* name[] is also at least 2 chars long */

	n32 = (name[0] << 0) | (name[1] << 8) | (name[2] << 16);
	while (i < max) {
		uint32_t p32;
		char ch;

		/* Quickly check match of the first 3 bytes */
		move_from_unaligned32(p32, p);
		p += 3;
		if ((p32 & 0x00ffffff) != n32) {
			/* Most likely case: 3 first bytes do not match */
			i++;
			if ((p32 & 0x00ff0000) == '\0')
				continue; // p[2] was NUL
			p++;
			if ((p32 & 0xff000000) == '\0')
				continue; // p[3] was NUL
			/* p[0..3] aren't matching and none is NUL, check the rest */
			while (*p++ != '\0')
				continue;
			continue;
		}

		/* Unlikely branch: first 3 bytes ([0..2]) match */
		if ((p32 & 0x00ff0000) == '\0') {
			/* name is 2-byte long, it is full match */
			//bb_error_msg("found:'%s' i:%u", name, i);
			return i;
		}
		/* Check remaining bytes [3..NUL] */
		ch = (p32 >> 24);
		j = 3;
		while (ch == name[j]) {
			if (ch == '\0') {
				//bb_error_msg("found:'%s' i:%u", name, i);
				return i;
			}
			ch = *++p;
			j++;
		}
		/* Not a match. Skip it, including NUL */
		while (ch != '\0')
			ch = *++p;
		p++;
		i++;
	}
	return -1;
#else
	while (i < max) {
		char ch;
		j = 0;
		/* Do we see "name\0" in applet_names[p] position? */
		while ((ch = *p) == name[j]) {
			if (ch == '\0') {
				//bb_error_msg("found:'%s' i:%u", name, i);
				return i; /* yes */
			}
			p++;
			j++;
		}
		/* No.
		 * p => 1st non-matching char in applet_names[],
		 * skip to and including NUL.
		 */
		while (ch != '\0')
			ch = *++p;
		p++;
		i++;
	}
	return -1;
#endif
}


void lbb_prepare(const char *applet
		IF_FEATURE_INDIVIDUAL(, char **argv))
				MAIN_EXTERNALLY_VISIBLE;
void lbb_prepare(const char *applet
		IF_FEATURE_INDIVIDUAL(, char **argv))
{
#ifdef __GLIBC__
	(*(int **)&bb_errno) = __errno_location();
	barrier();
#endif
	applet_name = applet;

	if (ENABLE_LOCALE_SUPPORT)
		setlocale(LC_ALL, "");

#if ENABLE_FEATURE_INDIVIDUAL
	/* Redundant for busybox (run_applet_and_exit covers that case)
	 * but needed for "individual applet" mode */
	if (argv[1]
	 && !argv[2]
	 && strcmp(argv[1], "--help") == 0
	 && !is_prefixed_with(applet, "busybox")
	) {
		/* Special case. POSIX says "test --help"
		 * should be no different from e.g. "test --foo".  */
		if (!ENABLE_TEST || strcmp(applet_name, "test") != 0)
			bb_show_usage();
	}
#endif
}

/* The code below can well be in applets/applets.c, as it is used only
 * for busybox binary, not "individual" binaries.
 * However, keeping it here and linking it into libbusybox.so
 * (together with remaining tiny applets/applets.o)
 * makes it possible to avoid --whole-archive at link time.
 * This makes (shared busybox) + libbusybox smaller.
 * (--gc-sections would be even better....)
 */

const char *applet_name;
#if !BB_MMU
bool re_execed;
#endif


/* If not built as a single-applet executable... */
#if !defined(SINGLE_APPLET_MAIN)

IF_FEATURE_SUID(static uid_t ruid;)  /* real uid */

# if ENABLE_FEATURE_SUID_CONFIG

static struct suid_config_t {
	/* next ptr must be first: this struct needs to be llist-compatible */
	struct suid_config_t *m_next;
	struct bb_uidgid_t m_ugid;
	int m_applet;
	mode_t m_mode;
} *suid_config;

static bool suid_cfg_readable;

/* libbb candidate */
static char *get_trimmed_slice(char *s, char *e)
{
	/* First, consider the value at e to be nul and back up until we
	 * reach a non-space char.  Set the char after that (possibly at
	 * the original e) to nul. */
	while (e-- > s) {
		if (!isspace(*e)) {
			break;
		}
	}
	e[1] = '\0';

	/* Next, advance past all leading space and return a ptr to the
	 * first non-space char; possibly the terminating nul. */
	return skip_whitespace(s);
}

static void parse_config_file(void)
{
	/* Don't depend on the tools to combine strings. */
	static const char config_file[] ALIGN1 = "/etc/busybox.conf";

	struct suid_config_t *sct_head;
	int applet_no;
	FILE *f;
	const char *errmsg;
	unsigned lc;
	smallint section;
	struct stat st;

	ruid = getuid();
	if (ruid == 0) /* run by root - don't need to even read config file */
		return;

	if ((stat(config_file, &st) != 0)       /* No config file? */
	 || !S_ISREG(st.st_mode)                /* Not a regular file? */
	 || (st.st_uid != 0)                    /* Not owned by root? */
	 || (st.st_mode & (S_IWGRP | S_IWOTH))  /* Writable by non-root? */
	 || !(f = fopen_for_read(config_file))  /* Cannot open? */
	) {
		return;
	}

	suid_cfg_readable = 1;
	sct_head = NULL;
	section = lc = 0;

	while (1) {
		char buffer[256];
		char *s;

		if (!fgets(buffer, sizeof(buffer), f)) { /* Are we done? */
			// Looks like bloat
			//if (ferror(f)) {   /* Make sure it wasn't a read error. */
			//	errmsg = "reading";
			//	goto pe_label;
			//}
			fclose(f);
			suid_config = sct_head;	/* Success, so set the pointer. */
			return;
		}

		s = buffer;
		lc++;					/* Got a (partial) line. */

		/* If a line is too long for our buffer, we consider it an error.
		 * The following test does mistreat one corner case though.
		 * If the final line of the file does not end with a newline and
		 * yet exactly fills the buffer, it will be treated as too long
		 * even though there isn't really a problem.  But it isn't really
		 * worth adding code to deal with such an unlikely situation, and
		 * we do err on the side of caution.  Besides, the line would be
		 * too long if it did end with a newline. */
		if (!strchr(s, '\n') && !feof(f)) {
			errmsg = "line too long";
			goto pe_label;
		}

		/* Trim leading and trailing whitespace, ignoring comments, and
		 * check if the resulting string is empty. */
		s = get_trimmed_slice(s, strchrnul(s, '#'));
		if (!*s) {
			continue;
		}

		/* Check for a section header. */

		if (*s == '[') {
			/* Unlike the old code, we ignore leading and trailing
			 * whitespace for the section name.  We also require that
			 * there are no stray characters after the closing bracket. */
			char *e = strchr(s, ']');
			if (!e   /* Missing right bracket? */
			 || e[1] /* Trailing characters? */
			 || !*(s = get_trimmed_slice(s+1, e)) /* Missing name? */
			) {
				errmsg = "section header";
				goto pe_label;
			}
			/* Right now we only have one section so just check it.
			 * If more sections are added in the future, please don't
			 * resort to cascading ifs with multiple strcasecmp calls.
			 * That kind of bloated code is all too common.  A loop
			 * and a string table would be a better choice unless the
			 * number of sections is very small. */
			if (strcasecmp(s, "SUID") == 0) {
				section = 1;
				continue;
			}
			section = -1;	/* Unknown section so set to skip. */
			continue;
		}

		/* Process sections. */

		if (section == 1) {		/* SUID */
			/* Since we trimmed leading and trailing space above, we're
			 * now looking for strings of the form
			 *    <key>[::space::]*=[::space::]*<value>
			 * where both key and value could contain inner whitespace. */

			/* First get the key (an applet name in our case). */
			char *e = strchr(s, '=');
			if (e) {
				s = get_trimmed_slice(s, e);
			}
			if (!e || !*s) {	/* Missing '=' or empty key. */
				errmsg = "keyword";
				goto pe_label;
			}

			/* Ok, we have an applet name.  Process the rhs if this
			 * applet is currently built in and ignore it otherwise.
			 * Note: this can hide config file bugs which only pop
			 * up when the busybox configuration is changed. */
			applet_no = find_applet_by_name(s);
			if (applet_no >= 0) {
				unsigned i;
				struct suid_config_t *sct;

				/* Note: We currently don't check for duplicates!
				 * The last config line for each applet will be the
				 * one used since we insert at the head of the list.
				 * I suppose this could be considered a feature. */
				sct = xzalloc(sizeof(*sct));
				sct->m_applet = applet_no;
				/*sct->m_mode = 0;*/
				sct->m_next = sct_head;
				sct_head = sct;

				/* Get the specified mode. */

				e = skip_whitespace(e+1);

				for (i = 0; i < 3; i++) {
					/* There are 4 chars for each of user/group/other.
					 * "x-xx" instead of "x-" are to make
					 * "idx > 3" check catch invalid chars.
					 */
					static const char mode_chars[] ALIGN1 = "Ssx-" "Ssx-" "x-xx";
					static const unsigned short mode_mask[] ALIGN2 = {
						S_ISUID, S_ISUID|S_IXUSR, S_IXUSR, 0, /* Ssx- */
						S_ISGID, S_ISGID|S_IXGRP, S_IXGRP, 0, /* Ssx- */
						                          S_IXOTH, 0  /*   x- */
					};
					const char *q = strchrnul(mode_chars + 4*i, *e);
					unsigned idx = q - (mode_chars + 4*i);
					if (idx > 3) {
						errmsg = "mode";
						goto pe_label;
					}
					sct->m_mode |= mode_mask[q - mode_chars];
					e++;
				}

				/* Now get the user/group info. */

				s = skip_whitespace(e);
				/* Default is 0.0, else parse USER.GROUP: */
				if (*s) {
					/* We require whitespace between mode and USER.GROUP */
					if ((s == e) || !(e = strchr(s, '.'))) {
						errmsg = "uid.gid";
						goto pe_label;
					}
					*e = ':'; /* get_uidgid needs USER:GROUP syntax */
					if (get_uidgid(&sct->m_ugid, s) == 0) {
						errmsg = "unknown user/group";
						goto pe_label;
					}
				}
			}
			continue;
		}

		/* Unknown sections are ignored. */

		/* Encountering configuration lines prior to seeing a
		 * section header is treated as an error.  This is how
		 * the old code worked, but it may not be desirable.
		 * We may want to simply ignore such lines in case they
		 * are used in some future version of busybox. */
		if (!section) {
			errmsg = "keyword outside section";
			goto pe_label;
		}
	} /* while (1) */

 pe_label:
	fclose(f);
	bb_error_msg("parse error in %s, line %u: %s", config_file, lc, errmsg);

	/* Release any allocated memory before returning. */
	llist_free((llist_t*)sct_head, NULL);
}
# else
static inline void parse_config_file(void)
{
	IF_FEATURE_SUID(ruid = getuid();)
}
# endif /* FEATURE_SUID_CONFIG */


# if ENABLE_FEATURE_SUID && NUM_APPLETS > 0
#  if ENABLE_FEATURE_SUID_CONFIG
/* check if u is member of group g */
static int ingroup(uid_t u, gid_t g)
{
	struct group *grp = getgrgid(g);
	if (grp) {
		char **mem;
		for (mem = grp->gr_mem; *mem; mem++) {
			struct passwd *pwd = getpwnam(*mem);
			if (pwd && (pwd->pw_uid == u))
				return 1;
		}
	}
	return 0;
}
#  endif

static void check_suid(int applet_no)
{
	gid_t rgid;  /* real gid */

	if (ruid == 0) /* set by parse_config_file() */
		return; /* run by root - no need to check more */
	rgid = getgid();

#  if ENABLE_FEATURE_SUID_CONFIG
	if (suid_cfg_readable) {
		uid_t uid;
		struct suid_config_t *sct;
		mode_t m;

		for (sct = suid_config; sct; sct = sct->m_next) {
			if (sct->m_applet == applet_no)
				goto found;
		}
		goto check_need_suid;
 found:
		/* Is this user allowed to run this applet? */
		m = sct->m_mode;
		if (sct->m_ugid.uid == ruid)
			/* same uid */
			m >>= 6;
		else if ((sct->m_ugid.gid == rgid) || ingroup(ruid, sct->m_ugid.gid))
			/* same group / in group */
			m >>= 3;
		if (!(m & S_IXOTH)) /* is x bit not set? */
			bb_error_msg_and_die("you have no permission to run this applet");

		/* We set effective AND saved ids. If saved-id is not set
		 * like we do below, seteuid(0) can still later succeed! */

		/* Are we directed to change gid
		 * (APPLET = *s* USER.GROUP or APPLET = *S* USER.GROUP)?
		 */
		if (sct->m_mode & S_ISGID)
			rgid = sct->m_ugid.gid;
		/* else: we will set egid = rgid, thus dropping sgid effect */
		if (setresgid(-1, rgid, rgid))
			bb_perror_msg_and_die("setresgid");

		/* Are we directed to change uid
		 * (APPLET = s** USER.GROUP or APPLET = S** USER.GROUP)?
		 */
		uid = ruid;
		if (sct->m_mode & S_ISUID)
			uid = sct->m_ugid.uid;
		/* else: we will set euid = ruid, thus dropping suid effect */
		if (setresuid(-1, uid, uid))
			bb_perror_msg_and_die("setresuid");

		goto ret;
	}
#   if !ENABLE_FEATURE_SUID_CONFIG_QUIET
	{
		static bool onetime = 0;

		if (!onetime) {
			onetime = 1;
			bb_error_msg("using fallback suid method");
		}
	}
#   endif
 check_need_suid:
#  endif
	if (APPLET_SUID(applet_no) == BB_SUID_REQUIRE) {
		/* Real uid is not 0. If euid isn't 0 too, suid bit
		 * is most probably not set on our executable */
		if (geteuid())
			bb_error_msg_and_die("must be suid to work properly");
	} else if (APPLET_SUID(applet_no) == BB_SUID_DROP) {
		/*
		 * Drop all privileges.
		 *
		 * Don't check for errors: in normal use, they are impossible,
		 * and in special cases, exiting is harmful. Example:
		 * 'unshare --user' when user's shell is also from busybox.
		 *
		 * 'unshare --user' creates a new user namespace without any
		 * uid mappings. Thus, busybox binary is setuid nobody:nogroup
		 * within the namespace, as that is the only user. However,
		 * since no uids are mapped, calls to setgid/setuid
		 * fail (even though they would do nothing).
		 */
		setgid(rgid);
		setuid(ruid);
	}
#  if ENABLE_FEATURE_SUID_CONFIG
 ret: ;
	llist_free((llist_t*)suid_config, NULL);
#  endif
}
# else
#  define check_suid(x) ((void)0)
# endif /* FEATURE_SUID */


# if ENABLE_FEATURE_INSTALLER
static const char usr_bin [] ALIGN1 = "/usr/bin/";
static const char usr_sbin[] ALIGN1 = "/usr/sbin/";
static const char *const install_dir[] = {
	&usr_bin [8], /* "/" */
	&usr_bin [4], /* "/bin/" */
	&usr_sbin[4]  /* "/sbin/" */
#  if !ENABLE_INSTALL_NO_USR
	,usr_bin
	,usr_sbin
#  endif
};

/* create (sym)links for each applet */
static void install_links(const char *busybox, int use_symbolic_links,
		char *custom_install_dir)
{
	/* directory table
	 * this should be consistent w/ the enum,
	 * busybox.h::bb_install_loc_t, or else... */
	int (*lf)(const char *, const char *);
	char *fpc;
        const char *appname = applet_names;
	unsigned i;
	int rc;

	lf = link;
	if (use_symbolic_links)
		lf = symlink;

	for (i = 0; i < ARRAY_SIZE(applet_main); i++) {
		fpc = concat_path_file(
				custom_install_dir ? custom_install_dir : install_dir[APPLET_INSTALL_LOC(i)],
				appname);
		// debug: bb_error_msg("%slinking %s to busybox",
		//		use_symbolic_links ? "sym" : "", fpc);
		rc = lf(busybox, fpc);
		if (rc != 0 && errno != EEXIST) {
			bb_simple_perror_msg(fpc);
		}
		free(fpc);
		while (*appname++ != '\0')
			continue;
	}
}
# elif ENABLE_BUSYBOX
static void install_links(const char *busybox UNUSED_PARAM,
		int use_symbolic_links UNUSED_PARAM,
		char *custom_install_dir UNUSED_PARAM)
{
}
# endif

static void run_applet_and_exit(const char *name, char **argv) NORETURN;

# if ENABLE_BUSYBOX
#  if ENABLE_FEATURE_SH_STANDALONE && ENABLE_FEATURE_TAB_COMPLETION
    /*
     * Insert "busybox" into applet table as well.
     * This makes standalone shell tab-complete this name too.
     * (Otherwise having "busybox" in applet table is not necessary,
     * there is other code which routes "busyboxANY_SUFFIX" name
     * to busybox_main()).
     */
//usage:#define busybox_trivial_usage NOUSAGE_STR
//usage:#define busybox_full_usage ""
//applet:IF_BUSYBOX(IF_FEATURE_SH_STANDALONE(IF_FEATURE_TAB_COMPLETION(APPLET(busybox, BB_DIR_BIN, BB_SUID_MAYBE))))
int busybox_main(int argc, char *argv[]) MAIN_EXTERNALLY_VISIBLE;
#  else
#   define busybox_main(argc,argv) busybox_main(argv)
static
#  endif
int busybox_main(int argc UNUSED_PARAM, char **argv)
{
	if (!argv[1]) {
		/* Called without arguments */
		const char *a;
		int col;
		unsigned output_width;
 help:
		output_width = get_terminal_width(2);

		dup2(1, 2);
		full_write2_str(bb_banner); /* reuse const string */
		full_write2_str(" multi-call binary.\n"); /* reuse */
		full_write2_str(
			"BusyBox is copyrighted by many authors between 1998-2015.\n"
			"Licensed under GPLv2. See source distribution for detailed\n"
			"copyright notices.\n"
			"\n"
			"Usage: busybox [function [arguments]...]\n"
			"   or: busybox --list"IF_FEATURE_INSTALLER("[-full]")"\n"
			IF_FEATURE_INSTALLER(
			"   or: busybox --install [-s] [DIR]\n"
			)
			"   or: function [arguments]...\n"
			"\n"
			IF_NOT_FEATURE_SH_STANDALONE(
			"\tBusyBox is a multi-call binary that combines many common Unix\n"
			"\tutilities into a single executable.  Most people will create a\n"
			"\tlink to busybox for each function they wish to use and BusyBox\n"
			"\twill act like whatever it was invoked as.\n"
			)
			IF_FEATURE_SH_STANDALONE(
			"\tBusyBox is a multi-call binary that combines many common Unix\n"
			"\tutilities into a single executable.  The shell in this build\n"
			"\tis configured to run built-in utilities without $PATH search.\n"
			"\tYou don't need to install a link to busybox for each utility.\n"
			"\tTo run external program, use full path (/sbin/ip instead of ip).\n"
			)
			"\n"
			"Currently defined functions:\n"
		);
		col = 0;
		a = applet_names;
		/* prevent last comma to be in the very last pos */
		output_width--;
		while (*a) {
			int len2 = strlen(a) + 2;
			if (col >= (int)output_width - len2) {
				full_write2_str(",\n");
				col = 0;
			}
			if (col == 0) {
				col = 6;
				full_write2_str("\t");
			} else {
				full_write2_str(", ");
			}
			full_write2_str(a);
			col += len2;
			a += len2 - 1;
		}
		full_write2_str("\n");
		return 0;
	}

	if (is_prefixed_with(argv[1], "--list")) {
		unsigned i = 0;
		const char *a = applet_names;
		dup2(1, 2);
		while (*a) {
#  if ENABLE_FEATURE_INSTALLER
			if (argv[1][6]) /* --list-full? */
				full_write2_str(install_dir[APPLET_INSTALL_LOC(i)] + 1);
#  endif
			full_write2_str(a);
			full_write2_str("\n");
			i++;
			while (*a++ != '\0')
				continue;
		}
		return 0;
	}

	if (ENABLE_FEATURE_INSTALLER && strcmp(argv[1], "--install") == 0) {
		int use_symbolic_links;
		const char *busybox;

		busybox = xmalloc_readlink(bb_busybox_exec_path);
		if (!busybox) {
			/* bb_busybox_exec_path is usually "/proc/self/exe".
			 * In chroot, readlink("/proc/self/exe") usually fails.
			 * In such case, better use argv[0] as symlink target
			 * if it is a full path name.
			 */
			if (argv[0][0] != '/')
				bb_error_msg_and_die("'%s' is not an absolute path", argv[0]);
			busybox = argv[0];
		}
		/* busybox --install [-s] [DIR]:
		 * -s: make symlinks
		 * DIR: directory to install links to
		 */
		use_symbolic_links = (argv[2] && strcmp(argv[2], "-s") == 0 && ++argv);
		install_links(busybox, use_symbolic_links, argv[2]);
		return 0;
	}

	if (strcmp(argv[1], "--help") == 0) {
		/* "busybox --help [<applet>]" */
		if (!argv[2])
			goto help;
		/* convert to "<applet> --help" */
		argv[0] = argv[2];
		argv[2] = NULL;
	} else {
		/* "busybox <applet> arg1 arg2 ..." */
		argv++;
	}
	/* We support "busybox /a/path/to/applet args..." too. Allows for
	 * "#!/bin/busybox"-style wrappers */
	applet_name = bb_get_last_path_component_nostrip(argv[0]);
	run_applet_and_exit(applet_name, argv);
}
# endif

# if NUM_APPLETS > 0
void FAST_FUNC run_applet_no_and_exit(int applet_no, const char *name, char **argv)
{
	int argc = string_array_len(argv);

	/*
	 * We do not use argv[0]: do not want to repeat massaging of
	 * "-/sbin/halt" -> "halt", for example.
	 */
	applet_name = name;

	/* Special case. POSIX says "test --help"
	 * should be no different from e.g. "test --foo".
	 * Thus for "test", we skip --help check.
	 * "true" and "false" are also special.
	 */
	if (1
#  if defined APPLET_NO_test
	 && applet_no != APPLET_NO_test
#  endif
#  if defined APPLET_NO_true
	 && applet_no != APPLET_NO_true
#  endif
#  if defined APPLET_NO_false
	 && applet_no != APPLET_NO_false
#  endif
	) {
		if (argc == 2 && strcmp(argv[1], "--help") == 0) {
			/* Make "foo --help" exit with 0: */
			xfunc_error_retval = 0;
			bb_show_usage();
		}
	}
	if (ENABLE_FEATURE_SUID)
		check_suid(applet_no);
	xfunc_error_retval = applet_main[applet_no](argc, argv);
	/* Note: applet_main() may also not return (die on a xfunc or such) */
	xfunc_die();
}
# endif /* NUM_APPLETS > 0 */

# if ENABLE_BUSYBOX || NUM_APPLETS > 0
static NORETURN void run_applet_and_exit(const char *name, char **argv)
{
#  if ENABLE_BUSYBOX
	if (is_prefixed_with(name, "busybox"))
		exit(busybox_main(/*unused:*/ 0, argv));
#  endif
#  if NUM_APPLETS > 0
	/* find_applet_by_name() search is more expensive, so goes second */
	{
		int applet = find_applet_by_name(name);
		if (applet >= 0)
			run_applet_no_and_exit(applet, name, argv);
	}
#  endif

	/*bb_error_msg_and_die("applet not found"); - links in printf */
	full_write2_str(applet_name);
	full_write2_str(": applet not found\n");
	/* POSIX: "If a command is not found, the exit status shall be 127" */
	exit(127);
}
# endif

#endif /* !defined(SINGLE_APPLET_MAIN) */


#if ENABLE_BUILD_LIBBUSYBOX
int lbb_main(char **argv)
#else
int main(int argc UNUSED_PARAM, char **argv)
#endif
{
#if 0
	/* TODO: find a use for a block of memory between end of .bss
	 * and end of page. For example, I'm getting "_end:0x812e698 2408 bytes"
	 * - more than 2k of wasted memory (in this particular build)
	 * *per each running process*!
	 * (If your linker does not generate "_end" name, weak attribute
	 * makes &_end == NULL, end_len == 0 here.)
	 */
	extern char _end[] __attribute__((weak));
	unsigned end_len = (-(int)_end) & 0xfff;
	printf("_end:%p %u bytes\n", &_end, end_len);
#endif

	/* Tweak malloc for reduced memory consumption */
#ifdef M_TRIM_THRESHOLD
	/* M_TRIM_THRESHOLD is the maximum amount of freed top-most memory
	 * to keep before releasing to the OS
	 * Default is way too big: 256k
	 */
	mallopt(M_TRIM_THRESHOLD, 8 * 1024);
#endif
#ifdef M_MMAP_THRESHOLD
	/* M_MMAP_THRESHOLD is the request size threshold for using mmap()
	 * Default is too big: 256k
	 */
	mallopt(M_MMAP_THRESHOLD, 32 * 1024 - 256);
#endif
#if 0 /*def M_TOP_PAD*/
	/* When the program break is increased, then M_TOP_PAD bytes are added
	 * to the sbrk(2) request. When the heap is trimmed because of free(3),
	 * this much free space is preserved at the top of the heap.
	 * glibc default seems to be way too big: 128k, but need to verify.
	 */
	mallopt(M_TOP_PAD, 8 * 1024);
#endif

#if !BB_MMU
	/* NOMMU re-exec trick sets high-order bit in first byte of name */
	if (argv[0][0] & 0x80) {
		re_execed = 1;
		argv[0][0] &= 0x7f;
	}
#endif

#if defined(SINGLE_APPLET_MAIN)

	/* Only one applet is selected in .config */
	if (argv[1] && is_prefixed_with(argv[0], "busybox")) {
		/* "busybox <applet> <params>" should still work as expected */
		argv++;
	}
	/* applet_names in this case is just "applet\0\0" */
	lbb_prepare(applet_names IF_FEATURE_INDIVIDUAL(, argv));
# if ENABLE_BUILD_LIBBUSYBOX
	return SINGLE_APPLET_MAIN(string_array_len(argv), argv);
# else
	return SINGLE_APPLET_MAIN(argc, argv);
# endif

#elif !ENABLE_BUSYBOX && NUM_APPLETS == 0

	full_write2_str(bb_basename(argv[0]));
	full_write2_str(": no applets enabled\n");
	exit(127);

#else

	lbb_prepare("busybox" IF_FEATURE_INDIVIDUAL(, argv));
# if !ENABLE_BUSYBOX
	if (argv[1] && is_prefixed_with(bb_basename(argv[0]), "busybox"))
		argv++;
# endif
	applet_name = argv[0];
	if (applet_name[0] == '-')
		applet_name++;
	applet_name = bb_basename(applet_name);

	/* If we are a result of execv("/proc/self/exe"), fix ugly comm of "exe" */
	if (ENABLE_FEATURE_SH_STANDALONE
	 || ENABLE_FEATURE_PREFER_APPLETS
	 || !BB_MMU
	) {
		if (NUM_APPLETS > 1)
			set_task_comm(applet_name);
	}

	parse_config_file(); /* ...maybe, if FEATURE_SUID_CONFIG */
	run_applet_and_exit(applet_name, argv);

#endif
}
