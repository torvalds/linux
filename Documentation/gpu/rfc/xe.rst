==========================
Xe – Merge Acceptance Plan
==========================
Xe is a new driver for Intel GPUs that supports both integrated and
discrete platforms starting with Tiger Lake (first Intel Xe Architecture).

This document aims to establish a merge plan for the Xe, by writing down clear
pre-merge goals, in order to avoid unnecessary delays.

Xe – Overview
=============
The main motivation of Xe is to have a fresh base to work from that is
unencumbered by older platforms, whilst also taking the opportunity to
rearchitect our driver to increase sharing across the drm subsystem, both
leveraging and allowing us to contribute more towards other shared components
like TTM and drm/scheduler.

This is also an opportunity to start from the beginning with a clean uAPI that is
extensible by design and already aligned with the modern userspace needs. For
this reason, the memory model is solely based on GPU Virtual Address space
bind/unbind (‘VM_BIND’) of GEM buffer objects (BOs) and execution only supporting
explicit synchronization. With persistent mapping across the execution, the
userspace does not need to provide a list of all required mappings during each
submission.

The new driver leverages a lot from i915. As for display, the intent is to share
the display code with the i915 driver so that there is maximum reuse there.

As for the power management area, the goal is to have a much-simplified support
for the system suspend states (S-states), PCI device suspend states (D-states),
GPU/Render suspend states (R-states) and frequency management. It should leverage
as much as possible all the existent PCI-subsystem infrastructure (pm and
runtime_pm) and underlying firmware components such PCODE and GuC for the power
states and frequency decisions.

Repository:

https://gitlab.freedesktop.org/drm/xe/kernel (branch drm-xe-next)

Xe – Platforms
==============
Currently, Xe is already functional and has experimental support for multiple
platforms starting from Tiger Lake, with initial support in userspace implemented
in Mesa (for Iris and Anv, our OpenGL and Vulkan drivers), as well as in NEO
(for OpenCL and Level0).

During a transition period, platforms will be supported by both Xe and i915.
However, the force_probe mechanism existent in both drivers will allow only one
official and by-default probe at a given time.

For instance, in order to probe a DG2 which PCI ID is 0x5690 by Xe instead of
i915, the following set of parameters need to be used:

```
i915.force_probe=!5690 xe.force_probe=5690
```

In both drivers, the ‘.require_force_probe’ protection forces the user to use the
force_probe parameter while the driver is under development. This protection is
only removed when the support for the platform and the uAPI are stable. Stability
which needs to be demonstrated by CI results.

In order to avoid user space regressions, i915 will continue to support all the
current platforms that are already out of this protection. Xe support will be
forever experimental and dependent on the usage of force_probe for these
platforms.

When the time comes for Xe, the protection will be lifted on Xe and kept in i915.

Xe – Pre-Merge Goals - Work-in-Progress
=======================================

Drm_scheduler
-------------
Xe primarily uses Firmware based scheduling (GuC FW). However, it will use
drm_scheduler as the scheduler ‘frontend’ for userspace submission in order to
resolve syncobj and dma-buf implicit sync dependencies. However, drm_scheduler is
not yet prepared to handle the 1-to-1 relationship between drm_gpu_scheduler and
drm_sched_entity.

Deeper changes to drm_scheduler should *not* be required to get Xe accepted, but
some consensus needs to be reached between Xe and other community drivers that
could also benefit from this work, for coupling FW based/assisted submission such
as the ARM’s new Mali GPU driver, and others.

As a key measurable result, the patch series introducing Xe itself shall not
depend on any other patch touching drm_scheduler itself that was not yet merged
through drm-misc. This, by itself, already includes the reach of an agreement for
uniform 1 to 1 relationship implementation / usage across drivers.

ASYNC VM_BIND
-------------
Although having a common DRM level IOCTL for VM_BIND is not a requirement to get
Xe merged, it is mandatory to have a consensus with other drivers and Mesa.
It needs to be clear how to handle async VM_BIND and interactions with userspace
memory fences. Ideally with helper support so people don't get it wrong in all
possible ways.

As a key measurable result, the benefits of ASYNC VM_BIND and a discussion of
various flavors, error handling and sample API suggestions are documented in
:doc:`The ASYNC VM_BIND document </gpu/drm-vm-bind-async>`.

Userptr integration and vm_bind
-------------------------------
Different drivers implement different ways of dealing with execution of userptr.
With multiple drivers currently introducing support to VM_BIND, the goal is to
aim for a DRM consensus on what’s the best way to have that support. To some
extent this is already getting addressed itself with the GPUVA where likely the
userptr will be a GPUVA with a NULL GEM call VM bind directly on the userptr.
However, there are more aspects around the rules for that and the usage of
mmu_notifiers, locking and other aspects.

This task here has the goal of introducing a documentation of the basic rules.

The documentation *needs* to first live in this document (API session below) and
then moved to another more specific document or at Xe level or at DRM level.

Documentation should include:

 * The userptr part of the VM_BIND api.

 * Locking, including the page-faulting case.

 * O(1) complexity under VM_BIND.

Some parts of userptr like mmu_notifiers should become GPUVA or DRM helpers when
the second driver supporting VM_BIND+userptr appears. Details to be defined when
the time comes.

Long running compute: minimal data structure/scaffolding
--------------------------------------------------------
The generic scheduler code needs to include the handling of endless compute
contexts, with the minimal scaffolding for preempt-ctx fences (probably on the
drm_sched_entity) and making sure drm_scheduler can cope with the lack of job
completion fence.

The goal is to achieve a consensus ahead of Xe initial pull-request, ideally with
this minimal drm/scheduler work, if needed, merged to drm-misc in a way that any
drm driver, including Xe, could re-use and add their own individual needs on top
in a next stage. However, this should not block the initial merge.

This is a non-blocker item since the driver without the support for the long
running compute enabled is not a showstopper.

Display integration with i915
-----------------------------
In order to share the display code with the i915 driver so that there is maximum
reuse, the i915/display/ code is built twice, once for i915.ko and then for
xe.ko. Currently, the i915/display code in Xe tree is polluted with many 'ifdefs'
depending on the build target. The goal is to refactor both Xe and i915/display
code simultaneously in order to get a clean result before they land upstream, so
that display can already be part of the initial pull request towards drm-next.

However, display code should not gate the acceptance of Xe in upstream. Xe
patches will be refactored in a way that display code can be removed, if needed,
from the first pull request of Xe towards drm-next. The expectation is that when
both drivers are part of the drm-tip, the introduction of cleaner patches will be
easier and speed up.

Drm_exec
--------
Helper to make dma_resv locking for a big number of buffers is getting removed in
the drm_exec series proposed in https://patchwork.freedesktop.org/patch/524376/
If that happens, Xe needs to change and incorporate the changes in the driver.
The goal is to engage with the Community to understand if the best approach is to
move that to the drivers that are using it or if we should keep the helpers in
place waiting for Xe to get merged.

This item ties into the GPUVA, VM_BIND, and even long-running compute support.

As a key measurable result, we need to have a community consensus documented in
this document and the Xe driver prepared for the changes, if necessary.

Xe – uAPI high level overview
=============================

...Warning: To be done in follow up patches after/when/where the main consensus in various items are individually reached.

Xe – Pre-Merge Goals - Completed
================================

Dev_coredump
------------

Xe needs to align with other drivers on the way that the error states are
dumped, avoiding a Xe only error_state solution. The goal is to use devcoredump
infrastructure to report error states, since it produces a standardized way
by exposing a virtual and temporary /sys/class/devcoredump device.

As the key measurable result, Xe driver needs to provide GPU snapshots captured
at hang time through devcoredump, but without depending on any core modification
of devcoredump infrastructure itself.

Later, when we are in-tree, the goal is to collaborate with devcoredump
infrastructure with overall possible improvements, like multiple file support
for better organization of the dumps, snapshot support, dmesg extra print,
and whatever may make sense and help the overall infrastructure.

DRM_VM_BIND
-----------
Nouveau, and Xe are all implementing ‘VM_BIND’ and new ‘Exec’ uAPIs in order to
fulfill the needs of the modern uAPI. Xe merge should *not* be blocked on the
development of a common new drm_infrastructure. However, the Xe team needs to
engage with the community to explore the options of a common API.

As a key measurable result, the DRM_VM_BIND needs to be documented in this file
below, or this entire block deleted if the consensus is for independent drivers
vm_bind ioctls.

Although having a common DRM level IOCTL for VM_BIND is not a requirement to get
Xe merged, it is mandatory to enforce the overall locking scheme for all major
structs and list (so vm and vma). So, a consensus is needed, and possibly some
common helpers. If helpers are needed, they should be also documented in this
document.

GPU VA
------
Two main goals of Xe are meeting together here:

1) Have an uAPI that aligns with modern UMD needs.

2) Early upstream engagement.

RedHat engineers working on Nouveau proposed a new DRM feature to handle keeping
track of GPU virtual address mappings. This is still not merged upstream, but
this aligns very well with our goals and with our VM_BIND. The engagement with
upstream and the port of Xe towards GPUVA is already ongoing.

As a key measurable result, Xe needs to be aligned with the GPU VA and working in
our tree. Missing Nouveau patches should *not* block Xe and any needed GPUVA
related patch should be independent and present on dri-devel or acked by
maintainers to go along with the first Xe pull request towards drm-next.
