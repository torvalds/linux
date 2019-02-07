.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _fe_property_parameters:

******************************
Digital TV property parameters
******************************

There are several different Digital TV parameters that can be used by
:ref:`FE_SET_PROPERTY and FE_GET_PROPERTY ioctls<FE_GET_PROPERTY>`.
This section describes each of them. Please notice, however, that only
a subset of them are needed to setup a frontend.


.. _DTV-UNDEFINED:

DTV_UNDEFINED
=============

Used internally. A GET/SET operation for it won't change or return
anything.


.. _DTV-TUNE:

DTV_TUNE
========

Interpret the cache of data, build either a traditional frontend
tunerequest so we can pass validation in the ``FE_SET_FRONTEND`` ioctl.


.. _DTV-CLEAR:

DTV_CLEAR
=========

Reset a cache of data specific to the frontend here. This does not
effect hardware.


.. _DTV-FREQUENCY:

DTV_FREQUENCY
=============

Frequency of the digital TV transponder/channel.

.. note::

  #. For satellite delivery systems, the frequency is in kHz.

  #. For cable and terrestrial delivery systems, the frequency is in
     Hz.

  #. On most delivery systems, the frequency is the center frequency
     of the transponder/channel. The exception is for ISDB-T, where
     the main carrier has a 1/7 offset from the center.

  #. For ISDB-T, the channels are usually transmitted with an offset of
     about 143kHz. E.g. a valid frequency could be 474,143 kHz. The
     stepping is  bound to the bandwidth of the channel which is
     typically 6MHz.

  #. In ISDB-Tsb, the channel consists of only one or three segments the
     frequency step is 429kHz, 3*429 respectively.


.. _DTV-MODULATION:

DTV_MODULATION
==============

Specifies the frontend modulation type for delivery systems that
supports more multiple modulations.

The modulation can be one of the types defined by enum :c:type:`fe_modulation`.

Most of the digital TV standards offers more than one possible
modulation type.

The table below presents a summary of the types of modulation types
supported by each delivery system, as currently defined by specs.

======================= =======================================================
Standard		Modulation types
======================= =======================================================
ATSC (version 1)	8-VSB and 16-VSB.
DMTB			4-QAM, 16-QAM, 32-QAM, 64-QAM and 4-QAM-NR.
DVB-C Annex A/C		16-QAM, 32-QAM, 64-QAM and 256-QAM.
DVB-C Annex B		64-QAM.
DVB-T			QPSK, 16-QAM and 64-QAM.
DVB-T2			QPSK, 16-QAM, 64-QAM and 256-QAM.
DVB-S			No need to set. It supports only QPSK.
DVB-S2			QPSK, 8-PSK, 16-APSK and 32-APSK.
ISDB-T			QPSK, DQPSK, 16-QAM and 64-QAM.
ISDB-S			8-PSK, QPSK and BPSK.
======================= =======================================================

.. note::

   Please notice that some of the above modulation types may not be
   defined currently at the Kernel. The reason is simple: no driver
   needed such definition yet.


.. _DTV-BANDWIDTH-HZ:

DTV_BANDWIDTH_HZ
================

Bandwidth for the channel, in HZ.

Should be set only for terrestrial delivery systems.

Possible values: ``1712000``, ``5000000``, ``6000000``, ``7000000``,
``8000000``, ``10000000``.

======================= =======================================================
Terrestrial Standard	Possible values for bandwidth
======================= =======================================================
ATSC (version 1)	No need to set. It is always 6MHz.
DMTB			No need to set. It is always 8MHz.
DVB-T			6MHz, 7MHz and 8MHz.
DVB-T2			1.172 MHz, 5MHz, 6MHz, 7MHz, 8MHz and 10MHz
ISDB-T			5MHz, 6MHz, 7MHz and 8MHz, although most places
			use 6MHz.
======================= =======================================================


.. note::


  #. For ISDB-Tsb, the bandwidth can vary depending on the number of
     connected segments.

     It can be easily derived from other parameters
     (DTV_ISDBT_SB_SEGMENT_IDX, DTV_ISDBT_SB_SEGMENT_COUNT).

  #. On Satellite and Cable delivery systems, the bandwidth depends on
     the symbol rate. So, the Kernel will silently ignore any setting
     :ref:`DTV-BANDWIDTH-HZ`. I will however fill it back with a
     bandwidth estimation.

     Such bandwidth estimation takes into account the symbol rate set with
     :ref:`DTV-SYMBOL-RATE`, and the rolloff factor, with is fixed for
     DVB-C and DVB-S.

     For DVB-S2, the rolloff should also be set via :ref:`DTV-ROLLOFF`.


.. _DTV-INVERSION:

DTV_INVERSION
=============

Specifies if the frontend should do spectral inversion or not.

The acceptable values are defined by :c:type:`fe_spectral_inversion`.


.. _DTV-DISEQC-MASTER:

DTV_DISEQC_MASTER
=================

Currently not implemented.


.. _DTV-SYMBOL-RATE:

DTV_SYMBOL_RATE
===============

Used on cable and satellite delivery systems.

Digital TV symbol rate, in bauds (symbols/second).


.. _DTV-INNER-FEC:

DTV_INNER_FEC
=============

Used on cable and satellite delivery systems.

The acceptable values are defined by :c:type:`fe_code_rate`.


.. _DTV-VOLTAGE:

DTV_VOLTAGE
===========

Used on satellite delivery systems.

The voltage is usually used with non-DiSEqC capable LNBs to switch the
polarzation (horizontal/vertical). When using DiSEqC epuipment this
voltage has to be switched consistently to the DiSEqC commands as
described in the DiSEqC spec.

The acceptable values are defined by :c:type:`fe_sec_voltage`.


.. _DTV-TONE:

DTV_TONE
========

Currently not used.


.. _DTV-PILOT:

DTV_PILOT
=========

Used on DVB-S2.

Sets DVB-S2 pilot.

The acceptable values are defined by :c:type:`fe_pilot`.


.. _DTV-ROLLOFF:

DTV_ROLLOFF
===========

Used on DVB-S2.

Sets DVB-S2 rolloff.

The acceptable values are defined by :c:type:`fe_rolloff`.


.. _DTV-DISEQC-SLAVE-REPLY:

DTV_DISEQC_SLAVE_REPLY
======================

Currently not implemented.


.. _DTV-FE-CAPABILITY-COUNT:

DTV_FE_CAPABILITY_COUNT
=======================

Currently not implemented.


.. _DTV-FE-CAPABILITY:

DTV_FE_CAPABILITY
=================

Currently not implemented.


.. _DTV-DELIVERY-SYSTEM:

DTV_DELIVERY_SYSTEM
===================

Specifies the type of the delivery system.

The acceptable values are defined by :c:type:`fe_delivery_system`.


.. _DTV-ISDBT-PARTIAL-RECEPTION:

DTV_ISDBT_PARTIAL_RECEPTION
===========================

Used only on ISDB.

If ``DTV_ISDBT_SOUND_BROADCASTING`` is '0' this bit-field represents
whether the channel is in partial reception mode or not.

If '1' ``DTV_ISDBT_LAYERA_*`` values are assigned to the center segment
and ``DTV_ISDBT_LAYERA_SEGMENT_COUNT`` has to be '1'.

If in addition ``DTV_ISDBT_SOUND_BROADCASTING`` is '1'
``DTV_ISDBT_PARTIAL_RECEPTION`` represents whether this ISDB-Tsb channel
is consisting of one segment and layer or three segments and two layers.

Possible values: 0, 1, -1 (AUTO)


.. _DTV-ISDBT-SOUND-BROADCASTING:

DTV_ISDBT_SOUND_BROADCASTING
============================

Used only on ISDB.

This field represents whether the other DTV_ISDBT_*-parameters are
referring to an ISDB-T and an ISDB-Tsb channel. (See also
``DTV_ISDBT_PARTIAL_RECEPTION``).

Possible values: 0, 1, -1 (AUTO)


.. _DTV-ISDBT-SB-SUBCHANNEL-ID:

DTV_ISDBT_SB_SUBCHANNEL_ID
==========================

Used only on ISDB.

This field only applies if ``DTV_ISDBT_SOUND_BROADCASTING`` is '1'.

(Note of the author: This might not be the correct description of the
``SUBCHANNEL-ID`` in all details, but it is my understanding of the
technical background needed to program a device)

An ISDB-Tsb channel (1 or 3 segments) can be broadcasted alone or in a
set of connected ISDB-Tsb channels. In this set of channels every
channel can be received independently. The number of connected ISDB-Tsb
segment can vary, e.g. depending on the frequency spectrum bandwidth
available.

Example: Assume 8 ISDB-Tsb connected segments are broadcasted. The
broadcaster has several possibilities to put those channels in the air:
Assuming a normal 13-segment ISDB-T spectrum he can align the 8 segments
from position 1-8 to 5-13 or anything in between.

The underlying layer of segments are subchannels: each segment is
consisting of several subchannels with a predefined IDs. A sub-channel
is used to help the demodulator to synchronize on the channel.

An ISDB-T channel is always centered over all sub-channels. As for the
example above, in ISDB-Tsb it is no longer as simple as that.

``The DTV_ISDBT_SB_SUBCHANNEL_ID`` parameter is used to give the
sub-channel ID of the segment to be demodulated.

Possible values: 0 .. 41, -1 (AUTO)


.. _DTV-ISDBT-SB-SEGMENT-IDX:

DTV_ISDBT_SB_SEGMENT_IDX
========================

Used only on ISDB.

This field only applies if ``DTV_ISDBT_SOUND_BROADCASTING`` is '1'.

``DTV_ISDBT_SB_SEGMENT_IDX`` gives the index of the segment to be
demodulated for an ISDB-Tsb channel where several of them are
transmitted in the connected manner.

Possible values: 0 .. ``DTV_ISDBT_SB_SEGMENT_COUNT`` - 1

Note: This value cannot be determined by an automatic channel search.


.. _DTV-ISDBT-SB-SEGMENT-COUNT:

DTV_ISDBT_SB_SEGMENT_COUNT
==========================

Used only on ISDB.

This field only applies if ``DTV_ISDBT_SOUND_BROADCASTING`` is '1'.

``DTV_ISDBT_SB_SEGMENT_COUNT`` gives the total count of connected
ISDB-Tsb channels.

Possible values: 1 .. 13

Note: This value cannot be determined by an automatic channel search.


.. _isdb-hierq-layers:

DTV-ISDBT-LAYER[A-C] parameters
===============================

Used only on ISDB.

ISDB-T channels can be coded hierarchically. As opposed to DVB-T in
ISDB-T hierarchical layers can be decoded simultaneously. For that
reason a ISDB-T demodulator has 3 Viterbi and 3 Reed-Solomon decoders.

ISDB-T has 3 hierarchical layers which each can use a part of the
available segments. The total number of segments over all layers has to
13 in ISDB-T.

There are 3 parameter sets, for Layers A, B and C.


.. _DTV-ISDBT-LAYER-ENABLED:

DTV_ISDBT_LAYER_ENABLED
-----------------------

Used only on ISDB.

Hierarchical reception in ISDB-T is achieved by enabling or disabling
layers in the decoding process. Setting all bits of
``DTV_ISDBT_LAYER_ENABLED`` to '1' forces all layers (if applicable) to
be demodulated. This is the default.

If the channel is in the partial reception mode
(``DTV_ISDBT_PARTIAL_RECEPTION`` = 1) the central segment can be decoded
independently of the other 12 segments. In that mode layer A has to have
a ``SEGMENT_COUNT`` of 1.

In ISDB-Tsb only layer A is used, it can be 1 or 3 in ISDB-Tsb according
to ``DTV_ISDBT_PARTIAL_RECEPTION``. ``SEGMENT_COUNT`` must be filled
accordingly.

Only the values of the first 3 bits are used. Other bits will be silently ignored:

``DTV_ISDBT_LAYER_ENABLED`` bit 0: layer A enabled

``DTV_ISDBT_LAYER_ENABLED`` bit 1: layer B enabled

``DTV_ISDBT_LAYER_ENABLED`` bit 2: layer C enabled

``DTV_ISDBT_LAYER_ENABLED`` bits 3-31: unused


.. _DTV-ISDBT-LAYER-FEC:

DTV_ISDBT_LAYER[A-C]_FEC
------------------------

Used only on ISDB.

The Forward Error Correction mechanism used by a given ISDB Layer, as
defined by :c:type:`fe_code_rate`.


Possible values are: ``FEC_AUTO``, ``FEC_1_2``, ``FEC_2_3``, ``FEC_3_4``,
``FEC_5_6``, ``FEC_7_8``


.. _DTV-ISDBT-LAYER-MODULATION:

DTV_ISDBT_LAYER[A-C]_MODULATION
-------------------------------

Used only on ISDB.

The modulation used by a given ISDB Layer, as defined by
:c:type:`fe_modulation`.

Possible values are: ``QAM_AUTO``, ``QPSK``, ``QAM_16``, ``QAM_64``, ``DQPSK``

.. note::

   #. If layer C is ``DQPSK``, then layer B has to be ``DQPSK``.

   #. If layer B is ``DQPSK`` and ``DTV_ISDBT_PARTIAL_RECEPTION``\ = 0,
      then layer has to be ``DQPSK``.


.. _DTV-ISDBT-LAYER-SEGMENT-COUNT:

DTV_ISDBT_LAYER[A-C]_SEGMENT_COUNT
----------------------------------

Used only on ISDB.

Possible values: 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, -1 (AUTO)

Note: Truth table for ``DTV_ISDBT_SOUND_BROADCASTING`` and
``DTV_ISDBT_PARTIAL_RECEPTION`` and ``LAYER[A-C]_SEGMENT_COUNT``

.. _isdbt-layer_seg-cnt-table:

.. flat-table:: Truth table for ISDB-T Sound Broadcasting
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  Partial Reception

       -  Sound Broadcasting

       -  Layer A width

       -  Layer B width

       -  Layer C width

       -  total width

    -  .. row 2

       -  0

       -  0

       -  1 .. 13

       -  1 .. 13

       -  1 .. 13

       -  13

    -  .. row 3

       -  1

       -  0

       -  1

       -  1 .. 13

       -  1 .. 13

       -  13

    -  .. row 4

       -  0

       -  1

       -  1

       -  0

       -  0

       -  1

    -  .. row 5

       -  1

       -  1

       -  1

       -  2

       -  0

       -  13



.. _DTV-ISDBT-LAYER-TIME-INTERLEAVING:

DTV_ISDBT_LAYER[A-C]_TIME_INTERLEAVING
--------------------------------------

Used only on ISDB.

Valid values: 0, 1, 2, 4, -1 (AUTO)

when DTV_ISDBT_SOUND_BROADCASTING is active, value 8 is also valid.

Note: The real time interleaving length depends on the mode (fft-size).
The values here are referring to what can be found in the
TMCC-structure, as shown in the table below.


.. c:type:: isdbt_layer_interleaving_table

.. flat-table:: ISDB-T time interleaving modes
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ``DTV_ISDBT_LAYER[A-C]_TIME_INTERLEAVING``

       -  Mode 1 (2K FFT)

       -  Mode 2 (4K FFT)

       -  Mode 3 (8K FFT)

    -  .. row 2

       -  0

       -  0

       -  0

       -  0

    -  .. row 3

       -  1

       -  4

       -  2

       -  1

    -  .. row 4

       -  2

       -  8

       -  4

       -  2

    -  .. row 5

       -  4

       -  16

       -  8

       -  4



.. _DTV-ATSCMH-FIC-VER:

DTV_ATSCMH_FIC_VER
------------------

Used only on ATSC-MH.

Version number of the FIC (Fast Information Channel) signaling data.

FIC is used for relaying information to allow rapid service acquisition
by the receiver.

Possible values: 0, 1, 2, 3, ..., 30, 31


.. _DTV-ATSCMH-PARADE-ID:

DTV_ATSCMH_PARADE_ID
--------------------

Used only on ATSC-MH.

Parade identification number

A parade is a collection of up to eight MH groups, conveying one or two
ensembles.

Possible values: 0, 1, 2, 3, ..., 126, 127


.. _DTV-ATSCMH-NOG:

DTV_ATSCMH_NOG
--------------

Used only on ATSC-MH.

Number of MH groups per MH subframe for a designated parade.

Possible values: 1, 2, 3, 4, 5, 6, 7, 8


.. _DTV-ATSCMH-TNOG:

DTV_ATSCMH_TNOG
---------------

Used only on ATSC-MH.

Total number of MH groups including all MH groups belonging to all MH
parades in one MH subframe.

Possible values: 0, 1, 2, 3, ..., 30, 31


.. _DTV-ATSCMH-SGN:

DTV_ATSCMH_SGN
--------------

Used only on ATSC-MH.

Start group number.

Possible values: 0, 1, 2, 3, ..., 14, 15


.. _DTV-ATSCMH-PRC:

DTV_ATSCMH_PRC
--------------

Used only on ATSC-MH.

Parade repetition cycle.

Possible values: 1, 2, 3, 4, 5, 6, 7, 8


.. _DTV-ATSCMH-RS-FRAME-MODE:

DTV_ATSCMH_RS_FRAME_MODE
------------------------

Used only on ATSC-MH.

Reed Solomon (RS) frame mode.

The acceptable values are defined by :c:type:`atscmh_rs_frame_mode`.


.. _DTV-ATSCMH-RS-FRAME-ENSEMBLE:

DTV_ATSCMH_RS_FRAME_ENSEMBLE
----------------------------

Used only on ATSC-MH.

Reed Solomon(RS) frame ensemble.

The acceptable values are defined by :c:type:`atscmh_rs_frame_ensemble`.


.. _DTV-ATSCMH-RS-CODE-MODE-PRI:

DTV_ATSCMH_RS_CODE_MODE_PRI
---------------------------

Used only on ATSC-MH.

Reed Solomon (RS) code mode (primary).

The acceptable values are defined by :c:type:`atscmh_rs_code_mode`.


.. _DTV-ATSCMH-RS-CODE-MODE-SEC:

DTV_ATSCMH_RS_CODE_MODE_SEC
---------------------------

Used only on ATSC-MH.

Reed Solomon (RS) code mode (secondary).

The acceptable values are defined by :c:type:`atscmh_rs_code_mode`.


.. _DTV-ATSCMH-SCCC-BLOCK-MODE:

DTV_ATSCMH_SCCC_BLOCK_MODE
--------------------------

Used only on ATSC-MH.

Series Concatenated Convolutional Code Block Mode.

The acceptable values are defined by :c:type:`atscmh_sccc_block_mode`.


.. _DTV-ATSCMH-SCCC-CODE-MODE-A:

DTV_ATSCMH_SCCC_CODE_MODE_A
---------------------------

Used only on ATSC-MH.

Series Concatenated Convolutional Code Rate.

The acceptable values are defined by :c:type:`atscmh_sccc_code_mode`.

.. _DTV-ATSCMH-SCCC-CODE-MODE-B:

DTV_ATSCMH_SCCC_CODE_MODE_B
---------------------------

Used only on ATSC-MH.

Series Concatenated Convolutional Code Rate.

Possible values are the same as documented on enum
:c:type:`atscmh_sccc_code_mode`.


.. _DTV-ATSCMH-SCCC-CODE-MODE-C:

DTV_ATSCMH_SCCC_CODE_MODE_C
---------------------------

Used only on ATSC-MH.

Series Concatenated Convolutional Code Rate.

Possible values are the same as documented on enum
:c:type:`atscmh_sccc_code_mode`.


.. _DTV-ATSCMH-SCCC-CODE-MODE-D:

DTV_ATSCMH_SCCC_CODE_MODE_D
---------------------------

Used only on ATSC-MH.

Series Concatenated Convolutional Code Rate.

Possible values are the same as documented on enum
:c:type:`atscmh_sccc_code_mode`.


.. _DTV-API-VERSION:

DTV_API_VERSION
===============

Returns the major/minor version of the Digital TV API


.. _DTV-CODE-RATE-HP:

DTV_CODE_RATE_HP
================

Used on terrestrial transmissions.

The acceptable values are defined by :c:type:`fe_transmit_mode`.


.. _DTV-CODE-RATE-LP:

DTV_CODE_RATE_LP
================

Used on terrestrial transmissions.

The acceptable values are defined by :c:type:`fe_transmit_mode`.


.. _DTV-GUARD-INTERVAL:

DTV_GUARD_INTERVAL
==================

The acceptable values are defined by :c:type:`fe_guard_interval`.

.. note::

   #. If ``DTV_GUARD_INTERVAL`` is set the ``GUARD_INTERVAL_AUTO`` the
      hardware will try to find the correct guard interval (if capable) and
      will use TMCC to fill in the missing parameters.
   #. Intervals ``GUARD_INTERVAL_1_128``, ``GUARD_INTERVAL_19_128``
      and ``GUARD_INTERVAL_19_256`` are used only for DVB-T2 at
      present.
   #. Intervals ``GUARD_INTERVAL_PN420``, ``GUARD_INTERVAL_PN595`` and
      ``GUARD_INTERVAL_PN945`` are used only for DMTB at the present.
      On such standard, only those intervals and ``GUARD_INTERVAL_AUTO``
      are valid.

.. _DTV-TRANSMISSION-MODE:

DTV_TRANSMISSION_MODE
=====================


Used only on OFTM-based standards, e. g. DVB-T/T2, ISDB-T, DTMB.

Specifies the FFT size (with corresponds to the approximate number of
carriers) used by the standard.

The acceptable values are defined by :c:type:`fe_transmit_mode`.

.. note::

   #. ISDB-T supports three carrier/symbol-size: 8K, 4K, 2K. It is called
      **mode** on such standard, and are numbered from 1 to 3:

      ====	========	========================
      Mode	FFT size	Transmission mode
      ====	========	========================
      1		2K		``TRANSMISSION_MODE_2K``
      2		4K		``TRANSMISSION_MODE_4K``
      3		8K		``TRANSMISSION_MODE_8K``
      ====	========	========================

   #. If ``DTV_TRANSMISSION_MODE`` is set the ``TRANSMISSION_MODE_AUTO``
      the hardware will try to find the correct FFT-size (if capable) and
      will use TMCC to fill in the missing parameters.

   #. DVB-T specifies 2K and 8K as valid sizes.

   #. DVB-T2 specifies 1K, 2K, 4K, 8K, 16K and 32K.

   #. DTMB specifies C1 and C3780.


.. _DTV-HIERARCHY:

DTV_HIERARCHY
=============

Used only on DVB-T and DVB-T2.

Frontend hierarchy.

The acceptable values are defined by :c:type:`fe_hierarchy`.


.. _DTV-STREAM-ID:

DTV_STREAM_ID
=============

Used on DVB-S2, DVB-T2 and ISDB-S.

DVB-S2, DVB-T2 and ISDB-S support the transmission of several streams on
a single transport stream. This property enables the digital TV driver to
handle substream filtering, when supported by the hardware. By default,
substream filtering is disabled.

For DVB-S2 and DVB-T2, the valid substream id range is from 0 to 255.

For ISDB, the valid substream id range is from 1 to 65535.

To disable it, you should use the special macro NO_STREAM_ID_FILTER.

Note: any value outside the id range also disables filtering.


.. _DTV-DVBT2-PLP-ID-LEGACY:

DTV_DVBT2_PLP_ID_LEGACY
=======================

Obsolete, replaced with DTV_STREAM_ID.


.. _DTV-ENUM-DELSYS:

DTV_ENUM_DELSYS
===============

A Multi standard frontend needs to advertise the delivery systems
provided. Applications need to enumerate the provided delivery systems,
before using any other operation with the frontend. Prior to it's
introduction, FE_GET_INFO was used to determine a frontend type. A
frontend which provides more than a single delivery system,
FE_GET_INFO doesn't help much. Applications which intends to use a
multistandard frontend must enumerate the delivery systems associated
with it, rather than trying to use FE_GET_INFO. In the case of a
legacy frontend, the result is just the same as with FE_GET_INFO, but
in a more structured format

The acceptable values are defined by :c:type:`fe_delivery_system`.


.. _DTV-INTERLEAVING:

DTV_INTERLEAVING
================

Time interleaving to be used.

The acceptable values are defined by :c:type:`fe_interleaving`.


.. _DTV-LNA:

DTV_LNA
=======

Low-noise amplifier.

Hardware might offer controllable LNA which can be set manually using
that parameter. Usually LNA could be found only from terrestrial devices
if at all.

Possible values: 0, 1, LNA_AUTO

0, LNA off

1, LNA on

use the special macro LNA_AUTO to set LNA auto


.. _DTV-SCRAMBLING-SEQUENCE-INDEX:

DTV_SCRAMBLING_SEQUENCE_INDEX
=============================

Used on DVB-S2.

This 18 bit field, when present, carries the index of the DVB-S2 physical
layer scrambling sequence as defined in clause 5.5.4 of EN 302 307.
There is no explicit signalling method to convey scrambling sequence index
to the receiver. If S2 satellite delivery system descriptor is available
it can be used to read the scrambling sequence index (EN 300 468 table 41).

By default, gold scrambling sequence index 0 is used.

The valid scrambling sequence index range is from 0 to 262142.
