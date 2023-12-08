.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later

.. _frontend-stat-properties:

******************************
Frontend statistics indicators
******************************

The values are returned via ``dtv_property.stat``. If the property is
supported, ``dtv_property.stat.len`` is bigger than zero.

For most delivery systems, ``dtv_property.stat.len`` will be 1 if the
stats is supported, and the properties will return a single value for
each parameter.

It should be noted, however, that new OFDM delivery systems like ISDB
can use different modulation types for each group of carriers. On such
standards, up to 3 groups of statistics can be provided, and
``dtv_property.stat.len`` is updated to reflect the "global" metrics,
plus one metric per each carrier group (called "layer" on ISDB).

So, in order to be consistent with other delivery systems, the first
value at :c:type:`dtv_property.stat.dtv_stats <dtv_stats>` array refers
to the global metric. The other elements of the array represent each
layer, starting from layer A(index 1), layer B (index 2) and so on.

The number of filled elements are stored at ``dtv_property.stat.len``.

Each element of the ``dtv_property.stat.dtv_stats`` array consists on
two elements:

-  ``svalue`` or ``uvalue``, where ``svalue`` is for signed values of
   the measure (dB measures) and ``uvalue`` is for unsigned values
   (counters, relative scale)

-  ``scale`` - Scale for the value. It can be:

   -  ``FE_SCALE_NOT_AVAILABLE`` - The parameter is supported by the
      frontend, but it was not possible to collect it (could be a
      transitory or permanent condition)

   -  ``FE_SCALE_DECIBEL`` - parameter is a signed value, measured in
      1/1000 dB

   -  ``FE_SCALE_RELATIVE`` - parameter is a unsigned value, where 0
      means 0% and 65535 means 100%.

   -  ``FE_SCALE_COUNTER`` - parameter is a unsigned value that counts
      the occurrence of an event, like bit error, block error, or lapsed
      time.


.. _DTV-STAT-SIGNAL-STRENGTH:

DTV_STAT_SIGNAL_STRENGTH
========================

Indicates the signal strength level at the analog part of the tuner or
of the demod.

Possible scales for this metric are:

-  ``FE_SCALE_NOT_AVAILABLE`` - it failed to measure it, or the
   measurement was not complete yet.

-  ``FE_SCALE_DECIBEL`` - signal strength is in 0.001 dBm units, power
   measured in miliwatts. This value is generally negative.

-  ``FE_SCALE_RELATIVE`` - The frontend provides a 0% to 100%
   measurement for power (actually, 0 to 65535).


.. _DTV-STAT-CNR:

DTV_STAT_CNR
============

Indicates the Signal to Noise ratio for the main carrier.

Possible scales for this metric are:

-  ``FE_SCALE_NOT_AVAILABLE`` - it failed to measure it, or the
   measurement was not complete yet.

-  ``FE_SCALE_DECIBEL`` - Signal/Noise ratio is in 0.001 dB units.

-  ``FE_SCALE_RELATIVE`` - The frontend provides a 0% to 100%
   measurement for Signal/Noise (actually, 0 to 65535).


.. _DTV-STAT-PRE-ERROR-BIT-COUNT:

DTV_STAT_PRE_ERROR_BIT_COUNT
============================

Measures the number of bit errors before the forward error correction
(FEC) on the inner coding block (before Viterbi, LDPC or other inner
code).

This measure is taken during the same interval as
``DTV_STAT_PRE_TOTAL_BIT_COUNT``.

In order to get the BER (Bit Error Rate) measurement, it should be
divided by
:ref:`DTV_STAT_PRE_TOTAL_BIT_COUNT <DTV-STAT-PRE-TOTAL-BIT-COUNT>`.

This measurement is monotonically increased, as the frontend gets more
bit count measurements. The frontend may reset it when a
channel/transponder is tuned.

Possible scales for this metric are:

-  ``FE_SCALE_NOT_AVAILABLE`` - it failed to measure it, or the
   measurement was not complete yet.

-  ``FE_SCALE_COUNTER`` - Number of error bits counted before the inner
   coding.


.. _DTV-STAT-PRE-TOTAL-BIT-COUNT:

DTV_STAT_PRE_TOTAL_BIT_COUNT
============================

Measures the amount of bits received before the inner code block, during
the same period as
:ref:`DTV_STAT_PRE_ERROR_BIT_COUNT <DTV-STAT-PRE-ERROR-BIT-COUNT>`
measurement was taken.

It should be noted that this measurement can be smaller than the total
amount of bits on the transport stream, as the frontend may need to
manually restart the measurement, losing some data between each
measurement interval.

This measurement is monotonically increased, as the frontend gets more
bit count measurements. The frontend may reset it when a
channel/transponder is tuned.

Possible scales for this metric are:

-  ``FE_SCALE_NOT_AVAILABLE`` - it failed to measure it, or the
   measurement was not complete yet.

-  ``FE_SCALE_COUNTER`` - Number of bits counted while measuring
   :ref:`DTV_STAT_PRE_ERROR_BIT_COUNT <DTV-STAT-PRE-ERROR-BIT-COUNT>`.


.. _DTV-STAT-POST-ERROR-BIT-COUNT:

DTV_STAT_POST_ERROR_BIT_COUNT
=============================

Measures the number of bit errors after the forward error correction
(FEC) done by inner code block (after Viterbi, LDPC or other inner
code).

This measure is taken during the same interval as
``DTV_STAT_POST_TOTAL_BIT_COUNT``.

In order to get the BER (Bit Error Rate) measurement, it should be
divided by
:ref:`DTV_STAT_POST_TOTAL_BIT_COUNT <DTV-STAT-POST-TOTAL-BIT-COUNT>`.

This measurement is monotonically increased, as the frontend gets more
bit count measurements. The frontend may reset it when a
channel/transponder is tuned.

Possible scales for this metric are:

-  ``FE_SCALE_NOT_AVAILABLE`` - it failed to measure it, or the
   measurement was not complete yet.

-  ``FE_SCALE_COUNTER`` - Number of error bits counted after the inner
   coding.


.. _DTV-STAT-POST-TOTAL-BIT-COUNT:

DTV_STAT_POST_TOTAL_BIT_COUNT
=============================

Measures the amount of bits received after the inner coding, during the
same period as
:ref:`DTV_STAT_POST_ERROR_BIT_COUNT <DTV-STAT-POST-ERROR-BIT-COUNT>`
measurement was taken.

It should be noted that this measurement can be smaller than the total
amount of bits on the transport stream, as the frontend may need to
manually restart the measurement, losing some data between each
measurement interval.

This measurement is monotonically increased, as the frontend gets more
bit count measurements. The frontend may reset it when a
channel/transponder is tuned.

Possible scales for this metric are:

-  ``FE_SCALE_NOT_AVAILABLE`` - it failed to measure it, or the
   measurement was not complete yet.

-  ``FE_SCALE_COUNTER`` - Number of bits counted while measuring
   :ref:`DTV_STAT_POST_ERROR_BIT_COUNT <DTV-STAT-POST-ERROR-BIT-COUNT>`.


.. _DTV-STAT-ERROR-BLOCK-COUNT:

DTV_STAT_ERROR_BLOCK_COUNT
==========================

Measures the number of block errors after the outer forward error
correction coding (after Reed-Solomon or other outer code).

This measurement is monotonically increased, as the frontend gets more
bit count measurements. The frontend may reset it when a
channel/transponder is tuned.

Possible scales for this metric are:

-  ``FE_SCALE_NOT_AVAILABLE`` - it failed to measure it, or the
   measurement was not complete yet.

-  ``FE_SCALE_COUNTER`` - Number of error blocks counted after the outer
   coding.


.. _DTV-STAT-TOTAL-BLOCK-COUNT:

DTV-STAT_TOTAL_BLOCK_COUNT
==========================

Measures the total number of blocks received during the same period as
:ref:`DTV_STAT_ERROR_BLOCK_COUNT <DTV-STAT-ERROR-BLOCK-COUNT>`
measurement was taken.

It can be used to calculate the PER indicator, by dividing
:ref:`DTV_STAT_ERROR_BLOCK_COUNT <DTV-STAT-ERROR-BLOCK-COUNT>` by
:ref:`DTV-STAT-TOTAL-BLOCK-COUNT`.

Possible scales for this metric are:

-  ``FE_SCALE_NOT_AVAILABLE`` - it failed to measure it, or the
   measurement was not complete yet.

-  ``FE_SCALE_COUNTER`` - Number of blocks counted while measuring
   :ref:`DTV_STAT_ERROR_BLOCK_COUNT <DTV-STAT-ERROR-BLOCK-COUNT>`.
