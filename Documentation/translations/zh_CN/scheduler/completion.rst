.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/scheduler/completion.rst

:翻译:

 司延腾 Yanteng Si <siyanteng@loongson.cn>

:校译:

 唐艺舟 Tang Yizhou <tangyeechou@gmail.com>

=======================================
完成 - "等待完成" 屏障应用程序接口(API)
=======================================

简介:
-----

如果你有一个或多个线程必须等待某些内核活动达到某个点或某个特定的状态，完成可以为这
个问题提供一个无竞争的解决方案。从语义上讲，它们有点像pthread_barrier()，并且使
用的案例类似

完成是一种代码同步机制，它比任何滥用锁/信号量和忙等待循环的行为都要好。当你想用yield()
或一些古怪的msleep(1)循环来允许其它代码继续运行时，你可能想用wait_for_completion*()
调用和completion()来代替。

使用“完成”的好处是，它们有一个良好定义、聚焦的目标，这不仅使得我们很容易理解代码的意图，
而且它们也会生成更高效的代码，因为所有线程都可以继续执行，直到真正需要结果的时刻。而且等
待和信号都高效的使用了低层调度器的睡眠/唤醒设施。

完成是建立在Linux调度器的等待队列和唤醒基础设施之上的。等待队列中的线程所等待的
事件被简化为 ``struct completion`` 中的一个简单标志，被恰如其名地称为‘done’。

由于完成与调度有关，代码可以在kernel/sched/completion.c中找到。


用法:
-----

使用完成需要三个主要部分:

 - 'struct completion' 同步对象的初始化
 - 通过调用wait_for_completion()的一个变体来实现等待部分。
 - 通过调用complete()或complete_all()实现发信端。

也有一些辅助函数用于检查完成的状态。请注意，虽然必须先做初始化，但等待和信号部分可以
按任何时间顺序出现。也就是说，一个线程在另一个线程检查是否需要等待它之前，已经将一个
完成标记为 "done"，这是完全正常的。

要使用完成API，你需要#include <linux/completion.h>并创建一个静态或动态的
``struct completion`` 类型的变量，它只有两个字段::

	struct completion {
		unsigned int done;
		wait_queue_head_t wait;
	};

结构体提供了->wait等待队列来放置任务进行等待（如果有的话），以及->done完成标志来表明它
是否完成。

完成的命名应当与正在被同步的事件名一致。一个好的例子是::

	wait_for_completion(&early_console_added);

	complete(&early_console_added);

好的、直观的命名（一如既往地）有助于代码的可读性。将一个完成命名为 ``complete``
是没有帮助的，除非其目的是超级明显的...


初始化完成:
-----------

动态分配的完成对象最好被嵌入到数据结构中，以确保在函数/驱动的生命周期内存活，以防
止与异步complete()调用发生竞争。

在使用wait_for_completion()的_timeout()或_killable()/_interruptible()变体
时应特别小心，因为必须保证在所有相关活动（complete()或reinit_completion()）发生
之前不会发生内存解除分配，即使这些等待函数由于超时或信号触发而过早返回。

动态分配的完成对象的初始化是通过调用init_completion()来完成的::

	init_completion(&dynamic_object->done);

在这个调用中，我们初始化 waitqueue 并将 ->done 设置为 0，即“not completed”或
“not done”。

重新初始化函数reinit_completion()，只是将->done字段重置为0（“not done”），而
不触及等待队列。这个函数的调用者必须确保没有任何令人讨厌的wait_for_completion()
调用在并行进行。

在同一个完成对象上调用init_completion()两次很可能是一个bug，因为它将队列重新初始
化为一个空队列，已排队的任务可能会“丢失”--在这种情况下使用reinit_completion()，但
要注意其他竞争。

对于静态声明和初始化，可以使用宏。

对于文件范围内的静态（或全局）声明，你可以使用 DECLARE_COMPLETION()::

	static DECLARE_COMPLETION(setup_done);
	DECLARE_COMPLETION(setup_done);

注意，在这种情况下，完成在启动时（或模块加载时）被初始化为“not done”，不需要调用
init_completion()。

当完成被声明为一个函数中的局部变量时，那么应该总是明确地使用
DECLARE_COMPLETION_ONSTACK()来初始化，这不仅仅是为了让lockdep正确运行，也是明确表
名它有限的使用范围是有意为之并被仔细考虑的::

	DECLARE_COMPLETION_ONSTACK(setup_done)

请注意，当使用完成对象作为局部变量时，你必须敏锐地意识到函数堆栈的短暂生命期：在所有
活动（如等待的线程）停止并且完成对象完全未被使用之前，函数不得返回到调用上下文。

再次强调这一点：特别是在使用一些具有更复杂结果的等待API变体时，比如超时或信号
（_timeout(), _killable()和_interruptible()）变体，等待可能会提前完成，而对象可
能仍在被其他线程使用 - 从wait_on_completion*()调用者函数的返回会取消分配函数栈，如
果complete()在其它某线程中完成调用，会引起微小的数据损坏。简单的测试可能不会触发这
些类型的竞争。

如果不确定的话，使用动态分配的完成对象， 最好是嵌入到其它一些生命周期长的对象中，长到
超过使用完成对象的任何辅助线程的生命周期，或者有一个锁或其他同步机制来确保complete()
不会在一个被释放的对象中调用。

在堆栈上单纯地调用DECLARE_COMPLETION()会触发一个lockdep警告。

等待完成:
---------

对于一个线程来说，要等待一些并发活动的完成，它要在初始化的完成结构体上调用
wait_for_completion()::

	void wait_for_completion(struct completion *done)

一个典型的使用场景是::

	CPU#1					CPU#2

	struct completion setup_done;

	init_completion(&setup_done);
	initialize_work(...,&setup_done,...);

	/* run non-dependent code */		/* do setup */

	wait_for_completion(&setup_done);	complete(setup_done);

这并不意味着调用wait_for_completion()和complete()有任何特定的时间顺序--如果调
用complete()发生在调用wait_for_completion()之前，那么等待方将立即继续执行，因为
所有的依赖都得到了满足；如果没有，它将阻塞，直到complete()发出完成的信号。

注意，wait_for_completion()是在调用spin_lock_irq()/spin_unlock_irq()，所以
只有当你知道中断被启用时才能安全地调用它。从IRQs-off的原子上下文中调用它将导致难以检
测的错误的中断启用。

默认行为是不带超时的等待，并将任务标记为“UNINTERRUPTIBLE”状态。wait_for_completion()
及其变体只有在进程上下文中才是安全的（因为它们可以休眠），但在原子上下文、中断上下文、IRQ
被禁用或抢占被禁用的情况下是不安全的--关于在原子/中断上下文中处理完成的问题，还请看下面的
try_wait_for_completion()。

由于wait_for_completion()的所有变体都可能（很明显）阻塞很长时间，这取决于它们所等
待的活动的性质，所以在大多数情况下，你可能不想在持有mutex锁的情况下调用它。


wait_for_completion*()可用的变体:
---------------------------------

下面的变体都会返回状态，在大多数(/所有)情况下都应该检查这个状态--在故意不检查状态的情
况下，你可能要做一个说明(例如，见arch/arm/kernel/smp.c:__cpu_up())。

一个常见的问题是不准确的返回类型赋值，所以要注意将返回值赋值给适当类型的变量。

检查返回值的具体含义也可能被发现是相当不准确的，例如，像这样的构造::

	if (!wait_for_completion_interruptible_timeout(...))

...会在成功完成和中断的情况下执行相同的代码路径--这可能不是你想要的结果::

	int wait_for_completion_interruptible(struct completion *done)

这个函数在任务等待时标记为TASK_INTERRUPTIBLE。如果在等待期间收到信号，它将返回
-ERESTARTSYS；否则为0::

	unsigned long wait_for_completion_timeout(struct completion *done, unsigned long timeout)

该任务被标记为TASK_UNINTERRUPTIBLE，并将最多超时等待“timeout”个jiffies。如果超时发生，则
返回0，否则返回剩余的时间（但至少是1）。

超时最好用msecs_to_jiffies()或usecs_to_jiffies()计算，以使代码在很大程度上不受
HZ的影响。

如果返回的超时值被故意忽略，那么注释应该解释原因
（例如，见drivers/mfd/wm8350-core.c wm8350_read_auxadc()::

	long wait_for_completion_interruptible_timeout(struct completion *done, unsigned long timeout)

这个函数传递一个以jiffies为单位的超时，并将任务标记为TASK_INTERRUPTIBLE。如果收到
信号，则返回-ERESTARTSYS；否则，如果完成超时，则返回0；如果完成了，则返回剩余的时间
（jiffies）。

更多的变体包括_killable，它使用TASK_KILLABLE作为指定的任务状态，如果它被中断，将返
回-ERESTARTSYS，如果完成了，则返回0。它也有一个_timeout变体::

	long wait_for_completion_killable(struct completion *done)
	long wait_for_completion_killable_timeout(struct completion *done, unsigned long timeout)

wait_for_completion_io()的_io变体的行为与非_io变体相同，只是将等待时间计为“IO等待”，
这对任务在调度/IO统计中的计算方式有影响::

	void wait_for_completion_io(struct completion *done)
	unsigned long wait_for_completion_io_timeout(struct completion *done, unsigned long timeout)


对完成发信号:
-------------

一个线程想要发出信号通知继续的条件已经达到，就会调用complete()，向其中一个等待者发出信
号表明它可以继续::

	void complete(struct completion *done)

... or calls complete_all() to signal all current and future waiters::

	void complete_all(struct completion *done)

即使在线程开始等待之前就发出了完成的信号，信号传递也会继续进行。这是通过等待者
“consuming”（递减）“struct completion” 的完成字段来实现的。等待的线程唤醒的顺序
与它们被排队的顺序相同（FIFO顺序）。

如果多次调用complete()，那么这将允许该数量的等待者继续进行--每次调用complete()将
简单地增加已完成的字段。但多次调用complete_all()是一个错误。complete()和
complete_all()都可以在IRQ/atomic上下文中安全调用。

在任何时候，只能有一个线程在一个特定的 “struct completion”上调用 complete() 或
complete_all() - 通过等待队列自旋锁进行序列化。任何对 complete() 或
complete_all() 的并发调用都可能是一个设计错误。

从IRQ上下文中发出完成信号 是可行的，因为它将正确地用
spin_lock_irqsave()/spin_unlock_irqrestore()执行锁操作


try_wait_for_completion()/completion_done():
--------------------------------------------

try_wait_for_completion()函数不会将线程放在等待队列中，而是在需要排队（阻塞）线
程时返回false，否则会消耗一个已发布的完成并返回true::

	bool try_wait_for_completion(struct completion *done)

最后，为了在不以任何方式改变完成的情况下检查完成的状态，可以调用completion_done()，
如果没有发布的完成尚未被等待者消耗，则返回false（意味着存在等待者），否则返回true::

	bool completion_done(struct completion *done)

try_wait_for_completion()和completion_done()都可以在IRQ或原子上下文中安全调用。
