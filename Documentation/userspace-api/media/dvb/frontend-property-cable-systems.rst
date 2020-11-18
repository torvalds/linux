.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _frontend-property-cable-systems:

*****************************************
Properties used on cable delivery systems
*****************************************


.. _dvbc-params:

DVB-C delivery system
=====================

The DVB-C Annex-A is the widely used cable standard. Transmission uses
QAM modulation.

The DVB-C Annex-C is optimized for 6MHz, and is used in Japan. It
supports a subset of the Annex A modulation types, and a roll-off of
0.13, instead of 0.15

The following parameters are valid for DVB-C Annex A/C:

-  :ref:`DTV_API_VERSION <DTV-API-VERSION>`

-  :ref:`DTV_DELIVERY_SYSTEM <DTV-DELIVERY-SYSTEM>`

-  :ref:`DTV_TUNE <DTV-TUNE>`

-  :ref:`DTV_CLEAR <DTV-CLEAR>`

-  :ref:`DTV_FREQUENCY <DTV-FREQUENCY>`

-  :ref:`DTV_MODULATION <DTV-MODULATION>`

-  :ref:`DTV_INVERSION <DTV-INVERSION>`

-  :ref:`DTV_SYMBOL_RATE <DTV-SYMBOL-RATE>`

-  :ref:`DTV_INNER_FEC <DTV-INNER-FEC>`

-  :ref:`DTV_LNA <DTV-LNA>`

In addition, the :ref:`DTV QoS statistics <frontend-stat-properties>`
are also valid.


.. _dvbc-annex-b-params:

DVB-C Annex B delivery system
=============================

The DVB-C Annex-B is only used on a few Countries like the United
States.

The following parameters are valid for DVB-C Annex B:

-  :ref:`DTV_API_VERSION <DTV-API-VERSION>`

-  :ref:`DTV_DELIVERY_SYSTEM <DTV-DELIVERY-SYSTEM>`

-  :ref:`DTV_TUNE <DTV-TUNE>`

-  :ref:`DTV_CLEAR <DTV-CLEAR>`

-  :ref:`DTV_FREQUENCY <DTV-FREQUENCY>`

-  :ref:`DTV_MODULATION <DTV-MODULATION>`

-  :ref:`DTV_INVERSION <DTV-INVERSION>`

-  :ref:`DTV_LNA <DTV-LNA>`

In addition, the :ref:`DTV QoS statistics <frontend-stat-properties>`
are also valid.
