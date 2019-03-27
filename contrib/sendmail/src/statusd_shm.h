/*
 * Copyright (c) 1999-2000 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 *	$Id: statusd_shm.h,v 8.8 2013-11-22 20:51:57 ca Exp $
 *
 * Contributed by Exactis.com, Inc.
 *
 */

/*
**  The shared memory part of statusd.
**
**  Attach to STATUSD_SHM_KEY and update the counter appropriate
**  for your type of service.
**
*/

#define STATUSD_MAGIC	110946
#define STATUSD_SHM_KEY	(key_t)(13)
#define STATUSD_LONGS	(2)

typedef struct
{
	unsigned long	magic;
	unsigned long	ul[STATUSD_LONGS];
} STATUSD_SHM;

/*
**  Offsets into ul[]. The appropriate program
**  increments these as appropriate.
*/

#define STATUSD_COOKIE		(0)	/* reregister cookie */

/* sendmail */
#define STATUSD_SM_NSENDMAIL	(1)	/* how many running */

extern void	shmtick __P((int, int));

