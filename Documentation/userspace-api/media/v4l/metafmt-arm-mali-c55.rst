.. SPDX-License-Identifier: GPL-2.0

.. _v4l2-meta-fmt-mali-c55-stats:

*************************************
V4L2_META_FMT_MALI_C55_STATS ('C55S')
*************************************

3A Statistics
=============

The ISP device collects different statistics over an input bayer frame. Those
statistics can be obtained by userspace from the
:ref:`mali-c55 3a stats <mali-c55-3a-stats>` metadata capture video node, using
the :c:type:`v4l2_meta_format` interface. The buffer contains a single instance
of the C structure :c:type:`mali_c55_stats_buffer` defined in
``mali-c55-config.h``, so the structure can be obtained from the buffer by:

.. code-block:: C

	struct mali_c55_stats_buffer *stats =
		(struct mali_c55_stats_buffer *)buf;

For details of the statistics see :c:type:`mali_c55_stats_buffer`.

Arm Mali-C55 uAPI data types
============================

.. kernel-doc:: include/uapi/linux/media/arm/mali-c55-config.h
