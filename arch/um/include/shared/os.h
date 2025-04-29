/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2015 Anton Ivanov (aivanov@{brocade.com,kot-begemot.co.uk})
 * Copyright (C) 2015 Thomas Meyer (thomas@m3y3r.de)
 * Copyright (C) 2002 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 */

#ifndef __OS_H__
#define __OS_H__

#include <irq_user.h>
#include <longjmp.h>
#include <mm_id.h>
/* This is to get size_t */
#ifndef __UM_HOST__
#include <linux/types.h>
#else
#include <sys/types.h>
#endif

#define CATCH_EINTR(expr) while ((errno = 0, ((expr) < 0)) && (errno == EINTR))

#define OS_TYPE_FILE 1
#define OS_TYPE_DIR 2
#define OS_TYPE_SYMLINK 3
#define OS_TYPE_CHARDEV 4
#define OS_TYPE_BLOCKDEV 5
#define OS_TYPE_FIFO 6
#define OS_TYPE_SOCK 7

/* os_access() flags */
#define OS_ACC_F_OK    0       /* Test for existence.  */
#define OS_ACC_X_OK    1       /* Test for execute permission.  */
#define OS_ACC_W_OK    2       /* Test for write permission.  */
#define OS_ACC_R_OK    4       /* Test for read permission.  */
#define OS_ACC_RW_OK   (OS_ACC_W_OK | OS_ACC_R_OK) /* Test for RW permission */

#ifdef CONFIG_64BIT
#define OS_LIB_PATH	"/usr/lib64/"
#else
#define OS_LIB_PATH	"/usr/lib/"
#endif

#define OS_SENDMSG_MAX_FDS 8

/*
 * types taken from stat_file() in hostfs_user.c
 * (if they are wrong here, they are wrong there...).
 */
struct uml_stat {
	int                ust_dev;        /* device */
	unsigned long long ust_ino;        /* inode */
	int                ust_mode;       /* protection */
	int                ust_nlink;      /* number of hard links */
	int                ust_uid;        /* user ID of owner */
	int                ust_gid;        /* group ID of owner */
	unsigned long long ust_size;       /* total size, in bytes */
	int                ust_blksize;    /* blocksize for filesystem I/O */
	unsigned long long ust_blocks;     /* number of blocks allocated */
	unsigned long      ust_atime;      /* time of last access */
	unsigned long      ust_mtime;      /* time of last modification */
	unsigned long      ust_ctime;      /* time of last change */
};

struct openflags {
	unsigned int r : 1;
	unsigned int w : 1;
	unsigned int s : 1;	/* O_SYNC */
	unsigned int c : 1;	/* O_CREAT */
	unsigned int t : 1;	/* O_TRUNC */
	unsigned int a : 1;	/* O_APPEND */
	unsigned int e : 1;	/* O_EXCL */
	unsigned int cl : 1;    /* FD_CLOEXEC */
};

#define OPENFLAGS() ((struct openflags) { .r = 0, .w = 0, .s = 0, .c = 0, \
					  .t = 0, .a = 0, .e = 0, .cl = 0 })

static inline struct openflags of_read(struct openflags flags)
{
	flags.r = 1;
	return flags;
}

static inline struct openflags of_write(struct openflags flags)
{
	flags.w = 1;
	return flags;
}

static inline struct openflags of_rdwr(struct openflags flags)
{
	return of_read(of_write(flags));
}

static inline struct openflags of_set_rw(struct openflags flags, int r, int w)
{
	flags.r = r;
	flags.w = w;
	return flags;
}

static inline struct openflags of_sync(struct openflags flags)
{
	flags.s = 1;
	return flags;
}

static inline struct openflags of_create(struct openflags flags)
{
	flags.c = 1;
	return flags;
}

static inline struct openflags of_trunc(struct openflags flags)
{
	flags.t = 1;
	return flags;
}

static inline struct openflags of_append(struct openflags flags)
{
	flags.a = 1;
	return flags;
}

static inline struct openflags of_excl(struct openflags flags)
{
	flags.e = 1;
	return flags;
}

static inline struct openflags of_cloexec(struct openflags flags)
{
	flags.cl = 1;
	return flags;
}

/* file.c */
extern int os_stat_file(const char *file_name, struct uml_stat *buf);
extern int os_stat_fd(const int fd, struct uml_stat *buf);
extern int os_access(const char *file, int mode);
extern int os_set_exec_close(int fd);
extern int os_ioctl_generic(int fd, unsigned int cmd, unsigned long arg);
extern int os_get_ifname(int fd, char *namebuf);
extern int os_set_slip(int fd);
extern int os_mode_fd(int fd, int mode);

extern int os_seek_file(int fd, unsigned long long offset);
extern int os_open_file(const char *file, struct openflags flags, int mode);
extern int os_read_file(int fd, void *buf, int len);
extern int os_write_file(int fd, const void *buf, int count);
extern int os_sync_file(int fd);
extern int os_file_size(const char *file, unsigned long long *size_out);
extern int os_pread_file(int fd, void *buf, int len, unsigned long long offset);
extern int os_pwrite_file(int fd, const void *buf, int count, unsigned long long offset);
extern int os_file_modtime(const char *file, long long *modtime);
extern int os_pipe(int *fd, int stream, int close_on_exec);
extern int os_set_fd_async(int fd);
extern int os_clear_fd_async(int fd);
extern int os_set_fd_block(int fd, int blocking);
extern int os_accept_connection(int fd);
extern int os_create_unix_socket(const char *file, int len, int close_on_exec);
extern int os_shutdown_socket(int fd, int r, int w);
extern int os_dup_file(int fd);
extern void os_close_file(int fd);
ssize_t os_rcv_fd_msg(int fd, int *fds, unsigned int n_fds,
		      void *data, size_t data_len);
extern int os_connect_socket(const char *name);
extern int os_file_type(char *file);
extern int os_file_mode(const char *file, struct openflags *mode_out);
extern int os_lock_file(int fd, int excl);
extern void os_flush_stdout(void);
extern unsigned os_major(unsigned long long dev);
extern unsigned os_minor(unsigned long long dev);
extern unsigned long long os_makedev(unsigned major, unsigned minor);
extern int os_falloc_punch(int fd, unsigned long long offset, int count);
extern int os_falloc_zeroes(int fd, unsigned long long offset, int count);
extern int os_eventfd(unsigned int initval, int flags);
extern int os_sendmsg_fds(int fd, const void *buf, unsigned int len,
			  const int *fds, unsigned int fds_num);
int os_poll(unsigned int n, const int *fds);
void *os_mmap_rw_shared(int fd, size_t size);
void *os_mremap_rw_shared(void *old_addr, size_t old_size, size_t new_size);

/* start_up.c */
extern void os_early_checks(void);
extern void os_check_bugs(void);
extern void check_host_supports_tls(int *supports_tls, int *tls_min);
extern void get_host_cpu_features(
	void (*flags_helper_func)(char *line),
	void (*cache_helper_func)(char *line));

/* mem.c */
extern int create_mem_file(unsigned long long len);

/* tlb.c */
extern void report_enomem(void);

/* process.c */
extern void os_alarm_process(int pid);
extern void os_kill_process(int pid, int reap_child);
extern void os_kill_ptraced_process(int pid, int reap_child);

extern int os_getpid(void);

extern void init_new_thread_signals(void);

extern int os_map_memory(void *virt, int fd, unsigned long long off,
			 unsigned long len, int r, int w, int x);
extern int os_protect_memory(void *addr, unsigned long len,
			     int r, int w, int x);
extern int os_unmap_memory(void *addr, int len);
extern int os_drop_memory(void *addr, int length);
extern int can_drop_memory(void);

void os_set_pdeathsig(void);

/* execvp.c */
extern int execvp_noalloc(char *buf, const char *file, char *const argv[]);
/* helper.c */
extern int run_helper(void (*pre_exec)(void *), void *pre_data, char **argv);
extern int run_helper_thread(int (*proc)(void *), void *arg,
			     unsigned int flags, unsigned long *stack_out);
extern int helper_wait(int pid);

struct os_helper_thread;
int os_run_helper_thread(struct os_helper_thread **td_out,
			 void *(*routine)(void *), void *arg);
void os_kill_helper_thread(struct os_helper_thread *td);
void os_fix_helper_thread_signals(void);

/* umid.c */
extern int umid_file_name(char *name, char *buf, int len);
extern int set_umid(char *name);
extern char *get_umid(void);

/* signal.c */
extern void timer_set_signal_handler(void);
extern void set_sigstack(void *sig_stack, int size);
extern void set_handler(int sig);
extern void send_sigio_to_self(void);
extern int change_sig(int signal, int on);
extern void block_signals(void);
extern void unblock_signals(void);
extern int um_set_signals(int enable);
extern int um_set_signals_trace(int enable);
extern void deliver_alarm(void);
extern void register_pm_wake_signal(void);
extern void block_signals_hard(void);
extern void unblock_signals_hard(void);
extern void mark_sigio_pending(void);

/* util.c */
extern void stack_protections(unsigned long address);
extern int raw(int fd);
extern void setup_machinename(char *machine_out);
extern void setup_hostinfo(char *buf, int len);
extern ssize_t os_getrandom(void *buf, size_t len, unsigned int flags);
extern void os_dump_core(void) __attribute__ ((noreturn));
extern void um_early_printk(const char *s, unsigned int n);
extern void os_fix_helper_signals(void);
extern void os_info(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));
extern void os_warn(const char *fmt, ...)
	__attribute__ ((format (printf, 1, 2)));

/* time.c */
extern void os_idle_sleep(void);
extern int os_timer_create(void);
extern int os_timer_set_interval(unsigned long long nsecs);
extern int os_timer_one_shot(unsigned long long nsecs);
extern void os_timer_disable(void);
extern long long os_persistent_clock_emulation(void);
extern long long os_nsecs(void);

/* skas/mem.c */
int syscall_stub_flush(struct mm_id *mm_idp);
struct stub_syscall *syscall_stub_alloc(struct mm_id *mm_idp);
void syscall_stub_dump_error(struct mm_id *mm_idp);

int map(struct mm_id *mm_idp, unsigned long virt,
	unsigned long len, int prot, int phys_fd,
	unsigned long long offset);
int unmap(struct mm_id *mm_idp, unsigned long addr, unsigned long len);

/* skas/process.c */
extern int is_skas_winch(int pid, int fd, void *data);
extern int start_userspace(unsigned long stub_stack);
extern void userspace(struct uml_pt_regs *regs);
extern void new_thread(void *stack, jmp_buf *buf, void (*handler)(void));
extern void switch_threads(jmp_buf *me, jmp_buf *you);
extern int start_idle_thread(void *stack, jmp_buf *switch_buf);
extern void initial_thread_cb_skas(void (*proc)(void *),
				 void *arg);
extern void halt_skas(void);
extern void reboot_skas(void);

/* irq.c */
extern int os_waiting_for_events_epoll(void);
extern void *os_epoll_get_data_pointer(int index);
extern int os_epoll_triggered(int index, int events);
extern int os_event_mask(enum um_irq_type irq_type);
extern int os_setup_epoll(void);
extern int os_add_epoll_fd(int events, int fd, void *data);
extern int os_mod_epoll_fd(int events, int fd, void *data);
extern int os_del_epoll_fd(int fd);
extern void os_set_ioignore(void);
extern void os_close_epoll_fd(void);
extern void um_irqs_suspend(void);
extern void um_irqs_resume(void);

/* sigio.c */
extern int add_sigio_fd(int fd);
extern int ignore_sigio_fd(int fd);
extern void maybe_sigio_broken(int fd);
extern void sigio_broken(void);
/*
 * unlocked versions for IRQ controller code.
 *
 * This is safe because it's used at suspend/resume and nothing
 * else is running.
 */
extern int __add_sigio_fd(int fd);
extern int __ignore_sigio_fd(int fd);

/* tty.c */
extern int get_pty(void);

long syscall(long number, ...);

/* irqflags tracing */
extern void block_signals_trace(void);
extern void unblock_signals_trace(void);
extern void um_trace_signals_on(void);
extern void um_trace_signals_off(void);

/* time-travel */
extern void deliver_time_travel_irqs(void);

#endif
