.. -*- coding: utf-8; mode: rst -*-

.. _CA_GET_CAP:

==========
CA_GET_CAP
==========

Name
----

CA_GET_CAP


Synopsis
--------

.. c:function:: int ioctl(fd, CA_GET_CAP, struct ca_caps *caps)
    :name: CA_GET_CAP


Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open() <dvb-ca-open>`.

``caps``
  Pointer to struct :c:type:`ca_caps`.

.. c:type:: struct ca_caps

.. flat-table:: struct ca_caps
    :header-rows:  1
    :stub-columns: 0

    -
      - type
      - name
      - description
    -
      -	unsigned int
      - slot_num
      - total number of CA card and module slots
    -
      - unsigned int
      - slot_type
      - bitmask with all supported slot types
    -
      - unsigned int
      - descr_num
      - total number of descrambler slots (keys)
    -
      - unsigned int
      - descr_type
      - bit mask with all supported descr types


Description
-----------

.. note:: This ioctl is undocumented. Documentation is welcome.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
