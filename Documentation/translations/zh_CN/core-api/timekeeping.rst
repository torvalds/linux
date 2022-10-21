.. SPDX-License-Identifier: GPL-2.0+

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/core-api/timekeeping.rst

:翻译:

 周彬彬 Binbin Zhou <zhoubinbin@loongson.cn>

:校译:

 吴想成 Wu Xiangcheng <bobwxc@email.cn>

ktime访问器
===========

设备驱动程序可以使用 ``ktime_get()`` 和 ``linux/timekeeping.h`` 中声明的许多相关
函数读取当前时间。根据经验，如果两个访问器都适用于某一用例，则使用名称较短的那个。

基于ktime_t的基础接口
---------------------

推荐的最简单形式返回不透明的 ``ktime_t`` ，其变体返回不同时钟参考的时间：

.. c:function:: ktime_t ktime_get( void )

	CLOCK_MONOTONIC

	对可靠的时间戳和准确测量短的时间间隔很有用。在系统启动时启动，但在挂起时
	停止。

.. c:function:: ktime_t ktime_get_boottime( void )

	CLOCK_BOOTTIME

	与 ``ktime_get()`` 类似，但在挂起时不会停止。这可以用于例如需要在挂起操作
	中与其他计算机同步的密钥过期时间。

.. c:function:: ktime_t ktime_get_real( void )

	CLOCK_REALTIME

	使用协调世界时(UTC)返回相对于1970年开始的UNIX纪元的时间，与用户空间的
	``gettimeofday()`` 相同。该函数用于所有需要在重启时持续存在的时间戳，比如
	inode时间，但应避免在内部使用，因为它可能由于闰秒更新和来自用户空间的NTP
	调整 ``settimeofday()`` 操作而向后跳转。

.. c:function:: ktime_t ktime_get_clocktai( void )

	CLOCK_TAI

	与 ``ktime_get_real()`` 类似，但使用国际原子时(TAI)作为参考而不是UTC，以
	避免在闰秒更新时跳转。这在内核中很少有用。

.. c:function:: ktime_t ktime_get_raw( void )

	CLOCK_MONOTONIC_RAW

	与 ``ktime_get()`` 类似，但以与硬件时钟源相同的速率运行，没有针对时钟漂移
	进行调整(NTP)。这在内核中也很少需要。

纳秒，timespec64和秒钟的输出
----------------------------

对于上述所有情况，以下函数变体会根据用户的要求以不同的格式返回时间：

.. c:function:: u64 ktime_get_ns( void )
		u64 ktime_get_boottime_ns( void )
		u64 ktime_get_real_ns( void )
		u64 ktime_get_clocktai_ns( void )
		u64 ktime_get_raw_ns( void )

	与普通的ktime_get函数相同，但返回各自时钟参考中的u64纳秒数，这对某些调用
	者来说可能更方便。

.. c:function:: void ktime_get_ts64( struct timespec64 * )
		void ktime_get_boottime_ts64( struct timespec64 * )
		void ktime_get_real_ts64( struct timespec64 * )
		void ktime_get_clocktai_ts64( struct timespec64 * )
		void ktime_get_raw_ts64( struct timespec64 * )

	同上，但返回的是 ``struct timespec64`` 中的时间，分为秒和纳秒。这可以避免
	在打印时间时，或在将其传递到期望有 ``timespec`` 或 ``timeval`` 结构体的外
	部接口时，进行额外的划分。

.. c:function:: time64_t ktime_get_seconds( void )
		time64_t ktime_get_boottime_seconds( void )
		time64_t ktime_get_real_seconds( void )
		time64_t ktime_get_clocktai_seconds( void )
		time64_t ktime_get_raw_seconds( void )

	将时间的粗粒度版本作为标量 ``time64_t`` 返回。 这避免了访问时钟硬件，并使
	用相应的参考将秒数舍入到最后一个计时器滴答的完整秒数。

粗略的和fast_ns访问
-------------------

对于更专业的情况，存在一些额外的变体：

.. c:function:: ktime_t ktime_get_coarse( void )
		ktime_t ktime_get_coarse_boottime( void )
		ktime_t ktime_get_coarse_real( void )
		ktime_t ktime_get_coarse_clocktai( void )

.. c:function:: u64 ktime_get_coarse_ns( void )
		u64 ktime_get_coarse_boottime_ns( void )
		u64 ktime_get_coarse_real_ns( void )
		u64 ktime_get_coarse_clocktai_ns( void )

.. c:function:: void ktime_get_coarse_ts64( struct timespec64 * )
		void ktime_get_coarse_boottime_ts64( struct timespec64 * )
		void ktime_get_coarse_real_ts64( struct timespec64 * )
		void ktime_get_coarse_clocktai_ts64( struct timespec64 * )

	他们比非粗略版本更快，但精度较低，对应于用户空间中的 ``CLOCK_MONOTONIC_COARSE``
	和 ``CLOCK_REALTIME_COARSE`` ，以及用户空间不可用的等效boottime/tai/raw的
	时间基准。

	此处返回的时间对应于最后一个计时器滴答，过去可能高达10毫秒（对于CONFIG_HZ=100），
	与读取 ``jiffies`` 变量相同。 这些只有在快速路径中调用时才有用，并且人们
	仍然期望精度优于秒，但不能轻易使用 ``jiffies`` ，例如用于inode时间戳。在
	大多数具有可靠周期计数器的现代机器上，跳过硬件时钟访问可以节省大约100个
	CPU周期，但在具有外部时钟源的旧硬件上，最多可以节省几微秒。

.. c:function:: u64 ktime_get_mono_fast_ns( void )
		u64 ktime_get_raw_fast_ns( void )
		u64 ktime_get_boot_fast_ns( void )
		u64 ktime_get_tai_fast_ns( void )
		u64 ktime_get_real_fast_ns( void )

	这些变体可以安全地从任何上下文中调用，包括在计时器更新期间从不屏蔽中断（NMI）
	调用，以及当我们在时钟源断电的情况下进入挂起状态时。这在某些跟踪或调试代
	码以及机器检查报告中很有用，但大多数驱动程序不应调用它们，因为在某些情况
	下时间是允许跳跃的

已废弃的时钟接口
----------------

较早的内核使用了一些其他接口，这些接口现在正在逐步被淘汰，但可能会出现在被移植到
的第三方驱动中。特别是，所有返回 ``struct timeval`` 或 ``struct timespec`` 的接口
都已被替换，因为在32位架构上，tv_sec成员会在2038年溢出。下面是推荐的替换函数:

.. c:function:: void ktime_get_ts( struct timespec * )

	用 ``ktime_get()`` 或者 ``ktime_get_ts64()`` 替换。

.. c:function:: void do_gettimeofday( struct timeval * )
		void getnstimeofday( struct timespec * )
		void getnstimeofday64( struct timespec64 * )
		void ktime_get_real_ts( struct timespec * )

	``ktime_get_real_ts64()`` 可以直接替代，但可以考虑使用单调的时间
	（ ``ktime_get_ts64()`` ）和/或基于 ``ktime_t`` 的接口
	（ ``ktime_get()`` / ``ktime_get_real()`` ）。

.. c:function:: struct timespec current_kernel_time( void )
		struct timespec64 current_kernel_time64( void )
		struct timespec get_monotonic_coarse( void )
		struct timespec64 get_monotonic_coarse64( void )

	这些被 ``ktime_get_coarse_real_ts64()`` 和 ``ktime_get_coarse_ts64()`` 取
	代。然而，许多需要粗粒度时间的代码可以使用简单的 ``jiffies`` 来代替，而现
	在一些驱动程序实际上可能需要更高分辨率的访问器。

.. c:function:: struct timespec getrawmonotonic( void )
		struct timespec64 getrawmonotonic64( void )
		struct timespec timekeeping_clocktai( void )
		struct timespec64 timekeeping_clocktai64( void )
		struct timespec get_monotonic_boottime( void )
		struct timespec64 get_monotonic_boottime64( void )

	这些被 ``ktime_get_raw()`` / ``ktime_get_raw_ts64()`` ，
	``ktime_get_clocktai()`` / ``ktime_get_clocktai_ts64()``
	以及 ``ktime_get_boottime()`` / ``ktime_get_boottime_ts64()`` 取代。但是，
	如果时钟源的特定选择对用户来说并不重要，请考虑转换为
	``ktime_get()`` / ``ktime_get_ts64()`` 以保持一致性。
