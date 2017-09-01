.. -*- coding: utf-8; mode: rst -*-

.. _CA_GET_MSG:

==========
CA_GET_MSG
==========

Name
----

CA_GET_MSG


Synopsis
--------

.. c:function:: int ioctl(fd, CA_GET_MSG, struct ca_msg *msg)
    :name: CA_GET_MSG


Arguments
---------

``fd``
  File descriptor returned by a previous call to :c:func:`open() <dvb-ca-open>`.

``msg``
  Pointer to struct :c:type:`ca_msg`.

.. c:type:: ca_msg

.. code-block:: c

    /* a message to/from a CI-CAM */
    struct ca_msg {
	unsigned int index;
	unsigned int type;
	unsigned int length;
	unsigned char msg[256];
    };

Description
-----------

.. note:: This ioctl is undocumented. Documentation is welcome.


Return Value
------------


On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
