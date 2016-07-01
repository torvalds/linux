.. -*- coding: utf-8; mode: rst -*-

.. _query-dvb-frontend-info:

*****************************
Querying frontend information
*****************************

Usually, the first thing to do when the frontend is opened is to check
the frontend capabilities. This is done using
:ref:`FE_GET_INFO`. This ioctl will enumerate the
DVB API version and other characteristics about the frontend, and can be
opened either in read only or read/write mode.


.. ------------------------------------------------------------------------------
.. This file was automatically converted from DocBook-XML with the dbxml
.. library (https://github.com/return42/sphkerneldoc). The origin XML comes
.. from the linux kernel, refer to:
..
.. * https://github.com/torvalds/linux/tree/master/Documentation/DocBook
.. ------------------------------------------------------------------------------
