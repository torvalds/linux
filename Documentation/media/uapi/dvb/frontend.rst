.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _dvb_frontend:

#######################
Digital TV Frontend API
#######################

The Digital TV frontend API was designed to support three groups of delivery
systems: Terrestrial, cable and Satellite. Currently, the following
delivery systems are supported:

-  Terrestrial systems: DVB-T, DVB-T2, ATSC, ATSC M/H, ISDB-T, DVB-H,
   DTMB, CMMB

-  Cable systems: DVB-C Annex A/C, ClearQAM (DVB-C Annex B)

-  Satellite systems: DVB-S, DVB-S2, DVB Turbo, ISDB-S, DSS

The Digital TV frontend controls several sub-devices including:

-  Tuner

-  Digital TV demodulator

-  Low noise amplifier (LNA)

-  Satellite Equipment Control (SEC) [#f1]_.

The frontend can be accessed through ``/dev/dvb/adapter?/frontend?``.
Data types and ioctl definitions can be accessed by including
``linux/dvb/frontend.h`` in your application.

.. note::

   Transmission via the internet (DVB-IP) and MMT (MPEG Media Transport)
   is not yet handled by this API but a future extension is possible.

.. [#f1]

   On Satellite systems, the API support for the Satellite Equipment
   Control (SEC) allows to power control and to send/receive signals to
   control the antenna subsystem, selecting the polarization and choosing
   the Intermediate Frequency IF) of the Low Noise Block Converter Feed
   Horn (LNBf). It supports the DiSEqC and V-SEC protocols. The DiSEqC
   (digital SEC) specification is available at
   `Eutelsat <http://www.eutelsat.com/satellites/4_5_5.html>`__.


.. toctree::
    :maxdepth: 1

    query-dvb-frontend-info
    dvb-fe-read-status
    dvbproperty
    frontend_fcalls
