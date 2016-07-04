.. -*- coding: utf-8; mode: rst -*-

.. _lirc_ioctl:

**************
LIRC ioctl fop
**************

The LIRC device's ioctl definition is bound by the ioctl function
definition of struct file_operations, leaving us with an unsigned int
for the ioctl command and an unsigned long for the arg. For the purposes
of ioctl portability across 32-bit and 64-bit, these values are capped
to their 32-bit sizes.

The following ioctls can be used to change specific hardware settings.
In general each driver should have a default set of settings. The driver
implementation is expected to re-apply the default settings when the
device is closed by user-space, so that every application opening the
device can rely on working with the default settings initially.

LIRC_GET_FEATURES
    Obviously, get the underlying hardware device's features. If a
    driver does not announce support of certain features, calling of the
    corresponding ioctls is undefined.

LIRC_GET_SEND_MODE
    Get supported transmit mode. Only LIRC_MODE_PULSE is supported by
    lircd.

LIRC_GET_REC_MODE
    Get supported receive modes. Only LIRC_MODE_MODE2 and
    LIRC_MODE_LIRCCODE are supported by lircd.

LIRC_GET_SEND_CARRIER
    Get carrier frequency (in Hz) currently used for transmit.

LIRC_GET_REC_CARRIER
    Get carrier frequency (in Hz) currently used for IR reception.

LIRC_{G,S}ET_{SEND,REC}_DUTY_CYCLE
    Get/set the duty cycle (from 0 to 100) of the carrier signal.
    Currently, no special meaning is defined for 0 or 100, but this
    could be used to switch off carrier generation in the future, so
    these values should be reserved.

LIRC_GET_REC_RESOLUTION
    Some receiver have maximum resolution which is defined by internal
    sample rate or data format limitations. E.g. it's common that
    signals can only be reported in 50 microsecond steps. This integer
    value is used by lircd to automatically adjust the aeps tolerance
    value in the lircd config file.

LIRC_GET_M{IN,AX}_TIMEOUT
    Some devices have internal timers that can be used to detect when
    there's no IR activity for a long time. This can help lircd in
    detecting that a IR signal is finished and can speed up the decoding
    process. Returns an integer value with the minimum/maximum timeout
    that can be set. Some devices have a fixed timeout, in that case
    both ioctls will return the same value even though the timeout
    cannot be changed.

LIRC_GET_M{IN,AX}_FILTER_{PULSE,SPACE}
    Some devices are able to filter out spikes in the incoming signal
    using given filter rules. These ioctls return the hardware
    capabilities that describe the bounds of the possible filters.
    Filter settings depend on the IR protocols that are expected. lircd
    derives the settings from all protocols definitions found in its
    config file.

LIRC_GET_LENGTH
    Retrieves the code length in bits (only for LIRC_MODE_LIRCCODE).
    Reads on the device must be done in blocks matching the bit count.
    The bit could should be rounded up so that it matches full bytes.

LIRC_SET_{SEND,REC}_MODE
    Set send/receive mode. Largely obsolete for send, as only
    LIRC_MODE_PULSE is supported.

LIRC_SET_{SEND,REC}_CARRIER
    Set send/receive carrier (in Hz).

LIRC_SET_TRANSMITTER_MASK
    This enables the given set of transmitters. The first transmitter is
    encoded by the least significant bit, etc. When an invalid bit mask
    is given, i.e. a bit is set, even though the device does not have so
    many transitters, then this ioctl returns the number of available
    transitters and does nothing otherwise.

LIRC_SET_REC_TIMEOUT
    Sets the integer value for IR inactivity timeout (cf.
    LIRC_GET_MIN_TIMEOUT and LIRC_GET_MAX_TIMEOUT). A value of 0
    (if supported by the hardware) disables all hardware timeouts and
    data should be reported as soon as possible. If the exact value
    cannot be set, then the next possible value _greater_ than the
    given value should be set.

LIRC_SET_REC_TIMEOUT_REPORTS
    Enable (1) or disable (0) timeout reports in LIRC_MODE_MODE2. By
    default, timeout reports should be turned off.

LIRC_SET_REC_FILTER_{,PULSE,SPACE}
    Pulses/spaces shorter than this are filtered out by hardware. If
    filters cannot be set independently for pulse/space, the
    corresponding ioctls must return an error and LIRC_SET_REC_FILTER
    shall be used instead.

LIRC_SET_MEASURE_CARRIER_MODE
    Enable (1)/disable (0) measure mode. If enabled, from the next key
    press on, the driver will send LIRC_MODE2_FREQUENCY packets. By
    default this should be turned off.

LIRC_SET_REC_{DUTY_CYCLE,CARRIER}_RANGE
    To set a range use
    LIRC_SET_REC_DUTY_CYCLE_RANGE/LIRC_SET_REC_CARRIER_RANGE
    with the lower bound first and later
    LIRC_SET_REC_DUTY_CYCLE/LIRC_SET_REC_CARRIER with the upper
    bound.

LIRC_NOTIFY_DECODE
    This ioctl is called by lircd whenever a successful decoding of an
    incoming IR signal could be done. This can be used by supporting
    hardware to give visual feedback to the user e.g. by flashing a LED.

LIRC_SETUP_{START,END}
    Setting of several driver parameters can be optimized by
    encapsulating the according ioctl calls with
    LIRC_SETUP_START/LIRC_SETUP_END. When a driver receives a
    LIRC_SETUP_START ioctl it can choose to not commit further setting
    changes to the hardware until a LIRC_SETUP_END is received. But
    this is open to the driver implementation and every driver must also
    handle parameter changes which are not encapsulated by
    LIRC_SETUP_START and LIRC_SETUP_END. Drivers can also choose to
    ignore these ioctls.

LIRC_SET_WIDEBAND_RECEIVER
    Some receivers are equipped with special wide band receiver which is
    intended to be used to learn output of existing remote. Calling that
    ioctl with (1) will enable it, and with (0) disable it. This might
    be useful of receivers that have otherwise narrow band receiver that
    prevents them to be used with some remotes. Wide band receiver might
    also be more precise On the other hand its disadvantage it usually
    reduced range of reception. Note: wide band receiver might be
    implictly enabled if you enable carrier reports. In that case it
    will be disabled as soon as you disable carrier reports. Trying to
    disable wide band receiver while carrier reports are active will do
    nothing.


.. _lirc_dev_errors:

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
