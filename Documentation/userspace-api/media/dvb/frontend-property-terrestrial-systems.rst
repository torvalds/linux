.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _frontend-property-terrestrial-systems:

***********************************************
Properties used on terrestrial delivery systems
***********************************************


.. _dvbt-params:

DVB-T delivery system
=====================

The following parameters are valid for DVB-T:

-  :ref:`DTV_API_VERSION <DTV-API-VERSION>`

-  :ref:`DTV_DELIVERY_SYSTEM <DTV-DELIVERY-SYSTEM>`

-  :ref:`DTV_TUNE <DTV-TUNE>`

-  :ref:`DTV_CLEAR <DTV-CLEAR>`

-  :ref:`DTV_FREQUENCY <DTV-FREQUENCY>`

-  :ref:`DTV_MODULATION <DTV-MODULATION>`

-  :ref:`DTV_BANDWIDTH_HZ <DTV-BANDWIDTH-HZ>`

-  :ref:`DTV_INVERSION <DTV-INVERSION>`

-  :ref:`DTV_CODE_RATE_HP <DTV-CODE-RATE-HP>`

-  :ref:`DTV_CODE_RATE_LP <DTV-CODE-RATE-LP>`

-  :ref:`DTV_GUARD_INTERVAL <DTV-GUARD-INTERVAL>`

-  :ref:`DTV_TRANSMISSION_MODE <DTV-TRANSMISSION-MODE>`

-  :ref:`DTV_HIERARCHY <DTV-HIERARCHY>`

-  :ref:`DTV_LNA <DTV-LNA>`

In addition, the :ref:`DTV QoS statistics <frontend-stat-properties>`
are also valid.


.. _dvbt2-params:

DVB-T2 delivery system
======================

DVB-T2 support is currently in the early stages of development, so
expect that this section maygrow and become more detailed with time.

The following parameters are valid for DVB-T2:

-  :ref:`DTV_API_VERSION <DTV-API-VERSION>`

-  :ref:`DTV_DELIVERY_SYSTEM <DTV-DELIVERY-SYSTEM>`

-  :ref:`DTV_TUNE <DTV-TUNE>`

-  :ref:`DTV_CLEAR <DTV-CLEAR>`

-  :ref:`DTV_FREQUENCY <DTV-FREQUENCY>`

-  :ref:`DTV_MODULATION <DTV-MODULATION>`

-  :ref:`DTV_BANDWIDTH_HZ <DTV-BANDWIDTH-HZ>`

-  :ref:`DTV_INVERSION <DTV-INVERSION>`

-  :ref:`DTV_CODE_RATE_HP <DTV-CODE-RATE-HP>`

-  :ref:`DTV_CODE_RATE_LP <DTV-CODE-RATE-LP>`

-  :ref:`DTV_GUARD_INTERVAL <DTV-GUARD-INTERVAL>`

-  :ref:`DTV_TRANSMISSION_MODE <DTV-TRANSMISSION-MODE>`

-  :ref:`DTV_HIERARCHY <DTV-HIERARCHY>`

-  :ref:`DTV_STREAM_ID <DTV-STREAM-ID>`

-  :ref:`DTV_LNA <DTV-LNA>`

In addition, the :ref:`DTV QoS statistics <frontend-stat-properties>`
are also valid.


.. _isdbt:

ISDB-T delivery system
======================

This ISDB-T/ISDB-Tsb API extension should reflect all information needed
to tune any ISDB-T/ISDB-Tsb hardware. Of course it is possible that some
very sophisticated devices won't need certain parameters to tune.

The information given here should help application writers to know how
to handle ISDB-T and ISDB-Tsb hardware using the Linux Digital TV API.

The details given here about ISDB-T and ISDB-Tsb are just enough to
basically show the dependencies between the needed parameter values, but
surely some information is left out. For more detailed information see
the following documents:

ARIB STD-B31 - "Transmission System for Digital Terrestrial Television
Broadcasting" and

ARIB TR-B14 - "Operational Guidelines for Digital Terrestrial Television
Broadcasting".

In order to understand the ISDB specific parameters, one has to have
some knowledge the channel structure in ISDB-T and ISDB-Tsb. I.e. it has
to be known to the reader that an ISDB-T channel consists of 13
segments, that it can have up to 3 layer sharing those segments, and
things like that.

The following parameters are valid for ISDB-T:

-  :ref:`DTV_API_VERSION <DTV-API-VERSION>`

-  :ref:`DTV_DELIVERY_SYSTEM <DTV-DELIVERY-SYSTEM>`

-  :ref:`DTV_TUNE <DTV-TUNE>`

-  :ref:`DTV_CLEAR <DTV-CLEAR>`

-  :ref:`DTV_FREQUENCY <DTV-FREQUENCY>`

-  :ref:`DTV_BANDWIDTH_HZ <DTV-BANDWIDTH-HZ>`

-  :ref:`DTV_INVERSION <DTV-INVERSION>`

-  :ref:`DTV_GUARD_INTERVAL <DTV-GUARD-INTERVAL>`

-  :ref:`DTV_TRANSMISSION_MODE <DTV-TRANSMISSION-MODE>`

-  :ref:`DTV_ISDBT_LAYER_ENABLED <DTV-ISDBT-LAYER-ENABLED>`

-  :ref:`DTV_ISDBT_PARTIAL_RECEPTION <DTV-ISDBT-PARTIAL-RECEPTION>`

-  :ref:`DTV_ISDBT_SOUND_BROADCASTING <DTV-ISDBT-SOUND-BROADCASTING>`

-  :ref:`DTV_ISDBT_SB_SUBCHANNEL_ID <DTV-ISDBT-SB-SUBCHANNEL-ID>`

-  :ref:`DTV_ISDBT_SB_SEGMENT_IDX <DTV-ISDBT-SB-SEGMENT-IDX>`

-  :ref:`DTV_ISDBT_SB_SEGMENT_COUNT <DTV-ISDBT-SB-SEGMENT-COUNT>`

-  :ref:`DTV_ISDBT_LAYERA_FEC <DTV-ISDBT-LAYER-FEC>`

-  :ref:`DTV_ISDBT_LAYERA_MODULATION <DTV-ISDBT-LAYER-MODULATION>`

-  :ref:`DTV_ISDBT_LAYERA_SEGMENT_COUNT <DTV-ISDBT-LAYER-SEGMENT-COUNT>`

-  :ref:`DTV_ISDBT_LAYERA_TIME_INTERLEAVING <DTV-ISDBT-LAYER-TIME-INTERLEAVING>`

-  :ref:`DTV_ISDBT_LAYERB_FEC <DTV-ISDBT-LAYER-FEC>`

-  :ref:`DTV_ISDBT_LAYERB_MODULATION <DTV-ISDBT-LAYER-MODULATION>`

-  :ref:`DTV_ISDBT_LAYERB_SEGMENT_COUNT <DTV-ISDBT-LAYER-SEGMENT-COUNT>`

-  :ref:`DTV_ISDBT_LAYERB_TIME_INTERLEAVING <DTV-ISDBT-LAYER-TIME-INTERLEAVING>`

-  :ref:`DTV_ISDBT_LAYERC_FEC <DTV-ISDBT-LAYER-FEC>`

-  :ref:`DTV_ISDBT_LAYERC_MODULATION <DTV-ISDBT-LAYER-MODULATION>`

-  :ref:`DTV_ISDBT_LAYERC_SEGMENT_COUNT <DTV-ISDBT-LAYER-SEGMENT-COUNT>`

-  :ref:`DTV_ISDBT_LAYERC_TIME_INTERLEAVING <DTV-ISDBT-LAYER-TIME-INTERLEAVING>`

In addition, the :ref:`DTV QoS statistics <frontend-stat-properties>`
are also valid.


.. _atsc-params:

ATSC delivery system
====================

The following parameters are valid for ATSC:

-  :ref:`DTV_API_VERSION <DTV-API-VERSION>`

-  :ref:`DTV_DELIVERY_SYSTEM <DTV-DELIVERY-SYSTEM>`

-  :ref:`DTV_TUNE <DTV-TUNE>`

-  :ref:`DTV_CLEAR <DTV-CLEAR>`

-  :ref:`DTV_FREQUENCY <DTV-FREQUENCY>`

-  :ref:`DTV_MODULATION <DTV-MODULATION>`

-  :ref:`DTV_BANDWIDTH_HZ <DTV-BANDWIDTH-HZ>`

In addition, the :ref:`DTV QoS statistics <frontend-stat-properties>`
are also valid.


.. _atscmh-params:

ATSC-MH delivery system
=======================

The following parameters are valid for ATSC-MH:

-  :ref:`DTV_API_VERSION <DTV-API-VERSION>`

-  :ref:`DTV_DELIVERY_SYSTEM <DTV-DELIVERY-SYSTEM>`

-  :ref:`DTV_TUNE <DTV-TUNE>`

-  :ref:`DTV_CLEAR <DTV-CLEAR>`

-  :ref:`DTV_FREQUENCY <DTV-FREQUENCY>`

-  :ref:`DTV_BANDWIDTH_HZ <DTV-BANDWIDTH-HZ>`

-  :ref:`DTV_ATSCMH_FIC_VER <DTV-ATSCMH-FIC-VER>`

-  :ref:`DTV_ATSCMH_PARADE_ID <DTV-ATSCMH-PARADE-ID>`

-  :ref:`DTV_ATSCMH_NOG <DTV-ATSCMH-NOG>`

-  :ref:`DTV_ATSCMH_TNOG <DTV-ATSCMH-TNOG>`

-  :ref:`DTV_ATSCMH_SGN <DTV-ATSCMH-SGN>`

-  :ref:`DTV_ATSCMH_PRC <DTV-ATSCMH-PRC>`

-  :ref:`DTV_ATSCMH_RS_FRAME_MODE <DTV-ATSCMH-RS-FRAME-MODE>`

-  :ref:`DTV_ATSCMH_RS_FRAME_ENSEMBLE <DTV-ATSCMH-RS-FRAME-ENSEMBLE>`

-  :ref:`DTV_ATSCMH_RS_CODE_MODE_PRI <DTV-ATSCMH-RS-CODE-MODE-PRI>`

-  :ref:`DTV_ATSCMH_RS_CODE_MODE_SEC <DTV-ATSCMH-RS-CODE-MODE-SEC>`

-  :ref:`DTV_ATSCMH_SCCC_BLOCK_MODE <DTV-ATSCMH-SCCC-BLOCK-MODE>`

-  :ref:`DTV_ATSCMH_SCCC_CODE_MODE_A <DTV-ATSCMH-SCCC-CODE-MODE-A>`

-  :ref:`DTV_ATSCMH_SCCC_CODE_MODE_B <DTV-ATSCMH-SCCC-CODE-MODE-B>`

-  :ref:`DTV_ATSCMH_SCCC_CODE_MODE_C <DTV-ATSCMH-SCCC-CODE-MODE-C>`

-  :ref:`DTV_ATSCMH_SCCC_CODE_MODE_D <DTV-ATSCMH-SCCC-CODE-MODE-D>`

In addition, the :ref:`DTV QoS statistics <frontend-stat-properties>`
are also valid.


.. _dtmb-params:

DTMB delivery system
====================

The following parameters are valid for DTMB:

-  :ref:`DTV_API_VERSION <DTV-API-VERSION>`

-  :ref:`DTV_DELIVERY_SYSTEM <DTV-DELIVERY-SYSTEM>`

-  :ref:`DTV_TUNE <DTV-TUNE>`

-  :ref:`DTV_CLEAR <DTV-CLEAR>`

-  :ref:`DTV_FREQUENCY <DTV-FREQUENCY>`

-  :ref:`DTV_MODULATION <DTV-MODULATION>`

-  :ref:`DTV_BANDWIDTH_HZ <DTV-BANDWIDTH-HZ>`

-  :ref:`DTV_INVERSION <DTV-INVERSION>`

-  :ref:`DTV_INNER_FEC <DTV-INNER-FEC>`

-  :ref:`DTV_GUARD_INTERVAL <DTV-GUARD-INTERVAL>`

-  :ref:`DTV_TRANSMISSION_MODE <DTV-TRANSMISSION-MODE>`

-  :ref:`DTV_INTERLEAVING <DTV-INTERLEAVING>`

-  :ref:`DTV_LNA <DTV-LNA>`

In addition, the :ref:`DTV QoS statistics <frontend-stat-properties>`
are also valid.
