.. SPDX-License-Identifier: GPL-2.0

.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/dev-tools/kasan.rst
:Translator: 万家兵 Wan Jiabing <wanjiabing@vivo.com>

内核地址消毒剂(KASAN)
=====================

概述
----

Kernel Address SANitizer(KASAN)是一种动态内存安全错误检测工具，主要功能是
检查内存越界访问和使用已释放内存的问题。

KASAN有三种模式:

1. 通用KASAN
2. 基于软件标签的KASAN
3. 基于硬件标签的KASAN

用CONFIG_KASAN_GENERIC启用的通用KASAN，是用于调试的模式，类似于用户空
间的ASan。这种模式在许多CPU架构上都被支持，但它有明显的性能和内存开销。

基于软件标签的KASAN或SW_TAGS KASAN，通过CONFIG_KASAN_SW_TAGS启用，
可以用于调试和自我测试，类似于用户空间HWASan。这种模式只支持arm64，但其
适度的内存开销允许在内存受限的设备上用真实的工作负载进行测试。

基于硬件标签的KASAN或HW_TAGS KASAN，用CONFIG_KASAN_HW_TAGS启用，被
用作现场内存错误检测器或作为安全缓解的模式。这种模式只在支持MTE（内存标签
扩展）的arm64 CPU上工作，但它的内存和性能开销很低，因此可以在生产中使用。

关于每种KASAN模式的内存和性能影响的细节，请参见相应的Kconfig选项的描述。

通用模式和基于软件标签的模式通常被称为软件模式。基于软件标签的模式和基于
硬件标签的模式被称为基于标签的模式。

支持
----

体系架构
~~~~~~~~

在x86_64、arm、arm64、powerpc、riscv、s390、xtensa和loongarch上支持通用KASAN，
而基于标签的KASAN模式只在arm64上支持。

编译器
~~~~~~

软件KASAN模式使用编译时工具在每个内存访问之前插入有效性检查，因此需要一个
提供支持的编译器版本。基于硬件标签的模式依靠硬件来执行这些检查，但仍然需要
一个支持内存标签指令的编译器版本。

通用KASAN需要GCC 8.3.0版本或更高版本，或者内核支持的任何Clang版本。

基于软件标签的KASAN需要GCC 11+或者内核支持的任何Clang版本。

基于硬件标签的KASAN需要GCC 10+或Clang 12+。

内存类型
~~~~~~~~

通用KASAN支持在所有的slab、page_alloc、vmap、vmalloc、堆栈和全局内存
中查找错误。

基于软件标签的KASAN支持slab、page_alloc、vmalloc和堆栈内存。

基于硬件标签的KASAN支持slab、page_alloc和不可执行的vmalloc内存。

对于slab，两种软件KASAN模式都支持SLUB和SLAB分配器，而基于硬件标签的
KASAN只支持SLUB。

用法
----

要启用KASAN，请使用以下命令配置内核::

	  CONFIG_KASAN=y

同时在 ``CONFIG_KASAN_GENERIC`` (启用通用KASAN模式)， ``CONFIG_KASAN_SW_TAGS``
(启用基于硬件标签的KASAN模式)，和 ``CONFIG_KASAN_HW_TAGS`` (启用基于硬件标签
的KASAN模式)之间进行选择。

对于软件模式，还可以在 ``CONFIG_KASAN_OUTLINE`` 和 ``CONFIG_KASAN_INLINE``
之间进行选择。outline和inline是编译器插桩类型。前者产生较小的二进制文件，
而后者快2倍。

要将受影响的slab对象的alloc和free堆栈跟踪包含到报告中，请启用
``CONFIG_STACKTRACE`` 。要包括受影响物理页面的分配和释放堆栈跟踪的话，
请启用 ``CONFIG_PAGE_OWNER`` 并使用 ``page_owner=on`` 进行引导。

启动参数
~~~~~~~~

KASAN受到通用 ``panic_on_warn`` 命令行参数的影响。当它被启用时，KASAN
在打印出错误报告后会使内核恐慌。

默认情况下，KASAN只对第一个无效的内存访问打印错误报告。使用
``kasan_multi_shot``，KASAN对每一个无效的访问都打印一份报告。这会禁用
了KASAN报告的 ``panic_on_warn``。

另外，独立于 ``panic_on_warn`` 、 ``kasan.fault=`` boot参数可以用
来控制恐慌和报告行为。

- ``kasan.fault=report`` 或 ``=panic`` 控制是否只打印KASAN report或
  同时使内核恐慌（默认： ``report`` ）。即使 ``kasan_multi_shot`` 被
  启用，恐慌也会发生。

基于软件和硬件标签的KASAN模式（见下面关于各种模式的部分）支持改变堆栈跟
踪收集行为：

- ``kasan.stacktrace=off`` 或 ``=on`` 禁用或启用分配和释放堆栈痕
  迹的收集（默认： ``on`` ）。

- ``kasan.stack_ring_size=<number of entries>`` 指定堆栈环的条
  目数（默认： ``32768`` ）。

基于硬件标签的KASAN模式是为了在生产中作为一种安全缓解措施使用。因此，它
支持额外的启动参数，允许完全禁用KASAN或控制其功能。

- ``kasan=off`` 或 ``=on`` 控制KASAN是否被启用（默认： ``on`` ）。

- ``kasan.mode=sync``, ``=async`` or ``=asymm`` 控制KASAN是否
  被配置为同步、异步或非对称的执行模式（默认： ``同步`` ）。
  同步模式：当标签检查异常发生时，会立即检测到不良访问。
  异步模式：不良访问的检测是延迟的。当标签检查异常发生时，信息被存储在硬
  件中（对于arm64来说是在TFSR_EL1寄存器中）。内核周期性地检查硬件，并\
  且只在这些检查中报告标签异常。
  非对称模式：读取时同步检测不良访问，写入时异步检测。

- ``kasan.vmalloc=off`` or ``=on`` 禁用或启用vmalloc分配的标记（默认： ``on`` ）。

错误报告
~~~~~~~~

典型的KASAN报告如下所示::

    ==================================================================
    BUG: KASAN: slab-out-of-bounds in kmalloc_oob_right+0xa8/0xbc [kasan_test]
    Write of size 1 at addr ffff8801f44ec37b by task insmod/2760

    CPU: 1 PID: 2760 Comm: insmod Not tainted 4.19.0-rc3+ #698
    Hardware name: QEMU Standard PC (i440FX + PIIX, 1996), BIOS 1.10.2-1 04/01/2014
    Call Trace:
     dump_stack+0x94/0xd8
     print_address_description+0x73/0x280
     kasan_report+0x144/0x187
     __asan_report_store1_noabort+0x17/0x20
     kmalloc_oob_right+0xa8/0xbc [kasan_test]
     kmalloc_tests_init+0x16/0x700 [kasan_test]
     do_one_initcall+0xa5/0x3ae
     do_init_module+0x1b6/0x547
     load_module+0x75df/0x8070
     __do_sys_init_module+0x1c6/0x200
     __x64_sys_init_module+0x6e/0xb0
     do_syscall_64+0x9f/0x2c0
     entry_SYSCALL_64_after_hwframe+0x44/0xa9
    RIP: 0033:0x7f96443109da
    RSP: 002b:00007ffcf0b51b08 EFLAGS: 00000202 ORIG_RAX: 00000000000000af
    RAX: ffffffffffffffda RBX: 000055dc3ee521a0 RCX: 00007f96443109da
    RDX: 00007f96445cff88 RSI: 0000000000057a50 RDI: 00007f9644992000
    RBP: 000055dc3ee510b0 R08: 0000000000000003 R09: 0000000000000000
    R10: 00007f964430cd0a R11: 0000000000000202 R12: 00007f96445cff88
    R13: 000055dc3ee51090 R14: 0000000000000000 R15: 0000000000000000

    Allocated by task 2760:
     save_stack+0x43/0xd0
     kasan_kmalloc+0xa7/0xd0
     kmem_cache_alloc_trace+0xe1/0x1b0
     kmalloc_oob_right+0x56/0xbc [kasan_test]
     kmalloc_tests_init+0x16/0x700 [kasan_test]
     do_one_initcall+0xa5/0x3ae
     do_init_module+0x1b6/0x547
     load_module+0x75df/0x8070
     __do_sys_init_module+0x1c6/0x200
     __x64_sys_init_module+0x6e/0xb0
     do_syscall_64+0x9f/0x2c0
     entry_SYSCALL_64_after_hwframe+0x44/0xa9

    Freed by task 815:
     save_stack+0x43/0xd0
     __kasan_slab_free+0x135/0x190
     kasan_slab_free+0xe/0x10
     kfree+0x93/0x1a0
     umh_complete+0x6a/0xa0
     call_usermodehelper_exec_async+0x4c3/0x640
     ret_from_fork+0x35/0x40

    The buggy address belongs to the object at ffff8801f44ec300
     which belongs to the cache kmalloc-128 of size 128
    The buggy address is located 123 bytes inside of
     128-byte region [ffff8801f44ec300, ffff8801f44ec380)
    The buggy address belongs to the page:
    page:ffffea0007d13b00 count:1 mapcount:0 mapping:ffff8801f7001640 index:0x0
    flags: 0x200000000000100(slab)
    raw: 0200000000000100 ffffea0007d11dc0 0000001a0000001a ffff8801f7001640
    raw: 0000000000000000 0000000080150015 00000001ffffffff 0000000000000000
    page dumped because: kasan: bad access detected

    Memory state around the buggy address:
     ffff8801f44ec200: fc fc fc fc fc fc fc fc fb fb fb fb fb fb fb fb
     ffff8801f44ec280: fb fb fb fb fb fb fb fb fc fc fc fc fc fc fc fc
    >ffff8801f44ec300: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 03
                                                                    ^
     ffff8801f44ec380: fc fc fc fc fc fc fc fc fb fb fb fb fb fb fb fb
     ffff8801f44ec400: fb fb fb fb fb fb fb fb fc fc fc fc fc fc fc fc
    ==================================================================

报告标题总结了发生的错误类型以及导致该错误的访问类型。紧随其后的是错误访问的
堆栈跟踪、所访问内存分配位置的堆栈跟踪（对于访问了slab对象的情况）以及对象
被释放的位置的堆栈跟踪（对于访问已释放内存的问题报告）。接下来是对访问的
slab对象的描述以及关于访问的内存页的信息。

最后，报告展示了访问地址周围的内存状态。在内部，KASAN单独跟踪每个内存颗粒的
内存状态，根据KASAN模式分为8或16个对齐字节。报告的内存状态部分中的每个数字
都显示了围绕访问地址的其中一个内存颗粒的状态。

对于通用KASAN，每个内存颗粒的大小为8个字节。每个颗粒的状态被编码在一个影子字节
中。这8个字节可以是可访问的，部分访问的，已释放的或成为Redzone的一部分。KASAN
对每个影子字节使用以下编码:00表示对应内存区域的所有8个字节都可以访问；数字N
(1 <= N <= 7)表示前N个字节可访问，其他(8 - N)个字节不可访问；任何负值都表示
无法访问整个8字节。KASAN使用不同的负值来区分不同类型的不可访问内存，如redzones
或已释放的内存（参见 mm/kasan/kasan.h）。

在上面的报告中，箭头指向影子字节 ``03`` ，表示访问的地址是部分可访问的。

对于基于标签的KASAN模式，报告最后的部分显示了访问地址周围的内存标签
(参考 `实施细则`_ 章节)。

请注意，KASAN错误标题（如 ``slab-out-of-bounds`` 或 ``use-after-free`` ）
是尽量接近的:KASAN根据其拥有的有限信息打印出最可能的错误类型。错误的实际类型
可能会有所不同。

通用KASAN还报告两个辅助调用堆栈跟踪。这些堆栈跟踪指向代码中与对象交互但不直接
出现在错误访问堆栈跟踪中的位置。目前，这包括 call_rcu() 和排队的工作队列。

CONFIG_KASAN_EXTRA_INFO
~~~~~~~~~~~~~~~~~~~~~~~

启用 CONFIG_KASAN_EXTRA_INFO 选项允许 KASAN 记录和报告更多信息。目前支持的
额外信息包括分配和释放时的 CPU 编号和时间戳。更多的信息可以帮助找到内核错误的原因，
并将错误与其他系统事件关联起来，但代价是用额外的内存来记录更多信息（有关更多
开销的细节，请参见 CONFIG_KASAN_EXTRA_INFO 选项的帮助文本）。

以下为 CONFIG_KASAN_EXTRA_INFO 开启后的报告（仅显示不同部分）::

    ==================================================================
    ...
    Allocated by task 134 on cpu 5 at 229.133855s:
    ...
    Freed by task 136 on cpu 3 at 230.199335s:
    ...
    ==================================================================

实施细则
--------

通用KASAN
~~~~~~~~~

软件KASAN模式使用影子内存来记录每个内存字节是否可以安全访问，并使用编译时工具
在每次内存访问之前插入影子内存检查。

通用KASAN将1/8的内核内存专用于其影子内存（16TB以覆盖x86_64上的128TB），并使用
具有比例和偏移量的直接映射将内存地址转换为其相应的影子地址。

这是将地址转换为其相应影子地址的函数::

    static inline void *kasan_mem_to_shadow(const void *addr)
    {
	return (void *)((unsigned long)addr >> KASAN_SHADOW_SCALE_SHIFT)
		+ KASAN_SHADOW_OFFSET;
    }

在这里 ``KASAN_SHADOW_SCALE_SHIFT = 3`` 。

编译时工具用于插入内存访问检查。编译器在每次访问大小为1、2、4、8或16的内存之前
插入函数调用( ``__asan_load*(addr)`` , ``__asan_store*(addr)``)。这些函数通过
检查相应的影子内存来检查内存访问是否有效。

使用inline插桩，编译器不进行函数调用，而是直接插入代码来检查影子内存。此选项
显著地增大了内核体积，但与outline插桩内核相比，它提供了x1.1-x2的性能提升。

通用KASAN是唯一一种通过隔离延迟重新使用已释放对象的模式
（参见 mm/kasan/quarantine.c 以了解实现）。

基于软件标签的KASAN模式
~~~~~~~~~~~~~~~~~~~~~~~

基于软件标签的KASAN使用软件内存标签方法来检查访问有效性。目前仅针对arm64架构实现。

基于软件标签的KASAN使用arm64 CPU的顶部字节忽略(TBI)特性在内核指针的顶部字节中
存储一个指针标签。它使用影子内存来存储与每个16字节内存单元相关的内存标签(因此，
它将内核内存的1/16专用于影子内存)。

在每次内存分配时，基于软件标签的KASAN都会生成一个随机标签，用这个标签标记分配
的内存，并将相同的标签嵌入到返回的指针中。

基于软件标签的KASAN使用编译时工具在每次内存访问之前插入检查。这些检查确保正在
访问的内存的标签等于用于访问该内存的指针的标签。如果标签不匹配，基于软件标签
的KASAN会打印错误报告。

基于软件标签的KASAN也有两种插桩模式（outline，发出回调来检查内存访问；inline，
执行内联的影子内存检查）。使用outline插桩模式，会从执行访问检查的函数打印错误
报告。使用inline插桩，编译器会发出 ``brk`` 指令，并使用专用的 ``brk`` 处理程序
来打印错误报告。

基于软件标签的KASAN使用0xFF作为匹配所有指针标签（不检查通过带有0xFF指针标签
的指针进行的访问）。值0xFE当前保留用于标记已释放的内存区域。


基于硬件标签的KASAN模式
~~~~~~~~~~~~~~~~~~~~~~~

基于硬件标签的KASAN在概念上类似于软件模式，但它是使用硬件内存标签作为支持而
不是编译器插桩和影子内存。

基于硬件标签的KASAN目前仅针对arm64架构实现，并且基于ARMv8.5指令集架构中引入
的arm64内存标记扩展(MTE)和最高字节忽略(TBI)。

特殊的arm64指令用于为每次内存分配指定内存标签。相同的标签被指定给指向这些分配
的指针。在每次内存访问时，硬件确保正在访问的内存的标签等于用于访问该内存的指针
的标签。如果标签不匹配，则会生成故障并打印报告。

基于硬件标签的KASAN使用0xFF作为匹配所有指针标签（不检查通过带有0xFF指针标签的
指针进行的访问）。值0xFE当前保留用于标记已释放的内存区域。

如果硬件不支持MTE（ARMv8.5之前），则不会启用基于硬件标签的KASAN。在这种情况下，
所有KASAN引导参数都将被忽略。

请注意，启用CONFIG_KASAN_HW_TAGS始终会导致启用内核中的TBI。即使提供了
``kasan.mode=off`` 或硬件不支持MTE（但支持TBI）。

基于硬件标签的KASAN只报告第一个发现的错误。之后，MTE标签检查将被禁用。

影子内存
--------

本节的内容只适用于软件KASAN模式。

内核将内存映射到地址空间的几个不同部分。内核虚拟地址的范围很大：没有足够的真实
内存来支持内核可以访问的每个地址的真实影子区域。因此，KASAN只为地址空间的某些
部分映射真实的影子。

默认行为
~~~~~~~~

默认情况下，体系结构仅将实际内存映射到用于线性映射的阴影区域（以及可能的其他
小区域）。对于所有其他区域 —— 例如vmalloc和vmemmap空间 —— 一个只读页面被映射
到阴影区域上。这个只读的影子页面声明所有内存访问都是允许的。

这给模块带来了一个问题：它们不存在于线性映射中，而是存在于专用的模块空间中。
通过连接模块分配器，KASAN临时映射真实的影子内存以覆盖它们。例如，这允许检测
对模块全局变量的无效访问。

这也造成了与 ``VMAP_STACK`` 的不兼容：如果堆栈位于vmalloc空间中，它将被分配
只读页面的影子内存，并且内核在尝试为堆栈变量设置影子数据时会出错。

CONFIG_KASAN_VMALLOC
~~~~~~~~~~~~~~~~~~~~

使用 ``CONFIG_KASAN_VMALLOC`` ，KASAN可以以更大的内存使用为代价覆盖vmalloc
空间。目前，这在arm64、x86、riscv、s390和powerpc上受支持。

这通过连接到vmalloc和vmap并动态分配真实的影子内存来支持映射。

vmalloc空间中的大多数映射都很小，需要不到一整页的阴影空间。因此，为每个映射
分配一个完整的影子页面将是一种浪费。此外，为了确保不同的映射使用不同的影子
页面，映射必须与 ``KASAN_GRANULE_SIZE * PAGE_SIZE`` 对齐。

相反，KASAN跨多个映射共享后备空间。当vmalloc空间中的映射使用影子区域的特定
页面时，它会分配一个后备页面。此页面稍后可以由其他vmalloc映射共享。

KASAN连接到vmap基础架构以懒清理未使用的影子内存。

为了避免交换映射的困难，KASAN预测覆盖vmalloc空间的阴影区域部分将不会被早期
的阴影页面覆盖，但是将不会被映射。这将需要更改特定于arch的代码。

这允许在x86上支持 ``VMAP_STACK`` ，并且可以简化对没有固定模块区域的架构的支持。

对于开发者
----------

忽略访问
~~~~~~~~

软件KASAN模式使用编译器插桩来插入有效性检查。此类检测可能与内核的某些部分
不兼容，因此需要禁用。

内核的其他部分可能会访问已分配对象的元数据。通常，KASAN会检测并报告此类访问，
但在某些情况下（例如，在内存分配器中），这些访问是有效的。

对于软件KASAN模式，要禁用特定文件或目录的检测，请将 ``KASAN_SANITIZE`` 添加
到相应的内核Makefile中:

- 对于单个文件(例如，main.o)::

    KASAN_SANITIZE_main.o := n

- 对于一个目录下的所有文件::

    KASAN_SANITIZE := n

对于软件KASAN模式，要在每个函数的基础上禁用检测，请使用KASAN特定的
``__no_sanitize_address`` 函数属性或通用的 ``noinstr`` 。

请注意，禁用编译器插桩（基于每个文件或每个函数）会使KASAN忽略在软件KASAN模式
的代码中直接发生的访问。当访问是间接发生的（通过调用检测函数）或使用没有编译器
插桩的基于硬件标签的模式时，它没有帮助。

对于软件KASAN模式，要在当前任务的一部分内核代码中禁用KASAN报告，请使用
``kasan_disable_current()``/``kasan_enable_current()`` 部分注释这部分代码。
这也会禁用通过函数调用发生的间接访问的报告。

对于基于标签的KASAN模式，要禁用访问检查，请使用 ``kasan_reset_tag()`` 或
``page_kasan_tag_reset()`` 。请注意，通过 ``page_kasan_tag_reset()``
临时禁用访问检查需要通过 ``page_kasan_tag`` / ``page_kasan_tag_set`` 保
存和恢复每页KASAN标签。

测试
~~~~

有一些KASAN测试可以验证KASAN是否正常工作并可以检测某些类型的内存损坏。

所有 KASAN 测试都与 KUnit 测试框架集成，可通过 ``CONFIG_KASAN_KUNIT_TEST`` 启用。
测试可以通过几种不同的方式自动运行和部分验证；请参阅以下说明。

如果检测到错误，每个 KASAN 测试都会打印多份 KASAN 报告中的一份。
然后测试会打印其编号和状态。

当测试通过::

        ok 28 - kmalloc_double_kzfree

当由于 ``kmalloc`` 失败而导致测试失败时::

        # kmalloc_large_oob_right: ASSERTION FAILED at mm/kasan/kasan_test.c:245
        Expected ptr is not null, but is
        not ok 5 - kmalloc_large_oob_right

当由于缺少KASAN报告而导致测试失败时::

        # kmalloc_double_kzfree: EXPECTATION FAILED at mm/kasan/kasan_test.c:709
        KASAN failure expected in "kfree_sensitive(ptr)", but none occurred
        not ok 28 - kmalloc_double_kzfree


最后打印所有KASAN测试的累积状态。成功::

        ok 1 - kasan

或者，如果其中一项测试失败::

        not ok 1 - kasan

有几种方法可以运行 KASAN 测试。

1. 可加载模块

   启用 ``CONFIG_KUNIT`` 后，可以将测试构建为可加载模块
   并通过使用 ``insmod`` 或 ``modprobe`` 加载 ``kasan_test.ko`` 来运行。

2. 内置

   通过内置 ``CONFIG_KUNIT``，测试也可以内置。
   测试将在启动时作为后期初始化调用运行。

3. 使用kunit_tool

   通过内置 ``CONFIG_KUNIT`` 和 ``CONFIG_KASAN_KUNIT_TEST`` ，还可以使用
   ``kunit_tool`` 以更易读的方式查看KUnit测试结果。这不会打印通过测试
   的KASAN报告。有关 ``kunit_tool`` 更多最新信息，请参阅
   `KUnit文档 <https://www.kernel.org/doc/html/latest/dev-tools/kunit/index.html>`_ 。

.. _KUnit: https://www.kernel.org/doc/html/latest/dev-tools/kunit/index.html
