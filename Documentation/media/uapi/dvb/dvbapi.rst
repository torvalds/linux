.. -*- coding: utf-8; mode: rst -*-

.. include:: <isonum.txt>

.. _dvbapi:

########################
Part II - Digital TV API
########################

.. note::

   This API is also known as **DVB API**, although it is generic
   enough to support all digital TV standards.

**Version 5.10**

.. class:: toc-title

        Table of Contents

.. toctree::
    :maxdepth: 5
    :numbered:

    intro
    frontend
    demux
    ca
    net
    legacy_dvb_apis
    examples
    audio_h
    ca_h
    dmx_h
    frontend_h
    net_h
    video_h


**********************
Revision and Copyright
**********************

Authors:

- J. K. Metzler, Ralph <rjkm@metzlerbros.de>

 - Original author of the DVB API documentation.

- O. C. Metzler, Marcus <rjkm@metzlerbros.de>

 - Original author of the DVB API documentation.

- Carvalho Chehab, Mauro <m.chehab@kernel.org>

 - Ported document to Docbook XML, addition of DVBv5 API, documentation gaps fix.

**Copyright** |copy| 2002-2003 : Convergence GmbH

**Copyright** |copy| 2009-2016 : Mauro Carvalho Chehab

****************
Revision History
****************

:revision: 2.1.0 / 2015-05-29 (*mcc*)

DocBook improvements and cleanups, in order to document the system calls
on a more standard way and provide more description about the current
DVB API.

:revision: 2.0.4 / 2011-05-06 (*mcc*)

Add more information about DVB APIv5, better describing the frontend
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
