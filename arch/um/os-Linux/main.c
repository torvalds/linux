/*
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/resource.h>
#include "as-layout.h"
#include "init.h"
#include "kern_constants.h"
#include "kern_util.h"
#include "os.h"
#include "um_malloc.h"

#define PGD_BOUND (4 * 1024 * 1024)
#define STACKSIZE (8 * 1024 * 1024)
#define THREAD_NAME_LEN (256)

static void set_stklim(void)
{
	struct rlimit lim;

	if (getrlimit(RLIMIT_STACK, &lim) < 0) {
		perror("getrlimit");
		exit(1);
	}
	if ((lim.rlim_cur == RLIM_INFINITY) || (lim.rlim_cur > STACKSIZE)) {
		lim.rlim_cur = STACKSIZE;
		if (setrlimit(RLIMIT_STACK, &lim) < 0) {
			perror("setrlimit");
			exit(1);
		}
	}
}

static __init void do_uml_initcalls(void)
{
	initcall_t *call;

	call = &__uml_initcall_start;
	while (call < &__uml_initcall_end) {
		(*call)();
		call++;
	}
}

static void last_ditch_exit(int sig)
{
	uml_cleanup();
	exit(1);
}

static void install_fatal_handler(int sig)
{
	struct sigaction action;

	/* All signals are enabled in this handler ... */
	sigemptyset(&action.sa_mask);

	/*
	 * ... including the signal being handled, plus we want the
	 * handler reset to the default behavior, so that if an exit
	 * handler is hanging for some reason, the UML will just die
	 * after this signal is sent a second time.
	 */
	action.sa_flags = SA_RESETHAND | SA_NODEFER;
	action.sa_restorer = NULL;
	action.sa_handler = last_ditch_exit;
	if (sigaction(sig, &action, NULL) < 0) {
		printf("failed to install handler for signal %d - errno = %d\n",
		       errno);
		exit(1);
	}
}

#define UML_LIB_PATH	":/usr/lib/uml"

static void setup_env_path(void)
{
	char *new_path = NULL;
	char *old_path = NULL;
	int path_len = 0;

	old_path = getenv("PATH");
	/*
	 * if no PATH variable is set or it has an empty value
	 * just use the default + /usr/lib/uml
	 */
	if (!old_path || (path_len = strlen(old_path)) == 0) {
		putenv("PATH=:/bin:/usr/bin/" UML_LIB_PATH);
		return;
	}

	/* append /usr/lib/uml to the existing path */
	path_len += strlen("PATH=" UML_LIB_PATH) + 1;
	new_path = malloc(path_len);
	if (!new_path) {
		perror("coudn't malloc to set a new PATH");
		return;
	}
	snprintf(new_path, path_len, "PATH=%s" UML_LIB_PATH, old_path);
	putenv(new_path);
}

extern int uml_exitcode;

extern void scan_elf_aux( char **envp);

int __init main(int argc, char **argv, char **envp)
{
	char **new_argv;
	int ret, i, err;

	set_stklim();

	setup_env_path();

	new_argv = malloc((argc + 1) * sizeof(char *));
	if (new_argv == NULL) {
		perror("Mallocing argv");
		exit(1);
	}
	for (i = 0; i < argc; i++) {
		new_argv[i] = strdup(argv[i]);
		if (new_argv[i] == NULL) {
			perror("Mallocing an arg");
			exit(1);
		}
	}
	new_argv[argc] = NULL;

	/*
	 * Allow these signals to bring down a UML if all other
	 * methods of control fail.
	 */
	install_fatal_handler(SIGINT);
	install_fatal_handler(SIGTERM);
	install_fatal_handler(SIGHUP);

	scan_elf_aux(envp);

	do_uml_initcalls();
	ret = linux_main(argc, argv);

	/*
	 * Disable SIGPROF - I have no idea why libc doesn't do this or turn
	 * off the profiling time, but UML dies with a SIGPROF just before
	 * exiting when profiling is active.
	 */
	change_sig(SIGPROF, 0);

	/*
	 * This signal stuff used to be in the reboot case.  However,
	 * sometimes a SIGVTALRM can come in when we're halting (reproducably
	 * when writing out gcov information, presumably because that takes
	 * some time) and cause a segfault.
	 */

	/* stop timers and set SIGVTALRM to be ignored */
	disable_timer();

	/* disable SIGIO for the fds and set SIGIO to be ignored */
	err = deactivate_all_fds();
	if (err)
		printf("deactivate_all_fds failed, errno = %d\n", -err);

	/*
	 * Let any pending signals fire now.  This ensures
	 * that they won't be delivered after the exec, when
	 * they are definitely not expected.
	 */
	unblock_signals();

	/* Reboot */
	if (ret) {
		printf("\n");
		execvp(new_argv[0], new_argv);
		perror("Failed to exec kernel");
		ret = 1;
	}
	printf("\n");
	return uml_exitcode;
}

extern void *__real_malloc(int);

void *__wrap_malloc(int size)
{
	void *ret;

	if (!kmalloc_ok)
		return __real_malloc(size);
	else if (size <= UM_KERN_PAGE_SIZE)
		/* finding contiguous pages can be hard*/
		ret = kmalloc(size, UM_GFP_KERNEL);
	else ret = vmalloc(size);

	/*
	 * glibc people insist that if malloc fails, errno should be
	 * set by malloc as well. So we do.
	 */
	if (ret == NULL)
		errno = ENOMEM;

	return ret;
}

void *__wrap_calloc(int n, int size)
{
	void *ptr = __wrap_malloc(n * size);

	if (ptr == NULL)
		return NULL;
	memset(ptr, 0, n * size);
	return ptr;
}

extern void __real_free(void *);

extern unsigned long high_physmem;

void __wrap_free(void *ptr)
{
	unsigned long addr = (unsigned long) ptr;

	/*
	 * We need to know how the allocation happened, so it can be correctly
	 * freed.  This is done by seeing what region of memory the pointer is
	 * in -
	 * 	physical memory - kmalloc/kfree
	 *	kernel virtual memory - vmalloc/vfree
	 * 	anywhere else - malloc/free
	 * If kmalloc is not yet possible, then either high_physmem and/or
	 * end_vm are still 0 (as at startup), in which case we call free, or
	 * we have set them, but anyway addr has not been allocated from those
	 * areas. So, in both cases __real_free is called.
	 *
	 * CAN_KMALLOC is checked because it would be bad to free a buffer
	 * with kmalloc/vmalloc after they have been turned off during
	 * shutdown.
	 * XXX: However, we sometimes shutdown CAN_KMALLOC temporarily, so
	 * there is a possibility for memory leaks.
	 */

	if ((addr >= uml_physmem) && (addr < high_physmem)) {
		if (kmalloc_ok)
			kfree(ptr);
	}
	else if ((addr >= start_vm) && (addr < end_vm)) {
		if (kmalloc_ok)
			vfree(ptr);
	}
	else __real_free(ptr);
}
