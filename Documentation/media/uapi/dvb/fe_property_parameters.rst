.. -*- coding: utf-8; mode: rst -*-

.. _fe_property_parameters:

******************************
Digital TV property parameters
******************************


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
supports more than one modulation type. The modulation can be one of the
types defined by enum :ref:`fe_modulation <fe-modulation>`.


.. _fe-modulation-t:

Modulation property
-------------------

Most of the digital TV standards currently offers more than one possible
modulation (sometimes called as "constellation" on some standards). This
enum contains the values used by the Kernel. Please note that not all
modulations are supported by a given standard.


.. _fe-modulation:

.. flat-table:: enum fe_modulation
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ID

       -  Description

    -  .. row 2

       -  .. _QPSK:

	  ``QPSK``

       -  QPSK modulation

    -  .. row 3

       -  .. _QAM-16:

	  ``QAM_16``

       -  16-QAM modulation

    -  .. row 4

       -  .. _QAM-32:

	  ``QAM_32``

       -  32-QAM modulation

    -  .. row 5

       -  .. _QAM-64:

	  ``QAM_64``

       -  64-QAM modulation

    -  .. row 6

       -  .. _QAM-128:

	  ``QAM_128``

       -  128-QAM modulation

    -  .. row 7

       -  .. _QAM-256:

	  ``QAM_256``

       -  256-QAM modulation

    -  .. row 8

       -  .. _QAM-AUTO:

	  ``QAM_AUTO``

       -  Autodetect QAM modulation

    -  .. row 9

       -  .. _VSB-8:

	  ``VSB_8``

       -  8-VSB modulation

    -  .. row 10

       -  .. _VSB-16:

	  ``VSB_16``

       -  16-VSB modulation

    -  .. row 11

       -  .. _PSK-8:

	  ``PSK_8``

       -  8-PSK modulation

    -  .. row 12

       -  .. _APSK-16:

	  ``APSK_16``

       -  16-APSK modulation

    -  .. row 13

       -  .. _APSK-32:

	  ``APSK_32``

       -  32-APSK modulation

    -  .. row 14

       -  .. _DQPSK:

	  ``DQPSK``

       -  DQPSK modulation

    -  .. row 15

       -  .. _QAM-4-NR:

	  ``QAM_4_NR``

       -  4-QAM-NR modulation



.. _DTV-BANDWIDTH-HZ:

DTV_BANDWIDTH_HZ
================

Bandwidth for the channel, in HZ.

Possible values: ``1712000``, ``5000000``, ``6000000``, ``7000000``,
``8000000``, ``10000000``.

.. note::

  #. DVB-T supports 6, 7 and 8MHz.

  #. DVB-T2 supports 1.172, 5, 6, 7, 8 and 10MHz.

  #. ISDB-T supports 5MHz, 6MHz, 7MHz and 8MHz, although most
     places use 6MHz.

  #. On DVB-C and DVB-S/S2, the bandwidth depends on the symbol rate.
     So, the Kernel will silently ignore setting :ref:`DTV-BANDWIDTH-HZ`.

  #. For DVB-C and DVB-S/S2, the Kernel will return an estimation of the
     bandwidth, calculated from :ref:`DTV-SYMBOL-RATE` and from
     the rolloff, with is fixed for DVB-C and DVB-S.

  #. For DVB-S2, the bandwidth estimation will use :ref:`DTV-ROLLOFF`.

  #. For ISDB-Tsb, it can vary depending on the number of connected
     segments.

  #. Bandwidth in ISDB-Tsb can be easily derived from other parameters
     (DTV_ISDBT_SB_SEGMENT_IDX, DTV_ISDBT_SB_SEGMENT_COUNT).


.. _DTV-INVERSION:

DTV_INVERSION
=============

Specifies if the frontend should do spectral inversion or not.


.. _fe-spectral-inversion-t:

enum fe_modulation: Frontend spectral inversion
-----------------------------------------------

This parameter indicates if spectral inversion should be presumed or
not. In the automatic setting (``INVERSION_AUTO``) the hardware will try
to figure out the correct setting by itself. If the hardware doesn't
support, the DVB core will try to lock at the carrier first with
inversion off. If it fails, it will try to enable inversion.


.. _fe-spectral-inversion:

.. flat-table:: enum fe_modulation
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ID

       -  Description

    -  .. row 2

       -  .. _INVERSION-OFF:

	  ``INVERSION_OFF``

       -  Don't do spectral band inversion.

    -  .. row 3

       -  .. _INVERSION-ON:

	  ``INVERSION_ON``

       -  Do spectral band inversion.

    -  .. row 4

       -  .. _INVERSION-AUTO:

	  ``INVERSION_AUTO``

       -  Autodetect spectral band inversion.



.. _DTV-DISEQC-MASTER:

DTV_DISEQC_MASTER
=================

Currently not implemented.


.. _DTV-SYMBOL-RATE:

DTV_SYMBOL_RATE
===============

Digital TV symbol rate, in bauds (symbols/second). Used on cable
standards.


.. _DTV-INNER-FEC:

DTV_INNER_FEC
=============

Used cable/satellite transmissions. The acceptable values are:


.. _fe-code-rate-t:

enum fe_code_rate: type of the Forward Error Correction.
--------------------------------------------------------


.. _fe-code-rate:

.. flat-table:: enum fe_code_rate
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ID

       -  Description

    -  .. row 2

       -  .. _FEC-NONE:

	  ``FEC_NONE``

       -  No Forward Error Correction Code

    -  .. row 3

       -  .. _FEC-AUTO:

	  ``FEC_AUTO``

       -  Autodetect Error Correction Code

    -  .. row 4

       -  .. _FEC-1-2:

	  ``FEC_1_2``

       -  Forward Error Correction Code 1/2

    -  .. row 5

       -  .. _FEC-2-3:

	  ``FEC_2_3``

       -  Forward Error Correction Code 2/3

    -  .. row 6

       -  .. _FEC-3-4:

	  ``FEC_3_4``

       -  Forward Error Correction Code 3/4

    -  .. row 7

       -  .. _FEC-4-5:

	  ``FEC_4_5``

       -  Forward Error Correction Code 4/5

    -  .. row 8

       -  .. _FEC-5-6:

	  ``FEC_5_6``

       -  Forward Error Correction Code 5/6

    -  .. row 9

       -  .. _FEC-6-7:

	  ``FEC_6_7``

       -  Forward Error Correction Code 6/7

    -  .. row 10

       -  .. _FEC-7-8:

	  ``FEC_7_8``

       -  Forward Error Correction Code 7/8

    -  .. row 11

       -  .. _FEC-8-9:

	  ``FEC_8_9``

       -  Forward Error Correction Code 8/9

    -  .. row 12

       -  .. _FEC-9-10:

	  ``FEC_9_10``

       -  Forward Error Correction Code 9/10

    -  .. row 13

       -  .. _FEC-2-5:

	  ``FEC_2_5``

       -  Forward Error Correction Code 2/5

    -  .. row 14

       -  .. _FEC-3-5:

	  ``FEC_3_5``

       -  Forward Error Correction Code 3/5



.. _DTV-VOLTAGE:

DTV_VOLTAGE
===========

The voltage is usually used with non-DiSEqC capable LNBs to switch the
polarzation (horizontal/vertical). When using DiSEqC epuipment this
voltage has to be switched consistently to the DiSEqC commands as
described in the DiSEqC spec.


.. _fe-sec-voltage:

.. flat-table:: enum fe_sec_voltage
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ID

       -  Description

    -  .. row 2

       -  .. _SEC-VOLTAGE-13:

	  ``SEC_VOLTAGE_13``

       -  Set DC voltage level to 13V

    -  .. row 3

       -  .. _SEC-VOLTAGE-18:

	  ``SEC_VOLTAGE_18``

       -  Set DC voltage level to 18V

    -  .. row 4

       -  .. _SEC-VOLTAGE-OFF:

	  ``SEC_VOLTAGE_OFF``

       -  Don't send any voltage to the antenna



.. _DTV-TONE:

DTV_TONE
========

Currently not used.


.. _DTV-PILOT:

DTV_PILOT
=========

Sets DVB-S2 pilot


.. _fe-pilot-t:

fe_pilot type
-------------


.. _fe-pilot:

.. flat-table:: enum fe_pilot
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ID

       -  Description

    -  .. row 2

       -  .. _PILOT-ON:

	  ``PILOT_ON``

       -  Pilot tones enabled

    -  .. row 3

       -  .. _PILOT-OFF:

	  ``PILOT_OFF``

       -  Pilot tones disabled

    -  .. row 4

       -  .. _PILOT-AUTO:

	  ``PILOT_AUTO``

       -  Autodetect pilot tones



.. _DTV-ROLLOFF:

DTV_ROLLOFF
===========

Sets DVB-S2 rolloff


.. _fe-rolloff-t:

fe_rolloff type
---------------


.. _fe-rolloff:

.. flat-table:: enum fe_rolloff
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ID

       -  Description

    -  .. row 2

       -  .. _ROLLOFF-35:

	  ``ROLLOFF_35``

       -  Roloff factor: α=35%

    -  .. row 3

       -  .. _ROLLOFF-20:

	  ``ROLLOFF_20``

       -  Roloff factor: α=20%

    -  .. row 4

       -  .. _ROLLOFF-25:

	  ``ROLLOFF_25``

       -  Roloff factor: α=25%

    -  .. row 5

       -  .. _ROLLOFF-AUTO:

	  ``ROLLOFF_AUTO``

       -  Auto-detect the roloff factor.



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

Specifies the type of Delivery system


.. _fe-delivery-system-t:

fe_delivery_system type
-----------------------

Possible values:


.. _fe-delivery-system:

.. flat-table:: enum fe_delivery_system
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ID

       -  Description

    -  .. row 2

       -  .. _SYS-UNDEFINED:

	  ``SYS_UNDEFINED``

       -  Undefined standard. Generally, indicates an error

    -  .. row 3

       -  .. _SYS-DVBC-ANNEX-A:

	  ``SYS_DVBC_ANNEX_A``

       -  Cable TV: DVB-C following ITU-T J.83 Annex A spec

    -  .. row 4

       -  .. _SYS-DVBC-ANNEX-B:

	  ``SYS_DVBC_ANNEX_B``

       -  Cable TV: DVB-C following ITU-T J.83 Annex B spec (ClearQAM)

    -  .. row 5

       -  .. _SYS-DVBC-ANNEX-C:

	  ``SYS_DVBC_ANNEX_C``

       -  Cable TV: DVB-C following ITU-T J.83 Annex C spec

    -  .. row 6

       -  .. _SYS-ISDBC:

	  ``SYS_ISDBC``

       -  Cable TV: ISDB-C (no drivers yet)

    -  .. row 7

       -  .. _SYS-DVBT:

	  ``SYS_DVBT``

       -  Terrestral TV: DVB-T

    -  .. row 8

       -  .. _SYS-DVBT2:

	  ``SYS_DVBT2``

       -  Terrestral TV: DVB-T2

    -  .. row 9

       -  .. _SYS-ISDBT:

	  ``SYS_ISDBT``

       -  Terrestral TV: ISDB-T

    -  .. row 10

       -  .. _SYS-ATSC:

	  ``SYS_ATSC``

       -  Terrestral TV: ATSC

    -  .. row 11

       -  .. _SYS-ATSCMH:

	  ``SYS_ATSCMH``

       -  Terrestral TV (mobile): ATSC-M/H

    -  .. row 12

       -  .. _SYS-DTMB:

	  ``SYS_DTMB``

       -  Terrestrial TV: DTMB

    -  .. row 13

       -  .. _SYS-DVBS:

	  ``SYS_DVBS``

       -  Satellite TV: DVB-S

    -  .. row 14

       -  .. _SYS-DVBS2:

	  ``SYS_DVBS2``

       -  Satellite TV: DVB-S2

    -  .. row 15

       -  .. _SYS-TURBO:

	  ``SYS_TURBO``

       -  Satellite TV: DVB-S Turbo

    -  .. row 16

       -  .. _SYS-ISDBS:

	  ``SYS_ISDBS``

       -  Satellite TV: ISDB-S

    -  .. row 17

       -  .. _SYS-DAB:

	  ``SYS_DAB``

       -  Digital audio: DAB (not fully supported)

    -  .. row 18

       -  .. _SYS-DSS:

	  ``SYS_DSS``

       -  Satellite TV:"DSS (not fully supported)

    -  .. row 19

       -  .. _SYS-CMMB:

	  ``SYS_CMMB``

       -  Terrestral TV (mobile):CMMB (not fully supported)

    -  .. row 20

       -  .. _SYS-DVBH:

	  ``SYS_DVBH``

       -  Terrestral TV (mobile): DVB-H (standard deprecated)



.. _DTV-ISDBT-PARTIAL-RECEPTION:

DTV_ISDBT_PARTIAL_RECEPTION
===========================

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

This field represents whether the other DTV_ISDBT_*-parameters are
referring to an ISDB-T and an ISDB-Tsb channel. (See also
``DTV_ISDBT_PARTIAL_RECEPTION``).

Possible values: 0, 1, -1 (AUTO)


.. _DTV-ISDBT-SB-SUBCHANNEL-ID:

DTV_ISDBT_SB_SUBCHANNEL_ID
==========================

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

This field only applies if ``DTV_ISDBT_SOUND_BROADCASTING`` is '1'.

``DTV_ISDBT_SB_SEGMENT_IDX`` gives the index of the segment to be
demodulated for an ISDB-Tsb channel where several of them are
transmitted in the connected manner.

Possible values: 0 .. ``DTV_ISDBT_SB_SEGMENT_COUNT`` - 1

Note: This value cannot be determined by an automatic channel search.


.. _DTV-ISDBT-SB-SEGMENT-COUNT:

DTV_ISDBT_SB_SEGMENT_COUNT
==========================

This field only applies if ``DTV_ISDBT_SOUND_BROADCASTING`` is '1'.

``DTV_ISDBT_SB_SEGMENT_COUNT`` gives the total count of connected
ISDB-Tsb channels.

Possible values: 1 .. 13

Note: This value cannot be determined by an automatic channel search.


.. _isdb-hierq-layers:

DTV-ISDBT-LAYER[A-C] parameters
===============================

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

Possible values: ``FEC_AUTO``, ``FEC_1_2``, ``FEC_2_3``, ``FEC_3_4``,
``FEC_5_6``, ``FEC_7_8``


.. _DTV-ISDBT-LAYER-MODULATION:

DTV_ISDBT_LAYER[A-C]_MODULATION
-------------------------------

Possible values: ``QAM_AUTO``, QP\ ``SK, QAM_16``, ``QAM_64``, ``DQPSK``

Note: If layer C is ``DQPSK`` layer B has to be ``DQPSK``. If layer B is
``DQPSK`` and ``DTV_ISDBT_PARTIAL_RECEPTION``\ =0 layer has to be
``DQPSK``.


.. _DTV-ISDBT-LAYER-SEGMENT-COUNT:

DTV_ISDBT_LAYER[A-C]_SEGMENT_COUNT
----------------------------------

Possible values: 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, -1 (AUTO)

Note: Truth table for ``DTV_ISDBT_SOUND_BROADCASTING`` and
``DTV_ISDBT_PARTIAL_RECEPTION`` and ``LAYER[A-C]_SEGMENT_COUNT``

.. _isdbt-layer_seg-cnt-table:

.. flat-table:: Truth table for ISDB-T Sound Broadcasting
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  PR

       -  SB

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

Valid values: 0, 1, 2, 4, -1 (AUTO)

when DTV_ISDBT_SOUND_BROADCASTING is active, value 8 is also valid.

Note: The real time interleaving length depends on the mode (fft-size).
The values here are referring to what can be found in the
TMCC-structure, as shown in the table below.


.. _isdbt-layer-interleaving-table:

.. flat-table:: ISDB-T time interleaving modes
    :header-rows:  0
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

Version number of the FIC (Fast Information Channel) signaling data.

FIC is used for relaying information to allow rapid service acquisition
by the receiver.

Possible values: 0, 1, 2, 3, ..., 30, 31


.. _DTV-ATSCMH-PARADE-ID:

DTV_ATSCMH_PARADE_ID
--------------------

Parade identification number

A parade is a collection of up to eight MH groups, conveying one or two
ensembles.

Possible values: 0, 1, 2, 3, ..., 126, 127


.. _DTV-ATSCMH-NOG:

DTV_ATSCMH_NOG
--------------

Number of MH groups per MH subframe for a designated parade.

Possible values: 1, 2, 3, 4, 5, 6, 7, 8


.. _DTV-ATSCMH-TNOG:

DTV_ATSCMH_TNOG
---------------

Total number of MH groups including all MH groups belonging to all MH
parades in one MH subframe.

Possible values: 0, 1, 2, 3, ..., 30, 31


.. _DTV-ATSCMH-SGN:

DTV_ATSCMH_SGN
--------------

Start group number.

Possible values: 0, 1, 2, 3, ..., 14, 15


.. _DTV-ATSCMH-PRC:

DTV_ATSCMH_PRC
--------------

Parade repetition cycle.

Possible values: 1, 2, 3, 4, 5, 6, 7, 8


.. _DTV-ATSCMH-RS-FRAME-MODE:

DTV_ATSCMH_RS_FRAME_MODE
------------------------

Reed Solomon (RS) frame mode.

Possible values are:

.. tabularcolumns:: |p{5.0cm}|p{12.5cm}|

.. _atscmh-rs-frame-mode:

.. flat-table:: enum atscmh_rs_frame_mode
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ID

       -  Description

    -  .. row 2

       -  .. _ATSCMH-RSFRAME-PRI-ONLY:

	  ``ATSCMH_RSFRAME_PRI_ONLY``

       -  Single Frame: There is only a primary RS Frame for all Group
	  Regions.

    -  .. row 3

       -  .. _ATSCMH-RSFRAME-PRI-SEC:

	  ``ATSCMH_RSFRAME_PRI_SEC``

       -  Dual Frame: There are two separate RS Frames: Primary RS Frame for
	  Group Region A and B and Secondary RS Frame for Group Region C and
	  D.



.. _DTV-ATSCMH-RS-FRAME-ENSEMBLE:

DTV_ATSCMH_RS_FRAME_ENSEMBLE
----------------------------

Reed Solomon(RS) frame ensemble.

Possible values are:


.. _atscmh-rs-frame-ensemble:

.. flat-table:: enum atscmh_rs_frame_ensemble
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ID

       -  Description

    -  .. row 2

       -  .. _ATSCMH-RSFRAME-ENS-PRI:

	  ``ATSCMH_RSFRAME_ENS_PRI``

       -  Primary Ensemble.

    -  .. row 3

       -  .. _ATSCMH-RSFRAME-ENS-SEC:

	  ``AATSCMH_RSFRAME_PRI_SEC``

       -  Secondary Ensemble.

    -  .. row 4

       -  .. _ATSCMH-RSFRAME-RES:

	  ``AATSCMH_RSFRAME_RES``

       -  Reserved. Shouldn't be used.



.. _DTV-ATSCMH-RS-CODE-MODE-PRI:

DTV_ATSCMH_RS_CODE_MODE_PRI
---------------------------

Reed Solomon (RS) code mode (primary).

Possible values are:


.. _atscmh-rs-code-mode:

.. flat-table:: enum atscmh_rs_code_mode
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ID

       -  Description

    -  .. row 2

       -  .. _ATSCMH-RSCODE-211-187:

	  ``ATSCMH_RSCODE_211_187``

       -  Reed Solomon code (211,187).

    -  .. row 3

       -  .. _ATSCMH-RSCODE-223-187:

	  ``ATSCMH_RSCODE_223_187``

       -  Reed Solomon code (223,187).

    -  .. row 4

       -  .. _ATSCMH-RSCODE-235-187:

	  ``ATSCMH_RSCODE_235_187``

       -  Reed Solomon code (235,187).

    -  .. row 5

       -  .. _ATSCMH-RSCODE-RES:

	  ``ATSCMH_RSCODE_RES``

       -  Reserved. Shouldn't be used.



.. _DTV-ATSCMH-RS-CODE-MODE-SEC:

DTV_ATSCMH_RS_CODE_MODE_SEC
---------------------------

Reed Solomon (RS) code mode (secondary).

Possible values are the same as documented on enum
:ref:`atscmh_rs_code_mode <atscmh-rs-code-mode>`:


.. _DTV-ATSCMH-SCCC-BLOCK-MODE:

DTV_ATSCMH_SCCC_BLOCK_MODE
--------------------------

Series Concatenated Convolutional Code Block Mode.

Possible values are:

.. tabularcolumns:: |p{4.5cm}|p{13.0cm}|

.. _atscmh-sccc-block-mode:

.. flat-table:: enum atscmh_scc_block_mode
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ID

       -  Description

    -  .. row 2

       -  .. _ATSCMH-SCCC-BLK-SEP:

	  ``ATSCMH_SCCC_BLK_SEP``

       -  Separate SCCC: the SCCC outer code mode shall be set independently
	  for each Group Region (A, B, C, D)

    -  .. row 3

       -  .. _ATSCMH-SCCC-BLK-COMB:

	  ``ATSCMH_SCCC_BLK_COMB``

       -  Combined SCCC: all four Regions shall have the same SCCC outer
	  code mode.

    -  .. row 4

       -  .. _ATSCMH-SCCC-BLK-RES:

	  ``ATSCMH_SCCC_BLK_RES``

       -  Reserved. Shouldn't be used.



.. _DTV-ATSCMH-SCCC-CODE-MODE-A:

DTV_ATSCMH_SCCC_CODE_MODE_A
---------------------------

Series Concatenated Convolutional Code Rate.

Possible values are:


.. _atscmh-sccc-code-mode:

.. flat-table:: enum atscmh_sccc_code_mode
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ID

       -  Description

    -  .. row 2

       -  .. _ATSCMH-SCCC-CODE-HLF:

	  ``ATSCMH_SCCC_CODE_HLF``

       -  The outer code rate of a SCCC Block is 1/2 rate.

    -  .. row 3

       -  .. _ATSCMH-SCCC-CODE-QTR:

	  ``ATSCMH_SCCC_CODE_QTR``

       -  The outer code rate of a SCCC Block is 1/4 rate.

    -  .. row 4

       -  .. _ATSCMH-SCCC-CODE-RES:

	  ``ATSCMH_SCCC_CODE_RES``

       -  to be documented.



.. _DTV-ATSCMH-SCCC-CODE-MODE-B:

DTV_ATSCMH_SCCC_CODE_MODE_B
---------------------------

Series Concatenated Convolutional Code Rate.

Possible values are the same as documented on enum
:ref:`atscmh_sccc_code_mode <atscmh-sccc-code-mode>`.


.. _DTV-ATSCMH-SCCC-CODE-MODE-C:

DTV_ATSCMH_SCCC_CODE_MODE_C
---------------------------

Series Concatenated Convolutional Code Rate.

Possible values are the same as documented on enum
:ref:`atscmh_sccc_code_mode <atscmh-sccc-code-mode>`.


.. _DTV-ATSCMH-SCCC-CODE-MODE-D:

DTV_ATSCMH_SCCC_CODE_MODE_D
---------------------------

Series Concatenated Convolutional Code Rate.

Possible values are the same as documented on enum
:ref:`atscmh_sccc_code_mode <atscmh-sccc-code-mode>`.


.. _DTV-API-VERSION:

DTV_API_VERSION
===============

Returns the major/minor version of the DVB API


.. _DTV-CODE-RATE-HP:

DTV_CODE_RATE_HP
================

Used on terrestrial transmissions. The acceptable values are the ones
described at :ref:`fe_transmit_mode_t <fe-transmit-mode-t>`.


.. _DTV-CODE-RATE-LP:

DTV_CODE_RATE_LP
================

Used on terrestrial transmissions. The acceptable values are the ones
described at :ref:`fe_transmit_mode_t <fe-transmit-mode-t>`.


.. _DTV-GUARD-INTERVAL:

DTV_GUARD_INTERVAL
==================

Possible values are:


.. _fe-guard-interval-t:

Modulation guard interval
-------------------------


.. _fe-guard-interval:

.. flat-table:: enum fe_guard_interval
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ID

       -  Description

    -  .. row 2

       -  .. _GUARD-INTERVAL-AUTO:

	  ``GUARD_INTERVAL_AUTO``

       -  Autodetect the guard interval

    -  .. row 3

       -  .. _GUARD-INTERVAL-1-128:

	  ``GUARD_INTERVAL_1_128``

       -  Guard interval 1/128

    -  .. row 4

       -  .. _GUARD-INTERVAL-1-32:

	  ``GUARD_INTERVAL_1_32``

       -  Guard interval 1/32

    -  .. row 5

       -  .. _GUARD-INTERVAL-1-16:

	  ``GUARD_INTERVAL_1_16``

       -  Guard interval 1/16

    -  .. row 6

       -  .. _GUARD-INTERVAL-1-8:

	  ``GUARD_INTERVAL_1_8``

       -  Guard interval 1/8

    -  .. row 7

       -  .. _GUARD-INTERVAL-1-4:

	  ``GUARD_INTERVAL_1_4``

       -  Guard interval 1/4

    -  .. row 8

       -  .. _GUARD-INTERVAL-19-128:

	  ``GUARD_INTERVAL_19_128``

       -  Guard interval 19/128

    -  .. row 9

       -  .. _GUARD-INTERVAL-19-256:

	  ``GUARD_INTERVAL_19_256``

       -  Guard interval 19/256

    -  .. row 10

       -  .. _GUARD-INTERVAL-PN420:

	  ``GUARD_INTERVAL_PN420``

       -  PN length 420 (1/4)

    -  .. row 11

       -  .. _GUARD-INTERVAL-PN595:

	  ``GUARD_INTERVAL_PN595``

       -  PN length 595 (1/6)

    -  .. row 12

       -  .. _GUARD-INTERVAL-PN945:

	  ``GUARD_INTERVAL_PN945``

       -  PN length 945 (1/9)


Notes:

1) If ``DTV_GUARD_INTERVAL`` is set the ``GUARD_INTERVAL_AUTO`` the
hardware will try to find the correct guard interval (if capable) and
will use TMCC to fill in the missing parameters.

2) Intervals 1/128, 19/128 and 19/256 are used only for DVB-T2 at
present

3) DTMB specifies PN420, PN595 and PN945.


.. _DTV-TRANSMISSION-MODE:

DTV_TRANSMISSION_MODE
=====================

Specifies the number of carriers used by the standard. This is used only
on OFTM-based standards, e. g. DVB-T/T2, ISDB-T, DTMB


.. _fe-transmit-mode-t:

enum fe_transmit_mode: Number of carriers per channel
-----------------------------------------------------

.. tabularcolumns:: |p{5.0cm}|p{12.5cm}|

.. _fe-transmit-mode:

.. flat-table:: enum fe_transmit_mode
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ID

       -  Description

    -  .. row 2

       -  .. _TRANSMISSION-MODE-AUTO:

	  ``TRANSMISSION_MODE_AUTO``

       -  Autodetect transmission mode. The hardware will try to find the
	  correct FFT-size (if capable) to fill in the missing parameters.

    -  .. row 3

       -  .. _TRANSMISSION-MODE-1K:

	  ``TRANSMISSION_MODE_1K``

       -  Transmission mode 1K

    -  .. row 4

       -  .. _TRANSMISSION-MODE-2K:

	  ``TRANSMISSION_MODE_2K``

       -  Transmission mode 2K

    -  .. row 5

       -  .. _TRANSMISSION-MODE-8K:

	  ``TRANSMISSION_MODE_8K``

       -  Transmission mode 8K

    -  .. row 6

       -  .. _TRANSMISSION-MODE-4K:

	  ``TRANSMISSION_MODE_4K``

       -  Transmission mode 4K

    -  .. row 7

       -  .. _TRANSMISSION-MODE-16K:

	  ``TRANSMISSION_MODE_16K``

       -  Transmission mode 16K

    -  .. row 8

       -  .. _TRANSMISSION-MODE-32K:

	  ``TRANSMISSION_MODE_32K``

       -  Transmission mode 32K

    -  .. row 9

       -  .. _TRANSMISSION-MODE-C1:

	  ``TRANSMISSION_MODE_C1``

       -  Single Carrier (C=1) transmission mode (DTMB)

    -  .. row 10

       -  .. _TRANSMISSION-MODE-C3780:

	  ``TRANSMISSION_MODE_C3780``

       -  Multi Carrier (C=3780) transmission mode (DTMB)


Notes:

1) ISDB-T supports three carrier/symbol-size: 8K, 4K, 2K. It is called
'mode' in the standard: Mode 1 is 2K, mode 2 is 4K, mode 3 is 8K

2) If ``DTV_TRANSMISSION_MODE`` is set the ``TRANSMISSION_MODE_AUTO``
the hardware will try to find the correct FFT-size (if capable) and will
use TMCC to fill in the missing parameters.

3) DVB-T specifies 2K and 8K as valid sizes.

4) DVB-T2 specifies 1K, 2K, 4K, 8K, 16K and 32K.

5) DTMB specifies C1 and C3780.


.. _DTV-HIERARCHY:

DTV_HIERARCHY
=============

Frontend hierarchy


.. _fe-hierarchy-t:

Frontend hierarchy
------------------


.. _fe-hierarchy:

.. flat-table:: enum fe_hierarchy
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ID

       -  Description

    -  .. row 2

       -  .. _HIERARCHY-NONE:

	  ``HIERARCHY_NONE``

       -  No hierarchy

    -  .. row 3

       -  .. _HIERARCHY-AUTO:

	  ``HIERARCHY_AUTO``

       -  Autodetect hierarchy (if supported)

    -  .. row 4

       -  .. _HIERARCHY-1:

	  ``HIERARCHY_1``

       -  Hierarchy 1

    -  .. row 5

       -  .. _HIERARCHY-2:

	  ``HIERARCHY_2``

       -  Hierarchy 2

    -  .. row 6

       -  .. _HIERARCHY-4:

	  ``HIERARCHY_4``

       -  Hierarchy 4



.. _DTV-STREAM-ID:

DTV_STREAM_ID
=============

DVB-S2, DVB-T2 and ISDB-S support the transmission of several streams on
a single transport stream. This property enables the DVB driver to
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


.. _DTV-INTERLEAVING:

DTV_INTERLEAVING
================

Time interleaving to be used. Currently, used only on DTMB.


.. _fe-interleaving:

.. flat-table:: enum fe_interleaving
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ID

       -  Description

    -  .. row 2

       -  .. _INTERLEAVING-NONE:

	  ``INTERLEAVING_NONE``

       -  No interleaving.

    -  .. row 3

       -  .. _INTERLEAVING-AUTO:

	  ``INTERLEAVING_AUTO``

       -  Auto-detect interleaving.

    -  .. row 4

       -  .. _INTERLEAVING-240:

	  ``INTERLEAVING_240``

       -  Interleaving of 240 symbols.

    -  .. row 5

       -  .. _INTERLEAVING-720:

	  ``INTERLEAVING_720``

       -  Interleaving of 720 symbols.



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
