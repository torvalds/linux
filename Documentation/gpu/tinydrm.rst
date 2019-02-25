============================
drm/tinydrm Tiny DRM drivers
============================

tinydrm is a collection of DRM drivers that are so small they can fit in a
single source file.

Helpers
=======

.. kernel-doc:: include/drm/tinydrm/tinydrm-helpers.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/tinydrm/core/tinydrm-helpers.c
   :export:

.. kernel-doc:: drivers/gpu/drm/tinydrm/core/tinydrm-pipe.c
   :export:

MIPI DBI Compatible Controllers
===============================

.. kernel-doc:: drivers/gpu/drm/tinydrm/mipi-dbi.c
   :doc: overview

.. kernel-doc:: include/drm/tinydrm/mipi-dbi.h
   :internal:

.. kernel-doc:: drivers/gpu/drm/tinydrm/mipi-dbi.c
   :export:
