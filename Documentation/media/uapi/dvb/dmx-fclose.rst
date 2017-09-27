.. -*- coding: utf-8; mode: rst -*-

.. _dmx_fclose:

========================
Digital TV demux close()
========================

Name
----

Digital TV demux close()


Synopsis
--------

.. c:function:: int close(int fd)
    :name: dvb-dmx-close


Arguments
---------

``fd``
  File descriptor returned by a previous call to
  :c:func:`open() <dvb-dmx-open>`.

Description
-----------

This system call deactivates and deallocates a filter that was
previously allocated via the :c:func:`open() <dvb-dmx-open>` call.


Return Value
------------

On success 0 is returned.

On error, -1 is returned and the ``errno`` variable is set
appropriately.

The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
