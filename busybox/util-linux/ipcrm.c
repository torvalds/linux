/* vi: set sw=4 ts=4: */
/*
 * ipcrm.c - utility to allow removal of IPC objects and data structures.
 *
 * 01 Sept 2004 - Rodney Radford <rradford@mindspring.com>
 * Adapted for busybox from util-linux-2.12a.
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config IPCRM
//config:	bool "ipcrm (2.9 kb)"
//config:	default y
//config:	help
//config:	The ipcrm utility allows the removal of System V interprocess
//config:	communication (IPC) objects and the associated data structures
//config:	from the system.

//applet:IF_IPCRM(APPLET_NOEXEC(ipcrm, ipcrm, BB_DIR_USR_BIN, BB_SUID_DROP, ipcrm))

//kbuild:lib-$(CONFIG_IPCRM) += ipcrm.o

#include "libbb.h"

/* X/OPEN tells us to use <sys/{types,ipc,sem}.h> for semctl() */
/* X/OPEN tells us to use <sys/{types,ipc,msg}.h> for msgctl() */
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/sem.h>

#if defined(__GNU_LIBRARY__) && !defined(_SEM_SEMUN_UNDEFINED)
/* union semun is defined by including <sys/sem.h> */
#else
/* according to X/OPEN we have to define it ourselves */
union semun {
	int val;
	struct semid_ds *buf;
	unsigned short *array;
	struct seminfo *__buf;
};
#endif

#define IPCRM_LEGACY 1


#if IPCRM_LEGACY

typedef enum type_id {
	SHM,
	SEM,
	MSG
} type_id;

static int remove_ids(type_id type, char **argv)
{
	unsigned long id;
	int nb_errors = 0;
	union semun arg;

	arg.val = 0;

	while (argv[0]) {
		id = bb_strtoul(argv[0], NULL, 10);
		if (errno || id > INT_MAX) {
			bb_error_msg("invalid id: %s", argv[0]);
			nb_errors++;
		} else {
			int ret = 0;
			if (type == SEM)
				ret = semctl(id, 0, IPC_RMID, arg);
			else if (type == MSG)
				ret = msgctl(id, IPC_RMID, NULL);
			else if (type ==  SHM)
				ret = shmctl(id, IPC_RMID, NULL);

			if (ret) {
				bb_perror_msg("can't remove id %s", argv[0]);
				nb_errors++;
			}
		}
		argv++;
	}

	return nb_errors;
}
#endif /* IPCRM_LEGACY */

//usage:#define ipcrm_trivial_usage
//usage:       "[-MQS key] [-mqs id]"
//usage:#define ipcrm_full_usage "\n\n"
//usage:       "Upper-case options MQS remove an object by shmkey value.\n"
//usage:       "Lower-case options remove an object by shmid value.\n"
//usage:     "\n	-mM	Remove memory segment after last detach"
//usage:     "\n	-qQ	Remove message queue"
//usage:     "\n	-sS	Remove semaphore"

int ipcrm_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int ipcrm_main(int argc, char **argv)
{
	int c;
	int error = 0;

	/* if the command is executed without parameters, do nothing */
	if (argc == 1)
		return 0;
#if IPCRM_LEGACY
	/* check to see if the command is being invoked in the old way if so
	   then run the old code. Valid commands are msg, shm, sem. */
	{
		type_id what = 0; /* silence gcc */
		char w;

		w = argv[1][0];
		if ( ((w == 'm' && argv[1][1] == 's' && argv[1][2] == 'g')
		       || (argv[1][0] == 's'
		           && ((w = argv[1][1]) == 'h' || w == 'e')
		           && argv[1][2] == 'm')
		     ) && argv[1][3] == '\0'
		) {
			if (argc < 3)
				bb_show_usage();

			if (w == 'h')
				what = SHM;
			else if (w == 'm')
				what = MSG;
			else if (w == 'e')
				what = SEM;

			if (remove_ids(what, &argv[2]))
				fflush_stdout_and_exit(EXIT_FAILURE);
			puts("resource(s) deleted");
			return 0;
		}
	}
#endif /* IPCRM_LEGACY */

	/* process new syntax to conform with SYSV ipcrm */
	while ((c = getopt(argc, argv, "q:m:s:Q:M:S:")) != -1) {
		int result;
		int id;
		int iskey;
		/* needed to delete semaphores */
		union semun arg;

		if (c == '?') /* option not in the string */
			bb_show_usage();

		id = 0;
		arg.val = 0;

		iskey = !(c & 0x20); /* uppercase? */
		if (iskey) {
			/* keys are in hex or decimal */
			key_t key = xstrtoul(optarg, 0);

			if (key == IPC_PRIVATE) {
				error++;
				bb_error_msg("illegal key (%s)", optarg);
				continue;
			}

			c |= 0x20; /* lowercase. c is 'q', 'm' or 's' now */
			/* convert key to id */
			id = ((c == 'q') ? msgget(key, 0) :
				(c == 'm') ? shmget(key, 0, 0) : semget(key, 0, 0));

			if (id < 0) {
				const char *errmsg;

				error++;
				switch (errno) {
				case EACCES:
					errmsg = "permission denied for";
					break;
				case EIDRM:
					errmsg = "already removed";
					break;
				case ENOENT:
					errmsg = "invalid";
					break;
				default:
					errmsg = "unknown error in";
					break;
				}
				bb_error_msg("%s %s (%s)", errmsg, "key", optarg);
				continue;
			}
		} else {
			/* ids are in decimal */
			id = xatoul(optarg);
		}

		result = ((c == 'q') ? msgctl(id, IPC_RMID, NULL) :
				(c == 'm') ? shmctl(id, IPC_RMID, NULL) :
				semctl(id, 0, IPC_RMID, arg));

		if (result) {
			const char *errmsg;
			const char *const what = iskey ? "key" : "id";

			error++;
			switch (errno) {
			case EACCES:
			case EPERM:
				errmsg = "permission denied for";
				break;
			case EINVAL:
				errmsg = "invalid";
				break;
			case EIDRM:
				errmsg = "already removed";
				break;
			default:
				errmsg = "unknown error in";
				break;
			}
			bb_error_msg("%s %s (%s)", errmsg, what, optarg);
			continue;
		}
	}

	/* print usage if we still have some arguments left over */
	if (optind != argc) {
		bb_show_usage();
	}

	/* exit value reflects the number of errors encountered */
	return error;
}
