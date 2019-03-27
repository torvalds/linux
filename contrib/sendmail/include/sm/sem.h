/*
 * Copyright (c) 2000-2001, 2005, 2008 Proofpoint, Inc. and its suppliers.
 *      All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: sem.h,v 1.11 2013-11-22 20:51:31 ca Exp $
 */

#ifndef SM_SEM_H
# define SM_SEM_H 1

#include <sm/gen.h>

/* key for semaphores */
# define SM_SEM_KEY	(41L)
# define SM_SEM_NO_ID	(-1)
# define SM_NO_SEM(id)	((id) < 0)

# if SM_CONF_SEM > 0
#  include <sys/types.h>
#  include <sys/ipc.h>
#  include <sys/sem.h>

# if SM_CONF_SEM == 2
union semun
{
	int		val;
	struct semid_ds	*buf;
	ushort		*array;
};
# endif /* SM_CONF_SEM == 2 */

# ifndef SEM_A
#  define SEM_A	0200
# endif /* SEM_A */
# ifndef SEM_R
#  define SEM_R	0400
# endif /* SEM_R */

# define SM_NSEM	1

extern int sm_sem_start __P((key_t, int, int, bool));
extern int sm_sem_stop __P((int));
extern int sm_sem_acq __P((int, int, int));
extern int sm_sem_rel __P((int, int, int));
extern int sm_sem_get __P((int, int));
extern int sm_semsetowner __P((int, uid_t, gid_t, mode_t));

# else /* SM_CONF_SEM > 0 */
#  define sm_sem_start(key, nsem, semflg, owner) 0
#  define sm_sem_stop(semid)	0
#  define sm_sem_acq(semid, semnum, timeout)	0
#  define sm_sem_rel(semid, semnum, timeout)	0
#  define sm_sem_get(semid, semnum)	0
# endif /* SM_CONF_SEM > 0 */

#endif /* ! SM_SEM_H */
