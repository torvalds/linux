Driver Basics
=============

Driver Entry and Exit points
----------------------------

.. kernel-doc:: include/linux/init.h
   :internal:

Atomic and pointer manipulation
-------------------------------

.. kernel-doc:: arch/x86/include/asm/atomic.h
   :internal:

Delaying, scheduling, and timer routines
----------------------------------------

.. kernel-doc:: include/linux/sched.h
   :internal:

.. kernel-doc:: kernel/sched/core.c
   :export:

.. kernel-doc:: kernel/sched/cpupri.c
   :internal:

.. kernel-doc:: kernel/sched/fair.c
   :internal:

.. kernel-doc:: include/linux/completion.h
   :internal:

.. kernel-doc:: kernel/time/timer.c
   :export:

Wait queues and Wake events
---------------------------

.. kernel-doc:: include/linux/wait.h
   :internal:

.. kernel-doc:: kernel/sched/wait.c
   :export:

High-resolution timers
----------------------

.. kernel-doc:: include/linux/ktime.h
   :internal:

.. kernel-doc:: include/linux/hrtimer.h
   :internal:

.. kernel-doc:: kernel/time/hrtimer.c
   :export:

Workqueues and Kevents
----------------------

.. kernel-doc:: include/linux/workqueue.h
   :internal:

.. kernel-doc:: kernel/workqueue.c
   :export:

Internal Functions
------------------

.. kernel-doc:: kernel/exit.c
   :internal:

.. kernel-doc:: kernel/signal.c
   :internal:

.. kernel-doc:: include/linux/kthread.h
   :internal:

.. kernel-doc:: kernel/kthread.c
   :export:

Kernel objects manipulation
---------------------------

.. kernel-doc:: lib/kobject.c
   :export:

Kernel utility functions
------------------------

.. kernel-doc:: include/linux/kernel.h
   :internal:

.. kernel-doc:: kernel/printk/printk.c
   :export:

.. kernel-doc:: kernel/panic.c
   :export:

.. kernel-doc:: kernel/sys.c
   :export:

.. kernel-doc:: kernel/rcu/srcu.c
   :export:

.. kernel-doc:: kernel/rcu/tree.c
   :export:

.. kernel-doc:: kernel/rcu/tree_plugin.h
   :export:

.. kernel-doc:: kernel/rcu/update.c
   :export:

Device Resource Management
--------------------------

.. kernel-doc:: drivers/base/devres.c
   :export:

