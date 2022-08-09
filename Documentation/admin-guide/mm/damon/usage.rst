.. SPDX-License-Identifier: GPL-2.0

===============
Detailed Usages
===============

DAMON provides below three interfaces for different users.

- *DAMON user space tool.*
  This is for privileged people such as system administrators who want a
  just-working human-friendly interface.  Using this, users can use the DAMON’s
  major features in a human-friendly way.  It may not be highly tuned for
  special cases, though.  It supports only virtual address spaces monitoring.
- *debugfs interface.*
  This is for privileged user space programmers who want more optimized use of
  DAMON.  Using this, users can use DAMON’s major features by reading
  from and writing to special debugfs files.  Therefore, you can write and use
  your personalized DAMON debugfs wrapper programs that reads/writes the
  debugfs files instead of you.  The DAMON user space tool is also a reference
  implementation of such programs.  It supports only virtual address spaces
  monitoring.
- *Kernel Space Programming Interface.*
  This is for kernel space programmers.  Using this, users can utilize every
  feature of DAMON most flexibly and efficiently by writing kernel space
  DAMON application programs for you.  You can even extend DAMON for various
  address spaces.

Nevertheless, you could write your own user space tool using the debugfs
interface.  A reference implementation is available at
https://github.com/awslabs/damo.  If you are a kernel programmer, you could
refer to :doc:`/vm/damon/api` for the kernel space programming interface.  For
the reason, this document describes only the debugfs interface

debugfs Interface
=================

DAMON exports three files, ``attrs``, ``target_ids``, and ``monitor_on`` under
its debugfs directory, ``<debugfs>/damon/``.


Attributes
----------

Users can get and set the ``sampling interval``, ``aggregation interval``,
``regions update interval``, and min/max number of monitoring target regions by
reading from and writing to the ``attrs`` file.  To know about the monitoring
attributes in detail, please refer to the :doc:`/vm/damon/design`.  For
example, below commands set those values to 5 ms, 100 ms, 1,000 ms, 10 and
1000, and then check it again::

    # cd <debugfs>/damon
    # echo 5000 100000 1000000 10 1000 > attrs
    # cat attrs
    5000 100000 1000000 10 1000


Target IDs
----------

Some types of address spaces supports multiple monitoring target.  For example,
the virtual memory address spaces monitoring can have multiple processes as the
monitoring targets.  Users can set the targets by writing relevant id values of
the targets to, and get the ids of the current targets by reading from the
``target_ids`` file.  In case of the virtual address spaces monitoring, the
values should be pids of the monitoring target processes.  For example, below
commands set processes having pids 42 and 4242 as the monitoring targets and
check it again::

    # cd <debugfs>/damon
    # echo 42 4242 > target_ids
    # cat target_ids
    42 4242

Note that setting the target ids doesn't start the monitoring.


Turning On/Off
--------------

Setting the files as described above doesn't incur effect unless you explicitly
start the monitoring.  You can start, stop, and check the current status of the
monitoring by writing to and reading from the ``monitor_on`` file.  Writing
``on`` to the file starts the monitoring of the targets with the attributes.
Writing ``off`` to the file stops those.  DAMON also stops if every target
process is terminated.  Below example commands turn on, off, and check the
status of DAMON::

    # cd <debugfs>/damon
    # echo on > monitor_on
    # echo off > monitor_on
    # cat monitor_on
    off

Please note that you cannot write to the above-mentioned debugfs files while
the monitoring is turned on.  If you write to the files while DAMON is running,
an error code such as ``-EBUSY`` will be returned.


Tracepoint for Monitoring Results
=================================

DAMON provides the monitoring results via a tracepoint,
``damon:damon_aggregated``.  While the monitoring is turned on, you could
record the tracepoint events and show results using tracepoint supporting tools
like ``perf``.  For example::

    # echo on > monitor_on
    # perf record -e damon:damon_aggregated &
    # sleep 5
    # kill 9 $(pidof perf)
    # echo off > monitor_on
    # perf script
