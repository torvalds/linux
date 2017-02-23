=========================
CPU hotplug in the Kernel
=========================

:Date: December, 2016
:Author: Sebastian Andrzej Siewior <bigeasy@linutronix.de>,
          Rusty Russell <rusty@rustcorp.com.au>,
          Srivatsa Vaddagiri <vatsa@in.ibm.com>,
          Ashok Raj <ashok.raj@intel.com>,
          Joel Schopp <jschopp@austin.ibm.com>

Introduction
============

Modern advances in system architectures have introduced advanced error
reporting and correction capabilities in processors. There are couple OEMS that
support NUMA hardware which are hot pluggable as well, where physical node
insertion and removal require support for CPU hotplug.

Such advances require CPUs available to a kernel to be removed either for
provisioning reasons, or for RAS purposes to keep an offending CPU off
system execution path. Hence the need for CPU hotplug support in the
Linux kernel.

A more novel use of CPU-hotplug support is its use today in suspend resume
support for SMP. Dual-core and HT support makes even a laptop run SMP kernels
which didn't support these methods.


Command Line Switches
=====================
``maxcpus=n``
  Restrict boot time CPUs to *n*. Say if you have fourV CPUs, using
  ``maxcpus=2`` will only boot two. You can choose to bring the
  other CPUs later online.

``nr_cpus=n``
  Restrict the total amount CPUs the kernel will support. If the number
  supplied here is lower than the number of physically available CPUs than
  those CPUs can not be brought online later.

``additional_cpus=n``
  Use this to limit hotpluggable CPUs. This option sets
  ``cpu_possible_mask = cpu_present_mask + additional_cpus``

  This option is limited to the IA64 architecture.

``possible_cpus=n``
  This option sets ``possible_cpus`` bits in ``cpu_possible_mask``.

  This option is limited to the X86 and S390 architecture.

``cede_offline={"off","on"}``
  Use this option to disable/enable putting offlined processors to an extended
  ``H_CEDE`` state on supported pseries platforms. If nothing is specified,
  ``cede_offline`` is set to "on".

  This option is limited to the PowerPC architecture.

``cpu0_hotplug``
  Allow to shutdown CPU0.

  This option is limited to the X86 architecture.

CPU maps
========

``cpu_possible_mask``
  Bitmap of possible CPUs that can ever be available in the
  system. This is used to allocate some boot time memory for per_cpu variables
  that aren't designed to grow/shrink as CPUs are made available or removed.
  Once set during boot time discovery phase, the map is static, i.e no bits
  are added or removed anytime. Trimming it accurately for your system needs
  upfront can save some boot time memory.

``cpu_online_mask``
  Bitmap of all CPUs currently online. Its set in ``__cpu_up()``
  after a CPU is available for kernel scheduling and ready to receive
  interrupts from devices. Its cleared when a CPU is brought down using
  ``__cpu_disable()``, before which all OS services including interrupts are
  migrated to another target CPU.

``cpu_present_mask``
  Bitmap of CPUs currently present in the system. Not all
  of them may be online. When physical hotplug is processed by the relevant
  subsystem (e.g ACPI) can change and new bit either be added or removed
  from the map depending on the event is hot-add/hot-remove. There are currently
  no locking rules as of now. Typical usage is to init topology during boot,
  at which time hotplug is disabled.

You really don't need to manipulate any of the system CPU maps. They should
be read-only for most use. When setting up per-cpu resources almost always use
``cpu_possible_mask`` or ``for_each_possible_cpu()`` to iterate. To macro
``for_each_cpu()`` can be used to iterate over a custom CPU mask.

Never use anything other than ``cpumask_t`` to represent bitmap of CPUs.


Using CPU hotplug
=================
The kernel option *CONFIG_HOTPLUG_CPU* needs to be enabled. It is currently
available on multiple architectures including ARM, MIPS, PowerPC and X86. The
configuration is done via the sysfs interface: ::

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

The files *offline*, *online*, *possible*, *present* represent the CPU masks.
Each CPU folder contains an *online* file which controls the logical on (1) and
off (0) state. To logically shutdown CPU4: ::

 $ echo 0 > /sys/devices/system/cpu/cpu4/online
  smpboot: CPU 4 is now offline

Once the CPU is shutdown, it will be removed from */proc/interrupts*,
*/proc/cpuinfo* and should also not be shown visible by the *top* command. To
bring CPU4 back online: ::

 $ echo 1 > /sys/devices/system/cpu/cpu4/online
 smpboot: Booting Node 0 Processor 4 APIC 0x1

The CPU is usable again. This should work on all CPUs. CPU0 is often special
and excluded from CPU hotplug. On X86 the kernel option
*CONFIG_BOOTPARAM_HOTPLUG_CPU0* has to be enabled in order to be able to
shutdown CPU0. Alternatively the kernel command option *cpu0_hotplug* can be
used. Some known dependencies of CPU0:

* Resume from hibernate/suspend. Hibernate/suspend will fail if CPU0 is offline.
* PIC interrupts. CPU0 can't be removed if a PIC interrupt is detected.

Please let Fenghua Yu <fenghua.yu@intel.com> know if you find any dependencies
on CPU0.

The CPU hotplug coordination
============================

The offline case
----------------
Once a CPU has been logically shutdown the teardown callbacks of registered
hotplug states will be invoked, starting with ``CPUHP_ONLINE`` and terminating
at state ``CPUHP_OFFLINE``. This includes:

* If tasks are frozen due to a suspend operation then *cpuhp_tasks_frozen*
  will be set to true.
* All processes are migrated away from this outgoing CPU to new CPUs.
  The new CPU is chosen from each process' current cpuset, which may be
  a subset of all online CPUs.
* All interrupts targeted to this CPU are migrated to a new CPU
* timers are also migrated to a new CPU
* Once all services are migrated, kernel calls an arch specific routine
  ``__cpu_disable()`` to perform arch specific cleanup.

Using the hotplug API
---------------------
It is possible to receive notifications once a CPU is offline or onlined. This
might be important to certain drivers which need to perform some kind of setup
or clean up functions based on the number of available CPUs: ::

  #include <linux/cpuhotplug.h>

  ret = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "X/Y:online",
                          Y_online, Y_prepare_down);

*X* is the subsystem and *Y* the particular driver. The *Y_online* callback
will be invoked during registration on all online CPUs. If an error
occurs during the online callback the *Y_prepare_down* callback will be
invoked on all CPUs on which the online callback was previously invoked.
After registration completed, the *Y_online* callback will be invoked
once a CPU is brought online and *Y_prepare_down* will be invoked when a
CPU is shutdown. All resources which were previously allocated in
*Y_online* should be released in *Y_prepare_down*.
The return value *ret* is negative if an error occurred during the
registration process. Otherwise a positive value is returned which
contains the allocated hotplug for dynamically allocated states
(*CPUHP_AP_ONLINE_DYN*). It will return zero for predefined states.

The callback can be remove by invoking ``cpuhp_remove_state()``. In case of a
dynamically allocated state (*CPUHP_AP_ONLINE_DYN*) use the returned state.
During the removal of a hotplug state the teardown callback will be invoked.

Multiple instances
~~~~~~~~~~~~~~~~~~
If a driver has multiple instances and each instance needs to perform the
callback independently then it is likely that a ''multi-state'' should be used.
First a multi-state state needs to be registered: ::

  ret = cpuhp_setup_state_multi(CPUHP_AP_ONLINE_DYN, "X/Y:online,
                                Y_online, Y_prepare_down);
  Y_hp_online = ret;

The ``cpuhp_setup_state_multi()`` behaves similar to ``cpuhp_setup_state()``
except it prepares the callbacks for a multi state and does not invoke
the callbacks. This is a one time setup.
Once a new instance is allocated, you need to register this new instance: ::

  ret = cpuhp_state_add_instance(Y_hp_online, &d->node);

This function will add this instance to your previously allocated
*Y_hp_online* state and invoke the previously registered callback
(*Y_online*) on all online CPUs. The *node* element is a ``struct
hlist_node`` member of your per-instance data structure.

On removal of the instance: ::
  cpuhp_state_remove_instance(Y_hp_online, &d->node)

should be invoked which will invoke the teardown callback on all online
CPUs.

Manual setup
~~~~~~~~~~~~
Usually it is handy to invoke setup and teardown callbacks on registration or
removal of a state because usually the operation needs to performed once a CPU
goes online (offline) and during initial setup (shutdown) of the driver. However
each registration and removal function is also available with a ``_nocalls``
suffix which does not invoke the provided callbacks if the invocation of the
callbacks is not desired. During the manual setup (or teardown) the functions
``get_online_cpus()`` and ``put_online_cpus()`` should be used to inhibit CPU
hotplug operations.


The ordering of the events
--------------------------
The hotplug states are defined in ``include/linux/cpuhotplug.h``:

* The states *CPUHP_OFFLINE* … *CPUHP_AP_OFFLINE* are invoked before the
  CPU is up.
* The states *CPUHP_AP_OFFLINE* … *CPUHP_AP_ONLINE* are invoked
  just the after the CPU has been brought up. The interrupts are off and
  the scheduler is not yet active on this CPU. Starting with *CPUHP_AP_OFFLINE*
  the callbacks are invoked on the target CPU.
* The states between *CPUHP_AP_ONLINE_DYN* and *CPUHP_AP_ONLINE_DYN_END* are
  reserved for the dynamic allocation.
* The states are invoked in the reverse order on CPU shutdown starting with
  *CPUHP_ONLINE* and stopping at *CPUHP_OFFLINE*. Here the callbacks are
  invoked on the CPU that will be shutdown until *CPUHP_AP_OFFLINE*.

A dynamically allocated state via *CPUHP_AP_ONLINE_DYN* is often enough.
However if an earlier invocation during the bring up or shutdown is required
then an explicit state should be acquired. An explicit state might also be
required if the hotplug event requires specific ordering in respect to
another hotplug event.

Testing of hotplug states
=========================
One way to verify whether a custom state is working as expected or not is to
shutdown a CPU and then put it online again. It is also possible to put the CPU
to certain state (for instance *CPUHP_AP_ONLINE*) and then go back to
*CPUHP_ONLINE*. This would simulate an error one state after *CPUHP_AP_ONLINE*
which would lead to rollback to the online state.

All registered states are enumerated in ``/sys/devices/system/cpu/hotplug/states``: ::

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

To rollback CPU4 to ``lib/percpu_cnt:online`` and back online just issue: ::

  $ cat /sys/devices/system/cpu/cpu4/hotplug/state
  169
  $ echo 140 > /sys/devices/system/cpu/cpu4/hotplug/target
  $ cat /sys/devices/system/cpu/cpu4/hotplug/state
  140

It is important to note that the teardown callbac of state 140 have been
invoked. And now get back online: ::

  $ echo 169 > /sys/devices/system/cpu/cpu4/hotplug/target
  $ cat /sys/devices/system/cpu/cpu4/hotplug/state
  169

With trace events enabled, the individual steps are visible, too: ::

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

As it an be seen, CPU4 went down until timestamp 22.996 and then back up until
95.552. All invoked callbacks including their return codes are visible in the
trace.

Architecture's requirements
===========================
The following functions and configurations are required:

``CONFIG_HOTPLUG_CPU``
  This entry needs to be enabled in Kconfig

``__cpu_up()``
  Arch interface to bring up a CPU

``__cpu_disable()``
  Arch interface to shutdown a CPU, no more interrupts can be handled by the
  kernel after the routine returns. This includes the shutdown of the timer.

``__cpu_die()``
  This actually supposed to ensure death of the CPU. Actually look at some
  example code in other arch that implement CPU hotplug. The processor is taken
  down from the ``idle()`` loop for that specific architecture. ``__cpu_die()``
  typically waits for some per_cpu state to be set, to ensure the processor dead
  routine is called to be sure positively.

User Space Notification
=======================
After CPU successfully onlined or offline udev events are sent. A udev rule like: ::

  SUBSYSTEM=="cpu", DRIVERS=="processor", DEVPATH=="/devices/system/cpu/*", RUN+="the_hotplug_receiver.sh"

will receive all events. A script like: ::

  #!/bin/sh

  if [ "${ACTION}" = "offline" ]
  then
      echo "CPU ${DEVPATH##*/} offline"

  elif [ "${ACTION}" = "online" ]
  then
      echo "CPU ${DEVPATH##*/} online"

  fi

can process the event further.

Kernel Inline Documentations Reference
======================================

.. kernel-doc:: include/linux/cpuhotplug.h
