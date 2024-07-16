.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _query-dvb-frontend-info:

*****************************
Querying frontend information
*****************************

Usually, the first thing to do when the frontend is opened is to check
the frontend capabilities. This is done using
:ref:`FE_GET_INFO`. This ioctl will enumerate the
Digital TV API version and other characteristics about the frontend, and can
be opened either in read only or read/write mode.
