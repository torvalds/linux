.. SPDX-License-Identifier: GPL-2.0
.. include:: ../disclaimer-zh_CN.rst

:Original: Documentation/locking/mutex-design.rst

:翻译:

  唐艺舟 Tang Yizhou <tangyeechou@gmail.com>

================
通用互斥锁子系统
================

:初稿:

  Ingo Molnar <mingo@redhat.com>

:更新:

  Davidlohr Bueso <davidlohr@hp.com>

什么是互斥锁？
--------------

在Linux内核中，互斥锁（mutex）指的是一个特殊的加锁原语，它在共享内存系统上
强制保证序列化，而不仅仅是指在学术界或类似的理论教科书中出现的通用术语“相互
排斥”。互斥锁是一种睡眠锁，它的行为类似于二进制信号量（semaphores），在
2006年被引入时[1]，作为后者的替代品。这种新的数据结构提供了许多优点，包括更
简单的接口，以及在当时更少的代码量（见缺陷）。

[1] https://lwn.net/Articles/164802/

实现
----

互斥锁由“struct mutex”表示，在include/linux/mutex.h中定义，并在
kernel/locking/mutex.c中实现。这些锁使用一个原子变量（->owner）来跟踪
它们生命周期内的锁状态。字段owner实际上包含的是指向当前锁所有者的
`struct task_struct *` 指针，因此如果无人持有锁，则它的值为空（NULL）。
由于task_struct的指针至少按L1_CACHE_BYTES对齐，低位（3）被用来存储额外
的状态（例如，等待者列表非空）。在其最基本的形式中，它还包括一个等待队列和
一个确保对其序列化访问的自旋锁。此外，CONFIG_MUTEX_SPIN_ON_OWNER=y的
系统使用一个自旋MCS锁（->osq，译注：MCS是两个人名的合并缩写），在下文的
（ii）中描述。

准备获得一把自旋锁时，有三种可能经过的路径，取决于锁的状态：

(i) 快速路径：试图通过调用cmpxchg()修改锁的所有者为当前任务，以此原子化地
    获取锁。这只在无竞争的情况下有效（cmpxchg()检查值是否为0，所以3个状态
    比特必须为0）。如果锁处在竞争状态，代码进入下一个可能的路径。

(ii) 中速路径：也就是乐观自旋，当锁的所有者正在运行并且没有其它优先级更高的
     任务（need_resched，需要重新调度）准备运行时，当前任务试图自旋来获得
     锁。原理是，如果锁的所有者正在运行，它很可能不久就会释放锁。互斥锁自旋体
     使用MCS锁排队，这样只有一个自旋体可以竞争互斥锁。

     MCS锁（由Mellor-Crummey和Scott提出）是一个简单的自旋锁，它具有一些
     理想的特性，比如公平，以及每个CPU在试图获得锁时在一个本地变量上自旋。
     它避免了常见的“检测-设置”自旋锁实现导致的（CPU核间）缓存行回弹
     （cacheline bouncing）这种昂贵的开销。一个类MCS锁是为实现睡眠锁的
     乐观自旋而专门定制的。这种定制MCS锁的一个重要特性是，它有一个额外的属性，
     当自旋体需要重新调度时，它们能够退出MCS自旋锁队列。这进一步有助于避免
     以下场景：需要重新调度的MCS自旋体将继续自旋等待自旋体所有者，即将获得
     MCS锁时却直接进入慢速路径。

(iii) 慢速路径：最后的手段，如果仍然无法获得锁，该任务会被添加到等待队列中，
      休眠直到被解锁路径唤醒。在通常情况下，它以TASK_UNINTERRUPTIBLE状态
      阻塞。

虽然从形式上看，内核互斥锁是可睡眠的锁，路径(ii)使它实际上成为混合类型。通过
简单地不中断一个任务并忙着等待几个周期，而不是立即睡眠，这种锁已经被认为显著
改善一些工作负载的性能。注意，这种技术也被用于读写信号量（rw-semaphores）。

语义
----

互斥锁子系统检查并强制执行以下规则:

    - 每次只有一个任务可以持有该互斥锁。
    - 只有锁的所有者可以解锁该互斥锁。
    - 不允许多次解锁。
    - 不允许递归加锁/解锁。
    - 互斥锁只能通过API进行初始化（见下文）。
    - 一个任务不能在持有互斥锁的情况下退出。
    - 持有锁的内存区域不得被释放。
    - 被持有的锁不能被重新初始化。
    - 互斥锁不能用于硬件或软件中断上下文，如小任务（tasklet）和定时器。

当CONFIG DEBUG_MUTEXES被启用时，这些语义将被完全强制执行。此外，互斥锁
调试代码还实现了一些其它特性，使锁的调试更容易、更快速：

    - 当打印到调试输出时，总是使用互斥锁的符号名称。
    - 加锁点跟踪，函数名符号化查找，系统持有的全部锁的列表，打印出它们。
    - 所有者跟踪。
    - 检测自我递归的锁并打印所有相关信息。
    - 检测多任务环形依赖死锁，并打印所有受影响的锁和任务（并且只限于这些任务）。


接口
----
静态定义互斥锁::

   DEFINE_MUTEX(name);

动态初始化互斥锁::

   mutex_init(mutex);

以不可中断方式（uninterruptible）获取互斥锁::

   void mutex_lock(struct mutex *lock);
   void mutex_lock_nested(struct mutex *lock, unsigned int subclass);
   int  mutex_trylock(struct mutex *lock);

以可中断方式（interruptible）获取互斥锁::

   int mutex_lock_interruptible_nested(struct mutex *lock,
				       unsigned int subclass);
   int mutex_lock_interruptible(struct mutex *lock);

当原子变量减为0时，以可中断方式（interruptible）获取互斥锁::

   int atomic_dec_and_mutex_lock(atomic_t *cnt, struct mutex *lock);

释放互斥锁::

   void mutex_unlock(struct mutex *lock);

检测是否已经获取互斥锁::

   int mutex_is_locked(struct mutex *lock);

缺陷
----

与它最初的设计和目的不同，'struct mutex' 是内核中最大的锁之一。例如：在
x86-64上它是32字节，而 'struct semaphore' 是24字节，rw_semaphore是
40字节。更大的结构体大小意味着更多的CPU缓存和内存占用。


何时使用互斥锁
--------------

总是优先选择互斥锁而不是任何其它锁原语，除非互斥锁的严格语义不合适，和/或临界区
阻止锁被共享。
