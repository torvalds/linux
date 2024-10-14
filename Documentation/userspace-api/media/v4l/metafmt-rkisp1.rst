.. SPDX-License-Identifier: GPL-2.0

.. _v4l2-meta-fmt-rk-isp1-stat-3a:

************************************************************************************************************************
V4L2_META_FMT_RK_ISP1_PARAMS ('rk1p'), V4L2_META_FMT_RK_ISP1_STAT_3A ('rk1s'), V4L2_META_FMT_RK_ISP1_EXT_PARAMS ('rk1e')
************************************************************************************************************************

========================
Configuration parameters
========================

The configuration of the RkISP1 ISP is performed by userspace by providing
parameters for the ISP to the driver using the :c:type:`v4l2_meta_format`
interface.

There are two methods that allow to configure the ISP, the `fixed parameters`
configuration format and the `extensible parameters` configuration
format.

.. _v4l2-meta-fmt-rk-isp1-params:

Fixed parameters configuration format
=====================================

When using the fixed configuration format, parameters are passed to the
:ref:`rkisp1_params <rkisp1_params>` metadata output video node, using
the `V4L2_META_FMT_RK_ISP1_PARAMS` meta format.

The buffer contains a single instance of the C structure
:c:type:`rkisp1_params_cfg` defined in ``rkisp1-config.h``. So the structure can
be obtained from the buffer by:

.. code-block:: c

	struct rkisp1_params_cfg *params = (struct rkisp1_params_cfg*) buffer;

This method supports a subset of the ISP features only, new applications should
use the extensible parameters method.

.. _v4l2-meta-fmt-rk-isp1-ext-params:

Extensible parameters configuration format
==========================================

When using the extensible configuration format, parameters are passed to the
:ref:`rkisp1_params <rkisp1_params>` metadata output video node, using
the `V4L2_META_FMT_RK_ISP1_EXT_PARAMS` meta format.

The buffer contains a single instance of the C structure
:c:type:`rkisp1_ext_params_cfg` defined in ``rkisp1-config.h``. The
:c:type:`rkisp1_ext_params_cfg` structure is designed to allow userspace to
populate the data buffer with only the configuration data for the ISP blocks it
intends to configure. The extensible parameters format design allows developers
to define new block types to support new configuration parameters, and defines a
versioning scheme so that it can be extended and versioned without breaking
compatibility with existing applications.

For these reasons, this configuration method is preferred over the `fixed
parameters` format alternative.

.. rkisp1_stat_buffer

===========================
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
