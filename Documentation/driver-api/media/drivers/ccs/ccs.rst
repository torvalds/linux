.. SPDX-License-Identifier: GPL-2.0-only OR BSD-3-Clause

.. include:: <isonum.txt>

.. _media-ccs-driver:

MIPI CCS camera sensor driver
=============================

The MIPI CCS camera sensor driver is a generic driver for `MIPI CCS
<https://www.mipi.org/specifications/camera-command-set>`_ compliant
camera sensors.

Also see :ref:`the CCS driver UAPI documentation <media-ccs-uapi>`.

CCS static data
---------------

The MIPI CCS driver supports CCS static data for all compliant devices,
including not just those compliant with CCS 1.1 but also CCS 1.0 and SMIA(++).
For CCS the file names are formed as

	ccs/ccs-sensor-vvvv-mmmm-rrrr.fw (sensor) and
	ccs/ccs-module-vvvv-mmmm-rrrr.fw (module).

For SMIA++ compliant devices the corresponding file names are

	ccs/smiapp-sensor-vv-mmmm-rr.fw (sensor) and
	ccs/smiapp-module-vv-mmmm-rrrr.fw (module).

For SMIA (non-++) compliant devices the static data file name is

	ccs/smia-sensor-vv-mmmm-rr.fw (sensor).

vvvv or vv denotes MIPI and SMIA manufacturer IDs respectively, mmmm model ID
and rrrr or rr revision number.

CCS tools
~~~~~~~~~

`CCS tools <https://github.com/MIPI-Alliance/ccs-tools/>`_ is a set of
tools for working with CCS static data files. CCS tools includes a
definition of the human-readable CCS static data YAML format and includes a
program to convert it to a binary.

Register definition generator
-----------------------------

The ccs-regs.asc file contains MIPI CCS register definitions that are used
to produce C source code files for definitions that can be better used by
programs written in C language. As there are many dependencies between the
produced files, please do not modify them manually as it's error-prone and
in vain, but instead change the script producing them.

Usage
~~~~~

Conventionally the script is called this way to update the CCS driver
definitions:

.. code-block:: none

	$ Documentation/driver-api/media/drivers/ccs/mk-ccs-regs -k \
		-e drivers/media/i2c/ccs/ccs-regs.h \
		-L drivers/media/i2c/ccs/ccs-limits.h \
		-l drivers/media/i2c/ccs/ccs-limits.c \
		-c Documentation/driver-api/media/drivers/ccs/ccs-regs.asc

CCS PLL calculator
==================

The CCS PLL calculator is used to compute the PLL configuration, given sensor's
capabilities as well as board configuration and user specified configuration. As
the configuration space that encompasses all these configurations is vast, the
PLL calculator isn't entirely trivial. Yet it is relatively simple to use for a
driver.

The PLL model implemented by the PLL calculator corresponds to MIPI CCS 1.1.

.. kernel-doc:: drivers/media/i2c/ccs-pll.h

**Copyright** |copy| 2020 Intel Corporation
