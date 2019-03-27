/*	$NetBSD: sem.c,v 1.11 2017/01/13 21:30:42 christos Exp $	*/

/*
 * Common code for semaphore tests.  This can be included both into
 * programs using librt and libpthread.
 */

#include <sys/types.h>

#include <rump/rump.h>
#include <rump/rump_syscalls.h>

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "h_macros.h"

ATF_TC(postwait);
ATF_TC_HEAD(postwait, tc)
{

	atf_tc_set_md_var(tc, "descr", "tests post and wait from a "
	    "single thread (%s)", LIBNAME);
}

ATF_TC_BODY(postwait, tc)
{
	sem_t sem;
	int rv;

	rump_init();

	ATF_REQUIRE_EQ(sem_init(&sem, 1, 0), 0);

	sem_post(&sem);
	sem_post(&sem);

	sem_wait(&sem);
	sem_wait(&sem);
	rv = sem_trywait(&sem);
	ATF_REQUIRE(errno == EAGAIN);
	ATF_REQUIRE(rv == -1);
}

ATF_TC(initvalue);
ATF_TC_HEAD(initvalue, tc)
{

	atf_tc_set_md_var(tc, "descr", "tests initialization with a non-zero "
	    "value (%s)", LIBNAME);
}

ATF_TC_BODY(initvalue, tc)
{
	sem_t sem;

	rump_init();
	sem_init(&sem, 1, 4);

	ATF_REQUIRE_EQ(sem_trywait(&sem), 0);
	ATF_REQUIRE_EQ(sem_trywait(&sem), 0);
	ATF_REQUIRE_EQ(sem_trywait(&sem), 0);
	ATF_REQUIRE_EQ(sem_trywait(&sem), 0);
	ATF_REQUIRE_EQ(sem_trywait(&sem), -1);
}

ATF_TC(destroy);
ATF_TC_HEAD(destroy, tc)
{

	atf_tc_set_md_var(tc, "descr", "tests sem_destroy works (%s)", LIBNAME);
}

ATF_TC_BODY(destroy, tc)
{
	sem_t sem;
	int rv, i;

	rump_init();
	for (i = 0; i < 2; i++) {
		sem_init(&sem, 1, 1);

		ATF_REQUIRE_EQ(sem_trywait(&sem), 0);
		ATF_REQUIRE_EQ(sem_trywait(&sem), -1);
		ATF_REQUIRE_EQ(sem_destroy(&sem), 0);
		rv = sem_trywait(&sem);
		ATF_REQUIRE_EQ(errno, EINVAL);
		ATF_REQUIRE_EQ(rv, -1);
	}
}

ATF_TC(busydestroy);
ATF_TC_HEAD(busydestroy, tc)
{

	atf_tc_set_md_var(tc, "descr", "tests sem_destroy report EBUSY for "
	    "a busy semaphore (%s)", LIBNAME);
}

static void *
hthread(void *arg)
{
	sem_t *semmarit = arg;

	for (;;) {
		sem_post(&semmarit[2]);
		sem_wait(&semmarit[1]);
		sem_wait(&semmarit[0]);
	}

	return NULL;
}

ATF_TC_BODY(busydestroy, tc)
{
	sem_t semmarit[3];
	pthread_t pt;
	int i;

	/* use a unicpu rump kernel.  this means less chance for race */
	setenv("RUMP_NCPU", "1", 1);

	rump_init();
	sem_init(&semmarit[0], 1, 0);
	sem_init(&semmarit[1], 1, 0);
	sem_init(&semmarit[2], 1, 0);

	pthread_create(&pt, NULL, hthread, semmarit);

	/*
	 * Make a best-effort to catch the other thread with its pants down.
	 * We can't do this for sure, can we?  Although, we could reach
	 * inside the rump kernel and inquire about the thread's sleep
	 * status.
	 */
	for (i = 0; i < 1000; i++) {
		sem_wait(&semmarit[2]);
		usleep(1);
		if (sem_destroy(&semmarit[1]) == -1)
			if (errno == EBUSY)
				break;

		/*
		 * Didn't catch it?  ok, recreate and post to make the
		 * other thread run
		 */
		sem_init(&semmarit[1], 1, 0);
		sem_post(&semmarit[0]);
		sem_post(&semmarit[1]);

	}
	if (i == 1000)
		atf_tc_fail("sem destroy not reporting EBUSY");

	pthread_cancel(pt);
	pthread_join(pt, NULL);
}

ATF_TC(blockwait);
ATF_TC_HEAD(blockwait, tc)
{

	atf_tc_set_md_var(tc, "descr", "tests sem_wait can handle blocking "
	    "(%s)", LIBNAME);
	atf_tc_set_md_var(tc, "timeout", "2");
}

ATF_TC_BODY(blockwait, tc)
{
	sem_t semmarit[3];
	pthread_t pt;
	int i;

	rump_init();
	sem_init(&semmarit[0], 1, 0);
	sem_init(&semmarit[1], 1, 0);
	sem_init(&semmarit[2], 1, 0);

	pthread_create(&pt, NULL, hthread, semmarit);

	/*
	 * Make a best-effort.  Unless we're extremely unlucky, we should
	 * at least one blocking wait.
	 */
	for (i = 0; i < 10; i++) {
		sem_wait(&semmarit[2]);
		usleep(1);
		sem_post(&semmarit[0]);
		sem_post(&semmarit[1]);

	}

	pthread_cancel(pt);
	pthread_join(pt, NULL);
}

ATF_TC(blocktimedwait);
ATF_TC_HEAD(blocktimedwait, tc)
{

	atf_tc_set_md_var(tc, "descr", "tests sem_timedwait can handle blocking"
	    " (%s)", LIBNAME);
	atf_tc_set_md_var(tc, "timeout", "2");
}

ATF_TC_BODY(blocktimedwait, tc)
{
	sem_t semid;
	struct timespec tp;

	rump_init();

	clock_gettime(CLOCK_REALTIME, &tp);
	tp.tv_nsec += 50000000;
	tp.tv_sec += tp.tv_nsec / 1000000000;
	tp.tv_nsec %= 1000000000;

	ATF_REQUIRE_EQ(sem_init(&semid, 1, 0), 0);
	ATF_REQUIRE_ERRNO(ETIMEDOUT, sem_timedwait(&semid, &tp) == -1);
}

ATF_TC(named);
ATF_TC_HEAD(named, tc)
{

	atf_tc_set_md_var(tc, "descr", "tests named semaphores (%s)", LIBNAME);
}

/*
 * Wow, easy naming rules.  it's these times i'm really happy i can
 * single-step into the kernel.
 */
#define SEM1 "/precious_sem"
#define SEM2 "/justsem"
ATF_TC_BODY(named, tc)
{
	sem_t *sem1, *sem2;
	void *rv;

	rump_init();
	sem1 = sem_open(SEM1, 0);
	ATF_REQUIRE_EQ(errno, ENOENT);
	ATF_REQUIRE_EQ(sem1, NULL);

	sem1 = sem_open(SEM1, O_CREAT, 0444, 1);
	if (sem1 == NULL)
		atf_tc_fail_errno("sem_open O_CREAT");

	rv = sem_open(SEM1, O_CREAT | O_EXCL);
	ATF_REQUIRE_EQ(errno, EEXIST);
	ATF_REQUIRE_EQ(rv, NULL);

	sem2 = sem_open(SEM2, O_CREAT, 0444, 0);
	if (sem2 == NULL)
		atf_tc_fail_errno("sem_open O_CREAT");

	/* check that semaphores are independent */
	ATF_REQUIRE_EQ(sem_trywait(sem2), -1);
	ATF_REQUIRE_EQ(sem_trywait(sem1), 0);
	ATF_REQUIRE_EQ(sem_trywait(sem1), -1);

	/* check that unlinked remains valid */
	sem_unlink(SEM2);
	ATF_REQUIRE_EQ(sem_post(sem2), 0);
	ATF_REQUIRE_EQ(sem_trywait(sem2), 0);
	ATF_REQUIRE_EQ(sem_trywait(sem2), -1);
	ATF_REQUIRE_EQ(errno, EAGAIN);

#if 0 /* see unlink */
	/* close it and check that it's gone */
	if (sem_close(sem2) != 0)
		atf_tc_fail_errno("sem close");
	ATF_REQUIRE_EQ(sem_trywait(sem2), -1);
	ATF_REQUIRE_EQ(errno, EINVAL);
#endif

	/* check that we still have sem1 */
	sem_post(sem1);
	ATF_REQUIRE_EQ(sem_trywait(sem1), 0);
	ATF_REQUIRE_EQ(sem_trywait(sem1), -1);
	ATF_REQUIRE_EQ(errno, EAGAIN);
}

ATF_TC(unlink);
ATF_TC_HEAD(unlink, tc)
{

	/* this is currently broken.  i'll append the PR number soon */
	atf_tc_set_md_var(tc, "descr", "tests unlinked semaphores can be "
	    "closed (%s)", LIBNAME);
}

#define SEM "/thesem"
ATF_TC_BODY(unlink, tc)
{
	sem_t *sem;

	rump_init();
	sem = sem_open(SEM, O_CREAT, 0444, 0);
	ATF_REQUIRE(sem);

	if (sem_unlink(SEM) == -1)
		atf_tc_fail_errno("unlink");
	if (sem_close(sem) == -1)
		atf_tc_fail_errno("close unlinked semaphore");
}

/* use rump calls for libpthread _ksem_foo() calls */
#define F1(name, a) int _ksem_##name(a); \
int _ksem_##name(a v1) {return rump_sys__ksem_##name(v1);}
#define F2(name, a, b) int _ksem_##name(a, b); \
int _ksem_##name(a v1, b v2) {return rump_sys__ksem_##name(v1, v2);}
F2(init, unsigned int, intptr_t *);
F1(close, intptr_t);
F1(destroy, intptr_t);
F1(post, intptr_t);
F1(unlink, const char *);
F1(trywait, intptr_t);
F1(wait, intptr_t);
F2(getvalue, intptr_t, unsigned int *);
F2(timedwait, intptr_t, const struct timespec *);
int _ksem_open(const char *, int, mode_t, unsigned int, intptr_t *);
int _ksem_open(const char *a, int b, mode_t c, unsigned int d, intptr_t *e)
    {return rump_sys__ksem_open(a,b,c,d,e);}
