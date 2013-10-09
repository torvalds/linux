/*
 * copyright Oracle 2007.  Licensed under GPLv2
 * To compile: gcc -Wall -o sembench sembench.c -lpthread
 *
 * usage: sembench -t thread count -w wakenum -r runtime -o op
 * op can be: 0 (ipc sem) 1 (nanosleep) 2 (futexes)
 *
 * example:
 *	sembench -t 1024 -w 512 -r 60 -o 2
 * runs 1024 threads, waking up 512 at a time, running for 60 seconds using
 * futex locking.
 *
 */
#define  _GNU_SOURCE
#define _POSIX_C_SOURCE 199309
#include <fcntl.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/syscall.h>
#include <errno.h>

#define VERSION "0.2"

/* futexes have been around since 2.5.something, but it still seems I
 * need to make my own syscall.  Sigh.
 */
#define FUTEX_WAIT              0
#define FUTEX_WAKE              1
#define FUTEX_FD                2
#define FUTEX_REQUEUE           3
#define FUTEX_CMP_REQUEUE       4
#define FUTEX_WAKE_OP           5
static inline int futex (int *uaddr, int op, int val,
			 const struct timespec *timeout,
			 int *uaddr2, int val3)
{
	return syscall(__NR_futex, uaddr, op, val, timeout, uaddr2, val3);
}

static void smp_mb(void)
{
	__sync_synchronize();
}

static int all_done = 0;
static int timeout_test = 0;

#define SEMS_PERID 250

struct sem_operations;

struct lockinfo {
	unsigned long id;
	unsigned long index;
	int data;
	pthread_t tid;
	struct lockinfo *next;
	struct sem_operations *ops;
	unsigned long ready;
};

struct sem_wakeup_info {
	int wakeup_count;
	struct sembuf sb[SEMS_PERID];
};

struct sem_operations {
	void (*wait)(struct lockinfo *l);
	int (*wake)(struct sem_wakeup_info *wi, int num_semids, int num);
	void (*setup)(struct sem_wakeup_info **wi, int num_semids);
	void (*cleanup)(int num_semids);
	char *name;
};

int *semid_lookup = NULL;

pthread_mutex_t worklist_mutex = PTHREAD_MUTEX_INITIALIZER;
static unsigned long total_burns = 0;
static unsigned long min_burns = ~0UL;
static unsigned long max_burns = 0;

/* currently running threads */
static int thread_count = 0;

struct lockinfo *worklist = NULL;
static int workers_started = 0;

/* total threads started */
static int num_threads = 2048;

static void worklist_add(struct lockinfo *l)
{
	smp_mb();
	l->ready = 1;
}

static struct lockinfo *worklist_rm(void)
{
	static int last_index = 0;
	int i;
	struct lockinfo *l;

	for (i = 0; i < num_threads; i++) {
		int test = (last_index + i) % num_threads;

		l = worklist + test;
		smp_mb();
		if (l->ready) {
			l->ready = 0;
			last_index = test;
			return l;
		}
	}
	return NULL;
}

/* ipc semaphore post& wait */
void wait_ipc_sem(struct lockinfo *l)
{
	struct sembuf sb;
	int ret;
	struct timespec *tvp = NULL;
	struct timespec tv = { 0, 1 };

	sb.sem_num = l->index;
	sb.sem_flg = 0;

	sb.sem_op = -1;
	l->data = 1;

	if (timeout_test && (l->id % 5) == 0)
		tvp = &tv;

	worklist_add(l);
	ret = semtimedop(semid_lookup[l->id], &sb, 1, tvp);

	while(l->data != 0 && tvp) {
		struct timespec tv2 = { 0, 500 };
		nanosleep(&tv2, NULL);
	}

	if (l->data != 0) {
		if (tvp)
			return;
		fprintf(stderr, "wakeup without data update\n");
		exit(1);
	}
	if (ret) {
		if (errno == EAGAIN && tvp)
			return;
		perror("semtimed op");
		exit(1);
	}
}

int ipc_wake_some(struct sem_wakeup_info *wi, int num_semids, int num)
{
	int i;
	int ret;
	struct lockinfo *l;
	int found = 0;

	for (i = 0; i < num_semids; i++) {
		wi[i].wakeup_count = 0;
	}
	while(num > 0) {
		struct sembuf *sb;
		l = worklist_rm();
		if (!l)
			break;
		if (l->data != 1)
			fprintf(stderr, "warning, lockinfo data was %d\n",
				l->data);
		l->data = 0;
		sb = wi[l->id].sb + wi[l->id].wakeup_count;
		sb->sem_num = l->index;
		sb->sem_op = 1;
		sb->sem_flg = IPC_NOWAIT;
		wi[l->id].wakeup_count++;
		found++;
		num--;
	}
	if (!found)
		return 0;
	for (i = 0; i < num_semids; i++) {
		int wakeup_total;
		int cur;
		int offset = 0;
		if (!wi[i].wakeup_count)
			continue;
		wakeup_total = wi[i].wakeup_count;
		while(wakeup_total > 0) {
			cur = wakeup_total > 64 ? 64 : wakeup_total;
			ret = semtimedop(semid_lookup[i], wi[i].sb + offset,
					 cur, NULL);
			if (ret) {
				perror("semtimedop");
				exit(1);
			}
			offset += cur;
			wakeup_total -= cur;
		}
	}
	return found;
}

void setup_ipc_sems(struct sem_wakeup_info **wi, int num_semids)
{
	int i;
	*wi = malloc(sizeof(**wi) * num_semids);
	semid_lookup = malloc(num_semids * sizeof(int));
	for(i = 0; i < num_semids; i++) {
		semid_lookup[i] = semget(IPC_PRIVATE, SEMS_PERID,
					 IPC_CREAT | 0777);
		if (semid_lookup[i] < 0) {
			perror("semget");
			exit(1);
		}
	}
	sleep(10);
}

void cleanup_ipc_sems(int num)
{
	int i;
	for (i = 0; i < num; i++) {
		semctl(semid_lookup[i], 0, IPC_RMID);
	}
}

struct sem_operations ipc_sem_ops = {
	.wait = wait_ipc_sem,
	.wake = ipc_wake_some,
	.setup = setup_ipc_sems,
	.cleanup = cleanup_ipc_sems,
	.name = "ipc sem operations",
};

/* futex post & wait */
void wait_futex_sem(struct lockinfo *l)
{
	int ret;
	l->data = 1;
	worklist_add(l);
	while(l->data == 1) {
		ret = futex(&l->data, FUTEX_WAIT, 1, NULL, NULL, 0);
		/*
		if (ret && ret != EWOULDBLOCK) {
			perror("futex wait");
			exit(1);
		}*/
	}
}

int futex_wake_some(struct sem_wakeup_info *wi, int num_semids, int num)
{
	int i;
	int ret;
	struct lockinfo *l;
	int found = 0;

	for (i = 0; i < num; i++) {
		l = worklist_rm();
		if (!l)
			break;
		if (l->data != 1)
			fprintf(stderr, "warning, lockinfo data was %d\n",
				l->data);
		l->data = 0;
		ret = futex(&l->data, FUTEX_WAKE, 1, NULL, NULL, 0);
		if (ret < 0) {
			perror("futex wake");
			exit(1);
		}
		found++;
	}
	return found;
}

void setup_futex_sems(struct sem_wakeup_info **wi, int num_semids)
{
	return;
}

void cleanup_futex_sems(int num)
{
	return;
}

struct sem_operations futex_sem_ops = {
	.wait = wait_futex_sem,
	.wake = futex_wake_some,
	.setup = setup_futex_sems,
	.cleanup = cleanup_futex_sems,
	.name = "futex sem operations",
};

/* nanosleep sems here */
void wait_nanosleep_sem(struct lockinfo *l)
{
	int ret;
	struct timespec tv = { 0, 1000000 };
	int count = 0;

	l->data = 1;
	worklist_add(l);
	while(l->data) {
		ret = nanosleep(&tv, NULL);
		if (ret) {
			perror("nanosleep");
			exit(1);
		}
		count++;
	}
}

int nanosleep_wake_some(struct sem_wakeup_info *wi, int num_semids, int num)
{
	int i;
	struct lockinfo *l;

	for (i = 0; i < num; i++) {
		l = worklist_rm();
		if (!l)
			break;
		if (l->data != 1)
			fprintf(stderr, "warning, lockinfo data was %d\n",
				l->data);
		l->data = 0;
	}
	return i;
}

void setup_nanosleep_sems(struct sem_wakeup_info **wi, int num_semids)
{
	return;
}

void cleanup_nanosleep_sems(int num)
{
	return;
}

struct sem_operations nanosleep_sem_ops = {
	.wait = wait_nanosleep_sem,
	.wake = nanosleep_wake_some,
	.setup = setup_nanosleep_sems,
	.cleanup = cleanup_nanosleep_sems,
	.name = "nano sleep sem operations",
};

void *worker(void *arg)
{
	struct lockinfo *l = (struct lockinfo *)arg;
	int burn_count = 0;
	pthread_t tid = pthread_self();
	size_t pagesize = getpagesize();
	char *buf = malloc(pagesize);

	if (!buf) {
		perror("malloc");
		exit(1);
	}

	l->tid = tid;
	workers_started = 1;
	smp_mb();

	while(!all_done) {
		l->ops->wait(l);
		if (all_done)
			break;
		burn_count++;
	}
	pthread_mutex_lock(&worklist_mutex);
	total_burns += burn_count;
	if (burn_count < min_burns)
		min_burns = burn_count;
	if (burn_count > max_burns)
		max_burns = burn_count;
	thread_count--;
	pthread_mutex_unlock(&worklist_mutex);
	return (void *)0;
}

void print_usage(void)
{
	printf("usage: sembench [-t threads] [-w wake incr] [-r runtime]");
	printf("                [-o num] (0=ipc, 1=nanosleep, 2=futex)\n");
	exit(1);
}

#define NUM_OPERATIONS 3
struct sem_operations *allops[NUM_OPERATIONS] = { &ipc_sem_ops,
						&nanosleep_sem_ops,
						&futex_sem_ops};

int main(int ac, char **av) {
	int ret;
	int i;
	int semid = 0;
	int sem_num = 0;
	int burn_count = 0;
	struct sem_wakeup_info *wi = NULL;
	struct timeval start;
	struct timeval now;
	int num_semids = 0;
	int wake_num = 256;
	int run_secs = 30;
	int pagesize = getpagesize();
	char *buf = malloc(pagesize);
	struct sem_operations *ops = allops[0];
	cpu_set_t cpu_mask;
	cpu_set_t target_mask;
	int target_cpu = 0;
	int max_cpu = -1;

	if (!buf) {
		perror("malloc");
		exit(1);
	}
	for (i = 1; i < ac; i++) {
		if (strcmp(av[i], "-t") == 0) {
			if (i == ac -1)
				print_usage();
			num_threads = atoi(av[i+1]);
			i++;
		} else if (strcmp(av[i], "-w") == 0) {
			if (i == ac -1)
				print_usage();
			wake_num = atoi(av[i+1]);
			i++;
		} else if (strcmp(av[i], "-r") == 0) {
			if (i == ac -1)
				print_usage();
			run_secs = atoi(av[i+1]);
			i++;
		} else if (strcmp(av[i], "-o") == 0) {
			int index;
			if (i == ac -1)
				print_usage();
			index = atoi(av[i+1]);
			if (index >= NUM_OPERATIONS) {
				fprintf(stderr, "invalid operations %d\n",
					index);
				exit(1);
			}
			ops = allops[index];
			i++;
		} else if (strcmp(av[i], "-T") == 0) {
			timeout_test = 1;
		} else if (strcmp(av[i], "-h") == 0) {
			print_usage();
		}
	}
	num_semids = (num_threads + SEMS_PERID - 1) / SEMS_PERID;
	ops->setup(&wi, num_semids);

	ret = sched_getaffinity(0, sizeof(cpu_set_t), &cpu_mask);
	if (ret) {
		perror("sched_getaffinity");
		exit(1);
	}
	for (i = 0; i < CPU_SETSIZE; i++)
		if (CPU_ISSET(i, &cpu_mask))
			max_cpu = i;
	if (max_cpu == -1) {
		fprintf(stderr, "sched_getaffinity returned empty mask\n");
		exit(1);
	}

	CPU_ZERO(&target_mask);

	worklist = malloc(sizeof(*worklist) * num_threads);
	memset(worklist, 0, sizeof(*worklist) * num_threads);

	for (i = 0; i < num_threads; i++) {
		struct lockinfo *l;
		pthread_t tid;
		thread_count++;
		l = worklist + i;
		if (!l) {
			perror("malloc");
			exit(1);
		}
		l->id = semid;
		l->index = sem_num++;
		l->ops = ops;
		if (sem_num >= SEMS_PERID) {
			semid++;
			sem_num = 0;
		}
		ret = pthread_create(&tid, NULL, worker, (void *)l);
		if (ret) {
			perror("pthread_create");
			exit(1);
		}

		while (!CPU_ISSET(target_cpu, &cpu_mask)) {
			target_cpu++;
			if (target_cpu > max_cpu)
				target_cpu = 0;
		}
		CPU_SET(target_cpu, &target_mask);
		ret = pthread_setaffinity_np(tid, sizeof(cpu_set_t),
					     &target_mask);
		CPU_CLR(target_cpu, &target_mask);
		target_cpu++;

		ret = pthread_detach(tid);
		if (ret) {
			perror("pthread_detach");
			exit(1);
		}
	}
	while(!workers_started) {
		smp_mb();
		usleep(200);
	}
	gettimeofday(&start, NULL);
	fprintf(stderr, "main loop going\n");
	while(1) {
		ops->wake(wi, num_semids, wake_num);
		burn_count++;
		gettimeofday(&now, NULL);
		if (now.tv_sec - start.tv_sec >= run_secs)
			break;
	}
	fprintf(stderr, "all done\n");
	all_done = 1;
	while(thread_count > 0) {
		ops->wake(wi, num_semids, wake_num);
		usleep(200);
	}
	printf("%d threads, waking %d at a time\n", num_threads, wake_num);
	printf("using %s\n", ops->name);
	printf("main thread burns: %d\n", burn_count);
	printf("worker burn count total %lu min %lu max %lu avg %lu\n",
	       total_burns, min_burns, max_burns, total_burns / num_threads);
	printf("run time %d seconds %lu worker burns per second\n",
		(int)(now.tv_sec - start.tv_sec),
		total_burns / (now.tv_sec - start.tv_sec));
	ops->cleanup(num_semids);
	return 0;
}

