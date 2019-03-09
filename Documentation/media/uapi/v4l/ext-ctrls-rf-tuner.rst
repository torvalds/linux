.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _rf-tuner-controls:

**************************
RF Tuner Control Reference
**************************

The RF Tuner (RF_TUNER) class includes controls for common features of
devices having RF tuner.

In this context, RF tuner is radio receiver circuit between antenna and
demodulator. It receives radio frequency (RF) from the antenna and
converts that received signal to lower intermediate frequency (IF) or
baseband frequency (BB). Tuners that could do baseband output are often
called Zero-IF tuners. Older tuners were typically simple PLL tuners
inside a metal box, while newer ones are highly integrated chips
without a metal box "silicon tuners". These controls are mostly
applicable for new feature rich silicon tuners, just because older
tuners does not have much adjustable features.

For more information about RF tuners see
`Tuner (radio) <http://en.wikipedia.org/wiki/Tuner_%28radio%29>`__
and `RF front end <http://en.wikipedia.org/wiki/RF_front_end>`__
from Wikipedia.


.. _rf-tuner-control-id:

RF_TUNER Control IDs
====================

``V4L2_CID_RF_TUNER_CLASS (class)``
    The RF_TUNER class descriptor. Calling
    :ref:`VIDIOC_QUERYCTRL` for this control will
    return a description of this control class.

``V4L2_CID_RF_TUNER_BANDWIDTH_AUTO (boolean)``
    Enables/disables tuner radio channel bandwidth configuration. In
    automatic mode bandwidth configuration is performed by the driver.

``V4L2_CID_RF_TUNER_BANDWIDTH (integer)``
    Filter(s) on tuner signal path are used to filter signal according
    to receiving party needs. Driver configures filters to fulfill
    desired bandwidth requirement. Used when
    V4L2_CID_RF_TUNER_BANDWIDTH_AUTO is not set. Unit is in Hz. The
    range and step are driver-specific.

``V4L2_CID_RF_TUNER_LNA_GAIN_AUTO (boolean)``
    Enables/disables LNA automatic gain control (AGC)

``V4L2_CID_RF_TUNER_MIXER_GAIN_AUTO (boolean)``
    Enables/disables mixer automatic gain control (AGC)

``V4L2_CID_RF_TUNER_IF_GAIN_AUTO (boolean)``
    Enables/disables IF automatic gain control (AGC)

``V4L2_CID_RF_TUNER_RF_GAIN (integer)``
    The RF amplifier is the very first amplifier on the receiver signal
    path, just right after the antenna input. The difference between the
    LNA gain and the RF gain in this document is that the LNA gain is
    integrated in the tuner chip while the RF gain is a separate chip.
    There may be both RF and LNA gain controls in the same device. The
    range and step are driver-specific.

``V4L2_CID_RF_TUNER_LNA_GAIN (integer)``
    LNA (low noise amplifier) gain is first gain stage on the RF tuner
    signal path. It is located very close to tuner antenna input. Used
    when ``V4L2_CID_RF_TUNER_LNA_GAIN_AUTO`` is not set. See
    ``V4L2_CID_RF_TUNER_RF_GAIN`` to understand how RF gain and LNA gain
    differs from the each others. The range and step are
    driver-specific.

``V4L2_CID_RF_TUNER_MIXER_GAIN (integer)``
    Mixer gain is second gain stage on the RF tuner signal path. It is
    located inside mixer block, where RF signal is down-converted by the
    mixer. Used when ``V4L2_CID_RF_TUNER_MIXER_GAIN_AUTO`` is not set.
    The range and step are driver-specific.

``V4L2_CID_RF_TUNER_IF_GAIN (integer)``
    IF gain is last gain stage on the RF tuner signal path. It is
    located on output of RF tuner. It controls signal level of
    intermediate frequency output or baseband output. Used when
    ``V4L2_CID_RF_TUNER_IF_GAIN_AUTO`` is not set. The range and step
    are driver-specific.

``V4L2_CID_RF_TUNER_PLL_LOCK (boolean)``
    Is synthesizer PLL locked? RF tuner is receiving given frequency
    when that control is set. This is a read-only control.
