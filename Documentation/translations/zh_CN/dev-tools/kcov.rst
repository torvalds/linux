.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/dev-tools/kcov.rst
:Translator: 刘浩阳 Haoyang Liu <tttturtleruss@hust.edu.cn>

KCOV: 用于模糊测试的代码覆盖率
==============================

KCOV 以一种适用于覆盖率引导的模糊测试的形式收集和暴露内核代码覆盖率信息。
一个正在运行的内核的覆盖率数据可以通过 ``kcov`` 调试文件导出。覆盖率的收集是基
于任务启用的，因此 KCOV 可以精确捕获单个系统调用的覆盖率。

要注意的是 KCOV 不是为了收集尽可能多的覆盖率数据。而是为了收集相对稳定的覆盖率
，这是系统调用输入的函数。为了完成这个目标，它不收集软硬中断的覆盖率（除非移除
覆盖率收集被启用，见下文）以及内核中固有的不确定部分的覆盖率（如调度器，锁定）

除了收集代码覆盖率，KCOV 还收集操作数比较的覆盖率。见 "操作数比较收集" 一节
查看详细信息。

除了从系统调用处理器收集覆盖率数据，KCOV 还从后台内核或软中断任务中执行的内核
被标注的部分收集覆盖率。见 "远程覆盖率收集" 一节查看详细信息。

先决条件
--------

KCOV 依赖编译器插桩，要求 GCC 6.1.0 及更高版本或者内核支持的任意版本的 Clang。

收集操作数比较的覆盖率需要 GCC 8+ 或者 Clang。

为了启用 KCOV，需要使用如下参数配置内核::

        CONFIG_KCOV=y

为了启用操作数比较覆盖率的收集，使用如下参数::

    CONFIG_KCOV_ENABLE_COMPARISONS=y

覆盖率数据只会在调试文件系统被挂载后才可以获取::

        mount -t debugfs none /sys/kernel/debug

覆盖率收集
----------

下面的程序演示了如何使用 KCOV 在一个测试程序中收集单个系统调用的覆盖率：

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

	/* 单个文件描述符允许
	 * 在单线程上收集覆盖率。
	 */
	fd = open("/sys/kernel/debug/kcov", O_RDWR);
	if (fd == -1)
		perror("open"), exit(1);
	/* 设置跟踪模式和跟踪大小。 */
	if (ioctl(fd, KCOV_INIT_TRACE, COVER_SIZE))
		perror("ioctl"), exit(1);
	/* 映射内核空间和用户空间共享的缓冲区。 */
	cover = (unsigned long*)mmap(NULL, COVER_SIZE * sizeof(unsigned long),
				     PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if ((void*)cover == MAP_FAILED)
		perror("mmap"), exit(1);
	/* 在当前线程中启用覆盖率收集。 */
	if (ioctl(fd, KCOV_ENABLE, KCOV_TRACE_PC))
		perror("ioctl"), exit(1);
	/* 在调用 ioctl() 之后重置覆盖率。 */
	__atomic_store_n(&cover[0], 0, __ATOMIC_RELAXED);
	/* 调用目标系统调用。 */
	read(-1, NULL, 0);
	/* 读取收集到的 PC 的数目。 */
	n = __atomic_load_n(&cover[0], __ATOMIC_RELAXED);
	for (i = 0; i < n; i++)
		printf("0x%lx\n", cover[i + 1]);
	/* 在当前线程上禁用覆盖率收集。在这之后
	 * 可以在其他线程上收集覆盖率
	 */
	if (ioctl(fd, KCOV_DISABLE, 0))
		perror("ioctl"), exit(1);
	/* 释放资源 */
	if (munmap(cover, COVER_SIZE * sizeof(unsigned long)))
		perror("munmap"), exit(1);
	if (close(fd))
		perror("close"), exit(1);
	return 0;
    }

在使用 ``addr2line`` 传输后，程序输出应该如下所示::

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

如果一个程序需要从多个线程收集覆盖率（独立地）。那么每个线程都需要单独打开
``/sys/kernel/debug/kcov``。

接口的细粒度允许高效的创建测试进程。即，一个父进程打开了
``/sys/kernel/debug/kcov``，启用了追踪模式，映射了覆盖率缓冲区，然后在一个循
环中创建了子进程。这个子进程只需要启用覆盖率收集即可（当一个线程退出时将自动禁
用覆盖率收集）。

操作数比较收集
--------------

操作数比较收集和覆盖率收集类似：

.. code-block:: c

    /* 包含和上文一样的头文件和宏定义。 */

    /* 每次记录的 64 位字的数量。 */
    #define KCOV_WORDS_PER_CMP 4

    /*
     * 收集的比较种类的格式。
     *
     * 0 比特表示是否是一个编译时常量。
     * 1 & 2 比特包含参数大小的 log2 值，最大 8 字节。
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
	* 注意缓冲区指针的类型是 uint64_t*，因为所有的
	* 比较操作数都被提升为 uint64_t 类型。
	*/
	cover = (uint64_t *)mmap(NULL, COVER_SIZE * sizeof(unsigned long),
				     PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if ((void*)cover == MAP_FAILED)
		perror("mmap"), exit(1);
	/* 注意这里是 KCOV_TRACE_CMP 而不是 KCOV_TRACE_PC。 */
	if (ioctl(fd, KCOV_ENABLE, KCOV_TRACE_CMP))
		perror("ioctl"), exit(1);
	__atomic_store_n(&cover[0], 0, __ATOMIC_RELAXED);
	read(-1, NULL, 0);
	/* 读取收集到的比较操作数的数量。 */
	n = __atomic_load_n(&cover[0], __ATOMIC_RELAXED);
	for (i = 0; i < n; i++) {
		uint64_t ip;

		type = cover[i * KCOV_WORDS_PER_CMP + 1];
		/* arg1 和 arg2 - 比较的两个操作数。 */
		arg1 = cover[i * KCOV_WORDS_PER_CMP + 2];
		arg2 = cover[i * KCOV_WORDS_PER_CMP + 3];
		/* ip - 调用者的地址。 */
		ip = cover[i * KCOV_WORDS_PER_CMP + 4];
		/* 操作数的大小。 */
		size = 1 << ((type & KCOV_CMP_MASK) >> 1);
		/* is_const - 当操作数是一个编译时常量时为真。*/
		is_const = type & KCOV_CMP_CONST;
		printf("ip: 0x%lx type: 0x%lx, arg1: 0x%lx, arg2: 0x%lx, "
			"size: %lu, %s\n",
			ip, type, arg1, arg2, size,
		is_const ? "const" : "non-const");
	}
	if (ioctl(fd, KCOV_DISABLE, 0))
		perror("ioctl"), exit(1);
	/* 释放资源。 */
	if (munmap(cover, COVER_SIZE * sizeof(unsigned long)))
		perror("munmap"), exit(1);
	if (close(fd))
		perror("close"), exit(1);
	return 0;
    }

注意 KCOV 的模式（代码覆盖率收集或操作数比较收集）是互斥的。

远程覆盖率收集
--------------

除了从用户空间进程发布的系统调用句柄收集覆盖率数据以外，KCOV 也可以从部分在其
他上下文中执行的内核中收集覆盖率 - 称为“远程”覆盖率。

使用 KCOV 收集远程覆盖率要求：

1. 修改内核源码并使用 ``kcov_remote_start`` 和 ``kcov_remote_stop`` 来标注要收集
   覆盖率的代码片段。

2. 在用户空间的收集覆盖率的进程应使用 ``KCOV_REMOTE_ENABLE`` 而不是 ``KCOV_ENABLE``。

``kcov_remote_start`` 和 ``kcov_remote_stop`` 的标注以及 ``KCOV_REMOTE_ENABLE``
ioctl 都接受可以识别特定覆盖率收集片段的句柄。句柄的使用方式取决于匹配代码片段执
行的上下文。

KCOV 支持在如下上下文中收集远程覆盖率：

1. 全局内核后台任务。这些任务是内核启动时创建的数量有限的实例（如，每一个
   USB HCD 产生一个 USB ``hub_event`` 工作器）。

2. 局部内核后台任务。这些任务通常是由于用户空间进程与某些内核接口进行交互时产
   生的，并且通常在进程退出时会被停止（如，vhost 工作器）。

3. 软中断。

对于 #1 和 #3，必须选择一个独特的全局句柄并将其传递给对应的
``kcov_remote_start`` 调用。一个用户空间进程必须将该句柄存储在
``kcov_remote_arg`` 结构体的 ``handle`` 数组字段中并将其传递给
``KCOV_REMOTE_ENABLE``。这会将使用的 KCOV 设备附加到由此句柄引用的代码片段。多个全局
句柄标识的不同代码片段可以一次性传递。

对于 #2，用户空间进程必须通过 ``kcov_remote_arg`` 结构体的 ``common_handle`` 字段
传递一个非零句柄。这个通用句柄将会被保存在当前 ``task_struct`` 结构体的
``kcov_handle`` 字段中并且需要通过自定义内核代码的修改来传递给新创建的本地任务
。这些任务需要在 ``kcov_remote_start`` 和 ``kcov_remote_stop`` 标注中依次使用传递过来的
句柄。

KCOV 对全局句柄和通用句柄均遵循一个预定义的格式。每一个句柄都是一个 ``u64`` 整形
。当前，只有最高位和低四位字节被使用。第 4-7 字节是保留位并且值必须为 0。

对于全局句柄，最高位的字节表示该句柄属于的子系统的标识。比如，KCOV 使用 ``1``
表示 USB 子系统类型。全局句柄的低 4 字节表示子系统中任务实例的标识。比如，每一
个 ``hub_event`` 工作器使用 USB 总线号作为任务实例的标识。

对于通用句柄，使用一个保留值 ``0`` 作为子系统标识，因为这些句柄不属于一个特定
的子系统。通用句柄的低 4 字节用于识别有用户进程生成的所有本地句柄的集合实例，
该进程将通用句柄传递给 ``KCOV_REMOTE_ENABLE``。

实际上，如果只从系统中的单个用户空间进程收集覆盖率，那么可以使用任意值作为通用
句柄的实例标识。然而，如果通用句柄被多个用户空间进程使用，每个进程必须使用唯一
的实例标识。一个选择是使用进程标识作为通用句柄实例的标识。

下面的程序演示了如何使用 KCOV 从一个由进程产生的本地任务和处理 USB 总线的全局
任务 #1 收集覆盖率：

.. code-block:: c

    /* 包含和上文一样的头文件和宏定义。 */

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

	/* 通过通用句柄和 USB 总线 #1 启用代码覆盖率收集。 */
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
	 * 在这里用户需要触发执行一个内核代码段
	 * 该代码段要么使用通用句柄标识
	 * 要么触发了一些 USB 总线 #1 上的一些活动。
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
