/*
 * tickadj - read, and possibly modify, the kernel `tick' and
 *	     `tickadj' variables, as well as `dosynctodr'.  Note that
 *	     this operates on the running kernel only.  I'd like to be
 *	     able to read and write the binary as well, but haven't
 *	     mastered this yet.
 *
 * HMS: The #includes here are different from those in xntpd/ntp_unixclock.c
 *      These seem "worse".
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "ntp_types.h"
#include "l_stdlib.h"

#include <stdio.h>
#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif /* HAVE_UNISTD_H */

#ifdef HAVE_SYS_TIMEX_H
# include <sys/timex.h>
#endif

#ifdef HAVE_ADJTIMEX	/* Linux */

struct timex txc;

#if 0
int
main(
	int argc,
	char *argv[]
	)
{
	int     c, i;
	int     quiet = 0;
	int     errflg = 0;
	char    *progname;
	extern int ntp_optind;
	extern char *ntp_optarg;

	progname = argv[0];
	if (argc==2 && argv[1][0] != '-') { /* old Linux format, for compatability */
	    if ((i = atoi(argv[1])) > 0) {
		    txc.time_tick = i;
		    txc.modes = ADJ_TIMETICK;
	    } else {
		    fprintf(stderr, "Silly value for tick: %s\n", argv[1]);
		    errflg++;
	    }
	} else {
	    while ((c = ntp_getopt(argc, argv, "a:qt:")) != EOF) {
		switch (c) {
		    case 'a':
			if ((i=atoi(ntp_optarg)) > 0) {
				txc.tickadj = i;
				txc.modes |= ADJ_TICKADJ;
			} else {
				fprintf(stderr,
					"%s: unlikely value for tickadj: %s\n",
					progname, ntp_optarg);
				errflg++;
			}
			break;

		    case 'q':
			quiet = 1;
			break;

		    case 't':
			if ((i=atoi(ntp_optarg)) > 0) {
				txc.time_tick = i;
				txc.modes |= ADJ_TIMETICK;
			} else {
				(void) fprintf(stderr,
				       "%s: unlikely value for tick: %s\n",
				       progname, ntp_optarg);
				errflg++;
			}
			break;

		    default:
			fprintf(stderr,
			    "Usage: %s [tick_value]\n-or-   %s [ -q ] [ -t tick ] [ -a tickadj ]\n",
			    progname, progname);
			errflg++;
			break;
		}
	    }
	}

	if (!errflg) {
		if (adjtimex(&txc) < 0)
			perror("adjtimex");
		else if (!quiet)
			printf("tick     = %ld\ntick_adj = %d\n",
			    txc.time_tick, txc.tickadj);
	}

	exit(errflg ? 1 : 0);
}
#else
int
main(
	int argc,
	char *argv[]
	)
{
	if (argc > 2)
	{
		fprintf(stderr, "Usage: %s [tick_value]\n", argv[0]);
		exit(-1);
	}
	else if (argc == 2)
	{
#ifdef ADJ_TIMETICK
		if ( (txc.time_tick = atoi(argv[1])) < 1 )
#else
		if ( (txc.tick = atoi(argv[1])) < 1 )
#endif
		{
			fprintf(stderr, "Silly value for tick: %s\n", argv[1]);
			exit(-1);
		}
#ifdef ADJ_TIMETICK
		txc.modes = ADJ_TIMETICK;
#else
#ifdef MOD_OFFSET
		txc.modes = ADJ_TICK;
#else
		txc.mode = ADJ_TICK;
#endif
#endif
	}
	else
	{
#ifdef ADJ_TIMETICK
		txc.modes = 0;
#else
#ifdef MOD_OFFSET
		txc.modes = 0;
#else
		txc.mode = 0;
#endif
#endif
	}

	if (adjtimex(&txc) < 0)
	{
		perror("adjtimex");
	}
	else
	{
#ifdef ADJ_TIMETICK
		printf("tick     = %ld\ntick_adj = %ld\n", txc.time_tick, txc.tickadj);
#else
		printf("tick = %ld\n", txc.tick);
#endif
	}

	exit(0);
}
#endif

#else	/* not Linux... kmem tweaking: */

#ifdef HAVE_SYS_FILE_H
# include <sys/file.h>
#endif
#include <sys/stat.h>

#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif

#ifdef NLIST_STRUCT
# include <nlist.h>
#else /* not NLIST_STRUCT */ /* was defined(SYS_AUX3) || defined(SYS_AUX2) */
# include <sys/resource.h>
# include <sys/file.h>
# include <a.out.h>
# ifdef HAVE_SYS_VAR_H
#  include <sys/var.h>
# endif
#endif

#include "ntp_stdlib.h"
#include "ntp_io.h"

#ifdef hz /* Was: RS6000 */
# undef hz
#endif /* hz */

#ifdef HAVE_KVM_OPEN
# include <kvm.h>
#endif

#ifdef SYS_VXWORKS
/* vxWorks needs mode flag -casey*/
#define open(name, flags)   open(name, flags, 0777)
#endif

#ifndef L_SET	/* Was: defined(SYS_PTX) || defined(SYS_IX86OSF1) */
# define L_SET SEEK_SET
#endif

#ifndef HZ
# define HZ	DEFAULT_HZ
#endif

#define	KMEM	"/dev/kmem"
#define	STREQ(a, b)	(*(a) == *(b) && strcmp((a), (b)) == 0)

char *progname;

int dokmem = 1;
int writetickadj = 0;
int writeopttickadj = 0;
int unsetdosync = 0;
int writetick = 0;
int quiet = 0;
int setnoprintf = 0;

const char *kmem = KMEM;
const char *file = NULL;
int   fd  = -1;

static	void	getoffsets	(off_t *, off_t *, off_t *, off_t *);
static	int	openfile	(const char *, int);
static	void	writevar	(int, off_t, int);
static	void	readvar		(int, off_t, int *);

/*
 * main - parse arguments and handle options
 */
int
main(
	int argc,
	char *argv[]
	)
{
	int c;
	int errflg = 0;
	off_t tickadj_offset;
	off_t tick_offset;
	off_t dosync_offset;
	off_t noprintf_offset;
	int tickadj, ktickadj;	/* HMS: Why isn't this u_long? */
	int tick, ktick;	/* HMS: Why isn't this u_long? */
	int dosynctodr;
	int noprintf;
	int hz;
	int hz_int, hz_hundredths;
	int recommend_tickadj;
	long tmp;

	init_lib();

	progname = argv[0];
	while ((c = ntp_getopt(argc, argv, "a:Adkpqst:")) != EOF)
	{
		switch (c)
		{
		    case 'a':
			writetickadj = atoi(ntp_optarg);
			if (writetickadj <= 0)
			{
				(void) fprintf(stderr,
					       "%s: unlikely value for tickadj: %s\n",
					       progname, ntp_optarg);
				errflg++;
			}

#if defined SCO5_CLOCK
			if (writetickadj % HZ) 
			{
				writetickadj = (writetickadj / HZ) * HZ;
				(void) fprintf(stderr,
					       "tickadj truncated to: %d\n", writetickadj);
			}
#endif /* SCO5_CLOCK */

			break;
		    case 'A':
			writeopttickadj = 1;
			break;
		    case 'd':
			++debug;
			break;
		    case 'k':
			dokmem = 1;
			break;
		    case 'p':
			setnoprintf = 1;
			break;
		    case 'q':
			quiet = 1;
			break;
		    case 's':
			unsetdosync = 1;
			break;
		    case 't':
			writetick = atoi(ntp_optarg);
			if (writetick <= 0)
			{
				(void) fprintf(stderr,
					       "%s: unlikely value for tick: %s\n",
					       progname, ntp_optarg);
				errflg++;
			}
			break;
		    default:
			errflg++;
			break;
		}
	}
	if (errflg || ntp_optind != argc)
	{
		(void) fprintf(stderr,
			       "usage: %s [-Adkpqs] [-a newadj] [-t newtick]\n", progname);
		exit(2);
	}

	getoffsets(&tick_offset, &tickadj_offset, &dosync_offset, &noprintf_offset);

	if (debug)
	{
		(void) printf("tick offset = %lu\n", (unsigned long)tick_offset);
		(void) printf("tickadj offset = %lu\n", (unsigned long)tickadj_offset);
		(void) printf("dosynctodr offset = %lu\n", (unsigned long)dosync_offset);
		(void) printf("noprintf offset = %lu\n", (unsigned long)noprintf_offset);
	}

	if (writetick && (tick_offset == 0))
	{
		(void) fprintf(stderr, 
			       "No tick kernel variable\n");
		errflg++;
	}
	
	if (writeopttickadj && (tickadj_offset == 0))
	{
		(void) fprintf(stderr, 
			       "No tickadj kernel variable\n");
		errflg++;
	}

	if (unsetdosync && (dosync_offset == 0))
	{
		(void) fprintf(stderr, 
			       "No dosynctodr kernel variable\n");
		errflg++;
	}
	
	if (setnoprintf && (noprintf_offset == 0))
	{
		(void) fprintf(stderr, 
			       "No noprintf kernel variable\n");
		errflg++;
	}

	if (tick_offset != 0)
	{
		readvar(fd, tick_offset, &tick);
#if defined(TICK_NANO) && defined(K_TICK_NAME)
		if (!quiet)
		    (void) printf("KERNEL %s = %d nsec\n", K_TICK_NAME, tick);
#endif /* TICK_NANO && K_TICK_NAME */

#ifdef TICK_NANO
		tick /= 1000;
#endif
	}
	else
	{
		tick = 0;
	}

	if (tickadj_offset != 0)
	{
		readvar(fd, tickadj_offset, &tickadj);

#ifdef SCO5_CLOCK
		/* scale from nsec/sec to usec/tick */
		tickadj /= (1000L * HZ);
#endif /*SCO5_CLOCK */

#if defined(TICKADJ_NANO) && defined(K_TICKADJ_NAME)
		if (!quiet)
		    (void) printf("KERNEL %s = %d nsec\n", K_TICKADJ_NAME, tickadj);
#endif /* TICKADJ_NANO && K_TICKADJ_NAME */

#ifdef TICKADJ_NANO
		tickadj += 999;
		tickadj /= 1000;
#endif
	}
	else
	{
		tickadj = 0;
	}

	if (dosync_offset != 0)
	{
		readvar(fd, dosync_offset, &dosynctodr);
	}

	if (noprintf_offset != 0)
	{
		readvar(fd, noprintf_offset, &noprintf);
	}

	(void) close(fd);

	if (unsetdosync && dosync_offset == 0)
	{
		(void) fprintf(stderr,
			       "%s: can't find %s in namelist\n",
			       progname,
#ifdef K_DOSYNCTODR_NAME
			       K_DOSYNCTODR_NAME
#else /* not K_DOSYNCTODR_NAME */
			       "dosynctodr"
#endif /* not K_DOSYNCTODR_NAME */
			       );
		exit(1);
	}

	hz = HZ;
#if defined(HAVE_SYSCONF) && defined(_SC_CLK_TCK)
	hz = (int) sysconf (_SC_CLK_TCK);
#endif /* not HAVE_SYSCONF && _SC_CLK_TCK */
#ifdef OVERRIDE_HZ
	hz = DEFAULT_HZ;
#endif
	ktick = tick;
#ifdef PRESET_TICK
	tick = PRESET_TICK;
#endif /* PRESET_TICK */
#ifdef TICKADJ_NANO
	tickadj /= 1000;
	if (tickadj == 0)
	    tickadj = 1;
#endif
	ktickadj = tickadj;
#ifdef PRESET_TICKADJ
	tickadj = (PRESET_TICKADJ) ? PRESET_TICKADJ : 1;
#endif /* PRESET_TICKADJ */

	if (!quiet)
	{
		if (tick_offset != 0)
		{
			(void) printf("KERNEL tick = %d usec (from %s kernel variable)\n",
				      ktick,
#ifdef K_TICK_NAME
				      K_TICK_NAME
#else
				      "<this can't happen>"
#endif			
				      );
		}
#ifdef PRESET_TICK
		(void) printf("PRESET tick = %d usec\n", tick);
#endif /* PRESET_TICK */
		if (tickadj_offset != 0)
		{
			(void) printf("KERNEL tickadj = %d usec (from %s kernel variable)\n",
				      ktickadj,
#ifdef K_TICKADJ_NAME
				      K_TICKADJ_NAME
#else
				      "<this can't happen>"
#endif
				      );
		}
#ifdef PRESET_TICKADJ
		(void) printf("PRESET tickadj = %d usec\n", tickadj);
#endif /* PRESET_TICKADJ */
		if (dosync_offset != 0)
		{
			(void) printf("dosynctodr is %s\n", dosynctodr ? "on" : "off");
		}
		if (noprintf_offset != 0)
		{
			(void) printf("kernel level printf's: %s\n",
				      noprintf ? "off" : "on");
		}
	}

	if (tick <= 0)
	{
		(void) fprintf(stderr, "%s: the value of tick is silly!\n",
			       progname);
		exit(1);
	}

	hz_int = (int)(1000000L / (long)tick);
	hz_hundredths = (int)((100000000L / (long)tick) - ((long)hz_int * 100L));
	if (!quiet)
	{
		(void) printf("KERNEL hz = %d\n", hz);
		(void) printf("calculated hz = %d.%02d Hz\n", hz_int,
			      hz_hundredths);
	}

#if defined SCO5_CLOCK
	recommend_tickadj = 100;
#else /* SCO5_CLOCK */
	tmp = (long) tick * 500L;
	recommend_tickadj = (int)(tmp / 1000000L);
	if (tmp % 1000000L > 0)
	{
		recommend_tickadj++;
	}

#ifdef MIN_REC_TICKADJ
	if (recommend_tickadj < MIN_REC_TICKADJ)
	{
		recommend_tickadj = MIN_REC_TICKADJ;
	}
#endif /* MIN_REC_TICKADJ */
#endif /* SCO5_CLOCK */
  

	if ((!quiet) && (tickadj_offset != 0))
	{
		(void) printf("recommended value of tickadj = %d us\n",
			      recommend_tickadj);
	}

	if (   writetickadj == 0
	       && !writeopttickadj
	       && !unsetdosync
	       && writetick == 0
	       && !setnoprintf)
	{
		exit(errflg ? 1 : 0);
	}

	if (writetickadj == 0 && writeopttickadj)
	{
		writetickadj = recommend_tickadj;
	}

	fd = openfile(file, O_WRONLY);

	if (setnoprintf && (noprintf_offset != 0))
	{
		if (!quiet)
		{
			(void) fprintf(stderr, "setting noprintf: ");
			(void) fflush(stderr);
		}
		writevar(fd, noprintf_offset, 1);
		if (!quiet)
		{
			(void) fprintf(stderr, "done!\n");
		}
	}

	if ((writetick > 0) && (tick_offset != 0))
	{
		if (!quiet)
		{
			(void) fprintf(stderr, "writing tick, value %d: ",
				       writetick);
			(void) fflush(stderr);
		}
		writevar(fd, tick_offset, writetick);
		if (!quiet)
		{
			(void) fprintf(stderr, "done!\n");
		}
	}

	if ((writetickadj > 0) && (tickadj_offset != 0))
	{
		if (!quiet)
		{
			(void) fprintf(stderr, "writing tickadj, value %d: ",
				       writetickadj);
			(void) fflush(stderr);
		}

#ifdef SCO5_CLOCK
		/* scale from usec/tick to nsec/sec */
		writetickadj *= (1000L * HZ);
#endif /* SCO5_CLOCK */

		writevar(fd, tickadj_offset, writetickadj);
		if (!quiet)
		{
			(void) fprintf(stderr, "done!\n");
		}
	}

	if (unsetdosync && (dosync_offset != 0))
	{
		if (!quiet)
		{
			(void) fprintf(stderr, "zeroing dosynctodr: ");
			(void) fflush(stderr);
		}
		writevar(fd, dosync_offset, 0);
		if (!quiet)
		{
			(void) fprintf(stderr, "done!\n");
		}
	}
	(void) close(fd);
	return(errflg ? 1 : 0);
}

/*
 * getoffsets - read the magic offsets from the specified file
 */
static void
getoffsets(
	off_t *tick_off,
	off_t *tickadj_off,
	off_t *dosync_off,
	off_t *noprintf_off
	)
{

#ifndef NOKMEM
# ifndef HAVE_KVM_OPEN
	const char **kname;
# endif
#endif

#ifndef NOKMEM
# ifdef NLIST_NAME_UNION
#  define NL_B {{
#  define NL_E }}
# else
#  define NL_B {
#  define NL_E }
# endif
#endif

#define K_FILLER_NAME "DavidLetterman"

#ifdef NLIST_EXTRA_INDIRECTION
	int i;
#endif

#ifndef NOKMEM
	static struct nlist nl[] =
	{
		NL_B
#ifdef K_TICKADJ_NAME
#define N_TICKADJ	0
		K_TICKADJ_NAME
#else
		K_FILLER_NAME
#endif
		NL_E,
		NL_B
#ifdef K_TICK_NAME
#define N_TICK		1
		K_TICK_NAME
#else
		K_FILLER_NAME
#endif
		NL_E,
		NL_B
#ifdef K_DOSYNCTODR_NAME
#define N_DOSYNC	2
		K_DOSYNCTODR_NAME
#else
		K_FILLER_NAME
#endif
		NL_E,
		NL_B
#ifdef K_NOPRINTF_NAME
#define N_NOPRINTF	3
		K_NOPRINTF_NAME
#else
		K_FILLER_NAME
#endif
		NL_E,
		NL_B "" NL_E,
	};

#ifndef HAVE_KVM_OPEN
	static const char *kernels[] =
	{
#ifdef HAVE_GETBOOTFILE
		NULL,			/* *** SEE BELOW! *** */
#endif
		"/kernel/unix",
		"/kernel",
		"/vmunix",
		"/unix",
		"/mach",
		"/hp-ux",
		"/386bsd",
		"/netbsd",
		"/stand/vmunix",
		"/bsd",
		NULL
	};
#endif /* not HAVE_KVM_OPEN */

#ifdef HAVE_KVM_OPEN
	/*
	 * Solaris > 2.5 doesn't have a kernel file.  Use the kvm_* interface
	 * to read the kernel name list. -- stolcke 3/4/96
	 */
	kvm_t *kvm_handle = kvm_open(NULL, NULL, NULL, O_RDONLY, progname);

	if (kvm_handle == NULL)
	{
		(void) fprintf(stderr,
			       "%s: kvm_open failed\n",
			       progname);
		exit(1);
	}
	if (kvm_nlist(kvm_handle, nl) == -1)
	{
		(void) fprintf(stderr,
			       "%s: kvm_nlist failed\n",
			       progname);
		exit(1);
	}
	kvm_close(kvm_handle);
#else /* not HAVE_KVM_OPEN */
#ifdef HAVE_GETBOOTFILE		/* *** SEE HERE! *** */
	if (kernels[0] == NULL)
	{
		char * cp = (char *)getbootfile();

		if (cp)
		{
			kernels[0] = cp;
		}
		else
		{
			kernels[0] = "/Placeholder";
		}
	}
#endif /* HAVE_GETBOOTFILE */
	for (kname = kernels; *kname != NULL; kname++)
	{
		struct stat stbuf;

		if (stat(*kname, &stbuf) == -1)
		{
			continue;
		}
		if (nlist(*kname, nl) >= 0)
		{
			break;
		}
		else
		{
			(void) fprintf(stderr,
				       "%s: nlist didn't find needed symbols from <%s>: %s\n",
				       progname, *kname, strerror(errno));
		}
	}
	if (*kname == NULL)
	{
		(void) fprintf(stderr,
			       "%s: Couldn't find the kernel\n",
			       progname);
		exit(1);
	}
#endif /* HAVE_KVM_OPEN */

	if (dokmem)
	{
		file = kmem;

		fd = openfile(file, O_RDONLY);
#ifdef NLIST_EXTRA_INDIRECTION
		/*
		 * Go one more round of indirection.
		 */
		for (i = 0; i < (sizeof(nl) / sizeof(struct nlist)); i++)
		{
			if ((nl[i].n_value) && (nl[i].n_sclass == 0x6b))
			{
				readvar(fd, nl[i].n_value, &nl[i].n_value);
			}
		}
#endif /* NLIST_EXTRA_INDIRECTION */
	}
#endif /* not NOKMEM */

	*tickadj_off  = 0;
	*tick_off     = 0;
	*dosync_off   = 0;
	*noprintf_off = 0;

#if defined(N_TICKADJ)
	*tickadj_off = nl[N_TICKADJ].n_value;
#endif

#if defined(N_TICK)
	*tick_off = nl[N_TICK].n_value;
#endif

#if defined(N_DOSYNC)
	*dosync_off = nl[N_DOSYNC].n_value;
#endif

#if defined(N_NOPRINTF)
	*noprintf_off = nl[N_NOPRINTF].n_value;
#endif
	return;
}

#undef N_TICKADJ
#undef N_TICK
#undef N_DOSYNC
#undef N_NOPRINTF


/*
 * openfile - open the file, check for errors
 */
static int
openfile(
	const char *name,
	int mode
	)
{
	int ifd;

	ifd = open(name, mode);
	if (ifd < 0)
	{
		(void) fprintf(stderr, "%s: open %s: ", progname, name);
		perror("");
		exit(1);
	}
	return ifd;
}


/*
 * writevar - write a variable into the file
 */
static void
writevar(
	int ofd,
	off_t off,
	int var
	)
{
	
	if (lseek(ofd, off, L_SET) == -1)
	{
		(void) fprintf(stderr, "%s: lseek fails: ", progname);
		perror("");
		exit(1);
	}
	if (write(ofd, (char *)&var, sizeof(int)) != sizeof(int))
	{
		(void) fprintf(stderr, "%s: write fails: ", progname);
		perror("");
		exit(1);
	}
	return;
}


/*
 * readvar - read a variable from the file
 */
static void
readvar(
	int ifd,
	off_t off,
	int *var
	)
{
	int i;
	
	if (lseek(ifd, off, L_SET) == -1)
	{
		(void) fprintf(stderr, "%s: lseek fails: ", progname);
		perror("");
		exit(1);
	}
	i = read(ifd, (char *)var, sizeof(int));
	if (i < 0)
	{
		(void) fprintf(stderr, "%s: read fails: ", progname);
		perror("");
		exit(1);
	}
	if (i != sizeof(int))
	{
		(void) fprintf(stderr, "%s: read expected %d, got %d\n",
			       progname, (int)sizeof(int), i);
		exit(1);
	}
	return;
}
#endif /* not Linux */
