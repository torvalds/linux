.. SPDX-License-Identifier: GPL-2.0

.. _v4l2-meta-fmt-rk-isp1-params:

.. _v4l2-meta-fmt-rk-isp1-stat-3a:

*****************************************************************************
V4L2_META_FMT_RK_ISP1_PARAMS ('rk1p'), V4L2_META_FMT_RK_ISP1_STAT_3A ('rk1s')
*****************************************************************************

Configuration parameters
========================

The configuration parameters are passed to the
:ref:`rkisp1_params <rkisp1_params>` metadata output video node, using
the :c:type:`v4l2_meta_format` interface. The buffer contains
a single instance of the C structure :c:type:`rkisp1_params_cfg` defined in
``rkisp1-config.h``. So the structure can be obtained from the buffer by:

.. code-block:: c

	struct rkisp1_params_cfg *params = (struct rkisp1_params_cfg*) buffer;

.. rkisp1_stat_buffer

3A and histogram statistics
===========================

The ISP1 device collects different statistics over an input Bayer frame.
Those statistics are obtained from the :ref:`rkisp1_stats <rkisp1_stats>`
metadata capture video node,
using the :c:type:`v4l2_meta_format` interface. The buffer contains a single
instance of the C structure :c:type:`rkisp1_stat_buffer` defined in
``rkisp1-config.h``. So the structure can be obtained from the buffer by:

.. code-block:: c

	struct rkisp1_stat_buffer *stats = (struct rkisp1_stat_buffer*) buffer;

The statistics collected are Exposure, AWB (Auto-white balance), Histogram and
AF (Auto-focus). See :c:type:`rkisp1_stat_buffer` for details of the statistics.

The 3A statistics and configuration parameters described here are usually
consumed and produced by dedicated user space libraries that comprise the
important tuning tools using software control loop.

rkisp1 uAPI data types
======================

.. kernel-doc:: include/uapi/linux/rkisp1-config.h
