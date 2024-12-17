.. SPDX-License-Identifier: GPL-2.0

============================================
Debugging and tracing in the media subsystem
============================================

This document serves as a starting point and lookup for debugging device
drivers in the media subsystem and to debug these drivers from userspace.

.. contents::
    :depth: 3

General debugging advice
------------------------

For general advice see the :doc:`general advice document
</process/debugging/index>`.

The following sections show you some of the available tools.

dev_debug module parameter
--------------------------

Every video device provides a ``dev_debug`` parameter, which allows to get
further insights into the IOCTLs in the background.::

  # cat /sys/class/video4linux/video3/name
  rkvdec
  # echo 0xff > /sys/class/video4linux/video3/dev_debug
  # dmesg -wH
  [...] videodev: v4l2_open: video3: open (0)
  [  +0.000036] video3: VIDIOC_QUERYCAP: driver=rkvdec, card=rkvdec,
  bus=platform:rkvdec, version=0x00060900, capabilities=0x84204000,
  device_caps=0x04204000

For the full documentation see :ref:`driver-api/media/v4l2-dev:video device
debugging`

dev_dbg() / v4l2_dbg()
----------------------

Two debug print statements, which are specific for devices and for the v4l2
subsystem, avoid adding these to your final submission unless they have
long-term value for investigations.

For a general overview please see the
:ref:`process/debugging/driver_development_debugging_guide:printk() & friends`
guide.

- Difference between both?

  - v4l2_dbg() utilizes v4l2_printk() under the hood, which further uses
    printk() directly, thus it cannot be targeted by dynamic debug
  - dev_dbg() can be targeted by dynamic debug
  - v4l2_dbg() has a more specific prefix format for the media subsystem, while
    dev_dbg only highlights the driver name and the location of the log

Dynamic debug
-------------

A method to trim down the debug output to your needs.

For general advice see the
:ref:`process/debugging/userspace_debugging_guide:dynamic debug` guide.

Here is one example, that enables all available pr_debug()'s within the file::

  $ alias ddcmd='echo $* > /proc/dynamic_debug/control'
  $ ddcmd '-p; file v4l2-h264.c +p'
  $ grep =p /proc/dynamic_debug/control
   drivers/media/v4l2-core/v4l2-h264.c:372 [v4l2_h264]print_ref_list_b =p
   "ref_pic_list_b%u (cur_poc %u%c) %s"
   drivers/media/v4l2-core/v4l2-h264.c:333 [v4l2_h264]print_ref_list_p =p
   "ref_pic_list_p (cur_poc %u%c) %s\n"

Ftrace
------

An internal kernel tracer that can trace static predefined events, function
calls, etc. Very useful for debugging problems without changing the kernel and
understanding the behavior of subsystems.

For general advice see the
:ref:`process/debugging/userspace_debugging_guide:ftrace` guide.

DebugFS
-------

This tool allows you to dump or modify internal values of your driver to files
in a custom filesystem.

For general advice see the
:ref:`process/debugging/driver_development_debugging_guide:debugfs` guide.

Perf & alternatives
-------------------

Tools to measure the various stats on a running system to diagnose issues.

For general advice see the
:ref:`process/debugging/userspace_debugging_guide:perf & alternatives` guide.

Example for media devices:

Gather statistics data for a decoding job: (This example is on a RK3399 SoC
with the rkvdec codec driver using the `fluster test suite
<https://github.com/fluendo/fluster>`__)::

  perf stat -d python3 fluster.py run -d GStreamer-H.264-V4L2SL-Gst1.0 -ts
  JVT-AVC_V1 -tv AUD_MW_E -j1
  ...
  Performance counter stats for 'python3 fluster.py run -d
  GStreamer-H.264-V4L2SL-Gst1.0 -ts JVT-AVC_V1 -tv AUD_MW_E -j1 -v':

         7794.23 msec task-clock:u                     #    0.697 CPUs utilized
               0      context-switches:u               #    0.000 /sec
               0      cpu-migrations:u                 #    0.000 /sec
           11901      page-faults:u                    #    1.527 K/sec
       882671556      cycles:u                         #    0.113 GHz                         (95.79%)
       711708695      instructions:u                   #    0.81  insn per cycle              (95.79%)
        10581935      branches:u                       #    1.358 M/sec                       (15.13%)
         6871144      branch-misses:u                  #   64.93% of all branches             (95.79%)
       281716547      L1-dcache-loads:u                #   36.144 M/sec                       (95.79%)
         9019581      L1-dcache-load-misses:u          #    3.20% of all L1-dcache accesses   (95.79%)
 <not supported>      LLC-loads:u
 <not supported>      LLC-load-misses:u

    11.180830431 seconds time elapsed

     1.502318000 seconds user
     6.377221000 seconds sys

The availability of events and metrics depends on the system you are running.

Error checking & panic analysis
-------------------------------

Various Kernel configuration options to enhance error detection of the Linux
Kernel with the cost of lowering performance.

For general advice see the
:ref:`process/debugging/driver_development_debugging_guide:kasan, ubsan,
lockdep and other error checkers` guide.

Driver verification with v4l2-compliance
----------------------------------------

To verify, that a driver adheres to the v4l2 API, the tool v4l2-compliance is
used, which is part of the `v4l_utils
<https://git.linuxtv.org/v4l-utils.git>`__, a suite of userspace tools to work
with the media subsystem.

To see the detailed media topology (and check it) use::

  v4l2-compliance -M /dev/mediaX --verbose

You can also run a full compliance check for all devices referenced in the
media topology with::

  v4l2-compliance -m /dev/mediaX

Debugging problems with receiving video
---------------------------------------

Implementing vidioc_log_status in the driver: this can log the current status
to the kernel log. It's called by v4l2-ctl --log-status. Very useful for
debugging problems with receiving video (TV/S-Video/HDMI/etc) since the video
signal is external (so unpredictable). Less useful with camera sensor inputs
since you have control over what the camera sensor does.

Usually you can just assign the default::

  .vidioc_log_status  = v4l2_ctrl_log_status,

But you can also create your own callback, to create a custom status log.

You can find an example in the cobalt driver
(`drivers/media/pci/cobalt/cobalt-v4l2.c <https://elixir.bootlin.com/linux/v6.11.6/source/drivers/media/pci/cobalt/cobalt-v4l2.c#L567>`__).

**Copyright** ©2024 : Collabora
