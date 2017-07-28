.. -*- coding: utf-8; mode: rst -*-

.. _dvb_frontend:

################
DVB Frontend API
################
The DVB frontend API was designed to support three types of delivery
systems:

-  Terrestrial systems: DVB-T, DVB-T2, ATSC, ATSC M/H, ISDB-T, DVB-H,
   DTMB, CMMB

-  Cable systems: DVB-C Annex A/C, ClearQAM (DVB-C Annex B), ISDB-C

-  Satellite systems: DVB-S, DVB-S2, DVB Turbo, ISDB-S, DSS

The DVB frontend controls several sub-devices including:

-  Tuner

-  Digital TV demodulator

-  Low noise amplifier (LNA)

-  Satellite Equipment Control (SEC) hardware (only for Satellite).

The frontend can be accessed through ``/dev/dvb/adapter?/frontend?``.
Data types and ioctl definitions can be accessed by including
``linux/dvb/frontend.h`` in your application.

.. note::

   Transmission via the internet (DVB-IP) is not yet handled by this
   API but a future extension is possible.

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
    frontend_legacy_dvbv3_api
