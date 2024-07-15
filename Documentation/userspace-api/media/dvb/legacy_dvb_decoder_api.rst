.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later OR GPL-2.0

.. _legacy_dvb_decoder_api:

============================
Legacy DVB MPEG Decoder APIs
============================

.. _legacy_dvb_decoder_notes:

General Notes
=============

This API has originally been designed for DVB only and is therefore limited to
the :ref:`legacy_dvb_decoder_formats` used in such digital TV-broadcastsystems.

To circumvent this limitations the more versatile :ref:`V4L2 <v4l2spec>` API has
been designed. Which replaces this part of the DVB API.

Nevertheless there have been projects build around this API.
To ensure compatibility this API is kept as it is.

.. attention:: Do **not** use this API in new drivers!

    For audio and video use the :ref:`V4L2 <v4l2spec>` and ALSA APIs.

    Pipelines should be set up using the :ref:`Media Controller  API<media_controller>`.

Practically the decoders seem to be treated differently. The application typically
knows which decoder is in use or it is specially written for one decoder type.
Querying capabilities are rarely used because they are already known.


.. _legacy_dvb_decoder_formats:

Data Formats
============

The API has been designed for DVB and compatible broadcastsystems.
Because of that fact the only supported data formats are ISO/IEC 13818-1
compatible MPEG streams. The supported payloads may vary depending on the
used decoder.

Timestamps are always MPEG PTS as defined in ITU T-REC-H.222.0 /
ISO/IEC 13818-1, if not otherwise noted.

For storing recordings typically TS streams are used, in lesser extent PES.
Both variants are commonly accepted for playback, but it may be driver dependent.




Table of Contents
=================

.. toctree::
    :maxdepth: 2

    legacy_dvb_video
    legacy_dvb_audio
    legacy_dvb_osd
