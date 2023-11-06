Driver Basics
=============

Driver Entry and Exit points
----------------------------

.. kernel-doc:: include/linux/module.h
   :internal:

Driver device table
-------------------

.. kernel-doc:: include/linux/mod_devicetable.h
   :internal:
   :no-identifiers: pci_device_id


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

Reference counting
------------------

.. kernel-doc:: include/linux/refcount.h
   :internal:

.. kernel-doc:: lib/refcount.c
   :export:

Atomics
-------

.. kernel-doc:: include/linux/atomic/atomic-instrumented.h
   :internal:

.. kernel-doc:: include/linux/atomic/atomic-arch-fallback.h
   :internal:

.. kernel-doc:: include/linux/atomic/atomic-long.h
   :internal:

Kernel objects manipulation
---------------------------

.. kernel-doc:: lib/kobject.c
   :export:

Kernel utility functions
------------------------

.. kernel-doc:: include/linux/kernel.h
   :internal:
   :no-identifiers: kstrtol kstrtoul

.. kernel-doc:: kernel/printk/printk.c
   :export:
   :no-identifiers: printk

.. kernel-doc:: kernel/panic.c
   :export:

Device Resource Management
--------------------------

.. kernel-doc:: drivers/base/devres.c
   :export:

