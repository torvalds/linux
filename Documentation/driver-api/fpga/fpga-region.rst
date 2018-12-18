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

How to program an FPGA using a region
-------------------------------------

First, allocate the info struct::

	info = fpga_image_info_alloc(dev);
	if (!info)
		return -ENOMEM;

Set flags as needed, i.e.::

	info->flags |= FPGA_MGR_PARTIAL_RECONFIG;

Point to your FPGA image, such as::

	info->sgt = &sgt;

Add info to region and do the programming::

	region->info = info;
	ret = fpga_region_program_fpga(region);

:c:func:`fpga_region_program_fpga()` operates on info passed in the
fpga_image_info (region->info).  This function will attempt to:

 * lock the region's mutex
 * lock the region's FPGA manager
 * build a list of FPGA bridges if a method has been specified to do so
 * disable the bridges
 * program the FPGA
 * re-enable the bridges
 * release the locks

Then you will want to enumerate whatever hardware has appeared in the FPGA.

How to add a new FPGA region
----------------------------

An example of usage can be seen in the probe function of [#f2]_.

.. [#f1] ../devicetree/bindings/fpga/fpga-region.txt
.. [#f2] ../../drivers/fpga/of-fpga-region.c

API to program an FPGA
----------------------

.. kernel-doc:: drivers/fpga/fpga-region.c
   :functions: fpga_region_program_fpga

API to add a new FPGA region
----------------------------

.. kernel-doc:: include/linux/fpga/fpga-region.h
   :functions: fpga_region

.. kernel-doc:: drivers/fpga/fpga-region.c
   :functions: fpga_region_create

.. kernel-doc:: drivers/fpga/fpga-region.c
   :functions: fpga_region_free

.. kernel-doc:: drivers/fpga/fpga-region.c
   :functions: fpga_region_register

.. kernel-doc:: drivers/fpga/fpga-region.c
   :functions: fpga_region_unregister
