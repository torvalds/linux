.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/core-api/cpu_hotplug.rst
:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>
 周彬彬 Binbin Zhou <zhoubinbin@loongson.cn>

:校译:

 吴想成 Wu XiangCheng <bobwxc@email.cn>

.. _cn_core_api_cpu_hotplug:

=================
内核中的CPU热拔插
=================

:时间: 2021年9月
:作者: Sebastian Andrzej Siewior <bigeasy@linutronix.de>,
       Rusty Russell <rusty@rustcorp.com.au>,
       Srivatsa Vaddagiri <vatsa@in.ibm.com>,
       Ashok Raj <ashok.raj@intel.com>,
       Joel Schopp <jschopp@austin.ibm.com>,
       Thomas Gleixner <tglx@linutronix.de>

简介
====

现代系统架构的演进已经在处理器中引入了先进的错误报告和纠正能力。有一些OEM也支
持可热拔插的NUMA（Non Uniform Memory Access，非统一内存访问）硬件,其中物理
节点的插入和移除需要支持CPU热插拔。

这样的进步要求内核可用的CPU被移除，要么是出于配置的原因，要么是出于RAS的目的，
以保持一个不需要的CPU不在系统执行路径。因此需要在Linux内核中支持CPU热拔插。

CPU热拔插支持的一个更新颖的用途是它在SMP的暂停恢复支持中的应用。双核和超线程支
持使得即使是笔记本电脑也能运行不支持这些方法的SMP内核。


命令行开关
==========

``maxcpus=n``
  限制启动时的CPU为 *n* 个。例如，如果你有四个CPU，使用 ``maxcpus=2`` 将只能启
  动两个。你可以选择稍后让其他CPU上线。

``nr_cpus=n``
  限制内核将支持的CPU总量。如果这里提供的数量低于实际可用的CPU数量，那么其他CPU
  以后就不能上线了。

``additional_cpus=n``
  使用它来限制可热插拔的CPU。该选项设置
  ``cpu_possible_mask = cpu_present_mask + additional_cpus``

  这个选项只限于IA64架构。

``possible_cpus=n``
  这个选项设置 ``cpu_possible_mask`` 中的 ``possible_cpus`` 位。

  这个选项只限于X86和S390架构。

``cpu0_hotplug``
  允许关闭CPU0。

  这个选项只限于X86架构。

CPU位图
=======

``cpu_possible_mask``
  系统中可能可用CPU的位图。这是用来为per_cpu变量分配一些启动时的内存，这些变量
  不会随着CPU的可用或移除而增加/减少。一旦在启动时的发现阶段被设置，该映射就是静态
  的，也就是说，任何时候都不会增加或删除任何位。根据你的系统需求提前准确地调整它
  可以节省一些启动时的内存。

``cpu_online_mask``
  当前在线的所有CPU的位图。在一个CPU可用于内核调度并准备接收设备的中断后，它被
  设置在 ``__cpu_up()`` 中。当使用 ``__cpu_disable()`` 关闭一个CPU时，它被清
  空，在此之前，所有的操作系统服务包括中断都被迁移到另一个目标CPU。

``cpu_present_mask``
  系统中当前存在的CPU的位图。它们并非全部在线。当物理热拔插被相关的子系统
  （如ACPI）处理时，可以改变和添加新的位或从位图中删除，这取决于事件是
  hot-add/hot-remove。目前还没有定死规定。典型的用法是在启动时启动拓扑结构，这时
  热插拔被禁用。

你真的不需要操作任何系统的CPU映射。在大多数情况下，它们应该是只读的。当设置每个
CPU资源时，几乎总是使用 ``cpu_possible_mask`` 或 ``for_each_possible_cpu()``
来进行迭代。宏 ``for_each_cpu()`` 可以用来迭代一个自定义的CPU掩码。

不要使用 ``cpumask_t`` 以外的任何东西来表示CPU的位图。


使用CPU热拔插
=============

内核选项 *CONFIG_HOTPLUG_CPU* 需要被启用。它目前可用于多种架构，包括ARM、MIPS、
PowerPC和X86。配置是通过sysfs接口完成的::

 $ ls -lh /sys/devices/system/cpu
 total 0
 drwxr-xr-x  9 root root    0 Dec 21 16:33 cpu0
 drwxr-xr-x  9 root root    0 Dec 21 16:33 cpu1
 drwxr-xr-x  9 root root    0 Dec 21 16:33 cpu2
 drwxr-xr-x  9 root root    0 Dec 21 16:33 cpu3
 drwxr-xr-x  9 root root    0 Dec 21 16:33 cpu4
 drwxr-xr-x  9 root root    0 Dec 21 16:33 cpu5
 drwxr-xr-x  9 root root    0 Dec 21 16:33 cpu6
 drwxr-xr-x  9 root root    0 Dec 21 16:33 cpu7
 drwxr-xr-x  2 root root    0 Dec 21 16:33 hotplug
 -r--r--r--  1 root root 4.0K Dec 21 16:33 offline
 -r--r--r--  1 root root 4.0K Dec 21 16:33 online
 -r--r--r--  1 root root 4.0K Dec 21 16:33 possible
 -r--r--r--  1 root root 4.0K Dec 21 16:33 present

文件 *offline* 、 *online* 、*possible* 、*present* 代表CPU掩码。每个CPU文件
夹包含一个 *online* 文件，控制逻辑上的开（1）和关（0）状态。要在逻辑上关闭CPU4::

 $ echo 0 > /sys/devices/system/cpu/cpu4/online
  smpboot: CPU 4 is now offline

一旦CPU被关闭，它将从 */proc/interrupts* 、*/proc/cpuinfo* 中被删除，也不应该
被 *top* 命令显示出来。要让CPU4重新上线::

 $ echo 1 > /sys/devices/system/cpu/cpu4/online
 smpboot: Booting Node 0 Processor 4 APIC 0x1

CPU又可以使用了。这应该对所有的CPU都有效。CPU0通常比较特殊，被排除在CPU热拔插之外。
在X86上，内核选项 *CONFIG_BOOTPARAM_HOTPLUG_CPU0* 必须被启用，以便能够关闭CPU0。
或者，可以使用内核命令选项 *cpu0_hotplug* 。CPU0的一些已知的依赖性:

* 从休眠/暂停中恢复。如果CPU0处于离线状态，休眠/暂停将失败。
* PIC中断。如果检测到PIC中断，CPU0就不能被移除。

如果你发现CPU0上有任何依赖性，请告知Fenghua Yu <fenghua.yu@intel.com>。

CPU的热拔插协作
===============

下线情况
--------

一旦CPU被逻辑关闭，注册的热插拔状态的清除回调将被调用，从 ``CPUHP_ONLINE`` 开始，到
``CPUHP_OFFLINE`` 状态结束。这包括:

* 如果任务因暂停操作而被冻结，那么 *cpuhp_tasks_frozen* 将被设置为true。

* 所有进程都会从这个将要离线的CPU迁移到新的CPU上。新的CPU是从每个进程的当前cpuset中
  选择的，它可能是所有在线CPU的一个子集。

* 所有针对这个CPU的中断都被迁移到新的CPU上。

* 计时器也会被迁移到新的CPU上。

* 一旦所有的服务被迁移，内核会调用一个特定的例程 ``__cpu_disable()`` 来进行特定的清
  理。

CPU热插拔API
============

CPU热拔插状态机
---------------

CPU热插拔使用一个从CPUHP_OFFLINE到CPUHP_ONLINE的线性状态空间的普通状态机。每个状态都
有一个startup和teardown的回调。

当一个CPU上线时，将按顺序调用startup回调，直到达到CPUHP_ONLINE状态。当设置状态的回调
或将实例添加到多实例状态时，也可以调用它们。

当一个CPU下线时，将按相反的顺序依次调用teardown回调，直到达到CPUHP_OFFLINE状态。当删
除状态的回调或从多实例状态中删除实例时，也可以调用它们。

如果某个使用场景只需要一个方向的热插拔操作回调（CPU上线或CPU下线），则在设置状态时，
可以将另一个不需要的回调设置为NULL。

状态空间被划分成三个阶段:

* PREPARE阶段

  PREPARE阶段涵盖了从CPUHP_OFFLINE到CPUHP_BRINGUP_CPU之间的状态空间。

  在该阶段中，startup回调在CPU上线操作启动CPU之前被调用，teardown回调在CPU下线操作使
  CPU功能失效之后被调用。

  这些回调是在控制CPU上调用的，因为它们显然不能在热插拔的CPU上运行，此时热插拔的CPU要
  么还没有启动，要么已经功能失效。

  startup回调用于设置CPU成功上线所需要的资源。teardown回调用于释放资源或在热插拔的CPU
  功能失效后，将待处理的工作转移到在线的CPU上。

  允许startup回调失败。如果回调失败，CPU上线操作被中止，CPU将再次被降到之前的状态（通
  常是CPUHP_OFFLINE）。

  本阶段中的teardown回调不允许失败。

* STARTING阶段

  STARTING阶段涵盖了CPUHP_BRINGUP_CPU + 1到CPUHP_AP_ONLINE之间的状态空间。

  该阶段中的startup回调是在早期CPU设置代码中的CPU上线操作期间，禁用中断的情况下在热拔
  插的CPU上被调用。teardown回调是在CPU完全关闭前不久的CPU下线操作期间，禁用中断的情况
  下在热拔插的CPU上被调用。

  该阶段中的回调不允许失败。

  回调用于低级别的硬件初始化/关机和核心子系统。

* ONLINE阶段

  ONLINE阶段涵盖了CPUHP_AP_ONLINE + 1到CPUHP_ONLINE之间的状态空间。

  该阶段中的startup回调是在CPU上线时在热插拔的CPU上调用的。teardown回调是在CPU下线操
  作时在热插拔CPU上调用的。

  回调是在每个CPU热插拔线程的上下文中调用的，该线程绑定在热插拔的CPU上。回调是在启用
  中断和抢占的情况下调用的。

  允许回调失败。如果回调失败，CPU热插拔操作被中止，CPU将恢复到之前的状态。

CPU 上线/下线操作
-----------------

一个成功的上线操作如下::

  [CPUHP_OFFLINE]
  [CPUHP_OFFLINE + 1]->startup()       -> 成功
  [CPUHP_OFFLINE + 2]->startup()       -> 成功
  [CPUHP_OFFLINE + 3]                  -> 略过，因为startup == NULL
  ...
  [CPUHP_BRINGUP_CPU]->startup()       -> 成功
  === PREPARE阶段结束
  [CPUHP_BRINGUP_CPU + 1]->startup()   -> 成功
  ...
  [CPUHP_AP_ONLINE]->startup()         -> 成功
  === STARTUP阶段结束
  [CPUHP_AP_ONLINE + 1]->startup()     -> 成功
  ...
  [CPUHP_ONLINE - 1]->startup()        -> 成功
  [CPUHP_ONLINE]

一个成功的下线操作如下::

  [CPUHP_ONLINE]
  [CPUHP_ONLINE - 1]->teardown()       -> 成功
  ...
  [CPUHP_AP_ONLINE + 1]->teardown()    -> 成功
  === STARTUP阶段开始
  [CPUHP_AP_ONLINE]->teardown()        -> 成功
  ...
  [CPUHP_BRINGUP_ONLINE - 1]->teardown()
  ...
  === PREPARE阶段开始
  [CPUHP_BRINGUP_CPU]->teardown()
  [CPUHP_OFFLINE + 3]->teardown()
  [CPUHP_OFFLINE + 2]                  -> 略过，因为teardown == NULL
  [CPUHP_OFFLINE + 1]->teardown()
  [CPUHP_OFFLINE]

一个失败的上线操作如下::

  [CPUHP_OFFLINE]
  [CPUHP_OFFLINE + 1]->startup()       -> 成功
  [CPUHP_OFFLINE + 2]->startup()       -> 成功
  [CPUHP_OFFLINE + 3]                  -> 略过，因为startup == NULL
  ...
  [CPUHP_BRINGUP_CPU]->startup()       -> 成功
  === PREPARE阶段结束
  [CPUHP_BRINGUP_CPU + 1]->startup()   -> 成功
  ...
  [CPUHP_AP_ONLINE]->startup()         -> 成功
  === STARTUP阶段结束
  [CPUHP_AP_ONLINE + 1]->startup()     -> 成功
  ---
  [CPUHP_AP_ONLINE + N]->startup()     -> 失败
  [CPUHP_AP_ONLINE + (N - 1)]->teardown()
  ...
  [CPUHP_AP_ONLINE + 1]->teardown()
  === STARTUP阶段开始
  [CPUHP_AP_ONLINE]->teardown()
  ...
  [CPUHP_BRINGUP_ONLINE - 1]->teardown()
  ...
  === PREPARE阶段开始
  [CPUHP_BRINGUP_CPU]->teardown()
  [CPUHP_OFFLINE + 3]->teardown()
  [CPUHP_OFFLINE + 2]                  -> 略过，因为teardown == NULL
  [CPUHP_OFFLINE + 1]->teardown()
  [CPUHP_OFFLINE]

一个失败的下线操作如下::

  [CPUHP_ONLINE]
  [CPUHP_ONLINE - 1]->teardown()       -> 成功
  ...
  [CPUHP_ONLINE - N]->teardown()       -> 失败
  [CPUHP_ONLINE - (N - 1)]->startup()
  ...
  [CPUHP_ONLINE - 1]->startup()
  [CPUHP_ONLINE]

递归失败不能被合理地处理。
请看下面的例子，由于下线操作失败而导致的递归失败::

  [CPUHP_ONLINE]
  [CPUHP_ONLINE - 1]->teardown()       -> 成功
  ...
  [CPUHP_ONLINE - N]->teardown()       -> 失败
  [CPUHP_ONLINE - (N - 1)]->startup()  -> 成功
  [CPUHP_ONLINE - (N - 2)]->startup()  -> 失败

CPU热插拔状态机在此停止，且不再尝试回滚，因为这可能会导致死循环::

  [CPUHP_ONLINE - (N - 1)]->teardown() -> 成功
  [CPUHP_ONLINE - N]->teardown()       -> 失败
  [CPUHP_ONLINE - (N - 1)]->startup()  -> 成功
  [CPUHP_ONLINE - (N - 2)]->startup()  -> 失败
  [CPUHP_ONLINE - (N - 1)]->teardown() -> 成功
  [CPUHP_ONLINE - N]->teardown()       -> 失败

周而复始，不断重复。在这种情况下，CPU留在该状态中::

  [CPUHP_ONLINE - (N - 1)]

这至少可以让系统取得进展，让用户有机会进行调试，甚至解决这个问题。

分配一个状态
------------

有两种方式分配一个CPU热插拔状态:

* 静态分配

  当子系统或驱动程序有相对于其他CPU热插拔状态的排序要求时，必须使用静态分配。例如，
  在CPU上线操作期间，PERF核心startup回调必须在PERF驱动startup回调之前被调用。在CPU
  下线操作中，驱动teardown回调必须在核心teardown回调之前调用。静态分配的状态由
  cpuhp_state枚举中的常量描述，可以在include/linux/cpuhotplug.h中找到。

  在适当的位置将状态插入枚举中，这样就满足了排序要求。状态常量必须被用于状态的设置
  和移除。

  当状态回调不是在运行时设置的，并且是kernel/cpu.c中CPU热插拔状态数组初始化的一部分
  时，也需要静态分配。

* 动态分配

  当对状态回调没有排序要求时，动态分配是首选方法。状态编号由setup函数分配，并在成功
  后返回给调用者。

  只有PREPARE和ONLINE阶段提供了一个动态分配范围。STARTING阶段则没有，因为该部分的大多
  数回调都有明确的排序要求。

CPU热插拔状态的设置
-------------------

核心代码提供了以下函数用来设置状态：

* cpuhp_setup_state(state, name, startup, teardown)
* cpuhp_setup_state_nocalls(state, name, startup, teardown)
* cpuhp_setup_state_cpuslocked(state, name, startup, teardown)
* cpuhp_setup_state_nocalls_cpuslocked(state, name, startup, teardown)

对于一个驱动程序或子系统有多个实例，并且每个实例都需要调用相同的CPU hotplug状态回
调的情况，CPU hotplug核心提供多实例支持。与驱动程序特定的实例列表相比，其优势在于
与实例相关的函数完全针对CPU hotplug操作进行序列化，并在添加和删除时提供状态回调的
自动调用。要设置这样一个多实例状态，可以使用以下函数：

* cpuhp_setup_state_multi(state, name, startup, teardown)

@state参数要么是静态分配的状态，要么是动态分配状态（PUHP_PREPARE_DYN，CPUHP_ONLINE_DYN）
的常量之一， 具体取决于应该分配动态状态的状态阶段（PREPARE，ONLINE）。

@name参数用于sysfs输出和检测。命名惯例是"subsys:mode"或"subsys/driver:mode"，
例如 "perf:mode"或"perf/x86:mode"。常见的mode名称有：

======== ============================================
prepare  对应PREPARE阶段中的状态

dead     对应PREPARE阶段中不提供startup回调的状态

starting 对应STARTING阶段中的状态

dying    对应STARTING阶段中不提供startup回调的状态

online   对应ONLINE阶段中的状态

offline  对应ONLINE阶段中不提供startup回调的状态
======== ============================================

由于@name参数只用于sysfs和检测，如果其他mode描述符比常见的描述符更好地描述状态的性质，
也可以使用。

@name参数的示例："perf/online", "perf/x86:prepare", "RCU/tree:dying", "sched/waitempty"

@startup参数是一个指向回调的函数指针，在CPU上线操作时被调用。若应用不需要startup
回调，则将该指针设为NULL。

@teardown参数是一个指向回调的函数指针，在CPU下线操作时调用。若应用不需要teardown
回调，则将该指针设为NULL。

这些函数在处理已注册回调的方式上有所不同:

  * cpuhp_setup_state_nocalls(), cpuhp_setup_state_nocalls_cpuslocked()和
    cpuhp_setup_state_multi()只注册回调。

  * cpuhp_setup_state()和cpuhp_setup_state_cpuslocked()注册回调，并对当前状态大于新
    安装状态的所有在线CPU调用@startup回调（如果不是NULL）。根据状态阶段，回调要么在
    当前的CPU上调用（PREPARE阶段），要么在CPU的热插拔线程中调用每个在线CPU（ONLINE阶段）。

    如果CPU N的回调失败，那么CPU 0...N-1的teardown回调被调用以回滚操作。状态设置失败，
    状态的回调没有被注册，在动态分配的情况下，分配的状态被释放。

状态设置和回调调用是针对CPU热拔插操作进行序列化的。如果设置函数必须从CPU热插拔的读
锁定区域调用，那么必须使用_cpuslocked()变体。这些函数不能在CPU热拔插回调中使用。

函数返回值：
  ======== ==========================================================
  0        静态分配的状态设置成功

  >0       动态分配的状态设置成功

           返回的数值是被分配的状态编号。如果状态回调后来必须被移除，
           例如模块移除，那么这个数值必须由调用者保存，并作为状态移
           除函数的@state参数。对于多实例状态，动态分配的状态编号也
           需要作为实例添加/删除操作的@state参数。

  <0	   操作失败
  ======== ==========================================================

移除CPU热拔插状态
-----------------

为了移除一个之前设置好的状态，提供了如下函数：

* cpuhp_remove_state(state)
* cpuhp_remove_state_nocalls(state)
* cpuhp_remove_state_nocalls_cpuslocked(state)
* cpuhp_remove_multi_state(state)

@state参数要么是静态分配的状态，要么是由cpuhp_setup_state*()在动态范围内分配
的状态编号。如果状态在动态范围内，则状态编号被释放，可再次进行动态分配。

这些函数在处理已注册回调的方式上有所不同:

  * cpuhp_remove_state_nocalls(), cpuhp_remove_state_nocalls_cpuslocked()
    和 cpuhp_remove_multi_state()只删除回调。

  * cpuhp_remove_state()删除回调，并调用所有当前状态大于被删除状态的在线CPU的
    teardown回调（如果不是NULL）。根据状态阶段，回调要么在当前的CPU上调用
    （PREPARE阶段），要么在CPU的热插拔线程中调用每个在线CPU（ONLINE阶段）。

    为了完成移除工作，teardown回调不能失败。

状态移除和回调调用是针对CPU热拔插操作进行序列化的。如果移除函数必须从CPU hotplug
读取锁定区域调用，那么必须使用_cpuslocked()变体。这些函数不能从CPU热插拔的回调中使用。

如果一个多实例的状态被移除，那么调用者必须先移除所有的实例。

多实例状态实例管理
------------------

一旦多实例状态被建立，实例就可以被添加到状态中：

  * cpuhp_state_add_instance(state, node)
  * cpuhp_state_add_instance_nocalls(state, node)

@state参数是一个静态分配的状态或由cpuhp_setup_state_multi()在动态范围内分配的状
态编号。

@node参数是一个指向hlist_node的指针，它被嵌入到实例的数据结构中。这个指针被交给
多实例状态的回调，可以被回调用来通过container_of()检索到实例。

这些函数在处理已注册回调的方式上有所不同:

  * cpuhp_state_add_instance_nocalls()只将实例添加到多实例状态的节点列表中。

  * cpuhp_state_add_instance()为所有当前状态大于@state的在线CPU添加实例并调用与
    @state相关的startup回调（如果不是NULL）。该回调只对将要添加的实例进行调用。
    根据状态阶段，回调要么在当前的CPU上调用（PREPARE阶段），要么在CPU的热插拔线
    程中调用每个在线CPU（ONLINE阶段）。

    如果CPU N的回调失败，那么CPU 0 ... N-1的teardown回调被调用以回滚操作，该函数
    失败，实例不会被添加到多实例状态的节点列表中。

要从状态的节点列表中删除一个实例，可以使用这些函数:

  * cpuhp_state_remove_instance(state, node)
  * cpuhp_state_remove_instance_nocalls(state, node)

参数与上述cpuhp_state_add_instance*()变体相同。

这些函数在处理已注册回调的方式上有所不同:

  * cpuhp_state_remove_instance_nocalls()只从状态的节点列表中删除实例。

  * cpuhp_state_remove_instance()删除实例并调用与@state相关的回调（如果不是NULL），
    用于所有当前状态大于@state的在线CPU。 该回调只对将要被移除的实例进行调用。
    根据状态阶段，回调要么在当前的CPU上调用（PREPARE阶段），要么在CPU的热插拔
    线程中调用每个在线CPU（ONLINE阶段）。

    为了完成移除工作，teardown回调不能失败。

节点列表的添加/删除操作和回调调用是针对CPU热拔插操作进行序列化。这些函数不能在
CPU hotplug回调和CPU hotplug读取锁定区域内使用。

样例
----

在STARTING阶段设置和取消静态分配的状态，以获取上线和下线操作的通知::

   ret = cpuhp_setup_state(CPUHP_SUBSYS_STARTING, "subsys:starting", subsys_cpu_starting, subsys_cpu_dying);
   if (ret < 0)
        return ret;
   ....
   cpuhp_remove_state(CPUHP_SUBSYS_STARTING);

在ONLINE阶段设置和取消动态分配的状态，以获取下线操作的通知::

   state = cpuhp_setup_state(CPUHP_ONLINE_DYN, "subsys:offline", NULL, subsys_cpu_offline);
   if (state < 0)
       return state;
   ....
   cpuhp_remove_state(state);

在ONLINE阶段设置和取消动态分配的状态，以获取有关上线操作的通知，而无需调用回调::

   state = cpuhp_setup_state_nocalls(CPUHP_ONLINE_DYN, "subsys:online", subsys_cpu_online, NULL);
   if (state < 0)
       return state;
   ....
   cpuhp_remove_state_nocalls(state);

在ONLINE阶段设置、使用和取消动态分配的多实例状态，以获得上线和下线操作的通知::

   state = cpuhp_setup_state_multi(CPUHP_ONLINE_DYN, "subsys:online", subsys_cpu_online, subsys_cpu_offline);
   if (state < 0)
       return state;
   ....
   ret = cpuhp_state_add_instance(state, &inst1->node);
   if (ret)
        return ret;
   ....
   ret = cpuhp_state_add_instance(state, &inst2->node);
   if (ret)
        return ret;
   ....
   cpuhp_remove_instance(state, &inst1->node);
   ....
   cpuhp_remove_instance(state, &inst2->node);
   ....
   remove_multi_state(state);

测试热拔插状态
==============

验证自定义状态是否按预期工作的一个方法是关闭一个CPU，然后再把它上线。也可以把CPU放到某
些状态（例如 ``CPUHP_AP_ONLINE`` ），然后再回到 ``CPUHP_ONLINE`` 。这将模拟在
``CPUHP_AP_ONLINE`` 之后的一个状态出现错误，从而导致回滚到在线状态。

所有注册的状态都被列举在 ``/sys/devices/system/cpu/hotplug/states`` ::

 $ tail /sys/devices/system/cpu/hotplug/states
 138: mm/vmscan:online
 139: mm/vmstat:online
 140: lib/percpu_cnt:online
 141: acpi/cpu-drv:online
 142: base/cacheinfo:online
 143: virtio/net:online
 144: x86/mce:online
 145: printk:online
 168: sched:active
 169: online

要将CPU4回滚到 ``lib/percpu_cnt:online`` ，再回到在线状态，只需发出::

  $ cat /sys/devices/system/cpu/cpu4/hotplug/state
  169
  $ echo 140 > /sys/devices/system/cpu/cpu4/hotplug/target
  $ cat /sys/devices/system/cpu/cpu4/hotplug/state
  140

需要注意的是，状态140的清除回调已经被调用。现在重新上线::

  $ echo 169 > /sys/devices/system/cpu/cpu4/hotplug/target
  $ cat /sys/devices/system/cpu/cpu4/hotplug/state
  169

启用追踪事件后，单个步骤也是可见的::

  #  TASK-PID   CPU#    TIMESTAMP  FUNCTION
  #     | |       |        |         |
      bash-394  [001]  22.976: cpuhp_enter: cpu: 0004 target: 140 step: 169 (cpuhp_kick_ap_work)
   cpuhp/4-31   [004]  22.977: cpuhp_enter: cpu: 0004 target: 140 step: 168 (sched_cpu_deactivate)
   cpuhp/4-31   [004]  22.990: cpuhp_exit:  cpu: 0004  state: 168 step: 168 ret: 0
   cpuhp/4-31   [004]  22.991: cpuhp_enter: cpu: 0004 target: 140 step: 144 (mce_cpu_pre_down)
   cpuhp/4-31   [004]  22.992: cpuhp_exit:  cpu: 0004  state: 144 step: 144 ret: 0
   cpuhp/4-31   [004]  22.993: cpuhp_multi_enter: cpu: 0004 target: 140 step: 143 (virtnet_cpu_down_prep)
   cpuhp/4-31   [004]  22.994: cpuhp_exit:  cpu: 0004  state: 143 step: 143 ret: 0
   cpuhp/4-31   [004]  22.995: cpuhp_enter: cpu: 0004 target: 140 step: 142 (cacheinfo_cpu_pre_down)
   cpuhp/4-31   [004]  22.996: cpuhp_exit:  cpu: 0004  state: 142 step: 142 ret: 0
      bash-394  [001]  22.997: cpuhp_exit:  cpu: 0004  state: 140 step: 169 ret: 0
      bash-394  [005]  95.540: cpuhp_enter: cpu: 0004 target: 169 step: 140 (cpuhp_kick_ap_work)
   cpuhp/4-31   [004]  95.541: cpuhp_enter: cpu: 0004 target: 169 step: 141 (acpi_soft_cpu_online)
   cpuhp/4-31   [004]  95.542: cpuhp_exit:  cpu: 0004  state: 141 step: 141 ret: 0
   cpuhp/4-31   [004]  95.543: cpuhp_enter: cpu: 0004 target: 169 step: 142 (cacheinfo_cpu_online)
   cpuhp/4-31   [004]  95.544: cpuhp_exit:  cpu: 0004  state: 142 step: 142 ret: 0
   cpuhp/4-31   [004]  95.545: cpuhp_multi_enter: cpu: 0004 target: 169 step: 143 (virtnet_cpu_online)
   cpuhp/4-31   [004]  95.546: cpuhp_exit:  cpu: 0004  state: 143 step: 143 ret: 0
   cpuhp/4-31   [004]  95.547: cpuhp_enter: cpu: 0004 target: 169 step: 144 (mce_cpu_online)
   cpuhp/4-31   [004]  95.548: cpuhp_exit:  cpu: 0004  state: 144 step: 144 ret: 0
   cpuhp/4-31   [004]  95.549: cpuhp_enter: cpu: 0004 target: 169 step: 145 (console_cpu_notify)
   cpuhp/4-31   [004]  95.550: cpuhp_exit:  cpu: 0004  state: 145 step: 145 ret: 0
   cpuhp/4-31   [004]  95.551: cpuhp_enter: cpu: 0004 target: 169 step: 168 (sched_cpu_activate)
   cpuhp/4-31   [004]  95.552: cpuhp_exit:  cpu: 0004  state: 168 step: 168 ret: 0
      bash-394  [005]  95.553: cpuhp_exit:  cpu: 0004  state: 169 step: 140 ret: 0

可以看到，CPU4一直下降到时间戳22.996，然后又上升到95.552。所有被调用的回调，
包括它们的返回代码都可以在跟踪中看到。

架构的要求
==========

需要具备以下功能和配置：

``CONFIG_HOTPLUG_CPU``
  这个配置项需要在Kconfig中启用

``__cpu_up()``
  调出一个cpu的架构接口

``__cpu_disable()``
  关闭CPU的架构接口，在此程序返回后，内核不能再处理任何中断。这包括定时器的关闭。

``__cpu_die()``
  这实际上是为了确保CPU的死亡。实际上，看看其他架构中实现CPU热拔插的一些示例代
  码。对于那个特定的架构，处理器被从 ``idle()`` 循环中拿下来。 ``__cpu_die()``
  通常会等待一些per_cpu状态的设置，以确保处理器的死亡例程被调用来保持活跃。

用户空间通知
============

在CPU成功上线或下线后，udev事件被发送。一个udev规则，比如::

  SUBSYSTEM=="cpu", DRIVERS=="processor", DEVPATH=="/devices/system/cpu/*", RUN+="the_hotplug_receiver.sh"

将接收所有事件。一个像这样的脚本::

  #!/bin/sh

  if [ "${ACTION}" = "offline" ]
  then
      echo "CPU ${DEVPATH##*/} offline"

  elif [ "${ACTION}" = "online" ]
  then
      echo "CPU ${DEVPATH##*/} online"

  fi

可以进一步处理该事件。

内核内联文档参考
================

该API在以下内核代码中:

include/linux/cpuhotplug.h
