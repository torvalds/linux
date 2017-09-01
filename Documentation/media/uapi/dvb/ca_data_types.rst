.. -*- coding: utf-8; mode: rst -*-

.. _ca_data_types:

*************
CA Data Types
*************

.. kernel-doc:: include/uapi/linux/dvb/ca.h

.. c:type:: ca_msg

Undocumented data types
=======================

.. note::

   Those data types are undocumented. Documentation is welcome.

.. c:type:: ca_msg

.. code-block:: c

    /* a message to/from a CI-CAM */
    struct ca_msg {
	unsigned int index;
	unsigned int type;
	unsigned int length;
	unsigned char msg[256];
    };


.. c:type:: ca_descr

.. code-block:: c

    struct ca_descr {
	unsigned int index;
	unsigned int parity;
	unsigned char cw[8];
    };
