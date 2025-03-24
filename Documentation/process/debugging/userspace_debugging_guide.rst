.. SPDX-License-Identifier: GPL-2.0

==========================
Userspace debugging advice
==========================

This document provides a brief overview of common tools to debug the Linux
Kernel from userspace.
For debugging advice aimed at driver developers go :doc:`here
</process/debugging/driver_development_debugging_guide>`.
For general debugging advice, see :doc:`general advice document
</process/debugging/index>`.

.. contents::
    :depth: 3

The following sections show you the available tools.

Dynamic debug
-------------

Mechanism to filter what ends up in the kernel log by dis-/en-abling log
messages.

Prerequisite: ``CONFIG_DYNAMIC_DEBUG``

Dynamic debug is only able to target:

- pr_debug()
- dev_dbg()
- print_hex_dump_debug()
- print_hex_dump_bytes()

Therefore the usability of this tool is, as of now, quite limited as there is
no uniform rule for adding debug prints to the codebase, resulting in a variety
of ways these prints are implemented.

Also, note that most debug statements are implemented as a variation of
dprintk(), which have to be activated via a parameter in respective module,
dynamic debug is unable to do that step for you.

Here is one example, that enables all available pr_debug()'s within the file::

  $ alias ddcmd='echo $* > /proc/dynamic_debug/control'
  $ ddcmd '-p; file v4l2-h264.c +p'
  $ grep =p /proc/dynamic_debug/control
   drivers/media/v4l2-core/v4l2-h264.c:372 [v4l2_h264]print_ref_list_b =p
   "ref_pic_list_b%u (cur_poc %u%c) %s"
   drivers/media/v4l2-core/v4l2-h264.c:333 [v4l2_h264]print_ref_list_p =p
   "ref_pic_list_p (cur_poc %u%c) %s\n"

**When should you use this over Ftrace ?**

- When the code contains one of the valid print statements (see above) or when
  you have added multiple pr_debug() statements during development
- When timing is not an issue, meaning if multiple pr_debug() statements in
  the code won't cause delays
- When you care more about receiving specific log messages than tracing the
  pattern of how a function is called

For the full documentation see :doc:`/admin-guide/dynamic-debug-howto`

Ftrace
------

Prerequisite: ``CONFIG_DYNAMIC_FTRACE``

This tool uses the tracefs file system for the control files and output files.
That file system will be mounted as a ``tracing`` directory, which can be found
in either ``/sys/kernel/`` or ``/sys/debug/kernel/``.

Some of the most important operations for debugging are:

- You can perform a function trace by adding a function name to the
  ``set_ftrace_filter`` file (which accepts any function name found within the
  ``available_filter_functions`` file) or you can specifically disable certain
  functions by adding their names to the ``set_ftrace_notrace`` file (more info
  at: :ref:`trace/ftrace:dynamic ftrace`).
- In order to find out where calls originate from you can activate the
  ``func_stack_trace`` option under ``options/func_stack_trace``.
- Tracing the children of a function call and showing the return values are
  possible by adding the desired function in the ``set_graph_function`` file
  (requires config ``FUNCTION_GRAPH_RETVAL``); more info at
  :ref:`trace/ftrace:dynamic ftrace with the function graph tracer`.

For the full Ftrace documentation see :doc:`/trace/ftrace`

Or you could also trace for specific events by :ref:`using event tracing
<trace/events:2. using event tracing>`, which can be defined as described here:
:ref:`Creating a custom Ftrace tracepoint
<process/debugging/driver_development_debugging_guide:ftrace>`.

For the full Ftrace event tracing documentation see :doc:`/trace/events`

.. _read_ftrace_log:

Reading the ftrace log
~~~~~~~~~~~~~~~~~~~~~~

The ``trace`` file can be read just like any other file (``cat``, ``tail``,
``head``, ``vim``, etc.), the size of the file is limited by the
``buffer_size_kb`` (``echo 1000 > buffer_size_kb``). The
:ref:`trace/ftrace:trace_pipe` will behave similarly to the ``trace`` file, but
whenever you read from the file the content is consumed.

Kernelshark
~~~~~~~~~~~

A GUI interface to visualize the traces as a graph and list view from the
output of the `trace-cmd
<https://git.kernel.org/pub/scm/utils/trace-cmd/trace-cmd.git/>`__ application.

For the full documentation see `<https://kernelshark.org/Documentation.html>`__

Perf & alternatives
-------------------

The tools mentioned above provide ways to inspect kernel code, results,
variable values, etc. Sometimes you have to find out first where to look and
for those cases, a box of performance tracking tools can help you to frame the
issue.

Why should you do a performance analysis?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

A performance analysis is a good first step when among other reasons:

- you cannot define the issue
- you do not know where it occurs
- the running system should not be interrupted or it is a remote system, where
  you cannot install a new module/kernel

How to do a simple analysis with linux tools?
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

For the start of a performance analysis, you can start with the usual tools
like:

- ``top`` / ``htop`` / ``atop`` (*get an overview of the system load, see
  spikes on specific processes*)
- ``mpstat -P ALL`` (*look at the load distribution among CPUs*)
- ``iostat -x`` (*observe input and output devices utilization and performance*)
- ``vmstat`` (*overview of memory usage on the system*)
- ``pidstat`` (*similar to* ``vmstat`` *but per process, to dial it down to the
  target*)
- ``strace -tp $PID`` (*once you know the process, you can figure out how it
  communicates with the Kernel*)

These should help to narrow down the areas to look at sufficiently.

Diving deeper with perf
~~~~~~~~~~~~~~~~~~~~~~~

The **perf** tool provides a series of metrics and events to further dial down
on issues.

Prerequisite: build or install perf on your system

Gather statistics data for finding all files starting with ``gcc`` in ``/usr``::

  # perf stat -d find /usr -name 'gcc*' | wc -l

   Performance counter stats for 'find /usr -name gcc*':

     1277.81 msec    task-clock             #    0.997 CPUs utilized
     9               context-switches       #    7.043 /sec
     1               cpu-migrations         #    0.783 /sec
     704             page-faults            #  550.943 /sec
     766548897       cycles                 #    0.600 GHz                         (97.15%)
     798285467       instructions           #    1.04  insn per cycle              (97.15%)
     57582731        branches               #   45.064 M/sec                       (2.85%)
     3842573         branch-misses          #    6.67% of all branches             (97.15%)
     281616097       L1-dcache-loads        #  220.390 M/sec                       (97.15%)
     4220975         L1-dcache-load-misses  #    1.50% of all L1-dcache accesses   (97.15%)
     <not supported> LLC-loads
     <not supported> LLC-load-misses

   1.281746009 seconds time elapsed

   0.508796000 seconds user
   0.773209000 seconds sys


  52

The availability of events and metrics depends on the system you are running.

For the full documentation see
`<https://perf.wiki.kernel.org/index.php/Main_Page>`__

Perfetto
~~~~~~~~

A set of tools to measure and analyze how well applications and systems perform.
You can use it to:

* identify bottlenecks
* optimize code
* make software run faster and more efficiently.

**What is the difference between perfetto and perf?**

* perf is tool as part of and specialized for the Linux Kernel and has CLI user
  interface.
* perfetto cross-platform performance analysis stack, has extended
  functionality into userspace and provides a WEB user interface.

For the full documentation see `<https://perfetto.dev/docs/>`__

Kernel panic analysis tools
---------------------------

  To capture the crash dump please use ``Kdump`` & ``Kexec``. Below you can find
  some advice for analysing the data.

  For the full documentation see the :doc:`/admin-guide/kdump/kdump`

  In order to find the corresponding line in the code you can use `faddr2line
  <https://elixir.bootlin.com/linux/v6.11.6/source/scripts/faddr2line>`__; note
  that you need to enable ``CONFIG_DEBUG_INFO`` for that to work.

  An alternative to using ``faddr2line`` is the use of ``objdump`` (and its
  derivatives for the different platforms like ``aarch64-linux-gnu-objdump``).
  Take this line as an example:

  ``[  +0.000240]  rkvdec_device_run+0x50/0x138 [rockchip_vdec]``.

  We can find the corresponding line of code by executing::

    aarch64-linux-gnu-objdump -dS drivers/staging/media/rkvdec/rockchip-vdec.ko | grep rkvdec_device_run\>: -A 40
    0000000000000ac8 <rkvdec_device_run>:
     ac8:	d503201f 	nop
     acc:	d503201f 	nop
    {
     ad0:	d503233f 	paciasp
     ad4:	a9bd7bfd 	stp	x29, x30, [sp, #-48]!
     ad8:	910003fd 	mov	x29, sp
     adc:	a90153f3 	stp	x19, x20, [sp, #16]
     ae0:	a9025bf5 	stp	x21, x22, [sp, #32]
        const struct rkvdec_coded_fmt_desc *desc = ctx->coded_fmt_desc;
     ae4:	f9411814 	ldr	x20, [x0, #560]
        struct rkvdec_dev *rkvdec = ctx->dev;
     ae8:	f9418015 	ldr	x21, [x0, #768]
        if (WARN_ON(!desc))
     aec:	b4000654 	cbz	x20, bb4 <rkvdec_device_run+0xec>
        ret = pm_runtime_resume_and_get(rkvdec->dev);
     af0:	f943d2b6 	ldr	x22, [x21, #1952]
        ret = __pm_runtime_resume(dev, RPM_GET_PUT);
     af4:	aa0003f3 	mov	x19, x0
     af8:	52800081 	mov	w1, #0x4                   	// #4
     afc:	aa1603e0 	mov	x0, x22
     b00:	94000000 	bl	0 <__pm_runtime_resume>
        if (ret < 0) {
     b04:	37f80340 	tbnz	w0, #31, b6c <rkvdec_device_run+0xa4>
        dev_warn(rkvdec->dev, "Not good\n");
     b08:	f943d2a0 	ldr	x0, [x21, #1952]
     b0c:	90000001 	adrp	x1, 0 <rkvdec_try_ctrl-0x8>
     b10:	91000021 	add	x1, x1, #0x0
     b14:	94000000 	bl	0 <_dev_warn>
        *bad = 1;
     b18:	d2800001 	mov	x1, #0x0                   	// #0
     ...

  Meaning, in this line from the crash dump::

    [  +0.000240]  rkvdec_device_run+0x50/0x138 [rockchip_vdec]

  I can take the ``0x50`` as offset, which I have to add to the base address
  of the corresponding function, which I find in this line::

    0000000000000ac8 <rkvdec_device_run>:

  The result of ``0xac8 + 0x50 = 0xb18``
  And when I search for that address within the function I get the
  following line::

    *bad = 1;
    b18:      d2800001        mov     x1, #0x0

**Copyright** ©2024 : Collabora
