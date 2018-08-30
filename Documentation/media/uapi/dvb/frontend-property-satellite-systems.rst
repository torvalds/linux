.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _frontend-property-satellite-systems:

*********************************************
Properties used on satellite delivery systems
*********************************************


.. _dvbs-params:

DVB-S delivery system
=====================

The following parameters are valid for DVB-S:

-  :ref:`DTV_API_VERSION <DTV-API-VERSION>`

-  :ref:`DTV_DELIVERY_SYSTEM <DTV-DELIVERY-SYSTEM>`

-  :ref:`DTV_TUNE <DTV-TUNE>`

-  :ref:`DTV_CLEAR <DTV-CLEAR>`

-  :ref:`DTV_FREQUENCY <DTV-FREQUENCY>`

-  :ref:`DTV_INVERSION <DTV-INVERSION>`

-  :ref:`DTV_SYMBOL_RATE <DTV-SYMBOL-RATE>`

-  :ref:`DTV_INNER_FEC <DTV-INNER-FEC>`

-  :ref:`DTV_VOLTAGE <DTV-VOLTAGE>`

-  :ref:`DTV_TONE <DTV-TONE>`

In addition, the :ref:`DTV QoS statistics <frontend-stat-properties>`
are also valid.

Future implementations might add those two missing parameters:

-  :ref:`DTV_DISEQC_MASTER <DTV-DISEQC-MASTER>`

-  :ref:`DTV_DISEQC_SLAVE_REPLY <DTV-DISEQC-SLAVE-REPLY>`


.. _dvbs2-params:

DVB-S2 delivery system
======================

In addition to all parameters valid for DVB-S, DVB-S2 supports the
following parameters:

-  :ref:`DTV_MODULATION <DTV-MODULATION>`

-  :ref:`DTV_PILOT <DTV-PILOT>`

-  :ref:`DTV_ROLLOFF <DTV-ROLLOFF>`

-  :ref:`DTV_STREAM_ID <DTV-STREAM-ID>`

-  :ref:`DTV_SCRAMBLING_SEQUENCE_INDEX <DTV-SCRAMBLING-SEQUENCE-INDEX>`

In addition, the :ref:`DTV QoS statistics <frontend-stat-properties>`
are also valid.


.. _turbo-params:

Turbo code delivery system
==========================

In addition to all parameters valid for DVB-S, turbo code supports the
following parameters:

-  :ref:`DTV_MODULATION <DTV-MODULATION>`


.. _isdbs-params:

ISDB-S delivery system
======================

The following parameters are valid for ISDB-S:

-  :ref:`DTV_API_VERSION <DTV-API-VERSION>`

-  :ref:`DTV_DELIVERY_SYSTEM <DTV-DELIVERY-SYSTEM>`

-  :ref:`DTV_TUNE <DTV-TUNE>`

-  :ref:`DTV_CLEAR <DTV-CLEAR>`

-  :ref:`DTV_FREQUENCY <DTV-FREQUENCY>`

-  :ref:`DTV_INVERSION <DTV-INVERSION>`

-  :ref:`DTV_SYMBOL_RATE <DTV-SYMBOL-RATE>`

-  :ref:`DTV_INNER_FEC <DTV-INNER-FEC>`

-  :ref:`DTV_VOLTAGE <DTV-VOLTAGE>`

-  :ref:`DTV_STREAM_ID <DTV-STREAM-ID>`
