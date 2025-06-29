.. SPDX-License-Identifier: GPL-2.0
.. _htm:

===================================
HTM (Hardware Trace Macro)
===================================

Athira Rajeev, 2 Mar 2025

.. contents::
    :depth: 3


Basic overview
==============

H_HTM is used as an interface for executing Hardware Trace Macro (HTM)
functions, including setup, configuration, control and dumping of the HTM data.
For using HTM, it is required to setup HTM buffers and HTM operations can
be controlled using the H_HTM hcall. The hcall can be invoked for any core/chip
of the system from within a partition itself. To use this feature, a debugfs
folder called "htmdump" is present under /sys/kernel/debug/powerpc.


HTM debugfs example usage
=========================

.. code-block:: sh

  #  ls /sys/kernel/debug/powerpc/htmdump/
  coreindexonchip  htmcaps  htmconfigure  htmflags  htminfo  htmsetup
  htmstart  htmstatus  htmtype  nodalchipindex  nodeindex  trace

Details on each file:

* nodeindex, nodalchipindex, coreindexonchip specifies which partition to configure the HTM for.
* htmtype: specifies the type of HTM. Supported target is hardwareTarget.
* trace: is to read the HTM data.
* htmconfigure: Configure/Deconfigure the HTM. Writing 1 to the file will configure the trace, writing 0 to the file will do deconfigure.
* htmstart: start/Stop the HTM. Writing 1 to the file will start the tracing, writing 0 to the file will stop the tracing.
* htmstatus: get the status of HTM. This is needed to understand the HTM state after each operation.
* htmsetup: set the HTM buffer size. Size of HTM buffer is in power of 2
* htminfo: provides the system processor configuration details. This is needed to understand the appropriate values for nodeindex, nodalchipindex, coreindexonchip.
* htmcaps : provides the HTM capabilities like minimum/maximum buffer size, what kind of tracing the HTM supports etc.
* htmflags : allows to pass flags to hcall. Currently supports controlling the wrapping of HTM buffer.

To see the system processor configuration details:

.. code-block:: sh

  # cat /sys/kernel/debug/powerpc/htmdump/htminfo > htminfo_file

The result can be interpreted using hexdump.

To collect HTM traces for a partition represented by nodeindex as
zero, nodalchipindex as 1 and coreindexonchip as 12

.. code-block:: sh

  # cd /sys/kernel/debug/powerpc/htmdump/
  # echo 2 > htmtype
  # echo 33 > htmsetup ( sets 8GB memory for HTM buffer, number is size in power of 2 )

This requires a CEC reboot to get the HTM buffers allocated.

.. code-block:: sh

  # cd /sys/kernel/debug/powerpc/htmdump/
  # echo 2 > htmtype
  # echo 0 > nodeindex
  # echo 1 > nodalchipindex
  # echo 12 > coreindexonchip
  # echo 1 > htmflags     # to set noWrap for HTM buffers
  # echo 1 > htmconfigure # Configure the HTM
  # echo 1 > htmstart     # Start the HTM
  # echo 0 > htmstart     # Stop the HTM
  # echo 0 > htmconfigure # Deconfigure the HTM
  # cat htmstatus         # Dump the status of HTM entries as data

Above will set the htmtype and core details, followed by executing respective HTM operation.

Read the HTM trace data
========================

After starting the trace collection, run the workload
of interest. Stop the trace collection after required period
of time, and read the trace file.

.. code-block:: sh

  # cat /sys/kernel/debug/powerpc/htmdump/trace > trace_file

This trace file will contain the relevant instruction traces
collected during the workload execution. And can be used as
input file for trace decoders to understand data.

Benefits of using HTM debugfs interface
=======================================

It is now possible to collect traces for a particular core/chip
from within any partition of the system and decode it. Through
this enablement, a small partition can be dedicated to collect the
trace data and analyze to provide important information for Performance
analysis, Software tuning, or Hardware debug.
