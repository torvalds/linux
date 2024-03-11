========================
Display Core Debug tools
========================

DC Visual Confirmation
======================

Display core provides a feature named visual confirmation, which is a set of
bars added at the scanout time by the driver to convey some specific
information. In general, you can enable this debug option by using::

  echo <N> > /sys/kernel/debug/dri/0/amdgpu_dm_visual_confirm

Where `N` is an integer number for some specific scenarios that the developer
wants to enable, you will see some of these debug cases in the following
subsection.

Multiple Planes Debug
---------------------

If you want to enable or debug multiple planes in a specific user-space
application, you can leverage a debug feature named visual confirm. For
enabling it, you will need::

  echo 1 > /sys/kernel/debug/dri/0/amdgpu_dm_visual_confirm

You need to reload your GUI to see the visual confirmation. When the plane
configuration changes or a full update occurs there will be a colored bar at
the bottom of each hardware plane being drawn on the screen.

* The color indicates the format - For example, red is AR24 and green is NV12
* The height of the bar indicates the index of the plane
* Pipe split can be observed if there are two bars with a difference in height
  covering the same plane

Consider the video playback case in which a video is played in a specific
plane, and the desktop is drawn in another plane. The video plane should
feature one or two green bars at the bottom of the video depending on pipe
split configuration.

* There should **not** be any visual corruption
* There should **not** be any underflow or screen flashes
* There should **not** be any black screens
* There should **not** be any cursor corruption
* Multiple plane **may** be briefly disabled during window transitions or
  resizing but should come back after the action has finished

Pipe Split Debug
----------------

Sometimes we need to debug if DCN is splitting pipes correctly, and visual
confirmation is also handy for this case. Similar to the MPO case, you can use
the below command to enable visual confirmation::

  echo 1 > /sys/kernel/debug/dri/0/amdgpu_dm_visual_confirm

In this case, if you have a pipe split, you will see one small red bar at the
bottom of the display covering the entire display width and another bar
covering the second pipe. In other words, you will see a bit high bar in the
second pipe.

DTN Debug
=========

DC (DCN) provides an extensive log that dumps multiple details from our
hardware configuration. Via debugfs, you can capture those status values by
using Display Test Next (DTN) log, which can be captured via debugfs by using::

  cat /sys/kernel/debug/dri/0/amdgpu_dm_dtn_log

Since this log is updated accordingly with DCN status, you can also follow the
change in real-time by using something like::

  sudo watch -d cat /sys/kernel/debug/dri/0/amdgpu_dm_dtn_log

When reporting a bug related to DC, consider attaching this log before and
after you reproduce the bug.

DMUB Firmware Debug
===================

Sometimes, dmesg logs aren't enough. This is especially true if a feature is
implemented primarily in DMUB firmware. In such cases, all we see in dmesg when
an issue arises is some generic timeout error. So, to get more relevant
information, we can trace DMUB commands by enabling the relevant bits in
`amdgpu_dm_dmub_trace_mask`.

Currently, we support the tracing of the following groups:

Trace Groups
------------

.. csv-table::
   :header-rows: 1
   :widths: 1, 1
   :file: ./trace-groups-table.csv

**Note: Not all ASICs support all of the listed trace groups**

So, to enable just PSR tracing you can use the following command::

  # echo 0x8020 > /sys/kernel/debug/dri/0/amdgpu_dm_dmub_trace_mask

Then, you need to enable logging trace events to the buffer, which you can do
using the following::

  # echo 1 > /sys/kernel/debug/dri/0/amdgpu_dm_dmcub_trace_event_en

Lastly, after you are able to reproduce the issue you are trying to debug,
you can disable tracing and read the trace log by using the following::

  # echo 0 > /sys/kernel/debug/dri/0/amdgpu_dm_dmcub_trace_event_en
  # cat /sys/kernel/debug/dri/0/amdgpu_dm_dmub_tracebuffer

So, when reporting bugs related to features such as PSR and ABM, consider
enabling the relevant bits in the mask before reproducing the issue and
attach the log that you obtain from the trace buffer in any bug reports that you
create.
