.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/mm/vmalloced-kernel-stacks.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:

====================
支持虚拟映射的内核栈
====================

:作者: Shuah Khan <skhan@linuxfoundation.org>

.. contents:: :local:

概览
----

这是介绍 `虚拟映射内核栈功能 <https://lwn.net/Articles/694348/>` 的代码
和原始补丁系列的信息汇总。

简介
----

内核堆栈溢出通常难以调试，并使内核容易被（恶意）利用。问题可能在稍后的时间出现，使其难以
隔离和究其根本原因。

带有保护页的虚拟映射内核堆栈如果溢出，会被立即捕获，而不会放任其导致难以诊断的损
坏。

HAVE_ARCH_VMAP_STACK和VMAP_STACK配置选项能够支持带有保护页的虚拟映射堆栈。
当堆栈溢出时，这个特性会引发可靠的异常。溢出后堆栈跟踪的可用性以及对溢出本身的
响应取决于架构。

.. note::
        截至本文撰写时， arm64, powerpc, riscv, s390, um, 和 x86 支持VMAP_STACK。

HAVE_ARCH_VMAP_STACK
--------------------

能够支持虚拟映射内核栈的架构应该启用这个bool配置选项。要求是:

- vmalloc空间必须大到足以容纳许多内核堆栈。这可能排除了许多32位架构。
- vmalloc空间的堆栈需要可靠地工作。例如，如果vmap页表是按需创建的，当堆栈指向
  具有未填充页表的虚拟地址时，这种机制需要工作，或者架构代码（switch_to()和
  switch_mm()，很可能）需要确保堆栈的页表项在可能未填充的堆栈上运行之前已经填
  充。
- 如果堆栈溢出到一个保护页，就应该发生一些合理的事情。“合理”的定义是灵活的，但
  在没有记录任何东西的情况下立即重启是不友好的。

VMAP_STACK
----------

VMAP_STACK bool配置选项在启用时分配虚拟映射的任务栈。这个选项依赖于
HAVE_ARCH_VMAP_STACK。

- 如果你想使用带有保护页的虚拟映射的内核堆栈，请启用该选项。这将导致内核栈溢出
  被立即捕获，而不是难以诊断的损坏。

.. note::

        使用KASAN的这个功能需要架构支持用真实的影子内存来支持虚拟映射，并且
        必须启用KASAN_VMALLOC。

.. note::

        启用VMAP_STACK时，无法在堆栈分配的数据上运行DMA。

内核配置选项和依赖性不断变化。请参考最新的代码库:

`Kconfig <https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/arch/Kconfig>`

分配方法
--------

当一个新的内核线程被创建时，线程堆栈是由页级分配器分配的虚拟连续的内存页组成。这
些页面被映射到有PAGE_KERNEL保护的连续的内核虚拟空间。

alloc_thread_stack_node()调用__vmalloc_node_range()来分配带有PAGE_KERNEL
保护的栈。

- 分配的堆栈被缓存起来，以后会被新的线程重用，所以在分配/释放堆栈给任务时，要手动
  进行memcg核算。因此，__vmalloc_node_range被调用时没有__GFP_ACCOUNT。
- vm_struct被缓存起来，以便能够找到在中断上下文中启动的空闲线程。 free_thread_stack()
  可以在中断上下文中调用。
- 在arm64上，所有VMAP的堆栈都需要有相同的对齐方式，以确保VMAP的堆栈溢出检测正常
  工作。架构特定的vmap堆栈分配器照顾到了这个细节。
- 这并不涉及中断堆栈--参考原始补丁

线程栈分配是由clone()、fork()、vfork()、kernel_thread()通过kernel_clone()
启动的。留点提示在这，以便搜索代码库，了解线程栈何时以及如何分配。

大量的代码是在:
`kernel/fork.c <https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/kernel/fork.c>`.

task_struct中的stack_vm_area指针可以跟踪虚拟分配的堆栈，一个非空的stack_vm_area
指针可以表明虚拟映射的内核堆栈已经启用。

::

        struct vm_struct *stack_vm_area;

堆栈溢出处理
------------

前守护页和后守护页有助于检测堆栈溢出。当堆栈溢出到守护页时，处理程序必须小心不要再
次溢出堆栈。当处理程序被调用时，很可能只留下很少的堆栈空间。

在x86上，这是通过处理表明内核堆栈溢出的双异常堆栈的缺页异常来实现的。

用守护页测试VMAP分配
--------------------

我们如何确保VMAP_STACK在分配时确实有前守护页和后守护页的保护？下面的 lkdtm 测试
可以帮助检测任何回归。

::

        void lkdtm_STACK_GUARD_PAGE_LEADING()
        void lkdtm_STACK_GUARD_PAGE_TRAILING()

结论
----

- vmalloced堆栈的percpu缓存似乎比高阶堆栈分配要快一些，至少在缓存命中时是这样。
- THREAD_INFO_IN_TASK完全摆脱了arch-specific thread_info，并简单地将
  thread_info（仅包含标志）和'int cpu'嵌入task_struct中。
- 一旦任务死亡，线程栈就可以被释放（无需等待RCU），然后，如果使用vmapped栈，就
  可以将整个栈缓存起来，以便在同一cpu上重复使用。
