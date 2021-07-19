.. include:: ../../disclaimer-zh_CN.rst

:Original: :doc:`../../../../core-api/irq/irqflags-tracing`
:Translator: Yanteng Si <siyanteng@loongson.cn>

.. _cn_irqflags-tracing.rst:


=================
IRQ-flags状态追踪
=================

:Author: 最初由Ingo Molnar <mingo@redhat.com>开始撰写

“irq-flags tracing”（中断标志追踪）功能可以 “追踪” hardirq和softirq的状态，它让
感兴趣的子系统有机会了解到到内核中发生的每一个
hardirqs-off/hardirqs-on、softirqs-off/softirqs-on事件。

CONFIG_TRACE_IRQFLAGS_SUPPORT是通用锁调试代码提供的CONFIG_PROVE_SPIN_LOCKING
和CONFIG_PROVE_RW_LOCKING所需要的。否则将只有CONFIG_PROVE_MUTEX_LOCKING和
CONFIG_PROVE_RWSEM_LOCKING在一个架构上被提供--这些都是不在IRQ上下文中使用的
锁API。（rwsems的一个异常是可以解决的）

架构对这一点的支持当然不属于“微不足道”的范畴，因为很多低级的汇编代码都要处理irq-flags
的状态变化。但是一个架构可以以一种相当直接且无风险的方式启用irq-flags-tracing。

架构如果想支持这个，需要先做一些代码组织上的改变:

- 在他们的arch级Kconfig文件中添加并启用TRACE_IRQFLAGS_SUPPORT。

然后还需要做一些功能上的改变来实现对irq-flags-tracing的支持:

- 在低级入口代码中增加（构建条件）对trace_hardirqs_off()/trace_hardirqs_on()
  函数的调用。锁验证器会密切关注 “real”的irq-flags是否与 “virtual”的irq-flags
  状态相匹配，如果两者不匹配，则会发出警告（并关闭自己）。通常维护arch中
  irq-flags-track的大部分时间都是在这种状态下度过的：看看lockdep的警告，试着
  找出我们还没有搞定的汇编代码。修复并重复。一旦系统启动，并且在irq-flags跟踪功
  能中没有出现lockdep警告的情况下，arch支持就完成了。

- 如果该架构有不可屏蔽的中断，那么需要通过lockdep_off()/lockdep_on()将这些中
  断从irq跟踪[和锁验证]机制中排除。

 一般来说，在一个架构中，不完整的irq-flags-tracing实现是没有风险的：lockdep
 会检测到这一点，并将自己关闭。即锁验证器仍然可靠。应该不会因为irq-tracing的错
 误而崩溃。（除非通过修改不该修改的条件来更改汇编或寄存器而破坏其他代码）
