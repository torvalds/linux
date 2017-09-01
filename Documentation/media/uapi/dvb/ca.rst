.. -*- coding: utf-8; mode: rst -*-

.. _dvb_ca:

#############
DVB CA Device
#############
The DVB CA device controls the conditional access hardware. It can be
accessed through ``/dev/dvb/adapter?/ca?``. Data types and and ioctl
definitions can be accessed by including ``linux/dvb/ca.h`` in your
application.

.. note::

   There are three ioctls at this API that aren't documented:
   :ref:`CA_GET_MSG`, :ref:`CA_SEND_MSG` and :ref:`CA_SET_DESCR`.
   Documentation for them are welcome.

.. toctree::
    :maxdepth: 1

    ca_data_types
    ca_function_calls
