========================
Display Core Debug tools
========================

DC Debugfs
==========

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
