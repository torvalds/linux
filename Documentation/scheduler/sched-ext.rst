==========================
Extensible Scheduler Class
==========================

sched_ext is a scheduler class whose behavior can be defined by a set of BPF
programs - the BPF scheduler.

* sched_ext exports a full scheduling interface so that any scheduling
  algorithm can be implemented on top.

* The BPF scheduler can group CPUs however it sees fit and schedule them
  together, as tasks aren't tied to specific CPUs at the time of wakeup.

* The BPF scheduler can be turned on and off dynamically anytime.

* The system integrity is maintained no matter what the BPF scheduler does.
  The default scheduling behavior is restored anytime an error is detected,
  a runnable task stalls, or on invoking the SysRq key sequence
  `SysRq-S`.

* When the BPF scheduler triggers an error, debug information is dumped to
  aid debugging. The debug dump is passed to and printed out by the
  scheduler binary. The debug dump can also be accessed through the
  `sched_ext_dump` tracepoint. The SysRq key sequence `SysRq-D`
  triggers a debug dump. This doesn't terminate the BPF scheduler and can
  only be read through the tracepoint.

Switching to and from sched_ext
===============================

``CONFIG_SCHED_CLASS_EXT`` is the config option to enable sched_ext and
``tools/sched_ext`` contains the example schedulers. The following config
options should be enabled to use sched_ext:

.. code-block:: none

    CONFIG_BPF=y
    CONFIG_SCHED_CLASS_EXT=y
    CONFIG_BPF_SYSCALL=y
    CONFIG_BPF_JIT=y
    CONFIG_DEBUG_INFO_BTF=y
    CONFIG_BPF_JIT_ALWAYS_ON=y
    CONFIG_BPF_JIT_DEFAULT_ON=y
    CONFIG_PAHOLE_HAS_SPLIT_BTF=y
    CONFIG_PAHOLE_HAS_BTF_TAG=y

sched_ext is used only when the BPF scheduler is loaded and running.

If a task explicitly sets its scheduling policy to ``SCHED_EXT``, it will be
treated as ``SCHED_NORMAL`` and scheduled by CFS until the BPF scheduler is
loaded.

When the BPF scheduler is loaded and ``SCX_OPS_SWITCH_PARTIAL`` is not set
in ``ops->flags``, all ``SCHED_NORMAL``, ``SCHED_BATCH``, ``SCHED_IDLE``, and
``SCHED_EXT`` tasks are scheduled by sched_ext.

However, when the BPF scheduler is loaded and ``SCX_OPS_SWITCH_PARTIAL`` is
set in ``ops->flags``, only tasks with the ``SCHED_EXT`` policy are scheduled
by sched_ext, while tasks with ``SCHED_NORMAL``, ``SCHED_BATCH`` and
``SCHED_IDLE`` policies are scheduled by CFS.

Terminating the sched_ext scheduler program, triggering `SysRq-S`, or
detection of any internal error including stalled runnable tasks aborts the
BPF scheduler and reverts all tasks back to CFS.

.. code-block:: none

    # make -j16 -C tools/sched_ext
    # tools/sched_ext/build/bin/scx_simple
    local=0 global=3
    local=5 global=24
    local=9 global=44
    local=13 global=56
    local=17 global=72
    ^CEXIT: BPF scheduler unregistered

The current status of the BPF scheduler can be determined as follows:

.. code-block:: none

    # cat /sys/kernel/sched_ext/state
    enabled
    # cat /sys/kernel/sched_ext/root/ops
    simple

You can check if any BPF scheduler has ever been loaded since boot by examining
this monotonically incrementing counter (a value of zero indicates that no BPF
scheduler has been loaded):

.. code-block:: none

    # cat /sys/kernel/sched_ext/enable_seq
    1

``tools/sched_ext/scx_show_state.py`` is a drgn script which shows more
detailed information:

.. code-block:: none

    # tools/sched_ext/scx_show_state.py
    ops           : simple
    enabled       : 1
    switching_all : 1
    switched_all  : 1
    enable_state  : enabled (2)
    bypass_depth  : 0
    nr_rejected   : 0
    enable_seq    : 1

Whether a given task is on sched_ext can be determined as follows:

.. code-block:: none

    # grep ext /proc/self/sched
    ext.enabled                                  :                    1

The Basics
==========

Userspace can implement an arbitrary BPF scheduler by loading a set of BPF
programs that implement ``struct sched_ext_ops``. The only mandatory field
is ``ops.name`` which must be a valid BPF object name. All operations are
optional. The following modified excerpt is from
``tools/sched_ext/scx_simple.bpf.c`` showing a minimal global FIFO scheduler.

.. code-block:: c

    /*
     * Decide which CPU a task should be migrated to before being
     * enqueued (either at wakeup, fork time, or exec time). If an
     * idle core is found by the default ops.select_cpu() implementation,
     * then insert the task directly into SCX_DSQ_LOCAL and skip the
     * ops.enqueue() callback.
     *
     * Note that this implementation has exactly the same behavior as the
     * default ops.select_cpu implementation. The behavior of the scheduler
     * would be exactly same if the implementation just didn't define the
     * simple_select_cpu() struct_ops prog.
     */
    s32 BPF_STRUCT_OPS(simple_select_cpu, struct task_struct *p,
                       s32 prev_cpu, u64 wake_flags)
    {
            s32 cpu;
            /* Need to initialize or the BPF verifier will reject the program */
            bool direct = false;

            cpu = scx_bpf_select_cpu_dfl(p, prev_cpu, wake_flags, &direct);

            if (direct)
                    scx_bpf_dsq_insert(p, SCX_DSQ_LOCAL, SCX_SLICE_DFL, 0);

            return cpu;
    }

    /*
     * Do a direct insertion of a task to the global DSQ. This ops.enqueue()
     * callback will only be invoked if we failed to find a core to insert
     * into in ops.select_cpu() above.
     *
     * Note that this implementation has exactly the same behavior as the
     * default ops.enqueue implementation, which just dispatches the task
     * to SCX_DSQ_GLOBAL. The behavior of the scheduler would be exactly same
     * if the implementation just didn't define the simple_enqueue struct_ops
     * prog.
     */
    void BPF_STRUCT_OPS(simple_enqueue, struct task_struct *p, u64 enq_flags)
    {
            scx_bpf_dsq_insert(p, SCX_DSQ_GLOBAL, SCX_SLICE_DFL, enq_flags);
    }

    s32 BPF_STRUCT_OPS_SLEEPABLE(simple_init)
    {
            /*
             * By default, all SCHED_EXT, SCHED_OTHER, SCHED_IDLE, and
             * SCHED_BATCH tasks should use sched_ext.
             */
            return 0;
    }

    void BPF_STRUCT_OPS(simple_exit, struct scx_exit_info *ei)
    {
            exit_type = ei->type;
    }

    SEC(".struct_ops")
    struct sched_ext_ops simple_ops = {
            .select_cpu             = (void *)simple_select_cpu,
            .enqueue                = (void *)simple_enqueue,
            .init                   = (void *)simple_init,
            .exit                   = (void *)simple_exit,
            .name                   = "simple",
    };

Dispatch Queues
---------------

To match the impedance between the scheduler core and the BPF scheduler,
sched_ext uses DSQs (dispatch queues) which can operate as both a FIFO and a
priority queue. By default, there is one global FIFO (``SCX_DSQ_GLOBAL``),
and one local dsq per CPU (``SCX_DSQ_LOCAL``). The BPF scheduler can manage
an arbitrary number of dsq's using ``scx_bpf_create_dsq()`` and
``scx_bpf_destroy_dsq()``.

A CPU always executes a task from its local DSQ. A task is "inserted" into a
DSQ. A task in a non-local DSQ is "move"d into the target CPU's local DSQ.

When a CPU is looking for the next task to run, if the local DSQ is not
empty, the first task is picked. Otherwise, the CPU tries to move a task
from the global DSQ. If that doesn't yield a runnable task either,
``ops.dispatch()`` is invoked.

Scheduling Cycle
----------------

The following briefly shows how a waking task is scheduled and executed.

1. When a task is waking up, ``ops.select_cpu()`` is the first operation
   invoked. This serves two purposes. First, CPU selection optimization
   hint. Second, waking up the selected CPU if idle.

   The CPU selected by ``ops.select_cpu()`` is an optimization hint and not
   binding. The actual decision is made at the last step of scheduling.
   However, there is a small performance gain if the CPU
   ``ops.select_cpu()`` returns matches the CPU the task eventually runs on.

   A side-effect of selecting a CPU is waking it up from idle. While a BPF
   scheduler can wake up any cpu using the ``scx_bpf_kick_cpu()`` helper,
   using ``ops.select_cpu()`` judiciously can be simpler and more efficient.

   A task can be immediately inserted into a DSQ from ``ops.select_cpu()``
   by calling ``scx_bpf_dsq_insert()``. If the task is inserted into
   ``SCX_DSQ_LOCAL`` from ``ops.select_cpu()``, it will be inserted into the
   local DSQ of whichever CPU is returned from ``ops.select_cpu()``.
   Additionally, inserting directly from ``ops.select_cpu()`` will cause the
   ``ops.enqueue()`` callback to be skipped.

   Note that the scheduler core will ignore an invalid CPU selection, for
   example, if it's outside the allowed cpumask of the task.

2. Once the target CPU is selected, ``ops.enqueue()`` is invoked (unless the
   task was inserted directly from ``ops.select_cpu()``). ``ops.enqueue()``
   can make one of the following decisions:

   * Immediately insert the task into either the global or a local DSQ by
     calling ``scx_bpf_dsq_insert()`` with one of the following options:
     ``SCX_DSQ_GLOBAL``, ``SCX_DSQ_LOCAL``, or ``SCX_DSQ_LOCAL_ON | cpu``.

   * Immediately insert the task into a custom DSQ by calling
     ``scx_bpf_dsq_insert()`` with a DSQ ID which is smaller than 2^63.

   * Queue the task on the BPF side.

3. When a CPU is ready to schedule, it first looks at its local DSQ. If
   empty, it then looks at the global DSQ. If there still isn't a task to
   run, ``ops.dispatch()`` is invoked which can use the following two
   functions to populate the local DSQ.

   * ``scx_bpf_dsq_insert()`` inserts a task to a DSQ. Any target DSQ can be
     used - ``SCX_DSQ_LOCAL``, ``SCX_DSQ_LOCAL_ON | cpu``,
     ``SCX_DSQ_GLOBAL`` or a custom DSQ. While ``scx_bpf_dsq_insert()``
     currently can't be called with BPF locks held, this is being worked on
     and will be supported. ``scx_bpf_dsq_insert()`` schedules insertion
     rather than performing them immediately. There can be up to
     ``ops.dispatch_max_batch`` pending tasks.

   * ``scx_bpf_move_to_local()`` moves a task from the specified non-local
     DSQ to the dispatching DSQ. This function cannot be called with any BPF
     locks held. ``scx_bpf_move_to_local()`` flushes the pending insertions
     tasks before trying to move from the specified DSQ.

4. After ``ops.dispatch()`` returns, if there are tasks in the local DSQ,
   the CPU runs the first one. If empty, the following steps are taken:

   * Try to move from the global DSQ. If successful, run the task.

   * If ``ops.dispatch()`` has dispatched any tasks, retry #3.

   * If the previous task is an SCX task and still runnable, keep executing
     it (see ``SCX_OPS_ENQ_LAST``).

   * Go idle.

Note that the BPF scheduler can always choose to dispatch tasks immediately
in ``ops.enqueue()`` as illustrated in the above simple example. If only the
built-in DSQs are used, there is no need to implement ``ops.dispatch()`` as
a task is never queued on the BPF scheduler and both the local and global
DSQs are executed automatically.

``scx_bpf_dsq_insert()`` inserts the task on the FIFO of the target DSQ. Use
``scx_bpf_dsq_insert_vtime()`` for the priority queue. Internal DSQs such as
``SCX_DSQ_LOCAL`` and ``SCX_DSQ_GLOBAL`` do not support priority-queue
dispatching, and must be dispatched to with ``scx_bpf_dsq_insert()``. See
the function documentation and usage in ``tools/sched_ext/scx_simple.bpf.c``
for more information.

Task Lifecycle
--------------

The following pseudo-code summarizes the entire lifecycle of a task managed
by a sched_ext scheduler:

.. code-block:: c

    ops.init_task();            /* A new task is created */
    ops.enable();               /* Enable BPF scheduling for the task */

    while (task in SCHED_EXT) {
        if (task can migrate)
            ops.select_cpu();   /* Called on wakeup (optimization) */

        ops.runnable();         /* Task becomes ready to run */

        while (task is runnable) {
            if (task is not in a DSQ) {
                ops.enqueue();  /* Task can be added to a DSQ */

                /* A CPU becomes available */

                ops.dispatch(); /* Task is moved to a local DSQ */
            }
            ops.running();      /* Task starts running on its assigned CPU */
            ops.tick();         /* Called every 1/HZ seconds */
            ops.stopping();     /* Task stops running (time slice expires or wait) */
        }

        ops.quiescent();        /* Task releases its assigned CPU (wait) */
    }

    ops.disable();              /* Disable BPF scheduling for the task */
    ops.exit_task();            /* Task is destroyed */

Where to Look
=============

* ``include/linux/sched/ext.h`` defines the core data structures, ops table
  and constants.

* ``kernel/sched/ext.c`` contains sched_ext core implementation and helpers.
  The functions prefixed with ``scx_bpf_`` can be called from the BPF
  scheduler.

* ``tools/sched_ext/`` hosts example BPF scheduler implementations.

  * ``scx_simple[.bpf].c``: Minimal global FIFO scheduler example using a
    custom DSQ.

  * ``scx_qmap[.bpf].c``: A multi-level FIFO scheduler supporting five
    levels of priority implemented with ``BPF_MAP_TYPE_QUEUE``.

ABI Instability
===============

The APIs provided by sched_ext to BPF schedulers programs have no stability
guarantees. This includes the ops table callbacks and constants defined in
``include/linux/sched/ext.h``, as well as the ``scx_bpf_`` kfuncs defined in
``kernel/sched/ext.c``.

While we will attempt to provide a relatively stable API surface when
possible, they are subject to change without warning between kernel
versions.
