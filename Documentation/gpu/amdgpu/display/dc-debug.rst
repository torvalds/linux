========================
Display Core Debug tools
========================

In this section, you will find helpful information on debugging the amdgpu
driver from the display perspective. This page introduces debug mechanisms and
procedures to help you identify if some issues are related to display code.

Narrow down display issues
==========================

Since the display is the driver's visual component, it is common to see users
reporting issues as a display when another component causes the problem. This
section equips users to determine if a specific issue was caused by the display
component or another part of the driver.

DC dmesg important messages
---------------------------

The dmesg log is the first source of information to be checked, and amdgpu
takes advantage of this feature by logging some valuable information. When
looking for the issues associated with amdgpu, remember that each component of
the driver (e.g., smu, PSP, dm, etc.) is loaded one by one, and this
information can be found in the dmesg log. In this sense, look for the part of
the log that looks like the below log snippet::

  [    4.254295] [drm] initializing kernel modesetting (IP DISCOVERY 0x1002:0x744C 0x1002:0x0E3B 0xC8).
  [    4.254718] [drm] register mmio base: 0xFCB00000
  [    4.254918] [drm] register mmio size: 1048576
  [    4.260095] [drm] add ip block number 0 <soc21_common>
  [    4.260318] [drm] add ip block number 1 <gmc_v11_0>
  [    4.260510] [drm] add ip block number 2 <ih_v6_0>
  [    4.260696] [drm] add ip block number 3 <psp>
  [    4.260878] [drm] add ip block number 4 <smu>
  [    4.261057] [drm] add ip block number 5 <dm>
  [    4.261231] [drm] add ip block number 6 <gfx_v11_0>
  [    4.261402] [drm] add ip block number 7 <sdma_v6_0>
  [    4.261568] [drm] add ip block number 8 <vcn_v4_0>
  [    4.261729] [drm] add ip block number 9 <jpeg_v4_0>
  [    4.261887] [drm] add ip block number 10 <mes_v11_0>

From the above example, you can see the line that reports that `<dm>`,
(**Display Manager**), was loaded, which means that display can be part of the
issue. If you do not see that line, something else might have failed before
amdgpu loads the display component, indicating that we don't have a
display issue.

After you identified that the DM was loaded correctly, you can check for the
display version of the hardware in use, which can be retrieved from the dmesg
log with the command::

  dmesg | grep -i 'display core'

This command shows a message that looks like this::

  [    4.655828] [drm] Display Core v3.2.285 initialized on DCN 3.2

This message has two key pieces of information:

* **The DC version (e.g., v3.2.285)**: Display developers release a new DC version
  every week, and this information can be advantageous in a situation where a
  user/developer must find a good point versus a bad point based on a tested
  version of the display code. Remember from page :ref:`Display Core <amdgpu-display-core>`,
  that every week the new patches for display are heavily tested with IGT and
  manual tests.
* **The DCN version (e.g., DCN 3.2)**: The DCN block is associated with the
  hardware generation, and the DCN version conveys the hardware generation that
  the driver is currently running. This information helps to narrow down the
  code debug area since each DCN version has its files in the DC folder per DCN
  component (from the example, the developer might want to focus on
  files/folders/functions/structs with the dcn32 label might be executed).
  However, keep in mind that DC reuses code across different DCN versions; for
  example, it is expected to have some callbacks set in one DCN that are the same
  as those from another DCN. In summary, use the DCN version just as a guide.

From the dmesg file, it is also possible to get the ATOM bios code by using::

  dmesg  | grep -i 'ATOM BIOS'

Which generates an output that looks like this::

  [    4.274534] amdgpu: ATOM BIOS: 113-D7020100-102

This type of information is useful to be reported.

Avoid loading display core
--------------------------

Sometimes, it might be hard to figure out which part of the driver is causing
the issue; if you suspect that the display is not part of the problem and your
bug scenario is simple (e.g., some desktop configuration) you can try to remove
the display component from the equation. First, you need to identify `dm` ID
from the dmesg log; for example, search for the following log::

  [    4.254295] [drm] initializing kernel modesetting (IP DISCOVERY 0x1002:0x744C 0x1002:0x0E3B 0xC8).
  [..]
  [    4.260095] [drm] add ip block number 0 <soc21_common>
  [    4.260318] [drm] add ip block number 1 <gmc_v11_0>
  [..]
  [    4.261057] [drm] add ip block number 5 <dm>

Notice from the above example that the `dm` id is 5 for this specific hardware.
Next, you need to run the following binary operation to identify the IP block
mask::

  0xffffffff & ~(1 << [DM ID])

From our example the IP mask is::

 0xffffffff & ~(1 << 5) = 0xffffffdf

Finally, to disable DC, you just need to set the below parameter in your
bootloader::

 amdgpu.ip_block_mask = 0xffffffdf

If you can boot your system with the DC disabled and still see the issue, it
means you can rule DC out of the equation. However, if the bug disappears, you
still need to consider the DC part of the problem and keep narrowing down the
issue. In some scenarios, disabling DC is impossible since it might be
necessary to use the display component to reproduce the issue (e.g., play a
game).

**Note: This will probably lead to the absence of a display output.**

Display flickering
------------------

Display flickering might have multiple causes; one is the lack of proper power
to the GPU or problems in the DPM switches. A good first generic verification
is to set the GPU to use high voltage::

   bash -c "echo high > /sys/class/drm/card0/device/power_dpm_force_performance_level"

The above command sets the GPU/APU to use the maximum power allowed which
disables DPM switches. If forcing DPM levels high does not fix the issue, it
is less likely that the issue is related to power management. If the issue
disappears, there is a good chance that other components might be involved, and
the display should not be ignored since this could be a DPM issues. From the
display side, if the power increase fixes the issue, it is worth debugging the
clock configuration and the pipe split police used in the specific
configuration.

Display artifacts
-----------------

Users may see some screen artifacts that can be categorized into two different
types: localized artifacts and general artifacts. The localized artifacts
happen in some specific areas, such as around the UI window corners; if you see
this type of issue, there is a considerable chance that you have a userspace
problem, likely Mesa or similar. The general artifacts usually happen on the
entire screen. They might be caused by a misconfiguration at the driver level
of the display parameters, but the userspace might also cause this issue. One
way to identify the source of the problem is to take a screenshot or make a
desktop video capture when the problem happens; after checking the
screenshot/video recording, if you don't see any of the artifacts, it means
that the issue is likely on the the driver side. If you can still see the
problem in the data collected, it is an issue that probably happened during
rendering, and the display code just got the framebuffer already corrupted.

Disabling/Enabling specific features
====================================

DC has a struct named `dc_debug_options`, which is statically initialized by
all DCE/DCN components based on the specific hardware characteristic. This
structure usually facilitates the bring-up phase since developers can start
with many disabled features and enable them individually. This is also an
important debug feature since users can change it when debugging specific
issues.

For example, dGPU users sometimes see a problem where a horizontal fillet of
flickering happens in some specific part of the screen. This could be an
indication of Sub-Viewport issues; after the users identified the target DCN,
they can set the `force_disable_subvp` field to true in the statically
initialized version of `dc_debug_options` to see if the issue gets fixed. Along
the same lines, users/developers can also try to turn off `fams2_config` and
`enable_single_display_2to1_odm_policy`. In summary, the `dc_debug_options` is
an interesting form for identifying the problem.

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

Collect Firmware information
============================

When reporting issues, it is important to have the firmware information since
it can be helpful for debugging purposes. To get all the firmware information,
use the command::

  cat /sys/kernel/debug/dri/0/amdgpu_firmware_info

From the display perspective, pay attention to the firmware of the DMCU and
DMCUB.

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
