.. SPDX-License-Identifier: (GPL-2.0+ OR MIT)

.. _v4l2-meta-fmt-rkisp1-params:

============================
V4L2_META_FMT_RK_ISP1_PARAMS
============================

Rockchip ISP1 Parameters Data

Description
===========

This format describes input parameters for the Rockchip ISP1.

It uses c-struct :c:type:`rkisp1_params_cfg`, which is defined in
the ``linux/rkisp1-config.h`` header file.

The parameters consist of multiple modules.
The module won't be updated if the corresponding bit was not set in module_*_update.

.. kernel-doc:: include/uapi/linux/rkisp1-config.h
   :functions: rkisp1_params_cfg
