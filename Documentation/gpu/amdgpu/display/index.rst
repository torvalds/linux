.. _amdgpu-display-core:

===================================
drm/amd/display - Display Core (DC)
===================================

AMD display engine is partially shared with other operating systems; for this
reason, our Display Core Driver is divided into two pieces:

#. **Display Core (DC)** contains the OS-agnostic components. Things like
   hardware programming and resource management are handled here.
#. **Display Manager (DM)** contains the OS-dependent components. Hooks to the
   amdgpu base driver and DRM are implemented here. For example, you can check
   display/amdgpu_dm/ folder.

------------------
DC Code validation
------------------

Maintaining the same code base across multiple OSes requires a lot of
synchronization effort between repositories and exhaustive validation. In the
DC case, we maintain a tree to centralize code from different parts. The shared
repository has integration tests with our Internal Linux CI farm, and we run a
comprehensive set of IGT tests in various AMD GPUs/APUs (mostly recent dGPUs
and APUs). Our CI also checks ARM64/32, PPC64/32, and x86_64/32 compilation
with DCN enabled and disabled.

When we upstream a new feature or some patches, we pack them in a patchset with
the prefix **DC Patches for <DATE>**, which is created based on the latest
`amd-staging-drm-next <https://gitlab.freedesktop.org/agd5f/linux>`_. All of
those patches are under a DC version tested as follows:

* Ensure that every patch compiles and the entire series pass our set of IGT
  test in different hardware.
* Prepare a branch with those patches for our validation team. If there is an
  error, a developer will debug as fast as possible; usually, a simple bisect
  in the series is enough to point to a bad change, and two possible actions
  emerge: fix the issue or drop the patch. If it is not an easy fix, the bad
  patch is dropped.
* Finally, developers wait a few days for community feedback before we merge
  the series.

It is good to stress that the test phase is something that we take extremely
seriously, and we never merge anything that fails our validation. Follows an
overview of our test set:

#. Manual test
    * Multiple Hotplugs with DP and HDMI.
    * Stress test with multiple display configuration changes via the user interface.
    * Validate VRR behaviour.
    * Check PSR.
    * Validate MPO when playing video.
    * Test more than two displays connected at the same time.
    * Check suspend/resume.
    * Validate FPO.
    * Check MST.
#. Automated test
    * IGT tests in a farm with GPUs and APUs that support DCN and DCE.
    * Compilation validation with the latest GCC and Clang from LTS distro.
    * Cross-compilation for PowerPC 64/32, ARM 64/32, and x86 32.

In terms of test setup for CI and manual tests, we usually use:

#. The latest Ubuntu LTS.
#. In terms of userspace, we only use fully updated open-source components
   provided by the distribution official package manager.
#. Regarding IGT, we use the latest code from the upstream.
#. Most of the manual tests are conducted in the GNome but we also use KDE.

Notice that someone from our test team will always reply to the cover letter
with the test report.

--------------
DC Information
--------------

The display pipe is responsible for "scanning out" a rendered frame from the
GPU memory (also called VRAM, FrameBuffer, etc.) to a display. In other words,
it would:

#. Read frame information from memory;
#. Perform required transformation;
#. Send pixel data to sink devices.

If you want to learn more about our driver details, take a look at the below
table of content:

.. toctree::

   display-manager.rst
   dcn-overview.rst
   dcn-blocks.rst
   mpo-overview.rst
   dc-debug.rst
   display-contributing.rst
   dc-glossary.rst
