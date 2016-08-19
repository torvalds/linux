.. -*- coding: utf-8; mode: rst -*-

.. _CA_SET_PID:

==========
CA_SET_PID
==========

Name
----

CA_SET_PID


Synopsis
--------

.. c:function:: int ioctl(fd, CA_SET_PID, ca_pid_t *pid)
    :name: CA_SET_PID


Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open() <dvb-ca-open>`.

``pid``
  Undocumented.


Description
-----------

.. note:: This ioctl is undocumented. Documentation is welcome.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
