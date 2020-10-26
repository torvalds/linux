FPGA Region
===========

Overview
--------

This document is meant to be a brief overview of the FPGA region API usage.  A
more conceptual look at regions can be found in the Device Tree binding
document [#f1]_.

For the purposes of this API document, let's just say that a region associates
an FPGA Manager and a bridge (or bridges) with a reprogrammable region of an
FPGA or the whole FPGA.  The API provides a way to register a region and to
program a region.

Currently the only layer above fpga-region.c in the kernel is the Device Tree
support (of-fpga-region.c) described in [#f1]_.  The DT support layer uses regions
to program the FPGA and then DT to handle enumeration.  The common region code
is intended to be used by other schemes that have other ways of accomplishing
enumeration after programming.

An fpga-region can be set up to know the following things:

 * which FPGA manager to use to do the programming

 * which bridges to disable before programming and enable afterwards.

Additional info needed to program the FPGA image is passed in the struct
fpga_image_info including:

 * pointers to the image as either a scatter-gather buffer, a contiguous
   buffer, or the name of firmware file

 * flags indicating specifics such as whether the image is for partial
   reconfiguration.

How to add a new FPGA region
----------------------------

An example of usage can be seen in the probe function of [#f2]_.

.. [#f1] ../devicetree/bindings/fpga/fpga-region.txt
.. [#f2] ../../drivers/fpga/of-fpga-region.c

API to add a new FPGA region
----------------------------

* struct fpga_region — The FPGA region struct
* devm_fpga_region_create() — Allocate and init a region struct
* fpga_region_register() —  Register an FPGA region
* fpga_region_unregister() —  Unregister an FPGA region

The FPGA region's probe function will need to get a reference to the FPGA
Manager it will be using to do the programming.  This usually would happen
during the region's probe function.

* fpga_mgr_get() — Get a reference to an FPGA manager, raise ref count
* of_fpga_mgr_get() —  Get a reference to an FPGA manager, raise ref count,
  given a device node.
* fpga_mgr_put() — Put an FPGA manager

The FPGA region will need to specify which bridges to control while programming
the FPGA.  The region driver can build a list of bridges during probe time
(:c:expr:`fpga_region->bridge_list`) or it can have a function that creates
the list of bridges to program just before programming
(:c:expr:`fpga_region->get_bridges`).  The FPGA bridge framework supplies the
following APIs to handle building or tearing down that list.

* fpga_bridge_get_to_list() — Get a ref of an FPGA bridge, add it to a
  list
* of_fpga_bridge_get_to_list() — Get a ref of an FPGA bridge, add it to a
  list, given a device node
* fpga_bridges_put() — Given a list of bridges, put them

.. kernel-doc:: include/linux/fpga/fpga-region.h
   :functions: fpga_region

.. kernel-doc:: drivers/fpga/fpga-region.c
   :functions: devm_fpga_region_create

.. kernel-doc:: drivers/fpga/fpga-region.c
   :functions: fpga_region_register

.. kernel-doc:: drivers/fpga/fpga-region.c
   :functions: fpga_region_unregister

.. kernel-doc:: drivers/fpga/fpga-mgr.c
   :functions: fpga_mgr_get

.. kernel-doc:: drivers/fpga/fpga-mgr.c
   :functions: of_fpga_mgr_get

.. kernel-doc:: drivers/fpga/fpga-mgr.c
   :functions: fpga_mgr_put

.. kernel-doc:: drivers/fpga/fpga-bridge.c
   :functions: fpga_bridge_get_to_list

.. kernel-doc:: drivers/fpga/fpga-bridge.c
   :functions: of_fpga_bridge_get_to_list

.. kernel-doc:: drivers/fpga/fpga-bridge.c
   :functions: fpga_bridges_put
