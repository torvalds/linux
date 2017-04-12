.. -*- coding: utf-8; mode: rst -*-

.. _CA_GET_DESCR_INFO:

=================
CA_GET_DESCR_INFO
=================

Name
----

CA_GET_DESCR_INFO


Synopsis
--------

.. c:function:: int  ioctl(fd, CA_GET_DESCR_INFO, struct ca_descr_info *desc)
    :name: CA_GET_DESCR_INFO

Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open() <dvb-ca-open>`.

``desc``
  Pointer to struct :c:type:`ca_descr_info`.

.. c:type:: struct ca_descr_info

.. flat-table:: struct ca_descr_info
    :header-rows:  1
    :stub-columns: 0

    -
      - type
      - name
      - description

    -
      - unsigned int
      - num
      - number of available descramblers (keys)
    -
      - unsigned int
      - type
      - type of supported scrambling system. Valid values are:
	``CA_ECD``, ``CA_NDS`` and ``CA_DSS``.


Description
-----------

.. note:: This ioctl is undocumented. Documentation is welcome.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
