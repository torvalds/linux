#ifndef _ASM_UAPI_LKL_HOST_OPS_H
#define _ASM_UAPI_LKL_HOST_OPS_H

/* Defined in {posix,nt}-host.c */
struct lkl_mutex;
struct lkl_sem;
struct lkl_tls_key;
typedef unsigned long lkl_thread_t;
struct lkl_jmp_buf {
	unsigned long buf[32];
};

/**
 * lkl_host_operations - host operations used by the Linux kernel
 *
 * These operations must be provided by a host library or by the application
 * itself.
 *
 * @virtio_devices - string containg the list of virtio devices in virtio mmio
 * command line format. This string is appended to the kernel command line and
 * is provided here for convenience to be implemented by the host library.
 *
 * @print - optional operation that receives console messages
 *
 * @panic - called during a kernel panic
 *
 * @sem_alloc - allocate a host semaphore an initialize it to count
 * @sem_free - free a host semaphore
 * @sem_up - perform an up operation on the semaphore
 * @sem_down - perform a down operation on the semaphore
 *
 * @mutex_alloc - allocate and initialize a host mutex; the recursive parameter
 * determines if the mutex is recursive or not
 * @mutex_free - free a host mutex
 * @mutex_lock - acquire the mutex
 * @mutex_unlock - release the mutex
 *
 * @thread_create - create a new thread and run f(arg) in its context; returns a
 * thread handle or 0 if the thread could not be created
 * @thread_detach - on POSIX systems, free up resources held by
 * pthreads. Noop on Win32.
 * @thread_exit - terminates the current thread
 * @thread_join - wait for the given thread to terminate. Returns 0
 * for success, -1 otherwise
 *
 * @tls_alloc - allocate a thread local storage key; returns 0 if successful; if
 * destructor is not NULL it will be called when a thread terminates with its
 * argument set to the current thread local storage value
 * @tls_free - frees a thread local storage key; returns 0 if succesful
 * @tls_set - associate data to the thread local storage key; returns 0 if
 * successful
 * @tls_get - return data associated with the thread local storage key or NULL
 * on error
 *
 * @mem_alloc - allocate memory
 * @mem_free - free memory
 *
 * @timer_create - allocate a host timer that runs fn(arg) when the timer
 * fires.
 * @timer_free - disarms and free the timer
 * @timer_set_oneshot - arm the timer to fire once, after delta ns.
 * @timer_set_periodic - arm the timer to fire periodically, with a period of
 * delta ns.
 *
 * @ioremap - searches for an I/O memory region identified by addr and size and
 * returns a pointer to the start of the address range that can be used by
 * iomem_access
 * @iomem_acess - reads or writes to and I/O memory region; addr must be in the
 * range returned by ioremap
 *
 * @gettid - returns the host thread id of the caller, which need not
 * be the same as the handle returned by thread_create
 *
 * @jmp_buf_set - runs the give function and setups a jump back point by saving
 * the context in the jump buffer; jmp_buf_longjmp can be called from the give
 * function or any callee in that function to return back to the jump back
 * point
 *
 * NOTE: we can't return from jmp_buf_set before calling jmp_buf_longjmp or
 * otherwise the saved context (stack) is not going to be valid, so we must pass
 * the function that will eventually call longjmp here
 *
 * @jmp_buf_longjmp - perform a jump back to the saved jump buffer
 */
struct lkl_host_operations {
	const char *virtio_devices;

	void (*print)(const char *str, int len);
	void (*panic)(void);

	struct lkl_sem* (*sem_alloc)(int count);
	void (*sem_free)(struct lkl_sem *sem);
	void (*sem_up)(struct lkl_sem *sem);
	void (*sem_down)(struct lkl_sem *sem);

	struct lkl_mutex *(*mutex_alloc)(int recursive);
	void (*mutex_free)(struct lkl_mutex *mutex);
	void (*mutex_lock)(struct lkl_mutex *mutex);
	void (*mutex_unlock)(struct lkl_mutex *mutex);

	lkl_thread_t (*thread_create)(void (*f)(void *), void *arg);
	void (*thread_detach)(void);
	void (*thread_exit)(void);
	int (*thread_join)(lkl_thread_t tid);
	lkl_thread_t (*thread_self)(void);
	int (*thread_equal)(lkl_thread_t a, lkl_thread_t b);

	struct lkl_tls_key *(*tls_alloc)(void (*destructor)(void *));
	void (*tls_free)(struct lkl_tls_key *key);
	int (*tls_set)(struct lkl_tls_key *key, void *data);
	void *(*tls_get)(struct lkl_tls_key *key);

	void* (*mem_alloc)(unsigned long);
	void (*mem_free)(void *);

	unsigned long long (*time)(void);

	void* (*timer_alloc)(void (*fn)(void *), void *arg);
	int (*timer_set_oneshot)(void *timer, unsigned long delta);
	void (*timer_free)(void *timer);

	void* (*ioremap)(long addr, int size);
	int (*iomem_access)(const volatile void *addr, void *val, int size,
			    int write);

	long (*gettid)(void);

	void (*jmp_buf_set)(struct lkl_jmp_buf *jmpb, void (*f)(void));
	void (*jmp_buf_longjmp)(struct lkl_jmp_buf *jmpb, int val);
};

/**
 * lkl_start_kernel - registers the host operations and starts the kernel
 *
 * The function returns only after the kernel is shutdown with lkl_sys_halt.
 *
 * @lkl_ops - pointer to host operations
 * @cmd_line - format for command line string that is going to be used to
 * generate the Linux kernel command line
 */
int lkl_start_kernel(struct lkl_host_operations *lkl_ops,
		    const char *cmd_line, ...);

/**
 * lkl_is_running - returns 1 if the kernel is currently running
 */
int lkl_is_running(void);

int lkl_printf(const char *, ...);
void lkl_bug(const char *, ...);

#endif
