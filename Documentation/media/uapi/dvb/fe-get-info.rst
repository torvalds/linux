.. -*- coding: utf-8; mode: rst -*-

.. _FE_GET_INFO:

*****************
ioctl FE_GET_INFO
*****************

Name
====

FE_GET_INFO - Query DVB frontend capabilities and returns information about the - front-end. This call only requires read-only access to the device


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct dvb_frontend_info *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <frontend_f_open>`.

``request``
    FE_GET_INFO

``argp``
    pointer to struct struct
    :ref:`dvb_frontend_info <dvb-frontend-info>`


Description
===========

All DVB frontend devices support the ``FE_GET_INFO`` ioctl. It is used
to identify kernel devices compatible with this specification and to
obtain information about driver and hardware capabilities. The ioctl
takes a pointer to dvb_frontend_info which is filled by the driver.
When the driver is not compatible with this specification the ioctl
returns an error.

.. _dvb-frontend-info:

struct dvb_frontend_info
========================

.. flat-table:: struct dvb_frontend_info
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  char

       -  name[128]

       -  Name of the frontend

    -  .. row 2

       -  fe_type_t

       -  type

       -  **DEPRECATED**. DVBv3 type. Should not be used on modern programs,
	  as a frontend may have more than one type. So, the DVBv5 API
	  should be used instead to enumerate and select the frontend type.

    -  .. row 3

       -  uint32_t

       -  frequency_min

       -  Minimal frequency supported by the frontend

    -  .. row 4

       -  uint32_t

       -  frequency_max

       -  Maximal frequency supported by the frontend

    -  .. row 5

       -  uint32_t

       -  frequency_stepsize

       -  Frequency step - all frequencies are multiple of this value

    -  .. row 6

       -  uint32_t

       -  frequency_tolerance

       -  Tolerance of the frequency

    -  .. row 7

       -  uint32_t

       -  symbol_rate_min

       -  Minimal symbol rate (for Cable/Satellite systems), in bauds

    -  .. row 8

       -  uint32_t

       -  symbol_rate_max

       -  Maximal symbol rate (for Cable/Satellite systems), in bauds

    -  .. row 9

       -  uint32_t

       -  symbol_rate_tolerance

       -  Maximal symbol rate tolerance, in ppm

    -  .. row 10

       -  uint32_t

       -  notifier_delay

       -  **DEPRECATED**. Not used by any driver.

    -  .. row 11

       -  enum :ref:`fe_caps <fe-caps>`

       -  caps

       -  Capabilities supported by the frontend


.. note::

   The frequencies are specified in Hz for Terrestrial and Cable
   systems. They're specified in kHz for Satellite systems


.. _fe-caps-t:

frontend capabilities
=====================

Capabilities describe what a frontend can do. Some capabilities are
supported only on some specific frontend types.


.. _fe-caps:

.. flat-table:: enum fe_caps
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ID

       -  Description

    -  .. row 2

       -  .. _FE-IS-STUPID:

	  ``FE_IS_STUPID``

       -  There's something wrong at the frontend, and it can't report its
	  capabilities

    -  .. row 3

       -  .. _FE-CAN-INVERSION-AUTO:

	  ``FE_CAN_INVERSION_AUTO``

       -  The frontend is capable of auto-detecting inversion

    -  .. row 4

       -  .. _FE-CAN-FEC-1-2:

	  ``FE_CAN_FEC_1_2``

       -  The frontend supports FEC 1/2

    -  .. row 5

       -  .. _FE-CAN-FEC-2-3:

	  ``FE_CAN_FEC_2_3``

       -  The frontend supports FEC 2/3

    -  .. row 6

       -  .. _FE-CAN-FEC-3-4:

	  ``FE_CAN_FEC_3_4``

       -  The frontend supports FEC 3/4

    -  .. row 7

       -  .. _FE-CAN-FEC-4-5:

	  ``FE_CAN_FEC_4_5``

       -  The frontend supports FEC 4/5

    -  .. row 8

       -  .. _FE-CAN-FEC-5-6:

	  ``FE_CAN_FEC_5_6``

       -  The frontend supports FEC 5/6

    -  .. row 9

       -  .. _FE-CAN-FEC-6-7:

	  ``FE_CAN_FEC_6_7``

       -  The frontend supports FEC 6/7

    -  .. row 10

       -  .. _FE-CAN-FEC-7-8:

	  ``FE_CAN_FEC_7_8``

       -  The frontend supports FEC 7/8

    -  .. row 11

       -  .. _FE-CAN-FEC-8-9:

	  ``FE_CAN_FEC_8_9``

       -  The frontend supports FEC 8/9

    -  .. row 12

       -  .. _FE-CAN-FEC-AUTO:

	  ``FE_CAN_FEC_AUTO``

       -  The frontend can autodetect FEC.

    -  .. row 13

       -  .. _FE-CAN-QPSK:

	  ``FE_CAN_QPSK``

       -  The frontend supports QPSK modulation

    -  .. row 14

       -  .. _FE-CAN-QAM-16:

	  ``FE_CAN_QAM_16``

       -  The frontend supports 16-QAM modulation

    -  .. row 15

       -  .. _FE-CAN-QAM-32:

	  ``FE_CAN_QAM_32``

       -  The frontend supports 32-QAM modulation

    -  .. row 16

       -  .. _FE-CAN-QAM-64:

	  ``FE_CAN_QAM_64``

       -  The frontend supports 64-QAM modulation

    -  .. row 17

       -  .. _FE-CAN-QAM-128:

	  ``FE_CAN_QAM_128``

       -  The frontend supports 128-QAM modulation

    -  .. row 18

       -  .. _FE-CAN-QAM-256:

	  ``FE_CAN_QAM_256``

       -  The frontend supports 256-QAM modulation

    -  .. row 19

       -  .. _FE-CAN-QAM-AUTO:

	  ``FE_CAN_QAM_AUTO``

       -  The frontend can autodetect modulation

    -  .. row 20

       -  .. _FE-CAN-TRANSMISSION-MODE-AUTO:

	  ``FE_CAN_TRANSMISSION_MODE_AUTO``

       -  The frontend can autodetect the transmission mode

    -  .. row 21

       -  .. _FE-CAN-BANDWIDTH-AUTO:

	  ``FE_CAN_BANDWIDTH_AUTO``

       -  The frontend can autodetect the bandwidth

    -  .. row 22

       -  .. _FE-CAN-GUARD-INTERVAL-AUTO:

	  ``FE_CAN_GUARD_INTERVAL_AUTO``

       -  The frontend can autodetect the guard interval

    -  .. row 23

       -  .. _FE-CAN-HIERARCHY-AUTO:

	  ``FE_CAN_HIERARCHY_AUTO``

       -  The frontend can autodetect hierarch

    -  .. row 24

       -  .. _FE-CAN-8VSB:

	  ``FE_CAN_8VSB``

       -  The frontend supports 8-VSB modulation

    -  .. row 25

       -  .. _FE-CAN-16VSB:

	  ``FE_CAN_16VSB``

       -  The frontend supports 16-VSB modulation

    -  .. row 26

       -  .. _FE-HAS-EXTENDED-CAPS:

	  ``FE_HAS_EXTENDED_CAPS``

       -  Currently, unused

    -  .. row 27

       -  .. _FE-CAN-MULTISTREAM:

	  ``FE_CAN_MULTISTREAM``

       -  The frontend supports multistream filtering

    -  .. row 28

       -  .. _FE-CAN-TURBO-FEC:

	  ``FE_CAN_TURBO_FEC``

       -  The frontend supports turbo FEC modulation

    -  .. row 29

       -  .. _FE-CAN-2G-MODULATION:

	  ``FE_CAN_2G_MODULATION``

       -  The frontend supports "2nd generation modulation" (DVB-S2/T2)>

    -  .. row 30

       -  .. _FE-NEEDS-BENDING:

	  ``FE_NEEDS_BENDING``

       -  Not supported anymore, don't use it

    -  .. row 31

       -  .. _FE-CAN-RECOVER:

	  ``FE_CAN_RECOVER``

       -  The frontend can recover from a cable unplug automatically

    -  .. row 32

       -  .. _FE-CAN-MUTE-TS:

	  ``FE_CAN_MUTE_TS``

       -  The frontend can stop spurious TS data output


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
