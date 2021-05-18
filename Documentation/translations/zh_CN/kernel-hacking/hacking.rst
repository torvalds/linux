.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/kernel-hacking/hacking.rst

:译者:

 吴想成 Wu XiangCheng <bobwxc@email.cn>

==============
内核骇客指北
==============

:作者: Rusty Russell

引言
=====

欢迎咱优雅的读者们来阅读Rusty的非常不靠谱的Linux内核骇客（Hacking）指南。本文
描述了内核代码的常见例程和一般要求：其目标是引导有经验的C程序员入门Linux内核
开发。我回避了实现细节：这是代码要做的，也忽略了很多有用的例程。

在你读这篇文章之前，请理解我从来没有想过要写这篇文章，因为我的资历太低了；
但我一直想读这样的文章，自己写是唯一的方法。我希望它能成长为一个最佳实践、
通用起点和其他信息的汇编。

玩家
=======

在任何时候，系统中的每个CPU都可以：

-  与任何进程无关，服务于硬件中断；

-  与任何进程无关，服务于软件中断（softirq）或子任务（tasklet）；

-  运行于内核空间中，与进程（用户上下文）相关联；

-  在用户空间中运行进程。

它们之间有优先级顺序。最下面的两个可以互相抢占，但上面为严格的层次结构：
每个层级只能被上方的抢占。例如，当一个软中断在CPU上运行时，没有其他软中断
会抢占它，但是硬件中断可以抢占它。不过，系统中的任何其他CPU都是独立执行的。

我们将会看到许多方法，用户上下文可以阻止中断，从而成为真正的不可抢占。

用户上下文
------------

用户上下文是指当您从系统调用或其他陷阱进入时：就像用户空间一样，您可以被更
重要的任务和中断抢占。您可以通过调用 :c:func:`schedule()` 进行睡眠。

.. note::

    在模块加载和卸载以及块设备层上的操作时，你始终处于用户上下文中。

在用户上下文中，当前 ``current`` 指针（指示我们当前正在执行的任务）是有效的，
且 :c:func:`in_interrupt()` （ ``include/linux/preempt.h`` ）值为非（false）。

.. warning::

    请注意，如果您禁用了抢占或软中断（见下文），:c:func:`in_interrupt()` 会
    返回假阳性。

硬件中断（Hard IRQs）
----------------------

像定时器、网卡和键盘等都是可能在任意时刻产生中断的真实硬件。内核运行中断
处理程序，为硬件提供服务。内核确保处理程序永远不会重入：如果相同的中断到达，
它将被排队（或丢弃）。因为它会关闭中断，所以处理程序必须很快：通常它只是
确认中断，标记一个“软件中断”以执行并退出。

您可以通过 :c:func:`in_irq()` 返回真来判断您处于硬件中断状态。

.. warning::

    请注意，如果中断被禁用，这将返回假阳性（见下文）。

软件中断上下文：软中断（Softirqs）与子任务（Tasklets）
-------------------------------------------------------

当系统调用即将返回用户空间或硬件中断处理程序退出时，任何标记为挂起（通常通
过硬件中断）的“软件中断”将运行（ ``kernel/softirq.c`` ）。

此处完成了许多真正的中断处理工作。在向SMP过渡的早期，只有“bottom halves下半
部”（BHs）机制，无法利用多个CPU的优势。在从那些一团糟的就电脑切换过来后不久，
我们放弃了这个限制，转而使用“软中断”。

``include/linux/interrupt.h`` 列出了不同的软中断。定时器软中断是一个非常重要
的软中断（ ``include/linux/timer.h`` ）：您可以注册它以在给定时间后为您调用
函数。

软中断通常是一个很难处理的问题，因为同一个软中断将同时在多个CPU上运行。因此，
子任务（ ``include/linux/interrupt.h`` ）更常用：它们是动态可注册的（意味着
您可以拥有任意数量），并且它们还保证任何子任务都只能在一个CPU上运行，不同的
子任务也可以同时运行。

.. warning::

    “tasklet”这个名字是误导性的：它们与“任务”无关，可能更多与当时
    阿列克谢·库兹涅佐夫享用的糟糕伏特加有关。

你可以使用 :c:func:`in_softirq()` 宏（ ``include/linux/preempt.h`` ）来确认
是否处于软中断（或子任务）中。

.. warning::

    注意，如果持有 :ref:`bottom half lock <local_bh_disable_zh>` 锁，这将返回
    假阳性。

一些基本规则
================

缺少内存保护
    如果你损坏了内存，无论是在用户上下文还是中断上下文中，整个机器都会崩溃。
    你确定你不能在用户空间里做你想做的事吗？

缺少浮点或MMX
    FPU上下文不会被保存；即使在用户上下文中，FPU状态也可能与当前进程不一致：
    您会弄乱某些用户进程的FPU状态。如果真的要这样做，就必须显式地保存/恢复
    完整的FPU状态（并避免上下文切换）。这通常不是个好主意；请优先用定点算法。

严格的堆栈限制
    对于大多数32位体系结构，根据配置选项的不同内核堆栈大约为3K到6K；对于大
    多数64位机器，内核堆栈大约为14K，并且经常与中断共享，因此你无法使用全部。
    应避免深度递归和栈上的巨型本地数组（用动态分配它们来代替）。

Linux内核是可移植的
    就这样吧。您的代码应该是纯64位的，并且不依赖于字节序（endian）。您还应该
    尽量减少CPU特定的东西，例如内联汇编（inline assembly）应该被干净地封装和
    最小化以便于移植。一般来说，它应该局限于内核树中有体系结构依赖的部分。

输入输出控制（ioctls）：避免编写新的系统调用
==============================================

系统调用（system call）通常看起来像这样::

    asmlinkage long sys_mycall(int arg)
    {
            return 0;
    }


首先，在大多数情况下，您无需创建新的系统调用。创建一个字符设备并为其实现适当
的输入输出控制（ioctls）。这比系统调用灵活得多，不必写进每个体系结构的
``include/asm/unistd.h`` 和 ``arch/kernel/entry.S`` 文件里，而且更容易被Linus
接受。

如果您的程序所做的只是读取或写入一些参数，请考虑实现 :c:func:`sysfs()` 接口。

在输入输出控制中，您处于进程的用户上下文。出现错误时，返回一个负的错误参数
（errno，请参阅 ``include/uapi/asm-generic/errno-base.h`` 、
``include/uapi/asm-generic/errno.h`` 和 ``include/linux/errno.h`` ），否则返
回0。

在睡眠之后，您应该检查是否出现了信号：Unix/Linux处理信号的方法是暂时退出系统
调用，并返回 ``-ERESTARTSYS`` 错误。系统调用入口代码将切换回用户上下文，处理
信号处理程序，然后系统调用将重新启动（除非用户禁用了该功能）。因此，您应该准
备好处理重新启动，例如若您处理某些数据结构到一半。

::

    if (signal_pending(current))
            return -ERESTARTSYS;


如果你要做更长时间的计算：优先考虑用户空间。如果你真的想在内核中做这件事，你
应该定期检查你是否需要让出CPU（请记得每个CPU都有协作多任务）。
习惯用法::

    cond_resched(); /* Will sleep */


接口设计的小注释：UNIX系统调用的格言是“提供机制而不是策略
Provide mechanism not policy”。

死锁的“配方”
====================

您不能调用任何可能睡眠的程序，除非：

- 您处于用户上下文中。

- 你未拥有任何自旋锁。

- 您已经启用中断（实际上，Andi Kleen说调度代码将为您启用它们，但这可能不是
  您想要的）。

注意，有些函数可能隐式地睡眠：常见的是用户空间访问函数（\*_user）和没有
``GFP_ATOMIC`` 的内存分配函数。

您应该始终打开  ``CONFIG_DEBUG_ATOMIC_SLEEP``  项来编译内核，如果您违反这些
规则，它将警告您。如果你 **真的** 违反了规则，你最终会锁住你的电脑。

真的会这样。


常用函数/程序
===============

:c:func:`printk()`
------------------

定义于 ``include/linux/printk.h``

:c:func:`printk()` 将内核消息提供给控制台、dmesg和syslog守护进程。它对于调
试和报告错误很有用，并且可以在中断上下文中使用，但是使用时要小心：如果机器
的控制台中充斥着printk消息则会无法使用。它使用与ANSI C printf基本兼容的格式
字符串，并通过C字符串串联为其提供第一个“优先”参数::

    printk(KERN_INFO "i = %u\n", i);


参见 ``include/linux/kern_levels.h`` ；了解其他 ``KERN_`` 值；syslog将这些值
解释为级别。特殊用法：打印IP地址使用::

    __be32 ipaddress;
    printk(KERN_INFO "my ip: %pI4\n", &ipaddress);


:c:func:`printk()` 内部使用的1K缓冲区，不捕获溢出。请确保足够使用。

.. note::

    当您开始在用户程序中将printf打成printk时，就知道自己是真正的内核程序员了
    :)

.. note::

    另一个注释：最初的unix第六版源代码在其printf函数的顶部有一个注释：“printf
    不应该用于叽叽喳喳”。你也应该遵循此建议。

:c:func:`copy_to_user()` / :c:func:`copy_from_user()` / :c:func:`get_user()` / :c:func:`put_user()`
---------------------------------------------------------------------------------------------------

定义于 ``include/linux/uaccess.h`` / ``asm/uaccess.h``

**[睡眠]**

:c:func:`put_user()` 和 :c:func:`get_user()` 用于从用户空间中获取和向用户空
间中传出单个值（如int、char或long）。指向用户空间的指针永远不应该直接取消
引用：应该使用这些程序复制数据。两者都返回 ``-EFAULT`` 或 0。

:c:func:`copy_to_user()` 和 :c:func:`copy_from_user()` 更通用：它们从/向用户
空间复制任意数量的数据。

.. warning::

    与 :c:func:`put_user()` 和 :c:func:`get_user()` 不同，它们返回未复制的
    数据量（即0仍然意味着成功）。

【是的，这个愚蠢的接口真心让我尴尬。火爆的口水仗大概每年都会发生。
—— Rusty Russell】

这些函数可以隐式睡眠。它不应该在用户上下文之外调用（没有意义）、调用时禁用中断
或获得自旋锁。

:c:func:`kmalloc()`/:c:func:`kfree()`
-------------------------------------

定义于 ``include/linux/slab.h``

**[可能睡眠：见下]**

这些函数用于动态请求指针对齐的内存块，类似用户空间中的malloc和free，但
:c:func:`kmalloc()` 需要额外的标志词。重要的值：

``GFP_KERNEL``
    可以睡眠和交换以释放内存。只允许在用户上下文中使用，但这是分配内存最可靠
    的方法。

``GFP_ATOMIC``
    不会睡眠。较 ``GFP_KERNEL`` 更不可靠，但可以从中断上下文调用。你 **应该**
    有一个很好的内存不足错误处理策略。

``GFP_DMA``
    分配低于16MB的ISA DMA。如果你不知道那是什么，那你就不需要了。非常不可靠。

如果您看到一个从无效上下文警告消息调用的睡眠的函数，那么您可能在没有
``GFP_ATOMIC`` 的情况下从中断上下文调用了一个睡眠的分配函数。你必须立即修复，
快点！

如果你要分配至少 ``PAGE_SIZE`` （ ``asm/page.h`` 或 ``asm/page_types.h`` ）
字节，请考虑使用 :c:func:`__get_free_pages()` （ ``include/linux/gfp.h`` ）。
它采用顺序参数（0表示页面大小，1表示双页，2表示四页……）和与上述相同的内存
优先级标志字。

如果分配的字节数超过一页，可以使用 :c:func:`vmalloc()` 。它将在内核映射中分
配虚拟内存。此块在物理内存中不是连续的，但是MMU（内存管理单元）使它看起来像
是为您准备好的连续空间（因此它只是看起来对cpu连续，对外部设备驱动程序则不然）。
如果您真的需要为一些奇怪的设备提供大量物理上连续的内存，那么您就会遇到问题：
Linux对此支持很差，因为正在运行的内核中的内存碎片化会使它变得很困难。最好的
方法是在引导过程的早期通过 :c:func:`alloc_bootmem()` 函数分配。

在创建自己的常用对象缓存之前，请考虑使用 ``include/linux/slab.h`` 中的slab
缓存。

:c:macro:`current`
------------------

定义于 ``include/asm/current.h``

此全局变量（其实是宏）包含指向当前任务结构（task structure）的指针，因此仅在
用户上下文中有效。例如，当进程进行系统调用时，这将指向调用进程的任务结构。
在中断上下文中不为空（**not NULL**）。

:c:func:`mdelay()`/:c:func:`udelay()`
-------------------------------------

定义于 ``include/asm/delay.h`` / ``include/linux/delay.h``

:c:func:`udelay()` 和 :c:func:`ndelay()` 函数可被用于小暂停。不要对它们使用
大的值，因为这样会导致溢出——帮助函数 :c:func:`mdelay()` 在这里很有用，或者
考虑 :c:func:`msleep()`。

:c:func:`cpu_to_be32()`/:c:func:`be32_to_cpu()`/:c:func:`cpu_to_le32()`/:c:func:`le32_to_cpu()`
-----------------------------------------------------------------------------------------------

定义于 ``include/asm/byteorder.h``

:c:func:`cpu_to_be32()` 系列函数（其中“32”可以替换为64或16，“be”可以替换为
“le”）是在内核中进行字节序转换的常用方法：它们返回转换后的值。所有的变体也
提供反向转换函数：
:c:func:`be32_to_cpu()` 等。

这些函数有两个主要的变体：指针变体，例如 :c:func:`cpu_to_be32p()` ，它获取
指向给定类型的指针，并返回转换后的值。另一个变体是“in-situ”系列，例如
:c:func:`cpu_to_be32s()` ，它转换指针引用的值，并返回void。

:c:func:`local_irq_save()`/:c:func:`local_irq_restore()`
--------------------------------------------------------

定义于 ``include/linux/irqflags.h``


这些程序禁用本地CPU上的硬中断，并还原它们。它们是可重入的；在其一个
``unsigned long flags`` 参数中保存以前的状态。如果您知道中断已启用，那么可
直接使用 :c:func:`local_irq_disable()` 和 :c:func:`local_irq_enable()`。

.. _local_bh_disable_zh:

:c:func:`local_bh_disable()`/:c:func:`local_bh_enable()`
--------------------------------------------------------

定义于 ``include/linux/bottom_half.h``


这些程序禁用本地CPU上的软中断，并还原它们。它们是可重入的；如果之前禁用了
软中断，那么在调用这对函数之后仍然会禁用它们。它们阻止软中断和子任务在当前
CPU上运行。

:c:func:`smp_processor_id()`
----------------------------

定义于 ``include/linux/smp.h``

:c:func:`get_cpu()` 禁用抢占（这样您就不会突然移动到另一个cpu）并返回当前
处理器号，介于0和 ``NR_CPUS`` 之间。请注意，CPU编号不一定是连续的。完成后，
使用 :c:func:`put_cpu()` 再次返回。

如果您知道您不能被另一个任务抢占（即您处于中断上下文中，或已禁用抢占），您
可以使用 :c:func:`smp_processor_id()`。

``__init``/``__exit``/``__initdata``
------------------------------------

定义于  ``include/linux/init.h``

引导之后，内核释放一个特殊的部分；用 ``__init`` 标记的函数和用 ``__initdata``
标记的数据结构在引导完成后被丢弃：同样地，模块在初始化后丢弃此内存。
``__exit`` 用于声明只在退出时需要的函数：如果此文件未编译为模块，则该函数将
被删除。请参阅头文件以使用。请注意，使用 :c:func:`EXPORT_SYMBOL()` 或
:c:func:`EXPORT_SYMBOL_GPL()` 将标记为 ``__init`` 的函数导出到模块是没有意义
的——这将出问题。


:c:func:`__initcall()`/:c:func:`module_init()`
----------------------------------------------

定义于  ``include/linux/init.h`` / ``include/linux/module.h``

内核的许多部分都作为模块（内核的可动态加载部分）良好服务。使用
:c:func:`module_init()` 和 :c:func:`module_exit()` 宏可以简化代码编写，无需
``#ifdef`` ，即可以作为模块运行或内置在内核中。

:c:func:`module_init()` 宏定义在模块插入时（如果文件编译为模块）或在引导时
调用哪个函数：如果文件未编译为模块，:c:func:`module_init()` 宏将等效于
:c:func:`__initcall()` ，它通过链接器的魔力确保在引导时调用该函数。

该函数可以返回一个错误值，以导致模块加载失败（不幸的是，如果将模块编译到内核
中，则此操作无效）。此函数在启用中断的用户上下文中调用，因此可以睡眠。

:c:func:`module_exit()`
-----------------------


定义于  ``include/linux/module.h``

这个宏定义了在模块删除时要调用的函数（如果是编译到内核中的文件，则无用武之地）。
只有在模块使用计数到零时才会调用它。这个函数也可以睡眠，但不能失败：当它返回
时，所有的东西都必须清理干净。

注意，这个宏是可选的：如果它不存在，您的模块将不可移除（除非 ``rmmod -f`` ）。

:c:func:`try_module_get()`/:c:func:`module_put()`
-------------------------------------------------

定义于 ``include/linux/module.h``

这些函数会操作模块使用计数，以防止删除（如果另一个模块使用其导出的符号之一，
则无法删除模块，参见下文）。在调用模块代码之前，您应该在该模块上调用
:c:func:`try_module_get()` ：若失败，那么该模块将被删除，您应该将其视为不存在。
若成功，您就可以安全地进入模块，并在完成后调用模块 :c:func:`module_put()` 。

大多数可注册结构体都有所有者字段，例如在
:c:type:`struct file_operations <file_operations>` 结构体中，此字段应设置为
宏 ``THIS_MODULE`` 。

等待队列 ``include/linux/wait.h``
====================================

**[睡眠]**

等待队列用于等待某程序在条件为真时唤醒另一程序。必须小心使用，以确保没有竞争
条件。先声明一个 :c:type:`wait_queue_head_t` ，然后对希望等待该条件的进程声明
一个关于它们自己的 :c:type:`wait_queue_entry_t` ，并将其放入队列中。

声明
-----

使用 :c:func:`DECLARE_WAIT_QUEUE_HEAD()` 宏声明一个 ``wait_queue_head_t`` ，
或者在初始化代码中使用 :c:func:`init_waitqueue_head()` 程序。

排队
-----

将自己放在等待队列中相当复杂，因为你必须在检查条件之前将自己放入队列中。有一
个宏可以来执行此操作： :c:func:`wait_event_interruptible()`
（ ``include/linux/wait.h`` ）第一个参数是等待队列头，第二个参数是计算的表达
式；当该表达式为true时宏返回0，或者在接收到信号时返回 ``-ERESTARTSYS`` 。
:c:func:`wait_event()` 版本会忽略信号。

唤醒排队任务
-------------

调用 :c:func:`wake_up()` （ ``include/linux/wait.h`` ），它将唤醒队列中的所有
进程。例外情况：如果有一个进程设置了 ``TASK_EXCLUSIVE`` ，队列的其余部分将不
会被唤醒。这个基本函数的其他变体也可以在同一个头文件中使用。

原子操作
=========

某些操作在所有平台上都有保证。第一类为操作 :c:type:`atomic_t`
（ ``include/asm/atomic.h`` ）的函数；它包含一个有符号整数（至少32位长），
您必须使用这些函数来操作或读取 :c:type:`atomic_t` 变量。
:c:func:`atomic_read()` 和 :c:func:`atomic_set()` 获取并设置计数器，还有
:c:func:`atomic_add()` ，:c:func:`atomic_sub()` ，:c:func:`atomic_inc()` ，
:c:func:`atomic_dec()` 和 :c:func:`atomic_dec_and_test()` （如果递减为零，
则返回true）。

是的。它在原子变量为零时返回true（即!=0）。

请注意，这些函数比普通的算术运算速度慢，因此不应过度使用。

第二类原子操作是在 ``unsigned long`` （ ``include/linux/bitops.h`` ）上的
原子位操作。这些操作通常采用指向位模式（bit pattern）的指针，第0位是最低有效
位。:c:func:`set_bit()`，:c:func:`clear_bit()` 和 :c:func:`change_bit()` 设置、
清除和更改给定位。:c:func:`test_and_set_bit()` ，:c:func:`test_and_clear_bit()`
和 :c:func:`test_and_change_bit()` 执行相同的操作，但如果之前设置了位，则返回
true；这些对于原子设置标志特别有用。

可以使用大于 ``BITS_PER_LONG`` 位的位索引调用这些操作。但结果在大端序平台上
不太正常，所以最好不要这样做。

符号
=====

在内核内部，正常的链接规则仍然适用（即除非用static关键字将符号声明为文件范围，
否则它可以在内核中的任何位置使用）。但是对于模块，会保留一个特殊可导出符号表，
该表将入口点限制为内核内部。模块也可以导出符号。

:c:func:`EXPORT_SYMBOL()`
-------------------------

定义于 ``include/linux/export.h``

这是导出符号的经典方法：动态加载的模块将能够正常使用符号。

:c:func:`EXPORT_SYMBOL_GPL()`
-----------------------------

定义于 ``include/linux/export.h``


类似于 :c:func:`EXPORT_SYMBOL()`，只是 :c:func:`EXPORT_SYMBOL_GPL()` 导出的
符号只能由具有由 :c:func:`MODULE_LICENSE()` 指定GPL兼容许可证的模块看到。这
意味着此函数被认为是一个内部实现问题，而不是一个真正的接口。一些维护人员和
开发人员在添加一些新的API或功能时可能却需要导出 EXPORT_SYMBOL_GPL()。

:c:func:`EXPORT_SYMBOL_NS()`
----------------------------

定义于 ``include/linux/export.h``

这是 ``EXPORT_SYMBOL()`` 的变体，允许指定符号命名空间。符号名称空间记录于
Documentation/core-api/symbol-namespaces.rst 。

:c:func:`EXPORT_SYMBOL_NS_GPL()`
--------------------------------

定义于 ``include/linux/export.h``

这是 ``EXPORT_SYMBOL_GPL()`` 的变体，允许指定符号命名空间。符号名称空间记录于
Documentation/core-api/symbol-namespaces.rst 。

程序与惯例
===========

双向链表 ``include/linux/list.h``
-----------------------------------

内核头文件中曾经有三组链表程序，但这一组是赢家。如果你对一个单链表没有特别迫切的
需求，那么这是一个不错的选择。

通常 :c:func:`list_for_each_entry()` 很有用。

返回值惯例
------------

对于在用户上下文中调用的代码，违背C语言惯例是很常见的，即返回0表示成功，返回
负错误值（例如 ``-EFAULT`` ）表示失败。这在一开始可能是不直观的，但在内核中
相当普遍。

使用 :c:func:`ERR_PTR()` （ ``include/linux/err.h`` ）将负错误值编码到指针中，
然后使用 :c:func:`IS_ERR()` 和 :c:func:`PTR_ERR()` 将其再取出：避免为错误值
使用单独的指针参数。挺讨厌的，但的确是个好方式。

破坏编译
----------

Linus和其他开发人员有时会更改开发内核中的函数或结构体名称；这样做不仅是为了
让每个人都保持警惕，还反映了一个重大的更改（例如，不能再在打开中断的情况下
调用，或者执行额外的检查，或者不执行以前捕获的检查）。通常这会附带一个linux
内核邮件列表中相当全面的注释；请搜索存档以查看。简单地对文件进行全局替换通常
会让事情变得 **更糟** 。

初始化结构体成员
------------------

初始化结构体的首选方法是使用指定的初始化器，如ISO C99所述。
例如::

    static struct block_device_operations opt_fops = {
            .open               = opt_open,
            .release            = opt_release,
            .ioctl              = opt_ioctl,
            .check_media_change = opt_media_change,
    };


这使得很容易查找（grep），并且可以清楚地看到设置了哪些结构字段。你应该这样做，
因为它看起来很酷。

GNU 扩展
----------

Linux内核中明确允许GNU扩展。请注意，由于缺乏通用性，一些更复杂的版本并没有
得到很好的支持，但以下内容被认为是标准的（有关更多详细信息，请参阅GCC info页
的“C 扩展”部分——是的，实际上是info页，手册页只是info中内容的简短摘要）。

- 内联函数

- 语句表达式（Statement expressions）（即（{ 和 }）结构）。


- 声明函数/变量/类型的属性（__attribute__）

- typeof

- 零长度数组

- 宏变量

- 空指针运算

- 非常量（Non-Constant）初始化程序

- 汇编程序指令（在 arch/ 和 include/asm/ 之内）

- 字符串函数名（__func__）。

- __builtin_constant_p()

在内核中使用long long时要小心，gcc为其生成的代码非常糟糕：除法和乘法在i386上
不能工作，因为内核环境中缺少用于它的gcc运行时函数。

C++
---

在内核中使用C++通常是个坏主意，因为内核不提供必要的运行时环境，并且不为其
测试包含文件。不过这仍然是可能的，但不建议。如果你真的想这么做，至少别用
异常处理（exceptions）。

#if
---

通常认为，在头文件（或.c文件顶部）中使用宏来抽象函数比在源代码中使用“if”预
处理器语句更干净。

把你的东西放进内核里
======================

为了让你的东西更正式、补丁更整洁，还有一些工作要做：

-  搞清楚你在谁的地界儿上干活。查看源文件的顶部、 ``MAINTAINERS`` 文件以及
   ``CREDITS`` 文件的最后一部分。你应该和此人协调，确保你没有重新发明轮子，
   或者尝试一些已经被拒绝的东西。

   确保你把你的名字和电子邮件地址放在你创建或修改的任何文件的顶部。当人们发
   现一个缺陷，或者想要做出修改时，这是他们首先会看的地方。

-  通常你需要一个配置选项来支持你的内核编程。在适当的目录中编辑 ``Kconfig`` 。
   配置语言很容易通过剪切和粘贴来使用，在
   Documentation/kbuild/kconfig-language.rst 中有完整的文档。

   在您对选项的描述中，请确保同时照顾到了专家用户和对此功能一无所知的用户。
   在此说明任何不兼容和问题。结尾一定要写上“如有疑问，就选N”（或者是“Y”）；
   这是针对那些看不懂你在说什么的人的。

-  编辑 ``Makefile`` ：配置变量在这里导出，因此通常你只需添加一行
   “obj-$(CONFIG_xxx) += xxx.o”。语法记录在
   Documentation/kbuild/makefiles.rst 。

-  如果你做了一些有意义的事情，那可以把自己放进 ``CREDITS`` ，通常不止一个
   文件（无论如何你的名字都应该在源文件的顶部）。维护人员意味着您希望在对
   子系统进行更改时得到询问，并了解缺陷；这意味着对某部分代码做出更多承诺。

-  最后，别忘记去阅读 Documentation/process/submitting-patches.rst ，
   也许还有 Documentation/process/submitting-drivers.rst 。

Kernel 仙女棒
===============

浏览源代码时的一些收藏。请随意添加到此列表。

``arch/x86/include/asm/delay.h``::

    #define ndelay(n) (__builtin_constant_p(n) ? \
            ((n) > 20000 ? __bad_ndelay() : __const_udelay((n) * 5ul)) : \
            __ndelay(n))


``include/linux/fs.h``::

    /*
     * Kernel pointers have redundant information, so we can use a
     * scheme where we can return either an error code or a dentry
     * pointer with the same return value.
     *
     * This should be a per-architecture thing, to allow different
     * error and pointer decisions.
     */
     #define ERR_PTR(err)    ((void *)((long)(err)))
     #define PTR_ERR(ptr)    ((long)(ptr))
     #define IS_ERR(ptr)     ((unsigned long)(ptr) > (unsigned long)(-1000))

``arch/x86/include/asm/uaccess_32.h:``::

    #define copy_to_user(to,from,n)                         \
            (__builtin_constant_p(n) ?                      \
             __constant_copy_to_user((to),(from),(n)) :     \
             __generic_copy_to_user((to),(from),(n)))


``arch/sparc/kernel/head.S:``::

    /*
     * Sun people can't spell worth damn. "compatibility" indeed.
     * At least we *know* we can't spell, and use a spell-checker.
     */

    /* Uh, actually Linus it is I who cannot spell. Too much murky
     * Sparc assembly will do this to ya.
     */
    C_LABEL(cputypvar):
            .asciz "compatibility"

    /* Tested on SS-5, SS-10. Probably someone at Sun applied a spell-checker. */
            .align 4
    C_LABEL(cputypvar_sun4m):
            .asciz "compatible"


``arch/sparc/lib/checksum.S:``::

            /* Sun, you just can't beat me, you just can't.  Stop trying,
             * give up.  I'm serious, I am going to kick the living shit
             * out of you, game over, lights out.
             */


致谢
=====

感谢Andi Kleen提出点子，回答我的问题，纠正我的错误，充实内容等帮助。
感谢Philipp Rumpf做了许多拼写和清晰度修复，以及一些优秀的不明显的点。
感谢Werner Almesberger对 :c:func:`disable_irq()` 做了一个很好的总结，
Jes Sorensen和Andrea Arcangeli补充了一些注意事项。
感谢Michael Elizabeth Chastain检查并补充了配置部分。
感谢Telsa Gwynne教我DocBook。
