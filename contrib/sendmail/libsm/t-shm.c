/*
 * Copyright (c) 2000-2002, 2004, 2005 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: t-shm.c,v 1.23 2013-11-22 20:51:43 ca Exp $")

#include <stdio.h>

#if SM_CONF_SHM
# include <stdlib.h>
# include <unistd.h>
# include <sys/wait.h>

# include <sm/heap.h>
# include <sm/string.h>
# include <sm/test.h>
# include <sm/shm.h>

# define SHMSIZE	1024
# define SHM_MAX	6400000
# define T_SHMKEY	21


/*
**  SHMINTER -- interactive testing of shared memory
**
**	Parameters:
**		owner -- create segment.
**
**	Returns:
**		0 on success
**		< 0 on failure.
*/

int shminter __P((bool));

int
shminter(owner)
	bool owner;
{
	int *shm, shmid;
	int i, t;

	shm = (int *) sm_shmstart(T_SHMKEY, SHMSIZE, 0, &shmid, owner);
	if (shm == (int *) 0)
	{
		perror("shminit failed");
		return -1;
	}

	while ((t = getchar()) != EOF)
	{
		switch (t)
		{
		  case 'c':
			*shm = 0;
			break;
		  case 'i':
			++*shm;
			break;
		  case 'd':
			--*shm;
			break;
		  case 's':
			sleep(1);
			break;
		  case 'l':
			t = *shm;
			for (i = 0; i < SHM_MAX; i++)
			{
				++*shm;
			}
			if (*shm != SHM_MAX + t)
				fprintf(stderr, "error: %d != %d\n",
					*shm, SHM_MAX + t);
			break;
		  case 'v':
			printf("shmval: %d\n", *shm);
			break;
		  case 'S':
			i = sm_shmsetowner(shmid, getuid(), getgid(), 0644);
			printf("sm_shmsetowner=%d\n", i);
			break;
		}
	}
	return sm_shmstop((void *) shm, shmid, owner);
}


/*
**  SHMBIG -- testing of shared memory
**
**	Parameters:
**		owner -- create segment.
**		size -- size of segment.
**
**	Returns:
**		0 on success
**		< 0 on failure.
*/

int shmbig __P((bool, int));

int
shmbig(owner, size)
	bool owner;
	int size;
{
	int *shm, shmid;
	int i;

	shm = (int *) sm_shmstart(T_SHMKEY, size, 0, &shmid, owner);
	if (shm == (int *) 0)
	{
		perror("shminit failed");
		return -1;
	}

	for (i = 0; i < size / sizeof(int); i++)
		shm[i] = i;
	for (i = 0; i < size / sizeof(int); i++)
	{
		if (shm[i] != i)
		{
			fprintf(stderr, "failed at %d: %d", i, shm[i]);
		}
	}

	return sm_shmstop((void *) shm, shmid, owner);
}


/*
**  SHMTEST -- test of shared memory
**
**	Parameters:
**		owner -- create segment.
**
**	Returns:
**		0 on success
**		< 0 on failure.
*/

# define MAX_CNT	10

int shmtest __P((int));

int
shmtest(owner)
	int owner;
{
	int *shm, shmid;
	int cnt = 0;

	shm = (int *) sm_shmstart(T_SHMKEY, SHMSIZE, 0, &shmid, owner);
	if (shm == (int *) 0)
	{
		perror("shminit failed");
		return -1;
	}

	if (owner)
	{
		int r;

		r = sm_shmsetowner(shmid, getuid(), getgid(), 0660);
		SM_TEST(r == 0);
		*shm = 1;
		while (*shm == 1 && cnt++ < MAX_CNT)
			sleep(1);
		SM_TEST(cnt <= MAX_CNT);

		/* release and re-acquire the segment */
		r = sm_shmstop((void *) shm, shmid, owner);
		SM_TEST(r == 0);
		shm = (int *) sm_shmstart(T_SHMKEY, SHMSIZE, 0, &shmid, owner);
		SM_TEST(shm != (int *) 0);
	}
	else
	{
		while (*shm != 1 && cnt++ < MAX_CNT)
			sleep(1);
		SM_TEST(cnt <= MAX_CNT);
		*shm = 2;

		/* wait a momemt so the segment is still in use */
		sleep(2);
	}
	return sm_shmstop((void *) shm, shmid, owner);
}

int
main(argc, argv)
	int argc;
	char *argv[];
{
	bool interactive = false;
	bool owner = false;
	int big = -1;
	int ch;
	int r = 0;
	int status;
	extern char *optarg;

# define OPTIONS	"b:io"
	while ((ch = getopt(argc, argv, OPTIONS)) != -1)
	{
		switch ((char) ch)
		{
		  case 'b':
			big = atoi(optarg);
			break;

		  case 'i':
			interactive = true;
			break;

		  case 'o':
			owner = true;
			break;

		  default:
			break;
		}
	}

	if (interactive)
		r = shminter(owner);
	else if (big > 0)
		r = shmbig(true, big);
	else
	{
		pid_t pid;
		extern int SmTestNumErrors;

		if ((pid = fork()) < 0)
		{
			perror("fork failed\n");
			return -1;
		}

		sm_test_begin(argc, argv, "test shared memory");
		if (pid == 0)
		{
			/* give the parent the chance to setup data */
			sleep(1);
			r = shmtest(false);
		}
		else
		{
			r = shmtest(true);
			(void) wait(&status);
		}
		SM_TEST(r == 0);
		if (SmTestNumErrors > 0)
			printf("add -DSM_CONF_SHM=0 to confENVDEF in devtools/Site/site.config.m4\nand start over.\n");
		return sm_test_end();
	}
	return r;
}
#else /* SM_CONF_SHM */
int
main(argc, argv)
	int argc;
	char *argv[];
{
	printf("No support for shared memory configured on this machine\n");
	return 0;
}
#endif /* SM_CONF_SHM */
