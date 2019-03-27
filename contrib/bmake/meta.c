/*      $NetBSD: meta.c,v 1.70 2018/02/13 19:37:30 sjg Exp $ */

/*
 * Implement 'meta' mode.
 * Adapted from John Birrell's patches to FreeBSD make.
 * --sjg
 */
/*
 * Copyright (c) 2009-2016, Juniper Networks, Inc.
 * Portions Copyright (c) 2009, John Birrell.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions 
 * are met: 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer. 
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.  
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */
#if defined(USE_META)

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif
#include <sys/stat.h>
#include <sys/ioctl.h>
#ifdef HAVE_LIBGEN_H
#include <libgen.h>
#elif !defined(HAVE_DIRNAME)
char * dirname(char *);
#endif
#include <errno.h>
#if !defined(HAVE_CONFIG_H) || defined(HAVE_ERR_H)
#include <err.h>
#endif

#include "make.h"
#include "job.h"

#ifdef HAVE_FILEMON_H
# include <filemon.h>
#endif
#if !defined(USE_FILEMON) && defined(FILEMON_SET_FD)
# define USE_FILEMON
#endif

static BuildMon Mybm;			/* for compat */
static Lst metaBailiwick;		/* our scope of control */
static char *metaBailiwickStr;		/* string storage for the list */
static Lst metaIgnorePaths;		/* paths we deliberately ignore */
static char *metaIgnorePathsStr;	/* string storage for the list */

#ifndef MAKE_META_IGNORE_PATHS
#define MAKE_META_IGNORE_PATHS ".MAKE.META.IGNORE_PATHS"
#endif
#ifndef MAKE_META_IGNORE_PATTERNS
#define MAKE_META_IGNORE_PATTERNS ".MAKE.META.IGNORE_PATTERNS"
#endif
#ifndef MAKE_META_IGNORE_FILTER
#define MAKE_META_IGNORE_FILTER ".MAKE.META.IGNORE_FILTER"
#endif

Boolean useMeta = FALSE;
static Boolean useFilemon = FALSE;
static Boolean writeMeta = FALSE;
static Boolean metaMissing = FALSE;	/* oodate if missing */
static Boolean filemonMissing = FALSE;	/* oodate if missing */
static Boolean metaEnv = FALSE;		/* don't save env unless asked */
static Boolean metaVerbose = FALSE;
static Boolean metaIgnoreCMDs = FALSE;	/* ignore CMDs in .meta files */
static Boolean metaIgnorePatterns = FALSE; /* do we need to do pattern matches */
static Boolean metaIgnoreFilter = FALSE;   /* do we have more complex filtering? */
static Boolean metaCurdirOk = FALSE;	/* write .meta in .CURDIR Ok? */
static Boolean metaSilent = FALSE;	/* if we have a .meta be SILENT */

extern Boolean forceJobs;
extern Boolean comatMake;
extern char    **environ;

#define	MAKE_META_PREFIX	".MAKE.META.PREFIX"

#ifndef N2U
# define N2U(n, u)   (((n) + ((u) - 1)) / (u))
#endif
#ifndef ROUNDUP
# define ROUNDUP(n, u)   (N2U((n), (u)) * (u))
#endif

#if !defined(HAVE_STRSEP)
# define strsep(s, d) stresep((s), (d), 0)
#endif

/*
 * Filemon is a kernel module which snoops certain syscalls.
 *
 * C chdir
 * E exec
 * F [v]fork
 * L [sym]link
 * M rename
 * R read
 * W write
 * S stat
 *
 * See meta_oodate below - we mainly care about 'E' and 'R'.
 *
 * We can still use meta mode without filemon, but 
 * the benefits are more limited.
 */
#ifdef USE_FILEMON
# ifndef _PATH_FILEMON
#   define _PATH_FILEMON "/dev/filemon"
# endif

/*
 * Open the filemon device.
 */
static void
filemon_open(BuildMon *pbm)
{
    int retry;
    
    pbm->mon_fd = pbm->filemon_fd = -1;
    if (!useFilemon)
	return;

    for (retry = 5; retry >= 0; retry--) {
	if ((pbm->filemon_fd = open(_PATH_FILEMON, O_RDWR)) >= 0)
	    break;
    }

    if (pbm->filemon_fd < 0) {
	useFilemon = FALSE;
	warn("Could not open %s", _PATH_FILEMON);
	return;
    }

    /*
     * We use a file outside of '.'
     * to avoid a FreeBSD kernel bug where unlink invalidates
     * cwd causing getcwd to do a lot more work.
     * We only care about the descriptor.
     */
    pbm->mon_fd = mkTempFile("filemon.XXXXXX", NULL);
    if (ioctl(pbm->filemon_fd, FILEMON_SET_FD, &pbm->mon_fd) < 0) {
	err(1, "Could not set filemon file descriptor!");
    }
    /* we don't need these once we exec */
    (void)fcntl(pbm->mon_fd, F_SETFD, FD_CLOEXEC);
    (void)fcntl(pbm->filemon_fd, F_SETFD, FD_CLOEXEC);
}

/*
 * Read the build monitor output file and write records to the target's
 * metadata file.
 */
static int
filemon_read(FILE *mfp, int fd)
{
    char buf[BUFSIZ];
    int n;
    int error;

    /* Check if we're not writing to a meta data file.*/
    if (mfp == NULL) {
	if (fd >= 0)
	    close(fd);			/* not interested */
	return 0;
    }
    /* rewind */
    (void)lseek(fd, (off_t)0, SEEK_SET);

    error = 0;
    fprintf(mfp, "\n-- filemon acquired metadata --\n");

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
	if ((int)fwrite(buf, 1, n, mfp) < n)
	    error = EIO;
    }
    fflush(mfp);
    if (close(fd) < 0)
	error = errno;
    return error;
}
#endif

/*
 * when realpath() fails,
 * we use this, to clean up ./ and ../
 */
static void
eat_dots(char *buf, size_t bufsz, int dots)
{
    char *cp;
    char *cp2;
    const char *eat;
    size_t eatlen;

    switch (dots) {
    case 1:
	eat = "/./";
	eatlen = 2;
	break;
    case 2:
	eat = "/../";
	eatlen = 3;
	break;
    default:
	return;
    }
    
    do {
	cp = strstr(buf, eat);
	if (cp) {
	    cp2 = cp + eatlen;
	    if (dots == 2 && cp > buf) {
		do {
		    cp--;
		} while (cp > buf && *cp != '/');
	    }
	    if (*cp == '/') {
		strlcpy(cp, cp2, bufsz - (cp - buf));
	    } else {
		return;			/* can't happen? */
	    }
	}
    } while (cp);
}

static char *
meta_name(char *mname, size_t mnamelen,
	  const char *dname,
	  const char *tname,
	  const char *cwd)
{
    char buf[MAXPATHLEN];
    char *rp;
    char *cp;
    char *tp;
    char *dtp;
    size_t ldname;

    /*
     * Weed out relative paths from the target file name.
     * We have to be careful though since if target is a
     * symlink, the result will be unstable.
     * So we use realpath() just to get the dirname, and leave the
     * basename as given to us.
     */
    if ((cp = strrchr(tname, '/'))) {
	if (cached_realpath(tname, buf)) {
	    if ((rp = strrchr(buf, '/'))) {
		rp++;
		cp++;
		if (strcmp(cp, rp) != 0)
		    strlcpy(rp, cp, sizeof(buf) - (rp - buf));
	    }
	    tname = buf;
	} else {
	    /*
	     * We likely have a directory which is about to be made.
	     * We pretend realpath() succeeded, to have a chance
	     * of generating the same meta file name that we will
	     * next time through.
	     */
	    if (tname[0] == '/') {
		strlcpy(buf, tname, sizeof(buf));
	    } else {
		snprintf(buf, sizeof(buf), "%s/%s", cwd, tname);
	    }
	    eat_dots(buf, sizeof(buf), 1);	/* ./ */
	    eat_dots(buf, sizeof(buf), 2);	/* ../ */
	    tname = buf;
	}
    }
    /* on some systems dirname may modify its arg */
    tp = bmake_strdup(tname);
    dtp = dirname(tp);
    if (strcmp(dname, dtp) == 0)
	snprintf(mname, mnamelen, "%s.meta", tname);
    else {
	ldname = strlen(dname);
	if (strncmp(dname, dtp, ldname) == 0 && dtp[ldname] == '/')
	    snprintf(mname, mnamelen, "%s/%s.meta", dname, &tname[ldname+1]);
	else
	    snprintf(mname, mnamelen, "%s/%s.meta", dname, tname);

	/*
	 * Replace path separators in the file name after the
	 * current object directory path.
	 */
	cp = mname + strlen(dname) + 1;

	while (*cp != '\0') {
	    if (*cp == '/')
		*cp = '_';
	    cp++;
	}
    }
    free(tp);
    return (mname);
}

/*
 * Return true if running ${.MAKE}
 * Bypassed if target is flagged .MAKE
 */
static int
is_submake(void *cmdp, void *gnp)
{
    static char *p_make = NULL;
    static int p_len;
    char  *cmd = cmdp;
    GNode *gn = gnp;
    char *mp = NULL;
    char *cp;
    char *cp2;
    int rc = 0;				/* keep looking */

    if (!p_make) {
	p_make = Var_Value(".MAKE", gn, &cp);
	p_len = strlen(p_make);
    }
    cp = strchr(cmd, '$');
    if ((cp)) {
	mp = Var_Subst(NULL, cmd, gn, VARF_WANTRES);
	cmd = mp;
    }
    cp2 = strstr(cmd, p_make);
    if ((cp2)) {
	switch (cp2[p_len]) {
	case '\0':
	case ' ':
	case '\t':
	case '\n':
	    rc = 1;
	    break;
	}
	if (cp2 > cmd && rc > 0) {
	    switch (cp2[-1]) {
	    case ' ':
	    case '\t':
	    case '\n':
		break;
	    default:
		rc = 0;			/* no match */
		break;
	    }
	}
    }
    free(mp);
    return (rc);
}

typedef struct meta_file_s {
    FILE *fp;
    GNode *gn;
} meta_file_t;

static int
printCMD(void *cmdp, void *mfpp)
{
    meta_file_t *mfp = mfpp;
    char *cmd = cmdp;
    char *cp = NULL;

    if (strchr(cmd, '$')) {
	cmd = cp = Var_Subst(NULL, cmd, mfp->gn, VARF_WANTRES);
    }
    fprintf(mfp->fp, "CMD %s\n", cmd);
    free(cp);
    return 0;
}

/*
 * Certain node types never get a .meta file
 */
#define SKIP_META_TYPE(_type) do { \
    if ((gn->type & __CONCAT(OP_, _type))) {	\
	if (verbose) { \
	    fprintf(debug_file, "Skipping meta for %s: .%s\n", \
		    gn->name, __STRING(_type));		       \
	} \
	return FALSE; \
    } \
} while (0)


/*
 * Do we need/want a .meta file ?
 */
static Boolean
meta_needed(GNode *gn, const char *dname,
	     char *objdir, int verbose)
{
    struct stat fs;

    if (verbose)
	verbose = DEBUG(META);
    
    /* This may be a phony node which we don't want meta data for... */
    /* Skip .meta for .BEGIN, .END, .ERROR etc as well. */
    /* Or it may be explicitly flagged as .NOMETA */
    SKIP_META_TYPE(NOMETA);
    /* Unless it is explicitly flagged as .META */
    if (!(gn->type & OP_META)) {
	SKIP_META_TYPE(PHONY);
	SKIP_META_TYPE(SPECIAL);
	SKIP_META_TYPE(MAKE);
    }

    /* Check if there are no commands to execute. */
    if (Lst_IsEmpty(gn->commands)) {
	if (verbose)
	    fprintf(debug_file, "Skipping meta for %s: no commands\n",
		    gn->name);
	return FALSE;
    }
    if ((gn->type & (OP_META|OP_SUBMAKE)) == OP_SUBMAKE) {
	/* OP_SUBMAKE is a bit too aggressive */
	if (Lst_ForEach(gn->commands, is_submake, gn)) {
	    if (DEBUG(META))
		fprintf(debug_file, "Skipping meta for %s: .SUBMAKE\n",
			gn->name);
	    return FALSE;
	}
    }

    /* The object directory may not exist. Check it.. */
    if (cached_stat(dname, &fs) != 0) {
	if (verbose)
	    fprintf(debug_file, "Skipping meta for %s: no .OBJDIR\n",
		    gn->name);
	return FALSE;
    }

    /* make sure these are canonical */
    if (cached_realpath(dname, objdir))
	dname = objdir;

    /* If we aren't in the object directory, don't create a meta file. */
    if (!metaCurdirOk && strcmp(curdir, dname) == 0) {
	if (verbose)
	    fprintf(debug_file, "Skipping meta for %s: .OBJDIR == .CURDIR\n",
		    gn->name);
	return FALSE;
    }
    return TRUE;
}

    
static FILE *
meta_create(BuildMon *pbm, GNode *gn)
{
    meta_file_t mf;
    char buf[MAXPATHLEN];
    char objdir[MAXPATHLEN];
    char **ptr;
    const char *dname;
    const char *tname;
    char *fname;
    const char *cp;
    char *p[4];				/* >= possible uses */
    int i;

    mf.fp = NULL;
    i = 0;

    dname = Var_Value(".OBJDIR", gn, &p[i++]);
    tname = Var_Value(TARGET, gn, &p[i++]);

    /* if this succeeds objdir is realpath of dname */
    if (!meta_needed(gn, dname, objdir, TRUE))
	goto out;
    dname = objdir;

    if (metaVerbose) {
	char *mp;

	/* Describe the target we are building */
	mp = Var_Subst(NULL, "${" MAKE_META_PREFIX "}", gn, VARF_WANTRES);
	if (*mp)
	    fprintf(stdout, "%s\n", mp);
	free(mp);
    }
    /* Get the basename of the target */
    if ((cp = strrchr(tname, '/')) == NULL) {
	cp = tname;
    } else {
	cp++;
    }

    fflush(stdout);

    if (!writeMeta)
	/* Don't create meta data. */
	goto out;

    fname = meta_name(pbm->meta_fname, sizeof(pbm->meta_fname),
		      dname, tname, objdir);

#ifdef DEBUG_META_MODE
    if (DEBUG(META))
	fprintf(debug_file, "meta_create: %s\n", fname);
#endif

    if ((mf.fp = fopen(fname, "w")) == NULL)
	err(1, "Could not open meta file '%s'", fname);

    fprintf(mf.fp, "# Meta data file %s\n", fname);

    mf.gn = gn;

    Lst_ForEach(gn->commands, printCMD, &mf);

    fprintf(mf.fp, "CWD %s\n", getcwd(buf, sizeof(buf)));
    fprintf(mf.fp, "TARGET %s\n", tname);

    if (metaEnv) {
	for (ptr = environ; *ptr != NULL; ptr++)
	    fprintf(mf.fp, "ENV %s\n", *ptr);
    }

    fprintf(mf.fp, "-- command output --\n");
    fflush(mf.fp);

    Var_Append(".MAKE.META.FILES", fname, VAR_GLOBAL);
    Var_Append(".MAKE.META.CREATED", fname, VAR_GLOBAL);

    gn->type |= OP_META;		/* in case anyone wants to know */
    if (metaSilent) {
	    gn->type |= OP_SILENT;
    }
 out:
    for (i--; i >= 0; i--) {
	free(p[i]);
    }

    return (mf.fp);
}

static Boolean
boolValue(char *s)
{
    switch(*s) {
    case '0':
    case 'N':
    case 'n':
    case 'F':
    case 'f':
	return FALSE;
    }
    return TRUE;
}

/*
 * Initialization we need before reading makefiles.
 */
void
meta_init(void)
{
#ifdef USE_FILEMON
	/* this allows makefiles to test if we have filemon support */
	Var_Set(".MAKE.PATH_FILEMON", _PATH_FILEMON, VAR_GLOBAL, 0);
#endif
}


#define get_mode_bf(bf, token) \
    if ((cp = strstr(make_mode, token))) \
	bf = boolValue(&cp[sizeof(token) - 1])

/*
 * Initialization we need after reading makefiles.
 */
void
meta_mode_init(const char *make_mode)
{
    static int once = 0;
    char *cp;

    useMeta = TRUE;
    useFilemon = TRUE;
    writeMeta = TRUE;

    if (make_mode) {
	if (strstr(make_mode, "env"))
	    metaEnv = TRUE;
	if (strstr(make_mode, "verb"))
	    metaVerbose = TRUE;
	if (strstr(make_mode, "read"))
	    writeMeta = FALSE;
	if (strstr(make_mode, "nofilemon"))
	    useFilemon = FALSE;
	if (strstr(make_mode, "ignore-cmd"))
	    metaIgnoreCMDs = TRUE;
	if (useFilemon)
	    get_mode_bf(filemonMissing, "missing-filemon=");
	get_mode_bf(metaCurdirOk, "curdirok=");
	get_mode_bf(metaMissing, "missing-meta=");
	get_mode_bf(metaSilent, "silent=");
    }
    if (metaVerbose && !Var_Exists(MAKE_META_PREFIX, VAR_GLOBAL)) {
	/*
	 * The default value for MAKE_META_PREFIX
	 * prints the absolute path of the target.
	 * This works be cause :H will generate '.' if there is no /
	 * and :tA will resolve that to cwd.
	 */
	Var_Set(MAKE_META_PREFIX, "Building ${.TARGET:H:tA}/${.TARGET:T}", VAR_GLOBAL, 0);
    }
    if (once)
	return;
    once = 1;
    memset(&Mybm, 0, sizeof(Mybm));
    /*
     * We consider ourselves master of all within ${.MAKE.META.BAILIWICK}
     */
    metaBailiwick = Lst_Init(FALSE);
    metaBailiwickStr = Var_Subst(NULL, "${.MAKE.META.BAILIWICK:O:u:tA}",
	VAR_GLOBAL, VARF_WANTRES);
    if (metaBailiwickStr) {
	str2Lst_Append(metaBailiwick, metaBailiwickStr, NULL);
    }
    /*
     * We ignore any paths that start with ${.MAKE.META.IGNORE_PATHS}
     */
    metaIgnorePaths = Lst_Init(FALSE);
    Var_Append(MAKE_META_IGNORE_PATHS,
	       "/dev /etc /proc /tmp /var/run /var/tmp ${TMPDIR}", VAR_GLOBAL);
    metaIgnorePathsStr = Var_Subst(NULL,
		   "${" MAKE_META_IGNORE_PATHS ":O:u:tA}", VAR_GLOBAL,
		   VARF_WANTRES);
    if (metaIgnorePathsStr) {
	str2Lst_Append(metaIgnorePaths, metaIgnorePathsStr, NULL);
    }

    /*
     * We ignore any paths that match ${.MAKE.META.IGNORE_PATTERNS}
     */
    cp = NULL;
    if (Var_Value(MAKE_META_IGNORE_PATTERNS, VAR_GLOBAL, &cp)) {
	metaIgnorePatterns = TRUE;
	free(cp);
    }
    cp = NULL;
    if (Var_Value(MAKE_META_IGNORE_FILTER, VAR_GLOBAL, &cp)) {
	metaIgnoreFilter = TRUE;
	free(cp);
    }
}

/*
 * In each case below we allow for job==NULL
 */
void
meta_job_start(Job *job, GNode *gn)
{
    BuildMon *pbm;

    if (job != NULL) {
	pbm = &job->bm;
    } else {
	pbm = &Mybm;
    }
    pbm->mfp = meta_create(pbm, gn);
#ifdef USE_FILEMON_ONCE
    /* compat mode we open the filemon dev once per command */
    if (job == NULL)
	return;
#endif
#ifdef USE_FILEMON
    if (pbm->mfp != NULL && useFilemon) {
	filemon_open(pbm);
    } else {
	pbm->mon_fd = pbm->filemon_fd = -1;
    }
#endif
}

/*
 * The child calls this before doing anything.
 * It does not disturb our state.
 */
void
meta_job_child(Job *job)
{
#ifdef USE_FILEMON
    BuildMon *pbm;

    if (job != NULL) {
	pbm = &job->bm;
    } else {
	pbm = &Mybm;
    }
    if (pbm->mfp != NULL) {
	close(fileno(pbm->mfp));
	if (useFilemon) {
	    pid_t pid;

	    pid = getpid();
	    if (ioctl(pbm->filemon_fd, FILEMON_SET_PID, &pid) < 0) {
		err(1, "Could not set filemon pid!");
	    }
	}
    }
#endif
}

void
meta_job_error(Job *job, GNode *gn, int flags, int status)
{
    char cwd[MAXPATHLEN];
    BuildMon *pbm;

    if (job != NULL) {
	pbm = &job->bm;
	if (!gn)
	    gn = job->node;
    } else {
	pbm = &Mybm;
    }
    if (pbm->mfp != NULL) {
	fprintf(pbm->mfp, "\n*** Error code %d%s\n",
		status,
		(flags & JOB_IGNERR) ?
		"(ignored)" : "");
    }
    if (gn) {
	Var_Set(".ERROR_TARGET", gn->path ? gn->path : gn->name, VAR_GLOBAL, 0);
    }
    getcwd(cwd, sizeof(cwd));
    Var_Set(".ERROR_CWD", cwd, VAR_GLOBAL, 0);
    if (pbm->meta_fname[0]) {
	Var_Set(".ERROR_META_FILE", pbm->meta_fname, VAR_GLOBAL, 0);
    }
    meta_job_finish(job);
}

void
meta_job_output(Job *job, char *cp, const char *nl)
{
    BuildMon *pbm;
    
    if (job != NULL) {
	pbm = &job->bm;
    } else {
	pbm = &Mybm;
    }
    if (pbm->mfp != NULL) {
	if (metaVerbose) {
	    static char *meta_prefix = NULL;
	    static int meta_prefix_len;

	    if (!meta_prefix) {
		char *cp2;

		meta_prefix = Var_Subst(NULL, "${" MAKE_META_PREFIX "}",
					VAR_GLOBAL, VARF_WANTRES);
		if ((cp2 = strchr(meta_prefix, '$')))
		    meta_prefix_len = cp2 - meta_prefix;
		else
		    meta_prefix_len = strlen(meta_prefix);
	    }
	    if (strncmp(cp, meta_prefix, meta_prefix_len) == 0) {
		cp = strchr(cp+1, '\n');
		if (!cp++)
		    return;
	    }
	}
	fprintf(pbm->mfp, "%s%s", cp, nl);
    }
}

int
meta_cmd_finish(void *pbmp)
{
    int error = 0;
    BuildMon *pbm = pbmp;
#ifdef USE_FILEMON
    int x;
#endif

    if (!pbm)
	pbm = &Mybm;

#ifdef USE_FILEMON
    if (pbm->filemon_fd >= 0) {
	if (close(pbm->filemon_fd) < 0)
	    error = errno;
	x = filemon_read(pbm->mfp, pbm->mon_fd);
	if (error == 0 && x != 0)
	    error = x;
	pbm->filemon_fd = pbm->mon_fd = -1;
    } else
#endif
	fprintf(pbm->mfp, "\n");	/* ensure end with newline */
    return error;
}

int
meta_job_finish(Job *job)
{
    BuildMon *pbm;
    int error = 0;
    int x;

    if (job != NULL) {
	pbm = &job->bm;
    } else {
	pbm = &Mybm;
    }
    if (pbm->mfp != NULL) {
	error = meta_cmd_finish(pbm);
	x = fclose(pbm->mfp);
	if (error == 0 && x != 0)
	    error = errno;
	pbm->mfp = NULL;
	pbm->meta_fname[0] = '\0';
    }
    return error;
}

void
meta_finish(void)
{
    Lst_Destroy(metaBailiwick, NULL);
    free(metaBailiwickStr);
    Lst_Destroy(metaIgnorePaths, NULL);
    free(metaIgnorePathsStr);
}

/*
 * Fetch a full line from fp - growing bufp if needed
 * Return length in bufp.
 */
static int 
fgetLine(char **bufp, size_t *szp, int o, FILE *fp)
{
    char *buf = *bufp;
    size_t bufsz = *szp;
    struct stat fs;
    int x;

    if (fgets(&buf[o], bufsz - o, fp) != NULL) {
    check_newline:
	x = o + strlen(&buf[o]);
	if (buf[x - 1] == '\n')
	    return x;
	/*
	 * We need to grow the buffer.
	 * The meta file can give us a clue.
	 */
	if (fstat(fileno(fp), &fs) == 0) {
	    size_t newsz;
	    char *p;

	    newsz = ROUNDUP((fs.st_size / 2), BUFSIZ);
	    if (newsz <= bufsz)
		newsz = ROUNDUP(fs.st_size, BUFSIZ);
	    if (newsz <= bufsz)
		return x;		/* truncated */
	    if (DEBUG(META)) 
		fprintf(debug_file, "growing buffer %u -> %u\n",
			(unsigned)bufsz, (unsigned)newsz);
	    p = bmake_realloc(buf, newsz);
	    if (p) {
		*bufp = buf = p;
		*szp = bufsz = newsz;
		/* fetch the rest */
		if (!fgets(&buf[x], bufsz - x, fp))
		    return x;		/* truncated! */
		goto check_newline;
	    }
	}
    }
    return 0;
}

/* Lst_ForEach wants 1 to stop search */
static int
prefix_match(void *p, void *q)
{
    const char *prefix = p;
    const char *path = q;
    size_t n = strlen(prefix);

    return (0 == strncmp(path, prefix, n));
}

/*
 * looking for exact or prefix/ match to
 * Lst_Find wants 0 to stop search
 */
static int
path_match(const void *p, const void *q)
{
    const char *prefix = q;
    const char *path = p;
    size_t n = strlen(prefix);
    int rc;

    if ((rc = strncmp(path, prefix, n)) == 0) {
	switch (path[n]) {
	case '\0':
	case '/':
	    break;
	default:
	    rc = 1;
	    break;
	}
    }
    return rc;
}

/* Lst_Find wants 0 to stop search */
static int
string_match(const void *p, const void *q)
{
    const char *p1 = p;
    const char *p2 = q;

    return strcmp(p1, p2);
}


static int
meta_ignore(GNode *gn, const char *p)
{
    char fname[MAXPATHLEN];

    if (p == NULL)
	return TRUE;

    if (*p == '/') {
	cached_realpath(p, fname); /* clean it up */
	if (Lst_ForEach(metaIgnorePaths, prefix_match, fname)) {
#ifdef DEBUG_META_MODE
	    if (DEBUG(META))
		fprintf(debug_file, "meta_oodate: ignoring path: %s\n",
			p);
#endif
	    return TRUE;
	}
    }

    if (metaIgnorePatterns) {
	char *pm;

	Var_Set(".p.", p, gn, 0);
	pm = Var_Subst(NULL,
		       "${" MAKE_META_IGNORE_PATTERNS ":@m@${.p.:M$m}@}",
		       gn, VARF_WANTRES);
	if (*pm) {
#ifdef DEBUG_META_MODE
	    if (DEBUG(META))
		fprintf(debug_file, "meta_oodate: ignoring pattern: %s\n",
			p);
#endif
	    free(pm);
	    return TRUE;
	}
	free(pm);
    }

    if (metaIgnoreFilter) {
	char *fm;

	/* skip if filter result is empty */
	snprintf(fname, sizeof(fname),
		 "${%s:L:${%s:ts:}}",
		 p, MAKE_META_IGNORE_FILTER);
	fm = Var_Subst(NULL, fname, gn, VARF_WANTRES);
	if (*fm == '\0') {
#ifdef DEBUG_META_MODE
	    if (DEBUG(META))
		fprintf(debug_file, "meta_oodate: ignoring filtered: %s\n",
			p);
#endif
	    free(fm);
	    return TRUE;
	}
	free(fm);
    }
    return FALSE;
}

/*
 * When running with 'meta' functionality, a target can be out-of-date
 * if any of the references in its meta data file is more recent.
 * We have to track the latestdir on a per-process basis.
 */
#define LCWD_VNAME_FMT ".meta.%d.lcwd"
#define LDIR_VNAME_FMT ".meta.%d.ldir"

/*
 * It is possible that a .meta file is corrupted,
 * if we detect this we want to reproduce it.
 * Setting oodate TRUE will have that effect.
 */
#define CHECK_VALID_META(p) if (!(p && *p)) { \
    warnx("%s: %d: malformed", fname, lineno); \
    oodate = TRUE; \
    continue; \
    }

#define DEQUOTE(p) if (*p == '\'') {	\
    char *ep; \
    p++; \
    if ((ep = strchr(p, '\''))) \
	*ep = '\0'; \
    }

Boolean
meta_oodate(GNode *gn, Boolean oodate)
{
    static char *tmpdir = NULL;
    static char cwd[MAXPATHLEN];
    char lcwd_vname[64];
    char ldir_vname[64];
    char lcwd[MAXPATHLEN];
    char latestdir[MAXPATHLEN];
    char fname[MAXPATHLEN];
    char fname1[MAXPATHLEN];
    char fname2[MAXPATHLEN];
    char fname3[MAXPATHLEN];
    const char *dname;
    const char *tname;
    char *p;
    char *cp;
    char *link_src;
    char *move_target;
    static size_t cwdlen = 0;
    static size_t tmplen = 0;
    FILE *fp;
    Boolean needOODATE = FALSE;
    Lst missingFiles;
    char *pa[4];			/* >= possible uses */
    int i;
    int have_filemon = FALSE;

    if (oodate)
	return oodate;		/* we're done */

    i = 0;

    dname = Var_Value(".OBJDIR", gn, &pa[i++]);
    tname = Var_Value(TARGET, gn, &pa[i++]);

    /* if this succeeds fname3 is realpath of dname */
    if (!meta_needed(gn, dname, fname3, FALSE))
	goto oodate_out;
    dname = fname3;

    missingFiles = Lst_Init(FALSE);

    /*
     * We need to check if the target is out-of-date. This includes
     * checking if the expanded command has changed. This in turn
     * requires that all variables are set in the same way that they
     * would be if the target needs to be re-built.
     */
    Make_DoAllVar(gn);

    meta_name(fname, sizeof(fname), dname, tname, dname);

#ifdef DEBUG_META_MODE
    if (DEBUG(META))
	fprintf(debug_file, "meta_oodate: %s\n", fname);
#endif

    if ((fp = fopen(fname, "r")) != NULL) {
	static char *buf = NULL;
	static size_t bufsz;
	int lineno = 0;
	int lastpid = 0;
	int pid;
	int x;
	LstNode ln;
	struct stat fs;

	if (!buf) {
	    bufsz = 8 * BUFSIZ;
	    buf = bmake_malloc(bufsz);
	}

	if (!cwdlen) {
	    if (getcwd(cwd, sizeof(cwd)) == NULL)
		err(1, "Could not get current working directory");
	    cwdlen = strlen(cwd);
	}
	strlcpy(lcwd, cwd, sizeof(lcwd));
	strlcpy(latestdir, cwd, sizeof(latestdir));

	if (!tmpdir) {
	    tmpdir = getTmpdir();
	    tmplen = strlen(tmpdir);
	}

	/* we want to track all the .meta we read */
	Var_Append(".MAKE.META.FILES", fname, VAR_GLOBAL);

	ln = Lst_First(gn->commands);
	while (!oodate && (x = fgetLine(&buf, &bufsz, 0, fp)) > 0) {
	    lineno++;
	    if (buf[x - 1] == '\n')
		buf[x - 1] = '\0';
	    else {
		warnx("%s: %d: line truncated at %u", fname, lineno, x);
		oodate = TRUE;
		break;
	    }
	    link_src = NULL;
	    move_target = NULL;
	    /* Find the start of the build monitor section. */
	    if (!have_filemon) {
		if (strncmp(buf, "-- filemon", 10) == 0) {
		    have_filemon = TRUE;
		    continue;
		}
		if (strncmp(buf, "# buildmon", 10) == 0) {
		    have_filemon = TRUE;
		    continue;
		}
	    }		    

	    /* Delimit the record type. */
	    p = buf;
#ifdef DEBUG_META_MODE
	    if (DEBUG(META))
		fprintf(debug_file, "%s: %d: %s\n", fname, lineno, buf);
#endif
	    strsep(&p, " ");
	    if (have_filemon) {
		/*
		 * We are in the 'filemon' output section.
		 * Each record from filemon follows the general form:
		 *
		 * <key> <pid> <data>
		 *
		 * Where:
		 * <key> is a single letter, denoting the syscall.
		 * <pid> is the process that made the syscall.
		 * <data> is the arguments (of interest).
		 */
		switch(buf[0]) {
		case '#':		/* comment */
		case 'V':		/* version */
		    break;
		default:
		    /*
		     * We need to track pathnames per-process.
		     *
		     * Each process run by make, starts off in the 'CWD'
		     * recorded in the .meta file, if it chdirs ('C')
		     * elsewhere we need to track that - but only for
		     * that process.  If it forks ('F'), we initialize
		     * the child to have the same cwd as its parent.
		     *
		     * We also need to track the 'latestdir' of
		     * interest.  This is usually the same as cwd, but
		     * not if a process is reading directories.
		     *
		     * Each time we spot a different process ('pid')
		     * we save the current value of 'latestdir' in a
		     * variable qualified by 'lastpid', and
		     * re-initialize 'latestdir' to any pre-saved
		     * value for the current 'pid' and 'CWD' if none.
		     */
		    CHECK_VALID_META(p);
		    pid = atoi(p);
		    if (pid > 0 && pid != lastpid) {
			char *ldir;
			char *tp;
		    
			if (lastpid > 0) {
			    /* We need to remember these. */
			    Var_Set(lcwd_vname, lcwd, VAR_GLOBAL, 0);
			    Var_Set(ldir_vname, latestdir, VAR_GLOBAL, 0);
			}
			snprintf(lcwd_vname, sizeof(lcwd_vname), LCWD_VNAME_FMT, pid);
			snprintf(ldir_vname, sizeof(ldir_vname), LDIR_VNAME_FMT, pid);
			lastpid = pid;
			ldir = Var_Value(ldir_vname, VAR_GLOBAL, &tp);
			if (ldir) {
			    strlcpy(latestdir, ldir, sizeof(latestdir));
			    free(tp);
			}
			ldir = Var_Value(lcwd_vname, VAR_GLOBAL, &tp);
			if (ldir) {
			    strlcpy(lcwd, ldir, sizeof(lcwd));
			    free(tp);
			}
		    }
		    /* Skip past the pid. */
		    if (strsep(&p, " ") == NULL)
			continue;
#ifdef DEBUG_META_MODE
		    if (DEBUG(META))
			    fprintf(debug_file, "%s: %d: %d: %c: cwd=%s lcwd=%s ldir=%s\n",
				    fname, lineno,
				    pid, buf[0], cwd, lcwd, latestdir);
#endif
		    break;
		}

		CHECK_VALID_META(p);

		/* Process according to record type. */
		switch (buf[0]) {
		case 'X':		/* eXit */
		    Var_Delete(lcwd_vname, VAR_GLOBAL);
		    Var_Delete(ldir_vname, VAR_GLOBAL);
		    lastpid = 0;	/* no need to save ldir_vname */
		    break;

		case 'F':		/* [v]Fork */
		    {
			char cldir[64];
			int child;

			child = atoi(p);
			if (child > 0) {
			    snprintf(cldir, sizeof(cldir), LCWD_VNAME_FMT, child);
			    Var_Set(cldir, lcwd, VAR_GLOBAL, 0);
			    snprintf(cldir, sizeof(cldir), LDIR_VNAME_FMT, child);
			    Var_Set(cldir, latestdir, VAR_GLOBAL, 0);
#ifdef DEBUG_META_MODE
			    if (DEBUG(META))
				    fprintf(debug_file, "%s: %d: %d: cwd=%s lcwd=%s ldir=%s\n",
					    fname, lineno,
					    child, cwd, lcwd, latestdir);
#endif
			}
		    }
		    break;

		case 'C':		/* Chdir */
		    /* Update lcwd and latest directory. */
		    strlcpy(latestdir, p, sizeof(latestdir));	
		    strlcpy(lcwd, p, sizeof(lcwd));
		    Var_Set(lcwd_vname, lcwd, VAR_GLOBAL, 0);
		    Var_Set(ldir_vname, lcwd, VAR_GLOBAL, 0);
#ifdef DEBUG_META_MODE
		    if (DEBUG(META))
			fprintf(debug_file, "%s: %d: cwd=%s ldir=%s\n", fname, lineno, cwd, lcwd);
#endif
		    break;

		case 'M':		/* renaMe */
		    /*
		     * For 'M'oves we want to check
		     * the src as for 'R'ead
		     * and the target as for 'W'rite.
		     */
		    cp = p;		/* save this for a second */
		    /* now get target */
		    if (strsep(&p, " ") == NULL)
			continue;
		    CHECK_VALID_META(p);
		    move_target = p;
		    p = cp;
		    /* 'L' and 'M' put single quotes around the args */
		    DEQUOTE(p);
		    DEQUOTE(move_target);
		    /* FALLTHROUGH */
		case 'D':		/* unlink */
		    if (*p == '/' && !Lst_IsEmpty(missingFiles)) {
			/* remove any missingFiles entries that match p */
			if ((ln = Lst_Find(missingFiles, p,
					   path_match)) != NULL) {
			    LstNode nln;
			    char *tp;

			    do {
				nln = Lst_FindFrom(missingFiles, Lst_Succ(ln),
						   p, path_match);
				tp = Lst_Datum(ln);
				Lst_Remove(missingFiles, ln);
				free(tp);
			    } while ((ln = nln) != NULL);
			}
		    }
		    if (buf[0] == 'M') {
			/* the target of the mv is a file 'W'ritten */
#ifdef DEBUG_META_MODE
			if (DEBUG(META))
			    fprintf(debug_file, "meta_oodate: M %s -> %s\n",
				    p, move_target);
#endif
			p = move_target;
			goto check_write;
		    }
		    break;
		case 'L':		/* Link */
		    /*
		     * For 'L'inks check
		     * the src as for 'R'ead
		     * and the target as for 'W'rite.
		     */
		    link_src = p;
		    /* now get target */
		    if (strsep(&p, " ") == NULL)
			continue;
		    CHECK_VALID_META(p);
		    /* 'L' and 'M' put single quotes around the args */
		    DEQUOTE(p);
		    DEQUOTE(link_src);
#ifdef DEBUG_META_MODE
		    if (DEBUG(META))
			fprintf(debug_file, "meta_oodate: L %s -> %s\n",
				link_src, p);
#endif
		    /* FALLTHROUGH */
		case 'W':		/* Write */
		check_write:
		    /*
		     * If a file we generated within our bailiwick
		     * but outside of .OBJDIR is missing,
		     * we need to do it again. 
		     */
		    /* ignore non-absolute paths */
		    if (*p != '/')
			break;

		    if (Lst_IsEmpty(metaBailiwick))
			break;

		    /* ignore cwd - normal dependencies handle those */
		    if (strncmp(p, cwd, cwdlen) == 0)
			break;

		    if (!Lst_ForEach(metaBailiwick, prefix_match, p))
			break;

		    /* tmpdir might be within */
		    if (tmplen > 0 && strncmp(p, tmpdir, tmplen) == 0)
			break;

		    /* ignore anything containing the string "tmp" */
		    if ((strstr("tmp", p)))
			break;

		    if ((link_src != NULL && cached_lstat(p, &fs) < 0) ||
			(link_src == NULL && cached_stat(p, &fs) < 0)) {
			if (!meta_ignore(gn, p)) {
			    if (Lst_Find(missingFiles, p, string_match) == NULL)
				Lst_AtEnd(missingFiles, bmake_strdup(p));
			}
		    }
		    break;
		check_link_src:
		    p = link_src;
		    link_src = NULL;
#ifdef DEBUG_META_MODE
		    if (DEBUG(META))
			fprintf(debug_file, "meta_oodate: L src %s\n", p);
#endif
		    /* FALLTHROUGH */
		case 'R':		/* Read */
		case 'E':		/* Exec */
		    /*
		     * Check for runtime files that can't
		     * be part of the dependencies because
		     * they are _expected_ to change.
		     */
		    if (meta_ignore(gn, p))
			break;
		    
		    /*
		     * The rest of the record is the file name.
		     * Check if it's not an absolute path.
		     */
		    {
			char *sdirs[4];
			char **sdp;
			int sdx = 0;
			int found = 0;

			if (*p == '/') {
			    sdirs[sdx++] = p; /* done */
			} else {
			    if (strcmp(".", p) == 0)
				continue;  /* no point */

			    /* Check vs latestdir */
			    snprintf(fname1, sizeof(fname1), "%s/%s", latestdir, p);
			    sdirs[sdx++] = fname1;

			    if (strcmp(latestdir, lcwd) != 0) {
				/* Check vs lcwd */
				snprintf(fname2, sizeof(fname2), "%s/%s", lcwd, p);
				sdirs[sdx++] = fname2;
			    }
			    if (strcmp(lcwd, cwd) != 0) {
				/* Check vs cwd */
				snprintf(fname3, sizeof(fname3), "%s/%s", cwd, p);
				sdirs[sdx++] = fname3;
			    }
			}
			sdirs[sdx++] = NULL;

			for (sdp = sdirs; *sdp && !found; sdp++) {
#ifdef DEBUG_META_MODE
			    if (DEBUG(META))
				fprintf(debug_file, "%s: %d: looking for: %s\n", fname, lineno, *sdp);
#endif
			    if (cached_stat(*sdp, &fs) == 0) {
				found = 1;
				p = *sdp;
			    }
			}
			if (found) {
#ifdef DEBUG_META_MODE
			    if (DEBUG(META))
				fprintf(debug_file, "%s: %d: found: %s\n", fname, lineno, p);
#endif
			    if (!S_ISDIR(fs.st_mode) &&
				fs.st_mtime > gn->mtime) {
				if (DEBUG(META))
				    fprintf(debug_file, "%s: %d: file '%s' is newer than the target...\n", fname, lineno, p);
				oodate = TRUE;
			    } else if (S_ISDIR(fs.st_mode)) {
				/* Update the latest directory. */
				cached_realpath(p, latestdir);
			    }
			} else if (errno == ENOENT && *p == '/' &&
				   strncmp(p, cwd, cwdlen) != 0) {
			    /*
			     * A referenced file outside of CWD is missing.
			     * We cannot catch every eventuality here...
			     */
			    if (Lst_Find(missingFiles, p, string_match) == NULL)
				    Lst_AtEnd(missingFiles, bmake_strdup(p));
			}
		    }
		    if (buf[0] == 'E') {
			/* previous latestdir is no longer relevant */
			strlcpy(latestdir, lcwd, sizeof(latestdir));
		    }
		    break;
		default:
		    break;
		}
		if (!oodate && buf[0] == 'L' && link_src != NULL)
		    goto check_link_src;
	    } else if (strcmp(buf, "CMD") == 0) {
		/*
		 * Compare the current command with the one in the
		 * meta data file.
		 */
		if (ln == NULL) {
		    if (DEBUG(META))
			fprintf(debug_file, "%s: %d: there were more build commands in the meta data file than there are now...\n", fname, lineno);
		    oodate = TRUE;
		} else {
		    char *cmd = (char *)Lst_Datum(ln);
		    Boolean hasOODATE = FALSE;

		    if (strstr(cmd, "$?"))
			hasOODATE = TRUE;
		    else if ((cp = strstr(cmd, ".OODATE"))) {
			/* check for $[{(].OODATE[:)}] */
			if (cp > cmd + 2 && cp[-2] == '$')
			    hasOODATE = TRUE;
		    }
		    if (hasOODATE) {
			needOODATE = TRUE;
			if (DEBUG(META))
			    fprintf(debug_file, "%s: %d: cannot compare command using .OODATE\n", fname, lineno);
		    }
		    cmd = Var_Subst(NULL, cmd, gn, VARF_WANTRES|VARF_UNDEFERR);

		    if ((cp = strchr(cmd, '\n'))) {
			int n;

			/*
			 * This command contains newlines, we need to
			 * fetch more from the .meta file before we
			 * attempt a comparison.
			 */
			/* first put the newline back at buf[x - 1] */
			buf[x - 1] = '\n';
			do {
			    /* now fetch the next line */
			    if ((n = fgetLine(&buf, &bufsz, x, fp)) <= 0)
				break;
			    x = n;
			    lineno++;
			    if (buf[x - 1] != '\n') {
				warnx("%s: %d: line truncated at %u", fname, lineno, x);
				break;
			    }
			    cp = strchr(++cp, '\n');
			} while (cp);
			if (buf[x - 1] == '\n')
			    buf[x - 1] = '\0';
		    }
		    if (!hasOODATE &&
			!(gn->type & OP_NOMETA_CMP) &&
			strcmp(p, cmd) != 0) {
			if (DEBUG(META))
			    fprintf(debug_file, "%s: %d: a build command has changed\n%s\nvs\n%s\n", fname, lineno, p, cmd);
			if (!metaIgnoreCMDs)
			    oodate = TRUE;
		    }
		    free(cmd);
		    ln = Lst_Succ(ln);
		}
	    } else if (strcmp(buf, "CWD") == 0) {
		/*
		 * Check if there are extra commands now
		 * that weren't in the meta data file.
		 */
		if (!oodate && ln != NULL) {
		    if (DEBUG(META))
			fprintf(debug_file, "%s: %d: there are extra build commands now that weren't in the meta data file\n", fname, lineno);
		    oodate = TRUE;
		}
		if (strcmp(p, cwd) != 0) {
		    if (DEBUG(META))
			fprintf(debug_file, "%s: %d: the current working directory has changed from '%s' to '%s'\n", fname, lineno, p, curdir);
		    oodate = TRUE;
		}
	    }
	}

	fclose(fp);
	if (!Lst_IsEmpty(missingFiles)) {
	    if (DEBUG(META))
		fprintf(debug_file, "%s: missing files: %s...\n",
			fname, (char *)Lst_Datum(Lst_First(missingFiles)));
	    oodate = TRUE;
	}
	if (!oodate && !have_filemon && filemonMissing) {
	    if (DEBUG(META))
		fprintf(debug_file, "%s: missing filemon data\n", fname);
	    oodate = TRUE;
	}
    } else {
	if (writeMeta && metaMissing) {
	    cp = NULL;

	    /* if target is in .CURDIR we do not need a meta file */
	    if (gn->path && (cp = strrchr(gn->path, '/')) && cp > gn->path) {
		if (strncmp(curdir, gn->path, (cp - gn->path)) != 0) {
		    cp = NULL;		/* not in .CURDIR */
		}
	    }
	    if (!cp) {
		if (DEBUG(META))
		    fprintf(debug_file, "%s: required but missing\n", fname);
		oodate = TRUE;
		needOODATE = TRUE;	/* assume the worst */
	    }
	}
    }

    Lst_Destroy(missingFiles, (FreeProc *)free);

    if (oodate && needOODATE) {
	/*
	 * Target uses .OODATE which is empty; or we wouldn't be here.
	 * We have decided it is oodate, so .OODATE needs to be set.
	 * All we can sanely do is set it to .ALLSRC.
	 */
	Var_Delete(OODATE, gn);
	Var_Set(OODATE, Var_Value(ALLSRC, gn, &cp), gn, 0);
	free(cp);
    }

 oodate_out:
    for (i--; i >= 0; i--) {
	free(pa[i]);
    }
    return oodate;
}

/* support for compat mode */

static int childPipe[2];

void
meta_compat_start(void)
{
#ifdef USE_FILEMON_ONCE
    /*
     * We need to re-open filemon for each cmd.
     */
    BuildMon *pbm = &Mybm;
    
    if (pbm->mfp != NULL && useFilemon) {
	filemon_open(pbm);
    } else {
	pbm->mon_fd = pbm->filemon_fd = -1;
    }
#endif
    if (pipe(childPipe) < 0)
	Punt("Cannot create pipe: %s", strerror(errno));
    /* Set close-on-exec flag for both */
    (void)fcntl(childPipe[0], F_SETFD, FD_CLOEXEC);
    (void)fcntl(childPipe[1], F_SETFD, FD_CLOEXEC);
}

void
meta_compat_child(void)
{
    meta_job_child(NULL);
    if (dup2(childPipe[1], 1) < 0 ||
	dup2(1, 2) < 0) {
	execError("dup2", "pipe");
	_exit(1);
    }
}

void
meta_compat_parent(void)
{
    FILE *fp;
    char buf[BUFSIZ];
    
    close(childPipe[1]);			/* child side */
    fp = fdopen(childPipe[0], "r");
    while (fgets(buf, sizeof(buf), fp)) {
	meta_job_output(NULL, buf, "");
	printf("%s", buf);
	fflush(stdout);
    }
    fclose(fp);
}

#endif	/* USE_META */
