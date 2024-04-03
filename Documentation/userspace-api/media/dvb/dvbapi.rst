.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. include:: <isonum.txt>

.. _dvbapi:

########################
Part II - Digital TV API
########################

.. note::

   This API is also known as Linux **DVB API**.

   It it was originally written to support the European digital TV
   standard (DVB), and later extended to support all digital TV standards.

   In order to avoid confusion, within this document, it was opted to refer to
   it, and to associated hardware as **Digital TV**.

   The word **DVB** is reserved to be used for:

     - the Digital TV API version
       (e. g. DVB API version 3 or DVB API version 5);
     - digital TV data types (enums, structs, defines, etc);
     - digital TV device nodes (``/dev/dvb/...``);
     - the European DVB standard.

**Version 5.10**

.. toctree::
    :caption: Table of Contents
    :maxdepth: 5
    :numbered:

    intro
    frontend
    demux
    ca
    net
    legacy_dvb_apis
    examples
    headers


**********************
Revision and Copyright
**********************

Authors:

- J. K. Metzler, Ralph <rjkm@metzlerbros.de>

 - Original author of the Digital TV API documentation.

- O. C. Metzler, Marcus <rjkm@metzlerbros.de>

 - Original author of the Digital TV API documentation.

- Carvalho Chehab, Mauro <mchehab+samsung@kernel.org>

 - Ported document to Docbook XML, addition of DVBv5 API, documentation gaps fix.

**Copyright** |copy| 2002-2003 : Convergence GmbH

**Copyright** |copy| 2009-2017 : Mauro Carvalho Chehab

****************
Revision History
****************

:revision: 2.2.0 / 2017-09-01 (*mcc*)

Most gaps between the uAPI document and the Kernel implementation
got fixed for the non-legacy API.

:revision: 2.1.0 / 2015-05-29 (*mcc*)

DocBook improvements and cleanups, in order to document the system calls
on a more standard way and provide more description about the current
Digital TV API.

:revision: 2.0.4 / 2011-05-06 (*mcc*)

Add more information about DVBv5 API, better describing the frontend
GET/SET props ioctl's.


:revision: 2.0.3 / 2010-07-03 (*mcc*)

Add some frontend capabilities flags, present on kernel, but missing at
the specs.


:revision: 2.0.2 / 2009-10-25 (*mcc*)

documents FE_SET_FRONTEND_TUNE_MODE and
FE_DISHETWORK_SEND_LEGACY_CMD ioctls.


:revision: 2.0.1 / 2009-09-16 (*mcc*)

Added ISDB-T test originally written by Patrick Boettcher


:revision: 2.0.0 / 2009-09-06 (*mcc*)

Conversion from LaTex to DocBook XML. The contents is the same as the
original LaTex version.


:revision: 1.0.0 / 2003-07-24 (*rjkm*)

Initial revision on LaTEX.
