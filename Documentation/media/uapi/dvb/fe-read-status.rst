.. -*- coding: utf-8; mode: rst -*-

.. _FE_READ_STATUS:

********************
ioctl FE_READ_STATUS
********************

Name
====

FE_READ_STATUS - Returns status information about the front-end. This call only requires - read-only access to the device


Synopsis
========

.. c:function:: int ioctl( int fd, int request, unsigned int *status )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <frontend_f_open>`.

``request``
    FE_READ_STATUS

``status``
    pointer to a bitmask integer filled with the values defined by enum
    :ref:`fe_status <fe-status>`.


Description
===========

All DVB frontend devices support the ``FE_READ_STATUS`` ioctl. It is
used to check about the locking status of the frontend after being
tuned. The ioctl takes a pointer to an integer where the status will be
written.

.. note::

   The size of status is actually sizeof(enum fe_status), with
   varies according with the architecture. This needs to be fixed in the
   future.


.. _fe-status-t:

int fe_status
=============

The fe_status parameter is used to indicate the current state and/or
state changes of the frontend hardware. It is produced using the enum
:ref:`fe_status <fe-status>` values on a bitmask

.. tabularcolumns:: |p{3.5cm}|p{14.0cm}|

.. _fe-status:

.. flat-table:: enum fe_status
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ID

       -  Description

    -  .. row 2

       -  .. _FE-HAS-SIGNAL:

	  ``FE_HAS_SIGNAL``

       -  The frontend has found something above the noise level

    -  .. row 3

       -  .. _FE-HAS-CARRIER:

	  ``FE_HAS_CARRIER``

       -  The frontend has found a DVB signal

    -  .. row 4

       -  .. _FE-HAS-VITERBI:

	  ``FE_HAS_VITERBI``

       -  The frontend FEC inner coding (Viterbi, LDPC or other inner code)
	  is stable

    -  .. row 5

       -  .. _FE-HAS-SYNC:

	  ``FE_HAS_SYNC``

       -  Synchronization bytes was found

    -  .. row 6

       -  .. _FE-HAS-LOCK:

	  ``FE_HAS_LOCK``

       -  The DVB were locked and everything is working

    -  .. row 7

       -  .. _FE-TIMEDOUT:

	  ``FE_TIMEDOUT``

       -  no lock within the last about 2 seconds

    -  .. row 8

       -  .. _FE-REINIT:

	  ``FE_REINIT``

       -  The frontend was reinitialized, application is recommended to
	  reset DiSEqC, tone and parameters


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
