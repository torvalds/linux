.. _display_todos:

==============================
AMDGPU - Display Contributions
==============================

First of all, if you are here, you probably want to give some technical
contribution to the display code, and for that, we say thank you :)

This page summarizes some of the issues you can help with; keep in mind that
this is a static page, and it is always a good idea to try to reach developers
in the amdgfx or some of the maintainers. Finally, this page follows the DRM
way of creating a TODO list; for more information, check
'Documentation/gpu/todo.rst'.

Gitlab issues
=============

Users can report issues associated with AMD GPUs at:

- https://gitlab.freedesktop.org/drm/amd

Usually, we try to add a proper label to all new tickets to make it easy to
filter issues. If you can reproduce any problem, you could help by adding more
information or fixing the issue.

Level: diverse

IGT
===

`IGT`_ provides many integration tests that can be run on your GPU. We always
want to pass a large set of tests to increase the test coverage in our CI. If
you wish to contribute to the display code but are unsure where a good place
is, we recommend you run all IGT tests and try to fix any failure you see in
your hardware. Keep in mind that this failure can be an IGT problem or a kernel
issue; it is necessary to analyze case-by-case.

Level: diverse

.. _IGT: https://gitlab.freedesktop.org/drm/igt-gpu-tools

Compilation
===========

Fix compilation warnings
------------------------

Enable the W1 or W2 warning level in the kernel compilation and try to fix the
issues on the display side.

Level: Starter

Fix compilation issues when using um architecture
-------------------------------------------------

Linux has a User-mode Linux (UML) feature, and the kernel can be compiled to
the **um** architecture. Compiling for **um** can bring multiple advantages
from the test perspective. We currently have some compilation issues in this
area that we need to fix.

Level: Intermediate

Code Refactor
=============

Add prefix to DC functions to improve the debug with ftrace
-----------------------------------------------------------

The Ftrace debug feature (check 'Documentation/trace/ftrace.rst') is a
fantastic way to check the code path when developers try to make sense of a
bug. Ftrace provides a filter mechanism that can be useful when the developer
has some hunch of which part of the code can cause the issue; for this reason,
if a set of functions has a proper prefix, it becomes easy to create a good
filter. Additionally, prefixes can improve stack trace readability.

The DC code does not follow some prefix rules, which makes the Ftrace filter
more complicated and reduces the readability of the stack trace. If you want
something simple to start contributing to the display, you can make patches for
adding prefixes to DC functions. To create those prefixes, use part of the file
name as a prefix for all functions in the target file. Check the
'amdgpu_dm_crtc.c` and `amdgpu_dm_plane.c` for some references. However, we
strongly advise not to send huge patches changing these prefixes; otherwise, it
will be hard to review and test, which can generate second thoughts from
maintainers. Try small steps; in case of double, you can ask before you put in
effort. We recommend first looking at folders like dceXYZ, dcnXYZ, basics,
bios, core, clk_mgr, hwss, resource, and irq.

Level: Starter

Reduce code duplication
-----------------------

AMD has an extensive portfolio with various dGPUs and APUs that amdgpu
supports. To maintain the new hardware release cadence, DCE/DCN was designed in
a modular design, making the bring-up for new hardware fast. Over the years,
amdgpu accumulated some technical debt in the code duplication area. For this
task, it would be a good idea to find a tool that can discover code duplication
(including patterns) and use it as guidance to reduce duplications.

Level: Intermediate

Make atomic_commit_[check|tail] more readable
---------------------------------------------

The functions responsible for atomic commit and tail are intricate and
extensive. In particular `amdgpu_dm_atomic_commit_tail` is a long function and
could benefit from being split into smaller helpers. Improvements in this area
are more than welcome, but keep in mind that changes in this area will affect
all ASICs, meaning that refactoring requires a comprehensive verification; in
other words, this effort can take some time for validation.

Level: Advanced

Documentation
=============

Expand kernel-doc
-----------------

Many DC functions do not have a proper kernel-doc; understanding a function and
adding documentation is a great way to learn more about the amdgpu driver and
also leave an outstanding contribution to the entire community.

Level: Starter

Beyond AMDGPU
=============

AMDGPU provides features that are not yet enabled in the userspace. This
section highlights some of the coolest display features, which could be enabled
with the userspace developer helper.

Enable underlay
---------------

AMD display has this feature called underlay (which you can read more about at
'Documentation/gpu/amdgpu/display/mpo-overview.rst') which is intended to
save power when playing a video. The basic idea is to put a video in the
underlay plane at the bottom and the desktop in the plane above it with a hole
in the video area. This feature is enabled in ChromeOS, and from our data
measurement, it can save power.

Level: Unknown

Adaptive Backlight Modulation (ABM)
-----------------------------------

ABM is a feature that adjusts the display panel's backlight level and pixel
values depending on the displayed image. This power-saving feature can be very
useful when the system starts to run off battery; since this will impact the
display output fidelity, it would be good if this option was something that
users could turn on or off.

Level: Unknown


HDR & Color management & VRR
----------------------------

HDR, Color Management, and VRR are huge topics and it's hard to put these into
concise ToDos. If you are interested in this topic, we recommend checking some
blog posts from the community developers to better understand some of the
specific challenges and people working on the subject. If anyone wants to work
on some particular part, we can try to help with some basic guidance. Finally,
keep in mind that we already have some kernel-doc in place for those areas.

Level: Unknown
