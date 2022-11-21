.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/core-api/cpu_hotplug.rst
:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:

 吴想成 Wu XiangCheng <bobwxc@email.cn>

.. _cn_core_api_cpu_hotplug:

=================
内核中的CPU热拔插
=================

:时间: 2016年12月
:作者: Sebastian Andrzej Siewior <bigeasy@linutronix.de>,
          Rusty Russell <rusty@rustcorp.com.au>,
          Srivatsa Vaddagiri <vatsa@in.ibm.com>,
          Ashok Raj <ashok.raj@intel.com>,
          Joel Schopp <jschopp@austin.ibm.com>

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

一旦CPU被逻辑关闭，注册的热插拔状态的清除回调将被调用，从 ``CPUHP_ONLINE`` 开始，在
``CPUHP_OFFLINE`` 状态结束。这包括:

* 如果任务因暂停操作而被冻结，那么 *cpuhp_tasks_frozen* 将被设置为true。

* 所有进程都会从这个将要离线的CPU迁移到新的CPU上。新的CPU是从每个进程的当前cpuset中
  选择的，它可能是所有在线CPU的一个子集。

* 所有针对这个CPU的中断都被迁移到新的CPU上。

* 计时器也会被迁移到新的CPU上。

* 一旦所有的服务被迁移，内核会调用一个特定的例程 ``__cpu_disable()`` 来进行特定的清
  理。

使用热插拔API
-------------

一旦一个CPU下线或上线，就有可能收到通知。这对某些需要根据可用CPU数量执行某种设置或清
理功能的驱动程序来说可能很重要::

  #include <linux/cpuhotplug.h>

  ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "X/Y:online",
                          Y_online, Y_prepare_down);

*X* 是子系统， *Y* 是特定的驱动程序。 *Y_online* 回调将在所有在线CPU的注册过程中被调用。
如果在线回调期间发生错误， *Y_prepare_down*  回调将在所有之前调用过在线回调的CPU上调
用。注册完成后，一旦有CPU上线， *Y_online* 回调将被调用，当CPU关闭时， *Y_prepare_down*
将被调用。所有之前在 *Y_online* 中分配的资源都应该在 *Y_prepare_down* 中释放。如果在
注册过程中发生错误，返回值 *ret* 为负值。否则会返回一个正值，其中包含动态分配状态
（ *CPUHP_AP_ONLINE_DYN* ）的分配热拔插。对于预定义的状态，它将返回0。

该回调可以通过调用 ``cpuhp_remove_state()`` 来删除。如果是动态分配的状态
（ *CPUHP_AP_ONLINE_DYN* ），则使用返回的状态。在移除热插拔状态的过程中，将调用拆解回调。

多个实例
~~~~~~~~

如果一个驱动程序有多个实例，并且每个实例都需要独立执行回调，那么很可能应该使用
``multi-state`` 。首先需要注册一个多状态的状态::

  ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN, "X/Y:online,
                                Y_online, Y_prepare_down);
  Y_hp_online = ret;

``cpuhp_setup_state_multi()`` 的行为与 ``cpuhp_setup_state()`` 类似，只是它
为多状态准备了回调，但不调用回调。这是一个一次性的设置。
一旦分配了一个新的实例，你需要注册这个新实例::

  ret = cpuhp_state_add_instance(Y_hp_online, &d->node);

这个函数将把这个实例添加到你先前分配的 ``Y_hp_online`` 状态，并在所有在线的
CPU上调用先前注册的回调（ ``Y_online`` ）。 *node* 元素是你的每个实例数据结构
中的一个 ``struct hlist_node`` 成员。

在移除该实例时::

  cpuhp_state_remove_instance(Y_hp_online, &d->node)

应该被调用，这将在所有在线CPU上调用拆分回调。

手动设置
~~~~~~~~

通常情况下，在注册或移除状态时调用setup和teamdown回调是很方便的，因为通常在CPU上线
（下线）和驱动的初始设置（关闭）时需要执行该操作。然而，每个注册和删除功能也有一个
_nocalls的后缀，如果不希望调用回调，则不调用所提供的回调。在手动设置（或关闭）期间，
应该使用 ``get_online_cpus()`` 和 ``put_online_cpus()`` 函数来抑制CPU热插拔操作。


事件的顺序
----------

热插拔状态被定义在 ``include/linux/cpuhotplug.h``:

* ``CPUHP_OFFLINE`` ... ``CPUHP_AP_OFFLINE`` 状态是在CPU启动前调用的。

* ``CPUHP_AP_OFFLINE`` ... ``CPUHP_AP_ONLINE`` 状态是在CPU被启动后被调用的。
  中断是关闭的，调度程序还没有在这个CPU上活动。从 ``CPUHP_AP_OFFLINE`` 开始，
  回调被调用到目标CPU上。

* ``CPUHP_AP_ONLINE_DYN`` 和 ``CPUHP_AP_ONLINE_DYN_END`` 之间的状态被保留
  给动态分配。

* 这些状态在CPU关闭时以相反的顺序调用，从 ``CPUHP_ONLINE`` 开始，在 ``CPUHP_OFFLINE``
  停止。这里的回调是在将被关闭的CPU上调用的，直到 ``CPUHP_AP_OFFLINE`` 。

通过 ``CPUHP_AP_ONLINE_DYN`` 动态分配的状态通常已经足够了。然而，如果在启动或关闭
期间需要更早的调用，那么应该获得一个显式状态。如果热拔插事件需要相对于另一个热拔插事
件的特定排序，也可能需要一个显式状态。

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
