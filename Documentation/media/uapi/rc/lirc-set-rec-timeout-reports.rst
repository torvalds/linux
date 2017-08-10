.. -*- coding: utf-8; mode: rst -*-

.. _lirc_set_rec_timeout_reports:

**********************************
ioctl LIRC_SET_REC_TIMEOUT_REPORTS
**********************************

Name
====

LIRC_SET_REC_TIMEOUT_REPORTS - enable or disable timeout reports for IR receive

Synopsis
========

.. c:function:: int ioctl( int fd, LIRC_SET_REC_TIMEOUT_REPORTS, __u32 *enable )
    :name: LIRC_SET_REC_TIMEOUT_REPORTS

Arguments
=========

``fd``
    File descriptor returned by open().

``enable``
    enable = 1 means enable timeout report, enable = 0 means disable timeout
    reports.


Description
===========

.. _lirc-mode2-timeout:

Enable or disable timeout reports for IR receive. By default, timeout reports
should be turned off.

.. note::

   This ioctl is only valid for :ref:`LIRC_MODE_MODE2 <lirc-mode-mode2>`.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
