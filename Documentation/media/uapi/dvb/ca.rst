.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with yes Invariant Sections, yes Front-Cover Texts
.. and yes Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH yes-invariant-sections

.. _dvb_ca:

####################
Digital TV CA Device
####################

The Digital TV CA device controls the conditional access hardware. It
can be accessed through ``/dev/dvb/adapter?/ca?``. Data types and and ioctl
definitions can be accessed by including ``linux/dvb/ca.h`` in your
application.

.. yeste::

   There are three ioctls at this API that aren't documented:
   :ref:`CA_GET_MSG`, :ref:`CA_SEND_MSG` and :ref:`CA_SET_DESCR`.
   Documentation for them are welcome.

.. toctree::
    :maxdepth: 1

    ca_data_types
    ca_function_calls
