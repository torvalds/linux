.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/scheduler/sched-arch.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:



===============================
架构特定代码的CPU调度器实现提示
===============================

	Nick Piggin, 2005

上下文切换
==========
1. 运行队列锁
默认情况下，switch_to arch函数在调用时锁定了运行队列。这通常不是一个问题，除非
switch_to可能需要获取运行队列锁。这通常是由于上下文切换中的唤醒操作造成的。见
arch/ia64/include/asm/switch_to.h的例子。

为了要求调度器在运行队列解锁的情况下调用switch_to，你必须在头文件
中`#define __ARCH_WANT_UNLOCKED_CTXSW`(通常是定义switch_to的那个文件）。

在CONFIG_SMP的情况下，解锁的上下文切换对核心调度器的实现只带来了非常小的性能损
失。

CPU空转
=======
你的cpu_idle程序需要遵守以下规则：

1. 现在抢占应该在空闲的例程上禁用。应该只在调用schedule()时启用，然后再禁用。

2. need_resched/TIF_NEED_RESCHED 只会被设置，并且在运行任务调用 schedule()
   之前永远不会被清除。空闲线程只需要查询need_resched，并且永远不会设置或清除它。

3. 当cpu_idle发现（need_resched() == 'true'），它应该调用schedule()。否则
   它不应该调用schedule()。

4. 在检查need_resched时，唯一需要禁用中断的情况是，我们要让处理器休眠到下一个中
   断（这并不对need_resched提供任何保护，它可以防止丢失一个中断）:

	4a. 这种睡眠类型的常见问题似乎是::

	        local_irq_disable();
	        if (!need_resched()) {
	                local_irq_enable();
	                *** resched interrupt arrives here ***
	                __asm__("sleep until next interrupt");
	        }

5. 当need_resched变为高电平时，TIF_POLLING_NRFLAG可以由不需要中断来唤醒它们
   的空闲程序设置。换句话说，它们必须定期轮询need_resched，尽管做一些后台工作或
   进入低CPU优先级可能是合理的。

      - 5a. 如果TIF_POLLING_NRFLAG被设置，而我们确实决定进入一个中断睡眠，那
            么需要清除它，然后发出一个内存屏障（接着测试need_resched，禁用中断，如3中解释）。

arch/x86/kernel/process.c有轮询和睡眠空闲函数的例子。


可能出现的arch/问题
===================

我发现的可能的arch问题（并试图解决或没有解决）。:

ia64 - safe_halt的调用与中断相比，是否很荒谬？ (它睡眠了吗) (参考 #4a)

sparc - 在这一点上，IRQ是开着的（？），把local_irq_save改为_disable。
      - 待办事项: 需要第二个CPU来禁用抢占 (参考 #1)
