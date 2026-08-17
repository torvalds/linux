.. SPDX-License-Identifier: MIT
.. Copyright © 2026 Intel Corporation

.. _drm/intel-display:

====================
Intel Display Driver
====================

The Intel display driver provides the display, or :ref:`drm-kms`, support for
both the :ref:`drm/xe <drm/xe>` and :ref:`drm/i915 <drm/i915>` Intel GPU
drivers.

The source code currently resides under ``drivers/gpu/drm/i915/display`` due to
historical reasons, and it's compiled separately into both drm/xe and drm/i915
kernel modules.

The drm/xe and drm/i915 drivers are the "core" or "parent" drivers for display,
as they initialize and own the drm device, and pass that on to the display
driver. The display driver isn't an independent driver in that sense.

.. toctree::
   :maxdepth: 1
   :caption: Detailed display topics

   async-flip
   atomic
   audio
   casf
   cdclk
   cmtg
   dmc
   dpio
   dpll
   drrs
   dsb
   fbc
   fifo-underrun
   frontbuffer
   hotplug
   plane
   psr
   snps-phy
   vbt
