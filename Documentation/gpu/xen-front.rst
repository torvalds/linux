====================================================
 drm/xen-front Xen para-virtualized frontend driver
====================================================

This frontend driver implements Xen para-virtualized display
according to the display protocol described at
include/xen/interface/io/displif.h

Driver modes of operation in terms of display buffers used
==========================================================

.. kernel-doc:: drivers/gpu/drm/xen/xen_drm_front.h
   :doc: Driver modes of operation in terms of display buffers used

Buffers allocated by the frontend driver
----------------------------------------

.. kernel-doc:: drivers/gpu/drm/xen/xen_drm_front.h
   :doc: Buffers allocated by the frontend driver

With GEM CMA helpers
~~~~~~~~~~~~~~~~~~~~

.. kernel-doc:: drivers/gpu/drm/xen/xen_drm_front.h
   :doc: With GEM CMA helpers

Without GEM CMA helpers
~~~~~~~~~~~~~~~~~~~~~~~

.. kernel-doc:: drivers/gpu/drm/xen/xen_drm_front.h
   :doc: Without GEM CMA helpers

Buffers allocated by the backend
--------------------------------

.. kernel-doc:: drivers/gpu/drm/xen/xen_drm_front.h
   :doc: Buffers allocated by the backend

Driver limitations
==================

.. kernel-doc:: drivers/gpu/drm/xen/xen_drm_front.h
   :doc: Driver limitations
