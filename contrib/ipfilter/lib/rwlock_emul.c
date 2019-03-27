/*	$FreeBSD$	*/

/*
 * Copyright (C) 2012 by Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * $Id$
 */

#include "ipf.h"

#define	EMM_MAGIC	0x97dd8b3a

void eMrwlock_read_enter(rw, file, line)
	eMrwlock_t *rw;
	char *file;
	int line;
{
	if (rw->eMrw_magic != EMM_MAGIC) {
		fprintf(stderr, "%s:eMrwlock_read_enter(%p): bad magic: %#x\n",
			rw->eMrw_owner, rw, rw->eMrw_magic);
		abort();
	}
	if (rw->eMrw_read != 0 || rw->eMrw_write != 0) {
		fprintf(stderr,
			"%s:eMrwlock_read_enter(%p): already locked: %d/%d\n",
			rw->eMrw_owner, rw, rw->eMrw_read, rw->eMrw_write);
		abort();
	}
	rw->eMrw_read++;
	rw->eMrw_heldin = file;
	rw->eMrw_heldat = line;
}


void eMrwlock_write_enter(rw, file, line)
	eMrwlock_t *rw;
	char *file;
	int line;
{
	if (rw->eMrw_magic != EMM_MAGIC) {
		fprintf(stderr, "%s:eMrwlock_write_enter(%p): bad magic: %#x\n",
			rw->eMrw_owner, rw, rw->eMrw_magic);
		abort();
	}
	if (rw->eMrw_read != 0 || rw->eMrw_write != 0) {
		fprintf(stderr,
			"%s:eMrwlock_write_enter(%p): already locked: %d/%d\n",
			rw->eMrw_owner, rw, rw->eMrw_read, rw->eMrw_write);
		abort();
	}
	rw->eMrw_write++;
	rw->eMrw_heldin = file;
	rw->eMrw_heldat = line;
}


void eMrwlock_try_upgrade(rw, file, line)
	eMrwlock_t *rw;
	char *file;
	int line;
{
	if (rw->eMrw_magic != EMM_MAGIC) {
		fprintf(stderr, "%s:eMrwlock_write_enter(%p): bad magic: %#x\n",
			rw->eMrw_owner, rw, rw->eMrw_magic);
		abort();
	}
	if (rw->eMrw_read != 0 || rw->eMrw_write != 0) {
		fprintf(stderr,
			"%s:eMrwlock_try_upgrade(%p): already locked: %d/%d\n",
			rw->eMrw_owner, rw, rw->eMrw_read, rw->eMrw_write);
		abort();
	}
	rw->eMrw_write++;
	rw->eMrw_heldin = file;
	rw->eMrw_heldat = line;
}

void eMrwlock_downgrade(rw, file, line)
	eMrwlock_t *rw;
	char *file;
	int line;
{
	if (rw->eMrw_magic != EMM_MAGIC) {
		fprintf(stderr, "%s:eMrwlock_write_enter(%p): bad magic: %#x\n",
			rw->eMrw_owner, rw, rw->eMrw_magic);
		abort();
	}
	if (rw->eMrw_read != 0 || rw->eMrw_write != 1) {
		fprintf(stderr,
			"%s:eMrwlock_write_enter(%p): already locked: %d/%d\n",
			rw->eMrw_owner, rw, rw->eMrw_read, rw->eMrw_write);
		abort();
	}
	rw->eMrw_write--;
	rw->eMrw_read++;
	rw->eMrw_heldin = file;
	rw->eMrw_heldat = line;
}


void eMrwlock_exit(rw)
	eMrwlock_t *rw;
{
	if (rw->eMrw_magic != EMM_MAGIC) {
		fprintf(stderr, "%s:eMrwlock_exit(%p): bad magic: %#x\n",
			rw->eMrw_owner, rw, rw->eMrw_magic);
		abort();
	}
	if (rw->eMrw_read != 1 && rw->eMrw_write != 1) {
		fprintf(stderr, "%s:eMrwlock_exit(%p): not locked: %d/%d\n",
			rw->eMrw_owner, rw, rw->eMrw_read, rw->eMrw_write);
		abort();
	}
	if (rw->eMrw_read == 1)
		rw->eMrw_read--;
	else if (rw->eMrw_write == 1)
		rw->eMrw_write--;
	rw->eMrw_heldin = NULL;
	rw->eMrw_heldat = 0;
}


static int initcount = 0;

void eMrwlock_init(rw, who)
	eMrwlock_t *rw;
	char *who;
{
	if (rw->eMrw_magic == EMM_MAGIC) {	/* safe bet ? */
		fprintf(stderr,
			"%s:eMrwlock_init(%p): already initialised?: %#x\n",
			rw->eMrw_owner, rw, rw->eMrw_magic);
		abort();
	}
	rw->eMrw_magic = EMM_MAGIC;
	rw->eMrw_read = 0;
	rw->eMrw_write = 0;
	if (who != NULL)
		rw->eMrw_owner = strdup(who);
	else
		rw->eMrw_owner = NULL;
	initcount++;
}


void eMrwlock_destroy(rw)
	eMrwlock_t *rw;
{
	if (rw->eMrw_magic != EMM_MAGIC) {
		fprintf(stderr, "%s:eMrwlock_destroy(%p): bad magic: %#x\n",
			rw->eMrw_owner, rw, rw->eMrw_magic);
		abort();
	}
	if (rw->eMrw_owner != NULL)
		free(rw->eMrw_owner);
	memset(rw, 0xa5, sizeof(*rw));
	initcount--;
}

void ipf_rwlock_clean()
{
	if (initcount != 0)
		abort();
}
