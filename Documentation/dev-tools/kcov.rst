KCOV: code coverage for fuzzing
===============================

KCOV collects and exposes kernel code coverage information in a form suitable
for coverage-guided fuzzing. Coverage data of a running kernel is exported via
the ``kcov`` debugfs file. Coverage collection is enabled on a task basis, and
thus KCOV can capture precise coverage of a single system call.

Note that KCOV does not aim to collect as much coverage as possible. It aims
to collect more or less stable coverage that is a function of syscall inputs.
To achieve this goal, it does not collect coverage in soft/hard interrupts
(unless remove coverage collection is enabled, see below) and from some
inherently non-deterministic parts of the kernel (e.g. scheduler, locking).

Besides collecting code coverage, KCOV can also collect comparison operands.
See the "Comparison operands collection" section for details.

Besides collecting coverage data from syscall handlers, KCOV can also collect
coverage for annotated parts of the kernel executing in background kernel
tasks or soft interrupts. See the "Remote coverage collection" section for
details.

Prerequisites
-------------

KCOV relies on compiler instrumentation and requires GCC 6.1.0 or later
or any Clang version supported by the kernel.

Collecting comparison operands is supported with GCC 8+ or with Clang.

To enable KCOV, configure the kernel with::

        CONFIG_KCOV=y

To enable comparison operands collection, set::

	CONFIG_KCOV_ENABLE_COMPARISONS=y

Coverage data only becomes accessible once debugfs has been mounted::

        mount -t debugfs none /sys/kernel/debug

Coverage collection
-------------------

The following program demonstrates how to use KCOV to collect coverage for a
single syscall from within a test program:

.. code-block:: c

    #include <stdio.h>
    #include <stddef.h>
    #include <stdint.h>
    #include <stdlib.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <sys/ioctl.h>
    #include <sys/mman.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <linux/types.h>

    #define KCOV_INIT_TRACE			_IOR('c', 1, unsigned long)
    #define KCOV_ENABLE			_IO('c', 100)
    #define KCOV_DISABLE			_IO('c', 101)
    #define COVER_SIZE			(64<<10)

    #define KCOV_TRACE_PC  0
    #define KCOV_TRACE_CMP 1

    int main(int argc, char **argv)
    {
	int fd;
	unsigned long *cover, n, i;

	/* A single fd descriptor allows coverage collection on a single
	 * thread.
	 */
	fd = open("/sys/kernel/debug/kcov", O_RDWR);
	if (fd == -1)
		perror("open"), exit(1);
	/* Setup trace mode and trace size. */
	if (ioctl(fd, KCOV_INIT_TRACE, COVER_SIZE))
		perror("ioctl"), exit(1);
	/* Mmap buffer shared between kernel- and user-space. */
	cover = (unsigned long*)mmap(NULL, COVER_SIZE * sizeof(unsigned long),
				     PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if ((void*)cover == MAP_FAILED)
		perror("mmap"), exit(1);
	/* Enable coverage collection on the current thread. */
	if (ioctl(fd, KCOV_ENABLE, KCOV_TRACE_PC))
		perror("ioctl"), exit(1);
	/* Reset coverage from the tail of the ioctl() call. */
	__atomic_store_n(&cover[0], 0, __ATOMIC_RELAXED);
	/* Call the target syscall call. */
	read(-1, NULL, 0);
	/* Read number of PCs collected. */
	n = __atomic_load_n(&cover[0], __ATOMIC_RELAXED);
	for (i = 0; i < n; i++)
		printf("0x%lx\n", cover[i + 1]);
	/* Disable coverage collection for the current thread. After this call
	 * coverage can be enabled for a different thread.
	 */
	if (ioctl(fd, KCOV_DISABLE, 0))
		perror("ioctl"), exit(1);
	/* Free resources. */
	if (munmap(cover, COVER_SIZE * sizeof(unsigned long)))
		perror("munmap"), exit(1);
	if (close(fd))
		perror("close"), exit(1);
	return 0;
    }

After piping through ``addr2line`` the output of the program looks as follows::

    SyS_read
    fs/read_write.c:562
    __fdget_pos
    fs/file.c:774
    __fget_light
    fs/file.c:746
    __fget_light
    fs/file.c:750
    __fget_light
    fs/file.c:760
    __fdget_pos
    fs/file.c:784
    SyS_read
    fs/read_write.c:562

If a program needs to collect coverage from several threads (independently),
it needs to open ``/sys/kernel/debug/kcov`` in each thread separately.

The interface is fine-grained to allow efficient forking of test processes.
That is, a parent process opens ``/sys/kernel/debug/kcov``, enables trace mode,
mmaps coverage buffer, and then forks child processes in a loop. The child
processes only need to enable coverage (it gets disabled automatically when
a thread exits).

Comparison operands collection
------------------------------

Comparison operands collection is similar to coverage collection:

.. code-block:: c

    /* Same includes and defines as above. */

    /* Number of 64-bit words per record. */
    #define KCOV_WORDS_PER_CMP 4

    /*
     * The format for the types of collected comparisons.
     *
     * Bit 0 shows whether one of the arguments is a compile-time constant.
     * Bits 1 & 2 contain log2 of the argument size, up to 8 bytes.
     */

    #define KCOV_CMP_CONST          (1 << 0)
    #define KCOV_CMP_SIZE(n)        ((n) << 1)
    #define KCOV_CMP_MASK           KCOV_CMP_SIZE(3)

    int main(int argc, char **argv)
    {
	int fd;
	uint64_t *cover, type, arg1, arg2, is_const, size;
	unsigned long n, i;

	fd = open("/sys/kernel/debug/kcov", O_RDWR);
	if (fd == -1)
		perror("open"), exit(1);
	if (ioctl(fd, KCOV_INIT_TRACE, COVER_SIZE))
		perror("ioctl"), exit(1);
	/*
	* Note that the buffer pointer is of type uint64_t*, because all
	* the comparison operands are promoted to uint64_t.
	*/
	cover = (uint64_t *)mmap(NULL, COVER_SIZE * sizeof(unsigned long),
				     PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if ((void*)cover == MAP_FAILED)
		perror("mmap"), exit(1);
	/* Note KCOV_TRACE_CMP instead of KCOV_TRACE_PC. */
	if (ioctl(fd, KCOV_ENABLE, KCOV_TRACE_CMP))
		perror("ioctl"), exit(1);
	__atomic_store_n(&cover[0], 0, __ATOMIC_RELAXED);
	read(-1, NULL, 0);
	/* Read number of comparisons collected. */
	n = __atomic_load_n(&cover[0], __ATOMIC_RELAXED);
	for (i = 0; i < n; i++) {
		uint64_t ip;

		type = cover[i * KCOV_WORDS_PER_CMP + 1];
		/* arg1 and arg2 - operands of the comparison. */
		arg1 = cover[i * KCOV_WORDS_PER_CMP + 2];
		arg2 = cover[i * KCOV_WORDS_PER_CMP + 3];
		/* ip - caller address. */
		ip = cover[i * KCOV_WORDS_PER_CMP + 4];
		/* size of the operands. */
		size = 1 << ((type & KCOV_CMP_MASK) >> 1);
		/* is_const - true if either operand is a compile-time constant.*/
		is_const = type & KCOV_CMP_CONST;
		printf("ip: 0x%lx type: 0x%lx, arg1: 0x%lx, arg2: 0x%lx, "
			"size: %lu, %s\n",
			ip, type, arg1, arg2, size,
		is_const ? "const" : "non-const");
	}
	if (ioctl(fd, KCOV_DISABLE, 0))
		perror("ioctl"), exit(1);
	/* Free resources. */
	if (munmap(cover, COVER_SIZE * sizeof(unsigned long)))
		perror("munmap"), exit(1);
	if (close(fd))
		perror("close"), exit(1);
	return 0;
    }

Note that the KCOV modes (collection of code coverage or comparison operands)
are mutually exclusive.

Remote coverage collection
--------------------------

Besides collecting coverage data from handlers of syscalls issued from a
userspace process, KCOV can also collect coverage for parts of the kernel
executing in other contexts - so-called "remote" coverage.

Using KCOV to collect remote coverage requires:

1. Modifying kernel code to annotate the code section from where coverage
   should be collected with ``kcov_remote_start`` and ``kcov_remote_stop``.

2. Using ``KCOV_REMOTE_ENABLE`` instead of ``KCOV_ENABLE`` in the userspace
   process that collects coverage.

Both ``kcov_remote_start`` and ``kcov_remote_stop`` annotations and the
``KCOV_REMOTE_ENABLE`` ioctl accept handles that identify particular coverage
collection sections. The way a handle is used depends on the context where the
matching code section executes.

KCOV supports collecting remote coverage from the following contexts:

1. Global kernel background tasks. These are the tasks that are spawned during
   kernel boot in a limited number of instances (e.g. one USB ``hub_event``
   worker is spawned per one USB HCD).

2. Local kernel background tasks. These are spawned when a userspace process
   interacts with some kernel interface and are usually killed when the process
   exits (e.g. vhost workers).

3. Soft interrupts.

For #1 and #3, a unique global handle must be chosen and passed to the
corresponding ``kcov_remote_start`` call. Then a userspace process must pass
this handle to ``KCOV_REMOTE_ENABLE`` in the ``handles`` array field of the
``kcov_remote_arg`` struct. This will attach the used KCOV device to the code
section referenced by this handle. Multiple global handles identifying
different code sections can be passed at once.

For #2, the userspace process instead must pass a non-zero handle through the
``common_handle`` field of the ``kcov_remote_arg`` struct. This common handle
gets saved to the ``kcov_handle`` field in the current ``task_struct`` and
needs to be passed to the newly spawned local tasks via custom kernel code
modifications. Those tasks should in turn use the passed handle in their
``kcov_remote_start`` and ``kcov_remote_stop`` annotations.

KCOV follows a predefined format for both global and common handles. Each
handle is a ``u64`` integer. Currently, only the one top and the lower 4 bytes
are used. Bytes 4-7 are reserved and must be zero.

For global handles, the top byte of the handle denotes the id of a subsystem
this handle belongs to. For example, KCOV uses ``1`` as the USB subsystem id.
The lower 4 bytes of a global handle denote the id of a task instance within
that subsystem. For example, each ``hub_event`` worker uses the USB bus number
as the task instance id.

For common handles, a reserved value ``0`` is used as a subsystem id, as such
handles don't belong to a particular subsystem. The lower 4 bytes of a common
handle identify a collective instance of all local tasks spawned by the
userspace process that passed a common handle to ``KCOV_REMOTE_ENABLE``.

In practice, any value can be used for common handle instance id if coverage
is only collected from a single userspace process on the system. However, if
common handles are used by multiple processes, unique instance ids must be
used for each process. One option is to use the process id as the common
handle instance id.

The following program demonstrates using KCOV to collect coverage from both
local tasks spawned by the process and the global task that handles USB bus #1:

.. code-block:: c

    /* Same includes and defines as above. */

    struct kcov_remote_arg {
	__u32		trace_mode;
	__u32		area_size;
	__u32		num_handles;
	__aligned_u64	common_handle;
	__aligned_u64	handles[0];
    };

    #define KCOV_INIT_TRACE			_IOR('c', 1, unsigned long)
    #define KCOV_DISABLE			_IO('c', 101)
    #define KCOV_REMOTE_ENABLE		_IOW('c', 102, struct kcov_remote_arg)

    #define COVER_SIZE	(64 << 10)

    #define KCOV_TRACE_PC	0

    #define KCOV_SUBSYSTEM_COMMON	(0x00ull << 56)
    #define KCOV_SUBSYSTEM_USB	(0x01ull << 56)

    #define KCOV_SUBSYSTEM_MASK	(0xffull << 56)
    #define KCOV_INSTANCE_MASK	(0xffffffffull)

    static inline __u64 kcov_remote_handle(__u64 subsys, __u64 inst)
    {
	if (subsys & ~KCOV_SUBSYSTEM_MASK || inst & ~KCOV_INSTANCE_MASK)
		return 0;
	return subsys | inst;
    }

    #define KCOV_COMMON_ID	0x42
    #define KCOV_USB_BUS_NUM	1

    int main(int argc, char **argv)
    {
	int fd;
	unsigned long *cover, n, i;
	struct kcov_remote_arg *arg;

	fd = open("/sys/kernel/debug/kcov", O_RDWR);
	if (fd == -1)
		perror("open"), exit(1);
	if (ioctl(fd, KCOV_INIT_TRACE, COVER_SIZE))
		perror("ioctl"), exit(1);
	cover = (unsigned long*)mmap(NULL, COVER_SIZE * sizeof(unsigned long),
				     PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if ((void*)cover == MAP_FAILED)
		perror("mmap"), exit(1);

	/* Enable coverage collection via common handle and from USB bus #1. */
	arg = calloc(1, sizeof(*arg) + sizeof(uint64_t));
	if (!arg)
		perror("calloc"), exit(1);
	arg->trace_mode = KCOV_TRACE_PC;
	arg->area_size = COVER_SIZE;
	arg->num_handles = 1;
	arg->common_handle = kcov_remote_handle(KCOV_SUBSYSTEM_COMMON,
							KCOV_COMMON_ID);
	arg->handles[0] = kcov_remote_handle(KCOV_SUBSYSTEM_USB,
						KCOV_USB_BUS_NUM);
	if (ioctl(fd, KCOV_REMOTE_ENABLE, arg))
		perror("ioctl"), free(arg), exit(1);
	free(arg);

	/*
	 * Here the user needs to trigger execution of a kernel code section
	 * that is either annotated with the common handle, or to trigger some
	 * activity on USB bus #1.
	 */
	sleep(2);

	n = __atomic_load_n(&cover[0], __ATOMIC_RELAXED);
	for (i = 0; i < n; i++)
		printf("0x%lx\n", cover[i + 1]);
	if (ioctl(fd, KCOV_DISABLE, 0))
		perror("ioctl"), exit(1);
	if (munmap(cover, COVER_SIZE * sizeof(unsigned long)))
		perror("munmap"), exit(1);
	if (close(fd))
		perror("close"), exit(1);
	return 0;
    }
