/*
 * Copyright (c) 2000-2001, 2003, 2005 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 */

#include <sm/gen.h>
SM_RCSID("@(#)$Id: shm.c,v 1.20 2013-11-22 20:51:43 ca Exp $")

#if SM_CONF_SHM
# include <stdlib.h>
# include <unistd.h>
# include <errno.h>
# include <sm/string.h>
# include <sm/shm.h>


/*
**  SM_SHMSTART -- initialize shared memory segment.
**
**	Parameters:
**		key -- key for shared memory.
**		size -- size of segment.
**		shmflag -- initial flags.
**		shmid -- pointer to return id.
**		owner -- create segment.
**
**	Returns:
**		pointer to shared memory segment,
**		NULL on failure.
**
**	Side Effects:
**		attaches shared memory segment.
*/

void *
sm_shmstart(key, size, shmflg, shmid, owner)
	key_t key;
	int size;
	int shmflg;
	int *shmid;
	bool owner;
{
	int save_errno;
	void *shm = SM_SHM_NULL;

	/* default: user/group accessible */
	if (shmflg == 0)
		shmflg = SHM_R|SHM_W|(SHM_R>>3)|(SHM_W>>3);
	if (owner)
		shmflg |= IPC_CREAT|IPC_EXCL;
	*shmid = shmget(key, size, shmflg);
	if (*shmid < 0)
		goto error;

	shm = shmat(*shmid, (void *) 0, 0);
	if (shm == SM_SHM_NULL)
		goto error;

	return shm;

  error:
	save_errno = errno;
	if (shm != SM_SHM_NULL || *shmid >= 0)
		sm_shmstop(shm, *shmid, owner);
	*shmid = SM_SHM_NO_ID;
	errno = save_errno;
	return (void *) 0;
}


/*
**  SM_SHMSTOP -- stop using shared memory segment.
**
**	Parameters:
**		shm -- pointer to shared memory.
**		shmid -- id.
**		owner -- delete segment.
**
**	Returns:
**		0 on success.
**		< 0 on failure.
**
**	Side Effects:
**		detaches (and maybe removes) shared memory segment.
*/


int
sm_shmstop(shm, shmid, owner)
	void *shm;
	int shmid;
	bool owner;
{
	int r;

	if (shm != SM_SHM_NULL && (r = shmdt(shm)) < 0)
		return r;
	if (owner && shmid >= 0 && (r = shmctl(shmid, IPC_RMID, NULL)) < 0)
		return r;
	return 0;
}


/*
**  SM_SHMSETOWNER -- set owner/group/mode of shared memory segment.
**
**	Parameters:
**		shmid -- id.
**		uid -- uid to use
**		gid -- gid to use
**		mode -- mode to use
**
**	Returns:
**		0 on success.
**		< 0 on failure.
*/

int
sm_shmsetowner(shmid, uid, gid, mode)
	int shmid;
	uid_t uid;
	gid_t gid;
	mode_t mode;
{
	int r;
	struct shmid_ds shmid_ds;

	memset(&shmid_ds, 0, sizeof(shmid_ds));
	if ((r = shmctl(shmid, IPC_STAT, &shmid_ds)) < 0)
		return r;
	shmid_ds.shm_perm.uid = uid;
	shmid_ds.shm_perm.gid = gid;
	shmid_ds.shm_perm.mode = mode;
	if ((r = shmctl(shmid, IPC_SET, &shmid_ds)) < 0)
		return r;
	return 0;
}
#endif /* SM_CONF_SHM */
