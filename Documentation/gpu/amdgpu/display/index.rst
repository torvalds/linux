.. _amdgpu-display-core:

===================================
drm/amd/display - Display Core (DC)
===================================

AMD display engine is partially shared with other operating systems; for this
reason, our Display Core Driver is divided into two pieces:

1. **Display Core (DC)** contains the OS-agnostic components. Things like
   hardware programming and resource management are handled here.
2. **Display Manager (DM)** contains the OS-dependent components. Hooks to the
   amdgpu base driver and DRM are implemented here.

The display pipe is responsible for "scanning out" a rendered frame from the
GPU memory (also called VRAM, FrameBuffer, etc.) to a display. In other words,
it would:

1. Read frame information from memory;
2. Perform required transformation;
3. Send pixel data to sink devices.

If you want to learn more about our driver details, take a look at the below
table of content:

.. toctree::

   display-manager.rst
   dc-debug.rst
   dcn-overview.rst
   dc-glossary.rst
