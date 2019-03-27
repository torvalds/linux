/*
 * Copyright (c) 2000-2001, 2005-2008 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: t-sem.c,v 1.18 2013-11-22 20:51:43 ca Exp $")

#include <stdio.h>

#if SM_CONF_SEM
# include <stdlib.h>
# include <unistd.h>
# include <sysexits.h>
# include <sm/heap.h>
# include <sm/string.h>
# include <sm/signal.h>
# include <sm/test.h>
# include <sm/sem.h>

# define T_SM_SEM_KEY (4321L)

static void
delay(t, s)
	int t;
	char *s;
{
	if (t > 0)
	{
#if DEBUG
		fprintf(stderr, "sleep(%d) before %s\n", t, s);
#endif /* DEBUG */
		sleep(t);
	}
#if DEBUG
	fprintf(stderr, "%s\n", s);
#endif /* DEBUG */
}


/*
**  SEMINTER -- interactive testing of semaphores.
**
**	Parameters:
**		owner -- create semaphores.
**
**	Returns:
**		0 on success
**		< 0 on failure.
*/

static int
seminter(owner)
	bool owner;
{
	int semid;
	int t;

	semid = sm_sem_start(T_SM_SEM_KEY, SM_NSEM, 0, owner);
	if (semid < 0)
	{
		perror("sm_sem_start failed");
		return 1;
	}

	while ((t = getchar()) != EOF)
	{
		switch (t)
		{
		  case 'a':
			delay(0, "try to acq");
			if (sm_sem_acq(semid, 0, 2) < 0)
			{
				perror("sm_sem_acq failed");
				return 1;
			}
			delay(0, "acquired");
			break;

		  case 'r':
			delay(0, "try to rel");
			if (sm_sem_rel(semid, 0, 2) < 0)
			{
				perror("sm_sem_rel failed");
				return 1;
			}
			delay(0, "released");
			break;

		  case 'v':
			if ((t = sm_sem_get(semid, 0)) < 0)
			{
				perror("get_sem failed");
				return 1;
			}
			printf("semval: %d\n", t);
			break;

		}
	}
	if (owner)
		return sm_sem_stop(semid);
	return 0;
}

/*
**  SEM_CLEANUP -- cleanup if something breaks
**
**	Parameters:
**		sig -- signal.
**
**	Returns:
**		none.
*/

static int semid_c = -1;
void
sem_cleanup(sig)
	int sig;
{
	if (semid_c >= 0)
		(void) sm_sem_stop(semid_c);
	exit(EX_UNAVAILABLE);
}

static int
drop_priv(uid, gid)
	uid_t uid;
	gid_t gid;
{
	int r;

	r = setgid(gid);
	if (r != 0)
		return r;
	r = setuid(uid);
	return r;
}

/*
**  SEMTEST -- test of semaphores
**
**	Parameters:
**		owner -- create semaphores.
**
**	Returns:
**		0 on success
**		< 0 on failure.
*/

# define MAX_CNT	10

static int
semtest(owner, uid, gid)
	int owner;
	uid_t uid;
	gid_t gid;
{
	int semid, r;
	int cnt = 0;

	if (!owner && uid != 0)
	{
		r = drop_priv(uid, gid);
		if (r < 0)
		{
			perror("drop_priv child failed");
			return -1;
		}
	}
	semid = sm_sem_start(T_SM_SEM_KEY, 1, 0, owner);
	if (semid < 0)
	{
		perror("sm_sem_start failed");
		return -1;
	}

	if (owner)
	{
		if (uid != 0)
		{
			r = sm_semsetowner(semid, uid, gid, 0660);
			if (r < 0)
			{
				perror("sm_semsetowner failed");
				return -1;
			}
			r = drop_priv(uid, gid);
			if (r < 0)
			{
				perror("drop_priv owner failed");
				return -1;
			}
		}

		/* just in case someone kills the program... */
		semid_c = semid;
		(void) sm_signal(SIGHUP, sem_cleanup);
		(void) sm_signal(SIGINT, sem_cleanup);
		(void) sm_signal(SIGTERM, sem_cleanup);

		delay(1, "parent: acquire 1");
		cnt = 0;
		do
		{
			r = sm_sem_acq(semid, 0, 0);
			if (r < 0)
			{
				sleep(1);
				++cnt;
			}
		} while (r < 0 && cnt <= MAX_CNT);
		SM_TEST(r >= 0);
		if (r < 0)
			return r;

		delay(3, "parent: release 1");
		cnt = 0;
		do
		{
			r = sm_sem_rel(semid, 0, 0);
			if (r < 0)
			{
				sleep(1);
				++cnt;
			}
		} while (r < 0 && cnt <= MAX_CNT);
		SM_TEST(r >= 0);
		if (r < 0)
			return r;

		delay(1, "parent: getval");
		cnt = 0;
		do
		{
			r = sm_sem_get(semid, 0);
			if (r <= 0)
			{
				sleep(1);
				++cnt;
			}
		} while (r <= 0 && cnt <= MAX_CNT);
		SM_TEST(r > 0);
		if (r <= 0)
			return r;

		delay(1, "parent: acquire 2");
		cnt = 0;
		do
		{
			r = sm_sem_acq(semid, 0, 0);
			if (r < 0)
			{
				sleep(1);
				++cnt;
			}
		} while (r < 0 && cnt <= MAX_CNT);
		SM_TEST(r >= 0);
		if (r < 0)
			return r;

		cnt = 0;
		do
		{
			r = sm_sem_rel(semid, 0, 0);
			if (r < 0)
			{
				sleep(1);
				++cnt;
			}
		} while (r < 0 && cnt <= MAX_CNT);
		SM_TEST(r >= 0);
		if (r < 0)
			return r;
	}
	else
	{
		delay(1, "child: acquire 1");
		cnt = 0;
		do
		{
			r = sm_sem_acq(semid, 0, 0);
			if (r < 0)
			{
				sleep(1);
				++cnt;
			}
		} while (r < 0 && cnt <= MAX_CNT);
		SM_TEST(r >= 0);
		if (r < 0)
			return r;

		delay(1, "child: release 1");
		cnt = 0;
		do
		{
			r = sm_sem_rel(semid, 0, 0);
			if (r < 0)
			{
				sleep(1);
				++cnt;
			}
		} while (r < 0 && cnt <= MAX_CNT);
		SM_TEST(r >= 0);
		if (r < 0)
			return r;

	}
	if (owner)
		return sm_sem_stop(semid);
	return 0;
}

int
main(argc, argv)
	int argc;
	char *argv[];
{
	bool interactive = false;
	bool owner = false;
	int ch, r;
	uid_t uid;
	gid_t gid;

	uid = 0;
	gid = 0;
	r = 0;

# define OPTIONS	"iog:u:"
	while ((ch = getopt(argc, argv, OPTIONS)) != -1)
	{
		switch ((char) ch)
		{
		  case 'g':
			gid = (gid_t)strtoul(optarg, 0, 0);
			break;

		  case 'i':
			interactive = true;
			break;

		  case 'u':
			uid = (uid_t)strtoul(optarg, 0, 0);
			break;

		  case 'o':
			owner = true;
			break;

		  default:
			break;
		}
	}

	if (interactive)
		r = seminter(owner);
	else
	{
		pid_t pid;

		printf("This test takes about 8 seconds.\n");
		printf("If it takes longer than 30 seconds, please interrupt it\n");
		printf("and compile again without semaphore support, i.e.,");
		printf("-DSM_CONF_SEM=0\n");
		if ((pid = fork()) < 0)
		{
			perror("fork failed\n");
			return -1;
		}

		sm_test_begin(argc, argv, "test semaphores");
		if (pid == 0)
		{
			/* give the parent the chance to setup data */
			sleep(1);
			r = semtest(false, uid, gid);
		}
		else
		{
			r = semtest(true, uid, gid);
		}
		SM_TEST(r == 0);
		return sm_test_end();
	}
	return r;
}
#else /* SM_CONF_SEM */
int
main(argc, argv)
	int argc;
	char *argv[];
{
	printf("No support for semaphores configured on this machine\n");
	return 0;
}
#endif /* SM_CONF_SEM */
