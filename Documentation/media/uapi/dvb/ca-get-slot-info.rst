.. -*- coding: utf-8; mode: rst -*-

.. _CA_GET_SLOT_INFO:

================
CA_GET_SLOT_INFO
================

Name
----

CA_GET_SLOT_INFO


Synopsis
--------

.. c:function:: int ioctl(fd, CA_GET_SLOT_INFO, struct ca_slot_info *info)
    :name: CA_GET_SLOT_INFO


Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open() <cec-open>`.

``info``
  Pointer to struct c:type:`ca_slot_info`.

.. _ca_slot_info_type:

.. flat-table:: ca_slot_info types
    :header-rows:  1
    :stub-columns: 0

    -
      - type
      - name
      - description
    -
       - CA_CI
       - 1
       - CI high level interface

    -
       - CA_CI_LINK
       - 2
       - CI link layer level interface

    -
       - CA_CI_PHYS
       - 4
       - CI physical layer level interface

    -
       - CA_DESCR
       - 8
       - built-in descrambler

    -
       - CA_SC
       - 128
       - simple smart card interface

.. _ca_slot_info_flag:

.. flat-table:: ca_slot_info flags
    :header-rows:  1
    :stub-columns: 0

    -
      - type
      - name
      - description

    -
       - CA_CI_MODULE_PRESENT
       - 1
       - module (or card) inserted

    -
       - CA_CI_MODULE_READY
       - 2
       -

.. c:type:: ca_slot_info

.. flat-table:: struct ca_slot_info
    :header-rows:  1
    :stub-columns: 0

    -
      - type
      - name
      - description

    -
       - int
       - num
       - slot number

    -
       - int
       - type
       - CA interface this slot supports, as defined at :ref:`ca_slot_info_type`.

    -
       - unsigned int
       - flags
       - flags as defined at :ref:`ca_slot_info_flag`.


Description
-----------

.. note:: This ioctl is undocumented. Documentation is welcome.


Return Value
------------

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
