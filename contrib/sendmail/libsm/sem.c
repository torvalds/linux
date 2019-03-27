/*
 * Copyright (c) 2000-2001, 2005, 2008 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: sem.c,v 1.15 2013-11-22 20:51:43 ca Exp $")

#if SM_CONF_SEM
# include <stdlib.h>
# include <unistd.h>
# include <sm/string.h>
# include <sm/sem.h>
# include <sm/heap.h>
# include <errno.h>

/*
**  SM_SEM_START -- initialize semaphores
**
**	Parameters:
**		key -- key for semaphores.
**		nsem -- number of semaphores.
**		semflg -- flag for semget(), if 0, use a default.
**		owner -- create semaphores.
**
**	Returns:
**		id for semaphores.
**		< 0 on failure.
*/

int
sm_sem_start(key, nsem, semflg, owner)
	key_t key;
	int nsem;
	int semflg;
	bool owner;
{
	int semid, i, err;
	unsigned short *semvals;

	semvals = NULL;
	if (semflg == 0)
		semflg = (SEM_A|SEM_R)|((SEM_A|SEM_R) >> 3);
	if (owner)
		semflg |= IPC_CREAT|IPC_EXCL;
	semid = semget(key, nsem, semflg);
	if (semid < 0)
		goto error;

	if (owner)
	{
		union semun semarg;

		semvals = (unsigned short *) sm_malloc(nsem * sizeof semvals);
		if (semvals == NULL)
			goto error;
		semarg.array = semvals;

		/* initialize semaphore values to be available */
		for (i = 0; i < nsem; i++)
			semvals[i] = 1;
		if (semctl(semid, 0, SETALL, semarg) < 0)
			goto error;
	}
	return semid;

error:
	err = errno;
	if (semvals != NULL)
		sm_free(semvals);
	if (semid >= 0)
		sm_sem_stop(semid);
	return (err > 0) ? (0 - err) : -1;
}

/*
**  SM_SEM_STOP -- stop using semaphores.
**
**	Parameters:
**		semid -- id for semaphores.
**
**	Returns:
**		0 on success.
**		< 0 on failure.
*/

int
sm_sem_stop(semid)
	int semid;
{
	return semctl(semid, 0, IPC_RMID, NULL);
}

/*
**  SM_SEM_ACQ -- acquire semaphore.
**
**	Parameters:
**		semid -- id for semaphores.
**		semnum -- number of semaphore.
**		timeout -- how long to wait for operation to succeed.
**
**	Returns:
**		0 on success.
**		< 0 on failure.
*/

int
sm_sem_acq(semid, semnum, timeout)
	int semid;
	int semnum;
	int timeout;
{
	int r;
	struct sembuf semops[1];

	semops[0].sem_num = semnum;
	semops[0].sem_op = -1;
	semops[0].sem_flg = SEM_UNDO |
			    (timeout != SM_TIME_FOREVER ? 0 : IPC_NOWAIT);
	if (timeout == SM_TIME_IMMEDIATE || timeout == SM_TIME_FOREVER)
		return semop(semid, semops, 1);
	do
	{
		r = semop(semid, semops, 1);
		if (r == 0)
			return r;
		sleep(1);
		--timeout;
	} while (timeout > 0);
	return r;
}

/*
**  SM_SEM_REL -- release semaphore.
**
**	Parameters:
**		semid -- id for semaphores.
**		semnum -- number of semaphore.
**		timeout -- how long to wait for operation to succeed.
**
**	Returns:
**		0 on success.
**		< 0 on failure.
*/

int
sm_sem_rel(semid, semnum, timeout)
	int semid;
	int semnum;
	int timeout;
{
	int r;
	struct sembuf semops[1];

#if PARANOID
	/* XXX should we check whether the value is already 0 ? */
	SM_REQUIRE(sm_get_sem(semid, semnum) > 0);
#endif /* PARANOID */

	semops[0].sem_num = semnum;
	semops[0].sem_op = 1;
	semops[0].sem_flg = SEM_UNDO |
			    (timeout != SM_TIME_FOREVER ? 0 : IPC_NOWAIT);
	if (timeout == SM_TIME_IMMEDIATE || timeout == SM_TIME_FOREVER)
		return semop(semid, semops, 1);
	do
	{
		r = semop(semid, semops, 1);
		if (r == 0)
			return r;
		sleep(1);
		--timeout;
	} while (timeout > 0);
	return r;
}

/*
**  SM_SEM_GET -- get semaphore value.
**
**	Parameters:
**		semid -- id for semaphores.
**		semnum -- number of semaphore.
**
**	Returns:
**		value of semaphore on success.
**		< 0 on failure.
*/

int
sm_sem_get(semid, semnum)
	int semid;
	int semnum;
{
	int semval;

	if ((semval = semctl(semid, semnum, GETVAL, NULL)) < 0)
		return -1;
	return semval;
}

/*
**  SM_SEMSETOWNER -- set owner/group/mode of semaphores.
**
**	Parameters:
**		semid -- id for semaphores.
**		uid -- uid to use
**		gid -- gid to use
**		mode -- mode to use
**
**	Returns:
**		0 on success.
**		< 0 on failure.
*/

int
sm_semsetowner(semid, uid, gid, mode)
	int semid;
	uid_t uid;
	gid_t gid;
	mode_t mode;
{
	int r;
	struct semid_ds	semidds;
	union semun {
		int		val;
		struct semid_ds	*buf;
		ushort		*array;
	} arg;

	memset(&semidds, 0, sizeof(semidds));
	arg.buf = &semidds;
	if ((r = semctl(semid, 1, IPC_STAT, arg)) < 0)
		return r;
	semidds.sem_perm.uid = uid;
	semidds.sem_perm.gid = gid;
	semidds.sem_perm.mode = mode;
	if ((r = semctl(semid, 1, IPC_SET, arg)) < 0)
		return r;
	return 0;
}
#endif /* SM_CONF_SEM */
