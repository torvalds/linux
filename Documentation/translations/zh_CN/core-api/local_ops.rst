.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/core-api/local_ops.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

.. _cn_local_ops:

========================
本地原子操作的语义和行为
========================

:作者: Mathieu Desnoyers


本文解释了本地原子操作的目的，如何为任何给定的架构实现这些操作，并说明了
如何正确使用这些操作。它还强调了在内存写入顺序很重要的情况下，跨CPU读取
这些本地变量时必须采取的预防措施。

.. note::

    注意，基于 ``local_t`` 的操作不建议用于一般内核操作。请使用 ``this_cpu``
    操作来代替使用，除非真的有特殊目的。大多数内核中使用的 ``local_t`` 已
    经被 ``this_cpu`` 操作所取代。 ``this_cpu`` 操作在一条指令中结合了重
    定位和类似 ``local_t`` 的语义，产生了更紧凑和更快的执行代码。


本地原子操作的目的
==================

本地原子操作的目的是提供快速和高度可重入的每CPU计数器。它们通过移除LOCK前
缀和通常需要在CPU间同步的内存屏障，将标准原子操作的性能成本降到最低。

在许多情况下，拥有快速的每CPU原子计数器是很有吸引力的：它不需要禁用中断来保护中
断处理程序，它允许在NMI（Non Maskable Interrupt）处理程序中使用连贯的计数器。
它对追踪目的和各种性能监测计数器特别有用。

本地原子操作只保证在拥有数据的CPU上的变量修改的原子性。因此，必须注意确保只
有一个CPU写到 ``local_t`` 的数据。这是通过使用每CPU的数据来实现的，并确
保我们在一个抢占式安全上下文中修改它。然而，从任何一个CPU读取 ``local_t``
数据都是允许的：这样它就会显得与所有者CPU的其他内存写入顺序不一致。


针对特定架构的实现
==================

这可以通过稍微修改标准的原子操作来实现：只有它们的UP变体必须被保留。这通常
意味着删除LOCK前缀（在i386和x86_64上）和任何SMP同步屏障。如果架构在SMP和
UP之间没有不同的行为，在你的架构的 ``local.h`` 中包括 ``asm-generic/local.h``
就足够了。

通过在一个结构体中嵌入一个 ``atomic_long_t`` ， ``local_t`` 类型被定义为
一个不透明的 ``signed long`` 。这样做的目的是为了使从这个类型到
``long`` 的转换失败。该定义看起来像::

    typedef struct { atomic_long_t a; } local_t;


使用本地原子操作时应遵循的规则
==============================

* 被本地操作触及的变量必须是每cpu的变量。

* *只有* 这些变量的CPU所有者才可以写入这些变量。

* 这个CPU可以从任何上下文（进程、中断、软中断、nmi...）中使用本地操作来更新
  它的local_t变量。

* 当在进程上下文中使用本地操作时，必须禁用抢占（或中断），以确保进程在获得每
  CPU变量和进行实际的本地操作之间不会被迁移到不同的CPU。

* 当在中断上下文中使用本地操作时，在主线内核上不需要特别注意，因为它们将在局
  部CPU上运行，并且已经禁用了抢占。然而，我建议无论如何都要明确地禁用抢占，
  以确保它在-rt内核上仍能正确工作。

* 读取本地cpu变量将提供该变量的当前拷贝。

* 对这些变量的读取可以从任何CPU进行，因为对 “ ``long`` ”，对齐的变量的更新
  总是原子的。由于写入程序的CPU没有进行内存同步，所以在读取 *其他* cpu的变
  量时，可以读取该变量的过期副本。


如何使用本地原子操作
====================

::

    #include <linux/percpu.h>
    #include <asm/local.h>

    static DEFINE_PER_CPU(local_t, counters) = LOCAL_INIT(0);


计数器
======

计数是在一个signed long的所有位上进行的。

在可抢占的上下文中，围绕本地原子操作使用 ``get_cpu_var()`` 和
``put_cpu_var()`` ：它确保在对每个cpu变量进行写访问时，抢占被禁用。比如
说::

    local_inc(&get_cpu_var(counters));
    put_cpu_var(counters);

如果你已经在一个抢占安全上下文中，你可以使用 ``this_cpu_ptr()`` 代替::

    local_inc(this_cpu_ptr(&counters));



读取计数器
==========

那些本地计数器可以从外部的CPU中读取，以求得计数的总和。请注意，local_read
所看到的跨CPU的数据必须被认为是相对于拥有该数据的CPU上发生的其他内存写入来
说不符合顺序的::

    long sum = 0;
    for_each_online_cpu(cpu)
            sum += local_read(&per_cpu(counters, cpu));

如果你想使用远程local_read来同步CPU之间对资源的访问，必须在写入者和读取者
的CPU上分别使用显式的 ``smp_wmb()`` 和 ``smp_rmb()`` 内存屏障。如果你使
用 ``local_t`` 变量作为写在缓冲区中的字节的计数器，就会出现这种情况：在缓
冲区写和计数器增量之间应该有一个 ``smp_wmb()`` ，在计数器读和缓冲区读之间
也应有一个 ``smp_rmb()`` 。

下面是一个使用 ``local.h`` 实现每个cpu基本计数器的示例模块::

    /* test-local.c
     *
     * Sample module for local.h usage.
     */


    #include <asm/local.h>
    #include <linux/module.h>
    #include <linux/timer.h>

    static DEFINE_PER_CPU(local_t, counters) = LOCAL_INIT(0);

    static struct timer_list test_timer;

    /* IPI called on each CPU. */
    static void test_each(void *info)
    {
            /* Increment the counter from a non preemptible context */
            printk("Increment on cpu %d\n", smp_processor_id());
            local_inc(this_cpu_ptr(&counters));

            /* This is what incrementing the variable would look like within a
             * preemptible context (it disables preemption) :
             *
             * local_inc(&get_cpu_var(counters));
             * put_cpu_var(counters);
             */
    }

    static void do_test_timer(unsigned long data)
    {
            int cpu;

            /* Increment the counters */
            on_each_cpu(test_each, NULL, 1);
            /* Read all the counters */
            printk("Counters read from CPU %d\n", smp_processor_id());
            for_each_online_cpu(cpu) {
                    printk("Read : CPU %d, count %ld\n", cpu,
                            local_read(&per_cpu(counters, cpu)));
            }
            mod_timer(&test_timer, jiffies + 1000);
    }

    static int __init test_init(void)
    {
            /* initialize the timer that will increment the counter */
            timer_setup(&test_timer, do_test_timer, 0);
            mod_timer(&test_timer, jiffies + 1);

            return 0;
    }

    static void __exit test_exit(void)
    {
            timer_delete_sync(&test_timer);
    }

    module_init(test_init);
    module_exit(test_exit);

    MODULE_LICENSE("GPL");
    MODULE_AUTHOR("Mathieu Desnoyers");
    MODULE_DESCRIPTION("Local Atomic Ops");
